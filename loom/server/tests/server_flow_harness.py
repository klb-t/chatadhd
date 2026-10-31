"""Wspólny rzeczywisty HTTP harness dialogu/PDF; wyłącznie nowe dane i loopback."""
import http.server
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest

import requests

from test_server_guest_credentials import Server
from test_server_guests import overlay, solve_pow
from test_server_security import sse_events


class RecordingProvider:
    def __init__(self, transcript):
        self.calls = []
        self.fail = False
        self.transcript = transcript
        provider = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def do_GET(self):
                # Liczymy także katalog modeli: „zero wywołań” nie ukrywa GET.
                call = {'path': self.path, 'request': None, 'raw_utf8': '', 'status': 200}
                provider.record(call)
                data = json.dumps({'data': [{'id': 'mock/http-flow'}]}).encode('utf-8')
                self.send_response(200 if self.path == '/models' else 404)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_POST(self):
                raw = self.rfile.read(int(self.headers['Content-Length']))
                call = {'path': self.path, 'request': json.loads(raw),
                        'raw_utf8': raw.decode('utf-8'), 'status': 400 if provider.fail else 200}
                provider.record(call)
                if provider.fail:
                    answer = {'error': {'message': 'Syntetyczny błąd dostawcy HTTP', 'type': 'invalid_request_error'}}
                else:
                    answer = {'choices': [{'message': {'role': 'assistant', 'content': provider.reply(len(provider.calls))},
                                           'finish_reason': 'stop'}],
                              'usage': {'prompt_tokens': 100, 'completion_tokens': 10, 'total_tokens': 110}}
                data = json.dumps(answer, ensure_ascii=False).encode('utf-8')
                self.send_response(call['status'] if self.path == '/chat/completions' else 404)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.base = f'http://127.0.0.1:{self.server.server_port}'
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        print(f'provider pid={os.getpid()} port={self.server.server_port}', flush=True)

    def record(self, call):
        self.calls.append(call)
        with self.transcript.open('a', encoding='utf-8') as out:
            out.write(json.dumps(call, ensure_ascii=False) + '\n')

    @staticmethod
    def reply(number):
        return f'Atrapa lokalna — odpowiedź {number}: żółw.'

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)


class HttpFlowCase(unittest.TestCase):
    def setUp(self):
        # Brak fallbacku do cudzego/produkcyjnego binarium.
        binary = Path(os.environ['LOOM_SERVER_BIN']).resolve(strict=True)
        if not binary.is_relative_to(Path(__file__).resolve().parents[3]):
            raise RuntimeError('LOOM_SERVER_BIN musi pochodzić z własnego worktree')
        self.tmp = tempfile.TemporaryDirectory(prefix='loom-http-flow-', dir=Path.cwd())
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        evidence = Path(os.environ.get('LOOM_FLOW_EVIDENCE_DIR', self.root)).resolve()
        if not evidence.is_relative_to(Path.cwd().resolve()):
            raise RuntimeError('Dowody muszą pozostać pod CWD')
        evidence.mkdir(parents=True, exist_ok=True)
        self.transcript = evidence / (self.id() + '.requests.jsonl')
        self.transcript.write_text('', encoding='utf-8')
        self.provider = RecordingProvider(self.transcript)
        self.addCleanup(self.provider.close)
        keys = self.root / 'invented-test-keys.txt'
        keys.write_text('LOOM_FLOW_FAKE_KEY=fake-http-flow-key-only\n', encoding='utf-8')
        overlay(self.root, 'server_accounts', {
            'allow_private_hosts': ['127.0.0.1'],
            'guests': {'enabled': True, 'sweep_interval_seconds': 0, 'keys_file': str(keys),
                       'secrets_routes': 'write_only_session', 'model_sources': ['byok', 'free', 'local'],
                       'protection': {'pow': {'bits': 0, 'bits_under_load': 0}},
                       'workdir': {'subdir': 'guests', 'idle_ttl_seconds': 0, 'max_bytes': 0}}})
        overlay(self.root, 'free_providers', {
            'providers': [{'id': 'http_flow', 'display_name': 'Atrapa HTTP', 'tier': 'free', 'enabled': True,
                           'base_url': self.provider.base, 'auth_env': 'LOOM_FLOW_FAKE_KEY', 'auth_secret': 'api_key',
                           'active_when': 'key_present', 'data_terms': {'training': 'no'}}],
            'free_model_choice': {'rank_source': 'table', 'tier_order': ['free'], 'table': [
                {'model': 'mock/http-flow', 'provider': 'http_flow', 'quality': 1.0}]}})
        self.srv = Server(self.root)
        self.addCleanup(lambda: self.srv.close())
        self.client = requests.Session()
        self.client.trust_env = False
        self.addCleanup(self.client.close)
        r = self.client.post(self.srv.base + '/api/auth/guest',
                             json=solve_pow(self.srv.base, self.client), timeout=30)
        self.assertEqual(r.status_code, 201, r.text)
        self.patch({'stream': False, 'semantic_analysis': False, 'auto_title': False,
                    'ui_language': 'pl', 'chat_flow': 'plain', 'chat_representation': 'raw',
                    'context_receipt_store': 'always'})
        r = self.client.post(self.srv.base + '/api/conversations', json={'title': 'Syntetyczny test HTTP'}, timeout=15)
        self.assertEqual(r.status_code, 201, r.text)
        self.conv = r.json()['id']

    def patch(self, values):
        r = self.client.patch(self.srv.base + '/api/config', json=values, timeout=15)
        self.assertEqual(r.status_code, 200, r.text)

    def history(self):
        r = self.client.get(self.srv.base + f'/api/conversations/{self.conv}/messages', timeout=15)
        self.assertEqual(r.status_code, 200, r.text)
        return r.json()

    def restart(self):
        self.srv.close()
        self.srv = Server(self.root)  # ten sam nowy katalog, ten sam cookie gościa

    def send(self, text, attachments=None, **options):
        body = {'conv_id': self.conv, 'message': text, 'stream': False, **options}
        if attachments is not None:
            body['attachments'] = attachments
        r = self.client.post(self.srv.base + '/api/chat', json=body, timeout=60)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.headers['Content-Type'].replace(' ', '').lower(), 'text/event-stream;charset=utf-8')
        events = sse_events(r)
        self.assertTrue(events, r.content)
        return events

    def success(self, text, attachments=None, **options):
        events = self.send(text, attachments, **options)
        self.assertEqual([e['type'] for e in events if e.get('type') in ('done', 'error')], ['done'], events)
        done = next(e for e in events if e.get('type') == 'done')
        self.assertEqual(done['text'], self.provider.reply(len(self.provider.calls)))
        self.assertEqual(done['conv_id'], self.conv)
        return done

    def wire_messages(self, index=-1):
        call = self.provider.calls[index]
        self.assertEqual(call['path'], '/chat/completions')
        self.assertEqual(call['request']['model'], 'mock/http-flow')
        self.assertFalse(call['request']['stream'])
        return [m for m in call['request']['messages'] if m['role'] != 'system']

    @staticmethod
    def text(message):
        content = message['content']
        return content if isinstance(content, str) else '\n'.join(p['text'] for p in content if p['type'] == 'text')
