"""Deterministic CFB/BIFF8 writer for mechanism tests only; no XLS reader."""
import struct


def legacy_xls():
    rec = lambda tag, body=b'': struct.pack('<HH', tag, len(body)) + body
    bof = lambda typ: rec(0x809, struct.pack('<HHHHII', 0x600, typ, 0xdbb, 0x7cc, 0x41, 6))
    sst = rec(0xfc, struct.pack('<IIHB', 1, 1, 1, 0) + b'A')
    bound = lambda offset: rec(0x85, struct.pack('<IBBBB', offset, 0, 0, 1, 0) + b'S')
    glob = bof(5) + bound(0) + sst + rec(0xa)
    glob = bof(5) + bound(len(glob)) + sst + rec(0xa)
    sheet = (bof(0x10) + rec(0x200, struct.pack('<IIHHH', 0, 1, 0, 2, 0)) +
             rec(0xfd, struct.pack('<HHHI', 0, 0, 0, 0)) +
             rec(0x203, struct.pack('<HHHd', 0, 1, 0, 2.0)) + rec(0xa))
    end, free = 0xfffffffe, 0xffffffff
    header = bytearray(512)
    header[:8] = bytes.fromhex('d0cf11e0a1b11ae1')
    struct.pack_into('<HHHHH', header, 24, 0x3e, 3, 0xfffe, 9, 6)
    struct.pack_into('<IIIIIIIII', header, 40, 0, 1, 0, 0, 4096, end, 0, end, 0)
    struct.pack_into('<109I', header, 76, 9, *([free] * 108))
    def entry(name, typ, child, start, size):
        data = bytearray(128)
        text = (name + '\0').encode('utf-16le')
        data[:len(text)] = text
        struct.pack_into('<HBBIII', data, 64, len(text), typ, 1, free, free, child)
        struct.pack_into('<IQ', data, 116, start, size)
        return data
    directory = (entry('Root Entry', 5, 1, end, 0) +
                 entry('Workbook', 2, free, 1, 4096) + bytes(256))
    fat = [end] + list(range(2, 9)) + [end, 0xfffffffd] + [free] * 118
    return bytes(header) + directory + (glob + sheet).ljust(4096, b'\0') + struct.pack('<128I', *fat)
