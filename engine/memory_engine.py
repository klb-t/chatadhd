"""
ChatADHD v0.07.01 - Hierarchical Memory Engine

Universal tree structure where every node can hold text, files,
directories, or folders.  Integrates with SemanticAnalyzer for
automatic entity/topic extraction on write.
"""
import json
import logging
import uuid
from dataclasses import asdict, dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

log = logging.getLogger(__name__)


def _gen_id() -> str:
    return uuid.uuid4().hex[:12]


@dataclass
class MemoryNode:
    id: str
    content: str
    parent_id: Optional[str] = None
    node_type: str = "text"        # text | folder | file | dir
    active: bool = True
    depth: int = 0
    weight: float = 1.0
    tags: list[str] = field(default_factory=list)
    created: str = ""
    metadata: dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        if not self.created:
            self.created = datetime.utcnow().isoformat() + "Z"


_KNOWN_FIELDS = frozenset(MemoryNode.__dataclass_fields__.keys())


class MemoryEngine:
    """In-memory tree persisted to a JSON file."""

    def __init__(self, path: Path, semantic_analyzer=None) -> None:
        self._path = Path(path)
        self._cache: dict[str, MemoryNode] = {}
        self._analyzer = semantic_analyzer
        # Unknown record keys (extension bag), by node id; written back after
        # the known fields.
        self._extras: dict[str, dict[str, Any]] = {}
        # File order of records: ("n", node_id) or ("r", raw_json_value) for
        # malformed records kept verbatim.
        self._slots: list[tuple[str, Any]] = []
        # Problems found while loading: [{"index": int|None, "reason": str}]
        self.load_issues: list[dict[str, Any]] = []
        # True when the file could not be parsed as a list: never overwritten.
        self._write_blocked = False
        self._backup_done = False
        self._load()

    # ── Persistence ────────────────────────────────────────────────

    def _issue(self, index: Optional[int], reason: str) -> None:
        self.load_issues.append({"index": index, "reason": reason})
        log.warning("memory load issue in %s (record %s): %s",
                    self._path.name, index, reason)

    def _load(self) -> None:
        if not self._path.exists():
            return
        try:
            data = json.loads(self._path.read_text(encoding="utf-8"))
        except Exception as e:
            log.warning("Failed to parse memory file %s", self._path, exc_info=True)
            self._issue(None, f"unparseable file: {e}")
            self._write_blocked = True
            return
        if not isinstance(data, list):
            self._issue(None, "top-level JSON is not a list")
            self._write_blocked = True
            return
        for idx, item in enumerate(data):
            try:
                if not isinstance(item, dict):
                    raise ValueError("record is not an object")
                filtered = {k: v for k, v in item.items() if k in _KNOWN_FIELDS}
                node = MemoryNode(**filtered)
                if node.id in self._cache:
                    raise ValueError(f"duplicate id {node.id!r}")
            except Exception as e:
                self._issue(idx, f"{type(e).__name__}: {e}")
                self._slots.append(("r", item))
                continue
            self._cache[node.id] = node
            self._slots.append(("n", node.id))
            extra = {k: v for k, v in item.items() if k not in _KNOWN_FIELDS}
            if extra:
                self._extras[node.id] = extra
        log.info("Loaded %d memory nodes from %s (%d issues)",
                 len(self._cache), self._path.name, len(self.load_issues))

    def _backup_corrupt(self) -> bool:
        if self._backup_done:
            return True
        stamp = datetime.utcnow().strftime("%Y%m%dT%H%M%SZ")
        dst = self._path.with_name(self._path.name + f".corrupt-{stamp}")
        try:
            dst.write_bytes(self._path.read_bytes())
        except OSError:
            log.error("Could not back up unreadable memory file %s", self._path,
                      exc_info=True)
            return False
        self._backup_done = True
        log.warning("Backed up unreadable memory file to %s", dst.name)
        return True

    def _save(self) -> None:
        if self._write_blocked:
            self._backup_corrupt()
            log.warning("memory file %s could not be read; NOT overwriting it "
                        "(changes are kept in memory only)", self._path)
            return
        self._path.parent.mkdir(parents=True, exist_ok=True)
        data: list[Any] = []
        seen = set()
        for kind, ref in self._slots:
            if kind == "r":
                data.append(ref)
            elif ref in self._cache:
                seen.add(ref)
                data.append(self._node_dict(ref))
        for nid in self._cache:
            if nid not in seen:
                self._slots.append(("n", nid))
                data.append(self._node_dict(nid))
        self._slots = [s for s in self._slots if s[0] == "r" or s[1] in self._cache]
        tmp = self._path.with_suffix(".tmp")
        tmp.write_text(
            json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8"
        )
        tmp.replace(self._path)

    def _node_dict(self, nid: str) -> dict[str, Any]:
        d = asdict(self._cache[nid])
        for k, v in self._extras.get(nid, {}).items():
            d.setdefault(k, v)
        return d

    # ── CRUD ───────────────────────────────────────────────────────

    def add_node(
        self,
        content: str,
        parent_id: Optional[str] = None,
        node_type: str = "text",
        metadata: Optional[dict] = None,
        tags: Optional[list[str]] = None,
    ) -> str:
        nid = _gen_id()
        depth = 0
        if parent_id and parent_id in self._cache:
            depth = self._cache[parent_id].depth + 1

        # Auto-tag via semantic analysis if available
        auto_tags = []
        if self._analyzer and node_type == "text":
            try:
                auto_tags = self._analyzer.extract_topics(content, threshold=1)
            except Exception:
                log.debug("Semantic tagging failed", exc_info=True)

        node = MemoryNode(
            id=nid,
            content=content,
            parent_id=parent_id,
            node_type=node_type,
            depth=depth,
            metadata=metadata or {},
            tags=list(tags or []) + auto_tags,
        )
        self._cache[nid] = node
        self._save()
        log.debug("Added memory node %s (type=%s, depth=%d)", nid, node_type, depth)
        return nid

    def update_node(self, nid: str, **kwargs: Any) -> None:
        if nid not in self._cache:
            log.warning("update_node: %s not found", nid)
            return
        node = self._cache[nid]
        for k, v in kwargs.items():
            if hasattr(node, k):
                setattr(node, k, v)
        self._save()

    def delete_node(self, nid: str, recursive: bool = False) -> None:
        if nid not in self._cache:
            return
        if recursive:
            children = [n.id for n in self._cache.values() if n.parent_id == nid]
            for cid in children:
                self.delete_node(cid, recursive=True)
        del self._cache[nid]
        self._extras.pop(nid, None)
        self._save()
        log.debug("Deleted memory node %s (recursive=%s)", nid, recursive)

    def get_node(self, nid: str) -> Optional[MemoryNode]:
        return self._cache.get(nid)

    def get_children(self, parent_id: Optional[str] = None) -> list[MemoryNode]:
        return sorted(
            [n for n in self._cache.values() if n.parent_id == parent_id],
            key=lambda n: (n.node_type != "folder", n.created),
        )

    def get_all(self) -> list[MemoryNode]:
        return list(self._cache.values())

    # ── Context building for LLM ──────────────────────────────────

    def get_active_context(self, max_chars: int = 16_000) -> str:
        """
        Serialise active memory nodes into a text block suitable for
        inclusion in the system prompt / context window.
        """
        lines: list[str] = []
        char_count = 0

        def _walk(node: MemoryNode, indent: int = 0) -> None:
            nonlocal char_count
            if not node.active or char_count > max_chars:
                return

            prefix = "  " * indent
            weight_tag = f" [w:{node.weight:.1f}]" if node.weight != 1.0 else ""
            tag_str = f" #{' #'.join(node.tags)}" if node.tags else ""

            if node.node_type == "folder":
                line = f"{prefix}[{node.content}]{weight_tag}{tag_str}"
            elif node.node_type == "file":
                path = node.metadata.get("path", "")
                line = f"{prefix}FILE: {node.content} ({path}){weight_tag}"
            elif node.node_type == "dir":
                path = node.metadata.get("path", "")
                line = f"{prefix}DIR: {node.content} ({path}){weight_tag}"
            else:
                line = f"{prefix}{node.content}{weight_tag}{tag_str}"

            lines.append(line)
            char_count += len(line)

            for child in self.get_children(node.id):
                _walk(child, indent + 1)

        for root in self.get_children(None):
            _walk(root)

        return "\n".join(lines)

    # ── Search ─────────────────────────────────────────────────────

    def search(self, query: str, limit: int = 10) -> list[MemoryNode]:
        """Simple substring search. Use SelectorEngine for semantic search."""
        q = query.lower()
        results = []
        for node in self._cache.values():
            if q in node.content.lower():
                results.append(node)
                if len(results) >= limit:
                    break
        return results

    # ── Graph data ─────────────────────────────────────────────────

    def get_graph_data(self) -> dict[str, list]:
        nodes: list[dict] = []
        edges: list[dict] = []
        for n in self._cache.values():
            nodes.append({
                "id": n.id,
                "label": n.content[:25],
                "type": "memory",
                "node_type": n.node_type,
                "active": n.active,
                "weight": n.weight,
                "tags": n.tags,
            })
            if n.parent_id:
                edges.append({
                    "src": n.parent_id,
                    "dst": n.id,
                    "type": "child",
                    "weight": 1.0,
                })
        return {"nodes": nodes, "edges": edges}
