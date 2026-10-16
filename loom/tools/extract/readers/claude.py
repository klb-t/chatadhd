"""Claude.ai data export: conversations.json (array) or a single chat object;
``chat_messages`` with ``parent_message_uuid`` trees (linear chain when no
parents), content blocks (text, thinking, tool_use, tool_result, image,
document, voice_note, unknown), attachments/files/files_v2, and the other
members (users, projects, memories)."""
from typing import Any, Iterator, Optional

from ..model import Attachment, Block, Message, Unit, json_pointer
from ..sources import Member, iter_json_array
from .base import (ReadContext, Reader, block_kind, first_of, getpath, load_member_json, note_unknown,
                   finalize_unit, thinking_block, unit_for_record, unknown_block)
from .tree_walk import TreeNode, linear_chain, walk_tree


class ClaudeReader(Reader):
    id = "claude"

    def iter_units(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        if member.kind != "conversations":
            yield from self._records(member, fmt, ctx)
            return
        head = member.head(64).lstrip()[:1]
        if head == b"[":
            with member.open() as fh:
                for i, conv in enumerate(iter_json_array(fh)):
                    yield self._conversation(conv, json_pointer(i), member, fmt, ctx)
        else:
            obj = load_member_json(member)
            if isinstance(obj, dict) and "chat_messages" in obj:
                yield self._conversation(obj, "", member, fmt, ctx)
            else:
                u = Unit(kind="unknown", title=member.name)
                note_unknown(u, "", obj)
                yield finalize_unit(u, member, fmt, ctx, {"json_pointer": ""})

    def _records(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        try:
            obj = load_member_json(member)
        except Exception as exc:  # noqa: BLE001
            u = Unit(kind="unknown", title=member.name)
            note_unknown(u, "", f"unreadable JSON: {exc}")
            yield finalize_unit(u, member, fmt, ctx, {"member": member.name})
            return
        if member.kind == "memory" and isinstance(obj, dict):
            # memories.json: one memory per text field and per project entry (shape inferred, as in the oracle)
            recs = []
            for k, v in obj.items():
                if isinstance(v, str) and k not in ("updated_at", "created_at"):
                    recs.append({"uuid": k, "name": k, "text": v})
                elif isinstance(v, dict):
                    for pk, pv in v.items():
                        recs.append({"uuid": f"{k}/{pk}", "name": f"{k}/{pk}", "text": pv})
        else:
            recs = obj if isinstance(obj, list) else [obj]
        for i, rec in enumerate(recs):
            title = first_of(rec, ["name", "title", "full_name", "email_address", "uuid"]) if isinstance(rec, dict) else None
            u = unit_for_record(member, fmt, member.kind, rec, ctx, index=i if isinstance(obj, list) else None,
                                title=str(title) if title else None,
                                ext_id=str(first_of(rec, ["uuid", "id"]) or "") or None if isinstance(rec, dict) else None)
            if member.kind == "project" and isinstance(rec, dict):
                docs = rec.get("docs") or rec.get("documents") or []
                u.flags["docs"] = len(docs) if isinstance(docs, list) else 0
            yield u

    def _conversation(self, conv: Any, pointer: str, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Unit:
        cspec, mspec = fmt["conversation"], fmt["message"]
        u = Unit(kind="conversation")
        if not isinstance(conv, dict):
            note_unknown(u, pointer, conv)
            return finalize_unit(u, member, fmt, ctx, {"json_pointer": pointer})
        u.raw = conv
        u.ext_id = first_of(conv, cspec["ext_id"])
        u.title = first_of(conv, cspec["title"])
        ctx.set_unit_times(u, getpath(conv, cspec["created"]["path"]), getpath(conv, cspec["updated"]["path"]))
        acct = getpath(conv, cspec.get("account"))
        if acct:
            u.account["id"] = acct
        if conv.get("model"):
            u.flags["model"] = conv["model"]
        if conv.get("project_uuid") or conv.get("project"):
            u.flags["project"] = conv.get("project_uuid") or getpath(conv, "project.uuid")
        msgs = conv.get(cspec["tree"])
        if not isinstance(msgs, list):
            if msgs is not None:
                note_unknown(u, pointer + "/chat_messages", msgs)
            return finalize_unit(u, member, fmt, ctx, {"json_pointer": pointer})
        order: list[str] = []
        nodes: dict[str, TreeNode] = {}
        has_parents = False
        payload_index: dict[str, int] = {}
        for i, m in enumerate(msgs):
            if not isinstance(m, dict):
                note_unknown(u, pointer + json_pointer("chat_messages", i), m)
                continue
            mid = m.get("uuid")
            if not isinstance(mid, str) or not mid or mid in nodes:
                mid = f"#{i}"
            parent = m.get(cspec["parent"])
            if isinstance(parent, str) and parent:
                has_parents = True
            nodes[mid] = TreeNode(parent if isinstance(parent, str) and parent else None, True, m)
            order.append(mid)
            payload_index[mid] = i
        if not has_parents:
            linear_chain(order, nodes)
        leaf = conv.get(cspec["current"])
        if not isinstance(leaf, str) or leaf not in nodes:
            leaf = order[-1] if order else None
        walk = walk_tree(order, nodes, leaf)
        roles = fmt.get("roles") or {}
        for e in walk.messages:
            raw = e.payload
            sender = raw.get(mspec["role"]) or "human"
            m = Message(id=e.id, parent=e.parent, role=roles.get(sender, sender), branch=e.branch, on_current_path=e.on_current,
                        raw_pointer=pointer + json_pointer("chat_messages", payload_index[e.id]))
            ctx.set_msg_time(m, getpath(raw, mspec["created"]["path"]), fallback=u.created_utc)
            self._blocks(m, raw, u, fmt, ctx, m.raw_pointer or "")
            for key in (fmt.get("attachments") or {}).get("fields", []):
                for att in raw.get(key) or []:
                    if isinstance(att, dict):
                        name = first_of(att, (fmt.get("attachments") or {}).get("name_fields", ["file_name", "name"]))
                        ref = first_of(att, ["file_uuid", "id", "uuid"]) or name or "?"
                        u.attachments.append(Attachment(ref=str(ref), name=name, mime=att.get("file_type") or att.get("mime_type"),
                                                        size=att.get("file_size") or att.get("size"), status="reference", message=m.id))
                        if att.get("extracted_content"):
                            m.blocks.append(Block(kind="attachment", native=key, ref=str(ref), text=str(att["extracted_content"]),
                                                  mime=att.get("file_type")))
            u.messages.append(m)
        u.tree = {"roots": walk.roots, "current_leaf": walk.current_leaf, "forks": walk.forks,
                  "fork_points_raw": walk.fork_points_raw if has_parents else 0,
                  "leaves_raw": walk.leaves_raw if has_parents else (1 if order else 0),
                  "current_path_len": walk.current_path_len if has_parents else len(order), "linear": not has_parents}
        if walk.unreachable:
            u.tree["unreachable"] = walk.unreachable
            for m in u.messages:
                if m.id in walk.unreachable:
                    m.meta["unreachable"] = True
        return finalize_unit(u, member, fmt, ctx, {"json_pointer": pointer})

    def _blocks(self, m: Message, raw: dict[str, Any], u: Unit, fmt: dict[str, Any], ctx: ReadContext, ptr: str) -> None:
        content = raw.get(fmt["message"]["content"])
        if isinstance(content, list) and content:
            for i, b in enumerate(content):
                if not isinstance(b, dict):
                    m.blocks.append(unknown_block(type(b).__name__, b))
                    continue
                bt = b.get("type")
                kind, mod = block_kind(fmt, bt)
                if kind == "text":
                    m.blocks.append(Block(kind="text", text=str(b.get("text") or ""), native=bt))
                    for c in b.get("citations") or []:
                        if isinstance(c, dict):
                            m.blocks.append(Block(kind="citation", native="citations", text=str(c.get("title") or c.get("url") or ""),
                                                  extra={k: v for k, v in c.items() if k in ("url", "title")}))
                elif kind == "thinking":
                    m.blocks.append(thinking_block(str(b.get("thinking") or b.get("text") or ""), bt, mod, ctx))
                elif kind == "tool_use":
                    m.blocks.append(Block(kind="tool_use", native=bt, name=b.get("name"), input=b.get("input"),
                                          text=_tool_text(b.get("input"))))
                elif kind == "tool_result":
                    txt = b.get("content")
                    if isinstance(txt, list):
                        txt = "\n".join(str(x.get("text") or "") for x in txt if isinstance(x, dict))
                    m.blocks.append(Block(kind="tool_result", native=bt, name=b.get("name") or b.get("tool_use_id"),
                                          text=str(txt or ""), input={"is_error": b.get("is_error")} if b.get("is_error") else None))
                elif kind == "attachment":
                    src = b.get("source") if isinstance(b.get("source"), dict) else {}
                    ref = str(first_of(b, ["file_uuid", "file_id", "id"]) or src.get("file_id") or b.get("name") or f"{bt}#{i}")
                    m.blocks.append(Block(kind="attachment", native=bt, ref=ref, mime=src.get("media_type") or b.get("media_type"),
                                          name=b.get("name") or b.get("title")))
                elif kind == "unknown":
                    m.blocks.append(unknown_block(bt, b))
                    note_unknown(u, ptr + json_pointer("content", i), b, native=bt)
                else:
                    m.blocks.append(Block(kind=kind, text=str(b.get("text") or ""), native=bt))
        else:
            text = raw.get(fmt["message"].get("text", "text"))
            if isinstance(text, str) and text:
                m.blocks.append(Block(kind="text", text=text, native="text"))
            elif content not in (None, [], ""):
                m.blocks.append(unknown_block(type(content).__name__, content))
                note_unknown(u, ptr + "/content", content)


def _tool_text(inp: Any) -> str:
    if isinstance(inp, dict):
        for k in ("command", "code", "content", "query", "text"):
            if isinstance(inp.get(k), str):
                return inp[k]
    return ""
