"""Opt-in upload SourceRecord: real HTTP, synthetic bytes, no model calls."""
import hashlib
import unittest
import requests
from server_flow_harness import HttpFlowCase


class UploadSource(HttpFlowCase):
    raw = 'Zażółć gęślą jaźń.\n'.encode()

    def upload(self, fields=None, client=None, headers=None):
        return (client or self.client).post(
            self.srv.base + '/api/client/blob/upload',
            files={'file': ('żółw.txt', self.raw, 'text/plain')},
            data=fields or {}, headers=headers or {}, timeout=20)

    def sources(self):
        r = self.client.get(self.srv.base + '/api/sources?limit=100', timeout=15)
        self.assertEqual(r.status_code, 200, r.text)
        return r.json()

    def test_durable_identity_retry_and_legacy(self):
        legacy = self.upload()
        self.assertEqual(legacy.status_code, 200, legacy.text)
        self.assertNotIn('source_id', legacy.json())
        self.assertEqual(self.sources(), [])
        fields = {'source_key': 'synthetic:upload-A', 'format': 'text'}
        first = self.upload(fields)
        self.assertEqual(first.status_code, 200, first.text)
        first = first.json()
        self.assertEqual(first['hash'], hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(first['source_key'], fields['source_key'])
        rows = self.sources()
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['id'], first['source_id'])
        self.assertEqual(rows[0]['parser'], 'loom.client_upload')
        self.assertEqual(rows[0]['title'], 'żółw.txt')
        self.assertEqual(self.upload(fields).json(), first)
        self.assertEqual(self.sources(), rows)
        other = self.upload({**fields, 'source_key': 'synthetic:upload-B'}).json()
        self.assertEqual(other['hash'], first['hash'])
        self.assertNotEqual(other['source_id'], first['source_id'])
        self.restart()
        self.assertEqual({s['id'] for s in self.sources()}, {first['source_id'], other['source_id']})
        original = self.client.get(self.srv.base + '/api/client/blob/' + first['hash'] + '/raw', timeout=15)
        self.assertEqual(original.status_code, 200)
        self.assertEqual(original.content, self.raw)
        self.assertEqual(self.provider.calls, [])

    def test_invalid_key_and_route_refusals(self):
        fields = {'source_key': 'synthetic:upload', 'format': 'text'}
        with requests.Session() as anonymous:
            anonymous.trust_env = False
            self.assertEqual(self.upload(fields, client=anonymous).status_code, 401)
        self.assertEqual(self.upload(fields, headers={'Origin': 'https://synthetic.invalid'}).status_code, 403)
        bad = self.upload({**fields, 'source_key': ''})
        self.assertEqual(bad.status_code, 400, bad.text)
        self.assertIn('client_upload_source_key_and_name_required', bad.text)
        self.assertEqual(self.sources(), [])
        self.assertEqual(self.provider.calls, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
