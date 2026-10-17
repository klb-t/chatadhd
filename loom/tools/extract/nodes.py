"""Views over the engine state: what a commander pane shows, what ``GET /api/view`` and the
``list`` call return — the same rows for every face, so the web Warsztat can later draw the
same commander without re-implementing anything.

Roots (``data/commander/layout.json#roots``, an open list): ``sources`` (archives →
conversations → branches → messages → blocks; attachments), ``package`` (folders → entries →
the same levels, on the COPY), ``selection``, ``timeline`` (one global list ordered by time
across every archive and service), ``attachments`` (by type), ``clusters``, ``recipe`` (steps,
bookmarks, views, tags), ``inventory`` and ``diff`` (JSON trees), ``bookmarks``.  A pane is
either a tree (``path`` = node ids from the root) or a flat list of one node kind under the
current node (``flat: "message"`` = every message of every archive), sorted by any column and
filtered live (plain text, folded, or an expression of the selection language).

Node ids are what the other calls take back: ``arch:N``, ``<uid>``, ``<uid>|bN`` (branch),
``<uid>#<message id>``, ``<uid>#<message id>/<block index>``, ``<uid>@<attachment ref>``; the
package copy prefixes them with ``pkg:``; ``dir:/path`` (package folder), ``step:sN``,
``atype:image``, ``cl:<id>``, ``json:<root>:<pointer>``, ``grp:<name>``.
"""
import ast
import json
import logging
import operator
import re
from typing import Any, Callable, Optional

from .i18n import label as _label, t
from .select_expr import ExprContext, ExprError, Item, _count, field_value, select as expr_select

log = logging.getLogger(__name__)

_ID = re.compile(r"^(?P<pkg>pkg:)?(?P<uid>sha256:[0-9a-f]+)"
                 r"(?:\|b(?P<branch>\d+)|#(?P<mid>.+?)(?:/(?P<blk>\d+))?|@(?P<att>.+))?$", re.S)
ITEM_KINDS = ("conversation", "message", "block", "attachment")


class ViewError(ValueError):
    pass


# ───────────────────────────── ids ─────────────────────────────
def parse_id(nid: str) -> Optional[dict[str, Any]]:
    m = _ID.match(nid or "")
    if not m:
        return None
    d = m.groupdict()
    kind = "conversation"
    if d["branch"]:
        kind = "branch"
    elif d["mid"] is not None:
        kind = "block" if d["blk"] is not None else "message"
    elif d["att"] is not None:
        kind = "attachment"
    return {"package": bool(d["pkg"]), "uid": d["uid"], "kind": kind, "branch": int(d["branch"]) if d["branch"] else None,
            "mid": d["mid"], "block": int(d["blk"]) if d["blk"] is not None else None, "att": d["att"]}


def resolve_refs(eng: Any, refs: list[str]) -> dict[str, Optional[set[str]]]:
    """Node references → selection grains (uid → message ids, None = whole conversation): a branch is its
    messages, a block or an attachment is the message that carries it.  Unknown references are errors."""
    by = {u.uid: u for u in eng.units}
    if eng.package is not None:
        for e in eng.package.entries:
            by.setdefault(e.unit.uid, e.unit)
    out: dict[str, Optional[set[str]]] = {}

    def add(uid: str, ms: Optional[set[str]]) -> None:
        if uid in out and out[uid] is None:
            return
        if ms is None:
            out[uid] = None
        else:
            out[uid] = (out.get(uid) or set()) | ms

    for r in refs:
        p = parse_id(r)
        if p is None or p["uid"] not in by:
            raise ViewError(f"unknown node {r!r}")
        u = by[p["uid"]]
        if p["kind"] == "conversation":
            add(u.uid, None)
        elif p["kind"] == "branch":
            br = u.branches()
            if not 1 <= p["branch"] <= len(br):
                raise ViewError(f"unknown branch {r!r}")
            add(u.uid, {m.id for m in br[p["branch"] - 1]})
        elif p["kind"] in ("message", "block"):
            if p["mid"] not in u.by_id():
                raise ViewError(f"unknown message {r!r}")
            add(u.uid, {p["mid"]})
        else:
            att = next((a for a in u.attachments if a.ref == p["att"]), None)
            if att is None:
                raise ViewError(f"unknown attachment {r!r}")
            add(u.uid, {att.message} if att.message else None)
    return out


# ───────────────────────────── computed columns ─────────────────────────────
_BIN = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul, ast.Div: operator.truediv,
        ast.FloorDiv: operator.floordiv, ast.Mod: operator.mod}


def compute(expr: str, getter: Callable[[str], Any], counter: Callable[[str], float]) -> Any:
    """A computed column (``layout.json#columns.*.expr``): arithmetic over field names and
    ``count(kind)``, ``len(x)``, ``round(x, n)``, ``max``/``min`` — nothing else is evaluated."""
    tree = ast.parse(expr, mode="eval")

    def ev(n: ast.AST) -> Any:
        if isinstance(n, ast.Expression):
            return ev(n.body)
        if isinstance(n, ast.Constant) and isinstance(n.value, (int, float, str)):
            return n.value
        if isinstance(n, ast.Name):
            return getter(n.id)
        if isinstance(n, ast.BinOp) and type(n.op) in _BIN:
            a, b = ev(n.left), ev(n.right)
            if isinstance(n.op, (ast.Div, ast.FloorDiv, ast.Mod)) and not b:
                return 0
            return _BIN[type(n.op)](a or 0, b or 0)
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, ast.USub):
            return -(ev(n.operand) or 0)
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and not n.keywords:
            fn = n.func.id
            if fn == "count" and len(n.args) == 1 and isinstance(n.args[0], (ast.Name, ast.Constant)):
                arg = n.args[0].id if isinstance(n.args[0], ast.Name) else str(n.args[0].value)
                return counter(arg)
            args = [ev(a) for a in n.args]
            if fn == "len" and len(args) == 1:
                return len(args[0]) if hasattr(args[0], "__len__") else 0
            if fn == "round" and 1 <= len(args) <= 2:
                return round(float(args[0] or 0), int(args[1]) if len(args) > 1 else 0)
            if fn in ("max", "min") and args:
                return (max if fn == "max" else min)(a or 0 for a in args)
        raise ViewError(f"computed column: unsupported expression {expr!r}")
    return ev(tree)


# ───────────────────────────── the view builder ─────────────────────────────
class Views:
    """Builds rows for one engine state; cheap to create per call."""

    def __init__(self, eng: Any, layout: dict[str, Any], *, bookmarks: Optional[set[str]] = None) -> None:
        self.eng = eng
        self.layout = layout
        self.by = {u.uid: u for u in eng.units}
        self.pkg = eng.package
        self.pkg_by = self.pkg.by_uid() if self.pkg is not None else {}
        self.sel = eng.selection.units
        self.reasons = eng.selection.reasons
        self.tags = eng.recipe.get("tags") or {}
        self.bookmarks = bookmarks if bookmarks is not None else {b.get("node") for b in eng.recipe.get("bookmarks") or []}
        self._ctx: dict[bool, ExprContext] = {}
        self._branches: dict[tuple[bool, str], list[Any]] = {}

    # -- lookups ------------------------------------------------------------------
    def ctx(self, package: bool = False) -> ExprContext:
        if package not in self._ctx:
            if package:
                units = [e.unit for e in self.pkg.entries] if self.pkg is not None else []
                c = ExprContext(units, self.eng.ds, self.eng.lex)
                base = self.eng.expr_context()
                c.bands, c.scores, c.clusters, c.tags = base.bands, base.scores, base.clusters, base.tags
                c.vectors_factory = base.vectors_factory
            else:
                c = self.eng.expr_context()
            self._ctx[package] = c
        return self._ctx[package]

    def unit(self, uid: str, package: bool = False) -> Any:
        if package:
            e = self.pkg_by.get(uid)
            return e.unit if e is not None else None
        return self.by.get(uid)

    def branches(self, u: Any, package: bool) -> list[Any]:
        key = (package, u.uid)
        if key not in self._branches:
            self._branches[key] = u.branches()
        return self._branches[key]

    def item(self, nid: str) -> tuple[Optional[Item], dict[str, Any]]:
        """Node id → (selection-language item, parsed id); branches resolve to their conversation."""
        p = parse_id(nid)
        if p is None:
            return None, {}
        u = self.unit(p["uid"], p["package"])
        if u is None:
            raise ViewError(f"unknown node {nid!r}")
        if p["kind"] in ("conversation", "branch"):
            return Item("conversation", u), p
        if p["kind"] in ("message", "block"):
            idx = next((i for i, m in enumerate(u.messages) if m.id == p["mid"]), None)
            if idx is None:
                raise ViewError(f"unknown message {nid!r}")
            m = u.messages[idx]
            if p["kind"] == "message":
                return Item("message", u, m, index=idx), p
            if not 0 <= p["block"] < len(m.blocks):
                raise ViewError(f"unknown block {nid!r}")
            return Item("block", u, m, m.blocks[p["block"]], index=p["block"]), p
        att = next((a for a in u.attachments if a.ref == p["att"]), None)
        if att is None:
            raise ViewError(f"unknown attachment {nid!r}")
        return Item("attachment", u, attachment=att), p

    # -- children -----------------------------------------------------------------
    def children(self, root: str, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        """(id, kind, object, has_children) under ``nid`` (None = the root level)."""
        fn = getattr(self, "_root_" + root.replace("-", "_"), None)
        if fn is None:
            raise ViewError(f"unknown root {root!r} (known: {', '.join(self.layout.get('roots') or [])})")
        return fn(nid)

    def _unit_children(self, nid: str, package: bool) -> list[tuple[str, str, Any, bool]]:
        item, p = self.item(nid)
        if item is None:
            return []
        pre = "pkg:" if package else ""
        u = item.unit
        out: list[tuple[str, str, Any, bool]] = []
        if p["kind"] == "conversation":
            brs = self.branches(u, package)
            if len(brs) > 1:
                for i, br in enumerate(brs, 1):
                    out.append((f"{pre}{u.uid}|b{i}", "branch", {"unit": u, "index": i, "messages": br}, True))
            else:
                for i, m in enumerate(u.messages):
                    out.append((f"{pre}{u.uid}#{m.id}", "message", Item("message", u, m, index=i), bool(m.blocks)))
            for a in u.attachments:
                out.append((f"{pre}{u.uid}@{a.ref}", "attachment", Item("attachment", u, attachment=a), False))
        elif p["kind"] == "branch":
            brs = self.branches(u, package)
            if not 1 <= p["branch"] <= len(brs):
                raise ViewError(f"unknown branch {nid!r}")
            index = {m.id: i for i, m in enumerate(u.messages)}
            for m in brs[p["branch"] - 1]:
                out.append((f"{pre}{u.uid}#{m.id}", "message", Item("message", u, m, index=index[m.id]), bool(m.blocks)))
        elif p["kind"] == "message":
            m = item.message
            for i, b in enumerate(m.blocks):
                out.append((f"{pre}{u.uid}#{m.id}/{i}", "block", Item("block", u, m, b, index=i), False))
        return out

    def _root_sources(self, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        eng = self.eng
        if nid is None:
            out = []
            for i, a in enumerate(eng.archives, 1):
                n = sum(1 for u in eng.conversations if (u.source or {}).get("sha256") == a.sha256)
                out.append((f"arch:{i}", "archive", {"label": a.label, "format": a.format, "size": a.size, "units": n,
                                                     "path": None if eng.aggregate else a.path, "sha256": a.sha256}, n > 0))
            return out
        if nid.startswith("arch:"):
            i = int(nid[5:])
            if not 1 <= i <= len(eng.archives):
                raise ViewError(f"unknown archive {nid!r}")
            sha = eng.archives[i - 1].sha256
            return [(u.uid, "conversation", Item("conversation", u), True)
                    for u in eng.conversations if (u.source or {}).get("sha256") == sha]
        return self._unit_children(nid, False)

    def _root_package(self, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        pkg = self.pkg
        if pkg is None:
            return []
        if nid is None or nid.startswith("dir:"):
            folder = "/" if nid is None else nid[4:]
            out: list[tuple[str, str, Any, bool]] = []
            prefix = folder.rstrip("/") + "/"
            subs = sorted({prefix + f[len(prefix):].split("/", 1)[0] for f in pkg.folders
                           if f != folder and f.startswith(prefix) and f[len(prefix):]})
            for f in subs:
                n = sum(1 for e in pkg.entries if e.folder == f or e.folder.startswith(f + "/"))
                out.append((f"dir:{f}", "folder", {"path": f, "name": f.rsplit("/", 1)[-1], "entries": n}, True))
            for e in pkg.entries:
                if e.folder == folder:
                    out.append((f"pkg:{e.unit.uid}", "conversation", Item("conversation", e.unit), True))
            return out
        return self._unit_children(nid, True)

    def _root_selection(self, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        if nid is not None:
            return self._unit_children(nid, False)
        return [(uid, "conversation", Item("conversation", self.by[uid]), True) for uid in self.sel if uid in self.by]

    def _root_timeline(self, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        if nid is not None:
            return self._unit_children(nid, False)
        kind = (self.layout.get("timeline") or {}).get("kind", "message")
        return [r for r in self.flat_items("sources", None, kind)]

    def _root_attachments(self, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        atts = [(f"{u.uid}@{a.ref}", Item("attachment", u, attachment=a)) for u in self.eng.conversations for a in u.attachments]
        if nid is None:
            groups: dict[str, int] = {}
            for _, it in atts:
                g = (it.attachment.mime or "?").split("/", 1)[0]
                groups[g] = groups.get(g, 0) + 1
            return [(f"atype:{g}", "group", {"name": g, "entries": n}, True) for g, n in sorted(groups.items())]
        if nid.startswith("atype:"):
            g = nid[6:]
            return [(i, "attachment", it, False) for i, it in atts if (it.attachment.mime or "?").split("/", 1)[0] == g]
        return []

    def _root_clusters(self, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        cl = self.eng.clusters
        if cl is None:
            return []
        if nid is None:
            return [(f"cl:{c['id']}", "cluster", {"id": c["id"], "name": c.get("name") or ", ".join(c.get("terms") or [])[:60],
                                                  "entries": len(c.get("conversations") or [])}, True)
                    for c in cl.get("clusters") or []]
        if nid.startswith("cl:"):
            c = next((c for c in cl.get("clusters") or [] if c["id"] == nid[3:]), None)
            if c is None:
                raise ViewError(f"unknown cluster {nid!r}")
            return [(uid, "conversation", Item("conversation", self.by[uid]), True) for uid in c.get("conversations") or [] if uid in self.by]
        return self._unit_children(nid, False)

    def _root_recipe(self, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        r = self.eng.recipe
        if nid is None:
            return [(f"grp:{g}", "group", {"name": g, "entries": len(r.get(g) or [])}, True) for g in ("steps", "bookmarks", "views", "tags")]
        if nid == "grp:steps":
            return [(f"step:{s['id']}", "step", s, False) for s in r.get("steps") or []]
        if nid == "grp:bookmarks":
            return [(b.get("node", ""), "bookmark", b, False) for b in r.get("bookmarks") or []]
        if nid == "grp:views":
            return [(f"view:{v.get('name')}", "view", v, False) for v in r.get("views") or []]
        if nid == "grp:tags":
            return [(k, "tag", {"node": k, "tags": v}, False) for k, v in sorted((r.get("tags") or {}).items())]
        return []

    def _root_bookmarks(self, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        if nid is not None:
            return self._unit_children(nid, False) if parse_id(nid) else []
        out = []
        for b in self.eng.recipe.get("bookmarks") or []:
            node = b.get("node", "")
            try:
                it, p = self.item(node)
            except ViewError:
                it, p = None, {}
            if it is None:
                out.append((node, "bookmark", b, False))
            else:
                out.append((node, it.kind if p.get("kind") != "branch" else "conversation", it, it.kind in ("conversation", "message")))
        return out

    def _json_root(self, name: str, doc: Any, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        if doc is None:
            return []
        ptr = "" if nid is None else nid.split(":", 2)[2]
        cur = doc
        for part in [x for x in ptr.split("/") if x != ""]:
            part = part.replace("~1", "/").replace("~0", "~")
            cur = cur[int(part)] if isinstance(cur, list) else cur[part]
        items = list(cur.items()) if isinstance(cur, dict) else list(enumerate(cur)) if isinstance(cur, list) else []
        out = []
        for k, v in items:
            key = str(k).replace("~", "~0").replace("/", "~1")
            out.append((f"json:{name}:{ptr}/{key}", "json", {"key": str(k), "value": v}, isinstance(v, (dict, list)) and bool(v)))
        return out

    def _root_inventory(self, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        return self._json_root("inventory", self.eng.inventory, nid)

    def _root_diff(self, nid: Optional[str]) -> list[tuple[str, str, Any, bool]]:
        return self._json_root("diff", self.eng.diff, nid)

    # -- flat lists ----------------------------------------------------------------------
    def flat_items(self, root: str, nid: Optional[str], kind: str) -> list[tuple[str, str, Any, bool]]:
        """Every node of ``kind`` under ``nid`` (whole root when None)."""
        if kind not in ITEM_KINDS:
            raise ViewError(f"flat list of {kind!r}: one of {', '.join(ITEM_KINDS)}")
        package = root == "package"
        pre = "pkg:" if package else ""
        if package:
            if self.pkg is None:
                return []
            folder = None if nid is None else nid[4:] if nid.startswith("dir:") else None
            units = [e.unit for e in self.pkg.entries if folder is None or e.folder == folder or e.folder.startswith(folder.rstrip("/") + "/")]
            if nid is not None and not nid.startswith("dir:"):
                units = [self.item(nid)[0].unit]
        elif nid is None:
            units = list(self.eng.conversations) if root != "selection" else [self.by[u] for u in self.sel if u in self.by]
        elif nid.startswith("arch:"):
            units = [x[2].unit for x in self._root_sources(nid)]
        elif nid.startswith("cl:"):
            units = [x[2].unit for x in self._root_clusters(nid)]
        else:
            it, _ = self.item(nid)
            units = [it.unit] if it is not None else []
        out: list[tuple[str, str, Any, bool]] = []
        for u in units:
            if kind == "conversation":
                out.append((f"{pre}{u.uid}", "conversation", Item("conversation", u), True))
            elif kind == "message":
                out += [(f"{pre}{u.uid}#{m.id}", "message", Item("message", u, m, index=i), bool(m.blocks)) for i, m in enumerate(u.messages)]
            elif kind == "block":
                for m in u.messages:
                    out += [(f"{pre}{u.uid}#{m.id}/{i}", "block", Item("block", u, m, b, index=i), False) for i, b in enumerate(m.blocks)]
            else:
                out += [(f"{pre}{u.uid}@{a.ref}", "attachment", Item("attachment", u, attachment=a), False) for a in u.attachments]
        if root == "timeline" or kind != "conversation":
            out.sort(key=lambda r: self.time_key(r[2]))  # one global order across archives and services, stable ties
        return out

    @staticmethod
    def time_key(it: Any) -> tuple:
        u = it.unit
        src = u.source or {}
        if it.kind == "conversation":
            t_ = u.created_utc or ""
        elif it.message is not None:
            t_ = it.message.created_utc or u.created_utc or ""
        else:
            m = next((m for m in u.messages if m.id == it.attachment.message), None) if it.attachment is not None else None
            t_ = (m.created_utc if m is not None else None) or u.created_utc or ""
        from .timestamps import key as _tskey
        return (t_ == "", _tskey(t_), u.service or "", src.get("sha256") or "", u.uid, it.index)

    # -- rows -----------------------------------------------------------------------------
    def fields_of(self, kind: str) -> list[dict[str, Any]]:
        cols = (self.layout.get("columns") or {}).get(kind)
        return cols if cols is not None else [{"id": "label"}]

    def value(self, nid: str, kind: str, obj: Any, field: str) -> Any:
        """A column value: view fields first, then any field of the selection language."""
        if isinstance(obj, Item):
            it = obj
            if field == "label":
                return self.label(kind, obj)
            if field == "preview":
                txt = it.block.text if it.block is not None else it.message.text() if it.message is not None else ""
                return " ".join((txt or "").split())
            if field == "blocks":
                return len(it.message.blocks) if it.message is not None and it.block is None else 0
            if field == "chars":
                return it.block.chars() if it.block is not None else it.message.chars() if it.message is not None else \
                    sum(m.chars() for m in it.unit.messages)
            if field == "folder":
                e = self.pkg_by.get(it.unit.uid) if nid.startswith("pkg:") else None
                return e.folder if e is not None else ""
            if field == "size" and it.attachment is not None:
                return it.attachment.size
            if field == "tags":
                return ",".join(self.tags.get(nid.removeprefix("pkg:"), []))
            if field == "selected":
                return self.sel_state(nid, it)
            try:
                v, _ = field_value(it, field, self.ctx(nid.startswith("pkg:")))
                return v
            except ExprError:
                return None
        if isinstance(obj, dict):
            if kind == "branch":
                br = obj["messages"]
                return {"label": f"{t('commander.branch')} {obj['index']}", "messages": len(br),
                        "created": br[0].created_utc if br else None, "last": br[-1].created_utc if br else None,
                        "current": all(m.on_current_path for m in br)}.get(field)
            if kind == "step":
                return {"label": f"{obj.get('id')} {obj.get('op')}", "op": obj.get("op"), "by": obj.get("by"),
                        "params": json.dumps(obj.get("params") or {}, ensure_ascii=False, sort_keys=True)}.get(field, obj.get(field))
            if kind == "json":
                v = obj["value"]
                return {"label": obj["key"], "value": v if not isinstance(v, (dict, list)) else f"{type(v).__name__}[{len(v)}]",
                        "type": type(v).__name__}.get(field)
            if field == "label":
                return obj.get("label") or obj.get("name") or obj.get("node") or obj.get("id")
            return obj.get(field)
        return None

    def label(self, kind: str, it: Item) -> str:
        if self.eng.aggregate:  # privacy: no titles, texts or file names in aggregate mode
            return {"conversation": it.unit.uid[:15], "message": f"{it.message.role} #{it.index}" if it.message else "",
                    "block": it.block.kind if it.block else "", "attachment": it.attachment.mime or "?" if it.attachment else ""}.get(kind, "")
        if kind == "conversation":
            e = self.pkg_by.get(it.unit.uid)
            return (e.name if e is not None and e.name else None) or it.unit.title or it.unit.ext_id or it.unit.uid[:15]
        if kind == "message":
            txt = " ".join((it.message.text() or "").split())
            return f"{it.message.role}: {txt}" if txt else f"{it.message.role} [{', '.join(sorted({b.kind for b in it.message.blocks}))}]"
        if kind == "block":
            b = it.block
            return f"{b.kind}{' ' + b.name if b.name else ''}: {' '.join((b.text or '').split())}"
        a = it.attachment
        return a.name or a.ref

    def sel_state(self, nid: str, it: Any) -> str:
        if not isinstance(it, Item):
            return ""
        ms = self.sel.get(it.unit.uid, False)
        if ms is False:
            return ""
        if it.kind == "conversation":
            return "all" if ms is None else "part"
        mid = it.message.id if it.message is not None else it.attachment.message if it.attachment is not None else None
        return "all" if ms is None or (mid is not None and mid in ms) else ""

    def row(self, nid: str, kind: str, obj: Any, children: bool) -> dict[str, Any]:
        cols: dict[str, Any] = {}
        for c in self.fields_of(kind):
            cid = c["id"]
            try:
                if c.get("expr"):
                    it = obj if isinstance(obj, Item) else None
                    v = compute(c["expr"], lambda f: self.value(nid, kind, obj, f),
                                (lambda w: _count(it, w)) if it is not None else (lambda w: 0))
                else:
                    v = self.value(nid, kind, obj, c.get("field") or cid)
            except (ViewError, ExprError, TypeError, ValueError, KeyError):
                log.debug("column %s of %s", cid, nid, exc_info=True)
                v = None
            cols[cid] = fmt(v, c)
        r: dict[str, Any] = {"id": nid, "kind": kind, "label": fmt(self.value(nid, kind, obj, "label"), {}), "cols": cols,
                             "children": bool(children)}
        st = self.sel_state(nid, obj)
        if st:
            r["sel"] = st
            reasons = self.reasons.get(obj.unit.uid) if isinstance(obj, Item) else None
            if reasons and not self.eng.aggregate:
                r["reason"] = "; ".join(reasons[:3])
        key = nid.removeprefix("pkg:")
        if key in self.bookmarks:
            r["bookmark"] = True
        if self.tags.get(key):
            r["tags"] = list(self.tags[key])
        return r

    # -- filter and sort ---------------------------------------------------------------------
    def filtered(self, rows: list[tuple[str, str, Any, bool]], text: str, package: bool) -> tuple[list[tuple[str, str, Any, bool]], Optional[str]]:
        """Live filter: an expression of the selection language when the text parses as one with a field
        (``role:user``, ``title~/x/``, ``count(tool_use) >= 2``), otherwise folded text on the visible columns."""
        text = (text or "").strip()
        if not text:
            return rows, None
        if looks_like_expr(text):
            items = [r[2] for r in rows if isinstance(r[2], Item)]
            try:
                hits = {id(it) for it, _ in expr_select(text, items, self.ctx(package))}
                return [r for r in rows if isinstance(r[2], Item) and id(r[2]) in hits], "expr"
            except ExprError as exc:
                log.debug("live filter is not an expression yet: %s", exc)
        lex = self.eng.lex
        needle = lex.fold(text.casefold())
        fields = (self.layout.get("filter") or {}).get("fields") or ["label", "cols"]
        out = []
        for r in rows:
            hay: list[str] = []
            for f in fields:
                if f == "cols":
                    hay += [str(fmt(self.value(r[0], r[1], r[2], c.get("field") or c["id"]), c)) for c in self.fields_of(r[1]) if not c.get("expr")]
                else:
                    hay.append(str(self.value(r[0], r[1], r[2], f) or ""))
            if needle in lex.fold(" ".join(hay).casefold()):
                out.append(r)
        return out, "text"

    def sorted_rows(self, rows: list[tuple[str, str, Any, bool]], sort: str) -> list[tuple[str, str, Any, bool]]:
        if not sort:
            return rows
        desc = sort.startswith("-")
        key = sort.lstrip("-+")

        def k(r: tuple[str, str, Any, bool]) -> tuple:
            if key in ("created", "time") and isinstance(r[2], Item):
                return (0,) + self.time_key(r[2])
            v = self.value(r[0], r[1], r[2], key)
            if v is None:
                return (2, "")
            if isinstance(v, (int, float)) and not isinstance(v, bool):
                return (0, v, r[0])
            return (1, str(v).casefold(), r[0])
        none = [r for r in rows if k(r)[0] == 2]
        some = sorted((r for r in rows if k(r)[0] != 2), key=k, reverse=desc)
        return some + none  # missing values last in both directions


def looks_like_expr(text: str) -> bool:
    return bool(re.search(r"[\w\])](?::|~/|\s*(?:>=|<=|==|!=|>|<)\s*\d)|^\(|\b(?:AND|OR|NOT|I|LUB|NIE)\b|^~\"|^[a-z]+\[", text))


def fmt(v: Any, col: dict[str, Any]) -> Any:
    """Cell text: datetimes shortened, floats rounded, lists joined; the renderer cuts to width."""
    if v is None:
        return ""
    if isinstance(v, bool):
        return "✓" if v else ""
    if isinstance(v, float):
        return round(v, int(col.get("digits", 2)))
    if isinstance(v, int):
        return v
    if isinstance(v, (list, tuple, set)):
        return ", ".join(str(x) for x in v)
    if isinstance(v, dict):
        return json.dumps(v, ensure_ascii=False, sort_keys=True)
    s = " ".join(str(v).split())
    if col.get("format") == "datetime" and re.match(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}", s):
        return s[:10] + " " + s[11:16]
    n = int(col.get("chars", 0) or 0)
    return s[:n] if n else s


def column_labels(layout: dict[str, Any], kind: str) -> list[dict[str, Any]]:
    cols = (layout.get("columns") or {}).get(kind) or [{"id": "label"}]
    return [{"id": c["id"], "label": _label(c.get("label")) or c["id"], "width": c.get("width", "auto"),
             "align": c.get("align", "left")} for c in cols]


__all__ = ["Views", "ViewError", "parse_id", "resolve_refs", "compute", "fmt", "looks_like_expr", "column_labels", "ITEM_KINDS"]
