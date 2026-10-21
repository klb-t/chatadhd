"""Detection of encoded data inside message blocks (base64 / data: URIs / hex / escapes).

Findings carry only the kind, the size, the decoded type and the place (conversation, message, block index): never the
content itself, so a report of findings leaks nothing.  Thresholds are parameters (defaults are presets, R42).
"""
import base64
import binascii
import json
import re
from typing import Any, Iterator, Optional

from .model import Unit

DEFAULTS = {"min_base64": 80, "min_hex": 64, "min_escapes": 12, "min_percent": 12}
KINDS = ("data_uri", "base64", "hex", "unicode_escape", "percent")

_DATA_URI = re.compile(r"data:([\w.+-]+/[\w.+-]+)?(;[\w=.+-]+)*;base64,([A-Za-z0-9+/=_-]{16,})")
_B64 = re.compile(r"(?<![A-Za-z0-9+/=_-])[A-Za-z0-9+/_-]{%d,}={0,2}(?![A-Za-z0-9+/=_-])")
_HEX = re.compile(r"(?<![0-9A-Fa-f])(?:[0-9A-Fa-f]{2}){%d,}(?![0-9A-Fa-f])")
_UESC = re.compile(r"(?:\\u[0-9A-Fa-f]{4}){%d,}")
_PCT = re.compile(r"(?:%%[0-9A-Fa-f]{2}){%d,}")

_MAGIC = ((b"\x89PNG\r\n\x1a\n", "image/png"), (b"\xff\xd8\xff", "image/jpeg"), (b"GIF8", "image/gif"),
          (b"%PDF", "application/pdf"), (b"PK\x03\x04", "application/zip"), (b"\x1f\x8b", "application/gzip"),
          (b"RIFF", "audio-or-image/riff"), (b"OggS", "audio/ogg"), (b"ID3", "audio/mpeg"))


def _decode_b64(s: str) -> Optional[bytes]:
    s = s.replace("-", "+").replace("_", "/")
    s += "=" * (-len(s) % 4)
    try:
        return base64.b64decode(s, validate=True)
    except (binascii.Error, ValueError):
        return None


def _classify(raw: bytes) -> str:
    for magic, mime in _MAGIC:
        if raw.startswith(magic):
            return mime
    try:
        txt = raw.decode("utf-8")
    except UnicodeDecodeError:
        return "binary"
    try:
        json.loads(txt)
        return "text/json"
    except ValueError:
        return "text/plain" if txt.isprintable() or "\n" in txt else "binary"


def _looks_like_b64(s: str) -> bool:
    # a long identifier / path / word is not base64: demand both letter cases or digits, and no long natural-word run
    return any(c.isdigit() for c in s) and any(c.islower() for c in s) and any(c.isupper() for c in s)


def scan_text(text: str, cfg: Optional[dict[str, int]] = None) -> Iterator[dict[str, Any]]:
    c = dict(DEFAULTS, **(cfg or {}))
    taken: list[tuple[int, int]] = []
    for m in _DATA_URI.finditer(text):
        raw = _decode_b64(m.group(3))
        taken.append(m.span())
        yield {"kind": "data_uri", "chars": len(m.group(3)), "decoded_bytes": len(raw) if raw is not None else None,
               "decoded_type": m.group(1) or (_classify(raw) if raw else "?")}
    for m in re.finditer(_B64.pattern % c["min_base64"], text):
        if any(a <= m.start() < b for a, b in taken) or not _looks_like_b64(m.group(0)):
            continue
        raw = _decode_b64(m.group(0).rstrip("="))
        if raw is None:
            continue
        taken.append(m.span())
        yield {"kind": "base64", "chars": len(m.group(0)), "decoded_bytes": len(raw), "decoded_type": _classify(raw)}
    for m in re.finditer(_HEX.pattern % (c["min_hex"] // 2), text):
        if any(a <= m.start() < b for a, b in taken):
            continue
        yield {"kind": "hex", "chars": len(m.group(0)), "decoded_bytes": len(m.group(0)) // 2, "decoded_type": None}
    for kind, pat, key in (("unicode_escape", _UESC, "min_escapes"), ("percent", _PCT, "min_percent")):
        for m in re.finditer(pat.pattern % c[key], text):
            yield {"kind": kind, "chars": len(m.group(0)), "decoded_bytes": None, "decoded_type": None}


def scan_unit(u: Unit, cfg: Optional[dict[str, int]] = None, kinds: Optional[tuple[str, ...]] = None) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for m in u.messages:
        for bi, b in enumerate(m.blocks):
            parts = [b.text or ""]
            if b.input is not None:
                parts.append(b.input if isinstance(b.input, str) else json.dumps(b.input, ensure_ascii=False, default=str))
            for text in parts:
                if len(text) < 12:
                    continue
                for f in scan_text(text, cfg):
                    if kinds and f["kind"] not in kinds:
                        continue
                    out.append(dict(f, message=m.id, block=bi, block_kind=b.kind))
    return out
