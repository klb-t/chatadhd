"""Entry point for the Android shell. The engine itself is loom/tools/extract (copied in by the build).

Everything here only calls the engine's own operations; parsing lives in the engine.
Results are returned as JSON strings so the Kotlin side needs no Python-object handling.
"""
import json
import sys
import traceback


def version():
    return sys.version


def _provider_rows(units):
    rows = {}
    for u in units:
        if u.kind not in ("conversation", "session"):
            continue
        st = u.stats or {}
        r = rows.setdefault((u.account or {}).get("service") or "?", {
            "provider": (u.account or {}).get("service") or "?", "conversations": 0, "messages": 0,
            "first": None, "last": None, "attachments": 0, "has_usage": False, "has_tool_traces": False})
        r["conversations"] += 1
        r["messages"] += st.get("messages", 0)
        r["attachments"] += len(u.attachments)
        r["has_usage"] = r["has_usage"] or bool(st.get("usage_messages"))
        r["has_tool_traces"] = r["has_tool_traces"] or bool(st.get("tool_calls") or st.get("agentic"))
        lo, hi = st.get("first_message_utc"), st.get("last_message_utc")
        if lo and (r["first"] is None or lo < r["first"]):
            r["first"] = lo
        if hi and (r["last"] is None or hi > r["last"]):
            r["last"] = hi
    return sorted(rows.values(), key=lambda r: r["provider"])


def inventory(path, timeline_limit=200):
    """Run the engine's inventory + per-message timeline on one file; return a JSON string."""
    try:
        from loom.tools.extract.engine import Engine
        with Engine() as eng:
            eng.set_sources([path])
            inv = eng.op("inventory", {})["inventory"]
            eng.op("timeline", {"key": "activity"})
            events = [{"t": r.get("t_utc") or r.get("t"), "provider": r.get("service") or "?",
                       "title": r.get("title") or "", "role": r.get("role") or ""}
                      for r in eng.timeline_rows if r.get("kind") == "message"]
            out = {"ok": True, "providers": _provider_rows(eng.units),
                   "totals": {"conversations": inv.get("conversations", 0),
                              "messages": (inv.get("messages") or {}).get("total", 0)},
                   "events_total": len(events), "events": events[:int(timeline_limit)]}
        return json.dumps(out, ensure_ascii=False)
    except Exception as exc:  # shown to the user as text, never a crash; no file content in the message
        tb = traceback.format_exc().strip().splitlines()[-1]
        return json.dumps({"ok": False, "error": f"{type(exc).__name__}: {str(exc)[:300]}", "where": tb[:300]},
                          ensure_ascii=False)
