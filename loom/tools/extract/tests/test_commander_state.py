"""X-Port Commander without a terminal: ``CommanderState`` driven by key lists (keys taken from
keymap.json by action, so a changed keymap does not break these tests).  Navigation, panes, views
and flat lists, selection (toggle/invert/expression/similar), live filter after every key, search,
go to, F3 viewer, compare, F5/F6/F7/F8/rename on the package, batch edit F4 with preview → apply,
undo/redo, bookmarks, tags, saved views, recording (navigation is never a step), autosave after
every step and resume, a background job stopped with Esc (state kept), the cost question answered
"no" by default, the vim profile, help generated from the keymap, replay of a recipe."""
import json
import threading
import time
import unittest

from _support import EXPORTS, ExtractTest

from loom.tools.extract import ops as _ops
from loom.tools.extract.api import Session
from loom.tools.extract.commander.driver import Driver
from loom.tools.extract.commander.keymap import Keymap
from loom.tools.extract.commander.state import CommanderState, parse_keys
from loom.tools.extract.datafiles import store

SRC = [str(EXPORTS / "openai_2026_sharded.zip"), str(EXPORTS / "anthropic_2026_full.zip")]


class CommanderTest(ExtractTest):
    def setUp(self) -> None:
        super().setUp()
        self.profile = "fkeys"

    def make(self, profile: str = "fkeys", autosave=None, background: bool = True, sources=SRC):
        self.profile = profile
        s = Session(face="tui", ds=store())
        self.addCleanup(s.close)
        if sources:
            s.call({"call": "open", "sources": list(sources)})
        st = CommanderState(s, profile=profile, autosave=autosave, background=background, width=140, height=40)
        self.km = st.km
        self.d = Driver(st)
        self.d.settle()
        return st

    def k(self, action: str) -> list[str]:
        keys = self.km.keys_for(action)
        self.assertTrue(keys, f"no key for {action} in {self.km.profile}")
        return keys[0].split()

    def act(self, *actions: str) -> None:
        for a in actions:
            self.d.press(*self.k(a))

    def typed(self, text: str, then: str = "Enter") -> None:
        self.d.type(text)
        if then:
            self.d.press(then)

    def steps(self, st) -> list[dict]:
        return st.s.eng.recipe["steps"]

    def cursor_to(self, st, label: str) -> None:
        rows = st.pane.rows
        i = next(i for i, r in enumerate(rows) if r["label"] == label)
        st.pane.cursor = i

    # -- navigation, views ------------------------------------------------------------------------
    def test_navigation_panes_views_are_not_recorded(self):
        st = self.make()
        n0 = len(self.steps(st))
        self.assertEqual([r["kind"] for r in st.pane.rows], ["archive", "archive"])
        self.act("open")
        self.assertEqual(len(st.pane.rows), 7)
        self.act("cursor.down", "cursor.down", "bottom", "top")
        self.assertEqual(st.pane.cursor, 0)
        self.act("open")  # the first conversation has 4 branches
        self.assertEqual([r["kind"] for r in st.pane.rows][:4], ["branch"] * 4)
        self.act("parent")
        self.assertEqual(st.pane.path, ["arch:1"])
        self.act("parent")
        self.assertEqual(st.pane.path, [])
        self.act("mode.flat")
        self.assertEqual(st.pane.flat, "conversation")
        self.act("mode.flat")
        self.assertEqual(st.pane.flat, "message")
        self.assertEqual(len(st.pane.rows), sum(len(u.messages) for u in st.s.eng.conversations))
        self.act("sort.cycle")
        self.assertEqual(st.pane.sort, "created")
        self.act("root.next")
        self.assertEqual(st.pane.root, "package")
        self.act("pane.switch")
        self.assertEqual(st.active, 1)
        self.assertEqual(len(self.steps(st)), n0, "navigation never adds a recipe step")
        screen = self.d.screen()
        self.assertIn("Pakiet", screen)
        self.assertIn("zazn.:", screen, "status line with the selection counter")
        self.assertIn("5Kopiuj", screen.replace(" ", ""), "F-key hints from the keymap")

    def test_live_filter_narrows_after_every_key_and_esc_restores(self):
        st = self.make()
        self.act("mode.flat")  # all conversations
        total = len(st.pane.rows)
        self.act("filter.live")
        seen = []
        for ch in "migr":
            self.d.press(ch)
            seen.append(len(st.pane.rows))
        self.assertEqual(seen[-1], 1)
        self.assertTrue(all(a >= b for a, b in zip(seen, seen[1:])), "incremental: shown after every key")
        self.d.press("Esc")
        self.assertEqual(len(st.pane.rows), total, "Esc gives the previous filter back")
        self.act("filter.live")
        self.typed("count(tool_use) >= 2")
        self.assertTrue(st.pane.rows and len(st.pane.rows) < total)
        self.assertEqual(st.pane.data["filter"]["how"], "expr")

    def test_selection_keys_record_steps_by_tui_with_reasons(self):
        st = self.make()
        self.act("open")
        self.act("select.toggle")
        self.assertEqual(st.pane.cursor, 1, "Insert moves on")
        self.assertEqual(st.status_data["selection"]["conversations"], 1)
        self.assertEqual(st.pane.rows[0].get("sel"), "all")
        self.act("select.invert")
        self.assertEqual(st.status_data["selection"]["conversations"], len(st.s.eng.conversations) - 1)
        self.act("select.clear", "select.expr")
        self.typed("title~/migracja|wyszukiwanie/i")
        self.assertEqual(st.status_data["selection"]["conversations"], 2)
        self.act("deselect.expr")
        self.typed("title~/wyszukiwanie/i")
        self.assertEqual(st.status_data["selection"]["conversations"], 1)
        self.act("selection.show")
        self.assertEqual(st.mode, "viewer")
        self.assertTrue(any("expr:" in line for line in st.overlay["lines"]), "a reason per item")
        self.d.press("Esc")
        self.act("select.similar")
        self.assertEqual(st.mode, "normal")
        ops = [s["op"] for s in self.steps(st) if s.get("note") != "implicit"]
        self.assertEqual(ops[:5], ["select.items", "select.invert", "select.clear", "select.expr", "select.expr"])
        self.assertTrue(all(s["by"] == "tui" for s in self.steps(st) if s.get("note") != "implicit"))

    def test_search_goto_viewer_compare(self):
        st = self.make()
        self.act("search")
        self.typed("postgresql")
        self.assertTrue(st.hits)
        cur = st.pane.current()
        self.assertEqual(cur["id"], st.hits[0]["id"], "the cursor lands on the hit, the pane opened where it is")
        self.act("search.next")
        self.assertIn("trafienie", st.message)
        uid = next(u.uid for u in st.s.eng.conversations if u.title == "Wyszukiwanie w sieci")
        self.act("goto")
        self.typed(uid[7:19])
        self.assertEqual(st.pane.current()["id"], uid)
        self.act("view")
        self.assertEqual(st.mode, "viewer")
        self.assertIn('"title": "Wyszukiwanie w sieci"', "\n".join(st.overlay["lines"]))
        self.act("view.cycle")
        self.assertEqual(st.overlay["title"].split(" ")[0], "markdown")
        self.d.press("Esc")
        self.assertEqual(st.mode, "normal")
        self.act("diff.nodes", "cursor.up", "diff.nodes")
        self.assertEqual(st.mode, "viewer")
        self.assertTrue(any("/title" in line for line in st.overlay["lines"]))
        self.d.press("Esc")
        self.act("help")
        text = "\n".join(st.overlay["lines"])
        for aid in self.km.bindings.values():
            from loom.tools.extract.i18n import label
            self.assertIn(label(st.actions[aid]["label"]), text, aid)

    def test_package_keys_copy_move_mkdir_rename_remove_undo_redo(self):
        st = self.make()
        self.act("open")
        self.cursor_to(st, "Migracja bazy danych")
        self.act("copy")
        self.cursor_to(st, "Wyszukiwanie w sieci")
        self.act("copy")
        pkg = st.s.eng.package
        self.assertEqual(len(pkg.entries), 2)
        self.act("pane.switch")
        self.assertEqual(st.pane.root, "package")
        self.assertEqual(len(st.pane.rows), 2)
        self.act("mkdir")
        self.typed("projekty")
        self.assertIn("/projekty", pkg.folders)
        self.cursor_to(st, "Migracja bazy danych")
        self.act("move")
        self.d.press("C-u")
        self.typed("/projekty")
        self.assertEqual({e.folder for e in pkg.entries if e.unit.title == "Migracja bazy danych"}, {"/projekty"})
        self.cursor_to(st, "projekty")
        self.act("open")
        self.act("rename")
        self.d.press("C-u")
        self.typed("Migracja → PostgreSQL")
        self.assertEqual(st.pane.rows[0]["label"], "Migracja → PostgreSQL")
        self.act("parent")
        self.cursor_to(st, "Wyszukiwanie w sieci")
        self.act("remove")
        self.assertEqual(len(pkg.entries), 1)
        ops = [s["op"] for s in self.steps(st) if s.get("note") != "implicit"]
        self.assertEqual(ops, ["package.copy", "package.copy", "package.mkdir", "package.move", "package.rename", "package.remove"])
        self.act("undo")
        self.assertEqual(len(pkg.entries), 2)
        self.act("undo", "undo")
        self.assertEqual({e.folder for e in pkg.entries}, {"/"})
        self.act("redo")
        self.assertEqual({e.folder for e in pkg.entries if e.unit.title == "Migracja bazy danych"}, {"/projekty"})
        # vim-style yank/paste: clipboard → package
        self.act("pane.switch")
        self.cursor_to(st, "Obrazy i dźwięk")
        self.act("yank")
        self.assertEqual(len(st.clipboard), 1)
        self.act("paste")
        self.assertIn("Obrazy i dźwięk", {e.unit.title for e in pkg.entries})

    def test_batch_edit_preview_apply_undo(self):
        st = self.make()
        self.act("open")
        self.cursor_to(st, "Migracja bazy danych")
        self.act("copy")
        h0 = st.s.eng.package.tree_hash()
        self.act("edit")
        self.assertEqual(st.mode, "menu")
        self.d.type("relabel_roles")
        self.d.press("Enter")
        self.assertEqual(st.mode, "prompt")
        self.assertTrue(st.prompt["label"].startswith("relabel_roles.where"))
        self.d.press("Enter")  # where: the preset (all messages of the package)
        self.typed("user=człowiek")  # map
        self.assertEqual(st.mode, "preview")
        self.assertTrue(any("role" in line for line in st.overlay["lines"]))
        self.assertEqual(st.s.eng.package.tree_hash(), h0, "the preview changed nothing")
        self.d.press("Esc")
        self.assertEqual(st.s.eng.package.tree_hash(), h0)
        self.act("edit")
        self.d.type("relabel_roles")
        self.d.press("Enter", "Enter")
        self.typed("user=człowiek")
        self.d.press("Enter")
        self.assertNotEqual(st.s.eng.package.tree_hash(), h0)
        last = self.steps(st)[-1]
        self.assertEqual((last["op"], last["by"], last["params"]["map"]), ("edit.relabel_roles", "tui", {"user": "człowiek"}))
        self.act("undo")
        self.assertEqual(st.s.eng.package.tree_hash(), h0)

    def test_bookmark_tag_saved_views_and_command_line(self):
        st = self.make()
        self.act("open")
        self.cursor_to(st, "Migracja bazy danych")
        uid = st.pane.current()["id"]
        self.act("bookmark")
        self.assertTrue(st.pane.current().get("bookmark"))
        self.act("tag")
        self.typed("chatadhd")
        self.assertEqual(st.pane.current().get("tags"), ["chatadhd"])
        self.assertEqual(st.s.eng.recipe["tags"], {uid: ["chatadhd"]})
        st.pane.filter = "migr"
        self.act("view.save")
        self.typed("moj widok")
        st.panes[0].__init__(root="timeline")
        self.act("view.load")
        self.d.press("Enter")
        self.assertEqual((st.panes[0].root, st.panes[0].filter), ("sources", "migr"))
        self.act("command")
        self.typed("sel tag:chatadhd")
        self.assertEqual(st.status_data["selection"]["conversations"], 1)
        self.act("command")
        self.typed('call status {}')
        self.assertEqual(st.mode, "viewer")

    def test_autosave_after_every_step_and_resume(self):
        path = self.tmp / "komander.autosave.json"
        st = self.make(autosave=path)
        self.act("open")
        self.cursor_to(st, "Migracja bazy danych")
        self.act("select.toggle")
        self.assertTrue(path.exists())
        doc = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(doc["recipe"]["steps"][-1]["op"], "select.items")
        self.act("cursor.up", "copy")
        doc = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(doc["recipe"]["steps"][-1]["op"], "package.copy")
        self.assertEqual(doc["panes"][0]["path"], ["arch:1"])
        writes = st.autosave.writes
        self.act("cursor.down", "cursor.up")
        self.assertEqual(st.autosave.writes, writes, "navigation does not rewrite the autosave")
        # a "crash": a new commander resumes from the file
        st2 = self.make(autosave=path, sources=None)
        st2.resume(st2.autosave.load())
        self.d.settle()
        self.assertEqual([(s["op"], s["by"]) for s in st2.s.eng.recipe["steps"] if s.get("note") != "implicit"],
                         [(s["op"], s["by"]) for s in st.s.eng.recipe["steps"] if s.get("note") != "implicit"])
        self.assertEqual(len(st2.s.eng.package.entries), 1)
        self.assertEqual(st2.panes[0].path, ["arch:1"])

    def test_background_job_esc_keeps_state_and_cost_question_defaults_to_no(self):
        st = self.make()
        started = threading.Event()

        def slow(eng, p):
            for i in range(1000):
                eng.emit("progress", done=i, total=1000)
                started.set()
                time.sleep(0.01)
            return {}
        orig = _ops.OPS["clusters"]
        _ops.OPS["clusters"] = slow
        self.addCleanup(lambda: _ops.OPS.__setitem__("clusters", orig))
        n = len(self.steps(st))
        st._run_op("clusters")
        self.assertIsNotNone(st.job)
        self.assertTrue(started.wait(10))
        st.tick()
        self.d.press(*self.k("cursor.down"), settle=False)  # the screen answers keys while the engine works
        self.assertEqual(st.pane.cursor, 1)
        self.assertIn("⏳", st.status_line())
        self.d.press("Esc", settle=False)
        self.d.settle(timeout=10)
        self.assertIsNone(st.job)
        self.assertIn("przerwano", st.message)
        self.assertEqual(len(self.steps(st)), n)

        answers = []

        def paid(eng, p):
            answers.append(eng.ask({"kind": "estimate", "estimate": {"calls": 2, "total": {"amount": 0.5, "currency": "USD", "known": True}}}))
            return {}
        _ops.OPS["clusters"] = paid
        st._run_op("clusters")
        self.d.settle(timeout=10)
        self.assertEqual(st.mode, "question")
        self.assertIn("2", "\n".join(st.overlay["lines"]))
        self.d.press("Enter")  # Enter alone is "no"
        self.d.settle(timeout=10)
        self.assertEqual(answers, [False])

    def test_vim_profile_and_sequences(self):
        st = self.make("vim")
        self.assertEqual(self.km.problems, [])
        self.assertTrue(any("'d d'" in s for s in self.km.shadowed), "the shadowed key is reported, not silent")
        self.d.press("j")
        self.assertEqual(st.pane.cursor, 1)
        self.d.press("k", "l")
        self.assertEqual(st.pane.path, ["arch:1"])
        self.d.press("G")
        self.assertEqual(st.pane.cursor, len(st.pane.rows) - 1)
        self.d.press("g")
        self.assertEqual(st.pending, ["g"])
        self.d.press("g")
        self.assertEqual((st.pane.cursor, st.pending), (0, []))
        self.d.press("y", "p")
        self.assertEqual(len(st.s.eng.package.entries), 1)
        self.d.press("Tab", "d", "d")
        self.assertEqual(len(st.s.eng.package.entries), 0)
        self.d.press("u")
        self.assertEqual(len(st.s.eng.package.entries), 1)

    def test_replay_recipe_step_by_step_and_parse_keys(self):
        self.assertEqual(parse_keys('F5 Down "role:user" Enter'), ["F5", "Down", "r", "o", "l", "e", ":", "u", "s", "e", "r", "Enter"])
        self.assertEqual(parse_keys("'a b'"), ["a", "Space", "b"])
        src = self.make()
        self.act("open")
        self.cursor_to(src, "Migracja bazy danych")
        self.act("select.toggle", "copy")
        rec = src.s.eng.normalised_recipe()
        st = self.make(sources=None)
        st.load_replay(rec)
        self.d.settle()
        want = [s for s in rec["steps"] if s["op"] not in ("scan", "read", "merge", "write")]
        for _ in want:
            self.d.press("Enter")
        self.d.press("Enter")
        self.assertIn("zakończone", st.message)
        got = [(s["op"], s["params"], s["by"]) for s in st.s.eng.recipe["steps"] if s["op"] not in ("scan", "read", "merge")]
        self.assertEqual(got, [(s["op"], s["params"], s["by"]) for s in want], "a replay records the same steps with their face")


class KeymapLayoutTest(ExtractTest):
    def test_shipped_profiles_are_valid_and_complete(self):
        ds = store()
        acts = ds.file("commander/actions.json")["actions"]
        for profile in ds.file("commander/keymap.json")["profiles"]:
            km = Keymap.load(ds, profile)
            self.assertEqual(km.problems, [], profile)
            for key, act in km.bindings.items():
                self.assertIn(act, km.actions, (profile, key))
        for a in acts:
            self.assertTrue(a["label"].get("pl") and a["label"].get("en"), a["id"])
            self.assertIn(a["kind"], ("state", "op"))
        state_fns = {n[2:].replace("_", ".") for n in dir(CommanderState) if n.startswith("a_")}
        self.assertEqual({a["id"] for a in acts} - state_fns, set(), "every action in actions.json has an implementation")

    def test_layout_columns_name_existing_fields(self):
        ds = store()
        layout = ds.file("commander/layout.json")
        fields = ds.file("select_fields.json")["fields"]
        view_fields = {"label", "preview", "blocks", "chars", "folder", "size", "tags", "selected"}
        dict_fields = {"archive": {"label", "format", "units", "size"}, "branch": {"label", "messages", "created", "last", "current"},
                       "folder": {"label", "entries"}, "group": {"label", "entries"}, "cluster": {"label", "entries"},
                       "step": {"label", "by", "params"}, "json": {"label", "value"}, "bookmark": {"label"}, "view": {"label"},
                       "tag": {"label", "tags"}}
        for kind, cols in layout["columns"].items():
            for c in cols:
                if c.get("expr"):
                    continue
                f = c.get("field") or c["id"]
                known = set(fields.get(kind) or {}) | view_fields if kind in fields else dict_fields.get(kind, set())
                self.assertIn(f, known, (kind, f))
                self.assertTrue(c.get("label", {}).get("pl") and c.get("label", {}).get("en"), (kind, c["id"]))
        self.assertEqual(set(layout["root_labels"]), set(layout["roots"]))
        colors = ds.file("commander/colors.json")["profiles"]
        self.assertEqual(set(colors), {"dark", "light", "mono"})
        attrs = set(colors["dark"]["attrs"])
        for name, prof in colors.items():
            self.assertEqual(set(prof["attrs"]), attrs, name)
        self.assertTrue(colors["mono"]["ascii"])

    def test_user_profile_with_inherits_and_problems_are_reported(self):
        from loom.tools.extract.datafiles import DataStore, set_store, write_json
        over = self.tmp / "overlay"
        write_json(over / "commander" / "keymap.json", {"profiles": {
            "moj": {"inherits": "vim", "bindings": {"q": "quit", "x x": "remove"}},
            "zly": {"inherits": "fkeys", "bindings": {"w": "no.such.action", "z": "top", "z z": "bottom"}},
            "petla": {"inherits": "petla", "bindings": {}}}})
        set_store(DataStore(overlay=over))
        km = Keymap.load(store(), "moj")
        self.assertEqual(km.problems, [])
        self.assertEqual(km.bindings["q"], "quit")
        self.assertEqual(km.bindings["j"], "cursor.down", "inherited from vim")
        self.assertEqual(km.bindings["F5"], "copy", "inherited from fkeys through vim")
        self.assertNotIn("x", km.bindings, "x x shadows the inherited x")
        bad = Keymap.load(store(), "zly")
        self.assertTrue(any("no.such.action" in p for p in bad.problems))
        self.assertTrue(any("'z'" in p for p in bad.problems), bad.problems)
        loop = Keymap.load(store(), "petla")
        self.assertTrue(any("cycle" in p for p in loop.problems))
        unknown = Keymap.load(store(), "nie-ma")
        self.assertTrue(unknown.problems and unknown.profile == "fkeys")


if __name__ == "__main__":
    unittest.main()
