"""A2-C-003 exact request association; controlled fixtures, zero provider calls."""
from copy import deepcopy
import json
import unittest

from loom.tools.structure import experiment_payer_boundary_v1 as boundary
from loom.tools.structure import experiment_workflow_v1 as workflow
from loom.tools.structure.test_experiment_workflow_v1 import spec
import test_research_programme_runner as fixtures

payer = boundary.payer


class PayerRequestBindingTests(unittest.TestCase):
    def setUp(self):
        self.f = fixtures.ProgrammeRunnerTests('test_exact_bytes_durable_reserve_receipts_and_usage')
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)
        self.queue = workflow.Queue(self.f.root / 'restored.sqlite')
        self.addCleanup(self.queue.db.close)
        self.adapter = boundary.PayerBoundary(
            self.queue, self.f.private, self.f.repo, self.f.policy['programme_id'],
            payer.transport.key_fingerprint('fixture-credential-not-a-real-key'),
            payer.sha(self.f.manifest.read_bytes()))

    def job(self, operation='one'):
        body = json.loads(self.f.body)
        job = next(workflow.ordered_jobs(spec()))
        job.update(operation_id=operation, request_sha256=payer.sha(self.f.body), request=body,
                   requested_model=body['model'], requested_provider=deepcopy(body['provider']))
        return job

    def complete(self):
        self.assertEqual(payer.run_stage(self.f.policy, self.f.manifest, self.f.evidence,
            self.f.private, self.f.keyfile, self.f.repo, transport_fn=self.f.send)['status'], 'completed')
        self.f.send.calls.clear()

    def import_pending(self, job):
        self.queue.add([job])
        self.queue.mark_dispatched(job['operation_id'], 'controlled-legacy-offline-import')

    def assert_unchanged_rejection(self, reason):
        before = self.queue.snapshot()
        evidence = {x['job']['operation_id']: self.queue.evidence(x['job']['operation_id']) for x in before}
        files = {p.name: p.read_bytes() for p in (self.f.private / 'records').iterdir()}
        with self.assertRaisesRegex(ValueError, reason):
            self.adapter.sync_verified()
        self.assertEqual(self.queue.snapshot(), before)
        self.assertEqual({k:self.queue.evidence(k) for k in evidence}, evidence)
        self.assertEqual({p.name:p.read_bytes() for p in (self.f.private / 'records').iterdir()}, files)
        self.assertEqual(self.f.send.calls, [])

    def test_valid_original_serialization_import_and_idempotent_sync(self):
        self.complete()
        job = self.job()
        # Preserve the payer hash despite irrelevant whitespace/key ordering.
        self.assertNotEqual(workflow.digest(job['request']), job['request_sha256'])
        self.import_pending(job)
        self.adapter.sync_verified()
        first = self.queue.snapshot()
        self.assertEqual(first[0]['state'], 'captured')
        self.assertTrue(first[0]['result']['billing_verified'])
        self.adapter.sync_verified()
        self.assertEqual(self.queue.snapshot(), first)
        self.assertEqual(len(self.queue.evidence('one')), 1)
        self.assertEqual(self.f.send.calls, [])

    def test_same_operation_different_request_hash_cannot_capture(self):
        self.complete()
        job = self.job()
        job['request']['max_tokens'] = 11
        job['request_sha256'] = workflow.digest(job['request'])
        self.import_pending(job)
        self.assert_unchanged_rejection('payer_queue_request_hash_mismatch')

    def test_stale_hash_does_not_hide_changed_request_body(self):
        self.complete()
        job = self.job()
        job['request']['messages'] = [{'role':'user','content':'controlled-fixture'}]
        self.import_pending(job)
        self.assert_unchanged_rejection('payer_queue_request_body_mismatch')

    def test_json_number_type_change_is_not_silently_relabelled(self):
        self.complete()
        job = self.job()
        job['request']['max_tokens'] = 10.0
        self.import_pending(job)
        self.assert_unchanged_rejection('payer_queue_request_body_mismatch')

    def test_requested_model_cannot_relabel_payer_model(self):
        self.complete()
        job = self.job()
        job['requested_model'] = 'fixture/other'
        self.import_pending(job)
        self.assert_unchanged_rejection('payer_queue_model_mismatch')

    def test_requested_provider_configuration_cannot_drop_parameters(self):
        self.complete()
        job = self.job()
        job['requested_provider'] = {'only':['fake']}
        self.import_pending(job)
        self.assert_unchanged_rejection('payer_queue_provider_mismatch')

    def test_route_cannot_relabel_payer_route(self):
        self.complete()
        job = self.job()
        job['route_id'] = 'jev'
        self.import_pending(job)
        self.assert_unchanged_rejection('payer_queue_route_mismatch')

    def test_manifest_mismatch_does_not_capture(self):
        self.complete()
        self.import_pending(self.job())
        self.adapter.manifest = 'a' * 64
        self.assert_unchanged_rejection('sync_manifest_mismatch')

    def test_later_mismatch_prevents_partial_batch_projection(self):
        self.f.two_operations()
        self.adapter.manifest = payer.sha(self.f.manifest.read_bytes())
        self.complete()
        first, second = self.job(), self.job('two')
        second['request_sha256'] = 'b' * 64
        self.import_pending(first)
        self.import_pending(second)
        self.assert_unchanged_rejection('payer_queue_request_hash_mismatch')

    def test_preexisting_wrong_first_result_is_not_rewritten(self):
        self.complete()
        job = self.job()
        job['request_sha256'] = 'b' * 64
        self.import_pending(job)
        self.queue.capture_first('one', workflow.result_record(job, b'controlled-old-evidence', {}))
        self.assert_unchanged_rejection('payer_queue_request_hash_mismatch')

    def test_dispatch_checks_full_request_body_before_controlled_post(self):
        job = self.job()
        job['request']['max_tokens'] = 11
        self.queue.add([job])
        self.queue.claim('fixture-worker', 'one')
        result = payer.run_stage(self.f.policy, self.f.manifest, self.f.evidence,
            self.f.private, self.f.keyfile, self.f.repo,
            transport_fn=self.adapter.transport(self.f.send, controlled_transport=True))
        self.assertEqual(result['status'], 'stopped')
        self.assertEqual(self.f.send.posts(), [])
        self.assertEqual(self.queue.snapshot()[0]['state'], 'claimed')
        self.assertEqual(self.queue.evidence('one'), [])
        self.assertNotEqual(result['unresolved_reservations_usd'], '0')

    def test_valid_verified_dispatch_still_reaches_controlled_transport_once(self):
        self.queue.add([self.job()])
        self.queue.claim('fixture-worker', 'one')
        result = payer.run_stage(self.f.policy, self.f.manifest, self.f.evidence,
            self.f.private, self.f.keyfile, self.f.repo,
            transport_fn=self.adapter.transport(self.f.send, controlled_transport=True))
        self.assertEqual(result['status'], 'completed')
        self.assertEqual(len(self.f.send.posts()), 1)
        self.adapter.sync_verified()
        self.assertTrue(self.queue.snapshot()[0]['result']['billing_verified'])


if __name__ == '__main__':
    unittest.main()
