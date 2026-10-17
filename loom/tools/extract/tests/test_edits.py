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
        "move_attachments": {"where": "conversation", "to": "media/{key}"},
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
        # slice C: the package's structure ops (an explicit copy, folder, move, rename, remove) are edits too —
        # logged, undoable and listed in the manifest (nothing leaves the package without an entry)
        self.assertEqual([o["op"] for o in oplog], ["package.copy", "edit.flatten", "edit.merge_conversations", "edit.sort",
                                                     "package.mkdir", "package.move", "package.rename", "package.remove"])
        self.assertTrue(all(o["lines"] and "pkgop." not in o["lines"][0] for o in oplog), oplog)
        self.assertTrue(all("_snapshot" not in o for o in oplog), "snapshots stay in memory, not in the package")
        man = json.loads((self.tmp / "pkg/manifest.json").read_text(encoding="utf-8"))
        self.assertEqual([e["op"] for e in man["edits"]], [o["op"] for o in oplog])
        rep = (self.tmp / "pkg/RAPORT.md").read_text(encoding="utf-8")
        self.assertIn("edit.merge_conversations", rep)
        convs = [json.loads(x) for x in (self.tmp / "pkg/flat/conversations.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertIn("Nowa nazwa", [c["title"] for c in convs])
        self.assertEqual([c["uid"] for c in convs], [e.unit.uid for e in eng.package.entries], "the cut keeps the package order (edit.sort)")

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

    def test_every_edit_replays_from_a_recipe_file_in_another_directory(self):
        """F19: a field param (``title``, ``account.label``) is a field name, never a path joined with the recipe's
        directory; ``przepis uruchom`` from a file elsewhere gives the same package tree as the run that wrote it."""
        import contextlib
        import io
        import shutil
        from loom.tools.extract import cli
        for name, params in self.CASES.items():
            with self.subTest(edit=name):
                eng = self._eng()
                eng.op(f"edit.{name}", dict(params))
                eng.op("write", {"package": str(self.tmp / "a" / name)})
                rec = json.loads((self.tmp / "a" / name / "recipe.json").read_text(encoding="utf-8"))
                step = next(s for s in rec["steps"] if s["op"] == f"edit.{name}")
                if "field" in params:
                    self.assertEqual(step["params"]["field"], params["field"], "a field name stays a field name")
                elsewhere = self.tmp / "elsewhere" / name
                elsewhere.mkdir(parents=True)
                shutil.copyfile(self.tmp / "a" / name / "recipe.json", elsewhere / "r.json")
                out, err = io.StringIO(), io.StringIO()
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    code = cli.main(["przepis", "uruchom", str(elsewhere / "r.json"), "--pakiet", str(self.tmp / "b" / name)])
                self.assertEqual(code, 0, err.getvalue() + out.getvalue())
                a = json.loads((self.tmp / "a" / name / "package.json").read_text(encoding="utf-8"))
                b = json.loads((self.tmp / "b" / name / "package.json").read_text(encoding="utf-8"))
                self.assertEqual(b, a, f"edit.{name} replays to the same package tree")

    def test_moved_attachments_are_where_the_links_point(self):
        eng = self._eng()
        eng.op("edit.move_attachments", {"where": "conversation[title~/Obrazy/]", "to": "media/{key}"})
        eng.op("cut", {"profiles": ["text"]})
        eng.op("write", {"package": str(self.tmp / "pkg")})
        pkg = self.tmp / "pkg"
        moved = sorted(p.relative_to(pkg).as_posix() for p in (pkg / "attachments/media").rglob("*") if p.is_file())
        self.assertTrue(moved)
        md = next(p.read_text(encoding="utf-8") for p in (pkg / "text").glob("0*.md") if "Obrazy" in p.read_text(encoding="utf-8"))
        link = md.split("](", 1)[1].split(")", 1)[0]
        self.assertIn("media/", link)
        self.assertTrue((pkg / "text" / link).resolve().exists())

    def test_scope_errors_are_explicit(self):
        eng = self._eng()
        with self.assertRaises(ValueError):
            eng.op("edit.merge_conversations", {"where": "conversation[title~/Migracja/]"})
        with self.assertRaises(ValueError):
            eng.op("edit.set", {"where": "conversation", "field": "no_such_field", "value": 1})
        with self.assertRaises(ValueError):
            eng.op("edit.flatten", {"where": "message"})


class UserEditTest(ExtractTest):
    def test_user_edit_needs_explicit_permission_and_is_undoable(self):
        from loom.tools.extract.engine import EngineError
        ov = store().overlay
        (ov / "edits").mkdir(parents=True)
        (ov / "user").mkdir(parents=True)
        (ov / "edits/upper_titles.json").write_text(json.dumps({
            "schema": "loom.extract.edit/1", "id": "upper_titles", "module": "user:upper", "label": {"pl": "Tytuły wielkimi", "en": "Upper titles"},
            "scope": ["conversation"], "params": {"where": {"type": "expr", "default": "conversation"}}, "invertible": "snapshot"}), encoding="utf-8")
        (ov / "user/upper.py").write_text(
            "def edit(pkg, kind, items, params, eng):\n"
            "    n = 0\n"
            "    for it in items:\n"
            "        if it.unit.title:\n"
            "            it.unit.title = it.unit.title.upper()\n"
            "            n += 1\n"
            "    return {'count': n}\n", encoding="utf-8")
        eng = self.engine(EXPORTS / "anthropic_2026_full.zip")
        eng.op("select.expr", {"expr": "kind:conversation"})
        with self.assertRaises(EngineError):
            eng.op("edit.user", {"edit": "upper_titles"})
        eng.settings["allow_user_code"] = True
        h0 = eng.ensure_package().tree_hash()
        r = eng.op("edit.user", {"edit": "upper_titles"})
        self.assertGreater(r["count"], 0)
        self.assertIn("WIDOK LISTY", [e.unit.title for e in eng.package.entries])
        self.assertEqual(eng.recipe["steps"][-1]["op"], "edit.user")
        eng.op("undo", {})
        self.assertEqual(eng.package.tree_hash(), h0)
        self.assertNotEqual(eng.recipe["steps"][-1]["op"], "edit.user", "undo removes the user edit's step")
        eng.op("redo", {})
        self.assertIn("WIDOK LISTY", [e.unit.title for e in eng.package.entries])
        rec = eng.normalised_recipe()
        self.assertTrue(rec["settings"]["allow_user_code"], "the recipe says it runs user code")
        again = self.engine()
        again.run_recipe(rec)
        self.assertIn("WIDOK LISTY", [e.unit.title for e in again.package.entries])
