"""Scoring mechanics, not model accuracy; synthetic mutations and dev labels only."""
from copy import deepcopy
import json
from pathlib import Path
import unittest

try:
    from .conversation_pilot_score import score_unit, score_experiment, fixture_units, strict_json
except ImportError:
    from conversation_pilot_score import score_unit, score_experiment, fixture_units, strict_json


def setup_case():
    text = 'Żółty most'
    full = {'observation': 'o_current', 'byte_start': 0, 'byte_len': len(text.encode()), 'quote': text}
    support = {'observation': 'o_memory', 'byte_start': 0, 'byte_len': 4, 'quote': 'memo'}
    claim = {'id': 'k_a', 'subject': 'e_a', 'object': '', 'observed_at': '2026-01-01T00:00:00Z',
             'assessment': {'basis': {'support': [support, deepcopy(support)]}}}
    second = deepcopy(claim); second['id'] = 'k_b'
    prefix = {'case_id': 'case', 'target_message_id': 'm_current', 'language': 'pl',
              'topics': [{'id': 't_a'}, {'id': 't_b'}],
              'messages': [{'id': 'm_current', 'observation': 'o_current', 'ordinal': 0, 'observed_at': '2026-01-02T00:00:00Z'}],
              'source_packet': {'observations': [{'id': 'o_current', 'text': text, 'source_group': 'g_current', 'observed_at': '2026-01-02T00:00:00Z'},
                                                  {'id': 'o_memory', 'text': 'memo', 'source_group': 'g_memory', 'observed_at': '2026-01-01T00:00:00Z'}],
                                'entities': [{'id': 'e_a', 'observed_at': '2026-01-01T00:00:00Z'}], 'claims': [claim, second]}}
    answer = {'case_id': 'case', 'message_id': 'm_current', 'memberships': [{'topic_id': 't_a', 'span': deepcopy(full)}, {'topic_id': 't_b', 'span': deepcopy(full)}],
              'boundaries': [{'topic_id': 't_a', 'type': 'onset', 'span': deepcopy(full)}],
              'relations': [{'type': 'correction', 'target_claim_id': 'k_a', 'span': deepcopy(full)}],
              'selected_claim_ids': ['k_a', 'k_b'], 'reference_decisions': [],
              'history_decisions': [{'claim_id': 'k_a', 'status': 'known_prior'}]}
    return prefix, answer


def envelope(answer, status='completed'):
    return {'case_id': answer['case_id'], 'message_id': answer['message_id'],
            'transport_status': status, 'raw_response': json.dumps(answer, ensure_ascii=False)}


class ConversationScorerTests(unittest.TestCase):
    def test_overlap_and_deduplicated_sources(self):
        prefix, answer = setup_case()
        row = score_unit(prefix, answer, envelope(answer))
        self.assertEqual(row['metrics']['topic_message'], {'tp': 2, 'fp': 0, 'fn': 0})
        self.assertEqual(row['metrics']['old_claim_selection']['tp'], 2)
        self.assertEqual(row['metrics']['old_source_selection']['tp'], 1)
        self.assertEqual(row['metrics']['old_source_groups']['tp'], 1)
        self.assertEqual(row['history']['correct'], 1)

    def test_overlapping_spans_are_union_not_repeated_evidence(self):
        prefix, answer = setup_case(); response = deepcopy(answer)
        full = response['memberships'][0]['span']
        first = {'observation': 'o_current', 'byte_start': 0, 'byte_len': len('Żółty'.encode()), 'quote': 'Żółty'}
        response['memberships'] += [{'topic_id': 't_a', 'span': first}, {'topic_id': 't_a', 'span': deepcopy(full)}]
        row = score_unit(prefix, answer, envelope(response))
        self.assertEqual(row['metrics']['topic_span']['tp'], 2 * len('Żółty most'.encode()))
        self.assertEqual(row['metrics']['topic_span']['fp'], 0)

    def test_utf8_character_offset_is_invalid(self):
        prefix, answer = setup_case(); response = deepcopy(answer)
        response['memberships'][0]['span'].update(byte_start=1, byte_len=1, quote='Ż')
        row = score_unit(prefix, answer, envelope(response))
        self.assertEqual(row['status'], 'invalid_output')
        self.assertEqual(row['diagnostics']['spans_invalid'], 1)
        self.assertEqual(row['diagnostics']['fabricated_quotes'], 1)

    def test_future_or_foreign_citation_is_rejected(self):
        prefix, answer = setup_case(); response = deepcopy(answer)
        response['memberships'][0]['span']['observation'] = 'o_future'
        response['selected_claim_ids'].append('k_hidden_future')
        row = score_unit(prefix, answer, envelope(response))
        self.assertEqual(row['status'], 'invalid_output')
        self.assertEqual(row['diagnostics']['out_of_prefix_citations'], 2)

    def test_prefix_memory_is_not_target_span(self):
        prefix, answer = setup_case(); response = deepcopy(answer)
        response['relations'][0]['span'] = {'observation': 'o_memory', 'byte_start': 0, 'byte_len': 4, 'quote': 'memo'}
        row = score_unit(prefix, answer, envelope(response))
        self.assertEqual(row['diagnostics']['non_target_citations'], 1)
        self.assertEqual(row['status'], 'invalid_output')

    def test_wrong_relation_type_and_target_are_both_scored(self):
        prefix, answer = setup_case(); response = deepcopy(answer)
        response['relations'][0]['type'] = 'contradiction'
        row = score_unit(prefix, answer, envelope(response))
        self.assertEqual(row['metrics']['relations'], {'tp': 0, 'fp': 1, 'fn': 1})
        self.assertEqual(row['relation_confusion'], {'correction->contradiction': 1})
        response['relations'][0]['target_claim_id'] = 'k_b'
        row = score_unit(prefix, answer, envelope(response))
        self.assertEqual(row['relation_confusion'], {'correction->missing': 1})

    def test_reversed_edge_field_and_identity_merge_forbidden(self):
        prefix, answer = setup_case(); response = deepcopy(answer)
        response['relations'][0]['source_claim_id'] = response['relations'][0].pop('target_claim_id')
        self.assertEqual(score_unit(prefix, answer, envelope(response))['status'], 'invalid_output')
        response = deepcopy(answer); response['relations'][0]['type'] = 'identity_merge'
        row = score_unit(prefix, answer, envelope(response))
        self.assertEqual(row['diagnostics']['identity_merges'], 1)

    def test_unknown_endpoint_does_not_establish_prior_availability(self):
        prefix, answer = setup_case()
        prefix['source_packet']['entities'][0]['observed_at'] = None
        gold = deepcopy(answer); gold['history_decisions'][0]['status'] = 'unknown'
        row = score_unit(prefix, gold, envelope(answer))
        self.assertEqual(row['status'], 'completed')
        self.assertEqual(row['history']['correct'], 0)
        self.assertEqual(row['diagnostics']['asserted_unknown_chronology'], 1)
        self.assertEqual(row['metrics']['unknown_time_claim_selection']['tp'], 2)

    def test_equal_support_timestamp_is_not_known_prior(self):
        prefix, answer = setup_case()
        prefix['source_packet']['observations'][1]['observed_at'] = prefix['messages'][0]['observed_at']
        gold = deepcopy(answer); gold['history_decisions'][0]['status'] = 'unknown'
        row = score_unit(prefix, gold, envelope(answer))
        self.assertEqual(row['diagnostics']['asserted_unknown_chronology'], 1)

    def test_failures_remain_in_all_query_denominator(self):
        prefix, answer = setup_case()
        units = [{'prefix': prefix, 'gold': answer}]
        result = score_experiment(units, [])
        self.assertEqual(result['all_queries']['queries'], 1)
        self.assertEqual(result['all_queries']['topic_set_accuracy'], 0)
        self.assertEqual(result['all_queries']['metrics']['topic_message']['fn'], 2)
        self.assertIsNone(result['all_queries']['metrics']['topic_message']['precision'])
        self.assertEqual(result['successful_output_conditional']['queries'], 0)
        for status in ('truncated', 'refused', 'error'):
            row = score_unit(prefix, answer, envelope(answer, status))
            self.assertFalse(row['topic_set_exact'])
            self.assertEqual(row['metrics']['topic_message']['fn'], 2)

    def test_empty_opportunities_are_null_but_exact_set_counts(self):
        prefix, answer = setup_case()
        for key in ('memberships', 'boundaries', 'relations', 'selected_claim_ids', 'reference_decisions', 'history_decisions'):
            answer[key] = []
        result = score_experiment([{'prefix': prefix, 'gold': answer}], [envelope(answer)])
        overall = result['all_queries']
        self.assertEqual(overall['topic_set_accuracy'], 1)
        self.assertIsNone(overall['metrics']['topic_message']['f1'])
        self.assertIsNone(overall['history']['accuracy'])
        missing = score_experiment([{'prefix': prefix, 'gold': answer}], [])
        self.assertEqual(missing['all_queries']['topic_set_accuracy'], 0)

    def test_ambiguous_reference_requires_same_span_alternatives_and_no_selection(self):
        prefix, answer = setup_case(); full = deepcopy(answer['memberships'][0]['span'])
        answer['memberships'] = []; answer['boundaries'] = []
        answer['reference_decisions'] = [{'span': full, 'status': 'ambiguous', 'alternative_topic_ids': ['t_a', 't_b'], 'selected_topic_id': None}]
        self.assertEqual(score_unit(prefix, answer, envelope(answer))['metrics']['reference_abstention']['tp'], 1)
        response = deepcopy(answer)
        response['reference_decisions'][0].update(status='resolved', selected_topic_id='t_b')
        row = score_unit(prefix, answer, envelope(response))
        self.assertEqual(row['diagnostics']['unsupported_reference_resolution'], 1)
        self.assertEqual(row['metrics']['reference_abstention']['fn'], 1)
        response = deepcopy(answer); response['reference_decisions'][0]['alternative_topic_ids'] = ['t_b']
        self.assertEqual(score_unit(prefix, answer, envelope(response))['metrics']['reference_abstention']['tp'], 0)

    def test_invalid_output_still_reports_located_unsupported_resolution(self):
        prefix, answer = setup_case(); full = deepcopy(answer['memberships'][0]['span'])
        answer['memberships'] = []; answer['boundaries'] = []
        answer['reference_decisions'] = [{'span': full, 'status': 'ambiguous', 'alternative_topic_ids': ['t_a', 't_b'], 'selected_topic_id': None}]
        response = deepcopy(answer)
        response['reference_decisions'][0]['selected_topic_id'] = 't_b'
        row = score_unit(prefix, answer, envelope(response))
        self.assertEqual(row['status'], 'invalid_output')
        self.assertEqual(row['diagnostics']['unsupported_reference_resolution'], 1)
        self.assertEqual(row['metrics']['reference_abstention'], {'tp': 0, 'fp': 0, 'fn': 1})
        # A different malformed field also cannot conceal a valid located guess.
        response['reference_decisions'][0]['status'] = 'resolved'
        response['history_decisions'] = 'broken'
        row = score_unit(prefix, answer, envelope(response))
        self.assertEqual(row['status'], 'invalid_output')
        self.assertEqual(row['diagnostics']['unsupported_reference_resolution'], 1)
        # A fabricated quote does not establish a located resolution opportunity.
        response['reference_decisions'][0]['span']['quote'] = 'fabricated'
        row = score_unit(prefix, answer, envelope(response))
        self.assertEqual(row['diagnostics']['unsupported_reference_resolution'], 0)

    def test_malformed_duplicate_keys_and_selective_reruns_rejected(self):
        prefix, answer = setup_case(); env = envelope(answer)
        env['raw_response'] = '{"case_id":"case","case_id":"case"}'
        self.assertEqual(score_unit(prefix, answer, env)['status'], 'invalid_output')
        with self.assertRaises(ValueError):
            strict_json('{"x":NaN}')
        with self.assertRaises(ValueError):
            score_experiment([{'prefix': prefix, 'gold': answer}], [envelope(answer), envelope(answer)])

    def test_development_reference_answers_exercise_mechanics_only(self):
        directory = Path(__file__).resolve().parents[2] / 'tests/fixtures/eval/conversation_context_pilot_v1'
        units = list(fixture_units(directory, 'development'))
        responses = []
        for unit in units:
            label = deepcopy(unit['gold']); label.pop('observation')
            label['case_id'] = unit['prefix']['case_id']
            responses.append(envelope(label))
        report = score_experiment(units, responses)
        self.assertEqual(report['all_queries']['queries'], 48)
        self.assertEqual(report['all_queries']['successful_outputs'], 48)
        self.assertEqual(report['all_queries']['topic_set_accuracy'], 1)
        self.assertEqual(report['all_queries']['claim_set_accuracy'], 1)
        self.assertEqual(report['all_queries']['history']['accuracy'], 1)
        self.assertEqual(report['all_queries']['onset_ordinal']['mae_first_onset'], 0)


if __name__ == '__main__':
    unittest.main()
