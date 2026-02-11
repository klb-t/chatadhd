
import sqlite3, json
from pathlib import Path
from typing import Dict, Any, List

DB_PATH = Path(__file__).parent / 'data' / 'client.db'
DB_PATH.parent.mkdir(parents=True, exist_ok=True)

class DB:
    def __init__(self, path: str = None):
        self.conn = sqlite3.connect(str(DB_PATH), check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        self._init()

    def _init(self):
        c = self.conn.cursor()
        c.execute('''CREATE TABLE IF NOT EXISTS nodes (
            id TEXT PRIMARY KEY, parent TEXT, content TEXT, node_type TEXT, active INTEGER,
            depth INTEGER, ord INTEGER, metadata TEXT, embedding BLOB, created TEXT)''')
        c.execute('''CREATE TABLE IF NOT EXISTS links (
            src TEXT, dst TEXT, rel TEXT, PRIMARY KEY (src,dst,rel))''')
        self.conn.commit()

    def insert_node(self, nid, parent, content, node_type, active, depth, ord, metadata, embedding, created):
        self.conn.execute('INSERT OR REPLACE INTO nodes VALUES (?,?,?,?,?,?,?,?,?,?)',
                          (nid, parent, content, node_type, 1 if active else 0, depth, ord, json.dumps(metadata or {}), embedding, created))
        self.conn.commit()

    def get_all_nodes(self) -> List[Dict[str,Any]]:
        cur = self.conn.execute('SELECT * FROM nodes ORDER BY ord')
        out = []
        for r in cur.fetchall():
            out.append({
                'id': r['id'], 'parent': r['parent'], 'content': r['content'], 'node_type': r['node_type'],
                'active': bool(r['active']), 'depth': r['depth'], 'order': r['ord'],
                'metadata': json.loads(r['metadata'] or '{}'), 'embedding': r['embedding'], 'created': r['created']
            })
        return out

    def insert_link(self, src, dst, rel='related'):
        self.conn.execute('INSERT OR REPLACE INTO links VALUES (?,?,?)', (src, dst, rel))
        self.conn.commit()

    def get_links(self):
        cur = self.conn.execute('SELECT * FROM links')
        return [{'src':r['src'],'dst':r['dst'],'rel':r['rel']} for r in cur.fetchall()]

    def delete_node(self, nid):
        self.conn.execute('DELETE FROM nodes WHERE id=?', (nid,))
        self.conn.execute('DELETE FROM links WHERE src=? OR dst=?', (nid, nid))
        self.conn.commit()
