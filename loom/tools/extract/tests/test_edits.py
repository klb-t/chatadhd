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
        "nest": {"where": "conversation[service:anthropic]", "by": "every_n", "n": 2, "on_forks": "replace"},
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

    def test_nest_builds_a_tree_by_rule_and_never_flattens_forks_silently(self):
        """F6 (§5.12): nest = list → tree by a parent field, every N messages or by date; a conversation that
        already has forks (the branched c1 fixture) is refused, skipped or re-nested on purpose, with a note."""
        eng = self._eng()
        c1 = next(e for e in eng.package.entries if e.unit.ext_id == "c1-0000-aaaa")
        c2 = next(e for e in eng.package.entries if e.unit.ext_id == "c2-0000-bbbb")
        h0 = eng.package.tree_hash()
        with self.assertRaises(ValueError) as cm:
            eng.op("edit.nest", {"where": "conversation[title~/Migracja/]"})
        self.assertIn("forks", str(cm.exception))
        self.assertEqual(eng.package.tree_hash(), h0, "a refused nest changes nothing")
        r = eng.op("edit.nest", {"where": "conversation[title~/Migracja/]", "on_forks": "skip"})
        self.assertEqual((r["count"], r["result"]["skipped_forked"]), (0, 1))
        self.assertEqual(c1.unit.leaf_count(), 4, "a skipped conversation keeps its branches")
        n_msgs = len(c2.unit.messages)
        r = eng.op("edit.nest", {"where": "conversation[title~/Wyszukiwanie/]", "by": "every_n", "n": 3})
        groups = (n_msgs + 2) // 3
        self.assertEqual(r["result"]["groups"], groups)
        self.assertEqual(len(c2.unit.messages), n_msgs, "nest keeps every message")
        self.assertEqual(c2.unit.leaf_count(), groups, "each group is a branch")
        self.assertEqual(len(c2.unit.tree["forks"]), groups - 1)
        self.assertTrue(c2.unit.messages[-1].on_current_path or any(m.on_current_path for m in c2.unit.messages))
        self.assertTrue(any("nest(by=every_n)" in n for n in c2.notes))
        eng.op("undo", {})
        self.assertEqual(c2.unit.leaf_count() if c2 in eng.package.entries else
                         next(e for e in eng.package.entries if e.unit.ext_id == "c2-0000-bbbb").unit.leaf_count(), 1)
        r = eng.op("edit.nest", {"where": "conversation[title~/Migracja/]", "by": "date", "on_forks": "replace"})
        self.assertEqual(r["count"], 1)
        c1 = next(e for e in eng.package.entries if e.unit.ext_id == "c1-0000-aaaa")
        self.assertEqual(len(c1.unit.messages), 14, "re-nesting keeps every message of the branched conversation")

    def test_merge_keeps_a_partial_selection_and_attachment_links(self):
        """F5: a message-grain entry stays partial after the merge (its messages, re-identified), and every
        attachment's message link follows the new ids."""
        eng = self.engine(EXPORTS / "anthropic_2026_full.zip")
        eng.op("select.expr", {"expr": "role:user", "kind": "message"})
        eng.op("package.copy", {})
        ents = [e for e in eng.package.entries if e.unit.service == "anthropic"]
        self.assertTrue(all(e.messages is not None for e in ents), "the selection is message-grain")
        want = sum(len(e.messages) for e in ents)
        n_att = sum(len(e.unit.attachments) for e in ents)
        eng.op("edit.merge_conversations", {"where": "conversation[service:anthropic]"})
        merged = next(e for e in eng.package.entries if "merged_from" in e.unit.flags)
        self.assertIsNotNone(merged.messages, "a partial entry is not widened to every message")
        self.assertEqual(len(merged.messages), want)
        ids = {m.id for m in merged.unit.messages}
        self.assertTrue(merged.messages <= ids)
        self.assertEqual(len(merged.unit.attachments), n_att)
        self.assertTrue(all(a.message is None or a.message in ids for a in merged.unit.attachments), "attachment links follow the new ids")

    def test_dropped_attachment_blocks_leave_the_outputs(self):
        """F4: drop_blocks on 'attachment' also takes the conversation's attachment entries of the messages in
        scope: nothing is re-added as metadata and no file is copied."""
        def counts(eng, name):
            eng.op("cut", {"profiles": ["flat"]})
            eng.op("write", {"package": str(self.tmp / name)})
            rows = [json.loads(x) for x in (self.tmp / name / "flat/messages.jsonl").read_text(encoding="utf-8").splitlines()]
            files = [p for p in (self.tmp / name).rglob("*") if p.is_file() and "attachments" in p.relative_to(self.tmp / name).parts[:1]]
            return sum(len(r.get("attachments") or []) for r in rows), len(files), eng.cut_info["attachments"]["copied"]
        eng = self._eng()
        before = counts(eng, "a")
        self.assertGreater(before[0], 0)
        self.assertGreater(before[1], 0)
        r = eng.op("edit.drop_blocks", {"where": "conversation", "kinds": ["attachment"]})
        self.assertGreater(r["result"]["attachments"], 0)
        after = counts(eng, "b")
        self.assertEqual(after, (0, 0, 0), f"before {before}")

    def test_output_field_names_can_be_renamed(self):
        """F10: 'nazw' in batch edits — a model field's output name (text → content): rename_key with in: "" (an
        alias on the package entry, invertible, in the oplog) and a profile field entry with "as"."""
        eng = self._eng()
        r = eng.op("edit.rename_key", {"where": "message", "in": "", "from": "text", "to": "content"})
        self.assertGreater(r["count"], 0)
        eng.op("cut", {"profiles": ["flat", "table"]})
        eng.op("write", {"package": str(self.tmp / "pkg")})
        rows = [json.loads(x) for x in (self.tmp / "pkg/flat/messages.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertTrue(rows and all("content" in r and "text" not in r for r in rows))
        header = (self.tmp / "pkg/table/messages.csv").read_text(encoding="utf-8").splitlines()[0].split(",")
        if "text" in (store().entry("profiles", "table").get("fields") or {}).get("message", []):
            self.assertIn("content", header)
            self.assertNotIn("text", header)
        oplog = [json.loads(x) for x in (self.tmp / "pkg/oplog.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertEqual(oplog[-1]["op"], "edit.rename_key")
        h = eng.package.tree_hash()
        eng.op("undo", {})
        self.assertNotEqual(eng.package.tree_hash(), h)
        self.assertTrue(all(not e.aliases for e in eng.package.entries))
        prof = dict(store().entry("profiles", "flat"))
        prof["id"] = "flat_alias"
        prof["fields"] = {"conversation": ["key", "title"], "message": ["id", {"field": "text", "as": "content"}]}
        prof["outputs"] = [{"kind": "jsonl", "path": "fa/messages.jsonl", "rows": "messages"}]
        eng.op("cut", {"profiles": [prof]})
        eng.op("write", {"package": str(self.tmp / "pkg2")})
        rows = [json.loads(x) for x in (self.tmp / "pkg2/fa/messages.jsonl").read_text(encoding="utf-8").splitlines()]
        self.assertTrue(rows and all(set(r) == {"id", "content"} for r in rows))

    def test_flatten_all_paths_and_add_field_expression(self):
        """F18 (§5.12): flatten all_paths = one list per path; add_field with an expression."""
        eng = self._eng()
        n = len(eng.package.entries)
        eng.op("edit.flatten", {"where": "conversation[title~/Migracja/]", "path": "all_paths"})
        parts = [e for e in eng.package.entries if e.unit.uid.endswith(("~p1", "~p2", "~p3", "~p4"))]
        self.assertEqual(len(parts), 4)
        self.assertEqual(len(eng.package.entries), n + 3)
        self.assertTrue(all(p.unit.leaf_count() == 1 for p in parts))
        eng.op("edit.add_field", {"where": "conversation", "field": "flags.etykieta", "expression": "{title} / {account.service}"})
        e = next(e for e in eng.package.entries if e.unit.ext_id == "c2-0000-bbbb")
        self.assertEqual(e.unit.flags["etykieta"], "Wyszukiwanie w sieci / openai")

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
