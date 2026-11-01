import importlib.util
import io
import json
from pathlib import Path
import stat
import struct
import subprocess
import sys
import unittest
import warnings
import zipfile

from fixtures import archive, changed, forged_prefix_size, parts, ONE, OPF, CONTAINER, RENDERED

ROOT = Path(__file__).resolve().parents[3]
READER = ROOT / 'loom/tools/epub_basic/reader.py'
spec = importlib.util.spec_from_file_location('epub_basic_reader', READER)
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)
PRESET = json.loads(READER.with_name('preset.json').read_text())


class ReaderTests(unittest.TestCase):
    def parse(self, raw=None, **limits):
        return reader.extract(archive() if raw is None else raw, dict(PRESET, **limits))

    def refuses(self, code, raw=None, **limits):
        with self.assertRaises(reader.EpubError) as error:
            self.parse(raw, **limits)
        self.assertEqual(code, error.exception.code)

    def test_spine_utf8_hash_original_and_exact_ranges(self):
        import hashlib
        raw = archive()
        got = self.parse(raw)
        self.assertEqual(got['text'], RENDERED)
        self.assertEqual(got['source'], {'sha256': hashlib.sha256(raw).hexdigest(),
                                        'size': len(raw), 'member': 'OEBPS/book.opf'})
        self.assertEqual([c['member'] for c in got['chapters']], ['OEBPS/Text/one.xhtml', 'OEBPS/Text/two.xhtml'])
        self.assertEqual([c['linear'] for c in got['chapters']], ['yes', 'no'])
        derived = got['text'].encode()
        self.assertEqual(got['projection']['size'], len(derived))
        self.assertEqual(got['projection']['sha256'], hashlib.sha256(derived).hexdigest())
        self.assertNotEqual(got['source']['sha256'], got['projection']['sha256'])
        rebuilt = b''
        for block, span in zip(got['blocks'], got['projection']['ranges']):
            a, b = span['text_range']
            self.assertEqual(derived[a:b].decode(), block['text'])
            a, b = span['byte_range']
            rebuilt += derived[a:b]
            self.assertTrue(block['locator'].startswith(block['member'] + '#spine/' + str(block['chapter_index']) + '/xhtml/'))
        self.assertEqual(rebuilt, derived)
        self.assertEqual(len(got['blocks']), 8)
        self.assertEqual(got['blocks'][2]['text'], got['blocks'][3]['text'])
        self.assertNotEqual(got['blocks'][2]['locator'], got['blocks'][3]['locator'])
        self.assertEqual(got['blocks'][4]['text'], '')
        self.assertEqual(got['blocks'][5]['xml_path'], [1, 5, 0])
        for chapter in got['chapters']:
            a, b = chapter['byte_range']
            first, count = chapter['first_block'], chapter['block_count']
            self.assertEqual(a, got['projection']['ranges'][first]['byte_range'][0])
            self.assertEqual(b, got['projection']['ranges'][first + count - 1]['byte_range'][1])
        self.assertEqual(self.parse(raw), got)
        self.assertEqual(raw, archive())

    def test_zip_layout_changes_original_hash_but_not_projection(self):
        a, b = self.parse(), self.parse(archive(list(reversed(parts()))))
        self.assertNotEqual(a['source']['sha256'], b['source']['sha256'])
        self.assertEqual(a['projection'], b['projection'])
        self.assertEqual(a['chapters'], b['chapters'])

    def test_empty_and_repeated_spine_items_are_distinct(self):
        blank = b'<html xmlns="http://www.w3.org/1999/xhtml"><body/></html>'
        got = self.parse(changed('OEBPS/Text/one.xhtml', blank))
        self.assertEqual(got['chapters'][0]['block_count'], 0)
        self.assertEqual(got['chapters'][0]['byte_range'], [0, 0])
        raw = changed('OEBPS/book.opf', OPF.replace(b'<itemref idref="one"/>', b'<itemref idref="one"/><itemref idref="one"/>'))
        got = self.parse(raw)
        self.assertEqual(len(got['chapters']), 3)
        self.assertNotEqual(got['blocks'][0]['locator'], got['blocks'][6]['locator'])
        self.assertEqual(got['blocks'][0]['text'], got['blocks'][6]['text'])

    def test_rendition_choice_is_configurable_and_recorded_by_opf_member(self):
        container = CONTAINER.replace(b'</rootfiles>', b'<rootfile full-path="second.opf" media-type="application/oebps-package+xml"/></rootfiles>')
        second = OPF.replace(b'Text/', b'OEBPS/Text/').replace(b'<itemref idref="one"/>', b'')
        raw = archive([(n, container if n.endswith('container.xml') else v) for n, v in parts()] + [('second.opf', second)])
        got = self.parse(raw, rootfile_index=1)
        self.assertEqual(got['source']['member'], 'second.opf')
        self.assertEqual(got['text'], '## Drugi\nKońcowy akapit.')
        self.assertIn('epub_selected_rendition_only', got['warnings'])
        self.refuses('epub_rendition_unavailable', raw, rootfile_index=2)
        self.refuses('epub_configuration_invalid', rootfile_index=True)

    def test_nested_paragraph_is_located_without_duplicate_list_text(self):
        raw = changed('OEBPS/Text/one.xhtml', ONE.replace(b'<div><p>W', b'<li><div><p>W').replace(b'</p></div>', b'</p></div></li>'))
        got = self.parse(raw)
        self.assertEqual(got['text'], RENDERED)
        self.assertEqual(got['blocks'][5]['kind'], 'paragraph')
        self.assertEqual(got['blocks'][5]['xml_path'], [1, 5, 0, 0])

    def test_stored_zip_and_percent_encoded_local_href(self):
        self.assertEqual(self.parse(archive(compression=zipfile.ZIP_STORED))['text'], RENDERED)
        raw = archive([(n.replace('one.xhtml', 'one space.xhtml'), v.replace(b'Text/one.xhtml', b'Text/one%20space.xhtml') if n.endswith('.opf') else v) for n, v in parts()])
        self.assertEqual(self.parse(raw)['text'], RENDERED)

    def test_relative_sibling_href_stays_inside_zip(self):
        raw = changed('OEBPS/book.opf', OPF.replace(b'Text/one.xhtml', b'Text/../Text/one.xhtml'))
        self.assertEqual(self.parse(raw)['text'], RENDERED)

    def test_missing_container_opf_and_spine_member(self):
        for name in ['META-INF/container.xml', 'OEBPS/book.opf', 'OEBPS/Text/one.xhtml']:
            with self.subTest(name=name):
                self.refuses('epub_missing_member', archive([(n, v) for n, v in parts() if n != name]))

    def test_missing_spine_reference(self):
        self.refuses('epub_invalid_spine', changed('OEBPS/book.opf', OPF.replace(b'idref="one"', b'idref="absent"')))

    def test_duplicate_manifest_id(self):
        self.refuses('epub_invalid_manifest', changed('OEBPS/book.opf', OPF.replace(b'id="two"', b'id="one"')))

    def test_traversal_zip_names(self):
        for name in ['../outside', '/abs', 'A/../../x', 'A\\x', 'C:/x', 'A/%2e%2e/x', 'A//x', './x', 'x/%ff']:
            with self.subTest(name=name), warnings.catch_warnings():
                warnings.simplefilter('ignore')
                self.refuses('epub_unsafe_path', archive(parts() + [(name, b'bad')]))

    def test_traversal_and_external_spine_hrefs(self):
        for href in [b'../../outside.xhtml', b'%2fetc/passwd', b'Text/%2e%2e/%2e%2e/%2e%2e/x', b'Text/one.xhtml#fragment']:
            with self.subTest(href=href):
                self.refuses('epub_unsafe_path', changed('OEBPS/book.opf', OPF.replace(b'Text/one.xhtml', href)))
        for href in [b'https://invalid.example/one.xhtml', b'//invalid.example/x', b'C:%5cx']:
            self.refuses('epub_external_spine', changed('OEBPS/book.opf', OPF.replace(b'Text/one.xhtml', href)))

    def test_duplicate_and_percent_alias_zip_members(self):
        for name in ['OEBPS/Text/one.xhtml', 'OEBPS/Text/%6fne.xhtml']:
            with self.subTest(name=name), warnings.catch_warnings():
                warnings.simplefilter('ignore')
                self.refuses('epub_duplicate_or_unsafe_part', archive(parts() + [(name, ONE)]))

    def test_nul_in_raw_zip_name_is_rejected_before_truncation(self):
        raw = archive(parts() + [('badZsuffix', b'bad')])
        raw = raw.replace(b'badZsuffix', b'bad\x00suffix')
        self.refuses('epub_unsafe_path', raw)

    def test_symlink_member(self):
        out = io.BytesIO(archive())
        with zipfile.ZipFile(out, 'a') as z:
            info = zipfile.ZipInfo('link')
            info.create_system = 3
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(info, '../../outside')
        self.refuses('epub_unsafe_path', out.getvalue())

    def test_dtd_and_external_entity_utf8_utf16_every_xml_path(self):
        attacks = ['<!DOCTYPE html SYSTEM "file:///never-read">',
                   '<!DOCTYPE html [<!ENTITY x SYSTEM "https://invalid.example/x">]>',
                   '<!DOCTYPE html [<!ENTITY x "boom"><!ENTITY y "&x;&x;">]>']
        for encoding in ['utf-8', 'utf-16']:
            for dtd in attacks:
                evil = ('<?xml version="1.0" encoding="' + encoding + '"?>' + dtd + '<html xmlns="http://www.w3.org/1999/xhtml"><body><p>x</p></body></html>').encode(encoding)
                for name in ['OEBPS/Text/one.xhtml', 'META-INF/container.xml', 'OEBPS/book.opf']:
                    with self.subTest(encoding=encoding, dtd=dtd, name=name):
                        self.refuses('epub_unsafe_xml', changed(name, evil))
                self.refuses('epub_unsafe_xml', archive(parts() + [('unused.xml', evil)]))

    def test_xml_base_is_not_silently_resolved_to_different_content(self):
        for name, raw, target in [('OEBPS/book.opf', OPF, b'<manifest>'),
                                  ('OEBPS/Text/one.xhtml', ONE, b'<body>')]:
            evil = raw.replace(target, target[:-1] + b' xml:base="https://invalid.example/">')
            self.refuses('epub_xml_base_unsupported', changed(name, evil))

    def test_undeclared_entity_and_malformed_xml(self):
        for raw in [ONE.replace(b'<body>', b'<body>&missing;'), b'<broken>']:
            self.refuses('epub_invalid_xml', changed('OEBPS/Text/one.xhtml', raw))

    def test_no_network_resource_fetch_and_no_script_text(self):
        raw = changed('OEBPS/Text/one.xhtml', ONE.replace(b'<body>', b'<body><script>must not appear</script><img src="https://invalid.example/x"/><iframe src="file:///never-read"/>'))
        got = self.parse(raw)
        self.assertEqual(got['text'], RENDERED)
        self.assertIn('epub_nontext_or_active_content_omitted', got['warnings'])

    def test_all_budget_ceilings_are_enforced(self):
        cases = {'max_input_bytes': 1, 'max_entries': 2, 'max_entry_bytes': 30,
                 'max_total_uncompressed_bytes': 50, 'max_ratio': 1,
                 'max_xml_depth': 2, 'max_xml_nodes': 3, 'max_text_chars': 10}
        for key, value in cases.items():
            with self.subTest(key=key):
                self.refuses('epub_limit_' + key, **{key: value})

    def test_global_node_budget_and_small_read_chunks(self):
        got = self.parse(read_chunk_bytes=1)
        self.assertEqual(got['text'], RENDERED)
        node_count = sum(1 for value in parts() if value[0].endswith(('.xml', '.opf', '.xhtml')) for _ in reader.ET.fromstring(value[1]).iter())
        self.refuses('epub_limit_max_xml_nodes', max_xml_nodes=node_count - 1)
        self.assertEqual(self.parse(max_xml_nodes=node_count)['text'], RENDERED)

    def test_explicit_zero_ceilings_preserve_safety(self):
        zero = {k: 0 for k in reader.LIMIT_KEYS}
        self.assertEqual(self.parse(**zero)['text'], RENDERED)
        self.refuses('epub_unsafe_path', archive(parts() + [('../x', b'x')]), **zero)

    def test_invalid_limit_types_and_schema(self):
        for value in [True, -1, 1.2, '4', None]:
            for key in reader.LIMIT_KEYS + ('read_chunk_bytes',):
                with self.subTest(key=key, value=value):
                    self.refuses('epub_configuration_invalid', **{key: value})
        self.refuses('epub_configuration_invalid', read_chunk_bytes=0)
        self.refuses('epub_configuration_invalid', schema='wrong')

    def test_forged_deflate_size_is_not_truncated_into_success(self):
        self.refuses('epub_invalid_container', forged_prefix_size())

    def test_encrypted_zip_flag_and_unsupported_compression(self):
        raw = bytearray(archive())
        local, central = raw.find(b'PK\x03\x04'), raw.find(b'PK\x01\x02')
        for at, offset in [(local, 6), (central, 8)]:
            flags = struct.unpack_from('<H', raw, at + offset)[0]
            struct.pack_into('<H', raw, at + offset, flags | 1)
        self.refuses('epub_encrypted', bytes(raw))
        self.refuses('epub_compression_unsupported', archive(compression=zipfile.ZIP_BZIP2))

    def test_multidisk_and_zip64_are_explicitly_rejected(self):
        raw = bytearray(archive())
        end = raw.rfind(b'PK\x05\x06')
        struct.pack_into('<H', raw, end + 4, 1)
        self.refuses('epub_invalid_container', bytes(raw))
        raw = bytearray(archive())
        struct.pack_into('<HH', raw, end + 8, 65535, 65535)
        self.refuses('epub_zip64_unsupported', bytes(raw))

    def test_corrupt_crc_and_truncated_zip(self):
        raw = bytearray(archive(compression=zipfile.ZIP_STORED))
        raw[raw.index(b'Za\xc5\xbc')] ^= 1
        self.refuses('epub_invalid_container', bytes(raw))
        self.refuses('epub_invalid_container', archive()[:-10])

    def test_forged_central_count_cannot_bypass_entry_budget(self):
        raw = bytearray(archive())
        end = raw.rfind(b'PK\x05\x06')
        struct.pack_into('<HH', raw, end + 8, 1, 1)
        self.refuses('epub_limit_max_entries', bytes(raw), max_entries=2)

    def test_encrypted_package_and_no_text_are_explicit(self):
        self.refuses('epub_encrypted', archive(parts() + [('META-INF/encryption.xml', b'<encryption/>')]))
        blank = b'<html xmlns="http://www.w3.org/1999/xhtml"><body><p/></body></html>'
        self.refuses('epub_no_text', archive([(n, blank if n.endswith('.xhtml') else v) for n, v in parts()]))


class ProcessContractTests(unittest.TestCase):
    # The real helper argv/stdin/stdout contract used by the native transport,
    # not a mocked subprocess. Native hook coverage is in test_hook.cpp.
    def run_helper(self, raw, limits=PRESET):
        return subprocess.run([sys.executable, '-I', '-B', '-S', str(READER), '-', json.dumps(limits)],
                              input=raw, capture_output=True, timeout=10, env={'LANG': 'C.UTF-8'})

    def test_real_isolated_process_matches_library_and_input_hash(self):
        raw = archive()
        process = self.run_helper(raw)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertEqual(process.stderr, b'')
        self.assertEqual(json.loads(process.stdout), reader.extract(raw, PRESET))

    def test_real_process_error_and_bounded_stdin(self):
        process = self.run_helper(archive(), dict(PRESET, max_input_bytes=30))
        self.assertEqual(process.returncode, 1)
        self.assertEqual(json.loads(process.stdout), {'error': {'code': 'epub_limit_max_input_bytes'}})
        self.assertEqual(process.stderr, b'')
        process = self.run_helper(changed('OEBPS/Text/one.xhtml', b'<!DOCTYPE x><x/>'))
        self.assertEqual(process.returncode, 1)
        self.assertEqual(json.loads(process.stdout), {'error': {'code': 'epub_unsafe_xml'}})


if __name__ == '__main__':
    unittest.main()
