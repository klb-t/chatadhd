"""Database layer - SQLite with graph memory support"""
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
        c.execute('''CREATE TABLE IF NOT EXISTS conversations (
            id TEXT PRIMARY KEY, title TEXT, created TEXT, updated TEXT,
            model TEXT, system_prompt TEXT, preset TEXT DEFAULT 'default')''')
        c.execute('''CREATE TABLE IF NOT EXISTS messages (
            id TEXT PRIMARY KEY, conv_id TEXT, parent_id TEXT, role TEXT,
            text TEXT, model TEXT, status TEXT DEFAULT 'active',
            created TEXT, attachments TEXT DEFAULT '[]')''')
        c.execute('''CREATE TABLE IF NOT EXISTS nodes (
            id TEXT PRIMARY KEY, parent TEXT, content TEXT, node_type TEXT,
            active INTEGER DEFAULT 1, depth INTEGER DEFAULT 0, ord INTEGER DEFAULT 0,
            metadata TEXT DEFAULT '{}', embedding BLOB, created TEXT)''')
        c.execute('''CREATE TABLE IF NOT EXISTS links (
            src TEXT, dst TEXT, rel TEXT DEFAULT 'related', PRIMARY KEY (src, dst, rel))''')
        self.conn.commit()
        log.info("Schema OK")
    
    # Conversations
    def create_conv(self, title, model, prompt=None, preset="default"):
        id, now = str(uuid.uuid4()), datetime.now().isoformat()
        self.conn.execute("INSERT INTO conversations VALUES (?,?,?,?,?,?,?)",
                         (id, title, now, now, model, prompt, preset))
        self.conn.commit()
        return {"id": id, "title": title, "created": now, "updated": now, "model": model, "system_prompt": prompt, "preset": preset}
    
    def get_conv(self, id):
        r = self.conn.execute("SELECT * FROM conversations WHERE id=?", (id,)).fetchone()
        return dict(r) if r else None
    
    def list_convs(self, limit=50):
        return [dict(r) for r in self.conn.execute("SELECT * FROM conversations ORDER BY updated DESC LIMIT ?", (limit,))]
    
    def update_conv(self, id, **fields):
        fields['updated'] = datetime.now().isoformat()
        sets = ', '.join(f"{k}=?" for k in fields.keys())
        self.conn.execute(f"UPDATE conversations SET {sets} WHERE id=?", (*fields.values(), id))
        self.conn.commit()
    
    def delete_conv(self, id):
        self.conn.execute("DELETE FROM messages WHERE conv_id=?", (id,))
        self.conn.execute("DELETE FROM conversations WHERE id=?", (id,))
        self.conn.commit()
    
    # Messages
    def create_msg(self, conv_id, role, text, parent_id=None, model=None, attachments=None):
        id, now = str(uuid.uuid4()), datetime.now().isoformat()
        self.conn.execute("INSERT INTO messages VALUES (?,?,?,?,?,?,?,?,?)",
                         (id, conv_id, parent_id, role, text, model, 'active', now, json.dumps(attachments or [])))
        self.conn.commit()
        return {"id": id, "conv_id": conv_id, "role": role, "text": text, "status": "active", "created": now}
    
    def get_msgs(self, conv_id, include_excluded=False):
        where = "AND status != 'deleted'" if include_excluded else "AND status = 'active'"
        return [dict(r) for r in self.conn.execute(f"SELECT * FROM messages WHERE conv_id=? {where} ORDER BY created", (conv_id,))]
    
    def set_msg_status(self, id, status):
        self.conn.execute("UPDATE messages SET status=? WHERE id=?", (status, id))
        self.conn.commit()
    
    # Memory Nodes
    def insert_node(self, id, parent, content, node_type, active, depth, order, metadata, embedding=None, created=None):
        created = created or datetime.now().isoformat()
        self.conn.execute("INSERT OR REPLACE INTO nodes VALUES (?,?,?,?,?,?,?,?,?,?)",
                         (id, parent, content, node_type, 1 if active else 0, depth, order, json.dumps(metadata or {}), embedding, created))
        self.conn.commit()
    
    def get_all_nodes(self):
        return [{'id': r['id'], 'parent': r['parent'], 'content': r['content'], 'node_type': r['node_type'],
                 'active': bool(r['active']), 'depth': r['depth'], 'order': r['ord'],
                 'metadata': json.loads(r['metadata'] or '{}'), 'embedding': r['embedding'], 'created': r['created']}
                for r in self.conn.execute("SELECT * FROM nodes ORDER BY depth, ord")]
    
    def update_node(self, id, **fields):
        if 'metadata' in fields: fields['metadata'] = json.dumps(fields['metadata'])
        if 'active' in fields: fields['active'] = 1 if fields['active'] else 0
        if 'order' in fields: fields['ord'] = fields.pop('order')
        sets = ', '.join(f"{k}=?" for k in fields.keys())
        self.conn.execute(f"UPDATE nodes SET {sets} WHERE id=?", (*fields.values(), id))
        self.conn.commit()
    
    def delete_node(self, id):
        self.conn.execute("DELETE FROM nodes WHERE id=?", (id,))
        self.conn.execute("DELETE FROM links WHERE src=? OR dst=?", (id, id))
        self.conn.commit()
    
    # Links
    def insert_link(self, src, dst, rel='related'):
        self.conn.execute("INSERT OR REPLACE INTO links VALUES (?,?,?)", (src, dst, rel))
        self.conn.commit()
    
    def get_links(self):
        return [{'src': r['src'], 'dst': r['dst'], 'rel': r['rel']} for r in self.conn.execute("SELECT * FROM links")]
    
    def delete_link(self, src, dst):
        self.conn.execute("DELETE FROM links WHERE src=? AND dst=?", (src, dst))
        self.conn.commit()
