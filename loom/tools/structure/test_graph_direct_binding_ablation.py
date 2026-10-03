"""Citation-basis driver tests use authored data, never validation or API."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
try:
    from . import graph_direct_binding_ablation as ablation
    from .test_new_graph_direct_lookup_driver import authored_inputs
except ImportError:
    import graph_direct_binding_ablation as ablation
    from test_new_graph_direct_lookup_driver import authored_inputs


class BindingAblationTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(); self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.cases = authored_inputs()['cases']
        self.frozen = {'baseline_direct_freeze': {'policy': ablation.direct.load_policy()}}
        self.graph = self.root / 'graph.json'; self.provenance = self.root / 'provenance.json'
        ablation.panel.write_new(self.graph, [])
        self.hint = {'case_id': self.cases[0]['id'], 'model_hint': 'Wrong copied bytes',
                     'binding_state': 'bound', 'source': {'semantic_support_established': False}}
        ablation.panel.write_new(self.provenance, [self.hint])
        self.patches = [patch.object(ablation, 'freeze', return_value=deepcopy(self.frozen)),
                        patch.object(ablation.direct, 'inputs', return_value=deepcopy(self.cases)),
                        patch.object(ablation, 'GRAPH', self.graph), patch.object(ablation, 'PROVENANCE', self.provenance)]
        for p in self.patches:p.start();self.addCleanup(p.stop)

    def test_prediction_does_not_read_gold_missing_graph_denominator_96(self):
        with patch.object(ablation.panel, 'load_dev_gold', side_effect=AssertionError('gold during prediction')):
            result = ablation.predict(self.frozen)
        self.assertEqual(len(result['rows']), 96)
        self.assertTrue(all(row['state'] == 'unavailable' for row in result['rows']))
        self.assertFalse(result['gold_read_during_prediction'])
        self.assertFalse(result['causal_prefix_extraction_verified'])

    def test_uses_unchanged_lookup_preserves_hints_without_mutation(self):
        original_bytes = self.provenance.read_bytes()
        expected = ablation.direct.lookup(self.cases[0], None, self.cases[0]['judgment_queries'][0], self.frozen['baseline_direct_freeze']['policy'])
        result = ablation.predict(self.frozen)
        first = result['rows'][0]
        for key, value in expected.items():self.assertEqual(first[key], value)
        self.assertEqual(first['graph_provenance']['source_binding_basis'], ablation.BASIS)
        self.assertEqual(first['case_binding_provenance'], [self.hint])
        first['case_binding_provenance'][0]['model_hint'] = 'mutated trace'
        self.assertEqual(self.provenance.read_bytes(), original_bytes)
        self.assertEqual(result['rows'][1]['case_binding_provenance'][0]['model_hint'], 'Wrong copied bytes')

    def test_duplicate_compiled_cases_are_rejected_before_prediction(self):
        self.graph.write_text('[{"case_id":"hand_0"},{"case_id":"hand_0"}]')
        with self.assertRaisesRegex(ValueError, 'duplicate_or_unknown'):
            ablation.predict(self.frozen)

    def test_freeze_drift_rejected_and_no_label_join(self):
        wrong = deepcopy(self.frozen); wrong['changed_variable'] = 'actor_filter'
        with patch.object(ablation.panel, 'load_dev_gold', side_effect=AssertionError('gold during prediction')):
            with self.assertRaisesRegex(ValueError, 'freeze_drift'):ablation.predict(wrong)

    def test_conflicting_effective_label_remains_unavailable(self):
        self.assertEqual(ablation.effective_label({'state': 'completed', 'label': 'conflicting'}), 'unavailable')
        self.assertEqual(ablation.effective_label({'state': 'unavailable', 'label': 'supported'}), 'unavailable')
        self.assertEqual(ablation.effective_label({'state': 'completed', 'label': 'unknown'}), 'unknown')


class BindingReceiptTests(unittest.TestCase):
    def test_complete_upstream_receipt_chain_then_source_drift_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); data = root / 'first_results'; data.mkdir()
            names = ('binding_inputs_first.json', 'compiled_binding_first.json', 'compiled_original_first.json',
                     'provenance_first.json', 'raw_model_contents_first.json')
            for name in names:ablation.panel.write_new(data / name, [])
            raw = root / 'first.response.bin'; raw.write_bytes(b'unchanged synthetic response')
            replay = root / 'freeze_before_replay.json'
            ablation.panel.write_new(replay, {'split': 'dev', 'live_calls': False,
                'files': {'first.response.bin': ablation.direct.file_hash(raw)}})
            receipt = data / 'freeze_before_gold.json'
            ablation.panel.write_new(receipt, {'split': 'dev', 'gold_loaded_by_driver': False,
                'freeze_sha256': ablation.direct.file_hash(replay),
                'files': {name: ablation.direct.file_hash(data / name) for name in names}})
            with patch.object(ablation, 'ROOT', root), patch.object(ablation, 'DATA', data), patch.object(ablation, 'SOURCE_FREEZE', receipt), patch.object(ablation, 'SOURCE_REPLAY_FREEZE', replay):
                self.assertEqual(ablation.verify_binding_receipt()['split'], 'dev')
                raw.write_bytes(b'changed synthetic first response')
                with self.assertRaisesRegex(ValueError, 'replay_hash_drift'):ablation.verify_binding_receipt()


if __name__ == '__main__':unittest.main()
