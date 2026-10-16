"""The selection language (one grammar for CLI, commander, API, edit scopes and
view filters).  Vocabulary from ``data/grammar/select.json`` (aliases,
operators) and ``data/select_fields.json`` (fields per node kind, has/is
features, count kinds, structural patterns).

    field:value      field~/regex/flags     field:a..b     field OP n
    has:x   is:x   count(kind) OP n   len/words/depth/forks OP n
    ~"text"[@thr]   like:UID[@thr]   near:UID±N   around:DATE±N[d|h]
    week-of:"term"  gap:>Nd   burst:   pattern:NAME   bare words
    AND OR NOT ( )  — Polish aliases (I, LUB, NIE, tytuł:, data:, …)

Every match returns ``reasons``: the terms that decided and the values that
matched.  Syntax errors carry a position.
"""
import re
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from typing import Any, Callable, Optional

from .datafiles import DataStore, store as default_store
from .lexical import Lexicon, TextIndex, compile_keyword, match_keyword, lexicon as default_lexicon
from .model import Attachment, Block, Message, Unit
from . import timestamps as ts


class ExprError(ValueError):
    def __init__(self, message: str, pos: int, text: str) -> None:
        super().__init__(f"{message} at position {pos}: {text[:pos]}⟨here⟩{text[pos:]}")
        self.pos = pos
        self.text = text


# ───────────────────────────── items ─────────────────────────────
@dataclass
class Item:
    kind: str
    unit: Unit
    message: Optional[Message] = None
    block: Optional[Block] = None
    attachment: Optional[Attachment] = None
    index: int = 0

    @property
    def id(self) -> str:
        if self.kind == "conversation":
            return self.unit.uid
        if self.kind == "message":
            return f"{self.unit.uid}#{self.message.id}"  # type: ignore[union-attr]
        if self.kind == "block":
            return f"{self.unit.uid}#{self.message.id}/{self.index}"  # type: ignore[union-attr]
        return f"{self.unit.uid}@{self.attachment.ref}"  # type: ignore[union-attr]


def items_of(units: list[Unit], kind: str = "conversation") -> list[Item]:
    out: list[Item] = []
    for u in units:
        if kind == "conversation":
            out.append(Item("conversation", u))
        elif kind == "message":
            for i, m in enumerate(u.messages):
                out.append(Item("message", u, message=m, index=i))
        elif kind == "block":
            for m in u.messages:
                for i, b in enumerate(m.blocks):
                    out.append(Item("block", u, message=m, block=b, index=i))
        elif kind == "attachment":
            for i, a in enumerate(u.attachments):
                out.append(Item("attachment", u, attachment=a, index=i))
        else:
            raise ValueError(f"unknown item kind: {kind!r}")
    return out


# ───────────────────────────── context ─────────────────────────────
class ExprContext:
    """What the evaluator may consult beyond the item itself."""

    def __init__(self, units: list[Unit], ds: Optional[DataStore] = None, lex: Optional[Lexicon] = None) -> None:
        self.ds = ds or default_store()
        self.lex = lex or default_lexicon()
        self.grammar = self.ds.file("grammar/select.json")
        self.fields = self.ds.file("select_fields.json")
        self.units = {u.uid: u for u in units}
        self.bands: dict[str, str] = {}
        self.scores: dict[str, float] = {}
        self.clusters: dict[str, list[str]] = {}
        self.tags: dict[str, list[str]] = {}
        self.labels: dict[str, list[str]] = {}
        self.timeline: Optional[dict[str, Any]] = None
        self.timeline_rows: Optional[list[dict[str, Any]]] = None
        self.vectors_factory: Optional[Callable[[str], Any]] = None
        self._vectors: dict[str, Any] = {}
        self._sim_cache: dict[tuple[str, str], dict[str, float]] = {}
        self._like_cache: dict[tuple[str, str], dict[str, float]] = {}
        self._kw_cache: dict[str, Any] = {}

    def vectors(self, level: str) -> Any:
        if level not in self._vectors:
            if self.vectors_factory is None:
                raise ExprError("vector similarity needs an index (run with vectors enabled)", 0, "")
            self._vectors[level] = self.vectors_factory(level)
        return self._vectors[level]

    def similarity(self, level: str, text: str) -> dict[str, float]:
        key = (level, text)
        if key not in self._sim_cache:
            self._sim_cache[key] = dict(self.vectors(level).query(text))
        return self._sim_cache[key]

    def like(self, level: str, ident: str) -> dict[str, float]:
        key = (level, ident)
        if key not in self._like_cache:
            self._like_cache[key] = dict(self.vectors(level).similar_to(ident))
        return self._like_cache[key]

    def keyword(self, term: str) -> Any:
        if term not in self._kw_cache:
            self._kw_cache[term] = compile_keyword(term, self.lex)
        return self._kw_cache[term]

    def after_gap(self, min_days: float) -> set[str]:
        if not self.timeline:
            return set()
        return {g["before"] for g in self.timeline.get("gaps", []) if g.get("days", 0) > min_days}

    def in_burst(self) -> set[str]:
        if not self.timeline or not self.timeline_rows:
            return set()
        buckets = {b["bucket"] for b in self.timeline.get("bursts", [])}
        unit = self.timeline.get("buckets", {}).get("unit", "week")
        out = set()
        for r in self.timeline_rows:
            if r.get("t"):
                b = ts.to_iso(ts.bucket_start(ts.from_iso(r["t"]), unit))
                if b in buckets:
                    out.add(r["conversation"])
        return out


# ───────────────────────────── AST ─────────────────────────────
@dataclass
class Node:
    op: str                      # and | or | not | term
    children: list["Node"] = field(default_factory=list)
    term: Optional[dict[str, Any]] = None
    text: str = ""

    def __repr__(self) -> str:
        if self.op == "term":
            return self.text
        if self.op == "not":
            return f"NOT {self.children[0]!r}"
        return "(" + f" {self.op.upper()} ".join(repr(c) for c in self.children) + ")"


_TOKEN_RE = re.compile(r"""
    (?P<ws>\s+) |
    (?P<lpar>\() | (?P<rpar>\)) |
    (?P<sim>~"(?:[^"\\]|\\.)*"(?:@[0-9.]+)?|~'(?:[^'\\]|\\.)*'(?:@[0-9.]+)?) |
    (?P<cnt>count\(\s*[\w.-]+\s*\)\s*(?:>=|<=|!=|=|>|<)\s*-?\d+(?:\.\d+)?) |
    (?P<cmp>[\w.ąćęłńóśźżĄĆĘŁŃÓŚŹŻ-]+\s*(?:>=|<=|!=|>|<)\s*-?\d+(?:\.\d+)?(?:[dhmw])?) |
    (?P<term>[\w.ąćęłńóśźżĄĆĘŁŃÓŚŹŻ-]+(?:~/(?:[^/\\]|\\.)*/[a-z]*|:(?:"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|[^\s()]*))) |
    (?P<word>"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|[^\s()]+)
""", re.VERBOSE | re.UNICODE)


def tokenize(text: str, grammar: dict[str, Any]) -> list[tuple[str, str, int]]:
    ops = {}
    for canon, words in (grammar.get("operators") or {}).items():
        for w in words:
            ops[w] = canon
    out: list[tuple[str, str, int]] = []
    pos = 0
    while pos < len(text):
        m = _TOKEN_RE.match(text, pos)
        if not m:
            raise ExprError("unexpected character", pos, text)
        kind = m.lastgroup
        tok = m.group(0)
        if kind == "ws":
            pos = m.end()
            continue
        if kind in ("word", "term", "cmp") and tok in ops:
            out.append((ops[tok], tok, pos))
        elif kind == "word" and tok.strip('"\'') == "" and tok != tok.strip('"\''):
            out.append(("word", tok, pos))
        else:
            out.append((kind or "word", tok, pos))
        pos = m.end()
    return out


class Parser:
    def __init__(self, text: str, grammar: dict[str, Any]) -> None:
        self.text = text
        self.tokens = tokenize(text, grammar)
        self.i = 0
        self.default_conj = (grammar.get("default_conjunction") or "AND").lower()
        self.aliases = grammar.get("field_aliases") or {}
        self.value_aliases = grammar.get("value_aliases") or {}

    def parse(self) -> Node:
        if not self.tokens:
            raise ExprError("empty expression", 0, self.text)
        node = self._or()
        if self.i < len(self.tokens):
            raise ExprError("unexpected token", self.tokens[self.i][2], self.text)
        return node

    def _peek(self) -> Optional[tuple[str, str, int]]:
        return self.tokens[self.i] if self.i < len(self.tokens) else None

    def _or(self) -> Node:
        left = self._and()
        parts = [left]
        while self._peek() and self._peek()[0] == "OR":
            self.i += 1
            parts.append(self._and())
        return parts[0] if len(parts) == 1 else Node("or", parts)

    def _and(self) -> Node:
        parts = [self._not()]
        while True:
            p = self._peek()
            if p is None or p[0] in ("OR", "rpar"):
                break
            if p[0] == "AND":
                self.i += 1
                parts.append(self._not())
                continue
            # implicit conjunction
            if self.default_conj == "or":
                return Node("or", [parts[0] if len(parts) == 1 else Node("and", parts), self._or()])
            parts.append(self._not())
        return parts[0] if len(parts) == 1 else Node("and", parts)

    def _not(self) -> Node:
        p = self._peek()
        if p and p[0] == "NOT":
            self.i += 1
            return Node("not", [self._not()])
        return self._atom()

    def _atom(self) -> Node:
        p = self._peek()
        if p is None:
            raise ExprError("expected a term", len(self.text), self.text)
        kind, tok, pos = p
        if kind == "lpar":
            self.i += 1
            node = self._or()
            q = self._peek()
            if not q or q[0] != "rpar":
                raise ExprError("missing ')'", q[2] if q else len(self.text), self.text)
            self.i += 1
            return node
        if kind == "rpar":
            raise ExprError("unexpected ')'", pos, self.text)
        self.i += 1
        return Node("term", term=self._term(kind, tok, pos), text=tok)

    # -- terms --------------------------------------------------------------------
    def _field(self, name: str) -> str:
        return self.aliases.get(name, self.aliases.get(name.lower(), name))

    def _unquote(self, v: str) -> str:
        if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
            return re.sub(r"\\(.)", r"\1", v[1:-1])
        return v

    def _term(self, kind: str, tok: str, pos: int) -> dict[str, Any]:
        if kind == "sim":
            m = re.fullmatch(r"~(\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*')(?:@([0-9.]+))?", tok)
            return {"t": "similar", "text": self._unquote(m.group(1)), "thr": float(m.group(2)) if m.group(2) else None, "pos": pos}
        if kind == "cnt":
            m = re.fullmatch(r"count\(\s*([\w.-]+)\s*\)\s*(>=|<=|!=|=|>|<)\s*(-?\d+(?:\.\d+)?)", tok)
            return {"t": "count", "what": m.group(1), "op": m.group(2), "n": float(m.group(3)), "pos": pos}
        if kind == "cmp":
            m = re.fullmatch(r"([\w.ąćęłńóśźżĄĆĘŁŃÓŚŹŻ-]+)\s*(>=|<=|!=|>|<)\s*(-?\d+(?:\.\d+)?)([dhmw])?", tok)
            f = self._field(m.group(1))
            if f == "gap":
                return {"t": "gap", "op": m.group(2), "n": float(m.group(3)), "unit": m.group(4) or "d", "pos": pos}
            return {"t": "cmp", "field": f, "op": m.group(2), "n": float(m.group(3)), "pos": pos}
        if kind == "term":
            m = re.fullmatch(r"([\w.ąćęłńóśźżĄĆĘŁŃÓŚŹŻ-]+)~/((?:[^/\\]|\\.)*)/([a-z]*)", tok)
            if m:
                flags = 0
                for ch in m.group(3):
                    flags |= {"i": re.IGNORECASE, "m": re.MULTILINE, "s": re.DOTALL, "x": re.VERBOSE}.get(ch, 0)
                try:
                    rx = re.compile(m.group(2), flags)
                except re.error as exc:
                    raise ExprError(f"invalid regex: {exc}", pos, self.text) from None
                return {"t": "regex", "field": self._field(m.group(1)), "re": rx, "src": m.group(2), "pos": pos}
            name, value = tok.split(":", 1)
            f = self._field(name)
            value = self._unquote(value)
            if f in ("has", "is", "role"):
                value = (self.value_aliases.get(f) or {}).get(value, value)
            if f == "has":
                return {"t": "has", "what": value, "pos": pos}
            if f == "is":
                return {"t": "is", "what": value, "pos": pos}
            if f == "pattern":
                return {"t": "pattern", "name": value, "pos": pos}
            if f == "like":
                m2 = re.fullmatch(r"(.+?)(?:@([0-9.]+))?", value)
                return {"t": "like", "uid": m2.group(1), "thr": float(m2.group(2)) if m2.group(2) else None, "pos": pos}
            if f == "near":
                m2 = re.fullmatch(r"(.+?)(?:(?:±|\+-|~)(\d+))?", value)
                return {"t": "near", "uid": m2.group(1), "n": int(m2.group(2)) if m2.group(2) else None, "pos": pos}
            if f == "around":
                m2 = re.fullmatch(r"([^±+~]+?)\s*(?:(?:±|\+-|~)\s*(\d+(?:\.\d+)?)([dhmw])?)?", value)
                if not m2:
                    raise ExprError("around: expected DATE±N[d|h]", pos, self.text)
                try:
                    ts.parse_date_bound(m2.group(1))
                except ValueError as exc:
                    raise ExprError(f"around: {exc}", pos, self.text) from None
                return {"t": "around", "date": m2.group(1), "n": m2.group(2), "unit": m2.group(3) or "d", "pos": pos}
            if f == "week-of":
                return {"t": "week_of", "term": value, "pos": pos}
            if f == "gap":
                m2 = re.fullmatch(r">?\s*(\d+(?:\.\d+)?)([dhmw])?", value)
                if not m2:
                    raise ExprError("gap: expected >Nd", pos, self.text)
                return {"t": "gap", "op": ">", "n": float(m2.group(1)), "unit": m2.group(2) or "d", "pos": pos}
            if f == "burst":
                return {"t": "burst", "pos": pos}
            if ".." in value and f not in ("text", "title"):
                lo, hi = value.split("..", 1)
                return {"t": "range", "field": f, "lo": lo or None, "hi": hi or None, "pos": pos}
            return {"t": "eq", "field": f, "value": value, "pos": pos}
        # bare word → text contains
        return {"t": "eq", "field": "text", "value": self._unquote(tok), "bare": True, "pos": pos}


def parse(text: str, ds: Optional[DataStore] = None) -> Node:
    ds = ds or default_store()
    return Parser(text, ds.file("grammar/select.json")).parse()


# ───────────────────────────── field access ─────────────────────────────
def _words(text: str) -> int:
    return len(text.split()) if text else 0


def _depth(u: Unit) -> int:
    kids = u.children()
    best = 0
    stack = [(r, 1) for r in kids.get(None, [])]
    while stack:
        nid, d = stack.pop()
        best = max(best, d)
        for c in kids.get(nid, []):
            stack.append((c, d + 1))
    return best


def field_value(item: Item, name: str, ctx: ExprContext) -> tuple[Any, str]:
    """(value, type) for ``name`` on ``item``; raw.<path> digs into the raw object."""
    u = item.unit
    st = u.stats or {}
    if name.startswith("raw."):
        base: Any = None
        if item.kind == "conversation":
            base = u.raw if u.raw is not None else u.as_dict()
        elif item.kind == "message" and item.message is not None:
            base = item.message.as_dict()
        elif item.kind == "block" and item.block is not None:
            base = item.block.as_dict()
        elif item.attachment is not None:
            base = item.attachment.as_dict()
        from .readers.base import getpath
        return getpath(base, name[4:]), "any"
    ftypes = (ctx.fields.get("fields") or {}).get(item.kind) or {}
    ftype = (ftypes.get(name) or {}).get("type", "any")
    # common
    if name == "uid":
        return item.id, "id"
    if name == "service":
        return u.account.get("service") or "", "enum"
    if name == "account":
        return u.account.get("id") or u.account.get("label") or "", "enum"
    if name == "source":
        return (u.source.get("archive") or ""), "enum"
    if name == "format":
        return u.source.get("format") or "", "enum"
    if name == "title":
        return u.title or "", "text"
    if name == "band":
        return ctx.bands.get(u.uid, ""), "enum"
    if name == "score":
        return ctx.scores.get(u.uid, 0.0), "number"
    if name == "cluster":
        return ctx.clusters.get(u.uid, []), "list"
    if name == "tag":
        return ctx.tags.get(item.id, []) + ctx.tags.get(u.uid, []), "list"
    if name == "label":
        return ctx.labels.get(item.id, []) + ctx.labels.get(u.uid, []), "list"
    if item.kind == "conversation":
        if name == "ext_id":
            return u.ext_id or "", "id"
        if name == "text":
            return u.text(), "text"
        if name == "created":
            return u.created_utc, "date"
        if name == "updated":
            return u.updated_utc, "date"
        if name == "first_message":
            return st.get("first_message_utc"), "date"
        if name == "last_message":
            return st.get("last_message_utc"), "date"
        if name == "lang":
            return list((st.get("lang") or {}).keys()) or [st.get("lang_main", "und")], "list"
        if name == "model":
            return list((st.get("models") or {}).keys()) + ([u.flags["model"]] if u.flags.get("model") else []), "list"
        if name == "tool":
            return list((st.get("tools") or {}).keys()), "list"
        if name == "role":
            return list((st.get("roles") or {}).keys()), "list"
        if name == "kind":
            return u.kind, "enum"
        if name == "messages":
            return len(u.messages), "number"
        if name == "len":
            return st.get("chars", 0), "number"
        if name == "words":
            return _words(u.text()), "number"
        if name == "depth":
            return _depth(u), "number"
        if name == "forks":
            return st.get("forks", 0), "number"
        if name == "branches":
            return st.get("branches", 0), "number"
        if name == "attachments":
            return len(u.attachments), "number"
        if name == "attachments_missing":
            return sum(1 for a in u.attachments if a.status == "missing"), "number"
        if name == "thinking_chars":
            return (st.get("thinking") or {}).get("chars", 0), "number"
        if name == "tokens":
            return st.get("tokens_est", 0), "number"
        if name == "versions":
            return len(u.versions), "number"
        if name == "unknown":
            return len(u.unknown), "number"
    elif item.kind == "message" and item.message is not None:
        m = item.message
        if name == "id":
            return m.id, "id"
        if name == "role":
            return m.role, "enum"
        if name == "text":
            return m.all_text(), "text"
        if name == "created":
            return m.created_utc, "date"
        if name == "model":
            return m.model or u.flags.get("model") or "", "enum"
        if name == "author":
            return m.author or "", "text"
        if name == "branch":
            return m.branch, "enum"
        if name == "tool":
            return m.tool_names(), "list"
        if name == "kind":
            return [b.kind for b in m.blocks], "list"
        if name == "len":
            return m.chars(), "number"
        if name == "words":
            return _words(m.all_text()), "number"
        if name == "index":
            return item.index, "number"
        if name == "attachments":
            return sum(1 for a in u.attachments if a.message == m.id), "number"
        if name == "on_current_path":
            return m.on_current_path, "bool"
        if name == "lang":
            return [ctx.lex.guess_lang([t for t, _, _ in ctx.lex.tokens(m.all_text())]) or "und"], "list"
    elif item.kind == "block" and item.block is not None:
        b = item.block
        if name == "kind":
            return b.kind, "enum"
        if name == "name":
            return b.name or "", "text"
        if name == "text":
            return b.text or "", "text"
        if name == "native":
            return b.native or "", "enum"
        if name == "ref":
            return b.ref or "", "id"
        if name == "mime":
            return b.mime or "", "enum"
        if name == "len":
            return b.chars(), "number"
        if name == "words":
            return _words(b.text), "number"
        if name == "role":
            return item.message.role if item.message else "", "enum"
        if name == "created":
            return item.message.created_utc if item.message else None, "date"
    elif item.kind == "attachment" and item.attachment is not None:
        a = item.attachment
        if name == "ref":
            return a.ref, "id"
        if name == "name":
            return a.name or "", "text"
        if name == "mime":
            return a.mime or "", "enum"
        if name == "size":
            return a.size or 0, "number"
        if name == "status":
            return a.status, "enum"
        if name == "created":
            return u.created_utc, "date"
    if name in ftypes:
        return None, ftype
    raise ExprError(f"unknown field {name!r} for {item.kind}", 0, name)


# ───────────────────────────── evaluation ─────────────────────────────
_OPS = {">=": lambda a, b: a >= b, "<=": lambda a, b: a <= b, ">": lambda a, b: a > b, "<": lambda a, b: a < b,
        "=": lambda a, b: a == b, "!=": lambda a, b: a != b}


def _dur(n: float, unit: str) -> timedelta:
    return timedelta(**{{"d": "days", "h": "hours", "m": "minutes", "w": "weeks"}[unit]: n})


def _date_in(value: Optional[str], lo: Optional[str], hi: Optional[str]) -> bool:
    dt = ts.from_iso(value)
    if dt is None:
        return False
    if lo and dt < ts.parse_date_bound(lo):
        return False
    if hi and dt >= ts.parse_date_bound(hi, end=True):
        return False
    return True


def evaluate(node: Node, item: Item, ctx: ExprContext) -> tuple[bool, list[str]]:
    if node.op == "and":
        reasons: list[str] = []
        for c in node.children:
            ok, r = evaluate(c, item, ctx)
            if not ok:
                return False, []
            reasons.extend(r)
        return True, reasons
    if node.op == "or":
        for c in node.children:
            ok, r = evaluate(c, item, ctx)
            if ok:
                return True, r
        return False, []
    if node.op == "not":
        ok, _ = evaluate(node.children[0], item, ctx)
        return (not ok), ([f"NOT {node.children[0]!r}"] if not ok else [])
    return _eval_term(node.term or {}, node.text, item, ctx)


def _eval_term(t: dict[str, Any], text: str, item: Item, ctx: ExprContext) -> tuple[bool, list[str]]:
    kind = t["t"]
    u = item.unit
    st = u.stats or {}
    if kind == "eq":
        value, ftype = field_value(item, t["field"], ctx)
        want = t["value"]
        if ftype == "text" or (ftype == "any" and isinstance(value, str)):
            if t.get("bare") or t["field"] in ("text", "title"):
                kw = ctx.keyword(want)
                idx = TextIndex.build(value or "", ctx.lex)
                ms = match_keyword(kw, idx)
                if ms:
                    return True, [f"{text} → „{ms[0].form}” ×{len(ms)}"]
                return False, []
            ok = ctx.lex.fold(want) in ctx.lex.fold(value or "")
            return ok, ([f"{text} → „{value[:60]}”"] if ok else [])
        if ftype == "date":
            ok = _date_in(value, want, want)
            return ok, ([f"{text} → {value}"] if ok else [])
        if ftype == "list":
            vals = [str(v).lower() for v in (value or [])]
            ok = want.lower() in vals or any(want.lower() in v for v in vals)
            return ok, ([f"{text} → {sorted(set(vals))[:5]}"] if ok else [])
        if ftype == "number":
            try:
                ok = float(value or 0) == float(want)
            except ValueError:
                raise ExprError(f"{t['field']}: not a number: {want!r}", t.get("pos", 0), text) from None
            return ok, ([f"{text}"] if ok else [])
        if ftype == "bool":
            ok = bool(value) == (want.lower() in ("1", "true", "tak", "yes"))
            return ok, ([text] if ok else [])
        sval = str(value or "")
        ok = sval.lower() == want.lower() or (bool(want) and want.lower() in sval.lower() and ftype == "id")
        return ok, ([f"{text} → {sval[:60]}"] if ok else [])
    if kind == "regex":
        value, _ = field_value(item, t["field"], ctx)
        if isinstance(value, list):
            value = " ".join(str(v) for v in value)
        if value is None:
            return False, []
        m = t["re"].search(str(value))
        return (m is not None), ([f"{text} → „{m.group(0)[:60]}”"] if m else [])
    if kind == "range":
        value, ftype = field_value(item, t["field"], ctx)
        if ftype == "date":
            ok = _date_in(value, t["lo"], t["hi"])
            return ok, ([f"{text} → {value}"] if ok else [])
        try:
            v = float(value or 0)
            lo = float(t["lo"]) if t["lo"] else None
            hi = float(t["hi"]) if t["hi"] else None
        except ValueError:
            raise ExprError("range needs numbers or dates", t.get("pos", 0), text) from None
        ok = (lo is None or v >= lo) and (hi is None or v <= hi)
        return ok, ([f"{text} → {v:g}"] if ok else [])
    if kind == "cmp":
        value, _ = field_value(item, t["field"], ctx)
        try:
            v = float(value or 0)
        except (TypeError, ValueError):
            return False, []
        ok = _OPS[t["op"]](v, t["n"])
        return ok, ([f"{text} → {v:g}"] if ok else [])
    if kind == "count":
        n = _count(item, t["what"])
        ok = _OPS[t["op"]](n, t["n"])
        return ok, ([f"{text} → {n}"] if ok else [])
    if kind == "has":
        ok = _has(item, t["what"])
        return ok, ([text] if ok else [])
    if kind == "is":
        ok = _is(item, t["what"], ctx)
        return ok, ([text] if ok else [])
    if kind == "pattern":
        pats = ctx.fields.get("patterns") or {}
        if t["name"] not in pats:
            raise ExprError(f"unknown pattern {t['name']!r} (known: {', '.join(sorted(pats))})", t.get("pos", 0), text)
        node = Parser(pats[t["name"]]["expr"], ctx.grammar).parse()
        ok, rs = evaluate(node, item, ctx)
        return ok, ([f"{text}: " + "; ".join(rs)] if ok else [])
    if kind == "similar":
        thr = t["thr"] if t["thr"] is not None else float(ctx.grammar.get("similarity_default_threshold", 0.35))
        level = "message" if item.kind in ("message", "block") else "conversation"
        sims = ctx.similarity(level, t["text"])
        ident = item.id if level == "conversation" else f"{u.uid}#{item.message.id}"  # type: ignore[union-attr]
        s = sims.get(ident, 0.0)
        ok = s >= thr
        return ok, ([f"{text} → sim {s:.2f} ({ctx.vectors(level).tier_name})"] if ok else [])
    if kind == "like":
        thr = t["thr"] if t["thr"] is not None else float(ctx.ds.preset("similar.threshold", 0.5))
        level = "message" if "#" in t["uid"] else "conversation"
        sims = ctx.like(level, t["uid"])
        ident = item.id if level == "conversation" else f"{u.uid}#{item.message.id}"  # type: ignore[union-attr]
        s = sims.get(ident, 0.0)
        ok = s >= thr
        return ok, ([f"{text} → sim {s:.2f}"] if ok else [])
    if kind == "near":
        n = t["n"] if t["n"] is not None else int(ctx.grammar.get("near_default", 3))
        uid, _, mid = t["uid"].partition("#")
        if item.kind == "conversation":
            ok = u.uid == uid or u.uid.startswith(uid)
            return ok, ([text] if ok else [])
        if not (u.uid == uid or u.uid.startswith(uid)) or item.message is None:
            return False, []
        if not mid:
            return True, [text]
        ids = [m.id for m in u.messages]
        if mid not in ids:
            return False, []
        d = abs(ids.index(mid) - ids.index(item.message.id))
        ok = d <= n
        return ok, ([f"{text} → distance {d}"] if ok else [])
    if kind == "around":
        n = float(t["n"]) if t["n"] else float(re.match(r"(\d+)", ctx.grammar.get("around_default", "3d")).group(1))
        unit = t["unit"] if t["n"] else re.sub(r"\d+", "", ctx.grammar.get("around_default", "3d")) or "d"
        centre = ts.parse_date_bound(t["date"])
        value, _ = field_value(item, "created", ctx)
        dt = ts.from_iso(value)
        if dt is None or centre is None:
            return False, []
        ok = abs(dt - centre) <= _dur(n, unit)
        return ok, ([f"{text} → {value}"] if ok else [])
    if kind == "week_of":
        if not ctx.timeline_rows:
            raise ExprError("week-of needs a timeline (run with timeline)", t.get("pos", 0), text)
        from .timeline import week_of_term
        weeks = week_of_term(ctx.timeline_rows, ctx.units, t["term"], ctx.lex, ctx.timeline.get("buckets", {}).get("unit", "week") if ctx.timeline else "week")
        value, _ = field_value(item, "created", ctx)
        dt = ts.from_iso(value)
        if dt is None or not weeks:
            return False, []
        b = ts.to_iso(ts.bucket_start(dt, ctx.timeline.get("buckets", {}).get("unit", "week") if ctx.timeline else "week"))
        ok = b in weeks
        return ok, ([f"{text} → {b} (of {len(weeks)})"] if ok else [])
    if kind == "gap":
        days = _dur(t["n"], t["unit"]).total_seconds() / 86400
        ok = u.uid in ctx.after_gap(days)
        return ok, ([text] if ok else [])
    if kind == "burst":
        ok = u.uid in ctx.in_burst()
        return ok, ([text] if ok else [])
    raise ExprError(f"unknown term kind {kind}", t.get("pos", 0), text)


def _count(item: Item, what: str) -> float:
    u = item.unit
    st = u.stats or {}
    if item.kind == "conversation":
        if what == "messages":
            return len(u.messages)
        if what == "branches":
            return st.get("branches", 0)
        if what == "forks":
            return st.get("forks", 0)
        if what == "attachments":
            return len(u.attachments)
        if what == "tools":
            return st.get("tool_calls", 0)
        if what == "versions":
            return len(u.versions)
        if what in ("user", "assistant", "tool", "system"):
            return (st.get("roles") or {}).get(what, 0)
        return (st.get("blocks") or {}).get(what, 0)
    if item.kind == "message" and item.message is not None:
        m = item.message
        if what == "attachments":
            return sum(1 for a in u.attachments if a.message == m.id)
        if what == "tools":
            return len(m.tool_names())
        if what == "messages":
            return 1
        return m.count_kind(what)
    if item.kind == "block" and item.block is not None:
        return 1 if item.block.kind == what else 0
    return 0


def _has(item: Item, what: str) -> bool:
    u = item.unit
    st = u.stats or {}
    msgs = [item.message] if item.message is not None else u.messages
    if what == "attachments":
        return bool(u.attachments) if item.kind == "conversation" else any(a.message == item.message.id for a in u.attachments)  # type: ignore[union-attr]
    if what == "branches":
        return bool(st.get("forks"))
    if what == "thinking":
        return any(m.has_kind("thinking") for m in msgs)
    if what == "tools":
        return any(m.has_kind("tool_use") or m.has_kind("tool_result") for m in msgs)
    if what == "citations":
        return any(m.has_kind("citation") for m in msgs)
    if what == "unknown":
        return bool(u.unknown) or any(m.has_kind("unknown") for m in msgs)
    if what == "text":
        return any(m.has_kind("text") and m.text().strip() for m in msgs)
    if what == "versions":
        return bool(u.versions)
    if what == "duplicates":
        return bool(u.duplicates)
    if what == "errors":
        return any(m.has_kind("error") for m in msgs)
    if what in ("code", "context", "system"):
        return any(m.has_kind(what) for m in msgs)
    return any(m.has_kind(what) for m in msgs)


def _is(item: Item, what: str, ctx: ExprContext) -> bool:
    u = item.unit
    m = item.message
    if what == "agentic":
        return (u.stats or {}).get("agentic", False) if m is None else (m.role == "tool" or m.has_kind("tool_use") or m.has_kind("tool_result"))
    if what == "sidechain":
        return bool(m.meta.get("sidechain")) if m is not None else any(x.meta.get("sidechain") for x in u.messages)
    if what == "compacted":
        return bool(m.meta.get("compacted")) if m is not None else any(x.meta.get("compacted") for x in u.messages)
    if what == "inferred":
        return bool(m.created_inferred) if m is not None else bool(u.created_inferred)
    if what == "duplicate":
        return bool(u.duplicates)
    if what == "version":
        return bool(u.versions) or bool(u.flags.get("superseded_by"))
    if what == "continuation":
        return bool(u.flags.get("continuation_of") or u.flags.get("continuations"))
    if what == "hypothesis":
        return bool(u.flags.get("format_hypothesis"))
    if what == "unreachable":
        return bool(m.meta.get("unreachable")) if m is not None else bool(u.tree.get("unreachable"))
    if what == "current_path":
        return m.on_current_path if m is not None else True
    if what == "deadend":
        kids = u.children()
        if m is not None:
            return not kids.get(m.id) and (m.role == "user" or m.has_kind("error"))
        return any(not kids.get(x.id) and (x.role == "user" or x.has_kind("error")) for x in u.messages)
    raise ExprError(f"unknown is: feature {what!r}", 0, what)


def select(expr: str, items: list[Item], ctx: ExprContext) -> list[tuple[Item, list[str]]]:
    node = Parser(expr, ctx.grammar).parse()
    out = []
    for it in items:
        ok, rs = evaluate(node, it, ctx)
        if ok:
            out.append((it, rs))
    return out


def validate(expr: str, ds: Optional[DataStore] = None) -> Node:
    """Parse only (syntax errors carry a position)."""
    return parse(expr, ds)
