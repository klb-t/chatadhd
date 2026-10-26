# -*- coding: utf-8 -*-
"""Testy tools/context_pack.py i katalogu narzędzi (IW-2). Bez sieci, bez modeli.

Uruchomienie:  python3 -m unittest discover -s tools/tests -v
"""
import contextlib
import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import context_pack as cp  # noqa: E402

HARNESS = Path(__file__).resolve().parent / "single_package_prompts.mjs"


def packs_fixture():
    return {
        "presets": {"header": "== {pack} ==", "ref_header": "-- {kind}: {label} ({ref_hash})",
                    "pointer": "[{path} {ref_hash}]", "footer": "== /{pack} =="},
        "packs": {
            "p": {"refs": [
                {"kind": "file", "path": "a.md", "label": "A"},
                {"kind": "file", "path": "a.md", "mode": "pointer"},
                {"kind": "section", "path": "doc.md", "heading": "## Druga"},
                {"kind": "lines", "path": "doc.md", "match": "^## ", "max_matches": 2},
                {"kind": "text", "text": "stały tekst"},
                {"kind": "tools", "place": "vm"},
            ]},
            "zly": {"refs": [{"kind": "nieznany"}]},
        },
    }


class Base(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="cp_"))
        (self.root / "a.md").write_text("treść A\n", encoding="utf-8")
        (self.root / "doc.md").write_text("# Tytuł\n## Pierwsza\np1\n## Druga\np2\n### pod\np3\n## Trzecia\np4\n",
                                          encoding="utf-8")
        self.cat = {"presets": {"unknown_effects": "worst_case", "worst_case_effects": ["writes_repo", "costs_money"]},
                    "tools": [{"id": "x", "does": "robi x", "effects": ["reads_only"],
                               "availability": {"vm": {"state": "available", "observed": "2026-10-26"}}},
                              {"id": "y", "does": "robi y"}]}

    def tearDown(self):
        shutil.rmtree(self.root, ignore_errors=True)


class TestRender(Base):
    def test_render_is_deterministic_and_hash_is_of_text(self):
        a = cp.render(self.root, packs_fixture(), "p", self.cat)
        b = cp.render(self.root, packs_fixture(), "p", self.cat)
        self.assertEqual(a["text"], b["text"])
        self.assertEqual(a["hash"], cp.sha(a["text"]))
        self.assertEqual(len(a["refs"]), 6)

    def test_ref_kinds(self):
        t = cp.render(self.root, packs_fixture(), "p", self.cat)["text"]
        self.assertIn("treść A", t)
        self.assertIn("[a.md sha256:", t)                     # pointer: ścieżka + hash, bez treści
        self.assertIn("## Druga\np2\n### pod\np3", t)          # sekcja do następnego nagłówka tego samego poziomu
        self.assertNotIn("p4", t)
        self.assertIn("## Pierwsza\n## Druga", t)             # lines z limitem dopasowań (preset)
        self.assertIn("- y: robi y |  | efekty: writes_repo, costs_money | vm: brak obserwacji", t)
        self.assertIn("vm: available (2026-10-26)", t)

    def test_content_change_changes_hash_and_pointer_too(self):
        h1 = cp.render(self.root, packs_fixture(), "p", self.cat)["hash"]
        (self.root / "a.md").write_text("treść A2\n", encoding="utf-8")
        self.assertNotEqual(h1, cp.render(self.root, packs_fixture(), "p", self.cat)["hash"])

    def test_errors(self):
        with self.assertRaises(cp.PackError):
            cp.render(self.root, packs_fixture(), "brak", self.cat)
        with self.assertRaises(cp.PackError):
            cp.render(self.root, packs_fixture(), "zly", self.cat)
        with self.assertRaises(cp.PackError):
            cp._section("## A\n", "## B")


class TestRepoData(unittest.TestCase):
    def test_repo_packs_render(self):
        packs = cp.load_json(cp.PACKS)
        cat = cp.load_json(cp.TOOLS)
        for name in packs["packs"]:
            r = cp.render(cp.REPO, packs, name, cat)
            self.assertTrue(r["text"].endswith("\n"), name)
            self.assertEqual(r["hash"], cp.render(cp.REPO, packs, name, cat)["hash"], name)

    def test_repo_tool_catalog_is_well_formed(self):
        cat = cp.load_json(cp.TOOLS)
        self.assertEqual(cp.check_tools(cat), [])
        ids = {t["id"] for t in cat["tools"]}
        # ziarno TELEPORT §5 jest w katalogu (przykład, nie zamknięta lista)
        for t in ("workflow_single_package", "scheduled_tasks", "send_message", "github_mcp", "rate_limit_events"):
            self.assertIn(t, ids)
        # każde narzędzie repo tools/*.py|*.sh ma wpis (nowy skrypt bez wpisu = ten test przypomina)
        tools_dir = cp.REPO / "tools"
        scripts = {p.stem for p in tools_dir.glob("*.py")} | {p.stem for p in tools_dir.glob("*.sh")}
        runs = " ".join(t.get("run", "") for t in cat["tools"])
        missing = sorted(s for s in scripts if f"tools/{s}." not in runs)
        self.assertEqual(missing, [], "skrypty bez wpisu w tools/routines/tools.json")

    def test_check_tools_flags_undated_availability(self):
        cat = {"tools": [{"id": "a", "does": "x", "availability": {"vm": {"state": "available"}}},
                         {"id": "a", "does": "x"}]}
        errs = cp.check_tools(cat)
        self.assertTrue(any("daty" in e for e in errs))
        self.assertTrue(any("powtórzone" in e for e in errs))

    def test_unknown_effects_are_worst_case(self):
        cat = {"presets": {"unknown_effects": "worst_case", "worst_case_effects": ["a", "b"]}}
        self.assertEqual(cp.effects_of(cat, {"id": "t"}), ["a", "b"])
        self.assertEqual(cp.effects_of({"presets": {"unknown_effects": "unknown"}}, {"id": "t"}), ["unknown"])


class TestBrief(unittest.TestCase):
    """Na prawdziwych danych repo (spec.json, context_packs.json)."""

    def setUp(self):
        self.packs = cp.load_json(cp.PACKS)
        self.cat = cp.load_json(cp.TOOLS)
        self.tmp = Path(tempfile.mkdtemp(prefix="cpb_"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def brief(self, **kw):
        b = {"wt": "/w", "branch": "b", "id": "X-1", "scope": "zakres", "accept": "akceptacja",
             "context_pack": "podwykonawca", "spec": ["R90"]}
        b.update(kw)
        return b

    def test_check_brief_advisories(self):
        adv = cp.check_brief(cp.REPO, self.packs, {"id": "x"}, self.cat)
        kinds = {a["kind"] for a in adv}
        self.assertEqual(kinds, {"no_spec", "no_context_pack"})
        adv = cp.check_brief(cp.REPO, self.packs, self.brief(spec=["R90", "R99999"], context_pack="brak"), self.cat)
        kinds = {a["kind"] for a in adv}
        self.assertIn("unknown_spec", kinds)
        self.assertIn("unknown_context_pack", kinds)
        filled = cp.fill_brief(cp.REPO, self.packs, self.brief(), self.cat)
        self.assertEqual(cp.check_brief(cp.REPO, self.packs, filled, self.cat), [])
        stale = dict(filled, context_pack_hash="sha256:00")
        kinds = {a["kind"] for a in cp.check_brief(cp.REPO, self.packs, stale, self.cat)}
        self.assertEqual(kinds, {"stale_prefix", "prefix_hash_mismatch"})

    def test_spec_ids_from_field_and_text(self):
        self.assertEqual(cp.spec_ids_of({"spec": ["spec:R1"], "scope": "patrz spec:UI12 i spec:R1."}), ["R1", "UI12"])

    def test_cli_check_brief_is_advisory_unless_strict(self):
        p = self.tmp / "b.json"
        p.write_text(json.dumps({"id": "x"}))
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(cp.main(["check-brief", str(p)]), 0)
            self.assertEqual(cp.main(["check-brief", str(p), "--strict"]), 1)

    @unittest.skipUnless(shutil.which("node"), "node niedostępny")
    def test_two_briefs_same_pack_send_identical_prefix(self):
        """Akceptacja IW-2: diff prefiksu dwóch briefów = 0, brief ma spec: ≥ 1, hash prefiksu trafia do karty."""
        b1 = cp.fill_brief(cp.REPO, self.packs, self.brief(id="A-1", scope="pierwszy"), self.cat)
        b2 = cp.fill_brief(cp.REPO, self.packs, self.brief(id="B-2", scope="drugi", wt="/inny", spec=["R89"]), self.cat)
        b3 = self.brief(id="C-3")  # bez wypełnienia: brief bez prefiksu nadal działa
        paths = []
        for i, b in enumerate((b1, b2, b3)):
            p = self.tmp / f"b{i}.json"
            p.write_text(json.dumps(b, ensure_ascii=False), encoding="utf-8")
            paths.append(str(p))
        r = subprocess.run(["node", str(HARNESS), *paths], capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 0, r.stderr)
        out = json.loads(r.stdout)
        p1, p2, p3 = (o["prompts"][0]["prompt"] for o in out)
        self.assertEqual(b1["context_pack_hash"], b2["context_pack_hash"])
        n = len(b1["context_prefix"])
        self.assertEqual(p1[:n], b1["context_prefix"])
        self.assertEqual(p1[:n], p2[:n])                       # bajt w bajt
        self.assertEqual(cp.sha(p1[:n]), b1["context_pack_hash"])
        self.assertIn("spec:R90", p1)
        self.assertIn("spec:R89", p2)
        self.assertIn(f"Context pack hash (copy into the card): {b1['context_pack_hash']}", p1)
        self.assertIn("context_pack_hash", out[0]["prompts"][0]["schema_keys"])
        self.assertTrue(p3.startswith("You implement one small work package."))
        self.assertNotIn("Context pack hash", p3)


if __name__ == "__main__":
    unittest.main()
