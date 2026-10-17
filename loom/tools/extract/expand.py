"""Stage ``expand``: neighbourhood of what was selected.

Presets: ``none`` | ``window:N`` (N messages before and after, on the same
path) | ``branch`` (the whole root→leaf path of every selected message) |
``conversation`` (the whole conversation — R18 preset) |
``conversation+linked`` (plus conversations linked by shared attachment
hashes, the same project, or a detected continuation) | ``time_window:P``
(messages of other conversations within P of the selected ones).  The result
says what was added and why.
"""
import re
from dataclasses import dataclass, field
from datetime import timedelta
from typing import Any, Optional

from .model import Unit
from . import timestamps as ts


@dataclass
class Selection:
    """uid → set of message ids (None = the whole unit)."""
    units: dict[str, Optional[set[str]]] = field(default_factory=dict)
    reasons: dict[str, list[str]] = field(default_factory=dict)

    def add(self, uid: str, messages: Optional[set[str]], reason: str) -> None:
        cur = self.units.get(uid, set())
        if uid in self.units and cur is None:
            pass
        elif messages is None:
            self.units[uid] = None
        else:
            self.units[uid] = (cur or set()) | set(messages)
        self.reasons.setdefault(uid, [])
        if reason not in self.reasons[uid]:
            self.reasons[uid].append(reason)

    def message_count(self, by_uid: dict[str, Unit]) -> int:
        n = 0
        for uid, ms in self.units.items():
            u = by_uid.get(uid)
            if u is None:
                continue
            n += len(u.messages) if ms is None else len(ms)
        return n

    def as_dict(self) -> dict[str, Any]:
        return {"units": {uid: (sorted(ms) if ms is not None else None) for uid, ms in sorted(self.units.items())},
                "reasons": {k: v for k, v in sorted(self.reasons.items())}}


@dataclass
class ExpandResult:
    preset: str
    selection: Selection
    added_messages: int
    linked_conversations: int

    def as_dict(self) -> dict[str, Any]:
        return {"preset": self.preset, "added_messages": self.added_messages, "linked_conversations": self.linked_conversations}


def expand(selection: Selection, units: list[Unit], preset: str, *, continuations: Optional[list[dict[str, Any]]] = None) -> ExpandResult:
    by_uid = {u.uid: u for u in units}
    before = selection.message_count(by_uid)
    before_units = set(selection.units)
    out = Selection({k: (None if v is None else set(v)) for k, v in selection.units.items()},
                    {k: list(v) for k, v in selection.reasons.items()})
    name, _, arg = preset.partition(":")
    if name == "none":
        pass
    elif name == "window":
        n = int(arg or 3)
        for uid, ms in list(selection.units.items()):
            u = by_uid.get(uid)
            if u is None or ms is None:
                continue
            for path in u.branches():
                ids = [m.id for m in path]
                hit = [i for i, mid in enumerate(ids) if mid in ms]
                for i in hit:
                    out.add(uid, set(ids[max(0, i - n):i + n + 1]), f"window:{n}")
    elif name == "branch":
        for uid, ms in list(selection.units.items()):
            u = by_uid.get(uid)
            if u is None or ms is None:
                continue
            for path in u.branches():
                ids = [m.id for m in path]
                if any(mid in ms for mid in ids):
                    out.add(uid, set(ids), "branch")
    elif name in ("conversation", "conversation+linked"):
        for uid in list(selection.units):
            out.add(uid, None, "conversation")
        if name == "conversation+linked":
            _link(out, selection, units, by_uid, continuations or [])
    elif name == "time_window":
        delta = ts.parse_duration(arg or "1h")
        stamps = []
        for uid, ms in selection.units.items():
            u = by_uid.get(uid)
            if u is None:
                continue
            for m in u.messages:
                if (ms is None or m.id in ms) and m.created_utc:
                    stamps.append(ts.from_iso(m.created_utc))
        for u in units:
            if u.kind not in ("conversation", "session"):
                continue
            for m in u.messages:
                dt = ts.from_iso(m.created_utc)
                if dt is None:
                    continue
                if any(abs(dt - s) <= delta for s in stamps):
                    if u.uid not in selection.units or (selection.units[u.uid] is not None and m.id not in selection.units[u.uid]):
                        out.add(u.uid, {m.id}, f"time_window:{arg}")
    else:
        raise ValueError(f"unknown expand preset: {preset!r}")
    after = out.message_count(by_uid)
    return ExpandResult(preset, out, after - before, len(set(out.units) - before_units))


def _link(out: Selection, selection: Selection, units: list[Unit], by_uid: dict[str, Unit], continuations: list[dict[str, Any]]) -> None:
    selected = set(selection.units)
    # shared attachment hashes
    by_hash: dict[str, set[str]] = {}
    for u in units:
        for a in u.attachments:
            if a.sha256:
                by_hash.setdefault(a.sha256, set()).add(u.uid)
    for uid in list(selected):
        u = by_uid.get(uid)
        if u is None:
            continue
        for a in u.attachments:
            if a.sha256:
                for other in by_hash.get(a.sha256, set()):
                    if other not in selected:
                        out.add(other, None, f"linked: shared attachment {a.ref}")
        proj = u.flags.get("project")
        if proj:
            for v in units:
                if v.uid != uid and v.flags.get("project") == proj and v.uid not in selected:
                    out.add(v.uid, None, f"linked: same project {proj}")
        for c in continuations:
            if c.get("from") == uid and c.get("to") not in selected:
                out.add(c["to"], None, f"linked: continuation ({c.get('confidence')})")
            if c.get("to") == uid and c.get("from") not in selected:
                out.add(c["from"], None, f"linked: continued from ({c.get('confidence')})")
        for ref in _shared_links(u):
            for v in units:
                if v.uid != uid and v.uid not in selected and ref in (v.ext_id or ""):
                    out.add(v.uid, None, f"linked: reference {ref}")


_LINK_RE = re.compile(r"https?://(?:chat\.openai\.com|chatgpt\.com|claude\.ai)/(?:share|chat)/([A-Za-z0-9-]+)")


def _shared_links(u: Unit) -> set[str]:
    return set(_LINK_RE.findall(u.text()))
