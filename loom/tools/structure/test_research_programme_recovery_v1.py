"""Controlled transport/fixture accounting only; never reads owner conversations."""
from copy import deepcopy
from decimal import Decimal
import base64
import json
from pathlib import Path
import sqlite3
import unittest
from unittest.mock import patch

import research_programme_runner as payer
import research_programme_recovery_v1 as recovery
import test_research_programme_runner as fixtures


class Crash(BaseException):
    pass


class ExternalRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.f = fixtures.ProgrammeRunnerTests('test_exact_bytes_durable_reserve_receipts_and_usage')
        self.f.setUp()
        self.addCleanup(self.f.doCleanups)
        self.external = None
        self.first = {}

    def rows(self):
        with sqlite3.connect(self.f.private / 'ledger.sqlite3') as db:
            return [json.loads(x[0]) for x in db.execute('SELECT payload FROM attempts')]

    def event_rows(self):
        with sqlite3.connect(self.f.private / 'ledger.sqlite3') as db:
            return [json.loads(x[0]) for x in db.execute('SELECT payload FROM recovery_events ORDER BY rowid')]

    def crash(self, point='after_send'):
        reserve, write, finish = payer.PrivateLedger.reserve, payer.write_private, payer.PrivateLedger.finish
        def transport(method, route, body=None, params=None):
            response = self.f.send(method, route, body, params)
            if method == 'POST':
                row = self.rows()[0]
                self.external = {'schema': recovery.CAPTURE_SCHEMA,
                    **{k: row[k] for k in ('operation_id', 'programme_id', 'stage_id', 'manifest_sha256',
                       'key_fingerprint_sha256', 'request_sha256')}, 'route_id': route,
                    'request_base64': base64.b64encode(body).decode(), 'capture_origin': 'controlled_transport_fixture',
                    'captured_at': payer.utc().isoformat(), 'response': recovery.encode_response(response)}
                if point == 'after_send':
                    raise Crash()
            return response
        def reserve_fault(ledger, *args, **kw):
            row = reserve(ledger, *args, **kw)
            if point == 'after_reserve':
                raise Crash()
            return row
        def write_fault(path, raw):
            if str(path).endswith('.response.bin') and point == 'before_response_write':
                raise Crash()
            write(path, raw)
            if str(path).endswith('.response.bin') and point == 'after_response_write':
                raise Crash()
            if str(path).endswith('.result.json') and point == 'after_result_write':
                raise Crash()
        def finish_fault(ledger, row):
            if point == 'before_settlement':
                raise Crash()
            return finish(ledger, row)
        with patch.object(payer.PrivateLedger, 'reserve', reserve_fault), patch.object(payer, 'write_private', write_fault), \
                patch.object(payer.PrivateLedger, 'finish', finish_fault), self.assertRaises(Crash):
            payer.run_stage(self.f.policy, self.f.manifest, self.f.evidence, self.f.private,
                            self.f.keyfile, self.f.repo, transport_fn=transport)
        self.first = {p: p.read_bytes() for p in (self.f.private / 'records').iterdir()}
        self.original = self.rows()
        self.f.send.calls.clear()

    def recover(self, evidence=True, **kw):
        external = self.external if evidence else None
        review = {'external_capture_sha256': payer.sha(payer.canonical(external)),
                  'request_response_attribution_reviewed': True,
                  'reviewer_ref': 'fixture-reviewer', 'basis_ref': 'fixture-independent-transport-capture'} if external else None
        return recovery.recover(self.f.policy, self.f.manifest, self.f.private, self.f.keyfile, self.f.repo, 'one',
            external_evidence=external, capture_review=review, transport_fn=self.f.send, controlled_transport=True, **kw)

    def ledger(self):
        return payer.PrivateLedger(self.f.private, self.f.repo, self.f.policy['programme_id'],
            payer.transport.key_fingerprint('fixture-credential-not-a-real-key'))

    def immutable(self):
        self.assertEqual(self.rows(), self.original)
        self.assertTrue(all(p.read_bytes() == raw for p, raw in self.first.items()))
        self.assertEqual(self.f.send.posts(), [])

    def test_recover_five_real_crash_points_without_post_or_original_rewrite(self):
        for point in ('after_send', 'before_response_write', 'after_response_write', 'before_settlement', 'after_result_write'):
            with self.subTest(point=point):
                t = ExternalRecoveryTests(); t.setUp()
                try:
                    t.crash(point)
                    self.assertEqual(t.recover()['status'], 'resolved')
                    with t.ledger().locked() as ledger:
                        self.assertEqual(ledger.rows()[0]['actual_cost_usd'], '0.01')
                        self.assertEqual(ledger.response_bytes(ledger.rows()[0]), recovery.decode_response(t.external['response'])['raw'])
                    t.immutable()
                finally:
                    t.doCleanups()

    def test_missing_external_evidence_is_durable_unknown(self):
        self.crash('after_reserve')
        result = self.recover(False)
        self.assertEqual(result['status'], 'unknown')
        self.assertIsNone(result['actual_cost_usd'])
        self.assertEqual(self.event_rows()[-1]['reason'], 'external_capture_required')
        with self.assertRaises(payer.ProgrammeError):
            with self.ledger().locked():
                pass
        self.assertEqual(self.rows()[0]['reservation_usd'], '0.0220')
        self.assertEqual(self.f.send.calls, [])
        self.immutable()

    def test_generation_id_alone_cannot_attribute_request(self):
        self.crash()
        self.external.pop('request_base64')
        self.assertEqual(self.recover()['status'], 'unknown')
        self.assertEqual(self.f.send.calls, [])
        self.immutable()

    def test_external_request_model_campaign_and_generation_mismatch(self):
        self.crash()
        original = deepcopy(self.external)
        for name in ('request_sha256', 'manifest_sha256', 'key_fingerprint_sha256', 'programme_id', 'operation_id', 'route_id'):
            with self.subTest(name=name):
                self.external = {**original, name: 'other'}
                self.assertEqual(self.recover()['status'], 'unknown')
                self.assertEqual(self.f.send.calls, [])
        self.immutable()

    def test_capture_review_hash_and_attribution_are_required(self):
        self.crash()
        result = recovery.recover(self.f.policy, self.f.manifest, self.f.private, self.f.keyfile, self.f.repo, 'one',
            external_evidence=self.external, capture_review={'external_capture_sha256': 'a'*64},
            transport_fn=self.f.send, controlled_transport=True)
        self.assertEqual(result['status'], 'unknown')
        self.assertEqual(self.f.send.calls, [])

    def test_generation_wrong_model_provider_cost_byok_rejected(self):
        self.crash()
        original = {'id': 'gen-fake', 'model': 'fake/model', 'provider_name': 'Fake',
                    'api_type': 'completions', 'total_cost': '0.01', 'is_byok': False}
        for name, value in (('id', 'unrelated'), ('model', 'other/model'), ('provider_name', 'Other'),
                            ('api_type', 'decisions'), ('total_cost', '0.011'), ('is_byok', True)):
            with self.subTest(name=name):
                self.f.send.generation_override = {'raw': fixtures.wire({'data': {**original, name: value}})}
                self.assertEqual(self.recover()['status'], 'unknown')
                self.assertIsNone(self.rows()[0]['actual_cost_usd'])
        self.immutable()

    def test_unavailable_billing_appended_then_later_resolved(self):
        self.crash()
        self.f.send.generation_pending = 1
        self.assertEqual(self.recover()['status'], 'unknown')
        first_events = self.event_rows()
        self.assertEqual(first_events[-1]['reads'][0]['response']['http_status'], 404)
        self.assertEqual(self.recover()['status'], 'resolved')
        self.assertEqual(self.event_rows()[:len(first_events)], first_events)
        self.immutable()

    def test_partial_response_retains_unknown_and_bytes(self):
        self.f.send.response_override = {'transport_error': 'response_read_failed', 'raw': b'{"id":'}
        self.crash('after_response_write')
        self.assertEqual(self.recover()['status'], 'unknown')
        self.assertEqual(self.f.send.calls, [])
        self.immutable()

    def test_conflicting_first_response_cannot_be_replaced(self):
        self.crash('after_response_write')
        response = recovery.decode_response(self.external['response'])
        response['raw'] += b' '
        self.external['response'] = recovery.encode_response(response)
        self.assertEqual(self.recover()['status'], 'unknown')
        self.assertEqual(self.f.send.calls, [])
        self.immutable()

    def test_provider_usage_delta_is_not_guessed_fee(self):
        self.crash()
        self.f.send.key_usage_override = Decimal('0.02')
        result = self.recover()
        self.assertEqual(result['status'], 'unknown')
        self.assertEqual(result['reason'], 'recovery_campaign_usage_unreconciled')
        self.immutable()

    def test_duplicate_resolution_is_idempotent_without_even_get(self):
        self.crash()
        first = self.recover()
        count = len(self.event_rows())
        self.f.send.calls.clear()
        second = self.recover()
        self.assertEqual(second['status'], 'already_resolved')
        self.assertEqual(first['event_id'], second['resolution_id'])
        self.assertEqual(len(self.event_rows()), count)
        self.assertEqual(self.f.send.calls, [])

    def test_recovery_crash_restart_at_each_new_boundary(self):
        for point in ('after_generation_get', 'after_key_get', 'before_event_commit', 'within_event_commit', 'after_event_commit'):
            with self.subTest(point=point):
                t = ExternalRecoveryTests(); t.setUp()
                try:
                    t.crash()
                    def fault(where):
                        if where == point:
                            raise Crash()
                    with self.assertRaises(Crash):
                        t.recover(fault=fault)
                    first_events = t.event_rows()
                    self.assertTrue(first_events)
                    outcome = t.recover()
                    self.assertIn(outcome['status'], ('resolved', 'already_resolved'))
                    self.assertEqual(t.event_rows()[:len(first_events)], first_events)
                    t.immutable()
                finally:
                    t.doCleanups()

    def test_original_request_tamper_cannot_enter_recovery(self):
        self.crash()
        path = next((self.f.private/'records').glob('*.request.bin'))
        path.write_bytes(b'changed fixture')
        with self.assertRaisesRegex(payer.ProgrammeError, 'original_request_changed'):
            self.recover()
        self.assertEqual(self.f.send.calls, [])

    def test_missing_started_witness_is_not_fabricated(self):
        self.crash()
        path = next((self.f.private/'records').glob('*.started.json'))
        path.unlink()  # controlled damaged journal, not an authentic owner record
        self.first.pop(path)
        self.assertEqual(self.recover()['status'], 'resolved')
        self.assertFalse(path.exists())
        with self.ledger().locked():
            pass
        self.immutable()

    def test_tampered_projection_or_event_is_rejected_on_replay(self):
        self.crash()
        self.recover()
        with sqlite3.connect(self.f.private/'ledger.sqlite3') as db:
            proof = json.loads(db.execute('SELECT payload FROM attempt_resolutions').fetchone()[0])
            proof['projection']['actual_cost_usd'] = '0'
            db.execute('UPDATE attempt_resolutions SET payload=?', (payer.canonical(proof).decode(),))
        with self.assertRaisesRegex(payer.ProgrammeError, 'proof_replay_mismatch'):
            with self.ledger().locked():
                pass

    def test_tampered_saved_external_raw_is_rejected_on_replay(self):
        self.crash()
        self.recover()
        with sqlite3.connect(self.f.private/'ledger.sqlite3') as db:
            event_id, raw = db.execute('SELECT event_id,payload FROM recovery_events LIMIT 1').fetchone()
            event = json.loads(raw); event['external_evidence']['response']['raw_base64'] = 'e30='
            db.execute('UPDATE recovery_events SET payload=? WHERE event_id=?', (payer.canonical(event).decode(), event_id))
        with self.assertRaisesRegex(payer.ProgrammeError, 'event_digest_mismatch'):
            with self.ledger().locked():
                pass

    def test_existing_manifest_cannot_be_replaced(self):
        self.crash()
        self.f.manifest.write_bytes(self.f.manifest.read_bytes() + b'\n')
        with self.assertRaisesRegex(payer.ProgrammeError, 'frozen_manifest_mismatch'):
            self.recover()
        self.assertEqual(self.f.send.calls, [])

    def test_other_orphan_file_does_not_get_adopted(self):
        self.crash()
        payer.write_private(self.f.private/'records'/'unrelated.response.bin', b'fixture')
        with self.assertRaisesRegex(payer.ProgrammeError, 'orphan_or_missing_evidence'):
            self.recover()
        self.assertEqual(self.f.send.calls, [])

    def test_no_credential_still_records_unknown_without_transport(self):
        self.crash()
        review = {'external_capture_sha256': payer.sha(payer.canonical(self.external)),
                  'request_response_attribution_reviewed': True, 'reviewer_ref': 'fixture', 'basis_ref': 'fixture'}
        outcome = recovery.recover(self.f.policy, self.f.manifest, self.f.private, None, self.f.repo, 'one',
            external_evidence=self.external, capture_review=review,
            key_fingerprint_sha256=payer.transport.key_fingerprint('fixture-credential-not-a-real-key'))
        self.assertEqual(outcome['status'], 'unknown')
        self.assertEqual(outcome['reason'], 'recovery_credential_reference_unavailable')
        self.assertEqual(self.f.send.calls, [])
        self.assertTrue(self.event_rows())
        self.immutable()

    def test_concurrent_recovery_serializes_under_existing_payer_lock(self):
        from concurrent.futures import ThreadPoolExecutor
        self.crash()
        with ThreadPoolExecutor(max_workers=2) as pool:
            outcomes = list(pool.map(lambda _: self.recover()['status'], range(2)))
        self.assertCountEqual(outcomes, ['resolved', 'already_resolved'])
        self.assertEqual(len(self.f.send.calls), 2)
        self.immutable()

    def test_boundary_syncs_external_response_and_rejects_second_dispatch(self):
        from loom.tools.structure import experiment_workflow_v1 as workflow
        from loom.tools.structure import experiment_payer_boundary_v1 as boundary
        from loom.tools.structure.test_experiment_workflow_v1 import spec
        self.crash()
        self.recover()
        queue = workflow.Queue(self.f.root / 'queue.sqlite')
        self.addCleanup(queue.db.close)
        job = next(workflow.ordered_jobs(spec()))
        job.update(operation_id='one', request_sha256=payer.sha(self.f.body), request=json.loads(self.f.body),
                   requested_model='fake/model', requested_provider=json.loads(self.f.body)['provider'])
        queue.add([job])
        # Captures source-free fixture data, not a task-quality claim.
        adapter = boundary.PayerBoundary(queue, self.f.private, self.f.repo, self.f.policy['programme_id'],
            payer.transport.key_fingerprint('fixture-credential-not-a-real-key'), payer.sha(self.f.manifest.read_bytes()))
        reference = {'programme_id': adapter.programme_id, 'operation_id': 'one',
            'manifest_sha256': adapter.manifest, 'key_fingerprint_sha256': adapter.fingerprint}
        with self.assertRaisesRegex(ValueError, 'reservation_not_dispatchable'):
            adapter.verify('one', reference)
        queue.mark_dispatched('one', payer.canonical(reference).decode())
        adapter.sync_verified()
        self.assertEqual(queue.snapshot()[0]['state'], 'captured')
        self.assertEqual(queue.snapshot()[0]['result']['actual_cost_usd'], '0.01')
        self.immutable()

    def test_existing_runner_does_not_repeat_recovered_post(self):
        self.crash()
        self.recover()
        self.f.send.calls.clear()
        outcome = payer.run_stage(self.f.policy, self.f.manifest, self.f.evidence, self.f.private,
            self.f.keyfile, self.f.repo, transport_fn=self.f.send)
        self.assertEqual(outcome['status'], 'already_completed_saved_receipt')
        self.assertEqual(self.f.send.posts(), [])
        self.immutable()

    def test_external_capture_impossible_chronology_retains_unknown(self):
        self.crash()
        for captured_at in ('1900-01-01T00:00:00+00:00', '2999-01-01T00:00:00+00:00', '2026-10-09T00:00:00'):
            with self.subTest(captured_at=captured_at):
                self.external['captured_at'] = captured_at
                self.assertEqual(self.recover()['status'], 'unknown')
                self.assertIsNone(self.rows()[0]['actual_cost_usd'])
                self.assertEqual(self.f.send.calls, [])
        self.immutable()

    def test_uncertain_timeout_recovery_keeps_stop_until_existing_reconciliation(self):
        base = self.f.send
        def timeout(method, route, body=None, params=None):
            response = base(method, route, body, params)
            if method == 'POST':
                row = self.rows()[0]
                self.external = {'schema': recovery.CAPTURE_SCHEMA,
                    **{k: row[k] for k in ('operation_id', 'programme_id', 'stage_id', 'manifest_sha256',
                       'key_fingerprint_sha256', 'request_sha256')}, 'route_id': route,
                    'request_base64': base64.b64encode(body).decode(), 'capture_origin': 'controlled_capture_fixture',
                    'captured_at': payer.utc().isoformat(), 'response': recovery.encode_response(response)}
                raise TimeoutError('fixture-timeout')
            return response
        result = payer.run_stage(self.f.policy, self.f.manifest, self.f.evidence, self.f.private,
            self.f.keyfile, self.f.repo, transport_fn=timeout)
        self.assertEqual(result['status'], 'stopped')
        self.original = self.rows()
        self.first = {p: p.read_bytes() for p in (self.f.private / 'records').iterdir()}
        base.calls.clear()
        self.assertEqual(self.recover()['status'], 'resolved')
        with self.assertRaisesRegex(payer.ProgrammeError, 'durable_programme_stop'):
            with self.ledger().locked():
                pass
        result = payer.reconcile_stop(self.f.policy, self.f.manifest, self.f.evidence, self.f.private,
            self.f.keyfile, self.f.repo, transport_fn=base)
        self.assertEqual(result['status'], 'stop_resolved_read_only')
        self.immutable()

    def test_arbitrary_lowercase_or_mixed_exception_text_never_becomes_metadata(self):
        self.crash()
        for canary in ('private_source_detail_canary', 'Private source detail canary'):
            with self.subTest(canary=canary):
                def untrusted(*args, **kwargs):
                    raise payer.ProgrammeError(canary)
                base = self.f.send
                try:
                    self.f.send = untrusted
                    result = self.recover()
                finally:
                    self.f.send = base
                self.assertEqual(result['reason'], 'recovery_evidence_unverified')
                self.assertNotIn(canary, json.dumps(result))
                self.assertNotIn(canary, json.dumps(self.event_rows()))
                self.assertEqual(recovery.safe_reason(payer.ProgrammeError(canary), 'fixed'), 'fixed')
        self.immutable()

    def test_conflicting_capture_after_resolution_is_not_silently_acknowledged(self):
        self.crash()
        self.recover()
        first_events = self.event_rows()
        self.external['capture_origin'] = 'different_fixture_origin'
        self.f.send.calls.clear()
        with self.assertRaisesRegex(payer.ProgrammeError, 'resolved_capture_conflict'):
            self.recover()
        self.assertEqual(self.event_rows(), first_events)
        self.immutable()

    def test_recovered_cache_hit_cannot_be_promoted_to_independent_response(self):
        from loom.tools.structure import experiment_workflow_v1 as workflow
        from loom.tools.structure import experiment_payer_boundary_v1 as boundary
        from loom.tools.structure.test_experiment_workflow_v1 import spec
        self.crash()
        self.external['response']['response_cache_status'] = 'HIT'
        self.assertEqual(self.recover()['status'], 'resolved')
        queue = workflow.Queue(self.f.root / 'cache-queue.sqlite')
        self.addCleanup(queue.db.close)
        job = next(workflow.ordered_jobs(spec()))
        job.update(operation_id='one', request_sha256=payer.sha(self.f.body), request=json.loads(self.f.body),
                   requested_model='fake/model', requested_provider=json.loads(self.f.body)['provider'])
        queue.add([job]); queue.mark_dispatched('one', 'fixture-ref')
        # Even a conflicting earlier queue observation cannot hide a saved HIT.
        queue.append_evidence('one', 'transport_response_metadata', {'response_cache_status': 'MISS'})
        adapter = boundary.PayerBoundary(queue, self.f.private, self.f.repo, self.f.policy['programme_id'],
            payer.transport.key_fingerprint('fixture-credential-not-a-real-key'), payer.sha(self.f.manifest.read_bytes()))
        with self.assertRaisesRegex(ValueError, 'cached_response_not_independent'):
            adapter.sync_verified()
        self.assertEqual(queue.snapshot()[0]['state'], 'pending')
        self.immutable()

    def test_later_paid_attempt_does_not_invalidate_historical_key_check(self):
        self.crash()
        self.recover()
        # Replay is unchanged when the accounting context later gains an
        # unrelated fully verified row; a recovered historical key snapshot is
        # compared with its recorded accounting set, not today's full total.
        with self.ledger().locked() as ledger:
            proof = ledger.late_proofs()['one']
            row = deepcopy(ledger.rows()[0]); row['operation_id'] = 'future'; row['generation_id'] = 'future-gen'
            original_rows, rows = ledger.original_rows, ledger.rows
            with patch.object(ledger, 'rows', lambda: rows() + [row]), patch.object(ledger, 'original_rows', lambda: original_rows() + [row]):
                recovery.validate_proof(ledger, self.original[0], proof)


if __name__ == '__main__':
    unittest.main(verbosity=2)
