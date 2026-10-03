#!/usr/bin/env python3
"""Frozen, bounded Jev Decisions pilot; no canonical graph writes or paid retries."""
from __future__ import annotations
import argparse
from collections import defaultdict
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import time
import urllib.error
import urllib.request
try:
    from . import openrouter_runner as safe
except ImportError:
    import openrouter_runner as safe

MODEL = 'typesafe/jev-1.13'
ROOT = 'https://openrouter.ai'
ENDPOINT = '/api/v1/models/' + MODEL + '/endpoints'
DECISIONS = '/api/alpha/decisions'
BODY_LIMIT = 16384
RESERVE = Decimal('0.001')
INPUT_CAP = Decimal('0.000000042')
HERE = Path(__file__).resolve().parent


def read_json(path):
    with Path(path).open('rb') as handle:
        raw = handle.read(safe.MAX_INPUT_BYTES + 1)
    if len(raw) > safe.MAX_INPUT_BYTES:
        raise safe.RunnerError('input_size_limit')
    return safe.parse_json(raw)


def write_new(path, value):
    with Path(path).open('xb') as handle:
        handle.write(safe.canonical(value) + b'\n')


def code_hashes():
    return {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest()
            for name in ('jev_live_pilot.py', 'openrouter_runner.py')}


def transport(method, path, body=None, key=None):
    allowed = (method == 'POST' and path == DECISIONS or
               method == 'GET' and (path in ('/api/v1/key', ENDPOINT) or
               re.fullmatch(r'/api/v1/generation\?id=[A-Za-z0-9_-]{1,200}', path)))
    if not allowed:
        raise safe.RunnerError('invalid_jev_route')
    headers = {'Content-Type': 'application/json', 'X-OpenRouter-Title': 'Loom bounded Jev pilot'}
    if key is not None:
        headers['Authorization'] = 'Bearer ' + key
    request = urllib.request.Request(ROOT + path, data=body, method=method, headers=headers)
    with safe._deadline():
        try:
            response = urllib.request.build_opener(safe._NoRedirect()).open(request, timeout=5 if 'generation?' in path else 30)
        except urllib.error.HTTPError as error:
            response = error
        except Exception:
            raise safe.RunnerError('jev_transport_uncertain') from None
        with response:
            raw = response.read(safe.MAX_RESPONSE_BYTES + 1)
            if len(raw) > safe.MAX_RESPONSE_BYTES:
                raise safe.RunnerError('jev_response_size_limit')
            return int(response.status), raw


def validate_body(body):
    safe._keys(body, {'model', 'state', 'questions', 'provider'})
    if body['model'] != MODEL or body['provider'] != {
        'only': ['typesafe'], 'allow_fallbacks': False,
        'max_price': {'prompt': '0.042', 'completion': '0'}}:
        raise safe.RunnerError('jev_model_provider_or_price_pin_invalid')
    safe._keys(body['state'], {'text'})
    if not isinstance(body['state']['text'], str) or not body['state']['text']:
        raise safe.RunnerError('jev_text_required')
    questions = body['questions']
    if not isinstance(questions, dict) or not 1 <= len(questions) <= 32:
        raise safe.RunnerError('jev_questions_invalid')
    for identifier, question in questions.items():
        if not isinstance(identifier, str) or not re.fullmatch(r'q[0-9]{2}', identifier):
            raise safe.RunnerError('jev_question_id_invalid')
        safe._keys(question, {'type', 'instructions', 'criteria'})
        safe._keys(question['criteria'], {'true', 'false'})
        if question['type'] != 'noul' or not all(isinstance(x, str) and x for x in
                [question['instructions'], *question['criteria'].values()]):
            raise safe.RunnerError('jev_noul_question_invalid')
    size = len(safe.canonical(body))
    if size > BODY_LIMIT or (size + 1024) * INPUT_CAP > RESERVE:
        raise safe.RunnerError('jev_body_exceeds_reservation_allowance')


def endpoint_identity(snapshot):
    data = snapshot.get('data', {}) if isinstance(snapshot, dict) else {}
    endpoints = data.get('endpoints', [])
    matches = [e for e in endpoints if e.get('tag') == 'typesafe' and e.get('status') == 0]
    if len(matches) != 1:
        raise safe.RunnerError('jev_endpoint_unavailable_or_ambiguous')
    endpoint = matches[0]
    pricing = endpoint.get('pricing', {})
    if safe._money(pricing.get('prompt')) > INPUT_CAP or safe._money(pricing.get('completion')) != 0:
        raise safe.RunnerError('jev_endpoint_price_exceeds_cap')
    for name, value in pricing.items():
        if name not in ('prompt', 'completion') and safe._money(value) != 0:
            raise safe.RunnerError('jev_unaccounted_endpoint_charge')
    aliases = {MODEL}
    for candidate in (endpoint.get('model_id'), endpoint.get('name', '').split(' | ')[-1]):
        if isinstance(candidate, str) and re.fullmatch(r'typesafe/jev-1\.13(?:-[0-9]{8})?', candidate):
            aliases.add(candidate)
    return sorted(aliases)


def prepare(request, inputs, output_dir, *, transport_fn=None):
    """Reads inputs only. Gold labels and split metadata never enter request bodies."""
    safe._keys(request, {'schema', 'enabled', 'experiment_id', 'model', 'budget_usd', 'batch_cap_usd', 'max_requests'})
    if (request['schema'] != 'loom.jev_pilot_request/1' or request['enabled'] is not True or
            request['model'] != MODEL or not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,63}', request['experiment_id'])):
        raise safe.RunnerError('jev_request_identity_invalid')
    if not 0 < safe._money(request['budget_usd']) <= 2 or not 0 < safe._money(request['batch_cap_usd']) <= Decimal('.10'):
        raise safe.RunnerError('jev_budget_invalid')
    if type(request['max_requests']) is not int or not 1 <= request['max_requests'] <= 64:
        raise safe.RunnerError('jev_request_limit_invalid')
    if not isinstance(inputs, list) or not 1 <= len(inputs) <= request['max_requests']:
        raise safe.RunnerError('jev_input_count_invalid')
    total = len(inputs) * RESERVE
    if total > min(safe._money(request['batch_cap_usd']), safe._money(request['budget_usd'])):
        raise safe.RunnerError('jev_batch_reservation_exceeds_budget')
    rows, ids = [], set()
    for case in inputs:
        safe._keys(case, {'case_id', 'language', 'state', 'questions'})
        identifier = case['case_id']
        if not isinstance(identifier, str) or not safe._ID.fullmatch(identifier) or identifier in ids or case['language'] not in ('pl', 'en'):
            raise safe.RunnerError('jev_case_identity_invalid')
        ids.add(identifier)
        body = {'model': MODEL, 'state': case['state'], 'questions': case['questions'],
                'provider': {'only': ['typesafe'], 'allow_fallbacks': False,
                             'max_price': {'prompt': '0.042', 'completion': '0'}}}
        validate_body(body)
        rows.append({'id': identifier, 'language': case['language'], 'body': body,
                     'request_hash': safe.digest(body), 'reservation_usd': str(RESERVE)})
    status, raw = (transport_fn or transport)('GET', ENDPOINT, None, None)
    if status != 200:
        raise safe.RunnerError('jev_catalog_http_error')
    snapshot = safe.parse_json(raw)
    aliases = endpoint_identity(snapshot)
    manifest = {'schema': 'loom.jev_manifest/1', 'experiment_id': request['experiment_id'],
                'created_at': safe._utc(), 'budget_usd': str(safe._money(request['budget_usd'])),
                'batch_cap_usd': str(safe._money(request['batch_cap_usd'])),
                'total_reservation_usd': str(total), 'requests': rows, 'model_aliases': aliases,
                'inputs_hash': safe.digest(inputs), 'endpoint_snapshot_hash': safe.digest(snapshot),
                'code_sha256': code_hashes(), 'billing_bound_guaranteed': False, 'no_graph_promotion': True}
    directory = Path(output_dir)
    directory.mkdir(parents=True, exist_ok=False)
    write_new(directory / 'inputs.json', inputs)
    write_new(directory / 'endpoint_snapshot.json', snapshot)
    write_new(directory / 'manifest.json', manifest)
    return manifest


def validate_manifest(manifest):
    if manifest.get('schema') != 'loom.jev_manifest/1' or manifest.get('code_sha256') != code_hashes():
        raise safe.RunnerError('jev_manifest_schema_or_code_mismatch')
    rows = manifest.get('requests')
    if not isinstance(rows, list) or not 1 <= len(rows) <= 64:
        raise safe.RunnerError('jev_manifest_count_invalid')
    if not 0 < safe._money(manifest['budget_usd']) <= 2 or not 0 < safe._money(manifest['batch_cap_usd']) <= Decimal('.10'):
        raise safe.RunnerError('jev_manifest_budget_invalid')
    if len(rows) * RESERVE != safe._money(manifest['total_reservation_usd']) or len(rows) * RESERVE > min(safe._money(manifest['batch_cap_usd']), safe._money(manifest['budget_usd'])):
        raise safe.RunnerError('jev_manifest_reservation_invalid')
    ids = set()
    for row in rows:
        validate_body(row['body'])
        if (not safe._ID.fullmatch(row['id']) or row['id'] in ids or row['request_hash'] != safe.digest(row['body']) or
                safe._money(row['reservation_usd']) != RESERVE):
            raise safe.RunnerError('jev_manifest_request_invalid')
        ids.add(row['id'])
    aliases = manifest.get('model_aliases')
    if not isinstance(aliases, list) or MODEL not in aliases or any(not re.fullmatch(r'typesafe/jev-1\.13(?:-[0-9]{8})?', x) for x in aliases):
        raise safe.RunnerError('jev_model_alias_invalid')


def key_check(send, key, budget, needed):
    status, raw = send('GET', '/api/v1/key', None, key)
    if status != 200:
        raise safe.RunnerError('jev_key_metadata_unavailable')
    data = safe.parse_json(raw).get('data', {})
    check = safe._key_gate(raw, budget, needed)
    if safe._money(data.get('byok_usage')) != 0:
        raise safe.RunnerError('jev_key_has_byok_usage')
    return {**check, 'byok_usage_usd': '0', 'billing_mode': 'credit_cap_with_usage_accounting_optional_audit'}


def parse_response(raw, request, aliases):
    value = safe.parse_json(raw)
    if not isinstance(value, dict):
        raise safe.RunnerError('jev_response_not_object')
    usage = value.get('usage')
    if not isinstance(usage, dict):
        raise safe.RunnerError('jev_usage_missing')
    if usage.get('is_byok') is True:
        raise safe.RunnerError('jev_response_byok_detected')
    cost = safe._money(usage.get('cost'))
    for name in ('input_tokens', 'output_tokens'):
        if type(usage.get(name)) is not int or usage[name] < 0:
            raise safe.RunnerError('jev_usage_tokens_invalid')
    result = {'reported_cost_usd': str(cost), 'input_tokens': usage['input_tokens'], 'output_tokens': usage['output_tokens']}
    if cost > RESERVE:
        raise safe.RunnerError('jev_cost_exceeds_reservation')
    if value.get('model') not in aliases or value.get('provider') != 'TypeSafe':
        raise safe.RunnerError('jev_response_identity_mismatch')
    identifier = value.get('id')
    if not isinstance(identifier, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,200}', identifier):
        raise safe.RunnerError('jev_generation_id_invalid')
    answers = value.get('answers')
    if not isinstance(answers, dict) or set(answers) != set(request['body']['questions']):
        raise safe.RunnerError('jev_answer_inventory_invalid')
    probabilities = {}
    for name, answer in answers.items():
        if (not isinstance(answer, dict) or answer.get('type') != 'noul' or
                type(answer.get('noul')) not in (int, float) or not 0 <= answer['noul'] <= 1):
            raise safe.RunnerError('jev_answer_probability_invalid')
        probabilities[name] = answer['noul']
    return {**result, 'generation_id': identifier, 'model': value['model'], 'provider': value['provider'], 'probabilities': probabilities}


def generation_check(send, key, parsed, aliases):
    """Optional read-only audit; the Decisions usage contract does not promise this lookup."""
    try:
        status, raw = send('GET', '/api/v1/generation?id=' + parsed['generation_id'], None, key)
        if status != 200:
            return {'audit_status': 'unavailable', 'http_status': status}
        data = safe.parse_json(raw).get('data', {})
    except Exception:
        return {'audit_status': 'unavailable'}
    if data.get('is_byok') is True:
        raise safe.RunnerError('jev_generation_byok_detected')
    if (data.get('id') != parsed['generation_id'] or data.get('model') not in aliases or
            data.get('provider_name') != 'TypeSafe' or data.get('api_type') != 'decisions'):
        return {'audit_status': 'identity_unverified'}
    try:
        cost = safe._money(data.get('total_cost'))
    except safe.RunnerError:
        return {'audit_status': 'cost_unavailable'}
    if cost > RESERVE:
        raise safe.RunnerError('jev_generation_cost_exceeds_reservation')
    return {'audit_status': 'verified' if data.get('is_byok') is False else 'byok_flag_unavailable',
            'id': data['id'], 'model': data['model'], 'provider_name': 'TypeSafe',
            'api_type': 'decisions', 'is_byok': False if data.get('is_byok') is False else None,
            'total_cost_usd': str(cost)}


def validate_ledger(ledger, manifest, directory):
    if ledger.get('schema') != 'loom.jev_ledger/1' or ledger.get('manifest_hash') != safe.digest(manifest):
        raise safe.RunnerError('jev_ledger_manifest_mismatch')
    attempts = ledger.get('attempts')
    if not isinstance(attempts, list) or len(attempts) > len(manifest['requests']):
        raise safe.RunnerError('jev_ledger_count_invalid')
    for index, row in enumerate(attempts):
        planned = manifest['requests'][index]
        if any(row.get(k) != planned[k] for k in ('id', 'request_hash', 'reservation_usd')):
            raise safe.RunnerError('jev_ledger_request_mismatch')
        if row.get('state') not in ('started', 'completed', 'rejected', 'uncertain'):
            raise safe.RunnerError('jev_ledger_state_invalid')
        if 'response_file' in row:
            if row['response_file'] != row['id'] + '.response.bin':
                raise safe.RunnerError('jev_response_path_invalid')
            raw = (directory / row['response_file']).read_bytes()
            if hashlib.sha256(raw).hexdigest() != row.get('response_sha256'):
                raise safe.RunnerError('jev_response_hash_mismatch')
    return attempts


def run(manifest, run_dir, *, transport_fn=None, key_loader=None):
    validate_manifest(manifest)
    directory = Path(run_dir)
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    send, loader = transport_fn or transport, key_loader or safe.load_key
    path = directory / 'ledger.json'
    with safe._lock(directory):
        if path.exists():
            ledger = read_json(path)
            attempts = validate_ledger(ledger, manifest, directory)
            for row in attempts:
                if row['state'] == 'started':
                    row.update(state='uncertain', reason='interrupted_no_retry')
                    ledger['stopped_reason'] = 'jev_interrupted_billing_unknown'
            safe._atomic(path, safe.canonical(ledger))
            if ledger.get('stopped_reason') or len(attempts) == len(manifest['requests']):
                return ledger
        else:
            if any(directory.glob('*.response.bin')):
                raise safe.RunnerError('jev_stranded_response_no_restart')
            ledger = {'schema': 'loom.jev_ledger/1', 'manifest_hash': safe.digest(manifest),
                      'experiment_id': manifest['experiment_id'], 'created_at': safe._utc(), 'attempts': [], 'no_graph_promotion': True}
            attempts = ledger['attempts']
            safe._atomic(path, safe.canonical(ledger))
        if not -300 <= time.time() - safe._timestamp(manifest['created_at']) <= 86400:
            raise safe.RunnerError('jev_prices_stale')
        key = loader()
        if not isinstance(key, str) or not key or key.encode() in safe.canonical(manifest):
            raise safe.RunnerError('jev_credential_invalid_or_in_manifest')
        ledger['key_before'] = key_check(send, key, safe._money(manifest['budget_usd']), (len(manifest['requests']) - len(attempts)) * RESERVE)
        safe._atomic(path, safe.canonical(ledger))
        for request in manifest['requests'][len(attempts):]:
            row = {k: request[k] for k in ('id', 'request_hash', 'reservation_usd')}
            row.update(state='started', started_at=safe._utc())
            attempts.append(row)
            safe._atomic(path, safe.canonical(ledger))
            try:
                started = time.monotonic()
                status, raw = send('POST', DECISIONS, safe.canonical(request['body']), key)
                if not isinstance(raw, bytes) or len(raw) > safe.MAX_RESPONSE_BYTES or key.encode() in raw:
                    raise safe.RunnerError('jev_response_unsafe_to_persist')
                row.update(response_file=row['id'] + '.response.bin', response_sha256=hashlib.sha256(raw).hexdigest(),
                           http_status=status, elapsed_seconds=round(time.monotonic() - started, 6))
                safe._atomic(directory / row['response_file'], raw)
                # Billing is independent of answer validity or HTTP status.
                # Keep known charges even if semantic parsing subsequently fails.
                row['cost_status'] = 'unknown'
                try:
                    envelope = safe.parse_json(raw)
                    usage = envelope.get('usage') if isinstance(envelope, dict) else None
                    cost = safe._money(usage.get('cost') if isinstance(usage, dict) else None)
                    row.update(reported_cost_usd=str(cost), cost_status='reported')
                except safe.RunnerError:
                    pass
                safe._atomic(path, safe.canonical(ledger))
                if 'reported_cost_usd' in row and safe._money(row['reported_cost_usd']) > RESERVE:
                    raise safe.RunnerError('jev_cost_exceeds_reservation')
                if status != 200:
                    raise safe.RunnerError('jev_http_error_no_retry')
                parsed = parse_response(raw, request, manifest['model_aliases'])
                row.update(parsed)
                safe._atomic(path, safe.canonical(ledger))
                row['generation_billing'] = generation_check(send, key, parsed, manifest['model_aliases'])
                row.update(state='completed', finished_at=safe._utc())
                safe._atomic(path, safe.canonical(ledger))
            except Exception as error:
                reason = str(error) if isinstance(error, safe.RunnerError) else 'jev_attempt_uncertain'
                row.update(state='rejected' if 'response_file' in row else 'uncertain', reason=reason, finished_at=safe._utc())
                ledger['stopped_reason'] = reason
                safe._atomic(path, safe.canonical(ledger))
                break
        try:
            ledger['key_after'] = key_check(send, key, safe._money(manifest['budget_usd']), Decimal(0))
        except Exception:
            ledger['post_key_check_failed'] = True
            ledger.setdefault('stopped_reason', 'jev_post_key_check_failed')
        ledger['reported_cost_usd'] = str(sum((safe._money(x['reported_cost_usd']) for x in attempts if 'reported_cost_usd' in x), Decimal(0)))
        ledger['cost_accounting_complete'] = all('reported_cost_usd' in x for x in attempts)
        ledger['attempts_with_unknown_cost'] = sum('reported_cost_usd' not in x for x in attempts)
        ledger['generation_verified_cost_usd'] = str(sum((safe._money(x['generation_billing']['total_cost_usd']) for x in attempts if x.get('generation_billing', {}).get('audit_status') == 'verified'), Decimal(0)))
        safe._atomic(path, safe.canonical(ledger))
        return ledger


def metrics(rows):
    available = [r for r in rows if r['probability'] is not None]
    high = [r for r in available if r['probability'] <= .2 or r['probability'] >= .8]
    tp = sum(r['probability'] >= .5 and r['label'] == 1 for r in available)
    fp = sum(r['probability'] >= .5 and r['label'] == 0 for r in available)
    fn = sum(r['probability'] < .5 and r['label'] == 1 for r in available)
    correct = sum((r['probability'] >= .5) == r['label'] for r in available)
    return {'planned': len(rows), 'available': len(available), 'missing': len(rows)-len(available),
            'coverage': len(available)/len(rows) if rows else None,
            'accuracy_available': correct/len(available) if available else None,
            'accuracy_planned': correct/len(rows) if rows else None,
            'brier_available': sum((r['probability']-r['label'])**2 for r in available)/len(available) if available else None,
            'precision_available': tp/(tp+fp) if tp+fp else None,
            'recall_available': tp/(tp+fn) if tp+fn else None,
            'f1_available': 2*tp/(2*tp+fp+fn) if 2*tp+fp+fn else None,
            'selective_coverage_planned': len(high)/len(rows) if rows else None,
            'selective_error': sum((r['probability'] >= .5) != r['label'] for r in high)/len(high) if high else None}


def score(manifest, run_dir, gold):
    validate_manifest(manifest)
    directory = Path(run_dir)
    ledger = read_json(directory / 'ledger.json')
    attempts = validate_ledger(ledger, manifest, directory)
    by_id = {r['id']: r for r in attempts}
    if not isinstance(gold, list) or len(gold) != len(manifest['requests']) or {g['case_id'] for g in gold} != {r['id'] for r in manifest['requests']}:
        raise safe.RunnerError('jev_gold_inventory_invalid')
    targets = {g['case_id']: g for g in gold}
    rows, parsed_by_id = [], {}
    for request in manifest['requests']:
        target = targets[request['id']]
        if set(target['labels']) != set(request['body']['questions']) or any(type(v) is not int or v not in (0,1) for v in target['labels'].values()):
            raise safe.RunnerError('jev_gold_labels_invalid')
        attempt = by_id.get(request['id'], {})
        probabilities = {}
        if attempt.get('state') == 'completed':
            raw = (directory / attempt['response_file']).read_bytes()
            parsed = parse_response(raw, request, manifest['model_aliases'])
            probabilities = parsed['probabilities']
        parsed_by_id[request['id']] = probabilities
        for question, label in target['labels'].items():
            rows.append({'case_id': request['id'], 'question': question, 'language': request['language'],
                         'split': target['split'], 'variant': target['variant'], 'family_id': target['family_id'],
                         'label': label, 'probability': probabilities.get(question),
                         'attempt_state': attempt.get('state', 'not_attempted')})
    grouped = {}
    for field in ('language', 'split', 'variant', 'question', 'family_id'):
        groups = defaultdict(list)
        for row in rows:
            groups[row[field]].append(row)
        grouped[field] = {name: metrics(items) for name, items in groups.items()}
    pairs = defaultdict(list)
    for target in gold:
        if target['variant'] == 'base':
            continue
        before = parsed_by_id.get(target['base_case_id'], {})
        after = parsed_by_id.get(target['case_id'], {})
        for question, label in target['labels'].items():
            a, b = before.get(question), after.get(question)
            pairs[target['variant']].append({'available': a is not None and b is not None,
                'delta': abs(a-b) if a is not None and b is not None else None,
                'agreement': (a >= .5) == (b >= .5) if a is not None and b is not None else None,
                'expected_change': question in target.get('expected_changed_questions', [])})
    comparisons = {}
    for variant, items in pairs.items():
        available = [x for x in items if x['available']]
        changed = [x for x in available if x['expected_change']]
        comparisons[variant] = {'planned_pairs': len(items), 'available_pairs': len(available),
            'mean_absolute_probability_delta': sum(x['delta'] for x in available)/len(available) if available else None,
            'hard_decision_agreement': sum(x['agreement'] for x in available)/len(available) if available else None,
            'foil_changed_questions_available': len(changed),
            'foil_changed_decision_fraction': sum(not x['agreement'] for x in changed)/len(changed) if changed else None}
    return {'schema': 'loom.jev_score/1', 'experiment_id': manifest['experiment_id'],
            'manifest_hash': safe.digest(manifest), 'gold_hash': safe.digest(gold),
            'threshold': .5, 'selective_thresholds': [.2,.8], 'overall': metrics(rows),
            'groups': grouped, 'comparisons': comparisons, 'rows': rows,
            'reported_cost_usd': ledger.get('reported_cost_usd'),
            'generation_verified_cost_usd': ledger.get('generation_verified_cost_usd'),
            'no_graph_promotion': True, 'synthetic_cases_not_production_accuracy': True}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    prep = commands.add_parser('prepare')
    prep.add_argument('--request', required=True, type=Path)
    prep.add_argument('--inputs', required=True, type=Path)
    prep.add_argument('--output-dir', required=True, type=Path)
    for name in ('run', 'score'):
        sub = commands.add_parser(name)
        sub.add_argument('--manifest', required=True, type=Path)
        sub.add_argument('--run-dir', required=True, type=Path)
        if name == 'score':
            sub.add_argument('--gold', required=True, type=Path)
            sub.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == 'prepare':
            result = prepare(read_json(args.request), read_json(args.inputs), args.output_dir)
            print(json.dumps({'requests': len(result['requests']), 'reservation_usd': result['total_reservation_usd']}))
        elif args.command == 'run':
            result = run(read_json(args.manifest), args.run_dir)
            print(json.dumps({'attempts': len(result['attempts']), 'stopped_reason': result.get('stopped_reason'),
                              'reported_cost_usd': result.get('reported_cost_usd')}))
            return 2 if result.get('stopped_reason') else 0
        else:
            result = score(read_json(args.manifest), args.run_dir, read_json(args.gold))
            write_new(args.output, result)
            print(json.dumps(result['overall']))
        return 0
    except safe.RunnerError as error:
        print(json.dumps({'error': str(error)}))
        return 2
    except (ValueError, KeyError, TypeError, OSError):
        print('{"error":"jev_local_input_or_storage_failure"}')
        return 2

if __name__ == '__main__':
    raise SystemExit(main())
