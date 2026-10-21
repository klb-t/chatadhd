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
            "first": None, "last": None, "attachments": 0, "has_usage": False, "has_tool_traces": False, "items": []})
        r["items"].append({"uid": u.uid, "title": u.title or "", "messages": st.get("messages", len(u.messages)),
                           "first": st.get("first_message_utc"), "last": st.get("last_message_utc"), "attachments": len(u.attachments)})
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


def _selection(eng, limit=0):
    by = eng.by_uid
    rows = []
    for uid in eng.selection.units:
        u = by.get(uid)
        if u is not None:
            ms = eng.selection.units[uid]
            rows.append({"uid": uid, "title": u.title or "", "provider": (u.account or {}).get("service") or "?",
                         "messages": len(u.messages) if ms is None else len(ms)})
    return {"summary": eng.selection_summary(), "items": rows[:int(limit)] if limit else rows, "total": len(rows)}


def _overview(eng):
    inv = eng.op("inventory", {})["inventory"]
    eng.op("timeline", {"key": "activity"})
    return {"ok": True, "providers": _provider_rows(eng.units),
            "totals": {"conversations": inv.get("conversations", 0),
                       "messages": (inv.get("messages") or {}).get("total", 0)},
            "sources": [str(p) for p in eng.sources]}


def _event(u, m):
    txt = m.text() if callable(getattr(m, "text", None)) else ""
    return {"uid": f"{u.uid}#{m.id}", "t": m.created_utc or "", "role": m.role or "", "preview": " ".join(txt.split())[:160]}


def events(uid):
    """All messages of one conversation (loaded when the conversation is expanded)."""
    with _lock:
        try:
            u = _need().by_uid.get(uid)
            if u is None:
                raise ValueError("Nie ma takiej rozmowy.")
            return json.dumps({"ok": True, "events": [_event(u, m) for m in u.messages]}, ensure_ascii=False)
        except Exception as exc:
            return _fail(exc)


def timeline_groups():
    """Global time axis, collapsed: month -> day -> count (messages of a day are loaded by day_events)."""
    with _lock:
        try:
            months = {}
            for r in _need().timeline_rows or []:
                if r.get("kind") != "message":
                    continue
                t = r.get("t_utc") or r.get("t") or "?"
                months.setdefault(t[:7], {}).setdefault(t[:10], 0)
                months[t[:7]][t[:10]] += 1
            out = [{"month": m, "days": [{"day": d, "count": c} for d, c in sorted(ds.items())]} for m, ds in sorted(months.items())]
            return json.dumps({"ok": True, "months": out}, ensure_ascii=False)
        except Exception as exc:
            return _fail(exc)


def day_events(day):
    """Messages of one day in time order, grouped into blocks: a run of consecutive messages of the same conversation is
    one block, so the switch from one conversation to another is a block boundary."""
    with _lock:
        try:
            eng = _need()
            by = eng.by_uid
            rows = [r for r in (eng.timeline_rows or []) if r.get("kind") == "message" and (r.get("t_utc") or r.get("t") or "?")[:10] == day]
            blocks = []
            for r in rows:
                conv = r.get("conversation")
                if not blocks or blocks[-1]["conversation"] != conv:
                    blocks.append({"conversation": conv, "title": r.get("title") or "", "provider": r.get("service") or "?", "events": []})
                u = by.get(conv)
                m = next((x for x in u.messages if x.id == r.get("message")), None) if u is not None else None
                blocks[-1]["events"].append({"uid": r.get("uid"), "t": r.get("t_utc") or r.get("t"), "role": r.get("role") or "",
                                             "preview": " ".join(m.text().split())[:160] if m is not None else ""})
            for blk in blocks:
                blk["first"], blk["last"] = blk["events"][0]["t"], blk["events"][-1]["t"]
            from loom.tools.extract.workblocks import find_work_blocks
            cfg = dict(eng.ds.preset("timeline.work_block", {}) or {})
            items, i = [], 0
            for wb in sorted(find_work_blocks(blocks, cfg), key=lambda w: w["start"]):
                items.extend({"type": "run", **blk} for blk in blocks[i:wb["start"]])
                runs = blocks[wb["start"]:wb["end"] + 1]
                items.append({"type": "work", "switches": wb["switches"], "conversations": wb["conversations"],
                              "first": runs[0]["first"], "last": runs[-1]["last"],
                              "messages": sum(len(r["events"]) for r in runs), "runs": [{"type": "run", **r} for r in runs]})
                i = wb["end"] + 1
            items.extend({"type": "run", **blk} for blk in blocks[i:])
            return json.dumps({"ok": True, "items": items}, ensure_ascii=False)
        except Exception as exc:
            return _fail(exc)


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


def _params(spec, ui=None):
    """Parameters of an edit / operation for the generic form. `name` is the engine key (what is sent back); `label`,
    `type_label` and `value_labels` are only the shown text (data/ui_labels.json); a name without a label is shown as it is."""
    ui = ui or {}
    out = []
    for name, ps in (spec or {}).items():
        if not isinstance(ps, dict):
            ps = {"type": str(ps)}
        typ = ps.get("type") or "string"
        values = ps.get("values") or []
        out.append({"name": name, "type": typ, "values": values,
                    "label": _lbl((ui.get("params") or {}).get(name), name),
                    "type_label": _lbl((ui.get("types") or {}).get(typ), typ),
                    "value_labels": [_lbl((ui.get("values") or {}).get(str(v)), str(v)) for v in values],
                    "default": ps.get("default"), "required": bool(ps.get("required")), "secret": bool(ps.get("secret"))})
    return out


def _lbl(x, fallback):
    return (x.get("pl") or x.get("en") or fallback) if isinstance(x, dict) else (x or fallback)


def _ui(ds):
    try:
        return ds.file("ui_labels.json")
    except Exception:  # a missing label file only means English names, never a broken screen
        return {}


def ui_labels():
    """Polish labels for the browser screen (roots, node kinds, viewer formats); the keys stay the engine's own."""
    with _lock:
        try:
            from loom.tools.extract.datafiles import store
            ds = store()
            layout = ds.file("commander/layout.json")
            ui = _ui(ds)
            roots = {r: _lbl((layout.get("root_labels") or {}).get(r), r) for r in layout.get("roots") or []}
            kinds = {k: _lbl(v, k) for k, v in (ui.get("kinds") or {}).items()}
            formats = {k: _lbl(v, k) for k, v in (ui.get("formats") or {}).items()}
            return json.dumps({"ok": True, "roots": roots, "kinds": kinds, "formats": formats}, ensure_ascii=False)
        except Exception as exc:
            return _fail(exc)


def catalog():
    """Everything the generic screens need: edits, operations, redaction presets and cut profiles (labels in Polish)."""
    with _lock:
        try:
            eng = _need()
            ds = eng.ds
            ui = _ui(ds)
            edits = [{"id": k, "label": _lbl(v.get("label"), k), "group": v.get("group") or "", "params": _params(v.get("params"), ui)}
                     for k, v in sorted(ds.entries("edits").items())]
            skip = {"scan", "read", "merge", "user.call", "inventory", "timeline"}
            ops = [{"id": k, "label": _lbl(v.get("label"), k), "long": bool(v.get("long")), "params": _params(v.get("params"), ui)}
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
