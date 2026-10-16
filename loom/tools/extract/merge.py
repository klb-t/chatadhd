"""Stage ``merge``: identity across archives, exact duplicates, versions.

Identity = (service, external id) when the format has one, else the content
hash.  The same content seen twice (two exports, a nested copy) is a duplicate:
one unit stays, the other sources are listed on it.  Different content under
one identity (an export from a later date) is a version: the newest stays
active, older ones are listed in ``versions[]`` with their source — nothing is
silently discarded.
"""
from dataclasses import dataclass, field
from typing import Any, Optional

from .model import Unit


@dataclass
class MergeResult:
    units: list[Unit]
    groups: dict[tuple[str, str], list[Unit]] = field(default_factory=dict)
    duplicates: int = 0
    versions: int = 0
    older: list[Unit] = field(default_factory=list)   # superseded versions (kept for diff / inventory)

    def as_dict(self) -> dict[str, Any]:
        return {"units": len(self.units), "identities": len(self.groups), "duplicates": self.duplicates,
                "versions": self.versions,
                "how": "identity = (service, ext_id) or content hash; same hash → duplicate; "
                       "different hash under one identity → version (newest by updated/created, then archive mtime, stays active)"}


def _order_key(u: Unit) -> tuple:
    src = u.source or {}
    return (u.updated_utc or u.created_utc or "", src.get("archive_mtime") or 0, src.get("archive") or "", src.get("member") or "",
            str(src.get("locator")))


def merge(units: list[Unit], *, newest_wins: bool = True) -> MergeResult:
    groups: dict[tuple[str, str], list[Unit]] = {}
    order: list[tuple[str, str]] = []
    for u in units:
        key = u.identity_key
        if key not in groups:
            groups[key] = []
            order.append(key)
        groups[key].append(u)
    out: list[Unit] = []
    res = MergeResult(units=out, groups=groups)
    for key in order:
        members = groups[key]
        if len(members) == 1:
            out.append(members[0])
            continue
        # exact duplicates first: group by content hash, keep the first seen in scan order
        by_hash: dict[str, list[Unit]] = {}
        hash_order: list[str] = []
        for u in members:
            if u.content_sha256 not in by_hash:
                hash_order.append(u.content_sha256)
            by_hash.setdefault(u.content_sha256, []).append(u)
        representatives: list[Unit] = []
        for h in hash_order:
            same = by_hash[h]
            rep = same[0]
            for dup in same[1:]:
                rep.duplicates.append({"source": dup.source, "uid": dup.uid})
                res.duplicates += 1
            representatives.append(rep)
        if len(representatives) == 1:
            out.append(representatives[0])
            continue
        ordered = sorted(representatives, key=_order_key, reverse=newest_wins)
        active = ordered[0]
        for older in ordered[1:]:
            active.versions.append({"source": older.source, "uid": older.uid, "content_sha256": older.content_sha256,
                                    "updated_utc": older.updated_utc, "created_utc": older.created_utc,
                                    "messages": len(older.messages), "title": older.title})
            older.flags["superseded_by"] = active.uid
            res.older.append(older)
            res.versions += 1
        active.flags["versions"] = len(active.versions)
        out.append(active)
    return res
