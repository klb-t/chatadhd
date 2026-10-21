"""Shared pieces of the cut: package entries and their stable keys, safe names,
deterministic file and zip sinks, the package attachment store."""
import hashlib
import io
import re
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, BinaryIO, Optional

from ..model import Unit
from ..timestamps import key as _tskey

_SAFE = re.compile(r"[^A-Za-z0-9._-]+")
ZIP_EPOCH = (1980, 1, 1, 0, 0, 0)


def safe_name(text: str, *, fallback: str = "item") -> str:
    s = _SAFE.sub("_", text or "").strip("._") or fallback
    return s[:120]


_DIR_BAD = re.compile(r'[\x00-\x1f\x7f/\\:*?"<>|]+')
FS_COMPONENT_BYTES = 255  # the file-system limit of one path component (ext4, f2fs, NTFS), not a Loom limit


def dir_title(text: Optional[str], *, fallback: str = "bez_tytułu", max_chars: int = 80) -> str:
    """A conversation title as one file-name component: letters of any language stay, whitespace becomes ``_``,
    characters no file system accepts (``/ \\ : * ? " < > |``, control characters) are dropped.  ``max_chars`` 0 = no cut
    (the file-system limit of one component still applies)."""
    s = re.sub(r"\s+", "_", _DIR_BAD.sub(" ", text or "").strip()).strip("._")
    if max_chars and len(s) > max_chars:
        s = s[:max_chars]
    while len(s.encode("utf-8")) > FS_COMPONENT_BYTES - 40:  # room for the stamp and a uid suffix
        s = s[:-1]
    return s.strip("._") or fallback


def conversation_dirs(entries: list["Entry"], stamp: Any, title: Any, *, max_chars: int = 80) -> dict[str, str]:
    """``entry.key`` → ``<stamp>-<title>``, one directory per conversation directly under the package root.  Two
    conversations with the same stamp and title (case-insensitively) get the uid in front of the second one's end."""
    out: dict[str, str] = {}
    used: set[str] = set()
    for e in entries:
        name = f"{stamp(e)}-{dir_title(title(e), max_chars=max_chars)}"
        if name.casefold() in used:
            name = f"{name}-{uid_hex(e.unit.uid)[:8]}"
        n = 1
        base = name
        while name.casefold() in used:
            n += 1
            name = f"{base}-{n}"
        used.add(name.casefold())
        out[e.key] = name
    return out


# The phone app copies a picked file to its private storage as "<millis>__<original name>"; people know the file by the
# original name.  The pattern is data (presets/defaults.json cut.source_copy_prefix; "" = leave names as they are).
COPY_PREFIX_DEFAULT = r"^\d+__"


def original_name(label: str, prefix: Optional[str] = None) -> str:
    """The name a source file had before the app copied it (``1792554698851__a.zip`` -> ``a.zip``).  The ONE place that
    strips the copy prefix; a nested archive's label (``<outer>!<member>``) loses the prefix of its outer file only."""
    pat = COPY_PREFIX_DEFAULT if prefix is None else prefix
    if not pat:
        return label
    stripped = re.sub(pat, "", label, count=1)
    return stripped or label


def archive_slug(label: str, prefix: Optional[str] = None) -> str:
    return safe_name(original_name(label, prefix).replace("!", "__"), fallback="archive")


def copy_prefix(ds: Any) -> Optional[str]:
    return None if ds is None else ds.preset("cut.source_copy_prefix", COPY_PREFIX_DEFAULT)


def archive_slugs(archives: list[Any], ds: Any = None) -> dict[tuple[str, str], str]:
    """(label, sha256) → a directory/file name unique within the package, made from the file's original name (the app's
    copy prefix is gone).  Two exports named alike from different folders never overwrite each other: ``export.zip``,
    ``export-2.zip`` in scan order.  ``ds``: the data store the preset is read from (None = the built-in pattern)."""
    out: dict[tuple[str, str], str] = {}
    used: set[str] = set()
    prefix = copy_prefix(ds)
    for a in archives:
        base = archive_slug(a.label, prefix)
        name, n = base, 1
        while name in used:
            n += 1
            stem, dot, ext = base.rpartition(".")
            name = f"{stem}-{n}.{ext}" if dot and stem else f"{base}-{n}"
        used.add(name)
        out[(a.label, a.sha256)] = name
    return out


def find_archive(archives: list[Any], unit: Any) -> Any:
    src = unit.source or {}
    for a in archives:
        if a.label == src.get("archive") and a.sha256 == src.get("sha256"):
            return a
    return None


def uid_hex(uid: str) -> str:
    return uid.split(":", 1)[-1]


@dataclass
class Entry:
    """One conversation in the package (a copy; edits change it, never the source)."""
    unit: Unit
    messages: Optional[set[str]] = None        # None = whole conversation
    folder: str = "/"
    key: str = ""
    origin: str = ""                           # uid of the source unit (preserve looks it up)
    name: Optional[str] = None                 # renamed in the package (title override)
    notes: list[str] = field(default_factory=list)
    aliases: dict[str, dict[str, str]] = field(default_factory=dict)  # output field renames per kind (edit.rename_key in: "")

    def as_ref(self) -> dict[str, Any]:
        return {"key": self.key, "uid": self.unit.uid, "origin": self.origin, "folder": self.folder,
                "messages": sorted(self.messages) if self.messages is not None else None, "name": self.name}


def entry_sort_key(u: Unit, order: str) -> tuple:
    src = u.source or {}
    if order == "source_order":
        return (src.get("archive") or "", src.get("member") or "", str((src.get("locator") or {}).get("json_pointer") or ""), u.uid)
    if order == "updated":
        return (_tskey(u.updated_utc or u.created_utc), u.uid)
    if order == "title":
        return ((u.title or "").casefold(), u.uid)
    return (_tskey(u.created_utc), u.service or "", u.uid)


def assign_keys(entries: list[Entry]) -> None:
    for i, e in enumerate(entries, 1):
        e.key = f"{i:04d}-{uid_hex(e.unit.uid)[:8]}"


# ───────────────────────────── sinks ─────────────────────────────
class Outputs:
    """Collects written files with their sha256 and size (paths relative to the package)."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.items: list[dict[str, Any]] = []

    def write_bytes(self, rel: str, data: bytes, **meta: Any) -> dict[str, Any]:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_name(path.name + ".tmp")
        tmp.write_bytes(data)
        tmp.replace(path)
        return self.add(rel, hashlib.sha256(data).hexdigest(), len(data), **meta)

    def write_text(self, rel: str, text: str, **meta: Any) -> dict[str, Any]:
        # a lone surrogate (invalid UTF-16 in the source, kept in tool inputs) becomes its JSON escape, never a crash
        return self.write_bytes(rel, text.encode("utf-8", errors="backslashreplace"), **meta)

    def add_file(self, rel: str, **meta: Any) -> dict[str, Any]:
        """Register a file already written at ``root/rel``."""
        h = hashlib.sha256()
        n = 0
        with open(self.root / rel, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
                n += len(chunk)
        return self.add(rel, h.hexdigest(), n, **meta)

    def add(self, rel: str, sha: str, size: int, **meta: Any) -> dict[str, Any]:
        rec = {"path": rel, "sha256": sha, "bytes": size, **{k: v for k, v in meta.items() if v is not None}}
        self.items = [o for o in self.items if o["path"] != rel] + [rec]
        return rec


class DirSink:
    """Members written as files under a directory (dir / file sources)."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.written: list[str] = []

    def open(self, name: str, info: Any = None) -> BinaryIO:
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        self.written.append(name)
        return open(path, "wb")

    def close(self) -> None:
        return None


class ZipSink:
    """Members written into a zip with the source's member metadata (date, compression, attributes),
    so the same inputs give the same bytes."""

    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self.path = path
        self.zf = zipfile.ZipFile(path, "w", allowZip64=True)
        self.written: list[str] = []

    def open(self, name: str, info: Optional[zipfile.ZipInfo] = None) -> BinaryIO:
        zi = zipfile.ZipInfo(name, date_time=info.date_time if info is not None else ZIP_EPOCH)
        zi.compress_type = info.compress_type if info is not None else zipfile.ZIP_DEFLATED
        zi.external_attr = info.external_attr if info is not None else (0o644 << 16)
        zi.create_system = info.create_system if info is not None else 3
        self.written.append(name)
        big = info is not None and info.file_size > 0x7FFFFFFF
        return self.zf.open(zi, "w", force_zip64=big)  # type: ignore[return-value]

    def close(self) -> None:
        self.zf.close()


def deterministic_zip(path: Path, files: list[tuple[str, Path]]) -> None:
    """A zip of ``(name, source path)`` pairs in the given order with fixed metadata."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    with zipfile.ZipFile(tmp, "w", allowZip64=True) as zf:
        for name, src in files:
            zi = zipfile.ZipInfo(name, date_time=ZIP_EPOCH)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = 0o644 << 16
            zi.create_system = 3
            with open(src, "rb") as fh, zf.open(zi, "w", force_zip64=True) as out:
                for chunk in iter(lambda: fh.read(1 << 20), b""):
                    out.write(chunk)
    tmp.replace(path)


def copy_stream(src: BinaryIO, dst: BinaryIO) -> int:
    n = 0
    for chunk in iter(lambda: src.read(1 << 20), b""):
        dst.write(chunk)
        n += len(chunk)
    return n


def zip_info_of(archive: Any, name: str) -> Optional[zipfile.ZipInfo]:
    if archive.kind != "zip":
        return None
    try:
        return archive._zipfile().getinfo(name)
    except KeyError:
        return None


def bytes_io() -> io.BytesIO:
    return io.BytesIO()
