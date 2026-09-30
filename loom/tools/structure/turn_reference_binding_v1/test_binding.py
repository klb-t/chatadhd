"""Synthetic mechanism counterexamples; no fixture or model-quality claim."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from binding import bind_turn_references, quote_free_copy, provenance_for_turn
import graph_panel_live as panel


def sample():
    t1 = {'id': 't1', 'speaker': 'alice', 'known_at': '2026-08-10T09:00:00Z', 'text': 'Żółty kabel powoduje zwarcie.'}
    t2 = {'id': 't2', 'speaker': 'bob', 'known_at': '2026-08-10T09:01:00Z', 'text': 'Nie zgadzam się: żółty kabel nie powoduje zwarcia.'}
    case = {'id': 'synthetic', 'source_id': 'synthetic:binding', 'turns': [t1, t2],
            'node_inventory': [{'id': 'A'}, {'id': 'B'}]}
    assertions = [{'id': ident, 'relation': 'causes', 'source': 'A', 'target': 'B', 'polarity': polarity,
                   'attributed_to': turn['speaker'], 'known_at': turn['known_at'],
                   'evidence': [{'turn_id': turn['id'], 'quote': turn['text']}]} for ident, turn, polarity in [('e1', t1, 'positive'), ('e2', t2, 'negative')]]
    event = {'assertion_id': 'e1', 'status': 'superseded', 'superseded_by': 'e2',
             'known_at': t2['known_at'], 'evidence': [{'turn_id': 't2', 'quote': t2['text']}]}
    return case, {'source_assertions': assertions, 'status_events': [event]}


class TurnBindingMechanismTests(unittest.TestCase):
    def setUp(self):
        self.case, self.value = sample()

    def bind(self):
        return bind_turn_references(self.value, self.case, raw_response_sha256='a' * 64)

    def test_copy_failure_recovered_with_original_hint_visible(self):
        self.value['source_assertions'][0]['evidence'][0]['quote'] = 'Zolty kabel causes short.'
        before = panel.compile_extraction(self.value, self.case)
        bound, records = self.bind(); after = panel.compile_extraction(bound, self.case)
        self.assertEqual(before['invalid_assertions'], 1)
        self.assertEqual(after['invalid_assertions'], 0)
        self.assertEqual(records[0]['model_hint'], 'Zolty kabel causes short.')
        self.assertFalse(records[0]['model_hint_exact_unique_in_turn'])
        self.assertEqual(records[0]['source']['derivation'], 'full_source_copied_by_turn_id')

    def test_input_deep_copy_and_non_quote_fields_unchanged(self):
        self.value['source_assertions'][0]['evidence'][0]['quote'] = 'kabel'
        original = deepcopy(self.value); case_before = deepcopy(self.case)
        bound, _ = self.bind()
        self.assertEqual(self.value, original); self.assertEqual(self.case, case_before)
        self.assertNotEqual(bound, original)
        self.assertEqual(quote_free_copy(bound), quote_free_copy(original))

    def test_unicode_full_turn_char_and_byte_provenance(self):
        bound, records = self.bind(); compiled = panel.compile_extraction(bound, self.case)
        source = records[0]['source']; text = self.case['turns'][0]['text']
        self.assertEqual(text[source['char_start']:source['char_end']], text)
        self.assertEqual(text.encode()[source['byte_start']:source['byte_end']].decode(), text)
        self.assertGreater(source['byte_end'], source['char_end'])
        self.assertEqual(compiled['source_assertions'][0]['evidence'][0]['quote'], text)

    def test_unknown_and_unhashable_turn_ids_remain_invalid(self):
        for ident in ('missing', ['t1']):
            self.value['source_assertions'][0]['evidence'][0]['turn_id'] = ident
            bound, records = self.bind()
            self.assertEqual(bound, self.value)
            self.assertEqual(records[0]['binding_state'], 'refused')
            self.assertEqual(panel.compile_extraction(bound, self.case)['invalid_assertions'], 1)

    def test_missing_quote_and_extra_evidence_fields_not_repaired(self):
        for evidence in ({'turn_id': 't1'}, {'turn_id': 't1', 'quote': 'bad', 'extra': True}):
            self.value['source_assertions'][0]['evidence'][0] = evidence
            bound, records = self.bind()
            self.assertEqual(bound, self.value)
            self.assertEqual(records[0]['reason'], 'malformed_evidence_shape')
            self.assertEqual(panel.compile_extraction(bound, self.case)['invalid_assertions'], 1)

    def test_bad_known_at_not_repaired(self):
        self.value['source_assertions'][0]['known_at'] = self.case['turns'][1]['known_at']
        bound, _ = self.bind()
        self.assertEqual(panel.compile_extraction(bound, self.case)['invalid_assertions'], 1)

    def test_duplicate_assertion_ids_keep_invalid_counter(self):
        self.value['source_assertions'].append(deepcopy(self.value['source_assertions'][0]))
        bound, _ = self.bind()
        self.assertEqual(panel.compile_extraction(bound, self.case)['invalid_assertions'], 1)

    def test_wrong_semantics_not_proven_by_valid_binding(self):
        self.value['source_assertions'][1]['polarity'] = 'positive'
        self.value['source_assertions'][1]['attributed_to'] = 'alice'
        bound, records = self.bind(); compiled = panel.compile_extraction(bound, self.case)
        wrong = compiled['source_assertions'][1]
        self.assertEqual(wrong['polarity'], 'positive'); self.assertEqual(wrong['attributed_to'], 'alice')
        self.assertEqual(wrong['content_truth'], 'unverified')
        self.assertTrue(all(not r['source']['semantic_support_established'] for r in records))

    def test_cross_speaker_supersession_unchanged_in_primary_arm(self):
        self.value['status_events'][0]['evidence'][0]['quote'] = 'paraphrased denial'
        bound, records = self.bind(); compiled = panel.compile_extraction(bound, self.case)
        self.assertEqual(len(compiled['status_events']), 1)
        self.assertEqual(compiled['status_events'][0]['superseded_by'], 'e2')
        self.assertEqual(records[-1]['model_hint'], 'paraphrased denial')

    def test_multiple_evidence_entries_and_order_preserved(self):
        self.value['source_assertions'][0]['evidence'].append({'turn_id': 't1', 'quote': 'other'})
        bound, records = self.bind()
        self.assertEqual(len(bound['source_assertions'][0]['evidence']), 2)
        self.assertEqual([r['evidence_pointer'] for r in records[:2]], ['/source_assertions/0/evidence/0', '/source_assertions/0/evidence/1'])

    def test_empty_and_duplicate_source_turns_refused(self):
        self.case['turns'][0]['text'] = ''
        bound, records = self.bind()
        self.assertEqual(records[0]['reason'], 'empty_or_invalid_source_turn')
        self.assertEqual(bound['source_assertions'][0], self.value['source_assertions'][0])
        self.case['turns'].append(deepcopy(self.case['turns'][0]))
        with self.assertRaises(ValueError): self.bind()

    def test_non_string_hint_retained_as_hint_binding_only_changes_quote(self):
        self.value['source_assertions'][0]['evidence'][0]['quote'] = {'model': 'malformed hint'}
        bound, records = self.bind()
        self.assertEqual(records[0]['model_hint'], {'model': 'malformed hint'})
        self.assertEqual(quote_free_copy(bound), quote_free_copy(self.value))
        self.assertEqual(panel.compile_extraction(bound, self.case)['invalid_assertions'], 0)


if __name__ == '__main__': unittest.main()
