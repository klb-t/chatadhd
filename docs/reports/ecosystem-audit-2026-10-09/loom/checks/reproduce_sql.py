#!/usr/bin/env python3
"""Reproduce SQL decisions from the pinned loom source, without a C++ build.

Usage: python reproduce_sql.py /path/to/klb-t-loom
This is an audit probe, not a claim that the complete native application ran.
"""
import json
import re
import sqlite3
import sys
from pathlib import Path

root = Path(sys.argv[1])
db_source = (root / "src/db.cpp").read_text()
msg_source = (root / "src/db_messages.cpp").read_text()
graph_source = (root / "src/db_graph.cpp").read_text()
db = sqlite3.connect(":memory:")
schema = re.search(r'R"SQL\((CREATE TABLE.*?\n)\)SQL"', db_source, re.S)
if not schema:
    schema = re.search(r'R"SQL\((\nCREATE TABLE.*?)\)SQL"', db_source, re.S)
assert schema, "Pinned schema not found"
db.executescript(schema.group(1))
db.execute("INSERT INTO conversations(id,title,created,updated) VALUES('c','audit','0','0')")
db.executemany("INSERT INTO messages(id,conv_id,role,text,created,status) VALUES(?,'c','user',?,'0',?)", [
    ("short", "yes", "active"), ("long", "x" * 25, "active"), ("excluded", "x" * 25, "excluded")])
select = re.search(r'"(SELECT id,conv_id[^"\n]+length\(text\)>=20[^"\n]+)"', msg_source).group(1)
count = re.search(r'"(SELECT COUNT\(\*\) FROM messages WHERE semantic_status=\'pending\')"', msg_source).group(1)
pending = db.execute(count).fetchone()[0]
selected = len(db.execute(select, (100,)).fetchall())
assert (pending, selected) == (3, 1)

insert_node = re.search(r'"(INSERT OR IGNORE INTO nodes[^"\n]+)"', graph_source).group(1)
db.execute(insert_node, ("n", "entity", "old", "old evidence", "[]", "{}", "0"))
db.execute(insert_node, ("n", "entity", "new", "new evidence", "[]", "{}", "1"))
label, content = db.execute("SELECT label,content FROM nodes WHERE id='n'").fetchone()
assert (label, content) == ("old", "old evidence")

insert_link = re.search(r'"(INSERT INTO links[^"\n]+)"', graph_source).group(1)
update_link = re.search(r'"(UPDATE links SET weight=\?,metadata=\? WHERE id=\?)"', graph_source).group(1)
db.execute(insert_link, ("l", "n", "n", "related", .4, '{"source":"a"}', "0"))
db.execute(update_link, (.9, '{"source":"b"}', "l"))
metadata = db.execute("SELECT metadata FROM links WHERE id='l'").fetchone()[0]
assert metadata == '{"source":"b"}'

assert 'if(m.text.empty()) continue;' in msg_source
assert 'set_meta_unlocked("schema_version", "4")' in db_source
assert 'get_meta_unlocked("schema_version")' not in db_source[db_source.index('Status Database::migrate_unlocked'):db_source.index('Result<int> Database::schema_version')]
print(json.dumps({
    "method": "actual SQL strings extracted from source, executed on Python sqlite3; two control-flow assertions are source checks",
    "tests_passed": 5,
    "pending_count": pending,
    "selectable_count": selected,
    "duplicate_id_retains_old_node": True,
    "link_upsert_replaces_prior_metadata": True,
    "empty_batch_message_skip_source_confirmed": True,
    "migration_version_overwrite_source_confirmed": True
}, indent=2))
