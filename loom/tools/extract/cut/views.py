"""Conversation views for restructuring profiles: which messages (branch policy),
which blocks and how (block policy), which fields, how times look (TimePolicy)
and what is masked (Redactor).  A view is plain JSON; every output format
(JSON, JSONL, Markdown, CSV, graph packet) is rendered from views, so a profile
is data and the code knows only these policies."""
from typing import Any, Optional

from ..i18n import label
from ..model import Block, Message, Unit
from .common import Entry, uid_hex

BRANCH_KEEP = ("tree", "current", "newest", "longest", "marked", "all_paths")
BLOCK_POLICY = ("keep", "omit", "reference", "note", "summary")


class RenderCtx:
    def __init__(self, eng: Any, *, time: Any, redactor: Any, att_paths: dict[tuple[str, str], str],
                 aggregate: bool = False, summaries: Optional[dict[str, dict[str, Any]]] = None) -> None:
        self.eng = eng
        self.time = time
        self.redactor = redactor
        self.att_paths = att_paths
        self.aggregate = aggregate
        self.summaries = summaries or {}
        self.notes: list[str] = []


# ───────────────────────────── branches ─────────────────────────────
def _path_to(unit: Unit, leaf: str) -> list[Message]:
    by = unit.by_id()
    out: list[Message] = []
    cur: Optional[str] = leaf
    seen: set[str] = set()
    while cur is not None and cur in by and cur not in seen:
        seen.add(cur)
        out.append(by[cur])
        cur = by[cur].parent
    return list(reversed(out))


def choose_paths(unit: Unit, keep: str, marked_leaf: Optional[str] = None) -> tuple[list[list[Message]], dict[str, Any]]:
    """Message lists to render for a branch policy, with what was cut."""
    paths = unit.branches()
    leaves = len(paths)
    total = len(unit.messages)
    if keep == "tree" or not unit.messages:
        return [list(unit.messages)], {"policy": keep, "leaves": leaves, "branches_cut": 0, "messages_cut": 0}
    if keep == "all_paths":
        return paths or [list(unit.messages)], {"policy": keep, "leaves": leaves, "branches_cut": 0, "messages_cut": 0}
    chosen: Optional[list[Message]] = None
    fallback = None
    if keep == "current":
        cur = [m for m in unit.messages if m.on_current_path]
        chosen = cur or None
        fallback = "longest" if not cur else None
    elif keep == "newest":
        best = max(paths, key=lambda p: (p[-1].created_utc or "", -paths.index(p))) if paths else None
        chosen = best
    elif keep == "marked":
        leaf = marked_leaf or unit.flags.get("marked_leaf")
        if leaf and leaf in unit.by_id():
            chosen = _path_to(unit, leaf)
        else:
            cur = [m for m in unit.messages if m.on_current_path]
            chosen, fallback = cur or None, "current (no marked leaf)"
    if keep == "longest" or chosen is None:
        chosen = max(paths, key=lambda p: (len(p), -paths.index(p))) if paths else list(unit.messages)
        if keep != "longest" and fallback is None:
            fallback = "longest"
    kept_ids = {m.id for m in chosen}
    info = {"policy": keep, "leaves": leaves, "branches_cut": max(0, leaves - 1), "messages_cut": total - len(kept_ids)}
    if fallback:
        info["fallback"] = fallback
    return [chosen], info


# ───────────────────────────── blocks ─────────────────────────────
def block_policy(policies: dict[str, str], kind: str) -> str:
    return policies.get(kind) or policies.get("*") or "omit"


def _att_lookup(unit: Unit, ref: Optional[str], message: str) -> Any:
    for a in unit.attachments:
        if a.ref == ref and (a.message in (None, message)):
            return a
    for a in unit.attachments:
        if a.ref == ref:
            return a
    return None


def render_block(b: Block, policy: str, unit: Unit, m: Message, ctx: RenderCtx, where: dict[str, Any]) -> Optional[dict[str, Any]]:
    if policy == "omit":
        return None
    red = ctx.redactor
    if policy == "keep":
        d: dict[str, Any] = {"kind": b.kind}
        if b.text:
            d["text"] = red.text(b.text, where, "text")
        if b.name:
            d["name"] = red.text(b.name, where, "name")
        if b.input is not None:
            d["input"] = red.value(b.input, where, "input")
        for k in ("native", "mime"):
            if getattr(b, k):
                d[k] = getattr(b, k)
        if b.complete is not None:
            d["complete"] = b.complete
        if b.kind == "attachment" or b.ref:
            d.update(_att_ref(b, unit, m, ctx))
        if b.kind == "unknown" and b.example is not None and not ctx.aggregate:
            d["example"] = red.value(b.example, where, "input")
        return d
    if policy == "reference":
        d = {"kind": b.kind}
        if b.name:
            d["name"] = red.text(b.name, where, "name")
        if b.kind == "attachment" or b.ref:
            d.update(_att_ref(b, unit, m, ctx))
        else:
            d["chars"] = b.chars()
        return d
    if policy == "summary":
        s = (ctx.summaries.get(f"{unit.uid}#{m.id}") or {}).get("summary")
        if s:
            return {"kind": b.kind, "summary": s}
        policy = "note"
    return {"kind": b.kind, "note": f"omitted ({b.chars()} chars)" if b.chars() else "omitted"}


def _att_ref(b: Block, unit: Unit, m: Message, ctx: RenderCtx) -> dict[str, Any]:
    a = _att_lookup(unit, b.ref, m.id)
    d: dict[str, Any] = {"ref": b.ref}
    if a is None:
        d["status"] = "reference"
        return d
    d["status"] = a.status
    if a.name:
        d["file_name"] = a.name
    if a.mime or b.mime:
        d["mime"] = a.mime or b.mime
    if a.size is not None:
        d["size"] = a.size
    if a.status == "resolved" and a.member:
        p = ctx.att_paths.get(((unit.source or {}).get("archive") or "", a.member))
        if p:
            d["path"] = p
    return d


# ───────────────────────────── fields ─────────────────────────────
def _pick(full: dict[str, Any], fields: list[str]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for f in fields:
        if f in full:
            out[f] = full[f]
            continue
        cur: Any = full
        for part in f.split("."):
            cur = cur.get(part) if isinstance(cur, dict) else None
        out[f] = cur
    return out


def conversation_fields(e: Entry, ctx: RenderCtx, cut: dict[str, Any], messages: list[Message]) -> dict[str, Any]:
    u = e.unit
    t = ctx.time
    red = ctx.redactor
    where = {"conversation": u.uid}
    src = u.source or {}
    title = e.name if e.name is not None else u.title
    first, last = (None, None)
    stamps = sorted(m.created_utc for m in messages if m.created_utc)
    if stamps:
        first, last = stamps[0], stamps[-1]
    st = u.stats or {}
    return {
        "key": e.key, "uid": u.uid, "uid_hex": uid_hex(u.uid), "ext_id": u.ext_id,
        "title": red.text(title, where, "title"), "folder": e.folder,
        "created": t.render(u.created, u.created_utc), "updated": t.render(u.updated, u.updated_utc),
        "created_src": u.created, "updated_src": u.updated, "created_utc": u.created_utc,
        "first_message": t.render(None, first), "last_message": t.render(None, last),
        "service": u.account.get("service"), "product": u.account.get("product"),
        "account": {"id": u.account.get("id"), "label": u.account.get("label")},
        "archive": src.get("archive"), "member": None if ctx.aggregate else src.get("member"), "format": src.get("format"),
        "locator": None if ctx.aggregate else src.get("locator"),
        "messages": len(messages), "messages_total": len(u.messages),
        "branches": st.get("branches", u.leaf_count()), "forks": st.get("forks", len(u.tree.get("forks") or [])),
        "branch_policy": cut.get("policy"), "branches_cut": cut.get("branches_cut", 0), "messages_cut": cut.get("messages_cut", 0),
        "tools": dict(sorted(u.tool_names().items())), "attachments": len(u.attachments),
        "lang": st.get("lang_main"), "models": dict(sorted(u.models().items())), "agentic": u.is_agentic(),
        "kind": u.kind, "versions": len(u.versions), "duplicates": len(u.duplicates),
        "tags": sorted(((ctx.eng.recipe.get("tags") or {}).get(u.uid)) or []) if ctx.eng is not None else [],
        "notes": list(e.notes),
    }


def message_fields(m: Message, e: Entry, policies: dict[str, str], ctx: RenderCtx, path_index: int) -> dict[str, Any]:
    u = e.unit
    where = {"conversation": u.uid, "message": m.id}
    blocks = []
    for i, b in enumerate(m.blocks):
        d = render_block(b, block_policy(policies, b.kind), u, m, ctx, {**where, "block": i})
        if d is not None:
            blocks.append(d)
    text = "\n".join(d.get("text", "") for d in blocks if d.get("kind") in ("text", "code") and d.get("text"))
    atts = [{k: v for k, v in d.items() if k != "kind"} for d in blocks if d.get("kind") == "attachment"]
    return {
        "conversation": e.key, "conversation_uid": u.uid, "id": m.id, "parent": m.parent, "role": m.role,
        "created": ctx.time.render(m.created, m.created_utc), "created_src": m.created, "created_utc": m.created_utc,
        "created_how": m.created_how, "author": ctx.redactor.text(m.author, where, "author") if m.author else None,
        "model": m.model, "branch": m.branch, "on_current_path": m.on_current_path, "path": path_index,
        "text": text, "blocks": blocks, "attachments": atts, "kinds": sorted({b.kind for b in m.blocks}),
        "notes": [f"{d['kind']}: {d['note']}" for d in blocks if "note" in d],
        "chars": len(text), "words": len(text.split()), "tools": m.tool_names(),
    }


def conversation_views(e: Entry, profile: dict[str, Any], ctx: RenderCtx) -> list[dict[str, Any]]:
    """One view per kept path (``all_paths`` gives several, the others one)."""
    u = e.unit
    bspec = profile.get("branches") or {}
    keep = bspec.get("keep", "current")
    if keep not in BRANCH_KEEP:
        raise ValueError(f"profile {profile.get('id')}: branches.keep must be one of {', '.join(BRANCH_KEEP)}")
    paths, cut = choose_paths(u, keep, (ctx.eng.package_marks.get(u.uid) if ctx.eng is not None and hasattr(ctx.eng, "package_marks") else None))
    policies = dict(profile.get("blocks") or {"*": "keep"})
    for v in policies.values():
        if v not in BLOCK_POLICY:
            raise ValueError(f"profile {profile.get('id')}: block policy {v!r} (one of {', '.join(BLOCK_POLICY)})")
    fields = profile.get("fields") or {}
    if ctx.time.anonymise == "conversation":
        stamps = sorted(m.created_utc for m in u.messages if m.created_utc)
        from ..timestamps import from_iso
        ctx.time.conv_origin = from_iso(stamps[0] if stamps else u.created_utc)
    views = []
    for pi, path in enumerate(paths):
        if e.messages is not None:
            path = [m for m in path if m.id in e.messages]
        conv = conversation_fields(e, ctx, cut, path)
        note_tpl = bspec.get("note_text")
        if cut.get("branches_cut") and bspec.get("alternatives", "note") == "note":
            conv["branch_note"] = label(note_tpl).format(n=cut["branches_cut"], k=len(u.tree.get("forks") or []),
                                                         m=cut.get("messages_cut", 0)) if note_tpl else \
                f"{cut['branches_cut']} branch(es) cut, {cut.get('messages_cut', 0)} message(s) not on the kept path"
        if len(paths) > 1:
            conv["path"] = pi
        msgs = [message_fields(m, e, policies, ctx, pi) for m in path]
        if profile.get("drop_empty_messages"):
            msgs = [mm for mm in msgs if any(b.get("text") or b.get("ref") or b.get("input") is not None or b.get("note") or b.get("summary")
                                             for b in mm["blocks"])]
        cfields = fields.get("conversation")
        mfields = fields.get("message")
        views.append({"conversation": _pick(conv, cfields) if cfields else conv, "conversation_full": conv,
                      "messages": [_pick(mm, mfields) if mfields else mm for mm in msgs], "messages_full": msgs})
    return views
