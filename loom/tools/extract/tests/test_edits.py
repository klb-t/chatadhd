"""Slice B — batch edits on the package copy: preview = the diff of apply,
apply + undo = identity (tree hash), redo, the oplog, recipe steps, the
source snapshot never changes, redaction is irreversible and says so."""
import json

from _support import EXPORTS, EXTRACT, ExtractTest
from loom.tools.extract.datafiles import store
from loom.tools.extract.edits import EDITS


class EditsTest(ExtractTest):
    def _eng(self):
        eng = self.engine(EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip")
        eng.op("select.expr", {"expr": "kind:conversation"})
        eng.op("package.copy", {})
        return eng

    CASES = {
        "set": {"where": "conversation[service:anthropic]", "field": "account.label", "value": "konto-B"},
        "replace_regex": {"where": "message", "field": "text", "pattern": "(?i)baz[ay]", "replacement": "BAZA"},
        "map_table": {"where": "message", "field": "role", "table": {"tool": "narzędzie"}},
        "rename_key": {"where": "message", "in": "meta", "from": "recipient", "to": "do_kogo"},
        "delete_field": {"where": "message", "field": "model"},
        "add_field": {"where": "conversation", "field": "flags.przejrzane", "value": True},
        "flatten": {"where": "conversation", "path": "current"},
        "nest": {"where": "conversation[service:anthropic]"},
        "split_at_date": {"where": "conversation", "at": "2025-10-09T08:53:30Z"},
        "merge_conversations": {"where": "conversation[service:anthropic]", "title": "Claude razem"},
        "sort": {"where": "conversation", "by": "title"},
        "drop_blocks": {"where": "conversation", "kinds": ["thinking", "tool_use", "tool_result"]},
        "keep_blocks": {"where": "conversation", "kinds": ["text"]},
        "relabel_roles": {"where": "message", "map": {"tool": "assistant"}},
        "shift_time": {"where": "conversation", "by": "-2d"},
        "anonymise_time": {"where": "conversation", "origin": "package"},
        "redact": {"where": "conversation", "profile": "personal", "terms": ["Migracja"]},
    }

    def test_every_described_edit_is_implemented_and_tested(self):
        self.assertEqual(set(store().entries("edits")), set(EDITS))
        self.assertEqual(set(self.CASES), set(EDITS))

    def test_preview_equals_apply_and_undo_restores(self):
        for name, params in self.CASES.items():
            with self.subTest(edit=name):
                eng = self._eng()
                src_before = [u.content_sha256 for u in eng.units]
                h0 = eng.package.tree_hash()
                pv = eng.op(f"edit.{name}", dict(params, preview=True))
                self.assertEqual(eng.package.tree_hash(), h0, "a preview changes nothing")
                self.assertGreater(pv["count"], 0, name)
                ap = eng.op(f"edit.{name}", dict(params))
                self.assertEqual(ap["patch"], pv["patch"], "the preview is exactly what apply does")
                self.assertEqual(ap["after"], pv["after_preview"])
                self.assertNotEqual(eng.package.tree_hash(), h0)
                self.assertEqual([u.content_sha256 for u in eng.units], src_before, "edits never touch the source snapshot")
                self.assertEqual(eng.recipe["steps"][-1]["op"], f"edit.{name}")
                self.assertFalse(any(s.get("params", {}).get("preview") for s in eng.recipe["steps"]), "previews are not steps")
                if name == "redact":
                    with self.assertRaises(ValueError):
                        eng.op("undo", {})
                    self.assertEqual(eng.package.oplog[-1]["invertible"], "none")
                    continue
                eng.op("undo", {})
                self.assertEqual(eng.package.tree_hash(), h0, "apply + undo = identity")
                self.assertNotEqual(eng.recipe["steps"][-1]["op"], f"edit.{name}", "undo removes the step")
                eng.op("redo", {})
                self.assertEqual(eng.package.tree_hash(), ap["after"], "redo = apply again")

    def test_structure_edits_do_what_they_say(self):
        eng = self._eng()
        c1 = next(e for e in eng.package.entries if e.unit.ext_id == "c1-0000-aaaa")
        eng.op("edit.flatten", {"where": "conversation[title~/Migracja/]", "path": "current"})
        self.assertEqual(c1.unit.leaf_count(), 1)
        self.assertTrue(any("flatten" in n for n in c1.notes))
        n = len(eng.package.entries)
        eng.op("edit.merge_conversations", {"where": "conversation[service:anthropic]"})
        self.assertEqual(len(eng.package.entries), n - 1)
        merged = next(e for e in eng.package.entries if "merged_from" in e.unit.flags)
        times = [m.created_utc for m in merged.unit.messages]
        self.assertEqual(times, sorted(times))
        self.assertTrue(all(m.meta.get("from_conversation") for m in merged.unit.messages))
        eng.op("edit.sort", {"where": "conversation", "by": "title"})
        titles = [(e.unit.title or "").casefold() for e in eng.package.entries]
        self.assertEqual(titles, sorted(titles))
        eng.op("package.mkdir", {"path": "/agent"})
        eng.op("package.move", {"where": "conversation[service:anthropic]", "into": "/agent"})
        self.assertEqual({e.folder for e in eng.package.entries if e.unit.service == "anthropic"}, {"/agent"})
        eng.op("package.rename", {"item": c1.unit.ext_id, "title": "Nowa nazwa"})
        eng.op("package.remove", {"where": "conversation[service:openai AND messages:0]"})
        self.assertTrue(all(e.unit.messages for e in eng.package.entries if e.unit.service == "openai"))
        eng.op("cut", {"profiles": ["flat"]})
        eng.op("write", {"package": str(self.tmp / "pkg")})
        oplog = [json.loads(x) for x in (self.tmp / "pkg/oplog.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertEqual([o["op"] for o in oplog], ["edit.flatten", "edit.merge_conversations", "edit.sort"])
        self.assertTrue(all("_snapshot" not in o for o in oplog), "snapshots stay in memory, not in the package")
        man = json.loads((self.tmp / "pkg/manifest.json").read_text(encoding="utf-8"))
        self.assertEqual([e["op"] for e in man["edits"]], [o["op"] for o in oplog])
        rep = (self.tmp / "pkg/RAPORT.md").read_text(encoding="utf-8")
        self.assertIn("edit.merge_conversations", rep)
        convs = [json.loads(x) for x in (self.tmp / "pkg/flat/conversations.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertIn("Nowa nazwa", [c["title"] for c in convs])

    def test_package_recipe_replays_the_edits(self):
        eng = self._eng()
        eng.op("edit.relabel_roles", {"where": "message", "map": {"tool": "assistant"}})
        eng.op("cut", {"profiles": ["table"]})
        w1 = eng.op("write", {"package": str(self.tmp / "a")})
        eng2 = self.engine()
        rec = json.loads((self.tmp / "a/recipe.json").read_text(encoding="utf-8"))
        rec["steps"].append({"op": "write", "params": {"package": str(self.tmp / "b")}})
        eng2.run_recipe(rec)
        self.assertEqual(eng2.last_write["reproducible_hash"], w1["reproducible_hash"])
        self.assertNotIn(",tool,", (self.tmp / "b/table/messages.csv").read_text(encoding="utf-8"))

    def test_scope_errors_are_explicit(self):
        eng = self._eng()
        with self.assertRaises(ValueError):
            eng.op("edit.merge_conversations", {"where": "conversation[title~/Migracja/]"})
        with self.assertRaises(ValueError):
            eng.op("edit.set", {"where": "conversation", "field": "no_such_field", "value": 1})
        with self.assertRaises(ValueError):
            eng.op("edit.flatten", {"where": "message"})
