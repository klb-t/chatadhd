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
            terms = ", ".join(x["term"] for x in c.get("top_terms") or [][:6])
            out.append(f"- {c['id']}: " + t("report.cluster_line", size=c["size"], terms=terms,
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
    # -- masked ------------------------------------------------------------------
    out += ["", f"## {t('report.masked')}", "", f"- {t('report.masked_none')}"]
    # -- cost --------------------------------------------------------------------
    out += ["", f"## {t('report.cost')}", "", f"- {t('report.cost_zero')}"]
    # -- edits -------------------------------------------------------------------
    out += ["", f"## {t('report.edits')}", "", f"- {t('report.edits_none')}"]
    # -- reproduce ---------------------------------------------------------------
    out += ["", f"## {t('report.reproduce')}", "", t("report.reproduce_line", steps=len(eng.recipe.get('steps') or [])), ""]
    return "\n".join(out)
