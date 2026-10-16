"""One engine, three faces (§3.11, requirement 33): the same recipe run from the CLI
(``przepis uruchom``), from the commander (headless key driver replaying it step by step and
writing with F9) and over the serve API (``POST /api/recipe/run``) gives three identical
``reproducible_hash`` values and byte-identical recipes.  And the other way round: what the
commander records from keys (both keymap profiles) replays through the CLI and the API to the
same package."""
import contextlib
import io
import json
import http.client
import unittest

from _support import EXPORTS, ExtractTest

from loom.tools.extract import cli
from loom.tools.extract.api import Session
from loom.tools.extract.commander.driver import Driver
from loom.tools.extract.commander.state import CommanderState
from loom.tools.extract.datafiles import store
from loom.tools.extract.serve import Server

SRC = [str(EXPORTS / "openai_2026_sharded.zip"), str(EXPORTS / "anthropic_2026_full.zip")]


class FacesParityTest(ExtractTest):
    def cli_run(self, recipe_path, package) -> str:
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(["przepis", "uruchom", str(recipe_path), "--pakiet", str(package)])
        self.assertEqual(code, 0, err.getvalue())
        return json.loads((package / "manifest.json").read_text(encoding="utf-8"))["reproducible_hash"]

    def serve_run(self, recipe_path, package) -> str:
        sess = Session(face="api", ds=store())
        srv = Server(sess, port=0).start()
        try:
            c = http.client.HTTPConnection("127.0.0.1", srv.port, timeout=60)
            c.request("POST", "/api/recipe/run", body=json.dumps({"recipe": str(recipe_path), "package": str(package)}).encode(),
                      headers={"X-Extract-Token": srv.token, "Content-Type": "application/json"})
            r = c.getresponse()
            body = json.loads(r.read().decode("utf-8"))
            c.close()
            self.assertEqual(r.status, 200, body)
            return body["reproducible_hash"]
        finally:
            srv.stop()
            sess.close()

    def commander(self, profile="fkeys", sources=None):
        sess = Session(face="tui", ds=store())
        self.addCleanup(sess.close)
        if sources:
            sess.call({"call": "open", "sources": sources})
        st = CommanderState(sess, profile=profile, width=140, height=40)
        return st, Driver(st).settle()

    def keys(self, st, action):
        return st.km.keys_for(action)[0].split()

    def test_the_same_recipe_through_cli_commander_and_api(self):
        rec = {"id": "parity", "sources": SRC, "steps": [
            {"op": "select.expr", "params": {"expr": "title~/migracja|wyszukiwanie/i"}},
            {"op": "package.copy", "params": {}},
            {"op": "package.mkdir", "params": {"path": "/projekt"}},
            {"op": "edit.relabel_roles", "params": {"where": "message", "map": {"tool": "narzędzie"}}},
            {"op": "edit.drop_blocks", "params": {"where": "message", "kinds": ["thinking"]}},
            {"op": "cut", "params": {"profiles": ["flat", "text"]}}]}
        rp = self.tmp / "parity.json"
        rp.write_text(json.dumps(rec, ensure_ascii=False), encoding="utf-8")
        h_cli = self.cli_run(rp, self.tmp / "p_cli")
        h_api = self.serve_run(rp, self.tmp / "p_api")
        st, d = self.commander()
        st.load_replay(rec)
        d.settle()
        while st.replay:
            d.press("Enter")
        d.press(*self.keys(st, "export"))
        d.press("C-u")
        d.type(str(self.tmp / "p_tui"))
        d.press("Enter")
        d.settle(timeout=60)
        h_tui = json.loads((self.tmp / "p_tui" / "manifest.json").read_text(encoding="utf-8"))["reproducible_hash"]
        if h_cli != h_tui:  # name the first differing output, so a failure says what differs
            a = {o["path"]: o["sha256"] for o in json.loads((self.tmp / "p_cli" / "manifest.json").read_text())["outputs"]}
            b = {o["path"]: o["sha256"] for o in json.loads((self.tmp / "p_tui" / "manifest.json").read_text())["outputs"]}
            self.fail(f"cli vs tui differ in: {sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))}")
        self.assertEqual(h_cli, h_api)
        recipes = {p: (self.tmp / p / "recipe.json").read_bytes() for p in ("p_cli", "p_api", "p_tui")}
        self.assertEqual(len(set(recipes.values())), 1, "byte-identical recipes")

    def test_keys_recorded_in_the_commander_replay_elsewhere(self):
        for profile in ("fkeys", "vim"):
            with self.subTest(profile=profile):
                st, d = self.commander(profile, SRC)
                d.press(*self.keys(st, "open"))
                rows = st.pane.rows
                st.pane.cursor = next(i for i, r in enumerate(rows) if r["label"] == "Migracja bazy danych")
                d.press(*self.keys(st, "select.toggle"))
                d.press(*self.keys(st, "select.expr"))
                d.type("title~/obrazy/i")
                d.press("Enter")
                d.press(*self.keys(st, "copy"))  # the selection (2 conversations) into the package
                d.press(*self.keys(st, "edit"))
                d.type("drop_blocks")
                d.press("Enter", "Enter")
                d.type("thinking,tool_use")
                d.press("Enter", "Enter")  # params, then apply the preview
                d.press(*self.keys(st, "export"))
                d.press("C-u")
                pkg = self.tmp / f"tui_{profile}"
                d.type(str(pkg))
                d.press("Enter")
                d.settle(timeout=60)
                man = json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))
                ops = [s["op"] for s in json.loads((pkg / "recipe.json").read_text(encoding="utf-8"))["steps"] if s.get("note") != "implicit"]
                self.assertEqual(ops,
                                 ["select.items", "select.expr", "package.copy", "edit.drop_blocks", "cut"])
                self.assertEqual(man["run"]["faces"], ["recipe", "tui"])
                h_cli = self.cli_run(pkg / "recipe.json", self.tmp / f"cli_{profile}")
                h_api = self.serve_run(pkg / "recipe.json", self.tmp / f"api_{profile}")
                self.assertEqual({h_cli, h_api}, {man["reproducible_hash"]})


if __name__ == "__main__":
    unittest.main()
