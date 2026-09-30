"""W1 actual C ABI -> native HTTP -> local provider, with synthetic messages only."""
import copy
import ctypes
import hashlib
import http.server
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest


def encoded(value):
    return json.dumps(value, ensure_ascii=False).encode('utf-8')


class ActiveTaskHttpTest(unittest.TestCase):
    def test_compiled_instruction_reaches_local_provider_and_invalid_binding_does_not(self):
        library = os.environ.get('LOOM_LIBRARY')
        if not library:
            self.skipTest('LOOM_LIBRARY not set; shared native build required')
        lib = ctypes.CDLL(library)
        signatures = {
            'loom_init_ex': ([ctypes.c_char_p, ctypes.c_void_p], ctypes.c_void_p),
            'loom_shutdown': ([ctypes.c_void_p], None),
            'loom_free_string': ([ctypes.c_void_p], None),
            'loom_set_config_json': ([ctypes.c_void_p, ctypes.c_char_p], ctypes.c_int),
            'loom_set_secret': ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p], ctypes.c_int),
            'loom_create_conversation': ([ctypes.c_void_p, ctypes.c_char_p], ctypes.c_void_p),
            'loom_get_message': ([ctypes.c_void_p, ctypes.c_char_p], ctypes.c_void_p),
            'loom_get_messages': ([ctypes.c_void_p, ctypes.c_char_p], ctypes.c_void_p),
            'loom_chat_ex': ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p], ctypes.c_void_p),
        }
        for name, (args, result) in signatures.items():
            getattr(lib, name).argtypes = args
            getattr(lib, name).restype = result
        calls = []

        class Provider(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                body = self.rfile.read(int(self.headers['Content-Length']))
                calls.append({'path': self.path, 'body': json.loads(body)})
                answer = encoded({'choices': [{'message': {'content': 'ASSISTANT_REJECTED_VARIANT: use decorative prose.'}}]})
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(answer)))
                self.end_headers()
                self.wfile.write(answer)

            def log_message(self, *args):
                pass

        def read(pointer):
            self.assertTrue(pointer)
            try:
                return json.loads(ctypes.string_at(pointer))
            finally:
                lib.loom_free_string(pointer)

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Provider)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory(prefix='loom-w1-http-') as directory:
                ctx = lib.loom_init_ex(encoded({'data_dir': directory, 'start_workers': False}), None)
                self.assertTrue(ctx)
                try:
                    self.assertEqual(lib.loom_set_config_json(ctx, encoded({
                        'base_url': f'http://127.0.0.1:{server.server_port}',
                        'default_model': 'synthetic/local', 'auto_title': False,
                        'semantic_analysis': False, 'system_prompt': '', 'stream': False,
                    })), 0)
                    self.assertEqual(lib.loom_set_secret(ctx, b'api_key', b'local-test-placeholder'), 0)
                    conv = read(lib.loom_create_conversation(ctx, b'W1 local transport'))['id']
                    source_text = 'Write a report; be concise except retain the audit appendix. Reject decorative prose.'
                    base = {'conv_id': conv, 'stream': False, 'include_memory': False, 'include_graph_memory': False}
                    first = read(lib.loom_chat_ex(ctx, encoded(dict(base, message=source_text)), None, None))
                    self.assertNotIn('error', first)
                    sources = read(lib.loom_get_messages(ctx, conv.encode()))
                    self.assertEqual(len(sources), 2)
                    events = ['user-refinement', 'assistant-rejected-attempt']
                    bindings = {event: {'message_id': msg['id'], 'text_sha256': hashlib.sha256(msg['text'].encode()).hexdigest()}
                                for event, msg in zip(events, sources)}

                    def clause(identity, kind, status, text, event, conditions=None):
                        return {'id': identity, 'kind': kind, 'status': status, 'text': text,
                                'source_event_ids': [event], 'claim_ids': [], 'conditions': conditions or [], 'supersedes': []}

                    spec = {'schema': 'loom.active_task_spec/1', 'product_ref': {'kind': 'product', 'id': 'p-http-1'},
                            'goal_id': 'report', 'knowledge_run': None,
                            'scope': {'conversation_id': conv, 'branch_id': 'native:active', 'task_id': 'report'},
                            'version': 1, 'previous_product_ref': None, 'known_at': '2026-09-30T20:00:00Z',
                            'representation': 'derived_product', 'materializer': {'id': 'manual-http-test', 'version': '1'},
                            'history_event_ids': events,
                            'source_refs': [{'event_id': event, 'locator': {'source': 'synthetic'},
                                             'known_at': '2026-09-30T19:00:00Z', 'quote': msg['text']}
                                            for event, msg in zip(events, sources)],
                            'statements': [clause('goal', 'goal', 'active', 'Write a concise report.', events[0]),
                                           clause('exception', 'exception', 'active', 'Retain audit identifiers: Żółć-17.', events[0], ['Audit appendix only.']),
                                           clause('rejected', 'alternative', 'rejected', 'ASSISTANT_REJECTED_VARIANT', events[1])],
                            'compiled_instruction': {'text': 'UNTRUSTED_SUMMARY', 'source_map': [
                                {'span': {'byte_start': 0, 'byte_len': 17}, 'statement_ids': ['goal']} ]}}
                    request = dict(base, message='Produce the final report now.', active_task_spec=spec, active_task_bindings=bindings)
                    result = read(lib.loom_chat_ex(ctx, encoded(request), None, None))
                    self.assertNotIn('error', result)
                    payload = calls[-1]['body']
                    self.assertEqual(calls[-1]['path'], '/chat/completions')
                    self.assertEqual(payload['messages'], result['context_trace']['messages'])
                    self.assertEqual(sum(m['content'] == request['message'] for m in payload['messages']), 1)
                    text = json.dumps(payload['messages'], ensure_ascii=False)
                    self.assertIn('Write a concise report.', text)
                    self.assertIn('Żółć-17', text)
                    self.assertIn('Audit appendix only.', text)
                    self.assertNotIn('ASSISTANT_REJECTED_VARIANT', text)
                    self.assertNotIn('UNTRUSTED_SUMMARY', text)
                    self.assertNotIn('active_task_spec', payload)
                    saved = read(lib.loom_get_message(ctx, result['user_message_id'].encode()))
                    self.assertEqual(saved['metadata']['active_task']['supplied_spec'], spec)
                    for original in sources:
                        self.assertEqual(read(lib.loom_get_message(ctx, original['id'].encode())), original)

                    # Explicit append composes history; trace opt-out still retains task provenance.
                    request.update(message='Use the explicit history recipe.', active_task_history='append', trace_context=False)
                    appended = read(lib.loom_chat_ex(ctx, encoded(request), None, None))
                    self.assertNotIn('error', appended)
                    self.assertNotIn('context_trace', appended)
                    self.assertIn('ASSISTANT_REJECTED_VARIANT', json.dumps(calls[-1]['body']['messages']))
                    saved = read(lib.loom_get_message(ctx, appended['user_message_id'].encode()))
                    self.assertEqual(saved['metadata']['active_task']['supplied_spec'], spec)
                    self.assertNotIn('context_trace', saved['metadata'])
                    before = read(lib.loom_get_messages(ctx, conv.encode()))
                    bad = copy.deepcopy(request)
                    bad['active_task_bindings'][events[0]]['text_sha256'] = '0' * 64
                    rejected = read(lib.loom_chat_ex(ctx, encoded(bad), None, None))
                    self.assertIn('error', rejected)
                    self.assertEqual(len(calls), 3)  # seed, compiled, append; invalid send makes no HTTP call
                    self.assertEqual(read(lib.loom_get_messages(ctx, conv.encode())), before)
                    print('W1 transport receipt: 3 local provider calls, 0 remote calls; invalid binding adds 0 messages.')
                finally:
                    lib.loom_shutdown(ctx)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)


if __name__ == '__main__':
    unittest.main(verbosity=2)
