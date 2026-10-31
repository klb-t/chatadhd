#!/usr/bin/env python3
"""P0: małe syntetyczne pliki UTF-8, faktyczne bajty i treść w kontekście HTTP.

Nie testuje formatów biurowych ani wykonania kodu; odpowiedzi są atrapami.
"""
import hashlib
import unittest

from server_flow_harness import HttpFlowCase


class TextFlow(HttpFlowCase):
    def check_text(self, name, mime, content):
        original = content.encode('utf-8')
        digest = hashlib.sha256(original).hexdigest()
        response = self.client.post(self.srv.base + '/api/client/blob/upload',
                                    files={'file': (name, original, mime)}, timeout=30)
        self.assertEqual(response.status_code, 200, response.text)
        self.assertEqual(response.json()['hash'], digest)
        self.assertEqual(response.json()['size'], len(original))
        ref = 'blob:' + digest + '/' + name
        question = 'Przytocz treść załączonego pliku jako tekst.'
        self.success(question, [ref])
        first = self.wire_messages()
        self.assertEqual(len(first), 1)
        self.assertEqual(first[0]['role'], 'user')
        self.assertEqual(first[0]['content'][0]['text'], question)
        # Sprawdzamy dokładny Unicode/wielowierszową treść, nie nazwę pliku.
        self.assertIn(content, self.text(first[0]))
        self.assertEqual(self.text(first[0]).count(content), 1)
        self.success('Wróć do treści poprzedniego pliku.')
        second = self.wire_messages()
        self.assertEqual([m['role'] for m in second], ['user', 'assistant', 'user'])
        self.assertEqual(second[0], first[0])
        rows = self.history()
        self.assertEqual(rows[0]['attachments'], [ref])
        self.assertEqual(rows[2]['attachments'], [])
        self.assertEqual(self.history(), rows)  # reopen
        self.restart()
        self.assertEqual(self.history(), rows)
        self.success('Co zapisano w pliku przed ponownym otwarciem?')
        third = self.wire_messages()
        self.assertEqual([m['role'] for m in third], ['user', 'assistant', 'user', 'assistant', 'user'])
        self.assertEqual(third[:3], second)
        self.assertIn(content, self.text(third[0]))
        raw = self.client.get(self.srv.base + f'/api/client/blob/{digest}/raw', timeout=15)
        self.assertEqual(raw.status_code, 200)
        self.assertEqual(raw.content, original)
        self.assertEqual(len(self.provider.calls), 3)

    def test_utf8_txt(self):
        self.check_text('notatka.txt', 'text/plain',
                        'Zażółć gęślą jaźń.\nDrugi wiersz: Łódź, żółw, ąćęłńóśźż.\n')

    def test_markdown(self):
        self.check_text('opis.md', 'text/markdown',
                        '# Przegląd syntetyczny\n\n- Żółw: **zielony**.\n- Liczba: 37.\n')

    def test_source_code_as_text(self):
        self.check_text('przyklad.py', 'text/x-python',
                        '# Syntetyczny kod; nie uruchamiać.\ndef powitanie():\n    return "Zażółć gęślą jaźń"\n')


if __name__ == '__main__':
    unittest.main(verbosity=2)
