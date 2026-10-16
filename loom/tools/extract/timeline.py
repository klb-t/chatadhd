"""Stage ``timeline``: one axis for every archive and service.

Rows carry ``t`` (UTC, from the chosen key preset) next to the native value;
ties are broken by a fixed order ``(t, service, archive, locator, index)`` so
the result is stable between runs and independent of workers.  Buckets
(day/week/month), gaps (longer than a preset, 0 = off), bursts (buckets with
activity above k × median, 0 = off) and cross-service continuation candidates
(a conversation in service B starting shortly after the last message of a
conversation in service A and sharing terms — ``inferred``, never merged)
are reported.  ``shift``, ``anonymise``, ``granularity`` and ``window`` are
options; the original stamps always stay.
"""
import statistics
from datetime import datetime, timedelta, timezone
from typing import Any, Optional

from .datafiles import DataStore, store as default_store
from .lexical import Lexicon, lexicon as default_lexicon
from .model import Unit
from . import timestamps as ts

SCHEMA = "loom.extract.timeline/1"
_EPOCH = datetime(1970, 1, 1, tzinfo=timezone.utc)


def _get(u: Unit, field: str) -> Optional[str]:
    if field.startswith("stats."):
        return (u.stats or {}).get(field[6:])
    return getattr(u, field, None)


def build_timeline(units: list[Unit], ds: Optional[DataStore] = None, *, key: Optional[str] = None,
                   zone: Optional[str] = None, bucket: Optional[str] = None, gap_days: Optional[float] = None,
                   burst_ratio: Optional[float] = None, window: tuple[Optional[datetime], Optional[datetime]] = (None, None),
                   shift_seconds: float = 0.0, anonymise: bool = False, granularity: Optional[str] = None,
                   continuations: Optional[bool] = None, lex: Optional[Lexicon] = None) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    ds = ds or default_store()
    presets = ds.file("timeline/presets.json")
    key = key or ds.preset("timeline.key", "created")
    if key not in presets["keys"]:
        raise ValueError(f"unknown timeline key: {key!r} (presets: {', '.join(presets['keys'])})")
    zone = zone or ds.preset("timeline.zone", "source")
    bucket = bucket or ds.preset("timeline.bucket", "week")
    gap_days = ds.preset("timeline.gap_days", 30) if gap_days is None else gap_days
    burst_ratio = ds.preset("timeline.burst_ratio", 3.0) if burst_ratio is None else burst_ratio
    cont_cfg = dict(ds.preset("timeline.continuation", {}) or {})
    if continuations is not None:
        cont_cfg["enabled"] = continuations
    spec = presets["keys"][key]
    ties = list(presets.get("ties", ["t", "service", "archive", "locator", "index"]))

    rows: list[dict[str, Any]] = []
    convs = [u for u in units if u.kind in ("conversation", "session")]
    for ui, u in enumerate(convs):
        src = u.source or {}
        base = {"service": u.account.get("service") or "", "account": u.account.get("id"), "archive": src.get("archive"),
                "archive_sha256": src.get("sha256"), "locator": src.get("locator"), "conversation": u.uid,
                "format": src.get("format")}
        if spec["level"] == "conversation":
            t = _get(u, spec["field"])
            rows.append({**base, "t": t, "t_src": u.created if key in ("created",) else (u.updated if key == "updated" else t),
                         "how": u.created_how, "inferred": bool(u.created_inferred), "kind": u.kind, "uid": u.uid,
                         "title": u.title, "messages": len(u.messages), "chars": (u.stats or {}).get("chars", 0),
                         "index": ui, "first_message": (u.stats or {}).get("first_message_utc"),
                         "last_message": (u.stats or {}).get("last_message_utc")})
        else:
            for mi, m in enumerate(u.messages):
                rows.append({**base, "t": m.created_utc, "t_src": m.created, "how": m.created_how, "inferred": bool(m.created_inferred),
                             "kind": "message", "uid": f"{u.uid}#{m.id}", "message": m.id, "role": m.role, "title": u.title,
                             "chars": m.chars(), "index": ui * 1_000_000 + mi, "branch": m.branch, "on_current_path": m.on_current_path})

    def sort_key(r: dict[str, Any]) -> tuple:
        parts = []
        for tname in ties:
            if tname == "t":
                parts.append((r["t"] is None, r["t"] or ""))
            elif tname == "locator":
                parts.append(ts_json(r.get("locator")))
            else:
                parts.append(str(r.get(tname) or "") if tname != "index" else r.get("index", 0))
        return tuple(parts)

    rows.sort(key=sort_key)

    # window
    w_from, w_to = window
    if w_from or w_to:
        rows = [r for r in rows if r["t"] and (not w_from or ts.from_iso(r["t"]) >= w_from) and (not w_to or ts.from_iso(r["t"]) < w_to)]

    # transforms (original kept in t_src / t_utc)
    first_t = next((ts.from_iso(r["t"]) for r in rows if r["t"]), None)
    for r in rows:
        r["t_utc"] = r["t"]
        dt = ts.from_iso(r["t"])
        if dt is None:
            continue
        if anonymise and first_t is not None:
            dt = _EPOCH + (dt - first_t)
            r["t_rel_seconds"] = (ts.from_iso(r["t_utc"]) - first_t).total_seconds()
        if shift_seconds:
            dt = ts.shift(dt, shift_seconds)
        if granularity:
            dt = ts.round_to(dt, granularity)
        r["t"] = ts.to_iso(dt)
        if zone and zone != "source":
            r["t_local"] = ts.in_zone(dt, zone)

    stamped = [r for r in rows if r["t"]]
    out: dict[str, Any] = {"schema": SCHEMA, "key": key, "level": spec["level"], "zone": zone, "ties": ties,
                           "rows": len(rows), "unstamped": len(rows) - len(stamped),
                           "range": {"from": stamped[0]["t"] if stamped else None, "to": stamped[-1]["t"] if stamped else None},
                           "transform": {"shift_seconds": shift_seconds, "anonymise": anonymise, "granularity": granularity,
                                         "window": [ts.to_iso(w_from), ts.to_iso(w_to)]},
                           "by_service": _count(r["service"] for r in rows),
                           "inferred": sum(1 for r in rows if r.get("inferred"))}

    # buckets
    bks: dict[str, dict[str, Any]] = {}
    for r in stamped:
        dt = ts.from_iso(r["t"])
        start = ts.to_iso(ts.bucket_start(dt, bucket))
        b = bks.setdefault(start, {"start": start, "conversations": 0, "messages": 0, "by_service": {}})
        if r["kind"] == "message":
            b["messages"] += 1
        else:
            b["conversations"] += 1
            b["messages"] += r.get("messages", 0)
        b["by_service"][r["service"]] = b["by_service"].get(r["service"], 0) + 1
    out["buckets"] = {"unit": bucket, "rows": [bks[k] for k in sorted(bks)]}

    # gaps (between consecutive stamped rows)
    gaps = []
    if gap_days and gap_days > 0:
        prev = None
        for r in stamped:
            dt = ts.from_iso(r["t"])
            if prev is not None and dt - prev[0] > timedelta(days=gap_days):
                gaps.append({"from": prev[1], "to": r["t"], "days": round((dt - prev[0]).total_seconds() / 86400, 1),
                             "after": prev[2], "before": r["uid"]})
            prev = (dt, r["t"], r["uid"])
    out["gaps"] = gaps
    out["gap_days"] = gap_days

    # bursts
    bursts = []
    if burst_ratio and burst_ratio > 0 and bks:
        counts = [b["messages"] for b in bks.values() if b["messages"] > 0]
        med = statistics.median(counts) if counts else 0
        if med > 0:
            for k in sorted(bks):
                if bks[k]["messages"] > burst_ratio * med:
                    bursts.append({"bucket": k, "messages": bks[k]["messages"], "ratio_to_median": round(bks[k]["messages"] / med, 2)})
    out["bursts"] = bursts
    out["burst_ratio"] = burst_ratio

    # continuations across services
    out["continuations"] = find_continuations(convs, cont_cfg, lex or default_lexicon()) if cont_cfg.get("enabled", True) else []
    out["continuation_preset"] = cont_cfg
    return out, rows


def find_continuations(convs: list[Unit], cfg: dict[str, Any], lex: Lexicon) -> list[dict[str, Any]]:
    window_min = float(cfg.get("window_minutes", 240) or 0)
    min_shared = int(cfg.get("min_shared_terms", 2))
    min_conf = float(cfg.get("min_confidence", 0.3))
    if window_min <= 0:
        return []
    items = []
    for u in convs:
        first = ts.from_iso((u.stats or {}).get("first_message_utc") or u.created_utc)
        last = ts.from_iso((u.stats or {}).get("last_message_utc") or u.updated_utc or u.created_utc)
        if first is None or last is None:
            continue
        terms = set(lex.term_keys(_lead_text(u), min_len=4))
        items.append((u, first, last, terms))
    items.sort(key=lambda x: (x[1], x[0].uid))
    out = []
    for i, (a, a_first, a_last, a_terms) in enumerate(items):
        for b, b_first, b_last, b_terms in items[i + 1:]:
            if b_first < a_last:
                continue
            gap = (b_first - a_last).total_seconds() / 60.0
            if gap > window_min:
                break
            if (a.account.get("service") or "") == (b.account.get("service") or ""):
                continue
            shared = sorted(a_terms & b_terms)
            if len(shared) < min_shared:
                continue
            conf = round(min(1.0, 0.3 + 0.1 * len(shared) - 0.3 * (gap / window_min)), 3)
            if conf < min_conf:
                continue
            a.flags.setdefault("continuations", []).append(b.uid)
            b.flags["continuation_of"] = a.uid
            out.append({"from": a.uid, "to": b.uid, "from_service": a.account.get("service"), "to_service": b.account.get("service"),
                        "gap_minutes": round(gap, 1), "shared_terms": shared[:10], "confidence": conf, "inferred": True})
    return out


def _lead_text(u: Unit) -> str:
    parts = [u.title or ""]
    n = 0
    for m in u.messages:
        if m.role == "user" and m.text():
            parts.append(m.text()[:600])
            n += 1
            if n >= 2:
                break
    return "\n".join(parts)


def ts_json(obj: Any) -> str:
    import json
    return json.dumps(obj, sort_keys=True, ensure_ascii=False) if obj is not None else ""


def _count(values: Any) -> dict[str, int]:
    out: dict[str, int] = {}
    for v in values:
        out[str(v)] = out.get(str(v), 0) + 1
    return dict(sorted(out.items()))


def week_of_term(rows: list[dict[str, Any]], units_by_uid: dict[str, Unit], term: str, lex: Lexicon,
                 bucket: str = "week") -> list[str]:
    """Buckets in which ``term`` had the highest lexical count (all of them on a tie)."""
    from .lexical import TextIndex, compile_keyword, match_keyword
    kw = compile_keyword(term, lex)
    counts: dict[str, int] = {}
    for r in rows:
        if not r.get("t"):
            continue
        u = units_by_uid.get(r["conversation"])
        if u is None:
            continue
        text = u.text() if r["kind"] != "message" else next((m.all_text() for m in u.messages if m.id == r.get("message")), "")
        n = len(match_keyword(kw, TextIndex.build(text, lex)))
        if n:
            b = ts.to_iso(ts.bucket_start(ts.from_iso(r["t"]), bucket))
            counts[b] = counts.get(b, 0) + n
    if not counts:
        return []
    best = max(counts.values())
    return sorted(k for k, v in counts.items() if v == best)
