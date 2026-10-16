"""Output writers: JSON, JSONL, Markdown, CSV, our graph packet, zip.  Every
writer is a pure function of its rows (UTF-8, ``\\n`` line ends, keys in the
order the profile names them, no wall clock), so the same inputs and recipe
give the same bytes."""
import csv
import io
import json
import os
from typing import Any, Optional

from .i18n import t
from .model import canonical_json, sha256_text


def jsonl(rows: list[dict[str, Any]]) -> str:
    return "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in rows)


def json_doc(obj: Any) -> str:
    return json.dumps(obj, ensure_ascii=False, indent=1) + "\n"


def _cell(v: Any) -> str:
    if v is None:
        return ""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (dict, list)):
        return json.dumps(v, ensure_ascii=False, sort_keys=True)
    return str(v)


def csv_doc(rows: list[dict[str, Any]], columns: Optional[list[str]] = None) -> str:
    cols = list(columns or [])
    if not cols:
        for r in rows:
            for k in r:
                if k not in cols:
                    cols.append(k)
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(cols)
    for r in rows:
        w.writerow([_cell(r.get(c)) for c in cols])
    return buf.getvalue()


def _rel(target: str, from_file: str) -> str:
    return os.path.relpath(target, os.path.dirname(from_file) or ".").replace(os.sep, "/")


def _fence(text: str, lang: str = "") -> str:
    fence = "```"
    while fence in text:
        fence += "`"
    return f"{fence}{lang}\n{text}\n{fence}"


def markdown_conversation(view: dict[str, Any], md_path: str) -> str:
    c = view["conversation_full"]
    out = [f"# {c.get('title') or t('md.untitled')}", ""]
    meta = [f"{t('md.service')}: {c.get('service') or '?'}", f"{t('md.created')}: {c.get('created') or '?'}",
            f"{t('md.messages')}: {c.get('messages')}", f"{t('md.key')}: `{c.get('key')}`"]
    out.append(" · ".join(meta))
    if c.get("branch_note"):
        out += ["", f"> {c['branch_note']}"]
    for n in c.get("notes") or []:
        out += ["", f"> {n}"]
    for i, m in enumerate(view["messages_full"], 1):
        out += ["", f"## {i}. {m.get('role')} — {m.get('created') or '?'}"]
        if m.get("model"):
            out.append(f"*{t('md.model')}: {m['model']}*")
        for b in m.get("blocks") or []:
            out.append("")
            out.append(_md_block(b, md_path))
    return "\n".join(out) + "\n"


def _md_block(b: dict[str, Any], md_path: str) -> str:
    kind = b.get("kind")
    if "note" in b:
        return f"*[{kind}: {b['note']}]*"
    if "summary" in b:
        return f"*[{kind} — {t('md.summary')}]* {b['summary']}"
    if kind == "text":
        return b.get("text", "")
    if kind == "attachment" or b.get("ref"):
        name = b.get("file_name") or b.get("ref") or "?"
        if b.get("path"):
            return f"[{t('md.attachment')}: {name}]({_rel(b['path'], md_path)})"
        ref = b.get("ref")
        both = f"{name} ({ref})" if ref and ref != name else name
        return f"*[{t('md.attachment')}: {both} — {b.get('status', 'reference')}]*"
    if kind == "thinking":
        return f"<details><summary>{t('md.thinking')}</summary>\n\n{b.get('text', '')}\n\n</details>"
    if kind in ("tool_use", "tool_result"):
        head = f"**[{kind}{': ' + b['name'] if b.get('name') else ''}]**"
        body = b.get("text") or ""
        if b.get("input") is not None:
            body = (body + "\n" if body else "") + json.dumps(b["input"], ensure_ascii=False, indent=1)
        return head + ("\n" + _fence(body) if body else "")
    if kind == "code":
        return _fence(b.get("text", ""))
    text = b.get("text") or ""
    return f"**[{kind}]**" + ("\n" + text if text else "")


def markdown_index(views: list[dict[str, Any]], links: dict[str, str], md_path: str) -> str:
    out = [f"# {t('md.index')}", ""]
    for v in views:
        c = v["conversation_full"]
        target = links.get(c["key"])
        title = c.get("title") or t("md.untitled")
        line = f"- [{title}]({_rel(target, md_path)})" if target else f"- {title}"
        out.append(f"{line} — {c.get('service') or '?'}, {c.get('created') or '?'}, {c.get('messages')} {t('md.messages_short')}")
    return "\n".join(out) + "\n"


def graph_packet(views: list[dict[str, Any]], *, sources: list[dict[str, Any]], tool: dict[str, Any],
                 clusters: Optional[dict[str, Any]] = None) -> dict[str, Any]:
    """``loom.graph_packet/1``: entities (conversation, message, attachment, tool, topic, source) and edges as
    claims (subject, predicate, object) — ``part_of``, ``reply_to``, ``mentions``, ``tagged_with``,
    ``derived_from``, ``uses_tool``; every entity carries its provenance (archive, member, locator)."""
    entities: list[dict[str, Any]] = []
    claims: list[dict[str, Any]] = []
    seen: set[str] = set()

    def ent(eid: str, **kw: Any) -> str:
        if eid not in seen:
            seen.add(eid)
            entities.append({"id": eid, **{k: v for k, v in kw.items() if v is not None}})
        return eid

    def edge(s: str, p: str, o: str, text: Optional[str] = None) -> None:
        claims.append({"id": f"e{len(claims) + 1}", "subject": s, "predicate": p, "object": o,
                       "text": text or f"{s} {p} {o}"})

    for s in sources:
        ent(f"source:{s['label']}", kind="source", label=s["label"], sha256=s.get("sha256"), format=s.get("format"))
    for v in views:
        c = v["conversation_full"]
        cid = ent(f"conversation:{c['key']}" + (f"/p{c['path']}" if "path" in c else ""), kind="conversation", label=c.get("title"),
                  uid=c["uid"], created=c.get("created"), service=c.get("service"),
                  provenance={"archive": c.get("archive"), "member": c.get("member"), "locator": c.get("locator"), "format": c.get("format")})
        if c.get("archive"):
            edge(cid, "derived_from", f"source:{c['archive']}")
        for m in v["messages_full"]:
            mid = ent(f"message:{c['key']}#{m['id']}", kind="message", role=m.get("role"), created=m.get("created"),
                      text=m.get("text") or None, provenance={"conversation": c["uid"], "message": m["id"]})
            edge(mid, "part_of", cid)
            if m.get("parent"):
                edge(mid, "reply_to", f"message:{c['key']}#{m['parent']}")
            for tname in m.get("tools") or []:
                edge(mid, "uses_tool", ent(f"tool:{tname}", kind="tool", label=tname))
            for a in m.get("attachments") or []:
                aid = ent(f"attachment:{c['key']}:{a.get('ref')}", kind="attachment", label=a.get("file_name") or a.get("ref"),
                          status=a.get("status"), path=a.get("path"), mime=a.get("mime"))
                edge(mid, "mentions", aid)
    if clusters:
        keys = {v["conversation_full"]["uid"]: v["conversation_full"]["key"] for v in views}
        for cl in clusters.get("clusters") or []:
            members = [keys[u] for u in cl.get("conversations") or [] if u in keys]
            if not members:
                continue
            tid = ent(f"topic:{cl['id']}", kind="topic", label=", ".join(x["term"] for x in (cl.get("top_terms") or [])[:4])
                      if isinstance(cl.get("top_terms"), list) else cl["id"])
            for k in members:
                edge(f"conversation:{k}", "tagged_with", tid)
    body = {"entities": entities, "claims": claims}
    return {"schema": "loom.graph_packet/1", "packet_id": "sha256:" + sha256_text(canonical_json(body))[:32],
            "task": {"title": "X-Port extract", "tool": tool.get("name"), "version": tool.get("version")},
            "definitions": [], "entities": entities, "claims": claims,
            "sources": [{"id": f"source:{s['label']}", **s} for s in sources],
            "provenance": {"tool": tool.get("name"), "version": tool.get("version"), "schema": "loom.extract"}}
