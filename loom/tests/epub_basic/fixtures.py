"""Deterministic synthetic EPUBs. No user documents or external data."""
import io
from pathlib import Path
import struct
import zipfile

CONTAINER = b'''<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OEBPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'''
OPF = b'''<package xmlns="http://www.idpf.org/2007/opf" version="3.0"><metadata/><manifest><item id="two" href="Text/two.xhtml" media-type="application/xhtml+xml"/><item id="one" href="Text/one.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="one"/><itemref idref="two" linear="no"/></spine></package>'''
ONE = '''<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Nie treść</title></head><body><h1>Rozdział pierwszy</h1><p id="pl">Zażółć <em>gęślą</em> jaźń.</p><p>Ten sam.</p><p>Ten sam.</p><p/><div><p>W środku.</p></div></body></html>'''.encode()
TWO = '''<html xmlns="http://www.w3.org/1999/xhtml"><head/><body><h2>Drugi</h2><p>Końcowy akapit.</p></body></html>'''.encode()
RENDERED = '# Rozdział pierwszy\nZażółć gęślą jaźń.\nTen sam.\nTen sam.\n\nW środku.\n## Drugi\nKońcowy akapit.'


def parts():
    # Intentionally reverse OPF spine and ZIP entry order.
    return [('mimetype', b'application/epub+zip'),
            ('OEBPS/Text/two.xhtml', TWO), ('OEBPS/Text/one.xhtml', ONE),
            ('OEBPS/book.opf', OPF), ('META-INF/container.xml', CONTAINER)]


def archive(entries=None, compression=zipfile.ZIP_DEFLATED):
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', compression=compression) as z:
        for name, raw in parts() if entries is None else entries:
            info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED if name == 'mimetype' else compression
            info.external_attr = 0o100644 << 16
            z.writestr(info, raw)
    return out.getvalue()


def changed(name, raw):
    return archive([(n, raw if n == name else value) for n, value in parts()])


def forged_prefix_size():
    """DEFLATE data expands beyond forged size with a valid prefix CRC."""
    import zlib
    raw = bytearray(archive(parts() + [('padding.bin', b'A' * 4096)]))
    local, central = raw.find(b'PK\x03\x04'), raw.find(b'PK\x01\x02')
    while raw[local + 30:local + 41] != b'padding.bin':
        n, x = struct.unpack_from('<HH', raw, local + 26)
        size = struct.unpack_from('<I', raw, local + 18)[0]
        local += 30 + n + x + size
    while raw[central + 46:central + 57] != b'padding.bin':
        n, x, c = struct.unpack_from('<3H', raw, central + 28)
        central += 46 + n + x + c
    for at, crc_off, size_off in [(local, 14, 22), (central, 16, 24)]:
        struct.pack_into('<I', raw, at + crc_off, zlib.crc32(b'A'))
        struct.pack_into('<I', raw, at + size_off, 1)
    return bytes(raw)


if __name__ == '__main__':
    # Small binaries are generated locally for native tests; no downloads.
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    args.destination.mkdir(parents=True, exist_ok=True)
    (args.destination / 'base.epub').write_bytes(archive())
    (args.destination / 'unsafe.epub').write_bytes(archive(parts() + [('../outside', b'no')]))
