"""Batch edits ("edycja seryjna") on the package tree: preview → diff, apply,
invert.  Each operation is a data entry (``data/edits/<id>.json``: label,
scope, params, invertibility, preview line) plus a function here (or a user
module registered by ``module: "user:<name>"``, loaded only on explicit
request).  The scope is the selection language (``message[role:tool]``,
``conversation[service:openai]``, a plain expression = conversations, or
``selection``).  Every applied edit is an oplog entry (op, params, count,
tree hash before/after) and a recipe step; undo restores the snapshot taken
before it (redaction is irreversible by design and says so).
"""
import copy
import re
from datetime import timedelta
from typing import Any, Callable, Optional

from . import timestamps as ts
from .cut.views import choose_paths
from .model import Message, Unit
from .package import Package, entry_dict
from .select_expr import ExprContext, items_of, select as expr_select

EDITS: dict[str, Callable[..., dict[str, Any]]] = {}
_WHERE = re.compile(r"^\s*(conversation|message|block|attachment)s?\s*(?:\[(.*)\])?\s*$", re.S)


class EditError(ValueError):
    pass


def edit(name: str) -> Callable[[Callable[..., dict[str, Any]]], Callable[..., dict[str, Any]]]:
    def deco(fn: Callable[..., dict[str, Any]]) -> Callable[..., dict[str, Any]]:
        EDITS[name] = fn
        return fn
    return deco


# ───────────────────────────── scope ─────────────────────────────
def resolve_scope(eng: Any, pkg: Package, where: Any, default_kind: str) -> tuple[str, list[Any]]:
    units = [e.unit for e in pkg.entries]
    if where in (None, "", "all"):
        return default_kind, items_of(units, default_kind)
    where = str(where)
    if where == "selection":
        sel = eng.selection.units
        items = []
        for it in items_of(units, default_kind):
            ms = sel.get(it.unit.uid, False)
            if ms is False:
                continue
            if it.message is not None and ms is not None and it.message.id not in ms:
                continue
            items.append(it)
        return default_kind, items
    m = _WHERE.match(where)
    if m:
        kind, expr = m.group(1), (m.group(2) or "").strip()
    else:
        kind, expr = "conversation", where
    items = items_of(units, kind)
    if not expr:
        return kind, items
    ctx = ExprContext(units, eng.ds, eng.lex)
    base = eng.expr_context()
    ctx.bands, ctx.scores, ctx.clusters, ctx.tags = base.bands, base.scores, base.clusters, base.tags
    ctx.vectors_factory = base.vectors_factory
    return kind, [it for it, _ in expr_select(expr, items, ctx)]


# ───────────────────────────── fields ─────────────────────────────
_DICT_FIELDS = {"conversation": ("flags", "account"), "message": ("meta",), "block": ("extra", "input"), "attachment": ()}


def _obj(item: Any) -> Any:
    return {"conversation": item.unit, "message": item.message, "block": item.block, "attachment": item.attachment}[item.kind]


def get_field(item: Any, field: str) -> Any:
    obj = _obj(item)
    if item.kind == "message" and field == "text":
        return obj.text()
    head, _, rest = field.partition(".")
    val = getattr(obj, head, None)
    if rest:
        for part in rest.split("."):
            val = val.get(part) if isinstance(val, dict) else None
    return val


def set_field(item: Any, field: str, value: Any) -> None:
    obj = _obj(item)
    if item.kind == "message" and field == "text":
        texts = [b for b in obj.blocks if b.kind == "text"]
        if texts:
            texts[0].text = str(value)
            for b in texts[1:]:
                obj.blocks.remove(b)
        else:
            from .model import Block
            obj.blocks.insert(0, Block(kind="text", text=str(value), native="edit"))
        return
    head, _, rest = field.partition(".")
    if rest:
        cur = getattr(obj, head, None)
        if not isinstance(cur, dict):
            if head in _DICT_FIELDS.get(item.kind, ()) or cur is None:
                cur = {}
                setattr(obj, head, cur)
            else:
                raise EditError(f"field {head!r} is not a dict on {item.kind}")
        parts = rest.split(".")
        for part in parts[:-1]:
            cur = cur.setdefault(part, {})
        cur[parts[-1]] = value
        return
    if head in ("created", "updated") and item.kind in ("conversation", "message"):
        st = ts.parse(value)
        setattr(obj, head, value)
        setattr(obj, f"{head}_utc", st.iso)
        return
    if not hasattr(obj, head):
        raise EditError(f"unknown field {field!r} on {item.kind} (dict fields: {', '.join(_DICT_FIELDS.get(item.kind, ())) or '-'})")
    setattr(obj, head, value)


def del_field(item: Any, field: str) -> bool:
    obj = _obj(item)
    head, _, rest = field.partition(".")
    if not rest:
        if head in _DICT_FIELDS.get(item.kind, ()):
            setattr(obj, head, {})
            return True
        if hasattr(obj, head):
            setattr(obj, head, None)
            return True
        return False
    cur = getattr(obj, head, None)
    parts = rest.split(".")
    for part in parts[:-1]:
        cur = cur.get(part) if isinstance(cur, dict) else None
    if isinstance(cur, dict) and parts[-1] in cur:
        del cur[parts[-1]]
        return True
    return False


def _entry_of(pkg: Package, unit: Unit) -> Any:
    for e in pkg.entries:
        if e.unit is unit:
            return e
    return None


def _conv_units(items: list[Any]) -> list[Unit]:
    seen: list[Unit] = []
    for it in items:
        if not any(u is it.unit for u in seen):
            seen.append(it.unit)
    return seen


# ───────────────────────────── values ─────────────────────────────
@edit("set")
def e_set(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    n = 0
    before = None
    for it in items:
        old = get_field(it, p["field"])
        if old != p.get("value"):
            before = old if before is None else before
            set_field(it, p["field"], p.get("value"))
            n += 1
    return {"count": n, "field": p["field"], "before": before, "after": p.get("value")}


@edit("replace_regex")
def e_replace_regex(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    flags = 0
    for ch in p.get("flags") or "":
        flags |= {"i": re.I, "m": re.M, "s": re.S, "x": re.X}.get(ch, 0)
    rx = re.compile(p["pattern"], flags)
    repl = p.get("replacement", "")
    field = p.get("field", "text")
    n = 0
    for it in items:
        if it.kind == "message" and field == "text":
            for b in it.message.blocks:
                if b.kind == "text" and b.text:
                    new, k = rx.subn(repl, b.text)
                    if k:
                        b.text, n = new, n + k
            continue
        old = get_field(it, field)
        if isinstance(old, str):
            new, k = rx.subn(repl, old)
            if k:
                set_field(it, field, new)
                n += k
    return {"count": n, "field": field}


@edit("map_table")
def e_map_table(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    table = p["table"]
    n = 0
    for it in items:
        old = get_field(it, p["field"])
        if isinstance(old, (str, int, float)) and str(old) in table:
            set_field(it, p["field"], table[str(old)])
            n += 1
    return {"count": n, "field": p["field"]}


@edit("rename_key")
def e_rename_key(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    n = 0
    for it in items:
        d = getattr(_obj(it), p.get("in", "meta"), None)
        if isinstance(d, dict) and p["from"] in d:
            d[p["to"]] = d.pop(p["from"])
            n += 1
    return {"count": n, "from": p["from"], "to": p["to"]}


@edit("delete_field")
def e_delete_field(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    return {"count": sum(1 for it in items if del_field(it, p["field"])), "field": p["field"]}


@edit("add_field")
def e_add_field(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    n = 0
    for it in items:
        if get_field(it, p["field"]) is None:
            set_field(it, p["field"], p.get("value"))
            n += 1
    return {"count": n, "field": p["field"]}


# ───────────────────────────── structure ─────────────────────────────
def _relink_linear(u: Unit) -> None:
    prev: Optional[str] = None
    for m in u.messages:
        m.parent = prev
        m.on_current_path = True
        m.branch = "0"
        prev = m.id
    u.tree = {"roots": [u.messages[0].id] if u.messages else [], "current_leaf": prev, "forks": [], "linear": True}


@edit("flatten")
def e_flatten(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    n = cut = 0
    for u in _conv_units(items):
        paths, info = choose_paths(u, p.get("path", "current"), pkg.marks.get(u.uid))
        keep = paths[0]
        cut += len(u.messages) - len(keep)
        u.messages = list(keep)
        _relink_linear(u)
        e = _entry_of(pkg, u)
        if e is not None:
            if e.messages is not None:
                e.messages &= {m.id for m in keep}
            if info.get("messages_cut"):
                e.notes.append(f"flatten({p.get('path', 'current')}): {info['messages_cut']} message(s) on {info['branches_cut']} other branch(es) cut")
        n += 1
    return {"count": n, "cut": cut}


@edit("nest")
def e_nest(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    us = _conv_units(items)
    for u in us:
        _relink_linear(u)
    return {"count": len(us)}


@edit("split_at_date")
def e_split(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    at = p["at"] if isinstance(p["at"], list) else [p["at"]]
    bounds = sorted(ts.to_iso(ts.parse_date_bound(str(a))) for a in at)
    n = parts_total = 0
    for u in _conv_units(items):
        e = _entry_of(pkg, u)
        groups: list[list[Message]] = [[] for _ in range(len(bounds) + 1)]
        for m in u.messages:
            gi = sum(1 for b in bounds if (m.created_utc or "") >= b)
            groups[gi].append(m)
        groups = [g for g in groups if g]
        if len(groups) < 2 or e is None:
            continue
        idx = pkg.entries.index(e)
        new_entries = []
        for gi, g in enumerate(groups, 1):
            ne = copy.deepcopy(e)
            ne.unit.messages = [copy.deepcopy(m) for m in g]
            ids = {m.id for m in ne.unit.messages}
            for m in ne.unit.messages:
                if m.parent not in ids:
                    m.parent = None
            ne.unit.uid = f"{u.uid}~{gi}"
            ne.unit.title = f"{u.title or ''} ({gi}/{len(groups)})".strip()
            ne.unit.tree = {"roots": [m.id for m in ne.unit.messages if m.parent is None], "current_leaf": None, "forks": []}
            ne.unit.created_utc = min((m.created_utc for m in g if m.created_utc), default=u.created_utc)
            ne.messages = None if e.messages is None else (e.messages & ids)
            ne.notes = list(e.notes) + [f"split_at_date: part {gi} of {len(groups)}"]
            new_entries.append(ne)
        pkg.entries[idx:idx + 1] = new_entries
        n += 1
        parts_total += len(groups)
    return {"count": n, "parts": parts_total}


@edit("merge_conversations")
def e_merge(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    us = _conv_units(items)
    if len(us) < 2:
        raise EditError("merge_conversations needs at least two conversations in scope")
    entries = [_entry_of(pkg, u) for u in us]
    first = entries[0]
    msgs: list[tuple[str, int, Message]] = []
    for ui, u in enumerate(us):
        for mi, m in enumerate(u.messages):
            mm = copy.deepcopy(m)
            mm.meta = dict(mm.meta or {})
            mm.meta["from_conversation"] = u.uid
            mm.meta["from_service"] = u.service
            mm.id = f"{ui}:{m.id}"
            msgs.append((m.created_utc or "", ui * 1_000_000 + mi, mm))
    msgs.sort(key=lambda x: (x[0], x[1]))
    merged = first.unit
    merged.messages = [m for _, _, m in msgs]
    _relink_linear(merged)
    merged.attachments = [a for u in us for a in u.attachments]
    merged.title = p.get("title") or merged.title
    merged.uid = "+".join(u.uid for u in us) if len(us) <= 3 else f"{us[0].uid}+{len(us) - 1}"
    merged.flags = dict(merged.flags or {})
    merged.flags["merged_from"] = [u.uid for u in us]
    first.messages = None
    first.notes.append(f"merge_conversations: {len(us)} conversations, {len(merged.messages)} messages ordered by time")
    for e in entries[1:]:
        pkg.entries.remove(e)
    return {"count": len(us)}


_SORT_KEYS = {"created": lambda u: u.created_utc or "", "updated": lambda u: u.updated_utc or u.created_utc or "",
              "title": lambda u: (u.title or "").casefold(), "first_message": lambda u: (u.stats or {}).get("first_message_utc") or "",
              "last_message": lambda u: (u.stats or {}).get("last_message_utc") or ""}


@edit("sort")
def e_sort(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    by = p.get("by", "created")
    rev = bool(p.get("reverse"))
    if p.get("level", "conversations") == "messages":
        n = 0
        for u in _conv_units(items):
            before = [m.id for m in u.messages]
            u.messages = sorted(u.messages, key=lambda m: m.created_utc or "", reverse=rev)
            n += int(before != [m.id for m in u.messages])
        return {"count": n, "by": "created"}
    scope = {id(u) for u in _conv_units(items)}
    pos = [i for i, e in enumerate(pkg.entries) if id(e.unit) in scope]
    ents = [pkg.entries[i] for i in pos]
    key = _SORT_KEYS.get(by, _SORT_KEYS["created"])
    ordered = sorted(ents, key=lambda e: key(e.unit), reverse=rev)
    for i, e in zip(pos, ordered):
        pkg.entries[i] = e
    return {"count": sum(1 for a, b in zip(ents, ordered) if a is not b), "by": by}


def _blocks_filter(items: list[Any], keep: Callable[[str], bool]) -> int:
    n = 0
    msgs: list[Message] = []
    for it in items:
        if it.kind == "message":
            msgs.append(it.message)
        else:
            msgs.extend(it.unit.messages)
    seen: set[int] = set()
    for m in msgs:
        if id(m) in seen:
            continue
        seen.add(id(m))
        before = len(m.blocks)
        m.blocks = [b for b in m.blocks if keep(b.kind)]
        n += before - len(m.blocks)
    return n


@edit("drop_blocks")
def e_drop_blocks(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    kinds = set(p["kinds"])
    return {"count": _blocks_filter(items, lambda k: k not in kinds)}


@edit("keep_blocks")
def e_keep_blocks(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    kinds = set(p["kinds"])
    return {"count": _blocks_filter(items, lambda k: k in kinds)}


@edit("relabel_roles")
def e_relabel(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    mp = p["map"]
    n = 0
    msgs = [it.message for it in items if it.kind == "message"] or [m for u in _conv_units(items) for m in u.messages]
    for m in msgs:
        if m.role in mp:
            m.role = mp[m.role]
            n += 1
    return {"count": n}


# ───────────────────────────── time ─────────────────────────────
def _shift_obj(obj: Any, delta: timedelta, field: str) -> bool:
    utc = getattr(obj, f"{field}_utc", None)
    if not utc:
        return False
    dt = ts.from_iso(utc)
    if dt is None:
        return False
    new = ts.to_iso(dt + delta)
    setattr(obj, f"{field}_utc", new)
    setattr(obj, field, new)
    return True


def _units_times(u: Unit, delta: timedelta) -> int:
    n = int(_shift_obj(u, delta, "created")) + int(_shift_obj(u, delta, "updated"))
    for m in u.messages:
        n += int(_shift_obj(m, delta, "created"))
    if u.stats:
        for k in ("first_message_utc", "last_message_utc"):
            if u.stats.get(k):
                u.stats[k] = ts.to_iso(ts.from_iso(u.stats[k]) + delta)
    return n


@edit("shift_time")
def e_shift(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    by = str(p["by"])
    sign = -1 if by.startswith("-") else 1
    delta = ts.parse_duration(by.lstrip("+-")) * sign
    return {"count": sum(_units_times(u, delta) for u in _conv_units(items)), "by": by}


@edit("anonymise_time")
def e_anonymise(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    from datetime import datetime, timezone
    epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
    us = _conv_units(items)
    n = 0

    def first_of(u: Unit) -> Any:
        st = [x for x in [u.created_utc] + [m.created_utc for m in u.messages] if x]
        return ts.from_iso(min(st)) if st else None
    if p.get("origin", "package") == "package":
        firsts = [f for f in (first_of(u) for u in us) if f is not None]
        if firsts:
            delta = epoch - min(firsts)
            n = sum(_units_times(u, delta) for u in us)
    else:
        for u in us:
            f = first_of(u)
            if f is not None:
                n += _units_times(u, epoch - f)
    return {"count": n}


@edit("redact")
def e_redact(pkg: Package, kind: str, items: list[Any], p: dict[str, Any], eng: Any) -> dict[str, Any]:
    from .redact import Redactor
    red = Redactor(p.get("profile") or "personal", eng.ds, terms=list(p.get("terms") or []))
    for u in _conv_units(items):
        where = {"conversation": u.uid}
        u.title = red.text(u.title, where, "title")
        for m in u.messages:
            mw = {**where, "message": m.id}
            m.author = red.text(m.author, mw, "author") if m.author else m.author
            for i, b in enumerate(m.blocks):
                bw = {**mw, "block": i}
                b.text = red.text(b.text, bw, "text")
                if b.name:
                    b.name = red.text(b.name, bw, "name")
                if b.input is not None:
                    b.input = red.value(b.input, bw, "input")
    rep = red.report()
    eng.redaction_reports.append(rep)
    return {"count": rep["entries"], "by_kind": rep["by_kind"], "irreversible": True}


# ───────────────────────────── driver ─────────────────────────────
def _diff(a: Any, b: Any, path: str, out: list[dict[str, Any]], limit: int) -> None:
    if limit and len(out) >= limit:
        return
    if isinstance(a, dict) and isinstance(b, dict):
        for k in a:
            p = f"{path}/{str(k).replace('~', '~0').replace('/', '~1')}"
            if k not in b:
                out.append({"op": "remove", "path": p, "old": a[k]})
            else:
                _diff(a[k], b[k], p, out, limit)
        for k in b:
            if k not in a:
                out.append({"op": "add", "path": f"{path}/{str(k).replace('~', '~0').replace('/', '~1')}", "value": b[k]})
        return
    if isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        for i, (x, y) in enumerate(zip(a, b)):
            _diff(x, y, f"{path}/{i}", out, limit)
        return
    if a != b:
        out.append({"op": "replace", "path": path, "old": a, "value": b})


def package_patch(before: dict[str, Any], after: dict[str, Any], limit: int = 0) -> list[dict[str, Any]]:
    """JSON Patch (RFC 6902, plus ``old`` for reading) between two package snapshots, keyed by unit uid."""
    a = {d["unit"]["uid"]: d for d in before["entries"]}
    b = {d["unit"]["uid"]: d for d in after["entries"]}
    out: list[dict[str, Any]] = []
    if before["order"] != after["order"] and set(before["order"]) == set(after["order"]):
        out.append({"op": "replace", "path": "/order", "old": before["order"], "value": after["order"]})
    _diff(a, b, "", out, limit)
    return out


def run_edit(eng: Any, op_id: str, params: dict[str, Any], *, preview: bool = False) -> dict[str, Any]:
    spec = eng.ds.entry("edits", op_id)
    mod = str(spec.get("module") or "")
    if mod.startswith("user:"):
        raise EditError(f"edit {op_id}: user module {mod!r} — call it through op user.call (needs settings.allow_user_code)")
    fn = EDITS.get(op_id)
    if fn is None:
        raise EditError(f"edit {op_id!r} is described in data but not implemented")
    p = {k: v.get("default") for k, v in (spec.get("params") or {}).items() if "default" in v}
    p.update({k: v for k, v in params.items() if v is not None})
    for k, v in (spec.get("params") or {}).items():
        if v.get("required") and p.get(k) is None:
            raise EditError(f"edit {op_id}: missing param {k!r}")
    pkg = eng.ensure_package()
    default_kind = (spec.get("scope") or ["conversation"])[0]
    kind, items = resolve_scope(eng, pkg, p.get("where"), default_kind)
    if kind not in (spec.get("scope") or [kind]):
        raise EditError(f"edit {op_id}: scope {kind!r} not allowed (allowed: {', '.join(spec.get('scope') or [])})")
    before_hash = pkg.tree_hash()
    snap = pkg.snapshot()
    res = fn(pkg, kind, items, p, eng)
    after_hash = pkg.tree_hash()
    limit = int(eng.ds.preset("edits.preview_sample", 0) or 0)
    patch = package_patch(snap, pkg.snapshot(), limit)
    from .i18n import label
    try:
        line = label(spec.get("preview_lines")).format(**{**p, **res})
    except (KeyError, IndexError, ValueError):
        line = f"{op_id}: {res.get('count', 0)}"
    out = {"edit": op_id, "scope": {"kind": kind, "where": p.get("where"), "items": len(items)}, "count": int(res.get("count", 0)),
           "before": before_hash, "after": after_hash, "patch": patch, "lines": [line], "result": {k: v for k, v in res.items() if k != "count"},
           "invertible": spec.get("invertible", "snapshot")}
    if preview:
        pkg.restore(snap)
        out["preview"] = True
        out["after_preview"] = after_hash
        out["after"] = pkg.tree_hash()
        return out
    entry = {"n": len(pkg.oplog) + 1, "op": f"edit.{op_id}", "params": p, "count": out["count"], "before": before_hash,
             "after": after_hash, "invertible": out["invertible"], "lines": out["lines"]}
    if out["invertible"] != "none":
        entry["_snapshot"] = snap
    pkg.oplog.append(entry)
    pkg.redo.clear()
    return out


def oplog_rows(pkg: Package) -> list[dict[str, Any]]:
    return [{k: v for k, v in e.items() if not k.startswith("_")} for e in pkg.oplog]


__all__ = ["EDITS", "EditError", "run_edit", "resolve_scope", "package_patch", "oplog_rows", "entry_dict"]
