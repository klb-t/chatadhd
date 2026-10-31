#!/usr/bin/env python3
"""M3 acceptance for the INTEGRATOR, after integration_hooks.patch + own build.

Not run by the isolated worker. Uses the existing real multipart/blob/chat
pipeline and a loopback provider. No external API or private document.
"""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import unittest

from server_flow_harness import HttpFlowCase
from test_server_guests import overlay

DOCX = Path(__file__).resolve().parents[2] / 'tools/docx_basic'
spec = importlib.util.spec_from_file_location('m3_docx_fixtures', DOCX / 'tests/fixtures.py')
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
MIME = 'application/vnd.openxmlformats-officedocument.wordprocessingml.document'


class DocxFlow(HttpFlowCase):
    def setUp(self):
        super().setUp()
        roots = list((self.root / 'guests').glob('*/config.json'))
        self.assertEqual(len(roots), 1)
        self.guest = roots[0].parent
        self.additions = json.loads((DOCX / 'chat_context_additions.json').read_text())
        self.reader = copy.deepcopy(self.additions['reader'])
        self.reader.update(python=sys.executable, script=str(DOCX / 'reader.py'))
        self.set_reader()

    def set_reader(self, **updates):
        self.reader.update(updates)
        overlay(self.guest, 'chat_context', {'attachments': {
            'document_readers': [self.reader], **self.additions['messages'],
            'docx_warnings': self.additions['warnings']}})
        self.restart()

    def upload(self, data=None, name='źródło.docx'):
        original = fixtures.package() if data is None else data
        before = len(self.provider.calls)
        r = self.client.post(self.srv.base + '/api/client/blob/upload',
                             files={'file': (name, original, MIME)}, timeout=30)
        self.assertEqual(r.status_code, 200, r.text)
        blob = r.json()
        self.assertEqual(blob['size'], len(original))
        self.assertEqual(blob['hash'], hashlib.sha256(original).hexdigest())
        self.assertEqual(len(self.provider.calls), before)  # extraction/upload never calls a model
        self.assert_original(blob['hash'], original)
        return 'blob:' + blob['hash'] + '/' + name, blob['hash'], original

    def assert_original(self, digest, original):
        r = self.client.get(self.srv.base + f'/api/client/blob/{digest}/raw', timeout=15)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.content, original)
        self.assertEqual(hashlib.sha256(r.content).hexdigest(), digest)

    def sources(self, digest):
        r = self.client.get(self.srv.base + '/api/sources', timeout=15)
        self.assertEqual(r.status_code, 200, r.text)
        return [s for s in r.json() if s['blob_hash'] == digest]

    def test_upload_context_source_repeat_followup_reopen_restart_original(self):
        ref, digest, original = self.upload()
        again, again_digest, _ = self.upload()
        self.assertEqual((again, again_digest), (ref, digest))
        self.assertEqual(self.provider.calls, [])
        done = self.success('Odczytaj polskie akapity i tabelę.', [ref])
        note, = done['attachment_notes']
        self.assertEqual(note['status'], 'partial')
        self.assertTrue(note['partial'])
        self.assertNotIn('pages', note)
        self.assertEqual(note['source']['sha256'], digest)
        self.assertEqual(note['source']['size'], len(original))
        self.assertIn('docx_formatting_and_pagination_omitted', note['warnings'])
        self.assertIn('docx_list_labels_not_reconstructed', note['warnings'])
        wire = self.wire_messages()
        self.assertEqual(len(wire), 1)
        self.assertEqual(sum(fixtures.EXPECTED in p.get('text', '') for p in wire[0]['content']), 1)
        self.assertIn(note['message'], self.text(wire[0]))
        self.assertNotIn('PK\x03\x04', self.text(wire[0]))
        source, = self.sources(digest)
        self.assertEqual(source['id'], note['source_id'])
        self.assertEqual(source['format'], 'docx')
        self.assertEqual(source['size'], len(original))
        self.assertEqual(source['uri'], 'blob:' + digest)
        rows = self.history()
        self.assertEqual(rows[0]['attachments'], [ref])
        self.assertEqual(rows[0]['metadata']['attachment_notes'], [note])
        self.assertEqual(rows[1]['metadata']['attachment_notes'], [note])
        self.success('Co było po tabeli?')
        self.assertEqual(self.wire_messages()[0], wire[0])
        rows = self.history()
        self.assertEqual(self.history(), rows)  # reopen
        self.restart()
        self.assertEqual(self.history(), rows)
        self.assert_original(digest, original)
        self.assertEqual(self.sources(digest), [source])
        self.success('Wróć do tabeli po restarcie.')
        self.assertEqual(self.wire_messages()[0], wire[0])
        self.assertEqual(len(self.provider.calls), 3)
        self.assertTrue(all(c['path'] == '/chat/completions' for c in self.provider.calls))
        self.assertEqual(len(self.transcript.read_text().splitlines()), 3)
        self.assertEqual(self.sources(digest), [source])
        self.assert_original(digest, original)

    def refuse(self, data, name='błędny.docx'):
        ref, digest, original = self.upload(data, name)
        before = self.history()
        calls = len(self.provider.calls)
        events = self.send('Odczytaj załącznik.', [ref])
        self.assertEqual([e['type'] for e in events], ['error'], events)
        self.assertIn(name, events[0]['message'])
        self.assertEqual(len(self.provider.calls), calls)
        self.assertEqual(self.history(), before)
        self.assertEqual(self.sources(digest), [])
        self.restart()
        self.assertEqual(self.history(), before)
        self.assert_original(digest, original)

    def test_malformed_empty_encrypted_traversal_xxe_refuse_before_provider(self):
        hostile = fixtures.parts()
        hostile['../outside.xml'] = '<x/>'
        xxe = fixtures.parts()
        xxe['word/comments.xml'] = '<!DOCTYPE x [<!ENTITY e SYSTEM "file:///must-not-read">]><x>&e;</x>'
        for data in (b'', b'broken', fixtures.package()[:-10], fixtures.package(fixtures.parts('')),
                     bytes.fromhex('d0cf11e0a1b11ae1') + b'encrypted', fixtures.package(hostile), fixtures.package(xxe)):
            with self.subTest(sha256=hashlib.sha256(data).hexdigest()): self.refuse(data)
        self.assertEqual(self.provider.calls, [])
        self.assertEqual(self.transcript.read_bytes(), b'')
        self.assertFalse((self.root / 'outside.xml').exists())

    def test_missing_parser_is_error_and_original_survives(self):
        self.set_reader(python=str(self.root / 'missing-python'))
        self.refuse(fixtures.package())
        self.set_reader(python=sys.executable, script=str(self.root / 'missing-reader.py'))
        self.refuse(fixtures.package())

    def test_expansion_limit_and_output_limit_refuse(self):
        self.set_reader(limits={**self.reader['limits'], 'max_total_uncompressed_bytes': 100})
        self.refuse(fixtures.package())
        self.set_reader(limits=self.additions['reader']['limits'], max_output_bytes=32)
        self.refuse(fixtures.package())

    def test_docm_and_other_extensions_are_not_enabled(self):
        for name in ('legacy.doc', 'sheet.xlsx', 'slides.pptx', 'open.odt', 'macro.docm'):
            self.refuse(fixtures.package(), name)
        macro = fixtures.parts()
        macro['word/vbaProject.bin'] = b'synthetic, never executable'
        self.refuse(fixtures.package(macro))

    def test_extracted_secret_refused_before_provider_and_survives_reopen(self):
        # Integrator: an invented marker occurs only inside OOXML, not the prompt.
        marker = 'SYNTHETIC_DOCX_PRIVATE_MARKER_731'
        overlay(self.guest, 'chat_secrets', {'patterns': [{
            'id': 'synthetic_docx_marker', 'regex': marker, 'flags': '',
            'label': {'pl': 'syntetyczny znacznik', 'en': 'synthetic marker'}}]})
        self.patch({'secrets_in_chat': 'never'})
        self.restart()
        ref, digest, original = self.upload(fixtures.package(fixtures.parts(fixtures.p(marker))))
        before = self.history()
        for restart in (False, True):
            if restart: self.restart()
            events = self.send('Odczytaj treść.', [ref])
            pending = [e['secret_pending'] for e in events if 'secret_pending' in e]
            self.assertEqual(len(pending), 1, events)
            self.assertEqual(pending[0]['status'], 'refused')
            self.assertEqual(self.provider.calls, [])
            self.assertEqual(self.history(), before)
            self.assert_original(digest, original)

    def test_external_relation_is_not_fetched_and_partial_survives_restart(self):
        entries = fixtures.parts()
        entries['word/_rels/document.xml.rels'] = (
            '<Relationships xmlns="' + fixtures.R + '"><Relationship Id="external" Type="hyperlink" '
            'Target="' + self.provider.base + '/MUST_NOT_FETCH" TargetMode="External"/></Relationships>')
        ref, digest, original = self.upload(fixtures.package(entries))
        done = self.success('Odczytaj treść bez pobierania odnośników.', [ref])
        note, = done['attachment_notes']
        self.assertIn('docx_external_relationships_ignored', note['warnings'])
        self.assertEqual([c['path'] for c in self.provider.calls], ['/chat/completions'])
        self.assertNotIn('MUST_NOT_FETCH', self.text(self.wire_messages()[0]))
        before = self.history()
        self.restart()
        self.assertEqual(self.history(), before)
        self.assert_original(digest, original)


if __name__ == '__main__': unittest.main(verbosity=2)
