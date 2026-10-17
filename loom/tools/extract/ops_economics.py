"""Slice 4 operation: ``token_economics`` (P38 requirement 37) — one engine op, so the CLI verb ``ekonomia``
(alias ``tokens``), the commander (menu, ``:op token_economics {…}``) and ``serwuj`` (``POST /api/op``) run the same
code.  The result lives in the engine state (``eng.economics``); ``write`` puts its files in the package
(``economics/``: JSON, Markdown in the run's language, one CSV per table)."""
from typing import Any

from .economics import build_economics, render_markdown, summary
from .ops import op


@op("token_economics")
def op_token_economics(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    if eng.sources:
        eng.ensure_units()
    doc = build_economics(eng, p)
    eng.economics = doc
    s = summary(doc)
    return {"counts": {k: s[k] for k in ("turns", "turns_measured", "turns_estimated", "sessions", "runs", "unknowns")},
            "summary": s, "economics": doc, "markdown": render_markdown(doc, eng.ds)}
