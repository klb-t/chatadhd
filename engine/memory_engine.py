
from dataclasses import dataclass, field
from typing import Optional, Dict, Any, List, Set, Iterable
from datetime import datetime
import uuid, logging, math

log = logging.getLogger('memory_engine')

@dataclass
class Node:
    id: str
    parent: Optional[str]
    content: str
    node_type: str = 'text'  # text/audio/image/file
    active: bool = True
    depth: int = 0
    order: int = 0
    metadata: Dict[str,Any] = field(default_factory=dict)
    embedding: Optional[List[float]] = None
    created: str = field(default_factory=lambda: datetime.utcnow().isoformat())
    links: Set[str] = field(default_factory=set)

class MemoryEngine:
    def __init__(self, db=None):
        self.db = db
        self.nodes: Dict[str, Node] = {}
        self.roots: List[str] = []
        if self.db:
            self.load_from_db()

    def load_from_db(self):
        for n in self.db.get_all_nodes():
            node = Node(id=n['id'], parent=n['parent'], content=n['content'], node_type=n['node_type'],
                        active=n['active'], depth=n['depth'], order=n['order'], metadata=n.get('metadata') or {},
                        embedding=n.get('embedding'), created=n.get('created'))
            self.nodes[node.id] = node
            if node.parent is None:
                self.roots.append(node.id)
        for l in (self.db.get_links() if hasattr(self.db,'get_links') else []):
            src,lst = l['src'], l['dst']
            if src in self.nodes:
                self.nodes[src].links.add(lst)

    def add_node(self, content: str, parent: Optional[str]=None, node_type: str='text', metadata: Dict=None) -> Node:
        nid = 'n_' + uuid.uuid4().hex[:12]
        depth = 0
        if parent and parent in self.nodes:
            depth = self.nodes[parent].depth + 1
        order = self._next_order(parent)
        node = Node(id=nid, parent=parent, content=content, node_type=node_type, active=True,
                    depth=depth, order=order, metadata=metadata or {})
        self.nodes[nid] = node
        if parent is None:
            self.roots.append(nid)
        if self.db:
            self.db.insert_node(node.id, node.parent, node.content, node.node_type, node.active,
                                node.depth, node.order, node.metadata, None, node.created)
        return node

    def _next_order(self, parent):
        return max([n.order for n in self.nodes.values() if n.parent==parent], default=-1) + 1

    def add_link(self, src: str, dst: str, rel: str='related'):
        if src in self.nodes and dst in self.nodes:
            self.nodes[src].links.add(dst)
            if self.db:
                self.db.insert_link(src,dst,rel)

    def get_children(self, parent: Optional[str]=None) -> List[Node]:
        return sorted([n for n in self.nodes.values() if n.parent==parent], key=lambda x: x.order)

    def find(self, q: str) -> List[Node]:
        ql = q.lower()
        return [n for n in self.nodes.values() if ql in (n.content or '').lower()]

    def delete(self, nid: str, recursive: bool=True):
        if nid not in self.nodes: return False
        if recursive:
            for c in list(self.get_children(nid)):
                self.delete(c.id, True)
        parent = self.nodes[nid].parent
        if parent is None and nid in self.roots: self.roots.remove(nid)
        del self.nodes[nid]
        if self.db:
            self.db.delete_node(nid)
        return True

    def to_text(self, active_only: bool=True) -> str:
        lines = []
        def walk(parent, indent=0):
            for n in sorted([x for x in self.nodes.values() if x.parent==parent], key=lambda z: z.order):
                if active_only and not n.active: continue
                icon = '📁' if n.node_type=='folder' else '📎' if n.node_type=='file' else '•'
                lines.append(('  '*indent) + f"{icon} {n.content}")
                walk(n.id, indent+1)
        walk(None,0)
        return '\n'.join(lines)

    def all_nodes(self) -> Iterable[Node]:
        return list(self.nodes.values())
