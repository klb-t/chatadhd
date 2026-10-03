"""Graph adapter mechanics only; no live calls or model-quality measurement."""
from copy import deepcopy
import json
import tempfile
from pathlib import Path
import unittest

try:
    from . import graph_panel_live as panel
except ImportError:
    import graph_panel_live as panel


def sample():
    case = {'id': 'mechanism-case', 'language': 'pl', 'source_id': 'synthetic:mechanism',
            'turns': [{'id': 't1', 'speaker': 'tester', 'known_at': '2026-08-10T09:00:00Z',
                       'text': 'Żółty kabel powoduje zwarcie.'},
                      {'id': 't2', 'speaker': 'tester', 'known_at': '2026-08-10T09:01:00Z',
                       'text': 'Poprawka: żółty kabel nie powoduje zwarcia; powoduje migotanie.'}],
            'node_inventory': [{'id': 'A', 'text': 'żółty kabel', 'aliases': []},
                               {'id': 'B', 'text': 'zwarcie', 'aliases': []},
                               {'id': 'C', 'text': 'migotanie', 'aliases': []}],
            'judgment_queries': [{'id': 'q1', 'relation': 'causes', 'source': 'A', 'target': 'B',
                                  'attributed_to': 'tester', 'as_of': '2026-08-10T09:00:00Z', 'scope': 'explicit_source'},
                                 {'id': 'q2', 'relation': 'causes', 'source': 'A', 'target': 'B',
                                  'attributed_to': 'tester', 'as_of': '2026-08-10T09:01:00Z', 'scope': 'explicit_source'},
                                 {'id': 'q3', 'relation': 'causes', 'source': 'A', 'target': 'C',
                                  'attributed_to': 'tester', 'as_of': '2026-08-10T09:00:00Z', 'scope': 'explicit_source'}]}
    assertions = []
    for ident, turn, target, polarity in [('s1', 0, 'B', 'positive'), ('s2', 1, 'B', 'negative'), ('s3', 1, 'C', 'positive')]:
        t = case['turns'][turn]
        assertions.append({'id': ident, 'relation': 'causes', 'source': 'A', 'target': target,
                           'polarity': polarity, 'attributed_to': 'tester', 'known_at': t['known_at'],
                           'evidence': [{'turn_id': t['id'], 'quote': t['text']}]})
    response = {'source_assertions': assertions, 'status_events': [{'assertion_id': 's1',
        'status': 'superseded', 'superseded_by': 's3', 'known_at': case['turns'][1]['known_at'],
        'evidence': [{'turn_id': 't2', 'quote': case['turns'][1]['text']}]}]}
    gold = {'id': case['id'], 'family': 'mechanism', 'source_assertions': [dict(a,
                evidence=[panel.locate_evidence(e, case) for e in a['evidence']]) for a in assertions],
            'status_events': [dict(response['status_events'][0],
                evidence=[panel.locate_evidence(e, case) for e in response['status_events'][0]['evidence']])],
            'judgments': [{'query_id': 'q1', 'label': 'supported'}, {'query_id': 'q2', 'label': 'refuted'},
                          {'query_id': 'q3', 'label': 'unknown'}]}
    return case, response, gold


class GraphPanelMechanismTests(unittest.TestCase):
    def setUp(self):
        self.case, self.response, self.gold = sample()

    def score(self, response=None):
        compiled = panel.compile_extraction(response or self.response, self.case)
        return panel.score_extraction([self.case], [self.gold], [compiled])

    def test_extraction_payload_has_no_queries_gold_or_family(self):
        self.case['gold'] = {'secret': True}
        payload = panel.extraction_payload(self.case)
        self.assertEqual(set(payload), {'id', 'source_id', 'turns', 'node_inventory'})
        self.assertNotIn('judgment_queries', payload)
        payload['turns'][0]['text'] = 'mutation'
        self.assertNotEqual(payload, self.case)
        self.assertNotEqual(self.case['turns'][0]['text'], 'mutation')

    def test_physical_prefix_excludes_future_and_other_queries(self):
        payload = panel.query_payload(self.case, self.case['judgment_queries'][0])
        self.assertEqual([t['id'] for t in payload['turns']], ['t1'])
        self.assertEqual(payload['query']['id'], 'q1')
        self.assertNotIn('judgment_queries', payload)
        self.assertNotIn('Poprawka', json.dumps(payload, ensure_ascii=False))

    def test_formal_scope_is_not_silently_judged_as_explicit_source(self):
        q = deepcopy(self.case['judgment_queries'][0]); q['scope'] = 'formal_implication'
        with self.assertRaises(ValueError): panel.query_payload(self.case, q)

    def test_quote_compiler_exact_unicode_and_utf8(self):
        e = panel.locate_evidence({'turn_id': 't1', 'quote': 'kabel powoduje zwarcie'}, self.case)
        text = self.case['turns'][0]['text']
        self.assertEqual(text[e['char_start']:e['char_end']], e['quote'])
        self.assertEqual(text.encode()[e['byte_start']:e['byte_end']].decode(), e['quote'])
        self.assertGreater(e['byte_start'], e['char_start'])

    def test_ambiguous_overlapping_quote_is_rejected(self):
        self.case['turns'][0]['text'] = 'aaaa'
        with self.assertRaises(ValueError):
            panel.locate_evidence({'turn_id': 't1', 'quote': 'aa'}, self.case)

    def test_nonexistent_quote_and_turn_are_rejected(self):
        for evidence in [{'turn_id': 'missing', 'quote': 'Żółty'}, {'turn_id': 't1', 'quote': 'invented'}]:
            with self.assertRaises(ValueError): panel.locate_evidence(evidence, self.case)

    def test_oracle_shape_is_only_a_scorer_mechanism_check(self):
        report = self.score()
        self.assertEqual((report['strict_edges']['tp'], report['strict_edges']['fp'], report['strict_edges']['fn']), (3, 0, 0))
        self.assertEqual(report['strict_status_events']['tp'], 1)
        self.assertIsNone(report['content_truth_accuracy'])

    def test_wrong_direction_and_attribution_count_fp_fn(self):
        for field, value in [('source', 'C'), ('attributed_to', 'reporter')]:
            response = deepcopy(self.response); response['source_assertions'][0][field] = value
            report = self.score(response)
            self.assertEqual((report['strict_edges']['tp'], report['strict_edges']['fp'], report['strict_edges']['fn']), (2, 1, 1))

    def test_duplicate_local_id_adds_fp_without_erasing_correct_edges(self):
        response = deepcopy(self.response); response['source_assertions'].append(deepcopy(response['source_assertions'][0]))
        report = self.score(response)
        self.assertEqual((report['strict_edges']['tp'], report['strict_edges']['fp'], report['strict_edges']['fn']), (3, 1, 0))

    def test_known_at_mismatch_and_truth_promotion_are_invalid(self):
        for field, value in [('known_at', '2026-08-10T09:01:00Z'), ('content_truth', 'fact')]:
            response = deepcopy(self.response); response['source_assertions'][0][field] = value
            compiled = panel.compile_extraction(response, self.case)
            self.assertEqual(compiled['invalid_assertions'], 1)

    def test_compiled_quote_offsets_cannot_be_tampered_before_scoring(self):
        for field, value in [('byte_start', 100), ('char_start', False), ('quote', 'invented')]:
            compiled = panel.compile_extraction(self.response, self.case)
            compiled['source_assertions'][0]['evidence'][0][field] = value
            report = panel.score_extraction([self.case], [self.gold], [compiled])
            self.assertEqual((report['strict_edges']['tp'], report['strict_edges']['fp'], report['strict_edges']['fn']), (2, 1, 1))

    def test_exact_subspan_is_source_bound_with_explicit_review_count(self):
        response = deepcopy(self.response)
        response['source_assertions'][0]['evidence'][0]['quote'] = 'kabel powoduje zwarcie'
        report = self.score(response)
        self.assertEqual(report['strict_edges']['tp'], 3)
        self.assertEqual(report['cases'][0]['narrow_quote_matches_needing_clause_review'], 1)

    def test_missing_output_keeps_every_gold_denominator(self):
        report = panel.score_extraction([self.case], [self.gold], [])
        self.assertEqual(report['strict_edges']['fn'], 3)
        self.assertEqual(report['strict_status_events']['fn'], 1)
        self.assertIsNone(report['strict_edges']['precision'])
        self.assertEqual(report['missing_or_unavailable_cases'], 1)

    def test_duplicate_unknown_case_output_rejected(self):
        compiled = panel.compile_extraction(self.response, self.case)
        with self.assertRaises(ValueError): panel.score_extraction([self.case], [self.gold], [compiled, compiled])
        compiled['case_id'] = 'not-known'
        with self.assertRaises(ValueError): panel.score_extraction([self.case], [self.gold], [compiled])

    def test_noul_conflict_is_unavailable_and_no_positive_is_unknown(self):
        self.assertEqual(panel.jev_label(.9, .9), 'conflicting')
        self.assertEqual(panel.jev_label(.5, .5), 'unknown')
        self.assertEqual(panel.jev_label(.8, .1), 'supported')
        self.assertEqual(panel.jev_label(.1, .8), 'refuted')
        for value in [True, float('nan'), -1, 2]:
            with self.assertRaises(ValueError): panel.jev_label(value, .1)

    def test_jev_shape_must_have_exactly_two_valid_probabilities(self):
        self.assertEqual(panel.compile_jev_judgment({'probabilities': {'q01': .8, 'q02': .1}}, 'q1')['label'], 'supported')
        for p in [{'q01': .8}, {'q01': .8, 'q02': .1, 'q03': .4}, {'q01': True, 'q02': .1}]:
            with self.assertRaises(ValueError): panel.compile_jev_judgment({'probabilities': p}, 'q1')

    def test_gpt_exact_query_identity_and_ternary_only(self):
        self.assertEqual(panel.compile_gpt_judgment('{' + '"query_id":"q1","label":"unknown"}', 'q1')['label'], 'unknown')
        for v in [{'query_id': 'q2', 'label': 'unknown'}, {'query_id': 'q1', 'label': 'false'}, {'query_id': 'q1', 'label': 'supported', 'confidence': 1}]:
            with self.assertRaises(ValueError): panel.compile_gpt_judgment(v, 'q1')

    def test_confusion_keeps_unavailable_in_recall_denominators(self):
        report = panel.score_judgments([self.gold], [{'query_id': 'q1', 'label': 'conflicting', 'state': 'completed'},
                         {'query_id': 'q3', 'label': 'unknown', 'state': 'completed'}])
        self.assertEqual(report['query_count'], 3)
        self.assertEqual(report['unavailable'], 2)
        self.assertEqual(report['confusion']['supported']['unavailable'], 1)
        self.assertEqual(report['confusion']['refuted']['unavailable'], 1)
        self.assertEqual(report['per_class']['supported']['fn'], 1)
        self.assertEqual(report['per_class']['unknown']['tp'], 1)

    def test_prepare_shapes_and_no_retry_or_network_surface(self):
        caps = {'prompt': '.4', 'completion': '1.6'}
        self.assertEqual(len(panel.extraction_requests([self.case], caps)), 1)
        self.assertEqual(len(panel.gpt_judgment_requests([self.case], caps)), 3)
        inputs = panel.jev_judgment_inputs([self.case])
        self.assertEqual(len(inputs), 3)
        self.assertEqual(set(inputs[0]['questions']), {'q01', 'q02'})
        self.assertEqual({q['type'] for q in inputs[0]['questions'].values()}, {'noul'})
        self.assertNotIn('Poprawka', inputs[0]['state']['text'])
        self.assertFalse(hasattr(panel, 'transport'))

    def test_first_output_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / 'first.json'
            panel.write_new(p, {'first': 1})
            with self.assertRaises(FileExistsError): panel.write_new(p, {'replacement': 2})


if __name__ == '__main__':
    unittest.main()
