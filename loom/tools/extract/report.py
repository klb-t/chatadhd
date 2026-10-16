"""``RAPORT.md``: in the owner's words, in the report language (preset ``pl``).
Sections of this slice: what was found, what could not be read, what the
formats do not contain, what remained ambiguous, what it cost ("0 — bez
modelu"), changes between exports, how to reproduce.  Sentences come from the
i18n table; numbers come from the inventory and the engine state (measured).
In aggregate mode no title, text fragment or file name appears.
"""
from typing import Any

from .i18n import label, t


def _line(items: list[str]) -> str:
    return "\n".join(f"- {x}" for x in items) if items else f"- {t('report.none')}"


def build_report(eng: Any, *, aggregate: bool = False) -> str:
    inv = eng.inventory or {}
    out: list[str] = [f"# {t('report.title')}", ""]
    # -- what was found ----------------------------------------------------------
    out += [f"## {t('report.found')}", ""]
    for a in inv.get("archives") or []:
        fmt = a.get("format") or "?"
        hyp = f" ({t('report.hypothesis')})" if a.get("format_hypothesis") else ""
        units = a.get("units") or {}
        out.append(f"- {a.get('label')}: {fmt}{hyp} — " + t("report.archive_line", members=a.get("members", {}).get("total", 0),
                                                            known=a.get("members", {}).get("known", 0),
                                                            unknown=a.get("members", {}).get("unknown", 0),
                                                            conversations=units.get("conversation", 0) + units.get("session", 0)))
    conv = inv.get("conversations") or 0
    msgs = inv.get("messages") or {}
    out.append("")
    out.append(t("report.totals", conversations=conv if isinstance(conv, int) else conv.get("total", 0), messages=msgs.get("total", 0),
                 services=", ".join(f"{k}: {v}" for k, v in (inv.get("units", {}).get("by_service") or {}).items()) or "-"))
    dates = inv.get("dates") or {}
    if dates.get("from") or dates.get("to"):
        out.append(t("report.date_range", **{"from": dates.get("from") or "?", "to": dates.get("to") or "?"}))
    tools = inv.get("tools") or {}
    if tools.get("calls") is not None:
        out.append(t("report.tools", calls=tools.get("calls", 0), names=", ".join(sorted((tools.get("names") or {}).keys())[:12]) or "-"))
    thinking = inv.get("thinking") or {}
    if thinking:
        out.append(t("report.thinking", blocks=thinking.get("blocks", 0), chars=thinking.get("chars", 0), full=thinking.get("full", 0)))
    branches = inv.get("branches") or {}
    if branches:
        out.append(t("report.branches", with_forks=branches.get("conversations_with_forks", 0), forks=branches.get("forks", 0)))
    att = inv.get("attachments") or {}
    if att:
        out.append(t("report.attachments", referenced=att.get("referenced", 0), resolved=att.get("resolved", 0), missing=att.get("missing", 0)))
    merge = inv.get("merge") or {}
    if merge:
        out.append(t("report.merge", duplicates=merge.get("duplicates", 0), versions=merge.get("versions", 0)))
    sel = eng.selection_summary()
    out += ["", f"### {t('report.selection')}", "",
            t("report.selection_line", conversations=sel["conversations"], messages=sel["messages"], tokens=sel["tokens_est"])]
    if eng.cascade is not None:
        c = eng.cascade.counts()
        out.append(t("report.cascade_line", include=c.get("include", 0), ambiguous=c.get("ambiguous", 0), exclude=c.get("exclude", 0),
                     cascade=eng.cascade.cascade_id, grain=eng.cascade.grain))
        sh = {k: len(v) for k, v in eng.cascade.shadow.items()}
        out.append(t("report.shadow", lexical_only=sh.get("lexical_only", 0), vectors_only=sh.get("vectors_only", 0), both=sh.get("both", 0)))
    if eng.clusters:
        out += ["", f"### {t('report.clusters')}", ""]
        for c in eng.clusters.get("clusters") or []:
            terms = "…" if aggregate else ", ".join(x["term"] for x in (c.get("top_terms") or [])[:6])
            out.append(f"- {c['id']}{(' „' + c['name'] + '”') if c.get('name') and not aggregate else ''}: " + t("report.cluster_line", size=c["size"], terms=terms,
                                             period=f"{(c.get('period') or {}).get('from') or '?'} – {(c.get('period') or {}).get('to') or '?'}"))
    # -- timeline ----------------------------------------------------------------
    if eng.timeline:
        tl = eng.timeline
        out += ["", f"## {t('report.timeline')}", "",
                t("report.timeline_line", key=tl.get("key"), rows=len(eng.timeline_rows or []), gaps=len(tl.get("gaps") or []),
                  bursts=len(tl.get("bursts") or []), continuations=len(tl.get("continuations") or []))]
    # -- diff --------------------------------------------------------------------
    if eng.diff:
        d = eng.diff["counts"]
        out += ["", f"## {t('report.diff')}", "",
                t("report.diff_line", new=d.get("new", 0), changed=d.get("changed", 0), removed=d.get("removed", 0), unchanged=d.get("unchanged", 0))]
    # -- unreadable ----------------------------------------------------------------
    out += ["", f"## {t('report.unreadable')}", ""]
    unreadable: list[str] = []
    for a in inv.get("archives") or []:
        for m in (a.get("members") or {}).get("unknown_list") or []:
            unreadable.append(f"{a.get('label')}: {m.get('name')} ({m.get('size')} B) — {m.get('why')}")
        if aggregate and (a.get("members") or {}).get("unknown"):
            unreadable.append(t("report.unknown_count", archive=a.get("label"), n=a["members"]["unknown"]))
    for m in (att.get("missing_list") or []):
        unreadable.append(t("report.missing_attachment", ref=m.get("ref"), conversation=m.get("conversation")))
    unk = inv.get("unknown") or {}
    if unk.get("by_native"):
        unreadable.append(t("report.unknown_blocks", n=sum((unk.get("by_native") or {}).values()),
                            kinds=", ".join(sorted((unk.get("by_native") or {}).keys())[:10]) or "-"))
    out.append(_line(unreadable))
    # -- absent ----------------------------------------------------------------
    out += ["", f"## {t('report.absent')}", ""]
    absent: list[str] = []
    from .i18n import language
    for fmt_id, entry in (inv.get("absent") or {}).items():
        if isinstance(entry, dict):
            ka = entry.get("known_absent") or {}
            items = (ka.get(language()) or next((v for v in ka.values() if v), [])) if isinstance(ka, dict) else list(ka)
            obs = entry.get("observed") or []
            absent.append(f"{fmt_id}: " + "; ".join([str(x) for x in items] + [label(o) if isinstance(o, dict) else str(o) for o in obs]))
        elif isinstance(entry, list):
            absent.append(f"{fmt_id}: " + "; ".join(str(x) for x in entry))
    out.append(_line(absent))
    # -- ambiguous ---------------------------------------------------------------
    out += ["", f"## {t('report.ambiguous')}", ""]
    if eng.cascade is not None:
        amb = [d for d in eng.cascade.units.values() if d.decision == "ambiguous"]
        pend = eng.cascade.model_pending or {}
        out.append(t("report.ambiguous_line", n=len(amb)))
        if pend:
            out.append(t("report.model_pending", flows=", ".join(f"{k}: {v.get('grains', 0)}" for k, v in pend.items())))
        if amb and not aggregate:
            out.append(_line([f"{d.unit.uid} — {d.unit.title or ''} ({round(d.score, 3)})" for d in amb[:20]]))
        out.append(t("report.how_to_resolve"))
    else:
        out.append(f"- {t('report.none')}")
    # -- cut (slice B) -----------------------------------------------------------
    cut = getattr(eng, "cut_info", None)
    if cut:
        out += ["", f"## {t('report.cut')}", ""]
        for pinfo in cut.get("profiles") or []:
            out.append("- " + t("report.cut_line", profile=pinfo.get("profile"), conversations=pinfo.get("conversations", 0),
                                messages=pinfo.get("messages", 0),
                                files=pinfo["outputs"] if isinstance(pinfo.get("outputs"), int) else len(pinfo.get("outputs") or [])))
            if pinfo.get("branches_cut"):
                out.append("  - " + t("report.cut_branches", n=pinfo["branches_cut"], m=pinfo.get("messages_cut", 0)))
        pres = cut.get("preserve")
        if pres:
            out.append("- " + t("report.cut_preserve", conversations=pres.get("conversations", 0),
                                exact=t("report.yes") if pres.get("byte_exact") else t("report.no_redacted")))
            for a in pres.get("archives") or []:
                om = a.get("omitted") or []
                ind = a.get("indivisible") or []
                if isinstance(om, int) or aggregate:
                    out.append("  - " + t("report.cut_omitted_count", archive=a.get("archive"), n=om if isinstance(om, int) else len(om)))
                else:
                    for o in om:
                        out.append("  - " + t("report.cut_omitted", archive=a.get("archive"), member=o.get("member"), why=o.get("why")))
                for o in ([] if isinstance(ind, int) else ind):
                    out.append("  - " + t("report.cut_indivisible", member=o.get("member") if not aggregate else "…", n=o.get("other_conversations", 0)))
                for nr in a.get("not_redacted") or []:
                    out.append("  - " + t("report.not_redacted", member=nr if not aggregate else "…"))
            if pres.get("partial_whole"):
                out.append("  - " + t("report.cut_partial", n=pres["partial_whole"]))
        att = cut.get("attachments")
        if att:
            miss = att.get("missing")
            out.append("- " + t("report.cut_attachments", referenced=att.get("referenced", 0), copied=att.get("copied", 0),
                                bytes=att.get("bytes", 0), missing=miss if isinstance(miss, int) else len(miss or [])))
            if not aggregate and isinstance(miss, list):
                for m in miss[:50]:
                    out.append("  - " + t("report.missing_attachment", ref=m.get("ref"), conversation=m.get("conversation")))
    if getattr(eng, "handoff_info", None):
        h = eng.handoff_info
        out += ["", f"## {t('report.handoff')}", "",
                "- " + t("report.handoff_line", conversations=h.get("conversations", 0), messages=h.get("messages", 0),
                         text=h.get("messages_with_text", 0), attachments=h.get("attachments", 0), missing=h.get("attachments_missing", 0))]
    if getattr(eng, "stats", None):
        st = eng.stats
        tot = st.get("total") or {}
        out += ["", f"## {t('report.stats')}", "",
                "- " + t("report.stats_line", conversations=tot.get("conversations", 0), messages=tot.get("messages", 0),
                         words=tot.get("words", 0), agentic=tot.get("agentic", 0),
                         **{"from": (st.get("period") or {}).get("from") or "?", "to": (st.get("period") or {}).get("to") or "?"}),
                "- " + t("report.stats_services", services=", ".join(f"{k}: {v}" for k, v in (st.get("services") or {}).items()) or "-",
                         languages=", ".join(f"{k}: {v}" for k, v in (st.get("languages") or {}).items()) or "-")]
        for row in st.get("topic_timeline") or []:
            out.append(f"  - {row['month']}: " + t("report.stats_month", conversations=row["conversations"], messages=row["messages"]))
    # -- masked ------------------------------------------------------------------
    out += ["", f"## {t('report.masked')}", ""]
    red = (cut or {}).get("redaction") or {}
    reps = list(getattr(eng, "redaction_reports", []) or [])
    if red.get("active") or reps:
        if red.get("active"):
            out.append("- " + t("report.masked_line", profile=red.get("profile"), n=red.get("entries", 0),
                                kinds=", ".join(f"{k}: {v}" for k, v in (red.get("by_kind") or {}).items()) or "-"))
            if red.get("attachments") != "omit":
                out.append("- " + t("report.masked_attachments"))
        for r in reps:
            out.append("- " + t("report.masked_edit", profile=r.get("profile"), n=r.get("entries", 0),
                                kinds=", ".join(f"{k}: {v}" for k, v in (r.get("by_kind") or {}).items()) or "-"))
    else:
        out.append(f"- {t('report.masked_none')}")
    # -- cost --------------------------------------------------------------------
    out += ["", f"## {t('report.cost')}", ""]
    runner = getattr(eng, "model_runner", None)
    calls, total = runner.summary() if runner is not None else ([], None)
    if calls:
        for c in calls:
            amt = c["cost"]["amount"] if c["cost"]["known"] else t("report.cost_unknown")
            out.append("- " + t("report.cost_flow", flow=c["flow"], model=c["model"], calls=c["calls"], cache=c["cache_hits"],
                                tin=c["tokens_in"], tout=c["tokens_out"], amount=amt))
        out.append("- " + t("report.cost_total", amount=total["amount"] if total and total.get("known") else t("report.cost_unknown"),
                            currency=(total or {}).get("currency", "USD")))
    else:
        out.append(f"- {t('report.cost_zero')}")
    # -- edits -------------------------------------------------------------------
    out += ["", f"## {t('report.edits')}", ""]
    pkg = getattr(eng, "package", None)
    if pkg is not None and pkg.oplog:
        for e in pkg.oplog:
            irr = f" ({t('report.irreversible')})" if e.get("invertible") == "none" else ""
            out.append(f"- {e['n']}. {e['op']}: " + "; ".join(e.get("lines") or [str(e.get("count"))]) + irr)
    else:
        out.append(f"- {t('report.edits_none')}")
    # -- reproduce ---------------------------------------------------------------
    out += ["", f"## {t('report.reproduce')}", "", t("report.reproduce_line", steps=len(eng.recipe.get('steps') or [])),
            t("report.reproduce_manifest"), ""]
    return "\n".join(out)
