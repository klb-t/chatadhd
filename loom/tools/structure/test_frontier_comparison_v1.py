"""DEV counterexamples: no paid model calls or independent quality claims."""
from copy import deepcopy
from decimal import Decimal
import tempfile
from pathlib import Path
import unittest

import frontier_comparison_v1 as comparison


class FrontierComparisonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Build and validate the common full DEV preparation once. Each test
        # receives independent copies; counterexamples still run real scoring
        # and the preparation-specific tests still call the actual builder.
        cls._base_cases, cls._base_references = comparison.synthetic_cases()
        cls._base_manifest = comparison.prepare(
            cls._base_cases, comparison.default_config(), references=cls._base_references)

    def setUp(self):
        self.cases = deepcopy(self._base_cases)
        self.references = deepcopy(self._base_references)
        self.manifest = deepcopy(self._base_manifest)

    def request(self, case_id='repeat', track='graph_completion'):
        return next(x for x in self.manifest['requests'] if x['case_id'] == case_id and x['track'] == track)

    def response(self, case_id='repeat', track='graph_completion'):
        req = self.request(case_id, track)
        return comparison.scripted_response(req, self.references[case_id])

    def score(self, request, response):
        return comparison.score_response(request, response, self.references[request['case_id']])

    @staticmethod
    def rehash(response):
        response['content_sha256'] = comparison.digest(response['content'])
        return response

    def test_matched_inputs_references_excluded_and_mechanics_not_model_quality(self):
        responses = [comparison.scripted_response(r, self.references[r['case_id']])
                     for r in self.manifest['requests']]
        result = comparison.replay_bundle(self.manifest, responses, self.references)
        self.assertEqual((result['requests'], result['matched_pairs'], result['mechanically_valid']), (36, 18, 36))
        self.assertEqual(result['scripted_reference_agreement'], 36)
        self.assertIsNone(result['model_semantic_quality'])
        for request in self.manifest['requests']:
            self.assertNotIn('completion_claims', request)
            self.assertNotIn('pattern_claim_sets', request)
            self.assertNotIn('right-second-claim', request['messages'][1]['content'])
        self.assertTrue(all(row['source_bytes_preserved'] for row in result['rows']))
        self.assertTrue(all(row['reference_hash_verified'] for row in result['rows']))
        self.assertEqual(result['paid_calls'], 0)

    def test_polarity_scope_and_direction_are_rejected_as_recurring_witnesses(self):
        for case_id in ('polarity', 'scope', 'direction'):
            request = self.request(case_id, 'pattern_discovery')
            response = self.response(case_id, 'pattern_discovery')
            fake = comparison._pattern_response(request['input']['packet'], self.references['repeat'])
            response['content'] = fake
            with self.assertRaisesRegex(ValueError, 'witness_mismatch'):
                self.score(request, self.rehash(response))

    def test_shared_source_occurrences_do_not_become_independent_support(self):
        request = self.request('shared_source', 'pattern_discovery')
        result = self.score(request, self.response('shared_source', 'pattern_discovery'))
        self.assertEqual(result['witnessed_occurrences'], 2)
        self.assertEqual(result['declared_independent_source_groups'], [1])
        self.assertFalse(result['source_group_independence_verified'])

    def test_duplicate_occurrences_and_parallel_edge_reuse_are_rejected(self):
        request = self.request('repeat', 'pattern_discovery')
        response = self.response('repeat', 'pattern_discovery')
        pattern = response['content']['patterns'][0]
        pattern['occurrences'].append(deepcopy(pattern['occurrences'][0]))
        with self.assertRaisesRegex(ValueError, 'duplicate_occurrence'):
            self.score(request, self.rehash(response))
        response = self.response('repeat', 'pattern_discovery')
        pattern = response['content']['patterns'][0]
        pattern['edges'].append(deepcopy(pattern['edges'][0]))
        for occurrence in pattern['occurrences']:
            occurrence['claim_ids'] *= 2
        with self.assertRaisesRegex(ValueError, 'noninjective_or_incomplete'):
            self.score(request, self.rehash(response))

    def test_abstraction_requires_loss_and_extra_semantics_are_not_silently_erased(self):
        request = self.request('repeat', 'pattern_discovery')
        response = self.response('repeat', 'pattern_discovery')
        response['content']['transformation_report']['loss'] = []
        with self.assertRaisesRegex(ValueError, 'abstraction_undeclared'):
            self.score(request, self.rehash(response))
        response = self.response('repeat', 'pattern_discovery')
        response['content']['patterns'][0]['nodes'][0]['polarity'] = 'negative'
        with self.assertRaisesRegex(ValueError, 'semantics_unrepresented'):
            self.score(request, self.rehash(response))

    def test_quote_hash_and_request_binding_drift_cannot_pass(self):
        request = self.request()
        response = self.response()
        response['content']['diff']['claims']['add'][0]['assessment']['basis']['support'][0]['quote'] = 'fabricated'
        with self.assertRaisesRegex(ValueError, 'quote_mismatch'):
            self.score(request, self.rehash(response))
        response = self.response(); response['input_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'response_binding_drift'):
            self.score(request, response)
        request = deepcopy(request); request['messages'][1]['content'] += ' changed'
        with self.assertRaisesRegex(ValueError, 'request_hash_or_input_drift'):
            self.score(request, self.response())

    def test_recorded_response_cannot_impersonate_recorded_instrument_origin(self):
        request = self.request(); response = self.response()
        response['response_origin'] = 'recorded'; response['captured_model'] = 'captured-model-alias'
        response['content']['diff']['origin'] = deepcopy(comparison.RECORDED)
        before = deepcopy(response['content'])
        result = self.score(request, self.rehash(response))
        self.assertEqual(result['instrument_origin']['kind'], 'model')
        self.assertEqual(result['raw_proposed_origin']['kind'], 'recorded')
        self.assertFalse(result['native_assessments_changed_by_binding'])
        self.assertEqual(response['content'], before)
        self.assertIsNone(result['model_semantic_quality'])
        self.assertFalse(result['captured_provider_identity_verified'])

    def test_completion_wrong_reference_remains_mechanically_valid_not_quality_success(self):
        request = self.request(); response = self.response()
        response['content']['diff']['claims']['add'] = []
        response['content']['transformation_report']['augmentation'] = []
        result = self.score(request, self.rehash(response))
        self.assertTrue(result['mechanical_validity'])
        self.assertFalse(result['reference_agreement'])
        self.assertIsNone(result['model_semantic_quality'])

    def test_deleting_existing_relation_needs_declared_loss_and_fails_whole_target(self):
        request = self.request(); response = self.response()
        old = request['input']['packet']['claims'][0]
        response['content']['diff']['claims']['remove'].append({'id': old['id'],
            'before_sha256': comparison.digest(old), 'reason': 'owner-selected alternate view'})
        with self.assertRaisesRegex(ValueError, 'actual_edit_loss_or_augmentation_undeclared'):
            self.score(request, self.rehash(response))
        response['content']['transformation_report']['loss'].append({'collection': 'claims',
            'id': old['id'], 'action': 'remove', 'reason': 'current projection omits an existing relation'})
        result = self.score(request, self.rehash(response))
        self.assertTrue(result['mechanical_validity'])
        self.assertFalse(result['existing_records_preserved'])
        self.assertFalse(result['reference_agreement'])

    def test_historical_primary_snapshot_identity_reservation_and_capability_are_frozen(self):
        config = comparison.historical_config()
        manifest = comparison.prepare(self.cases, config)
        self.assertEqual(manifest['request_count'], 36)
        self.assertEqual({x['model'] for x in config['models']}, {'openai/gpt-6.1-sol',
            'anthropic/claude-sonnet-5.5', 'google/gemini-3.1-pro-preview'})
        self.assertGreater(Decimal(manifest['cost_plan']['historical_reservation_total_usd']), Decimal(0))
        self.assertFalse(manifest['cost_plan']['billing_bound_guaranteed'])
        for request in manifest['requests']:
            plan = request['historical_openrouter_plan']
            self.assertEqual(plan['capability']['unsupported_body_parameters'], [])
            self.assertFalse(plan['capability']['current_endpoint_capability_verified'])
        config['models'][0]['historical_endpoint_evidence']['endpoint']['pricing']['prompt'] = '0'
        with self.assertRaisesRegex(ValueError, 'evidence_projection_drift'):
            comparison.prepare(self.cases, config)
        config = comparison.historical_config()
        config['models'][0]['generation_parameters']['model'] = 'another-identity'
        with self.assertRaisesRegex(ValueError, 'overwrites_execution_identity_or_source'):
            comparison.prepare(self.cases, config)
        config = comparison.historical_config()
        config['models'][0]['historical_endpoint_evidence']['preset_sha256'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'preset_evidence_drift'):
            comparison.prepare(self.cases[:1], config)
        config = comparison.historical_config()
        config['models'][0]['historical_endpoint_evidence']['retrieved_at'] = '2099-01-01T00:00:00Z'
        with self.assertRaisesRegex(ValueError, 'preset_identity_or_date_drift'):
            comparison.prepare(self.cases[:1], config)

    def test_existing_first_response_bytes_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'first-response.json'
            comparison.write(path, {'first': 'retained bytes'})
            first = path.read_bytes()
            with self.assertRaises(FileExistsError):
                comparison.write(path, {'later': 'overwrite attempt'})
            self.assertEqual(path.read_bytes(), first)

    def test_preparation_existing_directory_is_not_restarted(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileExistsError):
                comparison.main(['prepare', '--output', directory])

    def test_inferred_claim_requires_native_derivation_and_expected_property(self):
        request = self.request(); response = self.response()
        claim = response['content']['diff']['claims']['add'][0]
        claim['assessment']['evidence_class'] = 'inferred'
        with self.assertRaisesRegex(ValueError, 'derived_operator_required'):
            self.score(request, self.rehash(response))

    def test_model_reasoning_runtime_and_agent_counts_have_no_invented_ceiling(self):
        config = comparison.default_config()
        config['models'][0]['generation_parameters']['max_output_tokens'] = 10**12
        config['models'][0]['reasoning'] = {'custom_strategy': 'owner-specified', 'max_tokens': 10**15}
        config['recipes']['graph_completion'] = 'An owner-selected experimental recipe.'
        config['runtimes'][0].update(agents=10**12, kind='owner-runtime', parameters={'max_steps': None})
        manifest = comparison.prepare(self.cases, config)
        self.assertEqual(manifest['requests'][0]['runtime']['agents'], 10**12)
        self.assertFalse(manifest['requests'][0]['execution']['identity_and_current_capability_verified'])
        self.assertEqual(manifest['request_count'], 36)
        self.assertEqual(manifest['requests'][0]['messages'][0]['content'], 'An owner-selected experimental recipe.')

    def test_configured_resource_limit_and_token_cost_assumptions_are_explicit(self):
        config = comparison.default_config(); config['resource_limits'] = {'max_string_bytes': 1}
        with self.assertRaisesRegex(ValueError, 'configured_string_resource_limit'):
            comparison.prepare(self.cases, config)
        assumptions = {'input_tokens_per_call': 6000, 'output_tokens_per_call': 4096,
                       'usd_per_million_input': '3', 'usd_per_million_output': '15'}
        estimate = comparison.estimate_cost(36, assumptions)
        self.assertEqual(Decimal(estimate['estimated_usd']), Decimal('2.859840'))
        self.assertFalse(estimate['current_provider_price_claim'])
        self.assertIsNone(comparison.estimate_cost(36, None)['estimated_usd'])
        assumptions['usd_per_million_input'] = '1000000000000'
        self.assertGreater(Decimal(comparison.estimate_cost(36, assumptions)['estimated_usd']), Decimal(10**9))

    def test_inventory_missing_extra_and_response_duplicates_fail(self):
        responses = [comparison.scripted_response(r, self.references[r['case_id']])
                     for r in self.manifest['requests']]
        with self.assertRaisesRegex(ValueError, 'inventory_mismatch'):
            comparison.replay_bundle(self.manifest, responses[:-1], self.references)
        responses[-1] = responses[0]
        with self.assertRaisesRegex(ValueError, 'duplicate_response'):
            comparison.replay_bundle(self.manifest, responses, self.references)
        references = deepcopy(self.references); references['repeat']['completion_claims'] = []
        with self.assertRaisesRegex(ValueError, 'reference_bundle_drift'):
            comparison.replay_bundle(self.manifest, [], references)
        with self.assertRaisesRegex(ValueError, 'case_reference_drift'):
            comparison.score_response(self.request(), self.response(), references['repeat'])

    def test_duplicate_json_keys_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.json'
            path.write_text('{"meaning":1,"meaning":2}')
            with self.assertRaisesRegex(ValueError, 'duplicate_json_key'):
                comparison.read(path)


if __name__ == '__main__':
    unittest.main()
