"""Stage 3/4 evidence and adaptation checks; all outputs are authored controls."""
from copy import deepcopy
from decimal import Decimal
import hashlib
import unittest

import frontier_reply_followup_v1 as followup


class FrontierReplyFollowupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = followup.load_config()
        cls.stage3 = followup.prepare_stage3(cls.config)
        cls.stage4 = followup.prepare_stage4(cls.config)

    def test_small_stage3_preserves_matched_inputs_and_external_recipe_data(self):
        bundle = self.stage3
        self.assertEqual(bundle['manifest']['request_count'], 12)
        self.assertEqual(bundle['results']['matched_pairs'], 6)
        self.assertEqual(bundle['results']['mechanically_valid'], 12)
        self.assertIsNone(bundle['results']['model_semantic_quality'])
        self.assertLess(Decimal(bundle['manifest']['cost_plan']['historical_reservation_total_usd']), Decimal('2'))
        for request in bundle['manifest']['requests']:
            self.assertEqual(request['messages'][0]['content'], self.config['stage3']['recipes'][request['track']])
            self.assertEqual(request['model']['generation_parameters']['max_tokens'], 2048)

    def test_stage4_reuses_real_compiler_and_preserves_sources(self):
        results = self.stage4['results']
        self.assertEqual((results['requests'], results['matched_pairs'], results['mechanically_valid']), (12, 6, 12))
        self.assertTrue(all(row['same_rendered_text'] for row in results['pairs']))
        self.assertTrue(all(row['existing_records_preserved'] for row in results['rows']))
        self.assertTrue(all(row['semantic_links_status'] == 'draft_not_validated_logical_claims' for row in results['rows']))
        self.assertTrue(all(row['model_semantic_quality'] is None for row in results['rows']))
        self.assertTrue(all(not row['provider_schema_measured'] for row in results['rows']))
        self.assertTrue(all(row['instrument_model'] == 'scripted:authored-synthetic-reference' for row in results['rows']))
        self.assertTrue(all(row['instrument_model'] != row['configured_model'] for row in results['rows']))
        self.assertTrue(all(row['source_transformation_verified'] for row in results['rows']))
        for row in results['rows']:
            self.assertEqual(row['compiler_input_is_original_response'], row['arm'] == 'graph_v2')
        self.assertTrue(any(span['byte_len'] > span['char_len'] for row in results['rows'] for span in row['spans'].values()))

    def test_text_json_public_text_must_match_annotation_and_first_bytes_survive(self):
        manifest = self.stage4['manifest']; responses = deepcopy(self.stage4['responses'])
        index = next(i for i, row in enumerate(manifest['requests']) if row['arm']['representation'] == 'text_plus_json')
        compiler = followup.source_module(self.config['sources']['reply_compiler'])
        wire = followup.json.loads(followup._decode_capture(responses[index]['raw_capture']))
        wire['text'] = 'A mismatching public answer.'
        responses[index]['raw_capture'] = compiler.capture_response(followup.frontier.canonical(wire))
        before = deepcopy(responses[index])
        results = followup.score_stage4(manifest, responses, self.config)
        self.assertFalse(results['rows'][index]['mechanical_validity'])
        self.assertEqual(results['rows'][index]['error'], 'followup_text_annotation_mismatch')
        self.assertTrue(results['rows'][index]['first_capture_retained'])
        self.assertEqual(responses[index], before)

    def test_invalid_graph_child_is_not_repaired_and_keeps_capture(self):
        responses = deepcopy(self.stage4['responses'])
        compiler = followup.source_module(self.config['sources']['reply_compiler'])
        wire = followup.json.loads(followup._decode_capture(responses[0]['raw_capture']))
        wire['nodes'][0]['children'] = ['nonexistent']
        responses[0]['raw_capture'] = compiler.capture_response(followup.frontier.canonical(wire))
        result = followup.score_stage4(self.stage4['manifest'], responses, self.config)
        self.assertFalse(result['rows'][0]['mechanical_validity'])
        self.assertTrue(result['rows'][0]['first_capture_retained'])
        self.assertIn('unknown_child', result['rows'][0]['error'])

    def test_valid_changed_child_does_not_inherit_verified_scripted_lineage(self):
        responses = deepcopy(self.stage4['responses'])
        compiler = followup.source_module(self.config['sources']['reply_compiler'])
        wire = followup.json.loads(followup._decode_capture(responses[0]['raw_capture']))
        wire['nodes'][1]['text'] = 'Całkiem odmienny tekst bez udowodnionej transformacji. '
        responses[0]['raw_capture'] = compiler.capture_response(followup.frontier.canonical(wire))
        before = deepcopy(responses)
        result = followup.score_stage4(self.stage4['manifest'], responses, self.config)['rows'][0]
        self.assertTrue(result['mechanical_validity'])
        self.assertFalse(result['source_transformation_verified'])
        self.assertEqual(result['sample_origin'], 'unverified_scripted_transformation_claim')
        self.assertEqual(result['claimed_sample_origin'], 'scripted_transformation_of_separate_authored_synthetic_reference')
        self.assertEqual(result['instrument_model'], 'offline:unverified-capture')
        self.assertIsNone(result['model_semantic_quality'])
        self.assertEqual(responses, before)

    def test_optional_v1_arm_and_large_generation_parameters_are_configuration(self):
        config = deepcopy(self.config)
        stage = config['stage4']; stage['selection'] = stage['selection'][:1]; stage['models'] = stage['models'][:1]
        arm = deepcopy(stage['arms'][0]); arm.update(key='graph_v1_control', method_id='research.reply_graph_v1/1',
            wire_schema='loom.graph_reply/1', compiler_wire_schema='loom.graph_reply/1', schema_name='loom_graph_reply_v1')
        stage['arms'].append(arm)
        stage['generation_parameters']['max_tokens'] = 10**12
        stage['runtimes'][0]['agents'] = 10**12
        result = followup.prepare_stage4(config)
        self.assertEqual(result['results']['requests'], 3)
        self.assertEqual(result['results']['mechanically_valid'], 3)
        self.assertTrue(all(not row['historical_capability']['output_within_historical_capability'] for row in result['manifest']['requests']))
        self.assertTrue(all(row['runtime']['agents'] == 10**12 for row in result['manifest']['requests']))

    def test_historical_model_sample_cannot_be_relabelled_as_authored_synthetic(self):
        config = deepcopy(self.config); config['scripted_references']['sample_origin'] = 'in-session-model-pilot'
        with self.assertRaisesRegex(ValueError, 'scripted_source_origin_mismatch'):
            followup.prepare_stage4(config)

    def test_git_contract_hash_and_capture_hash_drift_are_rejected(self):
        binding = deepcopy(self.config['sources']['reply_compiler']); binding['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'git_source_hash_drift'):
            followup.source_module(binding)
        responses = deepcopy(self.stage4['responses']); responses[0]['raw_capture']['sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'first_capture_hash_drift'):
            followup.score_stage4(self.stage4['manifest'], responses, self.config)
        binding = deepcopy(self.config['sources']['reply_compiler']); binding['commit'] = 'origin/gpt/graph-replies-2026-10-02'
        with self.assertRaisesRegex(ValueError, 'immutable_commit_required'):
            followup.git_bytes(binding)
        responses = deepcopy(self.stage4['responses'])
        compiler = followup.source_module(self.config['sources']['reply_compiler'])
        responses[0]['parent_raw_capture'] = compiler.capture_response(b'{"unrelated":true}')
        with self.assertRaisesRegex(ValueError, 'parent_capture_source_drift'):
            followup.score_stage4(self.stage4['manifest'], responses, self.config)

    def test_optional_prompt_format_prices_exact_body_without_injected_schema(self):
        config = deepcopy(self.config); stage = config['stage4']
        stage['selection'] = stage['selection'][:1]; stage['models'] = stage['models'][:1]
        for arm in stage['arms']:
            arm['format_mode'] = 'prompt'
        bundle = followup.prepare_stage4(config)
        for row in bundle['manifest']['requests']:
            self.assertNotIn('response_format', row['body'])
            reservation = row['historical_reservation']
            self.assertEqual(reservation['exact_request_body_sha256'], followup.frontier.digest(row['body']))
            self.assertEqual(reservation['prompt_token_allowance'], len(followup.frontier.canonical(row['body'])) + 1088)

    def test_request_and_config_drift_do_not_change_results(self):
        manifest = deepcopy(self.stage4['manifest']); manifest['requests'][0]['body']['model'] = 'another-model'
        with self.assertRaisesRegex(ValueError, 'request_grid_configuration_drift'):
            followup.score_stage4(manifest, self.stage4['responses'], self.config)
        config = deepcopy(self.config); config['stage4']['generation_parameters']['max_tokens'] += 1
        with self.assertRaisesRegex(ValueError, 'configuration_drift'):
            followup.score_stage4(self.stage4['manifest'], self.stage4['responses'], config)

    def test_duplicate_arm_cannot_replace_the_expected_matched_configuration(self):
        manifest = deepcopy(self.stage4['manifest']); responses = deepcopy(self.stage4['responses'])
        first, second = manifest['requests'][:2]
        second['arm'] = deepcopy(first['arm']); second['body'] = deepcopy(first['body'])
        second['request_sha256'] = followup.frontier.digest({key: value for key, value in second.items() if key != 'request_sha256'})
        compiler = followup.source_module(self.config['sources']['reply_compiler'])
        responses[1] = followup.scripted_stage4(second, compiler, self.config['scripted_references']['samples'])
        with self.assertRaisesRegex(ValueError, 'request_grid_configuration_drift'):
            followup.score_stage4(manifest, responses, self.config)

    def test_method_descriptors_bind_version_parameters_preset_and_recipes(self):
        rows = followup.method_descriptors(self.config, (self.stage3, self.stage4))
        self.assertEqual(len(rows), 12)
        self.assertEqual({row['method_id'] for row in rows}, {'research.frontier_completion/1',
            'research.frontier_pattern_discovery/1', 'research.reply_graph_v2/1', 'research.reply_text_json/1'})
        for row in rows:
            self.assertEqual(row['metrics']['evidence_class'], 'scripted_mechanics')
            self.assertIsNone(row['metrics']['model_semantic_quality'])
            self.assertEqual(row['preset']['sha256'], followup.frontier.digest(row['preset']['values']))
            self.assertEqual(row['recipe_sha256'], hashlib.sha256(row['recipe_material'].encode()).hexdigest())
            self.assertEqual(len(row['prepared_requests']), 2)
            self.assertIn(row['parameters']['model'], {'openai/gpt-6.1-sol', 'anthropic/claude-sonnet-5.5', 'google/gemini-3.1-pro-preview'})
            self.assertIsNotNone(row['parameters']['provider'])
        self.assertEqual(rows[-1]['components'][-1]['method_id'], 'research.text_json_to_graph_reply/1')

    def test_euro_budget_is_separate_and_not_fabricated_usd_equivalence(self):
        self.assertEqual(self.config['campaign_budget']['currency'], 'EUR')
        self.assertEqual(self.config['campaign_budget']['key_limit_eur'], '5')
        self.assertIsNone(self.config['campaign_budget']['usd_to_eur'])
        self.assertEqual(self.stage4['results']['paid_calls'], 0)
        self.assertFalse(self.stage4['manifest']['cost_plan']['current_price_quote'])


if __name__ == '__main__':
    unittest.main()
