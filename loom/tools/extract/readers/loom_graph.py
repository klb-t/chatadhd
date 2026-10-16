"""Our own formats: the graph packet (``loom.graph_packet/1``) and the
``loom.export.v1`` conversation / hand-off shape (``metadata.export``), plus
re-reading a ``loom.extract.unit/1`` snapshot (``units.jsonl`` of a package)."""
import io
import json
from typing import Any, Iterator, Optional

from ..model import Attachment, Block, Message, Unit, json_pointer
from ..sources import Member
from .base import ReadContext, Reader, first_of, getpath, load_member_json, note_unknown, finalize_unit, unknown_block
from .tree_walk import TreeNode, linear_chain, walk_tree


class LoomGraphReader(Reader):
    id = "loom_graph"

    def iter_units(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        packet = load_member_json(member)
        u = Unit(kind=fmt.get("unit_kind", "graph"))
        if not isinstance(packet, dict):
            note_unknown(u, "", packet)
            yield finalize_unit(u, member, fmt, ctx, {"json_pointer": ""})
            return
        u.raw = packet
        u.ext_id = packet.get("packet_id")
        task = packet.get("task") if isinstance(packet.get("task"), dict) else {}
        u.title = str(first_of(task, ["title", "name", "goal", "id"]) or member.name)
        cspec = fmt.get("claim") or {}
        prev: Optional[str] = None
        counts: dict[str, int] = {}
        for coll in fmt.get("collections") or []:
            items = packet.get(coll)
            counts[coll] = len(items) if isinstance(items, list) else 0
            if coll != "claims" or not isinstance(items, list):
                continue
            for i, c in enumerate(items):
                if not isinstance(c, dict):
                    note_unknown(u, json_pointer(coll, i), c)
                    continue
                mid = str(c.get(cspec.get("id", "id")) or f"claim{i}")
                m = Message(id=mid, parent=prev, role="claim", raw_pointer=json_pointer(coll, i))
                ctx.set_msg_time(m, getpath(c, (cspec.get("created") or {}).get("path", "known_at")), fallback=None)
                text = first_of(c, cspec.get("text", ["text"]))
                if isinstance(text, str):
                    m.blocks.append(Block(kind="text", text=text, native="claim"))
                else:
                    m.blocks.append(unknown_block("claim", c))
                if c.get(cspec.get("subject", "subject")):
                    m.meta["subject"] = c[cspec.get("subject", "subject")]
                u.messages.append(m)
                prev = mid
        for key in packet:
            if key not in set(fmt.get("collections") or []) | {"schema", "packet_id", "task", "provenance", "history", "annotations"}:
                note_unknown(u, "/" + key, packet[key], native=key)
        u.flags["collections"] = counts
        u.tree = {"roots": [u.messages[0].id] if u.messages else [], "current_leaf": prev, "forks": [], "linear": True}
        if u.messages:
            first = min((m.created_utc for m in u.messages if m.created_utc), default=None)
            last = max((m.created_utc for m in u.messages if m.created_utc), default=None)
            u.created_utc, u.updated_utc = first, last
            if first:
                u.created_how, u.created_inferred = "first_message", True
        yield finalize_unit(u, member, fmt, ctx, {"json_pointer": ""})


class LoomExportReader(Reader):
    id = "loom_export"

    def iter_units(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        if member.name.endswith(".jsonl"):
            with member.open() as fh, io.TextIOWrapper(fh, encoding="utf-8", errors="replace") as text:
                for lineno, raw in enumerate(text, 1):
                    raw = raw.strip()
                    if raw:
                        d = json.loads(raw)
                        if isinstance(d, dict) and d.get("schema") == "loom.extract.unit/1":
                            yield self._unit_snapshot(d, member, fmt, ctx, f"line:{lineno}")
            return
        obj = load_member_json(member)
        if isinstance(obj, dict) and obj.get("schema") == "loom.extract.unit/1":
            yield self._unit_snapshot(obj, member, fmt, ctx, "")
            return
        if isinstance(obj, dict) and obj.get("schema") == "loom.extract.handoff/1":
            u = Unit(kind="member", title=member.name)
            u.flags["handoff"] = {k: obj.get(k) for k in ("from_manifest", "profile", "attachments")}
            yield finalize_unit(u, member, fmt, ctx, {"json_pointer": ""})
            return
        convs = obj if isinstance(obj, list) else [obj]
        for i, conv in enumerate(convs):
            yield self._conversation(conv, json_pointer(i) if isinstance(obj, list) else "", member, fmt, ctx)

    def _unit_snapshot(self, d: dict[str, Any], member: Member, fmt: dict[str, Any], ctx: ReadContext, ptr: str) -> Unit:
        u = Unit.from_dict(d)
        origin = dict(u.source)
        u.flags["snapshot_source"] = origin
        return finalize_unit(u, member, fmt, ctx, {"json_pointer": ptr, "origin": origin.get("locator")})

    def _conversation(self, conv: Any, pointer: str, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Unit:
        cspec, mspec = fmt["conversation"], fmt["message"]
        u = Unit(kind="conversation")
        if not isinstance(conv, dict):
            note_unknown(u, pointer, conv)
            return finalize_unit(u, member, fmt, ctx, {"json_pointer": pointer})
        u.raw = conv
        u.ext_id = first_of(conv, cspec["ext_id"])
        u.title = conv.get(cspec["title"])
        ctx.set_unit_times(u, getpath(conv, cspec["created"]["path"]), conv.get("updated"))
        export = getpath(conv, "metadata.export") or {}
        if isinstance(export, dict):
            if export.get("provider"):
                u.account["service"] = export["provider"]
            u.flags["export"] = {k: export.get(k) for k in ("schema", "provider", "member", "locator") if k in export}
        roles = fmt.get("roles") or {}
        msgs = conv.get(cspec["messages"]) or []
        order: list[str] = []
        nodes: dict[str, TreeNode] = {}
        has_parents = False
        for i, m in enumerate(msgs):
            if not isinstance(m, dict):
                note_unknown(u, pointer + json_pointer("messages", i), m)
                continue
            mid = str(m.get(mspec.get("id", "id")) or f"m{i}")
            if mid in nodes:
                mid = f"m{i}"
            parent = m.get(mspec.get("parent", "parent_id"))
            if isinstance(parent, str) and parent:
                has_parents = True
            nodes[mid] = TreeNode(parent if isinstance(parent, str) and parent else None, True, (i, m))
            order.append(mid)
        if not has_parents:
            linear_chain(order, nodes)
        walk = walk_tree(order, nodes, order[-1] if order else None)
        for e in walk.messages:
            i, m = e.payload
            role = str(m.get(mspec["role"]) or "user")
            msg = Message(id=e.id, parent=e.parent, role=roles.get(role, role), branch=e.branch, on_current_path=e.on_current,
                          raw_pointer=pointer + json_pointer("messages", i))
            ctx.set_msg_time(msg, getpath(m, mspec["created"]["path"]), fallback=u.created_utc)
            blocks = getpath(m, mspec.get("blocks"))
            if isinstance(blocks, list) and blocks:
                for b in blocks:
                    if isinstance(b, dict) and b.get("kind"):
                        msg.blocks.append(Block(kind=b["kind"], text=str(b.get("text") or ""), native=b.get("native"),
                                                name=b.get("name"), input=b.get("input"), ref=b.get("ref"), mime=b.get("mime")))
                    else:
                        msg.blocks.append(unknown_block("block", b))
            else:
                text = m.get(mspec["text"])
                if isinstance(text, str):
                    msg.blocks.append(Block(kind="text", text=text, native="content"))
                elif text is not None:
                    msg.blocks.append(unknown_block("content", text))
            for a in m.get("attachments") or []:
                ref = str(a if isinstance(a, str) else (a.get("ref") or a.get("path") or "?"))
                u.attachments.append(Attachment(ref=ref, name=ref.rsplit("/", 1)[-1], status="reference", message=msg.id))
            if m.get("model"):
                msg.model = m["model"]
            u.messages.append(msg)
        u.tree = {"roots": walk.roots, "current_leaf": walk.current_leaf, "forks": walk.forks, "linear": not has_parents}
        return finalize_unit(u, member, fmt, ctx, {"json_pointer": pointer})
