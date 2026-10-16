"""Google Takeout — Gemini Apps "My Activity" (JSON list of activity entries, or
the HTML rendering).  HYPOTHESIS format (``formats/google_gemini.json``): Takeout
carries no conversation id, so entries are grouped into conversations by a
time gap (preset ``grouping.gap_minutes``) and the grouping is flagged
``inferred``.  A correction is an edit of the format file."""
import re
from datetime import datetime, timedelta, timezone
from html.parser import HTMLParser
from typing import Any, Iterator, Optional

from ..model import Attachment, Block, Message, Unit, json_pointer, sha256_text
from ..sources import Member, iter_json_array
from .base import ReadContext, Reader, getpath, note_unknown, finalize_unit, strip_html, unknown_block
from .. import timestamps as ts


class GeminiReader(Reader):
    id = "gemini"

    def iter_units(self, member: Member, fmt: dict[str, Any], ctx: ReadContext) -> Iterator[Unit]:
        head = member.head(256).lstrip()[:1]
        entries: list[tuple[str, dict[str, Any]]] = []
        if head == b"[":
            with member.open() as fh:
                for i, e in enumerate(iter_json_array(fh)):
                    if isinstance(e, dict):
                        entries.append((json_pointer(i), e))
        else:
            html = member.read_bytes().decode("utf-8", errors="replace")
            for i, e in enumerate(_parse_takeout_html(html)):
                entries.append((f"html:{i}", e))
        yield from self._group(entries, member, fmt, ctx)

    def _group(self, entries: list[tuple[str, dict[str, Any]]], member: Member, fmt: dict[str, Any],
               ctx: ReadContext) -> Iterator[Unit]:
        espec = fmt.get("entries") or {}
        gap = timedelta(minutes=float((fmt.get("grouping") or {}).get("gap_minutes", 180)))
        stamped = []
        for ptr, e in entries:
            st = ctx.stamp(getpath(e, (espec.get("time") or {}).get("path", "time")))
            stamped.append((st, ptr, e))
        stamped.sort(key=lambda x: (x[0].utc is None, x[0].utc or datetime.min.replace(tzinfo=timezone.utc), x[1]))
        groups: list[list[tuple[Any, str, dict[str, Any]]]] = []
        for item in stamped:
            if groups and item[0].utc is not None and groups[-1][-1][0].utc is not None \
                    and item[0].utc - groups[-1][-1][0].utc <= gap:
                groups[-1].append(item)
            else:
                groups.append([item])
        for g in groups:
            yield self._conversation(g, member, fmt, ctx)

    def _conversation(self, group: list[tuple[Any, str, dict[str, Any]]], member: Member, fmt: dict[str, Any],
                      ctx: ReadContext) -> Unit:
        espec = fmt.get("entries") or {}
        prefixes = espec.get("prompt_prefix") or ["Prompted "]
        u = Unit(kind="conversation")
        first_st, first_ptr, first = group[0]
        prompt0 = _strip_prefix(str(first.get(espec.get("prompt", "title")) or ""), prefixes)
        u.title = prompt0[:80] if prompt0 else None
        u.ext_id = "gemini:" + (first_st.iso or "") + ":" + sha256_text(prompt0)[:12]
        ctx.set_unit_times(u, first.get("time"), group[-1][2].get("time"))
        u.flags["grouping_inferred"] = True
        u.flags["entries"] = len(group)
        prev: Optional[str] = None
        n = 0
        for st, ptr, e in group:
            prompt = _strip_prefix(str(e.get(espec.get("prompt", "title")) or ""), prefixes)
            if e.get("header") and not re.match(r"^(Gemini|Bard)", str(e.get("header"))):
                note_unknown(u, ptr, e, native=str(e.get("header")))
            m_user = Message(id=f"e{n}u", parent=prev, role="user", raw_pointer=ptr)
            ctx.set_msg_time(m_user, e.get("time"), fallback=u.created_utc)
            if prompt:
                m_user.blocks.append(Block(kind="text", text=prompt, native="title"))
            for att in e.get(espec.get("attachments", "attachments")) or []:
                if isinstance(att, dict):
                    ref = str(att.get("name") or att.get("url") or "?")
                    u.attachments.append(Attachment(ref=ref, name=att.get("name"), mime=att.get("mimeType"), status="reference", message=m_user.id))
                    m_user.blocks.append(Block(kind="attachment", native="attachments", ref=ref))
            u.messages.append(m_user)
            prev = m_user.id
            resp_html = getpath(e, espec.get("response_html"))
            resp_text = e.get(espec.get("response_text", "details")) if isinstance(e.get(espec.get("response_text", "details")), str) else None
            if not resp_html and not resp_text:
                subs = e.get(espec.get("subtitles", "subtitles")) or []
                texts = [s.get("name") for s in subs if isinstance(s, dict) and isinstance(s.get("name"), str)]
                resp_text = "\n".join(texts) if texts else None
            if resp_html or resp_text:
                m_ai = Message(id=f"e{n}a", parent=prev, role="assistant", raw_pointer=ptr)
                ctx.set_msg_time(m_ai, e.get("time"), fallback=u.created_utc)
                text = strip_html(str(resp_html)) if resp_html else str(resp_text)
                m_ai.blocks.append(Block(kind="text", text=text, native="safeHtmlItem" if resp_html else "details"))
                u.messages.append(m_ai)
                prev = m_ai.id
            for k in e:
                if k not in ("header", "title", "titleUrl", "time", "products", "activityControls", "safeHtmlItem",
                             "details", "subtitles", "attachments", "locationInfos"):
                    note_unknown(u, ptr + "/" + k, e[k], native=k)
            n += 1
        u.tree = {"roots": [u.messages[0].id] if u.messages else [], "current_leaf": prev, "forks": [], "linear": True}
        return finalize_unit(u, member, fmt, ctx, {"json_pointer": first_ptr, "entries": [p for _, p, _ in group]})


def _strip_prefix(text: str, prefixes: list[str]) -> str:
    for p in prefixes:
        if text.startswith(p):
            return text[len(p):].strip()
    return text.strip()


class _TakeoutHTML(HTMLParser):
    """Collects the text pieces of every ``content-cell`` div (Takeout HTML)."""

    def __init__(self) -> None:
        super().__init__()
        self.cells: list[list[str]] = []
        self._depth = 0
        self._in = False
        self._buf: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, Optional[str]]]) -> None:
        cls = dict(attrs).get("class") or ""
        if tag == "div" and "content-cell" in cls and not self._in:
            self._in, self._depth, self._buf = True, 1, [""]
            return
        if self._in:
            if tag == "div":
                self._depth += 1
            if tag == "br":
                self._buf.append("")

    def handle_endtag(self, tag: str) -> None:
        if self._in and tag == "div":
            self._depth -= 1
            if self._depth == 0:
                self._in = False
                self.cells.append([p.strip() for p in self._buf if p.strip()])

    def handle_data(self, data: str) -> None:
        if self._in:
            self._buf[-1] += data


def _parse_takeout_html(html: str) -> list[dict[str, Any]]:
    p = _TakeoutHTML()
    p.feed(html)
    out: list[dict[str, Any]] = []
    for pieces in p.cells:
        if not pieces:
            continue
        time_val = None
        for cand in reversed(pieces):
            parsed = _parse_takeout_date(cand)
            if parsed:
                time_val = parsed
                pieces = [x for x in pieces if x is not cand]
                break
        if not pieces:
            continue
        title = pieces[0]
        body = "\n".join(pieces[1:])
        out.append({"header": "Gemini Apps", "title": title, "time": time_val, "details": body or None})
    return out


_MONTHS = {m: i for i, m in enumerate(["jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"], 1)}


def _parse_takeout_date(text: str) -> Optional[str]:
    """``Oct 9, 2025, 10:53:20 AM CEST`` → ISO (zone kept when it is a numeric offset or UTC)."""
    m = re.match(r"^([A-Za-z]{3})\w*\s+(\d{1,2}),\s+(\d{4}),?\s+(\d{1,2}):(\d{2})(?::(\d{2}))?\s*(AM|PM)?\s*([A-Z]{2,5}|[+-]\d{2}:?\d{2})?$", text.strip())
    if not m:
        iso = ts.parse(text)
        return text if iso.utc is not None and iso.how in ("iso",) else None
    mon = _MONTHS.get(m.group(1).lower())
    if not mon:
        return None
    hour = int(m.group(4))
    if m.group(7) == "PM" and hour < 12:
        hour += 12
    if m.group(7) == "AM" and hour == 12:
        hour = 0
    zone = m.group(8) or ""
    suffix = "Z" if zone in ("UTC", "GMT") else (zone if re.fullmatch(r"[+-]\d{2}:?\d{2}", zone) else "")
    return f"{int(m.group(3)):04d}-{mon:02d}-{int(m.group(2)):02d}T{hour:02d}:{m.group(5)}:{m.group(6) or '00'}{suffix}"
