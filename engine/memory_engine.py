"""Hierarchical Memory Engine with metadata and attachments"""
import json
from pathlib import Path
from datetime import datetime
from dataclasses import dataclass, field, asdict
from typing import Optional, Dict, List, Any
import uuid

def gen_id():
    return uuid.uuid4().hex[:12]

@dataclass
class MemoryNode:
    id: str
    content: str
    parent_id: Optional[str] = None
    node_type: str = 'text'  # text, folder, file, dir
    active: bool = True
    depth: int = 0
    weight: float = 1.0
    created: str = ''
    metadata: Dict[str, Any] = field(default_factory=dict)
    
    def __post_init__(self):
        if not self.created:
            self.created = datetime.now().isoformat()


class MemoryEngine:
    def __init__(self, path):
        self.path = Path(path)
        self._cache: Dict[str, MemoryNode] = {}
        self._load()
    
    def _load(self):
        if self.path.exists():
            try:
                data = json.loads(self.path.read_text())
                known_fields = {'id', 'content', 'parent_id', 'node_type', 'active', 
                               'depth', 'weight', 'created', 'metadata'}
                for item in data:
                    filtered = {k: v for k, v in item.items() if k in known_fields}
                    self._cache[item['id']] = MemoryNode(**filtered)
            except Exception as e:
                print(f"Memory load error: {e}")
    
    def _save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        data = [asdict(n) for n in self._cache.values()]
        self.path.write_text(json.dumps(data, indent=2, ensure_ascii=False))
    
    def add_node(self, content, parent_id=None, node_type='text', metadata=None):
        nid = gen_id()
        depth = 0
        if parent_id and parent_id in self._cache:
            depth = self._cache[parent_id].depth + 1
        
        node = MemoryNode(
            id=nid, content=content, parent_id=parent_id,
            node_type=node_type, depth=depth, metadata=metadata or {}
        )
        self._cache[nid] = node
        self._save()
        return nid
    
    def update_node(self, nid, **kwargs):
        if nid in self._cache:
            node = self._cache[nid]
            for k, v in kwargs.items():
                if hasattr(node, k):
                    setattr(node, k, v)
            self._save()
    
    def delete_node(self, nid, recursive=False):
        if nid not in self._cache:
            return
        
        if recursive:
            children = [n.id for n in self._cache.values() if n.parent_id == nid]
            for cid in children:
                self.delete_node(cid, True)
        
        del self._cache[nid]
        self._save()
    
    def get_node(self, nid):
        return self._cache.get(nid)
    
    def get_children(self, parent_id=None):
        return sorted(
            [n for n in self._cache.values() if n.parent_id == parent_id],
            key=lambda n: (n.node_type != 'folder', n.created)
        )
    
    def get_all(self):
        return list(self._cache.values())
    
    def get_active_context(self, max_tokens=4000):
        """Build context from active nodes for LLM."""
        lines = []
        char_count = 0
        max_chars = max_tokens * 4
        
        def add_node(node, indent=0):
            nonlocal char_count
            if not node.active or char_count > max_chars:
                return
            
            prefix = "  " * indent
            weight_marker = f"[w:{node.weight}]" if node.weight != 1.0 else ""
            
            if node.node_type == 'folder':
                lines.append(f"{prefix}[{node.content}] {weight_marker}")
            elif node.node_type == 'file':
                path = node.metadata.get('path', '')
                lines.append(f"{prefix}FILE: {node.content} ({path}) {weight_marker}")
            elif node.node_type == 'dir':
                path = node.metadata.get('path', '')
                lines.append(f"{prefix}DIR: {node.content} ({path}) {weight_marker}")
            else:
                lines.append(f"{prefix}{node.content} {weight_marker}")
            
            char_count += len(lines[-1])
            
            for child in self.get_children(node.id):
                add_node(child, indent + 1)
        
        for root in self.get_children(None):
            add_node(root)
        
        return "\n".join(lines)
    
    def search(self, query, limit=10):
        """Simple text search."""
        query = query.lower()
        results = []
        for node in self._cache.values():
            if query in node.content.lower():
                results.append(node)
                if len(results) >= limit:
                    break
        return results
    
    def get_graph_data(self):
        """Get nodes and edges for graph visualization."""
        nodes = []
        edges = []
        
        for n in self._cache.values():
            nodes.append({
                'id': n.id,
                'label': n.content[:20],
                'type': 'memory',
                'node_type': n.node_type,
                'active': n.active,
                'weight': n.weight,
            })
            if n.parent_id:
                edges.append({
                    'src': n.parent_id,
                    'dst': n.id,
                    'type': 'child',
                    'weight': 1.0,
                })
        
        return {'nodes': nodes, 'edges': edges}
