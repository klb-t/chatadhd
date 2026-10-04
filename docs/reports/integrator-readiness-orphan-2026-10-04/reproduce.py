"""Synthetic offline-only arm_audit reproduction; never invokes a live runner."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import types
from unittest.mock import patch

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--repository', type=Path, required=True)
ap.add_argument('--output', type=Path, required=True)
args = ap.parse_args()
REPO = args.repository.resolve()
OUTPUT = args.output.resolve()
OUTPUT.mkdir(parents=True, exist_ok=True)
COMMIT = 'f8bf51bc8a317d3e3c73494edcd3860e9427e01d'
SOURCE_PATH = 'loom/tools/structure/model_study_readiness_v1.py'
SOURCE = subprocess.check_output(['git', 'show', COMMIT + ':' + SOURCE_PATH], cwd=REPO)
source_file = OUTPUT / 'pinned_model_study_readiness_v1.py'
source_file.write_bytes(SOURCE)
sys.path.insert(0, str(REPO))
module = types.ModuleType('loom.tools.structure.synthetic_readiness_repro')
module.__package__ = 'loom.tools.structure'
module.__file__ = str(source_file)
exec(compile(SOURCE, str(source_file), 'exec'), module.__dict__)
safe = module.safe

def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(safe.canonical(value) + b'\n')

def forbidden(*args, **kwargs):
    raise AssertionError('Reproduction must never load credentials or invoke transport')

model = 'synthetic/offline-model'
provider = {'only': ['synthetic-provider'], 'allow_fallbacks': False,
            'require_parameters': True,
            'max_price': {'prompt': '0.4', 'completion': '1.6'}}
manifest = {'schema': safe.MANIFEST_SCHEMA,
            'experiment_id': 'synthetic-orphan-review-20261004',
            'budget_usd': '1', 'max_requests': 2,
            'requests': [
                {'id': identifier, 'reservation_usd': '0.001',
                 'body': {'model': model, 'messages': [
                     {'role': 'user', 'content': 'Synthetic offline fixture ' + identifier}],
                     'max_tokens': 16, 'provider': provider, 'stream': False,
                     'usage': {'include': True}}}
                for identifier in ('request01', 'request02')],
            'pricing_evidence': [
                {'model': model, 'provider': 'synthetic-provider',
                 'pricing': {'prompt': '0.0000004', 'completion': '0.0000016'},
                 'source_url': safe.API_ROOT + '/models/' + model + '/endpoints',
                 'retrieved_at': '2026-10-04T00:00:00Z'}],
            'metadata': {'evidence_class': 'scripted_mechanism',
                         'prices_are_fabricated_fixture_data': True}}
arm = {'arm': 'synthetic_arm', 'kind': 'llm'}
prepared = OUTPUT / 'prepared'
write(prepared / arm['arm'] / 'manifest.json', manifest)

def response(identifier):
    return {'id': 'synthetic-generation-' + identifier, 'model': model,
            'choices': [{'finish_reason': 'stop', 'message': {
                'content': json.dumps({'query_id': identifier, 'label': 'unknown'})}}],
            'usage': {'prompt_tokens': 10, 'completion_tokens': 4, 'cost': '0.0000042'}}

results = {}
with patch.object(module.jev, 'transport', forbidden), \
     patch.object(safe, 'transport', forbidden), \
     patch.object(safe, 'load_key', forbidden):
    plan = safe.plan_manifest(manifest)
    first = plan['requests'][0]
    first_raw = safe.canonical(response('request01'))
    ledger = {'schema': 'loom.openrouter_ledger/1', 'manifest_hash': plan['manifest_hash'],
              'attempts': [{key: first[key] for key in ('id', 'request_hash', 'reservation_usd')}]}
    ledger['attempts'][0].update(
        state='completed', started_at='2026-10-04T00:00:01Z',
        finished_at='2026-10-04T00:00:02Z',
        response_file='request01.response.bin',
        response_sha256=hashlib.sha256(first_raw).hexdigest(),
        http_status=200, reported_cost_usd='0.0000042', elapsed_seconds=1)
    for name, has_ledger, has_orphan in (
        ('valid_ledger_plus_orphan', True, True),
        ('no_ledger_plus_orphan', False, True),
        ('valid_ledger_without_orphan', True, False)):
        runs = OUTPUT / name
        directory = runs / arm['arm']
        directory.mkdir(parents=True, exist_ok=True)
        if has_ledger:
            write(directory / 'ledger.json', ledger)
            (directory / 'request01.response.bin').write_bytes(first_raw)
        if has_orphan:
            (directory / 'request02.response.bin').write_bytes(safe.canonical(response('request02')))
        actual = module.arm_audit(
            prepared, runs, arm,
            now=safe._timestamp('2026-10-04T00:01:00Z'),
            max_age_seconds=86400, future_tolerance_seconds=300)
        write(runs / 'actual_arm_audit.json', actual)
        results[name] = {
            'ledger_present': has_ledger,
            'orphan_response_file': 'request02.response.bin' if has_orphan else None,
            'expected_orphan_block': has_orphan,
            'actual_orphan_block': 'stranded_responses_without_ledger' in actual['blockers'],
            'actual_blockers': actual['blockers'],
            'actual_unattempted_ids': actual['unattempted_ids'],
            'actual_attempts': actual['attempts'],
            'snapshot_fresh': actual['snapshot']['fresh'],
            'matches_expected': bool(actual['blockers']) == has_orphan}

dependencies = {
    str(Path(m.__file__).relative_to(REPO)): hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest()
    for m in (module.study, module.safe, module.jev, module.w3)}
result = {
    'schema': 'loom.integrator.arm_audit_orphan_reproduction/1',
    'source_commit': COMMIT, 'source_path': SOURCE_PATH,
    'source_sha256': hashlib.sha256(SOURCE).hexdigest(),
    'dependency_checkout_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
    'dependencies_sha256': dependencies,
    'test_kind': 'offline_scripted_mechanism',
    'new_provider_calls': 0, 'credential_reads': 0,
    'live_runners_invoked': False,
    'exact_manifest': manifest, 'exact_plan': plan, 'exact_ledger': ledger,
    'exact_first_response': response('request01'),
    'exact_orphan_response': response('request02'),
    'invocation': {'function': 'arm_audit', 'arm': arm,
                   'now': safe._timestamp('2026-10-04T00:01:00Z'),
                   'max_age_seconds': 86400, 'future_tolerance_seconds': 300},
    'results': results,
    'interpretation': 'Existing valid ledger hides an unreferenced response: request02 is reported unattempted without any arm blocker. This reproduces only arm_audit; no live execution or campaign audit occurred.'}
write(OUTPUT / 'REPRODUCTION.json', result)
print(json.dumps({'source_commit': COMMIT, 'source_sha256': result['source_sha256'],
                  'results': results}, indent=2))
