"""Focused public presentation contract checks, no private data or provider calls."""
import copy
import gzip
import importlib.util
import json
from pathlib import Path
import unittest

BASE = Path(__file__).parent
spec = importlib.util.spec_from_file_location('delivery_build', BASE/'delivery-build.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
from jsonschema import Draft202012Validator, ValidationError


class DeliveryContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = builder.read(BASE/'delivery-policy.json')
        cls.presentation, cls.contract, cls.artifact = builder.build(cls.policy)
        cls.validator = Draft202012Validator(cls.contract)

    def test_existing_schema_codec_and_exact_results_roundtrip(self):
        files = builder.graph.recover_files(self.artifact)
        self.assertEqual(builder.graph.strict_json(files['results'])['records'], [self.presentation])
        self.assertTrue(builder.graph.recover_results(self.artifact))
        self.assertEqual(self.artifact['contract']['schema'], 'loom.method_graph/1')
        self.assertEqual(self.artifact['trace']['schema'], 'loom.method_run_trace/1')
        lineage = {row['role']: row['sha256'] for row in self.presentation['evidence']}
        self.assertEqual(lineage['historical_initial_checkpoint'], '137c49ab13681d036b955039a580aff3cb86bc342b62981c484abcfe9363c28e')
        self.assertEqual(lineage['immediate_previous_checkpoint'], builder.read(BASE/'START.json')['previous_checkpoint']['sha256'])
        self.assertEqual(lineage['continuation_start_provenance'], builder.digest((BASE/'START.json').read_bytes()))
        self.assertNotIn('previous_private_checkpoint', lineage)

    def test_public_artifact_rebuild_matches_frozen_bytes(self):
        raw = builder.graph.canonical(self.artifact)+b'\n'
        self.assertEqual(gzip.decompress((BASE/'delivery-artifact-v3.json.gz').read_bytes()), raw)
        self.assertEqual(builder.read(BASE/'delivery-example-v3.json'), self.presentation)
        self.assertEqual(builder.read(BASE/'delivery-contract-v3.json'), self.contract)

    def test_changed_input_digest_is_rejected(self):
        changed = copy.deepcopy(self.policy)
        changed['inputs'][0]['sha256'] = '0'*64
        with self.assertRaisesRegex(ValueError, '^public_input_digest_changed$'):
            builder.build(changed)

    def test_model_quality_or_native_claim_cannot_be_promoted(self):
        for key in ['quality','native_execution','format','source_references','graph_integrity','goal_preservation','preference_preservation','stability','latency_seconds']:
            changed = copy.deepcopy(self.presentation)
            changed['expert'][key] = 1
            with self.subTest(key=key), self.assertRaises(ValidationError):
                self.validator.validate(changed)

    def test_historical_budget_is_not_current_preflight(self):
        expert = self.presentation['expert']
        self.assertEqual(expert['historical_campaign_attempts'], 720)
        self.assertEqual(expert['historical_campaign_cost_usd'], '0.873216500')
        for key in ['current_provider_usage_usd','current_provider_reservations_usd','current_available_budget_usd']:
            self.assertIsNone(expert[key])
            changed = copy.deepcopy(self.presentation)
            changed['expert'][key] = 0
            with self.subTest(key=key), self.assertRaises(ValidationError):
                self.validator.validate(changed)

    def test_scope_denominators_and_tokenizer_remain_explicit(self):
        offline = self.presentation['expert']['offline']
        self.assertEqual(offline['unit_of_dependence'], 'conversation_family')
        self.assertEqual(offline['retrieval']['families'], 12)
        self.assertEqual(offline['retrieval']['tasks'], 48)
        self.assertIsNone(offline['retrieval']['tokenizer'])
        self.assertIsNone(offline['retrieval']['model_tokens'])
        self.assertFalse(offline['retrieval']['independent_holdout'])
        for row in offline['retrieval']['summaries']:
            self.assertIn(row['family_count'], [4,8])
            self.assertEqual(row['task_count'], row['family_count']*4)
            for metric in row['metrics'].values():
                self.assertLessEqual(metric['families_observed'], row['family_count'])
                self.assertLessEqual(metric['tasks_observed'], row['task_count'])

    def test_no_automatic_adoption_or_hidden_private_payload(self):
        changed = copy.deepcopy(self.presentation)
        changed['adoption']['settings_changed'] = True
        with self.assertRaises(ValidationError):
            self.validator.validate(changed)
        changed = copy.deepcopy(self.presentation)
        changed['expert']['raw_conversation'] = 'fixture'
        with self.assertRaises(ValidationError):
            self.validator.validate(changed)
        self.assertEqual(self.presentation['adoption'], {'policy':'proposal_only','settings_changed':False,'winner':None})


if __name__ == '__main__':
    unittest.main(verbosity=2)
