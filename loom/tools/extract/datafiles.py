"""Loading of data files (presets) with the user's overlay.

Every setting the tool uses comes from a JSON file: shipped under
``loom/tools/extract/data/`` and optionally overridden by
``<app data dir>/extract/<kind>/<id>.json``.  An overlay entry may add, change
or disable (``"disabled": true``) a shipped entry; it never edits the shipped
file.  A missing or broken data file is an explicit error naming the file (R42),
never a hidden literal.  Each loaded entry carries ``_source`` ("preset" |
"overlay:<path>") so manifests can say where a setting came from.
"""
import json
import logging
from pathlib import Path
from typing import Any, Optional

from . import DATA_DIR, REPO_ROOT
from .paths import overlay_dir

log = logging.getLogger(__name__)


class DataFileError(ValueError):
    pass


def read_json(path: Path) -> Any:
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except FileNotFoundError:
        raise DataFileError(f"data file missing: {path}") from None
    except json.JSONDecodeError as exc:
        raise DataFileError(f"data file invalid JSON: {path}: {exc}") from None


def write_json(path: Path, obj: Any, *, indent: int = 1, sort_keys: bool = False) -> None:
    """Atomic UTF-8 JSON write (``.tmp`` → replace), keys in insertion order."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(obj, fh, ensure_ascii=False, indent=indent, sort_keys=sort_keys)
        fh.write("\n")
    tmp.replace(path)


class DataStore:
    """Catalog of data files of a given kind (``formats``, ``cascades``, …)."""

    def __init__(self, overlay: Optional[Path] = None, shipped: Optional[Path] = None) -> None:
        self.shipped = shipped or DATA_DIR
        self.overlay = overlay if overlay is not None else overlay_dir()
        self._cache: dict[tuple[str, str], Any] = {}

    # -- single files ---------------------------------------------------------
    def file(self, relpath: str) -> Any:
        """A single data file (``presets/defaults.json``) with overlay override."""
        key = ("file", relpath)
        if key in self._cache:
            return self._cache[key]
        base = read_json(self.shipped / relpath)
        source = "preset"
        if self.overlay is not None and (self.overlay / relpath).exists():
            over = read_json(self.overlay / relpath)
            base = _deep_merge(base, over)
            source = f"overlay:{self.overlay / relpath}"
        if isinstance(base, dict):
            base["_source"] = source
        self._cache[key] = base
        return base

    # -- catalogs of entries --------------------------------------------------
    def entries(self, kind: str) -> dict[str, Any]:
        """All entries of a kind keyed by ``id`` (shipped + overlay, disabled removed)."""
        key = ("kind", kind)
        if key in self._cache:
            return self._cache[key]
        out: dict[str, Any] = {}
        shipped_dir = self.shipped / kind
        if shipped_dir.is_dir():
            for path in sorted(shipped_dir.glob("*.json")):
                obj = read_json(path)
                if not isinstance(obj, dict) or "id" not in obj:
                    raise DataFileError(f"data file without id: {path}")
                obj["_source"] = "preset"
                obj["_path"] = str(path)
                out[obj["id"]] = obj
        if self.overlay is not None and (self.overlay / kind).is_dir():
            for path in sorted((self.overlay / kind).glob("*.json")):
                obj = read_json(path)
                if not isinstance(obj, dict) or "id" not in obj:
                    raise DataFileError(f"overlay file without id: {path}")
                if obj.get("disabled"):
                    out.pop(obj["id"], None)
                    continue
                merged = _deep_merge(out.get(obj["id"], {}), obj)
                merged["_source"] = f"overlay:{path}"
                merged["_path"] = str(path)
                out[obj["id"]] = merged
        self._cache[key] = out
        return out

    def entry(self, kind: str, ident: str) -> Any:
        entries = self.entries(kind)
        if ident not in entries:
            raise DataFileError(f"unknown {kind} entry: {ident!r} (known: {', '.join(sorted(entries)) or '-'})")
        return entries[ident]

    def defaults(self) -> dict[str, Any]:
        return self.file("presets/defaults.json")

    def preset(self, dotted: str, default: Any = None) -> Any:
        """``presets/defaults.json`` lookup by dotted path; None when absent."""
        node: Any = self.defaults()
        for part in dotted.split("."):
            if not isinstance(node, dict) or part not in node:
                return default
            node = node[part]
        return node

    # -- repository lexicons (reused, not copied) -----------------------------
    def repo_file(self, relpath: str) -> Any:
        """A data file shared with the kernel (``loom/data/lexicons/…``)."""
        key = ("repo", relpath)
        if key in self._cache:
            return self._cache[key]
        obj = read_json(REPO_ROOT / relpath)
        self._cache[key] = obj
        return obj


def _deep_merge(base: Any, over: Any) -> Any:
    if isinstance(base, dict) and isinstance(over, dict):
        out = dict(base)
        for k, v in over.items():
            out[k] = _deep_merge(base.get(k), v) if k in base else v
        return out
    return over


_default_store: Optional[DataStore] = None


def store() -> DataStore:
    global _default_store
    if _default_store is None:
        _default_store = DataStore()
    return _default_store


def set_store(s: Optional[DataStore]) -> None:
    global _default_store
    _default_store = s
