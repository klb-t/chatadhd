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


class ActiveTaskRevisionHttpTest(unittest.TestCase):
    """Real ABI/HTTP revision cases; each method owns a fresh database/provider."""

    def setUp(self):
        library = os.environ.get('LOOM_LIBRARY')
        if not library:
            self.skipTest('LOOM_LIBRARY not set; shared native build required')
        self.lib = ctypes.CDLL(library)
        signatures = {
            'loom_init_ex': ([ctypes.c_char_p, ctypes.c_void_p], ctypes.c_void_p),
            'loom_shutdown': ([ctypes.c_void_p], None),
            'loom_free_string': ([ctypes.c_void_p], None),
            'loom_set_config_json': ([ctypes.c_void_p, ctypes.c_char_p], ctypes.c_int),
            'loom_set_secret': ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p], ctypes.c_int),
            'loom_create_conversation': ([ctypes.c_void_p, ctypes.c_char_p], ctypes.c_void_p),
            'loom_get_message': ([ctypes.c_void_p, ctypes.c_char_p], ctypes.c_void_p),
            'loom_get_messages_ex': ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int], ctypes.c_void_p),
            'loom_edit_message': ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p], ctypes.c_void_p),
            'loom_chat_ex': ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p], ctypes.c_void_p),
        }
        for name, (args, result) in signatures.items():
            getattr(self.lib, name).argtypes = args
            getattr(self.lib, name).restype = result
        self.calls = []
        self.provider_status = 200
        fixture = self

        class Provider(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                number = len(fixture.calls) + 1
                status = fixture.provider_status
                fixture.calls.append({'path': self.path, 'body': body, 'status': status})
                answer = encoded({'choices': [{'message': {'content': f'LOCAL_REPLY_{number}'}}]}
                                 if status == 200 else {'error': {'message': 'synthetic local failure'}})
                self.send_response(status)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(answer)))
                self.end_headers()
                self.wfile.write(answer)

            def log_message(self, *args):
                pass

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Provider)
        self.addCleanup(self.server.server_close)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.thread.join, 5)
        self.addCleanup(self.server.shutdown)
        self.directory = tempfile.mkdtemp(prefix='loom-w1-revision-http-')
        print(f'W1 retained synthetic HTTP database: {self.directory}')
        self.ctx = None
        self.addCleanup(self.close_runtime)
        self.open_runtime()
        self.conv = self.read(self.lib.loom_create_conversation(self.ctx, b'W1 revisions local transport'))['id']

    def read(self, pointer):
        self.assertTrue(pointer)
        try:
            return json.loads(ctypes.string_at(pointer))
        finally:
            self.lib.loom_free_string(pointer)

    def close_runtime(self):
        if self.ctx:
            self.lib.loom_shutdown(self.ctx)
            self.ctx = None

    def open_runtime(self):
        self.assertIsNone(self.ctx)
        self.ctx = self.lib.loom_init_ex(encoded({'data_dir': self.directory, 'start_workers': False}), None)
        self.assertTrue(self.ctx)
        self.assertEqual(self.lib.loom_set_config_json(self.ctx, encoded({
            'base_url': f'http://127.0.0.1:{self.server.server_port}',
            'default_model': 'synthetic/local', 'auto_title': False,
            'semantic_analysis': False, 'system_prompt': '', 'stream': False,
        })), 0)
        self.assertEqual(self.lib.loom_set_secret(self.ctx, b'api_key', b'local-test-placeholder'), 0)

    def rows(self):
        return self.read(self.lib.loom_get_messages_ex(self.ctx, self.conv.encode(), 1))

    def row(self, identity):
        return self.read(self.lib.loom_get_message(self.ctx, identity.encode()))

    def chat(self, message, product=None, **options):
        request = {'conv_id': self.conv, 'message': message, 'stream': False,
                   'include_memory': False, 'include_graph_memory': False}
        if product is not None:
            request.update(active_task_spec=product[0], active_task_bindings=product[1])
        request.update(options)
        return self.read(self.lib.loom_chat_ex(self.ctx, encoded(request), None, None))

    def seed(self, text):
        result = self.chat(text)
        self.assertNotIn('error', result)
        return [self.row(result['user_message_id']), self.row(result['assistant_message_id'])]

    def product(self, identity, version, previous, sources, instruction):
        events = [f'event-{identity}-{i}' for i in range(len(sources))]
        spec = {
            'schema': 'loom.active_task_spec/1', 'product_ref': {'kind': 'product', 'id': identity},
            'goal_id': 'report', 'knowledge_run': None,
            'scope': {'conversation_id': self.conv, 'branch_id': 'native:active', 'task_id': 'report'},
            'version': version, 'previous_product_ref': previous,
            'known_at': '2026-09-30T20:00:00Z', 'representation': 'derived_product',
            'materializer': {'id': 'manual-http-revision-test', 'version': '1'},
            'history_event_ids': events,
            'source_refs': [{'event_id': event, 'locator': {'source': 'synthetic'},
                             'known_at': '2026-09-30T19:00:00Z', 'quote': source['text']}
                            for event, source in zip(events, sources)],
            'statements': [{'id': 'goal', 'kind': 'goal', 'status': 'active', 'text': instruction,
                            'source_event_ids': events, 'claim_ids': [], 'conditions': [], 'supersedes': []}],
            'compiled_instruction': {'text': 'UNTRUSTED', 'source_map': [
                {'span': {'byte_start': 0, 'byte_len': 9}, 'statement_ids': ['goal']}]},
        }
        bindings = {event: {'message_id': source['id'],
                            'text_sha256': hashlib.sha256(source['text'].encode('utf-8')).hexdigest()}
                    for event, source in zip(events, sources)}
        return spec, bindings

    def accepted(self, message, product, covered, **options):
        before = self.rows()
        before_calls = len(self.calls)
        result = self.chat(message, product, **options)
        self.assertNotIn('error', result)
        self.assertEqual(len(self.calls), before_calls + 1)
        self.assert_outbound(before, message, product, covered)
        if options.get('trace_context', True):
            self.assertEqual(result['context_trace']['messages'], self.calls[-1]['body']['messages'])
            active_covered = {m['id'] for m in before if m['status'] == 'active' and m['id'] in covered}
            self.assertEqual(set(result['context_trace']['replaced_history_message_ids']), active_covered)
        else:
            self.assertNotIn('context_trace', result)
        saved = self.row(result['user_message_id'])
        task = saved['metadata']['active_task']
        self.assertEqual(task['supplied_spec'], product[0])
        self.assertEqual(task['bindings'], product[1])
        self.assertEqual(set(task['history_coverage_message_ids']), set(covered))
        self.assertEqual(len(task['source_messages']), len(product[1]))
        for snapshot in task['source_messages']:
            source = self.row(snapshot['message_id'])
            self.assertEqual(snapshot['text'], source['text'])
            self.assertEqual(snapshot['text_sha256'], hashlib.sha256(source['text'].encode('utf-8')).hexdigest())
        if options.get('trace_context') is False:
            self.assertNotIn('context_trace', saved['metadata'])
        return saved

    def assert_outbound(self, before, message, product, covered):
        expected = [{'role': source['role'], 'content': source['text']}
                    for source in before if source['status'] == 'active' and source['id'] not in covered]
        expected += [
            {'role': 'user', 'content': '[Active task specification; derived from the selected source messages]\n'
                                       + '[goal] ' + product[0]['statements'][0]['text']},
            {'role': 'user', 'content': message},
        ]
        self.assertEqual(self.calls[-1]['path'], '/chat/completions')
        self.assertEqual(self.calls[-1]['body']['messages'], expected)
        self.assertEqual(sum(m['content'] == message for m in expected), 1)
        self.assertNotIn('active_task_spec', self.calls[-1]['body'])

    def rejected(self, message, product):
        before = self.rows()
        before_calls = len(self.calls)
        result = self.chat(message, product)
        self.assertIn('error', result)
        self.assertEqual(result['error']['code'], 'invalid_argument')
        self.assertEqual(len(self.calls), before_calls)
        self.assertEqual(self.rows(), before)
        return result

    def assert_unchanged(self, rows):
        for row in rows:
            self.assertEqual(self.row(row['id']), row)

    def test_selective_v2_v3_latest_replay_and_stale_rejection(self):
        first_sources = self.seed('REJECTED_SOURCE_V1: decorative prose with obsolete instructions.')
        v1 = self.product('http-revision-v1', 1, None, first_sources, 'Write the first concise report.')
        covered = {m['id'] for m in first_sources}
        saved1 = self.accepted('Accept version one.', v1, covered)

        second_sources = self.seed('REFINEMENT_SOURCE_V2: add the audit appendix.')
        v2 = self.product('http-revision-v2', 2, v1[0]['product_ref'], second_sources,
                          'Write the report with the audit appendix.')
        covered.update(m['id'] for m in second_sources)
        saved2 = self.accepted('Accept version two.', v2, covered)

        third_sources = self.seed('REFINEMENT_SOURCE_V3: retain the identifier Żółć-17.')
        v3 = self.product('http-revision-v3', 3, v2[0]['product_ref'], third_sources,
                          'Write the final report with appendix and Żółć-17.')
        covered.update(m['id'] for m in third_sources)
        saved3 = self.accepted('Accept version three.', v3, covered)
        ancestry = saved3['metadata']['active_task']['inherited_source_messages']
        self.assertEqual({entry['product_ref']['id'] for entry in ancestry},
                         {'http-revision-v1', 'http-revision-v2'})
        self.assertEqual({entry['source_message']['message_id'] for entry in ancestry},
                         {m['id'] for m in first_sources + second_sources})
        self.rejected('Cannot replay stale root.', v1)
        self.rejected('Cannot replay stale middle revision.', v2)
        self.accepted('Replay the exact latest product.', v3, covered)
        self.assert_unchanged(first_sources + second_sources + third_sources + [saved1, saved2, saved3])
        self.assertEqual(len(self.calls), 7)
        print('W1 revisions transport: 7 local calls, 0 remote; 2 stale rejections add 0 calls/rows.')

    def test_native_source_edit_requires_explicit_new_id_hash_and_quote_rebinding(self):
        sources = self.seed('SOURCE_BEFORE_NATIVE_EDIT: original wording.')
        v1 = self.product('http-edit-v1', 1, None, sources, 'Write the original report.')
        saved1 = self.accepted('Accept original source version.', v1, {m['id'] for m in sources})
        edited = self.read(self.lib.loom_edit_message(
            self.ctx, sources[0]['id'].encode(), 'SOURCE_AFTER_NATIVE_EDIT: corrected wording Żółć.'.encode('utf-8')))
        self.assertNotIn('error', edited)
        self.assertNotEqual(edited['id'], sources[0]['id'])
        self.assertEqual(edited['version_group_id'], sources[0]['version_group_id'])
        original_after_edit = self.row(sources[0]['id'])
        self.assertEqual(original_after_edit['status'], 'version')
        self.assertEqual(original_after_edit['text'], sources[0]['text'])
        # The old sources are inherited. An unbound edited sibling must reject,
        # although this successor's own binding and quote are independently valid.
        unrelated_source = self.row(saved1['id'])
        unbound = self.product('http-edit-v2', 2, v1[0]['product_ref'], [unrelated_source],
                               'Write the revised report.')
        rejected = self.rejected('Do not hide an unbound edited version.', unbound)
        self.assertIn('active edited version', rejected['error']['message'])
        rebound = self.product('http-edit-v2', 2, v1[0]['product_ref'], [edited],
                               'Write the explicitly corrected report.')
        stale_hash = copy.deepcopy(rebound)
        stale_hash[1][rebound[0]['history_event_ids'][0]]['text_sha256'] = hashlib.sha256(
            sources[0]['text'].encode('utf-8')).hexdigest()
        self.rejected('Old hash cannot authorize new bytes.', stale_hash)
        covered = {m['id'] for m in sources} | {edited['id']}
        saved2 = self.accepted('Accept explicitly rebound new native version.', rebound, covered)
        self.assertEqual(saved1['metadata']['active_task']['source_messages'][0]['text'], sources[0]['text'])
        self.assertEqual(saved2['metadata']['active_task']['source_messages'][0]['text'], edited['text'])
        self.assert_unchanged([original_after_edit, sources[1], edited, saved1])
        self.assertEqual(len(self.calls), 3)
        print('W1 native edit transport: 3 local calls, 0 remote; 2 rejected rebinding attempts add 0 calls/rows.')

    def test_trace_off_provider_failure_retains_product_across_runtime_restart(self):
        sources = self.seed('FAILED_REQUEST_SOURCE: obsolete decorative variant.')
        v1 = self.product('http-restart-v1', 1, None, sources, 'Use the first accepted instruction.')
        covered = {m['id'] for m in sources}
        before = self.rows()
        self.provider_status = 503
        failed = self.chat('The first transport attempt fails.', v1, trace_context=False)
        self.assertIn('error', failed)
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(self.calls[-1]['status'], 503)
        self.assert_outbound(before, 'The first transport attempt fails.', v1, covered)
        new_rows = [m for m in self.rows() if m['id'] not in {old['id'] for old in before}]
        self.assertEqual(len(new_rows), 1)
        failed_user = new_rows[0]
        self.assertEqual(failed_user['role'], 'user')
        self.assertEqual(failed_user['metadata']['active_task']['supplied_spec'], v1[0])
        self.assertEqual(failed_user['metadata']['active_task']['bindings'], v1[1])
        self.assertNotIn('context_trace', failed_user['metadata'])
        persisted = self.rows()

        self.close_runtime()
        self.open_runtime()
        self.assertEqual(self.rows(), persisted)
        self.assert_unchanged(sources + [failed_user])
        competing_root = self.product('http-restart-competing-root', 1, None, sources,
                                      'Attempt to forget the retained root.')
        self.rejected('A provider error is not acceptance rollback.', competing_root)
        self.provider_status = 200
        self.accepted('Retry the retained latest product after restart.', v1, covered, trace_context=False)
        new_sources = self.seed('POST_RESTART_REFINEMENT: produce a corrected report.')
        v2 = self.product('http-restart-v2', 2, v1[0]['product_ref'], new_sources,
                          'Use the corrected final instruction after restart.')
        covered.update(m['id'] for m in new_sources)
        saved2 = self.accepted('Accept the successor after restart.', v2, covered)
        self.rejected('The failed root is now stale.', v1)
        self.assert_unchanged(sources + new_sources + [failed_user, saved2])
        self.assertEqual(len(self.calls), 5)
        self.assertEqual(sum(call['status'] == 503 for call in self.calls), 1)
        print('W1 restart transport: 5 local calls (1 HTTP 503), 0 remote; 2 version rejections add 0 calls/rows.')


if __name__ == '__main__':
    unittest.main(verbosity=2)
