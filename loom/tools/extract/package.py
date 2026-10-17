"""The package tree: a COPY of the selection that batch edits change.

Sources and the unit snapshot are never touched; ``package.copy`` copies
selected conversations (whole, or the selected messages) into entries under
folders; ``package.move/mkdir/remove/rename`` reorganise them; ``edit.*``
operations (``edits.py``) change values and structure with a diff preview,
an operation log and undo/redo.  Restructuring profiles render from here.
"""
import copy
import json
from typing import Any, Optional

from .cut.common import Entry, assign_keys, entry_sort_key
from .model import Unit, canonical_json, sha256_text


def clone_unit(u: Unit) -> Unit:
    c = Unit.from_dict(json.loads(json.dumps(u.as_dict(), ensure_ascii=False, default=str)))
    c.uid, c.content_sha256 = u.uid, u.content_sha256
    return c


def entry_dict(e: Entry) -> dict[str, Any]:
    d = {"unit": e.unit.as_dict(), "messages": sorted(e.messages) if e.messages is not None else None, "folder": e.folder,
         "origin": e.origin, "name": e.name, "notes": list(e.notes)}
    if e.aliases:  # only when set, so tree hashes of packages without aliases stay what they were
        d["aliases"] = {k: dict(v) for k, v in sorted(e.aliases.items()) if v}
    return d


def entry_from_dict(d: dict[str, Any]) -> Entry:
    u = Unit.from_dict(copy.deepcopy(d["unit"]))
    u.uid, u.content_sha256 = d["unit"].get("uid", u.uid), d["unit"].get("content_sha256", u.content_sha256)
    return Entry(unit=u, messages=set(d["messages"]) if d.get("messages") is not None else None, folder=d.get("folder", "/"),
                 origin=d.get("origin", ""), name=d.get("name"), notes=list(d.get("notes") or []),
                 aliases={k: dict(v) for k, v in (d.get("aliases") or {}).items()})


class Package:
    def __init__(self) -> None:
        self.entries: list[Entry] = []
        self.folders: set[str] = {"/"}
        self.oplog: list[dict[str, Any]] = []
        self.redo: list[dict[str, Any]] = []
        self.marks: dict[str, str] = {}

    # -- identity ----------------------------------------------------------------
    def tree_hash(self) -> str:
        view = {"entries": [entry_dict(e) for e in self.entries], "folders": sorted(self.folders)}
        return "sha256:" + sha256_text(canonical_json(view))[:32]

    def by_uid(self) -> dict[str, Entry]:
        return {e.unit.uid: e for e in self.entries}

    def ordered(self, order: str) -> list[Entry]:
        """Entries in output order (preset ``package.order``) with their keys assigned."""
        if order == "package":
            out = list(self.entries)
        else:
            out = sorted(self.entries, key=lambda e: (e.folder,) + entry_sort_key(e.unit, order))
        assign_keys(out)
        return out

    # -- structure ops -------------------------------------------------------------
    def copy_in(self, units: list[Unit], selection: dict[str, Optional[set[str]]], into: str = "/") -> int:
        have = self.by_uid()
        added = 0
        # new entries arrive in time order (stable ties by service and uid), so the package order is chronological
        # until an edit (sort, merge, split) or a move says otherwise; the cut keeps the package order (preset)
        for u in sorted(units, key=lambda x: entry_sort_key(x, "created")):
            if u.uid not in selection:
                continue
            ms = selection[u.uid]
            if u.uid in have:
                e = have[u.uid]
                if e.messages is not None:
                    e.messages = None if ms is None else (e.messages | set(ms))
                continue
            self.entries.append(Entry(unit=clone_unit(u), messages=None if ms is None else set(ms), folder=into, origin=u.uid))
            added += 1
        self.folders.add(into)
        return added

    def snapshot(self, uids: Optional[list[str]] = None) -> dict[str, Any]:
        # deep copy: a unit's dicts (account, flags, meta, stats) are mutable and edits change them in place
        return {"entries": [copy.deepcopy(entry_dict(e)) for e in self.entries if uids is None or e.unit.uid in uids],
                "order": [e.unit.uid for e in self.entries], "folders": sorted(self.folders), "all": uids is None,
                "marks": dict(self.marks)}

    def restore(self, snap: dict[str, Any]) -> None:
        if snap.get("all"):
            self.entries = [entry_from_dict(copy.deepcopy(d)) for d in snap["entries"]]
        else:
            saved = {d["unit"]["uid"]: entry_from_dict(copy.deepcopy(d)) for d in snap["entries"]}
            cur = self.by_uid()
            cur.update(saved)
            self.entries = [cur[u] for u in snap["order"] if u in cur]
        self.folders = set(snap.get("folders") or ["/"])
        if "marks" in snap:
            self.marks = dict(snap["marks"])

    def find(self, refs: list[str]) -> list[Entry]:
        """Entries by uid, uid prefix, ext_id or key."""
        out: list[Entry] = []
        for r in refs:
            for e in self.entries:
                if e in out:
                    continue
                if r in (e.unit.uid, e.unit.ext_id, e.key) or (len(r) >= 8 and e.unit.uid.split(":", 1)[-1].startswith(r.split(":", 1)[-1])):
                    out.append(e)
        return out

    def as_dict(self) -> dict[str, Any]:
        return {"schema": "loom.extract.package_tree/1", "tree_hash": self.tree_hash(), "folders": sorted(self.folders),
                "entries": [e.as_ref() for e in self.entries], "oplog": len(self.oplog), "redo": len(self.redo)}
