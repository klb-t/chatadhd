"""Mechanisms for the controlled source-view intervention; no model-quality test."""
from copy import deepcopy
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

try:
    from . import source_view_experiment as experiment
    from . import graph_panel_live as panel, graph_panel_score_run as replay
except ImportError:
    import source_view_experiment as experiment
    import graph_panel_live as panel, graph_panel_score_run as replay


def toy():
    return {'id': 'mechanism', 'source_id': 'synthetic:mechanism', 'language': 'pl',
        'turns': [{'id': 't1', 'speaker': 'author', 'known_at': '2026-08-01T09:00:00Z', 'text': 'Jeśli żółw rusza, dzwonek dzwoni.'},
                  {'id': 't2', 'speaker': 'author', 'known_at': '2026-08-01T10:00:00Z', 'text': 'Wycofuję tę regułę.'}],
        'node_inventory': [{'id': 'A', 'text': 'żółw rusza', 'aliases': []}, {'id': 'B', 'text': 'dzwonek dzwoni', 'aliases': []}],
        'judgment_queries': [{'id': 'mechanism_q1', 'relation': 'implies', 'source': 'A', 'target': 'B',
            'attributed_to': 'author', 'as_of': '2026-08-01T09:00:00Z', 'scope': 'explicit_source'}]}


class SourceViewMechanisms(unittest.TestCase):
    def test_only_refutation_question_changes(self):
        case = toy(); before = deepcopy(case)
        first = experiment.specs([case], experiment.ARMS[0])[0]
        second = experiment.specs([case], experiment.ARMS[1])[0]
        self.assertEqual(case, before)
        self.assertEqual(first['case_id'], second['case_id'])
        self.assertEqual(first['state'], second['state'])
        self.assertEqual(first['questions']['q01'], second['questions']['q01'])
        self.assertNotEqual(first['questions']['q02'], second['questions']['q02'])
        self.assertEqual(set(first['questions']), {'q01', 'q02'})

    def test_historical_arm_is_exact_prior_frozen_recipe(self):
        case = toy()
        self.assertEqual(experiment.specs([case], experiment.ARMS[0]), panel.jev_judgment_inputs([case]))

    def test_future_turn_mutation_cannot_change_either_request(self):
        original = toy(); changed = deepcopy(original)
        changed['turns'][1]['text'] = 'Reaffirmation or another future statement.'
        for arm in experiment.ARMS:
            self.assertEqual(experiment.specs([original], arm), experiment.specs([changed], arm))

    def test_unknown_arm_and_formal_scope_are_refused(self):
        with self.assertRaises(ValueError): experiment.specs([toy()], 'unregistered')
        case = toy(); case['judgment_queries'][0]['scope'] = 'formal_implication'
        with self.assertRaises(ValueError): experiment.specs([case], experiment.ARMS[0])

    def test_active_policy_cannot_be_mutated_through_returned_requests(self):
        original = deepcopy(experiment.ACTIVE_REFUTE)
        request = experiment.specs([toy()], experiment.ARMS[1])[0]
        request['questions']['q02']['criteria']['true'] = 'mutated'
        self.assertEqual(experiment.ACTIVE_REFUTE, original)

    def test_missing_or_conflicting_preserves_all_three_class_denominators(self):
        gold = [{'id': 'toy', 'judgments': [{'query_id': f'q{i}', 'label': label} for i, label in enumerate(panel.LABELS)]}]
        result = panel.score_judgments(gold, [{'query_id': 'q0', 'state': 'completed', 'label': 'conflicting'}])
        for label in panel.LABELS:
            self.assertEqual(result['per_class'][label]['gold'], 1)
            self.assertEqual(result['per_class'][label]['fn'], 1)
            self.assertIsNone(result['per_class'][label]['precision'])

    def test_raw_cost_mismatch_is_hard_failure(self):
        with self.assertRaises(replay.BillingIntegrityError):
            replay.billing_consistent('0.002', {'reported_cost_usd': '0.000001'})

    def test_fixture_manifest_drift_fails_before_file_content(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory); (path / 'manifest.json').write_text('{}')
            with patch.object(experiment, 'FIXTURE', path):
                with self.assertRaisesRegex(ValueError, 'manifest_drift'):
                    experiment.load_fixture('inputs_dev.json')


if __name__ == '__main__':
    unittest.main()
