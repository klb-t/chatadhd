#!/usr/bin/env python3
"""Rzeczywisty multipart → loom-server → dostawca HTTP → trwała historia PDF.

Odpowiedzi atrapy nie są dowodem rozumowania ani jakości prawdziwego modelu.
"""
import hashlib
from pathlib import Path
import unittest

from server_flow_harness import HttpFlowCase


FIXTURES = Path(__file__).resolve().parents[2] / 'tests/fixtures/documents'
PAGE1 = 'Zażółć gęślą jaźń — PDF PAPUGA. Strona 1.'
PAGE2 = 'Druga strona: Łódź, żółw, ąćęłńóśźż.'
DOCUMENT = PAGE1 + '\f' + PAGE2


class PdfFlow(HttpFlowCase):
    def upload(self, name):
        original = (FIXTURES / name).read_bytes()
        r = self.client.post(self.srv.base + '/api/client/blob/upload',
                             files={'file': (name, original, 'application/pdf')}, timeout=30)
        self.assertEqual(r.status_code, 200, r.text)
        blob = r.json()
        self.assertEqual(blob['size'], len(original))
        self.assertEqual(blob['hash'], hashlib.sha256(original).hexdigest())
        self.assertEqual(blob['mime'], 'application/pdf')
        self.assert_original(blob['hash'], original)
        return 'blob:' + blob['hash'] + '/' + name, blob['hash'], original

    def assert_original(self, digest, original):
        r = self.client.get(self.srv.base + f'/api/client/blob/{digest}/raw', timeout=15)
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual(r.content, original)
        self.assertEqual(hashlib.sha256(r.content).hexdigest(), digest)

    def assert_document(self, message):
        self.assertEqual(message['role'], 'user')
        parts = message['content']
        self.assertIsInstance(parts, list)
        self.assertEqual(sum(DOCUMENT in p.get('text', '') for p in parts), 1)
        self.assertNotIn('%PDF', self.text(message))

    def assert_saved_notes(self, done):
        rows = self.history()
        self.assertEqual(rows[-1]['id'], done['message_id'])
        self.assertEqual(rows[-1]['metadata']['attachment_notes'], done['attachment_notes'])
        # Reopen przez HTTP, a potem restart rzeczywistego procesu na tych samych danych.
        self.assertEqual(self.history(), rows)
        self.restart()
        self.assertEqual(self.history(), rows)
        return rows

    def test_two_pages_followup_reopen_restart_and_original_bytes(self):
        ref, digest, original = self.upload('unicode-compressed.pdf')
        q1 = 'Podaj unikalny tekst drugiej strony załączonego PDF.'
        first = self.success(q1, [ref])
        wire1 = self.wire_messages()
        self.assertEqual(len(wire1), 1)
        self.assert_document(wire1[0])
        self.assertEqual(wire1[0]['content'][0]['text'], q1)
        self.assertEqual(first['attachment_notes'][0]['pages'], 2)
        self.assertEqual(first['attachment_notes'][0]['pages_without_text'], [])
        q2 = 'Powtórz treść drugiej strony poprzedniego pliku.'
        self.success(q2)  # celowo bez attachments
        wire2 = self.wire_messages()
        self.assertEqual([m['role'] for m in wire2], ['user', 'assistant', 'user'])
        self.assertEqual(wire2[0], wire1[0])
        self.assertEqual(self.text(wire2[1]), self.provider.reply(1))
        self.assertEqual(self.text(wire2[2]), q2)
        rows = self.history()
        self.assertEqual([m['role'] for m in rows], ['user', 'assistant', 'user', 'assistant'])
        self.assertEqual(rows[0]['attachments'], [ref])
        self.assertEqual(rows[2]['attachments'], [])
        self.assertEqual(rows[1]['metadata']['attachment_notes'], first['attachment_notes'])
        self.assertEqual(self.history(), rows)
        self.restart()
        self.assertEqual(self.history(), rows)
        self.assert_original(digest, original)
        q3 = 'Wróć do drugiej strony PDF po ponownym otwarciu.'
        self.success(q3)
        wire3 = self.wire_messages()
        self.assertEqual([m['role'] for m in wire3], ['user', 'assistant', 'user', 'assistant', 'user'])
        self.assertEqual(wire3[:3], wire2)
        self.assert_document(wire3[0])
        self.assertEqual(self.text(wire3[3]), self.provider.reply(2))
        self.assertEqual(self.text(wire3[4]), q3)
        self.assertEqual(len(self.provider.calls), 3)
        self.assertEqual(len(self.transcript.read_text(encoding='utf-8').splitlines()), 3)
        self.assert_original(digest, original)

    def test_mixed_pdf_persists_blank_page_number_and_warning(self):
        ref, digest, original = self.upload('mixed-text-blank.pdf')
        done = self.success('Co zawiera załączony dokument?', [ref])
        note, = done['attachment_notes']
        self.assertEqual(note['status'], 'read')
        self.assertEqual(note['pages'], 3)
        self.assertEqual(note['pages_without_text'], [3])
        self.assertIn('strony bez odczytywalnego tekstu: 3', note['message'])
        self.assertIn('skany nie zostały odczytane', note['message'])
        wire = self.wire_messages()[0]
        self.assertIn(note['message'], self.text(wire))
        self.assertIn(DOCUMENT + '\f', self.text(wire))
        self.assert_saved_notes(done)
        self.assert_original(digest, original)
        self.success('Czy była strona bez tekstu?')
        self.assertEqual(self.wire_messages()[0], wire)

    def test_controlled_limit_persists_exact_unicode_counts(self):
        self.patch({'chat_attachment_max_chars': 10})
        ref, digest, original = self.upload('unicode-compressed.pdf')
        done = self.success('Jaki fragment tekstu jest dostępny?', [ref])
        read, cut = done['attachment_notes']
        self.assertEqual(read['status'], 'read')
        self.assertEqual(read['pages'], 2)
        self.assertEqual(cut['status'], 'cut')
        self.assertEqual(cut['original_chars'], 78)
        self.assertEqual(cut['kept_chars'], 10)
        self.assertEqual(len(DOCUMENT), 78)
        self.assertIn('wysłano 10 z 78 znaków', cut['message'])
        wire = self.wire_messages()[0]
        self.assertIn('\n' + DOCUMENT[:10] + '\n', self.text(wire))
        self.assertIn(cut['message'], self.text(wire))
        self.assertNotIn(PAGE2, self.text(wire))
        self.assertNotIn('PAPUGA', self.text(wire))
        self.assert_saved_notes(done)
        self.assert_original(digest, original)
        self.success('Jaki fragment odczytano?')
        self.assertEqual(self.wire_messages()[0], wire)

    def refuse(self, name):
        ref, digest, original = self.upload(name)
        before = self.history()
        self.assertEqual(before, [])
        events = self.send('Jaka jest treść załączonego PDF?', [ref])
        self.assertEqual([e['type'] for e in events], ['error'])
        self.assertIn('nie została dołączona', events[0]['message'])
        self.assertIn(name, events[0]['message'])
        self.assertEqual(self.provider.calls, [])
        self.assertEqual(self.transcript.read_bytes(), b'')
        self.assertEqual(self.history(), before)
        self.restart()
        self.assertEqual(self.history(), before)
        self.assertEqual(self.provider.calls, [])
        self.assert_original(digest, original)

    def test_no_text_refuses_without_provider_or_partial_turn(self):
        self.refuse('no-text.pdf')

    def test_password_refuses_without_provider_or_partial_turn(self):
        self.refuse('password.pdf')


if __name__ == '__main__':
    unittest.main(verbosity=2)
