"""Token economics (P38 requirement 37): measured and estimated tokens, cost, outcomes.

Inputs, all local (zero network): the units already read (Claude Code transcripts carry per-turn counters in
``message.usage``; chat exports carry none and are estimated per message, labelled with the method), optionally a
saved session list (``--sesje``: the shape ``list_sessions`` returns, mapped by ``data/session_sources/*.json``;
the tool never calls an API) and git repositories (``--repo``: ``git log --all`` read-only, commits per session from
the ``Claude-Session`` trailer, lines changed).  Prices come from the one price book (``prices.py``); a number that
cannot be known is said to be unknown with its reason, never 0.

Report: per session (API counters + transcripts + commits), per run (main transcript, each subagent, each workflow
agent), per workflow run, per model, per tool, per day, per source (provider comparison), totals; Markdown (pl/en from
the i18n table), CSV per table and JSON — layout in ``data/economics/report.json``.  ``--zbiorczo`` replaces every
title, id, path and commit hash by a numbered label (counts only).
"""
import csv
import io
import json
import logging
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Optional

from .datafiles import DataStore
from .i18n import label, language, t
from .inventory import economics_fields
from .model import Unit
from .prices import PriceBook, cost_of
from .readers.base import getpath
from .tokencount import Estimator, calibration
from . import timestamps as ts

log = logging.getLogger(__name__)

SCHEMA = "loom.extract.economics/1"
MEASURED = ("input", "output", "cache_creation", "cache_read")
EXTRA_COUNTERS = ("cache_creation_5m", "cache_creation_1h")
ESTIMATED = ("est_input", "est_output", "est_context")
DUPLICATE_MODES = ("max", "last", "sum")


# ───────────────────────────── accumulator ─────────────────────────────
class Acc:
    """Sums of turns: measured counters, estimated counters, cost (known part + unknown turns with reasons)."""

    def __init__(self) -> None:
        self.n = {k: 0.0 for k in MEASURED + EXTRA_COUNTERS + ESTIMATED}
        self.turns = 0.0
        self.measured = 0.0
        self.estimated = 0.0
        self.cost_known = 0.0
        self.cost_unknown = 0.0
        self.cost_reasons: dict[str, int] = {}
        self.currency: Optional[str] = None
        self.methods: dict[str, int] = {}
        self.models: dict[str, int] = {}
        self.first: Optional[str] = None
        self.last: Optional[str] = None

    def add(self, turn: dict[str, Any], w: float = 1.0) -> None:
        self.turns += w
        if turn["measured"]:
            self.measured += w
        else:
            self.estimated += w
        for k, v in turn["usage"].items():
            if k in self.n:
                self.n[k] += v * w
        c = turn["cost"]
        if c.get("known"):
            self.cost_known += float(c["amount"]) * w
            self.currency = self.currency or c.get("currency")
        else:
            self.cost_unknown += w
            r = c.get("reason") or "no_price"
            self.cost_reasons[r] = self.cost_reasons.get(r, 0) + 1
        self.methods[turn["method"]] = self.methods.get(turn["method"], 0) + 1
        mk = turn.get("model") or "?"
        self.models[mk] = self.models.get(mk, 0) + 1
        tt = turn.get("t")
        if tt:
            if self.first is None or ts.key(tt) < ts.key(self.first):
                self.first = tt
            if self.last is None or ts.key(tt) > ts.key(self.last):
                self.last = tt

    def total(self) -> int:
        return int(round(sum(self.n[k] for k in MEASURED)))

    def cost(self) -> dict[str, Any]:
        cur = self.currency or "USD"
        if not self.turns:
            return {"amount": None, "known": False, "reason": "no_turns", "currency": cur}
        if self.cost_unknown == 0:
            return {"amount": round(self.cost_known, 6), "known": True, "currency": cur}
        return {"amount": None, "known": False, "reason": max(self.cost_reasons, key=lambda k: (self.cost_reasons[k], k)),
                "partial": round(self.cost_known, 6) if self.cost_known else None, "unknown_turns": int(round(self.cost_unknown)),
                "reasons": dict(sorted(self.cost_reasons.items())), "currency": cur}

    def row(self, grand_total: Optional[int] = None) -> dict[str, Any]:
        tot = self.total()
        inp_side = self.n["input"] + self.n["cache_creation"] + self.n["cache_read"]
        out: dict[str, Any] = {k: int(round(self.n[k])) for k in MEASURED + EXTRA_COUNTERS + ESTIMATED}
        out.update({"turns": int(round(self.turns)), "turns_measured": int(round(self.measured)), "turns_estimated": int(round(self.estimated)),
                    "total": tot if self.measured else None,
                    "est_total": int(round(self.n["est_context"] or self.n["est_input"])) + int(round(self.n["est_output"])) if self.estimated else None,
                    "cache_share": round(self.n["cache_read"] / tot, 4) if tot else None,
                    "cache_hit": round(self.n["cache_read"] / inp_side, 4) if inp_side else None,
                    "cost": self.cost(),
                    "methods": ", ".join(f"{k} ({v})" for k, v in sorted(self.methods.items())),
                    "models": ", ".join(f"{k} ({v})" for k, v in sorted(self.models.items(), key=lambda kv: (-kv[1], kv[0]))),
                    "first": self.first, "last": self.last})
        why: dict[str, str] = {}
        if not self.measured:
            for k in MEASURED + EXTRA_COUNTERS:
                out[k] = None
            for k in MEASURED + EXTRA_COUNTERS + ("total", "cache_share", "cache_hit", "share"):
                why[k] = "no_counters" if self.turns else "no_turns"
        if not self.estimated:
            for k in ESTIMATED:
                out[k] = None
            for k in ESTIMATED + ("est_total",):
                why[k] = "measured_instead" if self.measured else "no_turns"
        if grand_total is not None:
            out["share"] = round(tot / grand_total, 4) if grand_total and self.measured else None
        out["why"] = why
        return out


# ───────────────────────────── inputs: session list, git ─────────────────────────────
def load_session_lists(paths: list[str], ds: DataStore, source_id: Optional[str] = None) -> tuple[dict[str, dict[str, Any]], list[dict[str, Any]]]:
    """Saved session lists (JSON array / object with a wrapper / JSONL) → id → session row; problems are listed."""
    specs = ds.entries("session_sources")
    spec = specs.get(source_id or "claude_code.list_sessions") or next(iter(specs.values()), None)
    problems: list[dict[str, Any]] = []
    out: dict[str, dict[str, Any]] = {}
    if spec is None:
        problems.append({"key": "no_session_source", "n": len(paths)})
        return out, problems
    fields = spec.get("fields") or {}
    counters = list(spec.get("counters") or [])
    for i, p in enumerate(paths):
        path = Path(p).expanduser()
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as exc:
            problems.append({"key": "sessions_unreadable", "n": 1, "file": i + 1, "detail": type(exc).__name__})
            continue
        items: list[Any] = []
        try:
            doc = json.loads(text)
            if isinstance(doc, list):
                items = doc
            elif isinstance(doc, dict):
                wrapped = next((doc[w] for w in spec.get("wrappers") or [] if isinstance(doc.get(w), list)), None)
                items = wrapped if wrapped is not None else [doc]
        except json.JSONDecodeError:
            for ln, line in enumerate(text.splitlines(), 1):
                if not line.strip():
                    continue
                try:
                    items.append(json.loads(line))
                except json.JSONDecodeError:
                    problems.append({"key": "sessions_bad_line", "n": 1, "file": i + 1, "line": ln})
        for obj in items:
            if not isinstance(obj, dict) or not getpath(obj, fields.get("id", "id")):
                problems.append({"key": "sessions_no_id", "n": 1, "file": i + 1})
                continue
            row: dict[str, Any] = {k: getpath(obj, path) for k, path in fields.items()}
            row["id"] = str(row["id"])
            for k in counters + ["cost_usd"]:
                v = row.get(k)
                row[k] = (int(v) if k != "cost_usd" else float(v)) if isinstance(v, (int, float)) and not isinstance(v, bool) else None
            row["missing"] = sorted(k for k in counters + ["cost_usd"] if row.get(k) is None)
            row["currency"] = spec.get("currency", "USD")
            prev = out.get(row["id"])
            if prev is None or str(row.get("updated") or "") >= str(prev.get("updated") or ""):
                out[row["id"]] = row
            if prev is not None:
                problems.append({"key": "sessions_duplicate", "n": 1})
    return out, problems


def git_commits(repo: str, cfg: dict[str, Any]) -> dict[str, Any]:
    """Commits of every ref (read-only ``git log --all``): hash, author day, ``Claude-Session`` trailer, lines;
    and the set reachable from the main ref (None + reason when it does not exist)."""
    rx = trailer_regex(cfg, line=True)
    base = ["git", "-C", str(Path(repo).expanduser()), "-c", "core.fsmonitor=false", "-c", "core.pager=cat", "--no-pager"]
    env = {"GIT_TERMINAL_PROMPT": "0", "GIT_PAGER": "cat", "LC_ALL": "C", "PATH": "/usr/bin:/bin:/usr/local/bin",
           "HOME": str(Path.home()), "GIT_CONFIG_NOSYSTEM": "1"}
    fmt = "%x1e%H%x1f%aI%x1f%B%x1f"
    cmd = base + ["log", "--all", "--no-color", "--no-renames", f"--format={fmt}"] + (["--numstat"] if cfg.get("numstat", True) else [])
    out: dict[str, Any] = {"repo": repo, "commits": [], "main": None, "main_reason": None, "error": None}
    try:
        res = subprocess.run(cmd, capture_output=True, env=env, check=False)
    except OSError as exc:
        out["error"] = f"git_unavailable:{type(exc).__name__}"
        return out
    if res.returncode != 0:
        out["error"] = "git_log_failed:" + res.stderr.decode("utf-8", "replace").strip().splitlines()[0][:200] if res.stderr else "git_log_failed"
        return out
    for rec in res.stdout.decode("utf-8", "replace").split("\x1e"):
        if not rec.strip():
            continue
        parts = rec.split("\x1f")
        if len(parts) < 3:
            continue
        sha, date, body = parts[0].strip(), parts[1].strip(), parts[2]
        added = removed = files = 0
        binary = 0
        for line in (parts[3] if len(parts) > 3 else "").splitlines():
            cols = line.split("\t")
            if len(cols) != 3:
                continue
            files += 1
            if cols[0] == "-" or cols[1] == "-":
                binary += 1
                continue
            added += int(cols[0]) if cols[0].isdigit() else 0
            removed += int(cols[1]) if cols[1].isdigit() else 0
        sessions = sorted(set(rx.findall(body)))
        out["commits"].append({"hash": sha, "date": date, "sessions": sessions, "added": added, "removed": removed,
                               "files": files, "binary_files": binary})
    ref = str(cfg.get("main_ref") or "")
    if ref:
        r2 = subprocess.run(base + ["rev-list", ref, "--"], capture_output=True, env=env, check=False)
        if r2.returncode == 0:
            out["main"] = set(r2.stdout.decode("ascii", "replace").split())
        else:
            out["main_reason"] = "no_main_ref"
    else:
        out["main_reason"] = "main_ref_off"
    out["commits"].sort(key=lambda c: c["hash"])
    return out


def trailer_regex(cfg: dict[str, Any], *, line: bool = False) -> "re.Pattern[str]":
    """``Claude-Session: …/<id>`` → <id> (the last path segment; the id's shape is data: ``economics.git.session_id``).
    ``line``: the trailer is a whole line of a commit message; otherwise it may sit inside a quoted shell command."""
    trailer = re.escape(str(cfg.get("trailer") or "Claude-Session"))
    sid = str(cfg.get("session_id") or r"[A-Za-z0-9_][A-Za-z0-9_.-]*")
    if line:
        return re.compile(rf"^{trailer}:[ \t]*(?:\S*/)?({sid})/?[ \t]*$", re.M | re.I)
    return re.compile(rf"{trailer}:[ \t]*(?:[^\s\"']*/)?({sid})", re.I)


# ───────────────────────────── helpers ─────────────────────────────
def _day(iso: Optional[str], zone: str) -> Optional[str]:
    if not iso:
        return None
    if zone in ("", "UTC", "utc", "Z"):
        return iso[:10]
    dt = ts.from_iso(iso)
    if dt is None:
        return iso[:10]
    try:
        from zoneinfo import ZoneInfo
        return dt.astimezone(ZoneInfo(zone)).date().isoformat()
    except Exception:  # noqa: BLE001 - an unknown zone falls back to UTC, said in the settings echo
        log.debug("day zone %r unusable", zone, exc_info=True)
        return iso[:10]


def _block_text(b: Any) -> str:
    if b.text:
        return b.text
    if b.kind == "tool_use" and b.input is not None:
        return json.dumps(b.input, ensure_ascii=False, sort_keys=True)
    return ""


def _tool_input_text(b: Any) -> str:
    if isinstance(b.input, dict):
        return "\n".join(str(v) for v in b.input.values() if isinstance(v, str))
    return b.text or ""


def _unit_key(u: Unit) -> tuple[str, str, str, str]:
    return (str(u.source.get("archive") or ""), str(u.source.get("member") or ""), str(u.ext_id or ""), u.uid)


def _place(u: Unit) -> dict[str, Any]:
    if u.kind != "session":
        return {"role": "conversation", "family": None, "run": None}
    role = u.flags.get("transcript_role") or ("main" if not u.flags.get("parent_session") else "subagent")
    return {"role": role, "family": str(u.flags.get("parent_session") or u.ext_id or u.uid), "run": u.flags.get("workflow_run")}


def _provider(u: Unit) -> str:
    acc = u.account or {}
    return "/".join(str(x) for x in (acc.get("service"), acc.get("product")) if x) or str(u.source.get("format") or "?")


def _div_cost(c: dict[str, Any], n: Optional[int], why_zero: str) -> dict[str, Any]:
    cur = c.get("currency", "USD")
    if n is None:
        return {"amount": None, "known": False, "reason": "commits_unknown", "currency": cur}
    if n == 0:
        return {"amount": None, "known": False, "reason": why_zero, "currency": cur}
    if not c.get("known"):
        return {"amount": None, "known": False, "reason": c.get("reason") or "no_price", "currency": cur}
    return {"amount": round(float(c["amount"]) / n, 6), "known": True, "currency": cur, "basis": c.get("basis")}


# ───────────────────────────── build ─────────────────────────────
def build_economics(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    ds: DataStore = eng.ds
    cfg = ds.preset("economics") or {}
    aggregate = bool(eng.aggregate)
    dup_mode = str(p.get("duplicate_lines") or (cfg.get("usage") or {}).get("duplicate_lines") or "max")
    if dup_mode not in DUPLICATE_MODES:
        raise ValueError(f"economics.usage.duplicate_lines: {dup_mode!r} (one of {', '.join(DUPLICATE_MODES)})")
    ignore_models = set((cfg.get("usage") or {}).get("ignore_models") or [])
    attribution = str(p.get("tool_attribution") or cfg.get("tool_attribution") or "split")
    zone = str(p.get("day_zone") or cfg.get("day_zone") or "UTC")
    est_cfg = cfg.get("estimate") or {}
    context_mode = str(p.get("context") or est_cfg.get("context") or "path")
    oc = cfg.get("outcomes") or {}
    edit_tools = set(oc.get("edit_tools") or [])
    path_keys = list(oc.get("path_keys") or [])
    commit_rx = re.compile(str(oc.get("commit_command") or r"git\s+commit\b"))
    git_cfg = dict(cfg.get("git") or {})
    if p.get("main_ref") is not None:
        git_cfg["main_ref"] = p["main_ref"]
    trailer_rx = trailer_regex(git_cfg)
    estimator = Estimator(ds, p.get("estimate"))
    book = PriceBook(ds)

    units = [u for u in eng.conversations]
    if (p.get("of") or "all") == "selection":
        sel = eng.selection.units
        units = [u for u in units if u.uid in sel]
    units.sort(key=_unit_key)
    notes: dict[str, Any] = {}

    def note(key: str, n: int = 1, **detail: Any) -> None:
        e = notes.setdefault(key, {"key": key, "n": 0})
        e["n"] += n
        for k, v in detail.items():
            if isinstance(v, dict):
                d = e.setdefault(k, {})
                for kk, vv in v.items():
                    d[kk] = d.get(kk, 0) + vv
            else:
                e[k] = v

    # tool_use id → error (the bound tool_result), over every unit (a result may sit in another file)
    errors: dict[str, bool] = {}
    for u in units:
        for m in u.messages:
            for b in m.blocks:
                if b.kind == "tool_result":
                    tid = b.extra.get("tool_use_id") or b.name
                    if tid:
                        errors[str(tid)] = bool(isinstance(b.input, dict) and b.input.get("is_error"))

    price_cache: dict[tuple[Optional[str], Optional[str]], dict[str, Any]] = {}

    def price(model: Optional[str], day: Optional[str]) -> dict[str, Any]:
        k = (model, day)
        if k not in price_cache:
            price_cache[k] = book.lookup(model, day)
        return price_cache[k]

    # -- measured turns (one per API response) -------------------------------------------------------------------
    turns: list[dict[str, Any]] = []
    groups: dict[str, dict[str, Any]] = {}
    calib_pairs: list[tuple[int, int]] = []
    measured_units: set[str] = set()
    for u in units:
        if not any(isinstance(m.meta.get("usage"), dict) for m in u.messages):
            continue
        measured_units.add(u.uid)
        pl = _place(u)
        lang = (u.stats or {}).get("lang_main")
        for m in u.messages:
            us = m.meta.get("usage")
            if not isinstance(us, dict):
                if m.role == "assistant" and m.model not in ignore_models:
                    note("assistant_without_counters")
                continue
            if m.model in ignore_models:
                note("ignored_model_lines", models={str(m.model): 1})
                continue
            api = m.meta.get("api_id") or m.meta.get("request_id")
            key = f"api:{api}" if api and dup_mode != "sum" else f"{u.uid}#{m.id}"
            g = groups.get(key)
            counters = {k: int(us.get(k) or 0) for k in MEASURED + EXTRA_COUNTERS}
            if g is None:
                g = {"key": key, "unit": u, "place": pl, "model": m.model, "t": m.created_utc, "measured": True, "lines": 1,
                     "usage": counters, "tools": {}, "tool_ids": [], "files": set(), "edits": 0, "edits_failed": 0,
                     "commit_cmds": 0, "trailers": {}, "line_cost": m.meta.get("cost_usd"), "texts": [], "thinking": False,
                     "lang": lang, "seen_tools": set()}
                groups[key] = g
                turns.append(g)
            else:
                g["lines"] += 1
                note("duplicate_lines")
                if g["unit"] is not u:
                    note("duplicate_across_files")
                if dup_mode == "max":
                    for k in counters:
                        g["usage"][k] = max(g["usage"][k], counters[k])
                else:  # last
                    g["usage"] = counters
                if m.created_utc and (g["t"] is None or ts.key(m.created_utc) < ts.key(g["t"])):
                    g["t"] = m.created_utc
                if m.meta.get("cost_usd") is not None:
                    g["line_cost"] = max(float(g["line_cost"] or 0.0), float(m.meta["cost_usd"]))
            for k in (us.get("extra") or {}):
                note("usage_fields_unmapped", fields={str(k): 1})
            for b in m.blocks:
                if b.kind == "thinking":
                    g["thinking"] = True
                if b.kind in ("text", "tool_use"):
                    g["texts"].append(_block_text(b))
                if b.kind != "tool_use":
                    continue
                tid = b.extra.get("id")
                if tid and tid in g["seen_tools"]:  # the same call written again on a later line of the response
                    continue
                if tid:
                    g["seen_tools"].add(tid)
                name = str(b.name or "?")
                g["tools"][name] = g["tools"].get(name, 0) + 1
                failed = bool(tid and errors.get(str(tid)))
                g["tool_ids"].append((name, failed))
                if name in edit_tools and isinstance(b.input, dict):
                    g["edits"] += 1
                    if failed:
                        g["edits_failed"] += 1
                    for pk in path_keys:
                        if isinstance(b.input.get(pk), str) and b.input[pk]:
                            g["files"].add(b.input[pk])
                            break
                text_in = _tool_input_text(b)
                if text_in and commit_rx.search(text_in):
                    g["commit_cmds"] += 1
                    for sid in trailer_rx.findall(text_in):
                        g["trailers"][sid] = g["trailers"].get(sid, 0) + 1
    for g in turns:
        g["day"] = _day(g["t"], zone)
        hit = price(g["model"], g["day"])
        if g.get("line_cost") is not None:
            g["cost"] = {"amount": round(float(g["line_cost"]), 6), "known": True, "currency": "USD", "basis": "measured_line_cost"}
        else:
            c = cost_of(g["usage"], hit)
            c["basis"] = "prices×measured"
            g["cost"] = c
        g["method"] = "measured"
        if not g["thinking"] and g["usage"]["output"] > 0 and int(est_cfg.get("calibration_min_output", 1) or 0) <= g["usage"]["output"]:
            est, _how = estimator.count("\n".join(g["texts"]), g["model"], g["lang"])
            calib_pairs.append((est, g["usage"]["output"]))

    # -- estimated turns (sources without counters) ------------------------------------------------------------------
    for u in units:
        if u.uid in measured_units:
            continue
        lang = (u.stats or {}).get("lang_main")
        models = [m.model for m in u.messages if m.role == "assistant" and m.model]
        unit_model = max(set(models), key=lambda x: (models.count(x), x)) if models else (u.flags.get("model") or None)
        fn, how = estimator.counter(unit_model, lang)
        tok: dict[str, int] = {}
        for m in u.messages:
            tok[m.id] = fn("\n".join(_block_text(b) for b in m.blocks))
            estimator.used[how] = estimator.used.get(how, 0) + 1
        by = u.by_id()
        cum: dict[str, int] = {}
        pend: dict[str, int] = {}
        for m in u.messages:  # iterative: an agent session can be thousands of messages deep
            if m.id in cum:
                continue
            chain = []
            cur: Optional[Any] = m
            while cur is not None and cur.id not in cum:
                chain.append(cur)
                cur = by.get(cur.parent) if cur.parent in by and cur.parent != cur.id else None
            for x in reversed(chain):
                par = by.get(x.parent) if x.parent in by and x.parent != x.id else None
                if par is None:
                    cum[x.id], pend[x.id] = 0, 0
                else:
                    cum[x.id] = cum[par.id] + tok[par.id]
                    pend[x.id] = 0 if par.role == "assistant" else pend[par.id] + tok[par.id]
        pl = _place(u)
        for m in u.messages:
            if m.role != "assistant":
                continue
            usage = {k: 0 for k in MEASURED + EXTRA_COUNTERS}
            usage.update({"est_input": pend.get(m.id, 0), "est_output": tok.get(m.id, 0),
                          "est_context": cum.get(m.id, 0) if context_mode == "path" else 0})
            model = m.model or unit_model
            g = {"key": f"{u.uid}#{m.id}", "unit": u, "place": pl, "model": model, "t": m.created_utc, "measured": False, "lines": 1,
                 "usage": usage, "tools": {}, "tool_ids": [], "files": set(), "edits": 0, "edits_failed": 0, "commit_cmds": 0,
                 "trailers": {}, "method": how, "day": _day(m.created_utc, zone)}
            for b in m.blocks:
                if b.kind == "tool_use":
                    name = str(b.name or "?")
                    g["tools"][name] = g["tools"].get(name, 0) + 1
                    g["tool_ids"].append((name, bool(b.extra.get("id") and errors.get(str(b.extra["id"])))))
            billed_in = usage["est_context"] if context_mode == "path" else usage["est_input"]
            c = cost_of({"input": billed_in, "output": usage["est_output"]}, price(model, g["day"]))
            c["basis"] = "prices×estimated"
            g["cost"] = c
            turns.append(g)
            if not model:
                note("estimated_without_model")

    # -- session lists, links, git -------------------------------------------------------------------------------
    api_sessions, problems = load_session_lists([str(x) for x in (p.get("sessions") or [])], ds, p.get("session_source"))
    for pr in problems:
        note(pr["key"], pr.get("n", 1))
    commits: dict[str, dict[str, Any]] = {}
    main_sets: list[Optional[set[str]]] = []
    main_reasons: list[str] = []
    for i, repo in enumerate(p.get("repos") or []):
        res = git_commits(str(repo), git_cfg)
        if res["error"]:
            note("git_error", repos={f"repo-{i + 1}": 1}, error=res["error"].split(":")[0] if aggregate else res["error"])
            continue
        main_sets.append(res["main"])
        if res["main_reason"]:
            main_reasons.append(res["main_reason"])
        for c in res["commits"]:
            prev = commits.get(c["hash"])
            if prev is None:
                commits[c["hash"]] = {**c, "on_main": bool(res["main"] is not None and c["hash"] in res["main"])}
            elif res["main"] is not None and c["hash"] in res["main"]:
                prev["on_main"] = True
    main_known = bool(main_sets) and all(s is not None for s in main_sets)
    if main_reasons:
        note("main_ref_unknown", reason=main_reasons[0])

    # families (a main session + its subagents and workflow agents) and their links to API sessions
    fam_units: dict[str, list[Unit]] = {}
    for u in units:
        pl = _place(u)
        if pl["family"]:
            fam_units.setdefault(pl["family"], []).append(u)
    explicit = {str(k): str(v) for k, v in (p.get("links") or {}).items()}
    use_trailers = bool((cfg.get("links") or {}).get("trailer_in_transcripts", True))
    fam_trailers: dict[str, dict[str, int]] = {}
    for g in turns:
        fam = g["place"]["family"]
        if fam:
            d = fam_trailers.setdefault(fam, {})
            for sid, n in g["trailers"].items():
                d[sid] = d.get(sid, 0) + n
    links: dict[str, tuple[str, str]] = {}
    for fam in sorted(fam_units):
        exts = {fam} | {str(u.ext_id) for u in fam_units[fam] if u.ext_id}
        hit = next((explicit[x] for x in sorted(exts) if x in explicit), None)
        if hit:
            links[fam] = (hit, "explicit")
        elif fam in api_sessions:
            links[fam] = (fam, "same_id")
        elif use_trailers and fam_trailers.get(fam):
            tr = fam_trailers[fam]
            best = max(tr, key=lambda k: (tr[k], k))
            links[fam] = (best, "trailer" if len(tr) == 1 else "trailer_ambiguous")
            if len(tr) > 1:
                note("link_ambiguous")
    linked_api = {v[0] for v in links.values()}

    # -- groups ------------------------------------------------------------------------------------------------------
    total = Acc()
    for g in turns:
        total.add(g)
    grand = total.total()

    def label_for(kind: str, keys: Iterable[str]) -> dict[str, str]:
        return {k: f"{t('economics.label.' + kind)}-{i + 1:02d}" for i, k in enumerate(keys)}

    # runs (each transcript / conversation with turns)
    run_acc: dict[str, Acc] = {}
    run_out: dict[str, dict[str, Any]] = {}
    for g in turns:
        u = g["unit"]
        run_acc.setdefault(u.uid, Acc()).add(g)
        o = run_out.setdefault(u.uid, {"files": set(), "edits": 0, "edits_failed": 0})
        o["files"] |= g["files"]
        o["edits"] += g["edits"]
        o["edits_failed"] += g["edits_failed"]
    by_uid = {u.uid: u for u in units}
    # families in order of their first turn (stable labels in aggregate mode)
    fam_acc: dict[str, Acc] = {}
    fam_main: dict[str, Acc] = {}
    fam_out: dict[str, dict[str, Any]] = {}
    for g in turns:
        fam = g["place"]["family"]
        if not fam:
            continue
        fam_acc.setdefault(fam, Acc()).add(g)
        if g["place"]["role"] == "main":
            fam_main.setdefault(fam, Acc()).add(g)
        o = fam_out.setdefault(fam, {"files": set(), "edits": 0, "edits_failed": 0, "commit_cmds": 0})
        o["files"] |= g["files"]
        o["edits"] += g["edits"]
        o["edits_failed"] += g["edits_failed"]
        o["commit_cmds"] += g["commit_cmds"]

    def first_key(a: Optional[Acc], fallback: str) -> tuple[str, str]:
        return (ts.key(a.first) if a is not None and a.first else "~", fallback)

    # session rows: API sessions (linked or not), then families without an API row, then commit-only sessions
    commit_sessions: dict[str, list[dict[str, Any]]] = {}
    no_trailer = 0
    for c in commits.values():
        if not c["sessions"]:
            no_trailer += 1
        for sid in c["sessions"]:
            commit_sessions.setdefault(sid, []).append(c)
    fam_of_api: dict[str, str] = {}
    for fam, (sid, _how) in sorted(links.items()):
        fam_of_api.setdefault(sid, fam)
    keys: list[tuple[str, Optional[str], Optional[str]]] = []  # (row key, api id, family)
    for sid in api_sessions:
        keys.append((f"api:{sid}", sid, fam_of_api.get(sid)))
    for fam in fam_acc:
        if fam not in links or links[fam][0] not in api_sessions:
            sid = links[fam][0] if fam in links else None
            keys.append((f"fam:{fam}", sid, fam))
    known_sids = {k[1] for k in keys if k[1]}
    for sid in commit_sessions:
        if sid not in known_sids:
            keys.append((f"git:{sid}", sid, None))
            note("commits_session_without_rows")

    def row_time(k: tuple[str, Optional[str], Optional[str]]) -> tuple[str, str]:
        api = api_sessions.get(k[1]) if k[1] else None
        if api and api.get("created"):
            return (ts.key(str(api["created"])), k[0])
        return first_key(fam_acc.get(k[2]) if k[2] else None, k[0])
    keys.sort(key=row_time)
    s_labels = label_for("session", [k[0] for k in keys])
    fam_label: dict[str, str] = {}
    sessions: list[dict[str, Any]] = []
    for rk, sid, fam in keys:
        api = api_sessions.get(sid) if sid else None
        a = fam_acc.get(fam) if fam else None
        row: dict[str, Any] = {"label": s_labels[rk] if aggregate else ((api or {}).get("title") or sid or fam or rk),
                               "id": None if aggregate else (sid or fam), "transcript_id": None if aggregate else fam}
        if fam:
            fam_label[fam] = row["label"]
        if api:
            row.update({"model": api.get("model"), "api_input": api.get("input"), "api_output": api.get("output"),
                        "api_cache_creation": api.get("cache_creation"), "api_cache_read": api.get("cache_read"),
                        "api_missing": api.get("missing")})
            cost_api = ({"amount": round(api["cost_usd"], 6), "known": True, "currency": api.get("currency", "USD"), "basis": "session_api"}
                        if api.get("cost_usd") is not None else {"amount": None, "known": False, "reason": "no_cost_field", "currency": "USD"})
            row["cost_api"] = cost_api
            row["period"] = f"{str(api.get('created') or '?')[:10]} … {str(api.get('updated') or '?')[:10]}"
        else:
            no_api = "no_session_list" if not api_sessions else "not_in_session_list"
            row.update({"model": None, "api_input": None, "api_output": None, "api_cache_creation": None, "api_cache_read": None,
                        "cost_api": {"amount": None, "known": False, "reason": no_api, "currency": "USD"}})
            row["why"] = {k: no_api for k in ("model", "api_input", "api_output", "api_cache_creation", "api_cache_read", "coverage")}
            row["period"] = f"{(a.first or '?')[:10]} … {(a.last or '?')[:10]}" if a else "?"
        why = row.setdefault("why", {})
        if a is not None:
            r = a.row(grand)
            members = fam_units.get(fam, [])
            row.update({"tr_turns": r["turns"], "tr_input": r["input"], "tr_output": r["output"], "tr_cache_creation": r["cache_creation"],
                        "tr_cache_read": r["cache_read"], "tr_total": r["total"], "tr_cost": r["cost"],
                        "transcripts": len(members), "agents": sum(1 for x in members if _place(x)["role"] != "main"),
                        "link": t("economics.link." + links[fam][1]) if fam in links else t("economics.link.none"),
                        "files_touched": len(fam_out.get(fam, {}).get("files", ())), "edit_calls": fam_out.get(fam, {}).get("edits", 0),
                        "edits_failed": fam_out.get(fam, {}).get("edits_failed", 0), "commit_commands": fam_out.get(fam, {}).get("commit_cmds", 0)})
            mt = fam_main.get(fam).total() if fam in fam_main else 0
            row["main_share"] = round(mt / r["total"], 4) if r["total"] else None
            if not row.get("model"):
                row["model"] = r["models"]
            if api and api.get("cache_read") and r["cache_read"] is not None:
                row["coverage"] = round(r["cache_read"] / api["cache_read"], 4)
            else:
                row["coverage"] = None
                why.setdefault("coverage", "no_counters" if api else ("no_session_list" if not api_sessions else "not_in_session_list"))
            for k in ("api_input", "api_output", "api_cache_creation", "api_cache_read"):
                if api and row.get(k) is None:
                    why[k] = "no_field_in_session_list"
        else:
            for k in ("tr_turns", "tr_input", "tr_output", "tr_cache_creation", "tr_cache_read", "tr_total", "main_share", "coverage",
                      "files_touched", "edit_calls"):
                row[k] = None
                why[k] = "no_transcript"
            if api is not None:
                for k in ("api_input", "api_output", "api_cache_creation", "api_cache_read"):
                    if row.get(k) is None:
                        why[k] = "no_field_in_session_list"
            row["tr_cost"] = {"amount": None, "known": False, "reason": "no_transcript", "currency": "USD"}
            row["transcripts"], row["agents"] = 0, 0
            row["link"] = t("economics.link.none")
            if api is not None and sid not in linked_api:
                note("api_sessions_without_transcript")
        cs = commit_sessions.get(sid, []) if sid else []
        if commits or p.get("repos"):
            row["commits"] = len(cs)
            row["commits_main"] = sum(1 for c in cs if c["on_main"]) if main_known else None
            row["lines_added"] = sum(c["added"] for c in cs)
            row["lines_removed"] = sum(c["removed"] for c in cs)
        else:
            row["commits"] = row["commits_main"] = row["lines_added"] = row["lines_removed"] = None
            for k in ("commits", "commits_main", "lines_added", "lines_removed"):
                why[k] = "commits_unknown"
        if row["commits"] is not None and row["commits_main"] is None:
            why["commits_main"] = "main_ref_unknown"
        best_cost = row["cost_api"] if row["cost_api"].get("known") else row.get("tr_cost") or row["cost_api"]
        row["cost_per_commit"] = _div_cost(best_cost, row["commits"], "no_commits")
        row["cost_per_commit_main"] = _div_cost(best_cost, row["commits_main"], "no_commits_on_main") if main_known or not p.get("repos") \
            else {"amount": None, "known": False, "reason": "main_ref_unknown", "currency": "USD"}
        sessions.append(row)
    for fam in fam_acc:
        if fam not in links:
            note("transcripts_without_session_list" if api_sessions else "transcripts_unlinked")

    # runs
    run_keys = sorted(run_acc, key=lambda uid: (first_key(run_acc[uid], uid)))
    r_labels = label_for("run", run_keys)
    runs: list[dict[str, Any]] = []
    wf_acc: dict[tuple[str, str], Acc] = {}
    wf_agents: dict[tuple[str, str], int] = {}
    for uid in run_keys:
        u = by_uid[uid]
        pl = _place(u)
        r = run_acc[uid].row(grand)
        runs.append({**r, "label": r_labels[uid] if aggregate else (u.title or u.ext_id or uid), "id": None if aggregate else u.ext_id,
                     "session": ((fam_label.get(pl["family"]) or (None if aggregate else pl["family"])) if pl["family"] else t("economics.na")),
                     "role": t("economics.role." + pl["role"]), "provider": _provider(u),
                     "workflow_run": (pl["run"] if not aggregate else "•") if pl["run"] else t("economics.na"),
                     "files_touched": len(run_out[uid]["files"]), "edit_calls": run_out[uid]["edits"],
                     "edits_failed": run_out[uid]["edits_failed"]})
        if pl["run"] and pl["family"]:
            k = (pl["family"], str(pl["run"]))
            wf_agents[k] = wf_agents.get(k, 0) + 1
    for g in turns:
        pl = g["place"]
        if pl["run"] and pl["family"]:
            wf_acc.setdefault((pl["family"], str(pl["run"])), Acc()).add(g)
    wf_keys = sorted(wf_acc, key=lambda k: first_key(wf_acc[k], "/".join(k)))
    w_labels = label_for("workflow", ["/".join(k) for k in wf_keys])
    if aggregate:  # the run column of the runs table names the workflow run by its label too
        wl = {k[1]: w_labels["/".join(k)] for k in wf_keys}
        for row, uid in zip(runs, run_keys):
            run_id = _place(by_uid[uid])["run"]
            if run_id:
                row["workflow_run"] = wl.get(str(run_id))
    workflow_runs = [{**wf_acc[k].row(grand), "label": w_labels["/".join(k)] if aggregate else k[1],
                      "session": fam_label.get(k[0]) or (None if aggregate else k[0]), "agents": wf_agents.get(k, 0)} for k in wf_keys]

    # models, tools, days, providers
    model_acc: dict[str, Acc] = {}
    for g in turns:
        model_acc.setdefault(g["model"] or "?", Acc()).add(g)
    models = []
    for mk in sorted(model_acc, key=lambda k: (-model_acc[k].turns, k)):
        hit = price(None if mk == "?" else mk, None)
        models.append({**model_acc[mk].row(grand), "model": mk if mk != "?" else t("economics.model_unknown"),
                       "price": (f"{hit.get('pattern')} · {hit.get('source') or '?'} · {hit.get('valid_from') or '?'}" if hit.get("known")
                                 else t("economics.unknown", why=t("economics.why." + str(hit.get("reason") or "no_entry"))))})
    tool_acc: dict[str, Acc] = {}
    tool_calls: dict[str, int] = {}
    tool_failed: dict[str, int] = {}
    tool_turns: dict[str, int] = {}
    none_key = "\x00none"
    for g in turns:
        calls = g["tool_ids"]
        if not calls:
            tool_acc.setdefault(none_key, Acc()).add(g)
            continue
        n = len(calls)
        seen: set[str] = set()
        for name, failed in calls:
            w = 1.0 / n if attribution == "split" else 1.0
            tool_acc.setdefault(name, Acc()).add(g, w)
            tool_calls[name] = tool_calls.get(name, 0) + 1
            if failed:
                tool_failed[name] = tool_failed.get(name, 0) + 1
            if name not in seen:
                tool_turns[name] = tool_turns.get(name, 0) + 1
                seen.add(name)
    tools = []
    for name in sorted(tool_acc, key=lambda k: (k == none_key, -tool_acc[k].total(), k)):
        r = tool_acc[name].row(grand)
        if name != none_key:
            r["turns"] = tool_turns.get(name, 0)
        tools.append({**r, "tool": t("economics.no_tool") if name == none_key else name,
                      "calls": tool_calls.get(name, 0) if name != none_key else 0, "failed": tool_failed.get(name, 0)})
    if attribution != "split":
        note("tool_attribution_full")
    day_acc: dict[str, Acc] = {}
    for g in turns:
        day_acc.setdefault(g["day"] or "?", Acc()).add(g)
    day_commits: dict[str, int] = {}
    for c in commits.values():
        d = _day(c["date"], zone) if c["date"] else "?"
        day_commits[d or "?"] = day_commits.get(d or "?", 0) + 1
    days = []
    for d in sorted(set(day_acc) | set(day_commits)):
        r = day_acc[d].row(grand) if d in day_acc else Acc().row(grand)
        days.append({**r, "day": d if d != "?" else t("economics.day_unknown"), "commits": day_commits.get(d, 0) if commits or p.get("repos") else None})
        if days[-1]["commits"] is None:
            days[-1]["why"]["commits"] = "commits_unknown"
    prov_units: dict[tuple[str, str], list[Unit]] = {}
    for u in units:
        prov_units.setdefault((_provider(u), str(u.source.get("format") or "?")), []).append(u)
    prov_acc: dict[tuple[str, str], Acc] = {}
    for g in turns:
        u = g["unit"]
        prov_acc.setdefault((_provider(u), str(u.source.get("format") or "?")), Acc()).add(g)
    thinking_min = int(ds.preset("thinking.full_min_chars", 200))
    providers = []
    for k in sorted(prov_units):
        ef = economics_fields(prov_units[k], thinking_min)
        r = (prov_acc[k] if k in prov_acc else Acc()).row(grand)
        providers.append({**r, "provider": k[0], "format": k[1], "units": len(prov_units[k]), "has_usage": ef["has_usage"],
                          "has_tool_traces": ef["has_tool_traces"], "reasoning": t("economics.reasoning." + ef["reasoning"]),
                          "inventory": ef})

    # totals and outcomes
    tot_row = total.row(grand)
    n_commits = sum(1 for c in commits.values() if c["sessions"])
    tot_row.update({"label": t("economics.label.total"),
                    "commits": n_commits if (commits or p.get("repos")) else None,
                    "commits_main": (sum(1 for c in commits.values() if c["sessions"] and c["on_main"]) if main_known else None)
                    if (commits or p.get("repos")) else None})
    if tot_row["commits"] is None:
        tot_row["why"].update({"commits": "commits_unknown", "commits_main": "commits_unknown"})
    elif tot_row["commits_main"] is None:
        tot_row["why"]["commits_main"] = "main_ref_unknown"
    api_cost_known = [s["cost_usd"] for s in api_sessions.values() if s.get("cost_usd") is not None]
    if api_sessions:
        tot_row["cost_api"] = ({"amount": round(sum(api_cost_known), 6), "known": True, "currency": "USD", "basis": "session_api"}
                               if len(api_cost_known) == len(api_sessions)
                               else {"amount": None, "known": False, "reason": "no_cost_field", "currency": "USD",
                                     "partial": round(sum(api_cost_known), 6) if api_cost_known else None})
        for k in ("input", "output", "cache_creation", "cache_read"):
            vals = [s.get(k) for s in api_sessions.values()]
            tot_row["api_" + k] = sum(v for v in vals if v is not None) if all(v is not None for v in vals) else None
    # cost per commit over the sessions that have commits (a session list may hold other projects' sessions too)
    with_commits = [r for r in sessions if r.get("commits")]
    if tot_row["commits"] is None:
        tot_row["cost_per_commit"] = _div_cost({"known": False, "reason": "commits_unknown"}, None, "no_commits")
    elif not with_commits:
        tot_row["cost_per_commit"] = _div_cost({"known": True, "amount": 0.0}, 0, "no_commits")
    else:
        costs = [r["cost_per_commit"] for r in with_commits]
        if all(c.get("known") for c in costs):
            amount = sum(float(c["amount"]) * r["commits"] for c, r in zip(costs, with_commits))
            tot_row["cost_per_commit"] = {"amount": round(amount / sum(r["commits"] for r in with_commits), 6), "known": True,
                                          "currency": costs[0].get("currency", "USD"), "basis": "sessions_with_commits"}
        else:
            bad = next(c for c in costs if not c.get("known"))
            tot_row["cost_per_commit"] = {"amount": None, "known": False, "reason": bad.get("reason") or "no_price", "currency": "USD"}
    outcomes = {"repos": len(p.get("repos") or []), "commits_all": len(commits), "commits_with_session": n_commits,
                "commits_without_session": no_trailer, "main_ref": git_cfg.get("main_ref"), "main_known": main_known,
                "lines_added": sum(c["added"] for c in commits.values() if c["sessions"]),
                "lines_removed": sum(c["removed"] for c in commits.values() if c["sessions"]),
                "files_touched": len(set().union(*[g["files"] for g in turns])) if turns else 0,
                "edit_calls": sum(g["edits"] for g in turns), "edits_failed": sum(g["edits_failed"] for g in turns),
                "commit_commands_in_transcripts": sum(g["commit_cmds"] for g in turns)}
    if not aggregate:
        outcomes["commits"] = [{"hash": c["hash"], "date": c["date"], "sessions": c["sessions"], "on_main": c["on_main"],
                                "added": c["added"], "removed": c["removed"], "files": c["files"]} for c in sorted(commits.values(), key=lambda c: c["hash"])]
    if total.cost_unknown:
        note("cost_unknown_turns", int(round(total.cost_unknown)), reasons=dict(total.cost_reasons))
    cal = calibration(calib_pairs)
    method = {"measured": "message.usage (format file: usage)", "duplicate_lines": dup_mode, "tool_attribution": attribution,
              "day_zone": zone, "context": context_mode, "estimator": estimator.describe(),
              "expected_error": ({"basis": "calibration", **cal} if cal else
                                 {"basis": "none", "reason": t("economics.why.no_calibration")}),
              "expected_error_how": t("economics.how.calibration")}
    unknowns = [notes[k] for k in sorted(notes)]
    prices_used = []
    for (mk, day), hit in sorted(price_cache.items(), key=lambda kv: (str(kv[0][0]), str(kv[0][1]))):
        if day is None:
            continue
        prices_used.append({"model": mk, "day": day, "known": bool(hit.get("known")), "source": hit.get("source"),
                            "pattern": hit.get("pattern"), "valid_from": hit.get("valid_from"), "reason": hit.get("reason")})
    settings = {"of": p.get("of") or "all", "sessions_files": len(p.get("sessions") or []), "repos": len(p.get("repos") or []),
                "links": len(explicit), "duplicate_lines": dup_mode, "tool_attribution": attribution, "day_zone": zone,
                "context": context_mode, "main_ref": git_cfg.get("main_ref"), "estimate": estimator.method}
    doc = {"schema": SCHEMA, "aggregate": aggregate, "language": language(), "settings": settings, "method": method,
           "totals": tot_row, "providers": providers, "sessions": sessions, "runs": runs, "workflow_runs": workflow_runs,
           "models": models, "tools": tools, "days": days, "outcomes": outcomes, "unknowns": unknowns,
           "prices": _dedupe_prices(prices_used)}
    if aggregate:
        doc["prices"] = [{k: v for k, v in e.items()} for e in doc["prices"]]
    return json.loads(json.dumps(doc, ensure_ascii=False, default=_jsonable))


def _dedupe_prices(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """One row per (model, price used): first and last day it applied."""
    out: dict[tuple[Any, ...], dict[str, Any]] = {}
    for r in rows:
        k = (r["model"], r["known"], r["source"], r["pattern"], r["valid_from"], r["reason"])
        e = out.setdefault(k, {**{kk: r[kk] for kk in ("model", "known", "source", "pattern", "valid_from", "reason")}, "from": r["day"], "to": r["day"]})
        e["from"] = min(e["from"], r["day"])
        e["to"] = max(e["to"], r["day"])
    return [out[k] for k in sorted(out, key=lambda k: tuple(str(x) for x in k))]


def _jsonable(o: Any) -> Any:
    if isinstance(o, set):
        return sorted(o)
    return str(o)


# ───────────────────────────── rendering ─────────────────────────────
def _fmt_int(v: int) -> str:
    sep = t("economics.num.thousands")
    s = f"{abs(int(v)):,}".replace(",", sep)
    return ("-" if v < 0 else "") + s


def _fmt_amount(v: float) -> str:
    dec = t("economics.num.decimal")
    s = f"{abs(v):.2f}" if abs(v) >= 0.01 or v == 0 else f"{abs(v):.6f}".rstrip("0")
    whole, _, frac = s.partition(".")
    return ("-" if v < 0 else "") + _fmt_int(int(whole)) + (dec + frac if frac else "")


def cell(kind: str, v: Any, *, csv_mode: bool = False, why: Optional[str] = None) -> str:
    if kind == "cost":
        if not isinstance(v, dict):
            return t("economics.unknown", why=t("economics.why.no_column"))
        cur = v.get("currency") or "USD"
        if v.get("known"):
            amt = v["amount"]
            return (f"{amt}" if csv_mode else _fmt_amount(float(amt))) + f" {cur}"
        why = t("economics.why." + str(v.get("reason") or "no_price"))
        if v.get("partial"):
            part = f"{v['partial']}" if csv_mode else _fmt_amount(float(v["partial"]))
            return t("economics.unknown_partial", why=why, partial=f"{part} {cur}", n=v.get("unknown_turns", "?"))
        return t("economics.unknown", why=why)
    if v is None:
        return t("economics.unknown", why=t("economics.why." + (why or "not_measured")))
    if kind == "int":
        return str(int(v)) if csv_mode else _fmt_int(int(v))
    if kind == "share":
        return f"{float(v):.4f}" if csv_mode else f"{float(v) * 100:.1f}".replace(".", t("economics.num.decimal")) + " %"
    if kind == "bool":
        return t("report.yes") if v else t("report.no")
    return str(v)


def layout(ds: DataStore) -> dict[str, Any]:
    rel = str((ds.preset("economics.report") or {}).get("tables") or "economics/report.json")
    return ds.file(rel)


def _max_rows(ds: DataStore) -> int:
    return int((ds.preset("economics.report") or {}).get("max_rows", 0) or 0)


def table_rows(doc: dict[str, Any], tab: dict[str, Any]) -> list[dict[str, Any]]:
    rows = doc.get(tab["rows"])
    if isinstance(rows, dict):
        return [rows]
    return list(rows or [])


def render_csv(doc: dict[str, Any], table_id: str, ds: DataStore) -> str:
    lay = layout(ds)
    tab = next((x for x in lay.get("tables") or [] if x["id"] == table_id), None)
    if tab is None:
        raise ValueError(f"economics table {table_id!r} not in {lay.get('_source')}")
    cols = lay.get("columns") or {}
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(tab["columns"])
    for r in table_rows(doc, tab):
        w.writerow([cell((cols.get(c) or {}).get("kind", "text"), r.get(c), csv_mode=True, why=(r.get("why") or {}).get(c)) if c in r
                    else t("economics.unknown", why=t("economics.why.no_column")) for c in tab["columns"]])
    return buf.getvalue()


def render_markdown(doc: dict[str, Any], ds: DataStore) -> str:
    lay = layout(ds)
    cols = lay.get("columns") or {}
    max_rows = _max_rows(ds)
    out = [f"# {t('economics.title')}", ""]
    if doc.get("aggregate"):
        out += [t("economics.aggregate_note"), ""]
    m = doc.get("method") or {}
    est = m.get("estimator") or {}
    ee = m.get("expected_error") or {}
    out += [f"## {t('economics.method')}", "",
            "- " + t("economics.method.measured", dup=m.get("duplicate_lines")),
            "- " + t("economics.method.estimated", used=", ".join(f"{k}: {v}" for k, v in (est.get("used") or {}).items()) or "—",
                     context=m.get("context")),
            "- " + (t("economics.method.error_cal", n=ee.get("turns"), median=_pct(ee.get("median")), p10=_pct(ee.get("p10")), p90=_pct(ee.get("p90")))
                    if ee.get("basis") == "calibration" else t("economics.unknown", why=ee.get("reason") or "?")),
            "- " + t("economics.method.tools", how=t("economics.attribution." + str(m.get("tool_attribution")))),
            "- " + t("economics.method.days", zone=m.get("day_zone"))]
    for enc, why in sorted((est.get("unavailable") or {}).items()):
        out.append("- " + t("economics.method.tiktoken_unavailable", enc=enc, why=t("economics.why." + why.split(":")[0])))
    out.append("")
    for tab in lay.get("tables") or []:
        rows = table_rows(doc, tab)
        if not rows:
            continue
        out += [f"## {label(tab.get('title'))}", ""]
        heads = [label((cols.get(c) or {}).get("label")) or c for c in tab["columns"]]
        out.append("| " + " | ".join(heads) + " |")
        out.append("|" + "|".join("---" for _ in heads) + "|")
        shown = rows if max_rows <= 0 else rows[:max_rows]
        for r in shown:
            out.append("| " + " | ".join(cell((cols.get(c) or {}).get("kind", "text"), r.get(c), why=(r.get("why") or {}).get(c)).replace("|", "\\|") if c in r
                                         else t("economics.unknown", why=t("economics.why.no_column")) for c in tab["columns"]) + " |")
        if len(rows) > len(shown):
            out.append("")
            out.append(t("report.and_more", n=len(rows) - len(shown), where=f"economics/{tab['id']}.csv"))
        out.append("")
    oc = doc.get("outcomes") or {}
    out += [f"## {t('economics.outcomes')}", "",
            "- " + t("economics.outcomes.git", repos=oc.get("repos", 0), commits=oc.get("commits_with_session", 0),
                     without=oc.get("commits_without_session", 0), all=oc.get("commits_all", 0)),
            "- " + t("economics.outcomes.files", files=oc.get("files_touched", 0), edits=oc.get("edit_calls", 0), failed=oc.get("edits_failed", 0),
                     commits=oc.get("commit_commands_in_transcripts", 0)), ""]
    unk = doc.get("unknowns") or []
    if unk:
        out += [f"## {t('economics.unknowns')}", ""]
        for e in unk:
            parts = []
            for k, v in sorted((k, v) for k, v in e.items() if k not in ("key", "n")):
                if isinstance(v, dict):
                    parts.append(", ".join(f"{t('economics.why.' + str(kk)) if k == 'reasons' else kk}: {vv}" for kk, vv in sorted(v.items())))
                else:
                    parts.append(f"{t('economics.why.' + str(v)) if k == 'reason' else v}")
            out.append("- " + t("economics.note." + e["key"], n=e["n"]) + (" — " + "; ".join(parts) if parts else ""))
        out.append("")
    return "\n".join(out).rstrip() + "\n"


def _pct(v: Any) -> str:
    if v is None:
        return "?"
    return f"{float(v) * 100:+.1f}".replace(".", t("economics.num.decimal")) + " %"


def output_files(doc: dict[str, Any], ds: DataStore) -> list[tuple[str, str]]:
    """(relative path, text) of every economics output: JSON, Markdown in the run's language, one CSV per table."""
    files = [("economics/economics.json", json.dumps(doc, ensure_ascii=False, indent=1, sort_keys=False) + "\n"),
             (f"economics/{t('economics.file')}.md", render_markdown(doc, ds))]
    for tab in layout(ds).get("tables") or []:
        files.append((f"economics/{tab['id']}.csv", render_csv(doc, tab["id"], ds)))
    return files


def summary(doc: dict[str, Any]) -> dict[str, Any]:
    tot = doc.get("totals") or {}
    return {"turns": tot.get("turns"), "turns_measured": tot.get("turns_measured"), "turns_estimated": tot.get("turns_estimated"),
            "total": tot.get("total"), "est_total": tot.get("est_total"), "cache_share": tot.get("cache_share"),
            "cost": tot.get("cost"), "commits": tot.get("commits"), "sessions": len(doc.get("sessions") or []),
            "runs": len(doc.get("runs") or []), "unknowns": len(doc.get("unknowns") or [])}


__all__ = ["build_economics", "render_markdown", "render_csv", "output_files", "summary", "git_commits", "load_session_lists"]
