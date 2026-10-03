"""Driver integrity over authored fake DEV inventory, never independent validation."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
try:
    from . import new_graph_direct_lookup as direct
except ImportError:
    import new_graph_direct_lookup as direct


def authored_inputs():
    cases = []
    for n in range(24):
        ident = f'hand_{n}'
        cases.append({'id': ident, 'source_id': 'synthetic:' + ident, 'language': 'en',
            'node_inventory': [{'id': 'A', 'text': 'A', 'aliases': []}, {'id': 'B', 'text': 'B', 'aliases': []}],
            'turns': [{'id': ident + '_t1', 'known_at': '2026-01-01T00:00:00Z', 'speaker': 'author', 'text': 'No relation recorded.'}],
            'judgment_queries': [{'id': ident + '_q' + str(q), 'relation': 'implies', 'source': 'A', 'target': 'B',
                'attributed_to': 'author', 'as_of': '2026-01-01T00:00:00Z', 'scope': 'explicit_source'} for q in range(4)]})
    return {'split': 'dev', 'cases': cases}


class DirectDriverTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.input_path, self.graph_path, self.gold_path = [self.root / n for n in ('inputs.json', 'graph.json', 'gold.json')]
        direct.panel.write_new(self.input_path, authored_inputs())
        direct.panel.write_new(self.graph_path, [])
        self.patches = [patch.object(direct, 'INPUT_PATH', self.input_path), patch.object(direct, 'COMPILED_PATH', self.graph_path),
                        patch.object(direct, 'GOLD_PATH', self.gold_path)]
        for p in self.patches:
            p.start(); self.addCleanup(p.stop)
    def test_prediction_never_reads_gold_and_preserves_missing_96(self):
        # Gold does not even exist. Predictions must still freeze and complete.
        frozen = direct.freeze()
        predictions = direct.predict(frozen)
        self.assertFalse(self.gold_path.exists())
        self.assertEqual(predictions['query_count'], 96)
        self.assertEqual(len(predictions['rows']), 96)
        self.assertTrue(all(r['state'] == 'unavailable' for r in predictions['rows']))
        self.assertFalse(predictions['gold_read_during_prediction'])
        self.assertFalse(predictions['causal_prefix_extraction_verified'])

    def test_post_prediction_gold_join_keeps_oracle_separate(self):
        frozen = direct.freeze()
        predictions = direct.predict(frozen)
        golds = [{'id': c['id'], 'family': 'authored', 'language': 'en', 'source_assertions': [], 'status_events': [],
                  'judgments': [{'query_id': q['id'], 'label': 'unknown'} for q in c['judgment_queries']]}
                 for c in authored_inputs()['cases']]
        direct.panel.write_new(self.gold_path, {'split': 'dev', 'cases': golds})
        report = direct.score(frozen, predictions)
        self.assertEqual(report['actual_model_graph_pipeline']['query_count'], 96)
        self.assertEqual(report['actual_model_graph_pipeline']['unavailable'], 96)
        self.assertEqual(report['actual_model_graph_pipeline']['per_class']['unknown']['gold'], 96)
        self.assertEqual(report['actual_model_graph_pipeline']['accuracy_all_queries'], 0)
        self.assertEqual(report['oracle_graph_mechanism_only']['accuracy_all_queries'], 1)
        self.assertFalse(report['information_equivalent_to_raw_prefix_judges'])

    def test_input_or_code_freeze_drift_is_rejected(self):
        frozen = direct.freeze()
        self.graph_path.write_bytes(b'[{}]')
        with self.assertRaisesRegex(ValueError, 'freeze_drift'):
            direct.predict(frozen)

    def test_unknown_policy_keys_are_not_silent_configuration(self):
        policy = direct.load_policy(); policy['unimplemented_new_semantics'] = True
        path = self.root / 'policy.json'; direct.panel.write_new(path, policy)
        with self.assertRaises(ValueError):
            direct.load_policy(path)


if __name__ == '__main__':
    unittest.main()
