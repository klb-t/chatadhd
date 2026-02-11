"""
ChatADHD v0.07.00 - Database Layer
Thread-safe SQLite with WAL mode, message versioning, and graph links.

Design notes:
  - Every write acquires ``_lock`` so the DB is safe from concurrent threads
    (Kivy UI thread + background API thread).
  - WAL journal mode gives crash resilience.
  - Schema migrations are versioned so future upgrades never lose data.
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

_SCHEMA_VERSION = 2  # Bump when you add migrations.


def _gen_id(prefix: str = "") -> str:
    return prefix + uuid.uuid4().hex[:12]


class Database:
    """Persistent storage for conversations, messages, and graph links."""

    def __init__(self, path: Path) -> None:
        self._path = Path(path)
        self._path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.RLock()  # Reentrant so nested calls work.

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
                CREATE INDEX IF NOT EXISTS idx_links_src     ON links(src);
                CREATE INDEX IF NOT EXISTS idx_links_dst     ON links(dst);
            """)
            self._set_meta("schema_version", str(_SCHEMA_VERSION))
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
            self._conn.execute(
                "INSERT INTO links (id, src, dst, link_type, weight, metadata, created) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)",
                (lid, src, dst, link_type, weight, json.dumps(metadata or {}), now),
            )
            self._conn.commit()
        return lid

    def get_links(self, node_id: Optional[str] = None) -> list[dict[str, Any]]:
        with self._lock:
            if node_id:
                rows = self._conn.execute(
                    "SELECT * FROM links WHERE src = ? OR dst = ?",
                    (node_id, node_id),
                ).fetchall()
            else:
                rows = self._conn.execute("SELECT * FROM links").fetchall()
        return [dict(r) for r in rows]

    def delete_link(self, lid: str) -> None:
        with self._lock:
            self._conn.execute("DELETE FROM links WHERE id = ?", (lid,))
            self._conn.commit()

    # ------------------------------------------------------------------
    # Graph data for visualization
    # ------------------------------------------------------------------
    def get_graph_data(
        self, conv_id: Optional[str] = None
    ) -> dict[str, list]:
        nodes: list[dict] = []
        edges: list[dict] = []

        if conv_id:
            for m in self.get_msgs(conv_id, include_all=True):
                nodes.append({
                    "id": m["id"],
                    "label": m["text"][:25],
                    "type": m["role"],
                    "status": m["status"],
                    "weight": m.get("weight", 1.0),
                    "version_group": m.get("version_group_id"),
                })
                if m.get("parent_id"):
                    edges.append({
                        "src": m["parent_id"],
                        "dst": m["id"],
                        "type": "reply",
                        "weight": 1.0,
                    })

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
