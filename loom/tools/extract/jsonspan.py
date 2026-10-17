"""Byte-exact streaming spans of a JSON array: the elements of a top-level
array (or of ``obj[wrapper]`` at depth 1) as the source's own bytes, plus the
bytes between them.  This is what makes the ``preserve`` cut 1:1: a selected
conversation is copied from its locator without being parsed and serialised
again, and with every element selected the output equals the input byte for
byte.  Memory grows with one element, not with the file (chunked reads; the
buffer is dropped after each element).  JSON structural characters are ASCII
and never occur inside a UTF-8 multi-byte sequence, so scanning bytes is safe.
"""
import json
import re
from typing import Any, BinaryIO, Iterator, Optional

_STRUCT = re.compile(rb'[\[\]{}"]')
_IN_STRING = re.compile(rb'["\\]')
_SCALAR_END = re.compile(rb'[,\]}\s]')
_NON_WS = re.compile(rb'[^\s]')
_WS = b" \t\r\n"


class SpanError(ValueError):
    pass


class _Buf:
    def __init__(self, fh: BinaryIO, chunk: int) -> None:
        self.fh, self.chunk = fh, chunk
        self.buf = b""
        self.base = 0
        self.eof = False

    def fill(self) -> bool:
        if self.eof:
            return False
        data = self.fh.read(self.chunk)
        if not data:
            self.eof = True
            return False
        self.buf += data
        return True

    def byte(self, pos: int) -> Optional[int]:
        while pos - self.base >= len(self.buf):
            if not self.fill():
                return None
        return self.buf[pos - self.base]

    def search(self, pattern: "re.Pattern[bytes]", pos: int) -> Optional[int]:
        while True:
            m = pattern.search(self.buf, max(0, pos - self.base))
            if m:
                return self.base + m.start()
            if not self.fill():
                return None

    def slice(self, a: int, b: int) -> bytes:
        while b - self.base > len(self.buf):
            if not self.fill():
                break
        return self.buf[a - self.base:b - self.base]

    def drop(self, upto: int) -> None:
        k = upto - self.base
        if k > 0:
            self.buf = self.buf[k:]
            self.base = upto

    def rest(self, pos: int) -> Iterator[bytes]:
        """Everything from ``pos`` to the end of the stream, in chunks."""
        if pos - self.base < len(self.buf):
            yield self.buf[pos - self.base:]
        self.buf, self.base = b"", pos
        while not self.eof:
            data = self.fh.read(self.chunk)
            if not data:
                self.eof = True
                break
            yield data


def _skip_ws(b: _Buf, pos: int) -> int:
    q = b.search(_NON_WS, pos)
    if q is None:
        return pos + max(0, len(b.buf) - (pos - b.base))
    return q


def _skip_string(b: _Buf, pos: int) -> int:
    p = pos + 1
    while True:
        q = b.search(_IN_STRING, p)
        if q is None:
            raise SpanError("unterminated string")
        if b.byte(q) == 0x5C:  # backslash
            p = q + 2
            continue
        return q + 1


def _skip_value(b: _Buf, pos: int) -> int:
    c = b.byte(pos)
    if c is None:
        raise SpanError("unexpected end of input")
    if c in (0x7B, 0x5B):  # { [
        depth = 0
        p = pos
        while True:
            q = b.search(_STRUCT, p)
            if q is None:
                raise SpanError("unterminated container")
            ch = b.byte(q)
            if ch == 0x22:
                p = _skip_string(b, q)
                continue
            depth += 1 if ch in (0x7B, 0x5B) else -1
            p = q + 1
            if depth == 0:
                return p
    if c == 0x22:
        return _skip_string(b, pos)
    q = b.search(_SCALAR_END, pos)
    if q is None:
        return b.base + len(b.buf)
    return q


def _find_wrapper_array(b: _Buf, wrapper: str) -> int:
    """Position of ``[`` of the value of key ``wrapper`` in the top-level object."""
    pos = _skip_ws(b, 0)
    if b.byte(pos) == 0xEF:  # UTF-8 BOM
        pos = _skip_ws(b, pos + 3)
    if b.byte(pos) != 0x7B:
        raise SpanError("top level is not an object")
    pos += 1
    while True:
        pos = _skip_ws(b, pos)
        c = b.byte(pos)
        if c == 0x7D or c is None:
            raise SpanError(f"key {wrapper!r} not found")
        if c == 0x2C:
            pos += 1
            continue
        if c != 0x22:
            raise SpanError("object key expected")
        end = _skip_string(b, pos)
        key = json.loads(b.slice(pos, end).decode("utf-8", errors="replace"))
        pos = _skip_ws(b, end)
        if b.byte(pos) != 0x3A:
            raise SpanError("':' expected")
        pos = _skip_ws(b, pos + 1)
        if key == wrapper and b.byte(pos) == 0x5B:
            return pos
        pos = _skip_value(b, pos)


def iter_spans(fh: BinaryIO, *, wrapper: Optional[str] = None, chunk: int = 1 << 20,
               lead: bool = False) -> Iterator[tuple[str, Optional[int], bytes]]:
    """Events ``("prefix", None, bytes)``, then per element ``("sep", i, bytes)`` (before element ``i > 0``)
    and ``("element", i, bytes)``, then ``("suffix", None, bytes)`` (possibly several chunks).
    Concatenating every event's bytes gives the input back exactly.  ``lead=True`` accepts text before
    the array (``window.YTD.x.part0 = [ … ]`` of an X archive): it becomes part of the prefix."""
    b = _Buf(fh, chunk)
    if wrapper:
        start = _find_wrapper_array(b, wrapper)
    else:
        start = _skip_ws(b, 0)
        if b.byte(start) == 0xEF:
            start = _skip_ws(b, start + 3)
        if b.byte(start) != 0x5B:
            q = b.search(re.compile(rb"\["), start) if lead else None
            if q is None:
                raise SpanError("top level is not an array")
            start = q
    pos = start + 1
    nxt = _skip_ws(b, pos)
    if b.byte(nxt) == 0x5D:  # empty array
        for chunk_bytes in b.rest(0):
            yield ("suffix", None, chunk_bytes)
        return
    yield ("prefix", None, b.slice(0, nxt))
    b.drop(nxt)
    i = 0
    pos = nxt
    while True:
        end = _skip_value(b, pos)
        yield ("element", i, b.slice(pos, end))
        b.drop(end)
        q = _skip_ws(b, end)
        c = b.byte(q)
        if c == 0x2C:
            nxt = _skip_ws(b, q + 1)
            yield ("sep", i + 1, b.slice(end, nxt))
            b.drop(nxt)
            pos = nxt
            i += 1
            continue
        if c == 0x5D or c is None:
            for chunk_bytes in b.rest(end):
                yield ("suffix", None, chunk_bytes)
            return
        raise SpanError(f"',' or ']' expected at byte {q}")


def filter_array(fh: BinaryIO, out: BinaryIO, keep: Any, *, wrapper: Optional[str] = None,
                 chunk: int = 1 << 20, lead: bool = False) -> dict[str, int]:
    """Copy the array keeping elements for which ``keep(i, raw_bytes)`` is true; separators are the
    source's own (the one that preceded the kept element; none before the first kept)."""
    kept = total = 0
    pending_sep: Optional[bytes] = None
    first_sep: Optional[bytes] = None
    for kind, idx, data in iter_spans(fh, wrapper=wrapper, chunk=chunk, lead=lead):
        if kind == "prefix" or kind == "suffix":
            out.write(data)
        elif kind == "sep":
            pending_sep = data
            if first_sep is None:
                first_sep = data
        elif kind == "element":
            total += 1
            if keep(idx, data):
                if kept:
                    out.write(pending_sep if pending_sep is not None else (first_sep or b","))
                out.write(data)
                kept += 1
            pending_sep = None
    return {"kept": kept, "total": total}
