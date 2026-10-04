"""Summary-only fixtures; no real unseen cases or labels are loaded."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

from loom.tools.structure import stage5_selection_v1 as e


PROTOCOL = Path(__file__).resolve().parents[3] / 'docs/research/model_research_2026-10-04/stage5_protocol.json'


def record(name, *, correct=8, semantic=8, grounded=8, available=10, cost='0.01'):
    return {'configuration_id': name, 'source_stage': 1, 'task_id': 'aggregate-fixture',
            'dataset_sha256': '1' * 64, 'planned_query_set_sha256': '2' * 64,
            'rubric_sha256': '3' * 64,
            'evidence': {'model_quality_measured': True, 'kind': 'saved_provider_first_response',
                         'first_response_only': True, 'source_report_sha256': '4' * 64,
                         'dataset_inspection_status': 'scripted_aggregate_fixture_for_selector_only'},
            'counts': {'planned': 10, 'attempted_requests': 10, 'correct': correct, 'semantically_valid': semantic,
                       'source_grounded': grounded, 'available': available},
            'cost': {'total_usd': cost, 'complete': True, 'unknown_attempts': 0,
                     'evidence_kind': 'provider_reported'}}


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.protocol = json.loads(PROTOCOL.read_text())

    def select(self, *records):
        return e.select({'schema': 'loom.stage5.study_summary/1', 'records': list(records)}, self.protocol)

    def test_unmeasured_v3_and_scripted_quality_never_win(self):
        v3 = record('directed_refute_v3')
        v3['evidence']['model_quality_measured'] = False
        scripted = record('scripted', correct=10)
        scripted['evidence']['kind'] = 'scripted_mechanism'
        result = self.select(record('measured'), v3, scripted)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], ['measured'])
        self.assertIn('model_quality_unmeasured', result['records'][0]['reasons'])
        self.assertIn('not_provider_quality_evidence', result['records'][2]['reasons'])

    def test_dominance_and_tradeoffs_keep_all_without_quota(self):
        result = self.select(record('dominated', cost='.02'), record('quality', correct=9),
                             record('cheap', correct=7, cost='.001'), record('tied', correct=9))['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], ['cheap', 'quality', 'tied'])
        self.assertEqual(next(r for r in result['records'] if r['configuration_id'] == 'dominated')['dominated_by'],
                         ['quality', 'tied'])

    def test_availability_remains_separate_with_all_planned_correctness(self):
        reliable = record('reliable', correct=5, semantic=5, grounded=5, available=10)
        partial = record('partial', correct=5, semantic=5, grounded=5, available=5)
        result = self.select(reliable, partial)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], ['reliable'])
        self.assertEqual(result['records'][0]['metrics']['correctness_all_planned'], '1/2')
        self.assertEqual(result['records'][0]['metrics']['availability_all_planned'], '1/2')

    def test_semantics_and_grounding_are_not_parse_success(self):
        grounded = record('grounded', correct=9, semantic=9, grounded=9)
        parse_only = record('parse_only', correct=9, semantic=9, grounded=0)
        result = self.select(grounded, parse_only)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], ['grounded'])

    def test_unknown_cost_and_missing_grounding_never_impute_zero(self):
        unknown = record('unknown-cost')
        unknown['cost'].update(complete=False, unknown_attempts=1, total_usd=None)
        missing = record('missing-grounding')
        del missing['counts']['source_grounded']
        result = self.select(unknown, missing)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], [])
        self.assertIsNone(result['records'][1]['metrics']['reported_cost_per_planned_usd'])
        self.assertIsNone(result['records'][0]['metrics']['source_grounding_all_planned'])

    def test_dataset_and_query_and_rubric_groups_never_compete(self):
        records = [record('base', correct=1)]
        for field in ('dataset_sha256', 'planned_query_set_sha256', 'rubric_sha256'):
            other = record(field, correct=10)
            other[field] = '9' * 64
            records.append(other)
        result = self.select(*records)
        self.assertEqual(len(result['groups']), 4)
        self.assertEqual(sum(len(g['selected_configuration_ids']) for g in result['groups']), 4)

    def test_repeats_cannot_be_replacement_retry_evidence(self):
        retried = record('best-of')
        retried['evidence']['first_response_only'] = False
        result = self.select(retried)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], [])
        self.assertIn('not_first_response_evidence', result['records'][0]['reasons'])

    def test_nonfinite_negative_boolean_and_wrong_counts_refused(self):
        for value in ('NaN', 'Infinity', '-1', True):
            bad = record('bad', cost=value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.select(bad)
        for counts in ({'available': 11}, {'correct': 11}, {'planned': True}, {'source_grounded': -1}):
            bad = record('bad')
            bad['counts'].update(counts)
            with self.subTest(counts=counts), self.assertRaises(ValueError):
                self.select(bad)

    def test_empty_and_zero_planned_are_not_measured_winners(self):
        self.assertEqual(self.select()['groups'], [])
        empty = record('empty', correct=0, semantic=0, grounded=0, available=0)
        empty['counts']['planned'] = 0
        result = self.select(empty)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], [])

    def test_global_quality_flag_cannot_make_unexecuted_arm_measured(self):
        unexecuted = record('directed_refute_v3', correct=0, semantic=0, grounded=0, available=0, cost='0')
        unexecuted['counts']['attempted_requests'] = 0
        result = self.select(unexecuted)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], [])
        self.assertIn('no_measured_attempts', result['records'][0]['reasons'])

    def test_configurable_criteria_and_thresholds(self):
        self.protocol['selection']['eligibility_thresholds'] = [
            {'criterion': 'correctness_all_planned', 'operator': 'minimum', 'value': '.9'}]
        result = self.select(record('a', correct=8), record('b', correct=9))['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], ['b'])
        self.protocol['selection']['criteria'] = [self.protocol['selection']['criteria'][0]]
        self.protocol['selection']['eligibility_thresholds'] = []
        result = self.select(record('a', correct=9, cost='.02'), record('b', correct=8, cost='.001'))['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], ['a'])

    def test_custom_cost_denominator_cannot_bypass_incomplete_billing(self):
        self.protocol['selection']['criteria'] = [
            {'id': 'correct_per_cost', 'numerator': 'counts.correct',
             'denominator': 'cost.total_usd', 'direction': 'maximize'}]
        incomplete = record('incomplete-cost', cost='.01')
        incomplete['cost'].update(complete=False, unknown_attempts=1)
        result = self.select(incomplete)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], [])
        self.assertIn('billing_incomplete', result['records'][0]['reasons'])
        self.assertIsNone(result['records'][0]['metrics']['correct_per_cost'])

    def test_quality_only_selection_requires_billing_unless_explicit_policy_changes_it(self):
        self.protocol['selection']['criteria'] = [self.protocol['selection']['criteria'][0]]
        unknown = record('quality-only-unknown-cost', cost=None)
        unknown['cost'].update(complete=False, unknown_attempts=1)
        result = self.select(unknown)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], [])
        self.assertIn('billing_incomplete', result['records'][0]['reasons'])
        self.protocol['selection']['billing_admission'] = {'require_complete_actual_billing': False}
        result = self.select(unknown)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], ['quality-only-unknown-cost'])
        self.assertFalse(result['records'][0]['cost']['complete'])
        self.assertIsNone(result['records'][0]['cost']['total_usd'])
        # Explicit quality-only admission never creates a cost metric.
        self.protocol['selection']['criteria'][0]['denominator'] = 'cost.total_usd'
        result = self.select(unknown)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], [])
        self.assertIsNone(result['records'][0]['metrics']['correctness_all_planned'])

    def test_billing_admission_setting_is_explicit_boolean_and_valid_evidence_remains_required(self):
        for value in (1, 'false', None):
            self.protocol['selection']['billing_admission'] = {'require_complete_actual_billing': value}
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'invalid_billing_admission_policy'):
                self.select(record('known'))
        self.protocol['selection']['billing_admission'] = {'require_complete_actual_billing': True}
        self.protocol['selection']['criteria'] = [self.protocol['selection']['criteria'][0]]
        unsupported = record('unsupported-cost-evidence')
        unsupported['cost']['evidence_kind'] = 'estimated_reservation'
        result = self.select(unsupported)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], [])
        self.assertIn('billing_evidence_kind_unsupported', result['records'][0]['reasons'])

    def test_exact_rational_comparison_for_large_counts(self):
        size = 10 ** 100
        better = record('better')
        worse = record('worse')
        for row in (better, worse):
            row['counts'].update(planned=size, attempted_requests=size, available=size,
                                 semantically_valid=size, source_grounded=size)
        better['counts']['correct'] = size - 1
        worse['counts']['correct'] = size - 2
        result = self.select(worse, better)['groups'][0]
        self.assertEqual(result['selected_configuration_ids'], ['better'])

    def test_duplicate_configuration_and_contradictory_billing_refused(self):
        with self.assertRaisesRegex(ValueError, 'duplicate_configuration'):
            self.select(record('a'), record('a'))
        bad = record('bad')
        bad['cost']['unknown_attempts'] = 1
        with self.assertRaisesRegex(ValueError, 'contradictory_complete_cost'):
            self.select(bad)

    def test_same_query_hash_cannot_hide_a_shorter_denominator(self):
        shortened = record('shortened', correct=5, semantic=5, grounded=5, available=5)
        shortened['counts']['planned'] = 5
        with self.assertRaisesRegex(ValueError, 'conflicting_planned_denominators'):
            self.select(record('original'), shortened)

    def test_no_label_payload_or_path_is_propagated(self):
        for location in ('evidence', 'cost', 'record'):
            bad = record('nonneutral')
            target = bad if location == 'record' else bad[location]
            target['reference_answers'] = 'artificial-sentinel-not-a-real-label'
            with self.subTest(location=location), self.assertRaisesRegex(ValueError, 'nonneutral'):
                self.select(bad)

    def test_split_call_count_and_unknown_billing_consistency(self):
        split = record('split')
        split['counts']['attempted_requests'] = 20
        self.assertEqual(self.select(split)['groups'][0]['selected_configuration_ids'], ['split'])
        split['cost'].update(complete=False, unknown_attempts=21, total_usd=None)
        with self.assertRaisesRegex(ValueError, 'invalid_unknown_attempt_count'):
            self.select(split)

    def test_numeric_json_cost_retains_exact_decimal_lexeme(self):
        summary = {'schema': 'loom.stage5.study_summary/1', 'records': [record('cheaper'), record('dearer')]}
        raw = e.canonical(summary).decode()
        costs = ('0.010000000000000000000000000000000000000000000000001',
                 '0.010000000000000000000000000000000000000000000000002')
        for cost in costs:
            raw = raw.replace('"total_usd":"0.01"', '"total_usd":' + cost, 1)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'summary.json'
            path.write_text(raw)
            parsed, source_hash = e.read_neutral(path)
            self.assertEqual([r['cost']['total_usd'] for r in parsed['records']], list(costs))
            result = e.select(parsed, self.protocol, summary_sha256=source_hash)['groups'][0]
            self.assertEqual(result['selected_configuration_ids'], ['cheaper'])

    def test_content_hashes_determinism_and_exclusive_cli_output(self):
        summary = {'schema': 'loom.stage5.study_summary/1', 'records': [record('a')]}
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, protocol_path, target = root / 'summary.json', root / 'protocol.json', root / 'selection.json'
            source.write_bytes(e.canonical(summary) + b'\n')
            protocol_path.write_bytes(e.canonical(self.protocol) + b'\n')
            result = e.main(['--summary', str(source), '--protocol', str(protocol_path), '--output', str(target)])
            self.assertEqual(result['protocol_sha256'], e.sha256(protocol_path.read_bytes()))
            self.assertEqual(result['summary_sha256'], e.sha256(source.read_bytes()))
            payload = copy.deepcopy(result)
            expected = payload.pop('selection_payload_sha256')
            self.assertEqual(expected, e.sha256(e.canonical(payload)))
            self.assertFalse(result['tool_output_grants_authorization'])
            first = target.read_bytes()
            with self.assertRaises(FileExistsError):
                e.main(['--summary', str(source), '--protocol', str(protocol_path), '--output', str(target)])
            self.assertEqual(first, target.read_bytes())

    def test_refuses_sealed_paths_without_opening_and_json_duplicates(self):
        for name in ('real-holdout-key', 'validation_cases.json'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'sealed_or_validation_path_refused'):
                e.read_neutral(Path('/nonexistent') / name)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'summary.json'
            path.write_text('{"schema":"a","schema":"b"}')
            with self.assertRaisesRegex(ValueError, 'duplicate_json_key'):
                e.read_neutral(path)


if __name__ == '__main__':
    unittest.main()
