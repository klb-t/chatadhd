"""ZIP admission and bounded full-stream validation. Never extracts a path to disk."""

import io
import stat
import struct
import urllib.parse
import zipfile
import zlib


class OdtError(Exception):
    def __init__(self, code):
        self.code = code
        super().__init__(code)


def fail(code):
    raise OdtError(code)


LIMIT_KEYS = (
    'max_input_bytes', 'max_entries', 'max_entry_bytes',
    'max_total_uncompressed_bytes', 'max_ratio', 'max_xml_depth',
    'max_xml_nodes', 'max_units', 'max_table_cells',
    'max_projection_bytes', 'max_output_bytes', 'read_chunk_bytes',
)


def settings(values):
    if not isinstance(values, dict) or values.get('schema') != 'loom.odt_basic.limits/1':
        fail('odt_configuration_invalid')
    if set(values) != set(LIMIT_KEYS) | {'schema'}:
        fail('odt_configuration_invalid')
    for key in LIMIT_KEYS:
        if type(values[key]) is not int or values[key] < 0:
            fail('odt_configuration_invalid')
    if not values['read_chunk_bytes']:
        fail('odt_configuration_invalid')
    return dict(values)


def check(limits, key, value):
    if limits[key] and value > limits[key]:
        fail('odt_limit_' + key)


def safe_name(name):
    decoded = urllib.parse.unquote(name, errors='strict')
    for spelling in (name, decoded):
        if (not spelling or spelling.startswith('/') or '\\' in spelling
                or any(c in spelling for c in ':?#')
                or any(ord(c) < 32 or ord(c) == 127 for c in spelling)
                or any(p in ('', '.', '..') for p in spelling.rstrip('/').split('/'))):
            fail('odt_unsafe_path')
    return decoded.rstrip('/')


def central_directory(data, limits):
    # Count the real directory records before ZipFile allocates ZipInfo objects.
    # This bounded subset deliberately refuses ZIP64, split and appended archives.
    at = data.rfind(b'PK\x05\x06', max(0, len(data) - 65557))
    if at < 0 or at + 22 > len(data):
        fail('odt_invalid_zip')
    _, disk, cd_disk, local_count, count, size, offset, comment = struct.unpack_from('<4s4H2IH', data, at)
    if at + 22 + comment != len(data) or disk or cd_disk or local_count != count:
        fail('odt_invalid_zip')
    if count == 65535 or size == 0xffffffff or offset == 0xffffffff:
        fail('odt_zip64_unsupported')
    if offset + size != at or not data.startswith(b'PK\x03\x04'):
        fail('odt_invalid_zip')
    check(limits, 'max_entries', count)
    end, actual = at, 0
    at = offset
    while at < end:
        if at + 46 > end or data[at:at + 4] != b'PK\x01\x02':
            fail('odt_invalid_zip')
        nlen, xlen, clen = struct.unpack_from('<3H', data, at + 28)
        at += 46 + nlen + xlen + clen
        actual += 1
        check(limits, 'max_entries', actual)
    if at != end or actual != count:
        fail('odt_invalid_zip')
    return offset


def read_parts(data, limits):
    """Validate every stream, retain only mimetype/content.xml. Sizes are not trusted."""
    cd_offset = central_directory(data, limits)
    retained, aliases, declared_total = {}, set(), 0
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        infos = archive.infolist()
        for info in infos:
            name = info.orig_filename
            canonical = safe_name(name)
            if name != info.filename or canonical in aliases:
                fail('odt_duplicate_or_unsafe_part')
            aliases.add(canonical)
            if info.flag_bits & (1 | 64):
                fail('odt_encrypted_zip')
            if stat.S_IFMT(info.external_attr >> 16) not in (0, stat.S_IFREG, stat.S_IFDIR):
                fail('odt_unsafe_path')
            if info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                fail('odt_compression_unsupported')
            if info.extract_version >= 45:
                fail('odt_zip64_unsupported')
            if info.file_size > 0xffffffff or info.compress_size > 0xffffffff or info.header_offset > 0xffffffff:
                fail('odt_zip64_unsupported')
            check(limits, 'max_entry_bytes', info.file_size)
            declared_total += info.file_size
            check(limits, 'max_total_uncompressed_bytes', declared_total)
            if limits['max_ratio'] and info.file_size > limits['max_ratio'] * max(1, info.compress_size):
                fail('odt_limit_max_ratio')
        actual_total = 0
        spans = []
        for info in infos:
            # ZipFile checks local spelling, flags and overlap at open time, but
            # ZipExtFile truncates at the *claimed* file_size. Validate the entire
            # compressed stream ourselves so a forged small size cannot hide a bomb.
            with archive.open(info):
                pass
            header = info.header_offset
            if header < 0 or header + 30 > cd_offset or data[header:header + 4] != b'PK\x03\x04':
                fail('odt_invalid_zip')
            flags, method = struct.unpack_from('<HH', data, header + 6)
            if struct.unpack_from('<H', data, header + 4)[0] >= 45:
                fail('odt_zip64_unsupported')
            if flags != info.flag_bits or method != info.compress_type:
                fail('odt_invalid_zip')
            nlen, xlen = struct.unpack_from('<HH', data, header + 26)
            at = header + 30 + nlen + xlen
            end = at + info.compress_size
            if end > cd_offset:
                fail('odt_invalid_zip')
            spans.append((header, end))
            chunks, size, crc = [], 0, 0
            keep = info.filename in ('mimetype', 'content.xml')
            inflater = zlib.decompressobj(-15) if method == zipfile.ZIP_DEFLATED else None
            pending = b''
            while at < end or pending:
                if not pending:
                    take = min(limits['read_chunk_bytes'], end - at)
                    pending = data[at:at + take]
                    at += take
                if inflater:
                    chunk = inflater.decompress(pending, limits['read_chunk_bytes'])
                    pending = inflater.unconsumed_tail
                    if inflater.unused_data:
                        fail('odt_invalid_zip')
                else:
                    chunk, pending = pending, b''
                size += len(chunk)
                actual_total += len(chunk)
                check(limits, 'max_entry_bytes', size)
                check(limits, 'max_total_uncompressed_bytes', actual_total)
                if limits['max_ratio'] and size > limits['max_ratio'] * max(1, info.compress_size):
                    fail('odt_limit_max_ratio')
                if size > info.file_size:
                    fail('odt_zip_size_mismatch')
                crc = zlib.crc32(chunk, crc)
                if keep:
                    chunks.append(chunk)
            if size != info.file_size or crc != info.CRC or (inflater and not inflater.eof):
                fail('odt_invalid_zip')
            if info.is_dir() and size:
                fail('odt_invalid_zip')
            if keep:
                retained[info.filename] = b''.join(chunks)
        spans.sort()
        if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
            fail('odt_invalid_zip')
    return retained, len(infos)
