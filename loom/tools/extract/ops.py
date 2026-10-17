"""Operations as functions ``(engine, params) → result``; the registry of names,
parameter schemas and flags is ``data/ops.json``.  Each op is one recipe step.
This slice implements the pipeline up to the selection and a package writer
for what the slice produces (inventory, timeline, diff, selection with
explanations, clusters, decisions, recipe, manifest with a reproducible hash,
a Polish report); cut/profiles/edits/handoff arrive in slice B as more ops.
"""
import hashlib
import io
import json
import logging
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Optional

from . import cluster as _cluster
from . import diff as _diff
from . import expand as _expand
from . import inventory as _inventory
from . import read as _read
from . import sources as _sources
from . import timeline as _timeline
from . import timestamps as _ts
from .cascade import Cascade
from .criteria import build_criteria, criteria_from_terms, load_criteria
from .datafiles import write_json
from .decisions import Decisions
from .expand import Selection
from .model import Unit, canonical_json, sha256_text
from .recipe import normalise, recipe_hash, steps_by_face
from .select_expr import ExprError, items_of, select as expr_select

log = logging.getLogger(__name__)
OPS: dict[str, Callable[[Any, dict[str, Any]], dict[str, Any]]] = {}


def op(name: str) -> Callable[[Callable[..., dict[str, Any]]], Callable[..., dict[str, Any]]]:
    def deco(fn: Callable[..., dict[str, Any]]) -> Callable[..., dict[str, Any]]:
        OPS[name] = fn
        return fn
    return deco


# ───────────────────────────── pipeline ─────────────────────────────
@op("scan")
def op_scan(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    if not eng.sources:
        raise ValueError("scan: no sources (set_sources or recipe.sources)")
    eng.archives = _sources.scan(eng.sources, catalog=eng.catalog, nested_limit=int(p.get("nested_limit") or 0))
    if eng.aggregate:  # aggregate mode: no file name leaves the engine, archives are numbered
        for i, a in enumerate(eng.archives, 1):
            a.label = f"archive-{i}"
    eng.units_raw, eng.units, eng.merge_result = [], [], None
    members = sum(len(a.members) for a in eng.archives)
    unknown = sum(1 for a in eng.archives for m in a.members if not m.format and m.kind not in ("asset", "container"))
    unreadable = [(a.label, m.name) for a in eng.archives for m in a.members if m.kind == "unreadable"]
    if unreadable:  # a damaged member is a warning with its name; the scan goes on with the others
        from .i18n import t as _t
        eng.emit("note", text=_t("scan.unreadable", n=len(unreadable),
                                 members=", ".join(f"{a}:{m}" for a, m in unreadable) if not eng.aggregate else "…"))
    return {"counts": {"archives": len(eng.archives), "members": members, "members_unknown": unknown,
                       "members_unreadable": len(unreadable)},
            "archives": [{"label": a.label, "kind": a.kind, "format": a.format, "members": len(a.members), "sha256": a.sha256,
                          "size": a.size, "path": None if eng.aggregate else a.path} for a in eng.archives]}


@op("read")
def op_read(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    if not eng.archives:
        eng.op("scan", by="engine")
    ctx = eng.read_context()
    ctx.keep_raw = bool(p.get("keep_raw", ctx.keep_raw))
    eng.units_raw = _read.read_archives(eng.archives, ctx, progress=lambda d: eng.emit("progress", **d))
    eng.units, eng.merge_result = list(eng.units_raw), None
    eng._vectors.clear()
    kinds: dict[str, int] = {}
    for u in eng.units_raw:
        kinds[u.kind] = kinds.get(u.kind, 0) + 1
    return {"counts": {"units": len(eng.units_raw), "messages": sum(len(u.messages) for u in eng.units_raw),
                       "unknown_units": kinds.get("unknown", 0)}, "by_kind": dict(sorted(kinds.items()))}


@op("merge")
def op_merge(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    if not eng.units_raw:
        eng.op("read", by="engine")
    from . import merge as _merge
    eng.merge_result = _merge.merge(eng.units_raw, newest_wins=bool(p.get("newest_wins", True)))
    eng.units = eng.merge_result.units
    eng._vectors.clear()
    return {"counts": {"units": len(eng.units), "duplicates": eng.merge_result.duplicates, "versions": eng.merge_result.versions,
                       "conversations": len(eng.conversations)}}


@op("inventory")
def op_inventory(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    eng.inventory = _inventory.build_inventory(eng.archives, eng.units, eng.merge_result, eng.ds,
                                               aggregate=bool(p.get("aggregate")) or eng.aggregate)
    inv = eng.inventory
    return {"counts": {"archives": len(inv.get("archives") or []), "units": inv.get("units", {}).get("total", 0),
                       "conversations": inv.get("conversations", 0) if isinstance(inv.get("conversations"), int) else inv.get("conversations", {}).get("total", 0),
                       "messages": inv.get("messages", {}).get("total", 0)}, "inventory": inv}


@op("timeline")
def op_timeline(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    lo = _ts.parse_date_bound(p["from"]) if p.get("from") else None
    hi = _ts.parse_date_bound(p["to"], end=True) if p.get("to") else None
    shift = _ts.parse_duration(p["shift"]).total_seconds() if p.get("shift") else 0.0
    eng.timeline, eng.timeline_rows = _timeline.build_timeline(
        eng.units, eng.ds, key=p.get("key"), zone=p.get("zone"), bucket=p.get("bucket"), gap_days=p.get("gap_days"),
        burst_ratio=p.get("burst_ratio"), window=(lo, hi), shift_seconds=shift, anonymise=bool(p.get("anonymise")),
        granularity=p.get("granularity"), continuations=p.get("continuations"), lex=eng.lex)
    tl = eng.timeline
    if eng.aggregate:  # shared terms are content words: only their number leaves an aggregate run
        for c in tl.get("continuations") or []:
            if isinstance(c.get("shared_terms"), list):
                c["shared_terms"] = len(c["shared_terms"])
    return {"counts": {"rows": len(eng.timeline_rows), "buckets": len((tl.get("buckets") or {}).get("rows") or []),
                       "gaps": len(tl.get("gaps") or []), "bursts": len(tl.get("bursts") or []),
                       "continuations": len(tl.get("continuations") or [])},
            "key": tl.get("key"), "range": tl.get("range"), "timeline": tl}


def _load_units_from(eng: Any, paths: list[str]) -> tuple[list[Unit], str]:
    """Units of another export / package: a package (manifest.json + units.jsonl) is read from its
    snapshot; anything else goes through scan → read → merge with the same settings."""
    snapshot: Optional[Path] = None
    for raw in paths:
        q = Path(str(raw)).expanduser()
        if q.is_dir() and (q / "units.jsonl").exists():
            snapshot = q / "units.jsonl"
        elif q.name in ("manifest.json", "units.jsonl") and (q.parent / "units.jsonl").exists():
            snapshot = q.parent / "units.jsonl"
    if snapshot is not None and len(paths) == 1:
        units = []
        with open(snapshot, encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if line:
                    units.append(Unit.from_dict(json.loads(line)))
        return units, f"snapshot:{snapshot.name}"
    from . import merge as _merge
    archives = _sources.scan([str(x) for x in paths], catalog=eng.catalog)
    ctx = eng.read_context()
    raw_units = _read.read_archives(archives, ctx)
    return _merge.merge(raw_units).units, "sources"


@op("diff")
def op_diff(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    against = p.get("against")
    if isinstance(against, str):
        against = [against]
    if not against:
        raise ValueError("diff: 'against' (previous export, package or manifest) is required")
    old_units, how = _load_units_from(eng, [str(x) for x in against])
    eng.diff = _diff.diff_units(old_units, eng.units, identity=str(p.get("identity") or "ext_id"),
                                summary_only=bool(p.get("summary_only")), aggregate=eng.aggregate,
                                old_label=None if eng.aggregate else ", ".join(Path(str(x)).name for x in against),
                                new_label=None if eng.aggregate else ", ".join(Path(s).name for s in eng.sources))
    eng.diff["old"]["how"] = how
    result = {"counts": dict(eng.diff["counts"]), "diff": eng.diff}
    mode = p.get("mode") or "none"
    if mode != "none":
        uids, notes = _diff.select_uids(eng.diff, mode)
        sel = Selection()
        for uid in sorted(uids):
            sel.add(uid, None, f"diff:{mode}")
        result["selection"] = eng.apply_selection(sel, "add", {uid: {"source": "diff", "mode": mode} for uid in uids})
        result["notes"] = notes
    return result


# ───────────────────────────── selection ─────────────────────────────
def _criteria_from_params(eng: Any, p: dict[str, Any]) -> Any:
    crit = p.get("criteria")
    terms = list(p.get("terms") or [])
    regexes = list(p.get("regex") or [])
    if crit:
        overrides: dict[str, Any] = {}
        if terms or regexes:
            overrides["keywords"] = [{"term": t, "inflect": "rules", "weight": 1.0} for t in terms]
            overrides["regex"] = regexes
        return load_criteria(crit, eng.ds, eng.lex, overrides=overrides or None)
    if terms or regexes:
        return criteria_from_terms(terms, eng.ds, eng.lex, regexes=regexes)
    preset = eng.ds.preset("criteria")
    if preset:
        return load_criteria(preset, eng.ds, eng.lex)
    raise ValueError("select.criteria: give criteria (id, file or object) or terms/regex")


@op("select.criteria")
def op_select_criteria(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    crit = _criteria_from_params(eng, p)
    eng.criteria = crit
    manual = eng.decisions.as_manual(eng.units)
    vectors = eng.vector_index("window") if crit.vectors else None
    if crit.vectors:
        # the cascade indexes its own grains; here the index is created lazily by the cascade helper
        vectors = _window_index(eng)
    model_on = bool(p.get("model")) or eng.model_enabled()
    count_only = bool(p.get("model_count")) and not model_on
    cas = Cascade(eng.units, crit, eng.ds, eng.lex, cascade_id=p.get("cascade") or None, grain=p.get("grain") or None,
                  manual=manual, vectors=vectors, model_enabled=model_on or count_only)
    res = cas.run()
    estimate = None
    if count_only and res.model_pending:
        work = {f: {"calls": int(v.get("grains", 0)), "chars": int(v.get("chars", 0))} for f, v in res.model_pending.items()}
        estimate = eng.models().estimate(work)
    if model_on and res.model_pending:
        # two passes: the local levels decide what stays ambiguous, the estimate of the model levels on exactly that
        # is shown and must be accepted (or the run stops with the reason), then the model levels run (cached)
        runner = eng.models()
        work = {f: {"calls": int(v.get("grains", 0)), "chars": int(v.get("chars", 0))} for f, v in res.model_pending.items()}
        estimate = runner.estimate(work)
        eng.emit("question" if not runner.accept else "note", text="model estimate", estimate=estimate)
        runner.authorize(estimate)
        cas = Cascade(eng.units, crit, eng.ds, eng.lex, cascade_id=p.get("cascade") or None, grain=p.get("grain") or None,
                      manual=manual, vectors=vectors, model_enabled=True, model_runner=runner,
                      topic=crit.description or crit.id)
        res = cas.run()
    eng.cascade = res
    eng.unit_decisions = {uid: d.as_dict() for uid, d in res.units.items()}
    eng.explain = res.explain_rows()
    sel = Selection()
    meta: dict[str, dict[str, Any]] = {}
    for uid, d in res.units.items():
        if d.decision == "include":
            sel.add(uid, d.selected_messages, f"criteria:{crit.id}:{d.source}:{round(d.score, 3)}")
            meta[uid] = {"source": d.source, "score": round(d.score, 4), "confidence": round(d.confidence, 4), "band": d.decision}
    applied = eng.apply_selection(sel, p.get("mode") or "add", meta)
    for ud in res.units.values():
        if ud.manual:
            eng.recipe.setdefault("manual", {"include": [], "exclude": []})
    model_note = None
    if model_on and estimate is None:
        model_note = "models enabled; nothing stayed ambiguous, so no model was called"
    elif not model_on and res.model_pending:
        model_note = "model levels counted, not called (enable with --model and accept the estimate)"
    return {"counts": {**res.counts(), "grains": len(res.grains)}, "criteria": {"id": crit.id, "source": crit.source,
            "terms": crit.terms, "vectors": len(crit.vectors)}, "cascade": res.as_dict(), "selection": applied,
            "manual_applied": sum(1 for d in res.units.values() if d.manual), "model_note": model_note, "model_estimate": estimate,
            "shadow": {k: len(v) for k, v in res.shadow.items()}}


def _window_index(eng: Any) -> Any:
    """A vector index over window grains so the cascade's L2 can query it (built once per engine)."""
    from .cascade import make_grains
    from .vectors import VectorIndex
    if "window" in eng._vectors:
        return eng._vectors["window"]
    tier = eng.ds.preset("vectors.tier")
    idx = VectorIndex(tier=int(tier) if tier is not None else None, allow_download=bool(eng.ds.preset("vectors.allow_download", False)),
                      ask=eng.ask)
    items = []
    for u in eng.conversations:
        for g in make_grains(u, "window", ["text"], int(eng.ds.preset("window.size", 6)), int(eng.ds.preset("window.step", 3))):
            if g.text.strip():
                items.append((g.id, g.text))
    idx.index(items)
    if idx.note:
        eng.emit("note", text=idx.note)
    eng._vectors["window"] = idx
    return idx


def _selection_from_matches(matches: list[tuple[Any, list[str]]], kind: str, label: str) -> tuple[Selection, dict[str, dict[str, Any]]]:
    sel = Selection()
    meta: dict[str, dict[str, Any]] = {}
    for item, reasons in matches:
        reason = f"{label}: " + "; ".join(reasons[:3])
        if kind == "conversation":
            sel.add(item.unit.uid, None, reason)
        else:
            mid = item.message.id if item.message is not None else None
            if mid is None and getattr(item, "attachment", None) is not None:
                mid = item.attachment.message  # an attachment selects the message that carries it, when known
            if mid is None:
                sel.add(item.unit.uid, None, reason)
            else:
                sel.add(item.unit.uid, {mid}, reason)
        meta.setdefault(item.unit.uid, {"source": label, "reasons": []})["reasons"].append(reasons[:3])
    return sel, meta


@op("select.expr")
def op_select_expr(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    kind = p.get("kind") or "conversation"
    ctx = eng.expr_context()
    items = items_of(eng.conversations, kind)
    matches = expr_select(p["expr"], items, ctx)
    sel, meta = _selection_from_matches(matches, kind, f"expr:{p['expr']}")
    applied = eng.apply_selection(sel, p.get("mode") or "add", meta)
    return {"counts": {"matched": len(matches), "evaluated": len(items)}, "selection": applied,
            "matches": [] if eng.aggregate else [{"id": it.id, "reasons": r} for it, r in matches[:200]]}


@op("select.structural")
def op_select_structural(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    patterns = (eng.ds.file("select_fields.json").get("patterns") or {})
    name = p["pattern"]
    if name not in patterns:
        raise ExprError(f"unknown structural pattern {name!r} (known: {', '.join(sorted(patterns))})", 0, name)
    expr = patterns[name]["expr"]
    kind = p.get("kind") or "conversation"
    ctx = eng.expr_context()
    items = items_of(eng.conversations, kind)
    matches = expr_select(expr, items, ctx)
    sel, meta = _selection_from_matches(matches, kind, f"pattern:{name}")
    applied = eng.apply_selection(sel, p.get("mode") or "add", meta)
    return {"counts": {"matched": len(matches), "evaluated": len(items)}, "pattern": name, "expr": expr, "selection": applied}


@op("select.similar")
def op_select_similar(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    to = p.get("to") or []
    if isinstance(to, str):
        to = [to]
    idx = eng.vector_index("conversation")
    thr = float(p.get("threshold") or 0.0)
    cand = int(p.get("candidates") or 0)
    sel = Selection()
    meta: dict[str, dict[str, Any]] = {}
    found: dict[str, float] = {}
    for uid in to:
        if uid not in eng.by_uid:
            raise ValueError(f"select.similar: unknown unit {uid!r}")
        for other, score in idx.similar_to(uid, candidates=cand, threshold=thr):
            if other in to:
                continue
            if score > found.get(other, -1.0):
                found[other] = score
                sel.add(other, None, f"like:{uid}@{round(score, 3)} ({idx.tier_name})")
                meta[other] = {"source": "similar", "like": uid, "score": round(score, 4), "tier": idx.tier}
    applied = eng.apply_selection(sel, p.get("mode") or "add", meta)
    return {"counts": {"matched": len(found)}, "selection": applied, "index": idx.describe(),
            "matches": [] if eng.aggregate else sorted(({"uid": k, "score": round(v, 4)} for k, v in found.items()),
                                                      key=lambda x: (-x["score"], x["uid"]))}


@op("select.diff")
def op_select_diff(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    if eng.diff is None:
        raise ValueError("select.diff: run 'diff' first")
    uids, notes = _diff.select_uids(eng.diff, p.get("mode") or "only_new")
    sel = Selection()
    for uid in sorted(uids):
        sel.add(uid, None, f"diff:{p.get('mode') or 'only_new'}")
    applied = eng.apply_selection(sel, p.get("combine") or "intersect", {uid: {"source": "diff"} for uid in uids})
    return {"counts": {"matched": len(uids)}, "selection": applied, "notes": notes}


@op("select.cluster")
def op_select_cluster(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    if eng.clusters is None:
        eng.op("clusters", by="engine")
    cid = p["cluster"]
    cl = next((c for c in eng.clusters.get("clusters") or [] if c["id"] == cid), None)
    if cl is None:
        raise ValueError(f"select.cluster: unknown cluster {cid!r} (known: {', '.join(c['id'] for c in eng.clusters.get('clusters') or [])})")
    sel = Selection()
    for uid in cl["conversations"]:
        sel.add(uid, None, f"cluster:{cid}")
    applied = eng.apply_selection(sel, p.get("mode") or "add", {uid: {"source": "cluster", "cluster": cid} for uid in cl["conversations"]})
    return {"counts": {"matched": len(cl["conversations"])}, "selection": applied, "cluster": {k: v for k, v in cl.items() if k != "conversations"}}


@op("select.manual")
def op_select_manual(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    if p.get("decisions"):
        eng.decisions = Decisions.load(p["decisions"])
    inc = Selection()
    exc = Selection()
    applied_inc = applied_exc = 0
    carried: list[dict[str, str]] = []
    unknown = 0
    for e, uid, how in eng.decisions.resolve(eng.units):
        if uid is None:
            unknown += 1
            continue
        if how == "identity":  # a mark on an older version of a conversation that continued: carried, and said
            carried.append({"from": e["unit"], "to": uid, "ext_id": e.get("ext_id", "")})
        target = inc if e["decision"] == "include" else exc if e["decision"] == "exclude" else None
        if target is None:
            continue
        ms = {e["message"]} if e.get("message") else None
        target.add(uid, ms, f"manual:{e['decision']}:{e.get('reason', '')}")
        if target is inc:
            applied_inc += 1
        else:
            applied_exc += 1
    r1 = eng.apply_selection(inc, "add", {uid: {"source": "manual", "decision": "include"} for uid in inc.units})
    r2 = eng.apply_selection(exc, "subtract")
    for uid in inc.units:
        eng.unit_decisions.setdefault(uid, {}).update({"decision": "include", "source": "manual"})
    for uid in exc.units:
        if exc.units[uid] is None:
            eng.unit_decisions.setdefault(uid, {}).update({"decision": "exclude", "source": "manual"})
    if carried:
        from .i18n import t as _t
        eng.emit("note", text=_t("decisions.carried", n=len(carried)))
    if unknown:
        from .i18n import t as _t
        eng.emit("note", text=_t("decisions.unknown", n=unknown))
    return {"counts": {"include": applied_inc, "exclude": applied_exc, "entries": len(eng.decisions.entries),
                       "unknown_units": unknown, "carried": len(carried)}, "carried": carried,
            "selection": r2, "decisions_source": eng.decisions.source, "after_include": r1}


@op("select.clear")
def op_select_clear(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    n = len(eng.selection.units)
    eng.selection, eng.selection_meta = Selection(), {}
    return {"counts": {"cleared": n}}


@op("select.invert")
def op_select_invert(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    sel = Selection()
    for u in eng.conversations:
        if u.uid not in eng.selection.units:
            sel.add(u.uid, None, "invert")
    applied = eng.apply_selection(sel, "replace")
    return {"counts": {"selected": applied["after"]}, "selection": applied}


@op("expand")
def op_expand(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    conts = (eng.timeline or {}).get("continuations") if eng.timeline else None
    res = _expand.expand(eng.selection, eng.units, str(p.get("preset") or "conversation"), continuations=conts)
    before = eng.selection.message_count(eng.by_uid)
    eng.selection = res.selection
    eng.expand_result = res.as_dict()
    return {"counts": {"added_messages": res.added_messages, "linked_conversations": res.linked_conversations,
                       "messages_before": before, "messages_after": eng.selection.message_count(eng.by_uid)},
            "preset": res.preset}


def _public_meta(meta: Optional[dict[str, Any]], aggregate: bool) -> Optional[dict[str, Any]]:
    if meta is None or not aggregate:
        return meta
    return {k: v for k, v in meta.items() if k not in ("reasons", "like")}


def public_clusters(eng: Any) -> dict[str, Any]:
    """Clusters as they may leave the engine: in aggregate mode the content terms become a count."""
    doc = eng.clusters or {}
    if not eng.aggregate:
        return doc
    out = dict(doc)
    out["clusters"] = [dict(c, top_terms=len(c.get("top_terms") or []), sample_titles=[]) for c in doc.get("clusters") or []]
    return out


@op("clusters")
def op_clusters(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    eng.clusters = _cluster.discover(eng.units, eng.ds, eng.lex, min_conversations=p.get("min_conversations"),
                                     top_terms=p.get("top_terms"), max_clusters=p.get("max_clusters"),
                                     min_similarity=p.get("min_similarity"), aggregate=eng.aggregate)
    return {"counts": dict(eng.clusters["counts"]), "clusters": public_clusters(eng)["clusters"]}


# ───────────────────────────── decisions ─────────────────────────────
@op("mark")
def op_mark(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    uid = p["unit"]
    if p["decision"] == "remove":
        ok = eng.decisions.unmark(uid, grain=p.get("grain") or "conversation", message=p.get("message"))
        return {"counts": {"removed": int(ok)}}
    was = None
    d = eng.unit_decisions.get(uid)
    if d:
        was = {"decision": d.get("decision"), "score": d.get("score"), "source": d.get("source"), "level": d.get("best_grain")}
    u = eng.by_uid.get(uid)
    entry = eng.decisions.mark(uid, p["decision"], p.get("reason") or "", grain=p.get("grain") or "conversation",
                               message=p.get("message"), was=was, by="engine",
                               service=u.service if u is not None else None, ext_id=u.ext_id if u is not None else None)
    manual = eng.recipe.setdefault("manual", {"include": [], "exclude": []})
    bucket = manual.setdefault(p["decision"], [])
    bucket[:] = [e for e in bucket if e.get("unit") != uid]
    bucket.append({"unit": uid, "reason": p.get("reason") or "",
                   **({"service": u.service or "", "ext_id": u.ext_id} if u is not None and u.ext_id else {})})
    sel = Selection()
    sel.add(uid, {p["message"]} if p.get("message") else None, f"manual:{p['decision']}")
    if p["decision"] == "include":
        eng.apply_selection(sel, "add", {uid: {"source": "manual"}})
    elif p["decision"] == "exclude":
        eng.apply_selection(sel, "subtract")
    return {"counts": {"entries": len(eng.decisions.entries)}, "entry": entry}


@op("propose")
def op_propose(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    props = eng.decisions.propose(eng.units, eng.ds, eng.lex, min_corrections=p.get("min_corrections"))
    return {"counts": {"proposals": len(props), "corrections": len(eng.decisions.corrections())}, "proposals": props}


@op("explain")
def op_explain(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    uid = p["unit"]
    by = eng.by_uid
    hit = None
    for k, u in by.items():
        if k == uid or k.startswith(uid) or (u.ext_id and u.ext_id == uid):
            hit = u
            break
    if hit is None:
        raise ValueError(f"explain: unknown unit {uid!r}")
    rows = [r for r in eng.explain if str(r.get("unit") or r.get("grain", "")).startswith(hit.uid)]
    return {"unit": hit.uid, "title": None if eng.aggregate else hit.title, "service": hit.service,
            "decision": eng.unit_decisions.get(hit.uid), "grains": rows,
            "selected": hit.uid in eng.selection.units, "selection_reasons": eng.selection.reasons.get(hit.uid, []),
            "manual": next((e for e in eng.decisions.entries if e["unit"] == hit.uid), None),
            "meta": eng.selection_meta.get(hit.uid)}


# ───────────────────────────── dry run / write / report ─────────────────────────────
def _tokens_est(eng: Any, chars: int) -> int:
    return int(chars / float(eng.ds.preset("tokens.chars_per_token.default", 3.6) or 3.6))


@op("dry_run")
def op_dry_run(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    eng.ensure_units()
    summary = eng.selection_summary()
    by = eng.by_uid
    attachments = 0
    missing = 0
    bytes_est = 0
    for uid, ms in eng.selection.units.items():
        u = by.get(uid)
        if u is None:
            continue
        for a in u.attachments:
            attachments += 1
            if a.status == "missing":
                missing += 1
            bytes_est += int(a.size or 0)
        for m in u.messages:
            if ms is None or m.id in ms:
                bytes_est += m.chars()
    pending = (eng.cascade.model_pending if eng.cascade else {}) or {}
    model_est = eng.models().estimate({f: {"calls": int(v.get("grains", 0)), "chars": int(v.get("chars", 0))} for f, v in pending.items()}) \
        if pending else None
    return {"counts": {"conversations": summary["conversations"], "messages": summary["messages"], "attachments": attachments,
                       "attachments_missing": missing, "chars": summary["chars"], "tokens_est": summary["tokens_est"]},
            "package_bytes_est": bytes_est, "by_service": summary["by_service"], "whole": summary["whole"], "partial": summary["partial"],
            "model_pending": model_est,
            "cost_total": (model_est or {}).get("total") if model_est else {"amount": 0.0, "currency": "USD", "known": True, "note": "0 — bez modelu"},
            "would_write": [] if not p.get("package") else [str(p["package"])],
            "units": [] if eng.aggregate else [{"uid": uid, "title": by[uid].title, "service": by[uid].service,
                                                "messages": len(by[uid].messages) if ms is None else len(ms),
                                                "reasons": eng.selection.reasons.get(uid, [])[:3]}
                                               for uid, ms in sorted(eng.selection.units.items()) if uid in by]}


def _write_text(path: Path, text: str) -> tuple[str, int]:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = text.encode("utf-8", errors="backslashreplace")
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(data)
    tmp.replace(path)
    return hashlib.sha256(data).hexdigest(), len(data)


def _json_text(obj: Any) -> str:
    return json.dumps(obj, ensure_ascii=False, indent=1) + "\n"


@op("write")
def op_write(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    from .manifest import build_manifest, reproducible_hash
    from .report import build_report
    eng.ensure_units()
    aggregate = bool(p.get("aggregate")) or eng.aggregate
    pkg = Path(str(p["package"])).expanduser()
    from .i18n import t as _t
    try:
        pkg.mkdir(parents=True, exist_ok=True)
    except OSError as exc:  # the OUTPUT is the problem (a file in the way, no permission), not a source
        raise ValueError(_t("write.output_error", path=str(pkg), detail=f"{type(exc).__name__}: {exc}")) from exc
    outputs: list[dict[str, Any]] = []
    from .ops_cut import package_redactor, redact_edits
    # the staged cut / hand-off describe the tree they were made from: after an edit, an undo or a redo they are
    # made again from the current tree (deterministic, so the package still reproduces from its recipe)
    recut = []
    if eng.package is not None:
        now = eng.package.tree_hash()
        if eng.cut_params is not None and eng.cut_tree_hash != now:
            OPS["cut"](eng, dict(eng.cut_params))
            recut.append("cut")
        if eng.handoff_params is not None and eng.handoff_tree_hash != now:
            OPS["handoff"](eng, dict(eng.handoff_params))
            recut.append("handoff")
        if recut:
            eng.emit("note", text=_t("write.recut", ops=", ".join(recut)))

    red = getattr(eng, "_cut_redactor", None)
    if eng.cut_params is None and redact_edits(eng):  # no cut, but a redaction edit: the side files are masked too
        red = package_redactor(eng)
    red = red if red is not None and red.active else None

    def put(rel: str, text: str) -> None:
        if red is not None and rel != "recipe.json":  # the recipe is protected by references, never masked
            # a redacted package leaks nothing through its side files either (units, timeline, explain, report …);
            # JSON is redacted value by value (a mask never eats JSON syntax), Markdown as text
            if rel.endswith(".json"):
                text = _json_text(red.value(json.loads(text), {"file": rel}, "text"))
            elif rel.endswith(".jsonl"):
                text = "".join(json.dumps(red.value(json.loads(line), {"file": rel, "line": i + 1}, "text"), ensure_ascii=False) + "\n"
                               for i, line in enumerate(text.splitlines()) if line.strip())
            elif rel.endswith(".md"):
                text = red.text(text, {"file": rel}, "text")
        sha, n = _write_text(pkg / rel, text)
        outputs.append({"path": rel, "sha256": sha, "bytes": n})

    if eng.inventory is None:
        eng.op("inventory", {"aggregate": aggregate}, by="engine")
    if eng.timeline is None:
        eng.op("timeline", by="engine")
    put("inventory.json", _json_text(eng.inventory))
    tl = dict(eng.timeline)
    put("timeline.json", _json_text(tl))
    rows = eng.timeline_rows or []
    buf = io.StringIO()
    for r in rows:
        rr = dict(r)
        if aggregate:
            for k in ("title", "text", "locator", "archive"):
                rr.pop(k, None)
        buf.write(_timeline.ts_json(rr) + "\n")
    put("timeline.jsonl", buf.getvalue())
    if eng.diff is not None:
        put("diff.json", _json_text(eng.diff))
    if eng.clusters is not None:
        put("clusters.json", _json_text(public_clusters(eng)))
    sel_doc = {"schema": "loom.extract.selection/1", "summary": eng.selection_summary(),
               "units": {uid: {"messages": (sorted(ms) if ms is not None else None),
                               "reasons": [] if aggregate else eng.selection.reasons.get(uid, []),
                               "meta": _public_meta(eng.selection_meta.get(uid), aggregate)}
                         for uid, ms in sorted(eng.selection.units.items())},
               "expand": eng.expand_result,
               "cascade": eng.cascade.as_dict() if eng.cascade else None,
               "decisions": {uid: ({k: v for k, v in d.items() if k not in ("title", "reasons", "manual")} if aggregate else d)
                             for uid, d in sorted(eng.unit_decisions.items())}}
    put("selection.json", _json_text(sel_doc))
    if p.get("explain", True) and not aggregate:
        put("explain.jsonl", "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in eng.explain))
    put("decisions.json", _json_text(eng.decisions.as_dict()))
    if p.get("units_snapshot", True) and not aggregate:
        put("units.jsonl", "".join(json.dumps(u.as_dict(), ensure_ascii=False) + "\n" for u in eng.units))
    # -- slice B: package tree, edits, cut outputs, redaction, statistics, model calls -------------------------
    if eng.package is not None:
        from .edits import oplog_rows
        if eng.stats is None:
            eng.op("stats", by="engine")
        # the redo stack is session state, not package content: an undone edit leaves no trace in the files
        put("package.json", _json_text({k: v for k, v in eng.package.as_dict().items() if k != "redo"}))
        if eng.package.oplog:
            put("oplog.jsonl", "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in oplog_rows(eng.package)))
    if eng.stats is not None:
        put("stats.json", _json_text(eng.stats))

    if eng.model_runner is not None and eng.model_runner.calls:
        put("log.jsonl", "".join(json.dumps(r, ensure_ascii=False, sort_keys=True) + "\n" for r in eng.model_runner.log_rows()))
    if eng.staged is not None:
        import shutil
        for o in sorted(eng.staged.items, key=lambda o: o["path"]):
            src = eng.stage_dir / o["path"]
            dst = pkg / o["path"]
            dst.parent.mkdir(parents=True, exist_ok=True)
            if dst.exists():
                dst.unlink()
            shutil.copyfile(src, dst)
            outputs.append({k: v for k, v in o.items()})
    recipe_norm = eng.normalised_recipe()
    # the package's recipe describes the content, not where it is written: whoever replays it names the package
    recipe_norm["steps"] = [st for st in recipe_norm.get("steps") or [] if st.get("op") != "write"]
    eng.secrets_info = None
    if red is not None:
        # a redacted package carries no dictionary term in clear and its recipe still reproduces it: sensitive
        # values become sha256 references, kept in the local store outside the package (preset redaction.secret_store)
        from .sensitive import protect_recipe, save_store, store_path
        recipe_norm, secret_vals = protect_recipe(recipe_norm, red, eng.ds)
        if secret_vals:
            kept = bool(eng.ds.preset("redaction.secret_store", True))
            where = save_store(secret_vals, ds=eng.ds) if kept else None
            eng.secrets_info = {"values": len(secret_vals), "store": str(where) if where else None,
                                "default_store": str(store_path(eng.ds))}
    put("recipe.json", _json_text(recipe_norm))
    report = build_report(eng, aggregate=aggregate)
    put("RAPORT.md", report)
    if red is not None:  # last, so the counts include the side files; masks and locations, never values
        rep = {**red.report(), "from_edits": len(redact_edits(eng))}
        if eng.cut_info is not None:
            eng.cut_info["redaction"] = rep
        _w = lambda rel, text: outputs.append(dict(zip(("sha256", "bytes"), _write_text(pkg / rel, text)), path=rel))
        _w("redaction.json", _json_text(rep))
        _w("redaction.jsonl", "".join(json.dumps(e, ensure_ascii=False) + "\n" for e in red.entries))
    outputs.sort(key=lambda o: o["path"])
    repro = reproducible_hash(outputs)
    manifest = build_manifest(eng, outputs, repro, recipe_norm, aggregate)
    if red is not None:  # the manifest is written last: the package's masks, without entries (outputs and hashes stay)
        manifest = red.scrub(manifest, skip=("outputs", "recipe", "reproducible_hash", "sha256", "tree_hash", "id"))
    old = pkg / "manifest.json"
    if old.exists():  # files of an earlier run that this run did not produce are removed, so the package is exactly the manifest
        try:
            keep = {o["path"] for o in outputs} | {"manifest.json"}
            for o in json.loads(old.read_text(encoding="utf-8")).get("outputs") or []:
                q = pkg / o["path"]
                if o["path"] not in keep and q.is_file() and pkg.resolve() in q.resolve().parents:
                    q.unlink()
        except (OSError, json.JSONDecodeError):
            log.debug("previous manifest unreadable; stale files left", exc_info=True)
    _write_text(pkg / "manifest.json", _json_text(manifest))
    eng.last_write = {"package": str(pkg), "reproducible_hash": repro, "outputs": len(outputs)}
    return {"counts": {"outputs": len(outputs) + 1, "bytes": sum(o["bytes"] for o in outputs)}, "package": str(pkg),
            "reproducible_hash": repro, **({"recut": recut} if recut else {}),
            # aggregate mode: output paths carry attachment and member names, so only their number leaves the engine
            "outputs": len(outputs) + 1 if aggregate else [o["path"] for o in outputs] + ["manifest.json"]}


@op("report")
def op_report(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    from .report import build_report
    eng.ensure_units()
    if eng.inventory is None:
        eng.op("inventory", by="engine")
    text = build_report(eng, aggregate=eng.aggregate)
    return {"counts": {"chars": len(text)}, "report": text}


def user_module_path(base: Path, module: str, what: str) -> Path:
    """``<overlay>/user/<module>.py`` and nothing else: the module is a plain Python name (no path, no ``..``) and the
    resolved file lies inside the overlay's ``user/`` directory — a recipe cannot point at any other file on disk."""
    if not module.isidentifier():
        raise ValueError(f"{what}: module must be a Python name (a file in <overlay>/user/), got {module!r}")
    root = (Path(base) / "user").resolve()
    path = (root / f"{module}.py").resolve()
    if not path.is_relative_to(root):
        raise ValueError(f"{what}: module {module!r} resolves outside {root}")
    if not path.exists():
        raise ValueError(f"{what}: module not found: {path}")
    return path


@op("user.call")
def op_user_call(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    """A user function from the overlay (``<data dir>/extract/user/<module>.py``), loaded only on explicit request."""
    import importlib.util
    from .paths import overlay_dir
    base = overlay_dir()
    if base is None:
        raise ValueError("user.call: no overlay directory (app data dir unknown)")
    path = user_module_path(base, str(p["module"]), "user.call")
    if not str(p["function"]).isidentifier():
        raise ValueError(f"user.call: function must be a Python name, got {p['function']!r}")
    spec = importlib.util.spec_from_file_location(f"loom_extract_user_{p['module']}", path)
    mod = importlib.util.module_from_spec(spec)  # type: ignore[arg-type]
    assert spec and spec.loader
    spec.loader.exec_module(mod)
    fn = getattr(mod, p["function"])
    out = fn(eng, dict(p.get("args") or {}))
    eng.emit("note", text=f"user code executed: {path}:{p['function']}")
    return {"counts": {}, "result": out, "user_code": str(path)}


from . import ops_cut  # noqa: E402,F401  (registers the slice B operations)
from . import ops_views  # noqa: E402,F401  (registers the slice C operations)
