"""The CLI face (every verb, Polish and English, --json, exit codes), the i18n tables, and the
privacy guard (no real paths in tests/fixtures; aggregate output carries no content or file names)."""
import contextlib
import io
import json
import re
from pathlib import Path

from _support import EXPECTED_EXTRACT as E, EXPORTS, EXTRACT, REPO, ExtractTest
from loom.tools.extract import cli, i18n

PKG = REPO / "loom" / "tools" / "extract"


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = cli.main([str(a) for a in argv])
    return code, out.getvalue(), err.getvalue()


class CliTest(ExtractTest):
    def test_inventory_timeline_diff_text_and_json(self):
        code, out, err = run("inwentarz", EXPORTS / "openai_2026_sharded.zip", EXTRACT / "mixed_dir")
        self.assertEqual(code, 0, err)
        self.assertIn("Archiwa: 2", out)
        self.assertIn("Rozmów:", out)
        code, out, err = run("--json", "inventory", EXPORTS / "openai_2026_sharded.zip")
        self.assertEqual(code, 0, err)
        doc = json.loads(out)
        self.assertEqual(doc["results"][0]["op"], "inventory")
        self.assertEqual(doc["recipe"]["schema"], "loom.extract.recipe/1")
        code, out, err = run("os-czasu", EXTRACT / "continuation/chatgpt_part.zip", EXTRACT / "continuation/claude_part.zip", "--klucz", "activity")
        self.assertEqual(code, 0, err)
        self.assertIn("kontynuacje między usługami: 1", out)
        code, out, err = run("roznica", EXTRACT / "versions/chatgpt_2025-06-20.zip", EXTRACT / "versions/chatgpt_2025-10-01.zip")
        self.assertEqual(code, 0, err)
        self.assertIn("Nowe: 1, zmienione: 1, usunięte: 1, bez zmian: 1", out)
        code, out, err = run("--lang", "en", "diff", EXTRACT / "versions/chatgpt_2025-06-20.zip", EXTRACT / "versions/chatgpt_2025-10-01.zip", "--summary")
        self.assertIn("New: 1, changed: 1", out)

    def test_search_dry_run_package_explain_and_errors(self):
        code, out, err = run("szukaj", EXTRACT / "versions/chatgpt_2025-10-01.zip", "--slowa", "analizator", "archiwów", "rekurencja",
                             "katalogach", "inwentarz", "--kaskada", "lexical_only", "--na-sucho")
        self.assertEqual(code, 0, err)
        self.assertIn("włączone 1", out)
        self.assertIn("Na sucho", out)
        pkg = self.tmp / "pkg"
        code, out, err = run("szukaj", EXTRACT / "versions/chatgpt_2025-10-01.zip", "--wyrazenie", "has:attachments", "--wzorzec", "multi_turn",
                             "--sasiedztwo", "conversation", "--pakiet", pkg, "--zapisz-przepis", self.tmp / "r.json")
        self.assertEqual(code, 0, err)
        self.assertTrue((pkg / "manifest.json").exists())
        self.assertTrue((self.tmp / "r.json").exists())
        code, out, err = run("przepis", "sprawdz", self.tmp / "r.json")
        self.assertEqual(code, 0, err)
        code, out, err = run("przepis", "hash", self.tmp / "r.json")
        self.assertRegex(out.strip(), r"^[0-9a-f]{64}$")
        code, out, err = run("--json", "przepis", "normalizuj", self.tmp / "r.json")
        self.assertEqual(json.loads(out)["schema"], "loom.extract.recipe/1")
        code, out, err = run("przepis", "uruchom", pkg / "recipe.json", "--na-sucho")
        self.assertEqual(code, 0, err)
        self.assertIn("dry_run", out)
        manifest = json.loads((pkg / "manifest.json").read_text(encoding="utf-8"))
        uid = next(iter(json.loads((pkg / "selection.json").read_text(encoding="utf-8"))["units"]))
        code, out, err = run("wyjasnij", EXTRACT / "versions/chatgpt_2025-10-01.zip", "--wyrazenie", "has:attachments", "--jednostka", uid)
        self.assertEqual(code, 0, err)
        self.assertIn(uid, out)
        self.assertEqual(manifest["selection"]["conversations"], 2)
        # errors
        code, out, err = run("szukaj", EXTRACT / "versions/chatgpt_2025-10-01.zip")
        self.assertEqual(code, 2)
        code, out, err = run("inwentarz", self.tmp / "nope.zip")
        self.assertEqual(code, 6, "an unreadable source has its own exit code")
        self.assertIn("źródło nieczytelne", err)
        code, out, err = run("szukaj", EXTRACT / "versions/chatgpt_2025-10-01.zip", "--wyrazenie", "title~/(/")
        self.assertEqual(code, 1)
        code, out, err = run()
        self.assertEqual(code, 2)

    def test_mark_clusters_formats_ops(self):
        d = self.tmp / "decisions.json"
        code, out, err = run("zaznacz", "--decyzje", d, "sha256:abc", "wlacz", "--powod", "to o aplikacji")
        self.assertEqual(code, 0, err)
        self.assertEqual(json.loads(d.read_text(encoding="utf-8"))["entries"][0]["decision"], "include")
        code, out, err = run("zaznacz", "--decyzje", d, "--lista")
        self.assertIn("sha256:abc", out)
        code, out, err = run("zaznacz", "--decyzje", d, "sha256:abc", "usun")
        self.assertIn("Usunięto", out)
        code, out, err = run("zaznacz", "--decyzje", d, "--propozycje", EXTRACT / "versions/chatgpt_2025-10-01.zip")
        self.assertEqual(code, 0, err)
        self.assertIn("Propozycji: 0", out)
        code, out, err = run("zaznacz", "--decyzje", d)
        self.assertEqual(code, 2)
        code, out, err = run("klastry", EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip")
        self.assertEqual(code, 0, err)
        self.assertRegex(out, r"Klastrów: \d+")
        code, out, err = run("formaty")
        self.assertIn("openai.chatgpt.export", out)
        self.assertIn("hipoteza formatu", out)
        code, out, err = run("formaty", EXTRACT / "grok/x_archive.zip")
        self.assertIn("xai.grok.export", out)
        code, out, err = run("--json", "operacje")
        ops = json.loads(out)
        self.assertIn("select.expr", ops)
        self.assertTrue(all(v["implemented"] for v in ops.values()))

    def test_search_with_decisions_since_and_model_flag(self):
        d = self.tmp / "decisions.json"
        eng, units = self.units(EXTRACT / "versions/chatgpt_2025-10-01.zip")
        c4 = next(u for u in units if u.ext_id == "c4")
        run("zaznacz", "--decyzje", d, c4.uid, "include", "--powod", "obrazek")
        old = self.tmp / "old"
        run("inwentarz", EXTRACT / "versions/chatgpt_2025-06-20.zip", "--pakiet", old)
        code, out, err = run("szukaj", EXTRACT / "versions/chatgpt_2025-10-01.zip", "--slowa", "rekurencja", "--decyzje", d,
                             "--od-manifestu", old, "--tylko", "only_new", "--model", "--na-sucho")
        self.assertEqual(code, 0, err)
        self.assertIn("Decyzje ręczne: włączono 1", out)
        self.assertIn("select.diff", out)
        self.assertIn("poziomy z modelem policzone, nie wywołane", out)


class I18nTest(ExtractTest):
    def test_tables_have_the_same_keys_and_cover_the_code(self):
        pl, en = i18n.table("pl"), i18n.table("en")
        self.assertEqual(set(pl), set(en))
        used = set()
        for src in (PKG / "cli.py", PKG / "cli_cut.py", PKG / "report.py", PKG / "outputs.py", PKG / "cut" / "profiles.py"):
            text = src.read_text(encoding="utf-8")
            used.update(re.findall(r'(?<![A-Za-z_.])t\(["\']([a-z_.]+)["\']', text))
            used.update(re.findall(r'"((?:cli|report)\.[a-z_.]+)"', text))
        missing = sorted(k for k in used if k not in pl)
        self.assertEqual(missing, [])
        self.assertEqual(i18n.t("no.such.key"), "no.such.key", "a missing key is shown, never empty")
        self.assertEqual(i18n.label({"pl": "a", "en": "b"}), "a")
        i18n.set_language("en")
        self.assertEqual(i18n.label({"pl": "a", "en": "b"}), "b")
        self.assertEqual(i18n.label({"pl": "a"}), "a", "fallback to any language")


class PrivacyGuardTest(ExtractTest):
    FORBIDDEN_PATHS = ["docs/" + "exports", "real-" + "holdout-key"]  # built from pieces so this file passes its own guard
    REAL_MACHINE = re.compile(r"/(?:home|root)/[A-Za-z0-9_.-]+/\.claude/projects")

    def test_no_real_paths_in_tool_tests_or_fixtures(self):
        files = [p for p in PKG.rglob("*") if p.suffix in (".py", ".json", ".md") and "__pycache__" not in p.parts]
        files += [REPO / "loom" / "tools" / "gen_extract_fixtures.py"]
        files += [p for p in EXTRACT.rglob("*") if p.is_file() and p.suffix in (".json", ".jsonl", ".html", ".md", ".txt", ".py")]
        for f in files:
            text = f.read_text(encoding="utf-8", errors="replace")
            for needle in self.FORBIDDEN_PATHS:
                self.assertNotIn(needle, text, f)
            self.assertIsNone(self.REAL_MACHINE.search(text), f)

    def test_aggregate_output_has_no_titles_text_or_file_names(self):
        out, err = io.StringIO(), io.StringIO()
        sources = [EXTRACT / "claude_code/projects/-home-user-proj", EXTRACT / "gemini/takeout.zip", EXTRACT / "gemini/MyActivity.html",
                   EXTRACT / "grok/grok_com.json", EXTRACT / "grok/x_archive.zip", EXTRACT / "ours/packet.json", EXTRACT / "ours/conversation.json",
                   EXTRACT / "mixed_dir", EXTRACT / "versions/chatgpt_2025-10-01.zip", EXTRACT / "continuation/claude_part.zip"]
        pkg = self.tmp / "agg"
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = cli.main(["--zbiorczo", "--json", "szukaj", *map(str, sources), "--slowa", "rekurencja", "--wzorzec", "agentic",
                             "--pakiet", str(pkg)])
        self.assertEqual(code, 0, err.getvalue())
        files = sorted(p for p in pkg.rglob("*") if p.is_file())  # every file, recursively, manifest.json included
        self.assertIn(pkg / "manifest.json", files)
        text = out.getvalue() + "".join(p.read_text(encoding="utf-8") for p in files) + "\n".join(str(p.relative_to(pkg)) for p in files)
        names = [p.name for s in sources for p in ([s] if s.is_file() else s.rglob("*"))] + ["MyActivity.json", "grok-chat-item.js"]
        for needle in E["forbidden_in_aggregate"] + names:
            self.assertNotIn(needle, text, needle)
        self.assertNotIn(str(EXTRACT), text)
        doc = json.loads(out.getvalue())
        self.assertTrue(doc["results"])
