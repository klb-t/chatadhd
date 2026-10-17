"""Stage ``inventory``: measured, never guessed.  Per archive and in total:
members (known / unknown with an example), units by kind, messages, roles,
block kinds (native → common, counts and characters), tools, thinking,
branches, attachments (referenced / resolved / missing), date range and a
monthly histogram, languages, gaps, duplicates and versions, token estimate,
and ``absent``: what the formats do not contain (``known_absent`` of the
format file plus observations).  Every number carries a ``how``.
"""
from datetime import timedelta
from typing import Any, Optional

from .datafiles import DataStore, store as default_store
from .merge import MergeResult
from .model import Unit
from .sources import Archive
from . import timestamps as ts

SCHEMA = "loom.extract.inventory/1"


def _inc(d: dict[str, int], k: Any, n: int = 1) -> None:
    k = str(k)
    d[k] = d.get(k, 0) + n


def build_inventory(archives: list[Archive], units: list[Unit], merged: Optional[MergeResult], ds: Optional[DataStore] = None,
                    *, aggregate: bool = False) -> dict[str, Any]:
    ds = ds or default_store()
    gap_days = float(ds.preset("timeline.gap_days", 30) or 0)
    thinking_min = int(ds.preset("thinking.full_min_chars", 200))
    reasoning_share = float(ds.preset("thinking.reasoning_share", 0.5))
    tokens_cfg = ds.preset("tokens.chars_per_token", {})
    examples_n = int(ds.preset("report.examples_per_unknown", 1))
    max_list = int(ds.preset("report.max_list", 0))
    formats = ds.entries("formats")

    inv: dict[str, Any] = {"schema": SCHEMA, "aggregate": aggregate, "archives": [], "how": {}}
    # -- archives and members ----------------------------------------------------
    for a in archives:
        known = [m for m in a.members if m.format]
        unknown = [m for m in a.members if not m.format and m.kind not in ("asset", "container")]
        assets = [m for m in a.members if m.kind == "asset"]
        entry: dict[str, Any] = {
            "label": a.label, "path": None if aggregate else a.path, "kind": a.kind, "format": a.format,
            "format_hypothesis": bool(formats.get(a.format, {}).get("hypothesis")) if a.format else False,
            "sha256": a.sha256, "size": a.size,
            "members": {"total": len(a.members), "known": len(known), "unknown": len(unknown), "assets": len(assets),
                        "containers": sum(1 for m in a.members if m.kind == "container"),
                        "by_format": _count([m.format for m in known]),
                        "unknown_list": [] if aggregate else [
                            {"name": m.name, "size": m.size, "why": m.sniff_how} for m in (unknown if max_list <= 0 else unknown[:max_list])]},
            "units": _count([u.kind for u in units if u.source.get("sha256") == a.sha256 and u.source.get("archive") == a.label]),
            "economics": economics_fields([u for u in units if u.source.get("sha256") == a.sha256 and u.source.get("archive") == a.label
                                           and u.kind in ("conversation", "session")], thinking_min, reasoning_share),
        }
        inv["archives"].append(entry)
    inv["how"]["archives"] = "scan: sha256 of every member streamed; format from content (sniff rules named per member)"

    convs = [u for u in units if u.kind in ("conversation", "session")]
    inv["units"] = {"total": len(units), "by_kind": _count([u.kind for u in units]),
                    "by_service": _count([u.account.get("service") for u in units]),
                    "by_format": _count([u.source.get("format") for u in units])}
    inv["how"]["units"] = "one unit per conversation/session/record read; 'unknown' = member or object no reader could place"

    # -- messages, roles, blocks ---------------------------------------------------
    roles: dict[str, int] = {}
    blocks: dict[str, int] = {}
    block_chars: dict[str, int] = {}
    native: dict[str, int] = {}
    tools: dict[str, int] = {}
    models: dict[str, int] = {}
    msgs = 0
    chars = 0
    tokens = 0
    thinking = {"blocks": 0, "chars": 0, "full": 0, "full_min_chars": thinking_min}
    assistant_msgs = 0
    agentic_convs = 0
    msgs_with_tools = 0
    for u in convs:
        st = u.stats or {}
        msgs += st.get("messages", 0)
        chars += st.get("chars", 0)
        tokens += st.get("tokens_est", 0)
        for k, v in (st.get("roles") or {}).items():
            _inc(roles, k, v)
        for k, v in (st.get("blocks") or {}).items():
            _inc(blocks, k, v)
        for k, v in (st.get("block_chars") or {}).items():
            _inc(block_chars, k, v)
        for k, v in (st.get("blocks_native") or {}).items():
            _inc(native, k, v)
        for k, v in (st.get("tools") or {}).items():
            _inc(tools, k, v)
        for k, v in (st.get("models") or {}).items():
            _inc(models, k, v)
        th = st.get("thinking") or {}
        thinking["blocks"] += th.get("blocks", 0)
        thinking["chars"] += th.get("chars", 0)
        thinking["full"] += th.get("full", 0)
        assistant_msgs += (st.get("roles") or {}).get("assistant", 0)
        if st.get("agentic"):
            agentic_convs += 1
        msgs_with_tools += sum(1 for m in u.messages if m.has_kind("tool_use") or m.has_kind("tool_result") or m.role == "tool")
    inv["conversations"] = len(convs)
    inv["messages"] = {"total": msgs, "roles": roles, "chars": chars,
                       "tokens_est": tokens, "tokens_how": f"chars / chars_per_token per main language {tokens_cfg}"}
    inv["blocks"] = {"common": blocks, "chars": block_chars, "native_to_common": native}
    inv["tools"] = {"names": dict(sorted(tools.items(), key=lambda kv: (-kv[1], kv[0]))), "calls": sum(tools.values()),
                    "conversations_agentic": agentic_convs, "messages_with_tools": msgs_with_tools}
    inv["thinking"] = thinking
    inv["models"] = models
    inv["how"]["blocks"] = "native block type → common kind by the format's table; '*' → unknown (example kept)"
    inv["how"]["thinking.full"] = f"thinking blocks with >= {thinking_min} chars and not marked as a summary"
    inv["how"]["tools"] = "tool_use blocks by name; agentic = conversation with >= 1 tool_use/tool_result or role tool"

    # -- branches ------------------------------------------------------------------
    leaves = sum((u.stats or {}).get("branches", 0) for u in convs)
    forks = sum((u.stats or {}).get("forks", 0) for u in convs)
    inv["branches"] = {"leaves": leaves, "forks": forks, "conversations_with_forks": sum(1 for u in convs if (u.stats or {}).get("forks")),
                       "messages_off_current_path": sum(1 for u in convs for m in u.messages if not m.on_current_path)}
    inv["how"]["branches"] = "leaves = root→leaf paths; forks = nodes with >= 2 live children (walk_tree semantics)"

    # -- attachments -----------------------------------------------------------------
    att_status: dict[str, int] = {}
    mimes: dict[str, int] = {}
    missing: list[dict[str, Any]] = []
    att_bytes = 0
    for u in convs:
        for a in u.attachments:
            _inc(att_status, a.status)
            _inc(mimes, a.mime or "?")
            att_bytes += a.size or 0
            if a.status == "missing":
                missing.append({"ref": a.ref, "name": None if aggregate else a.name, "conversation": u.uid,
                                "title": None if aggregate else u.title})
    inv["attachments"] = {"referenced": sum(att_status.values()), **att_status, "mime": mimes, "bytes": att_bytes,
                          "missing_list": ([] if aggregate else (missing if max_list <= 0 else missing[:max_list])),
                          "missing_count": len(missing)}
    inv["how"]["attachments"] = "referenced in messages; resolved = a member of the same archive carries it; missing = no member found"

    # -- dates -----------------------------------------------------------------------
    by_month: dict[str, dict[str, int]] = {}
    hows: dict[str, int] = {}
    inferred = 0
    stamps = []
    for u in convs:
        _inc(hows, u.created_how)
        if u.created_inferred:
            inferred += 1
        if u.created_utc:
            stamps.append(u.created_utc)
            ym = u.created_utc[:7]
            by_month.setdefault(ym, {"conversations": 0, "messages": 0})
            by_month[ym]["conversations"] += 1
        for m in u.messages:
            if m.created_utc:
                ym = m.created_utc[:7]
                by_month.setdefault(ym, {"conversations": 0, "messages": 0})
                by_month[ym]["messages"] += 1
    stamps.sort()
    gaps = []
    if gap_days > 0 and len(stamps) >= 2:
        for a_, b_ in zip(stamps, stamps[1:]):
            da, db = ts.from_iso(a_), ts.from_iso(b_)
            if da and db and db - da > timedelta(days=gap_days):
                gaps.append({"from": a_, "to": b_, "days": round((db - da).total_seconds() / 86400, 1)})
    inv["dates"] = {"from": stamps[0] if stamps else None, "to": stamps[-1] if stamps else None,
                    "by_month": dict(sorted(by_month.items())), "created_how": hows, "inferred": inferred,
                    "gaps": gaps, "gap_days_preset": gap_days}
    inv["how"]["dates"] = "created of each conversation in UTC; how = epoch | iso | iso_nozone (zone assumed) | first_message | mtime"

    # -- languages ------------------------------------------------------------------
    main: dict[str, int] = {}
    weighted: dict[str, float] = {}
    for u in convs:
        st = u.stats or {}
        _inc(main, st.get("lang_main", "und"))
        for k, v in (st.get("lang") or {}).items():
            weighted[k] = weighted.get(k, 0.0) + v * st.get("chars", 0)
    total_w = sum(weighted.values()) or 1.0
    inv["languages"] = {"main_by_conversation": main, "shares": {k: round(v / total_w, 3) for k, v in sorted(weighted.items(), key=lambda kv: -kv[1])}}
    inv["how"]["languages"] = "function-word profiles per text block, character-weighted; 'und' = too short or no hits"

    # -- merge ----------------------------------------------------------------------
    inv["merge"] = merged.as_dict() if merged else {"units": len(units), "duplicates": 0, "versions": 0}

    # -- unknown -------------------------------------------------------------------
    unknown_blocks: dict[str, int] = {}
    unknown_examples: dict[str, Any] = {}
    unknown_entries = 0
    for u in units:
        for e in u.unknown:
            unknown_entries += 1
            key = str(e.get("native") or "?")
            _inc(unknown_blocks, key)
            if not aggregate and key not in unknown_examples and examples_n > 0:
                unknown_examples[key] = {"where": e.get("where"), "unit": u.uid, "example": e.get("example")}
    inv["unknown"] = {"entries": unknown_entries, "by_native": unknown_blocks, "examples": unknown_examples,
                      "units_unknown": sum(1 for u in units if u.kind == "unknown")}
    inv["how"]["unknown"] = "anything a reader saw but could not map: kept with where + example, never dropped"

    # -- absent ---------------------------------------------------------------------
    absent: dict[str, Any] = {}
    for fid in sorted({u.source.get("format") for u in units if u.source.get("format")}):
        f = formats.get(fid) or {}
        obs = []
        f_convs = [u for u in convs if u.source.get("format") == fid]
        f_assist = sum((u.stats or {}).get("roles", {}).get("assistant", 0) for u in f_convs)
        f_think = sum((u.stats or {}).get("thinking", {}).get("blocks", 0) for u in f_convs)
        f_tools = sum((u.stats or {}).get("tool_calls", 0) for u in f_convs)
        f_forks = sum((u.stats or {}).get("forks", 0) for u in f_convs)
        if f_assist and not f_think:
            obs.append({"key": "no_thinking", "n": f_assist})
        if f_convs and not f_tools:
            obs.append({"key": "no_tools", "n": len(f_convs)})
        if f_convs and not f_forks:
            obs.append({"key": "no_forks", "n": len(f_convs)})
        absent[fid] = {"known_absent": f.get("known_absent", {}), "observed": obs, "hypothesis": bool(f.get("hypothesis"))}
    inv["absent"] = absent
    inv["how"]["absent"] = "known_absent from the format file + observations over the read units (counts, not guesses)"
    # -- token economics: what each source can tell (measured, per format and in total) ------------------------
    fmts = sorted({str(u.source.get("format")) for u in convs if u.source.get("format")})
    inv["economics"] = {"by_format": {fid: economics_fields([u for u in convs if u.source.get("format") == fid], thinking_min,
                                                            reasoning_share) for fid in fmts},
                        "total": economics_fields(convs, thinking_min, reasoning_share)}
    inv["how"]["economics"] = ECONOMICS_HOW
    return inv


ECONOMICS_HOW = ("has_usage = at least one message carries measured token counters (usage of the format file); "
                 "has_tool_traces = at least one tool_use/tool_result block or tool-role message; reasoning = the class "
                 "of the thinking blocks by share (each block: redacted; empty = the text is withheld, a signature only; "
                 "summary; full = complete and >= thinking.full_min_chars; short = below the threshold): full when full "
                 "blocks are at least thinking.reasoning_share of all, else the class that has that share, else mixed; "
                 "none when there is no thinking block — counted over the read messages, never inferred from the format "
                 "name; reasoning_blocks has the counts")

REASONING_CLASSES = ("full", "summary", "redacted", "empty", "short")


def reasoning_class(th: dict[str, int], share: float) -> str:
    """The reasoning a source keeps, from the counts of its thinking blocks: ``full`` when full blocks are at least
    ``share`` of all (preset ``thinking.reasoning_share``), else the class holding that share, else ``mixed``."""
    n = th.get("blocks", 0)
    if not n:
        return "none"
    if th.get("full", 0) and th["full"] >= share * n:
        return "full"
    best = max(REASONING_CLASSES, key=lambda k: (th.get(k, 0), -REASONING_CLASSES.index(k)))
    return best if th.get(best, 0) >= share * n else "mixed"


def economics_fields(units: list[Unit], thinking_min: int, reasoning_share: float = 0.5) -> dict[str, Any]:
    """Measured: counters present, tool traces present, how much reasoning the export keeps (counts per class)."""
    usage = assistant = tool_use = tool_result = tool_role = 0
    th = {"blocks": 0, "full": 0, "summary": 0, "redacted": 0, "empty": 0, "short": 0}
    for u in units:
        for m in u.messages:
            if isinstance(m.meta.get("usage"), dict):
                usage += 1
            if m.role == "assistant":
                assistant += 1
            if m.role == "tool":
                tool_role += 1
            for b in m.blocks:
                if b.kind == "tool_use":
                    tool_use += 1
                elif b.kind == "tool_result":
                    tool_result += 1
                elif b.kind == "thinking":
                    th["blocks"] += 1
                    if b.extra.get("redacted"):
                        th["redacted"] += 1
                    elif b.extra.get("withheld") or not (b.text or "").strip():
                        th["empty"] += 1
                    elif b.complete is False:
                        th["summary"] += 1
                    elif b.chars() >= thinking_min:
                        th["full"] += 1
                    else:
                        th["short"] += 1
    return {"has_usage": usage > 0, "usage_messages": usage, "assistant_messages": assistant,
            "has_tool_traces": (tool_use + tool_result + tool_role) > 0,
            "tool_traces": {"tool_use": tool_use, "tool_result": tool_result, "tool_role_messages": tool_role},
            "reasoning": reasoning_class(th, reasoning_share), "reasoning_blocks": th}


def _count(values: list[Any]) -> dict[str, int]:
    out: dict[str, int] = {}
    for v in values:
        _inc(out, v if v is not None else "?")
    return dict(sorted(out.items()))
