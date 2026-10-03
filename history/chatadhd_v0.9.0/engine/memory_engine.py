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
        self._load()

    # ── Persistence ────────────────────────────────────────────────

    def _load(self) -> None:
        if not self._path.exists():
            return
        try:
            data = json.loads(self._path.read_text(encoding="utf-8"))
            for item in data:
                filtered = {k: v for k, v in item.items() if k in _KNOWN_FIELDS}
                node = MemoryNode(**filtered)
                self._cache[node.id] = node
            log.info("Loaded %d memory nodes from %s", len(self._cache), self._path.name)
        except Exception:
            log.exception("Failed to load memory from %s", self._path)

    def _save(self) -> None:
        self._path.parent.mkdir(parents=True, exist_ok=True)
        data = [asdict(n) for n in self._cache.values()]
        tmp = self._path.with_suffix(".tmp")
        tmp.write_text(
            json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8"
        )
        tmp.replace(self._path)

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
