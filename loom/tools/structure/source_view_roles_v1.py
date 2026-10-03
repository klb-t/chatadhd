"""Offline role-explicit source-view DEV recipe; network execution belongs to root.

The source record and question bytes are unchanged. A redundant role card renders
the supplied query's source/target propositions without judging their truth.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from decimal import Decimal
from pathlib import Path

try:
    from . import source_view_experiment as base
except ImportError:
    import source_view_experiment as base

panel, replay, jev, safe = base.panel, base.replay, base.jev, base.safe
ROOT = panel.ROOT
DESTINATION = ROOT / 'docs/research/recipe_experiments_2026-09-30/role_explicit_v1'
EXPERIMENT_ID = 'source-view-role-explicit-v1-20260930'
ARM = 'role_explicit_v1'


def role_representation(payload):
    """A lossless source record plus data-only expansion of the requested roles."""
    if payload['query']['scope'] != 'explicit_source':
        raise ValueError('role_recipe_explicit_source_only')
    nodes = {}
    for node in payload['node_inventory']:
        if node['id'] in nodes:
            raise ValueError('role_node_identity_ambiguous')
        nodes[node['id']] = node
    query = payload['query']
    if query['source'] not in nodes or query['target'] not in nodes:
        raise ValueError('role_query_endpoint_missing')
    card = {'relation': query['relation'],
        'directed_source_proposition': deepcopy(nodes[query['source']]),
        'directed_target_proposition': deepcopy(nodes[query['target']]),
        'requested_attributed_speaker': query['attributed_to'],
        'requested_as_of': query['as_of'], 'scope': query['scope']}
    return {'source_record': deepcopy(payload), 'requested_relation_roles': card}


def specs(cases):
    rows = base.specs(cases, 'active_refute_v2')
    for row in rows:
        payload = safe.parse_json(row['state']['text'])
        row['state'] = {'text': safe.canonical(role_representation(payload)).decode()}
    return rows


def prepare_plan(output):
    cases = base.load_fixture('inputs_dev.json')
    rows = specs(cases)
    if len(rows) != 48 or len({r['case_id'] for r in rows}) != 48:
        raise ValueError('role_recipe_expected_48_distinct_queries')
    # Validate every exact body before writes; additions cannot exceed the unchanged
    # per-request reservation allowance even though actual input tokens may rise.
    provider = {'only': ['typesafe'], 'allow_fallbacks': False,
        'max_price': {'prompt': '0.042', 'completion': '0'}}
    for row in rows:
        jev.validate_body({'model': jev.MODEL, 'state': row['state'],
            'questions': row['questions'], 'provider': provider})
    folder = Path(output)
    folder.mkdir(parents=True, exist_ok=False)
    panel.write_new(folder / 'inputs.json', rows)
    panel.write_new(folder / 'request.json', {
        'schema': 'loom.jev_pilot_request/1', 'enabled': True,
        'experiment_id': EXPERIMENT_ID, 'model': jev.MODEL, 'budget_usd': '2',
        'batch_cap_usd': '0.10', 'max_requests': 48})
    baseline = base.specs(cases, 'active_refute_v2')
    panel.write_new(folder / 'representation_receipt.json', {
        'schema': 'loom.source_view_roles.receipt/1', 'planned_queries': len(rows),
        'validation_read': False, 'gold_read': False, 'paid_calls': 0,
        'source_payload_preserved_exactly': True, 'questions_preserved_exactly': True,
        'question_order_preserved': True, 'session_budget_reset': False,
        'same_existing_shared_session_cap_usd': '2', 'reservation_usd': '0.048',
        'bindings': [{'query_id': a['case_id'],
            'baseline_state_sha256': safe.digest(a['state']),
            'represented_state_sha256': safe.digest(b['state']),
            'raw_source_record_sha256': safe.digest(safe.parse_json(a['state']['text'])),
            'questions_sha256': safe.digest(a['questions']),
            'question_order': list(a['questions'])}
            for a, b in zip(baseline, rows)]})
    return {'planned_requests': len(rows), 'total_reservation_usd': '0.048',
        'gold_read': False, 'validation_read': False, 'paid_calls': 0}


def freeze_plan(folder=DESTINATION):
    folder = Path(folder).resolve()
    if list(folder.rglob('ledger.json')) or list(folder.rglob('*.response.bin')):
        raise ValueError('cannot_freeze_role_recipe_after_attempts')
    code = [Path(__file__), Path(__file__).with_name('test_source_view_roles_v1.py'),
        Path(base.__file__), Path(panel.__file__), Path(replay.__file__),
        Path(jev.__file__), Path(safe.__file__)]
    files = [folder / x for x in ('PROTOCOL.md', 'inputs.json', 'request.json',
        'representation_receipt.json', 'FIRST_MECHANISM_RESULTS.json')]
    files += [base.FIXTURE / x for x in ('manifest.json', 'inputs_dev.json')]
    value = {'schema': 'loom.source_view_roles.freeze/1', 'frozen_at_utc': safe._utc(),
        'before_role_recipe_paid_calls': True, 'development_only': True,
        'baseline_outcomes_already_observed': True, 'no_validation_read': True,
        'source_record_and_questions_unchanged': True,
        'changed_variable': 'state representation: original payload nested plus redundant role card',
        'threshold': 'strictly_greater_than_0.5', 'paid_calls': 0,
        'session_budget_reset': False, 'shared_global_cap_usd': '2',
        'total_reservation_usd': '0.048',
        'files_sha256': {str(p.relative_to(ROOT)): panel.digest_file(p) for p in code + files}}
    panel.write_new(folder / 'FREEZE.json', value)
    return value


def validate_prepared(manifest_path, cases):
    """Whole expected inventory and public identity are gates before response reads."""
    manifest = replay.read(manifest_path)
    jev.validate_manifest(manifest)
    expected = specs(cases)
    if (manifest.get('experiment_id') != EXPERIMENT_ID
            or safe._money(manifest['budget_usd']) != Decimal('2')
            or safe._money(manifest['batch_cap_usd']) != Decimal('.10')
            or manifest.get('inputs_hash') != safe.digest(expected)
            or len(manifest['requests']) != len(expected)):
        raise ValueError('role_recipe_manifest_contract_drift')
    snapshot = replay.read(Path(manifest_path).with_name('endpoint_snapshot.json'))
    if (manifest.get('model_aliases') != jev.endpoint_identity(snapshot)
            or manifest.get('endpoint_snapshot_hash') != safe.digest(snapshot)):
        raise ValueError('role_recipe_public_endpoint_identity_drift')
    for request, spec in zip(manifest['requests'], expected):
        if (request['id'] != spec['case_id'] or request['language'] != spec['language']
                or request['body']['state'] != spec['state']
                or request['body']['questions'] != spec['questions']
                or list(request['body']['questions']) != list(spec['questions'])):
            raise ValueError('role_recipe_frozen_body_or_inventory_drift')
    return manifest


def validate_freeze(folder=DESTINATION):
    record = replay.read(Path(folder) / 'FREEZE.json')
    if (record.get('schema') != 'loom.source_view_roles.freeze/1'
            or record.get('before_role_recipe_paid_calls') is not True):
        raise ValueError('role_recipe_missing_prior_freeze')
    for name, expected in record['files_sha256'].items():
        if panel.digest_file(ROOT / name) != expected:
            raise ValueError('role_recipe_frozen_artifact_drift')
    return record


def replay_outputs(manifest, directory):
    directory = Path(directory)
    ledger = replay.read(directory / 'ledger.json')
    attempts = jev.validate_ledger(ledger, manifest, directory)
    replay.audit_billing_artifacts(attempts, directory)
    by_attempt = {r['id']: r for r in attempts}
    predictions = []
    for request in manifest['requests']:
        attempt = by_attempt.get(request['id'], {})
        prediction = {'query_id': request['id'], 'state': 'unavailable',
            'reason': attempt.get('reason', 'not_attempted')}
        if attempt.get('state') == 'completed':
            raw = (directory / attempt['response_file']).read_bytes()
            parsed = jev.parse_response(raw, request, manifest['model_aliases'])
            replay.billing_consistent(parsed['reported_cost_usd'], attempt)
            prediction = panel.compile_jev_judgment(parsed, request['id'])
        predictions.append(prediction)
    summary = {'arm': ARM, 'scope': 'diagnostic development; latest source commitment',
        'planned_requests': len(manifest['requests']), 'attempted_requests': len(attempts),
        'reported_cost_usd': str(sum((safe._money(a['reported_cost_usd'])
            for a in attempts if 'reported_cost_usd' in a), Decimal(0))),
        'unknown_cost_attempts': sum('reported_cost_usd' not in a for a in attempts),
        'unknown_cost_reservation_usd': str(sum((safe._money(a['reservation_usd'])
            for a in attempts if 'reported_cost_usd' not in a), Decimal(0))),
        'post_elapsed_seconds_sum': sum(a.get('elapsed_seconds', 0) for a in attempts),
        'manifest_sha256': safe.digest(manifest), 'ledger_sha256': safe.digest(ledger),
        'session_budget_reset': False, 'no_graph_promotion': True}
    return predictions, summary


def score(manifest_path, run_dir, output):
    validate_freeze()
    cases = base.load_fixture('inputs_dev.json')
    manifest = validate_prepared(manifest_path, cases)
    predictions, summary = replay_outputs(manifest, run_dir)
    folder = Path(output)
    folder.mkdir(parents=True, exist_ok=False)
    panel.write_new(folder / 'compiled_first.json', predictions)
    panel.write_new(folder / 'execution_summary.json', summary)
    # Preserve all first outcomes before consulting any reference labels.
    golds = base.load_fixture('gold_dev.json')
    result = panel.score_judgments(golds, predictions)
    result.update(arm=ARM, execution=summary, fixture_manifest_sha256=base.FIXTURE_SHA256)
    panel.write_new(folder / 'score_first.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    prep = sub.add_parser('prepare-plan'); prep.add_argument('--output', type=Path, required=True)
    freeze = sub.add_parser('freeze'); freeze.add_argument('--folder', type=Path, default=DESTINATION)
    scoring = sub.add_parser('score'); scoring.add_argument('--manifest', type=Path, required=True)
    scoring.add_argument('--run-dir', type=Path, required=True)
    scoring.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'prepare-plan': result = prepare_plan(args.output)
    elif args.command == 'freeze': result = freeze_plan(args.folder)
    else: result = score(args.manifest, args.run_dir, args.output)
    print(safe.canonical(result).decode())


if __name__ == '__main__':
    main()
