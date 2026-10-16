"""Grok: grok.com export (``{"conversations": [{"conversation": …, "responses": […]}]}``)
and the X archive (``data/grok-chat-item.js``).  HYPOTHESIS format
(``formats/xai_grok.json``); a correction is an edit of that file."""
import json
import re
from typing import Any, Iterator, Optional

from ..model import Block, Message, Unit, json_pointer
from ..sources import Member, iter_json_array
from .base import ReadContext, Reader, first_of, getpath, load_member_json, note_unknown, finalize_unit, unknown_block
from .tree_walk import TreeNode, linear_chain, walk_tree


class GrokReader(Reader):
    id = "grok"

    def iter_units(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        head = member.head(4096)
        if b"window.YTD" in head or member.name.endswith(".js"):
            yield from self._x_archive(member, fmt, ctx)
            return
        first = head.lstrip()[:1]
        shape = fmt["shapes"]["grok_com"]
        if first == b"[":
            with member.open() as fh:
                for i, conv in enumerate(iter_json_array(fh)):
                    yield self._grok_com(conv, json_pointer(i), member, fmt, shape, ctx)
        else:
            obj = load_member_json(member)
            lst = obj.get(shape["list"]) if isinstance(obj, dict) else None
            if isinstance(lst, list):
                for i, conv in enumerate(lst):
                    yield self._grok_com(conv, json_pointer(shape["list"], i), member, fmt, shape, ctx)
            elif isinstance(obj, dict) and shape["conversation"] in obj:
                yield self._grok_com(obj, "", member, fmt, shape, ctx)
            else:
                u = Unit(kind="unknown", title=member.name)
                note_unknown(u, "", obj)
                yield finalize_unit(u, member, fmt, ctx, {"json_pointer": ""})

    def _grok_com(self, conv: Any, pointer: str, member: Member, fmt: dict[str, Any], shape: dict[str, Any],
                  ctx: ReadContext) -> Unit:
        u = Unit(kind="conversation")
        if not isinstance(conv, dict):
            note_unknown(u, pointer, conv)
            return finalize_unit(u, member, fmt, ctx, {"json_pointer": pointer})
        u.raw = conv
        u.ext_id = first_of(conv, shape["ext_id"])
        u.title = getpath(conv, shape["title"])
        ctx.set_unit_times(u, getpath(conv, shape["created"]["path"]), getpath(conv, "conversation.modify_time"))
        mspec = shape["message"]
        roles = fmt.get("roles") or {}
        order: list[str] = []
        nodes: dict[str, TreeNode] = {}
        has_parents = False
        for i, wrapper in enumerate(conv.get(shape["responses"]) or []):
            r = wrapper.get(shape["response"]) if isinstance(wrapper, dict) and shape["response"] in wrapper else wrapper
            if not isinstance(r, dict):
                note_unknown(u, pointer + json_pointer(shape["responses"], i), wrapper)
                continue
            mid = str(r.get(mspec["id"]) or f"#{i}")
            if mid in nodes:
                mid = f"#{i}"
            parent = r.get(mspec.get("parent", "")) if mspec.get("parent") else None
            if isinstance(parent, str) and parent:
                has_parents = True
            nodes[mid] = TreeNode(parent if isinstance(parent, str) and parent else None, True, (i, r))
            order.append(mid)
        if not has_parents:
            linear_chain(order, nodes)
        walk = walk_tree(order, nodes, order[-1] if order else None)
        for e in walk.messages:
            i, r = e.payload
            sender = str(r.get(mspec["role"]) or "assistant")
            m = Message(id=e.id, parent=e.parent, role=roles.get(sender, sender.lower()), branch=e.branch,
                        on_current_path=e.on_current, raw_pointer=pointer + json_pointer(shape["responses"], i, shape["response"]))
            m.model = r.get(mspec.get("model", "model"))
            ctx.set_msg_time(m, getpath(r, mspec["created"]["path"]), fallback=u.created_utc)
            text = r.get(mspec["text"])
            if isinstance(text, str):
                m.blocks.append(Block(kind="text", text=text, native="message"))
            elif text is not None:
                m.blocks.append(unknown_block("message", text))
            for k in r:
                if k not in (mspec["id"], mspec.get("parent"), mspec["role"], mspec["text"], mspec["created"]["path"],
                             mspec.get("model", "model"), "query", "partial", "shared", "feedback"):
                    note_unknown(u, m.raw_pointer + "/" + k, r[k], native=k)
            u.messages.append(m)
        u.tree = {"roots": walk.roots, "current_leaf": walk.current_leaf, "forks": walk.forks, "linear": not has_parents}
        return finalize_unit(u, member, fmt, ctx, {"json_pointer": pointer})

    def _x_archive(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        shape = fmt["shapes"]["x_archive"]
        text = member.read_bytes().decode("utf-8", errors="replace")
        m = re.match(r"\s*window\.YTD\.[A-Za-z_]+\.part\d+\s*=\s*", text)
        payload = text[m.end():] if m else text
        try:
            items = json.loads(payload.rstrip().rstrip(";"))
        except json.JSONDecodeError as exc:
            u = Unit(kind="unknown", title=member.name)
            note_unknown(u, "", f"unreadable JS payload: {exc}")
            yield finalize_unit(u, member, fmt, ctx, {"member": member.name})
            return
        by_chat: dict[str, list[tuple[int, dict[str, Any]]]] = {}
        order_of: list[str] = []
        for i, it in enumerate(items if isinstance(items, list) else []):
            rec = it.get(shape["item"]) if isinstance(it, dict) else None
            if not isinstance(rec, dict):
                continue
            cid = str(rec.get(shape["ext_id"]) or "?")
            if cid not in by_chat:
                order_of.append(cid)
            by_chat.setdefault(cid, []).append((i, rec))
        mspec = shape["message"]
        roles = fmt.get("roles") or {}
        for cid in order_of:
            recs = by_chat[cid]
            u = Unit(kind="conversation", ext_id=cid)
            first_text = next((r.get(mspec["text"]) for _, r in recs if isinstance(r.get(mspec["text"]), str)), None)
            u.title = first_text[:80] if first_text else None
            ctx.set_unit_times(u, getpath(recs[0][1], mspec["created"]["path"]), getpath(recs[-1][1], mspec["created"]["path"]))
            prev: Optional[str] = None
            for i, r in recs:
                sender = str(r.get(mspec["role"]) or "assistant")
                msg = Message(id=f"i{i}", parent=prev, role=roles.get(sender, sender.lower()), raw_pointer=json_pointer(i, shape["item"]))
                ctx.set_msg_time(msg, getpath(r, mspec["created"]["path"]), fallback=u.created_utc)
                t = r.get(mspec["text"])
                if isinstance(t, str):
                    msg.blocks.append(Block(kind="text", text=t, native="message"))
                for k in r:
                    if k not in (shape["ext_id"], mspec["text"], mspec["role"], mspec["created"]["path"]):
                        note_unknown(u, msg.raw_pointer + "/" + k, r[k], native=k)
                u.messages.append(msg)
                prev = msg.id
            u.tree = {"roots": [u.messages[0].id] if u.messages else [], "current_leaf": prev, "forks": [], "linear": True}
            yield finalize_unit(u, member, fmt, ctx, {"json_pointer": json_pointer(recs[0][0])})
