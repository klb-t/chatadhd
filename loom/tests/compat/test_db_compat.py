"""Python engine.db <-> Loom Database: one data format, two implementations.

Hard invariant: Python must be able to open and fully use a DB created by
Loom and vice versa. Proven here by differential execution:
  (a) Python creates + fills -> Loom reads everything (API + raw) identically
      and Loom's CRUD matches Python's CRUD on a copy
  (b) Loom creates + fills -> results equal a pure-Python run; Python opens
      it (migration is a no-op), does CRUD, and both read the same state
  (c) interleaved: Python and Loom alternate on the same file
  (d) concurrent writers from both processes on one WAL database
  plus schema text identity, legacy-schema migration identity and JSON
  column byte format.
"""
import json
import shutil
import sqlite3
import unittest

from compat_common import (CompatTestCase, Normalizer, api_dump_py, canonical_api_dump, core_raw, core_schema,
                           normalized_dump, raw_dump_py, run_ops_py, schema_py, tool, tool_popen)
from engine.db import Database


def batch_messages():
    msgs = []
    for i in range(300):
        m = {"role": "user" if i % 2 == 0 else "assistant", "text": f"batch message {i} zażółć 😀 {'x' * (i % 40)}"}
        if i % 50 == 7:
            m["text"] = "   \n\t "  # skipped (blank)
        if i % 30 == 3:
            m = {"role": "user", "content": f"from content key {i}"}
        if i % 25 == 5:
            m["weight"] = 1.75
            m["metadata"] = {"i": i, "tags": ["a", "b"], "nested": {"k": [1, 2.5, None, True]}}
        if i % 40 == 9:
            m["attachments"] = ["/tmp/file_%d.txt" % i]
        if i == 11:
            m["attachments"] = None  # json.dumps(None) -> "null" (Python quirk kept)
        msgs.append(m)
    return msgs


def write_script():
    return [
        {"op": "create_conv", "args": {"title": "Alpha"}, "save": "c1"},
        {"op": "create_conv", "args": {"title": "Beta ąęś 😀 \"quoted\""}, "save": "c2"},
        {"op": "create_conv", "args": {}, "save": "c3"},
        {"op": "create_msg", "args": {"conv_id": "$c1.id", "role": "user",
                                      "text": "Hello, Loom! Let's talk about SQLite, FTS5 and the graph."}, "save": "m1"},
        {"op": "create_msg", "args": {"conv_id": "$c1.id", "role": "assistant", "model": "anthropic/claude-sonnet-4",
                                      "parent_id": "$m1", "weight": 0.25,
                                      "text": "Sure — here's an answer with émojis 🎉, \"quotes\" and \\ backslashes.",
                                      "attachments": ["/tmp/a.png", "/tmp/b.md"],
                                      "metadata": {"reasoning": "think", "topics": ["tech"], "n": 1.5,
                                                   "nested": {"a": [1, 2, {"b": None}]}, "ü": "ß"}},
         "save": "m2"},
        {"op": "get_msg", "args": {"mid": "$m2"}, "save": "m2row"},
        {"op": "edit_msg", "args": {"mid": "$m1", "new_text": "Hello again (edited)"}, "save": "m1v2"},
        {"op": "edit_msg", "args": {"mid": "$m1v2", "new_text": "Third version"}, "save": "m1v3"},
        {"op": "get_msg", "args": {"mid": "$m1"}, "save": "m1row"},
        {"op": "get_versions", "args": {"version_group_id": "$m1row.version_group_id"}},
        {"op": "get_versions", "args": {"version_group_id": "$m2row.version_group_id"}},
        {"op": "restore_version", "args": {"mid": "$m1v2"}},
        {"op": "restore_version", "args": {"mid": "m_000000000000"}},
        {"op": "edit_msg", "args": {"mid": "m_000000000000", "new_text": "nope"}},
        {"op": "set_msg_status", "args": {"mid": "$m2", "status": "excluded"}},
        {"op": "set_msg_status", "args": {"mid": "$m2", "status": "bogus"}},
        {"op": "batch_create_msgs", "args": {"conv_id": "$c2.id", "messages": batch_messages(), "batch_size": 100}},
        {"op": "mark_analysed", "args": {"msg_id": "$m1v2", "analysis": {
            "source": "llm", "entities": [{"name": "Loom", "kind": "concept"}], "topics": ["tech", "ai"],
            "summary": "ż" * 300, "sentiment": "positive"}}},
        {"op": "mark_analysed", "args": {"msg_id": "$m2", "analysis": {}}},
        {"op": "update_msg", "args": {"id": "$m2", "fields": {"weight": 2.0, "metadata": {"k": "v", "n": [1, 2]}}}},
        {"op": "update_conv", "args": {"id": "$c1.id", "fields": {"title": "Alpha renamed"}}},
        {"op": "create_node", "args": {"label": "Loom", "kind": "concept", "content": "kernel", "tags": ["a", "b"],
                                       "metadata": {"x": 1}}, "save": "n1"},
        {"op": "create_node", "args": {"label": "Fixed", "node_id": "n_fixed_id_1"}, "save": "n2"},
        {"op": "create_node", "args": {"label": "Other", "node_id": "n_fixed_id_1"}},
        {"op": "get_or_create_node", "args": {"label": "Loom", "kind": "concept"}},
        {"op": "get_or_create_node", "args": {"label": "tech", "kind": "topic"}, "save": "n3"},
        {"op": "find_node", "args": {"label": "Loom"}},
        {"op": "find_node", "args": {"label": "Loom", "kind": "topic"}},
        {"op": "update_node", "args": {"id": "$n1", "fields": {"content": "kernel v2", "tags": ["c"]}}},
        {"op": "create_link", "args": {"src": "$m1", "dst": "$n1", "link_type": "mentions", "weight": 0.9},
         "save": "l1"},
        {"op": "create_link", "args": {"src": "$m1", "dst": "$n1", "link_type": "mentions", "weight": 0.5,
                                       "metadata": {"u": 1}}, "save": "l1b"},
        {"op": "create_link", "args": {"src": "$m1", "dst": "$n3", "link_type": "tagged_with"}, "save": "l2"},
        {"op": "create_link", "args": {"src": "$n1", "dst": "$n3"}, "save": "l3"},
        {"op": "create_link", "args": {"src": "$m2", "dst": "$c1.id", "link_type": "part_of", "weight": 0.3}},
        {"op": "delete_link", "args": {"lid": "$l3"}},
        {"op": "get_links", "args": {}},
        {"op": "get_links", "args": {"node_id": "$m1"}},
        {"op": "get_links", "args": {"node_id": "$m1", "link_type": "mentions"}},
        {"op": "get_links", "args": {"link_type": "tagged_with"}},
        {"op": "get_node", "args": {"nid": "$n1"}},
        {"op": "get_node", "args": {"nid": "n_missing"}},
        {"op": "list_nodes", "args": {}},
        {"op": "list_nodes", "args": {"kind": "topic"}},
        {"op": "list_nodes", "args": {"limit": 1}},
        {"op": "get_unanalysed_msgs", "args": {"limit": 1000}},
        {"op": "get_unanalysed_msgs", "args": {"limit": 3}},
        {"op": "count_pending_semantic", "args": {}},
        {"op": "get_msgs", "args": {"conv_id": "$c1.id"}},
        {"op": "get_msgs", "args": {"conv_id": "$c1.id", "include_all": True}},
        {"op": "get_msgs", "args": {"conv_id": "$c2.id", "include_all": True}},
        {"op": "get_graph_data", "args": {"conv_id": "$c1.id"}},
        {"op": "get_graph_data", "args": {}},
        {"op": "get_conv", "args": {"cid": "$c1.id"}},
        {"op": "get_conv", "args": {"cid": "c_missing"}},
        {"op": "list_convs", "args": {"limit": 10}},
    ]


def crud_script():
    return [
        {"op": "create_msg", "args": {"conv_id": "$c3.id", "text": "new message in c3", "role": "user"}, "save": "x1"},
        {"op": "edit_msg", "args": {"mid": "$x1", "new_text": "edited x1"}, "save": "x2"},
        {"op": "update_msg", "args": {"id": "$m1v3", "fields": {"text": "updated text", "semantic_status": "done"}}},
        {"op": "update_msg", "args": {"id": "$m1v3", "fields": {"attachments": ["/x"], "model": None}}},
        {"op": "delete_node", "args": {"nid": "$n3"}},
        {"op": "delete_conv", "args": {"cid": "$c2.id"}},
        {"op": "update_node", "args": {"id": "n_fixed_id_1", "fields": {"label": "Fixed2", "metadata": {"z": True}}}},
        {"op": "update_conv", "args": {"id": "$c3.id", "fields": {"title": "Gamma", "source": "import"}}},
        {"op": "get_links", "args": {}},
        {"op": "list_nodes", "args": {"limit": 50}},
        {"op": "list_convs", "args": {"limit": 50}},
        {"op": "get_msgs", "args": {"conv_id": "$c1.id", "include_all": True}},
        {"op": "get_msgs", "args": {"conv_id": "$c3.id", "include_all": True}},
        {"op": "get_msgs", "args": {"conv_id": "$c2.id", "include_all": True}},
        {"op": "count_pending_semantic", "args": {}},
        {"op": "get_unanalysed_msgs", "args": {"limit": 1000}},
        {"op": "get_graph_data", "args": {"conv_id": "$c3.id"}},
        {"op": "vacuum", "args": {}},
        {"op": "list_convs", "args": {"limit": 50}},
    ]


def py_run(path, ops, vars_=None):
    db = Database(path)
    try:
        return run_ops_py(db, ops, vars_)
    finally:
        db.close()


def py_api_dump(path):
    db = Database(path)
    try:
        return canonical_api_dump(api_dump_py(db))
    finally:
        db.close()


def cpp_run(tmp, path, ops, vars_=None, tag="ops"):
    args = ["db-ops", path, tmp.file_json(f"{tag}.json", ops)]
    if vars_ is not None:
        args.append(tmp.file_json(f"{tag}_vars.json", vars_))
    return tool(*args)


def cpp_api_dump(path):
    return canonical_api_dump(tool("db-api-dump", path))


class DbCompatTest(CompatTestCase):

    def assertSameExecution(self, py_out, cpp_out, msg):
        n = Normalizer()
        m = Normalizer()
        self.assertSameJson(n(py_out["results"]), m(cpp_out["results"]), msg)
        return n, m

    # ── schema ────────────────────────────────────────────────────────
    def test_schema_text_matches_python(self):
        py_db = self.tmp.path / "py.db"
        cpp_db = self.tmp.path / "cpp.db"
        Database(py_db).close()
        tool("db-open", cpp_db)
        self.assertEqual(core_schema(schema_py(py_db)), core_schema(schema_py(cpp_db)))
        # The Loom tool reads sqlite_master identically too.
        self.assertEqual(schema_py(cpp_db), tool("db-schema", cpp_db))
        # Main DB: no triggers, no virtual tables (FTS lives in a separate file).
        for type_, name, _tbl, sql in schema_py(cpp_db):
            self.assertNotEqual(type_, "trigger")
            self.assertNotIn("VIRTUAL", (sql or "").upper(), name)
        opened = tool("db-open", cpp_db)
        self.assertEqual(opened["schema_version"], "4")
        self.assertEqual(opened["loom_schema_version"], "1")

    # ── (a) Python writes, Loom reads + CRUD ──────────────────────────
    def test_a_python_created_db_is_read_identically_by_loom(self):
        path = self.tmp.path / "py.db"
        py_w = py_run(path, write_script())
        raw_before = raw_dump_py(path)
        # Same file, two readers: API view and raw rows must be identical.
        self.assertSameJson(py_api_dump(path), cpp_api_dump(path), "API dump (Python file)")
        self.assertSameJson(raw_dump_py(path), tool("db-raw-dump", path), "raw dump (Python file)")
        # Loom's migration must not change any Python row.
        self.assertSameJson(core_raw(raw_before), core_raw(raw_dump_py(path)), "rows changed by Loom open")
        # Loom CRUD on the Python DB == Python CRUD on a copy.
        copy = self.tmp.path / "py_copy.db"
        shutil.copy(path, copy)
        for suffix in ("-wal", "-shm"):
            if (self.tmp.path / f"py.db{suffix}").exists():
                shutil.copy(self.tmp.path / f"py.db{suffix}", self.tmp.path / f"py_copy.db{suffix}")
        py_c = py_run(copy, crud_script(), py_w["vars"])
        cpp_c = cpp_run(self.tmp, path, crud_script(), py_w["vars"], "crud")
        # Same starting ids; only the ids created by the CRUD script differ.
        n_py, n_cpp = self.assertSameExecution(py_c, cpp_c, "CRUD results")
        self.assertSameJson(normalized_dump(n_py, py_api_dump(copy)), normalized_dump(n_cpp, py_api_dump(path)),
                            "state after CRUD")
        # And Python can still use the file Loom modified.
        self.assertSameJson(py_api_dump(path), cpp_api_dump(path), "API dump after Loom CRUD")

    # ── (b) Loom writes, Python reads + CRUD ──────────────────────────
    def test_b_loom_created_db_is_fully_usable_by_python(self):
        cpp_db = self.tmp.path / "cpp.db"
        py_db = self.tmp.path / "py.db"
        cpp_w = cpp_run(self.tmp, cpp_db, write_script(), tag="write")
        py_w = py_run(py_db, write_script())
        n_py, n_cpp = self.assertSameExecution(py_w, cpp_w, "write-script results")
        # Raw rows identical modulo ids/timestamps (same op sequence => same rowid order).
        self.assertSameJson(n_py(core_raw(raw_dump_py(py_db))), n_cpp(core_raw(raw_dump_py(cpp_db))), "raw rows")
        # Every JSON column Loom wrote is byte-identical to Python json.dumps.
        conn = sqlite3.connect(cpp_db)
        for table, cols in (("messages", ("attachments", "metadata")), ("nodes", ("tags", "metadata")),
                            ("links", ("metadata",)), ("conversations", ("metadata",))):
            for row in conn.execute(f"SELECT {', '.join(cols)} FROM {table}"):
                for text in row:
                    self.assertEqual(text, json.dumps(json.loads(text)), f"{table} JSON column format")
        conn.close()
        # Python opens the Loom DB: migration is a no-op on the core schema.
        schema_before = schema_py(cpp_db)
        Database(cpp_db).close()
        self.assertEqual(schema_before, schema_py(cpp_db))
        # Full Python CRUD on the Loom DB behaves exactly like on its own DB.
        py_on_cpp = py_run(cpp_db, crud_script(), cpp_w["vars"])
        py_on_py = py_run(py_db, crud_script(), py_w["vars"])
        self.assertSameExecution(py_on_py, py_on_cpp, "Python CRUD on Loom DB vs on Python DB")
        # Both implementations read the resulting file identically.
        self.assertSameJson(py_api_dump(cpp_db), cpp_api_dump(cpp_db), "API dump after Python CRUD")
        self.assertSameJson(raw_dump_py(cpp_db), tool("db-raw-dump", cpp_db), "raw dump after Python CRUD")

    # ── (c) interleaved ───────────────────────────────────────────────
    def test_c_interleaved_python_and_loom_on_one_file(self):
        script = write_script() + crud_script()
        chunks = [script[i:i + 7] for i in range(0, len(script), 7)]
        shared = self.tmp.path / "shared.db"
        vars_ = {}
        all_results = []
        for i, chunk in enumerate(chunks):
            if i % 2 == 0:
                out = py_run(shared, chunk, vars_)
            else:
                out = cpp_run(self.tmp, shared, chunk, vars_, f"chunk{i}")
            vars_ = out["vars"]
            all_results.extend(out["results"])
        reference = self.tmp.path / "reference.db"
        ref = py_run(reference, script)
        n_ref, n_mix = Normalizer(), Normalizer()
        self.assertSameJson(n_ref(ref["results"]), n_mix(all_results), "interleaved results vs pure Python")
        self.assertSameJson(normalized_dump(n_ref, py_api_dump(reference)), normalized_dump(n_mix, py_api_dump(shared)),
                            "final state")
        self.assertSameJson(py_api_dump(shared), cpp_api_dump(shared), "both readers agree")

    # ── (d) concurrent processes ──────────────────────────────────────
    def test_d_concurrent_writers_python_and_loom(self):
        path = self.tmp.path / "concurrent.db"
        db = Database(path)
        conv = db.create_conv("shared")
        n = 150
        cpp_ops = [{"op": "create_msg", "args": {"conv_id": conv["id"], "text": f"loom {i}", "role": "assistant"}}
                   for i in range(n)]
        proc = tool_popen("db-ops", path, self.tmp.file_json("concurrent.json", cpp_ops))
        for i in range(n):
            db.create_msg(conv["id"], f"python {i}", "user")
            db.create_link(f"m_py{i}", "n_x", "mentions")
        out, err = proc.communicate(timeout=300)
        self.assertEqual(proc.returncode, 0, err)
        cpp_results = json.loads(out)["results"]
        self.assertTrue(all(isinstance(r, str) and r.startswith("m_") for r in cpp_results), cpp_results[:3])
        msgs = db.get_msgs(conv["id"])
        self.assertEqual(len(msgs), 2 * n)
        self.assertEqual(sum(1 for m in msgs if m["role"] == "user"), n)
        db.close()
        self.assertSameJson(py_api_dump(path), cpp_api_dump(path))

    # ── legacy migrations ─────────────────────────────────────────────
    def test_legacy_schema_migrates_identically(self):
        legacy_sql = """
            CREATE TABLE conversations (id TEXT PRIMARY KEY, title TEXT NOT NULL, created TEXT NOT NULL,
                                        updated TEXT NOT NULL, metadata TEXT NOT NULL DEFAULT '{}');
            CREATE TABLE messages (id TEXT PRIMARY KEY, conv_id TEXT NOT NULL, parent_id TEXT,
                                   role TEXT NOT NULL, text TEXT NOT NULL, model TEXT,
                                   version_num INTEGER NOT NULL DEFAULT 1, weight REAL NOT NULL DEFAULT 1.0,
                                   attachments TEXT NOT NULL DEFAULT '[]', metadata TEXT NOT NULL DEFAULT '{}',
                                   created TEXT NOT NULL);
            CREATE TABLE links (id TEXT PRIMARY KEY, src TEXT NOT NULL, dst TEXT NOT NULL,
                                weight REAL NOT NULL DEFAULT 1.0, metadata TEXT NOT NULL DEFAULT '{}',
                                created TEXT NOT NULL);
            INSERT INTO conversations VALUES ('c_000000000001', 'Old', '2024-01-01T00:00:00Z',
                                              '2024-01-01T00:00:00Z', '{}');
            INSERT INTO messages (id, conv_id, role, text, created)
                   VALUES ('m_000000000001', 'c_000000000001', 'user', 'legacy text long enough', '2024-01-01T00:00:01Z');
            INSERT INTO links VALUES ('l_000000000001', 'm_000000000001', 'x', 1.0, '{}', '2024-01-01T00:00:02Z');
        """
        variants = {
            "v0": legacy_sql,
            "no_meta_only_messages": "CREATE TABLE messages (id TEXT PRIMARY KEY, conv_id TEXT NOT NULL, "
                                     "role TEXT NOT NULL, text TEXT NOT NULL, created TEXT NOT NULL);",
            "nodes_without_kind": "CREATE TABLE nodes (id TEXT PRIMARY KEY, label TEXT NOT NULL, created TEXT NOT NULL);",
            "old_meta_version": "CREATE TABLE _meta (key TEXT PRIMARY KEY, value TEXT);"
                                "INSERT INTO _meta VALUES ('schema_version', '2');",
        }
        for name, sql in variants.items():
            with self.subTest(variant=name):
                py_path = self.tmp.path / f"{name}_py.db"
                cpp_path = self.tmp.path / f"{name}_cpp.db"
                for p in (py_path, cpp_path):
                    conn = sqlite3.connect(p)
                    conn.executescript(sql)
                    conn.commit()
                    conn.close()
                Database(py_path).close()
                tool("db-open", cpp_path)
                self.assertEqual(core_schema(schema_py(py_path)), core_schema(schema_py(cpp_path)))
                self.assertSameJson(core_raw(raw_dump_py(py_path)), core_raw(raw_dump_py(cpp_path)))
                # And each side can open what the other migrated.
                Database(cpp_path).close()
                tool("db-open", py_path)
                try:
                    py_view = py_api_dump(cpp_path)
                except sqlite3.OperationalError as e:
                    # Python's own migration does not add every column (e.g.
                    # messages.metadata), so Python cannot read such a legacy
                    # file either; Loom must still open it without crashing.
                    self.assertIn("no such column", str(e))
                    tool("db-api-dump", cpp_path)
                    continue
                self.assertSameJson(py_view, cpp_api_dump(cpp_path))

    # ── derived FTS state never disturbs Python ───────────────────────
    def test_fts_index_is_separate_and_python_vacuum_works(self):
        path = self.tmp.path / "chatadhd.db"
        cpp_w = cpp_run(self.tmp, path, write_script(), tag="w")
        self.assertTrue((self.tmp.path / "chatadhd.fts.db").exists())
        db = Database(path)
        db.vacuum()
        run_ops_py(db, crud_script(), cpp_w["vars"])
        db.create_msg(cpp_w["vars"]["c1"]["id"], "a brand new zebra message", "user")
        db.close()
        hits = tool("db-search", path, "zebra")
        self.assertEqual(hits["mode"], "fts5")
        self.assertEqual([h["text"] for h in hits["results"]], ["a brand new zebra message"])
        like = tool("db-search", path, "zebra", "like")
        self.assertEqual(len(like["results"]), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
