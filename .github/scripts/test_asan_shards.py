"""Shard plan: every test in exactly one shard, new tests land in the rest shard, union is checked."""
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

import asan_shards as A
from verify_ctest import main as verify_main
from test_verify_ctest import fixture, native

PLAN = {"shards": [{"name": "a", "tests": ["unit.test_big", "server.*"]},
                   {"name": "b", "tests": ["compat.test_x"]},
                   {"name": "rest", "rest": True}]}
NAMES = ["unit.test_big", "server.keepalive", "compat.test_x", "unit.test_small", "unit.test_new"]


class ShardTests(unittest.TestCase):
    def test_each_test_in_exactly_one_shard_and_unlisted_go_to_rest(self):
        parts = A.assign(PLAN, NAMES)
        self.assertEqual(parts["a"], ["unit.test_big", "server.keepalive"])
        self.assertEqual(parts["b"], ["compat.test_x"])
        self.assertEqual(parts["rest"], ["unit.test_small", "unit.test_new"])
        self.assertEqual(sorted(sum(parts.values(), [])), sorted(NAMES))

    def test_first_shard_wins_and_stale_or_overlapping_patterns_only_advise(self):
        plan = {"shards": [{"name": "a", "tests": ["unit.*"]}, {"name": "b", "tests": ["unit.test_big", "gone.*"]},
                           {"name": "rest", "rest": True}]}
        self.assertEqual(A.assign(plan, NAMES)["b"], [])
        notes = " ".join(A.advisories(plan, NAMES))
        self.assertIn("gone.*", notes)
        self.assertIn("unit.test_big matches shards", notes)

    def test_plan_needs_exactly_one_rest_shard_and_unique_names(self):
        for shards in ([{"name": "a", "tests": []}], [{"name": "r", "rest": True}, {"name": "r2", "rest": True}],
                       [{"name": "a", "rest": True}, {"name": "a", "tests": []}], []):
            with self.subTest(shards=shards), tempfile.TemporaryDirectory() as d:
                p = Path(d, "p.json")
                p.write_text(json.dumps({"shards": shards}))
                with self.assertRaises(ValueError):
                    A.load_plan(p)

    def test_shipped_plan_loads_and_regex_selects_only_the_shard(self):
        plan = A.load_plan()
        self.assertGreaterEqual(len(plan["shards"]), 2)
        import re
        rx = re.compile(A.ctest_regex(["unit.test_a", "x.y"]))
        self.assertTrue(rx.search("unit.test_a"))
        self.assertFalse(rx.search("unit.test_ab"))
        self.assertFalse(rx.search("unitXtest_a"))

    def test_matrix_expands_only_sharded_legs(self):
        legs = [{"preset": "dev", "cxx": "g++"}, {"preset": "asan", "cxx": "g++", "sharded": True}]
        inc = A.matrix(legs, PLAN)["include"]
        self.assertEqual([i.get("shard") for i in inc], [None, "a", "b", "rest"])
        self.assertEqual(inc[1]["shards"], 3)
        self.assertNotIn("sharded", inc[1])

    def test_merge_requires_union_equal_to_whole_manifest(self):
        parts = A.assign(PLAN, NAMES)
        ev = [{"shard": s, "valid": True, "entry_names": ns} for s, ns in parts.items()]
        self.assertEqual(A.merge(PLAN, NAMES, ev), [])
        self.assertTrue(A.merge(PLAN, NAMES, ev[:2]))                      # a shard is missing
        ev2 = [dict(e) for e in ev]; ev2[2]["entry_names"] = ev2[2]["entry_names"][:1]
        self.assertTrue(A.merge(PLAN, NAMES, ev2))                         # an entry dropped
        ev3 = [dict(e) for e in ev]; ev3[0]["valid"] = False
        self.assertTrue(A.merge(PLAN, NAMES, ev3))
        self.assertTrue(A.merge(PLAN, NAMES + ["unit.test_added"], ev))    # manifest grew, nobody ran it

    def test_verify_cli_checks_only_its_shard_and_records_names(self):
        m, x = fixture([(n, native()) for n in ("unit.test_big", "unit.test_small")])
        full = {**m, "tests": m["tests"] + [{"name": "unit.test_other"}]}
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            plan = d / "plan.json"
            plan.write_text(json.dumps({"shards": [{"name": "a", "tests": ["unit.test_big", "unit.test_small"]},
                                                   {"name": "rest", "rest": True}]}))
            (d / "m.json").write_text(json.dumps(full))
            (d / "j.xml").write_bytes(ET.tostring(x))
            args = ["--manifest", str(d / "m.json"), "--junit", str(d / "j.xml"), "--preset", "dev",
                    "--output", str(d / "o.json"), "--shards-file", str(plan)]
            self.assertEqual(verify_main(args + ["--shard", "a"]), 0)
            out = json.loads((d / "o.json").read_text())
            self.assertEqual(out["entry_names"], ["unit.test_big", "unit.test_small"])
            self.assertEqual(out["shard"], "a")
            # the unsharded check still sees the third manifest entry as missing
            self.assertEqual(verify_main(args), 1)
            # the rest shard has an entry with no JUnit result
            self.assertEqual(verify_main(args + ["--shard", "rest"]), 1)


if __name__ == "__main__":
    unittest.main()
