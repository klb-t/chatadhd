"""Hand-off package for the app's importer (``handoff/``): a directory the app
imports in one step, using only fields the importers already understand
(``title``, ``messages[].role/content/created``) with everything else in
``metadata.export`` (``loom.export.v1``: provider, locator, native blocks,
branch, status) — so neither importer learns a new format.  Also a
``handoff.zip`` (conversations + attachments) for ``ConversationImporter.
import_file`` in one call.  Counts in ``handoff.json`` say what each importer
will see: messages, and messages with text (the Python importer skips empty
ones)."""
from pathlib import Path
from typing import Any, Optional

from . import outputs as out
from .cut.common import Entry, Outputs, archive_slug, copy_stream, deterministic_zip
from .cut.views import choose_paths


def build_handoff(eng: Any, entries: list[Entry], stage: Outputs, *, branches: str = "current", with_graph: bool = False,
                  zip_too: bool = True, redactor: Any = None, recipe_sha: Optional[str] = None) -> dict[str, Any]:
    root = "handoff"
    files: list[dict[str, Any]] = []
    att_copied: dict[tuple[str, str], str] = {}
    missing = 0
    by_label = {a.label: a for a in eng.archives}
    totals = {"conversations": 0, "conversations_with_messages": 0, "messages": 0, "messages_with_text": 0, "attachments": 0}
    for e in entries:
        u = e.unit
        src = u.source or {}
        paths, cut = choose_paths(u, branches, eng.package_marks.get(u.uid))
        msgs = paths[0] if paths else []
        if e.messages is not None:
            msgs = [m for m in msgs if m.id in e.messages]
        where = {"conversation": u.uid}
        conv_msgs = []
        for m in msgs:
            blocks = []
            for b in m.blocks:
                d = b.as_dict()
                if redactor is not None and redactor.active:
                    d = redactor.value(d, {**where, "message": m.id}, "text")
                blocks.append(d)
            text = "\n".join(b.get("text", "") for b in blocks if b.get("kind") == "text" and b.get("text"))
            atts = []
            for a in u.attachments:
                if a.message != m.id:
                    continue
                if a.status == "resolved" and a.member and (src.get("archive"), a.member) not in att_copied:
                    arc = by_label.get(src.get("archive") or "")
                    mem = arc.member(a.member) if arc is not None else None
                    if mem is not None:
                        rel = f"attachments/{archive_slug(arc.label)}/{a.member}"
                        target = stage.root / root / rel
                        target.parent.mkdir(parents=True, exist_ok=True)
                        with mem.open() as fi, open(target, "wb") as fo:
                            copy_stream(fi, fo)
                        stage.add_file(f"{root}/{rel}", profile="handoff")
                        att_copied[(src.get("archive"), a.member)] = rel
                if (src.get("archive"), a.member) in att_copied:
                    atts.append(att_copied[(src.get("archive"), a.member)])
                elif a.status != "resolved":
                    missing += 1
            conv_msgs.append({"id": m.id, "parent_id": m.parent, "role": m.role, "content": text,
                              "created": m.created_utc, "model": m.model, "attachments": atts,
                              "metadata": {"export": {"schema": "loom.export.v1", "provider": u.service, "locator": {"raw_pointer": m.raw_pointer},
                                                      "blocks": blocks, "branch": m.branch, "on_current_path": m.on_current_path,
                                                      "status": "active"}}})
            totals["messages"] += 1
            totals["messages_with_text"] += int(bool(text.strip()) and m.role != "system")
            totals["attachments"] += len(atts)
        title = e.name if e.name is not None else u.title
        if redactor is not None and redactor.active:
            title = redactor.text(title, where, "title")
        conv = {"id": u.ext_id or u.uid, "title": title, "created": u.created_utc, "updated": u.updated_utc, "messages": conv_msgs,
                "metadata": {"export": {"schema": "loom.export.v1", "provider": u.service, "key": u.uid, "format": src.get("format"),
                                        "archive": src.get("archive"), "member": None if eng.aggregate else src.get("member"),
                                        "locator": None if eng.aggregate else src.get("locator"), "versions": len(u.versions),
                                        "branches": {"policy": branches, **{k: v for k, v in cut.items() if k != "policy"}}},
                             "extract": {"recipe": recipe_sha, "package_key": e.key}}}
        rel = f"{root}/conversations/{e.key}.json"
        rec = stage.write_text(rel, out.json_doc(conv), profile="handoff")
        files.append({"path": f"conversations/{e.key}.json", "sha256": rec["sha256"], "conversations": 1, "messages": len(conv_msgs)})
        totals["conversations"] += 1
        totals["conversations_with_messages"] += int(bool(conv_msgs))
    graph = None
    if with_graph:
        from .cut.profiles import render_profile
        from .cut.views import RenderCtx
        from .redact import Redactor
        from .timestamps import TimePolicy
        prof = dict(eng.ds.entry("profiles", "graph"))
        prof["outputs"] = [{"kind": "graph", "path": f"{root}/graph/packet.json"}]
        ctx = RenderCtx(eng, time=TimePolicy(prof.get("timestamps")), redactor=redactor or Redactor("none", eng.ds), att_paths={},
                        aggregate=eng.aggregate)
        render_profile(prof, entries, ctx, stage, ds=eng.ds, tool=eng.tool_info(),
                       sources=[{"label": a.label, "sha256": a.sha256, "format": a.format} for a in eng.archives])
        graph = "graph/packet.json"
    doc = {"schema": "loom.extract.handoff/1", "from_recipe": recipe_sha, "profile": "handoff", "branches": branches,
           "files": files, "counts": totals, "attachments": {"copied": len(att_copied), "missing": missing}, "graph": graph,
           "import_hint": {"loom": "loom import <package>/handoff", "python": "ConversationImporter.import_file('<package>/handoff.zip')",
                           "extract": "python3 -m loom.tools.extract inwentarz <package>/handoff"}}
    stage.write_text(f"{root}/handoff.json", out.json_doc(doc), profile="handoff")
    if zip_too:
        # conversations only: the Python importer imports every file of a zip, and an image would become an OCR "conversation"
        names = sorted(o["path"] for o in stage.items if o["path"].startswith(f"{root}/conversations/"))
        deterministic_zip(stage.root / "handoff.zip", [(n[len(root) + 1:], stage.root / n) for n in names])
        stage.add_file("handoff.zip", profile="handoff")
    return {"path": f"{root}/", "conversations": totals["conversations"], "conversations_with_messages": totals["conversations_with_messages"],
            "messages": totals["messages"],
            "messages_with_text": totals["messages_with_text"], "attachments": len(att_copied), "attachments_missing": missing,
            "zip": "handoff.zip" if zip_too else None, "graph": graph, "branches": branches}
