"""Mechanical review aggregation tests, not semantic-quality evidence."""
import unittest
from copy import deepcopy

from loom.tools.structure.stage2_semantic_review_v1 import check_span, score


class ReviewTests(unittest.TestCase):
    def setUp(self):
        obs = {'id': 'ob', 'text': 'ż x ż', 'ordinal': 0, 'speaker': 'user', 'locator': {'json_pointer': '/text'}, 'attrs': {'node': 'n'}}
        self.span = {'observation': 'ob', 'byte_start': 5, 'byte_len': 2, 'quote': 'ż'}
        self.inputs = {'cases': [{'id': 'case', 'packet_hash': 'packet', 'source_packet': {'observations': [obs]}}]}
        self.reference = {'schema': 'loom.native_semantic_review.reference/1', 'cases': [{'case_id': 'case', 'packet_hash': 'packet', 'source_locators': [{'observation': 'ob', 'ordinal': 0, 'speaker': 'user', 'locator': obs['locator'], 'node': 'n'}], 'criteria': [{'id': 'criterion', 'support': [self.span], 'expected_interpretation': 'Source interpretation.', 'forbidden_interpretations': ['Overclaim.']}]}]}
        self.plan = {'planned_requests': 2, 'requests': [{'request': {'id': f'r{i}'}, 'case_id': 'case', 'request_hash': 'hash', 'method_id': 'method'} for i in range(2)]}
        self.record = {'request_id': 'r0', 'request_hash': 'hash', 'response_sha256': 'a' * 64, 'native_contract_valid': True, 'bundle_count': 1, 'reviewer': 'manual reviewer', 'criteria': [{'criterion_id': 'criterion', 'bundle_judgements': [{'bundle_index': 0, 'judgement': 'satisfied', 'rationale': 'Manual semantic judgement.', 'source_support': [self.span], 'candidate_pointers': ['/bundles/0/roots']}]}]}

    def test_exact_utf8_span_and_repeated_quote(self):
        obs = {'ob': self.inputs['cases'][0]['source_packet']['observations'][0]}
        check_span(self.span, obs)
        check_span(self.span | {'byte_start': 0}, obs)
        with self.assertRaisesRegex(ValueError, 'exact_span_mismatch'):
            check_span(self.span | {'byte_start': 4}, obs)

    def test_missing_planned_response_keeps_denominator(self):
        result = score(self.reference, self.inputs, self.plan, {'records': [self.record]})
        self.assertEqual(result['planned_criterion_slots'], 2)
        self.assertEqual(result['counts'], {'satisfied': 1, 'violated': 0, 'unresolved': 1})

    def test_absent_judgement_and_empty_bundle_are_unresolved(self):
        for record in (self.record | {'criteria': []}, self.record | {'bundle_count': 0, 'criteria': []}, self.record | {'native_contract_valid': False}):
            result = score(self.reference, self.inputs, self.plan, {'records': [record]})
            self.assertEqual(result['counts']['unresolved'], 2)

    def test_all_alternatives_required_no_cherry_pick(self):
        record = deepcopy(self.record)
        record['bundle_count'] = 2
        result = score(self.reference, self.inputs, self.plan, {'records': [record]})
        self.assertEqual(result['counts']['unresolved'], 2)
        entry = deepcopy(record['criteria'][0]['bundle_judgements'][0])
        entry.update(bundle_index=1, judgement='violated', candidate_pointers=['/bundles/1/roots'])
        record['criteria'][0]['bundle_judgements'].append(entry)
        result = score(self.reference, self.inputs, self.plan, {'records': [record]})
        self.assertEqual(result['counts']['violated'], 1)


if __name__ == '__main__':
    unittest.main()
