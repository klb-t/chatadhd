#!/usr/bin/env python3
"""Synthetic data directory + import export for Loom scale measurements.

NOT part of CI and not a test. No private content: all text is deterministic lorem-like PL/EN
generated from a seed. Writes the real format (schema v4 DDL as in loom/src/db/db.cpp, same ID,
timestamp and JSON formatting as Python/Loom) in large batched transactions, so Loom opens the
result directly (it adds its own loom_* tables on open).

  gen_synthetic_data.py DATA_DIR --conversations 5000 --messages 100000 [--seed 1]
        [--nodes N] [--links N] [--tasks N] [--export FILE --export-conversations N --export-format chatgpt|claude]

Defaults: nodes = messages/10, links = 2*nodes, tasks = conversations/10 (with one artifact each).
Scale is bounded only by disk: 1,000,000 messages is about 0.5 GB of database.
"""
import argparse
import json
import os
import random
import sqlite3
import sys
from datetime import datetime, timedelta

EN = ("the of and to in is that for it with as was on are this be by at from or have an not they which one you "
      "were all can her has there been if more when will would who so no data model graph memory search message "
      "conversation branch project context server import export tree node link index query result policy "
      "token budget summary review plan task agent file folder note idea code build test release deploy").split()
PL = ("i w nie na z to się że do jak o jest co po tak za od ale dla przez czy już tylko jego może bardzo "
      "dane model graf pamięć szukaj wiadomość rozmowa gałąź projekt kontekst serwer import eksport drzewo "
      "węzeł powiązanie indeks zapytanie wynik zasada budżet podsumowanie przegląd plan zadanie agent plik "
      "folder notatka pomysł kod budowa test wydanie wdrożenie").split()
KINDS = ("entity", "topic", "concept", "code_ref", "file", "person", "org")
LINK_TYPES = ("mentions", "tagged_with", "references", "depends_on", "part_of", "derived_from")
BASE = datetime(2024, 1, 1)

DDL = """
CREATE TABLE IF NOT EXISTS _meta (key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS conversations (id TEXT PRIMARY KEY, title TEXT NOT NULL, created TEXT NOT NULL,
 updated TEXT NOT NULL, source TEXT NOT NULL DEFAULT 'user', metadata TEXT NOT NULL DEFAULT '{}');
CREATE TABLE IF NOT EXISTS messages (id TEXT PRIMARY KEY, conv_id TEXT NOT NULL, parent_id TEXT,
 role TEXT NOT NULL, text TEXT NOT NULL, model TEXT, status TEXT NOT NULL DEFAULT 'active',
 version_group_id TEXT, version_num INTEGER NOT NULL DEFAULT 1, weight REAL NOT NULL DEFAULT 1.0,
 attachments TEXT NOT NULL DEFAULT '[]', metadata TEXT NOT NULL DEFAULT '{}', created TEXT NOT NULL,
 semantic_status TEXT NOT NULL DEFAULT 'pending',
 FOREIGN KEY (conv_id) REFERENCES conversations(id) ON DELETE CASCADE);
CREATE TABLE IF NOT EXISTS nodes (id TEXT PRIMARY KEY, kind TEXT NOT NULL DEFAULT 'entity', label TEXT NOT NULL,
 content TEXT NOT NULL DEFAULT '', tags TEXT NOT NULL DEFAULT '[]', metadata TEXT NOT NULL DEFAULT '{}',
 created TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS links (id TEXT PRIMARY KEY, src TEXT NOT NULL, dst TEXT NOT NULL,
 link_type TEXT NOT NULL DEFAULT 'related', weight REAL NOT NULL DEFAULT 1.0, metadata TEXT NOT NULL DEFAULT '{}',
 created TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS loom_tasks (id TEXT PRIMARY KEY, kind TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'pending',
 params TEXT NOT NULL DEFAULT '{}', input_hash TEXT, output_hash TEXT, checkpoint TEXT, attempts INTEGER NOT NULL DEFAULT 0,
 max_attempts INTEGER NOT NULL DEFAULT 3, error TEXT, parent_id TEXT, result TEXT, created TEXT NOT NULL, updated TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS loom_artifacts (id TEXT PRIMARY KEY, kind TEXT NOT NULL, title TEXT NOT NULL DEFAULT '',
 blob_hash TEXT, mime TEXT, task_id TEXT, metadata TEXT NOT NULL DEFAULT '{}', created TEXT NOT NULL);
"""
INDEXES = """
CREATE INDEX IF NOT EXISTS idx_msg_conv ON messages(conv_id);
CREATE INDEX IF NOT EXISTS idx_msg_parent ON messages(parent_id);
CREATE INDEX IF NOT EXISTS idx_msg_vgroup ON messages(version_group_id);
CREATE INDEX IF NOT EXISTS idx_msg_status ON messages(conv_id, status);
CREATE INDEX IF NOT EXISTS idx_msg_semantic ON messages(semantic_status);
CREATE INDEX IF NOT EXISTS idx_nodes_kind ON nodes(kind);
CREATE INDEX IF NOT EXISTS idx_links_src ON links(src);
CREATE INDEX IF NOT EXISTS idx_links_dst ON links(dst);
CREATE INDEX IF NOT EXISTS idx_links_type ON links(link_type);
"""


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)
        self.n = 0

    def hid(self, prefix):  # prefixed 12-hex id, deterministic
        return prefix + "%012x" % self.r.getrandbits(48)

    def text(self, lo=8, hi=70):
        words = PL if self.r.random() < 0.4 else EN
        k = self.r.randint(lo, hi)
        s = " ".join(self.r.choices(words, k=k))
        if self.r.random() < 0.05:
            s += " zażółć gęślą jaźń"
        return s[0].upper() + s[1:] + "."

    def ts(self, i):
        return (BASE + timedelta(seconds=i * 7, microseconds=(i * 13) % 1000000)).isoformat() + "Z"


def js(o):
    return json.dumps(o, ensure_ascii=True)


def build_db(a):
    os.makedirs(a.data_dir, exist_ok=True)
    open(os.path.join(a.data_dir, ".chatadhd_data"), "a").close()
    path = os.path.join(a.data_dir, "chatadhd.db")
    if os.path.exists(path):
        sys.exit("refusing to overwrite " + path)
    g = Gen(a.seed)
    db = sqlite3.connect(path)
    db.execute("PRAGMA journal_mode=WAL")
    db.execute("PRAGMA synchronous=OFF")
    db.executescript(DDL)
    db.execute("INSERT OR REPLACE INTO _meta VALUES('schema_version','4')")
    nconv, nmsg = a.conversations, a.messages
    conv_ids = [g.hid("c_") for _ in range(nconv)]
    per, extra = divmod(nmsg, nconv)
    sizes = [per + (1 if i < extra else 0) for i in range(nconv)]
    # one long conversation (like a real months-long chat): --big-conv messages, taken evenly from the others
    big = min(a.big_conv, max(0, nmsg - 2 * nconv)) if nconv > 1 else 0
    if big:
        share = big // (nconv - 1) + 1
        for i in range(1, nconv):
            sizes[i] = max(2, sizes[i] - share)
        sizes[0] = nmsg - sum(sizes[1:])
    batch, mi = [], 0
    rows = []
    for ci, cid in enumerate(conv_ids):
        rows.append((cid, "Rozmowa %d: %s" % (ci, g.text(2, 6)[:60]), g.ts(ci), g.ts(ci + sizes[ci]),
                     "user" if ci % 10 else "import", "{}"))
    db.executemany("INSERT INTO conversations VALUES(?,?,?,?,?,?)", rows)
    db.commit()
    for ci, cid in enumerate(conv_ids):
        parent = None
        for j in range(sizes[ci]):
            mid = g.hid("m_")
            batch.append((mid, cid, parent, "user" if j % 2 == 0 else "assistant", g.text(),
                          None if j % 2 == 0 else "fake/model", "active", None, 1, 1.0, "[]", "{}", g.ts(mi), "done"))
            parent = mid
            mi += 1
            if len(batch) >= 20000:
                db.executemany("INSERT INTO messages VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)", batch)
                db.commit()
                batch = []
    if batch:
        db.executemany("INSERT INTO messages VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)", batch)
        db.commit()
    nn = a.nodes if a.nodes is not None else nmsg // 10
    nl = a.links if a.links is not None else 2 * nn
    nodes = []
    ids = []
    for i in range(nn):
        nid = g.hid("n_")
        ids.append(nid)
        w = g.r.choice(EN + PL)
        nodes.append((nid, g.r.choice(KINDS), "%s %d" % (w, i), g.text(3, 12), js([w]), "{}", g.ts(i)))
        if len(nodes) >= 20000:
            db.executemany("INSERT INTO nodes VALUES(?,?,?,?,?,?,?)", nodes)
            nodes = []
    if nodes:
        db.executemany("INSERT INTO nodes VALUES(?,?,?,?,?,?,?)", nodes)
    links = []
    for i in range(nl if ids else 0):
        # hub-heavy: the low ids are chosen more often
        s = ids[int(len(ids) * g.r.random() ** 2)]
        d = ids[g.r.randrange(len(ids))]
        links.append((g.hid("l_"), s, d, g.r.choice(LINK_TYPES), round(g.r.random(), 3), "{}", g.ts(i)))
        if len(links) >= 20000:
            db.executemany("INSERT INTO links VALUES(?,?,?,?,?,?,?)", links)
            links = []
    if links:
        db.executemany("INSERT INTO links VALUES(?,?,?,?,?,?,?)", links)
    nt = a.tasks if a.tasks is not None else nconv // 10
    for i in range(nt):
        tid = g.hid("t_")
        db.execute("INSERT INTO loom_tasks(id,kind,status,params,result,created,updated) VALUES(?,?,?,?,?,?,?)",
                   (tid, "action", "done", js({"action": "summarise", "conv_id": conv_ids[i % nconv]}),
                    js({"text": g.text(10, 40)}), g.ts(i), g.ts(i)))
        db.execute("INSERT INTO loom_artifacts(id,kind,title,task_id,metadata,created) VALUES(?,?,?,?,?,?)",
                   (g.hid("a_"), "action_result", g.text(2, 5)[:60], tid, "{}", g.ts(i)))
    db.executescript(INDEXES)
    db.commit()
    db.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    db.close()
    return conv_ids


def write_export(a):
    g = Gen(a.seed + 1000)
    n = a.export_conversations
    per = max(2, a.export_messages // n)
    with open(a.export, "w", encoding="utf-8") as f:
        f.write("[")
        for ci in range(n):
            if ci:
                f.write(",")
            if a.export_format == "claude":
                msgs = [{"uuid": g.hid(""), "sender": "human" if j % 2 == 0 else "assistant", "text": g.text(),
                         "created_at": g.ts(ci * per + j)} for j in range(per)]
                f.write(js({"uuid": g.hid(""), "name": "Import %d" % ci, "created_at": g.ts(ci), "updated_at": g.ts(ci),
                            "chat_messages": msgs}))
            else:
                mapping, prev = {}, "root"
                mapping["root"] = {"id": "root", "message": None, "parent": None, "children": ["n0"]}
                for j in range(per):
                    nid = "n%d" % j
                    mapping[nid] = {"id": nid, "parent": prev, "children": ["n%d" % (j + 1)] if j + 1 < per else [],
                                    "message": {"id": nid, "author": {"role": "user" if j % 2 == 0 else "assistant"},
                                                "create_time": 1700000000 + ci * 1000 + j,
                                                "content": {"content_type": "text", "parts": [g.text()]}}}
                    prev = nid
                f.write(js({"title": "Import %d" % ci, "create_time": 1700000000 + ci, "update_time": 1700000000 + ci,
                            "mapping": mapping, "current_node": prev}))
        f.write("]")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("data_dir", nargs="?")
    p.add_argument("--conversations", type=int, default=1000)
    p.add_argument("--messages", type=int, default=20000)
    p.add_argument("--big-conv", type=int, default=0, help="messages in the first (long) conversation, taken from the others")
    p.add_argument("--nodes", type=int)
    p.add_argument("--links", type=int)
    p.add_argument("--tasks", type=int)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--export")
    p.add_argument("--export-conversations", type=int, default=1000)
    p.add_argument("--export-messages", type=int, default=20000)
    p.add_argument("--export-format", choices=("chatgpt", "claude"), default="chatgpt")
    a = p.parse_args()
    if not a.data_dir and not a.export:
        p.error("give DATA_DIR and/or --export FILE")
    if a.data_dir:
        if a.conversations < 1 or a.conversations > a.messages:
            p.error("need 1 <= conversations <= messages")
        build_db(a)
    if a.export:
        write_export(a)


if __name__ == "__main__":
    main()
