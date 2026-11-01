"""Synthetic mechanism fixtures; no provider/data/native backend dependencies."""
import ast
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from loom.tools.python_ast_basic import PythonAstError, parse, source_parts_request, validate
from loom.tools.python_ast_basic.reader import canonical, profile

HERE = Path(__file__).parent
ROOT = HERE.parents[2]
READER = ROOT / 'loom/tools/python_ast_basic/reader.py'


class PythonAstTests(unittest.TestCase):
    def read(self, raw, **kwargs):
        return parse(raw, source_key='synthetic:PL', name='żółw.py', **kwargs)

    def assert_ranges(self, result, raw):
        declarations = result['declarations']
        self.assertEqual(len(declarations), len(result['units']))
        self.assertEqual(len(declarations), len(result['observations']))
        self.assertEqual(len(declarations), len(result['provenance']))
        self.assertEqual(len({u['id'] for u in result['units']}), len(declarations))
        for ordinal, (d, u, o, p) in enumerate(zip(declarations, result['units'],
                                                  result['observations'], result['provenance'])):
            start, stop = d['byte_range']
            self.assertEqual(raw[start:stop].decode('utf-8'), o['text'])
            self.assertEqual(hashlib.sha256(raw[start:stop]).hexdigest(), d['text_sha256'])
            self.assertEqual(u['locator'], o['locator'])
            self.assertEqual(u['locator'], p['locator'])
            self.assertEqual(o['unit'], u['id'])
            self.assertEqual(o['ordinal'], ordinal)
            self.assertEqual(u['bytes'], stop - start)
            self.assertEqual(o['attrs']['evidence_class'], 'observed')
            self.assertEqual(o['attrs']['structural_evidence_class'], 'derived')
            self.assertNotIn('confidence', o['attrs'])
            self.assertEqual(u['locator']['byte_start'], start)
            self.assertEqual(u['locator']['byte_len'], stop - start)
            self.assertEqual(u['locator']['source'], result['source']['id'])
            self.assertEqual(p['evidence_class'], 'derived')
            if d['parent'] is not None:
                self.assertLess(d['parent'], ordinal)
                ps, pe = declarations[d['parent']]['byte_range']
                self.assertLessEqual(ps, start)
                self.assertLessEqual(stop, pe)

    def test_polish_nested_scopes_and_exact_ends(self):
        raw = (HERE / 'fixtures/zagniezdzenia.py').read_bytes()
        result = self.read(raw)
        ds = result['declarations']
        self.assertEqual([(d['kind'], d['name'], d['parent']) for d in ds], [
            ('module', 'żółw.py', None), ('class', 'Żółw', 0),
            ('async_function', 'powtórka', 1), ('function', 'wnętrze', 2),
            ('function', 'zwykła', 1), ('class', 'Wewnętrzna', 4),
            ('function', 'powtórka', 0), ('async_function', 'warunkowa', 0)])
        self.assertEqual(ds[1]['line_range'], [4, 21])
        self.assertEqual(ds[2]['line_range'], [6, 16])
        self.assertEqual(ds[3]['line_range'], [14, 15])
        self.assertEqual(result['observations'][1]['text'],
                         raw[raw.index(b'@dekorator'):raw.index(b'\n\ndef ')].decode())
        self.assertTrue(result['observations'][2]['text'].startswith('@ statyczny('))
        self.assertTrue(result['observations'][2]['text'].endswith('return wnętrze()'))
        self.assertEqual(raw[ds[2]['ast_range'][0]:ds[2]['ast_range'][0] + 9], b'async def')
        self.assertEqual(result['original_sha256'], hashlib.sha256(raw).hexdigest())
        self.assert_ranges(result, raw)

    def test_unicode_end_column_and_same_name_in_comment(self):
        raw = '# def gęślą(): fałszywy\ndef gęślą(): return "Łódź" # koniec\n'.encode()
        result = self.read(raw)
        d = result['declarations'][1]
        expected = 'def gęślą(): return "Łódź"'.encode()
        self.assertEqual(d['byte_range'], [raw.index(b'\ndef') + 1, raw.index(b' # koniec')])
        self.assertEqual(result['observations'][1]['text'].encode(), expected)
        self.assertEqual(result['units'][1]['attrs']['ast_end_col_offset'], len(expected))
        self.assert_ranges(result, raw)

    def test_crlf_bare_cr_and_bom_preserve_raw_coordinates(self):
        for newline in (b'\n', b'\r\n', b'\r'):
            for bom in (b'', b'\xef\xbb\xbf'):
                with self.subTest(newline=newline, bom=bom):
                    raw = bom + newline.join([b'# coding: utf-8', '# łódź'.encode(),
                                               b'def f():', '    return "żółć"'.encode(), b''])
                    result = self.read(raw)
                    self.assertEqual(result['declarations'][1]['byte_range'],
                                     [raw.index(b'def'), len(raw) - len(newline)])
                    self.assertEqual(result['declarations'][1]['line_range'], [3, 4])
                    self.assertEqual(result['observations'][0]['text'].encode(), raw)
                    self.assert_ranges(result, raw)

    def test_parenthesized_stacked_multiline_decorators(self):
        raw = '@(\n    dekorator("żółć")\n)\n@ inny\ndef f():\n    return 1\n'.encode()
        result = self.read(raw)
        self.assertEqual(result['declarations'][1]['line_range'], [1, 6])
        self.assertEqual(result['declarations'][1]['byte_range'], [0, len(raw)-1])
        self.assertTrue(result['observations'][1]['text'].startswith('@(\n'))
        self.assert_ranges(result, raw)

    def test_async_declarations_nested_in_async_and_class(self):
        raw = b'async def outer():\n    async def inner():\n        class C:\n            async def method(self): pass\n'
        result = self.read(raw)
        self.assertEqual([d['kind'] for d in result['declarations']],
                         ['module', 'async_function', 'async_function', 'class', 'async_function'])
        self.assertEqual([d['parent'] for d in result['declarations']], [None, 0, 1, 2, 3])
        self.assert_ranges(result, raw)

    def test_same_names_distinct_branches_and_scopes(self):
        raw = b'if True:\n    def f(): pass\nelse:\n    def f(): pass\ndef outer():\n    def f(): pass\n'
        result = self.read(raw)
        self.assertEqual([d['parent'] for d in result['declarations']], [None, 0, 0, 0, 3])
        self.assert_ranges(result, raw)

    def test_formfeed_and_unicode_separator_are_not_physical_lines(self):
        raw = 's = "a\u2028b"\n\fdef f(): return "ż"\n'.encode()
        result = self.read(raw)
        self.assertEqual(result['declarations'][1]['line_range'], [2, 2])
        self.assertEqual(result['declarations'][1]['byte_range'][0], raw.index(b'def'))
        self.assert_ranges(result, raw)

    def test_empty_and_single_declaration_module_ids_are_distinct(self):
        for raw in (b'', b'# comment\n', b'def f(): pass', b'async def f(): pass'):
            result = self.read(raw)
            self.assertEqual(result['declarations'][0]['byte_range'], [0, len(raw)])
            self.assertEqual(result['units'][0]['locator']['line'], None)
            self.assert_ranges(result, raw)

    def test_no_import_exec_evaluation_or_decorator_execution(self):
        with tempfile.TemporaryDirectory(prefix='python-ast-test-') as temporary:
            sentinel = Path(temporary) / 'must-not-exist'
            raw = (f'import nonexistent_module_that_must_not_be_imported\n'
                   f'open({str(sentinel)!r}, "w").write("BAD")\n'
                   f'@(_ for _ in ()).throw(RuntimeError("BAD"))\n'
                   f'def f(x=open({str(sentinel)!r}, "w")): pass\n'
                   f'raise RuntimeError("BAD")\n').encode()
            result = self.read(raw)
            self.assertEqual(len(result['declarations']), 2)
            self.assertFalse(sentinel.exists())

    def test_syntax_error_refuses_all_partial_output_without_source_text(self):
        for raw in ((HERE / 'fixtures/odmowa.py.txt').read_bytes(),
                    b'def valid(): pass\ndef broken(:\n', b'\x00', b'  def f(): pass\n'):
            with self.subTest(raw=raw):
                with self.assertRaises(PythonAstError) as caught:
                    self.read(raw)
                error = caught.exception
                self.assertEqual(error.code, 'python_ast_syntax_error')
                self.assertNotIn('prywatny-fixture-marker', canonical(error.to_json()))
                self.assertEqual(set(error.to_json()['error']), {'code', 'locator'})
                if error.locator:
                    self.assertLessEqual(error.locator['byte_start'], len(raw))

    def test_syntax_error_unicode_character_column_converts_to_bytes(self):
        raw = 'żółć = )\n'.encode()
        with self.assertRaises(PythonAstError) as caught:
            self.read(raw)
        self.assertEqual(caught.exception.locator['byte_start'], raw.index(b')'))
        self.assertEqual(caught.exception.locator['line'], 1)

    def test_invalid_utf8_cookie_and_original_hash_refused(self):
        for raw, code in ((b'#\xff', 'python_ast_invalid_utf8'),
                          (b'# coding: latin-1\ndef f(): pass', 'python_ast_non_utf8_cookie'),
                          (b'# coding: unknown-encoding\n', 'python_ast_syntax_error')):
            with self.assertRaises(PythonAstError) as caught:
                self.read(raw)
            self.assertEqual(caught.exception.code, code)
        with self.assertRaisesRegex(PythonAstError, 'original_hash_mismatch'):
            self.read(b'def f(): pass', expected_sha256='0'*64)

    def test_resource_failure_is_not_a_partial_success(self):
        with patch('loom.tools.python_ast_basic.reader.ast.parse', side_effect=RecursionError):
            with self.assertRaisesRegex(PythonAstError, 'resource_unavailable'):
                self.read(b'')

    def test_all_limits_configurable_and_zero_disables(self):
        raw = b'def f(): pass\n'
        for key in profile()['limits']:
            settings = profile()
            settings['limits'][key] = 1
            with self.subTest(key=key), self.assertRaisesRegex(PythonAstError, 'limit_' + key):
                self.read(raw, settings=settings)
            settings['limits'][key] = 0
            self.assertEqual(len(self.read(raw, settings=settings)['declarations']), 2)
        for value in (True, -1, '1', None):
            settings = profile()
            settings['limits']['max_input_bytes'] = value
            with self.assertRaisesRegex(PythonAstError, 'configuration_invalid'):
                self.read(raw, settings=settings)

    def test_method_pins_profile_interpreter_script_and_repeat_is_identical(self):
        raw = b'def f(): pass'
        first = self.read(raw)
        self.assertEqual(first, self.read(raw))
        self.assertEqual(validate(first, raw), first)
        self.assertEqual(first['method']['reader_sha256'], hashlib.sha256(READER.read_bytes()).hexdigest())
        changed = profile()
        changed['kinds']['function'] = 'owner_function_kind'
        second = self.read(raw, settings=changed)
        self.assertEqual(first['source'], second['source'])
        self.assertNotEqual(first['method_sha256'], second['method_sha256'])
        self.assertEqual(second['units'][1]['kind'], 'owner_function_kind')

    def test_receipt_tampering_and_changed_original_refused_before_hook(self):
        raw = b'def f(): pass\n'
        first = self.read(raw)
        for mutate in (lambda r: r['declarations'][1].update(parent=False),
                       lambda r: r['declarations'][1].update(parent=1),
                       lambda r: r['observations'][1].update(text='fabricated'),
                       lambda r: r['declarations'][1].update(byte_range=[0, 1]),
                       lambda r: r['method'].update(reader_sha256='0'*64),
                       lambda r: r['units'][1].update(kind='fabricated')):
            result = deepcopy(first)
            mutate(result)
            with self.assertRaises(PythonAstError):
                source_parts_request(result, raw)
        with self.assertRaisesRegex(PythonAstError, 'original_hash_mismatch'):
            validate(first, raw + b'# changed')

    def test_hook_request_uses_existing_source_parts_and_frozen_receipt(self):
        raw = b'def f(): pass'
        result = self.read(raw)
        request = source_parts_request(result, raw)
        self.assertEqual(request['format'], 'python_ast')
        self.assertEqual(request['hash'], hashlib.sha256(raw).hexdigest())
        self.assertEqual(request['python_ast_receipt'], result)
        self.assertEqual(request['pack']['source_parts']['formats']['python_ast']['level'], 'python_declaration')
        request['python_ast_receipt']['declarations'].clear()
        self.assertEqual(len(result['declarations']), 2)

    def test_native_observation_contract_codec_accepts_actual_records(self):
        # Pure existing graph contract validation; no native library/provider.
        from loom.tools.structure.agentic_graph_v1.packet import validate_source
        result = self.read((HERE / 'fixtures/zagniezdzenia.py').read_bytes())
        for o in result['observations']:
            validate_source({'observation': o, 'known_at': None,
                             'text_sha256': hashlib.sha256(o['text'].encode()).hexdigest()})

    def test_cli_success_and_refusal(self):
        args = [sys.executable, '-B', str(READER)]
        for filename, exitcode in (('zagniezdzenia.py', 0), ('odmowa.py.txt', 2)):
            completed = subprocess.run(args + [str(HERE / 'fixtures' / filename),
                                      '--source-key', 'synthetic:CLI', '--name', filename],
                                       capture_output=True, text=True, check=False, timeout=10)
            self.assertEqual(completed.returncode, exitcode, completed.stderr)
            result = json.loads(completed.stdout)
            self.assertEqual('error' in result, bool(exitcode))
            self.assertEqual(completed.stderr, '')


if __name__ == '__main__':
    unittest.main()
