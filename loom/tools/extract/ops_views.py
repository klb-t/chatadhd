"""Slice C operations: node-level selection (the commander's Insert, the API's ``select``
with items) and the recipe's own data — tags, bookmarks, saved views.  Tags are read by the
``tag:`` field of the selection language, so they live in the recipe (and its hash) like the
steps; bookmarks and views are recipe data the faces show.  None of these touch the package."""
from typing import Any

from .expand import Selection
from .ops import op


@op("select.items")
def op_select_items(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    from .nodes import resolve_refs
    eng.ensure_units()
    items = [str(x) for x in p.get("items") or []]
    grains = resolve_refs(eng, items)
    reason = str(p.get("reason") or "manual")
    sel = Selection()
    for uid, ms in grains.items():
        sel.add(uid, ms, reason)
    applied = eng.apply_selection(sel, p.get("mode") or "add", {uid: {"source": "items", "reason": reason} for uid in grains})
    return {"counts": {"items": len(items), "units": len(grains)}, "selection": applied}


def _node_key(item: str) -> str:
    return str(item).removeprefix("pkg:")  # a tag or bookmark names the node, wherever it is shown


@op("tag")
def op_tag(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    tags = eng.recipe.setdefault("tags", {})
    key, tag = _node_key(p["item"]), str(p["tag"]).strip()
    if not tag:
        raise ValueError("tag: empty tag")
    cur = set(tags.get(key) or [])
    if p.get("remove"):
        cur.discard(tag)
    else:
        cur.add(tag)
    if cur:
        tags[key] = sorted(cur)
    else:
        tags.pop(key, None)
    return {"counts": {"tags": len(cur), "tagged_nodes": len(tags)}, "node": key, "tags": sorted(cur)}


@op("bookmark")
def op_bookmark(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    marks = eng.recipe.setdefault("bookmarks", [])
    key = _node_key(p["item"])
    marks[:] = [b for b in marks if b.get("node") != key]
    if not p.get("remove"):
        b = {"node": key}
        if p.get("name"):
            b["name"] = str(p["name"])
        marks.append(b)
    return {"counts": {"bookmarks": len(marks)}, "node": key, "bookmarked": not p.get("remove")}


@op("view.save")
def op_view_save(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    views = eng.recipe.setdefault("views", [])
    name = str(p["name"]).strip()
    if not name:
        raise ValueError("view.save: empty name")
    state = p.get("state")
    if not isinstance(state, dict):
        raise ValueError("view.save: state must be an object")
    views[:] = [v for v in views if v.get("name") != name] + [{"name": name, "state": state}]
    views.sort(key=lambda v: str(v.get("name")))
    return {"counts": {"views": len(views)}, "name": name}


@op("view.delete")
def op_view_delete(eng: Any, p: dict[str, Any]) -> dict[str, Any]:
    views = eng.recipe.setdefault("views", [])
    n = len(views)
    views[:] = [v for v in views if v.get("name") != p["name"]]
    return {"counts": {"deleted": n - len(views), "views": len(views)}}
