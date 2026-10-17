"""Reader contract and shared helpers (stamps, block mapping, unknown bucket,
attachment resolution, per-unit statistics, finalisation)."""
import json
import logging
import re
import tempfile
from pathlib import Path
from typing import Any, Iterator, Optional

from ..datafiles import DataStore, store as default_store
from ..langdetect import LanguageDetector
from ..model import Attachment, Block, Message, Unit, json_pointer, sanitize_text
from ..sources import FormatCatalog, Member
from .. import timestamps as ts

log = logging.getLogger(__name__)


class ReadContext:
    """Settings a reader needs (all from presets/overlay) plus a temp dir."""

    def __init__(self, ds: Optional[DataStore] = None, catalog: Optional[FormatCatalog] = None,
                 tmpdir: Optional[str] = None) -> None:
        self.ds = ds or default_store()
        self.catalog = catalog or FormatCatalog(self.ds)
        self.assume_zone: str = self.ds.preset("timeline.assume_zone", "UTC")
        self.keep_raw: bool = bool(self.ds.preset("read.keep_raw", False))
        self.thinking_full_min: int = int(self.ds.preset("thinking.full_min_chars", 200))
        self.tokens: dict[str, float] = dict(self.ds.preset("tokens.chars_per_token", {"default": 3.6}))
        self.lang = LanguageDetector(self.ds)
        self._tmp: Optional[tempfile.TemporaryDirectory] = None
        self.tmpdir = tmpdir
        if self.tmpdir is None:
            self._tmp = tempfile.TemporaryDirectory(prefix="loom_extract_")
            self.tmpdir = self._tmp.name
        self.progress = None  # optional callable(dict)
        self.allow_user_code = False  # the face's grant (``--kod-uzytkownika``), set by the engine
        self.user_code_used: list[dict[str, Any]] = []
        self.sidecars: dict[tuple[str, str], Optional[dict[str, Any]]] = {}  # metadata files next to transcripts, read once

    def stamp(self, value: Any, mtime: Optional[float] = None) -> ts.Stamp:
        return ts.parse(value, assume_zone=self.assume_zone, mtime=mtime)

    def set_unit_times(self, unit: Unit, created: Any, updated: Any, *, mtime: Optional[float] = None) -> None:
        c = self.stamp(created, mtime=mtime)
        unit.created, unit.created_utc, unit.created_how, unit.created_inferred = created, c.iso, c.how, c.inferred
        u = self.stamp(updated)
        unit.updated, unit.updated_utc = updated, u.iso

    def set_msg_time(self, msg: Message, value: Any, *, fallback: Optional[str] = None) -> None:
        st = self.stamp(value)
        msg.created = value
        if st.utc is None and fallback:
            msg.created_utc, msg.created_how, msg.created_inferred = fallback, "conversation", True
        else:
            msg.created_utc, msg.created_how, msg.created_inferred = st.iso, st.how, st.inferred

    def close(self) -> None:
        if self._tmp is not None:
            self._tmp.cleanup()
            self._tmp = None


class Reader:
    id = "base"

    def iter_units(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        raise NotImplementedError


# ───────────────────────────── field access ─────────────────────────────
def getpath(obj: Any, path: Optional[str]) -> Any:
    if not path or obj is None:
        return None
    cur = obj
    for part in path.split("."):
        m = re.fullmatch(r"(.*)\[(\d+)\]", part)
        idx = None
        if m:
            part, idx = m.group(1), int(m.group(2))
        if part:
            if not isinstance(cur, dict) or part not in cur:
                return None
            cur = cur[part]
        if idx is not None:
            if not isinstance(cur, list) or idx >= len(cur):
                return None
            cur = cur[idx]
    return cur


def getpath_many(obj: Any, path: Optional[str]) -> list[Any]:
    """Every value at a dotted path where ``[*]`` stands for each element of a list (``a.b[*].c``); a path without
    ``[*]`` gives at most one value.  Missing → an empty list, nothing invented."""
    if not path or obj is None:
        return []
    head, star, rest = path.partition("[*]")
    if not star:
        v = getpath(obj, path)
        return [] if v is None else [v]
    lst = getpath(obj, head) if head else obj
    if not isinstance(lst, list):
        return []
    rest = rest[1:] if rest.startswith(".") else rest
    out: list[Any] = []
    for el in lst:
        out.extend(getpath_many(el, rest) if rest else ([el] if el is not None else []))
    return out


def first_of(obj: Any, paths: Any) -> Any:
    if isinstance(paths, str):
        paths = [paths]
    for p in paths or []:
        v = getpath(obj, p)
        if v not in (None, ""):
            return v
    return None


def stamp_path(obj: Any, spec: Any) -> Any:
    """``{"path": …, "type": …}`` or a plain path → native value."""
    if isinstance(spec, dict):
        return getpath(obj, spec.get("path"))
    return getpath(obj, spec)


# ───────────────────────────── blocks and unknowns ─────────────────────────────
def block_kind(fmt: dict[str, Any], native: Optional[str], table: str = "blocks") -> tuple[str, Optional[str]]:
    """Native type → (common kind, modifier) via the format's table; ``*`` → unknown."""
    tbl = fmt.get(table) or {}
    spec = tbl.get(native) if native is not None else None
    if spec is None:
        spec = tbl.get("*", "unknown")
    if ":" in spec:
        kind, mod = spec.split(":", 1)
        return kind, mod
    return spec, None


def example_of(obj: Any, limit: int = 240) -> Any:
    """A trimmed example of a native object for the unknown bucket."""
    if isinstance(obj, dict):
        out: dict[str, Any] = {}
        for i, (k, v) in enumerate(obj.items()):
            if i >= 12:
                out["…"] = f"+{len(obj) - 12} keys"
                break
            out[k] = example_of(v, 80)
        return out
    if isinstance(obj, list):
        return [example_of(v, 80) for v in obj[:3]] + (["…"] if len(obj) > 3 else [])
    if isinstance(obj, str):
        return obj if len(obj) <= limit else obj[:limit] + "…"
    return obj


def note_unknown(unit: Unit, where: str, obj: Any, native: Optional[str] = None) -> None:
    entry: dict[str, Any] = {"where": where, "example": example_of(obj)}
    if native is not None:
        entry["native"] = native
    unit.unknown.append(entry)


def unknown_block(native: Optional[str], obj: Any) -> Block:
    return Block(kind="unknown", native=native, example=example_of(obj))


def thinking_block(text: str, native: str, modifier: Optional[str], ctx: ReadContext) -> Block:
    """A reasoning block.  ``complete``: the text is the whole reasoning (``full`` modifier of the format file and the
    text is there); a ``full`` block whose text is empty (a signature only: the client withheld the text) is not
    complete and is marked ``withheld`` — present, but nothing of it is in the export."""
    complete = modifier == "full" and bool(text)
    if modifier == "summary":
        complete = False
    b = Block(kind="thinking", text=text or "", native=native, complete=complete)
    if modifier == "redacted":
        b.extra["redacted"] = True
    elif modifier == "full" and not text:
        b.extra["withheld"] = True
    return b


# ───────────────────────────── attachments ─────────────────────────────
def resolve_attachments(unit: Unit, member: Member, fmt: dict[str, Any]) -> None:
    """Find members of the archive that carry each referenced attachment."""
    archive = member.archive
    names = archive.names()
    by_base: dict[str, list[str]] = {}
    for n in names:
        by_base.setdefault(Path(n).name, []).append(n)
    for att in unit.attachments:
        if att.status == "resolved":
            continue
        ref = att.ref or ""
        cands: list[str] = []
        if att.name and att.name in by_base:
            cands = by_base[att.name]
        if not cands and ref:
            for base, full in by_base.items():
                if base == ref or base.startswith(ref + "-") or base.startswith(ref + ".") or base.startswith(ref + "_") \
                        or (att.name and base.endswith(att.name) and ref in base):
                    cands.extend(full)
        if not cands and ref and att.name:
            for base, full in by_base.items():
                if base.startswith(ref):
                    cands.extend(full)
        if cands:
            chosen = sorted(cands)[0]
            m = archive.member(chosen)
            if m is not None and m.kind == "unreadable":  # the file is there but damaged: not resolved, said why
                att.status, att.member = "unreadable", chosen
                continue
            att.status, att.member = "resolved", chosen
            if m is not None:
                att.size = att.size if att.size is not None else m.size
                att.sha256 = m.sha256
        elif att.status == "reference":
            att.status = "missing"


# ───────────────────────────── statistics ─────────────────────────────
def compute_stats(unit: Unit, ctx: ReadContext) -> dict[str, Any]:
    roles: dict[str, int] = {}
    kinds: dict[str, int] = {}
    kind_chars: dict[str, int] = {}
    native: dict[str, int] = {}
    tools: dict[str, int] = {}
    chars = 0
    thinking_blocks = thinking_chars = thinking_full = thinking_summary = thinking_redacted = 0
    usage_messages = 0
    texts: list[str] = []
    for m in unit.messages:
        roles[m.role] = roles.get(m.role, 0) + 1
        if isinstance(m.meta.get("usage"), dict):
            usage_messages += 1
        for b in m.blocks:
            kinds[b.kind] = kinds.get(b.kind, 0) + 1
            kind_chars[b.kind] = kind_chars.get(b.kind, 0) + b.chars()
            nat = f"{b.native or b.kind}→{b.kind}"
            native[nat] = native.get(nat, 0) + 1
            chars += b.chars()
            if b.kind == "thinking":
                thinking_blocks += 1
                thinking_chars += b.chars()
                if b.chars() >= ctx.thinking_full_min and b.complete is not False:
                    thinking_full += 1
                if b.complete is False and not b.extra.get("withheld"):
                    thinking_summary += 1
                if b.extra.get("redacted"):
                    thinking_redacted += 1
            if b.kind == "tool_use" and b.name:
                tools[b.name] = tools.get(b.name, 0) + 1
            if b.kind == "text" and b.text:
                texts.append(b.text)
    lang = ctx.lang.shares(texts)
    main = next(iter(lang), "und") if lang else "und"
    cpt = ctx.tokens.get(main, ctx.tokens.get("default", 3.6))
    att_status: dict[str, int] = {}
    for a in unit.attachments:
        att_status[a.status] = att_status.get(a.status, 0) + 1
    first, last = unit.first_last_utc()
    return {
        "messages": len(unit.messages), "roles": roles, "chars": chars,
        "tokens_est": int(round(chars / cpt)) if cpt else 0, "tokens_chars_per_token": cpt,
        "blocks": kinds, "block_chars": kind_chars, "blocks_native": native,
        "tools": tools, "tool_calls": sum(tools.values()),
        "thinking": {"blocks": thinking_blocks, "chars": thinking_chars, "full": thinking_full, "summary": thinking_summary,
                     "redacted": thinking_redacted},
        "usage_messages": usage_messages,
        "branches": unit.leaf_count(), "forks": len(unit.tree.get("forks") or []),
        "current_path_messages": sum(1 for m in unit.messages if m.on_current_path),
        "attachments": {"referenced": len(unit.attachments), **att_status},
        "lang": lang, "lang_main": main, "agentic": unit.is_agentic(), "models": unit.models(),
        "first_message_utc": first, "last_message_utc": last,
        "unknown": len(unit.unknown),
    }


# ───────────────────────────── finalisation ─────────────────────────────
def finalize_unit(unit: Unit, member: Member, fmt: dict[str, Any], ctx: ReadContext, locator: dict[str, Any]) -> Unit:
    unit.source = {"archive": member.archive.label, "archive_path": member.archive.path,
                   "archive_mtime": member.archive.mtime,
                   "sha256": member.archive.sha256, "member": member.name, "member_sha256": member.sha256,
                   "format": fmt["id"], "locator": locator}
    unit.account.setdefault("service", fmt.get("service"))
    unit.account.setdefault("product", fmt.get("product"))
    unit.account.setdefault("id", None)
    unit.account.setdefault("label", None)
    if unit.created_utc is None:
        first, _ = unit.first_last_utc()
        if first:
            unit.created_utc, unit.created_how, unit.created_inferred = first, "first_message", True
        elif member.mtime is not None or member.archive.mtime is not None:
            st = ctx.stamp(None, mtime=member.mtime if member.mtime is not None else member.archive.mtime)
            unit.created_utc, unit.created_how, unit.created_inferred = st.iso, "mtime", True
    if unit.updated_utc is None:
        _, last = unit.first_last_utc()
        unit.updated_utc = last or unit.created_utc
    for m in unit.messages:
        if m.created_utc is None and unit.created_utc:
            m.created_utc, m.created_how, m.created_inferred = unit.created_utc, "conversation", True
    unit.tree.setdefault("roots", [m.id for m in unit.messages if m.parent is None])
    unit.tree.setdefault("current_leaf", None)
    unit.tree.setdefault("forks", [])
    if fmt.get("hypothesis"):
        unit.flags["format_hypothesis"] = True
    lone = 0
    if unit.title:
        unit.title, n = sanitize_text(unit.title)
        lone += n
    for m in unit.messages:
        for b in m.blocks:
            if b.text:
                b.text, n = sanitize_text(b.text)
                lone += n
            if b.name:
                b.name, n = sanitize_text(b.name)
                lone += n
    if lone:
        unit.flags["lone_surrogates_replaced"] = lone
        unit.unknown.append({"where": "text", "native": "lone surrogate", "example": f"{lone} invalid UTF-16 surrogate(s) replaced by U+FFFD"})
    resolve_attachments(unit, member, fmt)
    unit.stats = compute_stats(unit, ctx)
    unit.compute_identity()
    if not ctx.keep_raw:
        unit.raw = None
    return unit


def unit_for_record(member: Member, fmt: dict[str, Any], kind: str, raw: Any, ctx: ReadContext,
                    *, index: Optional[int] = None, title: Optional[str] = None, ext_id: Optional[str] = None) -> Unit:
    """A non-conversation entity (account, feedback, project, …) as a unit."""
    u = Unit(kind=kind, ext_id=ext_id, title=title)
    u.raw = raw
    locator = {"json_pointer": json_pointer(index) if index is not None else ""}
    u.created_utc = None
    created = first_of(raw, ["created_at", "create_time", "created", "timestamp"]) if isinstance(raw, dict) else None
    if created is not None:
        ctx.set_unit_times(u, created, first_of(raw, ["updated_at", "update_time", "updated"]))
    fin = finalize_unit(u, member, fmt, ctx, locator)
    fin.raw = raw if ctx.keep_raw else None
    return fin


def load_member_json(member: Member) -> Any:
    return json.loads(member.read_bytes().decode("utf-8", errors="replace"))


def strip_html(html: str) -> str:
    text = re.sub(r"(?is)<(script|style).*?</\1>", " ", html)
    text = re.sub(r"(?i)<br\s*/?>|</p>|</div>|</li>", "\n", text)
    text = re.sub(r"<[^>]+>", "", text)
    import html as _html
    text = _html.unescape(text)
    return re.sub(r"[ \t]+\n", "\n", re.sub(r"\n{3,}", "\n\n", text)).strip()
