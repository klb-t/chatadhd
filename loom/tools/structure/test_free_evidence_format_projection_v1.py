"""Synthetic mechanism checks; strict primary compiler and gold remain unchanged."""
from copy import deepcopy
import unittest
from unittest.mock import patch

try:
    from . import free_evidence_format_projection_v1 as projection
    from .test_graph_free_extraction import case, output
except ImportError:
    import free_evidence_format_projection_v1 as projection
    from test_graph_free_extraction import case, output

free, safe = projection.free, projection.safe


class EvidenceProjectionChecks(unittest.TestCase):
    def string_output(self):
        model = output(); model['source_assertions'][0]['evidence'] = ['t1']
        return model

    def test_exact_unambiguous_string_is_only_changed_field(self):
        original = self.string_output(); before = deepcopy(original)
        derived, changes = projection.normalize_evidence(original, free.source_payload(case()))
        expected = deepcopy(original)
        expected['source_assertions'][0]['evidence'] = [{'turn_id': 't1'}]
        self.assertEqual(derived, expected)
        self.assertEqual(original, before)
        self.assertEqual(len(changes), 1)
        self.assertEqual(changes[0]['pointer'], '/source_assertions/0/evidence/0')

    def test_projection_keeps_raw_and_typed_semantics_and_full_source(self):
        source = free.source_payload(case()); model = self.string_output()
        base = free.compile_free(model, source)
        new, receipt = projection.project(base, source)
        self.assertEqual(base['source_assertions'], [])
        self.assertEqual(base['invalid_assertions'], 1)
        self.assertEqual(new['invalid_assertions'], 0)
        self.assertEqual(len(new['source_assertions']), 1)
        edge = new['source_assertions'][0]
        for key in ('id', 'relation', 'source', 'target', 'polarity', 'attributed_to', 'known_at'):
            self.assertEqual(edge[key], model['source_assertions'][0][key])
        self.assertEqual(edge['content_truth'], 'unverified')
        self.assertEqual(edge['evidence'][0]['quote'], source['turns'][0]['text'])
        self.assertEqual(receipt['original_model_object'], model)
        self.assertEqual(receipt['original_model_object_sha256'], safe.digest(model))
        self.assertNotEqual(receipt['original_model_object_sha256'], receipt['derived_model_object_sha256'])

    def test_prior_valid_objects_and_records_are_byte_identical(self):
        source = free.source_payload(case()); model = output()
        base = free.compile_free(model, source)
        new, receipt = projection.project(base, source)
        self.assertEqual(new, base)
        self.assertEqual(receipt['evidence_items_converted'], 0)

    def test_unknown_similar_or_whitespace_ids_are_never_guessed(self):
        for value in ('t01', ' t1', 't1 ', 'T1', 'turn1', '', 't2'):
            model = self.string_output(); model['source_assertions'][0]['evidence'] = [value]
            derived, changes = projection.normalize_evidence(model, free.source_payload(case()))
            self.assertEqual(derived, model)
            self.assertEqual(changes, [])
            self.assertEqual(free.compile_free(derived, free.source_payload(case()))['invalid_assertions'], 1)

    def test_ambiguous_source_turn_identity_keeps_primary_rejection(self):
        source = free.source_payload(case()); source['turns'].append(deepcopy(source['turns'][0]))
        model = self.string_output(); derived, changes = projection.normalize_evidence(model, source)
        self.assertEqual(derived, model); self.assertEqual(changes, [])

    def test_mixed_evidence_does_not_drop_unknown_bad_denominator(self):
        source = free.source_payload(case()); model = self.string_output()
        model['source_assertions'][0]['evidence'] = ['t1', 'unknown']
        derived, changes = projection.normalize_evidence(model, source)
        self.assertEqual(derived['source_assertions'][0]['evidence'], [{'turn_id': 't1'}, 'unknown'])
        self.assertEqual(len(changes), 1)
        self.assertEqual(free.compile_free(derived, source)['invalid_assertions'], 1)

    def test_known_at_and_wrong_endpoint_remain_invalid_after_binding(self):
        source = free.source_payload(case())
        for key, value in [('known_at', '2026-08-10T10:00:00Z'), ('target', 'missing')]:
            model = self.string_output(); model['source_assertions'][0][key] = value
            base = free.compile_free(model, source)
            projected, receipt = projection.project(base, source)
            self.assertEqual(projected['source_assertions'], [])
            self.assertEqual(projected['invalid_assertions'], 1)
            self.assertEqual(receipt['evidence_items_converted'], 1)

    def test_duplicate_json_keys_fail_in_unchanged_parser(self):
        model = '{"nodes":[],"source_assertions":[],"source_assertions":[],"status_events":[]}'
        with self.assertRaises(ValueError):
            projection.normalize_evidence(model, free.source_payload(case()))

    def test_unavailable_primary_output_cannot_be_salvaged(self):
        original = {'case_id': 'unavailable', 'state': 'unavailable', 'reason': 'response_compile_or_identity_rejected'}
        with patch.object(projection, 'normalize_evidence', side_effect=AssertionError('salvage attempted')):
            derived, receipt = projection.project(original, free.source_payload(case()))
        self.assertEqual(derived, original)
        self.assertTrue(receipt['base_unavailability_preserved'])

    def test_other_field_and_negated_atom_text_are_never_normalized(self):
        source = free.source_payload(case()); model = self.string_output()
        model['nodes'][0]['text'] = 'NOT(A OR B)'
        model['source_assertions'][0]['polarity'] = 'negative'
        model['source_assertions'][0]['attributed_to'] = 'writer with explicit quote'
        derived, _ = projection.normalize_evidence(model, source)
        self.assertEqual(derived['nodes'], model['nodes'])
        for key in model['source_assertions'][0]:
            if key != 'evidence': self.assertEqual(derived['source_assertions'][0][key], model['source_assertions'][0][key])

    def test_event_evidence_uses_same_rule_without_status_actor_repair(self):
        source = free.source_payload(case()); model = self.string_output()
        model['status_events'] = [{'assertion_id': 'old', 'status': 'superseded',
            'superseded_by': 'missing', 'known_at': source['turns'][0]['known_at'], 'evidence': ['t1']}]
        derived, changes = projection.normalize_evidence(model, source)
        self.assertEqual(derived['status_events'][0]['evidence'], [{'turn_id': 't1'}])
        self.assertEqual(derived['status_events'][0]['superseded_by'], 'missing')
        compiled = free.compile_free(derived, source)
        self.assertEqual(compiled['invalid_events'], 1)
        self.assertEqual(len(changes), 2)

    def test_modified_base_first_output_is_rejected(self):
        source = free.source_payload(case()); base = free.compile_free(output(), source)
        base['source_assertions'][0]['known_at'] = '2026-09-01T00:00:00Z'
        with self.assertRaises(ValueError): projection.project(base, source)


if __name__ == '__main__':
    unittest.main()
