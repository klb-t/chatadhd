"""Operation ``select.encoded``: select the conversations whose blocks carry encoded data (see encoded.py)."""
from typing import Any

from . import encoded as _enc
from .expand import Selection
from .ops import op


@op("select.encoded")
def op_select_encoded(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    kinds = tuple(k for k in (p.get("kinds") or _enc.KINDS) if k in _enc.KINDS) or _enc.KINDS
    cfg = {k: int(p[k]) for k in _enc.DEFAULTS if p.get(k) is not None}
    sel = Selection()
    meta: dict[str, dict[str, Any]] = {}
    by_kind: dict[str, int] = {}
    findings = 0
    per_conv: list[dict[str, Any]] = []
    for u in eng.conversations:
        fs = _enc.scan_unit(u, cfg, kinds)
        if not fs:
            continue
        findings += len(fs)
        for f in fs:
            by_kind[f["kind"]] = by_kind.get(f["kind"], 0) + 1
        sel.add(u.uid, {f["message"] for f in fs} if p.get("messages_only") else None, "encoded")
        meta[u.uid] = {"source": "encoded", "findings": len(fs)}
        per_conv.append({"uid": u.uid, "title": None if eng.aggregate else u.title, "findings": len(fs),
                         "types": sorted({str(f["decoded_type"]) for f in fs if f["decoded_type"]})})
    applied = eng.apply_selection(sel, p.get("mode") or "add", meta)
    return {"counts": {"conversations": len(per_conv), "findings": findings, "evaluated": len(eng.conversations)},
            "by_kind": by_kind, "conversations": per_conv, "selection": applied}
