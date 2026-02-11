"""Database with WAL mode and message versioning"""
import sqlite3
import json
from datetime import datetime
from pathlib import Path
import uuid

def gen_id(prefix=''):
    return prefix + uuid.uuid4().hex[:12]

class Database:
    def __init__(self, path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.conn = sqlite3.connect(str(self.path), check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        
        # WAL mode for crash protection
        self.conn.execute("PRAGMA journal_mode=WAL")
        self.conn.execute("PRAGMA synchronous=NORMAL")
        
        self._init_schema()
    
    def _init_schema(self):
        self.conn.executescript("""
            CREATE TABLE IF NOT EXISTS conversations (
                id TEXT PRIMARY KEY,
                title TEXT,
                created TEXT,
                updated TEXT,
                metadata TEXT DEFAULT '{}'
            );
            
            CREATE TABLE IF NOT EXISTS messages (
                id TEXT PRIMARY KEY,
                conv_id TEXT,
                parent_id TEXT,
                role TEXT,
                text TEXT,
                model TEXT,
                status TEXT DEFAULT 'active',
                version_group_id TEXT,
                version_num INTEGER DEFAULT 1,
                weight REAL DEFAULT 1.0,
                attachments TEXT DEFAULT '[]',
                metadata TEXT DEFAULT '{}',
                created TEXT,
                FOREIGN KEY (conv_id) REFERENCES conversations(id)
            );
            
            CREATE TABLE IF NOT EXISTS links (
                id TEXT PRIMARY KEY,
                src TEXT,
                dst TEXT,
                link_type TEXT,
                weight REAL DEFAULT 1.0,
                created TEXT
            );
            
            CREATE INDEX IF NOT EXISTS idx_msg_conv ON messages(conv_id);
            CREATE INDEX IF NOT EXISTS idx_msg_parent ON messages(parent_id);
            CREATE INDEX IF NOT EXISTS idx_msg_version ON messages(version_group_id);
            CREATE INDEX IF NOT EXISTS idx_links_src ON links(src);
            CREATE INDEX IF NOT EXISTS idx_links_dst ON links(dst);
        """)
        self.conn.commit()
    
    # === Conversations ===
    def create_conv(self, title="New Chat"):
        cid = gen_id('c_')
        now = datetime.now().isoformat()
        self.conn.execute(
            "INSERT INTO conversations (id, title, created, updated) VALUES (?, ?, ?, ?)",
            (cid, title, now, now)
        )
        self.conn.commit()
        return {'id': cid, 'title': title, 'created': now, 'updated': now}
    
    def list_convs(self, limit=50):
        rows = self.conn.execute(
            "SELECT * FROM conversations ORDER BY updated DESC LIMIT ?", (limit,)
        ).fetchall()
        return [dict(r) for r in rows]
    
    def get_conv(self, cid):
        row = self.conn.execute("SELECT * FROM conversations WHERE id=?", (cid,)).fetchone()
        return dict(row) if row else None
    
    def update_conv(self, cid, **kwargs):
        kwargs['updated'] = datetime.now().isoformat()
        sets = ', '.join(f"{k}=?" for k in kwargs.keys())
        self.conn.execute(f"UPDATE conversations SET {sets} WHERE id=?", (*kwargs.values(), cid))
        self.conn.commit()
    
    def delete_conv(self, cid):
        self.conn.execute("DELETE FROM messages WHERE conv_id=?", (cid,))
        self.conn.execute("DELETE FROM conversations WHERE id=?", (cid,))
        self.conn.commit()
    
    # === Messages (with versioning) ===
    def create_msg(self, conv_id, text, role, model=None, parent_id=None, 
                   attachments=None, version_group_id=None, weight=1.0, metadata=None):
        """Create message. If version_group_id provided, creates new version."""
        mid = gen_id('m_')
        now = datetime.now().isoformat()
        
        if not version_group_id:
            version_group_id = gen_id('vg_')
            version_num = 1
        else:
            # Get next version number
            row = self.conn.execute(
                "SELECT MAX(version_num) FROM messages WHERE version_group_id=?",
                (version_group_id,)
            ).fetchone()
            version_num = (row[0] or 0) + 1
            
            # Mark old versions as inactive
            self.conn.execute(
                "UPDATE messages SET status='version' WHERE version_group_id=? AND status='active'",
                (version_group_id,)
            )
        
        self.conn.execute("""
            INSERT INTO messages 
            (id, conv_id, parent_id, role, text, model, status, version_group_id, 
             version_num, weight, attachments, metadata, created)
            VALUES (?, ?, ?, ?, ?, ?, 'active', ?, ?, ?, ?, ?, ?)
        """, (mid, conv_id, parent_id, role, text, model, version_group_id,
              version_num, weight, json.dumps(attachments or []), 
              json.dumps(metadata or {}), now))
        
        self.update_conv(conv_id)
        self.conn.commit()
        return mid
    
    def get_msgs(self, conv_id, include_all=False):
        """Get messages. include_all=True shows excluded and version history."""
        if include_all:
            where = "conv_id=?"
        else:
            where = "conv_id=? AND status='active'"
        
        rows = self.conn.execute(
            f"SELECT * FROM messages WHERE {where} ORDER BY created", (conv_id,)
        ).fetchall()
        
        result = []
        for r in rows:
            m = dict(r)
            m['attachments'] = json.loads(m['attachments'] or '[]')
            m['metadata'] = json.loads(m['metadata'] or '{}')
            result.append(m)
        return result
    
    def get_msg(self, mid):
        row = self.conn.execute("SELECT * FROM messages WHERE id=?", (mid,)).fetchone()
        if row:
            m = dict(row)
            m['attachments'] = json.loads(m['attachments'] or '[]')
            m['metadata'] = json.loads(m['metadata'] or '{}')
            return m
        return None
    
    def get_versions(self, version_group_id):
        """Get all versions of a message."""
        rows = self.conn.execute(
            "SELECT * FROM messages WHERE version_group_id=? ORDER BY version_num",
            (version_group_id,)
        ).fetchall()
        return [dict(r) for r in rows]
    
    def set_msg_status(self, mid, status):
        self.conn.execute("UPDATE messages SET status=? WHERE id=?", (status, mid))
        self.conn.commit()
    
    def update_msg(self, mid, **kwargs):
        """Update message metadata (not text - use versioning for that)."""
        if 'attachments' in kwargs:
            kwargs['attachments'] = json.dumps(kwargs['attachments'])
        if 'metadata' in kwargs:
            kwargs['metadata'] = json.dumps(kwargs['metadata'])
        
        sets = ', '.join(f"{k}=?" for k in kwargs.keys())
        self.conn.execute(f"UPDATE messages SET {sets} WHERE id=?", (*kwargs.values(), mid))
        self.conn.commit()
    
    def edit_msg(self, mid, new_text):
        """Edit message by creating new version."""
        old = self.get_msg(mid)
        if not old:
            return None
        
        return self.create_msg(
            old['conv_id'], new_text, old['role'], old['model'],
            old['parent_id'], old.get('attachments'), old['version_group_id'],
            old.get('weight', 1.0), old.get('metadata')
        )
    
    def restore_version(self, mid):
        """Restore specific version as active."""
        msg = self.get_msg(mid)
        if not msg:
            return False
        
        # Deactivate current active
        self.conn.execute(
            "UPDATE messages SET status='version' WHERE version_group_id=? AND status='active'",
            (msg['version_group_id'],)
        )
        # Activate this one
        self.conn.execute("UPDATE messages SET status='active' WHERE id=?", (mid,))
        self.conn.commit()
        return True
    
    # === Links (for graph) ===
    def create_link(self, src, dst, link_type='related', weight=1.0):
        lid = gen_id('l_')
        now = datetime.now().isoformat()
        self.conn.execute(
            "INSERT INTO links (id, src, dst, link_type, weight, created) VALUES (?, ?, ?, ?, ?, ?)",
            (lid, src, dst, link_type, weight, now)
        )
        self.conn.commit()
        return lid
    
    def get_links(self, node_id=None):
        if node_id:
            rows = self.conn.execute(
                "SELECT * FROM links WHERE src=? OR dst=?", (node_id, node_id)
            ).fetchall()
        else:
            rows = self.conn.execute("SELECT * FROM links").fetchall()
        return [dict(r) for r in rows]
    
    def delete_link(self, lid):
        self.conn.execute("DELETE FROM links WHERE id=?", (lid,))
        self.conn.commit()
    
    def get_graph_data(self, conv_id=None):
        """Get nodes and edges for graph visualization."""
        nodes = []
        edges = []
        
        if conv_id:
            msgs = self.get_msgs(conv_id, include_all=True)
            for m in msgs:
                nodes.append({
                    'id': m['id'],
                    'label': m['text'][:20],
                    'type': 'chat',
                    'role': m['role'],
                    'status': m['status'],
                    'weight': m.get('weight', 1.0),
                    'version_group': m.get('version_group_id'),
                })
                if m.get('parent_id'):
                    edges.append({
                        'src': m['parent_id'],
                        'dst': m['id'],
                        'type': 'reply',
                        'weight': 1.0,
                    })
        
        # Add links
        for link in self.get_links():
            edges.append({
                'src': link['src'],
                'dst': link['dst'],
                'type': link['link_type'],
                'weight': link['weight'],
            })
        
        return {'nodes': nodes, 'edges': edges}
