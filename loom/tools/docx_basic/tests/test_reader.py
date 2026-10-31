import hashlib
import importlib.util
import io
import json
from pathlib import Path
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

from fixtures import BODY, EXPECTED, MAIN_TYPE, R, W, p, package, parts

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('docx_reader', ROOT / 'reader.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


class DocxReaderTests(unittest.TestCase):
    def setUp(self):
        self.limits = json.loads((ROOT / 'preset.json').read_text())

    def extract(self, data=None, **limits):
        return reader.extract(package() if data is None else data, {**self.limits, **limits})

    def refused(self, data, code=None, **limits):
        with self.assertRaises(reader.DocxError) as caught:
            self.extract(data, **limits)
        if code is not None: self.assertEqual(caught.exception.code, code)
        return caught.exception.code

    def test_polish_order_headings_list_table_and_hash(self):
        data = package()
        result = self.extract(data)
        self.assertEqual(result['text'], EXPECTED)
        self.assertEqual([b['kind'] for b in result['blocks']], ['heading', 'paragraph', 'list_item', 'table', 'paragraph'])
        self.assertEqual(result['blocks'][3]['rows'], [['Kolumna A', 'Kolumna B'], ['jeden', 'dwa']])
        self.assertEqual(result['source'], {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data), 'member': 'word/document.xml'})
        self.assertTrue(result['partial'])
        self.assertIn('docx_list_labels_not_reconstructed', result['warnings'])
        self.assertTrue(all(b['locator'].startswith('word/document.xml#body/') for b in result['blocks']))

    def test_repeated_extraction_is_deterministic_and_source_unchanged(self):
        data = package()
        before = bytes(data)
        self.assertEqual(self.extract(data), self.extract(data))
        self.assertEqual(data, before)

    def test_cli_reopen_new_process_and_original(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'tests') as td:
            path = Path(td) / 'źródło.docx'
            path.write_bytes(package())
            before = path.read_bytes()
            command = [sys.executable, '-I', '-B', str(ROOT / 'reader.py'), str(path), json.dumps(self.limits)]
            first = subprocess.run(command, capture_output=True, check=True)
            second = subprocess.run(command, capture_output=True, check=True)
            self.assertEqual(first.stdout, second.stdout)
            self.assertEqual(json.loads(first.stdout)['text'], EXPECTED)
            self.assertEqual(path.read_bytes(), before)
            self.assertEqual(sorted(x.name for x in Path(td).iterdir()), ['źródło.docx'])

    def test_extraction_has_no_network_or_llm_calls(self):
        with mock.patch.object(socket, 'socket', side_effect=AssertionError('network forbidden')) as network:
            result = self.extract()
        network.assert_not_called()
        self.assertEqual(result['llm_calls'], 0)

    def test_empty_file_and_empty_zip(self):
        self.refused(b'', 'docx_invalid_container')
        self.refused(package({}), 'docx_invalid_container')

    def test_empty_document(self):
        self.refused(package(parts(p('   '))), 'document_no_text')

    def test_invalid_zip_and_truncated_archive(self):
        for data in (b'PKbad', b'not a document', package()[:-1], package() + b'trailing'):
            with self.subTest(data=data[:8]): self.refused(data, 'docx_invalid_container')

    def test_forged_size_and_prefix_crc_cannot_hide_decompressed_data(self):
        import zlib
        entries = parts()
        entries['word/ignored.bin'] = b'A' * 100000
        data = bytearray(package(entries))
        at = data.index(b'PK\x01\x02')
        while data[at:at + 4] == b'PK\x01\x02':
            nlen, xlen, clen = struct.unpack_from('<HHH', data, at + 28)
            name = data[at + 46:at + 46 + nlen]
            if name == b'word/ignored.bin':
                struct.pack_into('<I', data, at + 16, zlib.crc32(b'A'))
                struct.pack_into('<I', data, at + 24, 1)
                break
            at += 46 + nlen + xlen + clen
        # The stdlib zipfile reader accepts the forged prefix, our reader refuses.
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            self.assertEqual(z.read('word/ignored.bin'), b'A')
        self.refused(bytes(data), 'docx_invalid_container')

    def test_crc_corruption_in_ignored_part_also_refuses(self):
        entries = parts()
        entries['word/ignored.bin'] = b'unique-binary-data'
        data = package(entries, zipfile.ZIP_STORED).replace(b'unique-binary-data', b'changed-binary-dat')
        self.refused(data, 'docx_invalid_container')

    def test_ole_encrypted_or_doc_is_explicitly_unsupported(self):
        self.refused(bytes.fromhex('d0cf11e0a1b11ae1') + b'anything', 'docx_encrypted_or_legacy')

    def test_zip_encryption_flags(self):
        data = bytearray(package())
        at = data.index(b'PK\x01\x02')
        struct.pack_into('<H', data, at + 8, struct.unpack_from('<H', data, at + 8)[0] | 1)
        self.refused(bytes(data), 'docx_encrypted')

    def test_all_resource_limits_are_data_and_zero_disables(self):
        for key, value in [('max_input_bytes', 10), ('max_total_uncompressed_bytes', 10),
                           ('max_entry_bytes', 10), ('max_entries', 2), ('max_xml_depth', 2),
                           ('max_xml_nodes', 2), ('max_text_chars', 5)]:
            with self.subTest(limit=key): self.refused(package(), 'docx_limit_' + key, **{key: value})
        self.assertEqual(self.extract(**{k: 0 for k in reader.LIMIT_KEYS})['text'], EXPECTED)

    def test_ratio_bomb(self):
        self.refused(package(parts(p('x' * 100000))), 'docx_limit_max_ratio', max_ratio=2)

    def test_forged_entry_count_still_bounded(self):
        data = bytearray(package())
        at = data.rfind(b'PK\x05\x06')
        struct.pack_into('<HH', data, at + 8, 1, 1)
        self.refused(bytes(data), 'docx_limit_max_entries', max_entries=2)

    def test_unsafe_paths_including_unread_parts(self):
        for name in ('../escape.xml', '/tmp/escape.xml', 'a/../../bad.xml', 'a\\bad.xml',
                     'C:/bad.xml', '%2e%2e/escape.xml', 'a//bad.xml', 'a/./bad.xml'):
            entries = parts()
            entries[name] = '<x/>'
            with self.subTest(name=name):
                self.refused(package(entries))

    def test_nul_member_name_refused_before_zipfile_truncation(self):
        entries = parts()
        entries['word/ignored.xml'] = '<x/>'
        data = package(entries).replace(b'word/ignored.xml', b'word/ign\x00red.xml')
        self.refused(data, 'docx_unsafe_path')

    def test_large_valid_deflate_stream_crosses_chunk_boundaries(self):
        text = 'Zażółć ' * 10000
        result = self.extract(package(parts(p(text))), max_ratio=0, read_chunk_bytes=127)
        self.assertEqual(result['text'], text)

    def test_utf16_xml_and_strict_wordprocessing_namespace(self):
        entries = parts()
        entries['word/document.xml'] = entries['word/document.xml'].encode('utf-16')
        self.assertEqual(self.extract(package(entries))['text'], EXPECTED)
        entries = parts()
        for name in ('word/document.xml', 'word/styles.xml'):
            entries[name] = entries[name].replace(W, 'http://purl.oclc.org/ooxml/wordprocessingml/main')
        self.assertEqual(self.extract(package(entries))['text'], EXPECTED)

    def test_unreferenced_style_cannot_change_heading(self):
        entries = parts()
        entries['word/_rels/document.xml.rels'] = '<Relationships xmlns="' + R + '"/>'
        result = self.extract(package(entries))
        self.assertEqual(result['blocks'][0]['kind'], 'paragraph')
        self.assertFalse(result['text'].startswith('#'))

    def test_auxiliary_comments_and_headers_have_explicit_warnings(self):
        entries = parts()
        entries['word/comments.xml'] = '<w:comments xmlns:w="' + W + '">' + p('COMMENT') + '</w:comments>'
        entries['word/header1.xml'] = '<w:hdr xmlns:w="' + W + '">' + p('HEADER') + '</w:hdr>'
        result = self.extract(package(entries))
        self.assertNotIn('COMMENT', result['text'])
        self.assertNotIn('HEADER', result['text'])
        self.assertIn('docx_comments_omitted', result['warnings'])
        self.assertIn('docx_headers_footers_omitted', result['warnings'])

    def test_cli_stdin_with_isolated_interpreter_and_no_source_path(self):
        command = [sys.executable, '-I', '-B', '-S', str(ROOT / 'reader.py'), '-', json.dumps(self.limits)]
        run = subprocess.run(command, input=package(), capture_output=True, env={'LANG': 'C.UTF-8'})
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(run.stdout)['text'], EXPECTED)
        self.assertEqual(run.stderr, b'')

    def test_parser_reads_no_files_and_writes_nothing(self):
        data = package()
        with mock.patch('builtins.open', side_effect=AssertionError('unexpected filesystem access')):
            result = self.extract(data)
        self.assertEqual(result['text'], EXPECTED)

    def test_integration_data_limits_and_all_error_warning_translations(self):
        additions = json.loads((ROOT / 'chat_context_additions.json').read_text())
        self.assertEqual(additions['reader']['limits'], self.limits)
        self.assertEqual(additions['reader']['extensions'], ['.docx'])
        self.assertTrue(all(set(words) >= {'pl', 'en'} for words in additions['warnings'].values()))
        # Literal mechanism codes in the parser must be presented through data.
        import ast
        tree = ast.parse((ROOT / 'reader.py').read_text())
        codes = {n.value for n in ast.walk(tree) if isinstance(n, ast.Constant)
                 and isinstance(n.value, str) and n.value.startswith('docx_')}
        translated = set(additions['warnings']) | set(additions['messages'])
        self.assertEqual(codes - translated - {'docx_limit_'}, set())

    def test_symlink_part_refused(self):
        data = bytearray(package())
        at = data.index(b'PK\x01\x02')
        struct.pack_into('<I', data, at + 38, (stat.S_IFLNK | 0o777) << 16)
        self.refused(bytes(data), 'docx_unsafe_path')

    def test_duplicate_parts_and_uri_aliases(self):
        stream = io.BytesIO(package())
        with zipfile.ZipFile(stream, 'a') as z:
            z.writestr('word/%64ocument.xml', '<x/>')
        self.refused(stream.getvalue(), 'docx_duplicate_or_unsafe_part')

    def test_dtd_internal_and_external_entities_utf8_utf16(self):
        for declaration in ('<!DOCTYPE w:document [<!ENTITY x "expanded">]>',
                            '<!DOCTYPE w:document SYSTEM "http://127.0.0.1:1/never">',
                            '<!DOCTYPE w:document [<!ENTITY x SYSTEM "file:///never-read">]>'):
            for encoding in ('utf-8', 'utf-16'):
                entries = parts()
                xml = declaration + entries['word/document.xml']
                entries['word/document.xml'] = xml.encode(encoding)
                with self.subTest(declaration=declaration, encoding=encoding):
                    self.refused(package(entries), 'docx_unsafe_xml')

    def test_xxe_in_ignored_xml_refused(self):
        entries = parts()
        entries['word/comments.xml'] = '<!DOCTYPE x [<!ENTITY e SYSTEM "file:///never-read">]><x>&e;</x>'
        self.refused(package(entries), 'docx_unsafe_xml')

    def test_invalid_xml_even_in_ignored_part_refuses(self):
        for member in ('word/document.xml', 'word/styles.xml'):
            entries = parts()
            entries[member] = '<broken>'
            self.refused(package(entries), 'docx_invalid_xml')

    def test_external_relations_warn_without_access(self):
        entries = parts()
        entries['word/_rels/document.xml.rels'] = ('<Relationships xmlns="' + R + '">'
            '<Relationship Id="ext" Type="hyperlink" Target="file:///never-read" TargetMode="External"/>'
            '<Relationship Id="web" Type="image" Target="http://127.0.0.1:1/no" TargetMode="External"/>'
            '</Relationships>')
        with mock.patch.object(socket, 'socket', side_effect=AssertionError('network forbidden')) as network:
            result = self.extract(package(entries))
        network.assert_not_called()
        self.assertIn('docx_external_relationships_ignored', result['warnings'])
        self.assertNotIn('never-read', result['text'])

    def test_unsafe_internal_relationships(self):
        for target in ('../../../outside.xml', 'file:///outside.xml', 'http://example.invalid/a', '..\\outside.xml', '%2e%2e/%2e%2e/outside.xml'):
            entries = parts()
            entries['word/_rels/document.xml.rels'] = '<Relationships xmlns="' + R + '"><Relationship Id="e" Type="x" Target="' + target + '"/></Relationships>'
            with self.subTest(target=target): self.refused(package(entries))

    def test_root_relationship_must_be_internal(self):
        entries = parts()
        entries['_rels/.rels'] = entries['_rels/.rels'].replace('Target="word/document.xml"', 'Target="http://example.invalid/x" TargetMode="External"')
        self.refused(package(entries), 'docx_invalid_relationships')

    def test_missing_relationship_and_main_type(self):
        entries = parts()
        del entries['_rels/.rels']
        self.refused(package(entries), 'docx_invalid_package')
        for kind in ('application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml',
                     'application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml',
                     'application/vnd.oasis.opendocument.text'):
            entries = parts()
            entries['[Content_Types].xml'] = entries['[Content_Types].xml'].replace(MAIN_TYPE, kind)
            self.refused(package(entries), 'docx_not_supported_document')

    def test_docm_and_vba_refuse(self):
        entries = parts()
        entries['[Content_Types].xml'] = entries['[Content_Types].xml'].replace(MAIN_TYPE, 'application/vnd.ms-word.document.macroEnabled.main+xml')
        self.refused(package(entries), 'docx_macros_unsupported')
        entries = parts()
        entries['word/vbaProject.bin'] = b'never execute'
        self.refused(package(entries), 'docx_macros_unsupported')

    def test_missing_parser(self):
        with mock.patch.object(reader, 'expat', None):
            self.refused(package(), 'document_reader_unavailable')

    def test_partial_omits_images_changes_fields_math_and_comments(self):
        body = (p('visible') + '<w:p><w:ins><w:r><w:t>TRACKED</w:t></w:r></w:ins>'
                '<w:r><w:drawing><w:t>IMAGE</w:t></w:drawing></w:r>'
                '<w:fldSimple w:instr="secret"><w:r><w:t>FIELD</w:t></w:r></w:fldSimple>'
                '<w:r><w:commentReference w:id="0"/></w:r>'
                '<m:oMath xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math"><m:t>MATH</m:t></m:oMath></w:p>')
        result = self.extract(package(parts(body)))
        for hidden in ('TRACKED', 'IMAGE', 'FIELD', 'MATH'): self.assertNotIn(hidden, result['text'])
        for warning in ('docx_images_omitted', 'docx_tracked_changes_omitted', 'docx_fields_omitted', 'docx_comments_omitted', 'docx_equations_omitted'):
            self.assertIn(warning, result['warnings'])

    def test_cross_paragraph_fields_omit_cached_results(self):
        body = (p('before') + '<w:p><w:r><w:fldChar w:fldCharType="begin"/></w:r></w:p>'
                + p('CACHED') + '<w:p><w:r><w:fldChar w:fldCharType="end"/></w:r></w:p>' + p('after'))
        self.assertEqual(self.extract(package(parts(body)))['text'], 'before\nafter')
        self.refused(package(parts(body.replace('w:fldCharType="end"', 'w:fldCharType="begin"'))), 'docx_invalid_structure')

    def test_namespace_prefix_is_not_significant_and_foreign_text_omitted(self):
        entries = parts()
        entries['word/document.xml'] = entries['word/document.xml'].replace('xmlns:w=', 'xmlns:z=').replace('<w:', '<z:').replace('</w:', '</z:').replace(' w:', ' z:')
        self.assertEqual(self.extract(package(entries))['text'], EXPECTED)
        entries = parts(p('visible') + '<x:p xmlns:x="urn:fake"><x:t>FORGED</x:t></x:p>')
        self.assertNotIn('FORGED', self.extract(package(entries))['text'])

    def test_tabs_breaks_entities_split_runs_unicode(self):
        body = '<w:p><w:r><w:t xml:space="preserve"> A &lt; </w:t><w:tab/><w:t>ż</w:t><w:br/></w:r><w:r><w:t>ółw</w:t></w:r></w:p>'
        self.assertEqual(self.extract(package(parts(body)))['text'], ' A < \tż\nółw')

    def test_invalid_limits_and_no_hidden_fallback(self):
        for value in (-1, True, '10', 1.5, None):
            self.refused(package(), 'docx_configuration_invalid', max_input_bytes=value)
        self.refused(package(), 'docx_configuration_invalid', read_chunk_bytes=0)
        with self.assertRaises(reader.DocxError): reader.extract(package(), {})

    def test_cli_error_returns_no_partial_text_or_path(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'tests') as td:
            path = Path(td) / 'broken.docx'
            path.write_bytes(b'bad')
            proc = subprocess.run([sys.executable, '-I', '-B', str(ROOT / 'reader.py'), str(path), json.dumps(self.limits)], capture_output=True)
            self.assertEqual(proc.returncode, 2)
            self.assertEqual(json.loads(proc.stdout), {'error': {'code': 'docx_invalid_container'}})
            self.assertEqual(proc.stderr, b'')


if __name__ == '__main__': unittest.main(verbosity=2)
