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


def get_reader(name: str) -> Any:
    if name in _instances:
        return _instances[name]
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
