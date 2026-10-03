"""Independent scorer audit, authored before opening validation predictions.

Synthetic mutation tests are scorer-mechanism tests, not extraction-quality
measurements. This module never reads the frozen quality fixture or parser
output. Its temporary validation fixture is deliberately unrelated.
"""
from contextlib import redirect_stdout
from copy import deepcopy
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

try:
    from . import round3_eval as scorer
except ImportError:
    import round3_eval as scorer


def examples():
    text = 'Jeśli łódź nie tonie, port nie alarmuje.'

    def located(fragment):
        a = text.index(fragment)
        b = a + len(fragment)
        return {'text': fragment, 'span': [a, b],
                'utf8_byte_span': [len(text[:a].encode()), len(text[:b].encode())]}

    gold = {'family': 'conditional', 'operation': 'implies',
            'operands': [dict(role='condition', **located('łódź nie tonie')),
                         dict(role='consequence', **located('port nie alarmuje'))],
            'cue': located('Jeśli'), 'qualifiers': {'relation_polarity': 'affirmative'}}
    case = {'id': 'audit-positive', 'family': 'conditional', 'text': text,
            'expected': [gold]}

    def candidate_location(loc):
        a, b = loc['span']
        x, y = loc['utf8_byte_span']
        return {'text': loc['text'], 'span': {'char_start': a, 'char_end': b,
                'byte_start': x, 'byte_end': y, 'quote': loc['text'],
                'coordinate_space': 'input.text'}}

    candidate = {'operation_family': 'conditional', 'operation': 'implies',
                 'slots': {op['role']: candidate_location(op) for op in gold['operands']},
                 'cue': candidate_location(gold['cue']),
                 'qualifiers': deepcopy(gold['qualifiers'])}
    negative = {'id': 'audit-negative', 'family': 'cause', 'text': 'The ferry arrived.',
                'expected': []}
    return case, candidate, negative


class ScorerMutationAudit(unittest.TestCase):
    def setUp(self):
        self.case, self.candidate, self.negative = examples()

    def run_candidate(self, candidate=None):
        return scorer.score([self.case], [{'case_id': self.case['id'],
                            'extraction': {'candidates': [candidate or self.candidate]}}])

    def assert_counts(self, result, tp, fp, fn, key='strict'):
        self.assertEqual((result[key]['tp'], result[key]['fp'], result[key]['fn']),
                         (tp, fp, fn))
        self.assertEqual(result[key]['gold_relations'], tp + fn)
        self.assertEqual(result[key]['predicted_relations'], tp + fp)

    def assert_invalid(self, candidate):
        result = self.run_candidate(candidate)
        self.assert_counts(result, 0, 1, 1)
        self.assertEqual(result['accounting']['invalid_outputs'], 1)

    def test_perfect_unicode_negative_operands(self):
        result = self.run_candidate()
        self.assert_counts(result, 1, 0, 0)
        self.assert_counts(result, 1, 0, 0, 'family_only')
        self.assertEqual(result['failures'], [])
        self.assertIsNone(result['semantic_truth_accuracy'])

    def test_direction_role_swap_is_both_fp_and_fn(self):
        candidate = deepcopy(self.candidate)
        candidate['slots']['condition'], candidate['slots']['consequence'] = (
            candidate['slots']['consequence'], candidate['slots']['condition'])
        result = self.run_candidate(candidate)
        self.assert_counts(result, 0, 1, 1)
        self.assert_counts(result, 1, 0, 0, 'family_only')

    def test_wrong_operation_is_strict_mismatch_only(self):
        candidate = deepcopy(self.candidate)
        candidate['operation'] = 'unless'
        result = self.run_candidate(candidate)
        self.assert_counts(result, 0, 1, 1)
        self.assert_counts(result, 1, 0, 0, 'family_only')

    def test_wrong_family_is_fp_fn_in_both_metrics(self):
        candidate = deepcopy(self.candidate)
        candidate['operation_family'] = 'cause'
        result = self.run_candidate(candidate)
        self.assert_counts(result, 0, 1, 1)
        self.assert_counts(result, 0, 1, 1, 'family_only')

    def test_missing_qualifier_is_strict_mismatch(self):
        candidate = deepcopy(self.candidate)
        del candidate['qualifiers']
        self.assert_counts(self.run_candidate(candidate), 0, 1, 1)

    def test_wrong_qualifier_is_strict_mismatch(self):
        candidate = deepcopy(self.candidate)
        candidate['qualifiers']['relation_polarity'] = 'negative'
        self.assert_counts(self.run_candidate(candidate), 0, 1, 1)

    def test_extra_role_is_strict_mismatch(self):
        candidate = deepcopy(self.candidate)
        candidate['slots']['invented'] = deepcopy(candidate['slots']['condition'])
        self.assert_counts(self.run_candidate(candidate), 0, 1, 1)

    def test_wrong_operand_text_is_invalid(self):
        candidate = deepcopy(self.candidate)
        candidate['slots']['condition']['text'] = 'łódź tonie'
        self.assert_invalid(candidate)

    def test_wrong_operand_quote_is_invalid(self):
        candidate = deepcopy(self.candidate)
        candidate['slots']['condition']['span']['quote'] = 'forged observation'
        self.assert_invalid(candidate)

    def test_wrong_operand_utf8_is_invalid(self):
        candidate = deepcopy(self.candidate)
        candidate['slots']['condition']['span']['byte_end'] -= 1
        self.assert_invalid(candidate)

    def test_negative_operand_offset_is_invalid(self):
        candidate = deepcopy(self.candidate)
        candidate['slots']['condition']['span']['char_start'] = -1
        self.assert_invalid(candidate)

    def test_missing_coordinate_space_is_invalid(self):
        candidate = deepcopy(self.candidate)
        del candidate['slots']['condition']['span']['coordinate_space']
        self.assert_invalid(candidate)

    def test_wrong_coordinate_space_is_invalid(self):
        candidate = deepcopy(self.candidate)
        candidate['cue']['span']['coordinate_space'] = 'transformed.text'
        self.assert_invalid(candidate)

    def test_float_byte_offsets_are_invalid(self):
        candidate = deepcopy(self.candidate)
        span = candidate['slots']['condition']['span']
        span['byte_start'] = float(span['byte_start'])
        span['byte_end'] = float(span['byte_end'])
        self.assert_invalid(candidate)

    def test_boolean_cue_offset_is_invalid(self):
        candidate = deepcopy(self.candidate)
        candidate['cue']['span']['char_start'] = False
        self.assert_invalid(candidate)

    def test_forged_cue_quote_is_invalid(self):
        candidate = deepcopy(self.candidate)
        candidate['cue']['span']['quote'] = 'fabricated cue quote'
        self.assert_invalid(candidate)

    def test_empty_out_of_bounds_cue_is_invalid(self):
        candidate = deepcopy(self.candidate)
        length = len(self.case['text'])
        byte_length = len(self.case['text'].encode())
        candidate['cue'] = {'text': '', 'span': {'char_start': length + 3,
            'char_end': length + 4, 'byte_start': byte_length,
            'byte_end': byte_length, 'quote': '', 'coordinate_space': 'input.text'}}
        self.assert_invalid(candidate)

    def test_duplicate_candidates_count_extra_fp(self):
        outputs = [{'case_id': self.case['id'], 'extraction': {
                    'candidates': [self.candidate, deepcopy(self.candidate)]}}]
        result = scorer.score([self.case], outputs)
        self.assert_counts(result, 1, 1, 0)
        self.assert_counts(result, 1, 1, 0, 'family_only')

    def test_missing_outputs_preserve_denominators_and_abstentions(self):
        result = scorer.score([self.case, self.negative], [])
        self.assert_counts(result, 0, 0, 1)
        self.assertEqual(result['accounting']['cases'], 2)
        self.assertEqual(result['accounting']['missing_output_cases'], 2)
        self.assertEqual(result['accounting']['abstain_gold_cases'], 1)
        self.assertEqual(result['accounting']['abstain_predicted_cases'], 2)

    def test_false_positive_on_abstention(self):
        negative = deepcopy(self.case)
        negative['expected'] = []
        result = scorer.score([negative], [{'case_id': negative['id'],
                            'extraction': {'candidates': [self.candidate]}}])
        self.assert_counts(result, 0, 1, 0)
        self.assertEqual(result['failures'][0]['failure'], 'false_positive')

    def test_duplicate_output_case_is_rejected(self):
        output = {'case_id': self.case['id'], 'extraction': {'candidates': []}}
        with self.assertRaises(ValueError):
            scorer.score([self.case], [output, deepcopy(output)])

    def test_unknown_output_case_is_rejected(self):
        with self.assertRaises(ValueError):
            scorer.score([self.case], [{'case_id': 'unknown', 'extraction': {}}])

    def test_duplicate_fixture_case_is_rejected(self):
        with self.assertRaises(ValueError):
            scorer.score([self.case, deepcopy(self.case)], [])

    def test_per_family_totals_reconcile(self):
        result = scorer.score([self.case, self.negative], [{
            'case_id': self.case['id'], 'extraction': {'candidates': [self.candidate]}}])
        for field in ['tp', 'fp', 'fn', 'gold_relations', 'predicted_relations']:
            self.assertEqual(sum(row['strict'][field] for row in result['per_family'].values()),
                             result['strict'][field])
        for field in ['cases', 'missing_output_cases', 'abstain_gold_cases']:
            self.assertEqual(sum(row['accounting'][field] for row in result['per_family'].values()),
                             result['accounting'][field])

    def test_empty_metric_denominators_are_none(self):
        result = scorer.score([self.negative], [])
        self.assert_counts(result, 0, 0, 0)
        self.assertIsNone(result['strict']['precision'])
        self.assertIsNone(result['strict']['recall'])


class FreezeAccessAudit(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.fixture = self.root / 'fixture'
        self.fixture.mkdir()
        payload = {'schema': 'test-only', 'split': 'validation', 'cases': []}
        (self.fixture / 'validation.json').write_text(json.dumps(payload))
        (self.fixture / 'manifest.json').write_text(json.dumps({'files': {
            'validation.json': scorer.digest(self.fixture / 'validation.json')}}))
        self.hashes = {'test-only.py': 'independently-fixed-hash'}
        self.freeze = self.root / 'FREEZE.json'
        self.freeze.write_text(json.dumps({'implementation_sha256': self.hashes,
            'fixture_manifest_sha256': scorer.digest(self.fixture / 'manifest.json')}))
        fixture_patch = patch.object(scorer, 'FIXTURE', self.fixture)
        impl_patch = patch.object(scorer, 'implementation_hashes', return_value=self.hashes)
        fixture_patch.start()
        impl_patch.start()
        self.addCleanup(fixture_patch.stop)
        self.addCleanup(impl_patch.stop)

    def test_validation_without_release_is_rejected(self):
        with self.assertRaises(ValueError):
            scorer.load_fixture('validation', freeze=self.freeze)

    def test_validation_without_freeze_is_rejected(self):
        with self.assertRaises(ValueError):
            scorer.load_fixture('validation', validation_release=True)

    def test_unchanged_preexisting_freeze_releases(self):
        self.assertEqual(scorer.load_fixture('validation', validation_release=True,
                         freeze=self.freeze)['cases'], [])

    def test_implementation_drift_is_rejected(self):
        with patch.object(scorer, 'implementation_hashes', return_value={'test-only.py': 'drift'}):
            with self.assertRaises(ValueError):
                scorer.load_fixture('validation', validation_release=True, freeze=self.freeze)

    def test_manifest_drift_is_rejected(self):
        (self.fixture / 'manifest.json').write_text('{"files":{}}')
        with self.assertRaises(ValueError):
            scorer.load_fixture('validation', validation_release=True, freeze=self.freeze)

    def test_fixture_split_drift_is_rejected(self):
        (self.fixture / 'validation.json').write_text('{"cases":["changed"]}')
        with self.assertRaises(ValueError):
            scorer.load_fixture('validation', validation_release=True, freeze=self.freeze)

    def test_first_result_cannot_be_overwritten(self):
        result = self.root / 'first.json'
        scorer.save_new(result, {'first': 1})
        with self.assertRaises(FileExistsError):
            scorer.save_new(result, {'replacement': 2})
        self.assertEqual(json.loads(result.read_text()), {'first': 1})

    def test_same_call_freeze_and_validation_release_is_rejected(self):
        new_freeze = self.root / 'not-preexisting.json'
        args = ['round3_eval', '--split', 'validation', '--validation-release',
                '--write-freeze', str(new_freeze), '--freeze', str(new_freeze),
                '--output', str(self.root / 'result.json')]
        with patch('sys.argv', args), patch.object(scorer, 'predictions', return_value=[]), redirect_stdout(io.StringIO()):
            with self.assertRaises(SystemExit) as failure:
                scorer.main()
            self.assertEqual(failure.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
