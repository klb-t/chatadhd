"""Claude Code local sessions: ``~/.claude/projects/<project>/<session>.jsonl``.

One unit (kind ``session``) per file.  Lines of type user/assistant become
messages (tree by ``parentUuid``; sidechains and compact summaries flagged),
``summary`` lines become session summaries, ``system`` lines system messages,
housekeeping line types are counted, anything else goes to the unknown bucket
with an example.  Tool calls and results are kept as blocks."""
import io
import json
from typing import Any, Iterator, Optional

from ..model import Block, Message, Unit
from ..sources import Member
from .base import ReadContext, Reader, block_kind, getpath, note_unknown, finalize_unit, thinking_block, unknown_block
from .tree_walk import TreeNode, walk_tree


class ClaudeCodeReader(Reader):
    id = "claude_code"

    def iter_units(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        lspec = fmt["line"]
        types = fmt.get("line_types") or {}
        roles = fmt.get("roles") or {}
        u = Unit(kind=fmt.get("unit_kind", "session"))
        order: list[str] = []
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
        u.ext_id = session_id or member.name
        ctx.set_unit_times(u, first_ts, last_ts, mtime=member.mtime)
        leaf = order[-1] if order else None
        walk = walk_tree(order, nodes, leaf)
        for e in walk.messages:
            lineno, obj = e.payload
            ltype = str(obj.get(lspec["type"]) or "")
            role = getpath(obj, lspec["role"]) or ltype
            m = Message(id=e.id, parent=e.parent, role=roles.get(role, role), branch=e.branch, on_current_path=e.on_current,
                        raw_pointer=f"line:{lineno}")
            m.model = getpath(obj, lspec.get("model"))
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
                self._blocks(m, obj, u, fmt, ctx, lspec)
            u.messages.append(m)
        u.tree = {"roots": walk.roots, "current_leaf": walk.current_leaf, "forks": walk.forks}
        u.flags["summaries"] = summaries
        u.flags["internal_lines"] = internal
        u.flags["lines"] = lines_total
        u.flags["sidechain_messages"] = sum(1 for m in u.messages if m.meta.get("sidechain"))
        u.flags["compacted_messages"] = sum(1 for m in u.messages if m.meta.get("compacted"))
        if summaries and not u.title:
            u.title = str(summaries[-1].get("summary") or "")[:80] or None
        if not u.title:
            for m in u.messages:
                if m.role == "user" and m.text():
                    u.title = m.text().strip().splitlines()[0][:80]
                    break
        yield finalize_unit(u, member, fmt, ctx, {"line": 1, "session": session_id})

    def _blocks(self, m: Message, obj: dict[str, Any], u: Unit, fmt: dict[str, Any], ctx: ReadContext, lspec: dict[str, Any]) -> None:
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
        # tool results are bound to the tool_use name when the id is known in this session
        names = {}
        for mm in u.messages:
            for bb in mm.blocks:
                if bb.kind == "tool_use" and bb.extra.get("id"):
                    names[bb.extra["id"]] = bb.name
        for bb in m.blocks:
            if bb.kind == "tool_result" and bb.name in names:
                bb.extra["tool_use_id"] = bb.name
                bb.name = names[bb.name]


def _tool_text(inp: Any) -> str:
    if isinstance(inp, dict):
        for k in ("command", "code", "content", "query", "pattern", "prompt", "description"):
            if isinstance(inp.get(k), str):
                return inp[k]
    return ""
