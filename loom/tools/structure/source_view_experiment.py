"""Frozen development intervention: historical versus active source refutation.

No network or key access. Existing bounded Jev runner executes the prepared
specs. Source commitments are not world facts; this fixture is not a holdout.
"""
from __future__ import annotations
import argparse
from decimal import Decimal
import hashlib
from pathlib import Path

try:
    from . import graph_panel_live as panel, graph_panel_score_run as replay
    from . import jev_live_pilot as jev, openrouter_runner as safe
except ImportError:
    import graph_panel_live as panel, graph_panel_score_run as replay
    import jev_live_pilot as jev, openrouter_runner as safe

FIXTURE = panel.ROOT / 'loom/tests/fixtures/research/source_view_lifecycle_v1'
FIXTURE_SHA256 = 'c47a87f62dd7724d50243c40494d70ae1a20ccd278d3bf3e3b12d3f28ef4072b'
ARMS = ('historical_refute_v1', 'active_refute_v2')
ACTIVE_REFUTE = {
    'type': 'noul',
    'instructions': "In the requested temporal view, is the requested speaker's latest applicable commitment an explicit denial or withdrawal of this exact relation, rather than a merely historical denial?",
    'criteria': {
        'true': "This speaker's latest applicable commitment explicitly denies or withdraws this positive relation by as_of. Positive withdrawal refutes the source commitment only, not the relation's world truth.",
        'false': "There is no currently applicable explicit denial or positive withdrawal. A later affirmation by the SAME attributed speaker replaces that speaker's earlier denial. Explicit withdrawal of a negative commitment leaves unknown unless a positive statement exists; withdrawal never affirms by itself. Another speaker's reply does not withdraw this speaker's commitment. Silence, similar topic and absence of positive support are not refutation."
    }
}


def load_fixture(name):
    if panel.digest_file(FIXTURE / 'manifest.json') != FIXTURE_SHA256:
        raise ValueError('lifecycle_fixture_manifest_drift')
    manifest = replay.read(FIXTURE / 'manifest.json')
    path = FIXTURE / name
    expected = manifest['files'][name]
    if panel.digest_file(path) != expected:
        raise ValueError('lifecycle_fixture_file_drift')
    value = replay.read(path)
    if value['split'] != 'dev' or len(value['cases']) != 12:
        raise ValueError('lifecycle_dev_only')
    return value['cases']


def specs(cases, arm):
    if arm not in ARMS:
        raise ValueError('unknown_lifecycle_arm')
    rows = panel.jev_judgment_inputs(cases)
    if arm == 'active_refute_v2':
        for row in rows:
            row['questions']['q02'] = safe.parse_json(safe.canonical(ACTIVE_REFUTE))
    return rows


def prepare_specs(output):
    cases = load_fixture('inputs_dev.json')
    directory = Path(output)
    directory.mkdir(parents=True, exist_ok=False)
    for arm in ARMS:
        rows = specs(cases, arm)
        if len(rows) != 48:
            raise ValueError('expected_48_lifecycle_queries')
        panel.write_new(directory / (arm + '_inputs.json'), rows)
        panel.write_new(directory / (arm + '_request.json'), {
            'schema': 'loom.jev_pilot_request/1', 'enabled': True,
            'experiment_id': 'source-view-' + arm.replace('_', '-') + '-20260930',
            'model': jev.MODEL, 'budget_usd': '2', 'batch_cap_usd': '0.10', 'max_requests': 48
        })
    panel.write_new(directory / 'freeze.json', {
        'frozen_at_utc': safe._utc(), 'before_first_lifecycle_paid_calls': True,
        'fixture_manifest_sha256': FIXTURE_SHA256, 'development_only': True,
        'changed_variable': 'q02 active-refutation instructions/criteria only; q01 and source identical',
        'threshold': 'strictly_greater_than_0.5', 'shared_global_cap_usd': '2',
        'session_budget_reset': False, 'total_reservation_usd': '0.096',
        'code_sha256': {str(p.relative_to(panel.ROOT)): panel.digest_file(p) for p in
            (Path(__file__), Path(panel.__file__), Path(replay.__file__), Path(jev.__file__), Path(safe.__file__))},
        'files_sha256': {p.name: panel.digest_file(p) for p in sorted(directory.glob('*.json'))},
        'no_graph_promotion': True
    })


def score(manifest_path, run_dir, arm, output):
    manifest = replay.read(manifest_path)
    jev.validate_manifest(manifest)
    directory = Path(run_dir)
    ledger = replay.read(directory / 'ledger.json')
    attempts = jev.validate_ledger(ledger, manifest, directory)
    cases = load_fixture('inputs_dev.json')
    expected = {r['case_id']: r for r in specs(cases, arm)}
    if set(expected) != {r['id'] for r in manifest['requests']}:
        raise ValueError('lifecycle_planned_query_set_drift')
    by_attempt = {r['id']: r for r in attempts}
    predictions = []
    raw_costs = []
    for request in manifest['requests']:
        original = expected[request['id']]
        if request['body']['state'] != original['state'] or request['body']['questions'] != original['questions']:
            raise ValueError('lifecycle_frozen_body_drift')
        attempt = by_attempt.get(request['id'], {})
        prediction = {'query_id': request['id'], 'state': 'unavailable', 'reason': attempt.get('reason', 'not_attempted')}
        parsed = None
        if 'response_file' in attempt:
            raw = (directory / attempt['response_file']).read_bytes()
            value = safe.parse_json(raw)
            usage = value.get('usage')
            if isinstance(usage, dict) and 'cost' in usage:
                replay.billing_consistent(usage['cost'], attempt)
                raw_costs.append(safe._money(usage['cost']))
            elif 'reported_cost_usd' in attempt:
                raise replay.BillingIntegrityError('lifecycle_ledger_cost_without_raw_usage')
            if attempt['state'] == 'completed':
                parsed = jev.parse_response(raw, request, manifest['model_aliases'])
                replay.billing_consistent(parsed['reported_cost_usd'], attempt)
        if parsed is not None:
            prediction = panel.compile_jev_judgment(parsed, request['id'])
        predictions.append(prediction)
    # Independent fixture labels are joined only after preserved responses pass replay.
    golds = load_fixture('gold_dev.json')
    report = panel.score_judgments(golds, predictions)
    report.update(arm=arm, fixture_manifest_sha256=FIXTURE_SHA256,
        scope='new diagnostic development; latest source commitment, never world truth',
        execution={'planned_requests': len(expected), 'attempted_requests': len(attempts),
            'reported_cost_usd': str(sum(raw_costs, Decimal(0))),
            'unknown_cost_attempts': sum('reported_cost_usd' not in r for r in attempts),
            'stopped_reason': ledger.get('stopped_reason'),
            'post_elapsed_seconds_sum': sum(r.get('elapsed_seconds', 0) for r in attempts)})
    folder = Path(output)
    folder.mkdir(parents=True, exist_ok=False)
    panel.write_new(folder / 'compiled_first.json', predictions)
    panel.write_new(folder / 'score_first.json', report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    prepare = sub.add_parser('prepare-specs')
    prepare.add_argument('--output', required=True, type=Path)
    scoring = sub.add_parser('score')
    scoring.add_argument('--manifest', required=True, type=Path)
    scoring.add_argument('--run-dir', required=True, type=Path)
    scoring.add_argument('--arm', required=True, choices=ARMS)
    scoring.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.command == 'prepare-specs':
        prepare_specs(args.output)
    else:
        result = score(args.manifest, args.run_dir, args.arm, args.output)
        print(safe.canonical({'arm': args.arm, 'per_class': result['per_class'], 'execution': result['execution']}).decode())


if __name__ == '__main__':
    main()
