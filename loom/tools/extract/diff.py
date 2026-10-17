"""Stage ``diff``: what is new since the last export.
Two unit sets (old / new export, two packages, or a package's ``units.jsonl``
snapshot) are compared by identity — ``(service, ext_id)`` when the format
has an external id (preset ``diff.identity: ext_id``), else by content hash —
and by content hash: **new** (absent from old), **changed** (same identity,
different hash, with a message-level breakdown: added, changed, removed,
title changed, new branches, new attachments), **removed** (in old, not in
new — listed, never skipped: exports drop conversations deleted on the
service side), **unchanged**.  ``select_uids`` turns a diff into a selection
(``only_new`` | ``changed`` | ``removed`` | ``all``) so an incremental run is
the same recipe with a ``diff`` step first.
"""
from typing import Any, Optional

from .model import Unit, canonical_json, sha256_text

SCHEMA = "loom.extract.diff/1"
MODES = ("only_new", "changed", "removed", "all", "unchanged")
CONV_KINDS = ("conversation", "session")


def _key(u: Unit, identity: str) -> tuple[str, str]:
    if identity == "ext_id" and u.ext_id:
        return (u.service or "", f"ext:{u.ext_id}")
    return (u.service or "", f"sha:{u.content_sha256}")


def _msg_hashes(u: Unit) -> dict[str, str]:
    return {m.id: sha256_text(canonical_json(m.hash_view())) for m in u.messages}


def _brief(u: Unit, aggregate: bool) -> dict[str, Any]:
    return {"uid": u.uid, "ext_id": u.ext_id, "title": None if aggregate else u.title, "service": u.service,
            "messages": len(u.messages), "created_utc": u.created_utc, "updated_utc": u.updated_utc,
            "source": None if aggregate else (u.source or {}).get("archive")}


def diff_units(old: list[Unit], new: list[Unit], *, identity: str = "ext_id", summary_only: bool = False,
               aggregate: bool = False, old_label: Optional[str] = None, new_label: Optional[str] = None) -> dict[str, Any]:
    if identity not in ("ext_id", "content_hash"):
        raise ValueError(f"unknown diff identity {identity!r} (ext_id | content_hash)")
    old_c = [u for u in old if u.kind in CONV_KINDS]
    new_c = [u for u in new if u.kind in CONV_KINDS]
    old_by: dict[tuple[str, str], Unit] = {}
    for u in old_c:
        old_by.setdefault(_key(u, identity), u)
    new_by: dict[tuple[str, str], Unit] = {}
    for u in new_c:
        new_by.setdefault(_key(u, identity), u)

    new_units: list[dict[str, Any]] = []
    changed: list[dict[str, Any]] = []
    removed: list[dict[str, Any]] = []
    unchanged: list[str] = []
    for key in sorted(new_by, key=lambda k: (k, new_by[k].uid)):
        nu = new_by[key]
        ou = old_by.get(key)
        if ou is None:
            new_units.append(_brief(nu, aggregate))
        elif ou.content_sha256 == nu.content_sha256:
            unchanged.append(nu.uid)
        else:
            oh, nh = _msg_hashes(ou), _msg_hashes(nu)
            added = sorted(i for i in nh if i not in oh)
            gone = sorted(i for i in oh if i not in nh)
            modified = sorted(i for i in nh if i in oh and nh[i] != oh[i])
            old_refs = {a.ref for a in ou.attachments}
            new_refs = sorted(a.ref for a in nu.attachments if a.ref not in old_refs)
            entry = {"ext_id": nu.ext_id, "service": nu.service, "old_uid": ou.uid, "new_uid": nu.uid,
                     "title": None if aggregate else nu.title,
                     "messages": {"added": len(added), "changed": len(modified), "removed": len(gone),
                                  "ids": None if (summary_only or aggregate) else {"added": added, "changed": modified, "removed": gone}},
                     "title_changed": (ou.title or "") != (nu.title or ""),
                     "new_branches": max(0, nu.leaf_count() - ou.leaf_count()),
                     "new_attachments": len(new_refs),
                     "updated": {"old": ou.updated_utc, "new": nu.updated_utc}}
            changed.append(entry)
    for key in sorted(old_by, key=lambda k: (k, old_by[k].uid)):
        if key not in new_by:
            removed.append(_brief(old_by[key], aggregate))

    out: dict[str, Any] = {
        "schema": SCHEMA, "identity": identity, "summary_only": summary_only, "aggregate": aggregate,
        "old": {"label": old_label, "units": len(old_c)}, "new": {"label": new_label, "units": len(new_c)},
        "counts": {"new": len(new_units), "changed": len(changed), "removed": len(removed), "unchanged": len(unchanged),
                   "messages_added": sum(c["messages"]["added"] for c in changed),
                   "messages_changed": sum(c["messages"]["changed"] for c in changed),
                   "messages_removed": sum(c["messages"]["removed"] for c in changed)},
        "how": "identity = (service, ext_id) when the format has an external id, else content sha256; "
               "changed = same identity, different content sha256; message ids compared by their hash view",
    }
    if summary_only:
        out.update({"new_units": len(new_units), "changed": len(changed), "removed": len(removed), "unchanged": len(unchanged)})
    else:
        out.update({"new_units": new_units, "changed": changed, "removed": removed, "unchanged": len(unchanged),
                    "unchanged_uids": unchanged})
    return out


def select_uids(diff: dict[str, Any], mode: str) -> tuple[set[str], list[str]]:
    """uids on the NEW side (old side for ``removed``) for a selection mode, with a note per uid."""
    if mode not in MODES:
        raise ValueError(f"unknown diff mode {mode!r} (one of {', '.join(MODES)})")
    if diff.get("summary_only"):
        raise ValueError("a summary-only diff cannot feed a selection (run diff without summary_only)")
    uids: set[str] = set()
    notes: list[str] = []
    if mode in ("only_new", "all"):
        for e in diff.get("new_units") or []:
            uids.add(e["uid"])
        notes.append(f"diff:new={len(diff.get('new_units') or [])}")
    if mode in ("changed", "all"):
        for e in diff.get("changed") or []:
            uids.add(e["new_uid"])
        notes.append(f"diff:changed={len(diff.get('changed') or [])}")
    if mode == "removed":
        for e in diff.get("removed") or []:
            uids.add(e["uid"])
        notes.append(f"diff:removed={len(diff.get('removed') or [])}")
    if mode == "unchanged":
        uids.update(diff.get("unchanged_uids") or [])
        notes.append(f"diff:unchanged={len(diff.get('unchanged_uids') or [])}")
    return uids, notes
