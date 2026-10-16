"""ChatGPT data export: conversations.json / conversations-NNN.json (array or
``{"conversations": [...]}`` wrapper), mapping trees, every content type kept
(text, multimodal parts, thoughts, code, execution output, browsing, quotes,
contexts, errors, unknown), attachments by pointer, non-conversation members
(user, feedback, shared links, textdocs, chat.html)."""
import re
from typing import Any, Iterator, Optional

from ..model import Attachment, Block, Message, Unit, json_pointer
from ..sources import Member, iter_json_array
from .base import (ReadContext, Reader, block_kind, first_of, getpath, load_member_json, note_unknown,
                   finalize_unit, thinking_block, unit_for_record, unknown_block)
from .tree_walk import TreeNode, walk_tree

_POINTER_RE = re.compile(r"(file[-_][A-Za-z0-9_-]+)")


def pointer_id(value: Any) -> Optional[str]:
    if not isinstance(value, str):
        return None
    m = _POINTER_RE.search(value)
    return m.group(1) if m else value.rsplit("/", 1)[-1] or None


class ChatGPTReader(Reader):
    id = "chatgpt"

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
            wrappers = fmt.get("wrappers") or []
            for w in wrappers:
                if isinstance(obj, dict) and isinstance(obj.get(w), list):
                    for i, conv in enumerate(obj[w]):
                        yield self._conversation(conv, json_pointer(w, i), member, fmt, ctx)
                    return
            if isinstance(obj, dict) and "mapping" in obj:
                yield self._conversation(obj, "", member, fmt, ctx)
            else:
                u = Unit(kind="unknown", title=member.name)
                note_unknown(u, "", obj)
                yield finalize_unit(u, member, fmt, ctx, {"json_pointer": ""})

    # -- non-conversation members ---------------------------------------------------
    def _records(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        kind = member.kind
        if not member.name.lower().endswith(".json"):
            u = Unit(kind=kind, title=member.name)
            yield finalize_unit(u, member, fmt, ctx, {"member": member.name})
            return
        try:
            obj = load_member_json(member)
        except Exception as exc:  # noqa: BLE001 - reported, never dropped
            u = Unit(kind="unknown", title=member.name)
            note_unknown(u, "", f"unreadable JSON: {exc}")
            yield finalize_unit(u, member, fmt, ctx, {"member": member.name})
            return
        if isinstance(obj, list):
            for i, rec in enumerate(obj):
                title = first_of(rec, ["title", "name", "email", "id"]) if isinstance(rec, dict) else None
                yield unit_for_record(member, fmt, kind, rec, ctx, index=i, title=str(title) if title else None,
                                      ext_id=str(first_of(rec, ["id", "conversation_id"]) or "") or None
                                      if isinstance(rec, dict) else None)
        else:
            title = first_of(obj, ["title", "name", "email", "id"]) if isinstance(obj, dict) else None
            yield unit_for_record(member, fmt, kind, obj, ctx, title=str(title) if title else None,
                                  ext_id=str(first_of(obj, ["id"]) or "") or None if isinstance(obj, dict) else None)

    # -- one conversation -----------------------------------------------------------
    def _conversation(self, conv: Any, pointer: str, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Unit:
        cspec = fmt["conversation"]
        mspec = fmt["message"]
        u = Unit(kind="conversation")
        if not isinstance(conv, dict):
            note_unknown(u, pointer, conv)
            return finalize_unit(u, member, fmt, ctx, {"json_pointer": pointer})
        u.raw = conv
        u.ext_id = first_of(conv, cspec["ext_id"])
        u.title = conv.get(cspec["title"])
        ctx.set_unit_times(u, getpath(conv, cspec["created"]["path"]), getpath(conv, cspec["updated"]["path"]))
        model = getpath(conv, cspec.get("model"))
        if model:
            u.flags["model"] = model
        for k in ("gizmo_id", "conversation_template_id", "is_archived", "is_starred"):
            if conv.get(k) not in (None, False):
                u.flags[k] = conv[k]
        mapping = conv.get(cspec["tree"])
        if not isinstance(mapping, dict):
            if mapping is not None:
                note_unknown(u, pointer + "/mapping", mapping)
            return finalize_unit(u, member, fmt, ctx, {"json_pointer": pointer})
        order: list[str] = []
        nodes: dict[str, TreeNode] = {}
        for nid, node in mapping.items():
            parent = node.get("parent") if isinstance(node, dict) else None
            msg = node.get("message") if isinstance(node, dict) else None
            nodes[nid] = TreeNode(parent if isinstance(parent, str) else None, isinstance(msg, dict), msg)
            order.append(nid)
        current = conv.get(cspec["current"])
        walk = walk_tree(order, nodes, current if isinstance(current, str) else None)
        roles = fmt.get("roles") or {}
        for e in walk.messages:
            raw = e.payload
            role = getpath(raw, mspec["role"]) or "unknown"
            m = Message(id=e.id, parent=e.parent, role=roles.get(role, role), branch=e.branch, on_current_path=e.on_current,
                        raw_pointer=pointer + json_pointer("mapping", e.id, "message"))
            m.author = getpath(raw, mspec.get("author_name"))
            m.model = getpath(raw, mspec.get("model"))
            ctx.set_msg_time(m, getpath(raw, mspec["created"]["path"]), fallback=u.created_utc)
            for k in ("status", "end_turn", "weight", "recipient", "channel"):
                v = raw.get(k)
                if v not in (None, "all", 1.0, 1):
                    m.meta[k] = v
            self._blocks(m, raw, u, fmt, ctx, m.raw_pointer or "")
            u.messages.append(m)
        u.tree = {"roots": walk.roots, "current_leaf": walk.current_leaf, "forks": walk.forks,
                  "fork_points_raw": walk.fork_points_raw, "leaves_raw": walk.leaves_raw,
                  "current_path_len": walk.current_path_len}
        if walk.unreachable:
            u.tree["unreachable"] = walk.unreachable
            for m in u.messages:
                if m.id in walk.unreachable:
                    m.meta["unreachable"] = True
        return finalize_unit(u, member, fmt, ctx, {"json_pointer": pointer})

    # -- blocks of one message ------------------------------------------------------
    def _blocks(self, m: Message, raw: dict[str, Any], u: Unit, fmt: dict[str, Any], ctx: ReadContext, ptr: str) -> None:
        content = raw.get("content")
        md = raw.get("metadata") if isinstance(raw.get("metadata"), dict) else {}
        if isinstance(content, str):
            m.blocks.append(Block(kind="text", text=content, native="string"))
        elif isinstance(content, dict):
            ct = content.get("content_type")
            kind, mod = block_kind(fmt, ct)
            if kind == "text" or (ct in ("text", "multimodal_text")) or mod == "<parts>" or fmt["blocks"].get(ct) == "<parts>":
                self._parts(m, u, content, fmt, ct)
            elif kind == "thinking":
                if ct == "thoughts":
                    for th in content.get("thoughts") or []:
                        txt = th.get("content") if isinstance(th, dict) else str(th)
                        b = thinking_block(txt or "", ct, mod, ctx)
                        if isinstance(th, dict) and th.get("summary"):
                            b.extra["summary"] = th["summary"]
                        m.blocks.append(b)
                else:
                    m.blocks.append(thinking_block(str(content.get("content") or ""), ct, mod, ctx))
            elif kind == "tool_use":
                recipient = raw.get("recipient")
                name = recipient if recipient and recipient != "all" else (content.get("language") or ct)
                m.blocks.append(Block(kind="tool_use", text=str(content.get("text") or ""), native=ct, name=name,
                                      input={"code": content.get("text"), "language": content.get("language")} if content.get("text") else None))
            elif kind == "tool_result":
                recipient = raw.get("recipient")
                name = getpath(raw, "author.name") or (recipient if recipient and recipient != "all" else ct)
                txt = content.get("text") or content.get("result") or ""
                m.blocks.append(Block(kind="tool_result", text=str(txt), native=ct, name=name,
                                      input={k: v for k, v in content.items() if k not in ("content_type", "text", "result")} or None))
            elif kind == "citation":
                m.blocks.append(Block(kind="citation", text=str(content.get("text") or content.get("title") or ""), native=ct,
                                      extra={k: v for k, v in content.items() if k in ("url", "title", "domain")}))
            elif kind == "context":
                if ct == "user_editable_context":
                    for k in ("user_profile", "user_instructions"):
                        if isinstance(content.get(k), str):
                            m.blocks.append(Block(kind="context", text=content[k], native=ct, name=k))
                else:
                    txt = content.get("model_set_context") or content.get("text") or ""
                    m.blocks.append(Block(kind="context", text=str(txt), native=ct, name=ct))
            elif kind == "error":
                m.blocks.append(Block(kind="error", text=str(content.get("text") or ""), native=ct, name=content.get("name")))
            elif kind == "unknown":
                m.blocks.append(unknown_block(ct, content))
                note_unknown(u, ptr + "/content", content, native=ct)
            else:
                m.blocks.append(Block(kind=kind, text=str(content.get("text") or ""), native=ct))
        elif content is not None:
            m.blocks.append(unknown_block(type(content).__name__, content))
            note_unknown(u, ptr + "/content", content)
        # attachments from metadata
        for att in md.get("attachments") or []:
            if not isinstance(att, dict):
                continue
            ref = pointer_id(att.get("id")) or att.get("name") or "?"
            a = Attachment(ref=ref, name=att.get("name"), mime=att.get("mime_type") or att.get("mimeType"),
                           size=att.get("size"), status="reference", message=m.id)
            u.attachments.append(a)
        for img in (md.get("aggregate_result") or {}).get("messages") or []:
            if isinstance(img, dict) and img.get("image_url"):
                ref = pointer_id(img["image_url"]) or "?"
                u.attachments.append(Attachment(ref=ref, name=None, mime="image/png", status="reference", message=m.id))
        for c in (md.get("citations") or []) + (md.get("content_references") or []):
            if isinstance(c, dict):
                meta = c.get("metadata") if isinstance(c.get("metadata"), dict) else c
                m.blocks.append(Block(kind="citation", text=str(meta.get("title") or meta.get("text") or ""), native="metadata.citations",
                                      extra={k: v for k, v in meta.items() if k in ("url", "title", "type")}))
        for g in md.get("search_result_groups") or []:
            if isinstance(g, dict):
                m.blocks.append(Block(kind="search_result", native="search_result_groups", name=g.get("domain"),
                                      text="\n".join(str(e.get("title") or e.get("url") or "") for e in g.get("entries") or [] if isinstance(e, dict))))

    def _parts(self, m: Message, u: Unit, content: dict[str, Any], fmt: dict[str, Any], ct: Optional[str]) -> None:
        parts = content.get("parts")
        if parts is None:
            if "text" in content:
                m.blocks.append(Block(kind="text", text=str(content["text"]), native=ct))
            return
        for part in parts if isinstance(parts, list) else [parts]:
            if isinstance(part, str):
                m.blocks.append(Block(kind="text", text=part, native=ct))
            elif isinstance(part, dict):
                pct = part.get("content_type") or part.get("type")
                kind, _ = block_kind(fmt, pct, table="parts")
                if kind == "attachment":
                    ref = pointer_id(part.get("asset_pointer")) or "?"
                    m.blocks.append(Block(kind="attachment", native=pct, ref=ref, mime=_mime_for(pct, part),
                                          extra={k: v for k, v in part.items() if k in ("width", "height", "size_bytes")}))
                    # the reference is registered on the unit so it is resolved or listed as missing, never dropped
                    if not any(a.ref == ref and a.message == m.id for a in u.attachments):
                        u.attachments.append(Attachment(ref=ref, name=None, mime=_mime_for(pct, part), size=part.get("size_bytes"),
                                                        status="reference", message=m.id))
                elif kind == "text":
                    m.blocks.append(Block(kind="text", text=str(part.get("text") or ""), native=pct))
                elif kind == "search_result":
                    m.blocks.append(Block(kind="search_result", native=pct, name=part.get("domain"),
                                          text="\n".join(str(e.get("title") or "") for e in part.get("entries") or [] if isinstance(e, dict))))
                elif kind == "unknown":
                    m.blocks.append(unknown_block(pct, part))
                else:
                    m.blocks.append(Block(kind=kind, text=str(part.get("text") or ""), native=pct))
            else:
                m.blocks.append(unknown_block(type(part).__name__, part))


def _mime_for(pct: Optional[str], part: dict[str, Any]) -> Optional[str]:
    if pct == "image_asset_pointer":
        return "image/*"
    if pct == "audio_asset_pointer":
        return "audio/*"
    if pct == "real_time_user_audio_video_asset_pointer":
        return "video/*"
    return part.get("mime_type")
