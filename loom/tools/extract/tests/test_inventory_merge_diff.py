"""Inventory (measured, with 'how'), merge (duplicates, versions), diff (new since last export)."""
import json

from _support import EXPECTED_EXTRACT as E, EXPORTS, EXTRACT, ExtractTest
from loom.tools.extract import diff as _diff
from loom.tools.extract.inventory import build_inventory
from loom.tools.extract.model import Unit


class InventoryTest(ExtractTest):
    def test_inventory_counts_and_how(self):
        eng, units = self.units(EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip")
        inv = build_inventory(eng.archives, units, eng.merge_result, eng.ds)
        self.assertEqual(inv["schema"], "loom.extract.inventory/1")
        self.assertEqual(inv["conversations"], 9)
        self.assertEqual(inv["messages"]["total"], 53)
        self.assertEqual(len(inv["archives"]), 2)
        for key in ("archives", "units", "blocks", "unknown"):
            self.assertIn(key, inv["how"], f"'{key}' says how it was counted")
        self.assertIn("tool_use", inv["blocks"]["common"])
        self.assertGreater(inv["tools"]["calls"], 0)
        self.assertGreaterEqual(inv["attachments"]["referenced"], inv["attachments"]["resolved"] + inv["attachments"]["missing"])
        self.assertEqual(inv["attachments"]["missing"], len(inv["attachments"]["missing_list"]))
        self.assertGreater(inv["attachments"]["missing"], 0, "missing attachments are listed, never silent")
        self.assertTrue(inv["dates"]["from"] <= inv["dates"]["to"])
        for fid, entry in inv["absent"].items():
            self.assertIn("known_absent", entry)
        a = inv["archives"][0]
        self.assertEqual(a["members"]["unknown"], len(a["members"]["unknown_list"]))
        self.assertTrue(all(m["why"] for m in a["members"]["unknown_list"]))

    def test_inventory_aggregate_has_no_paths_or_names(self):
        eng, units = self.units(EXTRACT / "gemini/takeout.zip", EXTRACT / "mixed_dir")
        inv = build_inventory(eng.archives, units, eng.merge_result, eng.ds, aggregate=True)
        text = json.dumps(inv, ensure_ascii=False)
        for forbidden in E["forbidden_in_aggregate"] + ["unknown.bin", "notes.md", "/home/"]:
            self.assertNotIn(forbidden, text)
        self.assertTrue(inv["aggregate"])
        self.assertEqual(inv["archives"][0]["path"], None)

    def test_unknown_block_kinds_are_kept_with_an_example(self):
        eng, units = self.units(EXPORTS / "openai_2026_sharded.zip")
        inv = build_inventory(eng.archives, units, eng.merge_result, eng.ds)
        self.assertTrue(inv["unknown"]["by_native"])
        self.assertTrue(inv["unknown"]["examples"])


class MergeTest(ExtractTest):
    def test_two_exports_of_one_account_versions_and_duplicates(self):
        eng, units = self.units(EXTRACT / "versions/chatgpt_2025-06-20.zip", EXTRACT / "versions/chatgpt_2025-10-01.zip")
        o = E["versions"]["merge_both"]
        d = eng.merge_result.as_dict()
        self.assertEqual((d["identities"], d["duplicates"], d["versions"]), (o["identities"], o["duplicates"], o["versions"]))
        c2 = next(u for u in units if u.ext_id == "c2")
        self.assertEqual(len(c2.versions), 1, "the older export is listed, not discarded")
        self.assertEqual(len(c2.messages), 4, "the newest version is active")
        self.assertEqual(c2.versions[0]["messages"], 2)
        c1 = next(u for u in units if u.ext_id == "c1")
        self.assertEqual(len(c1.duplicates), 1)
        self.assertEqual(sorted(u.ext_id for u in units if u.kind == "conversation"), ["c1", "c2", "c3", "c4"])

    def test_exact_duplicate_archive(self):
        eng, units = self.units(EXTRACT / "versions/chatgpt_2025-06-20.zip", EXTRACT / "duplicate/chatgpt_2025-06-20_copy.zip")
        o = E["duplicate"]
        d = eng.merge_result.as_dict()
        self.assertEqual((d["identities"], d["duplicates"], d["versions"]), (o["identities"], o["duplicates"], 0))

    def test_attachments_resolved_and_missing_in_the_new_export(self):
        eng, units = self.units(EXTRACT / "versions/chatgpt_2025-10-01.zip")
        o = E["versions"]["attachments_new"]
        atts = [a for u in units if u.kind == "conversation" for a in u.attachments]
        self.assertEqual(len(atts), o["referenced"])
        self.assertEqual(sum(1 for a in atts if a.status == "resolved"), o["resolved"])
        self.assertEqual(sum(1 for a in atts if a.status == "missing"), o["missing"])
        resolved = next(a for a in atts if a.status == "resolved")
        self.assertTrue(resolved.member and resolved.sha256)


class DiffTest(ExtractTest):
    def _old_new(self):
        e1, old = self.units(EXTRACT / "versions/chatgpt_2025-06-20.zip")
        e2, new = self.units(EXTRACT / "versions/chatgpt_2025-10-01.zip")
        return old, new

    def test_new_changed_removed_unchanged(self):
        old, new = self._old_new()
        d = _diff.diff_units(old, new)
        o = E["versions"]["diff"]
        for k in ("new", "changed", "removed", "unchanged", "messages_added", "messages_changed", "messages_removed"):
            self.assertEqual(d["counts"][k], o[k], k)
        ch = d["changed"][0]
        self.assertEqual(ch["ext_id"], o["title_changed"][0])
        self.assertTrue(ch["title_changed"])
        self.assertEqual(ch["new_attachments"], o["new_attachments"]["c2"])
        self.assertEqual(ch["messages"]["ids"]["added"], ["c2-m2", "c2-m3"])
        self.assertEqual([r["ext_id"] for r in d["removed"]], ["c3"], "removed is listed, never skipped")
        self.assertEqual([n["ext_id"] for n in d["new_units"]], ["c4"])

    def test_select_modes_and_summary_only(self):
        old, new = self._old_new()
        d = _diff.diff_units(old, new)
        by_ext = {u.ext_id: u.uid for u in new}
        self.assertEqual(_diff.select_uids(d, "only_new")[0], {by_ext["c4"]})
        self.assertEqual(_diff.select_uids(d, "changed")[0], {by_ext["c2"]})
        self.assertEqual(_diff.select_uids(d, "all")[0], {by_ext["c4"], by_ext["c2"]})
        self.assertEqual(_diff.select_uids(d, "removed")[0], {u.uid for u in old if u.ext_id == "c3"})
        s = _diff.diff_units(old, new, summary_only=True)
        self.assertEqual(s["new_units"], 1)
        with self.assertRaises(ValueError):
            _diff.select_uids(s, "only_new")
        with self.assertRaises(ValueError):
            _diff.select_uids(d, "bogus")

    def test_content_hash_identity_and_aggregate(self):
        old, new = self._old_new()
        d = _diff.diff_units(old, new, identity="content_hash", aggregate=True)
        self.assertEqual(d["counts"]["changed"], 0, "without ext ids a changed conversation is new + removed")
        self.assertEqual(d["counts"]["new"], 2)
        self.assertEqual(d["counts"]["removed"], 2)
        self.assertTrue(all(e["title"] is None for e in d["new_units"]))

    def test_diff_against_a_package_snapshot_and_in_the_engine(self):
        eng = self.engine(EXTRACT / "versions/chatgpt_2025-06-20.zip")
        eng.op("write", {"package": str(self.tmp / "old_pkg")})
        eng2 = self.engine(EXTRACT / "versions/chatgpt_2025-10-01.zip")
        r = eng2.op("diff", {"against": [str(self.tmp / "old_pkg")], "mode": "only_new"})
        self.assertEqual(r["diff"]["old"]["how"], "snapshot:units.jsonl")
        self.assertEqual(r["counts"]["new"], 1)
        self.assertEqual(r["selection"]["after"], 1)
        r2 = eng2.op("select.diff", {"mode": "changed", "combine": "add"})
        self.assertEqual(r2["counts"]["matched"], 1)
        self.assertEqual(len(eng2.selection.units), 2)
        lines = (self.tmp / "old_pkg" / "units.jsonl").read_text(encoding="utf-8").splitlines()
        self.assertEqual(Unit.from_dict(json.loads(lines[0])).uid, json.loads(lines[0])["uid"])
