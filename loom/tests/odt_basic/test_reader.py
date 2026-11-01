"""Synthetic format/security tests; no dependencies, models, providers or native build."""

import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import warnings
import xml.etree.ElementTree as ET
import zipfile
import zlib

from loom.tools.odt_basic import OdtError, extract, prepare_import, read_file
from loom.tools.odt_basic.adapter import read_unit
from loom.tools.odt_basic.reader import MIME, OFFICE, TEXT, TABLE, canonical

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'tools' / 'odt_basic'
FIXTURES = Path(__file__).parent / 'fixtures'
LIMITS = json.loads((TOOLS / 'limits.json').read_text())
CONFIG = json.loads((TOOLS / 'adapter.json').read_text())
EXPECTED = json.loads((FIXTURES / 'expected.json').read_text())
XML = (FIXTURES / 'content.xml').read_bytes()


def package(xml=XML, extras=(), compression=zipfile.ZIP_DEFLATED, mime=MIME.encode('ascii')):
    stream = io.BytesIO()
    # Fixed metadata makes this generated ODT a reproducible fixture, not user data.
    with warnings.catch_warnings():
        warnings.simplefilter('ignore', UserWarning)
        with zipfile.ZipFile(stream, 'w') as archive:
            for name, value, method in [('mimetype', mime, zipfile.ZIP_STORED),
                                         ('content.xml', xml, compression)] + list(extras):
                info = name if isinstance(name, zipfile.ZipInfo) else zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
                info.compress_type = method
                archive.writestr(info, value)
    return stream.getvalue()


def document(body):
    return (f'<o:document-content xmlns:o="{OFFICE}" xmlns:t="{TEXT}" xmlns:b="{TABLE}" '
            'xmlns:x="http://www.w3.org/1999/xlink"><o:body><o:text>'
            + body + '</o:text></o:body></o:document-content>').encode('utf-8')


def plan(data=None, limits=None, config=None, **kwargs):
    return prepare_import(package() if data is None else data, limits or LIMITS, config or CONFIG,
                          source_key=kwargs.get('source_key', 'synthetic:odt'),
                          name='synthetic.odt', language='pl')


def block_text(result, block):
    start, stop = block['byte_range']
    return result['text'].encode('utf-8')[start:stop].decode('utf-8')


class ReaderTests(unittest.TestCase):
    def error(self, code, data, **changes):
        limits = dict(LIMITS, **changes)
        with self.assertRaises(OdtError) as caught:
            extract(data, limits)
        self.assertEqual(caught.exception.code, code)

    def test_acceptance_heading_two_paragraphs_table_polish(self):
        result = extract(package(), LIMITS)
        self.assertEqual(result['text'], EXPECTED['text'])
        self.assertEqual(len(result['blocks']), EXPECTED['blocks'])
        self.assertEqual([b['kind'] for b in result['blocks'] if b['parent'] is None], EXPECTED['top_kinds'])
        self.assertEqual([block_text(result, b) for b in result['blocks'] if b['kind'] == 'table_cell'], EXPECTED['cell_texts'])
        self.assertEqual(result['blocks'][0]['level'], 2)
        self.assertEqual(result['llm_calls'], 0)

    def test_all_original_locators_resolve_against_raw_member(self):
        data = package()
        result = extract(data, LIMITS)
        root = ET.fromstring(XML)
        for block in result['blocks']:
            raw = block['raw_locator']
            self.assertEqual(raw['source'], 'sha256:' + hashlib.sha256(data).hexdigest())
            self.assertEqual(raw['member'], 'content.xml')
            self.assertEqual(raw['member_sha256'], hashlib.sha256(XML).hexdigest())
            node = root
            for index in raw['xml_path']:
                node = list(node)[index]
            self.assertEqual(node.tag, block['observed']['element'])
            self.assertEqual(node.attrib, block['observed']['attributes'])
            self.assertNotIn('byte_start', raw)

    def test_utf8_ranges_are_nested_and_separate_from_original(self):
        result = extract(package(), LIMITS)
        utf8 = result['text'].encode('utf-8')
        self.assertEqual(result['projection']['size'], len(utf8))
        self.assertEqual(result['projection']['sha256'], hashlib.sha256(utf8).hexdigest())
        self.assertNotEqual(result['projection']['sha256'], result['source']['sha256'])
        for block in result['blocks']:
            start, stop = block['byte_range']
            self.assertLessEqual(0, start)
            self.assertLessEqual(start, stop)
            self.assertLessEqual(stop, len(utf8))
            utf8[start:stop].decode('utf-8')
            if block['parent'] is not None:
                lo, hi = result['blocks'][block['parent']]['byte_range']
                self.assertLessEqual(lo, start)
                self.assertLessEqual(stop, hi)
        first = result['blocks'][0]
        self.assertEqual(first['byte_range'], [0, len('Plan Łodzi'.encode('utf-8'))])

    def test_file_read_preserves_bytes(self):
        data = package()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.odt'
            path.write_bytes(data)
            self.assertEqual(read_file(path, LIMITS)['source']['sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(path.read_bytes(), data)

    def test_empty_document_paragraph_and_cell_are_addressable(self):
        self.assertEqual(extract(package(document('')), LIMITS)['blocks'], [])
        result = extract(package(document('<t:p/><b:table><b:table-row><b:table-cell><t:p/></b:table-cell>'
                                          '<b:covered-table-cell/></b:table-row></b:table>')), LIMITS)
        self.assertEqual(result['text'], '\n\t')
        self.assertEqual(sum(b['kind'] == 'table_cell' for b in result['blocks']), 2)
        self.assertTrue(result['blocks'][-1]['covered'])

    def test_namespace_prefixes_utf16_and_numeric_entities(self):
        xml = document('<t:h t:outline-level="1">A &amp; B &#x141;</t:h>').decode('utf-8')
        xml = '<?xml version="1.0" encoding="UTF-16"?>' + xml
        self.assertEqual(extract(package(xml.encode('utf-16')), LIMITS)['text'], 'A & B Ł')

    def test_link_text_is_read_but_notes_fields_unknown_markup_not_flattened(self):
        xml = document('<t:p>A<t:a x:href="https://example.invalid/">B</t:a>C'
                       '<t:note><t:p>SECRET NOTE</t:p></t:note>D'
                       '<t:page-number>99</t:page-number>E</t:p>')
        result = extract(package(xml), LIMITS)
        self.assertEqual(result['text'], 'ABCDE')
        self.assertIn('odt_link_targets_not_followed', result['warnings'])
        self.assertIn('odt_unsupported_inline_omitted', result['warnings'])

    def test_sections_lists_and_nested_tables_keep_order(self):
        xml = document('<t:section><t:list><t:list-item><t:p>A</t:p></t:list-item>'
                       '<t:list-item><t:p>B</t:p></t:list-item></t:list></t:section>'
                       '<b:table><b:table-header-rows><b:table-row><b:table-cell>'
                       '<t:p>C</t:p><b:table><b:table-row><b:table-cell><t:p>D</t:p>'
                       '</b:table-cell></b:table-row></b:table></b:table-cell>'
                       '</b:table-row></b:table-header-rows></b:table>')
        result = extract(package(xml), LIMITS)
        self.assertEqual(result['text'], 'A\nB\nC\nD')
        self.assertEqual(sum(b['kind'] == 'table' for b in result['blocks']), 2)

    def test_repetition_is_observed_without_expansion(self):
        xml = document('<b:table><b:table-row b:number-rows-repeated="3">'
                       '<b:table-cell b:number-columns-repeated="4"><t:p>X</t:p></b:table-cell>'
                       '<b:table-cell><t:p>Y</t:p></b:table-cell></b:table-row>'
                       '<b:table-row><b:table-cell><t:p>Z</t:p></b:table-cell></b:table-row></b:table>')
        result = extract(package(xml), LIMITS)
        self.assertEqual(result['text'], 'X\tY\nZ')
        self.assertEqual([b['logical_row'] for b in result['blocks'] if b['kind'] == 'table_row'], [0, 3])
        self.assertEqual([b['logical_column'] for b in result['blocks'] if b['kind'] == 'table_cell'], [0, 4, 0])
        self.assertIn('odt_table_repetitions_not_expanded', result['warnings'])
        self.error('odt_limit_max_table_cells', package(xml), max_table_cells=15)

    def test_giant_whitespace_is_checked_before_allocation(self):
        self.error('odt_limit_max_projection_bytes', package(document('<t:p><t:s t:c="1000000000000"/></t:p>')))

    def test_invalid_positive_attributes(self):
        for value in ('0', '-1', '1.5', 'nan', '', '+1'):
            with self.subTest(value=value):
                self.error('odt_invalid_structure', package(document(f'<t:p><t:s t:c="{value}"/></t:p>')))

    def test_dtd_and_entities_refused_in_utf8_and_utf16(self):
        for declaration in ('<!DOCTYPE o:document-content>',
                            '<!DOCTYPE o:document-content [<!ENTITY x "boom">]>',
                            '<!DOCTYPE o:document-content SYSTEM "https://example.invalid/evil">',
                            '<!DOCTYPE o:document-content [<!ENTITY x SYSTEM "file:///not-read">]>'):
            for encoding in ('utf-8', 'utf-16'):
                with self.subTest(declaration=declaration, encoding=encoding):
                    xml = f'<?xml version="1.0" encoding="{encoding}"?>' + declaration + document('<t:p/>').decode()
                    self.error('odt_unsafe_xml', package(xml.encode(encoding)))

    def test_malformed_xml_wrong_root_and_duplicate_body(self):
        self.error('odt_invalid_xml', package(b'<broken>'))
        for xml in (b'<not-odt/>', document('<t:p/>').replace(b'</o:body>', b'</o:body><o:body/>'),
                    document('<t:p/>').replace(b'<o:text>', b'<o:spreadsheet>').replace(b'</o:text>', b'</o:spreadsheet>')):
            with self.subTest(xml=xml):
                self.error('odt_invalid_structure', package(xml))

    def test_zip_missing_mimetype_content_and_wrong_mime(self):
        self.error('odt_invalid_package', package(mime=b'application/zip'))
        for member in ('mimetype', 'content.xml'):
            stream = io.BytesIO()
            with zipfile.ZipFile(stream, 'w') as archive:
                archive.writestr(member, MIME if member == 'mimetype' else XML)
            self.error('odt_invalid_package', stream.getvalue())

    def test_traversal_and_encoded_aliases_even_in_unused_members(self):
        for name in ('../evil', '/absolute', 'C:/evil', 'a\\evil', 'a//evil', 'a/./evil',
                     '%2e%2e/evil', 'a/%2E%2E/evil', '%2Fabsolute', 'a%5cevil', 'a%00evil',
                     'file:evil', 'a?b', 'a#b'):
            with self.subTest(name=name):
                self.error('odt_unsafe_path', package(extras=[(name, b'X', zipfile.ZIP_STORED)]))
        self.error('odt_duplicate_or_unsafe_part', package(extras=[('content%2exml', b'X', zipfile.ZIP_STORED)]))

    def test_duplicate_member_and_symlink_refused(self):
        self.error('odt_duplicate_or_unsafe_part', package(extras=[('content.xml', XML, zipfile.ZIP_STORED)]))
        symlink = zipfile.ZipInfo('link')
        symlink.create_system = 3
        symlink.external_attr = (stat.S_IFLNK | 0o777) << 16
        self.error('odt_unsafe_path', package(extras=[(symlink, b'target', zipfile.ZIP_STORED)]))

    def test_encrypted_and_unsupported_compression_refused(self):
        data = bytearray(package())
        local = data.index(b'PK\x03\x04')
        central = data.index(b'PK\x01\x02')
        struct.pack_into('<H', data, local + 6, 1)
        struct.pack_into('<H', data, central + 8, 1)
        self.error('odt_encrypted_zip', bytes(data))
        data = bytearray(package())
        central = data.index(b'PK\x01\x02')
        struct.pack_into('<H', data, central + 10, 99)
        self.error('odt_compression_unsupported', bytes(data))

    def test_corrupt_crc_truncation_and_central_count(self):
        data = bytearray(package())
        index = data.index(MIME.encode('ascii'))
        data[index] ^= 1
        self.error('odt_invalid_zip', bytes(data))

    def test_central_entry_budget_precedes_zipinfo_allocation(self):
        data = bytearray(package())
        eocd = data.rindex(b'PK\x05\x06')
        struct.pack_into('<HH', data, eocd + 8, 1, 1)
        with patch('loom.tools.odt_basic.container.zipfile.ZipFile') as opened:
            self.error('odt_limit_max_entries', bytes(data), max_entries=1)
            opened.assert_not_called()

    def test_zip64_and_overlapping_members_refused(self):
        data = bytearray(package())
        eocd = data.rindex(b'PK\x05\x06')
        struct.pack_into('<I', data, eocd + 12, 0xffffffff)
        self.error('odt_zip64_unsupported', bytes(data))
        data = bytearray(package())
        struct.pack_into('<H', data, 4, 45)
        self.error('odt_zip64_unsupported', bytes(data))
        data = bytearray(package())
        central = data.index(b'PK\x01\x02')
        struct.pack_into('<I', data, central + 20, len(data))
        self.error('odt_invalid_zip', bytes(data))
        for data in (package()[:-10], b'not a zip', package() + b'garbage'):
            self.error('odt_invalid_zip', data)
        data = bytearray(package())
        eocd = data.rindex(b'PK\x05\x06')
        struct.pack_into('<HH', data, eocd + 8, 1, 1)
        self.error('odt_invalid_zip', bytes(data))

    def test_forged_prefix_size_crc_cannot_conceal_deflate_bomb(self):
        payload = b'A' * 100000
        data = bytearray(package(extras=[('ignored.bin', payload, zipfile.ZIP_DEFLATED)]))
        # Both headers lie consistently and CRC matches their advertised prefix.
        # ZipExtFile.read() alone would silently return that prefix.
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            local = archive.getinfo('ignored.bin').header_offset
        central = data.index(b'PK\x01\x02')
        while data[central + 46:central + 57] != b'ignored.bin':
            n, x, c = struct.unpack_from('<HHH', data, central + 28)
            central += 46 + n + x + c
        for crc_at, size_at in ((local + 14, local + 22), (central + 16, central + 24)):
            struct.pack_into('<I', data, crc_at, zlib.crc32(b'A'))
            struct.pack_into('<I', data, size_at, 1)
        self.error('odt_zip_size_mismatch', bytes(data), max_ratio=0, read_chunk_bytes=16)

    def test_actual_compression_ratio_and_all_resource_limits(self):
        bomb = package(extras=[('ignored.bin', b'A' * 100000, zipfile.ZIP_DEFLATED)])
        self.error('odt_limit_max_ratio', bomb)
        for key, value in (('max_input_bytes', 1), ('max_entries', 1), ('max_entry_bytes', 100),
                           ('max_total_uncompressed_bytes', 100), ('max_xml_depth', 2),
                           ('max_xml_nodes', 2), ('max_units', 2), ('max_projection_bytes', 3),
                           ('max_output_bytes', 10), ('max_table_cells', 3)):
            with self.subTest(key=key):
                self.error('odt_limit_' + key, package(), **{key: value})

    def test_limits_can_be_raised_or_disabled_and_chunk_one_works(self):
        limits = {key: (0 if key.startswith('max_') else value) for key, value in LIMITS.items()}
        limits['read_chunk_bytes'] = 1
        self.assertEqual(extract(package(), limits)['text'], EXPECTED['text'])
        self.assertEqual(extract(package(compression=zipfile.ZIP_STORED), LIMITS)['text'], EXPECTED['text'])

    def test_invalid_configuration_is_not_document_content(self):
        for limits in ({}, dict(LIMITS, read_chunk_bytes=0), dict(LIMITS, max_units=True),
                       dict(LIMITS, max_entries=-1), dict(LIMITS, max_ratio=1.5),
                       dict(LIMITS, typo=4)):
            with self.subTest(limits=limits):
                with self.assertRaisesRegex(OdtError, 'odt_configuration_invalid'):
                    extract(package(), limits)


class AdapterTests(unittest.TestCase):
    def test_native_source_unit_observation_shapes_and_unique_nested_ids(self):
        result = plan()
        self.assertEqual(len(result['sources']), 3)
        raw, text, structure = result['sources']
        self.assertEqual(raw['blob_hash'], hashlib.sha256(package()).hexdigest())
        self.assertEqual(raw['size'], len(package()))
        self.assertEqual(text['blob_hash'], hashlib.sha256(EXPECTED['text'].encode()).hexdigest())
        self.assertEqual(structure['blob_hash'], hashlib.sha256(canonical(result['extraction']).encode()).hexdigest())
        self.assertEqual(len({u['id'] for u in result['units']}), EXPECTED['blocks'])
        self.assertEqual(len({o['id'] for o in result['observations']}), EXPECTED['blocks'])
        unit_ids = {u['id'] for u in result['units']}
        for unit, observation in zip(result['units'], result['observations']):
            self.assertEqual(unit['source'], structure['id'])
            self.assertEqual(unit['locator']['source'], structure['id'])
            self.assertTrue(unit['locator']['json_pointer'].startswith('/blocks/'))
            self.assertIsNone(unit['locator']['byte_start'])
            expected_id = 'un_' + hashlib.sha256((unit['source'] + '\x1f' + canonical(unit['locator'])).encode()).hexdigest()[:16]
            self.assertEqual(unit['id'], expected_id)
            self.assertEqual(observation['unit'], unit['id'])
            self.assertEqual(observation['locator']['source'], text['id'])
            loc = observation['locator']
            self.assertEqual(EXPECTED['text'].encode()[loc['byte_start']:loc['byte_start'] + loc['byte_len']].decode(), observation['text'])
            if unit['attrs']['parent_unit']:
                self.assertIn(unit['attrs']['parent_unit'], unit_ids)

    def test_evidence_and_genesis_do_not_fabricate_inferences(self):
        result = plan()
        self.assertEqual(result['extraction']['evidence'], {'structure': 'observed', 'text': 'derived', 'inferred': []})
        self.assertEqual(result['genesis']['text'], EXPECTED['text'])
        self.assertEqual(result['genesis']['source']['group'], 'synthetic:odt')
        for observation in result['observations']:
            self.assertEqual(observation['attrs']['evidence_class'], 'derived')
            self.assertEqual(observation['attrs']['structure_evidence_class'], 'observed')
            self.assertNotIn('confidence', observation)

    def test_repeated_import_stable_and_zip_revision_preserves_original_identity(self):
        first = plan()
        self.assertEqual(first, plan())
        altered = plan(package(extras=[('metadata.bin', b'changed ZIP only', zipfile.ZIP_STORED)]))
        self.assertEqual(first['genesis']['text'], altered['genesis']['text'])
        self.assertNotEqual(first['sources'][0]['id'], altered['sources'][0]['id'])
        self.assertNotEqual(first['sources'][1]['id'], altered['sources'][1]['id'])
        self.assertNotEqual(first['units'][0]['id'], altered['units'][0]['id'])
        self.assertNotEqual(first['genesis']['source']['id'], plan(source_key='synthetic:another')['genesis']['source']['id'])

    def test_same_text_and_empty_spans_have_distinct_occurrences(self):
        result = plan(package(document('<t:p>X</t:p><t:p>X</t:p><t:p/>'
                                       '<b:table><b:table-row><b:table-cell><t:p/>'
                                       '</b:table-cell></b:table-row></b:table>')))
        self.assertEqual(len(result['units']), len({u['id'] for u in result['units']}))
        self.assertEqual(result['observations'][0]['text'], result['observations'][1]['text'])
        self.assertNotEqual(result['observations'][0]['id'], result['observations'][1]['id'])

    def test_read_one_cell_checks_raw_version_and_saved_plan(self):
        data = package()
        result = plan(data)
        cell = next(u for u, o in zip(result['units'], result['observations']) if u['kind'] == 'table_cell' and o['text'] == 'Żółw')
        read = read_unit(data, result, cell['id'], LIMITS, CONFIG)
        self.assertEqual(read['text'], 'Żółw')
        with self.assertRaisesRegex(OdtError, 'odt_source_mismatch'):
            read_unit(package(document('<t:p>changed</t:p>')), result, cell['id'], LIMITS, CONFIG)
        for mutation in ('range', 'text', 'path', 'evidence'):
            altered = copy.deepcopy(result)
            if mutation == 'range':
                altered['observations'][0]['locator']['byte_len'] = 10000
            elif mutation == 'text':
                altered['observations'][0]['text'] = 'injected'
            elif mutation == 'path':
                altered['units'][0]['attrs']['original_locator']['xml_path'] = [999]
            else:
                altered['observations'][0]['attrs']['evidence_class'] = 'observed'
            with self.subTest(mutation=mutation), self.assertRaisesRegex(OdtError, 'odt_receipt_mismatch'):
                read_unit(data, altered, cell['id'], LIMITS, CONFIG)

    def test_adapter_configuration_and_output_limit(self):
        for config in ({}, dict(CONFIG, original_source_kind=''), dict(CONFIG, kinds={})):
            with self.assertRaisesRegex(OdtError, 'odt_configuration_invalid'):
                prepare_import(package(), LIMITS, config, source_key='x', name='x.odt')
        # Extraction fits; the more detailed import plan does not.
        limits = dict(LIMITS, max_output_bytes=20000)
        extract(package(), limits)
        with self.assertRaisesRegex(OdtError, 'odt_limit_max_output_bytes'):
            plan(limits=limits)


class CliTests(unittest.TestCase):
    def run_cli(self, data, limits=LIMITS, context=None):
        args = [sys.executable, '-B', '-m', 'loom.tools.odt_basic', '-', json.dumps(limits)]
        if context is not None:
            args.append(json.dumps(context))
        return subprocess.run(args, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              cwd=ROOT.parent, timeout=10, check=False)

    def test_cli_extraction_and_import_mode(self):
        proc = self.run_cli(package())
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout)['text'], EXPECTED['text'])
        proc = self.run_cli(package(), context={'adapter': CONFIG, 'source_key': 'synthetic:odt', 'name': 'fixture.odt'})
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout)['schema'], 'loom.odt_basic.import/1')

    def test_cli_errors_are_bounded_codes_without_source_text_or_traceback(self):
        for data, limits, code in ((b'PRIVATE-MARKER', LIMITS, 'odt_invalid_zip'),
                                   (package(), dict(LIMITS, max_input_bytes=1), 'odt_limit_max_input_bytes')):
            proc = self.run_cli(data, limits)
            self.assertEqual(proc.returncode, 2)
            self.assertEqual(proc.stderr, b'')
            self.assertEqual(json.loads(proc.stdout), {'error': {'code': code}})
            self.assertNotIn(b'PRIVATE-MARKER', proc.stdout)


if __name__ == '__main__':
    # Fleet capacity admission applies to fixture/temp-file operations as well.
    if shutil.disk_usage(ROOT).free < 1073741824:
        raise SystemExit('BLOCKED_CAPACITY: free space below 1 GiB')
    unittest.main()
