#!/usr/bin/env python3
"""New DOCX -> G1 acceptance only; run by integrator with a fresh own server.

No services start on import. The inherited harness uses synthetic local state
and a recording loopback provider. This worker only syntax-checks this file.
"""
import copy
import hashlib
import json
from pathlib import Path
import unittest

from server_flow_harness import HttpFlowCase
from test_server_guests import overlay

LOOM = Path(__file__).resolve().parents[2]
FIXTURES = LOOM / 'tests/fixtures/docx_parts'
MIME = 'application/vnd.openxmlformats-officedocument.wordprocessingml.document'


class DocxSourceParts(HttpFlowCase):
    def setUp(self):
        super().setUp()
        roots = list((self.root / 'guests').glob('*/config.json'))
        self.assertEqual(len(roots), 1)
        additions = json.loads((LOOM / 'tools/docx_basic/chat_context_additions.json').read_text())
        self.reader = copy.deepcopy(additions['reader'])
        # Use package:docx_basic from the shipped preset: staged server and helper together.
        self.assertEqual(self.reader['python'], 'package:docx_basic')
        self.assertEqual(self.reader['script'], 'package:docx_basic')
        overlay(roots[0].parent, 'chat_context', {'attachments': {'document_readers': [self.reader]}})
        self.restart()

    def post(self, route, body):
        r = self.client.post(self.srv.base + route, json=body, timeout=30)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertNotIn('error', r.json())
        return r.json()

    def upload(self, name='base', previous=None):
        raw = (FIXTURES / (name + '.docx')).read_bytes()
        r = self.client.post(self.srv.base + '/api/client/blob/upload',
                             files={'file': ('synthetic.docx', raw, MIME)}, timeout=30)
        self.assertEqual(r.status_code, 200, r.text)
        digest = hashlib.sha256(raw).hexdigest()
        self.assertEqual(r.json()['hash'], digest)
        request = {'hash': digest, 'source_key': 'synthetic:docx-graph-http',
                   'name': 'synthetic.docx', 'format': 'docx', 'language': 'pl'}
        if previous is not None:
            request['previous_source_id'] = previous
        return request, raw

    def read(self, node):
        return self.post('/api/client/blob/text/read', {'node_id': node})

    def raw(self, digest, expected):
        r = self.client.get(self.srv.base + f'/api/client/blob/{digest}/raw', timeout=15)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.content, expected)

    def test_explicit_bridge_graph_selected_context_reopen(self):
        request, original = self.upload()
        before = self.client.get(self.srv.base + '/api/graph/nodes', params={'kind': 'paragraph'}, timeout=15)
        self.assertEqual(before.status_code, 200, before.text)
        self.assertEqual(before.json(), [])  # upload alone is not materialization
        result = self.post('/api/client/blob/text', request)
        ids = result['occurrence_nodes']
        self.assertEqual(len(ids), 11)
        self.assertEqual(len(set(ids)), 11)
        self.assertEqual(result['coordinate'], 'derived_utf8_bytes')
        self.assertEqual(result['derivation']['original_sha256'], request['hash'])
        self.assertNotEqual(result['source_sha256'], request['hash'])
        self.assertEqual(self.read(ids[0])['text'], self.read(ids[1])['text'])
        selected = self.read(ids[2])
        self.assertEqual(selected['text'], 'Wewnątrz\nakapitu.\n')
        self.assertEqual(selected['provenance']['unit']['attrs']['original_locator'], 'word/document.xml#body/2')
        edges = self.client.get(self.srv.base + '/api/graph/edges',
                                params={'node_id': result['source_node']}, timeout=15)
        self.assertEqual(edges.status_code, 200, edges.text)
        self.assertTrue(any(e['dst'] == result['original_source_node'] for e in edges.json()))
        self.assertEqual(self.provider.calls, [])
        self.patch({'context_execution': {'unified': {
            'enabled': True, 'include_knowledge': False,
            'legacy_seed_ids': [ids[2]], 'legacy_relation_hops': 0}}})
        self.success('Przytocz wybrany akapit.')
        messages = self.provider.calls[-1]['request']['messages']
        system = '\n'.join(self.text(m) for m in messages if m['role'] == 'system')
        self.assertIn('Wewnątrz\nakapitu.', system)
        self.assertNotIn('Żółw powtórzony.', system)
        self.assertNotIn('Komórka A', system)
        self.assertEqual(self.history()[0]['attachments'], [])
        self.restart()
        self.assertEqual(self.read(ids[2]), selected)
        self.assertEqual(self.post('/api/client/blob/text', request), result)
        self.raw(request['hash'], original)
        self.raw(result['source_sha256'], json.loads((FIXTURES / 'base.json').read_text())['text'].encode())
        self.assertEqual(len(self.provider.calls), 1)
        config = self.client.get(self.srv.base + '/api/config', timeout=15)
        self.assertEqual(config.status_code, 200)
        self.assertEqual(config.json()['context_execution']['unified']['legacy_seed_ids'], [ids[2]])
        self.success('Przytocz wybrany fragment po ponownym otwarciu.')
        after = self.provider.calls[-1]['request']['messages']
        system_after = '\n'.join(self.text(m) for m in after if m['role'] == 'system')
        self.assertIn('Wewnątrz\nakapitu.', system_after)
        self.assertNotIn('Żółw powtórzony.', system_after)
        self.assertNotIn('Komórka A', system_after)
        self.assertEqual(len(self.provider.calls), 2)

    def test_same_text_new_original_version_and_wrong_predecessor(self):
        request, original = self.upload()
        a = self.post('/api/client/blob/text', request)
        next_request, next_original = self.upload('zip-change', a['source_id'])
        b = self.post('/api/client/blob/text', next_request)
        self.assertEqual(a['source_sha256'], b['source_sha256'])
        self.assertNotEqual(a['source_id'], b['source_id'])
        self.assertNotEqual(a['occurrence_nodes'][0], b['occurrence_nodes'][0])
        bad = {**next_request, 'previous_source_id': a['derivation']['original_source_id']}
        denied = self.client.post(self.srv.base + '/api/client/blob/text', json=bad, timeout=30)
        self.assertNotEqual(denied.status_code, 200, denied.text)
        self.assertEqual(self.post('/api/client/blob/text', next_request), b)
        self.raw(request['hash'], original)
        self.raw(next_request['hash'], next_original)
        self.assertEqual(self.provider.calls, [])


if __name__ == '__main__':
    unittest.main()
