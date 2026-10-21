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
            # not an expression but a pattern the owner picks like the others: "contains encoded data", kinds as sub-choices
            from loom.tools.extract.encoded import KINDS
            labels = _ui(eng.ds).get("encoded_kinds") or {}
            kinds = [{"id": k, "label": _lbl(labels.get(k), k)} for k in KINDS]
            return json.dumps({"ok": True, "patterns": rows, "encoded_kinds": kinds}, ensure_ascii=False)
        except Exception as exc:
            return _fail(exc)


def select(how, value="", mode="add"):
    """how: pattern (value = name) | expr (value = selection expression) | encoded (value = kinds, comma separated) | all | invert | clear."""
    with _lock:
        try:
            eng = _need()
            extra = {}
            if how == "pattern":
                res = eng.op("select.structural", {"pattern": value, "mode": mode})
            elif how == "expr":
                res = eng.op("select.expr", {"expr": value, "mode": mode})
            elif how == "encoded":  # value = kinds, comma separated ("" = every kind the engine knows)
                kinds = [k for k in str(value or "").split(",") if k]
                res = eng.op("select.encoded", dict({"mode": mode}, **({"kinds": kinds} if kinds else {})))
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
            prof = [{"id": k, "label": _lbl(v.get("label"), k), "description": _lbl(v.get("description"), ""), "kind": v.get("kind") or ""}
                    for k, v in sorted(ds.entries("profiles").items())]
            # ready recipes (data/macros): a Polish name and a one-line description each; applied with the "macro" call
            macros = [{"id": k, "label": _lbl(v.get("label"), k), "description": _lbl(v.get("description"), ""), "steps": len(v.get("steps") or [])}
                      for k, v in sorted(ds.entries("macros").items())]
            return json.dumps({"ok": True, "edits": edits, "ops": ops, "redaction": red, "profiles": prof, "macros": macros},
                              ensure_ascii=False)
        except Exception as exc:
            return _fail(exc)


def call(req_json):
    """Any api.Session call (list, view, edit, apply, undo, redo, op, search, compare, goto, tag, bookmark, status…).
    A ready recipe (macro) and an undo come back with `summary_text` (one plain sentence about what changed) and `summary`
    (the same numbers as lists); the engine's own answer stays untouched below them.
    A call that works on the working package first brings the package in line with the selection (see `_package_phase`);
    its answer then carries `package_added` (selected conversations copied in) and, when there were some,
    `package_added_text` (the plain clause for the screen)."""
    with _lock:
        try:
            eng = _need()
            req = json.loads(req_json)
            kind = req.get("call")
            phase, create = _package_phase(eng, req)
            added = 0
            if phase == "before":
                added = _align_package(eng, create)[2]
            if kind == "macro" or kind == "undo":
                res = _call_measured(req)
            else:
                res = _sess.call(req)
            if phase == "after":  # undo / redo put an older snapshot of the whole tree back: align after it, not before it
                added = _align_package(eng, False)[2]
            if phase:
                res["package_added"] = added
                if added:
                    res["package_added_text"] = _package_note(eng, "package_added", added)
            res.setdefault("ok", True)
            return json.dumps(res, ensure_ascii=False, default=str)
        except Exception as exc:
            return _fail(exc)


# Operations that arrange the package tree by hand: the person is in charge of what is in the package, so they do not
# add the selection to it (an explicit `package.remove` must not be undone by the next call).  Everything else that
# changes or reads the package (the ops marked `mutates_package` in data/ops.json: edits, cut, write, handoff…) does.
_HAND_PACKAGE_OPS = frozenset({"package.copy", "package.move", "package.mkdir", "package.remove", "package.rename",
                               "package.mark", "undo", "redo"})


def _package_phase(eng, req):
    """(`before` | `after` | None, whether the package may be created for this call).  `before`: the package is brought
    in line with the selection first (edits, recipes, previews, ops that change or write the package; reads of the
    package in the browser only when a package already exists - looking never makes one).  `after`: undo and redo, whose
    restore of an older snapshot would take the copied-in conversations away again."""
    kind = req.get("call")
    if kind in ("macro", "edit"):
        return "before", True
    if kind in ("undo", "redo"):
        return "after", False
    if kind in ("apply", "op"):
        if kind == "apply" and req.get("edit"):
            return "before", True
        op = str(req.get("op") or "")
        if op and op not in _HAND_PACKAGE_OPS and (eng.ops_available().get(op) or {}).get("mutates_package"):
            return "before", True
        return None, False
    if kind in ("list", "view", "search", "goto", "compare", "explain", "stats"):
        roots = req.get("roots") if isinstance(req.get("roots"), list) else [req.get("root")]
        if "package" in roots or str(req.get("node") or "").startswith(("pkg:", "dir:")):
            return "before", False
    return None, False


def _package_note(eng, key, n):
    """One clause from data/ui_labels.json#effects (`package_added`, `package_left_out`) with a counted noun that needs
    no adjective to agree with it: 'do pakietu roboczego dołączono to, co zaznaczono po jego utworzeniu: 5 rozmów'."""
    from loom.tools.extract import plural
    return eng.ds.file("ui_labels.json")["effects"][key]["pl"].format(count=plural.count("conversation", n, "pl"))


def _call_measured(req):
    """The engine call plus the measured effect on the messages of the working package (loom.tools.extract.effects)."""
    from loom.tools.extract import effects
    eng = _eng
    action = "undo" if req.get("call") == "undo" else ("preview" if req.get("preview") else "apply")
    pkg = eng.package if action == "undo" else eng.ensure_package()  # an undo never creates a package
    before, hash_before = effects.snapshot(pkg), (pkg.tree_hash() if pkg is not None else None)
    limit = int(eng.ds.preset("edits.effect_names", 6) or 0)
    res = _sess.call(req)
    if res.get("ok") is False or res.get("error"):
        return res
    other = 0
    try:
        if action == "undo" and (res.get("result") or {}).get("undone") == []:
            res["summary_text"] = eng.ds.file("ui_labels.json")["effects"]["undo_none"]["pl"]
            return res
        if pkg is None:  # the engine made the package during this call: nothing was in it before, nothing changed
            before = effects.snapshot(eng.package)
        if action == "preview":  # the engine previews every step on the untouched package; the whole recipe is measured here
            steps = eng.ds.entry("macros", str(req.get("name") or "")).get("steps") or []
            after, other = effects.simulate(eng, steps)
            changed = True
        else:
            after = effects.snapshot(eng.package)
            changed = eng.package is not None and eng.package.tree_hash() != hash_before
        d = effects.diff(before, after)
        res["summary_text"] = effects.sentence(action, d, limit=limit, other_steps=other, tree_changed=changed)
        res["summary"] = dict(effects.structured(d), action=action, other_steps=other)
    except Exception as exc:  # the engine's answer is still true; only the sentence is missing, and it says so
        why = f"{type(exc).__name__}: {str(exc)[:160]}"
        res["summary_text"] = eng.ds.file("ui_labels.json")["effects"]["failed"]["pl"].format(why=why)
    return res


def _conversation_dirs(eng):
    """Top-level directories that a profile made per conversation (its output path has `{conversation.dir}`)."""
    dirs = set()
    for row in (eng.cut_info or {}).get("profiles") or []:
        spec = eng.ds.entry("profiles", row.get("profile"))
        if any("{conversation.dir}" in str(o.get("path")) for o in spec.get("outputs") or []):
            dirs |= {o.split("/", 1)[0] for o in row.get("outputs") or [] if "/" in o}
    return dirs


def _align_package(eng, create=True):
    """The export is of the selection, the cut works on the working package.  The package is made from the selection
    the first time anything needs it (an edit, a recipe preview) and is not re-made when the selection changes later,
    so a package made while nothing was selected is empty and every later edit, recipe and export worked on nothing.
    Here the selected conversations that the package lacks are copied in (the whole conversation, as at the package's
    creation; edits already made to the other entries stay) and the package entries that are not selected are returned
    so that the export can leave them out, not lose them.  Nothing is removed from the package.  `create` False: no
    package yet means nothing to align (a read never makes one).  Returns (package | None, left out, added)."""
    if eng.package is None and not create:
        return None, [], 0
    pkg = eng.ensure_package()
    want = set(eng.selection.units)
    added = len(want - {e.unit.uid for e in pkg.entries})
    if added:
        eng.op("package.copy", {"from": "selection"}, by="engine")
    return pkg, [e for e in pkg.entries if e.unit.uid not in want], added


def export_selected(zip_path, redaction="", profiles="", handoff=False):
    """Batch operation on the selection: cut (source 1:1 + text) -> package -> one zip at zip_path.
    The answer says what was done: `profiles_used` (id + label; empty = the default profile), `package_added` (selected
    conversations that were not in the working package and were copied in), `package_left_out` (package entries that
    are not selected, left out), `top_level_entries`, `top_level_dirs` and `conversation_dirs` of the zip."""
    with _lock:
        tmp = Path(tempfile.mkdtemp(prefix="xport_out_"))
        pkg = None
        original = None
        try:
            eng = _need()
            if not eng.selection.units:
                raise ValueError("Nic nie zaznaczono — najpierw wybierz rozmowy.")
            ids = [p for p in str(profiles or "").split(",") if p]
            cut = {}
            if redaction:
                cut["redaction"] = redaction
            if ids:
                cut["profiles"] = ids
            pkg, left_out, added = _align_package(eng)
            original = list(pkg.entries)
            if left_out:
                out_ids = {id(e) for e in left_out}
                pkg.entries = [e for e in original if id(e) not in out_ids]
            eng.op("cut", cut)
            if handoff:
                eng.op("handoff", {})
            eng.op("write", {"package": str(tmp / "pakiet")})
            files = 0
            tops, top_dirs = set(), set()
            with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
                for p in sorted((tmp / "pakiet").rglob("*")):
                    if p.is_file():
                        rel = p.relative_to(tmp / "pakiet").as_posix()
                        z.write(p, rel)
                        files += 1
                        tops.add(rel.split("/", 1)[0])
                        if "/" in rel:
                            top_dirs.add(rel.split("/", 1)[0])
            profs = eng.ds.entries("profiles")
            used = [{"id": i, "label": _lbl((profs.get(i) or {}).get("label"), i)} for i in ids]
            return json.dumps({"ok": True, "files": files, "selection": eng.selection_summary(), "profiles_used": used,
                               "package_added": added, "package_left_out": len(left_out),
                               "package_added_text": _package_note(eng, "package_added", added) if added else "",
                               "package_left_out_text": _package_note(eng, "package_left_out", len(left_out)) if left_out else "",
                               "top_level_entries": len(tops), "top_level_dirs": sorted(top_dirs),
                               "conversation_dirs": len(top_dirs & _conversation_dirs(eng))}, ensure_ascii=False)
        except Exception as exc:
            return _fail(exc)
        finally:
            if pkg is not None and original is not None:
                pkg.entries = original  # the entries left out of this export are back in the working package
            shutil.rmtree(tmp, ignore_errors=True)
