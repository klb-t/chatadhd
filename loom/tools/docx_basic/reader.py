#!/usr/bin/env python3
"""Bounded OOXML reader. Only derived text; callers retain the original bytes.

Resource limits are supplied as data (zero disables a ceiling). ZIP paths,
DTD/entities, macro content and external resource fetching are invariants.
There are no writes, network clients, subprocesses or model calls here.
"""
import hashlib
import io
import json
import posixpath
import stat
import struct
import sys
import urllib.parse
import xml.etree.ElementTree as ET
import zipfile
import zlib
try:
    from xml.parsers import expat
except ImportError:
    expat = None

W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
WS = 'http://purl.oclc.org/ooxml/wordprocessingml/main'
R = 'http://schemas.openxmlformats.org/package/2006/relationships'
CT = 'http://schemas.openxmlformats.org/package/2006/content-types'
OFFICE = ('http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument',
          'http://purl.oclc.org/ooxml/officeDocument/relationships/officeDocument')
MAIN_TYPE = 'application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml'
LIMIT_KEYS = ('max_input_bytes', 'max_total_uncompressed_bytes', 'max_entry_bytes',
              'max_entries', 'max_ratio', 'max_xml_depth', 'max_xml_nodes', 'max_text_chars')


class DocxError(Exception):
    def __init__(self, code):
        self.code = code
        super().__init__(code)


def fail(code):
    raise DocxError(code)


def settings(values):
    if not isinstance(values, dict) or values.get('schema') != 'loom.docx_basic.limits/1':
        fail('docx_configuration_invalid')
    for key in LIMIT_KEYS + ('read_chunk_bytes',):
        value = values.get(key)
        if type(value) is not int or value < 0:
            fail('docx_configuration_invalid')
    if values['read_chunk_bytes'] == 0:
        fail('docx_configuration_invalid')
    return values


def check(limits, key, value):
    if limits[key] and value > limits[key]:
        fail('docx_limit_' + key)


def safe_name(name):
    # OPC part names are URIs; validate decoded spelling too (no aliases).
    decoded = urllib.parse.unquote(name, errors='strict')
    for value in (name, decoded):
        if (not value or value.startswith('/') or '\\' in value or ':' in value
                or '?' in value or '#' in value or any(ord(c) < 32 or ord(c) == 127 for c in value)
                or any(p in ('', '.', '..') for p in value.rstrip('/').split('/'))):
            fail('docx_unsafe_path')
    return decoded.rstrip('/')


def central_directory(data, limits):
    # Count actual directory records BEFORE ZipFile allocates its entry objects.
    end = data.rfind(b'PK\x05\x06', max(0, len(data) - 65557))
    if end < 0 or end + 22 > len(data):
        fail('docx_invalid_container')
    _, disk, cd_disk, n_disk, count, size, offset, comment = struct.unpack_from('<4s4H2IH', data, end)
    if end + 22 + comment != len(data) or disk or cd_disk or n_disk != count:
        fail('docx_invalid_container')
    if count == 0xffff or size == 0xffffffff or offset == 0xffffffff:
        fail('docx_zip64_unsupported')
    if offset + size != end or not data.startswith(b'PK\x03\x04'):
        fail('docx_invalid_container')
    check(limits, 'max_entries', count)
    at, actual = offset, 0
    while at < end:
        if at + 46 > end or data[at:at + 4] != b'PK\x01\x02':
            fail('docx_invalid_container')
        name, extra, comm = struct.unpack_from('<3H', data, at + 28)
        at += 46 + name + extra + comm
        actual += 1
        check(limits, 'max_entries', actual)
    if at != end or actual != count:
        fail('docx_invalid_container')


class XmlBudget:
    def __init__(self, limits):
        self.limits = limits
        self.nodes = 0

    def parse(self, data):
        if expat is None:
            fail('document_reader_unavailable')
        parser = expat.ParserCreate(namespace_separator='}')
        tree = ET.TreeBuilder()
        depth = 0

        def qname(name):
            return '{' + name if '}' in name else name

        def start(name, attrs):
            nonlocal depth
            depth += 1
            self.nodes += 1
            check(self.limits, 'max_xml_depth', depth)
            check(self.limits, 'max_xml_nodes', self.nodes)
            tree.start(qname(name), {qname(k): v for k, v in attrs.items()})

        def end(name):
            nonlocal depth
            tree.end(qname(name))
            depth -= 1

        def forbidden(*_args):
            fail('docx_unsafe_xml')

        parser.StartElementHandler = start
        parser.EndElementHandler = end
        parser.CharacterDataHandler = tree.data
        parser.StartDoctypeDeclHandler = forbidden
        parser.EntityDeclHandler = forbidden
        parser.ExternalEntityRefHandler = forbidden
        parser.SkippedEntityHandler = forbidden
        parser.SetParamEntityParsing(expat.XML_PARAM_ENTITY_PARSING_NEVER)
        try:
            parser.Parse(data, True)
            return tree.close()
        except (expat.ExpatError, ET.ParseError):
            fail('docx_invalid_xml')


def read_parts(data, limits):
    central_directory(data, limits)
    parts, aliases, total = {}, set(), 0
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        infos = archive.infolist()
        check(limits, 'max_entries', len(infos))
        for info in infos:
            name = info.orig_filename
            canonical = safe_name(name)
            if name != info.filename or canonical in aliases:
                fail('docx_duplicate_or_unsafe_part')
            aliases.add(canonical)
            if info.flag_bits & (1 | 64):
                fail('docx_encrypted')
            mode = info.external_attr >> 16
            if stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR):
                fail('docx_unsafe_path')
            if info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                fail('docx_compression_unsupported')
            check(limits, 'max_entry_bytes', info.file_size)
            total += info.file_size
            check(limits, 'max_total_uncompressed_bytes', total)
            if limits['max_ratio'] and info.file_size > limits['max_ratio'] * max(1, info.compress_size):
                fail('docx_limit_max_ratio')
        actual_total = 0
        for info in infos:
            # ZipExtFile truncates to the central directory's claimed file_size.
            # Validate the complete compressed stream ourselves with zlib's bounded
            # decompressor, otherwise a forged size + prefix CRC can conceal a bomb.
            # ZipFile still validates local names, overlap and ZIP metadata on open.
            with archive.open(info):
                pass
            at = info.header_offset
            if at < 0 or at + 30 > len(data) or data[at:at + 4] != b'PK\x03\x04':
                fail('docx_invalid_container')
            flags, method = struct.unpack_from('<HH', data, at + 6)
            if flags & (1 | 64):
                fail('docx_encrypted')
            if flags != info.flag_bits or method != info.compress_type:
                fail('docx_invalid_container')
            nlen, xlen = struct.unpack_from('<HH', data, at + 26)
            at += 30 + nlen + xlen
            end = at + info.compress_size
            if end > len(data):
                fail('docx_invalid_container')
            chunks, size, crc = [], 0, 0
            inflater = zlib.decompressobj(-15) if method == zipfile.ZIP_DEFLATED else None
            pending = b''
            while at < end or pending:
                if not pending:
                    take = min(limits['read_chunk_bytes'], end - at)
                    pending = data[at:at + take]
                    at += take
                if inflater is not None:
                    chunk = inflater.decompress(pending, limits['read_chunk_bytes'])
                    pending = inflater.unconsumed_tail
                    if inflater.unused_data:
                        fail('docx_invalid_container')
                else:
                    chunk, pending = pending, b''
                size += len(chunk)
                actual_total += len(chunk)
                check(limits, 'max_entry_bytes', size)
                check(limits, 'max_total_uncompressed_bytes', actual_total)
                if limits['max_ratio'] and size > limits['max_ratio'] * max(1, info.compress_size):
                    fail('docx_limit_max_ratio')
                if size > info.file_size:
                    fail('docx_invalid_container')
                crc = zlib.crc32(chunk, crc)
                chunks.append(chunk)
            if (size != info.file_size or crc != info.CRC
                    or (inflater is not None and not inflater.eof)):
                fail('docx_invalid_container')
            if info.is_dir():
                if size:
                    fail('docx_invalid_container')
            else:
                parts[info.filename] = b''.join(chunks)
    return parts


def relationships(parts, roots, warnings):
    main, linked = [], {}
    for name, root in roots.items():
        if not name.endswith('.rels'):
            continue
        if root.tag != '{' + R + '}Relationships':
            fail('docx_invalid_relationships')
        if name == '_rels/.rels':
            base, owner = '', ''
        else:
            parent, filename = posixpath.split(name)
            if posixpath.basename(parent) != '_rels':
                fail('docx_invalid_relationships')
            base = posixpath.dirname(parent)
            owner = posixpath.join(base, filename[:-5])
            if owner not in parts:
                fail('docx_invalid_relationships')
        ids = set()
        for rel in root:
            if rel.tag != '{' + R + '}Relationship':
                fail('docx_invalid_relationships')
            rid, kind, target = (rel.get(k, '') for k in ('Id', 'Type', 'Target'))
            if not rid or rid in ids or not kind or not target:
                fail('docx_invalid_relationships')
            ids.add(rid)
            mode = rel.get('TargetMode', 'Internal')
            if mode == 'External':
                warnings.add('docx_external_relationships_ignored')
                if name == '_rels/.rels' and kind in OFFICE:
                    fail('docx_invalid_relationships')
                continue
            if mode != 'Internal':
                fail('docx_invalid_relationships')
            parsed = urllib.parse.urlsplit(target)
            if parsed.scheme or parsed.netloc or parsed.query or '\\' in target:
                fail('docx_unsafe_relationship')
            decoded = urllib.parse.unquote(parsed.path, errors='strict')
            resolved = posixpath.normpath(posixpath.join(base, decoded.lstrip('/'))
                                         if not decoded.startswith('/') else decoded[1:])
            safe_name(resolved)
            if resolved not in parts:
                fail('docx_missing_part')
            linked.setdefault(owner, []).append((kind, resolved))
            if name == '_rels/.rels' and kind in OFFICE:
                main.append(resolved)
    if len(main) != 1:
        fail('docx_invalid_package')
    return main[0], linked


def content_types(root, parts):
    if root.tag != '{' + CT + '}Types':
        fail('docx_invalid_package')
    defaults, overrides = {}, {}
    for item in root:
        ctype = item.get('ContentType', '')
        if not ctype:
            fail('docx_invalid_package')
        if 'macroenabled' in ctype.lower() or 'vbaproject' in ctype.lower():
            fail('docx_macros_unsupported')
        if item.tag == '{' + CT + '}Override':
            name = item.get('PartName', '')
            if not name.startswith('/'):
                fail('docx_invalid_package')
            safe_name(name[1:])
            if name[1:] in overrides or name[1:] not in parts:
                fail('docx_invalid_package')
            overrides[name[1:]] = ctype
        elif item.tag == '{' + CT + '}Default':
            ext = item.get('Extension', '')
            if not ext or ext.lower() in defaults:
                fail('docx_invalid_package')
            defaults[ext.lower()] = ctype
        else:
            fail('docx_invalid_package')
    result = {}
    for name in parts:
        if name == '[Content_Types].xml':
            continue
        result[name] = overrides.get(name, defaults.get(name.rsplit('.', 1)[-1].lower(), ''))
        if not result[name]:
            fail('docx_invalid_package')
    return result


class TextReader:
    def __init__(self, root, roots, warnings, limits):
        self.ns = root.tag.partition('}')[0] + '}'
        self.warnings, self.limits = warnings, limits
        self.chars = 0
        self.blocks = []
        self.styles = {}
        for tree in roots.values():
            if tree.tag == self.q('styles'):
                for style in tree.findall(self.q('style')):
                    sid = style.get(self.q('styleId'), '')
                    parent = style.find(self.q('basedOn'))
                    self.styles[sid] = (style.find(self.q('pPr')),
                                       parent.get(self.q('val'), '') if parent is not None else '')

    def q(self, local):
        return self.ns + local

    def add_text(self, text):
        self.chars += len(text)
        check(self.limits, 'max_text_chars', self.chars)
        return text

    def inline(self, node):
        if not node.tag.startswith(self.ns):
            self.warnings.add('docx_equations_omitted' if node.tag.startswith(
                '{http://schemas.openxmlformats.org/officeDocument/2006/math}')
                else 'docx_unsupported_markup_omitted')
            return ''
        local = node.tag[len(self.ns):]
        if node.tag == self.q('t'):
            return self.add_text(node.text or '')
        if local in ('tab', 'br', 'cr'):
            return self.add_text('\t' if local == 'tab' else '\n')
        if local in ('noBreakHyphen', 'softHyphen'):
            return self.add_text('\u2011' if local == 'noBreakHyphen' else '\u00ad')
        omitted = {'drawing': 'docx_images_omitted', 'pict': 'docx_images_omitted',
                   'object': 'docx_objects_omitted', 'del': 'docx_tracked_changes_omitted',
                   'ins': 'docx_tracked_changes_omitted', 'moveFrom': 'docx_tracked_changes_omitted',
                   'moveTo': 'docx_tracked_changes_omitted', 'fldSimple': 'docx_fields_omitted',
                   'instrText': 'docx_fields_omitted', 'altChunk': 'docx_embedded_content_omitted',
                   'footnoteReference': 'docx_notes_omitted', 'endnoteReference': 'docx_notes_omitted',
                   'commentReference': 'docx_comments_omitted', 'sym': 'docx_symbols_omitted'}
        if local in omitted:
            self.warnings.add(omitted[local])
            return ''
        if node.tag in (self.q('rPr'), self.q('pPr')):
            return ''
        if node.tag in (self.q('r'), self.q('hyperlink'), self.q('smartTag'), self.q('sdtContent')):
            return ''.join(self.inline(child) for child in node)
        # Unknown markup is never flattened as if it were ordinary body text.
        self.warnings.add('docx_unsupported_markup_omitted')
        return ''

    def property(self, paragraph, key):
        props = paragraph.find(self.q('pPr'))
        style = props.find(self.q('pStyle')) if props is not None else None
        sid = style.get(self.q('val'), '') if style is not None else ''
        seen = set()
        while True:
            found = props.find(self.q(key)) if props is not None else None
            if found is not None:
                return found
            if not sid or sid in seen or sid not in self.styles:
                return None
            seen.add(sid)
            props, sid = self.styles[sid]

    def paragraph(self, node, locator):
        # The body scanner also tracks fields spanning multiple paragraphs.
        if any(n.tag == self.q('fldChar') for n in node.iter()):
            self.warnings.add('docx_fields_omitted')
            return {'kind': 'paragraph', 'text': '', 'locator': locator}
        text = ''.join(self.inline(child) for child in node if child.tag != self.q('pPr'))
        result = {'kind': 'paragraph', 'text': text, 'locator': locator}
        outline = self.property(node, 'outlineLvl')
        if outline is not None:
            raw = outline.get(self.q('val'), '')
            if raw not in tuple(str(i) for i in range(10)):
                fail('docx_invalid_structure')
            if int(raw) < 9:
                result.update(kind='heading', level=int(raw) + 1)
        num = self.property(node, 'numPr')
        if num is not None:
            numid, level = num.find(self.q('numId')), num.find(self.q('ilvl'))
            if numid is not None and numid.get(self.q('val')) != '0':
                result.update(kind='list_item', num_id=numid.get(self.q('val'), ''),
                              level=level.get(self.q('val'), '0') if level is not None else '0')
                self.warnings.add('docx_list_labels_not_reconstructed')
        return result

    def table(self, table, locator):
        rows = []
        for ri, row in enumerate(table):
            if row.tag in (self.q('tblPr'), self.q('tblGrid')):
                continue
            if row.tag != self.q('tr'):
                self.warnings.add('docx_unsupported_markup_omitted')
                continue
            cells = []
            for ci, cell in enumerate(row):
                if cell.tag == self.q('trPr'):
                    if len(cell): self.warnings.add('docx_table_layout_omitted')
                    continue
                if cell.tag != self.q('tc'):
                    self.warnings.add('docx_unsupported_markup_omitted')
                    continue
                texts = []
                for pi, p in enumerate(cell):
                    if p.tag == self.q('p'):
                        texts.append(self.paragraph(p, f'{locator}/tr[{ri}]/tc[{ci}]/p[{pi}]')['text'])
                    elif p.tag == self.q('tcPr'):
                        if len(p): self.warnings.add('docx_table_layout_omitted')
                    else:
                        self.warnings.add('docx_complex_tables_omitted')
                cells.append('\n'.join(texts))
            rows.append(cells)
        return {'kind': 'table', 'rows': rows, 'text': '\n'.join('\t'.join(r) for r in rows),
                'locator': locator}

    def read(self, body, main):
        # fldChar fields may cross paragraphs: omit the complete covered range,
        # including the cached result; never present it as evaluated field content.
        field_depth = 0
        for index, node in enumerate(body):
            locator = main + '#body/' + str(index)
            field_nodes = [n for n in node.iter() if n.tag == self.q('fldChar')]
            omit = field_depth > 0 or bool(field_nodes)
            for field in field_nodes:
                kind = field.get(self.q('fldCharType'))
                if kind == 'begin': field_depth += 1
                elif kind == 'end':
                    field_depth -= 1
                    if field_depth < 0: fail('docx_invalid_structure')
                elif kind != 'separate': fail('docx_invalid_structure')
            if omit:
                self.warnings.add('docx_fields_omitted')
                continue
            if node.tag == self.q('p'):
                self.blocks.append(self.paragraph(node, locator))
            elif node.tag == self.q('tbl'):
                self.blocks.append(self.table(node, locator))
            elif node.tag == self.q('sectPr'):
                continue
            else:
                self.warnings.add('docx_unsupported_markup_omitted')
                self.inline(node)  # record specific omissions, no unplaced text
        if field_depth:
            fail('docx_invalid_structure')
        rendered = []
        for block in self.blocks:
            prefix = '#'*block['level'] + ' ' if block['kind'] == 'heading' else ('- ' if block['kind'] == 'list_item' else '')
            rendered.append(prefix + block['text'])
        text = '\n'.join(rendered)
        check(self.limits, 'max_text_chars', len(text))
        if not any(block['text'].strip() for block in self.blocks):
            fail('document_no_text')
        return text


def extract(data, limits):
    limits = settings(limits)
    check(limits, 'max_input_bytes', len(data))
    if data.startswith(bytes.fromhex('d0cf11e0a1b11ae1')):
        fail('docx_encrypted_or_legacy')
    if expat is None:
        fail('document_reader_unavailable')
    try:
        parts = read_parts(data, limits)
        if '[Content_Types].xml' not in parts or '_rels/.rels' not in parts:
            fail('docx_invalid_package')
        if any(posixpath.basename(n).lower() == 'vbaproject.bin' for n in parts):
            fail('docx_macros_unsupported')
        budget = XmlBudget(limits)
        roots = {'[Content_Types].xml': budget.parse(parts['[Content_Types].xml'])}
        types = content_types(roots['[Content_Types].xml'], parts)
        for name, raw in parts.items():
            if name == '[Content_Types].xml': continue
            ctype = types[name]
            if name.lower().endswith(('.xml', '.rels')) or ctype.endswith('+xml') or ctype in ('application/xml', 'text/xml'):
                roots[name] = budget.parse(raw)
        warnings = {'docx_formatting_and_pagination_omitted'}
        main, linked = relationships(parts, roots, warnings)
        if types.get(main) != MAIN_TYPE or main not in roots:
            fail('docx_not_supported_document')
        root = roots[main]
        if root.tag not in ('{' + W + '}document', '{' + WS + '}document'):
            fail('docx_invalid_structure')
        ns = root.tag.partition('}')[0] + '}'
        bodies = root.findall(ns + 'body')
        if len(bodies) != 1:
            fail('docx_invalid_structure')
        for name in parts:
            if name not in ('[Content_Types].xml', '_rels/.rels', main) and not name.endswith('.rels'):
                warnings.add('docx_auxiliary_parts_omitted')
        style_parts = {target: roots[target] for kind, target in linked.get(main, [])
                       if kind in tuple(base.rsplit('/', 1)[0] + '/styles' for base in OFFICE)
                       and target in roots}
        for tree in roots.values():
            if tree.tag in (ns + 'comments',): warnings.add('docx_comments_omitted')
            if tree.tag in (ns + 'footnotes', ns + 'endnotes'): warnings.add('docx_notes_omitted')
            if tree.tag in (ns + 'hdr', ns + 'ftr'): warnings.add('docx_headers_footers_omitted')
        reader = TextReader(root, style_parts, warnings, limits)
        text = reader.read(bodies[0], main)
        return {'schema': 'loom.docx_basic.result/1', 'text': text, 'blocks': reader.blocks,
                'partial': True, 'warnings': sorted(warnings),
                'source': {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data), 'member': main},
                'extractor': 'loom.docx_basic/1', 'llm_calls': 0}
    except DocxError:
        raise
    except (zipfile.BadZipFile, zipfile.LargeZipFile, zlib.error, EOFError, struct.error, UnicodeError, ValueError, NotImplementedError, RuntimeError):
        fail('docx_invalid_container')


def read_file(path, limits):
    limits = settings(limits)
    # A capped read handles growth after stat and never modifies the source.
    with open(path, 'rb') as stream:
        size = limits['max_input_bytes']
        data = stream.read(size + 1) if size else stream.read()
    return extract(data, limits)


def main(argv):
    try:
        if len(argv) != 3:
            fail('docx_configuration_invalid')
        limits = settings(json.loads(argv[2]))
        if argv[1] == '-':
            cap = limits['max_input_bytes']
            data = sys.stdin.buffer.read(cap + 1) if cap else sys.stdin.buffer.read()
            result = extract(data, limits)
        else:
            result = read_file(argv[1], limits)
        print(json.dumps(result, ensure_ascii=True, separators=(',', ':')))
        return 0
    except DocxError as error:
        result = {'error': {'code': error.code}}
    except OSError:
        result = {'error': {'code': 'docx_input_unavailable'}}
    except (ValueError, TypeError):
        result = {'error': {'code': 'docx_configuration_invalid'}}
    print(json.dumps(result, separators=(',', ':')))
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
