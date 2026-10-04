"""Source/provenance and controlled-intervention tests; no model calls."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

from loom.tools.structure import stage2_semantic_variants_v1 as e
from loom.tools.structure import candidate_graph


DATA = e.ROOT / 'docs/research/model_research_2026-10-04/stage2'


class Stage2Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = e.read(DATA / 'config.json')
        cls.inputs = e.read(DATA / 'inputs.synthetic_dev.json')
        cls.plan = e.verify(DATA / 'prepared')

    def test_exact_source_projection_rebuilds_without_gold_or_network(self):
        def forbidden(*args, **kwargs):
            self.fail('source preparation must not load a key or call a provider')
        with tempfile.TemporaryDirectory() as folder, patch.object(e.safe, 'transport', forbidden), patch.object(e.safe, 'load_key', forbidden):
            actual = e.project_inputs(DATA / 'config.json', Path(folder) / 'inputs.json')
        self.assertEqual(actual, self.inputs)
        self.assertFalse(actual['reference_labels_loaded'])

    def test_every_observation_pointer_recovers_exact_exported_text(self):
        members = {}
        for provider, relative in self.config['source_archives'].items():
            with ZipFile(e.ROOT / relative) as archive:
                members[provider] = e.safe.parse_json(archive.read('conversations.json'))
        for case in self.inputs['cases']:
            provider = case['id'].split(':')[0]
            for observation in case['source_packet']['observations']:
                value = members[provider]
                for part in observation['locator']['json_pointer'].split('/')[1:]:
                    key = part.replace('~1', '/').replace('~0', '~')
                    value = value[int(key)] if isinstance(value, list) else value[key]
                self.assertEqual(value, observation['text'])
                self.assertIsNone(observation['locator']['byte_start'])
                self.assertIsNone(observation['locator']['byte_len'])

    def test_current_branch_does_not_merge_unselected_claude_fork(self):
        case = next(c for c in self.inputs['cases'] if c['id'] == 'claude:nf-07-fork-python-or-cpp')
        nodes = [o['attrs']['node'] for o in case['source_packet']['observations']]
        self.assertEqual(nodes, ['u1', 'u2b', 'a2b'])
        self.assertNotIn('u2a', nodes)
        self.assertNotIn('a2a', nodes)

    def test_native_shape_has_explicit_no_prior_graph_and_python_preflight_passes(self):
        for case in self.inputs['cases']:
            packet = case['source_packet']
            self.assertEqual(packet['entities'], [])
            self.assertEqual(packet['claims'], [])
            empty = {'schema': 'loom.candidate_graph/1', 'packet_id': packet['snapshot_id'],
                     'entity_drafts': [], 'claim_drafts': [], 'roots': [], 'coverage': [], 'unknowns': []}
            self.assertTrue(candidate_graph.validate_bundle(empty, packet)['valid'])
        self.assertFalse(self.plan['native_runtime_wiring_proven'])
        self.assertEqual(self.plan['semantic_reference_status'], self.config['scoring']['semantic_reference_status'])

    def test_sixty_requests_have_same_source_across_four_controlled_arms(self):
        self.assertEqual(self.plan['planned_requests'], 60)
        self.assertEqual(self.plan['case_count'], 15)
        self.assertEqual(len(self.plan['methods']), 4)
        for case_id in self.config['case_ids']:
            rows = [r for r in self.plan['requests'] if r['case_id'] == case_id]
            self.assertEqual(len(rows), 4)
            self.assertEqual(len({r['packet_hash'] for r in rows}), 1)
            self.assertEqual(len({r['request']['body']['messages'][1]['content'] for r in rows}), 1)
            for row in rows:
                self.assertNotIn('ground_truth', row['request']['body']['messages'][1]['content'])
        recipes = e.read(DATA / 'recipes.json')['recipes']
        self.assertTrue(recipes[1]['system_prompt'].startswith(recipes[0]['system_prompt']))

    def test_configurable_presets_and_euro_budget_have_no_implicit_usd_conversion(self):
        self.assertEqual(self.plan['programme_budget']['currency'], 'EUR')
        self.assertEqual(self.plan['programme_budget']['amount'], '5')
        self.assertEqual(self.plan['programme_budget']['key_scope'], 'separate_dedicated_key')
        self.assertIsNone(self.plan['programme_budget']['provider_USD_cap'])
        self.assertFalse(self.plan['EUR_USD_parity_assumed'])
        self.assertEqual(self.plan['new_provider_calls'], 0)
        self.assertFalse(self.plan['gold_loaded_by_preparation'])
        for arm in self.plan['arms']:
            self.assertIn('pending', arm['execution_manifest_status'])


if __name__ == '__main__':
    unittest.main()
