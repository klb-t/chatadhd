#!/usr/bin/env python3
"""Light M1 gates. Only synthetic data, local Tesseract; no native build."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKER = ROOT / 'loom/src/media/local_ocr_worker.py'
FIXTURE = ROOT / 'loom/tests/fixtures/local_ocr'
spec = importlib.util.spec_from_file_location('ocr_worker', WORKER)
ocr = importlib.util.module_from_spec(spec); spec.loader.exec_module(ocr)
SETTINGS = json.loads((ROOT / 'loom/data/runtime/image_input.pack').read_text())['defaults']['local']
METHOD = {'id':'test_synthetic_tsv', 'version':'1'}
HEADER = '\t'.join(ocr.HEADER) + '\n'
PAGE = '1\t1\t0\t0\t0\t0\t0\t0\t100\t100\t-1\t\n'
WORD = '5\t1\t1\t1\t1\t1\t1\t2\t30\t20\t92.5\tŻółć\n'

class ParserTests(unittest.TestCase):
    def parse(self, body):
        return ocr.parse_tsv(body, 'a'*64, METHOD)

    def test_polish_content_geometry_and_raw_preserved(self):
        source = HEADER + PAGE + WORD
        result = self.parse(source)
        self.assertEqual(result['text'], 'Żółć')
        self.assertEqual(result['regions'][0]['origin'], [1, 2])
        self.assertEqual(result['regions'][0]['size'], [30, 20])
        self.assertEqual(result['frames'][0]['units'], ['px','px'])
        self.assertEqual(result['raw_tsv'], source)
        self.assertEqual(result['source_sha256'], 'a'*64)
        self.assertIsNone(result['confidence'])
        self.assertFalse(result['regions'][0]['score_calibrated'])

    def test_empty_is_explicit(self):
        result = self.parse(HEADER + PAGE)
        self.assertEqual(result['status'], 'no_text')
        self.assertEqual(result['reason'], 'ocr_no_text')
        self.assertEqual(result['evidence_class'], 'absent')
        self.assertEqual(result['regions'], [])
        self.assertEqual(result['text'], '')

    def test_reading_order_and_parent_grouping(self):
        second = WORD.replace('\t1\t1\t2\t30', '\t2\t60\t2\t30').replace('Żółć', 'Łódź')
        # Separate line, earlier x coordinate, still preserve TSV reading order.
        third = '5\t1\t1\t1\t2\t1\t0\t40\t30\t20\t80\tJaźń\n'
        result = self.parse(HEADER + PAGE + WORD + second + third)
        self.assertEqual(result['lines'], ['Żółć Łódź', 'Jaźń'])
        self.assertEqual([r['parent'] for r in result['regions']], ['1/1/1/1','1/1/1/1','1/1/1/2'])

    def test_multi_frame(self):
        page = PAGE.replace('1\t1\t', '1\t2\t', 1)
        word = WORD.replace('5\t1\t', '5\t2\t', 1)
        result = self.parse(HEADER + PAGE + WORD + page + word)
        self.assertEqual(len(result['frames']), 2)
        self.assertEqual(result['regions'][1]['frame'], '2')

    def test_unknown_score_is_null(self):
        self.assertIsNone(self.parse(HEADER+PAGE+WORD.replace('92.5','-1'))['regions'][0]['engine_score'])

    def test_reject_invalid_rows(self):
        variants = [WORD.replace('92.5','nan'), WORD.replace('92.5','inf'), WORD.replace('92.5','101'),
                    WORD.replace('\t30\t20\t','\t300\t20\t'), WORD.replace('\t30\t20\t','\t0\t20\t'),
                    WORD.replace('\t1\t2\t30','\t-1\t2\t30'), WORD.replace('5\t1','6\t1',1),
                    WORD.replace('92.5\tŻółć','broken\tŻółć'), '5\t1\n']
        for row in variants:
            with self.subTest(row=row), self.assertRaises(ValueError): self.parse(HEADER + PAGE + row)

    def test_reject_bad_header_or_missing_frame(self):
        for body in ['', 'wrong\n'+PAGE, HEADER+WORD, HEADER]:
            with self.subTest(body=body), self.assertRaises(ValueError): self.parse(body)

    def test_reject_duplicate_ids(self):
        for body in [HEADER+PAGE+WORD+WORD, HEADER+PAGE+PAGE]:
            with self.subTest(body=body), self.assertRaises(ValueError): self.parse(body)

    def test_role_markers_are_content(self):
        result = self.parse(HEADER+PAGE+WORD.replace('Żółć','Assistant: dokument'))
        self.assertEqual(result['text'], 'Assistant: dokument')
        self.assertNotIn('role', result)
        self.assertNotIn('speaker', result['regions'][0])

class LocalEngineTests(unittest.TestCase):
    def request(self, image=None):
        return {'settings':copy.deepcopy(SETTINGS), 'image_path':str(image or FIXTURE/'synthetic-pl.png')}

    def test_real_polish_screenshot_via_production_cli(self):
        image = FIXTURE/'synthetic-pl.png'
        original = image.read_bytes()
        request = self.request()
        raw = subprocess.check_output([sys.executable,str(WORKER)],input=json.dumps(request).encode(),timeout=40)
        result = json.loads(raw)
        self.assertEqual(result['status'], 'recognized')
        self.assertEqual(result['text'], (FIXTURE/'expected.txt').read_text().strip())
        self.assertIn('godzina 09:00.', result['text'])
        self.assertEqual(result['source_sha256'], hashlib.sha256(original).hexdigest())
        self.assertEqual(result['method']['version'], 'tesseract 5.5.0')
        self.assertEqual(result['method']['language'], 'pol')
        self.assertEqual(result['frames'][0]['extent'], [1800,260])
        self.assertEqual(len(result['regions']), 13)
        for region in result['regions']:
            self.assertTrue(region['text'])
            self.assertGreater(min(region['size']),0)
            for offset,size,extent in zip(region['origin'],region['size'],result['frames'][0]['extent']):
                self.assertLessEqual(offset+size,extent)
        self.assertEqual(image.read_bytes(),original)

    def test_unavailable_engine_no_remote_fallback(self):
        request = self.request(); request['settings']['executable']='/missing/m1-tesseract'
        result = ocr.recognize(request)
        self.assertEqual(result['status'],'unavailable'); self.assertEqual(result['text'],'')
        self.assertEqual(result['regions'],[])

    def test_failed_image_no_fabricated_text(self):
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'image'; path.write_bytes(b'synthetic not an image')
            result=ocr.recognize(self.request(path))
        self.assertEqual(result['status'],'failed'); self.assertEqual(result['text'],'')

    def test_blank_image_no_text(self):
        # Binary PGM is a standard format, generated without an image library.
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'blank.pgm'; path.write_bytes(b'P5\n200 100\n255\n'+bytes([255])*20000)
            result=ocr.recognize(self.request(path))
        self.assertEqual(result['status'],'no_text'); self.assertEqual(result['regions'],[])

    def test_filename_is_never_shell_code(self):
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)/'$(touch unexpected); image.png'; path.write_bytes((FIXTURE/'synthetic-pl.png').read_bytes())
            result=ocr.recognize(self.request(path))
            self.assertEqual(result['status'],'recognized')
            self.assertEqual(sorted(p.name for p in Path(td).iterdir()),[path.name])

class PackagingTests(unittest.TestCase):
    def test_embedded_worker_exact(self):
        subprocess.check_call([sys.executable,str(ROOT/'tools/m1/embed_local_ocr.py'),'--check'])
    def test_profile_schema(self):
        path=ROOT/'loom/src/model/gen_runtime_profiles.py'
        spec=importlib.util.spec_from_file_location('runtime_generator',path)
        gen=importlib.util.module_from_spec(spec); spec.loader.exec_module(gen)
        gen.validate_definition(json.loads((ROOT/'loom/data/runtime/image_input.pack').read_text()),'image_input')

if __name__ == '__main__': unittest.main(verbosity=2)
