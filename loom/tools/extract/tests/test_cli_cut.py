"""Slice B — the CLI: sentence mode, task files (JSON/TOML), dry run and plan,
cut/check/stats/reproduce/handoff/edit/undo/redo/cost/attachments/profile/flow/
describe/filter verbs, exit codes, aggregate privacy of the new verbs."""
import contextlib
import io
import json
import os

from _support import EXPORTS, EXTRACT, ExtractTest
from loom.tools.extract import cli
from loom.tools.extract.sentence import parse_sentence, sentence_to_recipe


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = cli.main([str(a) for a in argv])
    return code, out.getvalue(), err.getvalue()


class SentenceTest(ExtractTest):
    def test_polish_and_english_sentences_become_tasks(self):
        p = parse_sentence("wytnij z archiwów a.zip, b.zip i c/ wszystko na temat analizator archiwów od 2025-06 do 2025-12 "
                           "bez narzędzi tylko z załącznikami w profilu tekstowym do pakietu ~/x z przekazaniem")
        self.assertEqual(p["verb"], "cut")
        self.assertEqual(p["sources"], ["a.zip", "b.zip", "c/"])
        self.assertEqual(p["topic"], "analizator archiwów")
        self.assertEqual(p["range"], {"from": "2025-06", "to": "2025-12"})
        self.assertEqual(p["drop_blocks"], ["tool_use", "tool_result"])
        self.assertEqual(p["exprs"], ["has:attachments"])
        self.assertEqual(p["profiles"], ["text"])
        self.assertEqual(p["package"], "~/x")
        self.assertTrue(p["handoff"])
        self.assertEqual(p["rest"], "")
        ops = [s["op"] for s in sentence_to_recipe(p)["steps"]]
        self.assertEqual(ops, ["select.criteria", "select.expr", "select.expr", "expand", "edit.drop_blocks", "cut", "handoff", "write"])
        e = parse_sentence("extract from archive a.zip everything about Loom in the flat profile without thinking")
        self.assertEqual((e["grammar"], e["topic"], e["profiles"], e["drop_blocks"]), ("en", "Loom", ["flat"], ["thinking"]))
        crit = sentence_to_recipe(parse_sentence("wyciągnij z archiwum a.zip wszystko o ChatADHD"))["steps"][0]
        self.assertEqual(crit["params"], {"criteria": "chatadhd"}, "a topic naming shipped criteria uses them")

    def test_sentence_runs_shows_the_task_and_reports_what_it_did_not_understand(self):
        os.environ["SOURCE_DATE_EPOCH"] = "1760000000"
        pkg = self.tmp / "s"
        code, out, err = run(f"wyciągnij z archiwum {EXPORTS / 'openai_2026_sharded.zip'} wszystko o migracja w profilu płaskim do pakietu {pkg}")
        self.assertEqual(code, 0, err)
        self.assertIn("Zadanie ze zdania", err)
        self.assertTrue((pkg / "flat/messages.jsonl").exists())
        man = json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))
        self.assertIn("wyciągnij z archiwum", man["run"]["sentence"])
        self.assertEqual(man["run"]["faces"], ["recipe", "sentence"])
        code, out, err = run(f"pokaż z archiwum {EXPORTS / 'openai_2026_sharded.zip'} wszystko o migracja", "--na-sucho")
        self.assertEqual(code, 0, err)
        self.assertIn("Weszłoby", out)
        self.assertEqual(out.count("Weszłoby"), 1, "one dry run, not two")
        code, out, err = run("zrób coś ładnego")
        self.assertEqual(code, 2)
        self.assertIn("nie wskazuje archiwum", err)


class TaskFileTest(ExtractTest):
    def test_json_and_toml_task_files(self):
        os.environ["SOURCE_DATE_EPOCH"] = "1760000000"
        task = {"schema": "loom.extract.task/1", "sources": [str(EXPORTS / "anthropic_2026_full.zip")], "expr": ["title~/Widok/"],
                "profiles": ["preserve", "text"], "package": str(self.tmp / "t1"), "language": "pl"}
        (self.tmp / "task.json").write_text(json.dumps(task), encoding="utf-8")
        code, out, err = run("--zadanie", self.tmp / "task.json")
        self.assertEqual(code, 0, err)
        self.assertTrue((self.tmp / "t1/preserve/anthropic_2026_full.zip").exists())
        toml = (f'sources = ["{EXPORTS / "anthropic_2026_full.zip"}"]\nexpr = ["title~/Widok/"]\nprofiles = ["preserve", "text"]\n'
                f'package = "{self.tmp / "t2"}"\n')
        (self.tmp / "task.toml").write_text(toml, encoding="utf-8")
        code, out, err = run("--zadanie", self.tmp / "task.toml")
        self.assertEqual(code, 0, err)
        h1 = json.loads((self.tmp / "t1/manifest.json").read_text(encoding="utf-8"))["reproducible_hash"]
        h2 = json.loads((self.tmp / "t2/manifest.json").read_text(encoding="utf-8"))["reproducible_hash"]
        self.assertEqual(h1, h2, "JSON and TOML of the same task give the same package")
        code, out, err = run("--zadanie", self.tmp / "task.json", "--plan")
        self.assertEqual(code, 0, err)
        self.assertIn("plan:", out)


class CutVerbsTest(ExtractTest):
    def test_cut_check_stats_reproduce_edit_undo_redo(self):
        os.environ["SOURCE_DATE_EPOCH"] = "1760000000"
        pkg = self.tmp / "p"
        code, out, err = run("wytnij", EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip", "-e", "kind:conversation",
                             "--profil", "preserve", "--profil", "table", "--przekaz", "-p", pkg)
        self.assertEqual(code, 0, err)
        self.assertIn("Wycięto profile: preserve, table", out)
        self.assertIn("Przekazanie:", out)
        h0 = json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))["reproducible_hash"]
        code, out, err = run("sprawdz", "-p", pkg)
        self.assertEqual(code, 0, out + err)
        code, out, err = run("statystyki", "-p", pkg)
        self.assertIn("Statystyki: rozmów", out)
        code, out, err = run("odtworz", pkg / "manifest.json", "-p", self.tmp / "again")
        self.assertEqual(code, 0, out + err)
        self.assertIn("odtworzono bajt w bajt", out)
        code, out, err = run("edytuj", "-p", pkg, "--operacja", "relabel_roles", "--zakres", "message[role:tool]",
                             "--param", 'map={"tool": "assistant"}', "--podglad")
        self.assertEqual(code, 0, err)
        self.assertIn("podgląd", out)
        self.assertEqual(json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))["reproducible_hash"], h0)
        code, out, err = run("edytuj", "-p", pkg, "--operacja", "relabel_roles", "--zakres", "message[role:tool]", "--param", 'map={"tool": "assistant"}')
        self.assertEqual(code, 0, err)
        h1 = json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))["reproducible_hash"]
        self.assertNotEqual(h1, h0)
        self.assertNotIn(",tool,", (pkg / "table/messages.csv").read_text(encoding="utf-8"))
        code, out, err = run("cofnij", "-p", pkg)
        self.assertEqual(code, 0, err)
        self.assertEqual(json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))["reproducible_hash"], h0, "undo = the package before")
        code, out, err = run("ponow", "-p", pkg)
        self.assertEqual(code, 0, err)
        self.assertEqual(json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))["reproducible_hash"], h1, "redo = the edited package")
        code, out, err = run("cofnij", "-p", pkg, "5")
        self.assertEqual(code, 0, err)
        code, out, err = run("cofnij", "-p", pkg)
        self.assertEqual(code, 1)
        self.assertIn("nie ma czego cofnąć", err)
        code, out, err = run("przekaz", "-p", pkg, "--z-grafem")
        self.assertEqual(code, 0, err)
        self.assertTrue((pkg / "handoff/graph/packet.json").exists())

    def test_exit_codes(self):
        junk = self.tmp / "x.bin"
        junk.write_bytes(bytes(range(256)) * 4)
        code, out, err = run("wytnij", junk, "-e", "kind:conversation", "-p", self.tmp / "p")
        self.assertEqual(code, 3, err)
        self.assertIn("nic nie rozpoznano", err)
        code, out, err = run("wytnij", self.tmp / "missing.zip", "-e", "kind:conversation", "-p", self.tmp / "p")
        self.assertEqual(code, 6)
        data = self.tmp / "data"
        (data / "extract/cascades").mkdir(parents=True)
        from loom.tools.extract.datafiles import read_json, store
        cas = read_json(store().shipped / "cascades/default.json")
        cas.update({"id": "all_ambiguous", "bands": {"include": 1.01, "exclude": -0.01}})
        (data / "extract/cascades/all_ambiguous.json").write_text(json.dumps(cas), encoding="utf-8")
        code, out, err = run("--dane", data, "wytnij", EXPORTS / "openai_2026_sharded.zip", "--slowa", "migracja", "--kaskada", "all_ambiguous",
                             "--model", "--model-tani", "m-cheap", "-p", self.tmp / "p")
        self.assertEqual(code, 5, err)
        self.assertIn("krok płatny odmówiony", err)
        self.assertFalse((self.tmp / "p/manifest.json").exists(), "nothing written when a paid step is refused")
        code, out, err = run("--dane", data, "koszt", EXPORTS / "openai_2026_sharded.zip", "--slowa", "migracja", "--kaskada", "all_ambiguous",
                             "--model-tani", "m-cheap", "--model-mocny", "m-strong")
        self.assertEqual(code, 0, err)
        self.assertIn("Szacunek kroków płatnych", out)
        self.assertIn("nieznany", out, "no price → unknown, never zero")
        # F11: the minimal configuration — no --pakiet — uses the sentence's preset in the current directory, said aloud
        cwd = os.getcwd()
        os.chdir(self.tmp)
        try:
            code, out, err = run("wytnij", EXPORTS / "openai_2026_sharded.zip", "-t", "ChatADHD")
        finally:
            os.chdir(cwd)
        self.assertEqual(code, 0, err)
        self.assertTrue((self.tmp / "ekstrakt-chatadhd" / "manifest.json").exists())
        self.assertIn("ekstrakt-chatadhd", err)
        # F41: an output path that is a file is an error about the output, with no traceback
        blocker = self.write(self.tmp / "zajete", "plik, nie katalog")
        code, out, err = run("wytnij", EXPORTS / "openai_2026_sharded.zip", "-e", "kind:conversation", "-p", blocker)
        self.assertEqual(code, 1)
        self.assertIn("pakietu", err)
        self.assertNotIn("Traceback", err)

    def test_small_verbs(self):
        code, out, err = run("zalaczniki", EXPORTS / "openai_2026_sharded.zip", "--brakujace")
        self.assertEqual(code, 0, err)
        self.assertIn("missing", out)
        code, out, err = run("profil", "lista")
        self.assertIn("preserve", out)
        code, out, err = run("profil", "sprawdz", "timeline")
        self.assertEqual(code, 0)
        bad = self.tmp / "bad.json"
        bad.write_text(json.dumps({"schema": "loom.extract.profile/1", "id": "bad", "outputs": []}), encoding="utf-8")
        code, out, err = run("profil", "sprawdz", bad)
        self.assertEqual(code, 1)
        code, out, err = run("przeplyw", "sprawdz", "default")
        self.assertEqual(code, 0, out)
        code, out, err = run("opisz", 'rozmowy o "pamięć hierarchiczna" bez leków')
        crit = json.loads(out)
        self.assertEqual(crit["keywords"][0]["term"], "pamięć hierarchiczna")
        self.assertEqual(crit["exclude"]["keywords"][0]["term"], "leków")
        data = self.tmp / "data"
        code, out, err = run("--dane", data, "filtr", "zapisz", "agentowe", "is:agentic")
        self.assertEqual(code, 0, err)
        code, out, err = run("--dane", data, "szukaj", EXPORTS / "openai_2026_sharded.zip", "--filtr", "agentowe", "--na-sucho")
        self.assertEqual(code, 0, err)
        code, out, err = run("--dane", data, "filtr", "usun", "agentowe")
        self.assertEqual(code, 0, err)

    def test_aggregate_cut_prints_no_titles_or_names(self):
        sources = [EXPORTS / "openai_2026_sharded.zip", EXTRACT / "claude_code/projects"]
        code, out, err = run("--zbiorczo", "--json", "wytnij", *sources, "-e", "kind:conversation OR kind:session", "--profil", "text",
                             "--przekaz", "-p", self.tmp / "agg")
        self.assertEqual(code, 0, err)
        for needle in ("Migracja bazy danych", "Obrazy i dźwięk", "openai_2026_sharded.zip", "sess-0001", "schemat.png"):
            i = (out + err).find(needle)
            self.assertEqual(i, -1, (needle, (out + err)[max(0, i - 300):i + 40]))
