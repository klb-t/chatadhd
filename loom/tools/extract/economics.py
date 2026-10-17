"""Token economics (P38 requirement 37): measured and estimated tokens, cost, outcomes.

Inputs, all local (zero network): the units already read (Claude Code transcripts carry per-turn counters in
``message.usage``; chat exports carry none and are estimated per message, labelled with the method), optionally a
saved session list (``--sesje``: the shape ``list_sessions`` returns, mapped by ``data/session_sources/*.json``;
the tool never calls an API) and git repositories (``--repo``: ``git log --all`` read-only, never a lazy fetch,
commits per session from the ``Claude-Session`` trailer, lines changed).  Prices come from the one price book
(``prices.py``); a number that cannot be known is said to be unknown with its reason, never 0, and a number that is
known only as a bound or partly estimated says so next to it.

Measured is not always final: a response whose lines never carry ``stop_reason`` (subagent and workflow transcripts
write the stream-start snapshot of the counters) has a lower-bound output; it is labelled so, counted in the
unknowns, kept out of the estimator's calibration, and optionally replaced by an estimate
(``economics.usage.stale_output``).  Cost from measured tokens and cost from estimated tokens are kept apart
(``cost_measured`` / ``cost_estimated``; a cell that adds both says so), and amounts in different currencies are
never added.

Report: per session (API counters + transcripts + commits), per repository, per run (main transcript, each
subagent, each workflow agent), per workflow run, per model, per tool, per day, per source (provider comparison),
totals; Markdown (pl/en from the i18n table), CSV per table (numbers and reason codes in separate columns) and JSON —
layout in ``data/economics/report.json``.  ``--zbiorczo`` replaces every title, id, path, repository and commit hash
by a numbered label (counts only).
"""
import csv
import fnmatch
import io
import json
import logging
import os
import re
import subprocess
from datetime import timezone
from pathlib import Path
from typing import Any, Iterable, Optional

from .datafiles import DataStore
from .i18n import label, language, t
from .inventory import economics_fields
from .model import Unit
from .prices import PriceBook, cost_of
from .readers.base import getpath, getpath_many
from .tokencount import Estimator, calibration
from . import timestamps as ts

log = logging.getLogger(__name__)

SCHEMA = "loom.extract.economics/1"
MEASURED = ("input", "output", "cache_creation", "cache_read")
EXTRA_COUNTERS = ("cache_creation_5m", "cache_creation_1h", "output_thinking", "web_search_requests", "web_fetch_requests")
ESTIMATED = ("est_input", "est_output", "est_context")
COUNTERS = MEASURED + EXTRA_COUNTERS + ESTIMATED
DUPLICATE_MODES = ("max", "last", "sum")
STALE_MODES = ("keep", "estimate")
UTC_NAMES = ("", "UTC", "utc", "Z")
BASES = ("measured", "estimated")
# a turn's cost basis → the bucket it is added to (cost from measured tokens and from estimated tokens never mix
# silently: a cell that adds both says so)
COST_BASIS = {"prices×measured": "measured", "measured_line_cost": "measured", "prices×estimated": "estimated",
              "prices×measured+estimated_output": "estimated"}


def _r6(x: float) -> float:
    return round(x, 6)


# ───────────────────────────── accumulator ─────────────────────────────
class Acc:
    """Sums of turns: measured counters, estimated counters, cost per basis (measured / estimated) and currency, the
    turns whose cost is unknown (with reasons and missing price fields), and the turns whose output is only a
    bound or an estimate."""

    def __init__(self) -> None:
        self.n = {k: 0.0 for k in COUNTERS}
        self.turns = 0.0
        self.measured = 0.0
        self.estimated = 0.0
        self.out_lb = [0.0, 0.0]   # measured turns whose output is the stream-start snapshot: turns, output tokens
        self.out_est = [0.0, 0.0]  # measured turns whose output was replaced by an estimate: turns, output tokens
        self.hidden = 0.0          # estimated turns of a model whose hidden reasoning the export does not hold
        self.cost_sum: dict[str, dict[str, float]] = {b: {} for b in BASES}
        self.cost_turns = {b: 0.0 for b in BASES}
        self.cost_unknown = {b: 0.0 for b in BASES}
        self.cost_reasons: dict[str, dict[str, int]] = {b: {} for b in BASES}
        self.cost_missing: dict[str, dict[str, int]] = {b: {} for b in BASES}
        self.methods: dict[str, int] = {}
        self.models: dict[str, int] = {}
        self.first: Optional[str] = None
        self.last: Optional[str] = None

    def add(self, turn: dict[str, Any], w: float = 1.0, usage: Optional[dict[str, int]] = None) -> None:
        """Add ``turn`` with weight ``w`` (turn count and cost); ``usage`` = this share's integer counters (a turn split
        between its tool calls), else the turn's counters × ``w``."""
        self.turns += w
        if turn["measured"]:
            self.measured += w
        else:
            self.estimated += w
        if usage is None:
            for k, v in turn["usage"].items():
                if k in self.n:
                    self.n[k] += v * w
            out_share = float(turn["usage"].get("output", 0)) * w
        else:
            for k, v in usage.items():
                if k in self.n:
                    self.n[k] += v
            out_share = float(usage.get("output", 0))
        ob = turn.get("output_basis")
        if ob == "lower_bound":
            self.out_lb[0] += w
            self.out_lb[1] += out_share
        elif ob == "estimated":
            self.out_est[0] += w
            self.out_est[1] += out_share
        if turn.get("hidden_reasoning"):
            self.hidden += w
        c = turn["cost"]
        b = COST_BASIS.get(str(c.get("basis")), "measured" if turn["measured"] else "estimated")
        self.cost_turns[b] += w
        if c.get("known"):
            cur = str(c.get("currency") or "USD")
            self.cost_sum[b][cur] = self.cost_sum[b].get(cur, 0.0) + float(c["amount"]) * w
        else:
            self.cost_unknown[b] += w
            r = str(c.get("reason") or "no_price")
            self.cost_reasons[b][r] = self.cost_reasons[b].get(r, 0) + 1
            for f in c.get("missing") or []:
                self.cost_missing[b][str(f)] = self.cost_missing[b].get(str(f), 0) + 1
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

    def _currency(self) -> str:
        for b in BASES:
            for cur in self.cost_sum[b]:
                return cur
        return "USD"

    def cost(self, basis: Optional[str] = None) -> dict[str, Any]:
        """The cost of the turns (``basis`` = only those priced from measured / estimated tokens).  Known only when
        every turn's cost is known and in one currency; ``basis`` says what it was computed from (``mixed`` carries
        both parts)."""
        bases = [basis] if basis else list(BASES)
        if not sum(self.cost_turns[b] for b in bases):
            return {"amount": None, "known": False, "reason": f"no_turns_{basis}" if basis and self.turns else "no_turns",
                    "currency": self._currency()}
        used = [b for b in bases if self.cost_turns[b]]
        kind = used[0] if len(used) == 1 else "mixed"
        sums: dict[str, float] = {}
        reasons: dict[str, int] = {}
        missing: dict[str, int] = {}
        for b in bases:
            for cur, a in self.cost_sum[b].items():
                sums[cur] = sums.get(cur, 0.0) + a
            for r, n in self.cost_reasons[b].items():
                reasons[r] = reasons.get(r, 0) + n
            for f, n in self.cost_missing[b].items():
                missing[f] = missing.get(f, 0) + n
        unknown = sum(self.cost_unknown[b] for b in bases)
        if len(sums) > 1:  # never added across currencies
            out: dict[str, Any] = {"amount": None, "known": False, "reason": "mixed_currency", "currency": "/".join(sorted(sums)),
                                   "partial_by_currency": {k: _r6(v) for k, v in sorted(sums.items())}, "basis": kind}
            if unknown:
                out["unknown_turns"] = int(round(unknown))
            return out
        cur = next(iter(sums), self._currency())
        total = sum(sums.values())
        if unknown == 0:
            out = {"amount": _r6(total), "known": True, "currency": cur, "basis": kind}
            if kind == "mixed":
                out["measured"] = _r6(sum(self.cost_sum["measured"].values()))
                out["estimated"] = _r6(sum(self.cost_sum["estimated"].values()))
            return out
        out = {"amount": None, "known": False, "reason": max(reasons, key=lambda k: (reasons[k], k)) if reasons else "no_price",
               "partial": _r6(total) if total else None, "unknown_turns": int(round(unknown)),
               "reasons": dict(sorted(reasons.items())), "currency": cur, "basis": kind}
        if missing:
            out["missing"] = sorted(missing)
        return out

    def output_qual(self) -> Optional[dict[str, Any]]:
        if self.out_lb[0]:
            return {"code": "lower_bound", "n": int(round(self.out_lb[0])), "tokens": int(round(self.out_lb[1]))}
        if self.out_est[0]:
            return {"code": "estimated_output", "n": int(round(self.out_est[0])), "tokens": int(round(self.out_est[1]))}
        return None

    def row(self, grand_total: Optional[int] = None) -> dict[str, Any]:
        tot = self.total()
        inp_side = self.n["input"] + self.n["cache_creation"] + self.n["cache_read"]
        out: dict[str, Any] = {k: int(round(self.n[k])) for k in COUNTERS}
        out.update({"turns": int(round(self.turns)), "turns_measured": int(round(self.measured)), "turns_estimated": int(round(self.estimated)),
                    "total": tot if self.measured else None,
                    "est_total": int(round(self.n["est_context"] or self.n["est_input"])) + int(round(self.n["est_output"])) if self.estimated else None,
                    "cache_share": round(self.n["cache_read"] / tot, 4) if tot else None,
                    "cache_hit": round(self.n["cache_read"] / inp_side, 4) if inp_side else None,
                    "cost": self.cost(), "cost_measured": self.cost("measured"), "cost_estimated": self.cost("estimated"),
                    "output_lower_bound": int(round(self.out_lb[1])), "turns_output_lower_bound": int(round(self.out_lb[0])),
                    "turns_output_estimated": int(round(self.out_est[0])),
                    "methods": methods_text(self.methods), "methods_raw": dict(sorted(self.methods.items())),
                    "models": ", ".join(f"{k} ({_fmt_int(v)})" for k, v in sorted(self.models.items(), key=lambda kv: (-kv[1], kv[0]))),
                    "first": self.first, "last": self.last})
        why: dict[str, str] = {}
        if not self.measured:
            for k in MEASURED + EXTRA_COUNTERS + ("output_lower_bound", "turns_output_lower_bound", "turns_output_estimated"):
                out[k] = None
            for k in MEASURED + EXTRA_COUNTERS + ("total", "cache_share", "cache_hit", "share", "output_lower_bound",
                                                  "turns_output_lower_bound", "turns_output_estimated"):
                why[k] = "no_counters" if self.turns else "no_turns"
        if not self.estimated:
            for k in ESTIMATED:
                out[k] = None
            for k in ESTIMATED + ("est_total",):
                why[k] = "measured_instead" if self.measured else "no_turns"
        if grand_total is not None:
            out["share"] = round(tot / grand_total, 4) if grand_total and self.measured else None
        qual: dict[str, Any] = {}
        q = self.output_qual() if self.measured else None
        if q:
            for k in ("output", "total", "share"):
                qual[k] = q
        if self.estimated and self.hidden:
            for k in ("est_output", "est_total"):
                qual[k] = {"code": "hidden_reasoning", "n": int(round(self.hidden))}
        out["why"] = why
        out["qual"] = qual
        return out


def methods_text(methods: dict[str, int]) -> str:
    return ", ".join(f"{method_label(k)} ({_fmt_int(v)})" for k, v in sorted(methods.items()))


def method_label(how: str) -> str:
    """A method code (``measured``, ``chars/3.6 (default)``, ``tiktoken:o200k_base``) → words of the run's language."""
    if how == "measured":
        return t("economics.method_name.measured")
    m = re.fullmatch(r"chars/([0-9.]+) \((.*)\)", how)
    if m:
        lang = m.group(2)
        lang_txt = t("economics.lang.default") if lang == "default" else lang
        return t("economics.method_name.chars", k=m.group(1).replace(".", t("economics.num.decimal")), lang=lang_txt)
    if how.startswith("tiktoken:"):
        return t("economics.method_name.tiktoken", enc=how.split(":", 1)[1])
    return how


# ───────────────────────────── inputs: session list, git ─────────────────────────────
def load_session_lists(paths: list[str], ds: DataStore, source_id: Optional[str] = None) -> tuple[dict[str, dict[str, Any]], list[dict[str, Any]]]:
    """Saved session lists (JSON array / object with a wrapper / JSONL) → id → session row; problems are listed.
    A field whose path has ``[*]`` (``repos``: every source repository of the session) is a sorted list."""
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
            log.debug("session list %d unreadable", i + 1, exc_info=True)
            problems.append({"key": "sessions_unreadable", "n": 1, "file": i + 1, "detail": type(exc).__name__})
            continue
        items: list[Any] = []
        try:
            doc = json.loads(text)
            if isinstance(doc, list):
                items = doc
            elif isinstance(doc, dict):
                # a wrapper is a dotted path: a list of sessions (list_sessions: ccr.data) or one session (get_session: ccr)
                items = [doc]
                for w in spec.get("wrappers") or []:
                    v = getpath(doc, w)
                    if isinstance(v, list):
                        items = v
                        break
                    if isinstance(v, dict) and getpath(v, fields.get("id", "id")):
                        items = [v]
                        break
        except json.JSONDecodeError:
            log.debug("session list %d is not one JSON document; read as JSONL", i + 1, exc_info=True)
            for ln, line in enumerate(text.splitlines(), 1):
                if not line.strip():
                    continue
                try:
                    items.append(json.loads(line))
                except json.JSONDecodeError:
                    log.debug("session list %d line %d is not JSON", i + 1, ln, exc_info=True)
                    problems.append({"key": "sessions_bad_line", "n": 1, "file": i + 1, "line": ln})
        for obj in items:
            if not isinstance(obj, dict) or not getpath(obj, fields.get("id", "id")):
                problems.append({"key": "sessions_no_id", "n": 1, "file": i + 1})
                continue
            row: dict[str, Any] = {}
            for k, fpath in fields.items():
                if "[*]" in str(fpath):
                    row[k] = sorted({str(v) for v in getpath_many(obj, str(fpath)) if v not in (None, "")})
                else:
                    row[k] = getpath(obj, fpath)
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


def _first_line(b: bytes) -> str:
    lines = b.decode("utf-8", "replace").strip().splitlines()
    return lines[0][:200] if lines else ""


def git_commits(repo: str, cfg: dict[str, Any]) -> dict[str, Any]:
    """Commits of every ref (read-only ``git log --all``): hash, author date, ``Claude-Session`` trailer, lines; the
    set reachable from the first main ref that resolves (``economics.git.main_ref``: a name or a preference list; None
    + reason when none does).  Never a network call: lazy fetches are off (``GIT_NO_LAZY_FETCH``), and in a partial
    clone (promisor remote) ``--numstat`` — which would fetch missing blobs on a git without that switch — is not run
    (lines unknown, said); a shallow clone is said (its commits are a lower bound).  No external program the
    repository configures runs (textconv, external diff, fsmonitor, pager, signature display off)."""
    rx = trailer_regex(cfg, line=True)
    base = ["git", "-C", str(Path(repo).expanduser()), "-c", "core.fsmonitor=false", "-c", "core.pager=cat", "-c", "log.showSignature=false",
            "-c", "diff.external=", "--no-pager"]
    env = {**os.environ, "GIT_TERMINAL_PROMPT": "0", "GIT_PAGER": "cat", "LC_ALL": "C", "GIT_OPTIONAL_LOCKS": "0", "GIT_NO_LAZY_FETCH": "1"}
    out: dict[str, Any] = {"repo": repo, "commits": [], "main": None, "main_reason": None, "main_ref": None, "main_refs_tried": [],
                           "error": None, "partial_clone": False, "shallow": False, "lines_known": True}

    def run(args: list[str]) -> Optional[subprocess.CompletedProcess]:
        try:
            return subprocess.run(base + args, capture_output=True, env=env, check=False)
        except OSError as exc:
            log.debug("git %s unavailable", args[:1], exc_info=True)
            out["error"] = f"git_unavailable:{type(exc).__name__}"
            return None
    probe = run(["config", "--get-regexp", r"^(extensions\.partialclone|remote\..*\.promisor)$"])
    if probe is None:
        return out
    out["partial_clone"] = probe.returncode == 0 and bool(probe.stdout.strip())
    sh = run(["rev-parse", "--is-shallow-repository"])
    if sh is None:
        return out
    out["shallow"] = sh.returncode == 0 and sh.stdout.strip() == b"true"
    numstat = bool(cfg.get("numstat", True)) and not out["partial_clone"]
    out["lines_known"] = numstat
    fmt = "%x1e%H%x1f%aI%x1f%B%x1f"
    res = run(["log", "--all", "--no-color", "--no-renames", "--no-textconv", "--no-ext-diff", f"--format={fmt}"] + (["--numstat"] if numstat else []))
    if res is None:
        return out
    if res.returncode != 0:
        first = _first_line(res.stderr) if res.stderr else ""
        out["error"] = "git_log_failed" + (":" + first if first else "")
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
        out["commits"].append({"hash": sha, "date": date, "sessions": sessions, "added": added if numstat else None,
                               "removed": removed if numstat else None, "files": files if numstat else None, "binary_files": binary})
    refs = cfg.get("main_ref")
    refs = [str(x) for x in refs if x] if isinstance(refs, list) else ([str(refs)] if refs else [])
    out["main_refs_tried"] = refs
    if refs:
        for ref in refs:
            r1 = run(["rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}"])
            if r1 is None:
                return out
            if r1.returncode != 0:
                continue
            r2 = run(["rev-list", ref, "--"])
            if r2 is None:
                return out
            if r2.returncode == 0:
                out["main"] = set(r2.stdout.decode("ascii", "replace").split())
                out["main_ref"] = ref
                break
        if out["main"] is None:
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
    """The calendar day of a timestamp in ``zone`` (UTC by default).  A stamp with an offset (git's ``%aI`` keeps the
    author's) is converted first; only a stamp already in UTC (``…Z``) is read as written."""
    if not iso:
        return None
    if zone in UTC_NAMES and iso.endswith("Z"):
        return iso[:10]
    dt = ts.from_iso(iso)
    if dt is None:
        return iso[:10]
    if zone in UTC_NAMES:
        return dt.astimezone(timezone.utc).date().isoformat()
    try:
        from zoneinfo import ZoneInfo
        return dt.astimezone(ZoneInfo(zone)).date().isoformat()
    except Exception:  # noqa: BLE001 - validated in build_economics (an unknown zone is noted there); UTC here
        log.debug("day zone %r unusable", zone, exc_info=True)
        return dt.astimezone(timezone.utc).date().isoformat()


def _block_text(b: Any) -> str:
    if b.text:
        return b.text
    if b.kind == "tool_use" and b.input is not None:
        return json.dumps(b.input, ensure_ascii=False, sort_keys=True)
    return ""


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


def _matches(model: Optional[str], patterns: Iterable[str]) -> bool:
    name = (model or "").split("/", 1)[-1].lower()
    return bool(name) and any(fnmatch.fnmatchcase(name, str(p).lower()) for p in patterns)


def _div_cost(c: dict[str, Any], n: Optional[int], why_zero: str, why_none: str = "commits_unknown") -> dict[str, Any]:
    cur = c.get("currency", "USD")
    if n is None:
        return {"amount": None, "known": False, "reason": why_none, "currency": cur}
    if n == 0:
        return {"amount": None, "known": False, "reason": why_zero, "currency": cur}
    if not c.get("known"):
        out = {"amount": None, "known": False, "reason": c.get("reason") or "no_price", "currency": cur}
        if c.get("missing"):
            out["missing"] = c["missing"]
        return out
    return {"amount": _r6(float(c["amount"]) / n), "known": True, "currency": cur,
            "basis": "session_api" if c.get("basis") == "session_api" else "transcript_prices", "of": c.get("basis")}


def _split_ints(usage: dict[str, int], n: int) -> list[dict[str, int]]:
    """A turn's integer counters split between its ``n`` tool calls by the largest remainder (equal remainders go
    to the earlier calls): every share is an integer and the shares add up to the turn."""
    shares: list[dict[str, int]] = [{} for _ in range(n)]
    for k, v in usage.items():
        q, rem = divmod(int(v), n)
        for i in range(n):
            shares[i][k] = q + (1 if i < rem else 0)
    return shares


def _per_commit(rows: list[dict[str, Any]]) -> dict[str, Any]:
    """Cost per commit over the rows that have commits: known only when every row's is (same currency)."""
    with_c = [r for r in rows if r.get("commits")]
    if not with_c:
        return {"amount": None, "known": False, "reason": "no_commits", "currency": "USD"}
    cc = [r["cost_per_commit"] for r in with_c]
    if all(c.get("known") for c in cc) and len({c.get("currency") for c in cc}) == 1:
        amount = sum(float(c["amount"]) * r["commits"] for c, r in zip(cc, with_c))
        bases = sorted({str(c.get("basis")) for c in cc})
        return {"amount": _r6(amount / sum(r["commits"] for r in with_c)), "known": True, "currency": cc[0].get("currency", "USD"),
                "basis": bases[0] if len(bases) == 1 else "mixed_sources"}
    bad = next((c for c in cc if not c.get("known")), None)
    return {"amount": None, "known": False, "reason": (bad or {}).get("reason") or "mixed_currency", "currency": "USD"}


# ───────────────────────────── build ─────────────────────────────
def build_economics(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    ds: DataStore = eng.ds
    cfg = ds.preset("economics") or {}
    aggregate = bool(eng.aggregate)
    ucfg = cfg.get("usage") or {}
    dup_mode = str(p.get("duplicate_lines") or ucfg.get("duplicate_lines") or "max")
    if dup_mode not in DUPLICATE_MODES:
        raise ValueError(f"economics.usage.duplicate_lines: {dup_mode!r} (one of {', '.join(DUPLICATE_MODES)})")
    stale_mode = str(p.get("stale_output") or ucfg.get("stale_output") or "keep")
    if stale_mode not in STALE_MODES:
        raise ValueError(f"economics.usage.stale_output: {stale_mode!r} (one of {', '.join(STALE_MODES)})")
    ignore_models = set(ucfg.get("ignore_models") or [])
    attribution = str(p.get("tool_attribution") or cfg.get("tool_attribution") or "split")
    zone = str(p.get("day_zone") or cfg.get("day_zone") or "UTC")
    est_cfg = cfg.get("estimate") or {}
    context_mode = str(p.get("context") or est_cfg.get("context") or "path")
    hidden_models = [str(x) for x in (est_cfg.get("hidden_reasoning_models") or [])]
    calib_min = int(est_cfg.get("calibration_min_output", 1) or 0)
    skip_side = p.get("skip_session_side_files")
    skip_side = bool(cfg.get("skip_session_side_files", True) if skip_side is None else skip_side)
    oc = cfg.get("outcomes") or {}
    edit_tools = set(oc.get("edit_tools") or [])
    path_keys = list(oc.get("path_keys") or [])
    per_turn = p.get("per_turn")
    per_turn = bool(oc.get("per_turn", False) if per_turn is None else per_turn) and not aggregate
    commit_rx = re.compile(str(oc.get("commit_command") or r"git\s+commit\b"), re.M)
    commit_tools = {str(k): [str(x) for x in (v or [])] for k, v in (oc.get("commit_tools") or {"Bash": ["command"]}).items()}
    result_rx = re.compile(str(oc.get("commit_result") or r"^\[(?:[^\]\n]* )?([0-9a-f]{7,40})\]"), re.M)
    need_result = bool(oc.get("trailers_need_result", True))
    git_cfg = dict(cfg.get("git") or {})
    if p.get("main_ref") is not None:
        git_cfg["main_ref"] = p["main_ref"]
    trailer_rx = trailer_regex(git_cfg)
    estimator = Estimator(ds, p.get("estimate"))
    book = PriceBook(ds)
    zone_problem = None
    if zone not in UTC_NAMES:
        try:
            from zoneinfo import ZoneInfo
            ZoneInfo(zone)
        except Exception:  # noqa: BLE001 - an unknown zone is said and the days are counted in UTC
            log.debug("day zone %r unusable", zone, exc_info=True)
            zone_problem, zone = zone, "UTC"

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
            elif isinstance(v, int) and not isinstance(v, bool) and isinstance(e.get(k), int):
                e[k] += v
            else:
                e[k] = v

    # files Claude Code writes inside a session's directory (tool-results/*.txt, workflows/*.json, scripts): their
    # contents are tool outputs already inside the measured context — estimating them as conversations would count
    # them twice (preset economics.skip_session_side_files; false = estimate them too)
    if skip_side:
        prefixes: dict[tuple[str, str], list[str]] = {}
        for u in units:
            mem = str(u.source.get("member") or "")
            if u.kind == "session" and mem.endswith(".jsonl"):
                prefixes.setdefault((str(u.source.get("archive") or ""), str(u.source.get("sha256") or "")), []).append(mem[:-len(".jsonl")] + "/")
        kept: list[Unit] = []
        skipped = 0
        for u in units:
            mem = str(u.source.get("member") or "")
            akey = (str(u.source.get("archive") or ""), str(u.source.get("sha256") or ""))
            if u.kind != "session" and mem and any(mem.startswith(px) for px in prefixes.get(akey, ())):
                skipped += 1
                continue
            kept.append(u)
        if skipped:
            note("side_files_skipped", skipped)
        units = kept

    def commit_text(b: Any) -> Optional[str]:
        """The text of a commit command from the input keys of a commit tool (data: outcomes.commit_tools), from the
        ``git commit`` invocation on — file contents written by Edit/Write never count."""
        keys = commit_tools.get(str(b.name or ""))
        if not keys or not isinstance(b.input, dict):
            return None
        for k in keys:
            v = b.input.get(k)
            if isinstance(v, str):
                mm = commit_rx.search(v)
                if mm:
                    return v[mm.start():]
        return None

    # tool_use id → error (the bound tool_result), and the commit a commit command made (the hash git printed in its
    # result), over every unit (a result may sit in another file)
    errors: dict[str, bool] = {}
    commit_use: dict[str, str] = {}
    commit_hash: dict[str, str] = {}  # tool_use id → hash ("" = a result that shows no commit)
    for u in units:
        for m in u.messages:
            for b in m.blocks:
                if b.kind == "tool_use" and b.extra.get("id"):
                    txt = commit_text(b)
                    if txt is not None:
                        commit_use[str(b.extra["id"])] = txt
    for u in units:
        for m in u.messages:
            for b in m.blocks:
                if b.kind == "tool_result":
                    tid = b.extra.get("tool_use_id") or b.name
                    if tid:
                        errors[str(tid)] = bool(isinstance(b.input, dict) and b.input.get("is_error"))
                        if str(tid) in commit_use:
                            mm = None if errors[str(tid)] else result_rx.search(b.text or "")
                            commit_hash[str(tid)] = mm.group(1) if mm else ""

    price_cache: dict[tuple[Optional[str], Optional[str]], dict[str, Any]] = {}

    def price(model: Optional[str], day: Optional[str]) -> dict[str, Any]:
        k = (model, day)
        if k not in price_cache:
            price_cache[k] = book.lookup(model, day)
        return price_cache[k]

    # -- measured turns (one per API response) -------------------------------------------------------------------
    turns: list[dict[str, Any]] = []
    groups: dict[str, dict[str, Any]] = {}
    calib: dict[str, list[tuple[int, int]]] = {}
    calib_on: dict[str, dict[str, int]] = {}
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
                     "commit_cmds": 0, "commits_confirmed": 0, "commit_refs": [], "line_cost": m.meta.get("cost_usd"), "texts": [],
                     "thinking": False, "lang": lang, "seen_tools": set(), "final": False, "final_checked": False}
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
            if "usage_final" in m.meta:
                g["final_checked"] = True
                g["final"] = g["final"] or bool(m.meta["usage_final"])
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
                txt = commit_use.get(str(tid)) if tid else commit_text(b)
                if txt is not None:
                    g["commit_cmds"] += 1
                    h = commit_hash.get(str(tid)) if tid else None
                    if h:
                        g["commits_confirmed"] += 1
                    if h or (not need_result and h is None):
                        # (hash or None, trailers of the command): the repository's commit decides when the hash resolves
                        g["commit_refs"].append((h or None, trailer_rx.findall(txt)))
                    else:
                        note("commit_unconfirmed")
    stale = {"n": 0, "tokens": 0, "estimated": 0}
    for g in turns:
        g["day"] = _day(g["t"], zone)
        g["method"] = "measured"
        if g["final_checked"] and not g["final"]:
            # no line of this response carries stop_reason: the counters are the stream-start snapshot — output is a
            # lower bound (economics.usage.stale_output: keep), or the larger of it and an estimate from the content
            stale["n"] += 1
            stale["tokens"] += g["usage"]["output"]
            if stale_mode == "estimate":
                fn_s, how_s = estimator.counter(g["model"], g["lang"])
                est = max(fn_s("\n".join(g["texts"])), g["usage"]["output"])
                estimator.used[how_s] = estimator.used.get(how_s, 0) + 1
                stale["estimated"] += est
                g["usage"]["output"] = est
                g["output_basis"], g["output_how"] = "estimated", how_s
            else:
                g["output_basis"] = "lower_bound"
        elif not g["final_checked"]:
            g["output_basis"] = "final_unchecked"
            note("output_final_unchecked")
        else:
            g["output_basis"] = "final"
        hit = price(g["model"], g["day"])
        if g.get("line_cost") is not None:
            g["cost"] = {"amount": _r6(float(g["line_cost"])), "known": True, "currency": "USD", "basis": "measured_line_cost"}
        else:
            c = cost_of(g["usage"], hit)
            c["basis"] = "prices×measured+estimated_output" if g["output_basis"] == "estimated" else "prices×measured"
            g["cost"] = c
        if g["output_basis"] == "final" and not g["thinking"] and not g["usage"]["output_thinking"] and g["usage"]["output"] > 0 \
                and calib_min <= g["usage"]["output"]:
            fn_c, how_c = estimator.counter(g["model"], g["lang"])  # not counted in "used": calibration, not an estimate
            calib.setdefault(how_c, []).append((fn_c("\n".join(g["texts"])), g["usage"]["output"]))
            on = calib_on.setdefault(how_c, {})
            prov = _provider(g["unit"])
            on[prov] = on.get(prov, 0) + 1
    if stale["n"]:
        if stale_mode == "keep":
            note("output_lower_bound", stale["n"], tokens=stale["tokens"])
        else:
            note("output_estimated", stale["n"], tokens=stale["tokens"], tokens_estimated=stale["estimated"])

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
            on_chain: set[str] = set()
            cur: Optional[Any] = m
            while cur is not None and cur.id not in cum and cur.id not in on_chain:  # a parent cycle ends the walk
                chain.append(cur)
                on_chain.add(cur.id)
                cur = by.get(cur.parent) if cur.parent in by else None
            for x in reversed(chain):
                par = by.get(x.parent) if x.parent in by and x.parent in cum else None
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
                 "commits_confirmed": 0, "commit_refs": [], "method": how, "day": _day(m.created_utc, zone),
                 "hidden_reasoning": _matches(model, hidden_models)}
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
            if g["hidden_reasoning"]:
                note("estimated_hidden_reasoning", models={str(model): 1})

    # -- session lists, links, git -------------------------------------------------------------------------------
    api_sessions, problems = load_session_lists([str(x) for x in (p.get("sessions") or [])], ds, p.get("session_source"))
    for pr in problems:
        note(pr["key"], pr.get("n", 1))
    if zone_problem:
        note("day_zone_unknown", zone=zone_problem)
    commits: dict[str, dict[str, Any]] = {}
    main_sets: list[Optional[set[str]]] = []
    main_reasons: list[str] = []
    repos_given = list(p.get("repos") or [])
    repos_ok = 0
    lines_known = True
    shallow = 0
    main_used: list[str] = []
    tried: list[str] = []
    for i, repo in enumerate(repos_given):
        res = git_commits(str(repo), git_cfg)
        if res["error"]:
            note("git_error", repos={f"repo-{i + 1}": 1}, error=res["error"].split(":")[0] if aggregate else res["error"])
            continue
        repos_ok += 1
        if res["partial_clone"]:
            lines_known = False
            note("git_partial_clone")
        if res["shallow"]:
            shallow += 1
            note("git_shallow")
        main_sets.append(res["main"])
        tried = res.get("main_refs_tried") or tried
        if res["main_reason"]:
            main_reasons.append(res["main_reason"])
        elif res.get("main_ref"):
            main_used.append(res["main_ref"])
        for c in res["commits"]:
            prev = commits.get(c["hash"])
            if prev is None:
                commits[c["hash"]] = {**c, "on_main": bool(res["main"] is not None and c["hash"] in res["main"])}
            elif res["main"] is not None and c["hash"] in res["main"]:
                prev["on_main"] = True
    git_known = repos_ok > 0  # commits are counted only from repositories git read; none read = unknown, never 0
    git_why = "commits_unknown" if not repos_given else ("git_error" if not git_known else "commits_unknown")
    commits_lower = bool(git_known and (repos_ok < len(repos_given) or shallow))
    if git_known and repos_ok < len(repos_given):
        note("commits_lower_bound", len(repos_given) - repos_ok)
    main_known = bool(main_sets) and all(s is not None for s in main_sets)
    if main_reasons:
        note("main_ref_unknown", reason=main_reasons[0], refs=", ".join(tried) if tried else str(git_cfg.get("main_ref") or ""))

    # a commit a transcript made (hash from the tool result) → the sessions of the repository's commit
    by_prefix: dict[str, list[str]] = {}
    for h in commits:
        by_prefix.setdefault(h[:7], []).append(h)

    def resolve(h: str) -> Optional[dict[str, Any]]:
        hits = [x for x in by_prefix.get(h[:7], []) if x.startswith(h)]
        return commits[hits[0]] if len(hits) == 1 else None

    # families (a main session + its subagents and workflow agents) and their links to API sessions
    fam_units: dict[str, list[Unit]] = {}
    for u in units:
        pl = _place(u)
        if pl["family"]:
            fam_units.setdefault(pl["family"], []).append(u)
    explicit = {str(k): str(v) for k, v in (p.get("links") or {}).items()}
    use_trailers = bool((cfg.get("links") or {}).get("trailer_in_transcripts", True))
    fam_trailers: dict[str, dict[str, int]] = {}
    fam_via_repo: dict[str, bool] = {}
    for g in turns:
        fam = g["place"]["family"]
        if not fam:
            continue
        for h, sids in g["commit_refs"]:
            c = resolve(h) if h else None
            if c is not None:
                sids = c["sessions"]
                fam_via_repo[fam] = fam_via_repo.get(fam, True)
            else:
                fam_via_repo[fam] = False
            d = fam_trailers.setdefault(fam, {})
            for sid in sids:
                d[sid] = d.get(sid, 0) + 1
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
            if len(tr) > 1:
                links[fam] = (best, "trailer_ambiguous")
                note("link_ambiguous")
            else:
                links[fam] = (best, "commit" if fam_via_repo.get(fam) else "trailer")
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
        o = run_out.setdefault(u.uid, {"files": set(), "edits": 0, "edits_failed": 0, "commit_cmds": 0})
        o["files"] |= g["files"]
        o["edits"] += g["edits"]
        o["edits_failed"] += g["edits_failed"]
        o["commit_cmds"] += g["commit_cmds"]
    by_uid = {u.uid: u for u in units}
    fam_turns: dict[str, list[dict[str, Any]]] = {}
    for g in turns:
        fam = g["place"]["family"]
        if fam:
            fam_turns.setdefault(fam, []).append(g)

    def first_key(a: Optional[Acc], fallback: str) -> tuple[str, str]:
        return (ts.key(a.first) if a is not None and a.first else "~", fallback)

    # session rows: one per API session id (every family linked to it summed into it), then families linked to an
    # id that is not on the list (grouped by that id), then families without a link, then commit-only sessions
    commit_sessions: dict[str, list[dict[str, Any]]] = {}
    no_trailer = 0
    for c in commits.values():
        if not c["sessions"]:
            no_trailer += 1
        for sid in c["sessions"]:
            commit_sessions.setdefault(sid, []).append(c)
    row_fams: dict[str, list[str]] = {}
    row_sid: dict[str, Optional[str]] = {}
    for sid in api_sessions:
        row_fams[f"api:{sid}"], row_sid[f"api:{sid}"] = [], sid
    for fam in sorted(fam_turns):
        if fam in links:
            sid = links[fam][0]
            rk = f"api:{sid}" if sid in api_sessions else f"sid:{sid}"
            row_fams.setdefault(rk, []).append(fam)
            row_sid[rk] = sid
        else:
            row_fams[f"fam:{fam}"], row_sid[f"fam:{fam}"] = [fam], None
    known_sids = {s for s in row_sid.values() if s}
    for sid in commit_sessions:
        if sid not in known_sids:
            row_fams[f"git:{sid}"], row_sid[f"git:{sid}"] = [], sid
            note("commits_session_without_rows")
    multi = sum(len(f) - 1 for f in row_fams.values() if len(f) > 1)
    if multi:
        note("several_transcripts_one_session", multi)
    row_acc: dict[str, Optional[Acc]] = {}
    row_main: dict[str, Acc] = {}
    for rk, fams in row_fams.items():
        if not fams:
            row_acc[rk] = None
            continue
        a, am = Acc(), Acc()
        for fam in fams:
            for g in fam_turns.get(fam, []):
                a.add(g)
                if g["place"]["role"] == "main":
                    am.add(g)
        row_acc[rk], row_main[rk] = a, am

    def row_time(rk: str) -> tuple[str, str]:
        sid = row_sid.get(rk)
        api = api_sessions.get(sid) if sid else None
        if api and api.get("created"):
            return (ts.key(str(api["created"])), rk)
        return first_key(row_acc.get(rk), rk)
    keys = sorted(row_fams, key=row_time)
    s_labels = label_for("session", keys)
    fam_label: dict[str, str] = {}
    sessions: list[dict[str, Any]] = []
    for rk in keys:
        sid, fams = row_sid.get(rk), row_fams[rk]
        api = api_sessions.get(sid) if sid else None
        a = row_acc.get(rk)
        fam0 = fams[0] if fams else None
        row: dict[str, Any] = {"label": s_labels[rk] if aggregate else ((api or {}).get("title") or sid or fam0 or rk),
                               "id": None if aggregate else (sid or fam0),
                               "transcript_id": None if aggregate else (fam0 if len(fams) <= 1 else ", ".join(fams)),
                               "transcript_ids": None if aggregate else list(fams), "why": {}, "qual": {}}
        why, qual = row["why"], row["qual"]
        for fam in fams:
            fam_label[fam] = row["label"]
        if api:
            row.update({"model": api.get("model"), "api_input": api.get("input"), "api_output": api.get("output"),
                        "api_cache_creation": api.get("cache_creation"), "api_cache_read": api.get("cache_read"),
                        "api_missing": api.get("missing")})
            row["cost_api"] = ({"amount": _r6(api["cost_usd"]), "known": True, "currency": api.get("currency", "USD"), "basis": "session_api"}
                               if api.get("cost_usd") is not None else {"amount": None, "known": False, "reason": "no_cost_field", "currency": "USD"})
            row["period"] = f"{str(api.get('created') or '?')[:10]} … {str(api.get('updated') or '?')[:10]}"
            row["repos_raw"] = list(api.get("repos") or []) if "repos" in api else None
        else:
            no_api = "no_session_list" if not api_sessions else "not_in_session_list"
            row.update({"model": None, "api_input": None, "api_output": None, "api_cache_creation": None, "api_cache_read": None,
                        "cost_api": {"amount": None, "known": False, "reason": no_api, "currency": "USD"}, "repos_raw": None})
            why.update({k: no_api for k in ("model", "api_input", "api_output", "api_cache_creation", "api_cache_read", "coverage",
                                            "coverage_output", "coverage_cache_creation", "repos")})
            row["period"] = f"{(a.first or '?')[:10]} … {(a.last or '?')[:10]}" if a else "?"
        if a is not None:
            r = a.row(grand)
            members = [x for fam in fams for x in fam_units.get(fam, [])]
            files: set[str] = set()
            edits = edits_failed = cmds = 0
            for fam in fams:
                for g in fam_turns.get(fam, []):
                    files |= g["files"]
                    edits += g["edits"]
                    edits_failed += g["edits_failed"]
                    cmds += g["commit_cmds"]
            how_set = sorted({links[f][1] for f in fams if f in links})
            row.update({"tr_turns": r["turns"], "tr_input": r["input"], "tr_output": r["output"], "tr_cache_creation": r["cache_creation"],
                        "tr_cache_read": r["cache_read"], "tr_total": r["total"], "tr_cost": r["cost"],
                        "tr_cost_measured": r["cost_measured"], "tr_cost_estimated": r["cost_estimated"],
                        "tr_output_lower_bound": r["output_lower_bound"],
                        "transcripts": len(members), "agents": sum(1 for x in members if _place(x)["role"] != "main"),
                        "link": ", ".join(t("economics.link." + h) for h in how_set) if how_set else t("economics.link.none"),
                        "files_touched": len(files), "edit_calls": edits, "edits_failed": edits_failed, "commit_commands": cmds})
            if not aggregate:
                row["files"] = sorted(files)
            for k in ("output", "total"):
                if r["qual"].get(k):
                    qual["tr_" + k] = r["qual"][k]
            mt = row_main[rk].total() if rk in row_main else 0
            row["main_share"] = round(mt / r["total"], 4) if r["total"] else None
            if not row.get("model"):
                row["model"] = r["models"]
            for col, kind in (("coverage", "cache_read"), ("coverage_output", "output"), ("coverage_cache_creation", "cache_creation")):
                if api and api.get(kind) is not None and r[kind] is not None:
                    if api[kind] == 0:
                        row[col] = None
                        why[col] = "api_zero"
                    else:
                        row[col] = round(r[kind] / api[kind], 4)
                        if kind == "output" and r["qual"].get("output"):
                            qual[col] = r["qual"]["output"]
                else:
                    row[col] = None
                    why.setdefault(col, "no_field_in_session_list" if api else ("no_session_list" if not api_sessions else "not_in_session_list"))
            for k in ("api_input", "api_output", "api_cache_creation", "api_cache_read"):
                if api and row.get(k) is None:
                    why[k] = "no_field_in_session_list"
        else:
            for k in ("tr_turns", "tr_input", "tr_output", "tr_cache_creation", "tr_cache_read", "tr_total", "main_share", "coverage",
                      "coverage_output", "coverage_cache_creation", "files_touched", "edit_calls", "edits_failed", "commit_commands"):
                row[k] = None
                why[k] = "no_transcript"
            if api is not None:
                for k in ("api_input", "api_output", "api_cache_creation", "api_cache_read"):
                    if row.get(k) is None:
                        why[k] = "no_field_in_session_list"
            for k in ("tr_cost", "tr_cost_measured", "tr_cost_estimated"):
                row[k] = {"amount": None, "known": False, "reason": "no_transcript", "currency": "USD"}
            row["transcripts"], row["agents"] = 0, 0
            row["link"] = t("economics.link.none")
            if api is not None and sid not in linked_api:
                note("api_sessions_without_transcript")
        cs = commit_sessions.get(sid, []) if sid else []
        if git_known:
            row["commits"] = len(cs)
            row["commits_main"] = sum(1 for c in cs if c["on_main"]) if main_known else None
            if lines_known:
                row["lines_added"] = sum(c["added"] or 0 for c in cs)
                row["lines_removed"] = sum(c["removed"] or 0 for c in cs)
            else:
                row["lines_added"] = row["lines_removed"] = None
                why["lines_added"] = why["lines_removed"] = "partial_clone"
            if commits_lower:
                for k in ("commits", "commits_main"):
                    qual[k] = {"code": "commits_lower_bound"}
        else:
            row["commits"] = row["commits_main"] = row["lines_added"] = row["lines_removed"] = None
            for k in ("commits", "commits_main", "lines_added", "lines_removed"):
                why[k] = git_why
        if row["commits"] is not None and row["commits_main"] is None:
            why["commits_main"] = "main_ref_unknown"
        best_cost = row["cost_api"] if row["cost_api"].get("known") else row.get("tr_cost") or row["cost_api"]
        row["cost_per_commit"] = _div_cost(best_cost, row["commits"], "no_commits", git_why)
        row["cost_per_commit_main"] = _div_cost(best_cost, row["commits_main"], "no_commits_on_main", git_why) \
            if main_known or not git_known else {"amount": None, "known": False, "reason": "main_ref_unknown", "currency": "USD"}
        sessions.append(row)
    sid_label = {row_sid[rk]: s_labels[rk] if aggregate else ((api_sessions.get(row_sid[rk]) or {}).get("title") or row_sid[rk])
                 for rk in keys if row_sid.get(rk)}
    for rk, row in zip(keys, sessions):
        sid = row_sid.get(rk)
        par = (api_sessions.get(sid) or {}).get("parent") if sid else None
        row["parent"] = (sid_label.get(par) or (None if aggregate else par)) if par else t("economics.na")
        if par and par not in sid_label:
            row["why"]["parent"] = "not_in_session_list"
    for fam in fam_turns:
        if fam not in links:
            note("transcripts_without_session_list" if api_sessions else "transcripts_unlinked")

    # repositories (from the session list's sources): the S1 headline totals per project
    repo_rows: list[dict[str, Any]] = []
    repo_lbl: dict[str, str] = {}
    if api_sessions:
        by_repo: dict[str, list[dict[str, Any]]] = {}
        multi_repo = 0
        for rk, row in zip(keys, sessions):
            if not rk.startswith("api:"):
                continue
            rl = row.get("repos_raw")
            if rl and len(rl) > 1:
                multi_repo += 1
            for repo in (rl or ["\x00none"]):
                by_repo.setdefault(repo, []).append(row)
        if multi_repo:
            note("session_in_several_repos", multi_repo)
        repo_lbl = label_for("repo", sorted(k for k in by_repo if k != "\x00none"))
        for repo in sorted(by_repo, key=lambda k: (k == "\x00none", k)):
            rows = by_repo[repo]
            costs = [r["cost_api"] for r in rows]
            known = [c for c in costs if c.get("known")]
            curs = {c.get("currency", "USD") for c in known}
            if len(known) == len(costs) and len(curs) <= 1:
                cost = {"amount": _r6(sum(float(c["amount"]) for c in known)), "known": True, "currency": next(iter(curs), "USD"),
                        "basis": "session_api"}
            else:
                cost = {"amount": None, "known": False, "reason": "mixed_currency" if len(curs) > 1 else "no_cost_field", "currency": "USD",
                        "partial": _r6(sum(float(c["amount"]) for c in known)) if known and len(curs) <= 1 else None}
            rr: dict[str, Any] = {"repo": t("economics.repo_none") if repo == "\x00none" else (repo_lbl[repo] if aggregate else repo),
                                  "sessions": len(rows), "cost_api": cost, "why": {}, "qual": {}}
            for k in ("api_input", "api_output", "api_cache_creation", "api_cache_read"):
                vals = [r.get(k) for r in rows]
                rr[k] = sum(v for v in vals if v is not None) if all(v is not None for v in vals) else None
                if rr[k] is None:
                    rr["why"][k] = "no_field_in_session_list"
            if git_known:
                rr["commits"] = sum(r["commits"] or 0 for r in rows)
                rr["commits_main"] = sum(r["commits_main"] or 0 for r in rows) if main_known else None
                if not main_known:
                    rr["why"]["commits_main"] = "main_ref_unknown"
                rr["cost_per_commit"] = _per_commit(rows)
                if commits_lower:
                    rr["qual"]["commits"] = rr["qual"]["commits_main"] = {"code": "commits_lower_bound"}
            else:
                rr["commits"] = rr["commits_main"] = None
                rr["why"]["commits"] = rr["why"]["commits_main"] = git_why
                rr["cost_per_commit"] = {"amount": None, "known": False, "reason": git_why, "currency": "USD"}
            repo_rows.append(rr)
    for row in sessions:
        rl = row.pop("repos_raw", None)
        if rl is None:
            row["repos"] = None
            row["why"].setdefault("repos", "no_session_list" if not api_sessions else "no_field_in_session_list")
        else:
            row["repos"] = ", ".join((repo_lbl.get(x) or "?") if aggregate else x for x in rl) or t("economics.repo_none")

    # runs
    run_keys = sorted(run_acc, key=lambda uid: (first_key(run_acc[uid], uid)))
    r_labels = label_for("run", run_keys)
    runs: list[dict[str, Any]] = []
    wf_acc: dict[tuple[str, str], Acc] = {}
    wf_agents: dict[tuple[str, str], int] = {}
    wf_meta: dict[tuple[str, str], dict[str, Any]] = {}
    for uid in run_keys:
        u = by_uid[uid]
        pl = _place(u)
        r = run_acc[uid].row(grand)
        am = u.flags.get("agent_meta") or {}
        row = {**r, "label": r_labels[uid] if aggregate else (am.get("description") or u.title or u.ext_id or uid),
               "id": None if aggregate else u.ext_id,
               "session": ((fam_label.get(pl["family"]) or (None if aggregate else pl["family"])) if pl["family"] else t("economics.na")),
               "role": t("economics.role." + pl["role"]), "provider": _provider(u),
               "agent_type": ("•" if aggregate else am.get("agent_type")) if am.get("agent_type") else None,
               "workflow_run": (pl["run"] if not aggregate else "•") if pl["run"] else t("economics.na"),
               "files_touched": len(run_out[uid]["files"]), "edit_calls": run_out[uid]["edits"],
               "edits_failed": run_out[uid]["edits_failed"], "commit_commands": run_out[uid]["commit_cmds"]}
        if row["agent_type"] is None:
            row["why"]["agent_type"] = "no_agent_meta" if u.kind == "session" and pl["role"] != "main" else "not_an_agent"
        if not aggregate:
            row["files"] = sorted(run_out[uid]["files"])
        runs.append(row)
        if pl["run"] and pl["family"]:
            k = (pl["family"], str(pl["run"]))
            wf_agents[k] = wf_agents.get(k, 0) + 1
            if u.flags.get("workflow_meta") and k not in wf_meta:
                wf_meta[k] = u.flags["workflow_meta"]
    for g in turns:
        pl = g["place"]
        if pl["run"] and pl["family"]:
            wf_acc.setdefault((pl["family"], str(pl["run"])), Acc()).add(g)
    wf_keys = sorted(wf_acc, key=lambda k: first_key(wf_acc[k], "/".join(k)))
    w_labels = label_for("workflow", ["/".join(k) for k in wf_keys])
    if aggregate:  # the run column of the runs table names the workflow run by its label too
        wl = {k: w_labels["/".join(k)] for k in wf_keys}
        for row, uid in zip(runs, run_keys):
            pl = _place(by_uid[uid])
            if pl["run"] and pl["family"]:
                row["workflow_run"] = wl.get((pl["family"], str(pl["run"])))
    workflow_runs = []
    for k in wf_keys:
        meta = wf_meta.get(k) or {}
        wr = {**wf_acc[k].row(grand), "label": w_labels["/".join(k)] if aggregate else (meta.get("name") or k[1]),
              "session": fam_label.get(k[0]) or (None if aggregate else k[0]), "agents": wf_agents.get(k, 0)}
        for col, key in (("file_agents", "agents"), ("file_total_tokens", "total_tokens"), ("file_tool_calls", "tool_calls")):
            v = meta.get(key)
            wr[col] = int(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else None
            if wr[col] is None:
                wr["why"][col] = "no_workflow_file" if not meta else "no_field_in_workflow_file"
        workflow_runs.append(wr)

    # models, tools, days, providers
    model_acc: dict[str, Acc] = {}
    for g in turns:
        model_acc.setdefault(g["model"] or "?", Acc()).add(g)
    models = []
    for mk in sorted(model_acc, key=lambda k: (-model_acc[k].turns, k)):
        hit = price(None if mk == "?" else mk, None)
        models.append({**model_acc[mk].row(grand), "model": mk if mk != "?" else t("economics.model_unknown"),
                       "price": (f"{hit.get('pattern')} · {hit.get('source') or '?'} · {hit.get('valid_from') or '?'}" if hit.get("known")
                                 else t("economics.unknown_cost", why=t("economics.why." + str(hit.get("reason") or "no_entry"))))})
    tool_acc: dict[str, Acc] = {}
    tool_calls: dict[str, int] = {}
    tool_failed: dict[str, int] = {}
    tool_turns: dict[str, list[int]] = {}  # name → [turns measured, turns estimated] with that tool
    none_key = "\x00none"
    for g in turns:
        calls = g["tool_ids"]
        if not calls:
            tool_acc.setdefault(none_key, Acc()).add(g)
            continue
        n = len(calls)
        shares = _split_ints(g["usage"], n) if attribution == "split" else None
        seen: set[str] = set()
        for i, (name, failed) in enumerate(calls):
            if shares is not None:
                tool_acc.setdefault(name, Acc()).add(g, 1.0 / n, shares[i])
            else:
                tool_acc.setdefault(name, Acc()).add(g, 1.0)
            tool_calls[name] = tool_calls.get(name, 0) + 1
            if failed:
                tool_failed[name] = tool_failed.get(name, 0) + 1
            if name not in seen:
                tt = tool_turns.setdefault(name, [0, 0])
                tt[0 if g["measured"] else 1] += 1
                seen.add(name)
    tools = []
    for name in sorted(tool_acc, key=lambda k: (k == none_key, -tool_acc[k].total(), k)):
        r = tool_acc[name].row(grand)
        if name != none_key:
            tm, te = tool_turns.get(name, [0, 0])
            r["turns"], r["turns_measured"], r["turns_estimated"] = tm + te, tm, te
        tools.append({**r, "tool": t("economics.no_tool") if name == none_key else name,
                      "calls": tool_calls.get(name, 0) if name != none_key else 0, "failed": tool_failed.get(name, 0)})
    if attribution != "split":
        note("tool_attribution_full")
    day_acc: dict[str, Acc] = {}
    for g in turns:
        day_acc.setdefault(g["day"] or "?", Acc()).add(g)
    day_commits: dict[str, list[int]] = {}  # day → [with a session trailer, all]
    for c in commits.values():
        d = (_day(c["date"], zone) if c["date"] else None) or "?"
        dc = day_commits.setdefault(d, [0, 0])
        dc[1] += 1
        if c["sessions"]:
            dc[0] += 1
    days = []
    for d in sorted(set(day_acc) | set(day_commits)):
        r = day_acc[d].row(grand) if d in day_acc else Acc().row(grand)
        dc = day_commits.get(d, [0, 0])
        row = {**r, "day": d if d != "?" else t("economics.day_unknown"),
               "commits": dc[0] if git_known else None, "commits_all": dc[1] if git_known else None}
        if not git_known:
            row["why"]["commits"] = row["why"]["commits_all"] = git_why
        elif commits_lower:
            row["qual"]["commits"] = row["qual"]["commits_all"] = {"code": "commits_lower_bound"}
        days.append(row)
    prov_units: dict[tuple[str, str], list[Unit]] = {}
    for u in units:
        prov_units.setdefault((_provider(u), str(u.source.get("format") or "?")), []).append(u)
    prov_acc: dict[tuple[str, str], Acc] = {}
    for g in turns:
        u = g["unit"]
        prov_acc.setdefault((_provider(u), str(u.source.get("format") or "?")), Acc()).add(g)
    thinking_min = int(ds.preset("thinking.full_min_chars", 200))
    reasoning_share = float(ds.preset("thinking.reasoning_share", 0.5))
    providers = []
    for k in sorted(prov_units):
        ef = economics_fields(prov_units[k], thinking_min, reasoning_share)
        r = (prov_acc[k] if k in prov_acc else Acc()).row(grand)
        rb = ef["reasoning_blocks"]
        counts = ", ".join(f"{t('economics.reasoning.' + c)} {_fmt_int(rb[c])}" for c in ("full", "summary", "short", "empty", "redacted")
                           if rb.get(c))
        providers.append({**r, "provider": k[0], "format": k[1], "units": len(prov_units[k]), "has_usage": ef["has_usage"],
                          "has_tool_traces": ef["has_tool_traces"], "reasoning": t("economics.reasoning." + ef["reasoning"]),
                          "reasoning_blocks": t("economics.reasoning_counts", counts=counts, n=_fmt_int(rb["blocks"])) if rb.get("blocks")
                          else t("economics.reasoning.none"), "inventory": ef})

    # totals and outcomes
    tot_row = total.row(grand)
    n_commits = sum(1 for c in commits.values() if c["sessions"])
    tot_row.update({"label": t("economics.label.total"),
                    "commits": n_commits if git_known else None, "commits_all": len(commits) if git_known else None,
                    "commits_main": (sum(1 for c in commits.values() if c["sessions"] and c["on_main"]) if main_known else None)
                    if git_known else None})
    if tot_row["commits"] is None:
        tot_row["why"].update({"commits": git_why, "commits_main": git_why, "commits_all": git_why})
    else:
        if tot_row["commits_main"] is None:
            tot_row["why"]["commits_main"] = "main_ref_unknown"
        if commits_lower:
            for k in ("commits", "commits_all", "commits_main"):
                tot_row["qual"][k] = {"code": "commits_lower_bound"}
    api_cost_known = [s["cost_usd"] for s in api_sessions.values() if s.get("cost_usd") is not None]
    if api_sessions:
        tot_row["cost_api"] = ({"amount": _r6(sum(api_cost_known)), "known": True, "currency": "USD", "basis": "session_api"}
                               if len(api_cost_known) == len(api_sessions)
                               else {"amount": None, "known": False, "reason": "no_cost_field", "currency": "USD",
                                     "partial": _r6(sum(api_cost_known)) if api_cost_known else None})
        for k in ("input", "output", "cache_creation", "cache_read"):
            vals = [s.get(k) for s in api_sessions.values()]
            tot_row["api_" + k] = sum(v for v in vals if v is not None) if all(v is not None for v in vals) else None
            if tot_row["api_" + k] is None:
                tot_row["why"]["api_" + k] = "no_field_in_session_list"
    else:
        tot_row["cost_api"] = {"amount": None, "known": False, "reason": "no_session_list", "currency": "USD"}
        for k in ("input", "output", "cache_creation", "cache_read"):
            tot_row["api_" + k] = None
            tot_row["why"]["api_" + k] = "no_session_list"
    # cost per commit over the sessions that have commits (a session list may hold other projects' sessions too)
    if tot_row["commits"] is None:
        tot_row["cost_per_commit"] = {"amount": None, "known": False, "reason": git_why, "currency": "USD"}
    else:
        tot_row["cost_per_commit"] = _per_commit(sessions)
    outcomes = {"repos": len(repos_given), "repos_read": repos_ok, "commits_all": len(commits) if git_known else None,
                "commits_with_session": n_commits if git_known else None, "commits_without_session": no_trailer if git_known else None,
                "main_ref": git_cfg.get("main_ref"), "main_ref_used": sorted(set(main_used)), "main_known": main_known,
                "lines_known": bool(git_known and lines_known), "shallow_repos": shallow,
                "lines_added": sum(c["added"] or 0 for c in commits.values() if c["sessions"]) if git_known and lines_known else None,
                "lines_removed": sum(c["removed"] or 0 for c in commits.values() if c["sessions"]) if git_known and lines_known else None,
                "files_touched": len(set().union(*[g["files"] for g in turns])) if turns else 0,
                "edit_calls": sum(g["edits"] for g in turns), "edits_failed": sum(g["edits_failed"] for g in turns),
                "commit_commands_in_transcripts": sum(g["commit_cmds"] for g in turns),
                "commit_commands_confirmed": sum(g["commits_confirmed"] for g in turns)}
    if not aggregate:
        outcomes["commits"] = [{"hash": c["hash"], "date": c["date"], "sessions": c["sessions"], "on_main": c["on_main"],
                                "added": c["added"], "removed": c["removed"], "files": c["files"]} for c in sorted(commits.values(), key=lambda c: c["hash"])]
        outcomes["files"] = sorted(set().union(*[g["files"] for g in turns])) if turns else []
    if total.cost_unknown["measured"] or total.cost_unknown["estimated"]:
        reasons: dict[str, int] = {}
        for b in BASES:
            for rr_, n in total.cost_reasons[b].items():
                reasons[rr_] = reasons.get(rr_, 0) + n
        note("cost_unknown_turns", int(round(sum(total.cost_unknown.values()))), reasons=reasons)
    if len({cur for b in BASES for cur in total.cost_sum[b]}) > 1:
        note("mixed_currency")
    expected: dict[str, Any] = {}
    for how in sorted(set(calib) | set(estimator.used)):
        cal = calibration(calib.get(how, []))
        expected[how] = ({"basis": "calibration", **cal, "measured_on": dict(sorted(calib_on.get(how, {}).items()))} if cal else
                         {"basis": "none", "reason": "no_calibration_for_method" if calib else "no_calibration"})
    method = {"measured": "message.usage (format file: usage)", "duplicate_lines": dup_mode, "stale_output": stale_mode,
              "stale_output_turns": stale["n"], "tool_attribution": attribution, "day_zone": zone, "context": context_mode,
              "estimator": estimator.describe(), "expected_error": expected, "calibration_min_output": calib_min,
              "hidden_reasoning_models": hidden_models, "expected_error_how": t("economics.how.calibration")}
    unknowns = [notes[k] for k in sorted(notes)]
    prices_used = []
    for (mk, day), hit in sorted(price_cache.items(), key=lambda kv: (str(kv[0][0]), str(kv[0][1]))):
        if day is None:
            continue
        prices_used.append({"model": mk, "day": day, "known": bool(hit.get("known")), "source": hit.get("source"),
                            "pattern": hit.get("pattern"), "valid_from": hit.get("valid_from"), "reason": hit.get("reason")})
    settings = {"of": p.get("of") or "all", "sessions_files": len(p.get("sessions") or []), "repos": len(repos_given),
                "links": len(explicit), "duplicate_lines": dup_mode, "stale_output": stale_mode, "tool_attribution": attribution,
                "day_zone": zone, "context": context_mode, "main_ref": git_cfg.get("main_ref"), "estimate": estimator.method,
                "skip_session_side_files": skip_side, "per_turn": per_turn}
    doc = {"schema": SCHEMA, "aggregate": aggregate, "language": language(), "settings": settings, "method": method,
           "totals": tot_row, "providers": providers, "sessions": sessions, "repos": repo_rows, "runs": runs,
           "workflow_runs": workflow_runs, "models": models, "tools": tools, "days": days, "outcomes": outcomes, "unknowns": unknowns,
           "prices": _dedupe_prices(prices_used)}
    if per_turn:
        doc["turn_files"] = [{"run": g["unit"].ext_id, "t": g["t"], "model": g["model"], "files": sorted(g["files"])}
                             for g in turns if g["files"]]
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


def _why_text(code: str, v: Optional[dict[str, Any]] = None, ctx: Optional[dict[str, Any]] = None) -> str:
    """A reason code → its sentence (a missing price names the fields; a missing branch names the refs)."""
    if code == "price_field_missing" and v and v.get("missing"):
        return t("economics.why.price_field_missing_named", fields=", ".join(t("economics.price_field." + str(f)) for f in v["missing"]))
    if code in ("main_ref_unknown", "no_main_ref"):
        return t("economics.why." + code, ref=(ctx or {}).get("main_ref") or "main")
    return t("economics.why." + code)


def _qual_text(q: dict[str, Any]) -> str:
    return t("economics.qual." + str(q.get("code")), n=_fmt_int(int(q.get("n") or 0)), tokens=_fmt_int(int(q.get("tokens") or 0)))


def _basis_text(v: dict[str, Any]) -> str:
    b = v.get("basis")
    if b == "estimated":
        return t("economics.basis.estimated")
    if b == "mixed" and v.get("measured") is not None:
        cur = v.get("currency") or "USD"
        return t("economics.basis.mixed", measured=_fmt_amount(float(v["measured"])) + f" {cur}",
                 estimated=_fmt_amount(float(v["estimated"])) + f" {cur}")
    if b in ("session_api", "transcript_prices", "mixed_sources") and v.get("per_commit"):
        return t("economics.basis." + b)
    return ""


def cell(kind: str, v: Any, *, csv_mode: bool = False, why: Optional[str] = None, qual: Optional[dict[str, Any]] = None,
         ctx: Optional[dict[str, Any]] = None) -> str:
    """One Markdown cell: a number with its qualifier (a lower bound, partly estimated), a cost with its basis when
    it is not purely measured, or ``unknown (reason)``.  (CSV cells: ``csv_cells``.)"""
    if kind == "cost":
        if not isinstance(v, dict):
            return t("economics.unknown_cost", why=t("economics.why.no_column"))
        cur = v.get("currency") or "USD"
        if v.get("known"):
            amt = v["amount"]
            bt = _basis_text(v)
            return (f"{amt}" if csv_mode else _fmt_amount(float(amt))) + f" {cur}" + (f" ({bt})" if bt else "")
        code = str(v.get("reason") or "no_price")
        wt = _why_text(code, v, ctx)
        if code == "mixed_currency" and v.get("partial_by_currency"):
            return t("economics.unknown_cost", why=wt + ": " + ", ".join(f"{_fmt_amount(float(a))} {c}" for c, a in v["partial_by_currency"].items()))
        if v.get("partial"):
            part = f"{v['partial']}" if csv_mode else _fmt_amount(float(v["partial"]))
            return t("economics.unknown_cost_partial", why=wt, partial=f"{part} {cur}",
                     n=_fmt_int(int(v["unknown_turns"])) if v.get("unknown_turns") is not None else "?")
        return t("economics.unknown_cost", why=wt)
    if v is None:
        return t("economics.unknown", why=_why_text(why or "not_measured", None, ctx))
    if kind == "int":
        s = str(int(v)) if csv_mode else _fmt_int(int(v))
    elif kind == "share":
        s = f"{float(v):.4f}" if csv_mode else f"{float(v) * 100:.1f}".replace(".", t("economics.num.decimal")) + " %"
    elif kind == "bool":
        return t("report.yes") if v else t("report.no")
    else:
        s = str(v)
    if qual and not csv_mode:
        s += f" ({_qual_text(qual)})"
    return s


def csv_header(tab: dict[str, Any], cols: dict[str, Any]) -> list[str]:
    """CSV columns of a table: a number column is followed by ``<col>_why`` (the reason code when it is unknown, the
    qualifier code when it is a bound); a cost column is ``<col>_amount``, ``_currency``, ``_basis``, ``_why`` — so a
    spreadsheet reads numbers as numbers.  Sentences stay in the Markdown."""
    out: list[str] = []
    for c in tab["columns"]:
        kind = (cols.get(c) or {}).get("kind", "text")
        if kind == "cost":
            out += [f"{c}_amount", f"{c}_currency", f"{c}_basis", f"{c}_why"]
        elif kind in ("int", "share"):
            out += [c, f"{c}_why"]
        else:
            out.append(c)
    return out


def csv_cells(kind: str, v: Any, present: bool, why: Optional[str], qual: Optional[dict[str, Any]]) -> list[str]:
    if kind == "cost":
        if not present or not isinstance(v, dict):
            return ["", "", "", "no_column"]
        if v.get("known"):
            return [f"{v['amount']}", str(v.get("currency") or "USD"), str(v.get("basis") or ""), ""]
        return ["", str(v.get("currency") or ""), str(v.get("basis") or ""), str(v.get("reason") or "no_price")]
    if kind in ("int", "share"):
        if not present:
            return ["", "no_column"]
        if v is None:
            return ["", why or "not_measured"]
        num = str(int(v)) if kind == "int" else f"{float(v):.4f}"
        return [num, str((qual or {}).get("code") or "")]
    if not present or v is None:
        return [""]
    if kind == "bool":
        return ["true" if v else "false"]
    return [str(v)]


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


def _cell_ctx(doc: dict[str, Any]) -> dict[str, Any]:
    ref = (doc.get("outcomes") or {}).get("main_ref")
    return {"main_ref": ", ".join(ref) if isinstance(ref, list) else ref}


def _value(r: dict[str, Any], c: str, kind: str) -> Any:
    v = r.get(c)
    if kind == "cost" and isinstance(v, dict) and c.startswith("cost_per_commit"):
        v = {**v, "per_commit": True}
    return v


def render_csv(doc: dict[str, Any], table_id: str, ds: DataStore) -> str:
    lay = layout(ds)
    tab = next((x for x in lay.get("tables") or [] if x["id"] == table_id), None)
    if tab is None:
        raise ValueError(f"economics table {table_id!r} not in {lay.get('_source')}")
    cols = lay.get("columns") or {}
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(csv_header(tab, cols))
    for r in table_rows(doc, tab):
        line: list[str] = []
        for c in tab["columns"]:
            kind = (cols.get(c) or {}).get("kind", "text")
            line += csv_cells(kind, r.get(c), c in r, (r.get("why") or {}).get(c), (r.get("qual") or {}).get(c))
        w.writerow(line)
    return buf.getvalue()


def render_markdown(doc: dict[str, Any], ds: DataStore) -> str:
    lay = layout(ds)
    cols = lay.get("columns") or {}
    max_rows = _max_rows(ds)
    ctx = _cell_ctx(doc)
    out = [f"# {t('economics.title')}", ""]
    if doc.get("aggregate"):
        out += [t("economics.aggregate_note"), ""]
    m = doc.get("method") or {}
    est = m.get("estimator") or {}
    out += [f"## {t('economics.method')}", "",
            "- " + t("economics.method.measured", dup=m.get("duplicate_lines")),
            "- " + t("economics.method.stale_output", how=t("economics.stale." + str(m.get("stale_output") or "keep")),
                     n=_fmt_int(int(m.get("stale_output_turns") or 0))),
            "- " + t("economics.method.estimated", used=", ".join(f"{method_label(k)}: {_fmt_int(v)}" for k, v in (est.get("used") or {}).items()) or "—",
                     context=t("economics.context." + str(m.get("context") or "path")))]
    for how, ee in sorted((m.get("expected_error") or {}).items()):
        if ee.get("basis") == "calibration":
            on = ", ".join(f"{k}: {_fmt_int(v)}" for k, v in (ee.get("measured_on") or {}).items())
            out.append("- " + t("economics.method.error_cal", how=method_label(how), n=_fmt_int(int(ee.get("turns") or 0)), on=on or "—",
                                median=_pct(ee.get("median")), p10=_pct(ee.get("p10")), p90=_pct(ee.get("p90")),
                                min=_fmt_int(int(m.get("calibration_min_output") or 0))))
        else:
            out.append("- " + t("economics.method.error_none", how=method_label(how), why=_why_text(str(ee.get("reason") or "no_calibration"))))
    if m.get("hidden_reasoning_models"):
        out.append("- " + t("economics.method.hidden_reasoning", patterns=", ".join(m["hidden_reasoning_models"])))
    out += ["- " + t("economics.method.tools", how=t("economics.attribution." + str(m.get("tool_attribution")))),
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
            cells = []
            for c in tab["columns"]:
                kind = (cols.get(c) or {}).get("kind", "text")
                if c in r:
                    cells.append(cell(kind, _value(r, c, kind), why=(r.get("why") or {}).get(c), qual=(r.get("qual") or {}).get(c),
                                      ctx=ctx).replace("|", "\\|"))
                else:
                    cells.append(t("economics.unknown", why=t("economics.why.no_column")))
            out.append("| " + " | ".join(cells) + " |")
        if len(rows) > len(shown):
            out.append("")
            out.append(t("report.and_more", n=len(rows) - len(shown), where=f"economics/{tab['id']}.csv"))
        out.append("")
    oc = doc.get("outcomes") or {}

    def num(v: Any) -> str:
        return _fmt_int(int(v)) if isinstance(v, (int, float)) and not isinstance(v, bool) else t("economics.unknown_short")
    out += [f"## {t('economics.outcomes')}", "",
            "- " + t("economics.outcomes.git", repos=num(oc.get("repos", 0)), read=num(oc.get("repos_read", 0)),
                     commits=num(oc.get("commits_with_session")), without=num(oc.get("commits_without_session")), all=num(oc.get("commits_all"))),
            "- " + t("economics.outcomes.files", files=num(oc.get("files_touched", 0)), edits=num(oc.get("edit_calls", 0)),
                     failed=num(oc.get("edits_failed", 0)), commits=num(oc.get("commit_commands_in_transcripts", 0)),
                     confirmed=num(oc.get("commit_commands_confirmed", 0))), ""]
    unk = doc.get("unknowns") or []
    if unk:
        out += [f"## {t('economics.unknowns')}", ""]
        for e in unk:
            parts = []
            for k, v in sorted((k, v) for k, v in e.items() if k not in ("key", "n", "refs")):
                if isinstance(v, dict):
                    parts.append(", ".join(f"{t('economics.why.' + str(kk)) if k == 'reasons' else kk}: "
                                           f"{_fmt_int(vv) if isinstance(vv, int) and not isinstance(vv, bool) else vv}"
                                           for kk, vv in sorted(v.items())))
                elif k in ("reason", "error"):  # a code (": detail") → the sentence of the code
                    code, _, rest = str(v).partition(":")
                    parts.append(_why_text(code, None, {"main_ref": e.get("refs") or ctx.get("main_ref")}) + (f": {rest}" if rest else ""))
                elif isinstance(v, int) and not isinstance(v, bool):
                    parts.append(f"{t('economics.note_field.' + k)}: {_fmt_int(v)}")
                else:
                    parts.append(f"{k}: {v}")
            out.append("- " + t("economics.note." + e["key"], n=_fmt_int(int(e["n"]))) + (" — " + "; ".join(parts) if parts else ""))
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
            "cost": tot.get("cost"), "cost_measured": tot.get("cost_measured"), "cost_estimated": tot.get("cost_estimated"),
            "commits": tot.get("commits"), "sessions": len(doc.get("sessions") or []),
            "runs": len(doc.get("runs") or []), "unknowns": len(doc.get("unknowns") or []),
            "output_lower_bound_turns": tot.get("turns_output_lower_bound")}


__all__ = ["build_economics", "render_markdown", "render_csv", "csv_header", "output_files", "summary", "git_commits", "load_session_lists"]
