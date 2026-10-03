"""Data-driven method DAG and durable callback-only reference executor.

No provider client, key loading, worker allocation or graph application. Explicit
resource limits belong to one shared ledger; presets never become limits.
"""
from __future__ import annotations
from contextlib import contextmanager
from copy import deepcopy
from decimal import Decimal, InvalidOperation
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import threading
from typing import Any

from jsonschema import Draft202012Validator
try:
    from .validate import ContractValidator, canonical_bytes
except ImportError:
    from validate import ContractValidator, canonical_bytes

SCHEMA_PATH = Path(__file__).resolve().parents[3] / 'docs/contracts/analysis_plan.schema.json'
DIMENSIONS = ('money_usd', 'cpu_seconds', 'gpu_seconds', 'input_tokens',
    'output_tokens', 'calls', 'agents', 'wall_seconds', 'memory_bytes', 'storage_bytes')
KNOWN_STATUSES = ('instrument_measured', 'provider_reported')
MEASUREMENT_STATUSES = KNOWN_STATUSES + ('estimated', 'unknown', 'declared')
ACCOUNTING_TREATMENTS = ('use_declared_amount', 'retain_reservation', 'max_reservation_amount')


class PlanError(ValueError):
    def __init__(self, code):
        self.code = code
        super().__init__(code)


def encoded(value):
    def check(v):
        if v is None or type(v) in (bool, str, int): return
        if type(v) is float and math.isfinite(v): return
        if type(v) is list:
            for item in v: check(item)
            return
        if type(v) is dict and all(type(k) is str for k in v):
            for item in v.values(): check(item)
            return
        raise PlanError('finite_json_required')
    check(value)
    try: return canonical_bytes(value)
    except (ValueError, TypeError, UnicodeError, RecursionError):
        raise PlanError('finite_json_required') from None


def digest(value): return hashlib.sha256(encoded(value)).hexdigest()


def plan_identity(plan):
    normalized = deepcopy(plan)
    normalized['methods'] = sorted(normalized['methods'], key=lambda m: m['id'])
    return digest(normalized)


def quantity(value):
    if type(value) not in (str, int, float): raise PlanError('resource_quantity_invalid')
    try: result = Decimal(str(value))
    except InvalidOperation: raise PlanError('resource_quantity_invalid') from None
    if not result.is_finite() or result < 0: raise PlanError('resource_quantity_invalid')
    return result


def validate_limits(limits):
    if type(limits) is not dict or set(limits) != set(DIMENSIONS):
        raise PlanError('all_resource_dimensions_required')
    for rule in limits.values():
        if (type(rule) is not dict or set(rule) != {'limit', 'accounting', 'measurement_policy'}
                or rule['accounting'] not in ('cumulative', 'peak')):
            raise PlanError('resource_limit_policy_invalid')
        if rule['limit'] is not None: quantity(rule['limit'])
        policy = rule['measurement_policy']
        if (type(policy) is not dict or set(policy) != set(MEASUREMENT_STATUSES)
                or any(v not in ACCOUNTING_TREATMENTS for v in policy.values())):
            raise PlanError('resource_measurement_policy_invalid')


def validate_measurement_provenance(value):
    if (type(value) is not dict or value.get('status') not in MEASUREMENT_STATUSES
            or type(value.get('source_ref')) is not str or not value['source_ref'].strip()):
        raise PlanError('measurement_provenance_contract')
    # Domain/provider/instrument-specific details stay open data.
    encoded(value)


def validate_response(response):
    if type(response) is not dict or set(response) != {'output', 'measurements', 'measurement_provenance'}:
        raise PlanError('callback_result_contract')
    measurements = response['measurements']; provenance = response['measurement_provenance']
    if type(measurements) is not dict or type(provenance) is not dict:
        raise PlanError('callback_measurement_contract')
    if set(measurements) - set(DIMENSIONS) or set(provenance) - set(DIMENSIONS):
        raise PlanError('unknown_measurement_dimension')
    for d, origin in provenance.items():
        validate_measurement_provenance(origin)
        if d not in measurements: raise PlanError('measurement_provenance_without_quantity')
    for d, amount in measurements.items():
        if amount is not None:
            quantity(amount)
            if d not in provenance: raise PlanError('measurement_provenance_missing')
        elif d in provenance and provenance[d]['status'] in KNOWN_STATUSES:
            raise PlanError('known_measurement_missing_quantity')


def source_bindings(context):
    return {s['id']: deepcopy(s['binding']) for s in context['sources']}


def axis_size(axis):
    if 'values' in axis: return len(axis['values'])
    r = axis['range']; return max(0, (r['stop'] - r['start'] + r['step'] - 1) // r['step'])


def cardinality(plan):
    total = 1
    for name in sorted(plan['variant_axes']): total *= axis_size(plan['variant_axes'][name])
    return total


def variant_at(plan, index):
    if type(index) is not int or not 0 <= index < cardinality(plan):
        raise PlanError('variant_index_out_of_range')
    selected = {}
    for name in reversed(sorted(plan['variant_axes'])):
        axis = plan['variant_axes'][name]; size = axis_size(axis)
        index, offset = divmod(index, size)
        selected[name] = deepcopy(axis['values'][offset]) if 'values' in axis else axis['range']['start'] + offset * axis['range']['step']
    return {name: selected[name] for name in sorted(selected)}


def selected_variants(plan):
    selection = plan['selection']
    indices = range(cardinality(plan)) if selection['mode'] == 'all' else selection['indices']
    for index in indices: yield index, variant_at(plan, index)


def topological(plan):
    methods = {m['id']: m for m in plan['methods']}
    if len(methods) != len(plan['methods']): raise PlanError('duplicate_method_id')
    remaining = {name: set(m['depends_on']) for name, m in methods.items()}; ordered = []
    if any(dep not in methods for values in remaining.values() for dep in values):
        raise PlanError('missing_dependency')
    while remaining:
        ready = sorted(name for name, dependencies in remaining.items() if not dependencies)
        if not ready: raise PlanError('dependency_cycle')
        for name in ready:
            ordered.append(methods[name]); del remaining[name]
        for deps in remaining.values(): deps.difference_update(ready)
    return ordered


def validate_plan(plan):
    encoded(plan); contracts = ContractValidator()
    schema = contracts.schemas['analysis_plan.schema.json']
    validator = Draft202012Validator(schema, registry=contracts.registry, format_checker=contracts.format_checker)
    if next(validator.iter_errors(plan), None) is not None: raise PlanError('analysis_plan_schema')
    validate_limits(plan['resource_limits'])
    source_ids = [s['id'] for s in plan['sources']]
    if len(set(source_ids)) != len(source_ids): raise PlanError('duplicate_source_id')
    for axis in plan['variant_axes'].values():
        if axis_size(axis) <= 0: raise PlanError('empty_variant_axis')
        if 'values' in axis and len({encoded(v) for v in axis['values']}) != len(axis['values']):
            raise PlanError('duplicate_variant_value')
    for method in topological(plan):
        if set(method['variant_axes']) - plan['variant_axes'].keys(): raise PlanError('unknown_variant_axis')
        if set(method['source_refs']) - set(source_ids): raise PlanError('unknown_source_reference')
        if set(method['reservation']) != set(DIMENSIONS): raise PlanError('all_reservation_dimensions_required')
        for value in method['reservation'].values(): quantity(value)
    if plan['selection']['mode'] == 'indices':
        indices = plan['selection']['indices']
        if len(set(indices)) != len(indices) or any(i >= cardinality(plan) for i in indices):
            raise PlanError('selection_invalid')
    return plan


def _write_new(path, value):
    raw = encoded(value); path = Path(path)
    with path.open('xb') as stream:
        stream.write(raw); stream.flush(); os.fsync(stream.fileno())
    descriptor = os.open(path.parent, os.O_RDONLY)
    try: os.fsync(descriptor)
    finally: os.close(descriptor)


def _read(path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result: raise PlanError('duplicate_ledger_key')
            result[key] = value
        return result
    try:
        value = json.loads(Path(path).read_text(), object_pairs_hook=pairs,
            parse_constant=lambda _: (_ for _ in ()).throw(PlanError('nonfinite_ledger')))
        encoded(value); return value
    except (OSError, ValueError, TypeError): raise PlanError('ledger_unreadable') from None


class ResourceLedger:
    """Cross-process locked, append-only attempts; unknown quantities remain held.

    `peak` admission accounts for held reservations concurrently plus the greatest
    completed measurement. This is conservative after an uncertain callback.
    """
    def __init__(self, directory, limits, *, budget_id, opening_balances=None):
        validate_limits(limits)
        self.directory = Path(directory); self.directory.mkdir(parents=True, exist_ok=True)
        self.lock = threading.RLock(); self.limits = deepcopy(limits); self.budget_id = budget_id
        with self.locked():
            path = self.directory / 'ledger.json'
            existing = _read(path) if path.exists() else None
            balances = opening_balances if opening_balances is not None else existing['opening_balances'] if existing else {
                d: {'measured': '0', 'reserved_unknown': '0',
                    'provenance': {'status': 'declared', 'source_ref': 'caller-declared-new-ledger:' + budget_id,
                        'scope': 'opening_accounting_baseline_not_usage_measurement'}} for d in DIMENSIONS}
            if type(balances) is not dict or set(balances) != set(DIMENSIONS):
                raise PlanError('opening_balances_dimensions_required')
            for balance in balances.values():
                if type(balance) is not dict or set(balance) != {'measured', 'reserved_unknown', 'provenance'}:
                    raise PlanError('opening_balance_contract')
                quantity(balance['measured']); quantity(balance['reserved_unknown'])
                validate_measurement_provenance(balance['provenance'])
                if quantity(balance['measured']) and balance['provenance']['status'] not in KNOWN_STATUSES:
                    raise PlanError('opening_known_usage_requires_reported_provenance')
            self.opening_balances = deepcopy(balances)
            header = {'schema': 'loom.analysis_resource_ledger/1', 'budget_id': budget_id,
                'resource_limits': limits, 'opening_balances': balances}
            if path.exists():
                if existing != header: raise PlanError('shared_budget_identity_or_limits_changed')
            else: _write_new(path, header)

    @contextmanager
    def locked(self):
        with self.lock:
            with (self.directory / 'ledger.lock').open('a+b') as handle:
                fcntl.flock(handle, fcntl.LOCK_EX)
                try: yield
                finally: fcntl.flock(handle, fcntl.LOCK_UN)

    def _usage(self, additional=None):
        cumulative = {d: quantity(self.opening_balances[d]['measured']) for d in DIMENSIONS}
        peaks = dict(cumulative)
        held = {d: quantity(self.opening_balances[d]['reserved_unknown']) for d in DIMENSIONS}
        for folder in sorted(self.directory.glob('attempt-*')):
            reservation = _read(folder / 'reservation.json')
            if reservation['attempt_id'] != folder.name[8:]: raise PlanError('attempt_directory_identity')
            completion = _read(folder / 'completion.json') if (folder / 'completion.json').exists() else None
            if completion is not None:
                result = self._completed(folder, reservation)
            for d in DIMENSIONS:
                amount = result['measurements'].get(d) if completion else None
                if amount is None:
                    held[d] += quantity(reservation['reservation'][d])
                else:
                    status = result['measurement_provenance'][d]['status']
                    treatment = self.limits[d]['measurement_policy'][status]
                    reserved = quantity(reservation['reservation'][d]); q = quantity(amount)
                    if treatment == 'retain_reservation': held[d] += reserved
                    elif treatment == 'max_reservation_amount': held[d] += max(reserved, q)
                    else: cumulative[d] += q; peaks[d] = max(peaks[d], q)
        extra = {d: quantity(additional[d]) if additional else Decimal(0) for d in DIMENSIONS}
        return {d: cumulative[d] + held[d] + extra[d] if self.limits[d]['accounting'] == 'cumulative'
            else max(peaks[d], held[d] + extra[d]) for d in DIMENSIONS}

    def usage(self):
        with self.locked(): return {d: str(v) for d, v in self._usage().items()}

    def _completed(self, folder, reservation):
        """Bind accounting metadata to both preserved first return and result."""
        completion = _read(folder / 'completion.json'); result = _read(folder / 'result.json')
        validate_response(result)
        if (type(completion) is not dict or set(completion) != {
                'state', 'measurements', 'measurement_provenance', 'result_sha256'}
                or completion['state'] != 'completed'
                or completion['measurements'] != result['measurements']
                or completion['measurement_provenance'] != result['measurement_provenance']):
            raise PlanError('completion_result_metadata_drift')
        if (completion['result_sha256'] != digest(result)
                or digest(_read(folder / 'returned_first.json')) != digest(result)):
            raise PlanError('result_receipt_hash_drift')
        if reservation['attempt_id'] != folder.name[8:]: raise PlanError('attempt_directory_identity')
        return result

    def reserve(self, attempt_id, reservation, context):
        if len(attempt_id) != 64 or any(c not in '0123456789abcdef' for c in attempt_id):
            raise PlanError('attempt_id_invalid')
        with self.locked():
            folder = self.directory / ('attempt-' + attempt_id)
            if folder.exists(): raise PlanError('attempt_already_exists_no_retry')
            proposed = self._usage(additional=reservation)
            for d in DIMENSIONS:
                limit = self.limits[d]['limit']
                if limit is not None and proposed[d] > quantity(limit): raise PlanError('resource_limit_exceeded_' + d)
            folder.mkdir()
            _write_new(folder / 'reservation.json', {'schema': 'loom.analysis_attempt_reservation/1',
                'attempt_id': attempt_id, 'reservation': reservation, 'context': context,
                'state': 'reserved_before_callback'})
            descriptor = os.open(self.directory, os.O_RDONLY)
            try: os.fsync(descriptor)
            finally: os.close(descriptor)
        return folder

    def lookup(self, attempt_id):
        with self.locked():
            folder = self.directory / ('attempt-' + attempt_id)
            if not folder.exists(): return None
            reservation = _read(folder / 'reservation.json')
            if reservation['attempt_id'] != attempt_id: raise PlanError('attempt_directory_identity')
            bindings = source_bindings(reservation['context'])
            if (folder / 'completion.json').exists():
                result = self._completed(folder, reservation)
                return {'state': 'completed', 'attempt_id': attempt_id, 'result': result,
                    'dispatch': 'replayed_bound_snapshot', 'source_bindings': bindings}
            uncertain = {'state': 'uncertain', 'attempt_id': attempt_id,
                'reason': 'reservation_without_completion_no_retry', 'dispatch': 'no_retry',
                'source_bindings': bindings}
            if (folder / 'returned_first.json').exists():
                uncertain['returned_first'] = _read(folder / 'returned_first.json')
            return uncertain


def execute_variant(plan, index, ledger, callbacks, *, capabilities, packet_loader=None):
    """Execute registered callbacks only; return every branch result to caller.

    Callbacks receive method configuration, source refs, variant coordinates,
    optional caller-supplied packet and dependency receipts. No apply_diff call
    occurs here. Callback side effects are the registrant's responsibility.
    """
    validate_plan(plan)
    if ledger.limits != plan['resource_limits'] or ledger.budget_id != plan['resource_budget_ref']:
        raise PlanError('plan_shared_limits_mismatch')
    variant = variant_at(plan, index); outputs = {}; plan_hash = plan_identity(plan)
    for method in topological(plan):
        axes = {name: variant[name] for name in sorted(method['variant_axes'])}
        ident = digest({'plan_sha256': plan_hash, 'method_id': method['id'], 'axes': axes,
            'dependency_attempts': {d: outputs[d]['attempt_id'] for d in sorted(method['depends_on'])}})
        existing = ledger.lookup(ident)
        if existing is not None:
            outputs[method['id']] = existing; continue
        missing = set(method['required_capabilities']) - set(capabilities)
        callback = callbacks.get((method['method'], method['runtime']['id']))
        blocked = [d for d in method['depends_on'] if outputs[d]['state'] != 'completed']
        sources = [deepcopy(s) for s in plan['sources'] if s['id'] in method['source_refs']]
        unbound = [s['id'] for s in sources if s['binding']['mode'] == 'unbound']
        if missing or callback is None or blocked or unbound:
            outputs[method['id']] = {'state': 'unavailable', 'attempt_id': ident,
                'reason': 'missing_capability' if missing else 'unregistered_callback' if callback is None
                    else 'dependency_unavailable' if blocked else 'unbound_source_snapshot',
                'missing_capabilities': sorted(missing), 'blocked_dependencies': blocked,
                'unbound_sources': unbound, 'dispatch': 'no_dispatch'}
            continue
        context = {'plan_id': plan['id'], 'plan_sha256': plan_hash, 'method': deepcopy(method),
            'semantic_contract': deepcopy(plan['semantic_contract']), 'axes': axes,
            'sources': sources,
            'dependencies': {d: deepcopy(outputs[d]) for d in method['depends_on']},
            'presets': deepcopy(plan['presets']), 'no_automatic_graph_application': True}
        try: folder = ledger.reserve(ident, method['reservation'], context)
        except PlanError as exc:
            if exc.code == 'attempt_already_exists_no_retry':
                outputs[method['id']] = ledger.lookup(ident); continue
            if exc.code.startswith('resource_limit_exceeded_'):
                outputs[method['id']] = {'state': 'unavailable', 'attempt_id': ident, 'reason': exc.code}; continue
            raise
        try:
            packet = deepcopy(packet_loader(deepcopy(context))) if packet_loader else None
            response = callback(deepcopy(context), packet)
            # Preserve the first finite JSON return even if its accounting metadata
            # is invalid. Such a candidate is not promoted to a completed attempt.
            _write_new(folder / 'returned_first.json', response)
            response = _read(folder / 'returned_first.json')
            validate_response(response)
            with ledger.locked():
                _write_new(folder / 'result.json', response)
                _write_new(folder / 'completion.json', {'state': 'completed',
                    'measurements': response['measurements'],
                    'measurement_provenance': response['measurement_provenance'], 'result_sha256': digest(response)})
            outputs[method['id']] = {'state': 'completed', 'attempt_id': ident, 'result': response,
                'dispatch': 'callback_dispatched', 'source_bindings': source_bindings(context)}
        except Exception:
            try: _write_new(folder / 'failure.json', {'state': 'uncertain', 'reason': 'callback_or_persistence_failed_no_retry'})
            except OSError: pass
            outputs[method['id']] = {'state': 'uncertain', 'attempt_id': ident,
                'reason': 'callback_or_persistence_failed_no_retry', 'dispatch': 'no_retry',
                'source_bindings': source_bindings(context),
                'first_return_preserved': (folder / 'returned_first.json').exists()}
            if (folder / 'returned_first.json').exists():
                outputs[method['id']]['returned_first'] = _read(folder / 'returned_first.json')
    result = {'plan_id': plan['id'], 'variant_index': index, 'variant': variant,
        'results': outputs, 'resource_usage': ledger.usage(), 'canonical_graph_writes': 0,
        'resource_usage_kind': 'policy_accounting_not_actual_measurement',
        'acceptance_policy_executed': False}
    with ledger.locked():
        receipt = ledger.directory / ('execution-result-' + digest(result) + '.json')
        if not receipt.exists(): _write_new(receipt, result)
    return result
