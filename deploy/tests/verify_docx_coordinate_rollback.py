#!/usr/bin/env python3
"""One synthetic DOCX coordinate rollback/restore gate. No builds or production.

Uses two already staged, archive-verified packages read-only. Credentials live
only in memory; no password/session-hash columns are queried. A fresh work dir
is mandatory: never rerun an interrupted fixture blindly.
"""
import argparse
from contextlib import closing
import hashlib
import http.server
import json
import os
from pathlib import Path
import secrets
import shutil
import signal
import socket
import stat
import sqlite3
import subprocess
import sys
import tarfile
import threading
import time
import traceback

import requests

LIMIT = 128 * 1024 * 1024
REPO = Path(__file__).resolve().parents[2]
TOOL = REPO / 'deploy/cold_snapshot.py'
FIXTURE = REPO / 'loom/tests/fixtures/docx_parts/base.docx'
SHA = lambda data: hashlib.sha256(data).hexdigest()
ENV = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'LOOM_LOG_ENABLED': '0', 'PYTHONDONTWRITEBYTECODE': '1'}


def file_sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def verify_package(archive, expected):
    assert file_sha(archive) == expected, 'archive digest'
    stage = archive.parent / 'packages' / archive.name.removesuffix('.tar.gz')
    manifest = json.loads((stage / 'MANIFEST.json').read_text())
    checked = 0
    with tarfile.open(archive, 'r:gz') as tar:
        names = set()
        for entry in tar.getmembers():
            assert entry.name not in names, 'duplicate archive path'
            names.add(entry.name)
            assert not Path(entry.name).is_absolute() and '..' not in Path(entry.name).parts
            assert entry.name == stage.name or entry.name.startswith(stage.name + '/')
            assert entry.isdir() or entry.isfile() or entry.issym()
            if entry.isdir():
                continue
            relative = entry.name[len(stage.name) + 1:]
            target = stage / relative
            assert target.resolve().is_relative_to(stage.resolve())
            digest = SHA(tar.extractfile(entry).read())
            assert digest == file_sha(target), 'staged file differs from immutable archive'
            if relative != 'MANIFEST.json':
                assert digest == manifest['files'][relative]['sha256']
            checked += 1
    assert checked == len(manifest['files']) + 1
    return stage, {'archive': str(archive), 'archive_sha256': expected,
                   'source_head': manifest['source_head'], 'build_type': manifest['build_type'],
                   'manifest_sha256': file_sha(stage / 'MANIFEST.json'), 'checked_payloads': checked,
                   'server_sha256': file_sha(stage / 'bin/loom-server'),
                   'reader_sha256': file_sha(stage / 'share/loom/docx_basic/reader.py'),
                   'runtime_manifest': json.loads((stage / 'share/loom/docx_basic/runtime.json').read_text())}


def restricted(stage, root, command):
    args = ['/usr/bin/prlimit', '--fsize=33554432', '--', '/usr/bin/setpriv',
            '--no-new-privs', '--landlock-access', 'fs']
    for rule in ['execute,read-file,read-dir:/usr', 'read-file,write-file:/dev/null',
                 'execute,read-file,read-dir:' + str(stage),
                 'execute,write-file,read-file,read-dir,remove-dir,remove-file,make-dir,make-reg,make-sym,refer,truncate:' + str(root)]:
        args += ['--landlock-rule', 'path-beneath:' + rule]
    return args + command


def observe_databases(root):
    result = {}
    for path in sorted(root.rglob('*')):
        if not path.is_file() or path.suffix not in ('.db', '.sqlite'):
            continue
        with closing(sqlite3.connect(path.as_uri() + '?mode=ro', uri=True)) as db:
            db.execute('PRAGMA query_only=ON')
            assert db.execute('PRAGMA integrity_check').fetchall() == [('ok',)]
            assert db.execute('PRAGMA foreign_key_check').fetchall() == []
            ddl = db.execute("SELECT type,name,tbl_name,sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name").fetchall()
            tables = [row[1] for row in ddl if row[0] == 'table']
            counts = {t: db.execute('SELECT count(*) FROM "' + t.replace('"', '""') + '"').fetchone()[0] for t in tables}
            meta = dict(db.execute("SELECT key,value FROM _meta WHERE key IN ('schema_version','loom_schema_version','import_schema_version','annotation_schema_version') ORDER BY key")) if '_meta' in tables else {}
            result[str(path.relative_to(root))] = {'integrity': 'ok', 'foreign_keys': 0,
                'schema_sha256': SHA(json.dumps(ddl).encode()), 'user_version': db.execute('PRAGMA user_version').fetchone()[0],
                'counts': counts, 'meta': meta}
    assert result, 'actual SQLite databases expected'
    return result


class Gate:
    def __init__(self, out, stages):
        self.out, self.stages = out, stages
        self.proc = None
        self.client = requests.Session()
        self.client.trust_env = False
        self.results = {'status': 'incomplete', 'phases': [], 'commands': [], 'budget': [], 'assertions': [], 'provider_calls': []}
        self.expected_text = 'Wewnątrz\nakapitu.\n'
        self.selected = ''
        self.username = 'synthetic-' + secrets.token_hex(5)
        self.password = secrets.token_urlsafe(32)
        self.provider = None

    def check(self, condition, label):
        assert condition, label
        self.results['assertions'].append(label)

    def budget(self, reserve=0):
        # Even a directory walk updates atime. Frozen snapshot metadata must
        # remain untouched: scan fd-relative with O_NOATIME, not Path.rglob.
        logical = allocated = 0
        def walk(fd):
            nonlocal logical, allocated
            allocated += os.fstat(fd).st_blocks * 512
            for name in os.listdir(fd):
                info = os.stat(name, dir_fd=fd, follow_symlinks=False)
                if stat.S_ISDIR(info.st_mode):
                    child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_NOATIME, dir_fd=fd)
                    try: walk(child)
                    finally: os.close(child)
                else:
                    assert stat.S_ISREG(info.st_mode), 'unexpected new-data topology'
                    logical += info.st_size
                    allocated += info.st_blocks * 512
        fd = os.open(self.out.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_NOATIME)
        try: walk(fd)
        finally: os.close(fd)
        self.check(max(logical, allocated) + reserve < LIMIT, 'new data below 128MiB')
        self.results['budget'].append({'logical_bytes': logical, 'allocated_bytes': allocated, 'reserved_bytes': reserve})

    def save(self):
        (self.out / 'result.json').write_text(json.dumps(self.results, ensure_ascii=False, indent=2) + '\n')

    def phase(self, name, result):
        self.results['phases'].append({'name': name, **result})
        self.budget()
        self.save()
        print(name, flush=True)
        checkpoint = REPO / 'CHECKPOINT.md'
        prior = checkpoint.read_text() if checkpoint.exists() else ''
        checkpoint.write_text('# CHECKPOINT — DOCX coordinate rollback: ' + name + '\n\n'
            'base_head: 5a560710f422095510316b4c9fbdb71d313ff6ca\n'
            'done: wynik etapu w ' + str(self.out / 'result.json') + '\n'
            'next: kontynuacja bieżącego harnessu; po przerwaniu najpierw wynik/procesy, NIE ponawiać generatora.\n'
            'blocker: brak w ukończonym etapie; całość jeszcze nie zakończona.\n'
            'tests: jedna syntetyczna próba; bez rebuilda i starych macierzy.\n\n--- historia ---\n\n' + prior)

    def profile(self, root, domain, values):
        p = root / 'profiles'
        p.mkdir(exist_ok=True)
        (p / (domain + '.pack')).write_text(json.dumps({'schema': 'loom.runtime_profile_overlay/1', 'domain': domain, 'overrides': values}))

    def start_provider(self):
        gate = self
        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass
            def do_GET(self):
                gate.results['provider_calls'].append({'method': 'GET', 'path': self.path})
                self.send_response(404); self.end_headers()
            def do_POST(self):
                raw = self.rfile.read(int(self.headers['Content-Length']))
                body = json.loads(raw)
                assert gate.password not in raw.decode() and gate.username not in raw.decode()
                assert not self.headers.get('Authorization'), 'no credential in synthetic provider request'
                system = '\n'.join(m['content'] for m in body['messages'] if m['role'] == 'system')
                has_text = gate.expected_text.strip() in system
                has_id = bool(gate.selected) and '[' + gate.selected + ']' in system
                answer = ('SYNTHETIC_SELECTED ' + gate.selected + '\n' + gate.expected_text) if has_text and has_id else 'SYNTHETIC_MISSING_SELECTION'
                gate.results['provider_calls'].append({'method': 'POST', 'path': self.path, 'request_sha256': SHA(raw),
                    'system_sha256': SHA(system.encode()), 'selected_text_present': has_text, 'selected_id_present': has_id,
                    'unselected_text_present': 'Żółw powtórzony.' in system or 'Komórka A' in system,
                    'response_sha256': SHA(answer.encode())})
                payload = json.dumps({'choices': [{'message': {'role': 'assistant', 'content': answer}, 'finish_reason': 'stop'}]}).encode()
                self.send_response(200); self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(payload))); self.end_headers(); self.wfile.write(payload)
        self.provider = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.provider_thread = threading.Thread(target=self.provider.serve_forever, daemon=True)
        self.provider_thread.start()
        return 'http://127.0.0.1:' + str(self.provider.server_port)

    def start(self, which, root, label):
        assert self.proc is None
        stage = self.stages[which]
        sock = socket.socket(); sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]; sock.close()
        self.base = 'http://127.0.0.1:' + str(port)
        command = restricted(stage, root, [str(stage / 'bin/loom-server'), '--host', '127.0.0.1', '--port', str(port),
            '--accounts-root', str(root), '--data-dir', str(root), '--static-dir', str(stage / 'web'), '--threads', '4'])
        env = {**ENV, 'TMPDIR': str(root)}
        self.results['commands'].append({'phase': label, 'command': command, 'env': env})
        self.proc = subprocess.Popen(command, cwd=root, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(200):
            assert self.proc.poll() is None, 'server exited during readiness'
            try:
                response = self.client.get(self.base + '/api/healthz', timeout=.3)
                if response.status_code == 200 and response.json().get('ok'):
                    self.check(True, 'isolated server health ready')
                    return
            except requests.RequestException:
                pass
            time.sleep(.05)
        raise RuntimeError('isolated server readiness timeout')

    def stop(self):
        if self.proc is None:
            return
        process = self.proc
        process.send_signal(signal.SIGTERM)
        try:
            code = process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill(); process.wait(); raise RuntimeError('own test server required forced cleanup')
        finally:
            self.proc = None
        self.check(code == 0, 'graceful server exit0')
        self.results.setdefault('stopped_pids', []).append(process.pid)

    def request(self, method, path, status=200, **kwargs):
        response = self.client.request(method, self.base + path, timeout=45, **kwargs)
        self.check(response.status_code == status, method + ' ' + path + ' status ' + str(status))
        return response

    def auth(self):
        me = self.request('GET', '/api/auth/me').json()
        self.check(me.get('accounts') and me.get('authenticated'), 'restored account session authenticated')
        self.check(me['user']['login'] == self.username, 'same synthetic account')

    def cold(self, action, label, **args):
        self.check(self.proc is None, 'no own writer before cold operation')
        self.budget(reserve=16 * 1024 * 1024)
        command = ['/usr/bin/setpriv', '--no-new-privs', '--landlock-access', 'fs']
        for rule in ['execute,read-file,read-dir:/usr', 'read-file,write-file:/dev/null', 'read-file:' + str(TOOL),
                     'write-file,read-file,read-dir,remove-dir,remove-file,make-dir,make-reg,refer,truncate:' + str(self.out)]:
            command += ['--landlock-rule', 'path-beneath:' + rule]
        command += ['/usr/bin/python3', '-B', str(TOOL), action, '--allowed-root', str(self.out), '--writers-stopped']
        for key, value in args.items():
            command += ['--' + key.replace('_', '-'), str(value)]
        result = subprocess.run(command, cwd=self.out, env=ENV, capture_output=True, text=True, timeout=60)
        self.results['commands'].append({'phase': label, 'command': command, 'exit_code': result.returncode})
        # Only the tool's metadata receipt; never synthetic DB contents/passwords.
        (self.out / (label + '.json')).write_text(result.stdout)
        self.check(result.returncode == 0, 'cold_snapshot ' + label)
        return json.loads(result.stdout)

    def backup(self, root, label):
        pre = self.out / (label + '-preflight.json')
        rec = self.cold('preflight', label + '-preflight-result', source=root, receipt=pre)
        dest = self.out / label
        rec = self.cold('backup', label + '-backup-result', source=root, preflight=pre,
                        preflight_sha256=rec['receipt_sha256'], destination=dest, destination_root=self.out)
        return dest, rec['manifest_sha256']

    def restore(self, snapshot, digest, name):
        dest = self.out / name
        self.cold('restore', name + '-restore-result', snapshot=snapshot, manifest_sha256=digest,
                  destination=dest, destination_root=self.out)
        return dest / 'data'

    def original(self, digest):
        response = self.request('GET', '/api/client/blob/' + digest + '/raw')
        self.check(SHA(response.content) == digest, 'actual original SHA256')

    def chat(self, conv=None):
        before = len(self.results['provider_calls'])
        payload = {'message': 'Przytocz wybrany fragment.', 'model': 'synthetic/rollback', 'stream': False,
                   **({'conv_id': conv} if conv else {'new_conversation': True})}
        self.check(self.expected_text.strip() not in payload['message'], 'question contains no source')
        response = self.request('POST', '/api/chat', json=payload)
        events = [json.loads(line[5:]) for line in response.text.splitlines() if line.startswith('data:')]
        errors = [e for e in events if e.get('type') == 'error']
        done = [e for e in events if e.get('type') == 'done']
        calls = self.results['provider_calls'][before:]
        return events, {'http_status': response.status_code, 'event_types': [e.get('type') for e in events],
            'errors': errors, 'provider_calls': calls, 'done_count': len(done),
            'answer_contains_selected_text': any(self.expected_text.strip() in e.get('text', '') for e in done)}

    def run(self):
        provider = self.start_provider()
        root = self.out / 'baseline'; root.mkdir(mode=0o700)
        self.profile(root, 'server_accounts', {'registration': 'closed', 'argon2id_preset': 'owasp_min',
            'allow_private_hosts': ['127.0.0.1'], 'guests': {'enabled': False, 'keys_file': ''}})
        self.profile(root, 'free_providers', {'providers': []})
        command = restricted(self.stages['old'], root, [str(self.stages['old'] / 'bin/loom-server'),
            'user', 'add', self.username, '--password-stdin', '--accounts-root', str(root)])
        made = subprocess.run(command, input=self.password + '\n', text=True, cwd=root, env={**ENV, 'TMPDIR': str(root)},
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        self.check(made.returncode == 0, 'predecessor created synthetic account via CLI; output not logged')
        self.start('old', root, 'baseline-old')
        self.request('POST', '/api/auth/login', json={'login': self.username, 'password': self.password})
        self.auth()
        self.request('PATCH', '/api/config', json={'llm_provider': 'local', 'local_model_base_url': provider, 'base_url': provider,
            'default_model': 'synthetic/rollback', 'model_registry_autorefresh': 'off', 'auto_title': False,
            'semantic_analysis': False, 'semantic_model': '', 'intent_routing_method': 'off', 'stream': False,
            'chat_flow': 'plain', 'chat_representation': 'raw', 'ui_language': 'pl', 'context_receipt_store': 'always',
            'context_execution': {'unified': {'enabled': False, 'include_knowledge': False, 'budget_tokens': 8192,
                'legacy_seed_ids': [], 'legacy_relation_hops': 0}, 'graph_reply': {'mode': 'off'}}})
        conv = self.request('POST', '/api/conversations', status=201, json={'title': 'Syntetyczna rozmowa przed zmianą'}).json()['id']
        raw = FIXTURE.read_bytes(); original = SHA(raw)
        uploaded = self.request('POST', '/api/client/blob/upload', data={'source_key': 'synthetic:rollback-docx', 'format': 'docx'},
            files={'file': ('synthetic.docx', raw, 'application/vnd.openxmlformats-officedocument.wordprocessingml.document')}).json()
        self.check(uploaded['hash'] == original and uploaded['source_key'] == 'synthetic:rollback-docx', 'baseline original upload/key')
        self.original(original)
        baseline_conv = self.request('GET', '/api/conversations/' + conv).json()
        self.check(self.request('GET', '/api/conversations/' + conv + '/messages?all=1').json() == [], 'baseline no post-copy messages')
        baseline_config = self.request('GET', '/api/config').json()
        self.stop()
        baseline_db = observe_databases(root)
        snapshot, snapsha = self.backup(root, 'pre-change-snapshot')
        self.phase('baseline-and-cold-backup', {'databases': baseline_db, 'conversation_sha256': SHA(json.dumps(baseline_conv, sort_keys=True).encode()),
            'original_sha256': original, 'snapshot_manifest_sha256': snapsha})
        candidate = self.restore(snapshot, snapsha, 'candidate')
        self.start('candidate', candidate, 'candidate-derived')
        self.auth(); self.original(original)
        parts = self.request('POST', '/api/client/blob/text', json={'hash': original, 'source_key': uploaded['source_key'],
            'name': 'synthetic.docx', 'format': 'docx', 'language': 'pl'}).json()
        self.check(parts['coordinate'] == 'derived_utf8_bytes' and parts['derivation']['original_sha256'] == original, 'candidate derived provenance')
        self.selected = parts['occurrence_nodes'][2]
        selected = self.request('POST', '/api/client/blob/text/read', json={'node_id': self.selected}).json()
        self.check(selected['text'] == self.expected_text, 'candidate exact selected PL text')
        self.request('PATCH', '/api/config', json={'context_execution': {'unified': {'enabled': True, 'include_knowledge': False,
            'legacy_seed_ids': [self.selected], 'legacy_relation_hops': 0, 'budget_tokens': 8192}}})
        events, reply = self.chat(conv)
        self.check(reply['done_count'] == 1 and not reply['errors'] and reply['answer_contains_selected_text'], 'candidate synthetic answer uses selected fragment')
        self.check(len(reply['provider_calls']) == 1 and reply['provider_calls'][0]['selected_text_present'] and
                   reply['provider_calls'][0]['selected_id_present'] and not reply['provider_calls'][0]['unselected_text_present'], 'candidate exact actual system context')
        history = self.request('GET', '/api/conversations/' + conv + '/messages?all=1').json()
        self.check(len(history) == 2, 'candidate two durable post-copy messages')
        self.stop(); candidate_db = observe_databases(candidate)
        candidate_snapshot, candidatesha = self.backup(candidate, 'candidate-snapshot')
        self.phase('candidate-derived-context', {'databases': candidate_db, 'projection': parts, 'provenance': selected['provenance'],
            'selected_id': self.selected, 'selected_text_sha256': SHA(self.expected_text.encode()), 'reply': reply,
            'snapshot_manifest_sha256': candidatesha, 'default_package_reader': True})
        probe = self.restore(candidate_snapshot, candidatesha, 'old-on-candidate-copy')
        self.start('old', probe, 'old-on-derived-copy'); self.auth(); self.original(original)
        config = self.request('GET', '/api/config').json()
        self.check(config['context_execution']['unified']['legacy_seed_ids'] == [self.selected], 'old still stores selected ID')
        read = self.client.post(self.base + '/api/client/blob/text/read', json={'node_id': self.selected}, timeout=30)
        read_body = read.json()
        _, old_reply = self.chat()  # new conversation: no answer/history can leak the fragment into the test
        self.stop(); probe_db = observe_databases(probe)
        compatible = read.status_code == 200 and read_body.get('text') == self.expected_text and old_reply['answer_contains_selected_text'] and not old_reply['errors']
        self.phase('old-derived-observation', {'explicit_read_status': read.status_code, 'explicit_read_error': read_body.get('error'),
            'reply': old_reply, 'in_place_semantic_compatibility': compatible, 'databases': probe_db})
        restored = self.restore(snapshot, snapsha, 'restored-pre-change')
        before_start = observe_databases(restored)
        self.check(before_start == baseline_db, 'restored pre-start schema/counts/meta/integrity equal baseline')
        self.start('old', restored, 'old-after-restore'); self.auth(); self.original(original)
        restored_conv = self.request('GET', '/api/conversations/' + conv).json()
        self.check(restored_conv == baseline_conv, 'restored exact pre-copy conversation')
        self.check(self.request('GET', '/api/conversations/' + conv + '/messages?all=1').json() == [], 'RPO post-copy candidate messages absent')
        config = self.request('GET', '/api/config').json()
        self.check(config['context_execution'] == baseline_config['context_execution'], 'RPO selector restored to pre-change state')
        sources = self.request('GET', '/api/sources?limit=100').json()
        self.check(not any(s['id'] == parts['source_id'] for s in sources), 'RPO derived Source absent after restore')
        self.stop(); after_stop = observe_databases(restored)
        self.check(after_stop == baseline_db, 'restored opened-and-closed schema/counts/meta/integrity equal baseline')
        # No SQLite connection to frozen roots. O_NOATIME cold preflight verifies complete bytes/metadata again.
        original_after = self.cold('preflight', 'baseline-after-result', source=root, receipt=self.out / 'baseline-after.json')
        snapshot_after = self.cold('preflight', 'snapshot-after-result', source=snapshot / 'data', receipt=self.out / 'snapshot-after.json')
        before_tree = json.loads((self.out / 'pre-change-snapshot-preflight.json').read_text())['tree']
        self.check(json.loads((self.out / 'baseline-after.json').read_text())['tree'] == before_tree, 'baseline original bytes/metadata unchanged')
        self.check(json.loads((self.out / 'snapshot-after.json').read_text())['tree'] == before_tree, 'cold snapshot bytes/metadata unchanged')
        self.phase('restored-old-runtime', {'before_start': before_start, 'after_stop': after_stop,
            'original_sha256': original, 'exact_conversation': True, 'same_account_session': True,
            'metadata_preserved_by_cold_snapshot': True, 'RPO': 'snapshot boundary: two new messages, derived Source/Unit/Observation and selector change are absent',
            'baseline_and_snapshot_unchanged': True})
        self.results.update(status='completed-scoped-restore', in_place_semantic_compatibility=compatible,
                            restore_to_pre_change_pass=True, no_build=True, production_access=False, deployed=False)
        self.save()

    def cleanup(self):
        self.stop()
        self.client.close()
        if self.provider:
            self.provider.shutdown(); self.provider.server_close(); self.provider_thread.join(timeout=5)
        self.results['processes_finished'] = True
        self.budget(); self.save()


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', required=True, type=Path)
    for label in ['old', 'candidate']:
        parser.add_argument('--' + label + '-archive', required=True, type=Path)
        parser.add_argument('--' + label + '-sha256', required=True)
    args = parser.parse_args()
    out = args.work.absolute()
    assert out.is_relative_to(REPO) and not out.exists(), 'fresh own work directory required'
    out.mkdir(mode=0o700)
    stages = {}; packages = {}
    for label in ['old', 'candidate']:
        stages[label], packages[label] = verify_package(getattr(args, label + '_archive'), getattr(args, label + '_sha256'))
    (out / 'packages.json').write_text(json.dumps(packages, indent=2) + '\n')
    gate = Gate(out, stages)
    gate.results['free_bytes_before'] = shutil.disk_usage(out).free
    gate.results['limit_bytes'] = LIMIT
    gate.results['tool_sha256'] = file_sha(TOOL)
    gate.results['fixture_sha256'] = file_sha(FIXTURE)
    success = False
    try:
        gate.run(); success = True
    except Exception as error:
        gate.results['failure'] = {'type': type(error).__name__, 'message': str(error),
            'frames': [{'file': Path(f.filename).name, 'line': f.lineno, 'function': f.name} for f in traceback.extract_tb(error.__traceback__)]}
        gate.save()
    finally:
        gate.cleanup()
    print(json.dumps({'success': success, 'status': gate.results['status'], 'phases': len(gate.results['phases']),
        'in_place_semantic_compatibility': gate.results.get('in_place_semantic_compatibility'),
        'restore_to_pre_change_pass': gate.results.get('restore_to_pre_change_pass'), 'failure': gate.results.get('failure')}, ensure_ascii=False))
    return 0 if success else 1


if __name__ == '__main__':
    raise SystemExit(main())
