"""Native SQLite/WAL read boundaries, separate from semantic evaluation."""
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest

from core_snapshot import snapshot


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "db.sqlite"
        self.db = sqlite3.connect(self.path)
        self.addCleanup(self.db.close)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("CREATE TABLE loom_kb_runs(run_id PRIMARY KEY,status,inputs,summary,replayed_seq)")
        for run in ("a", "b"):
            self.db.execute("INSERT INTO loom_kb_runs VALUES(?, 'done','{}','{}',7)", (run,))
        for name in ("claims", "observations", "entities"):
            self.db.execute(f"CREATE TABLE loom_kb_{name}(run_id,id,body)")
            for run in ("a", "b"):
                body = {"id": name + run, "opaque": {"future": ["żółty", 1]}}
                self.db.execute(f"INSERT INTO loom_kb_{name} VALUES(?,?,?)", (run, body["id"], json.dumps(body)))
        self.db.execute("CREATE TABLE loom_kb_slot_values(run_id,instance_id,slot,ord,body)")
        self.db.execute("INSERT INTO loom_kb_slot_values VALUES('a','inst','intent',0,?)",
                        (json.dumps({"slot": "intent", "claim": "claimsa", "role": "intent"}),))
        self.db.commit()

    def test_committed_wal_run_scope_and_slot_join(self):
        self.assertTrue(Path(str(self.path) + "-wal").exists())
        result = snapshot(self.path, "a")
        self.assertEqual([x["id"] for x in result["claims"]], ["claimsa"])
        self.assertEqual(result["claims"][0]["opaque"], {"future": ["żółty", 1]})
        self.assertEqual(result["slot_values"][0]["instance"], "inst")
        self.assertEqual(result["run"]["replayed_seq"], 7)
        self.assertIn("loom_kb_operators", result["snapshot"]["missing_optional_tables"])
        self.assertEqual(result, snapshot(self.path, "a"))
        # A pending write on another connection is never observed.
        self.db.execute("DELETE FROM loom_kb_claims WHERE run_id='a'")
        self.assertEqual(len(snapshot(self.path, "a")["claims"]), 1)
        self.db.rollback()

    def test_unknown_run_missing_database_and_limits_fail(self):
        for run in ("", "nonexistent"):
            with self.assertRaises(ValueError):
                snapshot(self.path, run)
        absent = self.path.with_name("absent.sqlite")
        with self.assertRaises(FileNotFoundError):
            snapshot(absent, "a")
        self.assertFalse(absent.exists())
        with self.assertRaises(ValueError):
            snapshot(self.path, "a", max_rows=2)

    def test_corrupt_id_does_not_become_a_source_reference(self):
        self.db.execute("UPDATE loom_kb_claims SET body='{\"id\":\"wrong\"}' WHERE run_id='a'")
        self.db.commit()
        with self.assertRaisesRegex(ValueError, "indexed id"):
            snapshot(self.path, "a")

    def test_schema_without_required_bodies_is_not_empty_graph(self):
        self.db.execute("DROP TABLE loom_kb_observations")
        self.db.commit()
        with self.assertRaisesRegex(ValueError, "required table"):
            snapshot(self.path, "a")


if __name__ == "__main__":
    unittest.main()
