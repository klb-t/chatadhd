"""ZIP/XML safety primitives, adapted from the accepted DOCX reader at e2f6873c.

Kept local so deploying XLSX does not require another format's runtime package.
No extraction to disk. Actual inflated bytes, not only ZIP claims, are bounded.
"""
import io
import stat
import struct
import urllib.parse
import xml.etree.ElementTree as ET
import zipfile
import zlib
from xml.parsers import expat


class XlsxError(ValueError):
    def __init__(self, code):
        self.code = code
        super().__init__(code)


def fail(code):
    raise XlsxError(code)


def check(limits, key, value):
    if limits[key] and value > limits[key]:
        fail('xlsx_limit_' + key)


def safe_name(name):
    # OPC part names are URIs; validate decoded spelling too (no aliases).
    decoded = urllib.parse.unquote(name, errors='strict')
    for value in (name, decoded):
        if (not value or value.startswith('/') or '\\' in value or ':' in value
                or '?' in value or '#' in value or any(ord(c) < 32 or ord(c) == 127 for c in value)
                or any(p in ('', '.', '..') for p in value.rstrip('/').split('/'))):
            fail('xlsx_unsafe_path')
    return decoded.rstrip('/')


def central_directory(data, limits):
    # Count actual directory records BEFORE ZipFile allocates its entry objects.
    end = data.rfind(b'PK\x05\x06', max(0, len(data) - 65557))
    if end < 0 or end + 22 > len(data):
        fail('xlsx_invalid_container')
    _, disk, cd_disk, n_disk, count, size, offset, comment = struct.unpack_from('<4s4H2IH', data, end)
    if end + 22 + comment != len(data) or disk or cd_disk or n_disk != count:
        fail('xlsx_invalid_container')
    if count == 0xffff or size == 0xffffffff or offset == 0xffffffff:
        fail('xlsx_zip64_unsupported')
    if offset + size != end or not data.startswith(b'PK\x03\x04'):
        fail('xlsx_invalid_container')
    check(limits, 'max_entries', count)
    at, actual = offset, 0
    while at < end:
        if at + 46 > end or data[at:at + 4] != b'PK\x01\x02':
            fail('xlsx_invalid_container')
        name, extra, comm = struct.unpack_from('<3H', data, at + 28)
        at += 46 + name + extra + comm
        actual += 1
        check(limits, 'max_entries', actual)
    if at != end or actual != count:
        fail('xlsx_invalid_container')


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
            fail('xlsx_unsafe_xml')

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
            fail('xlsx_invalid_xml')


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
                fail('xlsx_duplicate_or_unsafe_part')
            aliases.add(canonical)
            if info.flag_bits & (1 | 64):
                fail('xlsx_encrypted')
            mode = info.external_attr >> 16
            if stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR):
                fail('xlsx_unsafe_path')
            if info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                fail('xlsx_compression_unsupported')
            check(limits, 'max_entry_bytes', info.file_size)
            total += info.file_size
            check(limits, 'max_total_uncompressed_bytes', total)
            if limits['max_ratio'] and info.file_size > limits['max_ratio'] * max(1, info.compress_size):
                fail('xlsx_limit_max_ratio')
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
                fail('xlsx_invalid_container')
            flags, method = struct.unpack_from('<HH', data, at + 6)
            if flags & (1 | 64):
                fail('xlsx_encrypted')
            if flags != info.flag_bits or method != info.compress_type:
                fail('xlsx_invalid_container')
            nlen, xlen = struct.unpack_from('<HH', data, at + 26)
            at += 30 + nlen + xlen
            end = at + info.compress_size
            if end > len(data):
                fail('xlsx_invalid_container')
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
                        fail('xlsx_invalid_container')
                else:
                    chunk, pending = pending, b''
                size += len(chunk)
                actual_total += len(chunk)
                check(limits, 'max_entry_bytes', size)
                check(limits, 'max_total_uncompressed_bytes', actual_total)
                if limits['max_ratio'] and size > limits['max_ratio'] * max(1, info.compress_size):
                    fail('xlsx_limit_max_ratio')
                if size > info.file_size:
                    fail('xlsx_invalid_container')
                crc = zlib.crc32(chunk, crc)
                chunks.append(chunk)
            if (size != info.file_size or crc != info.CRC
                    or (inflater is not None and not inflater.eof)):
                fail('xlsx_invalid_container')
            if info.is_dir():
                if size:
                    fail('xlsx_invalid_container')
            else:
                parts[info.filename] = b''.join(chunks)
    return parts
