#!/usr/bin/env python3
"""Eight unexecuted T3 arms; isolated binding of the frozen v1 execution client."""
from __future__ import annotations
import argparse
from copy import deepcopy
import hashlib
import io
import json
from pathlib import Path
import time
from types import FunctionType
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED
try:
    from . import recipe_live_pilot as v1
    from . import recipe_live_score as scoring
except ImportError:
    import recipe_live_pilot as v1
    import recipe_live_score as scoring

safe, pilot, recipes = v1.safe, v1.pilot, v1.recipes
HERE, REPO, FIXTURE = v1.HERE, v1.REPO, v1.FIXTURE
EXPERIMENT = 'jev-recipes-key-control8-20260930'
RUN_DIR = HERE / '.recipe-live' / EXPERIMENT
CASE_IDS = ('r06', 'r13', 'r15', 'r16')
ARMS = ('object_neutral', 'object_nonsense')
FIRST32_DIR = REPO / 'docs/research/jev_recipes_first32_2026-09-30'
FIRST32_SCORE_SHA256 = '28a297a834749d369334a189d0aacc4a8cb7786b861ddd64f5387452c72bcfa4'
FROZEN_V1_CODE = {
    'loom/tools/eval/jev_recipes.py': 'df5e139967279f305c36f1c84dd5afab0a05566219f8ebfff3f5d992ef8f90f5',
    'loom/tools/structure/jev_live_pilot.py': 'a3036e372d30f93659e1933a956b61972a50a50a216ba0340dee1b38d191e805',
    'loom/tools/structure/openrouter_runner.py': '2cd14424ca190f3d5fed47faf1e328993668a620b8202cfb7b196b363f971635',
    'loom/tools/structure/recipe_live_pilot.py': 'd60a2810f05307a7f4225a66df8bada257394725dd27d812e2842ed30135efb6'}


def code_hashes():
    if v1.code_hashes() != FROZEN_V1_CODE:
        raise safe.RunnerError('key_control_frozen_v1_client_changed')
    result = dict(FROZEN_V1_CODE)
    for path in (Path(__file__), HERE / 'recipe_live_score.py'):
        result[str(path.relative_to(REPO))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def selected_requests():
    archive = FIXTURE / 'requests.zip'
    if hashlib.sha256(archive.read_bytes()).hexdigest() != v1.ARCHIVE_SHA256:
        raise safe.RunnerError('key_control_original_archive_changed')
    bodies, index = recipes.unpack(archive)
    records = {r['request_id']: r for r in index['records']}
    rows = []
    for case in CASE_IDS:
        meaningful = safe.parse_json(bodies[records[case + '.object_meaningful']['file']])
        for arm in ARMS:
            identifier = case + '.' + arm
            record = records[identifier]
            raw = bodies[record['file']]
            if hashlib.sha256(raw).hexdigest() != record['sha256']:
                raise safe.RunnerError('key_control_source_hash_mismatch')
            body = safe.parse_json(raw)
            if body['state'] != meaningful['state'] or set(body['questions']) != {'expressed', 'inferred'}:
                raise safe.RunnerError('key_control_state_or_questions_changed')
            for name, question in body['questions'].items():
                previous = meaningful['questions'][name]
                if (question['type'] != previous['type'] or question['criteria'] != previous['criteria'] or
                        list(question['instructions'].values()) != list(previous['instructions'].values())):
                    raise safe.RunnerError('key_control_values_or_rule_order_changed')
            body['provider'] = deepcopy(v1.PROVIDER)
            wire = recipes.encode(body)
            if len(wire) > pilot.BODY_LIMIT or (len(wire) + 1024) * pilot.INPUT_CAP > pilot.RESERVE:
                raise safe.RunnerError('key_control_body_reservation_invalid')
            rows.append({'id': identifier, 'case_id': case, 'arm': arm, 'language': record['language'],
                         'source_file': record['file'], 'source_sha256': record['sha256'],
                         'body': body, 'request_hash': hashlib.sha256(wire).hexdigest(),
                         'reservation_usd': str(pilot.RESERVE)})
    return rows


def wire_bodies():
    return {r['id']: recipes.encode(r['body']) for r in selected_requests()}


def frozen_plan():
    return {'schema': 'loom.jev_recipe_key_control_plan/1', 'experiment_id': EXPERIMENT,
            'authorization_ref': v1.AUTHORIZATION_REF, 'planning_only': True, 'paid_retries': 0,
            'global_key_cap_usd': str(v1.GLOBAL_KEY_CAP), 'batch_cap_usd': str(v1.BATCH_CAP),
            'total_reservation_usd': '0.008', 'planned_calls': 8, 'planned_decisions': 16,
            'archive_sha256': v1.ARCHIVE_SHA256, 'first32_score_sha256': FIRST32_SCORE_SHA256,
            'evaluation_scope': 'targeted development; selected after first32 outcomes',
            'requests': selected_requests(), 'no_graph_promotion': True}


def prepare(plan, output_dir, *, transport_fn=None, run_dir=None):
    if plan != frozen_plan():
        raise safe.RunnerError('key_control_plan_not_frozen')
    hashes = code_hashes()
    status, raw = (transport_fn or pilot.transport)('GET', pilot.ENDPOINT, None, None)
    if status != 200:
        raise safe.RunnerError('key_control_catalog_http_error')
    snapshot = safe.parse_json(raw)
    aliases = pilot.endpoint_identity(snapshot)
    manifest = {'schema': 'loom.jev_recipe_key_control_manifest/1', 'experiment_id': EXPERIMENT,
                'authorization_ref': v1.AUTHORIZATION_REF, 'created_at': safe._utc(),
                'global_key_cap_usd': str(v1.GLOBAL_KEY_CAP), 'batch_cap_usd': str(v1.BATCH_CAP),
                'total_reservation_usd': '0.008', 'plan_hash': safe.digest(plan), 'requests': plan['requests'],
                'run_dir': str(Path(run_dir or RUN_DIR).resolve()), 'model_aliases': aliases,
                'endpoint_snapshot_hash': safe.digest(snapshot), 'code_sha256': hashes,
                'paid_retries': 0, 'billing_bound_guaranteed': False, 'no_graph_promotion': True}
    directory = Path(output_dir); directory.mkdir(parents=True, exist_ok=False)
    v1.write_new(directory / 'plan.json', plan)
    v1.write_new(directory / 'manifest.json', manifest)
    v1.write_new(directory / 'endpoint_snapshot.json', snapshot)
    return manifest


def validate_manifest(manifest):
    plan = frozen_plan()
    if (manifest.get('schema') != 'loom.jev_recipe_key_control_manifest/1' or
            manifest.get('experiment_id') != EXPERIMENT or manifest.get('code_sha256') != code_hashes() or
            manifest.get('authorization_ref') != v1.AUTHORIZATION_REF or
            manifest.get('plan_hash') != safe.digest(plan) or manifest.get('requests') != plan['requests'] or
            safe._money(manifest.get('global_key_cap_usd')) != v1.GLOBAL_KEY_CAP or
            not 0 < safe._money(manifest.get('batch_cap_usd')) <= v1.BATCH_CAP or
            safe._money(manifest.get('total_reservation_usd')) != 8 * pilot.RESERVE or
            8 * pilot.RESERVE > safe._money(manifest['batch_cap_usd']) or
            manifest.get('paid_retries') != 0 or manifest.get('no_graph_promotion') is not True):
        raise safe.RunnerError('key_control_manifest_or_cap_mismatch')
    path = manifest.get('run_dir')
    if not isinstance(path, str) or str(Path(path).resolve()) != path:
        raise safe.RunnerError('key_control_run_path_invalid')
    aliases = manifest.get('model_aliases')
    if (not isinstance(aliases, list) or pilot.MODEL not in aliases or
            any(not isinstance(a, str) or not pilot.re.fullmatch(r'typesafe/jev-1\.13(?:-[0-9]{8})?', a)
                for a in aliases)):
        raise safe.RunnerError('key_control_model_alias_invalid')
    safe._timestamp(manifest['created_at'])


def run(manifest, run_dir=None, *, transport_fn=None, key_loader=None):
    """Bind the frozen execution function without mutating v1 or its globals.

    The original first32 whitelist is intentionally closed. FunctionType gives
    its *unchanged* bytecode a private globals dictionary containing just this
    separate plan validator, wire provider and identity. All budget, receipt,
    parse, audit, stop and resume functions are the original frozen v1 objects.
    Both sources are hashed in the manifest. Original v1 remains independently
    callable/validatable, including concurrently in this Python interpreter.
    """
    namespace = dict(v1.run.__globals__)
    namespace.update(validate_manifest=validate_manifest, wire_bodies=wire_bodies, EXPERIMENT=EXPERIMENT)
    bound = FunctionType(v1.run.__code__, namespace, 'run_key_control', v1.run.__defaults__, v1.run.__closure__)
    bound.__kwdefaults__ = dict(v1.run.__kwdefaults__)
    return bound(manifest, run_dir, transport_fn=transport_fn, key_loader=key_loader)


def preflight(manifest, *, transport_fn=None, key_loader=None):
    validate_manifest(manifest)
    if not -300 <= time.time() - safe._timestamp(manifest['created_at']) <= 86400:
        raise safe.RunnerError('key_control_prices_stale')
    send = transport_fn or pilot.transport
    status, raw = send('GET', pilot.ENDPOINT, None, None)
    if status != 200 or pilot.endpoint_identity(safe.parse_json(raw)) != manifest['model_aliases']:
        raise safe.RunnerError('key_control_current_endpoint_changed')
    return v1.key_check(send, (key_loader or v1.load_key)(), 8 * pilot.RESERVE)


def score(manifest, run_dir):
    validate_manifest(manifest)
    directory = Path(run_dir)
    ledger = pilot.read_json(directory / 'ledger.json')
    v1.validate_ledger(ledger, manifest, directory)
    gold_path = FIXTURE / 'gold.json'
    if hashlib.sha256(gold_path.read_bytes()).hexdigest() != scoring.GOLD_SHA256:
        raise safe.RunnerError('key_control_original_gold_changed')
    gold = pilot.read_json(gold_path)['cases']
    first_path = FIRST32_DIR / 'first_score.json'
    if hashlib.sha256(first_path.read_bytes()).hexdigest() != FIRST32_SCORE_SHA256:
        raise safe.RunnerError('key_control_first32_comparator_changed')
    first = pilot.read_json(first_path)
    previous = [r for r in first['rows'] if r['case_id'] in CASE_IDS]
    attempts = {r['id']: r for r in ledger['attempts']}
    rows = []
    for request in manifest['requests']:
        a = attempts.get(request['id'], {})
        probs = {}
        if a.get('state') == 'completed':
            probs = pilot.parse_response((directory / a['response_file']).read_bytes(), request, manifest['model_aliases'])['probabilities']
        for question in ('expressed', 'inferred'):
            rows.append({'request_id': request['id'], 'case_id': request['case_id'], 'arm': request['arm'],
                         'question': question, 'language': request['language'],
                         'label': gold[request['case_id']][question], 'probability': probs.get(question),
                         'attempt_state': a.get('state', 'not_attempted')})
    profiles = {}
    combined = rows + previous
    for question in ('expressed', 'inferred'):
        profiles[question] = {arm: scoring.metrics([r for r in combined if r['question'] == question and r['arm'] == arm])
                              for arm in ('object_meaningful', 'string', *ARMS)}
    contrasts = []
    by_key = {(r['case_id'], r['question'], r['arm']): r for r in combined}
    for case in CASE_IDS:
        for question in ('expressed', 'inferred'):
            for a, b in (('object_neutral', 'object_meaningful'), ('object_nonsense', 'object_meaningful'),
                         ('object_neutral', 'object_nonsense'), ('string', 'object_meaningful')):
                left, right = by_key[case, question, a], by_key[case, question, b]
                p, q = left['probability'], right['probability']
                contrasts.append({'case_id': case, 'question': question, 'contrast': a + '_minus_' + b,
                                  'probability_delta': p - q if p is not None and q is not None else None,
                                  'squared_error_delta': (p-left['label'])**2 - (q-right['label'])**2
                                      if p is not None and q is not None else None})
    return {'schema': 'loom.jev_recipe_key_control_score/1', 'manifest_hash': safe.digest(manifest),
            'scorer_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'first32_score_sha256': FIRST32_SCORE_SHA256, 'evaluation_scope': frozen_plan()['evaluation_scope'],
            'status': {'planned_calls': 8, 'attempted_calls': len(attempts),
                       'completed_calls': sum(a['state'] == 'completed' for a in attempts.values()),
                       'stopped_reason': ledger.get('stopped_reason'), 'reported_cost_usd': ledger.get('reported_cost_usd')},
            'new_decisions': scoring.metrics(rows), 'profiles_same_four_cases': profiles,
            'contrasts': contrasts, 'new_rows': rows, 'first32_comparator_rows': previous, 'no_graph_promotion': True}


def archive(prepared_dir, output, *, score_path=None):
    prepared = Path(prepared_dir)
    manifest = pilot.read_json(prepared / 'manifest.json')
    validate_manifest(manifest)
    directory = Path(manifest['run_dir'])
    with safe._lock(directory):
        ledger = pilot.read_json(directory / 'ledger.json')
        attempts = v1.validate_ledger(ledger, manifest, directory)
        if any(a['state'] == 'started' for a in attempts) or not ledger.get('stopped_reason') and len(attempts) != 8:
            raise safe.RunnerError('key_control_archive_series_not_closed')
        files = {'prepared/' + n: (prepared / n).read_bytes()
                 for n in ('plan.json', 'manifest.json', 'endpoint_snapshot.json')}
        files['run/ledger.json'] = (directory / 'ledger.json').read_bytes()
        for i, a in enumerate(attempts):
            for n in (f'{i:02d}.started.json', f'{i:02d}.result.json'):
                files['run/' + n] = (directory / n).read_bytes()
            if 'response_file' in a:
                files['run/' + a['response_file']] = (directory / a['response_file']).read_bytes()
        for path, expected in manifest['code_sha256'].items():
            raw = (REPO / path).read_bytes()
            if hashlib.sha256(raw).hexdigest() != expected:
                raise safe.RunnerError('key_control_archive_executed_code_changed')
            files['replay/' + path] = raw
        for n in ('requests.zip', 'gold.json', 'corpus.json', 'recipes.json', 'manifest.json', 'source_freeze.json'):
            files['replay/loom/tests/fixtures/eval/jev_recipes_v1/' + n] = (FIXTURE / n).read_bytes()
        first = (FIRST32_DIR / 'first_score.json').read_bytes()
        if hashlib.sha256(first).hexdigest() != FIRST32_SCORE_SHA256:
            raise safe.RunnerError('key_control_archive_comparator_changed')
        files['replay/docs/research/jev_recipes_first32_2026-09-30/first_score.json'] = first
        if score_path is not None:
            raw = Path(score_path).read_bytes()
            if safe.parse_json(raw).get('manifest_hash') != safe.digest(manifest):
                raise safe.RunnerError('key_control_archive_score_manifest_mismatch')
            files['analysis/first_score.json'] = raw
        files['REPLAY.md'] = (
            '# Offline optional8 development replay\n\n'
            'Extract this archive, then from its root:\n\n```sh\n'
            'python replay/loom/tools/structure/recipe_key_control_pilot.py score '
            '--manifest prepared/manifest.json --run-dir run --output replayed_score.json\n```\n\n'
            'No credentials or network. Frozen v1/new adapter, original T3 bodies/gold and '
            'immutable first32 comparator are included. Inventory hashes cover all payloads. '
            'The absolute original run path remains in the manifest for identity.\n\n'
            'Before any execution resume after workspace restoration, restore run/ receipts '
            'and checkpoint into the original canonical path; never reconstruct responses '
            'through new paid calls. Original USD 2 nonresetting key cap remains shared.\n'
        ).encode('utf-8')
        inventory = {'schema': 'loom.jev_recipe_key_control_archive/1', 'experiment_id': EXPERIMENT,
                     'manifest_hash': safe.digest(manifest), 'archived_at': safe._utc(),
                     'attempted_calls': len(attempts), 'completed_calls': sum(a['state'] == 'completed' for a in attempts),
                     'stopped_reason': ledger.get('stopped_reason'),
                     'files': {n: {'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)}
                               for n, raw in sorted(files.items())}}
        files['INVENTORY.json'] = safe.canonical(inventory) + b'\n'
        if sum(map(len, files.values())) > 128 * 1024 * 1024:
            raise safe.RunnerError('key_control_archive_size_limit')
        buffer = io.BytesIO()
        with ZipFile(buffer, 'w', compression=ZIP_DEFLATED, compresslevel=9) as z:
            for name, raw in sorted(files.items()):
                info = ZipInfo(name, date_time=(2026, 9, 30, 0, 0, 0))
                info.compress_type = ZIP_DEFLATED; info.external_attr = 0o100600 << 16
                z.writestr(info, raw, compress_type=ZIP_DEFLATED, compresslevel=9)
        v1.write_bytes_new(output, buffer.getvalue())
        return inventory


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    freeze = sub.add_parser('freeze'); freeze.add_argument('--output', type=Path, required=True)
    prep = sub.add_parser('prepare'); prep.add_argument('--plan', type=Path, required=True); prep.add_argument('--output-dir', type=Path, required=True)
    backup = sub.add_parser('archive'); backup.add_argument('--prepared-dir', type=Path, required=True)
    backup.add_argument('--output', type=Path, required=True); backup.add_argument('--score', type=Path)
    for name in ('preflight', 'run', 'score'):
        cmd = sub.add_parser(name); cmd.add_argument('--manifest', type=Path, required=True)
        if name == 'score':
            cmd.add_argument('--run-dir', type=Path, required=True); cmd.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'freeze':
            v1.write_new(args.output, frozen_plan()); result = {'planning_only': True, 'planned_calls': 8, 'reservation_usd': '0.008'}
        elif args.command == 'prepare':
            m = prepare(pilot.read_json(args.plan), args.output_dir); result = {'paid_calls': 0, 'planned_calls': 8, 'run_dir': m['run_dir']}
        elif args.command == 'preflight':
            result = {'paid_calls': 0, 'key_check': preflight(pilot.read_json(args.manifest))}
        elif args.command == 'archive':
            inventory = archive(args.prepared_dir, args.output, score_path=args.score)
            result = {'archived_calls': inventory['attempted_calls'], 'files': len(inventory['files']),
                      'stopped_reason': inventory['stopped_reason']}
        elif args.command == 'score':
            result = score(pilot.read_json(args.manifest), args.run_dir); v1.write_new(args.output, result)
            result = result['status']
        else:
            m = pilot.read_json(args.manifest)
            if m.get('run_dir') != str(RUN_DIR.resolve()):
                raise safe.RunnerError('key_control_cli_noncanonical_run_path')
            ledger = run(m); result = {'attempts': len(ledger['attempts']), 'stopped_reason': ledger.get('stopped_reason')}
        print(json.dumps(result))
        return 2 if result.get('stopped_reason') else 0
    except (safe.RunnerError, OSError, ValueError, KeyError, TypeError):
        print(json.dumps({'ok': False, 'error': 'key_control_operation_rejected'})); return 2


if __name__ == '__main__':
    raise SystemExit(main())
