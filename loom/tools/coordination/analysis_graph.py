"""Execute a selected AnalysisPlan variant with the existing local graph algebra.

Creates reviewable packet artifacts, never writes the native graph, loads
credentials or dispatches a provider. Plans and resource limits remain data.
"""
from copy import deepcopy
import os
from pathlib import Path
import time

from .leases import CoordinationError, digest, execute_once
from ..contracts import analysis_plan_ref as plans
from ..structure.agentic_graph_v1 import packet as codec

CALLBACK = ('graph:apply_diff', 'local:graph_packet')
CAPABILITIES = ('local', 'graph:apply_diff')
ADAPTER = 'loom.coordinated_analysis_graph/1'


def _new_artifact_directory(directory):
    missing = []
    cursor = directory
    while not cursor.exists():
        missing.append(cursor)
        cursor = cursor.parent
    directory.mkdir(parents=True, exist_ok=False)
    # Persist newly created ancestor entries as well as the files fsynced below.
    for child in missing:
        descriptor = os.open(child.parent, os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def _validate(plan, index, packet):
    plans.validate_plan(plan)
    plans.variant_at(plan, index)
    if plan['selection']['mode'] == 'indices' and index not in plan['selection']['indices']:
        raise plans.PlanError('variant_not_selected')
    codec.validate_packet(packet)
    for source in plan['sources']:
        binding = source['binding']
        if binding.get('mode') != 'pinned_snapshot' or binding.get('snapshot_id') != packet['packet_id']:
            raise CoordinationError('graph_source_snapshot_mismatch')
    for method in plan['methods']:
        if (method['method'], method['runtime']['id']) != CALLBACK:
            continue
        config = method['config']
        if (set(config) - {'diff', 'policy', 'explicitly_accepted', 'input_dependency'}
                or not {'diff', 'policy'} <= set(config)
                or type(config.get('explicitly_accepted', False)) is not bool):
            raise CoordinationError('graph_method_configuration_invalid')
        if method['runtime']['config'] or method['tools']:
            raise CoordinationError('local_graph_runtime_options_unsupported')
        dependency = config.get('input_dependency')
        if dependency is not None and dependency not in method['depends_on']:
            raise CoordinationError('graph_input_dependency_not_declared')
        if digest(method['acceptance_policy']) != digest(config['policy']):
            raise CoordinationError('graph_acceptance_policy_mismatch')


def graph_callback(context, packet):
    """Use existing validation, reversible history and acceptance unchanged."""
    cpu, wall = time.process_time_ns(), time.perf_counter_ns()
    config = context['method']['config']
    dependency = config.get('input_dependency')
    if dependency is not None:
        packet = context['dependencies'][dependency]['result']['output']['selected_packet']
    selected, application = codec.apply_diff(packet, config['diff'], config['policy'],
        explicitly_accepted=config.get('explicitly_accepted', False))
    preview = codec.preview_diff(packet, config['diff'])
    output = {'selected_packet': selected, 'preview': preview, 'application': application,
              'canonical_store_written': False, 'acceptance_establishes_content_truth': False,
              'source_binding_verification': 'input_packet_content_hash_verified_not_source_authenticity'}
    # Uninstrumented dimensions follow the caller's reservation policy.
    measurements = {'money_usd': '0', 'calls': '0', 'input_tokens': '0', 'output_tokens': '0',
                    'cpu_seconds': str((time.process_time_ns() - cpu) / 1e9),
                    'wall_seconds': str((time.perf_counter_ns() - wall) / 1e9)}
    return {'output': output, 'measurements': measurements,
            'measurement_provenance': {key: {'status': 'instrument_measured',
                'source_ref': ADAPTER + ':local-graph-callback',
                'scope': 'callback_only_excludes_preparation_coordination_and_persistence'}
                for key in measurements}}


def run_coordinated_analysis_graph(store, *, task_id, source_commit, owner, plan,
        variant_index, packet, ledger_directory, output_root, lease_seconds,
        input_binding=None):
    started = time.perf_counter_ns()
    plan, packet = deepcopy(plan), deepcopy(packet)
    _validate(plan, variant_index, packet)
    effective = deepcopy(plan)
    if '_coordination_execution' in effective['extensions']:
        raise CoordinationError('reserved_adapter_extension')
    effective['extensions']['_coordination_execution'] = {
        'adapter': ADAPTER, 'source_commit': source_commit, 'packet_sha256': digest(packet)}
    spec = {'adapter': ADAPTER, 'plan': plan, 'effective_plan': effective,
            'variant_index': variant_index, 'packet': packet,
            'ledger_directory': str(Path(ledger_directory).resolve()),
            'output_root': str(Path(output_root).resolve()), 'input_binding': input_binding}
    store.register(task_id, source_commit=source_commit, spec=spec)

    def callback(bound, lease):
        directory = Path(bound['output_root']) / digest(task_id) / ('fence-%d' % lease['fence'])
        _new_artifact_directory(directory)
        plans._write_new(directory / 'input_first.json', bound)
        ledger = plans.ResourceLedger(bound['ledger_directory'], bound['plan']['resource_limits'],
                                     budget_id=bound['plan']['resource_budget_ref'])
        def guarded(context, loaded):
            # No background thread; renewal cannot resurrect an expired lease.
            store.renew(lease, lease_seconds=lease_seconds)
            return graph_callback(context, loaded)
        result = plans.execute_variant(bound['effective_plan'], bound['variant_index'], ledger,
            {CALLBACK: guarded}, capabilities=CAPABILITIES,
            packet_loader=lambda _: deepcopy(bound['packet']))
        states = [row['state'] for row in result['results'].values()]
        outcome = {'analysis_result': result, 'artifact_directory': str(directory),
                   'all_methods_completed': all(state == 'completed' for state in states),
                   'uncertain_methods': [name for name, row in result['results'].items()
                                         if row['state'] == 'uncertain'],
                   'execution_completed_is_not_semantic_success': True,
                   'network_calls': 0, 'canonical_store_written': False}
        plans._write_new(directory / 'result_first.json', outcome)
        return outcome

    execution = execute_once(store, task_id, owner=owner, lease_seconds=lease_seconds,
                             callback=callback)
    return {'execution': execution, 'timing': {
        'adapter_wall_seconds': (time.perf_counter_ns() - started) / 1e9,
        'includes': 'snapshot_validation_registration_ledger_graph_artifacts_completion',
        'excludes': 'CLI_input_read_database_initialization_JSON_output'},
        'network_calls': 0, 'canonical_store_written': False}
