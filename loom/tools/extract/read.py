"""Stage ``read``: archives → units, through the reader named by each member's
format.  Nothing is dropped: a member without a format becomes an ``unknown``
unit with an example, a reader failure becomes an ``unknown`` unit with the
error (and a debug log), assets are listed on their archive."""
import logging
from typing import Any, Iterator, Optional

from .model import Unit
from .readers import get_reader
from .readers.base import ReadContext, finalize_unit, note_unknown
from .sources import Archive, Member

log = logging.getLogger(__name__)


def read_member(member: Member, ctx: ReadContext) -> Iterator[Unit]:
    if member.kind in ("asset", "container"):
        return
    if member.kind == "unreadable" or not member.format:
        u = Unit(kind="unknown", title=member.name or member.archive.label)
        head = ""
        if member.kind != "unreadable":
            try:
                head = member.head(120).decode("utf-8", errors="replace")
            except (OSError, EOFError, ValueError, RuntimeError):  # zipfile.BadZipFile and zlib.error are among these
                log.debug("member head unreadable: %s", member.name, exc_info=True)
        note_unknown(u, "", {"member": member.name, "size": member.size, "sniff": member.sniff_how, "head": head},
                     native="unreadable member" if member.kind == "unreadable" else "unrecognised member")
        fmt = {"id": "unknown", "service": None, "product": None}
        yield finalize_unit(u, member, fmt, ctx, {"member": member.name})
        return
    fmt = ctx.catalog.get(member.format)
    try:
        # inside the try: an unknown reader (e.g. an overlay format naming a reader that does not exist) becomes an
        # 'unknown' unit with the reason, not the end of the whole run
        reader = get_reader(fmt["reader"], ctx)
        for unit in reader.iter_units(member, fmt, ctx):
            yield unit
    except Exception as exc:  # noqa: BLE001 - reported as a unit, never silent
        log.debug("reader %s failed on %s", fmt["reader"], member.name, exc_info=True)
        u = Unit(kind="unknown", title=member.name or member.archive.label)
        note_unknown(u, "", f"{type(exc).__name__}: {exc}", native=f"reader:{fmt['reader']}")
        yield finalize_unit(u, member, fmt, ctx, {"member": member.name})


def read_archives(archives: list[Archive], ctx: ReadContext, *, progress: Optional[Any] = None) -> list[Unit]:
    units: list[Unit] = []
    total = sum(len(a.members) for a in archives)
    done = 0
    for a in archives:
        for m in a.members:
            for u in read_member(m, ctx):
                units.append(u)
            done += 1
            if progress is not None:
                progress({"stage": "read", "done": done, "total": total, "archive": a.label, "member": m.name})
    return units
