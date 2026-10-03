"""Independent counterexamples for lazy plans and durable local execution."""
from copy import deepcopy
from pathlib import Path
import tempfile
import threading
import unittest

try:
    from . import analysis_plan_ref as ref
    from .validate import ContractValidator, SCHEMAS
except ImportError:
    import analysis_plan_ref as ref
    from validate import ContractValidator, SCHEMAS


def conservative_policy():
    return {s: 'use_declared_amount' if s in ref.KNOWN_STATUSES else 'max_reservation_amount'
            for s in ref.MEASUREMENT_STATUSES}


def limits(value='100'):
    return {d: {'limit': value, 'accounting': 'peak' if d in ('memory_bytes', 'agents') else 'cumulative',
            'measurement_policy': conservative_policy()}
            for d in ref.DIMENSIONS}


def method(ident='one', dependencies=(), axes=()):
    return {'id': ident, 'method': 'future:analysis', 'runtime': {'id': 'local:scripted', 'config': {}},
        'depends_on': list(dependencies), 'source_refs': ['source'], 'variant_axes': list(axes),
        'required_capabilities': ['local'], 'scope': {'full_source': True},
        'roles': [{'id': 'review', 'role': 'critique', 'cardinality': 1, 'config': {}}],
        'tools': [], 'reasoning': {'depth': 'user-configured'}, 'acceptance_policy': {'mode': 'automatic'},
        'evaluation_policy': {'retain_all': True}, 'reservation': {d: '1' for d in ref.DIMENSIONS}, 'config': {}}


def plan():
    return {'schema': 'loom.analysis_plan/1', 'id': 'mechanism', 'resource_budget_ref': 'shared',
        'semantic_contract': {'id': 'domain:graph-review', 'version': '1', 'config': {}},
        'sources': [{'id': 'source', 'ref': 'fixture:raw', 'provenance': {'known_at': None},
            'binding': {'mode': 'pinned_snapshot', 'snapshot_id': 'raw_fixture_v1'}}],
        'variant_axes': {}, 'selection': {'mode': 'all'}, 'methods': [method()],
        'resource_limits': limits(), 'presets': {'model': 'future:frontier', 'agents': 1000000000}, 'extensions': {}}


def result(output=None, measured='1'):
    measurements = {d: measured for d in ref.DIMENSIONS}
    return {'output': {'candidate': True} if output is None else output, 'measurements': measurements,
        'measurement_provenance': {d: {'status': 'instrument_measured', 'source_ref': 'scripted-mechanism-counter'} for d in measurements if measurements[d] is not None}}


class AnalysisPlanChecks(unittest.TestCase):
    def ledger(self, folder, p): return ref.ResourceLedger(folder, p['resource_limits'], budget_id=p['resource_budget_ref'])
    def execute(self, p, ledger, callback, index=0, packet_loader=None, capabilities=('local',)):
        return ref.execute_variant(p, index, ledger, {('future:analysis', 'local:scripted'): callback},
            capabilities=capabilities, packet_loader=packet_loader)

    def test_local_schema_registry_loads_without_global_kind_registration(self):
        validator = ContractValidator()
        self.assertIn('analysis_plan.schema.json', validator.schemas)
        self.assertNotIn('loom.analysis_plan/1', SCHEMAS)
        self.assertEqual(ref.validate_plan(plan()), plan())

    def test_billion_and_larger_symbolic_spaces_do_not_allocate_workers(self):
        p = plan(); p['variant_axes'] = {'agent': {'range': {'start': 0, 'stop': 10**60, 'step': 1}},
            'prompt': {'values': ['A', 'B']}}
        ref.validate_plan(p)
        self.assertEqual(ref.cardinality(p), 2 * 10**60)
        iterator = ref.selected_variants(p)
        self.assertEqual(next(iterator), (0, {'agent': 0, 'prompt': 'A'}))
        self.assertEqual(next(iterator), (1, {'agent': 0, 'prompt': 'B'}))
        self.assertEqual(ref.variant_at(p, 2 * 10**60 - 1), {'agent': 10**60 - 1, 'prompt': 'B'})

    def test_cycle_missing_dependency_bad_axis_and_reference_fail(self):
        for kind in ('cycle', 'missing', 'axis', 'source', 'empty_range'):
            p = plan()
            if kind == 'cycle': p['methods'][0]['depends_on'] = ['one']
            elif kind == 'missing': p['methods'][0]['depends_on'] = ['absent']
            elif kind == 'axis': p['methods'][0]['variant_axes'] = ['absent']
            elif kind == 'source': p['methods'][0]['source_refs'] = ['absent']
            else: p['variant_axes'] = {'x': {'range': {'start': 3, 'stop': 1, 'step': 1}}}
            with self.subTest(kind=kind), self.assertRaises(ref.PlanError): ref.validate_plan(p)

    def test_explicit_limits_do_not_come_from_presets(self):
        p = plan(); p['presets']['money_usd'] = 1000000000
        p['resource_limits']['money_usd']['limit'] = '0'
        with tempfile.TemporaryDirectory() as temporary:
            called = []
            output = self.execute(p, self.ledger(temporary, p), lambda *_: called.append(True))
            self.assertEqual(called, [])
            self.assertEqual(output['results']['one']['reason'], 'resource_limit_exceeded_money_usd')

    def test_missing_capability_does_not_veto_independent_methods(self):
        p = plan(); p['methods'] = [method('one'), method('two')]
        p['methods'][0]['required_capabilities'] = ['absent']
        with tempfile.TemporaryDirectory() as temporary:
            called = []
            response = self.execute(p, self.ledger(temporary, p), lambda c, _: (called.append(c['method']['id']) or result()))
            self.assertEqual(called, ['two'])
            self.assertEqual(response['results']['one']['state'], 'unavailable')
            self.assertEqual(response['results']['two']['state'], 'completed')

    def test_reservation_is_durable_before_packet_loader_and_callback(self):
        p = plan()
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p); observed = []
            def loader(_):
                reservations = list(Path(temporary).glob('attempt-*/reservation.json'))
                self.assertEqual(len(reservations), 1); observed.append('loader')
                return {'original': True}
            def callback(_, packet):
                self.assertEqual(observed, ['loader']); self.assertTrue(packet['original'])
                observed.append('callback'); return result()
            response = self.execute(p, ledger, callback, packet_loader=loader)
            self.assertEqual(response['results']['one']['state'], 'completed')
            self.assertEqual(observed, ['loader', 'callback'])

    def test_unknown_exception_retains_reservation_and_never_retries(self):
        p = plan()
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p); calls = []
            def fail(*_): calls.append(1); raise RuntimeError('private details excluded')
            first = self.execute(p, ledger, fail); second = self.execute(p, ledger, fail)
            self.assertEqual(calls, [1])
            self.assertEqual(first['results']['one']['state'], 'uncertain')
            self.assertEqual(second['results']['one']['state'], 'uncertain')
            self.assertEqual(ledger.usage()['money_usd'], '1')
            text = ''.join(p.read_text() for p in Path(temporary).rglob('*.json'))
            self.assertNotIn('private details excluded', text)

    def test_missing_measurement_retains_only_unknown_dimension(self):
        p = plan()
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p)
            r = result(measured='0'); r['measurements']['money_usd'] = None
            del r['measurement_provenance']['money_usd']
            response = self.execute(p, ledger, lambda *_: r)
            self.assertEqual(response['results']['one']['state'], 'completed')
            self.assertEqual(ledger.usage()['money_usd'], '1')
            self.assertEqual(ledger.usage()['cpu_seconds'], '0')

    def test_completed_callback_is_replayed_without_extra_call(self):
        p = plan()
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p); calls = []
            callback = lambda *_: (calls.append(1) or result())
            self.execute(p, ledger, callback); self.execute(p, ledger, callback)
            self.assertEqual(calls, [1])
            self.assertEqual(len(list(Path(temporary).glob('attempt-*'))), 1)

    def test_method_presentation_permutation_keeps_attempt_identity(self):
        p = plan(); p['methods'] = [method('one'), method('two', ['one'])]
        permuted = deepcopy(p); permuted['methods'].reverse()
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p); calls = []
            callback = lambda c, _: (calls.append(c['method']['id']) or result())
            self.execute(p, ledger, callback); self.execute(permuted, ledger, callback)
            self.assertEqual(calls, ['one', 'two'])

    def test_dependency_variant_changes_child_identity_even_without_own_axis(self):
        p = plan(); p['variant_axes'] = {'model': {'values': ['A', 'B']}}
        p['methods'] = [method('one', axes=['model']), method('two', ['one'])]
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p); calls = []
            callback = lambda c, _: (calls.append(c['method']['id']) or result())
            first = self.execute(p, ledger, callback, 0); second = self.execute(p, ledger, callback, 1)
            self.assertNotEqual(first['results']['two']['attempt_id'], second['results']['two']['attempt_id'])
            self.assertEqual(calls, ['one', 'two', 'one', 'two'])

    def test_one_budget_shared_across_distinct_plan_branches(self):
        p = plan(); p['resource_limits']['money_usd']['limit'] = '1'
        another = deepcopy(p); another['id'] = 'another'
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p); calls = []
            callback = lambda *_: (calls.append(1) or result())
            self.execute(p, ledger, callback)
            blocked = self.execute(another, ledger, callback)
            self.assertEqual(calls, [1])
            self.assertEqual(blocked['results']['one']['reason'], 'resource_limit_exceeded_money_usd')
            with self.assertRaises(ref.PlanError): ref.ResourceLedger(temporary, limits('1000'), budget_id='shared')

    def test_peak_budget_reuses_capacity_after_completed_measurement(self):
        p = plan(); p['resource_limits']['memory_bytes']['limit'] = '1'
        p['methods'] = [method('one'), method('two')]
        with tempfile.TemporaryDirectory() as temporary:
            response = self.execute(p, self.ledger(temporary, p), lambda *_: result())
            self.assertTrue(all(r['state'] == 'completed' for r in response['results'].values()))
            self.assertEqual(response['resource_usage']['memory_bytes'], '1')

    def test_actual_overrun_blocks_further_admission_without_hiding_measurement(self):
        p = plan(); p['resource_limits']['money_usd']['limit'] = '1'; p['methods'] = [method('one'), method('two')]
        with tempfile.TemporaryDirectory() as temporary:
            r = result(); r['measurements']['money_usd'] = '2'
            response = self.execute(p, self.ledger(temporary, p), lambda *_: r)
            self.assertEqual(response['resource_usage']['money_usd'], '2')
            self.assertEqual(response['results']['two']['state'], 'unavailable')

    def test_callback_proposals_never_apply_or_mutate_original_packet(self):
        p = plan(); original = {'schema': 'other.domain/1', 'claims': ['raw']}
        with tempfile.TemporaryDirectory() as temporary:
            def callback(context, packet):
                self.assertTrue(context['no_automatic_graph_application']); packet['claims'].clear()
                return result({'delete_all': True})
            response = self.execute(p, self.ledger(temporary, p), callback, packet_loader=lambda _: original)
            self.assertEqual(original['claims'], ['raw'])
            self.assertEqual(response['canonical_graph_writes'], 0)
            self.assertFalse(response['acceptance_policy_executed'])

    def test_generic_graph_packet_api_is_supplied_lazily(self):
        p = plan()
        with tempfile.TemporaryDirectory() as temporary:
            def loader(_):
                from loom.tools.structure.agentic_graph_v1 import packet
                return packet.make_packet(origin={'kind': 'system', 'actor': 'offline-plan-demo',
                    'model': None, 'recipe_sha256': None, 'response_sha256': None})
            def callback(_, value):
                from loom.tools.structure.agentic_graph_v1 import packet
                packet.validate_packet(value)
                return result({'packet_id': value['packet_id'], 'proposal': 'inspect gaps'})
            response = self.execute(p, self.ledger(temporary, p), callback, packet_loader=loader)
            self.assertEqual(response['results']['one']['state'], 'completed')

    def test_corrupt_first_result_cannot_be_accepted_as_completed(self):
        p = plan()
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p); self.execute(p, ledger, lambda *_: result())
            next(Path(temporary).glob('attempt-*/result.json')).write_text('{}')
            with self.assertRaises(ref.PlanError): self.execute(p, ledger, lambda *_: result())

    def test_concurrent_same_attempt_invokes_callback_only_once(self):
        p = plan(); entered = threading.Event(); release = threading.Event(); calls = []; replies = []
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p)
            def callback(*_):
                calls.append(1); entered.set()
                if not release.wait(5): raise RuntimeError('mechanism coordination timed out')
                return result()
            worker = threading.Thread(target=lambda: replies.append(self.execute(p, ledger, callback)))
            worker.start(); self.assertTrue(entered.wait(5))
            pending = self.execute(p, ledger, callback)
            self.assertEqual(pending['results']['one']['state'], 'uncertain')
            release.set(); worker.join(5)
            self.assertFalse(worker.is_alive()); self.assertEqual(calls, [1])
            self.assertEqual(replies[0]['results']['one']['state'], 'completed')

    def test_incomplete_reservation_directory_fails_closed(self):
        p = plan()
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p)
            (Path(temporary) / ('attempt-' + 'a' * 64)).mkdir()
            with self.assertRaises(ref.PlanError): self.execute(p, ledger, lambda *_: result())

    def test_imported_known_and_unknown_usage_cannot_reset_on_reopen(self):
        p = plan(); p['resource_limits']['money_usd']['limit'] = '2'
        balances = {d: {'measured': '0', 'reserved_unknown': '0', 'provenance': {'status': 'provider_reported', 'source_ref': 'old-ledger'}} for d in ref.DIMENSIONS}
        balances['money_usd'].update(measured='1', reserved_unknown='0.25')
        with tempfile.TemporaryDirectory() as temporary:
            ledger = ref.ResourceLedger(temporary, p['resource_limits'], budget_id='shared', opening_balances=balances)
            self.assertEqual(ledger.usage()['money_usd'], '1.25')
            blocked = self.execute(p, ledger, lambda *_: result())
            self.assertEqual(blocked['results']['one']['state'], 'unavailable')
            reopened = self.ledger(temporary, p)
            self.assertEqual(reopened.usage()['money_usd'], '1.25')
            reset = deepcopy(balances); reset['money_usd']['measured'] = '0'
            with self.assertRaises(ref.PlanError): ref.ResourceLedger(temporary, p['resource_limits'], budget_id='shared', opening_balances=reset)

    def test_null_or_empty_provenance_cannot_release_reservation(self):
        for origin in (None, {}, {'status': 'instrument_measured', 'source_ref': ''}):
            with self.subTest(origin=origin), tempfile.TemporaryDirectory() as temporary:
                p = plan(); ledger = self.ledger(temporary, p); r = result(measured='0')
                r['measurement_provenance']['money_usd'] = origin
                response = self.execute(p, ledger, lambda *_: r)
                self.assertEqual(response['results']['one']['state'], 'uncertain')
                self.assertEqual(ledger.usage()['money_usd'], '1')
                self.assertEqual(ref._read(next(Path(temporary).glob('attempt-*/returned_first.json'))), r)

    def test_estimated_unknown_and_declared_zero_stay_held_by_explicit_policy(self):
        for status in ('estimated', 'unknown', 'declared'):
            with self.subTest(status=status), tempfile.TemporaryDirectory() as temporary:
                p = plan(); ledger = self.ledger(temporary, p); r = result(measured='0')
                r['measurement_provenance']['money_usd'] = {'status': status, 'source_ref': 'diagnostic-zero'}
                response = self.execute(p, ledger, lambda *_: r)
                self.assertEqual(response['results']['one']['state'], 'completed')
                self.assertEqual(ledger.usage()['money_usd'], '1')
                self.assertEqual(response['results']['one']['result']['measurement_provenance']['money_usd']['status'], status)

    def test_larger_estimate_increases_held_admission_quantity_without_becoming_measured(self):
        p = plan()
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p); r = result()
            r['measurements']['money_usd'] = '2.5'
            r['measurement_provenance']['money_usd'] = {'status': 'estimated', 'source_ref': 'cost-estimator'}
            response = self.execute(p, ledger, lambda *_: r)
            self.assertEqual(ledger.usage()['money_usd'], '2.5')
            self.assertEqual(response['results']['one']['result']['measurement_provenance']['money_usd']['status'], 'estimated')

    def test_caller_accounting_policy_does_not_promote_estimate_to_observation(self):
        p = plan(); p['resource_limits']['money_usd']['measurement_policy']['estimated'] = 'use_declared_amount'
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p); r = result(measured='0')
            r['measurement_provenance']['money_usd'] = {'status': 'estimated', 'source_ref': 'caller-estimator'}
            response = self.execute(p, ledger, lambda *_: r)
            self.assertEqual(ledger.usage()['money_usd'], '0')
            self.assertEqual(response['results']['one']['result']['measurement_provenance']['money_usd']['status'], 'estimated')

    def test_unbound_source_does_not_dispatch_or_replay_as_fresh(self):
        p = plan(); p['sources'][0]['binding'] = {'mode': 'unbound'}
        with tempfile.TemporaryDirectory() as temporary:
            called = []; ledger = self.ledger(temporary, p)
            response = self.execute(p, ledger, lambda *_: called.append('callback'),
                packet_loader=lambda _: called.append('loader'))
            self.assertEqual(called, [])
            self.assertEqual(response['results']['one']['reason'], 'unbound_source_snapshot')
            self.assertEqual(response['results']['one']['dispatch'], 'no_dispatch')

    def test_bound_snapshot_replay_is_explicit_and_revision_change_dispatches_again(self):
        p = plan(); p['sources'][0]['binding'] = {'mode': 'revision', 'revision': 'A'}
        raw = {'text': 'A'}; loaded = []
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p)
            def loader(_): loaded.append(raw['text']); return raw
            first = self.execute(p, ledger, lambda _, packet: result(packet), packet_loader=loader)
            raw['text'] = 'B'
            replay = self.execute(p, ledger, lambda _, packet: result(packet), packet_loader=loader)
            self.assertEqual(replay['results']['one']['dispatch'], 'replayed_bound_snapshot')
            self.assertEqual(replay['results']['one']['source_bindings'], {'source': {'mode': 'revision', 'revision': 'A'}})
            self.assertEqual(replay['results']['one']['result']['output']['text'], 'A')
            p['sources'][0]['binding']['revision'] = 'B'
            fresh = self.execute(p, ledger, lambda _, packet: result(packet), packet_loader=loader)
            self.assertEqual(fresh['results']['one']['dispatch'], 'callback_dispatched')
            self.assertEqual(fresh['results']['one']['result']['output']['text'], 'B')
            self.assertNotEqual(first['results']['one']['attempt_id'], fresh['results']['one']['attempt_id'])
            self.assertEqual(loaded, ['A', 'B'])

    def test_invalid_metadata_preserves_first_finite_candidate_but_not_completion(self):
        p = plan()
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p); r = result({'important_first_candidate': ['retain']})
            r['measurements']['money_usd'] = '-1'
            response = self.execute(p, ledger, lambda *_: r)
            folder = next(Path(temporary).glob('attempt-*'))
            self.assertEqual(ref._read(folder / 'returned_first.json'), r)
            self.assertFalse((folder / 'completion.json').exists())
            self.assertFalse((folder / 'result.json').exists())
            self.assertEqual(response['results']['one']['state'], 'uncertain')
            self.assertEqual(ledger.usage()['money_usd'], '1')

    def test_completion_measurements_or_provenance_cannot_drift_from_returned_result(self):
        for changed in ('measurements', 'measurement_provenance'):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as temporary:
                p = plan(); p['resource_limits']['money_usd']['limit'] = '1'
                ledger = self.ledger(temporary, p); self.execute(p, ledger, lambda *_: result())
                path = next(Path(temporary).glob('attempt-*/completion.json')); completion = ref._read(path)
                if changed == 'measurements': completion[changed]['money_usd'] = '0'
                else: completion[changed]['money_usd']['status'] = 'estimated'
                path.write_bytes(ref.encoded(completion))
                with self.assertRaises(ref.PlanError): ledger.usage()
                with self.assertRaises(ref.PlanError): self.execute(p, ledger, lambda *_: result())
                other = deepcopy(p); other['id'] = 'another-plan'
                with self.assertRaises(ref.PlanError): self.execute(other, ledger, lambda *_: result())

    def test_nonzero_opening_known_usage_requires_typed_reported_origin(self):
        p = plan(); balances = {d: {'measured': '1', 'reserved_unknown': '0',
            'provenance': {'status': 'declared', 'source_ref': 'not-an-observation'}} for d in ref.DIMENSIONS}
        with tempfile.TemporaryDirectory() as temporary, self.assertRaises(ref.PlanError):
            ref.ResourceLedger(temporary, p['resource_limits'], budget_id='shared', opening_balances=balances)

    def test_large_resource_amount_cannot_round_away_small_budget_increment(self):
        p = plan(); large = '1' + '0' * 80
        p['resource_limits']['money_usd']['limit'] = large
        p['methods'][0]['reservation']['money_usd'] = '0.00000001'
        balances = {d: {'measured': '0', 'reserved_unknown': '0',
            'provenance': {'status': 'provider_reported', 'source_ref': 'opening-provider-record'}} for d in ref.DIMENSIONS}
        balances['money_usd']['measured'] = large
        with tempfile.TemporaryDirectory() as temporary:
            ledger = ref.ResourceLedger(temporary, p['resource_limits'], budget_id='shared', opening_balances=balances)
            called = []; response = self.execute(p, ledger, lambda *_: called.append(1))
            self.assertEqual(called, [])
            self.assertEqual(response['results']['one']['reason'], 'resource_limit_exceeded_money_usd')
        self.assertEqual(ref.exact_sum(ref.quantity('1.25'), ref.quantity('0.000000000000000000000000000001')),
            ref.quantity('1.250000000000000000000000000001'))

    def test_loaded_packet_snapshot_is_preserved_without_claiming_source_verification(self):
        p = plan(); p['sources'][0]['binding'] = {'mode': 'content_fingerprint', 'sha256': 'a' * 64}
        original = {'source_projection': ['preserve-first']}
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p)
            def callback(context, packet):
                self.assertEqual(context['source_binding_verification'], 'caller_declared_not_verified')
                packet['source_projection'].clear(); return result()
            response = self.execute(p, ledger, callback, packet_loader=lambda _: original)
            path = next(Path(temporary).glob('attempt-*/loaded_packet_first.json'))
            loaded = ref._read(path)
            self.assertEqual(loaded['packet'], original)
            self.assertTrue(loaded['loader_supplied'])
            outcome = response['results']['one']
            self.assertEqual(outcome['loaded_packet_sha256'], ref.digest(loaded))
            self.assertEqual(outcome['source_binding_verification'], 'caller_declared_not_verified')
            loaded['packet'] = {'changed': True}; path.write_bytes(ref.encoded(loaded))
            with self.assertRaises(ref.PlanError): ledger.usage()

    def test_direct_execution_respects_explicit_selection_before_dispatch(self):
        p = plan(); p['variant_axes'] = {'prompt': {'values': ['A', 'B']}}
        p['selection'] = {'mode': 'indices', 'indices': [0]}
        with tempfile.TemporaryDirectory() as temporary:
            ledger = self.ledger(temporary, p); called = []
            with self.assertRaises(ref.PlanError) as caught:
                self.execute(p, ledger, lambda *_: called.append('callback'), index=1,
                    packet_loader=lambda _: called.append('loader'))
            self.assertEqual(caught.exception.code, 'variant_not_selected')
            self.assertEqual(called, [])
            self.assertEqual(list(Path(temporary).glob('attempt-*')), [])
            self.assertEqual(ref.variant_at(p, 1), {'prompt': 'B'})  # coordinate inspection remains useful


if __name__ == '__main__': unittest.main()
