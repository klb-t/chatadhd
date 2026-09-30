"""Independent interruption/concurrency counterexamples, synthetic graph only."""
from copy import deepcopy
import json
import multiprocessing
import os
import time
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from loom.tools.coordination import analysis_graph as adapter
from loom.tools.coordination.leases import CoordinationError, LeaseStore
from loom.tools.contracts.test_analysis_plan_ref import plan as make_plan
from loom.tools.structure.agentic_graph_v1.test_packet import fixture, MODEL, AUTO

COMMIT = 'a' * 40


def case():
    packet = fixture()
    plan = make_plan()
    plan['sources'][0]['binding']['snapshot_id'] = packet['packet_id']
    method = plan['methods'][0]
    method['method'], method['runtime']['id'] = adapter.CALLBACK
    method['acceptance_policy'] = deepcopy(AUTO)
    diff = adapter.codec.empty_diff(packet, proposal_id='independent-review', origin=MODEL)
    method['config'] = {'diff': diff, 'policy': deepcopy(AUTO)}
    return plan, packet


def invoke(directory, *, store=None, task_id='review', plan=None, packet=None, source_commit=COMMIT):
    if plan is None or packet is None:
        default_plan, default_packet = case()
        plan = default_plan if plan is None else plan
        packet = default_packet if packet is None else packet
    directory = Path(directory)
    store = store or LeaseStore(directory / 'leases.sqlite')
    return adapter.run_coordinated_analysis_graph(store, task_id=task_id, source_commit=source_commit,
        owner='independent-review', plan=plan, packet=packet, variant_index=0,
        ledger_directory=directory / 'ledger', output_root=directory / 'outputs', lease_seconds=10)


def racing_process(directory, ready, start, results):
    ready.put(True)
    start.wait(10)
    try:
        result = invoke(directory)
        results.put(('ok', result['execution']['acquired']))
    except Exception as exc:
        results.put(('error', type(exc).__name__, str(exc)))


def crash_before_receipt(directory):
    store = LeaseStore(Path(directory) / 'leases.sqlite')
    def interrupted(*args, **kwargs):
        os._exit(23)
    with patch.object(store, 'complete', interrupted):
        invoke(directory, store=store)


def crash_after_reservation(directory):
    def interrupted(*args, **kwargs):
        os._exit(24)
    with patch.object(adapter, 'graph_callback', interrupted):
        invoke(directory)


class AnalysisGraphAdversarial(unittest.TestCase):
    def test_expiry_before_dispatch_never_calls_graph(self):
        with tempfile.TemporaryDirectory() as temporary:
            now = [100.0]
            store = LeaseStore(Path(temporary) / 'leases.sqlite', clock=lambda: now[0])
            begin = store.begin
            def expired(lease):
                now[0] += 11
                return begin(lease)
            with patch.object(store, 'begin', expired), patch.object(adapter, 'graph_callback') as graph:
                with self.assertRaises(CoordinationError): invoke(temporary, store=store)
                graph.assert_not_called()
            self.assertEqual(store.inspect('review')['state'], 'pending')
            self.assertFalse(list(Path(temporary).rglob('input_first.json')))

    def test_stale_fence_before_dispatch_cannot_apply_graph(self):
        with tempfile.TemporaryDirectory() as temporary:
            now = [100.0]
            store = LeaseStore(Path(temporary) / 'leases.sqlite', clock=lambda: now[0])
            begin = store.begin
            def stolen(lease):
                now[0] += 11
                claim = store.claim('review', owner='new-worker', lease_seconds=10)
                self.assertTrue(claim['acquired'])
                return begin(lease)
            with patch.object(store, 'begin', stolen), patch.object(adapter, 'graph_callback') as graph:
                with self.assertRaisesRegex(CoordinationError, 'stale_or_foreign'): invoke(temporary, store=store)
                graph.assert_not_called()
            self.assertEqual(store.inspect('review')['fence'], 2)

    def test_late_completion_retains_first_result_without_retry(self):
        with tempfile.TemporaryDirectory() as temporary:
            now = [100.0]
            store = LeaseStore(Path(temporary) / 'leases.sqlite', clock=lambda: now[0])
            complete = store.complete
            def expired(lease, **kwargs):
                now[0] += 11
                return complete(lease, **kwargs)
            with patch.object(store, 'complete', expired):
                with self.assertRaises(CoordinationError): invoke(temporary, store=store)
            self.assertEqual(store.inspect('review')['state'], 'outcome_unknown')
            first = next(Path(temporary).rglob('result_first.json'))
            before = first.read_bytes()
            with patch.object(adapter, 'graph_callback') as graph:
                response = invoke(temporary, store=store)
                graph.assert_not_called()
            self.assertFalse(response['execution']['acquired'])
            self.assertEqual(first.read_bytes(), before)
            retained = store.receipts('review')[-1]['detail']['evidence']
            self.assertTrue(any(e['kind'] == 'first_callback_return' for e in retained))

    def test_expiry_after_first_effect_blocks_second_method(self):
        with tempfile.TemporaryDirectory() as temporary:
            now = [100.0]
            store = LeaseStore(Path(temporary) / 'leases.sqlite', clock=lambda: now[0])
            plan, packet = case()
            second = deepcopy(plan['methods'][0]); second['id'] = 'two'
            plan['methods'].append(second)
            callback = adapter.graph_callback; calls = []
            def delayed(context, loaded):
                calls.append(context['method']['id'])
                returned = callback(context, loaded)
                now[0] += 11
                return returned
            with patch.object(adapter, 'graph_callback', delayed):
                with self.assertRaises(CoordinationError): invoke(temporary, store=store, plan=plan, packet=packet)
            self.assertEqual(calls, ['one'])
            first = json.loads(next(Path(temporary).rglob('result_first.json')).read_text())
            self.assertFalse(first['all_methods_completed'])
            self.assertEqual(first['uncertain_methods'], ['two'])
            self.assertEqual(first['analysis_result']['results']['one']['state'], 'completed')
            self.assertEqual(store.inspect('review')['state'], 'outcome_unknown')

    def test_changed_configuration_is_identity_conflict_preserves_first_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            plan, packet = case()
            invoke(temporary, plan=plan, packet=packet)
            originals = {p: p.read_bytes() for p in Path(temporary).rglob('*first.json')}
            plan['methods'][0]['config']['explicitly_accepted'] = True
            with patch.object(adapter, 'graph_callback') as graph:
                with self.assertRaisesRegex(CoordinationError, 'task_identity_conflict'):
                    invoke(temporary, plan=plan, packet=packet)
                graph.assert_not_called()
            self.assertTrue(originals)
            self.assertEqual({p: p.read_bytes() for p in originals}, originals)

    def test_new_code_identity_cannot_reuse_old_method_attempt(self):
        with tempfile.TemporaryDirectory() as temporary:
            first = invoke(temporary)['execution']['outcome']['analysis_result']['results']['one']
            other = invoke(temporary, task_id='new-code', source_commit='b' * 40)
            second = other['execution']['outcome']['analysis_result']['results']['one']
            self.assertNotEqual(first['attempt_id'], second['attempt_id'])
            self.assertEqual(second['dispatch'], 'callback_dispatched')

    def test_killed_worker_retains_artifacts_and_restart_never_retries(self):
        with tempfile.TemporaryDirectory() as temporary:
            ctx = multiprocessing.get_context('spawn')
            worker = ctx.Process(target=crash_before_receipt, args=(temporary,))
            worker.start(); worker.join(25)
            if worker.is_alive(): worker.terminate(); worker.join(5)
            self.assertEqual(worker.exitcode, 23)
            store = LeaseStore(Path(temporary) / 'leases.sqlite', clock=lambda: time.time() + 20)
            self.assertEqual(store.inspect('review')['state'], 'outcome_unknown')
            preserved = {p: p.read_bytes() for p in Path(temporary).rglob('*first.json')}
            self.assertTrue(any(p.name == 'result_first.json' for p in preserved))
            self.assertTrue(any(p.name == 'returned_first.json' for p in preserved))
            with patch.object(adapter, 'graph_callback') as graph:
                restart = invoke(temporary, store=store)
                graph.assert_not_called()
            self.assertFalse(restart['execution']['acquired'])
            self.assertEqual({p: p.read_bytes() for p in preserved}, preserved)
            self.assertNotIn('completed', [r['event'] for r in store.receipts('review')])

    def test_killed_inner_reservation_stays_unknown_after_explicit_outer_retry(self):
        with tempfile.TemporaryDirectory() as temporary:
            ctx = multiprocessing.get_context('spawn')
            worker = ctx.Process(target=crash_after_reservation, args=(temporary,))
            worker.start(); worker.join(25)
            if worker.is_alive(): worker.terminate(); worker.join(5)
            self.assertEqual(worker.exitcode, 24)
            store = LeaseStore(Path(temporary) / 'leases.sqlite', clock=lambda: time.time() + 20)
            self.assertEqual(store.inspect('review')['state'], 'outcome_unknown')
            reservation = next(Path(temporary).glob('ledger/attempt-*/reservation.json'))
            before = reservation.read_bytes()
            store.reconcile('review', expected_fence=1, actor='reviewer', decision='retry',
                outcome={'manual_test': True}, evidence=[{'kind': 'worker_exit_24'}],
                retry_basis='Explicitly inspect preserved local reservation via a new outer run')
            with patch.object(adapter, 'graph_callback') as graph:
                restart = invoke(temporary, store=store)
                graph.assert_not_called()
            outcome = restart['execution']['outcome']
            self.assertFalse(outcome['all_methods_completed'])
            self.assertEqual(outcome['uncertain_methods'], ['one'])
            self.assertEqual(reservation.read_bytes(), before)
            self.assertEqual(len(list(Path(temporary).glob('ledger/attempt-*'))), 1)
            self.assertEqual(store.inspect('review')['fence'], 2)

    def test_two_actual_processes_share_one_claim_and_first_result(self):
        with tempfile.TemporaryDirectory() as temporary:
            LeaseStore(Path(temporary) / 'leases.sqlite')
            ctx = multiprocessing.get_context('spawn')
            ready, results, start = ctx.Queue(), ctx.Queue(), ctx.Event()
            workers = [ctx.Process(target=racing_process, args=(temporary, ready, start, results)) for _ in range(2)]
            for worker in workers: worker.start()
            try:
                for _ in workers: ready.get(timeout=15)
                start.set()
                outcomes = [results.get(timeout=25) for _ in workers]
                self.assertEqual(sorted(outcomes), [('ok', False), ('ok', True)])
            finally:
                for worker in workers:
                    worker.join(10)
                    if worker.is_alive(): worker.terminate(); worker.join(5)
            self.assertEqual(len(list(Path(temporary).rglob('result_first.json'))), 1)
            self.assertEqual(len(list(Path(temporary).glob('ledger/attempt-*'))), 1)


if __name__ == '__main__': unittest.main()
