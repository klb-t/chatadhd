"""What an edit or a ready recipe did (or would do) to the messages of the working package, in plain words.

The engine reports per step only ``changed: N`` (how many items), not which keys vanished.  So the effect is measured:
``snapshot`` records, per message, which fields are filled (author, model, created, ... and every key of ``meta``) and
how many blocks of each kind it has; ``diff`` compares two snapshots; ``sentence`` turns the difference into one
Polish sentence (templates and noun forms are data: ``data/ui_labels.json#effects`` / ``#nouns``).  Nothing here is
guessed: a number is a count of what differs between the two snapshots, and what cannot be measured is said to be
``nieznane``.  The preview is measured by really running the steps on the package and putting it back
(``simulate``), so it describes exactly what Zastosuj would do and changes nothing.
"""
from __future__ import annotations

import dataclasses
from collections import Counter
from typing import Any, Callable, Optional

from .datafiles import store
from .model import Message
from .plural import count as plural_count

_MSG_FIELDS = [f for f in dataclasses.fields(Message) if f.name not in ("id", "blocks", "meta")]


def _filled(m: Message, f: dataclasses.Field) -> bool:
    v = getattr(m, f.name)
    if v is None or v is False or v == "" or v == [] or v == {}:
        return False
    return not (f.default is not dataclasses.MISSING and v == f.default)


def snapshot(pkg: Any) -> dict[tuple[str, str], tuple[frozenset, Counter]]:
    """``(unit uid, message id) -> (filled keys, blocks per kind)`` for every message of the package.
    A key is ``("f", name)`` for a field of the message and ``("m", key)`` for a key of its ``meta``."""
    out: dict[tuple[str, str], tuple[frozenset, Counter]] = {}
    if pkg is None:
        return out
    for e in pkg.entries:
        for m in e.unit.messages:
            keys = {("f", f.name) for f in _MSG_FIELDS if _filled(m, f)}
            if isinstance(m.meta, dict):
                keys |= {("m", str(k)) for k in m.meta}
            out[(e.unit.uid, m.id)] = (frozenset(keys), Counter(b.kind for b in m.blocks))
    return out


def diff(before: dict, after: dict) -> dict[str, Any]:
    """Counts of what differs: per key the number of messages that lost / gained it, the keys that stay in the changed
    messages, blocks lost / gained per kind, whole messages gone / new."""
    changed = 0
    lost: Counter = Counter()
    gained: Counter = Counter()
    kept: Counter = Counter()
    blocks_lost: Counter = Counter()
    blocks_gained: Counter = Counter()
    for k, (fb, bb) in before.items():
        if k not in after:
            continue
        fa, ba = after[k]
        dl, dg = fb - fa, fa - fb
        bl = {kind: bb[kind] - ba[kind] for kind in bb if bb[kind] > ba[kind]}
        bg = {kind: ba[kind] - bb[kind] for kind in ba if ba[kind] > bb[kind]}
        if dl or dg or bl or bg:
            changed += 1
            lost.update(dl)
            gained.update(dg)
            kept.update(fa & fb)
            blocks_lost.update(bl)
            blocks_gained.update(bg)
    return {"messages": len(before), "messages_after": len(after), "changed": changed,
            "lost": lost, "gained": gained, "kept": kept, "blocks_lost": blocks_lost, "blocks_gained": blocks_gained,
            "messages_gone": sum(1 for k in before if k not in after), "messages_new": sum(1 for k in after if k not in before)}


def simulate(eng: Any, steps: list[dict[str, Any]]) -> tuple[Optional[dict], int]:
    """Run the ``edit.*`` steps of a recipe on the package, snapshot the result and put everything back (tree, undo and
    redo lists, redaction reports).  Returns ``(snapshot after, number of steps that are not edits)``; those steps are
    not run, so their effect is unknown."""
    from .edits import run_edit
    pkg = eng.ensure_package()
    snap, n_log, redo, n_red = pkg.snapshot(), len(pkg.oplog), list(pkg.redo), len(eng.redaction_reports)
    others = 0
    try:
        for st in steps:
            op = str(st.get("op") or "")
            if op.startswith("edit.") and op != "edit.user":
                run_edit(eng, op[5:], dict(st.get("params") or {}))
            else:
                others += 1
        return snapshot(pkg), others
    finally:
        pkg.restore(snap)
        del pkg.oplog[n_log:]
        pkg.redo[:] = redo
        del eng.redaction_reports[n_red:]


# ───────────────────────────── sentences ─────────────────────────────
def _tx(key: str, lang: str) -> str:
    x = (store().file("ui_labels.json").get("effects") or {}).get(key) or {}
    return x.get(lang) or x.get("pl") or key


_FIELD_ORDER = {f.name: i for i, f in enumerate(_MSG_FIELDS)}


def _rank(item: tuple) -> tuple:
    """Most common first; ties: the fields of the message in the model's own order, then ``meta`` keys by name."""
    k, n = item
    return (-n, _FIELD_ORDER.get(k[1], len(_FIELD_ORDER)) if isinstance(k, tuple) and k[0] == "f" else len(_FIELD_ORDER) + 1, str(k))


def _names(c: Counter, labels: dict, limit: int, counts: bool, lang: str) -> list[str]:
    """Most common first; at most ``limit`` names (0 = all), then an ellipsis."""
    rows = sorted(c.items(), key=_rank)
    shown = [f"{n}× {labels[k]}" if counts else labels[k] for k, n in (rows[:limit] if limit else rows)]
    if limit and len(rows) > limit:
        shown.append(_tx("more", lang))
    return shown


def _key_labels(d: dict) -> dict:
    """``("f","author") -> "author"``; a ``meta`` key shows as it is unless a field has the same name (``meta.model``)."""
    fields = {k[1] for c in (d["lost"], d["gained"], d["kept"]) for k in c if k[0] == "f"}
    return {k: (k[1] if k[0] == "f" or k[1] not in fields else "meta." + k[1])
            for c in (d["lost"], d["gained"], d["kept"]) for k in c}


def _block_label(kind: str, lang: str) -> str:
    row = (store().file("block_kinds.json").get("kinds") or {}).get(kind) or {}
    return row.get(lang) or row.get("pl") or kind


def structured(d: dict) -> dict[str, Any]:
    """The same numbers as lists (for the details tree): name -> number of messages / blocks."""
    lab = _key_labels(d)
    def rows(c: Counter, labels: dict) -> list[list]:
        return [[labels[k], n] for k, n in sorted(c.items(), key=_rank)]
    ident = {k: k for k in set(d["blocks_lost"]) | set(d["blocks_gained"])}
    return {"messages_changed": d["changed"], "removed": rows(d["lost"], lab), "added": rows(d["gained"], lab),
            "kept": rows(d["kept"], lab), "blocks_removed": rows(d["blocks_lost"], ident),
            "blocks_added": rows(d["blocks_gained"], ident), "messages_gone": d["messages_gone"], "messages_new": d["messages_new"]}


def _loc(n: int, lang: str) -> str:
    from .plural import noun
    return noun("message_loc", n, lang)


def sentence(action: str, d: dict, *, limit: int = 6, other_steps: int = 0, lang: str = "pl", tree_changed: bool = True) -> str:
    """One plain sentence for ``action`` in ``preview | apply | undo`` from a ``diff``.  ``limit``: names listed per part
    (0 = all; the details tree always has all of them).  ``tree_changed``: whether the package tree differs at all (a
    change that is not in the messages is then said to be elsewhere)."""
    lab = _key_labels(d)
    bl = {k: _block_label(k, lang) for k in set(d["blocks_lost"]) | set(d["blocks_gained"])}

    def part(fields: Counter, blocks: Counter, counts: bool = True) -> str:
        s = ", ".join(_names(fields, lab, limit, counts, lang)) if fields else ""
        if blocks:
            b = _tx("blocks", lang) + ": " + ", ".join(_names(blocks, bl, limit, counts, lang))
            s = f"{s}; {b}" if s else b
        return s

    def msgs(k: int) -> str:
        return plural_count("message", k, lang) if k else ""

    def join(*xs: str) -> str:
        return "; ".join(x for x in xs if x)

    removed, added = join(part(d["lost"], d["blocks_lost"]), msgs(d["messages_gone"])), join(part(d["gained"], d["blocks_gained"]), msgs(d["messages_new"]))
    kept = part(d["kept"], Counter(), counts=False)
    done = action != "preview"
    pieces: list[str] = []
    if action == "undo":
        if added:
            pieces.append(_tx("restored", lang) + " " + added)
        if removed:
            pieces.append(_tx("removed_by_undo", lang) + " " + removed)
    else:
        if removed:
            pieces.append(_tx("gone_done" if done else "gone", lang) + ": " + removed)
        if added:
            pieces.append(_tx("came_done" if done else "came", lang) + ": " + added)
        if kept:
            pieces.append(_tx("stays_done" if done else "stays", lang) + ": " + kept)
    head = {"preview": _tx("preview_head", lang), "apply": _tx("apply_head", lang), "undo": ""}[action]
    tail = _tx("apply_tail", lang) if action == "apply" else ""
    if not (removed or added):  # nothing measurable changed in the messages
        body = _tx(f"{action}_nothing", lang)
        why = _tx("empty_package", lang) if not d["messages"] else (_tx("tree_only", lang) if tree_changed else "")
        out = " ".join(x for x in (head, body, why) if x)
    else:
        pieces_text = "; ".join(pieces)
        n = d["changed"]
        if n:
            key = "preview_from" if action == "preview" and (d["lost"] or d["blocks_lost"]) else f"{action}_in"
            body = _tx(key, lang).format(n=n, pieces=pieces_text, message_loc=_loc(n, lang))
        else:
            body = _tx(f"{action}_plain", lang).format(pieces=pieces_text)
        out = " ".join(x for x in ((_tx("undo_head", lang) if action == "undo" else head), body, tail) if x)
    if other_steps:
        out += " " + _tx("other_steps", lang).format(k=other_steps)
    return out
