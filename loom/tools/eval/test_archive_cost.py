import json
import sqlite3
import tempfile
import unittest
from pathlib import Path

import archive_cost


def make_db(path):
    con = sqlite3.connect(path)
    con.execute("CREATE TABLE messages(id TEXT, conv_id TEXT, role TEXT, text TEXT, status TEXT)")
    con.executemany("INSERT INTO messages VALUES (?,?,?,?,?)", [
        ("m1", "c1", "user", "a" * 1200, "active"),
        ("m2", "c1", "assistant", "b" * 2400, "active"),
        ("m3", "c1", "user", "c" * 600, "version"),
        ("m4", "c2", "user", "d" * 1200, "active"),
        ("m5", "c2", "assistant", "e" * 999, "excluded"),
        ("m6", "c2", "assistant", "f" * 5000, "deleted"),
    ])
    con.commit()
    con.close()


class ArchiveCostTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.db = Path(self.tmp.name) / "chatadhd.db"
        make_db(self.db)
        self.pricing = json.loads(archive_cost.PRICING.read_text(encoding="utf-8"))

    def tearDown(self):
        self.tmp.cleanup()

    def test_stats_split_by_status(self):
        s = archive_cost.archive_stats(self.db)
        self.assertEqual(s["conversations"], 2)
        self.assertEqual(s["messages"], 5)  # deleted never counted
        self.assertEqual(s["chars_active"], 4800)
        self.assertEqual(s["chars_versions"], 600)
        self.assertEqual(s["chars_excluded"], 999)
        self.assertEqual(s["chars_by_role"], {"assistant": 2400, "user": 2400})

    def test_estimate_arithmetic(self):
        s = archive_cost.archive_stats(self.db)
        e = archive_cost.estimate(s, self.pricing, output_ratio=0.5, prefix_tokens=0)
        self.assertEqual(e["tokens"], {"low": 1350, "high": 1800})  # 5400 chars / 4 and / 3
        p = self.pricing["models"]["claude-opus-5-5"]
        want = round((1800 * p["input"] + 900 * p["output"]) / 1e6, 2)
        self.assertEqual(e["models"]["claude-opus-5-5"]["high"], want)
        self.assertEqual(e["models"]["claude-opus-5-5"]["high_batch"], round(want * 0.5, 2))

    def test_fraction_and_versions(self):
        s = archive_cost.archive_stats(self.db)
        e = archive_cost.estimate(s, self.pricing, fraction=0.5, include_versions=False)
        self.assertEqual(e["tokens"]["high"], 800)  # 4800 * 0.5 / 3
        with self.assertRaises(ValueError):
            archive_cost.estimate(s, self.pricing, fraction=0)

    def test_db_opened_read_only(self):
        before = self.db.read_bytes()
        archive_cost.main(["--db", str(self.db), "--json"])
        self.assertEqual(self.db.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
