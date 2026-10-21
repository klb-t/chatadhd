"""Conversation views for restructuring profiles: which messages (branch policy),
which blocks and how (block policy), which fields, how times look (TimePolicy)
and what is masked (Redactor).  A view is plain JSON; every output format
(JSON, JSONL, Markdown, CSV, graph packet) is rendered from views, so a profile
is data and the code knows only these policies."""
from typing import Any, Optional

from ..i18n import label
from ..model import Block, Message, Unit
from ..timestamps import key as _tskey
from .common import Entry, uid_hex

BRANCH_KEEP = ("tree", "current", "newest", "longest", "marked", "all_paths")
BLOCK_POLICY = ("keep", "omit", "reference", "note", "summary")


TEXT_ATTACHMENT = (".txt", ".md", ".markdown", ".csv", ".tsv", ".json", ".jsonl", ".log", ".py", ".js", ".ts", ".html", ".htm",
                   ".xml", ".yaml", ".yml", ".toml", ".ini", ".sql", ".sh", ".c", ".cpp", ".h", ".java", ".rs", ".go")


class RenderCtx:
    def __init__(self, eng: Any, *, time: Any, redactor: Any, att_paths: dict[tuple[str, str], str],
                 aggregate: bool = False, summaries: Optional[dict[str, dict[str, Any]]] = None, extract_text: bool = False) -> None:
        self.extract_text = extract_text
        self.eng = eng
        self.time = time
        self.redactor = redactor
        self.att_paths = att_paths
        self.aggregate = aggregate
        self.summaries = summaries or {}
        self.dirs: dict[str, str] = {}   # entry.key → "<stamp>-<title>" (op_cut computes it once, attachments use the same names)
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
        best = max(paths, key=lambda p: (_tskey(p[-1].created_utc), -paths.index(p))) if paths else None
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
    red = ctx.redactor
    aw = {"conversation": unit.uid, "message": m.id, "attachment": b.ref}
    # attachment names often carry personal names: the ref and the file name are a redactable field (file_name)
    d: dict[str, Any] = {"ref": red.text(b.ref, aw, "file_name") if b.ref else b.ref}
    if a is None:
        d["status"] = "reference"
        return d
    d["status"] = a.status
    if a.name:
        d["file_name"] = red.text(a.name, aw, "file_name")
    if a.mime or b.mime:
        d["mime"] = a.mime or b.mime
    if a.size is not None:
        d["size"] = a.size
    if a.status == "resolved" and a.member:
        p = ctx.att_paths.get(((unit.source or {}).get("archive") or "", a.member))
        if p:
            d["path"] = p
        if ctx.extract_text and (str(a.mime or "").startswith("text/") or a.member.lower().endswith(TEXT_ATTACHMENT)):
            # text attachments are embedded as text (images/audio need OCR/ASR providers: not in this slice)
            from .common import find_archive
            arc = find_archive(ctx.eng.archives, unit) if ctx.eng is not None else None
            mem = arc.member(a.member) if arc is not None else None
            if mem is not None:
                d["extracted_text"] = ctx.redactor.text(mem.read_bytes().decode("utf-8", errors="replace"),
                                                        {"conversation": unit.uid, "attachment": a.ref}, "text")
    return d


# ───────────────────────────── fields ─────────────────────────────
def field_name(f: Any) -> str:
    """Output name of a profile field entry: ``"text"`` or ``{"field": "text", "as": "content"}``."""
    return str(f.get("as") or f.get("field")) if isinstance(f, dict) else str(f)


def _pick(full: dict[str, Any], fields: list[Any]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for f in fields:
        src = str(f.get("field")) if isinstance(f, dict) else str(f)
        name = field_name(f)
        if src in full:
            out[name] = full[src]
            continue
        cur: Any = full
        for part in src.split("."):
            cur = cur.get(part) if isinstance(cur, dict) else None
        out[name] = cur
    return out


def _alias(row: dict[str, Any], aliases: dict[str, str]) -> dict[str, Any]:
    """Output names renamed by the package entry (``edit.rename_key`` with ``in: ""``); a copy, order kept."""
    if not aliases:
        return row
    return {aliases.get(k, k): v for k, v in row.items()}


def conversation_fields(e: Entry, ctx: RenderCtx, cut: dict[str, Any], messages: list[Message]) -> dict[str, Any]:
    u = e.unit
    t = ctx.time
    red = ctx.redactor
    where = {"conversation": u.uid}
    src = u.source or {}
    title = e.name if e.name is not None else u.title
    first, last = (None, None)
    stamps = sorted((m.created_utc for m in messages if m.created_utc), key=_tskey)
    if stamps:
        first, last = stamps[0], stamps[-1]
    st = u.stats or {}
    return {
        "key": e.key, **({"dir": ctx.dirs[e.key]} if e.key in ctx.dirs else {}), "uid": u.uid, "uid_hex": uid_hex(u.uid), "ext_id": u.ext_id,
        "title": red.text(title, where, "title"), "folder": e.folder,
        "created": t.render(u.created, u.created_utc), "updated": t.render(u.updated, u.updated_utc),
        "created_src": u.created, "updated_src": u.updated, "created_utc": u.created_utc,
        "first_message": t.render(None, first), "last_message": t.render(None, last),
        "service": u.account.get("service"), "product": u.account.get("product"),
        "account": {"id": red.text(u.account.get("id"), where, "account"), "label": red.text(u.account.get("label"), where, "account")},
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
    # attachments a message carries only in its metadata (no content block) are still shown, never dropped
    pol = block_policy(policies, "attachment")
    if pol != "omit":
        have = {b.ref for b in m.blocks if b.ref}
        for a in u.attachments:
            if a.message == m.id and a.ref not in have:
                have.add(a.ref)
                from ..model import Block as _Block
                d = render_block(_Block(kind="attachment", ref=a.ref, mime=a.mime, native="metadata"), "keep" if pol == "keep" else "reference",
                                 u, m, ctx, {**where, "attachment": a.ref})
                if d is not None:
                    d["from"] = "metadata"
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
        stamps = sorted((m.created_utc for m in u.messages if m.created_utc), key=_tskey)
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
            before = len(msgs)
            msgs = [mm for mm in msgs if any(b.get("text") or b.get("ref") or b.get("input") is not None or b.get("note") or b.get("summary")
                                             for b in mm["blocks"])]
            # tool-only and thinking-only turns whose blocks this profile omits are counted, never dropped silently
            conv["messages_dropped_empty"] = before - len(msgs)
            conv["messages"] = len(msgs)
        cfields = fields.get("conversation")
        mfields = fields.get("message")
        al = getattr(e, "aliases", None) or {}
        views.append({"conversation": _alias(_pick(conv, cfields) if cfields else conv, al.get("conversation") or {}), "conversation_full": conv,
                      "messages": [_alias(_pick(mm, mfields) if mfields else mm, al.get("message") or {}) for mm in msgs],
                      "messages_full": msgs})
    return views
