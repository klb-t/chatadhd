"""Entry point for the Android shell. The engine itself is loom/tools/extract (copied in by the build).

Everything here only calls the engine's own operations; parsing lives in the engine.
Results are returned as JSON strings so the Kotlin side needs no Python-object handling.
"""
import json
import shutil
import sys
import tempfile
import threading
import traceback
import zipfile
from pathlib import Path

_lock = threading.Lock()
_sess = None  # api.Session of the open file (one at a time), kept between calls
_eng = None   # its engine


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


def _fail(exc):
    return json.dumps({"ok": False, "error": f"{type(exc).__name__}: {str(exc)[:300]}"}, ensure_ascii=False)


def _need():
    if _eng is None:
        raise RuntimeError("Najpierw wybierz plik eksportu.")
    return _eng


def _selection(eng, limit=200):
    by = eng.by_uid
    rows = []
    for uid in eng.selection.units:
        u = by.get(uid)
        if u is not None:
            ms = eng.selection.units[uid]
            rows.append({"title": u.title or "", "provider": (u.account or {}).get("service") or "?",
                         "messages": len(u.messages) if ms is None else len(ms)})
    return {"summary": eng.selection_summary(), "items": rows[:int(limit)], "total": len(rows)}


def _overview(eng):
    inv = eng.op("inventory", {})["inventory"]
    eng.op("timeline", {"key": "activity"})
    events = [{"t": r.get("t_utc") or r.get("t"), "provider": r.get("service") or "?",
               "title": r.get("title") or "", "role": r.get("role") or ""}
              for r in eng.timeline_rows if r.get("kind") == "message"]
    return {"ok": True, "providers": _provider_rows(eng.units),
            "totals": {"conversations": inv.get("conversations", 0),
                       "messages": (inv.get("messages") or {}).get("total", 0)},
            "sources": [str(p) for p in eng.sources],
            "events_total": len(events), "events": events[:200]}


def open_sources(paths_json):
    """(Re)open the session on ALL given files at once (the engine merges duplicates and versions).

    What was already done in the session (selection, edits) is carried over by replaying the engine's own recipe on
    the new list of sources, so adding a source loses nothing. Returns the overview as JSON."""
    global _sess, _eng
    with _lock:
        try:
            from loom.tools.extract.api import Session
            from loom.tools.extract.datafiles import store
            paths = [str(p) for p in json.loads(paths_json)]
            if not paths:
                if _sess is not None:
                    _sess.close()
                _sess = _eng = None
                return json.dumps({"ok": True, "empty": True})
            recipe = None
            if _eng is not None:
                rec = _eng.normalised_recipe()
                if rec.get("steps"):
                    recipe = dict(rec, sources=paths)
            sess = Session(face="api", ds=store())
            sess.call({"call": "open", "recipe": recipe} if recipe else {"call": "open", "sources": paths})
            eng = sess.eng
            if recipe is not None and [str(p) for p in eng.sources] != paths:
                eng.set_sources(paths)
                eng.ensure_units()
            out = _overview(eng)
            if _sess is not None:
                _sess.close()
            _sess, _eng = sess, eng
            return json.dumps(out, ensure_ascii=False)
        except Exception as exc:  # shown as text, never a crash; no file content in the message
            return _fail(exc)


def overview():
    """The overview of the session that is already open (the UI was recreated, the engine session was not)."""
    with _lock:
        try:
            return json.dumps(_overview(_need()), ensure_ascii=False) if _eng is not None else json.dumps({"ok": True, "empty": True})
        except Exception as exc:
            return _fail(exc)


def patterns():
    """The engine's structural patterns (data/select_fields.json) with Polish labels."""
    with _lock:
        try:
            eng = _need()
            pats = eng.ds.file("select_fields.json").get("patterns") or {}
            rows = [{"name": k, "label": (v.get("label") or {}).get("pl") or k, "expr": v.get("expr")}
                    for k, v in sorted(pats.items())]
            return json.dumps({"ok": True, "patterns": rows}, ensure_ascii=False)
        except Exception as exc:
            return _fail(exc)


def select(how, value="", mode="add"):
    """how: pattern (value = name) | expr (value = selection expression) | encoded | all | invert | clear."""
    with _lock:
        try:
            eng = _need()
            extra = {}
            if how == "pattern":
                res = eng.op("select.structural", {"pattern": value, "mode": mode})
            elif how == "expr":
                res = eng.op("select.expr", {"expr": value, "mode": mode})
            elif how == "encoded":
                res = eng.op("select.encoded", {"mode": mode})
                extra = {"encoded": {"by_kind": res.get("by_kind"), "counts": res.get("counts")}}
            elif how == "all":
                res = eng.op("select.expr", {"expr": "uid~/./", "mode": mode})
            elif how == "invert":
                res = eng.op("select.invert", {})
            elif how == "clear":
                res = eng.op("select.clear", {})
            else:
                raise ValueError(f"nieznany sposób wyboru: {how}")
            matched = (res.get("counts") or {}).get("matched")
            return json.dumps(dict(ok=True, matched=matched, selection=_selection(eng), **extra), ensure_ascii=False)
        except Exception as exc:
            return _fail(exc)


def _params(spec):
    out = []
    for name, ps in (spec or {}).items():
        if not isinstance(ps, dict):
            ps = {"type": str(ps)}
        out.append({"name": name, "type": ps.get("type") or "string", "values": ps.get("values") or [],
                    "default": ps.get("default"), "required": bool(ps.get("required")), "secret": bool(ps.get("secret"))})
    return out


def _lbl(x, fallback):
    return (x.get("pl") or x.get("en") or fallback) if isinstance(x, dict) else (x or fallback)


def catalog():
    """Everything the generic screens need: edits, operations, redaction presets and cut profiles (labels in Polish)."""
    with _lock:
        try:
            eng = _need()
            ds = eng.ds
            edits = [{"id": k, "label": _lbl(v.get("label"), k), "group": v.get("group") or "", "params": _params(v.get("params"))}
                     for k, v in sorted(ds.entries("edits").items())]
            skip = {"scan", "read", "merge", "user.call", "inventory", "timeline"}
            ops = [{"id": k, "label": _lbl(v.get("label"), k), "long": bool(v.get("long")), "params": _params(v.get("params"))}
                   for k, v in sorted(_sess.call({"call": "ops"})["ops"].items())
                   if k not in skip and not k.startswith("edit.") and v.get("implemented", True) and "api" in (v.get("faces") or ["api"])]
            red = [{"id": k, "label": _lbl(v.get("label"), k)} for k, v in sorted(ds.entries("redaction").items())]
            prof = [{"id": k, "label": _lbl(v.get("label"), k), "kind": v.get("kind") or ""} for k, v in sorted(ds.entries("profiles").items())]
            return json.dumps({"ok": True, "edits": edits, "ops": ops, "redaction": red, "profiles": prof}, ensure_ascii=False)
        except Exception as exc:
            return _fail(exc)


def call(req_json):
    """Any api.Session call (list, view, edit, apply, undo, redo, op, search, compare, goto, tag, bookmark, status…)."""
    with _lock:
        try:
            _need()
            req = json.loads(req_json)
            res = _sess.call(req)
            res.setdefault("ok", True)
            return json.dumps(res, ensure_ascii=False, default=str)
        except Exception as exc:
            return _fail(exc)


def export_selected(zip_path, redaction="", profiles="", handoff=False):
    """Batch operation on the selection: cut (source 1:1 + text) -> package -> one zip at zip_path."""
    with _lock:
        tmp = Path(tempfile.mkdtemp(prefix="xport_out_"))
        try:
            eng = _need()
            if not eng.selection.units:
                raise ValueError("Nic nie zaznaczono — najpierw wybierz rozmowy.")
            cut = {}
            if redaction:
                cut["redaction"] = redaction
            if profiles:
                cut["profiles"] = [p for p in profiles.split(",") if p]
            eng.op("cut", cut)
            if handoff:
                eng.op("handoff", {})
            eng.op("write", {"package": str(tmp / "pakiet")})
            files = 0
            with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
                for p in sorted((tmp / "pakiet").rglob("*")):
                    if p.is_file():
                        z.write(p, p.relative_to(tmp / "pakiet").as_posix())
                        files += 1
            return json.dumps({"ok": True, "files": files, "selection": eng.selection_summary()}, ensure_ascii=False)
        except Exception as exc:
            return _fail(exc)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
