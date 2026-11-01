import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest
import warnings
import zipfile
import zlib

from fixtures import C, O, R, S, package, parts
from loom.tools.xlsx_basic import XlsxError, read, read_cell, read_range
from loom.tools.xlsx_basic.import_hook import prepare_source_parts, register_with_resource_graph
from loom.tools.xlsx_basic.reader import settings


class ReaderTest(unittest.TestCase):
    def setUp(self):
        self.data = package()

    def refuse(self, data, code, **kwargs):
        with self.assertRaises(XlsxError) as caught:
            read(data, **kwargs)
        self.assertEqual(caught.exception.code, code)

    def test_workbook_relationships_order_and_original(self):
        result = read(self.data)
        self.assertEqual(result['source'], {'sha256': hashlib.sha256(self.data).hexdigest(),
                                           'size': len(self.data), 'format': 'xlsx'})
        self.assertEqual([s['name'] for s in result['sheets']], ['Zażółć', 'Pusty'])
        self.assertEqual([s['sheet_id'] for s in result['sheets']], ['4', '8'])
        self.assertEqual(result['sheets'][1]['cells'], [])
        self.assertEqual(result['sheets'][1]['state'], 'hidden')
        self.assertEqual(self.data, package())

    def test_checked_in_synthetic_fixture_and_golden(self):
        fixture = Path(__file__).with_name('table.xlsx')
        data = fixture.read_bytes()
        golden = json.loads(fixture.with_name('expected.json').read_text())
        self.assertEqual(data, self.data)
        self.assertEqual(hashlib.sha256(data).hexdigest(), golden['original_sha256'])
        result = read(data)
        self.assertEqual([s['name'] for s in result['sheets']], golden['sheet_names'])
        for ref in ('A2', 'B2', 'B3'):
            self.assertEqual(read_cell(data, 'Zażółć', ref)['sheets'][0]['cells'][0]['text'], golden[ref])
        self.assertEqual(read_cell(data, 'Zażółć', 'C3')['sheets'][0]['cells'][0]['status'], golden['C3_status'])
        self.assertEqual(read_cell(data, 'Zażółć', 'A5')['sheets'][0]['cells'][0]['code'], golden['A5_code'])
        self.assertEqual(fixture.read_bytes(), data)

    def test_strings_numeric_precision_and_boolean(self):
        cells = {c['ref']: c for c in read(self.data)['sheets'][0]['cells']}
        self.assertEqual(cells['A1']['text'], 'Nazwa')
        self.assertEqual(cells['A2']['text'], 'żółw')
        self.assertEqual(cells['B1']['text'], 'Ilość')
        self.assertEqual(cells['A3']['text'], ' a &\nβ')
        self.assertEqual(cells['B2']['value'], '9007199254740993')
        self.assertEqual(cells['B2']['style_index'], '1')
        self.assertIs(cells['C2']['value'], True)
        self.assertEqual(cells['A2']['string_origin']['member'], 'xl/strings.xml')

    def test_cell_range_absent_is_not_observed_blank(self):
        result = read_range(self.data, 'Zażółć', '$A$2:$C$3')
        cells = result['sheets'][0]['cells']
        self.assertEqual([c['ref'] for c in cells], ['A2', 'B2', 'C2', 'A3', 'B3', 'C3'])
        self.assertEqual(cells[-2]['status'], 'observed')
        self.assertEqual(cells[-2]['text'], '')
        self.assertEqual(cells[-1]['status'], 'absent')
        self.assertIsNone(cells[-1]['value'])
        self.assertEqual(result['selection']['range'], 'A2:C3')
        self.assertEqual(cells[0]['locator']['original_sha256'], hashlib.sha256(self.data).hexdigest())
        self.assertEqual(cells[0]['locator']['sheet_id'], '4')
        self.assertEqual(cells[0]['locator']['member'], 'xl/worksheets/table.xml')
        self.assertIn("c[@r='A2']", cells[0]['locator']['xml_path'])
        self.assertNotIn('byte_start', cells[0]['locator'])
        single = read_cell(self.data, 'Zażółć', 'B2')
        self.assertEqual(single['sheets'][0]['cells'][0]['value'], '9007199254740993')

    def test_formula_and_cached_values_never_observed(self):
        for markup in ('<f>1+1</f>', '<f t="shared" si="0"/>', '<f t="array" ref="A5:B5">1+1</f>'):
            p = parts(); p['xl/worksheets/table.xml'] = p['xl/worksheets/table.xml'].replace(
                '<f>HYPERLINK("https://invalid.example/", "never")</f>', markup)
            cell = read_cell(package(p), 'Zażółć', 'A5')['sheets'][0]['cells'][0]
            self.assertEqual(cell['status'], 'unsupported')
            self.assertEqual(cell['code'], 'xlsx_formula_unsupported')
            self.assertIsNone(cell['text'])
            self.assertIsNone(cell['value'])
            self.assertNotIn('123456789', json.dumps(cell))

    def test_strict_namespaces(self):
        p = {name: value.replace(S, 'http://purl.oclc.org/ooxml/spreadsheetml/main').replace(
            O, 'http://purl.oclc.org/ooxml/officeDocument/relationships') for name, value in parts().items()}
        self.assertEqual(read_cell(package(p), 'Zażółć', 'A2')['sheets'][0]['cells'][0]['text'], 'żółw')

    def test_wrong_hash_and_invalid_selection(self):
        self.refuse(self.data, 'xlsx_original_hash_mismatch', expected_sha256='0'*64)
        self.refuse(self.data, 'xlsx_sheet_not_found', sheet='none')
        self.refuse(self.data, 'xlsx_sheet_required', selected_range='A1')
        for selection in ('B2:A1', 'A0', 'XFE1', 'A1048577', 'A1:B2:C3', 'Sheet!A1', '', 'A1:'):
            with self.subTest(selection=selection):
                self.refuse(self.data, 'xlsx_invalid_range', sheet='Zażółć', selected_range=selection)

    def test_limits_and_configuration(self):
        for key, value in (('max_input_bytes', 1), ('max_total_uncompressed_bytes', 1),
                           ('max_entry_bytes', 1), ('max_entries', 1), ('max_ratio', 1),
                           ('max_xml_depth', 1), ('max_xml_nodes', 1), ('max_text_chars', 1),
                           ('max_sheets', 1), ('max_cells', 1), ('max_output_bytes', 1)):
            with self.subTest(key=key):
                limits = settings(); limits[key] = value
                self.refuse(self.data, 'xlsx_limit_' + key, limits=limits)
        limits = settings(); limits['max_range_cells'] = 4
        self.refuse(self.data, 'xlsx_limit_max_range_cells', limits=limits,
                    sheet='Zażółć', selected_range='A1:XFD1048576')
        for value in (-1, True, '1'):
            limits = settings(); limits['max_cells'] = value
            self.refuse(self.data, 'xlsx_configuration_invalid', limits=limits)
        limits = settings(); limits['read_chunk_bytes'] = 0
        self.refuse(self.data, 'xlsx_configuration_invalid', limits=limits)
        limits = settings(); limits.update({key: 0 for key in limits if key.startswith('max_')})
        self.assertEqual(len(read(self.data, limits=limits)['sheets']), 2)

    def test_invalid_zip_and_crc(self):
        self.refuse(b'not zip', 'xlsx_invalid_container')
        self.refuse(self.data[:-1], 'xlsx_invalid_container')
        raw = bytearray(package(compression=zipfile.ZIP_STORED))
        at = raw.find(b'Nazwa'); raw[at] ^= 1
        self.refuse(bytes(raw), 'xlsx_invalid_container')

    def test_path_traversal_alias_duplicates_and_symlinks(self):
        for name in ('../escape', '/absolute', 'xl/../../escape', 'xl\\bad', 'xl/%2e%2e/escape'):
            self.refuse(package(extra=[(name, 'x')]), 'xlsx_unsafe_path')
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', UserWarning)
            self.refuse(package(extra=[('xl/book.xml', 'x')]), 'xlsx_duplicate_or_unsafe_part')
        self.refuse(package(extra=[('xl/%62ook.xml', 'x')]), 'xlsx_duplicate_or_unsafe_part')
        stream = io.BytesIO(self.data)
        with zipfile.ZipFile(stream, 'a') as archive:
            info = zipfile.ZipInfo('link'); info.external_attr = (0o120777 << 16)
            archive.writestr(info, '/etc/passwd')
        self.refuse(stream.getvalue(), 'xlsx_unsafe_path')

    def test_encryption_and_unsupported_compression(self):
        raw = bytearray(self.data)
        central = raw.find(b'PK\x01\x02')
        struct.pack_into('<H', raw, central+8, 1)
        self.refuse(bytes(raw), 'xlsx_encrypted')
        raw = bytearray(self.data)
        struct.pack_into('<H', raw, central+10, zipfile.ZIP_BZIP2)
        self.refuse(bytes(raw), 'xlsx_compression_unsupported')

    def test_forged_inflated_size_and_prefix_crc_refused(self):
        raw = bytearray(package(extra=[('payload.bin', 'x' * 32768)]))
        stream = io.BytesIO(raw)
        with zipfile.ZipFile(stream) as archive:
            local = archive.getinfo('payload.bin').header_offset
        at = raw.find(b'PK\x01\x02')
        while raw[at+46:at+46+len('payload.bin')] != b'payload.bin':
            n, x, c = struct.unpack_from('<HHH', raw, at+28)
            at += 46 + n + x + c
        crc = zlib.crc32(b'x')
        struct.pack_into('<I', raw, at+16, crc)
        struct.pack_into('<I', raw, at+24, 1)
        struct.pack_into('<I', raw, local+14, crc)
        struct.pack_into('<I', raw, local+22, 1)
        self.refuse(bytes(raw), 'xlsx_invalid_container')

    def test_zero_length_cells_and_dates_are_lexical(self):
        p = parts(); p['xl/worksheets/table.xml'] = p['xl/worksheets/table.xml'].replace(
            '<c r="B3"/>', '<c r="B3" t="d"><v>2026-11-01T00:00:00Z</v></c>')
        cell = read_cell(package(p), 'Zażółć', 'B3')['sheets'][0]['cells'][0]
        self.assertEqual(cell['value'], '2026-11-01T00:00:00Z')
        self.assertEqual(cell['kind'], 'd')

    def test_shared_strings_optional_and_empty_sheet(self):
        p = parts()
        p['xl/book.xml'] = p['xl/book.xml'].replace('<sheet name="Zażółć" sheetId="4" r:id="table"/>', '')
        key = 'xl/_rels/book.xml.rels'
        p[key] = p[key].replace(f'<Relationship Id="strings" Type="{O}/sharedStrings" Target="strings.xml"/>', '')
        self.assertEqual(read_range(package(p), 'Pusty', 'A1:B2')['sheets'][0]['cells'][0]['status'], 'absent')

    def test_unknown_cell_markup_not_observed(self):
        p = parts(); p['xl/worksheets/table.xml'] = p['xl/worksheets/table.xml'].replace(
            '<c r="B3"/>', '<c r="B3"><unknown/><v>42</v></c>')
        cell = read_cell(package(p), 'Zażółć', 'B3')['sheets'][0]['cells'][0]
        self.assertEqual(cell['code'], 'xlsx_cell_markup_unsupported')
        self.assertIsNone(cell['text'])

    def test_source_plan_requires_identity_and_rechecks_original(self):
        with self.assertRaisesRegex(XlsxError, 'xlsx_import_identity_required'):
            prepare_source_parts(self.data, expected_sha256='', source_key='key', name='n',
                                 sheet='Zażółć', selected_range='A1')
        with self.assertRaisesRegex(XlsxError, 'xlsx_original_hash_mismatch'):
            prepare_source_parts(self.data, expected_sha256='0'*64, source_key='key', name='n',
                                 sheet='Zażółć', selected_range='A1')

    def test_dtd_entities_in_any_xml_even_unused_and_utf16(self):
        evil = '<!DOCTYPE x [<!ENTITY leak SYSTEM "file:///etc/passwd">]><x>&leak;</x>'
        for raw in (evil, evil.encode('utf-16')):
            self.refuse(package(extra=[('unused.xml', raw)]), 'xlsx_unsafe_xml')
        self.refuse(package(extra=[('bad.xml', '<x>')]), 'xlsx_invalid_xml')

    def test_relationship_refusals(self):
        key = 'xl/_rels/book.xml.rels'
        for target in ('https://invalid.example/x', '../../../escape', 'worksheets/table.xml#frag',
                       'worksheets/%5cfoo.xml'):
            p = parts(); p[key] = p[key].replace('worksheets/table.xml', target)
            with self.subTest(target=target):
                self.refuse(package(p), 'xlsx_unsafe_relationship' if 'escape' not in target else 'xlsx_unsafe_path')
        p = parts(); p[key] = p[key].replace('Id="table"', 'Id="table" TargetMode="External"')
        self.refuse(package(p), 'xlsx_external_relationship_unsupported')
        p = parts(); p[key] = p[key].replace('worksheets/table.xml', 'missing.xml')
        self.refuse(package(p), 'xlsx_missing_part')
        p = parts(); p[key] = p[key].replace('Id="empty"', 'Id="table"')
        self.refuse(package(p), 'xlsx_invalid_relationships')

    def test_macro_and_wrong_package_type_refused(self):
        p = parts(); p['[Content_Types].xml'] = p['[Content_Types].xml'].replace(
            'spreadsheetml.sheet.main+xml', 'ms-excel.sheet.macroEnabled.main+xml')
        self.refuse(package(p), 'xlsx_active_content_unsupported')
        p = parts(); p['[Content_Types].xml'] = p['[Content_Types].xml'].replace(
            'spreadsheetml.sheet.main+xml', 'wordprocessingml.document.main+xml')
        self.refuse(package(p), 'xlsx_workbook_type_unsupported')

    def test_invalid_cells_and_strings(self):
        key = 'xl/worksheets/table.xml'
        for old, new, code in (
            ('<v>1</v>', '<v>2</v>', 'xlsx_invalid_shared_string_index'),
            ('r="B2"', 'r="A2"', 'xlsx_duplicate_or_misplaced_cell'),
            ('r="B2"', 'r="B3"', 'xlsx_duplicate_or_misplaced_cell'),
            ('r="B2"', 'r="B0"', 'xlsx_invalid_cell'),
            ('<v>9007199254740993</v>', '<v>NaN</v>', 'xlsx_invalid_cell_value'),
            ('<v>9007199254740993</v>', '<v><x/></v>', 'xlsx_invalid_cell_value'),
            ('<row r="3">', '<row r="2">', 'xlsx_invalid_row'),
        ):
            p = parts(); p[key] = p[key].replace(old, new, 1)
            with self.subTest(code=code, replacement=new):
                self.refuse(package(p), code)

    def test_unsupported_cell_types_explicit(self):
        p = parts(); p['xl/worksheets/table.xml'] = p['xl/worksheets/table.xml'].replace(
            '<c r="B3"/>', '<c r="B3" t="e"><v>#DIV/0!</v></c>')
        cell = read_cell(package(p), 'Zażółć', 'B3')['sheets'][0]['cells'][0]
        self.assertEqual(cell['status'], 'unsupported')
        self.assertIsNone(cell['value'])

    def test_source_parts_plan_utf8_hashes_no_formula_observation(self):
        plan = prepare_source_parts(self.data, expected_sha256=hashlib.sha256(self.data).hexdigest(),
                                    source_key='synthetic:table', name='table.xlsx',
                                    sheet='Zażółć', selected_range='A1:C5')
        raw = plan['derived']['text'].encode('utf-8')
        self.assertEqual(plan['derived']['sha256'], hashlib.sha256(raw).hexdigest())
        self.assertNotEqual(plan['derived']['sha256'], plan['original']['sha256'])
        self.assertNotIn('123456789', plan['derived']['text'])
        self.assertEqual(len(plan['units']), 7)
        self.assertEqual(len(plan['unavailable_cells']), 8)
        for unit in plan['units']:
            loc = unit['locator']
            actual = raw[loc['byte_start']:loc['byte_start'] + loc['byte_len']]
            self.assertEqual(actual.decode('utf-8'), unit['observation']['text'])
            self.assertEqual(loc['source'], 'sha256:' + plan['derived']['sha256'])
            self.assertEqual(unit['attrs']['xlsx']['original_sha256'], plan['original']['sha256'])
        self.assertEqual(plan, prepare_source_parts(self.data, expected_sha256=plan['original']['sha256'],
                          source_key='synthetic:table', name='table.xlsx', sheet='Zażółć', selected_range='A1:C5'))

    def test_existing_resource_import_extension(self):
        from loom.tools.resource_graph import parsers
        from loom.tools.resource_graph.core import ResourceGraph
        before = dict(parsers._REGISTRY)
        try:
            if 'xlsx' not in before:
                register_with_resource_graph()
            self.assertEqual(parsers.parse(self.data, 'xlsx')['source']['sha256'], hashlib.sha256(self.data).hexdigest())
            graph = ResourceGraph()
            graph.attach_inline(self.data, logical_id='synthetic-xlsx', format='xlsx')
            selected = graph.select('synthetic-xlsx', '/sheets/0/cells/0/text')
            self.assertEqual(selected, 'Nazwa')
            with self.assertRaisesRegex(ValueError, 'parser_already_registered'):
                register_with_resource_graph()
            self.assertEqual(parsers.parse(b'1', 'json'), 1)
        finally:
            parsers._REGISTRY.clear(); parsers._REGISTRY.update(before)

    def test_cli_stdin_success_error_and_input_budget(self):
        command = [sys.executable, '-B', '-m', 'loom.tools.xlsx_basic']
        success = subprocess.run(command + ['--sheet', 'Zażółć', '--range', 'A2'],
                                 input=self.data, capture_output=True, timeout=5)
        self.assertEqual(success.returncode, 0, success.stderr)
        self.assertEqual(json.loads(success.stdout)['sheets'][0]['cells'][0]['text'], 'żółw')
        failed = subprocess.run(command, input=b'bad zip', capture_output=True, timeout=5)
        self.assertEqual(failed.returncode, 2)
        self.assertEqual(json.loads(failed.stdout)['error']['code'], 'xlsx_invalid_container')
        limits = settings(); limits['max_input_bytes'] = 1
        failed = subprocess.run(command + ['--limits-json', json.dumps(limits)],
                                input=self.data, capture_output=True, timeout=5)
        self.assertEqual(json.loads(failed.stdout)['error']['code'], 'xlsx_limit_max_input_bytes')


if __name__ == '__main__':
    unittest.main()
