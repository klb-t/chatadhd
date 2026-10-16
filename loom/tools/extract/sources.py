"""Stage ``scan``: containers (file, directory, zip, zip-in-zip) and members, with
format recognised from CONTENT; plus streaming helpers for large JSON.

Every member gets a sha256 (streamed), size, mtime and a format id from
``data/formats/*.json`` whose ``sniff.any_of`` rules are evaluated here.  A
rule object may combine several conditions (all must hold).  Vocabulary:

  magic                   hex prefix of the bytes
  text_regex              regex over the decoded head (utf-8, errors replaced)
  html_contains           case-insensitive substring of the head
  extension               hint only; must be combined with a content condition
  json_object_keys_all / json_object_keys_any     top-level object keys
  json_array_first_keys_all / _any                keys of the first array element
  json_array_first_value  {"path", "regex"} on the first array element
  json_path_equals        {"path", "value"} on the top-level object
  json_wrapper_first_keys_all {"wrapper", "keys"}  first element of object[wrapper]
  jsonl_first_keys_all / _any                     keys of the first JSONL line
  jsonl_any_line_keys_all + jsonl_scan_lines      keys of any of the first N lines
  zip_member / zip_member_glob                    container-level (zip or dir)
  zip_member_json {"member", "array_first_keys_all"|"object_keys_all"}  content of a member
  dir_glob                container-level, directories only

Extension is never decisive on its own (an ``extension`` rule without another
condition is rejected at load time).
"""
import fnmatch
import hashlib
import io
import json
import logging
import os
import re
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Iterator, Optional

from .datafiles import DataFileError, DataStore, store as default_store

log = logging.getLogger(__name__)

_JSON_DECODER = json.JSONDecoder()


# ───────────────────────────── members and archives ─────────────────────────────
@dataclass
class Member:
    archive: "Archive"
    name: str                       # path inside the container ("" for a plain file)
    size: int
    sha256: str
    mtime: Optional[float]
    format: Optional[str] = None    # format id or None
    kind: str = "unknown"           # conversations | account | … | asset | unknown
    sniff_how: str = ""             # which rule matched (for the inventory)
    depth: int = 0

    @property
    def locator(self) -> dict[str, Any]:
        return {"archive": self.archive.label, "member": self.name}

    def open(self) -> io.BufferedIOBase:
        return self.archive.open_member(self.name)

    def read_bytes(self) -> bytes:
        with self.open() as fh:
            return fh.read()

    def head(self, n: int) -> bytes:
        with self.open() as fh:
            return fh.read(n)

    def materialize(self, tmpdir: str) -> str:
        """A real path on disk (needed by ``engine.importer``)."""
        if self.archive.kind in ("file", "dir") and self.archive.depth == 0:
            return str(self.archive.member_path(self.name))
        safe = re.sub(r"[^A-Za-z0-9._-]+", "_", self.name) or "member"
        target = Path(tmpdir) / f"{self.sha256[:12]}_{safe}"
        if not target.exists():
            with self.open() as src, open(target, "wb") as dst:
                for chunk in iter(lambda: src.read(1 << 20), b""):
                    dst.write(chunk)
        return str(target)

    def as_dict(self) -> dict[str, Any]:
        return {"name": self.name, "size": self.size, "sha256": self.sha256, "format": self.format,
                "kind": self.kind, "sniff": self.sniff_how}


@dataclass
class Archive:
    path: str                       # as given (or "<outer>!<member>" for nested zips)
    kind: str                       # file | dir | zip
    label: str                      # short name used in locators / outputs
    sha256: str = ""
    size: int = 0
    mtime: Optional[float] = None
    format: Optional[str] = None    # provider format (container-level) when recognised
    members: list[Member] = field(default_factory=list)
    depth: int = 0
    _zip_bytes: Optional[bytes] = None
    _root: Optional[Path] = None

    # -- access --------------------------------------------------------------------
    def member_path(self, name: str) -> Path:
        assert self._root is not None
        return self._root / name if name else self._root

    def open_member(self, name: str) -> io.BufferedIOBase:
        if self.kind == "zip":
            zf = self._zipfile()
            return zf.open(name, "r")  # type: ignore[return-value]
        return open(self.member_path(name), "rb")

    def _zipfile(self) -> zipfile.ZipFile:
        if self._zip_bytes is not None:
            return zipfile.ZipFile(io.BytesIO(self._zip_bytes))
        return zipfile.ZipFile(self.path)

    def names(self) -> list[str]:
        return [m.name for m in self.members]

    def member(self, name: str) -> Optional[Member]:
        for m in self.members:
            if m.name == name:
                return m
        return None

    def find_members(self, pattern: str) -> list[Member]:
        return [m for m in self.members if fnmatch.fnmatchcase(m.name, pattern)]

    def as_dict(self) -> dict[str, Any]:
        return {"path": self.path, "kind": self.kind, "label": self.label, "sha256": self.sha256, "size": self.size,
                "mtime": self.mtime, "format": self.format, "members": [m.as_dict() for m in self.members]}


# ───────────────────────────── hashing ─────────────────────────────
def sha256_stream(fh: io.BufferedIOBase) -> tuple[str, int]:
    h = hashlib.sha256()
    n = 0
    for chunk in iter(lambda: fh.read(1 << 20), b""):
        h.update(chunk)
        n += len(chunk)
    return h.hexdigest(), n


def sha256_file(path: Path) -> tuple[str, int]:
    with open(path, "rb") as fh:
        return sha256_stream(fh)


# ───────────────────────────── format catalogue ─────────────────────────────
class FormatCatalog:
    def __init__(self, ds: Optional[DataStore] = None) -> None:
        self.ds = ds or default_store()
        self.formats: dict[str, dict[str, Any]] = self.ds.entries("formats")
        for fid, f in self.formats.items():
            for rule in (f.get("sniff") or {}).get("any_of", []):
                if "extension" in rule and len([k for k in rule if k != "extension"]) == 0:
                    raise DataFileError(f"format {fid}: an 'extension' sniff rule needs a content condition")
        self.ordered = sorted(self.formats.values(), key=lambda f: (f.get("priority", 100), f["id"]))
        presets = self.ds.defaults()
        self.head_bytes = int((presets.get("sniff") or {}).get("head_bytes", 65536))
        self.json_whole_limit = int((presets.get("sniff") or {}).get("json_whole_limit_bytes", 1 << 26))
        self.asset_extensions = set((presets.get("assets") or {}).get("extensions", [
            ".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".svg", ".pdf", ".wav", ".mp3", ".m4a", ".ogg",
            ".mp4", ".webm", ".mov", ".dat", ".csv", ".docx", ".xlsx", ".pptx", ".zip.enc"]))

    def get(self, fid: str) -> dict[str, Any]:
        return self.ds.entry("formats", fid)


# ───────────────────────────── sniffing ─────────────────────────────
class _Probe:
    """Lazy views of a member's content used by sniff rules."""

    def __init__(self, member: Member, head_bytes: int, json_whole_limit: int) -> None:
        self.member = member
        self._head: Optional[bytes] = None
        self._json: Any = "unset"
        self._jsonl: Optional[list[dict[str, Any]]] = None
        self.head_bytes = head_bytes
        self.json_whole_limit = json_whole_limit

    @property
    def head(self) -> bytes:
        if self._head is None:
            try:
                self._head = self.member.head(self.head_bytes)
            except Exception:
                log.debug("head read failed for %s", self.member.name, exc_info=True)
                self._head = b""
        return self._head

    @property
    def text(self) -> str:
        return self.head.decode("utf-8", errors="replace")

    def json_head(self) -> Any:
        """``("array", first_element)`` | ``("object", obj, partial)`` | None."""
        if self._json != "unset":
            return self._json
        self._json = None
        first = self.head.lstrip()[:1]
        if first == b"[":
            try:
                with self.member.open() as fh:
                    for el in iter_json_array(fh):
                        self._json = ("array", el)
                        break
                    else:
                        self._json = ("array", None)
            except Exception:
                log.debug("array sniff failed for %s", self.member.name, exc_info=True)
        elif first == b"{":
            try:
                if self.member.size <= self.json_whole_limit:
                    obj = json.loads(self.member.read_bytes().decode("utf-8", errors="replace"))
                    self._json = ("object", obj, False)
                else:
                    self._json = ("object", top_level_keys_partial(self.text), True)
            except Exception:
                log.debug("object sniff failed for %s", self.member.name, exc_info=True)
        return self._json

    def jsonl_lines(self, n: int) -> list[dict[str, Any]]:
        if self._jsonl is None or len(self._jsonl) < n:
            out: list[dict[str, Any]] = []
            try:
                with self.member.open() as fh:
                    tw = io.TextIOWrapper(fh, encoding="utf-8", errors="replace")
                    try:
                        for raw in tw:
                            raw = raw.strip()
                            if not raw:
                                continue
                            try:
                                obj = json.loads(raw)
                            except json.JSONDecodeError:
                                break
                            if isinstance(obj, dict):
                                out.append(obj)
                            if len(out) >= n:
                                break
                    finally:
                        tw.detach()  # the member's handle is closed by the with; no unclosed-wrapper warning
            except Exception:
                log.debug("jsonl sniff failed for %s", self.member.name, exc_info=True)
            self._jsonl = out
        return self._jsonl[:n]


def _path_get(obj: Any, path: str) -> Any:
    cur = obj
    for part in path.split("."):
        m = re.fullmatch(r"(.*)\[(\d+)\]", part)
        idx = None
        if m:
            part, idx = m.group(1), int(m.group(2))
        if part:
            if not isinstance(cur, dict) or part not in cur:
                return None
            cur = cur[part]
        if idx is not None:
            if not isinstance(cur, list) or idx >= len(cur):
                return None
            cur = cur[idx]
    return cur


def _keys(obj: Any) -> set[str]:
    return set(obj.keys()) if isinstance(obj, dict) else set()


def rule_matches(rule: dict[str, Any], probe: _Probe, container_names: Optional[list[str]], archive: "Archive") -> bool:
    """All conditions of ``rule`` must hold."""
    for cond, arg in rule.items():
        ok = _condition(cond, arg, rule, probe, container_names, archive)
        if not ok:
            return False
    return True


def _condition(cond: str, arg: Any, rule: dict[str, Any], probe: _Probe, names: Optional[list[str]],
               archive: "Archive") -> bool:
    if cond == "magic":
        return probe.head.startswith(bytes.fromhex(arg))
    if cond == "text_regex":
        return re.search(arg, probe.text) is not None
    if cond == "html_contains":
        return arg.lower() in probe.text.lower()
    if cond == "extension":
        return Path(probe.member.name or archive.path).suffix.lower() in [e.lower() for e in arg]
    if cond in ("json_object_keys_all", "json_object_keys_any", "json_path_equals"):
        jh = probe.json_head()
        if not jh or jh[0] != "object":
            return False
        obj = jh[1]
        if cond == "json_object_keys_all":
            return set(arg) <= _keys(obj)
        if cond == "json_object_keys_any":
            return bool(set(arg) & _keys(obj))
        return _path_get(obj, arg["path"]) == arg["value"]
    if cond in ("json_wrapper_first_keys_all", "json_wrapper_first_keys_any"):
        jh = probe.json_head()
        if not jh or jh[0] != "object":
            return False
        arr = _path_get(jh[1], arg["wrapper"])
        if not (isinstance(arr, list) and arr):
            return False
        return set(arg["keys"]) <= _keys(arr[0]) if cond.endswith("all") else bool(set(arg["keys"]) & _keys(arr[0]))
    if cond in ("json_array_first_keys_all", "json_array_first_keys_any", "json_array_first_value"):
        jh = probe.json_head()
        if not jh or jh[0] != "array" or jh[1] is None:
            return False
        first = jh[1]
        if cond == "json_array_first_keys_all":
            return set(arg) <= _keys(first)
        if cond == "json_array_first_keys_any":
            return bool(set(arg) & _keys(first))
        v = _path_get(first, arg["path"])
        return isinstance(v, str) and re.search(arg["regex"], v) is not None
    if cond in ("jsonl_first_keys_all", "jsonl_first_keys_any"):
        lines = probe.jsonl_lines(1)
        if not lines:
            return False
        ks = _keys(lines[0])
        return set(arg) <= ks if cond.endswith("all") else bool(set(arg) & ks)
    if cond == "jsonl_any_line_keys_all":
        n = int(rule.get("jsonl_scan_lines", 20))
        return any(set(arg) <= _keys(line) for line in probe.jsonl_lines(n))
    if cond == "jsonl_scan_lines":
        return True
    if cond in ("zip_member", "zip_member_glob", "dir_glob"):
        if names is None:
            return False
        if cond == "dir_glob" and archive.kind != "dir":
            return False
        if cond == "zip_member":
            return arg in names
        return any(fnmatch.fnmatchcase(n, arg) for n in names)
    if cond == "zip_member_json":
        if names is None:
            return False
        member = archive.member(arg["member"])
        if member is None:
            return False
        sub = _Probe(member, probe.head_bytes, probe.json_whole_limit)
        jh = sub.json_head()
        if not jh:
            return False
        if "array_first_keys_all" in arg:
            return jh[0] == "array" and jh[1] is not None and set(arg["array_first_keys_all"]) <= _keys(jh[1])
        if "object_keys_all" in arg:
            return jh[0] == "object" and set(arg["object_keys_all"]) <= _keys(jh[1])
        return True
    raise DataFileError(f"unknown sniff condition: {cond!r}")


def sniff_member(member: Member, catalog: FormatCatalog, *, container_names: Optional[list[str]] = None) -> tuple[Optional[str], str]:
    """(format id, rule description) for a single member by its content."""
    probe = _Probe(member, catalog.head_bytes, catalog.json_whole_limit)
    if not probe.head:
        return None, "empty"
    for f in catalog.ordered:
        for rule in (f.get("sniff") or {}).get("any_of", []):
            if any(k in rule for k in ("zip_member", "zip_member_glob", "zip_member_json", "dir_glob")):
                continue  # container-level rules are evaluated on the archive
            try:
                if rule_matches(rule, probe, container_names, member.archive):
                    return f["id"], _rule_name(rule)
            except DataFileError:
                raise
            except Exception:
                log.debug("sniff rule failed: %s on %s", rule, member.name, exc_info=True)
    return None, "no rule matched"


def sniff_container(archive: Archive, catalog: FormatCatalog) -> tuple[Optional[str], str]:
    names = archive.names()
    dummy = Member(archive, "", 0, "", None)
    probe = _Probe(dummy, catalog.head_bytes, catalog.json_whole_limit)
    probe._head = b"-"
    for f in catalog.ordered:
        for rule in (f.get("sniff") or {}).get("any_of", []):
            if not any(k in rule for k in ("zip_member", "zip_member_glob", "zip_member_json", "dir_glob")):
                continue
            try:
                if rule_matches(rule, probe, names, archive):
                    return f["id"], _rule_name(rule)
            except DataFileError:
                raise
            except Exception:
                log.debug("container sniff rule failed: %s", rule, exc_info=True)
    return None, "no container rule matched"


def _rule_name(rule: dict[str, Any]) -> str:
    return ",".join(f"{k}={json.dumps(v, ensure_ascii=False)[:60]}" for k, v in rule.items())


def member_kind(name: str, fmt: Optional[dict[str, Any]], catalog: FormatCatalog) -> Optional[str]:
    """Kind from the provider's ``members`` table; ``asset`` by extension; None = not in the table."""
    if fmt:
        table = fmt.get("members") or {}
        for pattern, kind in table.items():
            if pattern == "*":
                continue
            if fnmatch.fnmatchcase(name, pattern) or fnmatch.fnmatchcase(Path(name).name, pattern):
                return kind
    ext = Path(name).suffix.lower()
    if ext in catalog.asset_extensions:
        return "asset"
    if fmt and "*" in (fmt.get("members") or {}):
        return fmt["members"]["*"]   # a provider archive: unlisted members are 'unknown', never guessed
    return None


# ───────────────────────────── scanning ─────────────────────────────
def scan(paths: list[str], *, catalog: Optional[FormatCatalog] = None, nested_limit: int = 0) -> list[Archive]:
    """Scan every path (file, dir, zip) into archives with sniffed members."""
    catalog = catalog or FormatCatalog()
    out: list[Archive] = []
    for p in paths:
        path = Path(p).expanduser()
        if not path.exists():
            raise FileNotFoundError(f"source not found: {p}")
        if path.is_dir():
            out.append(_scan_dir(path, catalog))
        elif zipfile.is_zipfile(path):
            out.extend(_scan_zip(path, catalog, nested_limit=nested_limit))
        else:
            out.append(_scan_file(path, catalog))
    return out


def _finish_archive(archive: Archive, catalog: FormatCatalog) -> None:
    fmt_id, how = sniff_container(archive, catalog)
    archive.format = fmt_id
    fmt = catalog.get(fmt_id) if fmt_id else None
    for m in archive.members:
        if m.format or m.kind == "container":
            continue
        kind = member_kind(m.name, fmt, catalog)
        if kind == "conversations" and fmt_id:
            m.format, m.kind, m.sniff_how = fmt_id, "conversations", f"container:{how}"
            continue
        if kind == "asset":
            m.kind, m.sniff_how = "asset", "assets.extensions"
            continue
        if kind == "unknown":
            m.kind, m.sniff_how = "unknown", "members:* (not in the provider's table)"
            continue
        if kind is not None:
            m.format, m.kind, m.sniff_how = fmt_id, kind, f"members:{kind}"
            continue
        fid, mhow = sniff_member(m, catalog, container_names=archive.names())
        if fid:
            m.format, m.kind, m.sniff_how = fid, "conversations", mhow
        else:
            m.kind, m.sniff_how = "unknown", mhow
    if archive.format is None:
        # a container with no provider rule: its format is the first conversation member's
        for m in archive.members:
            if m.format and m.kind == "conversations":
                archive.format = m.format
                break


def _scan_file(path: Path, catalog: FormatCatalog) -> Archive:
    sha, size = sha256_file(path)
    a = Archive(path=str(path), kind="file", label=path.name, sha256=sha, size=size, mtime=path.stat().st_mtime,
                _root=path)
    m = Member(a, "", size, sha, a.mtime)
    a.members.append(m)
    fid, how = sniff_member(m, catalog)
    if fid:
        m.format, m.kind, m.sniff_how = fid, "conversations", how
        a.format = fid
    else:
        m.kind, m.sniff_how = "unknown", how
    return a


def _scan_dir(path: Path, catalog: FormatCatalog) -> Archive:
    a = Archive(path=str(path), kind="dir", label=path.name or str(path), _root=path, mtime=path.stat().st_mtime)
    agg = hashlib.sha256()
    total = 0
    for fp in sorted(p for p in path.rglob("*") if p.is_file()):
        rel = fp.relative_to(path).as_posix()
        sha, size = sha256_file(fp)
        agg.update(f"{rel}\0{sha}\0".encode("utf-8"))
        total += size
        a.members.append(Member(a, rel, size, sha, fp.stat().st_mtime))
    a.sha256, a.size = agg.hexdigest(), total
    _finish_archive(a, catalog)
    return a


def _scan_zip(path: Path, catalog: FormatCatalog, *, nested_limit: int = 0, data: Optional[bytes] = None,
              label: Optional[str] = None, depth: int = 0) -> list[Archive]:
    if data is None:
        sha, size = sha256_file(path)
        a = Archive(path=str(path), kind="zip", label=label or path.name, sha256=sha, size=size,
                    mtime=path.stat().st_mtime, depth=depth)
    else:
        a = Archive(path=str(path), kind="zip", label=label or path.name, sha256=hashlib.sha256(data).hexdigest(),
                    size=len(data), mtime=None, depth=depth, _zip_bytes=data)
    nested: list[Archive] = []
    with a._zipfile() as zf:
        for info in zf.infolist():
            if info.is_dir():
                continue
            with zf.open(info) as fh:
                sha, size = sha256_stream(fh)
            m = Member(a, info.filename, size, sha, None, depth=depth)
            a.members.append(m)
            if info.filename.lower().endswith(".zip") and (nested_limit == 0 or size <= nested_limit):
                with zf.open(info) as fh:
                    inner = fh.read()
                if zipfile.is_zipfile(io.BytesIO(inner)):
                    m.kind, m.sniff_how = "container", "nested zip"
                    nested.extend(_scan_zip(Path(f"{a.path}!{info.filename}"), catalog, nested_limit=nested_limit,
                                            data=inner, label=f"{a.label}!{info.filename}", depth=depth + 1))
    _finish_archive(a, catalog)
    return [a] + nested


# ───────────────────────────── streaming JSON ─────────────────────────────
def iter_json_array(fh: io.BufferedIOBase, *, wrapper: Optional[str] = None, chunk: int = 1 << 20) -> Iterator[Any]:
    """Yield elements of a top-level JSON array (or of ``obj[wrapper]``) without loading the file.

    Incremental ``raw_decode`` over a growing text buffer (the same idea as
    ``_iter_json_elements`` in ``engine/importer.py``); memory grows with one
    element, not with the file.
    """
    reader = io.TextIOWrapper(fh, encoding="utf-8", errors="replace")
    try:
        yield from _iter_json_array(reader, wrapper, chunk)
    finally:
        reader.detach()  # the caller owns ``fh``; a stopped iteration leaves no unclosed wrapper behind


def _iter_json_array(reader: io.TextIOWrapper, wrapper: Optional[str], chunk: int) -> Iterator[Any]:
    buf = reader.read(chunk)
    pos = 0

    def fill() -> bool:
        nonlocal buf, pos
        more = reader.read(chunk)
        if not more:
            return False
        buf = buf[pos:] + more
        pos = 0
        return True

    def skip_ws() -> None:
        nonlocal pos
        while True:
            while pos < len(buf) and buf[pos] in " \t\r\n,":
                pos += 1
            if pos < len(buf):
                return
            if not fill():
                return

    if wrapper:
        # find `"wrapper"` at depth 1 followed by ':' and '['
        while True:
            m = re.search(r'"' + re.escape(wrapper) + r'"\s*:\s*\[', buf[pos:])
            if m:
                pos += m.end()
                break
            if len(buf) - pos > 4 * chunk or not fill():
                return
            pos = max(0, pos)
    else:
        skip_ws()
        if pos < len(buf) and buf[pos] == "[":
            pos += 1
        else:
            return
    while True:
        skip_ws()
        if pos >= len(buf):
            return
        if buf[pos] == "]":
            return
        while True:
            try:
                obj, end = _JSON_DECODER.raw_decode(buf, pos)
                pos = end
                yield obj
                break
            except json.JSONDecodeError:
                if not fill():
                    raise


def top_level_keys_partial(text: str) -> dict[str, Any]:
    """Depth-1 keys of a (possibly truncated) JSON object head; values are None."""
    keys: dict[str, Any] = {}
    depth = 0
    in_str = False
    esc = False
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            i += 1
            continue
        if ch == '"':
            if depth == 1:
                m = re.match(r'"((?:[^"\\]|\\.)*)"\s*:', text[i:])
                if m:
                    try:
                        keys[json.loads('"' + m.group(1) + '"')] = None
                    except json.JSONDecodeError:
                        pass
                    i += m.end()
                    continue
            in_str = True
        elif ch in "{[":
            depth += 1
        elif ch in "}]":
            depth -= 1
        i += 1
    return keys


def load_json_member(member: Member) -> Any:
    return json.loads(member.read_bytes().decode("utf-8", errors="replace"))


def temp_dir() -> tempfile.TemporaryDirectory:
    return tempfile.TemporaryDirectory(prefix="loom_extract_")
