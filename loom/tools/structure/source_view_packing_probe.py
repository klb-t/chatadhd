"""Offline plan/replay for the explicitly replicated DEV q01 packing probe.

No network, credential access, gold labels or canonical graph writes. Execution
belongs to the unchanged bounded Jev runner and the integrating root agent.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from copy import deepcopy
from decimal import Decimal
from pathlib import Path

try:
    from . import graph_panel_live as panel, graph_panel_score_run as replay
    from . import jev_live_pilot as jev, openrouter_runner as safe
except ImportError:
    import graph_panel_live as panel, graph_panel_score_run as replay
    import jev_live_pilot as jev, openrouter_runner as safe

ROOT = panel.ROOT
SOURCE = ROOT / 'docs/research/source_view_experiment_2026-09-30'
DESTINATION = ROOT / 'docs/research/source_view_packing_probe_2026-09-30'
POLICY = Path(__file__).with_name('source_view_packing_probe_policy.json')
ARMS = ('historical_refute_v1', 'active_refute_v2')


class ProbeIntegrityError(ValueError):
    """Corrupt attribution/billing/artifacts abort replay, never become answers."""


def read(path):
    return safe.parse_json(Path(path).read_bytes())


def decimal_text(value):
    return format(value.normalize(), 'f')


def validate_policy(value):
    safe._keys(value, {'schema', 'split', 'selection', 'conditions',
        'measurements_per_cell', 'planned_requests', 'experiment_id',
        'budget_usd', 'batch_cap_usd', 'session_budget_reset'})
    if (value['schema'] != 'loom.source_view_packing_probe.policy/1'
            or value['split'] != 'diagnostic_dev' or value['session_budget_reset'] is not False
            or value['measurements_per_cell'] != 2 or value['planned_requests'] != 24
            or value['selection'] != {'largest_q01_absolute_deltas': 3,
                'stable_q01_controls': 1, 'tie_break': 'lexicographic_query_id'}
            or value['conditions'] != [
                {'id': 'A', 'origin': 'historical_refute_v1', 'questions': ['q01', 'q02']},
                {'id': 'B', 'origin': 'active_refute_v2', 'questions': ['q01', 'q02']},
                {'id': 'C', 'origin': 'historical_refute_v1', 'questions': ['q01']}]
            or value['budget_usd'] != '2' or value['batch_cap_usd'] != '0.10'):
        raise ProbeIntegrityError('packing_policy_drift')
    return value


def select_queries(historical, active):
    """Deterministic post-outcome DEV selection, using q01 probability only."""
    by_arm = []
    for rows in (historical, active):
        mapping = {}
        for row in rows:
            ident = row['query_id']; probability = row.get('support_noul')
            if (not isinstance(ident, str) or not ident or ident in mapping
                    or row.get('state') != 'completed'
                    or type(probability) not in (int, float) or not 0 <= probability <= 1):
                raise ProbeIntegrityError('selection_input_invalid')
            mapping[ident] = row
        by_arm.append(mapping)
    if set(by_arm[0]) != set(by_arm[1]):
        raise ProbeIntegrityError('selection_query_inventory_mismatch')
    rows = []
    for ident in sorted(by_arm[0]):
        a = Decimal(str(by_arm[0][ident]['support_noul']))
        b = Decimal(str(by_arm[1][ident]['support_noul']))
        rows.append({'query_id': ident, 'historical_q01': str(a), 'active_q01': str(b),
            'absolute_q01_delta': str(abs(a - b))})
    ranked = sorted(rows, key=lambda r: (-Decimal(r['absolute_q01_delta']), r['query_id']))
    if len(ranked) < 4 or Decimal(ranked[2]['absolute_q01_delta']) == 0:
        raise ProbeIntegrityError('selection_needs_three_changed_queries')
    chosen = [dict(r, selection_role='largest_observed_q01_delta') for r in ranked[:3]]
    controls = [r for r in rows if Decimal(r['absolute_q01_delta']) == 0]
    if not controls:
        raise ProbeIntegrityError('selection_stable_control_missing')
    chosen.append(dict(controls[0], selection_role='exact_q01_stable_control'))
    return chosen


def build_inputs(selected, manifests, policy):
    validate_policy(policy)
    if len(selected) != 4 or len({r['query_id'] for r in selected}) != 4:
        raise ProbeIntegrityError('expected_four_distinct_selected_queries')
    by_arm = {arm: {r['id']: r for r in manifests[arm]['requests']} for arm in ARMS}
    rows = []; bindings = []
    # Each measurement round has all cells, limiting a simple round-time confound.
    # Condition order remains fixed A/B/C and is declared, not causal randomization.
    for measurement in range(1, policy['measurements_per_cell'] + 1):
        for chosen in selected:
            ident = chosen['query_id']
            old, new = (by_arm[arm][ident] for arm in ARMS)
            if (old['language'] != new['language'] or old['body']['state'] != new['body']['state']
                    or old['body']['questions']['q01'] != new['body']['questions']['q01']):
                raise ProbeIntegrityError('source_q01_or_state_intervention_drift')
            state = safe.parse_json(old['body']['state']['text'])
            if state['query']['id'] != ident:
                raise ProbeIntegrityError('source_inner_query_identity_drift')
            for condition in policy['conditions']:
                original = by_arm[condition['origin']][ident]
                outer = 'svpack1_' + ident + '_' + condition['id'].lower() + '_m' + str(measurement)
                questions = {q: deepcopy(original['body']['questions'][q]) for q in condition['questions']}
                row = {'case_id': outer, 'language': original['language'],
                    'state': deepcopy(original['body']['state']), 'questions': questions}
                body = {'model': jev.MODEL, 'state': row['state'], 'questions': questions,
                    'provider': deepcopy(original['body']['provider'])}
                jev.validate_body(body)
                rows.append(row)
                bindings.append({'request_id': outer, 'original_query_id': ident,
                    'condition': condition['id'], 'measurement': measurement,
                    'expected_body_sha256': safe.digest(body),
                    'original_body_sha256': original['request_hash'],
                    'state_sha256': safe.digest(row['state']),
                    'q01_sha256': safe.digest(questions['q01'])})
    if len(rows) != policy['planned_requests'] or len({r['case_id'] for r in rows}) != len(rows):
        raise ProbeIntegrityError('physical_request_inventory_invalid')
    return rows, bindings


def prepare_plan(output=DESTINATION):
    policy = validate_policy(read(POLICY)); folder = Path(output)
    if folder.exists():
        raise FileExistsError(str(folder))
    manifests = {}; predictions = {}; sources = {}
    for arm in ARMS:
        mp = SOURCE / arm / 'prepared/manifest.json'
        cp = SOURCE / arm / 'first_score/compiled_first.json'
        manifests[arm] = read(mp); jev.validate_manifest(manifests[arm])
        predictions[arm] = read(cp)
        if len(predictions[arm]) != 48 or {r['query_id'] for r in predictions[arm]} != {r['id'] for r in manifests[arm]['requests']}:
            raise ProbeIntegrityError('expected_existing_48_dev_queries')
        sources.update({str(path.relative_to(ROOT)): panel.digest_file(path) for path in (mp, cp)})
    selected = select_queries(predictions[ARMS[0]], predictions[ARMS[1]])
    inputs, bindings = build_inputs(selected, manifests, policy)
    request = {'schema': 'loom.jev_pilot_request/1', 'enabled': True,
        'experiment_id': policy['experiment_id'], 'model': jev.MODEL,
        'budget_usd': policy['budget_usd'], 'batch_cap_usd': policy['batch_cap_usd'],
        'max_requests': policy['planned_requests']}
    folder.mkdir(parents=True, exist_ok=False)
    panel.write_new(folder / 'policy.json', policy)
    panel.write_new(folder / 'selection.json', {
        'schema': 'loom.source_view_packing_probe.selection/1',
        'selected_after_observing_first_dev_probabilities': True,
        'prior_dev_gold_known_to_reviewer': True, 'selection_reads_gold': False,
        'rule': policy['selection'], 'selected_queries': selected,
        'source_artifact_sha256': sources, 'request_bindings': bindings})
    panel.write_new(folder / 'inputs.json', inputs)
    panel.write_new(folder / 'request.json', request)
    return {'folder': str(folder), 'selected': selected, 'planned_requests': len(inputs),
        'reservation_usd': str(jev.RESERVE * len(inputs)), 'paid_calls': 0}


def freeze_plan(folder=DESTINATION):
    folder = Path(folder)
    files = ['PROTOCOL.md', 'policy.json', 'selection.json', 'inputs.json', 'request.json',
        'FIRST_MECHANISM_RESULTS.json']
    code = [Path(__file__), Path(__file__).with_name('test_source_view_packing_probe.py'),
        Path(panel.__file__), Path(replay.__file__), Path(jev.__file__), Path(safe.__file__)]
    value = {'schema': 'loom.source_view_packing_probe.freeze/1', 'frozen_at_utc': safe._utc(),
        'before_probe_paid_calls': True, 'paid_calls': 0, 'development_only': True,
        'declared_physical_measurements_not_retry': True, 'session_budget_reset': False,
        'files_sha256': {name: panel.digest_file(folder / name) for name in files},
        'code_sha256': {str(path.relative_to(ROOT)): panel.digest_file(path) for path in code}}
    panel.write_new(folder / 'freeze.json', value)
    return value


def validate_plan(folder):
    folder = Path(folder); freeze = read(folder / 'freeze.json')
    if freeze.get('schema') != 'loom.source_view_packing_probe.freeze/1':
        raise ProbeIntegrityError('plan_freeze_schema_invalid')
    for name, digest in freeze['files_sha256'].items():
        if Path(name).name != name or panel.digest_file(folder / name) != digest:
            raise ProbeIntegrityError('plan_file_hash_drift')
    for name, digest in freeze['code_sha256'].items():
        if Path(name).is_absolute() or '..' in Path(name).parts or panel.digest_file(ROOT / name) != digest:
            raise ProbeIntegrityError('plan_code_hash_drift')
    return validate_policy(read(folder / 'policy.json'))


def replay_probabilities(manifest, run_dir, endpoint_snapshot):
    """Generic gold-free offline Jev replay with fatal receipt/identity errors."""
    directory = Path(run_dir); jev.validate_manifest(manifest)
    if (safe.digest(endpoint_snapshot) != manifest['endpoint_snapshot_hash']
            or endpoint_snapshot.get('data', {}).get('id') != jev.MODEL
            or jev.endpoint_identity(endpoint_snapshot) != manifest['model_aliases']):
        raise ProbeIntegrityError('endpoint_snapshot_identity_or_hash_drift')
    ledger = read(directory / 'ledger.json')
    attempts = jev.validate_ledger(ledger, manifest, directory)
    replay.audit_billing_artifacts(attempts, directory)
    by_attempt = {r['id']: r for r in attempts}; rows = []; costs = []
    for request in manifest['requests']:
        attempt = by_attempt.get(request['id'], {})
        row = {'request_id': request['id'], 'state': 'unavailable',
            'reason': attempt.get('reason', 'not_completed'), 'probabilities': None}
        if 'reported_cost_usd' in attempt:
            costs.append(safe._money(attempt['reported_cost_usd']))
        if 'response_file' in attempt:
            path = directory / attempt['response_file']
            if path.stat().st_size > safe.MAX_RESPONSE_BYTES:
                raise ProbeIntegrityError('response_size_limit')
            row.update(response_file=attempt['response_file'], response_sha256=attempt['response_sha256'])
        if attempt.get('state') == 'completed':
            if attempt.get('http_status') != 200 or 'response_file' not in attempt:
                raise ProbeIntegrityError('completed_response_missing_or_not_http200')
            parsed = jev.parse_response(path.read_bytes(), request, manifest['model_aliases'])
            replay.billing_consistent(parsed['reported_cost_usd'], attempt)
            if any(attempt.get(k) != v for k, v in parsed.items()):
                raise ProbeIntegrityError('ledger_response_value_mismatch')
            row.update(state='completed', reason=None, probabilities=parsed['probabilities'],
                reported_cost_usd=parsed['reported_cost_usd'], model=parsed['model'], provider=parsed['provider'])
        rows.append(row)
    cost = str(sum(costs, Decimal(0)))
    if 'reported_cost_usd' in ledger and safe._money(ledger['reported_cost_usd']) != safe._money(cost):
        raise replay.BillingIntegrityError('ledger_total_cost_mismatch')
    return rows, {'planned': len(rows), 'attempted': len(attempts),
        'completed': sum(r['state'] == 'completed' for r in rows),
        'unavailable': sum(r['state'] != 'completed' for r in rows),
        'reported_cost_usd': cost,
        'attempts_with_unknown_cost': sum('reported_cost_usd' not in r for r in attempts),
        'ledger_sha256': panel.digest_file(directory / 'ledger.json')}


def summarize_cells(bindings, rows):
    actual = {r['request_id']: r for r in rows}
    if len(actual) != len(rows) or set(actual) != {b['request_id'] for b in bindings}:
        raise ProbeIntegrityError('summary_request_inventory_mismatch')
    groups = defaultdict(list)
    for b in bindings:
        groups[(b['original_query_id'], b['condition'])].append((b, actual[b['request_id']]))
    cells = []; by_cell = {}
    for (ident, condition), items in sorted(groups.items()):
        if len(items) != 2 or {b['measurement'] for b, _ in items} != {1, 2}:
            raise ProbeIntegrityError('expected_two_declared_measurements_per_cell')
        items.sort(key=lambda item: item[0]['measurement'])
        values = [Decimal(str(r['probabilities']['q01'])) for _, r in items if r['state'] == 'completed']
        cell = {'original_query_id': ident, 'condition': condition, 'planned_measurements': 2,
            'available_measurements': len(values), 'complete_cell': len(values) == 2,
            'measurements': [dict(measurement=b['measurement'], **r) for b, r in items],
            'q01_mean_available': decimal_text(sum(values) / len(values)) if values else None,
            'q01_min_available': decimal_text(min(values)) if values else None,
            'q01_max_available': decimal_text(max(values)) if values else None,
            'q01_range_available': decimal_text(max(values) - min(values)) if values else None}
        cells.append(cell); by_cell[(ident, condition)] = cell
    contrasts = []
    for ident in sorted({b['original_query_id'] for b in bindings}):
        subset = {c: by_cell[(ident, c)] for c in ('A', 'B', 'C')}
        complete = all(cell['complete_cell'] for cell in subset.values())
        contrast = {'original_query_id': ident, 'complete_three_condition_comparison': complete}
        if complete:
            mean = {c: Decimal(v['q01_mean_available']) for c, v in subset.items()}
            contrast.update(B_minus_A_q01_mean=decimal_text(mean['B'] - mean['A']),
                C_minus_A_q01_mean=decimal_text(mean['C'] - mean['A']),
                C_minus_B_q01_mean=decimal_text(mean['C'] - mean['B']),
                within_condition_q01_ranges={c: v['q01_range_available'] for c, v in subset.items()})
        contrasts.append(contrast)
    return {'cells': cells, 'contrasts': contrasts,
        'no_gold_accuracy_computed': True, 'replication_count_per_cell': 2,
        'causal_packing_claim': False, 'global_reliability_claim': False,
        'limitation': 'post-results selected diagnostic DEV; n=2 ranges are descriptive, not a stochasticity or causal independence guarantee'}


def score_plan(plan_dir, prepared_dir, run_dir, output_dir):
    plan_dir = Path(plan_dir); prepared_dir = Path(prepared_dir)
    policy = validate_plan(plan_dir); inputs = read(plan_dir / 'inputs.json')
    manifest = read(prepared_dir / 'manifest.json'); selection = read(plan_dir / 'selection.json')
    if (manifest['experiment_id'] != policy['experiment_id']
            or manifest['inputs_hash'] != safe.digest(inputs)
            or read(prepared_dir / 'inputs.json') != inputs):
        raise ProbeIntegrityError('executed_plan_identity_drift')
    expected = {r['case_id']: r for r in inputs}; bindings = selection['request_bindings']
    by_binding = {b['request_id']: b for b in bindings}
    if len(manifest['requests']) != 24 or set(expected) != set(by_binding):
        raise ProbeIntegrityError('executed_plan_request_inventory_drift')
    for req in manifest['requests']:
        if req['id'] not in expected:
            raise ProbeIntegrityError('unplanned_probe_request')
        source = expected[req['id']]
        body = {'model': jev.MODEL, 'state': source['state'], 'questions': source['questions'],
            'provider': {'only': ['typesafe'], 'allow_fallbacks': False,
                'max_price': {'prompt': '0.042', 'completion': '0'}}}
        if req['body'] != body or req['request_hash'] != by_binding[req['id']]['expected_body_sha256']:
            raise ProbeIntegrityError('executed_body_drift')
    rows, accounting = replay_probabilities(manifest, run_dir, read(prepared_dir / 'endpoint_snapshot.json'))
    report = summarize_cells(bindings, rows)
    report.update(schema='loom.source_view_packing_probe.score/1', accounting=accounting,
        split='diagnostic_dev', no_graph_promotion=True, primary_source_view_scores_unchanged=True,
        manifest_sha256=panel.digest_file(prepared_dir / 'manifest.json'),
        plan_freeze_sha256=panel.digest_file(plan_dir / 'freeze.json'))
    output = Path(output_dir); output.mkdir(parents=True, exist_ok=False)
    panel.write_new(output / 'compiled_first.json', rows)
    panel.write_new(output / 'score_first.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    plan = sub.add_parser('prepare-plan'); plan.add_argument('--output', type=Path, default=DESTINATION)
    freeze = sub.add_parser('freeze-plan'); freeze.add_argument('--plan-dir', type=Path, default=DESTINATION)
    score = sub.add_parser('score')
    for key in ('plan-dir', 'prepared-dir', 'run-dir', 'output'):
        score.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'prepare-plan':
        result = prepare_plan(args.output)
    elif args.command == 'freeze-plan':
        result = freeze_plan(args.plan_dir)
    else:
        result = score_plan(args.plan_dir, args.prepared_dir, args.run_dir, args.output)
    print(safe.canonical(result).decode())


if __name__ == '__main__':
    main()
