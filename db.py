"""
Unified Memory Database
========================
Wszystko jest Item: wiadomości, załączniki, notatki, foldery.
Linki tworzą graf między dowolnymi elementami.
"""
import sqlite3, json, uuid, logging
from pathlib import Path
from datetime import datetime
from typing import Dict, Any, List, Optional

log = logging.getLogger('db')

class DB:
    def __init__(self, path: Path = None):
        self.path = path or (Path(__file__).parent / 'data' / 'client.db')
        self.path.parent.mkdir(parents=True, exist_ok=True)
        log.info(f"DB: {self.path}")
        self.conn = sqlite3.connect(str(self.path), check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        self._init_schema()
    
    def _init_schema(self):
        c = self.conn.cursor()
        
        # === UNIFIED ITEMS TABLE ===
        # Wszystko: wiadomości, załączniki, notatki, foldery, projekty
        c.execute('''CREATE TABLE IF NOT EXISTS items (
            id TEXT PRIMARY KEY,
            item_type TEXT NOT NULL,      -- message/attachment/note/folder/project/tag
            parent_id TEXT,               -- hierarchia (folder, conversation)
            
            -- Content (nullable - nie wszystko ma tekst)
            content TEXT,                 -- tekst wiadomości, notatki, komentarz
            content_path TEXT,            -- ścieżka do pliku (dla attachments)
            
            -- Metadata
            role TEXT,                    -- user/assistant/system (dla messages)
            model TEXT,                   -- model AI (dla assistant messages)
            status TEXT DEFAULT 'active', -- active/excluded/archived/deleted
            
            -- Timestamps
            created TEXT NOT NULL,
            updated TEXT,
            
            -- Flexible metadata (JSON)
            meta TEXT DEFAULT '{}',       -- confidence, source, mime_type, tags, etc.
            
            -- Embeddings (optional, for semantic search)
            embedding BLOB,
            
            -- Ordering within parent
            sort_order INTEGER DEFAULT 0
        )''')
        
        # === LINKS TABLE (Graf) ===
        # Relacje między dowolnymi items
        c.execute('''CREATE TABLE IF NOT EXISTS links (
            id TEXT PRIMARY KEY,
            source_id TEXT NOT NULL,
            target_id TEXT NOT NULL,
            link_type TEXT DEFAULT 'related',  -- related/reply/attachment/tag/reference
            meta TEXT DEFAULT '{}',
            created TEXT NOT NULL,
            UNIQUE(source_id, target_id, link_type)
        )''')
        
        # === INDICES ===
        c.execute('CREATE INDEX IF NOT EXISTS idx_items_type ON items(item_type)')
        c.execute('CREATE INDEX IF NOT EXISTS idx_items_parent ON items(parent_id)')
        c.execute('CREATE INDEX IF NOT EXISTS idx_items_status ON items(status)')
        c.execute('CREATE INDEX IF NOT EXISTS idx_links_source ON links(source_id)')
        c.execute('CREATE INDEX IF NOT EXISTS idx_links_target ON links(target_id)')
        
        self.conn.commit()
        log.info("Schema OK (unified)")
    
    # === GENERIC ITEM OPERATIONS ===
    
    def create_item(self, item_type: str, content: str = None, parent_id: str = None,
                    content_path: str = None, role: str = None, model: str = None,
                    meta: Dict = None, status: str = 'active') -> Dict:
        id = str(uuid.uuid4())
        now = datetime.now().isoformat()
        
        # Get next sort order
        if parent_id:
            r = self.conn.execute("SELECT MAX(sort_order) FROM items WHERE parent_id=?", (parent_id,)).fetchone()
            sort_order = (r[0] or 0) + 1
        else:
            sort_order = 0
        
        self.conn.execute("""INSERT INTO items 
            (id, item_type, parent_id, content, content_path, role, model, status, created, updated, meta, sort_order)
            VALUES (?,?,?,?,?,?,?,?,?,?,?,?)""",
            (id, item_type, parent_id, content, content_path, role, model, status, now, now, json.dumps(meta or {}), sort_order))
        self.conn.commit()
        
        return {"id": id, "item_type": item_type, "parent_id": parent_id, "content": content,
                "content_path": content_path, "role": role, "model": model, "status": status,
                "created": now, "updated": now, "meta": meta or {}, "sort_order": sort_order}
    
    def get_item(self, id: str) -> Optional[Dict]:
        r = self.conn.execute("SELECT * FROM items WHERE id=?", (id,)).fetchone()
        if not r: return None
        return self._row_to_item(r)
    
    def _row_to_item(self, r) -> Dict:
        return {
            "id": r["id"], "item_type": r["item_type"], "parent_id": r["parent_id"],
            "content": r["content"], "content_path": r["content_path"],
            "role": r["role"], "model": r["model"], "status": r["status"],
            "created": r["created"], "updated": r["updated"],
            "meta": json.loads(r["meta"] or "{}"), "sort_order": r["sort_order"],
            "embedding": r["embedding"]
        }
    
    def update_item(self, id: str, **fields):
        fields["updated"] = datetime.now().isoformat()
        if "meta" in fields:
            fields["meta"] = json.dumps(fields["meta"])
        sets = ", ".join(f"{k}=?" for k in fields.keys())
        self.conn.execute(f"UPDATE items SET {sets} WHERE id=?", (*fields.values(), id))
        self.conn.commit()
    
    def delete_item(self, id: str, recursive: bool = True):
        if recursive:
            for child in self.get_children(id):
                self.delete_item(child["id"], True)
        self.conn.execute("DELETE FROM links WHERE source_id=? OR target_id=?", (id, id))
        self.conn.execute("DELETE FROM items WHERE id=?", (id,))
        self.conn.commit()
    
    def get_children(self, parent_id: str = None, item_type: str = None, 
                     include_inactive: bool = False) -> List[Dict]:
        where = ["parent_id IS ?" if parent_id is None else "parent_id=?"]
        params = [parent_id]
        
        if item_type:
            where.append("item_type=?")
            params.append(item_type)
        if not include_inactive:
            where.append("status='active'")
        
        rows = self.conn.execute(
            f"SELECT * FROM items WHERE {' AND '.join(where)} ORDER BY sort_order, created",
            params
        )
        return [self._row_to_item(r) for r in rows]
    
    def query_items(self, item_type: str = None, status: str = None, 
                    limit: int = 50, order_by: str = "updated DESC") -> List[Dict]:
        where = []
        params = []
        if item_type:
            where.append("item_type=?")
            params.append(item_type)
        if status:
            where.append("status=?")
            params.append(status)
        
        where_clause = f"WHERE {' AND '.join(where)}" if where else ""
        rows = self.conn.execute(
            f"SELECT * FROM items {where_clause} ORDER BY {order_by} LIMIT ?",
            params + [limit]
        )
        return [self._row_to_item(r) for r in rows]
    
    # === LINK OPERATIONS ===
    
    def create_link(self, source_id: str, target_id: str, link_type: str = "related", 
                    meta: Dict = None) -> Dict:
        id = str(uuid.uuid4())
        now = datetime.now().isoformat()
        try:
            self.conn.execute("INSERT INTO links VALUES (?,?,?,?,?,?)",
                (id, source_id, target_id, link_type, json.dumps(meta or {}), now))
            self.conn.commit()
            return {"id": id, "source_id": source_id, "target_id": target_id, 
                    "link_type": link_type, "meta": meta or {}, "created": now}
        except sqlite3.IntegrityError:
            return None  # Link already exists
    
    def get_links_from(self, source_id: str, link_type: str = None) -> List[Dict]:
        if link_type:
            rows = self.conn.execute(
                "SELECT * FROM links WHERE source_id=? AND link_type=?", (source_id, link_type))
        else:
            rows = self.conn.execute("SELECT * FROM links WHERE source_id=?", (source_id,))
        return [{"id": r["id"], "source_id": r["source_id"], "target_id": r["target_id"],
                 "link_type": r["link_type"], "meta": json.loads(r["meta"] or "{}"),
                 "created": r["created"]} for r in rows]
    
    def get_links_to(self, target_id: str, link_type: str = None) -> List[Dict]:
        if link_type:
            rows = self.conn.execute(
                "SELECT * FROM links WHERE target_id=? AND link_type=?", (target_id, link_type))
        else:
            rows = self.conn.execute("SELECT * FROM links WHERE target_id=?", (target_id,))
        return [{"id": r["id"], "source_id": r["source_id"], "target_id": r["target_id"],
                 "link_type": r["link_type"], "meta": json.loads(r["meta"] or "{}"),
                 "created": r["created"]} for r in rows]
    
    def delete_link(self, source_id: str, target_id: str, link_type: str = None):
        if link_type:
            self.conn.execute("DELETE FROM links WHERE source_id=? AND target_id=? AND link_type=?",
                (source_id, target_id, link_type))
        else:
            self.conn.execute("DELETE FROM links WHERE source_id=? AND target_id=?",
                (source_id, target_id))
        self.conn.commit()
    
    # === CONVENIENCE METHODS (backwards compatible) ===
    
    # Conversations (type='project' with role=None)
    def create_conv(self, title: str, model: str, prompt: str = None, preset: str = "default") -> Dict:
        meta = {"system_prompt": prompt, "preset": preset}
        return self.create_item("project", content=title, meta=meta, model=model)
    
    def get_conv(self, id: str) -> Optional[Dict]:
        item = self.get_item(id)
        if item and item["item_type"] == "project":
            # Backwards compatible format
            return {"id": item["id"], "title": item["content"], "model": item["model"],
                    "system_prompt": item["meta"].get("system_prompt"),
                    "preset": item["meta"].get("preset", "default"),
                    "created": item["created"], "updated": item["updated"]}
        return None
    
    def list_convs(self, limit: int = 50) -> List[Dict]:
        items = self.query_items(item_type="project", limit=limit)
        return [{"id": i["id"], "title": i["content"], "model": i["model"],
                 "system_prompt": i["meta"].get("system_prompt"),
                 "preset": i["meta"].get("preset", "default"),
                 "created": i["created"], "updated": i["updated"]} for i in items]
    
    def update_conv(self, id: str, **fields):
        update = {}
        if "title" in fields:
            update["content"] = fields.pop("title")
        if "model" in fields:
            update["model"] = fields.pop("model")
        # Rest goes to meta
        if fields:
            item = self.get_item(id)
            if item:
                meta = item["meta"]
                meta.update(fields)
                update["meta"] = meta
        if update:
            self.update_item(id, **update)
    
    def delete_conv(self, id: str):
        self.delete_item(id, recursive=True)
    
    # Messages
    def create_msg(self, conv_id: str, role: str, text: str, parent_id: str = None,
                   model: str = None, attachments: List = None) -> Dict:
        item = self.create_item("message", content=text, parent_id=conv_id,
                                role=role, model=model, meta={"attachments": attachments or []})
        # Backwards compatible
        return {"id": item["id"], "conv_id": conv_id, "role": role, "text": text,
                "model": model, "status": item["status"], "created": item["created"]}
    
    def get_msgs(self, conv_id: str, include_excluded: bool = False) -> List[Dict]:
        items = self.get_children(conv_id, item_type="message", include_inactive=include_excluded)
        return [{"id": i["id"], "conv_id": conv_id, "role": i["role"], "text": i["content"],
                 "model": i["model"], "status": i["status"], "created": i["created"]} for i in items]
    
    def set_msg_status(self, id: str, status: str):
        self.update_item(id, status=status)
    
    # Memory nodes (backwards compatible)
    def insert_node(self, id: str, parent: str, content: str, node_type: str,
                    active: bool, depth: int, order: int, metadata: Dict,
                    embedding: bytes = None, created: str = None):
        # Map to new schema
        status = "active" if active else "excluded"
        meta = metadata or {}
        meta["depth"] = depth
        
        now = created or datetime.now().isoformat()
        self.conn.execute("""INSERT OR REPLACE INTO items 
            (id, item_type, parent_id, content, status, created, updated, meta, sort_order, embedding)
            VALUES (?,?,?,?,?,?,?,?,?,?)""",
            (id, f"node_{node_type}", parent, content, status, now, now, json.dumps(meta), order, embedding))
        self.conn.commit()
    
    def get_all_nodes(self) -> List[Dict]:
        rows = self.conn.execute(
            "SELECT * FROM items WHERE item_type LIKE 'node_%' ORDER BY sort_order")
        result = []
        for r in rows:
            meta = json.loads(r["meta"] or "{}")
            result.append({
                "id": r["id"], "parent": r["parent_id"], "content": r["content"],
                "node_type": r["item_type"].replace("node_", ""),
                "active": r["status"] == "active",
                "depth": meta.get("depth", 0), "order": r["sort_order"],
                "metadata": meta, "embedding": r["embedding"], "created": r["created"]
            })
        return result
    
    def update_node(self, id: str, **fields):
        update = {}
        if "content" in fields:
            update["content"] = fields.pop("content")
        if "active" in fields:
            update["status"] = "active" if fields.pop("active") else "excluded"
        if "order" in fields:
            update["sort_order"] = fields.pop("order")
        if "node_type" in fields:
            update["item_type"] = f"node_{fields.pop('node_type')}"
        if update:
            self.update_item(id, **update)
    
    def delete_node(self, id: str):
        self.delete_item(id, recursive=True)
    
    def get_links(self) -> List[Dict]:
        rows = self.conn.execute("SELECT * FROM links")
        return [{"src": r["source_id"], "dst": r["target_id"], "rel": r["link_type"]} for r in rows]
    
    def insert_link(self, src: str, dst: str, rel: str = "related"):
        self.create_link(src, dst, rel)
