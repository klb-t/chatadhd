"""
ChatADHD v0.07.01 - Database Layer
Thread-safe SQLite with WAL mode, message versioning, graph nodes/edges.

Design notes:
  - Every write acquires ``_lock`` so the DB is safe from concurrent threads
    (Kivy UI thread + background API thread).
  - WAL journal mode gives crash resilience.
  - Schema migrations are versioned so future upgrades never lose data.
  - ``nodes`` table: first-class graph entities (entities, topics, concepts).
  - ``links`` table: typed, weighted edges between any IDs (messages, nodes, convs).
"""
import json
import logging
import sqlite3
import threading
import uuid
from datetime import datetime
from pathlib import Path
from typing import Any, Optional

log = logging.getLogger(__name__)

_SCHEMA_VERSION = 3  # Bump when you add migrations.


def _gen_id(prefix: str = "") -> str:
    return prefix + uuid.uuid4().hex[:12]


class Database:
    """Persistent storage for conversations, messages, graph nodes & edges."""

    def __init__(self, path: Path) -> None:
        self._path = Path(path)
        self._path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.RLock()

        self._conn = sqlite3.connect(
            str(self._path),
            check_same_thread=False,
            timeout=30,
        )
        self._conn.row_factory = sqlite3.Row
        self._conn.execute("PRAGMA journal_mode=WAL")
        self._conn.execute("PRAGMA synchronous=NORMAL")
        self._conn.execute("PRAGMA foreign_keys=ON")
        self._init_schema()
        self._migrate()
        log.info("Database opened: %s", self._path)

    # ------------------------------------------------------------------
    # Schema
    # ------------------------------------------------------------------
    def _init_schema(self) -> None:
        with self._lock:
            self._conn.executescript("""
                CREATE TABLE IF NOT EXISTS _meta (
                    key   TEXT PRIMARY KEY,
                    value TEXT
                );

                CREATE TABLE IF NOT EXISTS conversations (
                    id       TEXT PRIMARY KEY,
                    title    TEXT NOT NULL,
                    created  TEXT NOT NULL,
                    updated  TEXT NOT NULL,
                    source   TEXT NOT NULL DEFAULT 'user',
                    metadata TEXT NOT NULL DEFAULT '{}'
                );

                CREATE TABLE IF NOT EXISTS messages (
                    id               TEXT PRIMARY KEY,
                    conv_id          TEXT NOT NULL,
                    parent_id        TEXT,
                    role             TEXT NOT NULL,
                    text             TEXT NOT NULL,
                    model            TEXT,
                    status           TEXT NOT NULL DEFAULT 'active',
                    version_group_id TEXT,
                    version_num      INTEGER NOT NULL DEFAULT 1,
                    weight           REAL NOT NULL DEFAULT 1.0,
                    attachments      TEXT NOT NULL DEFAULT '[]',
                    metadata         TEXT NOT NULL DEFAULT '{}',
                    created          TEXT NOT NULL,
                    FOREIGN KEY (conv_id) REFERENCES conversations(id) ON DELETE CASCADE
                );

                /* ── Graph nodes ─────────────────────────────────── */
                CREATE TABLE IF NOT EXISTS nodes (
                    id        TEXT PRIMARY KEY,
                    kind      TEXT NOT NULL DEFAULT 'entity',
                    label     TEXT NOT NULL,
                    content   TEXT NOT NULL DEFAULT '',
                    tags      TEXT NOT NULL DEFAULT '[]',
                    metadata  TEXT NOT NULL DEFAULT '{}',
                    created   TEXT NOT NULL
                );

                /* ── Graph edges ─────────────────────────────────── */
                CREATE TABLE IF NOT EXISTS links (
                    id        TEXT PRIMARY KEY,
                    src       TEXT NOT NULL,
                    dst       TEXT NOT NULL,
                    link_type TEXT NOT NULL DEFAULT 'related',
                    weight    REAL NOT NULL DEFAULT 1.0,
                    metadata  TEXT NOT NULL DEFAULT '{}',
                    created   TEXT NOT NULL
                );

                CREATE INDEX IF NOT EXISTS idx_msg_conv     ON messages(conv_id);
                CREATE INDEX IF NOT EXISTS idx_msg_parent    ON messages(parent_id);
                CREATE INDEX IF NOT EXISTS idx_msg_vgroup    ON messages(version_group_id);
                CREATE INDEX IF NOT EXISTS idx_msg_status    ON messages(conv_id, status);
                CREATE INDEX IF NOT EXISTS idx_nodes_kind    ON nodes(kind);
                CREATE INDEX IF NOT EXISTS idx_links_src     ON links(src);
                CREATE INDEX IF NOT EXISTS idx_links_dst     ON links(dst);
                CREATE INDEX IF NOT EXISTS idx_links_type    ON links(link_type);
            """)
            self._conn.commit()

    def _migrate(self) -> None:
        """Run forward-only migrations."""
        cur_ver = int(self._get_meta("schema_version") or "0")
        if cur_ver < 3:
            with self._lock:
                # Add nodes table if upgrading from v2.
                self._conn.executescript("""
                    CREATE TABLE IF NOT EXISTS nodes (
                        id TEXT PRIMARY KEY, kind TEXT NOT NULL DEFAULT 'entity',
                        label TEXT NOT NULL, content TEXT NOT NULL DEFAULT '',
                        tags TEXT NOT NULL DEFAULT '[]', metadata TEXT NOT NULL DEFAULT '{}',
                        created TEXT NOT NULL
                    );
                    CREATE INDEX IF NOT EXISTS idx_nodes_kind ON nodes(kind);
                    CREATE INDEX IF NOT EXISTS idx_links_type ON links(link_type);
                """)
                # Add source column to conversations if missing.
                try:
                    self._conn.execute("SELECT source FROM conversations LIMIT 1")
                except sqlite3.OperationalError:
                    self._conn.execute(
                        "ALTER TABLE conversations ADD COLUMN source TEXT NOT NULL DEFAULT 'user'"
                    )
                self._conn.commit()
        self._set_meta("schema_version", str(_SCHEMA_VERSION))
        with self._lock:
            self._conn.commit()

    # ------------------------------------------------------------------
    # Meta helpers
    # ------------------------------------------------------------------
    def _set_meta(self, key: str, value: str) -> None:
        self._conn.execute(
            "INSERT OR REPLACE INTO _meta (key, value) VALUES (?, ?)",
            (key, value),
        )

    def _get_meta(self, key: str) -> Optional[str]:
        row = self._conn.execute(
            "SELECT value FROM _meta WHERE key = ?", (key,)
        ).fetchone()
        return row[0] if row else None

    # ------------------------------------------------------------------
    # Conversations
    # ------------------------------------------------------------------
    def create_conv(self, title: str = "New Chat") -> dict[str, Any]:
        cid = _gen_id("c_")
        now = datetime.utcnow().isoformat() + "Z"
        with self._lock:
            self._conn.execute(
                "INSERT INTO conversations (id, title, created, updated) VALUES (?, ?, ?, ?)",
                (cid, title, now, now),
            )
            self._conn.commit()
        return {"id": cid, "title": title, "created": now, "updated": now}

    def list_convs(self, limit: int = 50) -> list[dict[str, Any]]:
        with self._lock:
            rows = self._conn.execute(
                "SELECT * FROM conversations ORDER BY updated DESC LIMIT ?", (limit,)
            ).fetchall()
        return [dict(r) for r in rows]

    def get_conv(self, cid: str) -> Optional[dict[str, Any]]:
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM conversations WHERE id = ?", (cid,)
            ).fetchone()
        return dict(row) if row else None

    def update_conv(self, cid: str, **kwargs: Any) -> None:
        kwargs["updated"] = datetime.utcnow().isoformat() + "Z"
        sets = ", ".join(f"{k} = ?" for k in kwargs)
        with self._lock:
            self._conn.execute(
                f"UPDATE conversations SET {sets} WHERE id = ?",
                (*kwargs.values(), cid),
            )
            self._conn.commit()

    def delete_conv(self, cid: str) -> None:
        with self._lock:
            self._conn.execute("DELETE FROM messages WHERE conv_id = ?", (cid,))
            self._conn.execute("DELETE FROM conversations WHERE id = ?", (cid,))
            self._conn.commit()
        log.info("Deleted conversation %s", cid)

    # ------------------------------------------------------------------
    # Messages
    # ------------------------------------------------------------------
    def create_msg(
        self,
        conv_id: str,
        text: str,
        role: str,
        model: Optional[str] = None,
        parent_id: Optional[str] = None,
        attachments: Optional[list] = None,
        version_group_id: Optional[str] = None,
        weight: float = 1.0,
        metadata: Optional[dict] = None,
    ) -> str:
        mid = _gen_id("m_")
        now = datetime.utcnow().isoformat() + "Z"

        with self._lock:
            if not version_group_id:
                version_group_id = _gen_id("vg_")
                version_num = 1
            else:
                row = self._conn.execute(
                    "SELECT MAX(version_num) FROM messages WHERE version_group_id = ?",
                    (version_group_id,),
                ).fetchone()
                version_num = (row[0] or 0) + 1
                self._conn.execute(
                    "UPDATE messages SET status = 'version' "
                    "WHERE version_group_id = ? AND status = 'active'",
                    (version_group_id,),
                )

            self._conn.execute(
                """INSERT INTO messages
                   (id, conv_id, parent_id, role, text, model, status,
                    version_group_id, version_num, weight, attachments, metadata, created)
                   VALUES (?, ?, ?, ?, ?, ?, 'active', ?, ?, ?, ?, ?, ?)""",
                (
                    mid, conv_id, parent_id, role, text, model,
                    version_group_id, version_num, weight,
                    json.dumps(attachments or []),
                    json.dumps(metadata or {}),
                    now,
                ),
            )
            self.update_conv(conv_id)
            self._conn.commit()
        return mid

    def get_msgs(
        self, conv_id: str, include_all: bool = False
    ) -> list[dict[str, Any]]:
        where = "conv_id = ?" if include_all else "conv_id = ? AND status = 'active'"
        with self._lock:
            rows = self._conn.execute(
                f"SELECT * FROM messages WHERE {where} ORDER BY created",
                (conv_id,),
            ).fetchall()
        result = []
        for r in rows:
            m = dict(r)
            m["attachments"] = json.loads(m["attachments"] or "[]")
            m["metadata"] = json.loads(m["metadata"] or "{}")
            result.append(m)
        return result

    def get_msg(self, mid: str) -> Optional[dict[str, Any]]:
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM messages WHERE id = ?", (mid,)
            ).fetchone()
        if not row:
            return None
        m = dict(row)
        m["attachments"] = json.loads(m["attachments"] or "[]")
        m["metadata"] = json.loads(m["metadata"] or "{}")
        return m

    def get_versions(self, version_group_id: str) -> list[dict[str, Any]]:
        with self._lock:
            rows = self._conn.execute(
                "SELECT * FROM messages WHERE version_group_id = ? ORDER BY version_num",
                (version_group_id,),
            ).fetchall()
        return [dict(r) for r in rows]

    def set_msg_status(self, mid: str, status: str) -> None:
        if status not in ("active", "excluded", "version", "deleted"):
            raise ValueError(f"Invalid status: {status}")
        with self._lock:
            self._conn.execute(
                "UPDATE messages SET status = ? WHERE id = ?", (status, mid)
            )
            self._conn.commit()

    def update_msg(self, mid: str, **kwargs: Any) -> None:
        if "attachments" in kwargs:
            kwargs["attachments"] = json.dumps(kwargs["attachments"])
        if "metadata" in kwargs:
            kwargs["metadata"] = json.dumps(kwargs["metadata"])
        sets = ", ".join(f"{k} = ?" for k in kwargs)
        with self._lock:
            self._conn.execute(
                f"UPDATE messages SET {sets} WHERE id = ?", (*kwargs.values(), mid)
            )
            self._conn.commit()

    def edit_msg(self, mid: str, new_text: str) -> Optional[str]:
        old = self.get_msg(mid)
        if not old:
            return None
        return self.create_msg(
            old["conv_id"], new_text, old["role"], old["model"],
            old["parent_id"], old.get("attachments"),
            old["version_group_id"], old.get("weight", 1.0),
            old.get("metadata"),
        )

    def restore_version(self, mid: str) -> bool:
        msg = self.get_msg(mid)
        if not msg:
            return False
        with self._lock:
            self._conn.execute(
                "UPDATE messages SET status = 'version' "
                "WHERE version_group_id = ? AND status = 'active'",
                (msg["version_group_id"],),
            )
            self._conn.execute(
                "UPDATE messages SET status = 'active' WHERE id = ?", (mid,)
            )
            self._conn.commit()
        return True

    # ------------------------------------------------------------------
    # Links (graph edges)
    # ------------------------------------------------------------------
    def create_link(
        self, src: str, dst: str, link_type: str = "related",
        weight: float = 1.0, metadata: Optional[dict] = None,
    ) -> str:
        lid = _gen_id("l_")
        now = datetime.utcnow().isoformat() + "Z"
        with self._lock:
            # Upsert: don't create duplicate src→dst of same type.
            existing = self._conn.execute(
                "SELECT id FROM links WHERE src=? AND dst=? AND link_type=?",
                (src, dst, link_type),
            ).fetchone()
            if existing:
                self._conn.execute(
                    "UPDATE links SET weight=?, metadata=? WHERE id=?",
                    (weight, json.dumps(metadata or {}), existing[0]),
                )
                self._conn.commit()
                return existing[0]
            self._conn.execute(
                "INSERT INTO links (id, src, dst, link_type, weight, metadata, created) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)",
                (lid, src, dst, link_type, weight, json.dumps(metadata or {}), now),
            )
            self._conn.commit()
        return lid

    def get_links(self, node_id: Optional[str] = None,
                  link_type: Optional[str] = None) -> list[dict[str, Any]]:
        with self._lock:
            if node_id and link_type:
                rows = self._conn.execute(
                    "SELECT * FROM links WHERE (src=? OR dst=?) AND link_type=?",
                    (node_id, node_id, link_type),
                ).fetchall()
            elif node_id:
                rows = self._conn.execute(
                    "SELECT * FROM links WHERE src=? OR dst=?",
                    (node_id, node_id),
                ).fetchall()
            elif link_type:
                rows = self._conn.execute(
                    "SELECT * FROM links WHERE link_type=?", (link_type,),
                ).fetchall()
            else:
                rows = self._conn.execute("SELECT * FROM links").fetchall()
        return [dict(r) for r in rows]

    def delete_link(self, lid: str) -> None:
        with self._lock:
            self._conn.execute("DELETE FROM links WHERE id = ?", (lid,))
            self._conn.commit()

    # ------------------------------------------------------------------
    # Nodes (graph entities: extracted concepts, topics, files, etc.)
    # ------------------------------------------------------------------
    def create_node(
        self, label: str, kind: str = "entity",
        content: str = "", tags: Optional[list[str]] = None,
        metadata: Optional[dict] = None, node_id: Optional[str] = None,
    ) -> str:
        nid = node_id or _gen_id("n_")
        now = datetime.utcnow().isoformat() + "Z"
        with self._lock:
            self._conn.execute(
                "INSERT OR IGNORE INTO nodes (id, kind, label, content, tags, metadata, created) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)",
                (nid, kind, label, content,
                 json.dumps(tags or []), json.dumps(metadata or {}), now),
            )
            self._conn.commit()
        return nid

    def get_node(self, nid: str) -> Optional[dict[str, Any]]:
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM nodes WHERE id=?", (nid,)
            ).fetchone()
        if not row:
            return None
        n = dict(row)
        n["tags"] = json.loads(n["tags"] or "[]")
        n["metadata"] = json.loads(n["metadata"] or "{}")
        return n

    def find_node(self, label: str, kind: Optional[str] = None) -> Optional[dict[str, Any]]:
        """Find node by exact label (optionally filtered by kind)."""
        with self._lock:
            if kind:
                row = self._conn.execute(
                    "SELECT * FROM nodes WHERE label=? AND kind=?", (label, kind),
                ).fetchone()
            else:
                row = self._conn.execute(
                    "SELECT * FROM nodes WHERE label=?", (label,),
                ).fetchone()
        if not row:
            return None
        n = dict(row)
        n["tags"] = json.loads(n["tags"] or "[]")
        n["metadata"] = json.loads(n["metadata"] or "{}")
        return n

    def list_nodes(self, kind: Optional[str] = None,
                   limit: int = 200) -> list[dict[str, Any]]:
        with self._lock:
            if kind:
                rows = self._conn.execute(
                    "SELECT * FROM nodes WHERE kind=? ORDER BY created DESC LIMIT ?",
                    (kind, limit),
                ).fetchall()
            else:
                rows = self._conn.execute(
                    "SELECT * FROM nodes ORDER BY created DESC LIMIT ?", (limit,),
                ).fetchall()
        result = []
        for r in rows:
            n = dict(r)
            n["tags"] = json.loads(n["tags"] or "[]")
            n["metadata"] = json.loads(n["metadata"] or "{}")
            result.append(n)
        return result

    def update_node(self, nid: str, **kwargs: Any) -> None:
        if "tags" in kwargs:
            kwargs["tags"] = json.dumps(kwargs["tags"])
        if "metadata" in kwargs:
            kwargs["metadata"] = json.dumps(kwargs["metadata"])
        sets = ", ".join(f"{k} = ?" for k in kwargs)
        with self._lock:
            self._conn.execute(
                f"UPDATE nodes SET {sets} WHERE id = ?", (*kwargs.values(), nid)
            )
            self._conn.commit()

    def delete_node(self, nid: str) -> None:
        with self._lock:
            self._conn.execute("DELETE FROM links WHERE src=? OR dst=?", (nid, nid))
            self._conn.execute("DELETE FROM nodes WHERE id=?", (nid,))
            self._conn.commit()

    def get_or_create_node(self, label: str, kind: str = "entity",
                           **kwargs: Any) -> str:
        """Return existing node ID or create a new one."""
        existing = self.find_node(label, kind)
        if existing:
            return existing["id"]
        return self.create_node(label, kind, **kwargs)

    # ------------------------------------------------------------------
    # Graph data for visualization
    # ------------------------------------------------------------------
    def get_graph_data(
        self, conv_id: Optional[str] = None
    ) -> dict[str, list]:
        nodes: list[dict] = []
        edges: list[dict] = []
        seen_ids: set[str] = set()

        # Messages as graph nodes.
        if conv_id:
            for m in self.get_msgs(conv_id, include_all=True):
                nodes.append({
                    "id": m["id"],
                    "label": m["text"][:25],
                    "type": m["role"],
                    "kind": "message",
                    "status": m["status"],
                    "weight": m.get("weight", 1.0),
                    "version_group": m.get("version_group_id"),
                })
                seen_ids.add(m["id"])
                if m.get("parent_id"):
                    edges.append({
                        "src": m["parent_id"],
                        "dst": m["id"],
                        "type": "reply",
                        "weight": 1.0,
                    })

        # Explicit graph nodes.
        for n in self.list_nodes():
            if n["id"] not in seen_ids:
                nodes.append({
                    "id": n["id"],
                    "label": n["label"],
                    "type": n["kind"],
                    "kind": n["kind"],
                    "status": "active",
                    "weight": 1.0,
                    "tags": n.get("tags", []),
                })
                seen_ids.add(n["id"])

        # All edges.
        for link in self.get_links():
            edges.append({
                "src": link["src"],
                "dst": link["dst"],
                "type": link["link_type"],
                "weight": link["weight"],
            })

        return {"nodes": nodes, "edges": edges}

    # ------------------------------------------------------------------
    # Housekeeping
    # ------------------------------------------------------------------
    def close(self) -> None:
        with self._lock:
            self._conn.close()
        log.info("Database closed")

    def vacuum(self) -> None:
        """Reclaim space. Run during maintenance windows."""
        with self._lock:
            self._conn.execute("VACUUM")
