"""Independent end-to-end checks of explicitly scripted, unpaid recipe adapters."""
from pathlib import Path
import importlib.util
import tempfile
import unittest

try:
    from . import graph_panel_live as panel
except ImportError:
    import graph_panel_live as panel

PATH = panel.ROOT / 'docs/research/recipe_experiments_2026-09-30/scripted_transport_v1.py'
SPEC = importlib.util.spec_from_file_location('loom_scripted_recipe_mechanisms', PATH)
scripted = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scripted)


class ScriptedRecipeChecks(unittest.TestCase):
    def test_role_classifier_preserves_conflict_and_explicit_scripted_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / 'roles'
            result = scripted.roles_mechanism(folder)
            self.assertEqual(result['real_api_calls'], 0)
            self.assertFalse(result['model_quality_measured'])
            self.assertEqual(result['post_count'], 4)
            self.assertEqual(result['labels'], ['supported', 'refuted', 'unknown', 'conflicting'])
            self.assertEqual(result['unavailable'], 1)
            receipt = scripted.replay.read(folder / 'SCRIPTED_EXECUTION_RECEIPT.json')
            self.assertEqual(receipt['actual_reported_cost_usd'], '0')
            self.assertTrue(receipt['fake_usage_excluded_from_actual_spend_and_model_profiles'])
            score = scripted.replay.read(folder / 'scripted_contract_score.json')
            self.assertEqual(score['execution_kind'], 'scripted_not_model')
            self.assertEqual(score['scripted_contract']['query_count'], 4)

    def test_schema_duplicate_json_and_bad_evidence_never_become_hidden_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / 'hint'
            result = scripted.schema_hint_mechanism(folder)
            self.assertEqual(result['post_count'], 3)
            self.assertEqual(result['compile_states'], ['completed', 'unavailable', 'completed'])
            score = scripted.replay.read(folder / 'scripted_contract_score.json')['scripted_contract']
            self.assertEqual((score['strict_edges']['tp'], score['strict_edges']['fp'], score['strict_edges']['fn']), (1, 1, 2))
            self.assertEqual(score['strict_edges']['gold'], 3)
            self.assertEqual(score['strict_reference_atom_alignment']['gold'], 6)
            receipt = scripted.replay.read(folder / 'SCRIPTED_EXECUTION_RECEIPT.json')
            self.assertEqual(receipt['actual_reported_cost_usd'], '0')
            self.assertEqual(receipt['invalid_assertions'], [0, None, 1])


if __name__ == '__main__':
    unittest.main()
