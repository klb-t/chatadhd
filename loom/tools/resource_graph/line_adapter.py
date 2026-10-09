"""Bounded reference access to a local JSON Lines source.

No import, renderer, node population or whole-source hash is needed. The first
JSON Pointer component addresses a zero-based physical line. Root selection is
a structure descriptor; root children are a requested page of actual values.
The caller registers this operator in the same resource adapter registry as
other formats. The parser is independent of local access and page projection.
"""
from __future__ import annotations

import json
import io
import math
import os
import stat
from contextlib import contextmanager
from itertools import islice
from pathlib import Path
from typing import Any, Callable, Iterator
from urllib.parse import unquote, urlsplit
from .core import ResourceError


class LineAccessError(ResourceError):
    """Explicit status: absence and partial access are never an empty source."""

    def __init__(self, status: str, code: str, *, limit_source: str | None = None):
        super().__init__(status, code)
        self.limit_source = limit_source


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    obj: dict[str, Any] = {}
    for key, value in pairs:
        if key in obj:
            raise LineAccessError("corrupt", "duplicate_object_key")
        obj[key] = value
    return obj


def _reject_constant(value: str) -> Any:
    raise LineAccessError("corrupt", "non_json_numeric_constant")


def _finite_float(value: str) -> float:
    number = float(value)
    if not math.isfinite(number):
        raise LineAccessError("unsupported", "json_number_out_of_range")
    return number


def parse_json_line(raw: bytes) -> Any:
    """Parse one already bounded UTF-8 line, retaining every distinct field."""
    try:
        return json.loads(raw.decode("utf-8"), object_pairs_hook=_unique_object,
                          parse_constant=_reject_constant, parse_float=_finite_float)
    except (UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as exc:
        if isinstance(exc, LineAccessError):
            raise
        raise LineAccessError("corrupt", "invalid_json_line") from exc


def _pointer_parts(pointer: str) -> list[str]:
    if pointer == "":
        return []
    if not isinstance(pointer, str) or not pointer.startswith("/"):
        raise LineAccessError("unsupported", "invalid_json_pointer")
    result = []
    for token in pointer[1:].split("/"):
        # RFC 6901 only permits ~0 and ~1 escape sequences.
        i = 0
        while i < len(token):
            if token[i] == "~":
                if i + 1 >= len(token) or token[i + 1] not in "01":
                    raise LineAccessError("unsupported", "invalid_json_pointer")
                i += 1
            i += 1
        result.append(token.replace("~1", "/").replace("~0", "~"))
    return result


def _array_index(token: str) -> int:
    if not token or not token.isascii() or not token.isdecimal():
        raise LineAccessError("unsupported", "invalid_array_index")
    if len(token) > 1 and token.startswith("0"):
        raise LineAccessError("unsupported", "invalid_array_index")
    try:
        return int(token)
    except ValueError as exc:
        raise LineAccessError("unsupported", "invalid_array_index") from exc


def _subtree(value: Any, parts: list[str]) -> Any:
    for token in parts:
        try:
            if isinstance(value, dict):
                value = value[token]
            elif isinstance(value, list):
                value = value[_array_index(token)]
            else:
                raise KeyError(token)
        except (KeyError, IndexError) as exc:
            raise LineAccessError("unavailable", "fragment_not_found") from exc
    return value


def _version(info: os.stat_result) -> dict[str, Any]:
    # A filesystem observation/version token, deliberately not logical identity
    # or content hash. No content equality claim follows from stat equality.
    return {"basis": "filesystem_stat", "device": info.st_dev,
            "inode": info.st_ino, "size": info.st_size,
            "mtime_ns": info.st_mtime_ns, "ctime_ns": info.st_ctime_ns}


def _load_profile() -> dict[str, Any]:
    path = Path(__file__).resolve().parents[2] / "data/resource_graph/line_adapter.json"
    return json.loads(path.read_text(encoding="utf-8"))


class _MeteredRaw(io.RawIOBase):
    """Count OS-facing bytes, including read-ahead, within each request budget."""
    def __init__(self, source: Any, owner: "JsonLinesStructure"):
        self.source, self.owner = source, owner

    def readable(self) -> bool:
        return True

    def seekable(self) -> bool:
        return True

    def tell(self) -> int:
        return self.source.tell()

    def seek(self, offset: int, whence: int = 0) -> int:
        return self.source.seek(offset, whence)

    def readinto(self, target: Any) -> int:
        remaining = self.owner._limit("max_scan_bytes") - self.owner.last_request_bytes
        if remaining <= 0:
            raise self.owner._budget_error("max_scan_bytes", "request_scan_budget_exceeded")
        count = self.source.readinto(memoryview(target)[:remaining])
        self.owner.last_request_bytes += count
        self.owner.bytes_read += count
        if self.owner.observer is not None:
            self.owner.observer(count)
        return count


class JsonLinesAdapter:
    """Register a local parser without adding a format switch to the core.

    ``allowed_roots`` explicitly authorizes local paths when used standalone.
    A core may instead supply ``access.authorize_local(path)``. Both checks are
    applied when both exist. Explicit environment ceilings are separate from
    caller policy; they must come from the hosting environment, not this parser.
    """

    def __init__(self, *, allowed_roots: tuple[str | Path, ...] = (),
                 policy: dict[str, Any] | None = None,
                 environment_limits: dict[str, int] | None = None,
                 observer: Callable[[int], None] | None = None):
        self.profile = _load_profile()
        self.policy = {**self.profile["policy"], **(policy or {})}
        self.environment_limits = dict(environment_limits or {})
        self.allowed_roots = tuple(Path(root).resolve() for root in allowed_roots)
        self.observer = observer

    def open(self, resource: dict[str, Any], access: Any = None) -> "JsonLinesStructure":
        if resource.get("permissions", {}).get("read") is False:
            raise LineAccessError("unavailable", "read_not_authorized")
        if resource.get("members"):
            raise LineAccessError("unsupported", "seekable_local_source_required")
        locator = resource["locator"]
        parts = urlsplit(locator)
        if parts.scheme and parts.scheme != "file":
            raise LineAccessError("unsupported", "seekable_local_source_required")
        if parts.scheme and (parts.netloc or parts.query or parts.fragment):
            raise LineAccessError("unsupported", "local_file_locator_required")
        path = Path(unquote(parts.path) if parts.scheme else locator).absolute()
        policy = {**self.policy, **resource.get("policy", {}).get("line_adapter", {})}
        if "mode" in resource.get("policy", {}):
            policy["freshness"] = resource["policy"]["mode"]
        return JsonLinesStructure(path, resource, access, self.allowed_roots,
                                  policy, self.environment_limits, self.observer)


class JsonLinesStructure:
    def __init__(self, path: Path, resource: dict[str, Any], access: Any,
                 allowed_roots: tuple[Path, ...], policy: dict[str, Any],
                 environment_limits: dict[str, int], observer: Callable[[int], None] | None):
        self.path, self.resource, self.access = path, resource, access
        self.allowed_roots, self.policy = allowed_roots, policy
        self.environment_limits, self.observer = environment_limits, observer
        for key in ("max_line_bytes", "max_scan_bytes", "max_page_items",
                    "index_stride", "max_index_entries", "read_buffer_bytes"):
            if type(policy[key]) is not int or policy[key] <= 0:
                raise ValueError("invalid_line_adapter_policy:" + key)
        for key, ceiling in environment_limits.items():
            if key not in ("max_line_bytes", "max_scan_bytes", "max_page_items",
                           "max_index_entries") or type(ceiling) is not int or ceiling <= 0:
                raise ValueError("invalid_line_adapter_environment_limit:" + key)
        if policy["freshness"] not in ("snapshot", "live"):
            raise ValueError("invalid_line_adapter_freshness")
        self.bytes_read = 0
        self.last_request_bytes = 0
        self.parsed_lines = 0
        self._offsets = {0: 0}
        self._known_count: int | None = None
        self._status = "unloaded"
        self._last_error: str | None = None
        self._closed = False
        # Only stat/file-descriptor metadata is read here; no source bytes.
        with self._file(validate_version=False) as source:
            self.source_version = _version(os.fstat(source.fileno()))
        if self.source_version["size"] == 0:
            self._status, self._known_count = "empty", 0

    def _limit(self, name: str) -> int:
        return min(self.policy[name], self.environment_limits.get(name, self.policy[name]))

    def _budget_error(self, name: str, code: str) -> LineAccessError:
        source = ("environment_limits" if name in self.environment_limits and
                  self.environment_limits[name] <= self.policy[name] else "user_policy")
        return LineAccessError("partial", code, limit_source=source)

    def _authorize(self) -> Path:
        if self.resource.get("permissions", {}).get("read") is False:
            raise LineAccessError("unavailable", "read_not_authorized")
        hook = getattr(self.access, "authorize_local", None)
        resolved = self.path.resolve()
        if self.allowed_roots and not any(resolved.is_relative_to(root) for root in self.allowed_roots):
            raise LineAccessError("unavailable", "local_path_not_authorized")
        if hook is not None:
            try:
                authorized = hook(resolved)
            except Exception as exc:
                # AccessFailure belongs to the transport layer. Do not expose
                # its exception text or local paths through adapter diagnostics.
                if hasattr(exc, "status"):
                    raise LineAccessError(exc.status, "local_path_not_authorized") from exc
                raise
            if authorized is False:
                raise LineAccessError("unavailable", "local_path_not_authorized")
        elif not self.allowed_roots:
            raise LineAccessError("unavailable", "local_authorizer_required")
        return resolved

    @contextmanager
    def _file(self, *, validate_version: bool = True) -> Iterator[Any]:
        if self._closed:
            raise LineAccessError("unavailable", "resource_handle_closed")
        try:
            path = self._authorize()
            if not all(hasattr(os, name) for name in ("O_DIRECTORY", "O_NOFOLLOW", "O_NONBLOCK")):
                raise LineAccessError("unsupported", "secure_local_open_not_supported")
            # Open each resolved directory without following a replacement
            # symlink. The authorization/open gap cannot redirect to another
            # path via a changed symlink. FIFOs/devices are rejected below.
            directory = os.open(path.anchor, os.O_RDONLY | os.O_DIRECTORY)
            try:
                for component in path.parts[1:-1]:
                    child = os.open(component, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                                    dir_fd=directory)
                    os.close(directory)
                    directory = child
                descriptor = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK,
                                     dir_fd=directory)
            finally:
                os.close(directory)
            with os.fdopen(descriptor, "rb", buffering=0) as source:
                info = os.fstat(source.fileno())
                if not stat.S_ISREG(info.st_mode):
                    raise LineAccessError("unsupported", "regular_file_required")
                current = _version(info)
                if validate_version and current != self.source_version:
                    if self.policy["freshness"] == "snapshot":
                        raise LineAccessError("unavailable", "source_version_changed")
                    self.source_version = current
                    self._offsets, self._known_count = {0: 0}, None
                    self._status = "unloaded"
                yield source
                # A racing write is reported instead of a mixed-version result.
                if validate_version and _version(os.fstat(source.fileno())) != current:
                    raise LineAccessError("unavailable", "source_changed_during_read")
        except LineAccessError as exc:
            self._status, self._last_error = exc.status, exc.code
            raise
        except OSError as exc:
            self._status, self._last_error = "unavailable", "local_source_unavailable"
            raise LineAccessError(self._status, self._last_error) from exc

    def _line(self, source: Any) -> bytes:
        raw = source.readline(self._limit("max_line_bytes") + 1)
        if len(raw) > self._limit("max_line_bytes"):
            raise self._budget_error("max_line_bytes", "line_byte_budget_exceeded")
        if raw and not raw.endswith(b"\n") and source.tell() < self.source_version["size"]:
            raise self._budget_error("max_scan_bytes", "request_scan_budget_exceeded")
        return raw

    def _rows(self, offset: int, limit: int) -> list[tuple[str, Any]]:
        self.last_request_bytes = 0
        result = []
        with self._file() as raw_source, io.BufferedReader(
                _MeteredRaw(raw_source, self), min(self.policy["read_buffer_bytes"],
                    self._limit("max_scan_bytes"), self._limit("max_line_bytes") + 1)) as source:
            candidates = [line for line in self._offsets if line <= offset]
            position = max(candidates)
            source.seek(self._offsets[position])
            while position < offset + limit:
                raw = self._line(source)
                if not raw:
                    self._known_count = position
                    break
                if position >= offset:
                    value = parse_json_line(raw)
                    self.parsed_lines += 1
                    result.append((str(position), value))
                position += 1
                if (self.policy["offset_cache"] and position % self.policy["index_stride"] == 0
                        and len(self._offsets) < self._limit("max_index_entries")):
                    self._offsets[position] = source.tell()
                if source.tell() == self.source_version["size"]:
                    self._known_count = position
                    break
        self._status = "empty" if self._known_count == 0 else "partial"
        self._last_error = None
        return result

    def select(self, pointer: str) -> Any:
        parts = _pointer_parts(pointer)
        if not parts:
            return self.describe()
        rows = self._rows(_array_index(parts[0]), 1)
        if not rows:
            raise LineAccessError("unavailable", "fragment_not_found")
        return _subtree(rows[0][1], parts[1:])

    def children(self, pointer: str, offset: int = 0, limit: int | None = None) -> list[tuple[str, Any]]:
        if limit is None:
            limit = self._limit("max_page_items")
        if type(offset) is not int or offset < 0 or type(limit) is not int or limit < 0:
            raise ValueError("invalid_page_range")
        if limit > self._limit("max_page_items"):
            raise self._budget_error("max_page_items", "page_item_budget_exceeded")
        if limit == 0:
            return []
        if pointer == "":
            return self._rows(offset, limit)
        value = self.select(pointer)
        if isinstance(value, dict):
            return list(islice(value.items(), offset, offset + limit))
        if isinstance(value, list):
            return [(str(index), value[index]) for index in range(offset, min(len(value), offset + limit))]
        return []

    def describe(self) -> dict[str, Any]:
        return {"kind": "sequence", "selector": "json-pointer", "length": self._known_count,
                "address_basis": "zero_based_physical_line", "materialized": False,
                "syntax_recognition": "json-lines", "semantic_recognition": "unknown"}

    def node(self, pointer: str) -> dict[str, Any]:
        """One projection description; a sequence root never loads its values."""
        if pointer == "":
            return {"is_container": True, "size": self._known_count,
                    "value": None, "value_type": "list"}
        value = self.select(pointer)
        compound = isinstance(value, (dict, list))
        return {"is_container": compound, "size": len(value) if compound else None,
                "value": None if compound else value, "value_type": type(value).__name__}

    def refresh(self) -> None:
        """Observe a version before a core operation, without reading contents.

        Snapshot rejects changes. Live invalidates offsets and accepts the new
        observation. A projection must pin this revision and compare at end.
        """
        with self._file():
            pass

    def metadata(self) -> dict[str, Any]:
        # Revalidate without adopting a new revision: doing that here could
        # disguise values read under a different revision earlier in a query.
        with self._file(validate_version=False) as source:
            if _version(os.fstat(source.fileno())) != self.source_version:
                raise LineAccessError("partial", "source_version_changed_since_observation")
        return {"source_version": dict(self.source_version), "content_sha256": None,
                "parser_id": "json-lines-local", "parser_version": "1",
                "status": self._status, "partial": self._status != "empty",
                "recognition": {"syntax": "per_requested_line", "semantics": "unknown"},
                "content_hash_status": "unloaded", "last_error": self._last_error,
                "bytes_read": self.bytes_read, "last_request_bytes": self.last_request_bytes,
                "parsed_lines": self.parsed_lines, "offset_index_entries": len(self._offsets),
                "policy": dict(self.policy), "environment_limits": dict(self.environment_limits),
                "capabilities": {"read": True, "write_back": False, "bounded_pages": True}}

    def close(self) -> None:
        self._closed = True
        self._offsets = {0: 0}
