import json
from copy import deepcopy
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

    def test_reserved_uri_characters_open_exact_file_without_creating_sibling(self):
        for name in ("literal?archive.db", "literal#archive.db", "percent%20 space.db"):
            with self.subTest(name=name):
                path = Path(self.tmp.name) / name
                make_db(path)
                before = path.read_bytes()
                names = {p.name for p in path.parent.iterdir()}
                self.assertEqual(archive_cost.archive_stats(path)["chars_active"], 4800)
                self.assertEqual(path.read_bytes(), before)
                self.assertEqual({p.name for p in path.parent.iterdir()}, names)

    def test_missing_database_is_not_created(self):
        path = Path(self.tmp.name) / "absent?new.db"
        names = {p.name for p in path.parent.iterdir()}
        with self.assertRaises(sqlite3.OperationalError):
            archive_cost.archive_stats(path)
        self.assertEqual({p.name for p in path.parent.iterdir()}, names)

    def test_even_conversation_median_uses_both_middle_sizes(self):
        stats=archive_cost.archive_stats(self.db)
        # c1=4200, c2=1200. The prior upper-middle value4200 overstated this.
        self.assertEqual(stats["conversation_chars"], {"median":2700,"max":4200})
        with sqlite3.connect(self.db) as con:
            con.execute("DELETE FROM messages")
        self.assertEqual(archive_cost.archive_stats(self.db)["conversation_chars"],
                         {"median":0,"max":0})

    def test_unknown_status_counted_explicitly_and_configurable(self):
        with sqlite3.connect(self.db) as con:
            con.executemany("INSERT INTO messages VALUES (?,?,?,?,?)", [
                ("null", "c3", "user", "a"*60, None),
                ("future", "c3", "assistant", "b"*60, "future_status"),
            ])
        stats = archive_cost.archive_stats(self.db)
        self.assertEqual(stats["messages"], 7)
        self.assertEqual(stats["conversations"], 3)
        self.assertEqual(stats["chars_active"], 4800)
        self.assertEqual(stats["unknown_status_messages"], 2)
        self.assertEqual(stats["chars_unknown_status"], 120)
        self.assertEqual(archive_cost.estimate(stats,self.pricing)["tokens"]["high"],1840)
        self.assertEqual(archive_cost.estimate(stats,self.pricing,include_unknown_status=False)["tokens"]["high"],1800)

    def test_invalid_parameters_rejected_before_cost(self):
        stats=archive_cost.archive_stats(self.db)
        for field in ("output_ratio", "prefix_tokens", "fraction"):
            for value in (-1, float("nan"), float("inf"), float("-inf"), True, "1"):
                with self.subTest(field=field,value=value), self.assertRaises(ValueError):
                    archive_cost.estimate(stats,self.pricing,**{field:value})
        for field in ("include_versions","include_unknown_status"):
            with self.subTest(field=field),self.assertRaises(ValueError):
                archive_cost.estimate(stats,self.pricing,**{field:1})

    def test_invalid_counts_and_prices_rejected(self):
        stats=archive_cost.archive_stats(self.db)
        for field in ("chars_active","chars_versions","conversations","chars_unknown_status"):
            for value in (-1, True, 1.5, float("nan")):
                bad={**stats,field:value}
                with self.subTest(field=field,value=value),self.assertRaises(ValueError):
                    archive_cost.estimate(bad,self.pricing)
        for field in ("input","output","cache_read"):
            for value in (-1,True,float("nan"),float("inf")):
                bad=deepcopy(self.pricing)
                next(iter(bad["models"].values()))[field]=value
                with self.subTest(field=field,value=value),self.assertRaises(ValueError):
                    archive_cost.estimate(stats,bad)
        for value in (-1,True,float("nan"),float("inf")):
            with self.subTest(discount=value),self.assertRaises(ValueError):
                archive_cost.estimate(stats,{**self.pricing,"batch_discount":value})

    def test_large_finite_budget_supported_overflow_rejected(self):
        stats={"chars_active":10**12,"chars_versions":0,"conversations":10**9}
        pricing={"models":{"expensive":{"input":10**6,"output":10**6,"cache_read":10**6}}}
        out=archive_cost.estimate(stats,pricing,output_ratio=10,prefix_tokens=10**9)
        self.assertGreater(out["models"]["expensive"]["high"],10**9)
        huge={"chars_active":10**300,"chars_versions":0,"conversations":1}
        with self.assertRaises(ValueError):
            archive_cost.estimate(huge,pricing,output_ratio=1e300)
        for key in ("prefix_cache","batch"):
            self.assertIn("unverified",out["assumptions"][key])


if __name__ == "__main__":
    unittest.main()
