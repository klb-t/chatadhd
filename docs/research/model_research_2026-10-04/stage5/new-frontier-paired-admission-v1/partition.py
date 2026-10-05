#!/usr/bin/env python3
"""Offline, data-configured partition of one frozen manifest; no dispatch/scoring."""
from __future__ import annotations
import argparse
from copy import deepcopy
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import sys


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), default=str).encode('utf-8') + b'\n'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def bound(path, expected):
    raw = Path(path).read_bytes()
    if sha(raw) != expected:
        raise ValueError('frozen_source_hash_mismatch')
    return raw


def pointer(value, name):
    for part in name.split('/')[1:]:
        value = value[part.replace('~1', '/').replace('~0', '~')]
    return value


def grouped(manifest, config):
    operations = manifest['operations']
    ids = [op['operation_id'] for op in operations]
    if len(ids) != len(set(ids)):
        raise ValueError('duplicate_physical_operation_id')
    cases, methods = config['expected_case_ids'], config['expected_method_ids']
    if not cases or not methods or len(cases) != len(set(cases)) or len(methods) != len(set(methods)):
        raise ValueError('invalid_partition_dimensions')
    observed = [(pointer(op, config['case_pointer']), pointer(op, config['method_pointer'])) for op in operations]
    expected = [(case, method) for case in cases for method in methods]
    if observed != expected:
        raise ValueError('frozen_order_or_pair_membership_mismatch')
    width = len(methods)
    return [(case, operations[index * width:(index + 1) * width]) for index, case in enumerate(cases)]


def write_exact(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if path.read_bytes() != raw:
            raise ValueError('conflicting_partition_artifact')
    else:
        path.write_bytes(raw)


def prepare(config_path, output):
    config_path, output = Path(config_path).resolve(), Path(output).resolve()
    config = json.loads(config_path.read_bytes())
    base = config_path.parent
    project = base
    while not (project / 'loom/tools/structure/research_programme_manifest.py').exists():
        if project == project.parent:
            raise ValueError('repository_root_missing')
        project = project.parent
    sys.path.insert(0, str(project / 'loom/tools/structure'))
    import research_programme_manifest as manifests
    import research_programme_runner as runner
    manifest_path = (base / config['master_manifest']['file']).resolve()
    raw_master = bound(manifest_path, config['master_manifest']['sha256'])
    master = manifests.read_manifest_bytes(raw_master)
    manifests.load_manifest(master, base_dir=manifest_path.parent)
    partitions = grouped(master, config)
    policy_raw = bound(base / config['policy']['file'], config['policy']['sha256'])
    policy = json.loads(policy_raw)
    source_plan_raw = bound(base / config['cost_plan']['file'], config['cost_plan']['sha256'])
    source_plan = json.loads(source_plan_raw)
    selection_raw = bound(base / config['selection']['file'], config['selection']['sha256'])
    quotes = source_plan['quotes']
    reservations, _, _ = runner.price_plan(manifests.load_operations(master, base_dir=manifest_path.parent), quotes, policy)
    total = sum(reservations.values(), Decimal(0))
    if total != Decimal(source_plan['reservation_usd']):
        raise ValueError('source_total_forecast_mismatch')
    source_blobs = [(pin['file'], bound(manifest_path.parent / pin['file'], pin['sha256']))
                    for pin in master['metadata'].get('source_manifests', [])]
    entries, flattened = [], []
    for index, (case, rows) in enumerate(partitions, 1):
        pair_dir = output / ('pair-%02d' % index)
        prepared_dir = pair_dir / 'prepared'
        child = deepcopy(master)
        child['stage_id'] = config['child_stage_prefix'] + '.pair-%02d' % index
        child['operations'] = deepcopy(rows)
        child['metadata']['fixed_partition'] = {
            'schema': 'loom.fixed_manifest_partition/1',
            'case_id': case, 'ordinal': index, 'total_partitions': len(partitions),
            'master_manifest_sha256': sha(raw_master),
            'selection_freeze_sha256': sha(selection_raw),
            'configuration_sha256': sha(config_path.read_bytes()),
            'physical_operations_unchanged': True,
            'selection_policy': 'fixed_complete_source_order_stop_before_first_unaffordable_pair',
        }
        for op in rows:
            write_exact(prepared_dir / op['request_file'], bound(manifest_path.parent / op['request_file'], op['request_sha256']))
        for name, raw in source_blobs:
            write_exact(prepared_dir / name, raw)
        raw_child = canonical(child)
        manifests.load_manifest(child, base_dir=prepared_dir)
        child_ops = manifests.load_operations(child, base_dir=prepared_dir)
        amounts, pairs, expected = runner.price_plan(child_ops, quotes, policy)
        subtotal = sum(amounts.values(), Decimal(0))
        cost = {
            'schema': 'loom.research_programme_forecast/1', 'status': 'offline_forecast_only',
            'stage_id': child['stage_id'], 'manifest_sha256': sha(raw_child),
            'master_manifest_sha256': sha(raw_master), 'source_cost_plan_sha256': sha(source_plan_raw),
            'policy_sha256': sha(policy_raw), 'operations': len(rows), 'paid_calls': 0, 'network_calls': 0,
            'reservation_usd': str(subtotal), 'actual_cost_usd': None,
            'operation_reservations_usd': {op['operation_id']: str(amounts[op['operation_id']]) for op in rows},
            'model_provider_pairs': pairs, 'quotes': quotes,
            'billing_verification_timing': policy['billing_verification_timing'],
            'next_pair_requires_previous_billing_and_cumulative_budget_gate': True,
            'whole_cohort_completion_guaranteed': False,
        }
        write_exact(prepared_dir / 'manifest.json', raw_child)
        write_exact(pair_dir / 'operator-policy.json', policy_raw)
        write_exact(pair_dir / 'cost-plan-v1.json', canonical(cost))
        entries.append({'ordinal': index, 'case_id': case, 'stage_id': child['stage_id'],
                        'manifest_file': str((prepared_dir / 'manifest.json').relative_to(output)),
                        'manifest_sha256': sha(raw_child), 'reservation_usd': str(subtotal),
                        'operation_ids': [op['operation_id'] for op in rows],
                        'request_sha256': [op['request_sha256'] for op in rows]})
        flattened.extend(rows)
    if flattened != master['operations']:
        raise ValueError('partition_does_not_preserve_full_original_operation_rows')
    receipt = {'schema': 'loom.fixed_manifest_partition_receipt/1', 'status': 'offline_partition_verified',
               'source_manifest_sha256': sha(raw_master), 'source_cost_plan_sha256': sha(source_plan_raw),
               'configuration_sha256': sha(config_path.read_bytes()), 'source_selection_sha256': sha(selection_raw),
               'source_policy_sha256': sha(policy_raw), 'paid_calls': 0, 'network_calls': 0,
               'partitions': entries, 'operations': len(flattened), 'whole_reservation_usd': str(total),
               'operation_rows_exactly_preserved': True, 'request_bytes_exactly_preserved': True,
               'physical_ids_exactly_preserved': True, 'source_prepared_bytes_exactly_preserved': True,
               'frozen_precollection_order': True, 'outcomes_inspected': False,
               'method_tasks_distinct_not_same_task_head_to_head': True,
               'programme_id_in_source': master['programme_id'],
               'programme_id_in_operator_policy': policy['programme_id'],
               'invocation_programme_projection_required': master['programme_id'] != policy['programme_id'],
               'initial_reference_actual_usd': source_plan['previous_fully_verified_actual_usd'],
               'budget': 'Existing native cumulative ledger and cap admission remain unchanged. Before each pair, root binds its invocation to the existing key programme and freezes derived bytes. Never dispatch master or skip an unaffordable pair to select later outcomes.'}
    write_exact(output / 'PARTITION.json', canonical(receipt))
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    result = prepare(args.config, args.output)
    print(json.dumps({k: result[k] for k in ['status', 'operations', 'whole_reservation_usd', 'paid_calls', 'network_calls']}))


if __name__ == '__main__':
    main()
