#!/usr/bin/env python3
"""Short installed-artifact acceptance; synthetic loopback providers only.

Run via deploy/staging-smoke.py, with checkout denied by Landlock. The copied
harness is test support, never part of the immutable runtime package.
"""
import hashlib
import json
import os
from pathlib import Path
import unittest

from server_flow_harness import HttpFlowCase
from test_server_guest_credentials import CredentialBoundary
from test_server_pdf_flow import PdfFlow, DOCUMENT
from test_server_docx_flow import DocxFlow, fixtures
from test_docx_package import PackageFlow
from test_server_url_share_positive import UrlHttpPositive
from test_server_security import sse_events

LOOM = Path(__file__).resolve().parents[2]


class StagingFlow(HttpFlowCase):
    def test_locale_dialog_restart_and_error(self):
        self.patch({'ui_language': 'pl'})
        first = self.success('Syntetyczny dialog: Zażółć gęślą jaźń.')
        rows = self.history()
        self.assertEqual(rows[-1]['text'], first['text'])
        self.restart()
        response = self.client.get(self.srv.base + '/api/config', timeout=15)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()['ui_language'], 'pl')
        self.assertEqual(self.history(), rows)
        self.success('Kontynuuj dialog po restarcie.')
        self.assertIn('Zażółć gęślą jaźń', self.text(self.wire_messages()[0]))
        good = self.history()
        self.provider.fail = True
        events = self.send('Syntetyczny błąd dostawcy.')
        self.assertTrue(any(e.get('type') == 'error' for e in events), events)
        self.assertFalse(any(e.get('type') == 'done' for e in events), events)
        failed = self.history()
        self.assertEqual(failed[:-1], good)
        self.assertEqual(failed[-1]['role'], 'user')
        self.assertEqual(failed[-1]['text'], 'Syntetyczny błąd dostawcy.')

    def test_screenshot_local_ocr_context_and_reopen(self):
        path = LOOM / 'tests/fixtures/local_ocr/synthetic-pl.png'
        original = path.read_bytes()
        expected = (path.parent / 'expected.txt').read_text().strip()
        response = self.client.post(self.srv.base + '/api/import',
            files={'file': (path.name, original, 'image/png')}, timeout=60)
        self.assertEqual(response.status_code, 200, response.text)
        events = sse_events(response)
        self.assertFalse([e for e in events if e.get('type') == 'error'], events)
        done = next(e for e in events if e.get('type') == 'done')
        self.assertEqual(done['format'], 'screenshot')
        self.assertEqual(self.provider.calls, [])
        self.conv = done['conversations'][0]['id']
        rows = self.history()
        self.assertEqual(rows[0]['text'], expected)
        self.success('Odczytaj termin spotkania z tekstu.')
        self.assertIn(expected, self.text(self.wire_messages()[0]))
        self.assertNotIn('image_url', json.dumps(self.provider.calls))
        rows = self.history()
        self.restart()
        self.assertEqual(self.history(), rows)
        self.success('Odczytaj ponownie zapisany termin.')
        self.assertIn(expected, self.text(self.wire_messages()[0]))

    def test_url_private_address_refused_before_fetch(self):
        descriptor = {'schema': 'loom.url_import_request/1', 'mode': 'share',
                      'url': self.provider.base + '/must-not-fetch'}
        response = self.client.post(self.srv.base + '/api/import', files={
            'file': ('source.loomurl', json.dumps(descriptor), 'application/json')}, timeout=30)
        self.assertEqual(response.status_code, 200, response.text)
        done = next(e for e in sse_events(response) if e.get('type') == 'done')
        self.assertEqual(done['status'], 'denied')
        self.assertEqual(done['reason'], 'address_not_public')
        self.assertEqual(done['conversations'], [])
        self.assertEqual(done['llm_calls'], 0)
        self.assertEqual(self.provider.calls, [])


class StagingPdf(PdfFlow):
    def test_two_polish_pages_warning_original_and_restart(self):
        ref, digest, original = self.upload('unicode-compressed.pdf')
        done = self.success('Odczytaj dwie polskie strony.', [ref])
        self.assertEqual(done['attachment_notes'][0]['pages'], 2)
        self.assert_document(self.wire_messages()[0])
        self.assert_original(digest, original)
        ref2, digest2, original2 = self.upload('mixed-text-blank.pdf')
        done2 = self.success('Odczytaj dokument ze stroną bez tekstu.', [ref2])
        note, = done2['attachment_notes']
        self.assertEqual(note['pages_without_text'], [3])
        self.assertIn('strony bez odczytywalnego tekstu: 3', note['message'])
        self.assertIn(DOCUMENT, self.text(self.wire_messages()[-1]))
        self.assert_saved_notes(done2)
        self.assert_original(digest, original)
        self.assert_original(digest2, original2)
        self.success('Wróć do obu dokumentów po restarcie.')
        wire = self.wire_messages()
        self.assert_document(wire[0])
        self.assertIn(note['message'], self.text(wire[2]))


class StagingDocx(DocxFlow):
    def setUp(self):
        HttpFlowCase.setUp(self)  # No explicit interpreter/helper override.

    def test_default_docx_context_warnings_original_restart_and_refusal(self):
        ref, digest, original = self.upload()
        done = self.success('Odczytaj polskie akapity i tabelę.', [ref])
        note, = done['attachment_notes']
        self.assertEqual(note['status'], 'partial')
        self.assertIn('docx_formatting_and_pagination_omitted', note['warnings'])
        self.assertEqual(note['source']['sha256'], digest)
        self.assertIn(fixtures.EXPECTED, self.text(self.wire_messages()[0]))
        rows = self.history()
        self.assertEqual(rows[0]['metadata']['attachment_notes'], [note])
        self.restart()
        self.assertEqual(self.history(), rows)
        self.assert_original(digest, original)
        self.success('Wróć do tabeli po restarcie.')
        self.assertIn(fixtures.EXPECTED, self.text(self.wire_messages()[0]))
        self.refuse(b'synthetic broken ZIP')


def main():
    for path in json.loads(os.environ['LOOM_DENIED_SOURCE_PATHS']):
        try:
            Path(path).read_bytes()
        except PermissionError:
            print('PASS checkout denied: ' + path, flush=True)
        else:
            raise AssertionError('checkout is readable: ' + path)
    package = Path(os.environ['LOOM_PACKAGE_ROOT'])
    runtime = package / 'share/loom/docx_basic'
    manifest = json.loads((runtime / 'runtime.json').read_text())
    assert manifest['reader_sha256'] == hashlib.sha256((runtime / 'reader.py').read_bytes()).hexdigest()
    cases = [
        StagingFlow('test_locale_dialog_restart_and_error'),
        CredentialBoundary('test_endpoint_change_without_byok_never_sends_operator_a_to_b'),
        StagingPdf('test_two_polish_pages_warning_original_and_restart'),
        StagingFlow('test_screenshot_local_ocr_context_and_reopen'),
        StagingDocx('test_default_docx_context_warnings_original_restart_and_refusal'),
        PackageFlow('test_shared_library_default'),
        UrlHttpPositive('test_default_helper_share_page_reopen_bytes_and_no_models'),
        StagingFlow('test_url_private_address_refused_before_fetch'),
    ]
    result = unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(cases))
    summary = {'run': result.testsRun, 'failures': len(result.failures),
               'errors': len(result.errors), 'skipped': len(result.skipped),
               'success': result.wasSuccessful(), 'scope': 'synthetic installed-artifact backend smoke; no GUI/real model/deploy'}
    Path('smoke-result.json').write_text(json.dumps(summary, indent=2) + '\n')
    return not result.wasSuccessful()


if __name__ == '__main__':
    raise SystemExit(main())
