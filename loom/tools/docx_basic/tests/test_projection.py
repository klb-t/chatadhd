"""Focused projection increment. Runs no old reader/HTTP/G1 suites."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from fixtures import W, p, package, parts

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('projection_reader', ROOT / 'reader.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)

# New fixture: repeated Polish values, a break inside one paragraph, omissions,
# two empty paragraphs and a trailing empty paragraph. It is never owner data.
BODY = (p('Żółw powtórzony.') + p('Żółw powtórzony.')
        + '<w:p><w:r><w:t>Wewnątrz</w:t><w:br/><w:t>akapitu.</w:t></w:r></w:p>'
        + p('') + p('')
        + '<w:p><w:pPr><w:outlineLvl w:val="1"/></w:pPr><w:r><w:t>Tytuł</w:t></w:r></w:p>'
        + '<w:p><w:pPr><w:numPr><w:numId w:val="1"/></w:numPr></w:pPr><w:r><w:t>Lista</w:t></w:r></w:p>'
        + '<w:tbl><w:tr><w:tc>' + p('Komórka A') + '</w:tc><w:tc>' + p('Komórka B') + '</w:tc></w:tr></w:tbl>'
        + '<w:p><w:r><w:drawing/></w:r></w:p>' + p('Koniec.') + p(''))


def fixture(variant='base'):
    entries = parts(BODY.replace('Koniec.', 'Zmiana.') if variant == 'text-change' else BODY)
    if variant == 'zip-change':
        entries['word/opaque.bin'] = b'synthetic opaque retained original'
    return package(entries)


class ProjectionTests(unittest.TestCase):
    def setUp(self):
        self.limits = json.loads((ROOT / 'preset.json').read_text())
        self.original = fixture()
        self.result = reader.extract(self.original, self.limits)

    def test_utf8_bytes_tile_derived_blob_only(self):
        e = self.result
        data = e['text'].encode('utf-8')
        self.assertEqual(e['projection']['schema'], 'loom.docx_basic.projection/1')
        self.assertEqual(e['projection']['coordinate'], 'derived_utf8_bytes')
        self.assertEqual(e['projection']['sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(e['projection']['size'], len(data))
        self.assertNotEqual(e['source']['sha256'], e['projection']['sha256'])
        self.assertTrue(self.original.startswith(b'PK'))
        restored, cursor = b'', 0
        for block, span in zip(e['blocks'], e['projection']['ranges'], strict=True):
            begin, end = span['byte_range']
            start, stop = span['text_range']
            self.assertEqual(cursor, begin)
            self.assertEqual(data[start:stop].decode(), block['text'])
            restored += data[begin:end]
            cursor = end
        self.assertEqual(restored, data)
        self.assertEqual(cursor, len(data))

    def test_duplicates_are_distinct_recognized_occurrences(self):
        e = self.result
        self.assertEqual(e['blocks'][0]['text'], e['blocks'][1]['text'])
        self.assertNotEqual(e['blocks'][0]['locator'], e['blocks'][1]['locator'])
        self.assertNotEqual(e['projection']['ranges'][0], e['projection']['ranges'][1])
        data = e['text'].encode()
        a, b = (data[r['byte_range'][0]:r['byte_range'][1]] for r in e['projection']['ranges'][:2])
        self.assertEqual(a, b)

    def test_break_does_not_invent_another_paragraph(self):
        e = self.result
        self.assertEqual(e['blocks'][2]['text'], 'Wewnątrz\nakapitu.')
        begin, end = e['projection']['ranges'][2]['byte_range']
        self.assertEqual(e['text'].encode()[begin:end].decode(), 'Wewnątrz\nakapitu.\n')
        self.assertEqual(e['blocks'][2]['locator'], 'word/document.xml#body/2')

    def test_empty_and_omitted_blocks_remain_located(self):
        e = self.result
        self.assertEqual(len(e['blocks']), 11)
        for i in (3, 4, 8, 10):
            self.assertEqual(e['blocks'][i]['text'], '')
            self.assertEqual(e['blocks'][i]['locator'], f'word/document.xml#body/{i}')
        spans = [tuple(r['byte_range']) for r in e['projection']['ranges']]
        self.assertEqual(len(spans), len(set(spans)))
        self.assertEqual(spans[-1][0], spans[-1][1])
        self.assertIn('docx_images_omitted', e['warnings'])
        self.assertIn('docx_formatting_and_pagination_omitted', e['warnings'])
        self.assertTrue(e['partial'])
        self.assertNotIn('pages', e)

    def test_rendered_prefixes_are_outside_observation_text(self):
        e = self.result
        self.assertEqual(e['blocks'][5]['kind'], 'heading')
        self.assertEqual(e['blocks'][6]['kind'], 'list_item')
        self.assertEqual(e['blocks'][7]['kind'], 'table')
        data = e['text'].encode()
        for i, marker in ((5, b'## '), (6, b'- ')):
            r = e['projection']['ranges'][i]
            self.assertEqual(data[r['byte_range'][0]:r['text_range'][0]], marker)
        self.assertEqual(e['blocks'][7]['rows'], [['Komórka A', 'Komórka B']])

    def test_same_projection_can_have_different_original_versions(self):
        e2 = reader.extract(fixture('zip-change'), self.limits)
        self.assertNotEqual(e2['source']['sha256'], self.result['source']['sha256'])
        self.assertEqual(e2['projection']['sha256'], self.result['projection']['sha256'])
        self.assertEqual(e2['projection']['ranges'], self.result['projection']['ranges'])
        # The backend must include original/method identity, not just text hash.

    def test_text_revision_changes_projection_hash(self):
        e2 = reader.extract(fixture('text-change'), self.limits)
        self.assertNotEqual(e2['source']['sha256'], self.result['source']['sha256'])
        self.assertNotEqual(e2['projection']['sha256'], self.result['projection']['sha256'])
        self.assertEqual(e2['blocks'][0], self.result['blocks'][0])

    def test_replay_is_exact_and_original_is_unchanged(self):
        self.assertEqual(reader.extract(self.original, self.limits), self.result)
        self.assertEqual(self.original, fixture())
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'synthetic.docx'
            path.write_bytes(self.original)
            run = subprocess.run([sys.executable, '-I', '-B', '-S', str(ROOT / 'reader.py'),
                                  str(path), json.dumps(self.limits)], check=True, capture_output=True)
            self.assertEqual(json.loads(run.stdout), self.result)
            self.assertEqual(path.read_bytes(), self.original)

    def test_existing_limits_refuse_before_any_projection(self):
        for key in ('max_input_bytes', 'max_text_chars'):
            with self.subTest(key=key), self.assertRaises(reader.DocxError) as caught:
                reader.extract(self.original, {**self.limits, key: 1})
            self.assertEqual(caught.exception.code, 'docx_limit_' + key)

    def test_changed_invalid_source_is_not_reused_as_old_projection(self):
        with self.assertRaises(reader.DocxError):
            reader.extract(self.original[:-8], self.limits)
        with self.assertRaises(reader.DocxError) as caught:
            reader.extract(package(parts(p(''))), self.limits)
        self.assertEqual(caught.exception.code, 'document_no_text')


if __name__ == '__main__':
    unittest.main()
