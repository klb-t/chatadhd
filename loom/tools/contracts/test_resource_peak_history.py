"""Reservation history evidence, independently synchronized from callback claims.

All callback quantities are scripted. These tests do not measure actual memory.
"""
from copy import deepcopy
import multiprocessing
import os
from pathlib import Path
import signal
import tempfile
import threading
import unittest

from . import analysis_plan_ref as ref
from .test_analysis_plan_ref import plan, result


def overlap_plan():
    p = plan()
    p['variant_axes'] = {'worker': {'values': [0, 1]}}
    p['methods'][0]['variant_axes'] = ['worker']
    p['methods'][0]['reservation']['memory_bytes'] = '10'
    p['resource_limits']['memory_bytes']['limit'] = '20'
    return p


def ledger_at(directory, p, **kwargs):
    return ref.ResourceLedger(directory, p['resource_limits'],
        budget_id=p['resource_budget_ref'], **kwargs)


def execute(p, index, ledger, callback):
    return ref.execute_variant(p, index, ledger,
        {('future:analysis', 'local:scripted'): callback}, capabilities=['local'])


def process_overlap(directory, p, index, channel, release):
    try:
        def callback(*_):
            channel.send(('entered', index))
            if not release.wait(15): raise RuntimeError('fixture_release_timeout')
            return result(measured='7')
        output = execute(p, index, ledger_at(directory, p), callback)
        channel.send(('completed', output['results']['one']))
    except BaseException as error:
        channel.send(('failed', type(error).__name__))
    finally:
        channel.close()


def process_crash_boundary(directory, p, phase, channel):
    original = ref._write_new
    original_reserve = ref.ResourceLedger.reserve

    def stop_here():
        channel.send(('boundary', phase))
        channel.recv()  # Parent kills this process; there is no automatic retry.

    def writing(path, value):
        name = Path(path).name
        if name == 'reservation.json' and phase == 'before_reservation': stop_here()
        original(path, value)
        if name == 'completion.json' and phase == 'after_completion': stop_here()

    def reserving(self, *args, **kwargs):
        folder = original_reserve(self, *args, **kwargs)
        # The full reserve operation includes fsync of the ledger directory,
        # not only the file and attempt directory, before packet loading.
        if phase == 'after_reservation': stop_here()
        return folder

    ref._write_new = writing
    ref.ResourceLedger.reserve = reserving
    try:
        def callback(*_):
            Path(directory, 'callback-dispatched').write_text('scripted')
            return result(measured='7')
        execute(p, 1, ledger_at(directory, p), callback)
        channel.send(('unexpected_return', phase))
    except BaseException as error:
        channel.send(('failed', type(error).__name__))
    finally:
        channel.close()


def write_legacy_attempt(directory, ident, *, completed):
    """Generate the actual schema/1 format, without downgrading a new receipt."""
    folder = Path(directory, 'attempt-' + ident)
    folder.mkdir()
    amounts = {d: '0' for d in ref.DIMENSIONS}
    amounts['memory_bytes'] = '10'
    ref._write_new(folder / 'reservation.json', {
        'schema': 'loom.analysis_attempt_reservation/1', 'attempt_id': ident,
        'reservation': amounts, 'context': {'sources': []}, 'state': 'reserved_before_callback'})
    if completed:
        response = result(measured='0')
        response['measurements']['memory_bytes'] = '7'
        loaded = {'loader_supplied': False, 'packet': None}
        ref._write_new(folder / 'loaded_packet_first.json', loaded)
        ref._write_new(folder / 'returned_first.json', response)
        ref._write_new(folder / 'result.json', response)
        ref._write_new(folder / 'completion.json', {
            'state': 'completed', 'measurements': response['measurements'],
            'measurement_provenance': response['measurement_provenance'],
            'result_sha256': ref.digest(response), 'loaded_packet_sha256': ref.digest(loaded)})
    return folder


class ResourcePeakHistoryChecks(unittest.TestCase):
    def assert_peak(self, ledger, expected, observed, legacy=0):
        accounting = ledger.accounting()
        peak = accounting['historical_reserved_peak']
        self.assertEqual(peak['amounts']['memory_bytes'], expected)
        self.assertEqual(peak['observed_reservations'], observed)
        self.assertEqual(peak['legacy_reservations_without_observation'], legacy)
        self.assertEqual(peak['complete_for_retained_attempts'], legacy == 0)
        self.assertEqual(peak['evidence_class'], 'reservation_accounting_not_usage_measurement')
        self.assertTrue(all(v is None for v in accounting['actual_instrumented_peak']['amounts'].values()))
        self.assertEqual(accounting['actual_instrumented_peak']['evidence_class'], 'unavailable')
        self.assertEqual(accounting['current_admission_load'], ledger.usage())
        return accounting

    def test_two_ledger_objects_threads_overlap_then_reopen_and_replay(self):
        p = overlap_plan()
        with tempfile.TemporaryDirectory() as directory:
            ledgers = [ledger_at(directory, p), ledger_at(directory, p)]
            entered, release = threading.Barrier(3), threading.Event()
            outputs, failures, calls = [], [], []

            def callback(context, _):
                calls.append(context['axes']['worker'])
                entered.wait(10)
                if not release.wait(10): raise RuntimeError('fixture_release_timeout')
                return result(measured='7')

            def worker(index):
                try: outputs.append(execute(p, index, ledgers[index], callback))
                except BaseException as error: failures.append(error)

            threads = [threading.Thread(target=worker, args=(i,)) for i in (0, 1)]
            for thread in threads: thread.start()
            try:
                entered.wait(10)
                held = self.assert_peak(ledgers[0], '20', 2)
                self.assertEqual(held['current_admission_load']['memory_bytes'], '20')
            finally:
                release.set()
                for thread in threads: thread.join(15)
            self.assertFalse(any(t.is_alive() for t in threads))
            self.assertEqual(failures, [])
            self.assertEqual(len(outputs), 2)
            reopened = ledger_at(directory, p)
            settled = self.assert_peak(reopened, '20', 2)
            self.assertEqual(settled['current_admission_load']['memory_bytes'], '7')
            self.assertEqual(settled['current_reserved_load']['memory_bytes'], '0')
            self.assertEqual(settled['maximum_callback_quantity_by_status']['instrument_measured']['memory_bytes'], '7')
            ids = {r['results']['one']['attempt_id'] for r in outputs}
            snapshots = {f.relative_to(directory): f.read_bytes() for f in Path(directory).glob('attempt-*/reservation.json')}
            replays = [execute(p, i, reopened, callback) for i in (0, 1)]
            self.assertEqual(sorted(calls), [0, 1])
            self.assertEqual({r['results']['one']['attempt_id'] for r in replays}, ids)
            self.assertTrue(all(r['results']['one']['dispatch'] == 'replayed_bound_snapshot' for r in replays))
            self.assertEqual(self.assert_peak(reopened, '20', 2), settled)
            self.assertEqual({f.relative_to(directory): f.read_bytes() for f in Path(directory).glob('attempt-*/reservation.json')}, snapshots)

    def test_two_spawned_processes_overlap_and_survive_reopen(self):
        p = overlap_plan()
        ctx = multiprocessing.get_context('spawn')
        with tempfile.TemporaryDirectory() as directory:
            ledger = ledger_at(directory, p)
            release = ctx.Event()
            channels = [ctx.Pipe() for _ in (0, 1)]
            processes = [ctx.Process(target=process_overlap,
                args=(directory, p, i, channels[i][1], release)) for i in (0, 1)]
            for process in processes: process.start()
            try:
                for i, (parent, child) in enumerate(channels):
                    child.close()
                    self.assertTrue(parent.poll(15), 'worker did not enter callback')
                    self.assertEqual(parent.recv(), ('entered', i))
                self.assert_peak(ledger, '20', 2)
                self.assertEqual(ledger.usage()['memory_bytes'], '20')
                release.set()
                ids = []
                for parent, _ in channels:
                    self.assertTrue(parent.poll(15), 'worker did not complete')
                    status, output = parent.recv()
                    self.assertEqual(status, 'completed')
                    self.assertEqual(output['state'], 'completed')
                    ids.append(output['attempt_id'])
                for process in processes:
                    process.join(15)
                    self.assertEqual(process.exitcode, 0)
                reopened = ledger_at(directory, p)
                self.assert_peak(reopened, '20', 2)
                self.assertEqual(reopened.usage()['memory_bytes'], '7')
                self.assertEqual(len(set(ids)), 2)
                self.assertTrue(all(reopened.lookup(i)['dispatch'] == 'replayed_bound_snapshot' for i in ids))
            finally:
                release.set()
                for process in processes:
                    if process.is_alive(): process.kill()
                    process.join(15)
                for parent, _ in channels: parent.close()

    def crash_at(self, directory, p, phase):
        ctx = multiprocessing.get_context('spawn')
        parent, child = ctx.Pipe()
        process = ctx.Process(target=process_crash_boundary, args=(directory, p, phase, child))
        process.start()
        child.close()
        try:
            self.assertTrue(parent.poll(15), 'worker did not reach persistence boundary')
            self.assertEqual(parent.recv(), ('boundary', phase))
            os.kill(process.pid, signal.SIGKILL)
            process.join(15)
            self.assertEqual(process.exitcode, -signal.SIGKILL)
        finally:
            if process.is_alive(): process.kill()
            process.join(15)
            parent.close()

    def test_kill_before_reservation_write_has_no_callback_and_fails_closed(self):
        p = overlap_plan()
        with tempfile.TemporaryDirectory() as directory:
            ledger_at(directory, p)
            self.crash_at(directory, p, 'before_reservation')
            self.assertFalse(Path(directory, 'callback-dispatched').exists())
            self.assertEqual(len(list(Path(directory).glob('attempt-*'))), 1)
            self.assertEqual(list(Path(directory).glob('attempt-*/reservation.json')), [])
            with self.assertRaises(ref.PlanError): ledger_at(directory, p).accounting()
            with self.assertRaises(ref.PlanError):
                execute(p, 1, ledger_at(directory, p), lambda *_: self.fail('must not dispatch'))

    def test_kill_after_reservation_retains_overlap_before_callback(self):
        p = overlap_plan()
        with tempfile.TemporaryDirectory() as directory:
            ledger = ledger_at(directory, p)
            ledger.reserve('a' * 64, p['methods'][0]['reservation'], {'sources': []})
            self.crash_at(directory, p, 'after_reservation')
            self.assertFalse(Path(directory, 'callback-dispatched').exists())
            reopened = ledger_at(directory, p)
            self.assert_peak(reopened, '20', 2)
            self.assertEqual(reopened.usage()['memory_bytes'], '20')
            replay = execute(p, 1, reopened, lambda *_: self.fail('must not dispatch'))
            self.assertEqual(replay['results']['one']['state'], 'uncertain')
            self.assertEqual(replay['results']['one']['dispatch'], 'no_retry')
            self.assert_peak(reopened, '20', 2)

    def test_kill_after_completion_replays_without_losing_previous_overlap(self):
        p = overlap_plan()
        with tempfile.TemporaryDirectory() as directory:
            ledger = ledger_at(directory, p)
            ledger.reserve('a' * 64, p['methods'][0]['reservation'], {'sources': []})
            self.crash_at(directory, p, 'after_completion')
            self.assertTrue(Path(directory, 'callback-dispatched').exists())
            reopened = ledger_at(directory, p)
            self.assert_peak(reopened, '20', 2)
            self.assertEqual(reopened.usage()['memory_bytes'], '10')
            replay = execute(p, 1, reopened, lambda *_: self.fail('must not dispatch'))
            self.assertEqual(replay['results']['one']['dispatch'], 'replayed_bound_snapshot')
            self.assert_peak(reopened, '20', 2)

    def test_old_completed_attempts_have_no_invented_peak_and_are_never_rewritten(self):
        p = overlap_plan()
        with tempfile.TemporaryDirectory() as directory:
            ledger = ledger_at(directory, p)
            write_legacy_attempt(directory, 'a' * 64, completed=True)
            write_legacy_attempt(directory, 'b' * 64, completed=True)
            originals = {f.relative_to(directory): f.read_bytes() for f in Path(directory).rglob('*.json')}
            accounting = self.assert_peak(ledger, None, 0, legacy=2)
            self.assertEqual(accounting['current_admission_load']['memory_bytes'], '7')
            self.assertEqual(accounting['current_reserved_load']['memory_bytes'], '0')
            self.assertEqual(ledger.lookup('a' * 64)['dispatch'], 'replayed_bound_snapshot')
            ledger.reserve('c' * 64, p['methods'][0]['reservation'], {'sources': []})
            self.assert_peak(ledger_at(directory, p), '10', 1, legacy=2)
            for name, raw in originals.items(): self.assertEqual(Path(directory, name).read_bytes(), raw)

    def test_legacy_held_load_is_visible_only_at_new_observation_boundary(self):
        p = overlap_plan()
        with tempfile.TemporaryDirectory() as directory:
            ledger = ledger_at(directory, p)
            write_legacy_attempt(directory, 'a' * 64, completed=False)
            self.assert_peak(ledger, None, 0, legacy=1)
            ledger.reserve('b' * 64, p['methods'][0]['reservation'], {'sources': []})
            accounting = self.assert_peak(ledger, '20', 1, legacy=1)
            self.assertEqual(accounting['current_reserved_load']['memory_bytes'], '20')
            self.assertEqual(accounting['historical_reserved_peak']['scope'], 'recorded_reservation_boundaries_in_this_ledger')

    def test_opening_balances_are_not_reconstructed_history(self):
        p = overlap_plan()
        with tempfile.TemporaryDirectory() as directory:
            balances = {d: {'measured': '0', 'reserved_unknown': '0',
                'provenance': {'status': 'provider_reported', 'source_ref': 'scripted-opening'}} for d in ref.DIMENSIONS}
            balances['memory_bytes'].update(measured='7', reserved_unknown='5')
            ledger = ledger_at(directory, p, opening_balances=balances)
            header = Path(directory, 'ledger.json').read_bytes()
            opening = self.assert_peak(ledger, None, 0)
            self.assertEqual(opening['current_admission_load']['memory_bytes'], '7')
            self.assertEqual(opening['current_reserved_load']['memory_bytes'], '5')
            ledger.reserve('a' * 64, p['methods'][0]['reservation'], {'sources': []})
            accounting = self.assert_peak(ledger_at(directory, p), '15', 1)
            self.assertEqual(accounting['historical_reserved_peak']['external_history_before_opening_balances'], 'not_reconstructed')
            self.assertEqual(Path(directory, 'ledger.json').read_bytes(), header)
            self.assertEqual(ledger.opening_balances, balances)

    def test_large_estimate_increases_admission_but_not_original_reservation_history(self):
        p = overlap_plan()
        p['resource_limits']['memory_bytes']['limit'] = '100'
        with tempfile.TemporaryDirectory() as directory:
            ledger = ledger_at(directory, p)
            response = result(measured='0')
            response['measurements']['memory_bytes'] = '30'
            response['measurement_provenance']['memory_bytes'] = {'status': 'estimated', 'source_ref': 'scripted-estimate'}
            execute(p, 0, ledger, lambda *_: response)
            accounting = self.assert_peak(ledger, '10', 1)
            self.assertEqual(accounting['current_admission_load']['memory_bytes'], '30')
            self.assertEqual(accounting['current_reserved_load']['memory_bytes'], '10')
            self.assertEqual(accounting['maximum_callback_quantity_by_status']['estimated']['memory_bytes'], '30')
            amounts = {d: '0' for d in ref.DIMENSIONS}
            amounts['memory_bytes'] = '5'
            ledger.reserve('a' * 64, amounts, {'sources': []})
            accounting = self.assert_peak(ledger, '15', 2)
            self.assertEqual(accounting['current_admission_load']['memory_bytes'], '35')

    def test_capacity_reuse_does_not_erase_peak_and_rejected_limit_adds_no_observation(self):
        p = overlap_plan()
        p['resource_limits']['memory_bytes']['limit'] = '10'
        with tempfile.TemporaryDirectory() as directory:
            ledger = ledger_at(directory, p)
            execute(p, 0, ledger, lambda *_: result(measured='7'))
            execute(p, 1, ledger, lambda *_: result(measured='7'))
            self.assert_peak(ledger, '10', 2)
            amounts = {d: '0' for d in ref.DIMENSIONS}
            amounts['memory_bytes'] = '11'
            with self.assertRaises(ref.PlanError) as raised:
                ledger.reserve('a' * 64, amounts, {'sources': []})
            self.assertEqual(raised.exception.code, 'resource_limit_exceeded_memory_bytes')
            self.assertFalse(Path(directory, 'attempt-' + 'a' * 64).exists())
            self.assert_peak(ledger, '10', 2)

    def test_observation_binding_dimensions_and_arithmetic_fail_closed(self):
        p = overlap_plan()
        for mutation in ('missing', 'ledger', 'reservation', 'dimensions', 'arithmetic'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                ledger = ledger_at(directory, p)
                folder = ledger.reserve('a' * 64, p['methods'][0]['reservation'], {'sources': []})
                record = ref._read(folder / 'reservation.json')
                observation = record['reservation_observation']
                if mutation == 'missing': del record['reservation_observation']
                elif mutation == 'ledger': observation['ledger_sha256'] = 'b' * 64
                elif mutation == 'reservation': record['reservation']['memory_bytes'] = '9'
                elif mutation == 'dimensions': del observation['held_before']['memory_bytes']
                else: observation['held_after']['memory_bytes'] = '11'
                (folder / 'reservation.json').write_bytes(ref.encoded(record))
                with self.assertRaises(ref.PlanError): ledger.accounting()
                with self.assertRaises(ref.PlanError): ledger.lookup('a' * 64)

    def test_reservation_history_preserves_exact_decimal_sums(self):
        p = overlap_plan()
        p['resource_limits']['memory_bytes']['limit'] = None
        with tempfile.TemporaryDirectory() as directory:
            ledger = ledger_at(directory, p)
            amounts = {d: '0' for d in ref.DIMENSIONS}
            amounts['memory_bytes'] = '10000000000000000000000000000000000000000'
            ledger.reserve('a' * 64, amounts, {'sources': []})
            tiny = deepcopy(amounts)
            tiny['memory_bytes'] = '0.00000000000000000000000000000000000000001'
            ledger.reserve('b' * 64, tiny, {'sources': []})
            expected = '10000000000000000000000000000000000000000.00000000000000000000000000000000000000001'
            accounting = self.assert_peak(ledger_at(directory, p), expected, 2)
            self.assertEqual(accounting['current_admission_load']['memory_bytes'], expected)


if __name__ == '__main__': unittest.main()
