"""Mechanism/integrity tests; synthetic probabilities never estimate model quality."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from loom.tools.structure.w3_directed_commitment_v1 import experiment as e
from loom.tools.structure.test_source_view_experiment import toy


class RecipeTests(unittest.TestCase):
    def test_only_one_criterion_changes_over_all_controls(self):
        cases = e.load_controls('inputs_dev.json')
        original = deepcopy(cases)
        old = e.specs(cases, 'active_refute_v2')
        new = e.specs(cases, 'directed_refute_v3')
        self.assertEqual(len(old), 48)
        for a, b in zip(old, new):
            c = deepcopy(b)
            c['questions']['q02']['criteria']['false'] = a['questions']['q02']['criteria']['false']
            self.assertEqual(a, c)
            self.assertEqual(list(b['questions']), ['q01', 'q02'])
            self.assertTrue(b['questions']['q02']['criteria']['false'].endswith(e.recipe()['append']))
        self.assertEqual(cases, original)

    def test_future_source_not_in_request(self):
        case = toy()
        changed = deepcopy(case)
        changed['turns'][1]['text'] = 'Future secret changes interpretation.'
        self.assertEqual(e.specs([case], 'directed_refute_v3'), e.specs([changed], 'directed_refute_v3'))

    def test_ambiguous_ids_and_unknown_endpoints_rejected(self):
        for field in ('turns', 'node_inventory'):
            case = toy(); case[field].append(deepcopy(case[field][0]))
            with self.assertRaises(ValueError): e.specs([case], 'directed_refute_v3')
        case = toy(); case['judgment_queries'][0]['target'] = 'missing'
        with self.assertRaises(ValueError): e.specs([case], 'directed_refute_v3')

    def test_duplicate_query_rejected(self):
        with self.assertRaises(ValueError): e.specs([toy(), toy()], 'directed_refute_v3')

    def test_gold_input_identity_and_full_denominator(self):
        cases = e.load_controls('inputs_dev.json'); golds = e.load_controls('gold_dev.json')
        expected = {q['id'] for c in cases for q in c['judgment_queries']}
        self.assertEqual(expected, {q['query_id'] for g in golds for q in g['judgments']})
        result = e.panel.score_judgments(golds, [])
        self.assertEqual((result['query_count'], result['unavailable']), (48, 48))
        self.assertIsNone(result['content_truth_accuracy'])
        self.assertEqual({label: result['per_class'][label]['gold'] for label in e.panel.LABELS},
                         {'supported': 14, 'refuted': 10, 'unknown': 24})

    def test_duplicate_keys_not_salvaged(self):
        with self.assertRaises(ValueError): e.safe.parse_json('{"answers":{},"answers":{}}')

    def test_threshold_unknown_conflict_and_missing_separated(self):
        rows = []
        for ident, (a, b) in enumerate(((.5, .5), (.51, .5), (.5, .51), (.51, .51))):
            rows.append(e.panel.compile_jev_judgment({'probabilities': {'q01': a, 'q02': b}}, f'q{ident}'))
        self.assertEqual([r['label'] for r in rows], ['unknown', 'supported', 'refuted', 'conflicting'])
        golds = [{'judgments': [{'query_id': f'q{i}', 'label': 'unknown'} for i in range(5)]}]
        score = e.panel.score_judgments(golds, rows)
        self.assertEqual(score['unavailable'], 2)
        self.assertEqual(score['query_count'], 5)


class ReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.runs = Path(cls.temp.name) / 'runs'
        cls.binding = e.restore_history(cls.runs)
        cls.cases = e.base.load_fixture('inputs_dev.json')

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_actual_first_bytes_and_saved_outputs_match(self):
        self.assertEqual(len(self.binding['members']), 98)
        for arm in e.base.ARMS:
            directory = e.HISTORY / arm
            rows, summary = e.replay(directory / 'prepared/manifest.json', self.runs / arm, self.cases, arm)
            self.assertEqual(rows, e.integrity.read(directory / 'first_score/compiled_first.json'))
            self.assertEqual(summary['attempted_requests'], 48)
            self.assertEqual(summary['unknown_cost_attempts'], 0)
            self.assertIsNone(summary['invoice_verified_total_usd'])

    def test_recipe_identity_blocks_old_responses(self):
        with self.assertRaisesRegex(ValueError, 'identity_drift'):
            e.replay(e.HISTORY / 'active_refute_v2/prepared/manifest.json',
                     self.runs / 'active_refute_v2', self.cases, 'directed_refute_v3')

    def test_profiles_pass_existing_consumer_and_exact_counts(self):
        from loom.tools.eval import model_profiles as m
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'profile'
            summary = e.historical(output)
            value = e.integrity.read(output / 'profiles.json')
            self.assertEqual(m.validate(value), [])
            self.assertEqual(summary['correct_over_planned'],
                             {'historical_refute_v1': [42, 48], 'active_refute_v2': [45, 48]})
            self.assertIsNone(value['compensation']['ablation']['current_results'])
            for p in value['profiles']:
                self.assertFalse(p['validity']['inherit_to_other_version'])
                self.assertIsNone(p['cost']['reported_total_usd'])
            self.assertEqual(value['new_model_calls'], 0)

    def test_prepare_and_verify_offline_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / 'plan'
            with patch.object(e.jev, 'transport', side_effect=AssertionError('network forbidden')):
                result = e.prepare(out)
                self.assertEqual(e.verify_plan(out), result)
            self.assertEqual([b['queries'] for b in result['batches']], [48] * 4)
            request = e.integrity.read(out / 'historical_dev/active_refute_v2/request.json')
            self.assertFalse(request['enabled'], 'Never automatically repeat the original paid first attempts')
            self.assertEqual(result['all_variants_reservation_usd'], '0.144')
            self.assertIsNone(result['measured_quality'])
            with self.assertRaises(FileExistsError): e.prepare(out)


if __name__ == '__main__':
    unittest.main()
