"""Text-like formats (HTML, MHT, Markdown, text, JSONL with roles, generic JSON
message lists, SQLite) THROUGH ``engine.importer.ConversationImporter``: a
subclass replaces the single write method ``_import_message_list`` with a
capture into the common model, so the role heuristics, the Markdown splitter
and the HTML parser stay one (no database, no Kivy; ``engine.events`` is a
plain pub/sub)."""
import logging
from pathlib import Path
from typing import Any, Iterator, Optional

from ..model import Block, Message, Unit
from ..paths import ensure_repo_on_path
from ..sources import Member
from .base import ReadContext, Reader, note_unknown, finalize_unit

log = logging.getLogger(__name__)

_importer_cls: Any = None


def _capturing_importer_class() -> Any:
    global _importer_cls
    if _importer_cls is not None:
        return _importer_cls
    ensure_repo_on_path()
    from engine.importer import ConversationImporter  # noqa: WPS433 - adapter import

    class CapturingImporter(ConversationImporter):
        """Captures every converged message list instead of writing to a DB."""

        def __init__(self) -> None:
            super().__init__(db=None, engine=None)
            self.captured: list[tuple[list[dict[str, Any]], Optional[str]]] = []

        def _import_message_list(self, messages, title):  # type: ignore[override]
            self.captured.append((list(messages), title))
            return {"id": f"capture_{len(self.captured)}", "title": title}

    _importer_cls = CapturingImporter
    return _importer_cls


class TextlikeReader(Reader):
    id = "textlike"

    def iter_units(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        cls = _capturing_importer_class()
        imp = cls()
        method = fmt.get("importer_method") or "import_file"
        path = member.materialize(ctx.tmpdir)
        title_hint = Path(member.name or member.archive.path).stem
        try:
            getattr(imp, method)(path, None)
        except Exception as exc:  # noqa: BLE001 - the failure becomes an unknown unit, never silence
            log.debug("importer %s failed on %s", method, member.name, exc_info=True)
            u = Unit(kind="unknown", title=title_hint)
            note_unknown(u, "", f"{type(exc).__name__}: {exc}", native=method)
            yield finalize_unit(u, member, fmt, ctx, {"member": member.name, "importer": method})
            return
        if not imp.captured:
            u = Unit(kind="unknown", title=title_hint)
            note_unknown(u, "", "importer produced no conversation", native=method)
            yield finalize_unit(u, member, fmt, ctx, {"member": member.name, "importer": method})
            return
        for i, (messages, title) in enumerate(imp.captured):
            u = Unit(kind="conversation", title=title or title_hint)
            u.ext_id = None
            ctx.set_unit_times(u, None, None, mtime=member.mtime if member.mtime is not None else member.archive.mtime)
            prev: Optional[str] = None
            for j, msg in enumerate(messages):
                role = msg.get("role", msg.get("sender", "user"))
                role = "user" if role in ("user", "human") else ("assistant" if role in ("assistant", "ai", "claude") else str(role))
                content = msg.get("content", msg.get("text", ""))
                if isinstance(content, list):
                    parts = []
                    for p in content:
                        if isinstance(p, dict):
                            parts.append(str(p.get("text") or ("[image]" if p.get("type") == "image_url" else "")))
                        elif isinstance(p, str):
                            parts.append(p)
                    content = "\n".join(x for x in parts if x)
                if not isinstance(content, str):
                    content = str(content) if content else ""
                m = Message(id=f"m{j}", parent=prev, role=role, raw_pointer=f"captured:{i}/{j}")
                ctx.set_msg_time(m, msg.get("created") or msg.get("timestamp") or msg.get("created_at"), fallback=None)
                if content.strip():
                    m.blocks.append(Block(kind="text", text=content, native="content"))
                u.messages.append(m)
                prev = m.id
            u.tree = {"roots": [u.messages[0].id] if u.messages else [], "current_leaf": prev, "forks": [], "linear": True}
            u.flags["via_importer"] = method
            yield finalize_unit(u, member, fmt, ctx, {"member": member.name, "importer": method, "capture": i})
