"""Exercise frozen research adapters end-to-end with explicitly scripted replies.

Every POST/GET is intercepted here; no real credential is loaded. The fake
accounting values are parser fixtures, never real usage or model quality.
"""
from copy import deepcopy
from pathlib import Path
import argparse
import hashlib
from decimal import Decimal

from loom.tools.structure import source_view_roles_v1 as roles
from loom.tools.structure import graph_free_schema_hint_v2 as hint
from loom.tools.structure.test_source_view_experiment import toy
from loom.tools.structure.test_graph_free_extraction import case, output, gold

panel, replay, safe, jev = roles.panel, roles.replay, roles.safe, roles.jev
SENTINEL = 'offline-scripted-transport-only'
KIND = 'scripted_not_model'


def key_metadata():
    return safe.canonical({'data': {'limit': 2, 'limit_remaining': 2,
        'limit_reset': None, 'include_byok_in_limit': False, 'byok_usage': 0,
        'is_management_key': False, 'is_provisioning_key': False}})


def roles_mechanism(folder):
    folder = Path(folder); folder.mkdir(parents=True, exist_ok=False)
    panel.write_new(folder / 'SCRIPTED_EXECUTION_KIND.json', {
        'execution_kind': KIND, 'real_api_calls': 0, 'actual_reported_cost_usd': '0',
        'fake_usage_excluded_from_actual_spend_and_model_profiles': True})
    # Four independent toy IDs exercise all 3 labels and conflicting no-forcing.
    cases = []
    for index in range(4):
        c = toy(); c['id'] = f'scripted_roles_{index}'
        c['source_id'] = f'synthetic:scripted_roles_{index}'
        c['judgment_queries'][0]['id'] = f'scripted_roles_{index}_q1'
        cases.append(c)
    inputs = roles.specs(cases)
    panel.write_new(folder / 'scripted_inputs.json', inputs)
    actual_public_snapshot = replay.read(roles.ROOT /
        'docs/research/source_view_experiment_2026-09-30/active_refute_v2/prepared/endpoint_snapshot.json')
    calls = []
    probabilities = [(.8, .1), (.1, .8), (.1, .1), (.8, .8)]
    post_count = 0
    def transport(method, path, body, key):
        nonlocal post_count
        calls.append({'method': method, 'path': path,
            'request_body_sha256': hashlib.sha256(body).hexdigest() if body else None})
        if method == 'GET' and path == jev.ENDPOINT:
            if key is not None: raise AssertionError('public snapshot GET cannot take sentinel')
            return 200, safe.canonical(actual_public_snapshot)
        if key != SENTINEL: raise AssertionError('real key access forbidden in scripted workflow')
        if method == 'GET' and path == '/api/v1/key': return 200, key_metadata()
        if method == 'GET' and path.startswith('/api/v1/generation?'):
            return 404, safe.canonical({'error': 'scripted generation audit unavailable'})
        if method != 'POST' or path != jev.DECISIONS:
            raise AssertionError('unregistered scripted transport call')
        parsed = safe.parse_json(body)
        ident = safe.parse_json(parsed['state']['text'])['source_record']['query']['id']
        expected = inputs[post_count]
        if ident != expected['case_id'] or parsed['questions'] != expected['questions']:
            raise AssertionError('scripted request inventory drift')
        q01, q02 = probabilities[post_count]; post_count += 1
        return 200, safe.canonical({'id': f'scripted_roles_generation_{post_count}',
            'model': jev.MODEL, 'provider': 'TypeSafe',
            'usage': {'cost': .000001, 'input_tokens': 100, 'output_tokens': 0, 'is_byok': False},
            'answers': {'q01': {'type': 'noul', 'noul': q01},
                        'q02': {'type': 'noul', 'noul': q02}}})
    request = {'schema': 'loom.jev_pilot_request/1', 'enabled': True,
        'experiment_id': roles.EXPERIMENT_ID, 'model': jev.MODEL,
        'budget_usd': '2', 'batch_cap_usd': '.10', 'max_requests': 48}
    manifest = jev.prepare(request, inputs, folder / 'prepared', transport_fn=transport)
    roles.validate_prepared(folder / 'prepared/manifest.json', cases)
    ledger = jev.run(manifest, folder / 'run', transport_fn=transport, key_loader=lambda: SENTINEL)
    predictions, summary = roles.replay_outputs(manifest, folder / 'run')
    panel.write_new(folder / 'scripted_compiled_first.json', predictions)
    labels = ['supported', 'refuted', 'unknown', 'unknown']
    # These are intentionally scripted expected outcomes, not fixture/model gold.
    targets = [{'id': c['id'], 'judgments': [{'query_id': c['judgment_queries'][0]['id'], 'label': label}]}
               for c, label in zip(cases, labels)]
    scored = panel.score_judgments(targets, predictions)
    panel.write_new(folder / 'scripted_contract_score.json', {'execution_kind': KIND,
        'model_quality_measured': False, 'scripted_contract': scored})
    panel.write_new(folder / 'SCRIPTED_EXECUTION_RECEIPT.json', {
        'execution_kind': KIND, 'real_api_calls': 0, 'actual_reported_cost_usd': '0',
        'model_quality_measured': False, 'sentinel_is_not_a_real_key': True,
        'public_snapshot_source': 'previously saved actual public endpoint snapshot',
        'scripted_post_calls': post_count, 'local_transport_calls': calls,
        'fake_usage_cost_usd': ledger['reported_cost_usd'],
        'fake_usage_excluded_from_actual_spend_and_model_profiles': True,
        'observed_compiled_labels': [p['label'] for p in predictions],
        'conflicting_response_kept_unavailable': scored['unavailable'] == 1,
        'source_record_and_question_hashes': [{'id': r['case_id'],
            'state_sha256': safe.digest(r['state']), 'questions_sha256': safe.digest(r['questions'])}
            for r in inputs], 'wrapper_execution_summary_fixture_only': summary})
    return {'scripted_cases': len(cases), 'post_count': post_count,
        'labels': [p['label'] for p in predictions], 'unavailable': scored['unavailable'],
        'real_api_calls': 0, 'model_quality_measured': False}


def schema_hint_mechanism(folder):
    folder = Path(folder); folder.mkdir(parents=True, exist_ok=False)
    panel.write_new(folder / 'SCRIPTED_EXECUTION_KIND.json', {
        'execution_kind': KIND, 'real_api_calls': 0, 'actual_reported_cost_usd': '0',
        'fake_usage_excluded_from_actual_spend_and_model_profiles': True})
    cases = []
    for index in range(3):
        c = case(); c['id'] = f'scripted_hint_{index}'; c['source_id'] = f'synthetic:scripted_hint_{index}'
        cases.append(c)
    rows = hint.requests(cases)
    pricing = replay.read(roles.ROOT /
        'docs/research/graph_method_panel_v1/extraction/prepared/manifest.json')['pricing_evidence']
    manifest = {'schema': safe.MANIFEST_SCHEMA, 'experiment_id': 'graph-dev-free-schema-hint-v2-01',
        'budget_usd': '2', 'max_requests': 3, 'requests': rows,
        'pricing_evidence': pricing, 'metadata': hint.manifest_metadata()}
    prepared = folder / 'prepared'; prepared.mkdir()
    panel.write_new(prepared / 'manifest.json', manifest)
    valid = output(); bad_evidence = deepcopy(valid)
    bad_evidence['source_assertions'][0]['evidence'] = ['t1']
    duplicate_keys = '{"nodes":[],"source_assertions":[],"source_assertions":[],"status_events":[]}'
    contents = [safe.canonical(valid).decode(), duplicate_keys, safe.canonical(bad_evidence).decode()]
    calls = []; post_count = 0
    def transport(method, path, body, key):
        nonlocal post_count
        calls.append({'method': method, 'path': path,
            'request_body_sha256': hashlib.sha256(body).hexdigest() if body else None})
        if key != SENTINEL: raise AssertionError('real key access forbidden in scripted workflow')
        if method == 'GET' and path == '/key': return 200, key_metadata()
        if method != 'POST' or path != '/chat/completions':
            raise AssertionError('unregistered scripted transport call')
        parsed = safe.parse_json(body)
        if safe.parse_json(parsed['messages'][1]['content'])['id'] != cases[post_count]['id']:
            raise AssertionError('scripted hint request inventory drift')
        content = contents[post_count]; post_count += 1
        return 200, safe.canonical({'model': panel.MODEL, 'provider': 'OpenAI',
            'usage': {'cost': .0001, 'is_byok': False},
            'choices': [{'finish_reason': 'stop', 'message': {'content': content}}]})
    ledger = safe.run_manifest(manifest, folder / 'run', transport_fn=transport, key_loader=lambda: SENTINEL)
    outputs, summary = hint.load_run(prepared / 'manifest.json', folder / 'run', cases)
    panel.write_new(folder / 'scripted_compiled_first.json', outputs)
    golds = []
    for c in cases:
        g = gold(); g['id'] = c['id']
        for edge in g['source_assertions']:
            for evidence in edge['evidence']: evidence['source_id'] = c['source_id']
        golds.append(g)
    scored = hint.score_free(cases, golds, outputs)
    panel.write_new(folder / 'scripted_contract_score.json', {'execution_kind': KIND,
        'model_quality_measured': False, 'scripted_contract': scored})
    panel.write_new(folder / 'SCRIPTED_EXECUTION_RECEIPT.json', {
        'execution_kind': KIND, 'real_api_calls': 0, 'actual_reported_cost_usd': '0',
        'model_quality_measured': False, 'sentinel_is_not_a_real_key': True,
        'scripted_post_calls': post_count, 'local_transport_calls': calls,
        'fake_usage_cost_usd': str(sum((safe._money(a['reported_cost_usd'])
            for a in ledger['attempts'] if 'reported_cost_usd' in a), Decimal(0))),
        'fake_usage_excluded_from_actual_spend_and_model_profiles': True,
        'response_design': ['well-formed grounded assertion', 'duplicate inner JSON key',
            'string evidence instead of turn_id object'],
        'compile_states': [r['state'] for r in outputs],
        'invalid_assertions': [r.get('invalid_assertions') for r in outputs],
        'wrapper_execution_summary_fixture_only': summary})
    return {'scripted_cases': len(cases), 'post_count': post_count,
        'compile_states': [r['state'] for r in outputs], 'real_api_calls': 0,
        'model_quality_measured': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(); args.output.mkdir(parents=True, exist_ok=False)
    result = {'execution_kind': KIND,
        'roles': roles_mechanism(args.output / 'roles'),
        'schema_hint': schema_hint_mechanism(args.output / 'schema_hint')}
    panel.write_new(args.output / 'SUMMARY.json', result)
    print(safe.canonical(result).decode())


if __name__ == '__main__':
    main()
