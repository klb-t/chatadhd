"""Restructuring profiles as data (``data/profiles/*.json``) rendered from the
package entries: ``restructure`` (views → outputs), ``timeline`` (one
chronological axis of the package across sources) and ``cards``.  The code
knows output kinds and policies; which fields, blocks, branches, times and
files a profile produces is the profile's data."""
from typing import Any, Optional

from .. import outputs as out
from .. import timeline as _timeline
from ..datafiles import DataStore
from ..i18n import label, t
from .common import Entry, Outputs, deterministic_zip
from .views import RenderCtx, conversation_views

PROFILE_KINDS = ("preserve", "restructure", "timeline", "cards")
OUTPUT_KINDS = ("json", "jsonl", "csv", "markdown", "markdown_index", "markdown_journal", "markdown_cards", "graph", "zip")
ROWS = ("conversations", "messages", "blocks", "timeline", "cards")


def check_profile(prof: dict[str, Any]) -> list[str]:
    """Problems of a profile file (empty = valid)."""
    p: list[str] = []
    if prof.get("schema") != "loom.extract.profile/1":
        p.append("schema must be loom.extract.profile/1")
    kind = prof.get("kind", "restructure")
    if kind not in PROFILE_KINDS:
        p.append(f"kind must be one of {', '.join(PROFILE_KINDS)}")
    if kind == "preserve":
        return p
    for i, o in enumerate(prof.get("outputs") or []):
        if o.get("kind") not in OUTPUT_KINDS:
            p.append(f"outputs[{i}].kind must be one of {', '.join(OUTPUT_KINDS)}")
        if not o.get("path"):
            p.append(f"outputs[{i}].path missing")
        if o.get("rows") and o["rows"] not in ROWS:
            p.append(f"outputs[{i}].rows must be one of {', '.join(ROWS)}")
    if not prof.get("outputs"):
        p.append("outputs: at least one output")
    from .views import BLOCK_POLICY, BRANCH_KEEP
    keep = (prof.get("branches") or {}).get("keep", "current")
    if keep not in BRANCH_KEEP:
        p.append(f"branches.keep must be one of {', '.join(BRANCH_KEEP)}")
    for k, v in (prof.get("blocks") or {}).items():
        if v not in BLOCK_POLICY:
            p.append(f"blocks.{k} must be one of {', '.join(BLOCK_POLICY)}")
    return p


def _expand_path(tpl: str, conv: dict[str, Any], profile_id: str) -> str:
    key = conv["key"] + (f"-p{conv['path']}" if "path" in conv else "")
    return (tpl.replace("{conversation.key}", key).replace("{conversation.uid_hex}", conv.get("uid_hex", ""))
            .replace("{profile}", profile_id))


def _rows(views: list[dict[str, Any]], what: str) -> list[dict[str, Any]]:
    if what == "conversations":
        return [v["conversation"] for v in views]
    if what == "messages":
        return [m for v in views for m in v["messages"]]
    if what == "blocks":
        rows = []
        for v in views:
            for m in v["messages_full"]:
                for i, b in enumerate(m.get("blocks") or []):
                    rows.append({"conversation": m["conversation"], "message": m["id"], "role": m["role"], "created": m["created"],
                                 "block": i, **b})
        return rows
    raise ValueError(f"rows {what!r} not available for this profile kind")


def render_profile(prof: dict[str, Any], entries: list[Entry], ctx: RenderCtx, outputs: Outputs, *, ds: DataStore,
                   tool: dict[str, Any], sources: list[dict[str, Any]], clusters: Optional[dict[str, Any]] = None) -> dict[str, Any]:
    pid = prof["id"]
    kind = prof.get("kind", "restructure")
    produced: list[str] = []
    info: dict[str, Any] = {"profile": pid, "kind": kind, "outputs": produced, "conversations": 0, "messages": 0}

    def put(rel: str, text: str) -> None:
        outputs.write_text(rel, text, profile=pid)
        produced.append(rel)

    if kind == "timeline":
        return _render_timeline(prof, entries, ctx, put, info, ds)
    if kind == "cards":
        return _render_cards(prof, entries, ctx, put, info)
    views: list[dict[str, Any]] = []
    for e in entries:
        views.extend(conversation_views(e, prof, ctx))
    info["conversations"] = len(views)
    info["messages"] = sum(len(v["messages"]) for v in views)
    info["branches_cut"] = sum(int(v["conversation_full"].get("branches_cut") or 0) for v in views)
    info["messages_cut"] = sum(int(v["conversation_full"].get("messages_cut") or 0) for v in views)
    md_links: dict[str, str] = {}
    for spec in prof.get("outputs") or []:
        k, path = spec["kind"], spec["path"]
        per_conv = "{conversation." in path
        if k == "jsonl":
            put(path, out.jsonl(_rows(views, spec.get("rows", "messages"))))
        elif k == "json":
            if per_conv:
                for v in views:
                    put(_expand_path(path, v["conversation_full"], pid), out.json_doc({"conversation": v["conversation"], "messages": v["messages"]}))
            else:
                rows = spec.get("rows", "conversations")
                if rows == "conversations" and spec.get("nested", True):
                    doc = [{**v["conversation"], "messages": v["messages"]} for v in views]
                else:
                    doc = _rows(views, rows)
                put(path, out.json_doc({"schema": "loom.extract.profile_output/1", "profile": pid, "rows": rows, "items": doc}))
        elif k == "csv":
            rows = spec.get("rows", "messages")
            cols = spec.get("columns") or ((prof.get("fields") or {}).get("message") if rows == "messages" else
                                           (prof.get("fields") or {}).get("conversation"))
            put(path, out.csv_doc(_rows(views, rows), cols))
        elif k == "markdown":
            if per_conv:
                for v in views:
                    rel = _expand_path(path, v["conversation_full"], pid)
                    put(rel, out.markdown_conversation(v, rel))
                    md_links.setdefault(v["conversation_full"]["key"], rel)
            else:
                put(path, "\n---\n\n".join(out.markdown_conversation(v, path) for v in views))
        elif k == "markdown_index":
            put(path, out.markdown_index(views, md_links, path))
        elif k == "graph":
            put(path, out.json_doc(out.graph_packet(views, sources=sources, tool=tool, clusters=clusters)))
        elif k == "zip":
            include = spec.get("include") or [pid + "/"]
            files = [p for p in produced if any(p.startswith(pre) for pre in include)]
            if spec.get("attachments"):
                atts = sorted({b["path"] for v in views for m in v["messages_full"] for b in m.get("attachments") or [] if b.get("path")})
                files += [a for a in atts if a not in files]
            deterministic_zip(outputs.root / path, [(f, outputs.root / f) for f in files])
            outputs.add_file(path, profile=pid)
            produced.append(path)
        else:
            raise ValueError(f"profile {pid}: unknown output kind {k!r}")
    return info


def _render_timeline(prof: dict[str, Any], entries: list[Entry], ctx: RenderCtx, put: Any, info: dict[str, Any], ds: DataStore) -> dict[str, Any]:
    key = prof.get("key") or ("activity" if prof.get("rows", "messages") == "messages" else ds.preset("timeline.key", "created"))
    tl, rows = _timeline.build_timeline([e.unit for e in entries], ds, key=key, lex=ctx.eng.lex if ctx.eng is not None else None)
    keys = {e.unit.uid: e.key for e in entries}
    sel = {e.unit.uid: e.messages for e in entries}
    out_rows: list[dict[str, Any]] = []
    for r in rows:
        ms = sel.get(r["conversation"])
        if ms is not None and r.get("message") is not None and r["message"] not in ms:
            continue
        row = {"t": ctx.time.render(r.get("t_src"), r.get("t_utc") or r.get("t")), "t_src": r.get("t_src"), "kind": r.get("kind"),
               "conversation": keys.get(r["conversation"]), "uid": r.get("uid"), "service": r.get("service"), "account": r.get("account"),
               "role": r.get("role"), "title": None if ctx.aggregate else ctx.redactor.text(r.get("title"), {"conversation": r["conversation"]}, "title"),
               "chars": r.get("chars"), "how": r.get("how"), "inferred": r.get("inferred"),
               "locator": None if ctx.aggregate else r.get("locator")}
        out_rows.append({k: v for k, v in row.items() if v is not None})
    for g in tl.get("gaps") or []:
        out_rows.append({"t": ctx.time.render(None, g["from"]), "kind": "gap", "to": ctx.time.render(None, g["to"]), "days": g["days"]})
    for b in tl.get("bursts") or []:
        out_rows.append({"t": ctx.time.render(None, b["bucket"]), "kind": "burst", "messages": b["messages"], "ratio_to_median": b["ratio_to_median"]})
    for c in tl.get("continuations") or []:
        out_rows.append({"t": None, "kind": "continuation", "from": keys.get(c.get("from"), c.get("from")), "to": keys.get(c.get("to"), c.get("to")),
                         "gap_minutes": c.get("gap_minutes"), "confidence": c.get("confidence"), "inferred": True})
    out_rows.sort(key=lambda r: (r.get("t") is None, str(r.get("t") or ""), {"gap": 1, "burst": 2, "continuation": 3}.get(r.get("kind", ""), 0)))
    info["conversations"] = len(entries)
    info["messages"] = sum(1 for r in out_rows if r.get("kind") == "message")
    info["gaps"], info["bursts"], info["continuations"] = len(tl.get("gaps") or []), len(tl.get("bursts") or []), len(tl.get("continuations") or [])
    for spec in prof.get("outputs") or []:
        if spec["kind"] == "jsonl":
            put(spec["path"], out.jsonl(out_rows))
        elif spec["kind"] == "markdown_journal":
            put(spec["path"], _journal(out_rows))
        elif spec["kind"] == "json":
            put(spec["path"], out.json_doc({"schema": "loom.extract.timeline/1", "key": key, "rows": out_rows,
                                            "buckets": tl.get("buckets"), "range": tl.get("range")}))
    return info


def _journal(rows: list[dict[str, Any]]) -> str:
    lines = [f"# {t('md.journal')}", ""]
    day = None
    for r in rows:
        tt = str(r.get("t") or "")
        d = tt[:10] if tt else "?"
        if d != day:
            lines += ["", f"## {d}", ""]
            day = d
        if r.get("kind") == "gap":
            lines.append(f"> {t('md.gap', days=r.get('days'))}")
        elif r.get("kind") == "burst":
            lines.append(f"> {t('md.burst', messages=r.get('messages'), ratio=r.get('ratio_to_median'))}")
        elif r.get("kind") == "continuation":
            lines.append(f"> {t('md.continuation', src=r.get('from'), dst=r.get('to'), conf=r.get('confidence'))}")
        else:
            hhmm = tt[11:16] if len(tt) >= 16 else ""
            lines.append(f"- {hhmm} · {r.get('service') or '?'} · {r.get('role') or r.get('kind')} · {r.get('title') or ''} ({r.get('chars') or 0})"
                         .replace("  ", " "))
    return "\n".join(lines).strip() + "\n"


def _render_cards(prof: dict[str, Any], entries: list[Entry], ctx: RenderCtx, put: Any, info: dict[str, Any]) -> dict[str, Any]:
    cards = []
    for e in entries:
        for v in conversation_views(e, prof, ctx):
            c = v["conversation_full"]
            roles: dict[str, int] = {}
            for m in v["messages_full"]:
                roles[m["role"]] = roles.get(m["role"], 0) + 1
            summ = ctx.summaries.get(e.unit.uid) or {}
            cards.append({"key": c["key"], "uid": c["uid"], "title": c.get("title"), "service": c.get("service"),
                          "period": {"from": c.get("first_message"), "to": c.get("last_message")}, "participants": roles,
                          "models": c.get("models"), "tools": c.get("tools"), "attachments": c.get("attachments"),
                          "messages": c.get("messages"), "summary": summ.get("summary"), "open_questions": summ.get("open_questions") or [],
                          "summary_source": summ.get("source"),
                          "note": None if summ.get("summary") else t("cards.no_summary")})
    info["conversations"] = len(cards)
    for spec in prof.get("outputs") or []:
        if spec["kind"] == "json":
            put(spec["path"], out.json_doc({"schema": "loom.extract.cards/1", "cards": cards}))
        elif spec["kind"] == "markdown_cards":
            lines = [f"# {t('md.cards')}", ""]
            for cd in cards:
                lines += [f"## {cd.get('title') or t('md.untitled')}", "",
                          f"- {t('md.service')}: {cd.get('service') or '?'}",
                          f"- {t('md.period')}: {cd['period'].get('from') or '?'} – {cd['period'].get('to') or '?'}",
                          f"- {t('md.participants')}: " + ", ".join(f"{k} {v}" for k, v in sorted(cd["participants"].items())),
                          f"- {t('md.tools')}: " + (", ".join(f"{k} {v}" for k, v in (cd.get("tools") or {}).items()) or "-"),
                          f"- {t('md.summary')}: {cd.get('summary') or cd.get('note')}", ""]
                for q in cd.get("open_questions") or []:
                    lines.append(f"  - {q}")
            put(spec["path"], "\n".join(lines).strip() + "\n")
    return info


def profile_label(prof: dict[str, Any]) -> str:
    return label(prof.get("label"))
