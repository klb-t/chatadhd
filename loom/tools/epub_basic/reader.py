#!/usr/bin/env python3
"""Local EPUB ZIP/XML projection; no extraction, network, execution or DB writes.

ZIP and XML safety mechanisms follow the existing bounded DOCX reader. Limits
are trusted caller data (zero disables a ceiling); unsafe paths, declarations,
encryption and ambiguous ZIP aliases are always refused. Original bytes and
member/XML locators remain distinct from derived UTF-8 byte coordinates.
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

CONTAINER = 'urn:oasis:names:tc:opendocument:xmlns:container'
OPF = 'http://www.idpf.org/2007/opf'
XHTML = 'http://www.w3.org/1999/xhtml'
LIMIT_KEYS = ('max_input_bytes', 'max_total_uncompressed_bytes', 'max_entry_bytes',
              'max_entries', 'max_ratio', 'max_xml_depth', 'max_xml_nodes', 'max_text_chars')


class EpubError(Exception):
    def __init__(self, code):
        self.code = code
        super().__init__(code)


def fail(code):
    raise EpubError(code)


def settings(values):
    if not isinstance(values, dict) or values.get('schema') != 'loom.epub_basic.limits/1':
        fail('epub_configuration_invalid')
    for key in LIMIT_KEYS + ('read_chunk_bytes',):
        value = values.get(key)
        if type(value) is not int or value < 0:
            fail('epub_configuration_invalid')
    if type(values.get('rootfile_index', 0)) is not int or values.get('rootfile_index', 0) < 0:
        fail('epub_configuration_invalid')
    if values['read_chunk_bytes'] == 0:
        fail('epub_configuration_invalid')
    return values


def check(limits, key, value):
    if limits[key] and value > limits[key]:
        fail('epub_limit_' + key)


def safe_name(name):
    # OPC part names are URIs; validate decoded spelling too (no aliases).
    try:
        decoded = urllib.parse.unquote(name, errors='strict')
    except UnicodeError:
        fail('epub_unsafe_path')
    for value in (name, decoded):
        if (not value or value.startswith('/') or '\\' in value or ':' in value
                or '?' in value or '#' in value or any(ord(c) < 32 or ord(c) == 127 for c in value)
                or any(p in ('', '.', '..') for p in value.rstrip('/').split('/'))):
            fail('epub_unsafe_path')
    return decoded.rstrip('/')


def central_directory(data, limits):
    # Count actual directory records BEFORE ZipFile allocates its entry objects.
    end = data.rfind(b'PK\x05\x06', max(0, len(data) - 65557))
    if end < 0 or end + 22 > len(data):
        fail('epub_invalid_container')
    _, disk, cd_disk, n_disk, count, size, offset, comment = struct.unpack_from('<4s4H2IH', data, end)
    if end + 22 + comment != len(data) or disk or cd_disk or n_disk != count:
        fail('epub_invalid_container')
    if count == 0xffff or size == 0xffffffff or offset == 0xffffffff:
        fail('epub_zip64_unsupported')
    if offset + size != end or not data.startswith(b'PK\x03\x04'):
        fail('epub_invalid_container')
    check(limits, 'max_entries', count)
    at, actual = offset, 0
    while at < end:
        if at + 46 > end or data[at:at + 4] != b'PK\x01\x02':
            fail('epub_invalid_container')
        name, extra, comm = struct.unpack_from('<3H', data, at + 28)
        at += 46 + name + extra + comm
        actual += 1
        check(limits, 'max_entries', actual)
    if at != end or actual != count:
        fail('epub_invalid_container')


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
            if 'http://www.w3.org/XML/1998/namespace}base' in attrs:
                fail('epub_xml_base_unsupported')
            check(self.limits, 'max_xml_depth', depth)
            check(self.limits, 'max_xml_nodes', self.nodes)
            tree.start(qname(name), {qname(k): v for k, v in attrs.items()})

        def end(name):
            nonlocal depth
            tree.end(qname(name))
            depth -= 1

        def forbidden(*_args):
            fail('epub_unsafe_xml')

        parser.StartElementHandler = start
        parser.EndElementHandler = end
        parser.CharacterDataHandler = tree.data
        parser.StartDoctypeDeclHandler = forbidden
        parser.EntityDeclHandler = forbidden
        parser.ExternalEntityRefHandler = forbidden
        parser.SkippedEntityHandler = forbidden
        parser.SetParamEntityParsing(expat.XML_PARAM_ENTITY_PARSING_NEVER)
        try:
            for offset in range(0, len(data), self.limits['read_chunk_bytes']):
                parser.Parse(data[offset:offset + self.limits['read_chunk_bytes']], False)
            parser.Parse(b'', True)
            return tree.close()
        except (expat.ExpatError, ET.ParseError):
            fail('epub_invalid_xml')


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
                fail('epub_duplicate_or_unsafe_part')
            aliases.add(canonical)
            if info.flag_bits & (1 | 64):
                fail('epub_encrypted')
            mode = info.external_attr >> 16
            if stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR):
                fail('epub_unsafe_path')
            if info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                fail('epub_compression_unsupported')
            check(limits, 'max_entry_bytes', info.file_size)
            total += info.file_size
            check(limits, 'max_total_uncompressed_bytes', total)
            if limits['max_ratio'] and info.file_size > limits['max_ratio'] * max(1, info.compress_size):
                fail('epub_limit_max_ratio')
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
                fail('epub_invalid_container')
            flags, method = struct.unpack_from('<HH', data, at + 6)
            if flags & (1 | 64):
                fail('epub_encrypted')
            if flags != info.flag_bits or method != info.compress_type:
                fail('epub_invalid_container')
            nlen, xlen = struct.unpack_from('<HH', data, at + 26)
            at += 30 + nlen + xlen
            end = at + info.compress_size
            if end > len(data):
                fail('epub_invalid_container')
            chunks, size, crc = [], 0, 0
            inflater = zlib.decompressobj(-15) if method == zipfile.ZIP_DEFLATED else None
            pending = b''
            while at < end or pending or (inflater is not None and not inflater.eof):
                if not pending:
                    take = min(limits['read_chunk_bytes'], end - at)
                    pending = data[at:at + take]
                    at += take
                if inflater is not None:
                    chunk = inflater.decompress(pending, limits['read_chunk_bytes'])
                    pending = inflater.unconsumed_tail
                    if not chunk and not pending and at == end and not inflater.eof:
                        fail('epub_invalid_container')
                    if inflater.unused_data:
                        fail('epub_invalid_container')
                else:
                    chunk, pending = pending, b''
                size += len(chunk)
                actual_total += len(chunk)
                check(limits, 'max_entry_bytes', size)
                check(limits, 'max_total_uncompressed_bytes', actual_total)
                if limits['max_ratio'] and size > limits['max_ratio'] * max(1, info.compress_size):
                    fail('epub_limit_max_ratio')
                if size > info.file_size:
                    fail('epub_invalid_container')
                crc = zlib.crc32(chunk, crc)
                chunks.append(chunk)
            if (size != info.file_size or crc != info.CRC
                    or (inflater is not None and not inflater.eof)):
                fail('epub_invalid_container')
            if info.is_dir():
                if size:
                    fail('epub_invalid_container')
            else:
                parts[info.filename] = b''.join(chunks)
    return parts


def local_member(base, href):
    """Resolve one publication URI without ever consulting the filesystem."""
    try:
        uri = urllib.parse.urlsplit(href)
        decoded = urllib.parse.unquote(uri.path, errors='strict')
    except (ValueError, UnicodeError):
        fail('epub_unsafe_path')
    if (not decoded or uri.scheme or uri.netloc or uri.query or uri.fragment
            or href.startswith('/') or decoded.startswith('/') or '\\' in decoded
            or ':' in decoded or any(ord(c) < 32 or ord(c) == 127 for c in href + decoded)):
        fail('epub_unsafe_path')
    # Relative .. may legitimately refer to a sibling directory in an EPUB;
    # crossing the archive root is forbidden. ZIP entry names never allow .. .
    path = base.split('/') if base else []
    for item in decoded.split('/'):
        if item == '..':
            if not path:
                fail('epub_unsafe_path')
            path.pop()
        elif item not in ('', '.'):
            path.append(item)
    name = '/'.join(path)
    safe_name(name)
    return name


def one(parent, tag):
    found = parent.findall(tag)
    if len(found) != 1:
        fail('epub_invalid_structure')
    return found[0]


def publication(parts, budget):
    if parts.get('mimetype') != b'application/epub+zip':
        fail('epub_invalid_package')
    if 'META-INF/encryption.xml' in parts:
        fail('epub_encrypted')
    raw = parts.get('META-INF/container.xml')
    if raw is None:
        fail('epub_missing_member')
    container = budget.parse(raw)
    if container.tag != '{' + CONTAINER + '}container':
        fail('epub_invalid_structure')
    rootfiles = one(container, '{' + CONTAINER + '}rootfiles').findall('{' + CONTAINER + '}rootfile')
    candidates = [node for node in rootfiles if node.get('media-type') == 'application/oebps-package+xml']
    if not candidates:
        fail('epub_invalid_package')
    # Rendition selection is trusted caller data, never ZIP order or title.
    selected = budget.limits.get('rootfile_index', 0)
    if selected >= len(candidates):
        fail('epub_rendition_unavailable')
    main = local_member('', candidates[selected].get('full-path', ''))
    if main not in parts:
        fail('epub_missing_member')
    root = budget.parse(parts[main])
    if root.tag != '{' + OPF + '}package':
        fail('epub_invalid_structure')
    manifest = one(root, '{' + OPF + '}manifest')
    spine = one(root, '{' + OPF + '}spine')
    items = {}
    for node in manifest:
        if node.tag != '{' + OPF + '}item':
            fail('epub_invalid_structure')
        key = node.get('id', '')
        if not key or key in items:
            fail('epub_invalid_manifest')
        href = node.get('href', '')
        # Remote resources are never fetched, including non-spine assets.
        # They remain declared but unsupported in this basic projection.
        try:
            remote = urllib.parse.urlsplit(href)
        except ValueError:
            fail('epub_unsafe_path')
        name = None if remote.scheme or remote.netloc else local_member(posixpath.dirname(main), href)
        items[key] = (name, node.get('media-type'))
    chapters = []
    for index, node in enumerate(spine):
        if node.tag != '{' + OPF + '}itemref':
            fail('epub_invalid_structure')
        ref = node.get('idref', '')
        if ref not in items:
            fail('epub_invalid_spine')
        name, media = items[ref]
        if name is None:
            fail('epub_external_spine')
        if media != 'application/xhtml+xml':
            fail('epub_spine_type_unsupported')
        if name not in parts:
            fail('epub_missing_member')
        linear = node.get('linear', 'yes')
        if linear not in ('yes', 'no'):
            fail('epub_invalid_spine')
        chapters.append({'index': index, 'idref': ref, 'member': name,
                         'linear': linear, 'locator': name + '#spine/' + str(index)})
    if not chapters:
        fail('epub_invalid_spine')
    warnings = {'epub_layout_and_pagination_omitted'}
    if len(candidates) > 1:
        warnings.add('epub_selected_rendition_only')
    if any(name is None for name, _ in items.values()):
        warnings.add('epub_external_resources_omitted')
    return main, chapters, warnings, {main: root, 'META-INF/container.xml': container}


def xhtml_blocks(root, chapter, warnings, limits, text_count):
    if root.tag != '{' + XHTML + '}html':
        fail('epub_invalid_xhtml')
    body = one(root, '{' + XHTML + '}body')
    blocks = []
    recognized = {'p': 'paragraph', 'pre': 'paragraph', 'li': 'list_item',
                  **{'h' + str(n): 'heading' for n in range(1, 7)}}
    omitted = {'script', 'style', 'svg', 'math', 'iframe', 'object', 'embed', 'img', 'audio', 'video'}

    def local(node):
        return node.tag[len(XHTML) + 2:] if node.tag.startswith('{' + XHTML + '}') else ''

    def inline(node):
        pieces, work = [], [('node', node)]
        while work:
            kind, value = work.pop()
            if kind == 'text':
                pieces.append(value)
                continue
            tag = local(value)
            if tag in omitted or not tag:
                warnings.add('epub_nontext_or_active_content_omitted')
                continue
            if tag == 'br':
                pieces.append('\n')
                continue
            pieces.append(value.text or '')
            for child in reversed(value):
                work.append(('text', child.tail or ''))
                work.append(('node', child))
        return ''.join(pieces)

    # Iterative tree walk avoids Python recursion ceilings even when callers
    # deliberately disable/increase the configured XML depth ceiling.
    nested = {}
    for node in reversed(list(body.iter())):
        nested[node] = any(local(child) in recognized or nested[child] for child in node)
    pending = [(body, [list(root).index(body)])]
    while pending:
        node, path = pending.pop()
        tag = local(node)
        if tag in omitted or not tag:
            warnings.add('epub_nontext_or_active_content_omitted')
            continue
        if tag in recognized and not nested[node]:
            # Nested blocks (e.g. li > p) are separate recognized occurrences;
            # never duplicate their text in an encompassing list item.
            value = inline(node)
            if tag != 'pre':
                # XHTML whitespace normalization, not an original byte range.
                value = ' '.join(value.split())
            text_count[0] += len(value)
            check(limits, 'max_text_chars', text_count[0])
            locator = chapter['locator'] + '/xhtml/' + '/'.join(map(str, path))
            block = {'kind': recognized[tag], 'text': value, 'locator': locator,
                     'member': chapter['member'], 'chapter_index': chapter['index'],
                     'xml_path': path, 'ordinal': len(blocks)}
            if tag.startswith('h'):
                block['level'] = int(tag[1:])
            blocks.append(block)
        else:
            if (node.text or '').strip() or any((child.tail or '').strip() for child in node):
                warnings.add('epub_unplaced_text_omitted')
            for index in range(len(node) - 1, -1, -1):
                pending.append((node[index], path + [index]))
    return blocks


def project(blocks, chapters, limits):
    rendered, ranges, offset, chars = [], [], 0, 0
    for index, block in enumerate(blocks):
        prefix = '#' * block['level'] + ' ' if block['kind'] == 'heading' else ('- ' if block['kind'] == 'list_item' else '')
        separator = '\n' if index + 1 < len(blocks) else ''
        value = prefix + block['text'] + separator
        chars += len(value)
        check(limits, 'max_text_chars', chars)
        start = offset + len(prefix.encode('utf-8'))
        stop = start + len(block['text'].encode('utf-8'))
        end = offset + len(value.encode('utf-8'))
        ranges.append({'byte_range': [offset, end], 'text_range': [start, stop]})
        rendered.append(value)
        offset = end
    for chapter in chapters:
        first, count = chapter['first_block'], chapter['block_count']
        start = ranges[first]['byte_range'][0] if first < len(ranges) else offset
        end = ranges[first + count - 1]['byte_range'][1] if count else start
        chapter['byte_range'] = [start, end]
    text = ''.join(rendered)
    return text, {'schema': 'loom.epub_basic.projection/1', 'coordinate': 'derived_utf8_bytes',
                  'sha256': hashlib.sha256(text.encode('utf-8')).hexdigest(), 'size': offset,
                  'ranges': ranges}


def extract(data, limits):
    limits = settings(limits)
    check(limits, 'max_input_bytes', len(data))
    try:
        parts = read_parts(data, limits)
        budget = XmlBudget(limits)
        main, chapters, warnings, roots = publication(parts, budget)
        # Parse every XML member to reject hostile unused XML as well. These
        # roots share one node budget across the publication. No XInclude,
        # schema validation, external entity resolution or resource loading.
        used = set(roots)
        chapter_members = {c['member'] for c in chapters}
        for name, raw in parts.items():
            if name not in used and (name.lower().endswith(('.xml', '.xhtml', '.html', '.opf', '.ncx', '.svg'))
                                     or name in chapter_members):
                roots[name] = budget.parse(raw)
        blocks, text_count = [], [0]
        for chapter in chapters:
            chapter['first_block'] = len(blocks)
            found = xhtml_blocks(roots[chapter['member']], chapter, warnings, limits, text_count)
            chapter['block_count'] = len(found)
            blocks.extend(found)
        text, projection = project(blocks, chapters, limits)
        if not any(block['text'].strip() for block in blocks):
            fail('epub_no_text')
        return {'schema': 'loom.epub_basic.result/1', 'extractor': 'loom.epub_basic/1',
                'source': {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data), 'member': main},
                'partial': True, 'warnings': sorted(warnings), 'text': text,
                'chapters': chapters, 'blocks': blocks, 'projection': projection}
    except (zipfile.BadZipFile, zlib.error, struct.error, NotImplementedError, EOFError, ValueError):
        fail('epub_invalid_container')
    except RecursionError:
        fail('epub_structure_depth_unsupported')


def read_input(stream, limits):
    data, size = [], 0
    while True:
        # Do not allocate more than one byte beyond the caller's input budget.
        take = limits['read_chunk_bytes']
        if limits['max_input_bytes']:
            take = min(take, max(1, limits['max_input_bytes'] - size + 1))
        chunk = stream.read(take)
        if not chunk:
            return b''.join(data)
        size += len(chunk)
        check(limits, 'max_input_bytes', size)
        data.append(chunk)


def main(argv):
    try:
        if len(argv) != 3:
            fail('epub_configuration_invalid')
        try:
            limits = settings(json.loads(argv[2]))
        except (ValueError, TypeError):
            fail('epub_configuration_invalid')
        if argv[1] == '-':
            data = read_input(sys.stdin.buffer, limits)
        else:
            with open(argv[1], 'rb') as stream:
                data = read_input(stream, limits)
        result = extract(data, limits)
        status = 0
    except EpubError as error:
        result, status = {'error': {'code': error.code}}, 1
    except OSError:
        result, status = {'error': {'code': 'epub_input_unavailable'}}, 1
    sys.stdout.write(json.dumps(result, ensure_ascii=True, sort_keys=True, separators=(',', ':')) + '\n')
    return status


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
