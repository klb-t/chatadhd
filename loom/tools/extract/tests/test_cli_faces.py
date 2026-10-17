"""CLI verbs of the faces: ``komander --bez-ekranu --klawisze`` (scripted keys, autosave next to the
package, an existing autosave kept as .prev and resumed with --wznow), ``api`` (calls of the one
dispatcher from arguments and a JSONL file), the curses-less message; and the i18n tables cover
every key the faces use (static and the ones built from data: F-key labels, root hints, questions,
package-op lines)."""
import contextlib
import io
import json
import re
import unittest
from pathlib import Path

from _support import EXPORTS, ExtractTest

from loom.tools.extract import cli, i18n
from loom.tools.extract.commander.keymap import Keymap
from loom.tools.extract.datafiles import store

PKG = Path(__file__).resolve().parents[1]
SRC = [str(EXPORTS / "openai_2026_sharded.zip")]


class CliFacesTest(ExtractTest):
    def run_cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def test_headless_commander_with_a_key_script_and_resume(self):
        pkg = self.tmp / "pkg"
        code, out, err = self.run_cli("--json", "komander", *SRC, "--pakiet", str(pkg), "--bez-ekranu",
                                      "--klawisze", "Enter Insert F5 Tab F7 projekty Enter")
        self.assertEqual(code, 0, err)
        doc = json.loads(out)
        ops = [s["op"] for s in doc["recipe"]["steps"] if s.get("note") != "implicit"]
        self.assertEqual(ops, ["select.items", "package.copy", "package.mkdir"])
        self.assertEqual(doc["status"]["package"]["entries"], 1)
        auto = pkg / "komander.autosave.json"
        self.assertTrue(auto.exists())
        # a second start keeps the earlier session (never lost) unless asked to resume it
        code, out, err = self.run_cli("komander", *SRC, "--pakiet", str(pkg), "--bez-ekranu", "--klawisze", "Down")
        self.assertEqual(code, 0, err)
        self.assertTrue((pkg / "komander.autosave.prev.json").exists())
        self.assertIn("komander.autosave.prev.json", err)
        prev = json.loads((pkg / "komander.autosave.prev.json").read_text(encoding="utf-8"))
        (pkg / "komander.autosave.json").write_text(json.dumps(prev), encoding="utf-8")
        code, out, err = self.run_cli("--json", "komander", "--pakiet", str(pkg), "--bez-ekranu", "--wznow")
        self.assertEqual(code, 0, err)
        doc = json.loads(out)
        self.assertEqual(doc["status"]["package"]["entries"], 1, "resumed: the package is back")
        self.assertIn("Pakiet", "\n".join(doc["screen"]))

    def test_replay_option_and_screen_text(self):
        rec = {"sources": SRC, "steps": [{"op": "select.expr", "params": {"expr": "title~/migracja/i"}}]}
        rp = self.tmp / "r.json"
        rp.write_text(json.dumps(rec), encoding="utf-8")
        code, out, err = self.run_cli("komander", "--odtworz", str(rp), "--bez-ekranu", "--autozapis", "off", "--klawisze", "Enter Enter")
        self.assertEqual(code, 0, err)
        self.assertIn("zazn.: 1 rozm.", out)

    def test_api_verb_runs_calls_from_arguments_and_a_file(self):
        calls = self.tmp / "calls.jsonl"
        calls.write_text("# a comment line\n" + json.dumps({"call": "select", "how": "expr", "expr": "title~/migracja/i"}) + "\n"
                         + json.dumps({"call": "selection"}) + "\n" + json.dumps({"call": "nope"}) + "\n", encoding="utf-8")
        code, out, err = self.run_cli("api", '{"call": "list", "root": "sources"}', "--plik", str(calls), "--zrodla", *SRC)
        self.assertEqual(code, 1, "an unknown call is reported and the exit code says so")
        lines = [json.loads(x) for x in out.splitlines()]
        self.assertEqual(lines[0]["rows"][0]["kind"], "archive")
        self.assertEqual(lines[1]["selection"]["conversations"], 1)
        self.assertEqual(lines[2]["total"], 1)
        self.assertEqual(lines[3]["code"], "unknown_call")

    def test_autosave_is_never_in_the_repository_without_a_package_or_overlay(self):
        """F42: without --pakiet and an overlay, the commander's autosave goes to the per-user state directory —
        never Path.cwd(), which is the repository when run as python3 -m loom.tools.extract."""
        import argparse
        import os
        from _support import REPO
        from loom.tools.extract.cli_faces import autosave_path
        os.environ.pop("LOOM_EXTRACT_DATA_DIR", None)
        os.environ.pop("CHATADHD_DATA", None)
        cwd = os.getcwd()
        os.chdir(REPO)
        try:
            p = autosave_path(argparse.Namespace(autozapis=None, pakiet=None), store())
        finally:
            os.chdir(cwd)
        self.assertIsNotNone(p)
        self.assertFalse(p.resolve().is_relative_to(REPO.resolve()), p)
        self.assertTrue(p.resolve().is_relative_to((self.tmp / "state").resolve()), p)
        os.environ.pop("LOOM_EXTRACT_STATE")
        os.environ["XDG_STATE_HOME"] = str(self.tmp / "xdg")
        p2 = autosave_path(argparse.Namespace(autozapis=None, pakiet=None), store())
        self.assertTrue(str(p2).startswith(str(self.tmp / "xdg" / "loom-extract")), p2)
        ignored = (REPO / ".gitignore").read_text(encoding="utf-8")
        self.assertIn("komander.autosave*.json", ignored)
        self.assertIn("serve.token", ignored)

    def test_i18n_covers_the_faces(self):
        pl, en = i18n.table("pl"), i18n.table("en")
        self.assertEqual(set(pl), set(en))
        used = set()
        files = [PKG / "api.py", PKG / "nodes.py", PKG / "serve.py", PKG / "cli_faces.py", PKG / "ops_views.py"] + sorted((PKG / "commander").glob("*.py"))
        for f in files:
            text = f.read_text(encoding="utf-8")
            used.update(re.findall(r'(?<![A-Za-z_.])t\(\s*["\']([a-z_.]+)["\']', text))
        used = {k for k in used if not k.endswith(".")}  # prefixes of keys built from data; their families are added below
        for v in cli.VERBS[-3:]:
            used.add(v["help"])
            used.update(kw["help"] for _, kw in v["opts"] if isinstance(kw.get("help"), str))
        ds = store()
        for profile in ds.file("commander/keymap.json")["profiles"]:
            km = Keymap.load(ds, profile)
            used.update(f"commander.short.{km.bindings[f'F{i}']}" for i in range(1, 11) if f"F{i}" in km.bindings)
        used.update(f"commander.hint.{r}" for r in ds.file("commander/layout.json")["roots"])
        used.update(f"api.question.{k}" for k in ("estimate", "x10"))
        used.update(f"pkgop.{k}" for k in ("copy", "move", "mkdir", "remove", "rename", "mark"))
        missing = sorted(k for k in used if k not in pl)
        self.assertEqual(missing, [])
        for k in used:
            self.assertEqual(set(re.findall(r"\{(\w+)\}", pl[k])), set(re.findall(r"\{(\w+)\}", en[k])), k)


if __name__ == "__main__":
    unittest.main()
