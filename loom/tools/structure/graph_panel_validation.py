"""Explicitly released validation packaging/replay; never network or key access.

Frozen DEV recipes, compilers and primary metrics are reused as pure functions.
Only the new whole-family graph_methods_panel_v1 fixture may be released. The
coordinator separately performs bounded calls with inherited immutable ledgers.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from decimal import Decimal
import hashlib
from pathlib import Path

try:
    from . import graph_panel_live as panel, graph_panel_score_run as integrity
    from . import openrouter_runner as safe, jev_live_pilot as jev
except ImportError:
    import graph_panel_live as panel, graph_panel_score_run as integrity
    import openrouter_runner as safe, jev_live_pilot as jev

ROOT = panel.ROOT
FIXTURE = panel.FIXTURE
HERE = ROOT / 'docs/research/graph_method_panel_v1'
FIXTURE_MANIFEST_SHA256 = 'cf564f652647dd93e3d5f66acd2741eb69d3e39e3070ec69884c8023dc0a0569'
RELEASE_SCHEMA = 'loom.graph_panel_validation_release/1'
_RELEASE_TOKEN = object()
MINIMUM_PINS = {
    'loom/tools/structure/graph_panel_validation.py',
    'loom/tools/structure/test_graph_panel_validation.py',
    'loom/tools/structure/graph_panel_live.py',
    'loom/tools/structure/graph_panel_score_run.py',
    'loom/tools/structure/openrouter_runner.py',
    'loom/tools/structure/jev_live_pilot.py',
    'docs/research/graph_method_panel_v1/VALIDATION_WRAPPER_PROTOCOL.md',
    'docs/research/graph_method_panel_v1/SCORER_DRIVER_FREEZE4.json',
    'docs/research/graph_method_panel_v1/gpt_judge_v2_recipe.json',
    'docs/research/graph_method_panel_v1/gpt_judge_v2_batch01/prepared/manifest.json',
    'docs/research/graph_method_panel_v1/extraction/prepared/manifest.json',
    'docs/research/model_method_panel_v1/public_preflight/gpt41mini_endpoints.json',
    'docs/research/model_method_panel_v1/public_preflight/jev_endpoints.json',
}


class VerifiedRelease(dict):
    """Internal verified-context ticket; a SHA-looking field is not authorization."""
    def __init__(self, value, *, _token=None):
        if _token is not _RELEASE_TOKEN:
            raise ValueError('only_authorize_returns_verified_release')
        super().__init__(value)
        self._authority_token = _token
        self._verified_value_sha256 = safe.digest(dict(self))


def require_verified_release(release):
    if (type(release) is not VerifiedRelease or
            release._authority_token is not _RELEASE_TOKEN or
            safe.digest(dict(release)) != release._verified_value_sha256):
        raise ValueError('authorization_required_before_validation_read')


def public_jev_identity():
    snapshot = read(ROOT / 'docs/research/model_method_panel_v1/public_preflight/jev_endpoints.json')
    if snapshot.get('data', {}).get('id') != jev.MODEL:
        raise ValueError('frozen_jev_public_model_identity_invalid')
    return snapshot, jev.endpoint_identity(snapshot)


def preflight_manifest_identity(manifest):
    """Check fixed public Jev identity before even loading sealed source inputs."""
    if manifest.get('schema') == 'loom.jev_manifest/1':
        jev.validate_manifest(manifest)
        snapshot, aliases = public_jev_identity()
        if (manifest.get('model_aliases') != aliases or
                manifest.get('endpoint_snapshot_hash') != safe.digest(snapshot)):
            raise ValueError('jev_manifest_public_identity_drift')
    elif manifest.get('schema') == safe.MANIFEST_SCHEMA:
        if manifest.get('metadata', {}).get('instrument') != 'gpt':
            raise ValueError('manifest_instrument_schema_mismatch')
        safe.plan_manifest(manifest)
    else:
        raise ValueError('unrecognized_released_manifest')


def read(path):
    return safe.parse_json(Path(path).read_bytes())


def pin_path(relative):
    if not isinstance(relative, str) or Path(relative).is_absolute():
        raise ValueError('release_requires_repository_relative_path')
    path = (ROOT / relative).resolve()
    if not path.is_relative_to(ROOT.resolve()):
        raise ValueError('release_pin_outside_repository')
    if path.name in ('inputs_validation.json', 'gold_validation.json'):
        raise ValueError('sealed_files_are_verified_only_after_release')
    return path


def authorize(release_path, release_sha256):
    """Must finish before any sealed-file read, including digesting sealed bytes.

    External expected SHA is the coordinator's pinned attestation. This is not a
    cryptographic person-identity signature; repository metadata cannot prove one.
    """
    path = Path(release_path)
    if path.name != 'RELEASE.json' or panel.digest_file(path) != release_sha256:
        raise ValueError('root_release_not_externally_pinned')
    release = read(path)
    safe._keys(release, {
        'schema', 'issued_by', 'authorized', 'released_at', 'fixture_manifest_sha256',
        'methods_sha256', 'inherited_ledger_sha256', 'global_budget_usd',
        'batch_cap_usd', 'known_reported_spent_usd',
        'retained_unknown_cost_reservation_usd', 'remaining_reservation_allowance_usd',
        'no_retries', 'session_budget_reset', 'all_intended_methods_frozen',
    })
    if (release['schema'] != RELEASE_SCHEMA or release['issued_by'] != '/root' or
            release['authorized'] is not True or release['no_retries'] is not True or
            release['session_budget_reset'] is not False or
            release['all_intended_methods_frozen'] is not True):
        raise ValueError('coordinator_release_contract_invalid')
    safe._timestamp(release['released_at'])
    if (release['fixture_manifest_sha256'] != FIXTURE_MANIFEST_SHA256 or
            panel.digest_file(FIXTURE / 'manifest.json') != FIXTURE_MANIFEST_SHA256):
        raise ValueError('only_new_graph_panel_fixture_may_be_released')
    pins = release['methods_sha256']
    if not isinstance(pins, dict) or not MINIMUM_PINS <= set(pins):
        raise ValueError('intended_methods_not_frozen')
    for relative, expected in pins.items():
        if panel.digest_file(pin_path(relative)) != expected:
            raise ValueError('released_method_hash_drift')
    # Preserve the primary freeze4 dependencies, not merely coordinator-selected
    # current replacements. Additional local/formal methods may be pinned too.
    original = read(HERE / 'SCORER_DRIVER_FREEZE4.json')
    for relative, expected in original['files_sha256'].items():
        if panel.digest_file(pin_path(relative)) != expected:
            raise ValueError('primary_frozen_dependency_drift')
    # Validate the pinned public identity before any sealed source/gold bytes.
    public_jev_identity()
    budget = safe._money(release['global_budget_usd'])
    cap = safe._money(release['batch_cap_usd'])
    known = safe._money(release['known_reported_spent_usd'])
    uncertain = safe._money(release['retained_unknown_cost_reservation_usd'])
    allowance = safe._money(release['remaining_reservation_allowance_usd'])
    if (budget != Decimal('2') or not 0 < cap <= Decimal('.10') or
            uncertain < Decimal('.001366') or allowance <= 0 or known + uncertain + allowance > budget):
        raise ValueError('nonreset_global_budget_release_invalid')
    ledgers = release['inherited_ledger_sha256']
    if not isinstance(ledgers, dict) or not ledgers:
        raise ValueError('inherited_accounting_missing')
    recorded_known, recorded_uncertain = Decimal(0), Decimal(0)
    for relative, expected in ledgers.items():
        ledger_path = pin_path(relative)
        if ledger_path.name != 'ledger.json' or panel.digest_file(ledger_path) != expected:
            raise ValueError('inherited_ledger_hash_drift')
        ledger = read(ledger_path)
        if ledger.get('schema') not in ('loom.openrouter_ledger/1', 'loom.jev_ledger/1'):
            raise ValueError('unrecognized_inherited_ledger')
        for attempt in ledger.get('attempts', []):
            if 'reported_cost_usd' in attempt:
                recorded_known += safe._money(attempt['reported_cost_usd'])
            else:
                # A cost-unknown attempted request consumes its retained reserve;
                # never silently zero it or count unattempted planned requests.
                if 'reservation_usd' not in attempt:
                    raise ValueError('unknown_attempt_reservation_missing')
                recorded_uncertain += safe._money(attempt['reservation_usd'])
    if recorded_known > known or recorded_uncertain > uncertain:
        raise ValueError('release_understates_inherited_spending')
    release = deepcopy(release)
    release['release_sha256'] = release_sha256
    return VerifiedRelease(release, _token=_RELEASE_TOKEN)


def load_inputs(release):
    require_verified_release(release)
    manifest = read(FIXTURE / 'manifest.json')
    path = FIXTURE / 'inputs_validation.json'
    if panel.digest_file(path) != manifest['files']['inputs_validation.json']:
        raise ValueError('released_validation_input_drift')
    payload = read(path)
    if payload.get('split') != 'validation' or len(payload.get('cases', [])) != 24:
        raise ValueError('expected_only_24_released_validation_cases')
    cases = payload['cases']
    if (len({c['id'] for c in cases}) != 24 or
            any(not c['id'].startswith('gpv1_') or c['id'].startswith('gpv1_dev_') for c in cases)):
        raise ValueError('wrong_validation_case_inventory')
    for case in cases:
        for query in case['judgment_queries']:
            if query['scope'] not in ('explicit_source', 'formal_implication'):
                raise ValueError('unregistered_query_scope')
    return cases


def load_gold(release, cases):
    require_verified_release(release)
    manifest = read(FIXTURE / 'manifest.json')
    path = FIXTURE / 'gold_validation.json'
    if panel.digest_file(path) != manifest['files']['gold_validation.json']:
        raise ValueError('released_validation_gold_drift')
    payload = read(path)
    if payload.get('split') != 'validation' or len(payload.get('cases', [])) != 24:
        raise ValueError('expected_only_24_released_validation_gold_cases')
    gold = payload['cases']
    if {g['id'] for g in gold} != {c['id'] for c in cases}:
        raise ValueError('released_input_gold_inventory_mismatch')
    return gold


def explicit_cases(cases):
    """Never relabel formal implication as a direct source claim."""
    result = deepcopy(cases)
    for case in result:
        if any(q['scope'] not in ('explicit_source', 'formal_implication') for q in case['judgment_queries']):
            raise ValueError('unregistered_query_scope')
        case['judgment_queries'] = [q for q in case['judgment_queries'] if q['scope'] == 'explicit_source']
    return result


def expected_gpt(cases, track):
    # DEV manifests pin prices and recipes; use no outcome data from those runs.
    template_path = HERE / ('extraction/prepared/manifest.json' if track == 'assisted_extraction'
                            else 'gpt_judge_v2_batch01/prepared/manifest.json')
    template = read(template_path)
    caps = template['requests'][0]['body']['provider']['max_price']
    if {k: safe._money(v) for k, v in caps.items()} != {'prompt': Decimal('.4'), 'completion': Decimal('1.6')}:
        raise ValueError('frozen_gpt_price_caps_drift')
    if track == 'assisted_extraction':
        rows = panel.extraction_requests(cases, caps)
    elif track == 'supplied_edge_judgment':
        rows = panel.gpt_judgment_requests(explicit_cases(cases), caps)
        replacement = integrity.validate_recipe(HERE / 'gpt_judge_v2_recipe.json')
        for row in rows:
            row['body']['messages'][0]['content'] = replacement
    else:
        raise ValueError('unregistered_gpt_track')
    for row in rows:
        row['reservation_usd'] = safe.estimate_reservation(row['body'])['minimum_reservation_usd']
    return rows, template['pricing_evidence']


def expected_jev(cases):
    rows = []
    for item in panel.jev_judgment_inputs(explicit_cases(cases)):
        body = {'model': jev.MODEL, 'state': item['state'], 'questions': item['questions'],
                'provider': {'only': ['typesafe'], 'allow_fallbacks': False,
                             'max_price': {'prompt': '0.042', 'completion': '0'}}}
        jev.validate_body(body)
        rows.append({'id': item['case_id'], 'language': item['language'], 'body': body,
                     'request_hash': safe.digest(body), 'reservation_usd': str(jev.RESERVE)})
    return rows


def greedy_batches(rows, cap, row_limit):
    """Stable fixture order, exact reservations; never outcome-adaptive packing."""
    cap = safe._money(cap)
    if not 0 < cap <= Decimal('.10') or row_limit not in (24, 48):
        raise ValueError('invalid_packaging_policy')
    batches, current, total = [], [], Decimal(0)
    ids = set()
    for row in rows:
        if row['id'] in ids:
            raise ValueError('duplicate_planned_id')
        ids.add(row['id'])
        reserve = safe._money(row['reservation_usd'])
        if not 0 < reserve <= cap:
            raise ValueError('single_row_exceeds_batch_cap')
        if current and (len(current) >= row_limit or total + reserve > cap):
            batches.append(current); current, total = [], Decimal(0)
        current.append(row); total += reserve
    if current:
        batches.append(current)
    return batches


def materialize_plan(cases, release, output_dir):
    """Pure inputs-only package, suitable for toy tests without sealed-file I/O."""
    gpt_extract, extract_prices = expected_gpt(cases, 'assisted_extraction')
    gpt_judge, judge_prices = expected_gpt(cases, 'supplied_edge_judgment')
    jev_judge = expected_jev(cases)
    prepared = []
    for instrument, track, rows, prices in (
        ('gpt', 'assisted_extraction', gpt_extract, extract_prices),
        ('gpt', 'supplied_edge_judgment', gpt_judge, judge_prices),
        ('jev', 'supplied_edge_judgment', jev_judge, None),
    ):
        batches = greedy_batches(rows, release['batch_cap_usd'], 24 if track == 'assisted_extraction' else 48)
        for number, batch in enumerate(batches, 1):
            experiment = f'graph-val-{instrument}-{"extract" if track == "assisted_extraction" else "judge"}-{number:02d}'
            metadata = {'split': 'validation', 'instrument': instrument, 'track': track,
                        'release_sha256': release['release_sha256'], 'session_budget_reset': False,
                        'batch_cap_usd': release['batch_cap_usd'], 'no_graph_promotion': True,
                        'gold_read_during_preparation': False}
            if instrument == 'gpt':
                manifest = {'schema': safe.MANIFEST_SCHEMA, 'experiment_id': experiment,
                            'budget_usd': '2', 'max_requests': len(batch), 'requests': batch,
                            'pricing_evidence': prices, 'metadata': metadata}
                plan = safe.plan_manifest(manifest)
                reservation = safe._money(plan['total_reservation_usd'])
            else:
                snapshot, aliases = public_jev_identity()
                manifest = {'schema': 'loom.jev_manifest/1', 'experiment_id': experiment,
                            'created_at': release['released_at'], 'budget_usd': '2',
                            'batch_cap_usd': release['batch_cap_usd'],
                            'total_reservation_usd': str(sum((safe._money(r['reservation_usd']) for r in batch), Decimal(0))),
                            'requests': batch, 'model_aliases': aliases,
                            'inputs_hash': safe.digest([r['body'] for r in batch]),
                            'endpoint_snapshot_hash': safe.digest(snapshot), 'code_sha256': jev.code_hashes(),
                            'billing_bound_guaranteed': False, 'no_graph_promotion': True,
                            'metadata': metadata}
                jev.validate_manifest(manifest)
                reservation = safe._money(manifest['total_reservation_usd'])
            if reservation > safe._money(release['batch_cap_usd']):
                raise ValueError('packaged_batch_cap_exceeded')
            prepared.append((experiment, manifest, reservation))
    total = sum((reserve for _, _, reserve in prepared), Decimal(0))
    if total > safe._money(release['remaining_reservation_allowance_usd']):
        raise ValueError('validation_plan_exceeds_inherited_allowance')
    directory = Path(output_dir)
    directory.mkdir(parents=True, exist_ok=False)
    index = []
    for experiment, manifest, reservation in prepared:
        folder = directory / experiment / 'prepared'
        folder.mkdir(parents=True, exist_ok=False)
        panel.write_new(folder / 'manifest.json', manifest)
        if manifest['schema'] == 'loom.jev_manifest/1':
            panel.write_new(folder / 'endpoint_snapshot.json', snapshot)
        index.append({'experiment_id': experiment, 'instrument': manifest['metadata']['instrument'],
                      'track': manifest['metadata']['track'], 'manifest': str((folder / 'manifest.json').relative_to(directory)),
                      'manifest_sha256': panel.digest_file(folder / 'manifest.json'),
                      'request_ids': [r['id'] for r in manifest['requests']],
                      'reservation_usd': str(reservation)})
    formal = []
    for case in cases:
        for query in case['judgment_queries']:
            if query['scope'] == 'formal_implication':
                prefix = [deepcopy(t) for t in case['turns'] if panel._time(t['known_at']) <= panel._time(query['as_of'])]
                formal.append({'case_id': case['id'], 'source_id': case['source_id'],
                               'turns': prefix, 'node_inventory': deepcopy(case['node_inventory']),
                               'query': deepcopy(query), 'scope': 'formal_implication',
                               'never_sent_to_direct_source_judges': True})
    panel.write_new(directory / 'formal_query_inventory.json', formal)
    report = {'schema': 'loom.graph_panel_validation_packaging/1', 'split': 'validation',
              'release_sha256': release['release_sha256'], 'fixture_manifest_sha256': FIXTURE_MANIFEST_SHA256,
              'case_count': len(cases), 'explicit_query_count': len(gpt_judge), 'formal_query_count': len(formal),
              'total_reservation_usd': str(total), 'session_budget_reset': False,
              'no_network_or_key_access': True, 'gold_read': False, 'batches': index}
    panel.write_new(directory / 'batch_index.json', report)
    return report


def prepare(release_path, release_sha256, output_dir):
    release = authorize(release_path, release_sha256)
    cases = load_inputs(release)
    return materialize_plan(cases, release, output_dir)


def replay(manifest_path, run_dir, cases, release):
    """Same frozen integrity checks/compilers, supplied validation cases explicitly."""
    manifest = read(manifest_path); directory = Path(run_dir)
    preflight_manifest_identity(manifest)
    meta = manifest.get('metadata', {})
    if (meta.get('split') != 'validation' or meta.get('release_sha256') != release['release_sha256'] or
            meta.get('session_budget_reset') is not False or
            safe._money(manifest.get('budget_usd')) != Decimal('2') or
            safe._money(meta.get('batch_cap_usd')) != safe._money(release['batch_cap_usd'])):
        raise ValueError('manifest_not_bound_to_validation_release')
    instrument, track = meta.get('instrument'), meta.get('track')
    if instrument not in ('gpt', 'jev') or track not in ('assisted_extraction', 'supplied_edge_judgment'):
        raise ValueError('unregistered_instrument_or_track')
    if instrument == 'jev' and track != 'supplied_edge_judgment':
        raise ValueError('jev_not_free_extractor')
    ledger = read(directory / 'ledger.json')
    if instrument == 'gpt':
        plan = safe.plan_manifest(manifest)
        attempts = safe._validate_ledger(ledger, plan, directory)
        expected_rows = expected_gpt(cases, track)[0]
        snapshot = read(integrity.SNAPSHOT)
        reserve = safe._money(plan['total_reservation_usd'])
    else:
        jev.validate_manifest(manifest)
        attempts = jev.validate_ledger(ledger, manifest, directory)
        expected_rows = expected_jev(cases)
        reserve = safe._money(manifest['total_reservation_usd'])
    limit = 24 if track == 'assisted_extraction' else 48
    if len(manifest['requests']) > limit or reserve > safe._money(release['batch_cap_usd']):
        raise ValueError('replay_batch_policy_drift')
    # Do not let a valid individual row authorize an arbitrarily shortened batch
    # and thereby erase unattempted planned denominators.
    packaged = greedy_batches(expected_rows, release['batch_cap_usd'], limit)
    if manifest['requests'] not in packaged:
        raise ValueError('deterministic_planned_batch_inventory_drift')
    expected = {r['id']: r for r in expected_rows}
    integrity.audit_billing_artifacts(attempts, directory)
    by_attempt = {r['id']: r for r in attempts}; case_by = {c['id']: c for c in cases}
    outputs, diagnostics = [], []
    for request in manifest['requests']:
        ident = request['id']
        if ident not in expected or request != expected[ident]:
            raise ValueError('frozen_validation_request_drift')
        attempt = by_attempt.get(ident, {})
        identity = 'case_id' if track == 'assisted_extraction' else 'query_id'
        output = {identity: ident, 'state': 'unavailable', 'reason': 'not_attempted'}
        if attempt.get('state') == 'completed' and attempt.get('http_status') == 200 and 'response_file' in attempt:
            try:
                raw = (directory / attempt['response_file']).read_bytes()
                if len(raw) > safe.MAX_RESPONSE_BYTES or hashlib.sha256(raw).hexdigest() != attempt['response_sha256']:
                    raise ValueError('response_artifact_drift')
                if instrument == 'jev':
                    parsed = jev.parse_response(raw, request, manifest['model_aliases'])
                    integrity.billing_consistent(parsed['reported_cost_usd'], attempt)
                    output = panel.compile_jev_judgment(parsed, ident)
                else:
                    content = integrity.gpt_content(raw, request['body'], snapshot)
                    integrity.billing_consistent(safe._response_result(raw)['reported_cost_usd'], attempt)
                    output = (panel.compile_extraction(content, case_by[ident]) if track == 'assisted_extraction'
                              else panel.compile_gpt_judgment(content, ident))
            except (KeyError, TypeError, ValueError, IndexError):
                output = {identity: ident, 'state': 'unavailable', 'reason': 'response_compile_or_identity_rejected'}
        elif attempt:
            output.update(reason='attempt_not_complete', attempt_state=attempt.get('state'))
        outputs.append(output)
        diagnostics.append({'request_id': ident, 'attempt_state': attempt.get('state', 'not_attempted'),
                            'compile_state': output['state'], 'reported_cost_usd': attempt.get('reported_cost_usd'),
                            'elapsed_seconds': attempt.get('elapsed_seconds')})
    costs = [safe._money(a['reported_cost_usd']) for a in attempts if 'reported_cost_usd' in a]
    unknown = sum((safe._money(a['reservation_usd']) for a in attempts if 'reported_cost_usd' not in a), Decimal(0))
    summary = {'instrument': instrument, 'track': track, 'split': 'validation',
               'release_sha256': release['release_sha256'], 'planned_requests': len(manifest['requests']),
               'attempted_requests': len(attempts), 'compiled_complete': sum(o['state'] == 'completed' for o in outputs),
               'reported_known_cost_usd': str(sum(costs, Decimal(0))),
               'missing_cost_attempts': sum('reported_cost_usd' not in a for a in attempts),
               'unknown_attempt_reserved_usd': str(unknown),
               'elapsed_seconds_recorded_sum': sum(a.get('elapsed_seconds', 0) for a in attempts),
               'manifest_sha256': panel.digest_file(manifest_path), 'ledger_sha256': panel.digest_file(directory / 'ledger.json'),
               'no_graph_promotion': True, 'diagnostics': diagnostics}
    return outputs, summary


def score_primary(cases, golds, outputs, track):
    if track == 'assisted_extraction':
        ids = {r['case_id'] for r in outputs}
        score = panel.score_extraction([c for c in cases if c['id'] in ids],
                                       [g for g in golds if g['id'] in ids], outputs)
    else:
        ids = {r['query_id'] for r in outputs}
        explicit = {q['id'] for c in explicit_cases(cases) for q in c['judgment_queries']}
        if not ids <= explicit:
            raise ValueError('formal_or_unknown_query_in_direct_source_score')
        relevant = []
        for gold in golds:
            item = deepcopy(gold)
            item['judgments'] = [q for q in item['judgments'] if q['query_id'] in ids]
            if item['judgments']:
                relevant.append(item)
        score = panel.score_judgments(relevant, outputs)
    score['split'] = 'validation'  # metadata only; primary metric algebra unchanged
    return score


def evaluate(release_path, release_sha256, manifest_path, run_dir, output_dir):
    release = authorize(release_path, release_sha256)
    preflight_manifest_identity(read(manifest_path))
    cases = load_inputs(release)
    outputs, summary = replay(manifest_path, run_dir, cases, release)
    folder = Path(output_dir)
    folder.mkdir(parents=True, exist_ok=False)
    # Preserve first compiled outputs before authorized gold access; no outcome
    # adaptation or replacement of an existing score directory is permitted.
    panel.write_new(folder / 'compiled_first.json', outputs)
    panel.write_new(folder / 'execution_summary.json', summary)
    gold = load_gold(release, cases)
    report = score_primary(cases, gold, outputs, summary['track'])
    panel.write_new(folder / 'score_first.json', report)
    return summary, report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--release', type=Path, required=True)
    p.add_argument('--release-sha256', required=True)
    sub = p.add_subparsers(dest='command', required=True)
    prep = sub.add_parser('prepare'); prep.add_argument('--output', type=Path, required=True)
    score = sub.add_parser('score'); score.add_argument('manifest', type=Path)
    score.add_argument('run_dir', type=Path); score.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.command == 'prepare':
        value = prepare(args.release, args.release_sha256, args.output)
    else:
        summary, report = evaluate(args.release, args.release_sha256, args.manifest, args.run_dir, args.output)
        value = {'summary': summary, 'strict_edges': report.get('strict_edges'), 'per_class': report.get('per_class')}
    print(safe.canonical(value).decode())


if __name__ == '__main__':
    main()
