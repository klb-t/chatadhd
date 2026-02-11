from dataclasses import dataclass, field
from typing import Optional, Dict, Any, List, Set
from datetime import datetime
import uuid, logging

log = logging.getLogger('memory')

@dataclass
class Node:
    id: str
    parent: Optional[str]
    content: str
    node_type: str = 'text'
    active: bool = True
    depth: int = 0
    order: int = 0
    metadata: Dict[str, Any] = field(default_factory=dict)
    embedding: Optional[List[float]] = None
    created: str = field(default_factory=lambda: datetime.utcnow().isoformat())
    links: Set[str] = field(default_factory=set)

class MemoryEngine:
    def __init__(self, db=None):
        self.db = db
        self.nodes: Dict[str, Node] = {}
        self.roots: List[str] = []
        if self.db: self._load()
    
    def _load(self):
        for n in self.db.get_all_nodes():
            node = Node(id=n['id'], parent=n['parent'], content=n['content'], node_type=n['node_type'],
                       active=n['active'], depth=n['depth'], order=n['order'], metadata=n.get('metadata') or {},
                       embedding=n.get('embedding'), created=n.get('created'))
            self.nodes[node.id] = node
            if node.parent is None: self.roots.append(node.id)
        for link in self.db.get_links():
            if link['src'] in self.nodes: self.nodes[link['src']].links.add(link['dst'])
        log.info(f"Loaded {len(self.nodes)} nodes")
    
    def add_node(self, content, parent=None, node_type='text', metadata=None):
        nid = 'n_' + uuid.uuid4().hex[:12]
        depth = self.nodes[parent].depth + 1 if parent and parent in self.nodes else 0
        order = max([n.order for n in self.nodes.values() if n.parent == parent], default=-1) + 1
        node = Node(id=nid, parent=parent, content=content, node_type=node_type, active=True, depth=depth, order=order, metadata=metadata or {})
        self.nodes[nid] = node
        if parent is None: self.roots.append(nid)
        if self.db: self.db.insert_node(node.id, node.parent, node.content, node.node_type, node.active, node.depth, node.order, node.metadata, None, node.created)
        return node
    
    def add_link(self, src, dst, rel='related'):
        if src in self.nodes and dst in self.nodes:
            self.nodes[src].links.add(dst)
            if self.db: self.db.insert_link(src, dst, rel)
    
    def update_node(self, id, **fields):
        if id not in self.nodes: return
        for k, v in fields.items():
            if hasattr(self.nodes[id], k): setattr(self.nodes[id], k, v)
        if self.db: self.db.update_node(id, **fields)
    
    def delete_node(self, id, recursive=True):
        if id not in self.nodes: return False
        if recursive:
            for c in list(self.get_children(id)): self.delete_node(c.id, True)
        if self.nodes[id].parent is None and id in self.roots: self.roots.remove(id)
        del self.nodes[id]
        if self.db: self.db.delete_node(id)
        return True
    
    def get_children(self, parent=None):
        return sorted([n for n in self.nodes.values() if n.parent == parent], key=lambda x: x.order)
    
    def all_nodes(self): return list(self.nodes.values())
    def active_nodes(self): return [n for n in self.nodes.values() if n.active]
