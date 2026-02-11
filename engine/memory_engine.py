"""Memory/knowledge graph engine"""
import uuid
import logging
from datetime import datetime
from dataclasses import dataclass, field
from typing import Optional, List, Dict, Any

log = logging.getLogger('memory')

@dataclass
class MemoryNode:
    id: str
    content: str
    node_type: str = 'text'
    parent: Optional[str] = None
    active: bool = True
    depth: int = 0
    order: int = 0
    metadata: Dict[str, Any] = field(default_factory=dict)
    created: str = field(default_factory=lambda: datetime.now().isoformat())

class MemoryEngine:
    def __init__(self, db):
        self.db = db
        self._cache = {}
        self._load()
    
    def _load(self):
        self._cache.clear()
        for row in self.db.get_all_nodes():
            self._cache[row['id']] = MemoryNode(**row)
        log.info(f"Loaded {len(self._cache)} nodes")
    
    def get_children(self, parent_id: Optional[str]) -> List[MemoryNode]:
        return sorted(
            [n for n in self._cache.values() if n.parent == parent_id],
            key=lambda n: n.order
        )
    
    def add_node(self, content: str, parent_id: Optional[str] = None, 
                 node_type: str = 'text', metadata: Dict = None) -> MemoryNode:
        siblings = self.get_children(parent_id)
        parent_depth = self._cache[parent_id].depth if parent_id else -1
        
        node = MemoryNode(
            id=str(uuid.uuid4()),
            content=content,
            node_type=node_type,
            parent=parent_id,
            depth=parent_depth + 1,
            order=len(siblings),
            metadata=metadata or {}
        )
        
        self.db.insert_node(
            node.id, node.parent, node.content, node.node_type,
            node.active, node.depth, node.order, node.metadata
        )
        self._cache[node.id] = node
        return node
    
    def update_node(self, node_id: str, **kwargs):
        if node_id not in self._cache:
            return
        node = self._cache[node_id]
        for k, v in kwargs.items():
            if hasattr(node, k):
                setattr(node, k, v)
        self.db.update_node(node_id, **kwargs)
    
    def delete_node(self, node_id: str, recursive: bool = False):
        if node_id not in self._cache:
            return
        
        if recursive:
            for child in self.get_children(node_id):
                self.delete_node(child.id, True)
        
        self.db.delete_node(node_id)
        del self._cache[node_id]
    
    def get_active_context(self, limit: int = 10) -> str:
        """Get active memory nodes as context string"""
        active = [n for n in self._cache.values() if n.active]
        active.sort(key=lambda n: n.created, reverse=True)
        
        if not active:
            return ""
        
        lines = ["<memory>"]
        for node in active[:limit]:
            lines.append(f"- {node.content}")
        lines.append("</memory>")
        
        return "\n".join(lines)
