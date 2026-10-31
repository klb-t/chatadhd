#!/usr/bin/env python3
"""G1: syntetyczny multipart → źródło/geneza/graf → istniejący kontekst → atrapa.

Wymaga własnego, świeżego LOOM_SERVER_BIN; nie uruchamia kompilacji ani modeli.
"""
import hashlib
import unittest
import requests
from test_server_guests import solve_pow

from server_flow_harness import HttpFlowCase


class GraphSourceParts(HttpFlowCase):
    def post(self, path, body):
        r = self.client.post(self.srv.base + path, json=body, timeout=30)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertNotIn('error', r.json())
        return r.json()

    def upload(self, text, name='synthetic.md', kind='markdown', key='synthetic:document', previous=None):
        raw = text.encode('utf-8')
        r = self.client.post(self.srv.base + '/api/client/blob/upload',
                             files={'file': (name, raw, 'text/plain')}, timeout=30)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.json()['hash'], hashlib.sha256(raw).hexdigest())
        request = {'hash': r.json()['hash'], 'name': name, 'format': kind,
                   'source_key': key, 'language': 'pl'}
        if previous:
            request['previous_source_id'] = previous
        return self.post('/api/client/blob/text', request), request

    def read(self, node):
        return self.post('/api/client/blob/text/read', {'node_id': node})

    def original(self, result, text):
        r = self.client.get(self.srv.base + '/api/client/blob/' + result['source_sha256'] + '/raw', timeout=15)
        self.assertEqual(r.status_code, 200)
        self.assertEqual(r.content, text.encode('utf-8'))

    def test_occurrence_context_and_reopen(self):
        text = '# Nagłówek\nZażółć gęślą jaźń.\nZażółć gęślą jaźń.\nNIEWYBRANY WIERSZ\n'
        result, request = self.upload(text)
        ids = result['occurrence_nodes']
        self.assertEqual(len(ids), 4)
        first, second = self.read(ids[1]), self.read(ids[2])
        self.assertNotEqual(ids[1], ids[2])
        self.assertEqual(first['text'], second['text'])
        self.assertNotEqual(first['provenance']['byte_range'], second['provenance']['byte_range'])
        self.assertEqual(first['source']['id'], result['source_id'])
        nodes = self.client.get(self.srv.base + '/api/graph/nodes',
                                params={'kind': 'text_span'}, timeout=15)
        self.assertEqual(nodes.status_code, 200, nodes.text)
        self.assertTrue(set(ids).issubset({n['id'] for n in nodes.json()}))
        edges = self.client.get(self.srv.base + '/api/graph/edges',
                                params={'node_id': result['version_node']}, timeout=15)
        self.assertEqual(edges.status_code, 200, edges.text)
        self.assertTrue(any(e['src'] == ids[2] and e['dst'] == result['version_node'] for e in edges.json()))
        self.assertTrue(any(e['dst'] == result['source_node'] for e in edges.json()))
        self.assertTrue(any(e['dst'] == result['method_node'] for e in edges.json()))
        self.assertEqual(self.post('/api/client/blob/text', request), result)
        self.assertEqual(''.join(self.read(node)['text'] for node in ids), text)
        self.assertEqual(self.provider.calls, [])
        self.patch({'context_execution': {'unified': {
            'enabled': True, 'include_knowledge': False, 'legacy_seed_ids': [ids[2]],
            'legacy_relation_hops': 0}}})
        self.success('Przytocz wskazany fragment grafu.')  # bez attachments i bez tekstu źródła w pytaniu
        sent = self.provider.calls[-1]['request']['messages']
        system = '\n'.join(self.text(m) for m in sent if m['role'] == 'system')
        self.assertIn('Zażółć gęślą jaźń.', system)
        self.assertNotIn('NIEWYBRANY WIERSZ', system)
        self.assertEqual(system.count('Zażółć gęślą jaźń.'), 1)
        self.assertEqual(self.history()[0]['attachments'], [])
        self.restart()
        self.assertEqual(self.read(ids[2]), second)
        self.assertEqual(self.post('/api/client/blob/text', request), result)
        self.original(result, text)
        self.success('Przytocz fragment po ponownym otwarciu.')
        sent = self.provider.calls[-1]['request']['messages']
        self.assertTrue(any('Zażółć gęślą jaźń.' in self.text(m) for m in sent if m['role'] == 'system'))

    def test_code_version_and_independent_source(self):
        old = '# Łódź\ndef żółw():\n    return "gęś"\n'
        new = '# Łódź\ndef żółw():\n    return "żuraw"\n'
        a, _ = self.upload(old, 'synthetic.py', 'code', 'synthetic:code')
        b, _ = self.upload(new, 'synthetic.py', 'code', 'synthetic:code', a['source_id'])
        c, _ = self.upload(old, 'synthetic.py', 'code', 'synthetic:other-code')
        self.assertNotEqual(a['source_id'], b['source_id'])
        self.assertNotEqual(a['source_id'], c['source_id'])
        self.assertEqual(a['source_sha256'], c['source_sha256'])
        self.assertEqual(a['observations'], [])
        self.assertEqual(a['reason'], 'code_text_only_no_ast')
        self.restart()
        for result, content in [(a, old), (b, new), (c, old)]:
            self.original(result, content)
            self.assertEqual(''.join(self.read(n)['text'] for n in result['occurrence_nodes']), content)
        self.assertEqual(self.read(b['occurrence_nodes'][0])['source']['metadata']['previous_source_id'], a['source_id'])
        self.assertEqual(self.provider.calls, [])

    def test_session_isolation_and_cross_site_refusal(self):
        result, request = self.upload('Prywatny syntetyczny żółw.\n')
        routes = [('/api/client/blob/text', request),
                  ('/api/client/blob/text/read', {'node_id': result['occurrence_nodes'][0]})]
        with requests.Session() as other:
            other.trust_env = False
            for route, body in routes:
                denied = other.post(self.srv.base + route, json=body, timeout=15)
                self.assertEqual(denied.status_code, 401, denied.text)
                cross = self.client.post(self.srv.base + route, json=body,
                                         headers={'Origin': 'https://synthetic.invalid'}, timeout=15)
                self.assertEqual(cross.status_code, 403, cross.text)
            created = other.post(self.srv.base + '/api/auth/guest',
                                 json=solve_pow(self.srv.base, other), timeout=30)
            self.assertEqual(created.status_code, 201, created.text)
            for route, body in routes:
                absent = other.post(self.srv.base + route, json=body, timeout=15)
                self.assertEqual(absent.status_code, 404, absent.text)
        self.assertEqual(self.post('/api/client/blob/text', request), result)
        self.original(result, 'Prywatny syntetyczny żółw.\n')
        self.assertEqual(self.provider.calls, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
