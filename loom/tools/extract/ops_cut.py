"""Slice B operations: the package tree (copy/move/mkdir/remove/rename/mark),
batch edits with undo/redo, the cut (preserve + profiles as data), the
hand-off, statistics, the attachments browser, model steps (cost, flow.run,
describe).  Registered into ``ops.OPS``; names and parameter schemas are in
``data/ops.json`` like every other op."""
import logging
from pathlib import Path
from typing import Any

from . import edits as _edits
from .cut.common import archive_slugs, copy_stream, find_archive
from .cut.preserve import cut_preserve
from .cut.profiles import render_profile
from .cut.views import RenderCtx
from .expand import Selection
from .ops import OPS, op
from .package import Package
from .redact import Redactor
from .timestamps import TimePolicy, from_iso

log = logging.getLogger(__name__)


# ───────────────────────────── package tree ─────────────────────────────
@op("package.copy")
def op_package_copy(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    if eng.package is None:
        eng.package = Package()
    src = p.get("from", "selection")
    if src == "all":
        sel = {u.uid: None for u in eng.conversations}
    elif src == "selection":
        sel = dict(eng.selection.units)
    else:
        raise ValueError(f"package.copy: from must be selection | all, got {src!r}")
    added = eng.package.copy_in(eng.conversations, sel, into=str(p.get("into") or "/"))
    return {"counts": {"added": added, "entries": len(eng.package.entries)}, "tree_hash": eng.package.tree_hash()}


def _targets(eng: Any, p: dict[str, Any]) -> list[Any]:
    pkg = eng.ensure_package()
    refs = p.get("items")
    if refs:
        return pkg.find([str(r) for r in refs])
    _, items = _edits.resolve_scope(eng, pkg, p.get("where") or "all", "conversation")
    seen: list[Any] = []
    for it in items:
        e = next((e for e in pkg.entries if e.unit is it.unit), None)
        if e is not None and e not in seen:
            seen.append(e)
    return seen


@op("package.move")
def op_package_move(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    ents = _targets(eng, p)
    into = str(p["into"])
    for e in ents:
        e.folder = into
    eng.package.folders.add(into)
    return {"counts": {"moved": len(ents)}, "tree_hash": eng.package.tree_hash()}


@op("package.mkdir")
def op_package_mkdir(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    pkg = eng.ensure_package()
    pkg.folders.add(str(p["path"]))
    return {"counts": {"folders": len(pkg.folders)}, "tree_hash": pkg.tree_hash()}


@op("package.remove")
def op_package_remove(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    ents = _targets(eng, p)
    pkg = eng.package
    pkg.entries = [e for e in pkg.entries if e not in ents]
    return {"counts": {"removed": len(ents), "entries": len(pkg.entries)}, "tree_hash": pkg.tree_hash()}


@op("package.rename")
def op_package_rename(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    ents = _targets(eng, {"items": [p["item"]]})
    for e in ents:
        e.name = str(p["title"])
    return {"counts": {"renamed": len(ents)}, "tree_hash": eng.package.tree_hash()}


@op("package.mark")
def op_package_mark(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    """Mark the leaf whose path is kept by ``branches.keep: marked``."""
    pkg = eng.ensure_package()
    ents = pkg.find([str(p["item"])])
    for e in ents:
        if p["leaf"] not in e.unit.by_id():
            raise ValueError(f"package.mark: message {p['leaf']!r} not in {e.unit.uid}")
        pkg.marks[e.unit.uid] = str(p["leaf"])
    return {"counts": {"marked": len(ents)}}


@op("package.show")
def op_package_show(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    pkg = eng.ensure_package()
    order = p.get("order") or eng.ds.preset("cut.order", "created")
    ents = pkg.ordered(order)
    return {"counts": {"entries": len(ents), "folders": len(pkg.folders)}, "package": pkg.as_dict(),
            "entries": [{"key": e.key, "uid": e.unit.uid, "title": None if eng.aggregate else (e.name or e.unit.title),
                         "folder": e.folder, "messages": len(e.unit.messages) if e.messages is None else len(e.messages)} for e in ents]}


# ───────────────────────────── edits, undo, redo ─────────────────────────────
def _register_edit_ops() -> None:
    for name in list(_edits.EDITS):
        def fn(eng: Any, p: dict[str, Any], _name: str = name) -> dict[str, Any]:
            preview = bool(p.pop("preview", False))
            res = _edits.run_edit(eng, _name, p, preview=preview)
            if eng.aggregate:
                res["patch"] = len(res["patch"])
            out = {"counts": {"changed": res["count"], "items": res["scope"]["items"]}, **res}
            if preview:
                out["_no_record"] = True  # a preview changes nothing, so it is not a recipe step
            return out
        OPS[f"edit.{name}"] = fn


_register_edit_ops()


@op("edit.user")
def op_edit_user(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    """A user's edit registered by a data entry (``edits/<id>.json`` with ``module: "user:<name>"``)."""
    preview = bool(p.get("preview"))
    params = dict(p.get("params") or {})
    if p.get("where") is not None:
        params["where"] = p["where"]
    res = _edits.run_edit(eng, str(p["edit"]), params, preview=preview)
    if not preview:  # undo/redo find the recipe step by the op that recorded it
        eng.package.oplog[-1]["recipe_op"] = "edit.user"
        eng.package.oplog[-1]["recipe_params"] = {k: v for k, v in p.items() if k != "preview"}
    out = {"counts": {"changed": res["count"], "items": res["scope"]["items"]}, **res, "user_code": True}
    if preview:
        out["_no_record"] = True
    return out


@op("undo")
def op_undo(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    pkg = eng.ensure_package()
    n = int(p.get("steps") or 1)
    undone = []
    for _ in range(n):
        if not pkg.oplog:
            break
        entry = pkg.oplog[-1]
        if "_snapshot" not in entry:
            raise ValueError(f"undo: {entry['op']} is irreversible ({entry.get('invertible')}); nothing was changed")
        pkg.oplog.pop()
        pkg.restore(entry["_snapshot"])
        pkg.redo.append(entry)
        steps = eng.recipe.get("steps") or []
        for i in range(len(steps) - 1, -1, -1):
            if steps[i]["op"] == entry.get("recipe_op", entry["op"]):
                del steps[i]
                break
        undone.append(entry["op"])
    return {"counts": {"undone": len(undone)}, "undone": undone, "tree_hash": pkg.tree_hash()}


@op("redo")
def op_redo(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    pkg = eng.ensure_package()
    n = int(p.get("steps") or 1)
    redone = []
    for _ in range(n):
        if not pkg.redo:
            break
        entry = pkg.redo.pop()
        keep = list(pkg.redo)
        eng.op(entry.get("recipe_op", entry["op"]), dict(entry.get("recipe_params", entry["params"])), by="engine", note="redo")
        pkg.redo = keep
        redone.append(entry["op"])
    return {"counts": {"redone": len(redone)}, "redone": redone, "tree_hash": pkg.tree_hash()}


# ───────────────────────────── cut ─────────────────────────────
def _redactor(eng: Any, p: dict[str, Any]) -> Redactor:
    red = getattr(eng, "_cut_redactor", None)
    key = (str(p.get("redaction") or "none"), tuple(p.get("dictionary") or []))
    if red is None or getattr(eng, "_cut_redactor_key", None) != key:
        red = Redactor(p.get("redaction") or "none", eng.ds, terms=list(p.get("dictionary") or []))
        eng._cut_redactor, eng._cut_redactor_key = red, key
    return red


def _copy_attachments(eng: Any, entries: list[Any], stage: Any, mode: str) -> tuple[dict[tuple[str, str], str], dict[str, Any]]:
    paths: dict[tuple[str, str], str] = {}
    info = {"referenced": 0, "copied": 0, "bytes": 0, "missing": [], "mode": mode}
    slugs = archive_slugs(eng.archives)
    for e in entries:
        u = e.unit
        arc_label = (u.source or {}).get("archive") or ""
        arc = find_archive(eng.archives, u)
        for a in u.attachments:
            info["referenced"] += 1
            if a.status != "resolved" or not a.member:
                info["missing"].append({"ref": a.ref, "conversation": u.uid, "message": a.message, "status": a.status})
                continue
            key = (arc_label, a.member)
            if key in paths or mode == "reference_only":
                continue
            mem = arc.member(a.member) if arc is not None else None
            if mem is None:
                info["missing"].append({"ref": a.ref, "conversation": u.uid, "message": a.message, "status": "member not found"})
                continue
            to = (u.flags or {}).get("attachments_to")
            if to:  # edit.move_attachments: the user's layout; a name already taken gets the archive slug in front
                d = to.replace("{key}", e.key).replace("{uid}", u.uid.split(":", 1)[-1][:8]).replace("{service}", u.service or "x")
                rel = f"attachments/{d}/{a.member.rsplit('/', 1)[-1]}"
                if rel in paths.values():
                    rel = f"attachments/{d}/{slugs[(arc.label, arc.sha256)]}-{a.member.rsplit('/', 1)[-1]}"
            else:
                rel = f"attachments/{slugs[(arc.label, arc.sha256)]}/{a.member}"
            target = stage.root / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            with mem.open() as fi, open(target, "wb") as fo:
                n = copy_stream(fi, fo)
            stage.add_file(rel, profile="attachments")
            paths[key] = rel
            info["copied"] += 1
            info["bytes"] += n
    if mode == "copy_all":
        for arc in eng.archives:
            for m in arc.members:
                if m.kind == "asset" and (arc.label, m.name) not in paths:
                    rel = f"attachments/{slugs[(arc.label, arc.sha256)]}/{m.name}"
                    target = stage.root / rel
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with m.open() as fi, open(target, "wb") as fo:
                        info["bytes"] += copy_stream(fi, fo)
                    stage.add_file(rel, profile="attachments")
                    paths[(arc.label, m.name)] = rel
                    info["copied"] += 1
    return paths, info


def _profiles(eng: Any, names: Any) -> list[dict[str, Any]]:
    out = []
    for n in names or []:
        if isinstance(n, dict):
            from .cut.profiles import check_profile
            probs = check_profile(n)
            if probs:
                raise ValueError(f"inline profile {n.get('id')}: " + "; ".join(probs))
            out.append(n)
        else:
            out.append(dict(eng.ds.entry("profiles", str(n))))
    return out


@op("cut")
def op_cut(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    pkg = eng.ensure_package()
    entries = pkg.ordered(p.get("order") or "package")
    stage = eng.stage()
    profiles = _profiles(eng, p.get("profiles"))
    red = _redactor(eng, p)
    restructure = [pr for pr in profiles if pr.get("kind", "restructure") != "preserve"]
    att_mode = str(p.get("attachments") or "copy_used")
    if red.active and red.attachments == "omit":
        att_mode = "reference_only"
    att_paths, att_info = _copy_attachments(eng, entries, stage, att_mode) if restructure else ({}, None)
    firsts = sorted(x for e in entries for x in [e.unit.created_utc] + [m.created_utc for m in e.unit.messages] if x)
    infos: list[dict[str, Any]] = []
    preserve_info = None
    tool = eng.tool_info()
    sources = [{"label": a.label, "sha256": a.sha256, "format": a.format} for a in eng.archives]
    for prof in profiles:
        if prof.get("kind") == "preserve":
            res = cut_preserve(eng, stage, {e.origin for e in entries}, settings=prof, redactor=red,
                               attachments_mode="reference_only" if att_mode == "reference_only" else att_mode)
            preserve_info = res.as_dict(eng.aggregate)
            if pkg.oplog:
                preserve_info["edits_note"] = "batch edits change restructuring profiles; preserve is the source bytes"
            partial = sum(1 for e in entries if e.messages is not None)
            if partial:
                preserve_info["partial_whole"] = partial
            infos.append({"profile": "preserve", "kind": "preserve", "conversations": preserve_info["conversations"]})
            continue
        tp = TimePolicy(prof.get("timestamps"), zone=p.get("zone"), format=p.get("time_format"), shift=p.get("shift"),
                        anonymise=p.get("anonymise"), granularity=p.get("granularity"))
        tp.origin = from_iso(firsts[0]) if firsts else None
        ctx = RenderCtx(eng, time=tp, redactor=red, att_paths=att_paths, aggregate=eng.aggregate, summaries=eng.summaries,
                        extract_text=bool(p.get("extract_text")))
        info = render_profile(prof, entries, ctx, stage, ds=eng.ds, tool=tool, sources=sources,
                              clusters=None if eng.aggregate else eng.clusters)
        info["timestamps"] = tp.as_dict()
        infos.append(info)
    if eng.aggregate:
        for i in infos:
            if isinstance(i.get("outputs"), list):
                i["outputs"] = len(i["outputs"])
    eng.redaction_entries = list(red.entries)
    eng.cut_info = {"profiles": infos, "preserve": preserve_info, "attachments": _public_att(att_info, eng.aggregate),
                    "redaction": red.report(), "entries": len(entries), "order": p.get("order") or "package"}
    return {"counts": {"profiles": len(infos), "conversations": len(entries), "files": len(stage.items),
                       "masked": len(red.entries)}, "cut": eng.cut_info}


def _public_att(info: Any, aggregate: bool) -> Any:
    if info is None:
        return None
    d = dict(info)
    if aggregate:
        d["missing"] = len(d["missing"])
    return d


@op("handoff")
def op_handoff(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    from .handoff import build_handoff
    eng.ensure_units()
    pkg = eng.ensure_package()
    entries = pkg.ordered(p.get("order") or "package")
    red = _redactor(eng, {"redaction": p.get("redaction") or getattr(eng, "_cut_redactor_key", ("none",))[0],
                          "dictionary": list(getattr(eng, "_cut_redactor_key", ("none", ()))[1])})
    from .recipe import recipe_hash
    norm = eng.normalised_recipe()
    norm["steps"] = [st for st in norm.get("steps") or [] if st.get("op") != "write"]
    info = build_handoff(eng, entries, eng.stage(), branches=str(p.get("branches") or "current"), with_graph=bool(p.get("graph")),
                         zip_too=bool(p.get("zip", True)), redactor=red, recipe_sha="sha256:" + recipe_hash(norm, eng.ds))
    eng.handoff_info = info
    return {"counts": {k: info[k] for k in ("conversations", "messages", "messages_with_text", "attachments")}, "handoff": info}


@op("stats")
def op_stats(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    from .stats import package_stats
    eng.ensure_units()
    if p.get("of", "package") == "package":
        entries = eng.ensure_package().ordered(p.get("order") or "package")
    else:
        from .cut.common import Entry, assign_keys
        entries = [Entry(unit=u, origin=u.uid) for u in eng.conversations]
        assign_keys(entries)
    eng.stats = package_stats(entries, aggregate=eng.aggregate)
    return {"counts": {k: eng.stats["total"][k] for k in ("conversations", "messages", "words")}, "stats": eng.stats}


@op("attachments")
def op_attachments(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    """Attachments browser: every reference with its status, by type, size and conversation."""
    eng.ensure_units()
    scope = p.get("of", "all")
    units = eng.conversations if scope == "all" else [eng.by_uid[u] for u in eng.selection.units if u in eng.by_uid]
    rows = []
    for u in units:
        for a in u.attachments:
            if p.get("missing") and a.status == "resolved":
                continue
            if p.get("mime") and not str(a.mime or "").startswith(str(p["mime"])):
                continue
            if p.get("min_size") and int(a.size or 0) < int(p["min_size"]):
                continue
            if p.get("conversation") and p["conversation"] not in (u.uid, u.ext_id):
                continue
            rows.append({"conversation": u.uid, "service": u.service, "ref": None if eng.aggregate else a.ref,
                         "name": None if eng.aggregate else a.name, "mime": a.mime, "size": a.size, "status": a.status,
                         "member": None if eng.aggregate else a.member, "message": None if eng.aggregate else a.message})
    by_status: dict[str, int] = {}
    by_mime: dict[str, int] = {}
    for r in rows:
        by_status[r["status"]] = by_status.get(r["status"], 0) + 1
        by_mime[str(r["mime"] or "?")] = by_mime.get(str(r["mime"] or "?"), 0) + 1
    return {"counts": {"attachments": len(rows), **{f"status_{k}": v for k, v in sorted(by_status.items())}},
            "by_mime": dict(sorted(by_mime.items())), "bytes": sum(int(r["size"] or 0) for r in rows), "rows": rows}


# ───────────────────────────── model steps ─────────────────────────────
def _pending_work(eng: Any) -> dict[str, dict[str, Any]]:
    pend = (eng.cascade.model_pending if eng.cascade else {}) or {}
    return {f: {"calls": int(v.get("grains", 0)), "chars": int(v.get("chars", 0))} for f, v in pend.items()}


@op("cost")
def op_cost(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    """Estimate of the paid steps (nothing is called): the cascade's model levels on what stayed ambiguous,
    plus summaries of the package when asked."""
    eng.ensure_units()
    work = _pending_work(eng)
    if p.get("summaries"):
        ents = eng.ensure_package().entries
        work["summarize"] = {"calls": len(ents), "chars": sum(e.unit.stats.get("chars", 0) if e.unit.stats else 0 for e in ents)}
    est = eng.models().estimate(work)
    return {"counts": {"calls": est["calls"]}, "estimate": est}


@op("flow.run")
def op_flow_run(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    """Run a model flow over the package (``summarize``) or a text (``describe_criteria``) — estimate first."""
    flow = str(p["flow"])
    runner = eng.models()
    if flow == "describe_criteria":
        text = str(p.get("text") or "")
        est = runner.estimate({flow: {"calls": 1, "chars": len(text)}})
        runner.authorize(est)
        parsed = runner.call(flow, {"description": text})
        return {"counts": {"calls": 1}, "criteria": parsed, "estimate": est}
    if flow == "name_clusters":
        if not eng.clusters:
            eng.op("clusters", by="engine")
        cl = eng.clusters.get("clusters") or []
        prompts = [{"terms": ", ".join(x["term"] for x in (c.get("top_terms") or [])[:10]),
                    "titles": " | ".join(str(x) for x in (c.get("sample_titles") or [])[:6])} for c in cl]
        est = runner.estimate({flow: {"calls": len(cl), "chars": sum(len(v["terms"]) + len(v["titles"]) for v in prompts)}})
        runner.authorize(est)
        for c, v in zip(cl, prompts):
            parsed = runner.call(flow, v)
            c["name"] = parsed.get("name")
            c["name_source"] = f"model:{flow}"
        return {"counts": {"calls": len(cl)}, "estimate": est, "names": {c["id"]: c.get("name") for c in cl}}
    ents = eng.ensure_package().ordered("package")
    texts = {e.unit.uid: "\n".join(f"{m.role}: {m.text()}" for m in e.unit.messages if m.text() and (e.messages is None or m.id in e.messages))
             for e in ents}
    est = runner.estimate({flow: {"calls": len(ents), "chars": sum(len(v) for v in texts.values())}})
    runner.authorize(est)
    n = 0
    for e in ents:
        parsed = runner.call(flow, {"title": e.unit.title or "", "window": texts[e.unit.uid]})
        if flow == "summarize":
            eng.summaries[e.unit.uid] = {"summary": parsed.get("summary"), "open_questions": parsed.get("open_questions") or [],
                                         "source": f"model:{flow}"}
        n += 1
    return {"counts": {"calls": n}, "estimate": est}


@op("describe")
def op_describe(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    """A filter described in words → criteria (keywords, regexes, vector queries) shown for correction.
    Without a model: the rule grammar (``data/grammar``); with ``model``: the ``describe_criteria`` flow."""
    from .sentence import describe_rules
    text = str(p["text"])
    if p.get("model"):
        res = op_flow_run(eng, {"flow": "describe_criteria", "text": text})
        crit = res["criteria"] if isinstance(res.get("criteria"), dict) else {}
        crit.setdefault("schema", "loom.extract.criteria/1")
        crit.setdefault("id", "described")
        return {"counts": {"keywords": len(crit.get("keywords") or [])}, "criteria": crit, "source": "model:describe_criteria",
                "estimate": res.get("estimate")}
    crit, rest = describe_rules(text, eng.ds)
    return {"counts": {"keywords": len(crit.get("keywords") or []), "regex": len(crit.get("regex") or [])}, "criteria": crit,
            "source": "rules", "rest": rest}


__all__ = ["OPS"]
