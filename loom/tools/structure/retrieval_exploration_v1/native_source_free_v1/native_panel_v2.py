"""Frozen source-only wrappers and actual native pipeline; no model/key access."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import time
import os
import signal
import sys
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
FIXTURE = ROOT / 'loom/tests/fixtures/research/graph_methods_panel_v1'
DOCS = ROOT / 'docs/research/retrieval_exploration_v1'
NATIVE_ENV = ROOT / 'loom/tools/structure/graph_composed_validation_v1/native_baseline/environment_before_build.json'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_new(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as out:
        out.write(json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + '\n')


def source_only(case):
    return {k: case[k] for k in ('id', 'language', 'source_id', 'turns')}


def to_openai(case, arm, policy):
    if arm not in policy['arms']:
        raise ValueError('undeclared_native_arm')
    if not case['turns'] or len({t['id'] for t in case['turns']}) != len(case['turns']):
        raise ValueError('empty_or_duplicate_turn_inventory')
    mapping = {}
    for i, t in enumerate(case['turns']):
        epoch = datetime.fromisoformat(t['known_at'].replace('Z', '+00:00')).timestamp()
        mapping[t['id']] = {'id': t['id'], 'parent': case['turns'][i - 1]['id'] if i else None,
            'children': [case['turns'][i + 1]['id']] if i + 1 < len(case['turns']) else [],
            'message': {'id': t['id'], 'author': {'role': policy['transport_user_role'] if arm == 'transport_user' else t['speaker'],
                'name': t['speaker']}, 'create_time': epoch, 'update_time': epoch,
                'content': {'content_type': 'text', 'parts': [t['text']]},
                'status': 'finished_successfully', 'end_turn': True,
                'metadata': {'loom_source_id': case['source_id'], 'loom_source_turn_id': t['id'],
                    'loom_source_speaker': t['speaker'], 'loom_source_known_at': t['known_at']}}}
    return {'id': case['id'], 'title': case['id'], 'create_time': min(n['message']['create_time'] for n in mapping.values()),
        'update_time': max(n['message']['create_time'] for n in mapping.values()), 'mapping': mapping,
        'current_node': case['turns'][-1]['id'], 'metadata': {'loom_source_id': case['source_id'], 'synthetic': True,
            'role_projection': arm, 'fixture_language': case['language']}}


def prepare():
    policy = json.loads((HERE / 'policy.json').read_text())
    cases = json.loads((FIXTURE / 'inputs_dev.json').read_text())['cases']
    if len(cases) != 24:
        raise ValueError('expected24sourcecases')
    only = [source_only(c) for c in cases]
    write_new(HERE / 'source_only_inputs.json', only)
    rows = []
    for arm in policy['arms']:
        for case in only:
            path = HERE / 'prepared' / arm / (case['id'] + '.json')
            write_new(path, to_openai(case, arm, policy))
            rows.append({'case_id': case['id'], 'source_id': case['source_id'], 'language': case['language'],
                'arm': arm, 'input_path': str(path.relative_to(ROOT)), 'input_sha256': sha(path),
                'source_turns': len(case['turns']), 'original_text_sha256': {t['id']: hashlib.sha256(t['text'].encode()).hexdigest() for t in case['turns']}})
    write_new(HERE / 'prepared_manifest.json', {'schema': 'loom.research.native_source_only_prepared/1',
        'created_at': datetime.now(timezone.utc).isoformat(), 'cases_per_arm': 24, 'planned_case_arm_runs': len(rows),
        'source_input_sha256': sha(FIXTURE / 'inputs_dev.json'), 'source_only_input_sha256': sha(HERE / 'source_only_inputs.json'),
        'policy_sha256': sha(HERE / 'policy.json'), 'rows': rows, 'gold_accessed': False, 'validation_accessed': False})


def freeze():
    preflight = json.loads((HERE / 'BINARY_BEFORE_OUTPUT.json').read_text())
    cli = ROOT / 'loom/build/dev/cli/loom'
    if sha(cli) != preflight['binary_sha256']:
        raise ValueError('native_binary_drift')
    environment = json.loads(NATIVE_ENV.read_text())
    for name, value in environment['source_files_sha256'].items():
        if sha(ROOT / name) != value:
            raise ValueError('compiled_native_source_drift:' + name)
    paths = [HERE / name for name in ('policy.json', 'native_panel_v2.py', 'native_process_metrics_v2.py', 'evaluate_native_v2.py', 'test_native_panel_v2.py',
             'source_only_inputs.json', 'prepared_manifest.json', 'BINARY_BEFORE_OUTPUT.json', 'PACK_BEFORE_OUTPUT.json')]
    paths += [DOCS / 'NATIVE_SOURCE_FREE_PROTOCOL.md', DOCS / 'NATIVE_SOURCE_FREE_PROTOCOL2.md', NATIVE_ENV]
    write_new(HERE / 'freeze_before_outputs2.json', {'created_at': datetime.now(timezone.utc).isoformat(),
        'files_sha256': {str(p.relative_to(ROOT)): sha(p) for p in paths}, 'native_binary_sha256': sha(cli),
        'native_pack_hash': preflight['native_pack_hash'], 'source_head_at_freeze': subprocess.check_output(['git', 'rev-parse', 'HEAD']).decode().strip(),
        'prior_dev_gold_exposed_elsewhere': True, 'this_arm_gold_accessed': False, 'validation_accessed': False, 'paid_calls': 0})


TABLES = ('loom_kb_observations', 'loom_kb_entities', 'loom_kb_claims', 'loom_kb_status_records', 'loom_kb_decisions',
          'loom_kb_principles', 'loom_kb_areas', 'loom_kb_forks')


def snapshot_database(data_dir, run):
    db = data_dir / 'chatadhd.db'
    conn = sqlite3.connect(f'file:{db}?mode=ro', uri=True)
    conn.row_factory = sqlite3.Row
    try:
        tables = {r[0] for r in conn.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        bodies = {}
        for table in TABLES:
            if table in tables:
                bodies[table] = [json.loads(r[0]) for r in conn.execute(f'SELECT body FROM {table} WHERE run_id=? ORDER BY id', (run,))]
            else:
                bodies[table] = []
        sources = [dict(r) for r in conn.execute('SELECT id,kind,uri,blob_hash,size,metadata FROM loom_sources ORDER BY id')]
        blob_verifications = []
        for source in sources:
            h = source['blob_hash']
            if not h:
                continue
            path = data_dir / 'blobs' / h[:2] / h[2:4] / h
            if not path.exists():
                blob_verifications.append({'source_id': source['id'], 'blob_hash': h, 'available': False})
            else:
                blob_verifications.append({'source_id': source['id'], 'blob_hash': h, 'available': True,
                    'actual_sha256': sha(path), 'bytes': path.stat().st_size})
        messages = [dict(r) for r in conn.execute('SELECT id,role,text,created FROM messages ORDER BY rowid')]
        return {'run': run, 'bodies': bodies, 'sources': sources, 'blob_verifications': blob_verifications,
            'imported_core_messages': messages, 'database_sha256_after_run': sha(db)}
    finally:
        conn.close()


def run(workspace):
    before = json.loads((HERE / 'freeze_before_outputs2.json').read_text())
    for name, h in before['files_sha256'].items():
        if sha(ROOT / name) != h:
            raise ValueError('native_experiment_freeze_drift:' + name)
    cli = ROOT / 'loom/build/dev/cli/loom'
    if sha(cli) != before['native_binary_sha256']:
        raise ValueError('native_binary_drift')
    policy = json.loads((HERE / 'policy.json').read_text())
    manifest = json.loads((HERE / 'prepared_manifest.json').read_text())
    for planned in manifest['rows']:
        if sha(ROOT / planned['input_path']) != planned['input_sha256']:
            raise ValueError('source_input_drift_before_outputs')
    workspace.mkdir(exist_ok=False)
    output_root = HERE / 'first_runs2'
    output_root.mkdir(exist_ok=False)
    start = time.monotonic()
    rows = []
    blocked_reason = None
    env = dict(os.environ)
    for key in list(env):
        if key.startswith('OPENROUTER_') or key in ('OPENAI_API_KEY', 'ANTHROPIC_API_KEY'):
            del env[key]
    env['LC_ALL'] = 'C'
    for planned in manifest['rows']:
        row = dict(planned, state='unavailable')
        if blocked_reason is not None:
            row['reason'] = blocked_reason
            rows.append(row)
            continue
        if time.monotonic() - start >= policy['whole_series_timeout_seconds']:
            row['reason'] = 'whole_series_time_cap_before_attempt'
            rows.append(row)
            continue
        path = ROOT / planned['input_path']
        if sha(path) != planned['input_sha256']:
            raise ValueError('source_input_drift')
        ident = planned['arm'] + '_' + planned['case_id']
        directory = output_root / ident
        directory.mkdir()
        data_dir = workspace / ident
        config = dict(policy['pipeline_config'], sources=[str(path)])
        config_path = directory / 'executed_config.json'
        write_new(config_path, config)
        metric_path = directory / 'process_metrics.json'
        command = [sys.executable, str(HERE / 'native_process_metrics_v2.py'), '--metric-output', str(metric_path), '--',
            str(cli), '--data-dir', str(data_dir), '--json', '--quiet', 'knowledge', 'run', '--config', str(config_path)]
        wall = time.monotonic()
        try:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, start_new_session=True)
            try:
                stdout, stderr = process.communicate(timeout=min(policy['per_process_timeout_seconds'],
                    policy['whole_series_timeout_seconds'] - (wall - start)))
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                stdout, stderr = process.communicate()
                (directory / 'stdout.bin').write_bytes(stdout)
                (directory / 'stderr.bin').write_bytes(stderr)
                row.update({'reason': 'first_native_process_timeout', 'wall_seconds': time.monotonic() - wall})
                row['output_directory'] = str(directory.relative_to(ROOT))
                rows.append(row)
                continue
            (directory / 'stdout.bin').write_bytes(stdout)
            (directory / 'stderr.bin').write_bytes(stderr)
            row.update({'process_exit_code': process.returncode, 'wall_seconds': time.monotonic() - wall,
                'stdout_sha256': sha(directory / 'stdout.bin'), 'stderr_sha256': sha(directory / 'stderr.bin')})
            try:
                payload = json.loads(stdout)
            except (UnicodeDecodeError, json.JSONDecodeError):
                payload = None
            if payload is None:
                row['reason'] = 'first_native_stdout_not_json'
            elif process.returncode or payload.get('status') != 'done':
                row['reason'] = 'first_terminal_native_failure'
            elif payload.get('pack_hash') != before['native_pack_hash']:
                row['reason'] = 'runtime_native_pack_identity_drift'
                blocked_reason = 'native_pack_drift_prevents_remaining_attempts'
            else:
                native = snapshot_database(data_dir, payload['run'])
                write_new(directory / 'native_snapshot.json', native)
                row.update({'state': 'completed', 'run': payload['run'], 'pack_hash': payload['pack_hash'],
                    'snapshot_sha256': sha(directory / 'native_snapshot.json'), 'native_counts': {k: len(v) for k, v in native['bodies'].items()}})
        except (OSError, sqlite3.Error, KeyError, ValueError) as error:
            # First process bytes are retained before database decoding. Operational
            # failures are unavailable cases, never silently replaced by a retry.
            row.update({'reason': 'first_native_process_or_snapshot_failure',
                'error_type': type(error).__name__, 'wall_seconds': time.monotonic() - wall})
        row['output_directory'] = str(directory.relative_to(ROOT))
        rows.append(row)
    write_new(HERE / 'first_run_ledger2.json', {'schema': 'loom.research.native_source_free_first_runs/1',
        'created_at': datetime.now(timezone.utc).isoformat(), 'planned_case_arm_runs': len(manifest['rows']),
        'rows': rows, 'whole_series_wall_seconds': time.monotonic() - start, 'paid_calls': 0,
        'this_arm_gold_accessed': False, 'validation_accessed': False, 'native_binary_sha256': before['native_binary_sha256']})
    paths = [HERE / 'first_run_ledger2.json'] + sorted(output_root.rglob('*'))
    write_new(HERE / 'freeze_before_evaluation2.json', {'created_at': datetime.now(timezone.utc).isoformat(),
        'files_sha256': {str(p.relative_to(ROOT)): sha(p) for p in paths if p.is_file()},
        'this_arm_gold_accessed': False, 'validation_accessed': False})
    print(json.dumps({'planned': len(rows), 'completed': sum(r['state'] == 'completed' for r in rows),
        'wall_seconds': time.monotonic() - start, 'paid_calls': 0}))


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('stage', choices=('prepare', 'freeze', 'run'))
    p.add_argument('--workspace', type=Path)
    args = p.parse_args()
    if args.stage == 'run':
        if args.workspace is None:
            p.error('run requires fresh isolated --workspace')
        run(args.workspace)
    else:
        globals()[args.stage]()
