"""Frozen scorer denominators, paired controls and replay; mock probabilities."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
try:
    from . import recipe_live_score as score
    from .test_recipe_live_pilot import HTTP
except ImportError:
    import recipe_live_score as score
    from test_recipe_live_pilot import HTTP
p = score.runner


class RecipeScoreTests(unittest.TestCase):
    def test_missing_positive_does_not_become_observed_false_negative(self):
        rows = [{'label': y, 'probability': prob} for y, prob in
                ((1, .9), (0, .9), (1, .1), (0, .1), (1, None))]
        result = score.metrics(rows)
        self.assertEqual((result['tp'], result['tn'], result['fp'], result['fn']), (1, 1, 1, 1))
        self.assertEqual((result['planned'], result['observed'], result['missing_positive']), (5, 4, 1))
        self.assertEqual(result['precision_observed'], .5)
        self.assertEqual(result['recall_observed'], .5)
        self.assertAlmostEqual(result['recall_planned'], 1 / 3)
        self.assertEqual(result['accuracy_planned'], .4)
        self.assertEqual(sum(b['count'] for b in result['ece_bins']), 4)
        self.assertEqual(result['selective_coverage_planned'], .8)

    def test_empty_predictions_and_zero_precision_denominator_are_honest(self):
        result = score.metrics([{'label': 1, 'probability': None}, {'label': 0, 'probability': None}])
        self.assertIsNone(result['precision_observed'])
        self.assertIsNone(result['recall_observed'])
        self.assertIsNone(result['brier_observed'])
        self.assertEqual(result['recall_planned'], 0)
        self.assertEqual(result['accuracy_planned'], 0)
        result = score.metrics([{'label': 1, 'probability': .1}, {'label': 0, 'probability': .1}])
        self.assertIsNone(result['precision_observed'])
        self.assertEqual(result['recall_observed'], 0)

    def test_paired_bootstrap_retains_incomplete_cases_and_both_directions(self):
        rows = []
        for case, a, b in (('r01', .9, .1), ('r02', .1, .9), ('r03', .9, None)):
            for arm, probability in (('object_meaningful', a), ('string', b)):
                rows.append({'case_id': case, 'label': 1, 'arm': arm, 'probability': probability})
        result = score.paired(rows)
        self.assertEqual(result, score.paired(rows))
        self.assertEqual(result['complete_pairs'], 2)
        self.assertEqual(result['planned_pairs'], 3)
        self.assertEqual(result['unresolved_case_ids'], ['r03'])
        self.assertEqual(result['accuracy_delta']['mean'], 0)
        self.assertEqual(result['accuracy_delta']['percentile_95_interval'], [-1, 1])
        self.assertAlmostEqual(result['brier_delta']['mean'], 0)
        self.assertEqual(result['bootstrap_replicates'], 2000)

    def test_probability_edges_have_finite_logloss_and_complete_bin_counts(self):
        result = score.metrics([{'label': 1, 'probability': 0}, {'label': 0, 'probability': 1}])
        self.assertTrue(score.math.isfinite(result['logloss_observed']))
        self.assertEqual([b['count'] for b in result['ece_bins']], [1, 0, 0, 0, 1])
        self.assertEqual(result['ece_observed'], 1)

    def test_offline_complete_and_partial_replay_use_original_raw_not_checkpoint_p(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); run_dir = root / 'run'
            m = p.prepare(p.frozen_plan(), root / 'prepared', transport_fn=HTTP(), run_dir=run_dir)
            p.run(m, transport_fn=HTTP(), key_loader=lambda: 'unit-secret')
            report = score.score(m, run_dir)
            self.assertEqual(report['overall']['planned'], 64)
            self.assertEqual(report['overall']['observed'], 64)
            self.assertEqual(report['status']['completed_calls'], 32)
            self.assertEqual(report['paired']['expressed']['accuracy_delta']['mean'], 0)
            self.assertEqual(report['paired']['inferred']['accuracy_delta']['mean'], 0)
            # Constant positive mock can lose to the simpler all-negative baseline.
            self.assertGreater(report['overall']['baselines_planned']['all_negative_accuracy'],
                               report['overall']['accuracy_observed'])
            checkpoint = p.pilot.read_json(run_dir / 'ledger.json')
            checkpoint['attempts'][0]['probabilities']['expressed'] = .01
            # Changing checkpoint p without immutable receipt fails closed.
            p.safe._atomic(run_dir / 'ledger.json', p.safe.canonical(checkpoint))
            with self.assertRaisesRegex(p.safe.RunnerError, 'terminal_receipt'):
                score.score(m, run_dir)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); run_dir = root / 'run'
            m = p.prepare(p.frozen_plan(), root / 'prepared', transport_fn=HTTP(), run_dir=run_dir)
            p.run(m, transport_fn=HTTP(response=lambda v: (503, p.safe.canonical(v))), key_loader=lambda: 'unit-secret')
            report = score.score(m, run_dir)
            self.assertEqual(report['overall']['observed'], 0)
            self.assertEqual(report['overall']['missing'], 64)
            self.assertEqual(report['status']['attempted_calls'], 1)
            self.assertEqual(report['paired']['expressed']['complete_pairs'], 0)


if __name__ == '__main__':
    unittest.main()
