"""Readers: one per source structure; field mapping comes from ``data/formats/*.json``.

Contract (``base.Reader``): ``iter_units(member, fmt, ctx) -> Iterator[Unit]``.
Every reader also reports what it saw but could not place (``unit.unknown``)
and what the format lacks by definition comes from ``known_absent`` of the
format file.  Text-like formats go THROUGH ``engine.importer`` (one role
heuristic, one HTML parser); provider trees use the lossless walk.
"""
import importlib
from typing import Any

_REGISTRY: dict[str, str] = {
    "chatgpt": "chatgpt:ChatGPTReader",
    "claude": "claude:ClaudeReader",
    "gemini": "gemini:GeminiReader",
    "grok": "grok:GrokReader",
    "claude_code": "claude_code:ClaudeCodeReader",
    "loom_graph": "loom_graph:LoomGraphReader",
    "loom_export": "loom_graph:LoomExportReader",
    "textlike": "textlike:TextlikeReader",
}
_instances: dict[str, Any] = {}


class _FunctionReader:
    """A user's module with a plain ``iter_units(member, fmt, ctx)`` function."""

    def __init__(self, fn: Any, ident: str) -> None:
        self.id = ident
        self.iter_units = fn


def user_reader(name: str, ctx: Any) -> Any:
    """``reader: "user:<name>"`` (spec §8 row 35, §8.1 #52): ``<overlay>/readers/<name>.py`` registered by a format
    data entry, loaded only when this face allows user code (``--kod-uzytkownika``); the module defines
    ``iter_units(member, fmt, ctx)`` or a ``Reader`` class with that method.  The report names it as the user's."""
    import importlib.util
    ds = getattr(ctx, "ds", None)
    if not getattr(ctx, "allow_user_code", False):
        raise PermissionError(f"user reader {name!r}: user code needs this face's grant (--kod-uzytkownika)")
    if not name.isidentifier():
        raise ValueError(f"user reader: {name!r} is not a Python name")
    base = getattr(ds, "overlay", None)
    if base is None:
        raise FileNotFoundError(f"user reader {name!r}: no overlay directory")
    root = (base / "readers").resolve()
    path = (root / f"{name}.py").resolve()
    if not path.is_relative_to(root) or not path.exists():
        raise FileNotFoundError(f"user reader {name!r}: {path} not found")
    spec = importlib.util.spec_from_file_location(f"loom_extract_user_reader_{name}", path)
    mod = importlib.util.module_from_spec(spec)  # type: ignore[arg-type]
    assert spec and spec.loader
    spec.loader.exec_module(mod)
    if callable(getattr(mod, "iter_units", None)):
        inst: Any = _FunctionReader(mod.iter_units, f"user:{name}")
    elif isinstance(getattr(mod, "Reader", None), type):
        inst = mod.Reader()
    else:
        raise ValueError(f"user reader {name!r}: {path} defines neither iter_units nor Reader")
    used = getattr(ctx, "user_code_used", None)
    if isinstance(used, list):
        used.append({"reader": f"user:{name}", "path": str(path)})
    return inst


def get_reader(name: str, ctx: Any = None) -> Any:
    if name in _instances:
        return _instances[name]
    if name.startswith("user:"):
        inst = user_reader(name[5:], ctx)
        _instances[name] = inst  # per process; a changed file is read again by a new process
        return inst
    if name not in _REGISTRY:
        raise KeyError(f"unknown reader: {name!r} (known: {', '.join(sorted(_REGISTRY))})")
    mod_name, cls_name = _REGISTRY[name].split(":")
    mod = importlib.import_module(f"{__name__}.{mod_name}")
    inst = getattr(mod, cls_name)()
    _instances[name] = inst
    return inst


def register(name: str, target: str) -> None:
    """``target`` = ``module:Class`` inside this package or an absolute module path."""
    _REGISTRY[name] = target
    _instances.pop(name, None)
