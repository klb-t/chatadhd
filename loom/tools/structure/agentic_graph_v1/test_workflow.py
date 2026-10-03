"""Scripted transport tests: no paid pilot, no model-quality claim."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest

from . import packet as codec, workflow
from .test_packet import fixture, entity, MODEL, AUTO, PREVIEW


def method(stages=2, policy=AUTO):
    return {'schema': 'loom.graph_packet_workflow/1', 'id': 'scripted-test',
            'stages': [{'id': f'stage-{i}', 'operation': 'complete_graph' if i == 1 else 'critique_graph',
                        'model': 'scripted.test', 'recipe': 'source-review-v1', 'apply_policy': deepcopy(policy),
                        'on_failure': 'stop', 'context': 'current'} for i in range(1, stages + 1)],
            'authorization': {'paid_live': False, 'usage_jump_confirmed': False, 'reason': 'offline scripted mechanism'},
            'resources': {'expected_usage_multiplier': 1, 'confirmation_multiplier': 10, 'packet_limits': None}}


def response(request, *, changed=True):
    packet = request['packet']
    diff = codec.empty_diff(packet, proposal_id='proposal-' + request['stage']['id'], origin=MODEL)
    if changed:
        after = deepcopy(packet['entities'][0]); after['label'] += ' reviewed'
        diff['entities']['update'] = [{'id': after['id'], 'before_sha256': codec.digest(packet['entities'][0]), 'after': after}]
    return {'content': diff, 'origin': dict(MODEL, response_sha256='c' * 64),
            'known_at': '2026-09-30T12:20:00Z', 'accounting': {'source': 'scripted', 'paid_cost_usd': 0}}


class WorkflowTests(unittest.TestCase):
    def test_two_stage_history_and_first_proposals_preserved_without_forced_consensus(self):
        original = fixture(); calls = []
        def transport(request):
            calls.append(deepcopy(request)); return response(request)
        result = workflow.run_workflow(original, method(), transport)
        self.assertEqual(result['planned_stages'], 2); self.assertEqual(result['completed_stages'], 2)
        self.assertEqual(calls[1]['packet']['entities'][0]['label'], 'lampa reviewed')
        self.assertEqual(len(calls[1]['prior_proposals']), 1)
        self.assertEqual(result['selected_packet']['entities'][0]['label'], 'lampa reviewed reviewed')
        self.assertEqual(len(result['selected_packet']['history']), 2)
        self.assertEqual(original, fixture()); self.assertFalse(result['model_quality_measured'])

    def test_three_stage_and_mixed_model_fields_are_data(self):
        plan = method(3); plan['stages'][1]['model'] = 'model.other'; plan['stages'][2]['operation'] = 'new_unlisted_operation'
        calls = []
        def transport(request):
            calls.append(request); return response(request, changed=False)
        result = workflow.run_workflow(fixture(), plan, transport)
        self.assertEqual(result['completed_stages'], 3)
        self.assertEqual(calls[1]['stage']['model'], 'model.other')
        self.assertEqual(calls[2]['stage']['operation'], 'new_unlisted_operation')

    def test_preview_does_not_apply_and_reviewer_still_sees_first_unaccepted_proposal(self):
        plan = method(policy=PREVIEW); calls = []
        def transport(request):
            calls.append(request); return response(request)
        result = workflow.run_workflow(fixture(), plan, transport)
        self.assertEqual(result['selected_packet'], fixture())
        self.assertEqual(calls[1]['packet'], fixture())
        self.assertFalse(calls[1]['prior_proposals'][0]['accepted'])
        self.assertEqual(calls[1]['prior_proposals'][0]['model_proposed_diff']['entities']['update'][0]['after']['label'], 'lampa reviewed')

    def test_instrument_binding_only_changes_top_level_diff_origin_and_availability(self):
        result = workflow.run_workflow(fixture(), method(1), response)
        row = result['stages'][0]; raw, bound = row['model_proposed_diff'], row['instrument_bound_diff']
        self.assertIsNone(raw['known_at']); self.assertEqual(bound['known_at'], '2026-09-30T12:20:00Z')
        self.assertEqual(raw['entities'], bound['entities']); self.assertEqual(raw['claims'], bound['claims'])
        self.assertNotEqual(raw['origin']['response_sha256'], bound['origin']['response_sha256'])
        self.assertEqual(result['selected_packet']['sources'][0]['known_at'], '2026-09-30T12:00:00Z')

    def test_malformed_first_response_saved_before_parsing_no_retry(self):
        calls = []
        def transport(request):
            calls.append(request); return {'content': '{broken', 'origin': MODEL, 'known_at': None, 'accounting': {}}
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / 'first'
            result = workflow.run_workflow(fixture(), method(3), transport, output_dir=folder)
            self.assertEqual(len(calls), 1); self.assertEqual(result['planned_stages'], 3)
            self.assertEqual(result['attempted_stages'], 1); self.assertEqual(result['completed_stages'], 0)
            self.assertIn('{broken', (folder / '0001.first_transport_response.json').read_text())
            self.assertTrue((folder / 'result_first.json').is_file())
            with self.assertRaises(FileExistsError): workflow.run_workflow(fixture(), method(), transport, output_dir=folder)

    def test_continue_policy_preserves_failure_and_planned_denominator(self):
        plan = method(2); plan['stages'][0]['on_failure'] = 'continue'; calls = []
        def transport(request):
            calls.append(request)
            return {'bad': True} if len(calls) == 1 else response(request)
        result = workflow.run_workflow(fixture(), plan, transport)
        self.assertEqual(result['attempted_stages'], 2); self.assertEqual(result['completed_stages'], 1)
        self.assertEqual(result['stages'][0]['first_transport_response'], {'bad': True})

    def test_paid_auth_and_usage_jump_confirmations_are_config_data_checked_before_transport(self):
        calls = []
        def transport(request):
            calls.append(request); return response(request)
        transport.paid_live = True
        plan = method(1)
        with self.assertRaises(ValueError): workflow.run_workflow(fixture(), plan, transport)
        self.assertFalse(calls)
        plan['authorization']['paid_live'] = True
        workflow.run_workflow(fixture(), plan, transport); self.assertEqual(len(calls), 1)
        plan['resources']['expected_usage_multiplier'] = 10
        with self.assertRaises(ValueError): workflow.run_workflow(fixture(), plan, transport)
        self.assertEqual(len(calls), 1)
        plan['authorization']['usage_jump_confirmed'] = True
        workflow.run_workflow(fixture(), plan, transport); self.assertEqual(len(calls), 2)

    def test_independent_original_projection_is_selected_explicitly_never_merged(self):
        plan = method(2); plan['stages'][1]['context'] = 'original'
        result = workflow.run_workflow(fixture(), plan, response)
        self.assertEqual(result['selected_packet']['entities'][0]['label'], 'lampa reviewed')
        self.assertEqual(len(result['selected_packet']['history']), 1)
        self.assertEqual(len(result['stages']), 2)
        self.assertNotEqual(result['stages'][0]['application_receipt']['after_packet_sha256'],
                            result['stages'][1]['application_receipt']['after_packet_sha256'])

    def test_source_bytes_native_claim_fields_and_unknown_times_preserved(self):
        result = workflow.run_workflow(fixture(), method(3), response)
        final = result['selected_packet']
        self.assertEqual(final['sources'], fixture()['sources']); self.assertEqual(final['claims'], fixture()['claims'])
        self.assertIsNone(final['provenance']['claims']['cl_test']['known_at'])
        self.assertFalse(result['canonical_store_written']); self.assertFalse(result['acceptance_establishes_content_truth'])


if __name__ == '__main__':
    unittest.main()
