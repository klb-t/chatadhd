#!/usr/bin/env python3
"""Independent loopback HTTP capture; no imports from production or its tests."""
import argparse
import base64
import hashlib
import http.server
import json
import os
from pathlib import Path
import socket
import sqlite3
import subprocess
import threading
import time
import traceback
import urllib.request


def sha(data):
    return hashlib.sha256(data).hexdigest()


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', required=True, type=Path)
    parser.add_argument('--library', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    data = out / 'runtime'
    data.mkdir()
    checks, captures, transcripts = [], [], []
    trace_baselines = {}
    mode = {'http_status': 200}
    proc = None
    log = (out / 'native.log').open('wb')

    def write(name, value):
        (out / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')

    def check(name, condition, detail=None):
        checks.append({'check': name, 'pass': bool(condition), 'detail': detail})
        write('checks.json', checks)

    class Provider(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            raw = self.rfile.read(int(self.headers['Content-Length']))
            body = json.loads(raw)
            record = {'path': self.path, 'body': body, 'body_sha256': sha(raw),
                      'body_bytes_base64': base64.b64encode(raw).decode(),
                      'response_status': mode['http_status']}
            captures.append(record)
            write('provider-captures.json', captures)
            if mode['http_status'] != 200:
                reply = b'{"error":{"message":"W6_SYNTHETIC_PROVIDER_FAILURE"}}'
                mime = 'application/json'
            elif body.get('stream'):
                reply = (b'data: {"choices":[{"delta":{"content":"W6 fixed stream reply"}}]}\n\n'
                         b'data: [DONE]\n\n')
                mime = 'text/event-stream'
            else:
                reply = json.dumps({'choices': [{'message': {'role': 'assistant',
                         'content': 'W6 fixed reply'}}],
                         'usage': {'prompt_tokens': 4, 'completion_tokens': 3}}).encode()
                mime = 'application/json'
            self.send_response(mode['http_status'])
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(reply)))
            self.end_headers()
            self.wfile.write(reply)

    provider = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Provider)
    threading.Thread(target=provider.serve_forever, daemon=True).start()
    port = free_port()
    base = f'http://127.0.0.1:{port}'
    env = os.environ.copy()
    for key in ('http_proxy', 'https_proxy', 'HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY', 'all_proxy'):
        env.pop(key, None)
    env['NO_PROXY'] = '127.0.0.1,localhost'
    env['LD_LIBRARY_PATH'] = str(args.library.resolve().parent)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def api(path, body=None, method=None):
        raw = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(base + path, data=raw,
                                     headers={'Content-Type': 'application/json'}, method=method)
        with opener.open(req, timeout=60) as response:
            text = response.read().decode()
        if path == '/api/chat':
            result = [json.loads(line[5:].strip()) for line in text.splitlines()
                      if line.startswith('data:')]
        else:
            result = json.loads(text)
        transcripts.append({'path': path, 'request': body, 'response': result})
        # Dummy key is never included in the public API transcript.
        if not path.startswith('/api/secrets'):
            write('api-transcript.json', [r for r in transcripts if not r['path'].startswith('/api/secrets')])
        return result

    command = [str(args.server.resolve()), '--host', '127.0.0.1', '--port', str(port), '--data-dir', str(data)]

    def start():
        nonlocal proc
        proc = subprocess.Popen(command, env=env, stdout=log, stderr=log)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                raise RuntimeError(f'native server exited {proc.returncode}')
            try:
                api('/api/healthz')
                return
            except OSError:
                time.sleep(.05)
        raise RuntimeError('native server readiness timeout')

    def stop():
        if proc is not None and proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=15)

    def saved(mid):
        with sqlite3.connect(data / 'chatadhd.db') as db:
            return json.loads(db.execute('SELECT metadata FROM messages WHERE id=?', (mid,)).fetchone()[0])

    def send(label, message, options, expect_sent=True, expect_trace=True, expected_final='done'):
        before = len(captures)
        events = api('/api/chat', {'message': message, **options})
        started = next((e for e in events if e['type'] == 'start'), None)
        final = events[-1]
        check(label + ': terminal event', final['type'] == expected_final, final['type'])
        observed = len(captures) - before
        check(label + ': provider count', observed == int(expect_sent), observed)
        meta = saved(started['user_message_id']) if started else {}
        trace = meta.get('context_trace')
        check(label + ': trace retention', (trace is not None) == expect_trace)
        if trace:
            trace_baselines[started['user_message_id']] = trace
            if expect_sent and observed == 1:
                payload = captures[-1]['body']
                check(label + ': exact array equality', trace['messages'] == payload['messages'])
                compact = json.dumps(payload['messages'], ensure_ascii=False, separators=(',', ':')).encode()
                check(label + ': compact array digest', trace['messages_sha256'] == sha(compact))
                check(label + ': captured provider path', captures[-1]['path'] == '/chat/completions')
            if expected_final == 'done':
                check(label + ': returned trace equality', final.get('context_trace') == trace)
        elif expected_final == 'done':
            check(label + ': returned trace absent', 'context_trace' not in final)
        return started, final, trace

    repo = Path(__file__).resolve().parents[3]
    write('environment.json', {'base_sha': subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip(),
          'server_sha256': sha(args.server.read_bytes()), 'library_sha256': sha(args.library.read_bytes()),
          'harness_sha256': sha(Path(__file__).read_bytes()), 'command': command,
          'python_version': subprocess.check_output(['python3', '--version'], text=True).strip(),
          'provenance': 'new synthetic fixture; local fake provider; no owner data'})
    try:
        start()
        api('/api/config', {'base_url': f'http://127.0.0.1:{provider.server_port}',
                           'semantic_model': '', 'semantic_analysis': False, 'default_model': 'w6-local',
                           'system_prompt': 'W6_SYSTEM_SENTINEL', 'stream': False}, 'PATCH')
        api('/api/secrets/api_key', {'value': 'W6_LOCAL_DUMMY_KEY'})
        api('/api/memory', {'content': 'W6_MEMORY_SENTINEL: Preserve μ and 🙂 exactly.', 'active': True})
        source = data / 'w6-source.json'
        source.write_text(json.dumps([{'id': 'w6-transport-source', 'title': 'Loom W6 synthetic architecture',
            'current_node': 'u1', 'mapping': {'u1': {'id': 'u1', 'parent': None, 'children': [],
            'message': {'id': 'u1', 'author': {'role': 'user'}, 'create_time': 1780272000,
            'content': {'content_type': 'text', 'parts': ['Loom is a software application. Requirement: Loom must preserve original source bytes. Decision: use a provider registry.']},
            'metadata': {}}}}}]), encoding='utf-8')
        write('source-fixture.json', json.loads(source.read_text()))
        run = api('/api/knowledge/run', {'sources': [str(source)], 'llm': 'off', 'out_dir': 'w6-knowledge',
                                        'stage_params': {'catalog': {'import': {'mode': 'full'}}}})
        check('offline knowledge completes', run['status'] == 'done', run['status'])
        check('offline knowledge zero provider calls', len(captures) == 0, len(captures))
        knowledge = {'run': run['run'], 'budget_tokens': 1000, 'detail_resolution': 'full', 'relation_hops': 0}
        first, _, trace = send('knowledge', 'Explain Loom source preservation. W6_FIRST μ 🙂',
                               {'knowledge_context': knowledge, 'trace_context': True, 'include_graph_memory': False})
        check('knowledge resolved run', trace['knowledge_context_request']['run'] == run['run'])
        check('knowledge section present', trace['knowledge_context'] is not None)
        check('memory section present', 'W6_MEMORY_SENTINEL' in json.dumps(captures[-1]['body']['messages']))
        conv = first['conv_id']
        _, _, trace = send('history', 'W6_SECOND', {'conv_id': conv, 'trace_context': True, 'include_graph_memory': False})
        check('history prior IDs exactly two', len(trace['history_message_ids']) == 2, trace['history_message_ids'])
        check('history first user once', sum(m.get('content') == 'Explain Loom source preservation. W6_FIRST μ 🙂' for m in trace['messages']) == 1)
        _, _, trace = send('isolated', 'W6_ISOLATED', {'conv_id': conv, 'include_memory': False,
                      'include_history': False, 'include_graph_memory': False, 'trace_context': True})
        check('isolated no historical IDs', trace['history_message_ids'] == [])
        check('isolated memory absent', 'W6_MEMORY_SENTINEL' not in json.dumps(trace['messages']))
        send('optout', 'W6_NO_TRACE', {'trace_context': False, 'include_history': False}, expect_trace=False)
        mode['http_status'] = 500
        send('provider500', 'W6_FAIL_HTTP', {'trace_context': True}, expected_final='error')
        mode['http_status'] = 200
        api('/api/secrets/api_key', method='DELETE')
        send('missing_key', 'W6_FAIL_AUTH', {'trace_context': True}, expect_sent=False, expected_final='error')
        api('/api/secrets/api_key', {'value': 'W6_LOCAL_DUMMY_KEY'})
        send('missing_run', 'W6_FAIL_RUN', {'knowledge_context': {'run': 'w6-nonexistent'}, 'trace_context': True},
             expect_sent=False, expect_trace=False, expected_final='error')
        _, final, _ = send('stream', 'W6_STREAM', {'stream': True, 'trace_context': True})
        check('provider stream explicitly enabled', captures[-1]['body']['stream'] is True)
        check('stream fixed answer delivered', final['text'] == 'W6 fixed stream reply')
        write('trace-baselines.json', trace_baselines)
        stop()
        start()
        for mid, trace in trace_baselines.items():
            check('reopen ' + mid, saved(mid).get('context_trace') == trace)
        before = len(captures)
        api('/api/graph/reindex', {})
        for mid, trace in trace_baselines.items():
            check('reindex ' + mid, saved(mid).get('context_trace') == trace)
        check('reindex no provider requests', len(captures) == before)
    except Exception:
        check('harness completed', False, traceback.format_exc())
    finally:
        stop()
        provider.shutdown()
        provider.server_close()
        log.close()
        write('summary.json', {'checks_passed': sum(c['pass'] for c in checks), 'checks_total': len(checks),
                              'provider_requests': len(captures), 'failures': [c for c in checks if not c['pass']]})
    print((out / 'summary.json').read_text())
    return 0 if all(c['pass'] for c in checks) else 1


if __name__ == '__main__':
    raise SystemExit(main())
