#!/usr/bin/env python3
"""Execute only the preregistered T3 first32; immutable first attempts, no retries.

The default CLI run path is fixed for this experiment. Test seams are transports,
credential loaders and an isolated prepare run_dir; no production code is edited.
"""
from __future__ import annotations
import argparse
from copy import deepcopy
from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import stat
import time
try:
    from . import jev_live_pilot as pilot
    from ..eval import jev_recipes as recipes
except ImportError:
    import jev_live_pilot as pilot
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'eval'))
    import jev_recipes as recipes

safe = pilot.safe
HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
FIXTURE = REPO / 'loom/tests/fixtures/eval/jev_recipes_v1'
EXPERIMENT = 'jev-recipes-t3-first32-20260930'
RUN_DIR = HERE / '.recipe-live' / EXPERIMENT
AUTHORIZATION_REF = 'docs/research/JEV_LIVE_PROTOCOL_2026-09-28.md#existing-shared-usd2-key'
GLOBAL_KEY_CAP = Decimal('2')
BATCH_CAP = Decimal('0.10')
ARMS = ('object_meaningful', 'string')
ARCHIVE_SHA256 = 'c14ba47a84192f708fe1fae06454996eb9a90444e42a93cf379b8fb92619673c'
PROVIDER = {'only': ['typesafe'], 'allow_fallbacks': False,
            'max_price': {'prompt': '0.042', 'completion': '0'}}


def write_bytes_new(path, raw):
    """Exclusive, flushed first evidence: unlike the checkpoint, never replaced."""
    with Path(path).open('xb') as handle:
        handle.write(raw)
        handle.flush()
        os.fsync(handle.fileno())


def write_new(path, value):
    write_bytes_new(path, safe.canonical(value) + b'\n')


def code_hashes():
    paths = [Path(__file__), HERE / 'jev_live_pilot.py', HERE / 'openrouter_runner.py',
             HERE.parent / 'eval/jev_recipes.py']
    return {str(p.relative_to(REPO)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


def selected_requests():
    """Input-only selection: never opens labels or any independent holdout."""
    archive = FIXTURE / 'requests.zip'
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise safe.RunnerError('recipe_original_archive_hash_mismatch')
    bodies, index = recipes.unpack(archive)
    if index.get('body_count') != 208:
        raise safe.RunnerError('recipe_source_inventory_changed')
    records = {r['request_id']: r for r in index['records']}
    rows = []
    for n in range(1, 17):
        for arm in ARMS:
            identifier = f'r{n:02d}.{arm}'
            source = records[identifier]
            raw = bodies[source['file']]
            if source['family'] != 'relation' or hashlib.sha256(raw).hexdigest() != source['sha256']:
                raise safe.RunnerError('recipe_source_body_mismatch')
            body = safe.parse_json(raw)
            body['provider'] = deepcopy(PROVIDER)
            validate_body(body)
            rows.append({'id': identifier, 'case_id': f'r{n:02d}', 'arm': arm,
                         'language': source['language'], 'source_file': source['file'],
                         'source_sha256': source['sha256'], 'body': body,
                         'request_hash': hashlib.sha256(recipes.encode(body)).hexdigest(),
                         'reservation_usd': str(pilot.RESERVE)})
    return rows


def validate_body(body):
    safe._keys(body, {'model', 'provider', 'state', 'questions'})
    if body['model'] != pilot.MODEL or body['provider'] != PROVIDER:
        raise safe.RunnerError('recipe_model_provider_price_pin_invalid')
    safe._keys(body['state'], {'conversation_history', 'target_turn', 'candidate_relation'})
    if (not isinstance(body['state']['conversation_history'], list) or
            any(not isinstance(body['state'][k], str) or not body['state'][k]
                for k in ('target_turn', 'candidate_relation'))):
        raise safe.RunnerError('recipe_state_invalid')
    safe._keys(body['questions'], {'expressed', 'inferred'})
    for question in body['questions'].values():
        safe._keys(question, {'type', 'instructions', 'criteria'})
        safe._keys(question['criteria'], {'true', 'false'})
        instruction = question['instructions']
        if isinstance(instruction, dict):
            safe._keys(instruction, {'task', 'criterion', 'boundary'})
            values = list(instruction.values())
        else:
            values = [instruction]
        if (question['type'] != 'noul' or
                any(not isinstance(v, str) or not v for v in values + list(question['criteria'].values()))):
            raise safe.RunnerError('recipe_noul_instruction_invalid')
    size = len(recipes.encode(body))
    if size > pilot.BODY_LIMIT or (size + 1024) * pilot.INPUT_CAP > pilot.RESERVE:
        raise safe.RunnerError('recipe_body_exceeds_reservation_allowance')


def wire_bodies():
    # Recover the frozen insertion order, including instruction object rule order.
    # A canonical manifest checkpoint sorts its keys; that must not reorder rules
    # on the wire and confound the object versus string representation contrast.
    return {r['id']: recipes.encode(r['body']) for r in selected_requests()}


def frozen_plan():
    return {'schema': 'loom.jev_recipe_plan/1', 'experiment_id': EXPERIMENT,
            'authorization_ref': AUTHORIZATION_REF, 'planning_only': True,
            'paid_retries': 0, 'global_key_cap_usd': str(GLOBAL_KEY_CAP),
            'batch_cap_usd': str(BATCH_CAP), 'total_reservation_usd': '0.032',
            'archive_sha256': hashlib.sha256((FIXTURE / 'requests.zip').read_bytes()).hexdigest(),
            'planned_calls': 32, 'planned_decisions': 64, 'requests': selected_requests(),
            'no_graph_promotion': True}


def validate_plan(plan):
    if plan != frozen_plan():
        raise safe.RunnerError('recipe_plan_does_not_match_frozen_input_selection')


def prepare(plan, output_dir, *, transport_fn=None, run_dir=None):
    """Freeze catalog through GET only; no credential or gold is loaded."""
    validate_plan(plan)
    status, raw = (transport_fn or pilot.transport)('GET', pilot.ENDPOINT, None, None)
    if status != 200:
        raise safe.RunnerError('recipe_catalog_http_error')
    snapshot = safe.parse_json(raw)
    aliases = pilot.endpoint_identity(snapshot)
    manifest = {'schema': 'loom.jev_recipe_manifest/1', 'experiment_id': EXPERIMENT,
                'authorization_ref': AUTHORIZATION_REF, 'created_at': safe._utc(),
                'global_key_cap_usd': str(GLOBAL_KEY_CAP), 'batch_cap_usd': str(BATCH_CAP),
                'total_reservation_usd': plan['total_reservation_usd'],
                'plan_hash': safe.digest(plan), 'requests': plan['requests'],
                'run_dir': str(Path(run_dir or RUN_DIR).resolve()),
                'model_aliases': aliases, 'endpoint_snapshot_hash': safe.digest(snapshot),
                'code_sha256': code_hashes(), 'paid_retries': 0,
                'billing_bound_guaranteed': False, 'no_graph_promotion': True}
    directory = Path(output_dir)
    directory.mkdir(parents=True, exist_ok=False)
    write_new(directory / 'plan.json', plan)
    write_new(directory / 'endpoint_snapshot.json', snapshot)
    write_new(directory / 'manifest.json', manifest)
    return manifest


def validate_manifest(manifest):
    plan = frozen_plan()
    if (manifest.get('schema') != 'loom.jev_recipe_manifest/1' or
            manifest.get('experiment_id') != EXPERIMENT or
            manifest.get('authorization_ref') != AUTHORIZATION_REF or
            manifest.get('code_sha256') != code_hashes() or
            manifest.get('plan_hash') != safe.digest(plan) or
            manifest.get('requests') != plan['requests'] or
            manifest.get('paid_retries') != 0 or manifest.get('no_graph_promotion') is not True):
        raise safe.RunnerError('recipe_manifest_or_frozen_requests_mismatch')
    rows = manifest['requests']
    if (len(rows) > 64 or len({r['id'] for r in rows}) != len(rows) or
            safe._money(manifest['global_key_cap_usd']) != GLOBAL_KEY_CAP or
            not 0 < safe._money(manifest['batch_cap_usd']) <= BATCH_CAP or
            len(rows) * pilot.RESERVE != safe._money(manifest['total_reservation_usd']) or
            len(rows) * pilot.RESERVE > safe._money(manifest['batch_cap_usd'])):
        raise safe.RunnerError('recipe_manifest_cap_invalid')
    run_dir = manifest.get('run_dir')
    if not isinstance(run_dir, str) or str(Path(run_dir).resolve()) != run_dir:
        raise safe.RunnerError('recipe_run_path_invalid')
    aliases = manifest.get('model_aliases')
    if (not isinstance(aliases, list) or pilot.MODEL not in aliases or
            any(not isinstance(x, str) or not pilot.re.fullmatch(r'typesafe/jev-1\.13(?:-[0-9]{8})?', x)
                for x in aliases)):
        raise safe.RunnerError('recipe_model_alias_invalid')
    safe._timestamp(manifest['created_at'])


def load_key():
    """One explicit private file; never read another environment or discover keys."""
    filename = os.environ.get('OPENROUTER_KEY_FILE')
    if not filename:
        raise safe.RunnerError('recipe_key_file_not_configured')
    try:
        path = Path(filename).expanduser().resolve()
        if path == REPO or REPO in path.parents:
            raise safe.RunnerError('recipe_key_file_must_be_outside_repository')
        info = path.stat()
        if not stat.S_ISREG(info.st_mode) or info.st_mode & 0o077 or info.st_size > 4096:
            raise safe.RunnerError('recipe_key_file_requires_private_regular_file')
        key = path.read_text('utf-8').strip()
    except (OSError, UnicodeError):
        raise safe.RunnerError('recipe_key_file_unavailable') from None
    if not key or len(key) > 4096 or not key.isascii() or any(c.isspace() for c in key):
        raise safe.RunnerError('recipe_key_invalid')
    return key


def key_check(send, key, needed):
    status, raw = send('GET', '/api/v1/key', None, key)
    if status != 200:
        raise safe.RunnerError('recipe_key_metadata_unavailable')
    data = safe.parse_json(raw).get('data')
    if not isinstance(data, dict) or 'limit_reset' not in data or data['limit_reset'] is not None:
        raise safe.RunnerError('recipe_nonresetting_key_not_explicit')
    check = safe._key_gate(raw, GLOBAL_KEY_CAP, needed)
    if safe._money(data.get('byok_usage')) != 0:
        raise safe.RunnerError('recipe_key_has_byok_usage')
    return {**check, 'byok_usage_usd': '0'}


def preflight(manifest, *, transport_fn=None, key_loader=None):
    """Read-only current catalog/key checks, without creating attempt state."""
    validate_manifest(manifest)
    if not -300 <= time.time() - safe._timestamp(manifest['created_at']) <= 86400:
        raise safe.RunnerError('recipe_prices_stale')
    send = transport_fn or pilot.transport
    status, raw = send('GET', pilot.ENDPOINT, None, None)
    if status != 200:
        raise safe.RunnerError('recipe_catalog_http_error')
    if pilot.endpoint_identity(safe.parse_json(raw)) != manifest['model_aliases']:
        raise safe.RunnerError('recipe_current_endpoint_identity_changed')
    key = (key_loader or load_key)()
    return key_check(send, key, safe._money(manifest['total_reservation_usd']))


def summarize(ledger):
    attempts = ledger['attempts']
    ledger['reported_cost_usd'] = str(sum((safe._money(a['reported_cost_usd']) for a in attempts
                                          if 'reported_cost_usd' in a), Decimal(0)))
    ledger['attempts_with_unknown_cost'] = sum('reported_cost_usd' not in a for a in attempts)
    ledger['generation_reported_cost_usd'] = str(sum((safe._money(a['generation_reported_cost_usd'])
        for a in attempts if 'generation_reported_cost_usd' in a), Decimal(0)))


def generation_check(send, key, parsed, aliases, row):
    """One optional GET; preserve reported audit cost before contradiction stops."""
    try:
        status, raw = send('GET', '/api/v1/generation?id=' + parsed['generation_id'], None, key)
        if status != 200:
            return {'audit_status': 'unavailable', 'http_status': status}
        data = safe.parse_json(raw).get('data')
    except Exception:
        return {'audit_status': 'unavailable'}
    if not isinstance(data, dict):
        return {'audit_status': 'invalid_envelope'}
    if data.get('id') != parsed['generation_id']:
        return {'audit_status': 'identity_unverified'}
    try:
        cost = safe._money(data.get('total_cost'))
        row['generation_reported_cost_usd'] = str(cost)
    except safe.RunnerError:
        cost = None
    if data.get('is_byok') is True:
        raise safe.RunnerError('recipe_generation_byok_detected')
    if cost is not None and cost > pilot.RESERVE:
        raise safe.RunnerError('recipe_generation_cost_exceeds_reservation')
    for name, allowed in (('model', aliases), ('provider_name', ['TypeSafe']), ('api_type', ['decisions'])):
        if name in data and data[name] not in allowed:
            raise safe.RunnerError('recipe_generation_identity_contradiction')
    if any(name not in data for name in ('model', 'provider_name', 'api_type')):
        return {'audit_status': 'identity_unverified'}
    return {'audit_status': 'verified' if data.get('is_byok') is False and cost is not None else 'partial',
            'model': data['model'], 'provider_name': 'TypeSafe', 'api_type': 'decisions',
            'is_byok': data.get('is_byok') if type(data.get('is_byok')) is bool else None,
            'total_cost_usd': str(cost) if cost is not None else None}


def validate_ledger(ledger, manifest, directory):
    if (ledger.get('schema') != 'loom.jev_recipe_ledger/1' or
            ledger.get('manifest_hash') != safe.digest(manifest)):
        raise safe.RunnerError('recipe_ledger_manifest_mismatch')
    attempts = ledger.get('attempts')
    if not isinstance(attempts, list) or len(attempts) > len(manifest['requests']):
        raise safe.RunnerError('recipe_ledger_count_invalid')
    for i, row in enumerate(attempts):
        request = manifest['requests'][i]
        start = pilot.read_json(directory / f'{i:02d}.started.json')
        for key in ('id', 'request_hash', 'reservation_usd'):
            if row.get(key) != request[key] or start.get(key) != row.get(key):
                raise safe.RunnerError('recipe_ledger_request_mismatch')
        if (start.get('state') != 'started' or start.get('manifest_hash') != safe.digest(manifest) or
                start.get('started_at') != row.get('started_at')):
            raise safe.RunnerError('recipe_start_receipt_mismatch')
        if row.get('state') not in ('started', 'completed', 'rejected', 'uncertain'):
            raise safe.RunnerError('recipe_ledger_state_invalid')
        terminal = directory / f'{i:02d}.result.json'
        if row['state'] != 'started' and (not terminal.exists() or pilot.read_json(terminal) != row):
            raise safe.RunnerError('recipe_terminal_receipt_mismatch')
        if 'response_file' in row:
            if row['response_file'] != row['id'] + '.response.bin':
                raise safe.RunnerError('recipe_response_path_invalid')
            if hashlib.sha256((directory / row['response_file']).read_bytes()).hexdigest() != row.get('response_sha256'):
                raise safe.RunnerError('recipe_response_hash_mismatch')
    return attempts


def run(manifest, run_dir=None, *, transport_fn=None, key_loader=None):
    validate_manifest(manifest)
    directory = Path(run_dir or manifest['run_dir']).resolve()
    if str(directory) != manifest['run_dir']:
        raise safe.RunnerError('recipe_canonical_run_path_mismatch')
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    send, loader = transport_fn or pilot.transport, key_loader or load_key
    wires = wire_bodies()
    path = directory / 'ledger.json'
    with safe._lock(directory):
        if path.exists():
            ledger = pilot.read_json(path)
            attempts = validate_ledger(ledger, manifest, directory)
            for i, row in enumerate(attempts):
                if row['state'] != 'started':
                    if row['state'] != 'completed':
                        ledger['stopped_reason'] = row.get('reason', 'recipe_terminal_attempt_failed')
                    continue
                terminal = directory / f'{i:02d}.result.json'
                if terminal.exists():
                    finished = pilot.read_json(terminal)
                    if (finished.get('state') not in ('completed', 'rejected', 'uncertain') or
                            any(finished.get(k) != row[k] for k in
                                ('id', 'request_hash', 'reservation_usd', 'started_at'))):
                        raise safe.RunnerError('recipe_terminal_recovery_invalid')
                    row.clear(); row.update(finished)
                else:
                    row.update(state='uncertain', reason='interrupted_no_retry', finished_at=safe._utc())
                    write_new(terminal, row)
                if row['state'] != 'completed':
                    ledger['stopped_reason'] = row.get('reason', 'recipe_interrupted_billing_unknown')
            safe._atomic(path, safe.canonical(ledger))
            validate_ledger(ledger, manifest, directory)
            if ledger.get('stopped_reason') or len(attempts) == len(manifest['requests']):
                summarize(ledger)
                safe._atomic(path, safe.canonical(ledger))
                return ledger
        else:
            if any(directory.glob('*.response.bin')) or any(directory.glob('*.started.json')) or any(directory.glob('*.result.json')):
                raise safe.RunnerError('recipe_stranded_evidence_no_restart')
            ledger = {'schema': 'loom.jev_recipe_ledger/1', 'manifest_hash': safe.digest(manifest),
                      'experiment_id': EXPERIMENT, 'created_at': safe._utc(), 'attempts': [],
                      'no_graph_promotion': True}
            attempts = ledger['attempts']
            safe._atomic(path, safe.canonical(ledger))
        if not -300 <= time.time() - safe._timestamp(manifest['created_at']) <= 86400:
            raise safe.RunnerError('recipe_prices_stale')
        key = loader()
        if not isinstance(key, str) or not key or key.encode() in safe.canonical(manifest):
            raise safe.RunnerError('recipe_credential_invalid_or_in_manifest')
        for request in manifest['requests'][len(attempts):]:
            # Current global remaining budget covers every remaining first attempt.
            ledger['key_before_next'] = key_check(send, key, (len(manifest['requests']) - len(attempts)) * pilot.RESERVE)
            safe._atomic(path, safe.canonical(ledger))
            i = len(attempts)
            row = {k: request[k] for k in ('id', 'request_hash', 'reservation_usd')}
            row.update(state='started', started_at=safe._utc())
            write_new(directory / f'{i:02d}.started.json', {**row, 'manifest_hash': safe.digest(manifest)})
            attempts.append(row)
            safe._atomic(path, safe.canonical(ledger))
            try:
                began = time.monotonic()
                status, raw = send('POST', pilot.DECISIONS, wires[request['id']], key)
                if not isinstance(raw, bytes) or len(raw) > safe.MAX_RESPONSE_BYTES or key.encode() in raw:
                    raise safe.RunnerError('recipe_response_unsafe_to_persist')
                row.update(response_file=row['id'] + '.response.bin', response_sha256=hashlib.sha256(raw).hexdigest(),
                           http_status=status, elapsed_seconds=round(time.monotonic() - began, 6), cost_status='unknown')
                write_bytes_new(directory / row['response_file'], raw)
                # Preserve billing independently from HTTP status and answer validity.
                try:
                    envelope = safe.parse_json(raw)
                    usage = envelope.get('usage') if isinstance(envelope, dict) else None
                    cost = safe._money(usage.get('cost') if isinstance(usage, dict) else None)
                    row.update(reported_cost_usd=str(cost), cost_status='reported')
                except safe.RunnerError:
                    pass
                safe._atomic(path, safe.canonical(ledger))
                if 'reported_cost_usd' in row and safe._money(row['reported_cost_usd']) > pilot.RESERVE:
                    raise safe.RunnerError('recipe_cost_exceeds_reservation')
                if status != 200:
                    raise safe.RunnerError('recipe_http_error_no_retry')
                parsed = pilot.parse_response(raw, request, manifest['model_aliases'])
                row.update(parsed)
                row['generation_billing'] = generation_check(send, key, parsed, manifest['model_aliases'], row)
                row.update(state='completed', finished_at=safe._utc())
            except Exception as error:
                reason = str(error) if isinstance(error, safe.RunnerError) else 'recipe_attempt_uncertain'
                row.update(state='rejected' if 'response_file' in row else 'uncertain', reason=reason, finished_at=safe._utc())
                ledger['stopped_reason'] = reason
            write_new(directory / f'{i:02d}.result.json', row)
            safe._atomic(path, safe.canonical(ledger))
            if ledger.get('stopped_reason'):
                break
        try:
            ledger['key_after'] = key_check(send, key, Decimal(0))
        except Exception:
            ledger['post_key_check_failed'] = True
            ledger.setdefault('stopped_reason', 'recipe_post_key_check_failed')
        summarize(ledger)
        safe._atomic(path, safe.canonical(ledger))
        return ledger


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    freeze = commands.add_parser('freeze')
    freeze.add_argument('--output', type=Path, required=True)
    prepare_cmd = commands.add_parser('prepare')
    prepare_cmd.add_argument('--plan', type=Path, required=True)
    prepare_cmd.add_argument('--output-dir', type=Path, required=True)
    run_cmd = commands.add_parser('run')
    run_cmd.add_argument('--manifest', type=Path, required=True)
    preflight_cmd = commands.add_parser('preflight')
    preflight_cmd.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'freeze':
            plan = frozen_plan()
            write_new(args.output, plan)
            result = {'planning_only': True, 'planned_calls': 32, 'reservation_usd': '0.032'}
        elif args.command == 'prepare':
            manifest = prepare(pilot.read_json(args.plan), args.output_dir)
            result = {'paid_calls': 0, 'planned_calls': len(manifest['requests']), 'run_dir': manifest['run_dir']}
        elif args.command == 'preflight':
            result = {'paid_calls': 0, 'key_check': preflight(pilot.read_json(args.manifest))}
        else:
            manifest = pilot.read_json(args.manifest)
            # CLI cannot point the same experiment at a fresh ledger directory.
            if manifest.get('run_dir') != str(RUN_DIR.resolve()):
                raise safe.RunnerError('recipe_cli_run_path_not_canonical')
            ledger = run(manifest)
            result = {'attempts': len(ledger['attempts']), 'reported_cost_usd': ledger.get('reported_cost_usd'),
                      'stopped_reason': ledger.get('stopped_reason')}
        print(json.dumps(result))
        return 0
    except (safe.RunnerError, OSError, ValueError, KeyError, TypeError):
        print(json.dumps({'ok': False, 'error': 'recipe_operation_rejected'}))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
