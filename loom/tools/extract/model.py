"""The unified unit model: archive → conversation → branch → message → block → attachment.

One intermediate representation for every source, a superset of what passes
through ``walk_tree`` in ``loom/src/archive/ingest_parse.cpp`` and compatible
in fields with ``loom.export.v1``.  The raw source object stays reachable by
``raw_pointer`` (and ``raw`` when kept), so a 1:1 cut never depends on this
interpretation.  ``uid`` is a hash of the content (an exact duplicate in another
archive has the same one); ``identity_key`` = (service, ext_id) follows a
conversation into a newer export, where new messages give it a new ``uid`` —
manual decisions are matched by both (decisions.py).
"""
import hashlib
import json
import re
from dataclasses import dataclass, field
from typing import Any, Iterator, Optional

from .timestamps import key as _tskey

SCHEMA_UNIT = "loom.extract.unit/1"

UNIT_KINDS = ("conversation", "project", "memory", "account", "feedback", "shared_link",
              "artifact", "member", "session", "graph", "unknown")


def canonical_json(obj: Any) -> str:
    return json.dumps(obj, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


_LONE_SURROGATE = re.compile(r"[\ud800-\udfff]")


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8", errors="surrogatepass")).hexdigest()


def sanitize_text(text: str) -> tuple[str, int]:
    """Replace lone surrogates (invalid UTF-8 in the source) with U+FFFD; returns (text, count)."""
    if not text:
        return text, 0
    n = len(_LONE_SURROGATE.findall(text))
    return (_LONE_SURROGATE.sub("�", text), n) if n else (text, 0)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def short_uid(hexdigest: str) -> str:
    return "sha256:" + hexdigest[:32]


@dataclass
class Block:
    kind: str
    text: str = ""
    native: Optional[str] = None
    name: Optional[str] = None          # tool name (tool_use / tool_result)
    input: Any = None                   # tool input / result payload
    ref: Optional[str] = None           # attachment reference
    mime: Optional[str] = None
    complete: Optional[bool] = None     # thinking: full or summary
    example: Any = None                 # unknown: the native object (trimmed)
    extra: dict[str, Any] = field(default_factory=dict)

    def chars(self) -> int:
        return len(self.text or "")

    def as_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {"kind": self.kind}
        if self.text:
            d["text"] = self.text
        for k in ("native", "name", "ref", "mime"):
            v = getattr(self, k)
            if v is not None:
                d[k] = v
        if self.input is not None:
            d["input"] = self.input
        if self.complete is not None:
            d["complete"] = self.complete
        if self.example is not None:
            d["example"] = self.example
        if self.extra:
            d["extra"] = self.extra
        return d

    def hash_view(self) -> dict[str, Any]:
        return {"kind": self.kind, "text": self.text, "name": self.name, "ref": self.ref,
                "input": self.input if isinstance(self.input, (str, int, float)) else
                (canonical_json(self.input) if self.input is not None else None)}


@dataclass
class Attachment:
    ref: str
    name: Optional[str] = None
    mime: Optional[str] = None
    size: Optional[int] = None
    status: str = "reference"           # resolved | missing | reference
    member: Optional[str] = None        # archive member that resolves it
    sha256: Optional[str] = None
    message: Optional[str] = None       # message id that references it

    def as_dict(self) -> dict[str, Any]:
        return {k: v for k, v in self.__dict__.items() if v is not None}


@dataclass
class Message:
    id: str
    parent: Optional[str]
    role: str
    created: Any = None                 # native value (untouched)
    created_utc: Optional[str] = None   # ISO Z
    created_how: str = "none"
    created_inferred: bool = False
    branch: str = ""
    on_current_path: bool = True
    blocks: list[Block] = field(default_factory=list)
    raw_pointer: Optional[str] = None
    author: Optional[str] = None
    model: Optional[str] = None
    meta: dict[str, Any] = field(default_factory=dict)

    def text(self, kinds: tuple[str, ...] = ("text",)) -> str:
        return "\n".join(b.text for b in self.blocks if b.kind in kinds and b.text)

    def all_text(self) -> str:
        return "\n".join(b.text for b in self.blocks if b.text)

    def has_kind(self, kind: str) -> bool:
        return any(b.kind == kind for b in self.blocks)

    def count_kind(self, kind: str) -> int:
        return sum(1 for b in self.blocks if b.kind == kind)

    def tool_names(self) -> list[str]:
        return [b.name for b in self.blocks if b.kind in ("tool_use", "tool_result") and b.name]

    def chars(self) -> int:
        return sum(b.chars() for b in self.blocks)

    def as_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {"id": self.id, "parent": self.parent, "role": self.role,
                             "created": self.created, "created_utc": self.created_utc,
                             "created_how": self.created_how}
        if self.created_inferred:
            d["created_inferred"] = True
        d["branch"] = self.branch
        d["on_current_path"] = self.on_current_path
        if self.author:
            d["author"] = self.author
        if self.model:
            d["model"] = self.model
        d["blocks"] = [b.as_dict() for b in self.blocks]
        if self.raw_pointer:
            d["raw_pointer"] = self.raw_pointer
        if self.meta:
            d["meta"] = self.meta
        return d

    def hash_view(self) -> dict[str, Any]:
        return {"id": self.id, "parent": self.parent, "role": self.role,
                "created": self.created if isinstance(self.created, (str, int, float)) else None,
                "blocks": [b.hash_view() for b in self.blocks]}


@dataclass
class Unit:
    kind: str
    ext_id: Optional[str] = None
    title: Optional[str] = None
    created: Any = None
    updated: Any = None
    created_utc: Optional[str] = None
    updated_utc: Optional[str] = None
    created_how: str = "none"
    created_inferred: bool = False
    account: dict[str, Any] = field(default_factory=dict)   # service, product, id, label
    source: dict[str, Any] = field(default_factory=dict)    # archive, sha256, member, format, locator
    tree: dict[str, Any] = field(default_factory=dict)      # roots, current_leaf, forks
    messages: list[Message] = field(default_factory=list)
    attachments: list[Attachment] = field(default_factory=list)
    unknown: list[dict[str, Any]] = field(default_factory=list)
    versions: list[dict[str, Any]] = field(default_factory=list)
    duplicates: list[dict[str, Any]] = field(default_factory=list)
    stats: dict[str, Any] = field(default_factory=dict)
    flags: dict[str, Any] = field(default_factory=dict)     # sidechain, compacted, continuation, …
    raw: Any = None                                         # native object (not hashed, optional)
    uid: str = ""
    content_sha256: str = ""

    # -- identity ----------------------------------------------------------------
    def compute_identity(self) -> None:
        view = {"kind": self.kind, "ext_id": self.ext_id, "title": self.title,
                "messages": [m.hash_view() for m in self.messages],
                "attachments": sorted(a.ref for a in self.attachments)}
        self.content_sha256 = sha256_text(canonical_json(view))
        self.uid = short_uid(self.content_sha256)

    @property
    def service(self) -> Optional[str]:
        return self.account.get("service")

    @property
    def identity_key(self) -> tuple[str, str]:
        """(service, ext_id) when an external id exists, else (service, uid)."""
        return (self.service or "", self.ext_id or self.uid)

    # -- tree helpers --------------------------------------------------------------
    def by_id(self) -> dict[str, Message]:
        return {m.id: m for m in self.messages}

    def children(self) -> dict[Optional[str], list[str]]:
        out: dict[Optional[str], list[str]] = {}
        ids = {m.id for m in self.messages}
        for m in self.messages:
            parent = m.parent if m.parent in ids else None
            out.setdefault(parent, []).append(m.id)
        return out

    def current_path(self) -> list[Message]:
        return [m for m in self.messages if m.on_current_path]

    def branches(self) -> list[list[Message]]:
        """Every root→leaf path (document order, deterministic)."""
        kids = self.children()
        by_id = self.by_id()
        out: list[list[Message]] = []
        # iterative depth-first walk (same order as the recursive one): an agent session can be thousands
        # of messages deep, which a recursive walk would not survive
        stack: list[tuple[str, list[Message]]] = [(r, []) for r in reversed(kids.get(None, []))]
        while stack:
            mid, prefix = stack.pop()
            path = prefix + [by_id[mid]]
            ch = kids.get(mid, [])
            if not ch:
                out.append(path)
                continue
            for c in reversed(ch):
                stack.append((c, path))
        return out

    def leaf_count(self) -> int:
        kids = self.children()
        return sum(1 for m in self.messages if not kids.get(m.id))

    def iter_blocks(self) -> Iterator[tuple[Message, Block]]:
        for m in self.messages:
            for b in m.blocks:
                yield m, b

    def text(self) -> str:
        return "\n".join(m.text() for m in self.messages if m.text())

    def tool_names(self) -> dict[str, int]:
        out: dict[str, int] = {}
        for m in self.messages:
            for n in m.tool_names():
                out[n] = out.get(n, 0) + 1
        return out

    def models(self) -> dict[str, int]:
        out: dict[str, int] = {}
        for m in self.messages:
            if m.model:
                out[m.model] = out.get(m.model, 0) + 1
        return out

    def is_agentic(self) -> bool:
        return any(m.role == "tool" or m.has_kind("tool_use") or m.has_kind("tool_result")
                   for m in self.messages)

    def first_last_utc(self) -> tuple[Optional[str], Optional[str]]:
        stamps = sorted((m.created_utc for m in self.messages if m.created_utc), key=_tskey)
        if not stamps:
            return None, None
        return stamps[0], stamps[-1]

    # -- serialisation -----------------------------------------------------------
    def as_dict(self, *, with_raw: bool = False) -> dict[str, Any]:
        d: dict[str, Any] = {
            "schema": SCHEMA_UNIT, "kind": self.kind, "uid": self.uid, "content_sha256": self.content_sha256,
            "ext_id": self.ext_id, "title": self.title,
            "created": self.created, "updated": self.updated,
            "created_utc": self.created_utc, "updated_utc": self.updated_utc,
            "created_how": self.created_how, "created_inferred": self.created_inferred,
            "account": self.account, "source": self.source, "tree": self.tree,
            "messages": [m.as_dict() for m in self.messages],
            "attachments": [a.as_dict() for a in self.attachments],
            "unknown": self.unknown, "versions": self.versions, "duplicates": self.duplicates,
            "stats": self.stats, "flags": self.flags,
        }
        if with_raw and self.raw is not None:
            d["raw"] = self.raw
        return d

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Unit":
        u = cls(kind=d.get("kind", "unknown"), ext_id=d.get("ext_id"), title=d.get("title"),
                created=d.get("created"), updated=d.get("updated"), created_utc=d.get("created_utc"),
                updated_utc=d.get("updated_utc"), created_how=d.get("created_how", "none"),
                created_inferred=bool(d.get("created_inferred")), account=d.get("account") or {},
                source=d.get("source") or {}, tree=d.get("tree") or {}, unknown=d.get("unknown") or [],
                versions=d.get("versions") or [], duplicates=d.get("duplicates") or [],
                stats=d.get("stats") or {}, flags=d.get("flags") or {}, raw=d.get("raw"))
        for md in d.get("messages") or []:
            blocks = [Block(kind=b.get("kind", "unknown"), text=b.get("text", ""), native=b.get("native"),
                            name=b.get("name"), input=b.get("input"), ref=b.get("ref"), mime=b.get("mime"),
                            complete=b.get("complete"), example=b.get("example"), extra=b.get("extra") or {})
                      for b in md.get("blocks") or []]
            u.messages.append(Message(id=md["id"], parent=md.get("parent"), role=md.get("role", "unknown"),
                                      created=md.get("created"), created_utc=md.get("created_utc"),
                                      created_how=md.get("created_how", "none"),
                                      created_inferred=bool(md.get("created_inferred")),
                                      branch=md.get("branch", ""), on_current_path=md.get("on_current_path", True),
                                      blocks=blocks, raw_pointer=md.get("raw_pointer"), author=md.get("author"),
                                      model=md.get("model"), meta=md.get("meta") or {}))
        for ad in d.get("attachments") or []:
            u.attachments.append(Attachment(**{k: ad.get(k) for k in Attachment.__dataclass_fields__ if k in ad}))
        u.uid = d.get("uid", "")
        u.content_sha256 = d.get("content_sha256", "")
        if not u.uid:
            u.compute_identity()
        return u


def grain_id(unit: Unit, grain: str, *, message: Optional[str] = None, branch: Optional[str] = None,
             window: Optional[int] = None, block: Optional[int] = None) -> str:
    if grain == "conversation":
        return unit.uid
    if grain == "branch":
        return f"{unit.uid}/{branch or ''}"
    if grain == "message":
        return f"{unit.uid}#{message}"
    if grain == "window":
        return f"{unit.uid}#w{window}"
    if grain == "block":
        return f"{unit.uid}#{message}/b{block}"
    raise ValueError(f"unknown grain: {grain!r}")


def json_pointer(*parts: Any) -> str:
    """RFC 6901 pointer from path parts."""
    out = ""
    for p in parts:
        s = str(p).replace("~", "~0").replace("/", "~1")
        out += "/" + s
    return out
