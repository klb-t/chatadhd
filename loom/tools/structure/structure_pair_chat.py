#!/usr/bin/env python3
"""Frozen same-task pair classification using the unchanged bounded runner."""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
from decimal import Decimal
import hashlib
from pathlib import Path

try:
    from . import openrouter_runner as safe
except ImportError:
    import openrouter_runner as safe

MODEL = 'openai/gpt-4.1-mini'
PROVIDER = 'openai'
EXPERIMENT = 'structure-pair-chat-v1'
SYSTEM = ('Answer the supplied q01 using its instructions and true/false criteria applied to state.text. '
          'Return only one JSON object with exactly one key, q01, whose value is a JSON boolean: '
          '{"q01":true} or {"q01":false}. Do not include explanation, confidence, or Markdown.')
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
            for name in ('structure_pair_chat.py', 'openrouter_runner.py')}


def body_for(case):
    safe._keys(case, {'case_id', 'language', 'state', 'questions'})
    safe._keys(case['state'], {'text'})
    safe._keys(case['questions'], {'q01'})
    question = case['questions']['q01']
    safe._keys(question, {'instructions', 'criteria', 'type'})
    safe._keys(question['criteria'], {'true', 'false'})
    if (question['type'] != 'noul' or case['language'] not in ('pl', 'en') or
            not all(isinstance(x, str) and x for x in (case['state']['text'],
                    question['instructions'], *question['criteria'].values()))):
        raise safe.RunnerError('invalid_pair_input')
    # Strings and criteria are copied exactly; no category, label or split metadata.
    user = {'state': case['state'], 'questions': case['questions']}
    return {'model': MODEL, 'messages': [{'role': 'system', 'content': SYSTEM},
                {'role': 'user', 'content': safe.canonical(user).decode('utf-8')}],
            'stream': False, 'temperature': 0, 'max_tokens': 128,
            'response_format': {'type': 'json_object'}, 'usage': {'include': True},
            'provider': {'only': [PROVIDER], 'allow_fallbacks': False,
                         'require_parameters': True,
                         'max_price': {'prompt': '0.4', 'completion': '1.6'}}}


def prepare(inputs_path, snapshot_path, output_dir, retrieved_at):
    """Offline: only inputs and a public endpoint snapshot; never loads gold."""
    inputs, snapshot = read_json(inputs_path), read_json(snapshot_path)
    if not isinstance(inputs, list) or len(inputs) != 48:
        raise safe.RunnerError('expected_frozen_48_inputs')
    matches = [e for e in snapshot.get('data', {}).get('endpoints', [])
               if e.get('tag') == PROVIDER and e.get('status') == 0]
    if len(matches) != 1:
        raise safe.RunnerError('endpoint_unavailable_or_ambiguous')
    endpoint = matches[0]
    if not {'max_tokens', 'temperature', 'response_format'} <= set(endpoint.get('supported_parameters', [])):
        raise safe.RunnerError('endpoint_required_parameters_absent')
    rows = []
    for case in inputs:
        body = body_for(case)
        rows.append({'id': case['case_id'], 'body': body,
                     'reservation_usd': safe.estimate_reservation(body)['minimum_reservation_usd'],
                     'metadata': {'language': case['language']}})
    manifest = {'schema': safe.MANIFEST_SCHEMA, 'experiment_id': EXPERIMENT,
                'budget_usd': '2', 'max_requests': 48, 'requests': rows,
                'pricing_evidence': [{'model': MODEL, 'provider': PROVIDER,
                    'pricing': endpoint['pricing'], 'retrieved_at': retrieved_at,
                    'source_url': safe.API_ROOT + '/models/' + MODEL + '/endpoints'}],
                'metadata': {'created_at': safe._utc(), 'batch_cap_usd': '0.10',
                    'inputs_hash': safe.digest(inputs),
                    'inputs_file_sha256': hashlib.sha256(Path(inputs_path).read_bytes()).hexdigest(),
                    'system_prompt_sha256': hashlib.sha256(SYSTEM.encode()).hexdigest(),
                    'endpoint_snapshot_hash': safe.digest(snapshot), 'code_sha256': code_hashes(),
                    'gold_read_during_preparation': False, 'no_graph_promotion': True}}
    plan = safe.plan_manifest(manifest)
    if safe._money(plan['total_reservation_usd']) >= Decimal('.10'):
        raise safe.RunnerError('batch_reservation_exceeds_cap')
    directory = Path(output_dir)
    directory.mkdir(parents=True, exist_ok=False)
    write_new(directory / 'inputs.json', inputs)
    write_new(directory / 'endpoint_snapshot.json', snapshot)
    write_new(directory / 'manifest.json', manifest)
    write_new(directory / 'plan.json', plan)
    with (directory / 'system_prompt.txt').open('x') as handle:
        handle.write(SYSTEM)
    return plan


def validate_manifest(manifest):
    plan = safe.plan_manifest(manifest)
    metadata = manifest.get('metadata', {})
    if (manifest['experiment_id'] != EXPERIMENT or manifest['budget_usd'] != '2' or
            safe._money(plan['total_reservation_usd']) >= Decimal('.10') or
            metadata.get('code_sha256') != code_hashes() or
            metadata.get('system_prompt_sha256') != hashlib.sha256(SYSTEM.encode()).hexdigest()):
        raise safe.RunnerError('pair_manifest_protocol_mismatch')
    for row in manifest['requests']:
        try:
            user = safe.parse_json(row['body']['messages'][1]['content'])
            expected = body_for({'case_id': row['id'], 'language': row['metadata']['language'], **user})
        except (IndexError, KeyError, TypeError):
            raise safe.RunnerError('pair_body_protocol_mismatch') from None
        if expected != row['body']:
            raise safe.RunnerError('pair_body_protocol_mismatch')
    return plan


def parse_boolean(raw):
    status = safe._response_result(raw)
    if status['state'] != 'completed':
        raise safe.RunnerError(status['reason'])
    response = safe.parse_json(raw)
    if response.get('model') != MODEL or response.get('provider') not in ('OpenAI', PROVIDER):
        raise safe.RunnerError('response_model_provider_mismatch')
    content = safe.parse_json(response['choices'][0]['message']['content'])
    if not isinstance(content, dict) or set(content) != {'q01'} or type(content['q01']) is not bool:
        raise safe.RunnerError('q01_boolean_object_required')
    return content['q01']


def metrics(rows):
    available = [r for r in rows if r['prediction'] is not None]
    tp = sum(r['prediction'] is True and r['label'] == 1 for r in available)
    tn = sum(r['prediction'] is False and r['label'] == 0 for r in available)
    fp = sum(r['prediction'] is True and r['label'] == 0 for r in available)
    fn = sum(r['prediction'] is False and r['label'] == 1 for r in available)
    return {'planned': len(rows), 'available': len(available), 'missing': len(rows)-len(available),
            'coverage': len(available)/len(rows) if rows else None,
            'tp': tp, 'tn': tn, 'fp': fp, 'fn': fn,
            'accuracy_available': (tp+tn)/len(available) if available else None,
            'accuracy_planned': (tp+tn)/len(rows) if rows else None,
            'precision_available': tp/(tp+fp) if tp+fp else None,
            'recall_available': tp/(tp+fn) if tp+fn else None,
            'f1_available': 2*tp/(2*tp+fp+fn) if 2*tp+fp+fn else None}


def score(manifest, run_dir, gold):
    plan = validate_manifest(manifest)
    directory = Path(run_dir)
    ledger = read_json(directory / 'ledger.json')
    attempts = safe._validate_ledger(ledger, plan, directory)
    if (not isinstance(gold, list) or len(gold) != len(manifest['requests']) or
            {g['case_id'] for g in gold} != {r['id'] for r in manifest['requests']}):
        raise safe.RunnerError('gold_inventory_invalid')
    targets, by_id = {g['case_id']: g for g in gold}, {r['id']: r for r in attempts}
    rows = []
    for request in manifest['requests']:
        target = targets[request['id']]
        if (not isinstance(target.get('labels'), dict) or set(target['labels']) != {'q01'} or
                type(target['labels']['q01']) is not int or target['labels']['q01'] not in (0,1)):
            raise safe.RunnerError('gold_label_invalid')
        attempt = by_id.get(request['id'], {})
        prediction, error = None, attempt.get('reason', 'not_attempted')
        if attempt.get('state') == 'completed' and attempt.get('http_status') == 200:
            try:
                prediction = parse_boolean((directory / attempt['response_file']).read_bytes())
                error = None
            except safe.RunnerError as exc:
                error = str(exc)
        rows.append({'case_id': request['id'], 'language': request['metadata']['language'],
                     'label': target['labels']['q01'], 'prediction': prediction,
                     'attempt_state': attempt.get('state', 'not_attempted'), 'error': error,
                     **{f: target.get(f) for f in ('split', 'variant', 'family_id')}})
    groups = {}
    for field in ('language', 'split', 'variant', 'family_id'):
        grouped = defaultdict(list)
        for row in rows:
            grouped[str(row[field])].append(row)
        groups[field] = {name: metrics(items) for name, items in grouped.items()}
    costs = [safe._money(r['reported_cost_usd']) for r in attempts if 'reported_cost_usd' in r]
    latencies = [r['elapsed_seconds'] for r in attempts if 'elapsed_seconds' in r]
    return {'schema': 'loom.structure_pair_chat_score/1', 'experiment_id': EXPERIMENT,
            'manifest_hash': safe.digest(manifest), 'gold_hash': safe.digest(gold),
            'overall': metrics(rows), 'groups': groups, 'rows': rows,
            'errors': dict(Counter(r['error'] for r in rows if r['error'] is not None)),
            'attempt_state_counts': dict(Counter(r['attempt_state'] for r in rows)),
            'reported_cost_usd': str(sum(costs, Decimal(0))),
            'attempts_without_reported_cost': len(attempts)-len(costs),
            'sum_response_elapsed_seconds': sum(latencies), 'response_latencies_seconds': latencies,
            'stopped_reason': ledger.get('stopped_reason'), 'no_graph_promotion': True,
            'synthetic_cases_not_production_accuracy': True,
            'comparison_scope': 'same classification question; different output/API/model, no graph serialization'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    prep = sub.add_parser('prepare')
    for name in ('inputs', 'snapshot', 'output-dir'):
        prep.add_argument('--'+name, required=True, type=Path)
    prep.add_argument('--retrieved-at', required=True)
    for command in ('run', 'score'):
        part = sub.add_parser(command)
        part.add_argument('--manifest', required=True, type=Path)
        part.add_argument('--run-dir', required=True, type=Path)
        if command == 'score':
            part.add_argument('--gold', required=True, type=Path)
            part.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == 'prepare':
            result = prepare(args.inputs, args.snapshot, args.output_dir, args.retrieved_at)
        else:
            manifest = read_json(args.manifest)
            validate_manifest(manifest)
            if args.command == 'run':
                result = safe.run_manifest(manifest, args.run_dir)
            else:
                result = score(manifest, args.run_dir, read_json(args.gold))
                write_new(args.output, result)
        print(safe.canonical(result).decode())
        return 0
    except safe.RunnerError as exc:
        print(safe.canonical({'error': str(exc)}).decode())
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
