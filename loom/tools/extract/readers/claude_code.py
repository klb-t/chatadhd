"""Claude Code local sessions: ``~/.claude/projects/<project>/<session>.jsonl``.

One unit (kind ``session``) per file, identified per file: the main session
file keeps its ``sessionId`` as ``ext_id``; a subagent transcript
(``agent-*.jsonl``) or a resumed-session file — which carry the parent's
``sessionId`` — gets its own file stem and ``flags.parent_session``, so it is a
related unit, never merged with the main session as a "version".  The current
leaf is the last non-sidechain message (format preset ``leaf: last_main``), so
a subagent writing last never decides the main path.  Lines of type user/assistant become
messages (tree by ``parentUuid``; sidechains and compact summaries flagged),
``summary`` lines become session summaries, ``system`` lines system messages,
housekeeping line types are counted, anything else goes to the unknown bucket
with an example.  Tool calls and results are kept as blocks."""
import fnmatch
import io
import json
import logging
from pathlib import Path, PurePosixPath
from typing import Any, Iterator, Optional

from ..model import Block, Message, Unit
from ..sources import Member
from .base import ReadContext, Reader, block_kind, getpath, note_unknown, finalize_unit, thinking_block, unknown_block
from .tree_walk import TreeNode, walk_tree

log = logging.getLogger(__name__)


class ClaudeCodeReader(Reader):
    id = "claude_code"

    def iter_units(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        lspec = fmt["line"]
        types = fmt.get("line_types") or {}
        roles = fmt.get("roles") or {}
        u = Unit(kind=fmt.get("unit_kind", "session"))
        order: list[str] = []
        sidechain: set[str] = set()
        tool_names: dict[str, Any] = {}  # tool_use id → name, kept for the session (one pass, not per message)
        nodes: dict[str, TreeNode] = {}
        summaries: list[dict[str, Any]] = []
        internal: dict[str, int] = {}
        session_id: Optional[str] = None
        first_ts: Any = None
        last_ts: Any = None
        lines_total = 0
        with member.open() as fh, io.TextIOWrapper(fh, encoding="utf-8", errors="replace") as text:
            for lineno, raw in enumerate(text, 1):
                raw = raw.strip()
                if not raw:
                    continue
                lines_total += 1
                try:
                    obj = json.loads(raw)
                except json.JSONDecodeError:
                    note_unknown(u, f"line:{lineno}", raw[:200], native="invalid-json")
                    continue
                if not isinstance(obj, dict):
                    note_unknown(u, f"line:{lineno}", obj)
                    continue
                ltype = str(obj.get(lspec["type"]) or "")
                mapped = types.get(ltype, types.get("*", "unknown"))
                sid = obj.get(lspec["session"])
                if sid and not session_id:
                    session_id = str(sid)
                tsv = obj.get(lspec["created"]["path"])
                if tsv:
                    first_ts = first_ts or tsv
                    last_ts = tsv
                for k in ("cwd", "version", "git_branch"):
                    v = obj.get(lspec.get(k, ""))
                    if v and k not in u.flags:
                        u.flags[k] = v
                if mapped == "message":
                    mid = str(obj.get(lspec["id"]) or f"#{lineno}")
                    if mid in nodes:
                        mid = f"#{lineno}"
                    parent = obj.get(lspec["parent"])
                    nodes[mid] = TreeNode(parent if isinstance(parent, str) and parent else None, True, (lineno, obj))
                    order.append(mid)
                    if obj.get(lspec.get("sidechain", "isSidechain")):
                        sidechain.add(mid)
                elif mapped == "summary":
                    summaries.append({"summary": obj.get(lspec.get("summary", "summary")), "leaf": obj.get(lspec.get("leaf", "leafUuid")), "line": lineno})
                elif mapped == "system":
                    mid = str(obj.get(lspec["id"]) or f"#{lineno}")
                    parent = obj.get(lspec["parent"])
                    nodes[mid] = TreeNode(parent if isinstance(parent, str) and parent else None, True, (lineno, obj))
                    order.append(mid)
                elif mapped == "member":
                    internal[ltype] = internal.get(ltype, 0) + 1
                else:
                    note_unknown(u, f"line:{lineno}", obj, native=ltype or "?")
        stem = member.name.rsplit("/", 1)[-1] if member.name else Path(member.archive.path).name
        stem = stem[:-len(".jsonl")] if stem.endswith(".jsonl") else stem
        place = transcript_place(member, fmt, session_id=session_id, stem=stem)
        if place is not None:
            # <session>/subagents/**/x.jsonl or <session>/[subagents/]workflows/<run>/agent-*.jsonl: the directory names
            # the parent session (a measured link); the id is the path from the session directory, unique across runs
            u.ext_id = place["ext_id"]
            u.flags["parent_session"] = place["session"]
            u.flags["parent_session_how"] = "path"
            u.flags["transcript_role"] = place["role"]
            if place.get("run"):
                u.flags["workflow_run"] = place["run"]
            _read_sidecars(u, member, fmt, place, stem, ctx)
        elif session_id and stem != session_id:
            # an agent transcript or a resumed session in its own file: its own identity, linked to the parent session
            u.ext_id = stem
            u.flags["parent_session"] = session_id
            u.flags["parent_session_how"] = "sessionId"
        else:
            u.ext_id = session_id or member.name
            u.flags["transcript_role"] = "main"
        ctx.set_unit_times(u, first_ts, last_ts, mtime=member.mtime)
        leaf_policy = str(fmt.get("leaf") or "last_main")
        main_ids = [x for x in order if x not in sidechain]
        leaf = (main_ids[-1] if main_ids else None) if leaf_policy == "last_main" else None
        leaf = leaf or (order[-1] if order else None)
        walk = walk_tree(order, nodes, leaf)
        for e in walk.messages:
            lineno, obj = e.payload
            ltype = str(obj.get(lspec["type"]) or "")
            role = getpath(obj, lspec["role"]) or ltype
            m = Message(id=e.id, parent=e.parent, role=roles.get(role, role), branch=e.branch, on_current_path=e.on_current,
                        raw_pointer=f"line:{lineno}")
            m.model = getpath(obj, lspec.get("model"))
            read_usage(m, obj, fmt.get("usage"))
            ctx.set_msg_time(m, obj.get(lspec["created"]["path"]), fallback=u.created_utc)
            if obj.get(lspec.get("sidechain", "isSidechain")):
                m.meta["sidechain"] = True
            if obj.get(lspec.get("compact", "isCompactSummary")):
                m.meta["compacted"] = True
            if obj.get(lspec.get("agent", "agentId")):
                m.meta["agent"] = obj[lspec.get("agent", "agentId")]
            if ltype == "system":
                txt = obj.get("content")
                m.blocks.append(Block(kind="system", text=str(txt or ""), native="system", name=obj.get("subtype")))
            else:
                self._blocks(m, obj, u, fmt, ctx, lspec, tool_names)
            u.messages.append(m)
        u.tree = {"roots": walk.roots, "current_leaf": walk.current_leaf, "forks": walk.forks}
        u.flags["summaries"] = summaries
        u.flags["internal_lines"] = internal
        u.flags["lines"] = lines_total
        u.flags["sidechain_messages"] = sum(1 for m in u.messages if m.meta.get("sidechain"))
        if u.messages and all(m.meta.get("sidechain") for m in u.messages if m.role != "system"):
            u.flags["agent_transcript"] = True
        if "transcript_role" not in u.flags:  # a file next to the session's, linked by sessionId
            u.flags["transcript_role"] = "subagent" if u.flags.get("agent_transcript") else "resumed"
        u.flags["compacted_messages"] = sum(1 for m in u.messages if m.meta.get("compacted"))
        if summaries and not u.title:
            u.title = str(summaries[-1].get("summary") or "")[:80] or None
        if not u.title:
            for m in u.messages:
                if m.role == "user" and m.text():
                    u.title = m.text().strip().splitlines()[0][:80]
                    break
        yield finalize_unit(u, member, fmt, ctx, {"line": 1, "session": session_id})

    def _blocks(self, m: Message, obj: dict[str, Any], u: Unit, fmt: dict[str, Any], ctx: ReadContext, lspec: dict[str, Any],
                tool_names: Optional[dict[str, Any]] = None) -> None:
        names = tool_names if tool_names is not None else {}
        content = getpath(obj, lspec["content"])
        if isinstance(content, str):
            m.blocks.append(Block(kind="text", text=content, native="string"))
            return
        if not isinstance(content, list):
            if content is not None:
                m.blocks.append(unknown_block(type(content).__name__, content))
                note_unknown(u, m.raw_pointer + "/message/content", content)
            return
        for i, b in enumerate(content):
            if not isinstance(b, dict):
                m.blocks.append(unknown_block(type(b).__name__, b))
                continue
            bt = b.get("type")
            kind, mod = block_kind(fmt, bt)
            if kind == "text":
                m.blocks.append(Block(kind="text", text=str(b.get("text") or ""), native=bt))
            elif kind == "thinking":
                m.blocks.append(thinking_block(str(b.get("thinking") or ""), bt, mod, ctx))
            elif kind == "tool_use":
                inp = b.get("input")
                m.blocks.append(Block(kind="tool_use", native=bt, name=b.get("name"), input=inp, text=_tool_text(inp),
                                      extra={"id": b.get("id")} if b.get("id") else {}))
            elif kind == "tool_result":
                c = b.get("content")
                if isinstance(c, list):
                    c = "\n".join(str(x.get("text") or "") for x in c if isinstance(x, dict))
                m.blocks.append(Block(kind="tool_result", native=bt, name=b.get("tool_use_id"), text=str(c or ""),
                                      input={"is_error": True} if b.get("is_error") else None))
            elif kind == "attachment":
                src = b.get("source") if isinstance(b.get("source"), dict) else {}
                m.blocks.append(Block(kind="attachment", native=bt, ref=str(src.get("file_id") or b.get("name") or f"{bt}#{i}"),
                                      mime=src.get("media_type")))
            elif kind == "unknown":
                m.blocks.append(unknown_block(bt, b))
                note_unknown(u, f"{m.raw_pointer}/message/content/{i}", b, native=bt)
            else:
                m.blocks.append(Block(kind=kind, text=str(b.get("text") or ""), native=bt))
        # tool results are bound to the tool_use name when the id is known in this session (earlier messages; the
        # map grows as messages are read, so a long agent session is read in one pass)
        for bb in m.blocks:
            if bb.kind == "tool_result" and bb.name in names:
                bb.extra["tool_use_id"] = bb.name
                bb.name = names[bb.name]
        for bb in m.blocks:
            if bb.kind == "tool_use" and bb.extra.get("id"):
                names[bb.extra["id"]] = bb.name


def _tool_text(inp: Any) -> str:
    if isinstance(inp, dict):
        for k in ("command", "code", "content", "query", "pattern", "prompt", "description"):
            if isinstance(inp.get(k), str):
                return inp[k]
    return ""


def _path_parts(member: Member) -> tuple[list[str], int]:
    """(path parts ending with the file name, index of the first part inside the scanned source).  A directory source:
    the root's own path, then the member's path below it; a zip: the member's path only (the zip's location says
    nothing about the layout); a single file: its path, nothing of it inside the source but the file name."""
    if member.archive.kind == "file" or not member.name:
        parts = list(Path(member.archive.path).parts)
        return parts, len(parts) - 1
    rel = list(PurePosixPath(member.name).parts)
    if member.archive.kind == "dir":
        root = list(Path(member.archive.path).parts)
        return root + rel, len(root)
    return rel, 0


def transcript_place(member: Member, fmt: dict[str, Any], *, session_id: Optional[str] = None,
                     stem: Optional[str] = None) -> Optional[dict[str, Any]]:
    """Where a transcript sits: ``<session>/subagents/**/agent-*.jsonl`` → subagent of <session>; ``<session>/
    [subagents/]workflows/<run>/agent-*.jsonl`` → agent of workflow run <run>.  Directory and file names come from
    the format file (``transcripts.subagent_dirs`` / ``workflow_dirs`` / ``agent_files``).  Matched from the file
    upwards (the nearest directory wins, so an ancestor that happens to be called ``workflows`` or ``subagents``
    never decides), and only:
      - a file named like an agent transcript; a file whose lines' sessionId is its own name (a main transcript) is
        never re-placed;
      - when the lines carry a sessionId, it must be the session the path names;
      - a session directory above the scanned source (a source that is itself a session or subagents directory)
        is accepted only when the lines' sessionId confirms it.
    None = not an agent transcript by its place (the caller links it by the lines' sessionId, if any)."""
    tspec = fmt.get("transcripts") or {}
    subs = set(tspec.get("subagent_dirs") or [])
    wfs = set(tspec.get("workflow_dirs") or [])
    if not subs and not wfs:
        return None
    if stem and session_id and stem == session_id:
        return None
    parts, inside = _path_parts(member)
    if not parts:
        return None
    pats = [str(x) for x in (tspec.get("agent_files") or ["*.jsonl"])]
    if not any(fnmatch.fnmatchcase(parts[-1], pat) for pat in pats):
        return None
    dirs = parts[:-1]
    run: Optional[str] = None
    if len(dirs) >= 3 and dirs[-2] in wfs:
        role, run, s = "workflow_agent", dirs[-1], len(dirs) - 3
        if s >= 1 and dirs[s] in subs:
            s -= 1
    else:
        i_sub = next((i for i in range(len(dirs) - 1, -1, -1) if dirs[i] in subs), None)
        if i_sub is None or i_sub == 0:
            return None
        role, s = "subagent", i_sub - 1
    if s < 0:
        return None
    session = dirs[s]
    if session_id and session_id != session:
        return None
    if s < inside and session_id != session:
        return None
    rel = "/".join(parts[s:])
    rel = rel[:-len(".jsonl")] if rel.endswith(".jsonl") else rel
    return {"session": session, "role": role, "run": run, "ext_id": rel, "parts": parts, "inside": inside, "s": s}


def _sidecar(member: Member, name: str, ctx: ReadContext) -> Optional[dict[str, Any]]:
    """A JSON file Claude Code writes next to the transcripts, read only when it is a member of the same scanned
    source (dir or zip); cached per source, so the run's summary is read once for all its agents."""
    if member.archive.kind not in ("dir", "zip"):
        return None
    cache = ctx.sidecars
    key = (member.archive.path, name)
    if key in cache:
        return cache[key]
    cache[key] = None
    sm = member.archive.member(name)
    if sm is None:
        return None
    try:
        doc = json.loads(sm.read_bytes().decode("utf-8", "replace"))
    except (OSError, ValueError, RuntimeError):  # zipfile errors are among these
        log.debug("sidecar %s unreadable", name, exc_info=True)
        return None
    cache[key] = doc if isinstance(doc, dict) else None
    return cache[key]


def _read_sidecars(u: Unit, member: Member, fmt: dict[str, Any], place: dict[str, Any], stem: str, ctx: ReadContext) -> None:
    """Measured metadata next to an agent transcript (format file ``transcripts.agent_meta`` / ``workflow_meta``):
    the agent's ``<stem>.meta.json`` (type, description) and the workflow run's ``<session>/workflows/<run>.json``
    (name, agent count, its own token total).  Absent or unreadable = not there; nothing is inferred."""
    tspec = fmt.get("transcripts") or {}
    if not member.name:
        return
    here = member.name.rsplit("/", 1)[0] if "/" in member.name else ""
    am = tspec.get("agent_meta") or {}
    if am.get("file"):
        name = str(am["file"]).format(stem=stem)
        doc = _sidecar(member, f"{here}/{name}" if here else name, ctx)
        if doc is not None:
            meta = {k: getpath(doc, path) for k, path in (am.get("fields") or {}).items() if getpath(doc, path) is not None}
            if meta:
                u.flags["agent_meta"] = meta
    wm = tspec.get("workflow_meta") or {}
    if wm.get("file") and place.get("run"):
        s, inside, parts = place["s"], place["inside"], place["parts"]
        if s < inside:  # the session directory is above the scanned source: the run's file is not in it
            return
        sess_dir = "/".join(parts[inside:s + 1])
        name = str(wm["file"]).format(session=sess_dir, run=place["run"])
        doc = _sidecar(member, name, ctx)
        if doc is not None:
            meta = {k: getpath(doc, path) for k, path in (wm.get("fields") or {}).items() if getpath(doc, path) is not None}
            if meta:
                u.flags["workflow_meta"] = meta


def read_usage(m: Message, obj: dict[str, Any], uspec: Optional[dict[str, Any]]) -> None:
    """Measured counters of one line (``usage`` of the format file) → ``m.meta``: ``usage`` (mapped counters, labels,
    ``extra`` = every unmapped field, never dropped), ``api_id`` / ``request_id`` (one API response may span several
    lines), ``cost_usd`` when the line carries its own cost."""
    if not uspec:
        return
    raw = getpath(obj, uspec.get("path"))
    cost = getpath(obj, uspec.get("cost_usd")) if uspec.get("cost_usd") else None
    if isinstance(cost, (int, float)) and not isinstance(cost, bool):
        m.meta["cost_usd"] = float(cost)
    if not isinstance(raw, dict):
        return
    out: dict[str, Any] = {}
    consumed: set[str] = set()
    for key, path in (uspec.get("fields") or {}).items():
        consumed.add(str(path).split(".")[0])
        v = getpath(raw, path)
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            out[key] = int(v)
    for key, path in (uspec.get("labels") or {}).items():
        consumed.add(str(path).split(".")[0])
        v = getpath(raw, path)
        if v is not None:
            out[key] = v
    extra = {k: v for k, v in raw.items() if k not in consumed}
    if extra:
        out["extra"] = extra
    m.meta["usage"] = out
    if uspec.get("stop_reason"):
        # stop_reason is set only on the line written when the response ended (the counters there are final); lines
        # written while it streams carry null — their output_tokens is the stream-start snapshot
        sr = getpath(obj, uspec["stop_reason"])
        m.meta["usage_final"] = bool(sr)
        if sr:
            m.meta["stop_reason"] = str(sr)
    for key in ("api_id", "request_id"):
        v = getpath(obj, uspec.get(key)) if uspec.get(key) else None
        if v:
            m.meta[key] = str(v)
