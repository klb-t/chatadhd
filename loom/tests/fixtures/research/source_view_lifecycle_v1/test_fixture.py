"""Offline fixture integrity and causal-view mechanism checks, not model quality."""
from __future__ import annotations

from collections import Counter
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('lifecycle_author', HERE / 'author_fixture.py')
authoring = importlib.util.module_from_spec(spec)
spec.loader.exec_module(authoring)
adapter = authoring.adapter


class FixtureMechanisms(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.inputs = json.loads((HERE / 'inputs_dev.json').read_text())
        cls.gold = json.loads((HERE / 'gold_dev.json').read_text())
        cls.policy = json.loads((HERE / 'policy.json').read_text())
        cls.by_id = {c['id']: c for c in cls.inputs['cases']}
        cls.gold_by_id = {c['id']: c for c in cls.gold['cases']}

    def families(self, name):
        return [g for g in self.gold['cases'] if g['family'] == name]

    def test_exact_inventory_and_class_denominators(self):
        self.assertEqual(self.inputs['schema'], 'loom.research.graph_methods_panel.inputs/1')
        self.assertEqual(self.inputs['split'], 'dev')
        self.assertEqual(len(self.by_id), 12)
        queries = [q for c in self.inputs['cases'] for q in c['judgment_queries']]
        self.assertEqual(len(queries), 48)
        self.assertEqual(len({q['id'] for q in queries}), 48)
        self.assertEqual(Counter(c['language'] for c in self.inputs['cases']), {'pl': 6, 'en': 6})
        self.assertEqual(Counter(q['label'] for c in self.gold['cases'] for q in c['judgments']),
                         {'supported': 18, 'refuted': 14, 'unknown': 16})
        self.assertEqual(sum(len(g['source_assertions']) for g in self.gold['cases']), 24)
        self.assertEqual(sum(len(g['status_events']) for g in self.gold['cases']), 12)
        for family in {g['family'] for g in self.gold['cases']}:
            self.assertEqual({g['language'] for g in self.families(family)}, {'pl', 'en'})

    def test_input_contains_no_evaluator_gold_family_or_trace(self):
        allowed = {'id', 'language', 'source_id', 'turns', 'node_inventory', 'judgment_queries'}
        for case in self.inputs['cases']:
            self.assertEqual(set(case), allowed)
            self.assertEqual(len(case['judgment_queries']), 4)
            self.assertEqual({q['id'] for q in case['judgment_queries']},
                             {q['query_id'] for q in self.gold_by_id[case['id']]['judgments']})
            self.assertTrue(all(q['scope'] == 'explicit_source' for q in case['judgment_queries']))
            for query in case['judgment_queries']:
                payload = adapter.query_payload(case, query)
                self.assertEqual(set(payload), {'case_id', 'source_id', 'turns', 'node_inventory', 'query'})
                serialized = json.dumps(payload)
                for forbidden in ('source_view', 'historical_assertion_ids', 'active_assertion_ids', 'family', 'label'):
                    self.assertNotIn('"' + forbidden + '"', serialized)

    def test_all_quotes_have_exact_unique_source_and_utf8_coordinates(self):
        saw_multibyte = False
        for gold in self.gold['cases']:
            case = self.by_id[gold['id']]
            turns = {t['id']: t for t in case['turns']}
            for record in gold['source_assertions'] + gold['status_events']:
                self.assertEqual(record['content_truth'], 'unverified')
                self.assertTrue(record['evidence'])
                for ev in record['evidence']:
                    turn = turns[ev['turn_id']]
                    self.assertEqual(ev['source_id'], case['source_id'])
                    self.assertEqual(ev['coordinate_space'], 'turn.text')
                    self.assertEqual(turn['text'][ev['char_start']:ev['char_end']], ev['quote'])
                    raw = turn['text'].encode('utf-8')
                    self.assertEqual(raw[ev['byte_start']:ev['byte_end']].decode('utf-8'), ev['quote'])
                    self.assertEqual(turn['text'].count(ev['quote']), 1)
                    self.assertEqual(record['known_at'], turn['known_at'])
                    saw_multibyte |= ev['byte_end'] != ev['char_end']
        self.assertTrue(saw_multibyte)

    def test_coordinate_adapter_nonzero_multibyte_offset_and_ambiguous_quote_rejection(self):
        case = deepcopy(self.inputs['cases'][0])
        turn = case['turns'][0]
        quote = 'brama zostaje odblokowana'
        ev = adapter.locate_evidence({'turn_id': turn['id'], 'quote': quote}, case)
        self.assertGreater(ev['byte_start'], ev['char_start'])
        self.assertEqual(turn['text'].encode()[ev['byte_start']:ev['byte_end']].decode(), quote)
        turn['text'] += ' ' + quote
        with self.assertRaises(ValueError):
            adapter.locate_evidence({'turn_id': turn['id'], 'quote': quote}, case)

    def test_every_request_is_a_physical_causal_prefix(self):
        saw_strict_prefix = False
        for case in self.inputs['cases']:
            for query in case['judgment_queries']:
                payload = adapter.query_payload(case, query)
                expected = [t for t in case['turns'] if adapter._time(t['known_at']) <= adapter._time(query['as_of'])]
                self.assertEqual(payload['turns'], expected)
                self.assertTrue(all(adapter._time(t['known_at']) <= adapter._time(query['as_of']) for t in payload['turns']))
                saw_strict_prefix |= len(expected) < len(case['turns'])
        self.assertTrue(saw_strict_prefix)

    def test_future_content_mutation_cannot_change_earlier_request(self):
        for case in self.inputs['cases']:
            query = case['judgment_queries'][0]
            before = adapter.query_payload(case, query)
            mutated = deepcopy(case)
            for turn in mutated['turns']:
                if adapter._time(turn['known_at']) > adapter._time(query['as_of']):
                    turn['text'] = 'DO NOT SEND FUTURE TEXT OR GOLD'
                    turn['speaker'] = 'future_mutation'
            self.assertEqual(adapter.query_payload(mutated, query), before)
            before['turns'][0]['text'] = 'mutating payload must not mutate source'
            self.assertNotEqual(case['turns'][0]['text'], before['turns'][0]['text'])

    def test_gold_views_keep_only_as_of_matching_attribution_and_direction(self):
        for gold in self.gold['cases']:
            case = self.by_id[gold['id']]
            queries = {q['id']: q for q in case['judgment_queries']}
            assertions = {a['id']: a for a in gold['source_assertions']}
            events = {e['id']: e for e in gold['status_events']}
            for judgment in gold['judgments']:
                query = queries[judgment['query_id']]
                view = judgment['source_view']
                self.assertEqual(view['policy_id'], self.policy['id'])
                self.assertEqual(view['as_of'], query['as_of'])
                self.assertEqual(view['content_truth'], 'unverified')
                self.assertEqual(judgment['label'], self.policy['state_labels'][view['stance']])
                for ident in view['historical_assertion_ids'] + view['active_assertion_ids']:
                    assertion = assertions[ident]
                    self.assertLessEqual(adapter._time(assertion['known_at']), adapter._time(query['as_of']))
                    for key in ('relation', 'source', 'target', 'attributed_to'):
                        self.assertEqual(assertion[key], query[key])
                for ident in view['historical_status_event_ids'] + view['active_status_event_ids']:
                    event = events[ident]
                    self.assertLessEqual(adapter._time(event['known_at']), adapter._time(query['as_of']))
                    self.assertEqual(event['attributed_to'], query['attributed_to'])
                    assertion = assertions[event['assertion_id']]
                    for key in ('relation', 'source', 'target'):
                        self.assertEqual(assertion[key], query[key])
                self.assertTrue(set(view['active_assertion_ids']) <= set(view['historical_assertion_ids']))
                self.assertTrue(set(view['active_status_event_ids']) <= set(view['historical_status_event_ids']))

    def test_reaffirmation_retains_denial_as_history_without_active_refutation(self):
        for gold in self.families('reaffirmation_after_denial'):
            self.assertEqual([j['label'] for j in gold['judgments']], ['supported', 'refuted', 'supported', 'unknown'])
            assertions, events = gold['source_assertions'], gold['status_events']
            self.assertEqual([a['polarity'] for a in assertions], ['positive', 'negative', 'positive'])
            final = gold['judgments'][2]['source_view']
            self.assertIn(assertions[1]['id'], final['historical_assertion_ids'])
            self.assertEqual(final['active_assertion_ids'], [assertions[2]['id']])
            self.assertEqual(final['active_status_event_ids'], [])
            self.assertEqual(events[1]['assertion_id'], assertions[1]['id'])
            self.assertEqual(events[1]['superseded_by'], assertions[2]['id'])

    def test_positive_withdrawal_is_null_replacement_and_not_a_negative_assertion(self):
        for gold in self.families('withdrawal_without_replacement'):
            self.assertEqual([j['label'] for j in gold['judgments']], ['supported', 'refuted', 'refuted', 'unknown'])
            self.assertEqual([a['polarity'] for a in gold['source_assertions']], ['positive'])
            event = gold['status_events'][0]
            self.assertEqual(event['status'], 'withdrawn')
            self.assertIsNone(event['superseded_by'])
            self.assertEqual(gold['judgments'][1]['source_view']['active_assertion_ids'], [])
            self.assertEqual(gold['judgments'][2]['source_view']['active_status_event_ids'], [event['id']])

    def test_quoted_withdrawal_is_not_reporter_commitment(self):
        for gold in self.families('quoted_withdrawal_attribution'):
            case = self.by_id[gold['id']]
            self.assertEqual(case['turns'][1]['speaker'], 'Owen')
            event = gold['status_events'][0]
            self.assertEqual(event['attributed_to'], 'Mira')
            self.assertEqual(event['evidence'][0]['turn_id'], case['turns'][1]['id'])
            self.assertEqual([j['label'] for j in gold['judgments']], ['supported', 'refuted', 'unknown', 'unknown'])
            for reporter_view in gold['judgments'][2:]:
                self.assertEqual(reporter_view['source_view']['historical_assertion_ids'], [])
                self.assertEqual(reporter_view['source_view']['historical_status_event_ids'], [])

    def test_repeated_positives_are_three_observations_without_fabricated_supersession(self):
        for gold in self.families('repeated_positive_history'):
            self.assertEqual(len(gold['source_assertions']), 3)
            self.assertEqual(gold['status_events'], [])
            self.assertEqual(len({a['known_at'] for a in gold['source_assertions']}), 3)
            self.assertEqual(len({a['evidence'][0]['turn_id'] for a in gold['source_assertions']}), 3)
            self.assertEqual([j['label'] for j in gold['judgments']], ['supported', 'supported', 'supported', 'unknown'])
            self.assertEqual(len(gold['judgments'][2]['source_view']['historical_assertion_ids']), 3)

    def test_other_speaker_positive_and_irrelevant_speech_do_not_erase_denial(self):
        for gold in self.families('other_speaker_reaffirmation'):
            self.assertEqual([j['label'] for j in gold['judgments']], ['supported', 'refuted', 'supported', 'refuted'])
            mira_negative = gold['source_assertions'][1]
            owen_positive = gold['source_assertions'][2]
            final = gold['judgments'][3]['source_view']
            self.assertEqual(final['active_assertion_ids'], [mira_negative['id']])
            self.assertNotIn(owen_positive['id'], final['historical_assertion_ids'])

    def test_withdrawn_denial_has_no_inferred_positive_and_silence_preserves_unknown(self):
        for gold in self.families('withdrawn_denial_and_silence'):
            self.assertEqual([j['label'] for j in gold['judgments']], ['refuted', 'unknown', 'unknown', 'unknown'])
            self.assertEqual([a['polarity'] for a in gold['source_assertions']], ['negative'])
            self.assertIsNone(gold['status_events'][0]['superseded_by'])
            self.assertEqual(gold['judgments'][1]['source_view']['stance'], 'withdrawn_negative')
            self.assertEqual(gold['judgments'][2]['source_view']['stance'], 'withdrawn_negative')
            self.assertEqual(gold['judgments'][1]['source_view']['active_assertion_ids'], [])

    def test_existing_judgment_scorer_accepts_new_gold_and_counts_missing_conflicting(self):
        labels = [{'query_id': j['query_id'], 'label': j['label'], 'state': 'completed'}
                  for g in self.gold['cases'] for j in g['judgments']]
        # Gold replay is scorer wiring only; it is not a method prediction/accuracy result.
        replay = adapter.score_judgments(self.gold['cases'], labels)
        self.assertEqual(replay['query_count'], 48)
        self.assertEqual(replay['available'], 48)
        self.assertEqual(replay['failures'], [])
        absent = adapter.score_judgments(self.gold['cases'], [])
        self.assertEqual(absent['query_count'], 48)
        self.assertEqual(absent['unavailable'], 48)
        self.assertEqual(absent['accuracy_all_queries'], 0)
        conflict = adapter.score_judgments(self.gold['cases'],
                    [{'query_id': labels[0]['query_id'], 'label': 'conflicting', 'state': 'completed'}])
        self.assertEqual(conflict['unavailable'], 48)
        self.assertIsNone(conflict['content_truth_accuracy'])
        unknown = adapter.score_judgments(self.gold['cases'],
                    [{'query_id': j['query_id'], 'label': 'unknown', 'state': 'completed'} for j in labels])
        self.assertEqual(unknown['accuracy_all_queries'], 16 / 48)
        self.assertEqual(unknown['per_class']['supported']['fn'], 18)
        self.assertEqual(unknown['per_class']['refuted']['fn'], 14)

    def test_deterministic_authoring_and_frozen_hashes(self):
        inputs, gold = authoring.author()
        self.assertEqual(inputs, self.inputs)
        self.assertEqual(gold, self.gold)
        self.assertEqual(self.policy, authoring.POLICY)
        manifest = json.loads((HERE / 'manifest.json').read_text())
        self.assertEqual(manifest['live_model_calls'], 0)
        self.assertFalse(manifest['old_validation_gold_read'])
        self.assertFalse(manifest['method_predictions_read'])
        for name, digest in manifest['files'].items():
            self.assertEqual(hashlib.sha256((HERE / name).read_bytes()).hexdigest(), digest, name)
        for name, digest in manifest['dependencies'].items():
            self.assertEqual(hashlib.sha256((authoring.ROOT / name).read_bytes()).hexdigest(), digest, name)
        self.assertTrue(manifest['compatibility']['query_payload'])
        self.assertTrue(manifest['compatibility']['score_judgments'])
        self.assertFalse(manifest['compatibility']['withdrawal_extraction_abi'])


if __name__ == '__main__':
    unittest.main()
