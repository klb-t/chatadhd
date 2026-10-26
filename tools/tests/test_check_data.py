"""IW-9: kontrola emituje dane; znacznik → addendum."""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
sys.path.insert(0, str(TOOLS))
import check_data as cd  # noqa: E402
import run_record as rr  # noqa: E402

os.environ["RUN_RECORD_HOOKS"] = "off"

DOC_V1 = """# Projekt X

Tekst, przykład: telefon. Pomiar 23.10: 37/60 commitów, tura ≈ 0,28 mln tokenów, 12 min.

## §5 Decyzje z odwrotem

| Decyzja | Dlaczego | Odwrót |
|---|---|---|
| Zapis biegu jest jednym źródłem | trzy kąty | proza jak dziś |
| Jedna skrzynka z `class`; dwie tabele | dwa stany | rozdzielenie |
| Stara decyzja do usunięcia | bo tak | brak |

## §6 Inne
- punkt poza decyzjami
"""

DOC_V2 = DOC_V1.replace("| trzy kąty |", "| trzy kąty czytają jeden rekord |") \
    .replace("| Stara decyzja do usunięcia | bo tak | brak |\n", "| Nowa decyzja | pomiar | wyłączyć |\n") \
    + "\n## Kontrola przed użyciem (addendum)\nZastosowano 3.\n"


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.pre = cd.presets()
        self.doc = "docs/architecture/X.md"
        (self.root / "docs/architecture").mkdir(parents=True)
        (self.root / self.doc).write_text(DOC_V1, encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()


class TestExtractAndDiff(Base):
    def test_extract_fields(self):
        d = cd.extract(DOC_V1, self.pre)
        self.assertEqual([x["text"] for x in d["decisions"]][0], "Zapis biegu jest jednym źródłem")
        self.assertEqual(d["decisions"][0]["reversal"], "proza jak dziś")
        self.assertEqual(d["decisions"][0]["why"], "trzy kąty")
        self.assertEqual(len(d["decisions"]), 3)  # punkt z §6 nie jest decyzją
        self.assertTrue(any("przykład" in x["text"] for x in d["examples"]))
        vals = [m["value"] for m in d["measurements"]]
        self.assertIn("12 min", vals)
        self.assertTrue(any("mln" in v for v in vals))

    def test_diff_against_previous_save(self):
        cd.save_data(self.root, self.doc, cd.from_extract(self.root, self.doc, self.pre), self.pre)
        (self.root / self.doc).write_text(DOC_V2, encoding="utf-8")
        cd.save_data(self.root, self.doc, cd.from_extract(self.root, self.doc, self.pre), self.pre)
        r = cd.render(self.root, self.doc, self.pre)
        dd = r["diff"]
        self.assertEqual([x["text"] for x in dd["added"]], ["Nowa decyzja"])
        self.assertEqual([x["text"] for x in dd["removed"]], ["Stara decyzja do usunięcia"])
        self.assertEqual(dd["changed"][0]["fields"], ["why"])
        self.assertEqual(dd["unchanged"], 1)
        md = cd.render_md(r)
        self.assertIn("+ Nowa decyzja", md)
        self.assertIn("− Stara decyzja", md)
        self.assertIn("Pełny tekst: docs/architecture/X.md", md)
        self.assertEqual(r["counts"]["additions"], 1)

    def test_ingest_workflow_return(self):
        ret = {"checks": [{"doc": self.doc, "blocking": 1}], "fixes": [],
               "data": [{"doc": self.doc, "verdict": "usable after fixes", "r60_answer": "nie",
                         "findings": [{"line": 3, "quote": "przykład: telefon", "pattern": "A", "severity": "blocking",
                                       "why": "w", "fix": "f"}],
                         "decisions": [{"text": "Zapis biegu jest jednym źródłem", "reversal": "proza"}],
                         "examples": [{"text": "telefon"}], "additions": [], "measurements": [{"value": "12 min"}]}]}
        p = self.root / "ret.json"
        p.write_text(json.dumps(ret), encoding="utf-8")
        self.assertEqual(cd.main(["--root", str(self.root), "ingest", str(p), "--run-id", "r_x"]), 0)
        data = cd.load_sidecar(self.root, self.doc, self.pre)
        self.assertEqual(data["schema"], "loom.check_data/1")
        self.assertEqual(data["source"]["kind"], "check")
        self.assertEqual(data["findings"][0]["state"], "open")
        self.assertTrue(data["findings"][0]["id"].startswith("f_"))
        self.assertEqual(data["r60"], "nie")
        # ponowny ingest tego samego: znalezisko nie dubluje się, stan zostaje
        cd.resolve(self.root, self.doc, self.pre, data["findings"][0]["id"], "rejected", "nie łamie reguły", None)
        cd.main(["--root", str(self.root), "ingest", str(p)])
        data = cd.load_sidecar(self.root, self.doc, self.pre)
        self.assertEqual(len(data["findings"]), 1)
        self.assertEqual(data["findings"][0]["state"], "rejected")


class TestMarkerToAddendum(Base):
    def inbox(self):
        return [{"id": "i1", "class": "marker", "node": self.doc, "text": "to przykład, nie wymaganie", "at": "2026-10-26T18:00:00Z"},
                {"id": "i2", "class": "approval", "node": "R90", "text": "Zatwierdzam"},
                {"id": "i3", "class": "marker", "node": "docs/inny.md", "text": "inny dokument"}]

    def test_marker_becomes_finding_and_addendum_record(self):
        found = cd.marker_findings(self.root, self.doc, self.pre, self.inbox(), {})
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]["origin"], "marker")
        _, n = cd.add_markers(self.root, self.doc, self.pre, found)
        self.assertEqual(n, 1)
        _, n = cd.add_markers(self.root, self.doc, self.pre, found)
        self.assertEqual(n, 0)  # idempotentne
        res = cd.addendum(self.root, self.doc, self.pre)
        self.assertEqual(res["findings"], 1)
        self.assertEqual(res["waits_for"], [])
        cfg = rr.load_config()
        rec, _ = rr.load(self.root, cfg, res["run_id"])
        self.assertEqual(rec["routine"]["id"], "addendum.apply")
        self.assertEqual(rec["state"], "paused")
        self.assertEqual(rec["params"]["doc"], self.doc)
        self.assertEqual(rec["advisories"][0]["origin"], "marker")
        data = cd.load_sidecar(self.root, self.doc, self.pre)
        fid = data["findings"][0]["id"]
        self.assertEqual(data["findings"][0]["state"], "queued")
        cd.resolve(self.root, self.doc, self.pre, fid, "applied", None, None)
        rec, _ = rr.load(self.root, cfg, res["run_id"])
        self.assertEqual(rec["state"], "done")

    def test_running_agent_is_not_restarted(self):
        cfg = rr.load_config()
        running = rr.start(self.root, cfg, "package.run", params={"brief": "x", "doc": self.doc})["run_id"]
        cd.add_markers(self.root, self.doc, self.pre, cd.marker_findings(self.root, self.doc, self.pre, self.inbox(), {}))
        res = cd.addendum(self.root, self.doc, self.pre)
        self.assertEqual(res["waits_for"], [running])
        rec, _ = rr.load(self.root, cfg, running)
        self.assertEqual(rec["state"], "running")  # bieg w toku nietknięty
        add, _ = rr.load(self.root, cfg, res["run_id"])
        self.assertEqual(add["waits_for"], [running])
        self.assertEqual(add["steps"][-1]["state"], "paused")

    def test_gesture_marker_from_words(self):
        g = {"markers": [{"marker": "example", "node": self.doc, "state": "active", "use_id": "u1", "at": "t"},
                         {"marker": "example", "node": self.doc, "state": "reversed", "use_id": "u2"}]}
        found = cd.marker_findings(self.root, self.doc, self.pre, [], g)
        self.assertEqual(len(found), 1)
        self.assertIn("example", found[0]["pattern"])

    def test_routine_end_to_end(self):
        inbox = self.root / "inbox.json"
        inbox.write_text(json.dumps(self.inbox()), encoding="utf-8")
        env = {**os.environ, "RUN_RECORD_HOOKS": "off"}
        r = subprocess.run([sys.executable, str(TOOLS / "routine.py"), "--root", str(self.root), "run", "addendum.apply",
                            "--param", f"doc={self.doc}", "--param", f"inbox={inbox}"],
                           capture_output=True, text=True, env=env)
        self.assertIn(r.returncode, (0, 3), r.stderr + r.stdout)
        cfg = rr.load_config()
        rows = [x for x in rr.read_index(self.root, cfg).values() if x.get("routine") == "addendum.apply"]
        self.assertEqual(len(rows), 1)
        data = cd.load_sidecar(self.root, self.doc, self.pre)
        self.assertEqual([f["state"] for f in data["findings"]], ["queued"])


class TestRepoArtifacts(unittest.TestCase):
    def test_workflow_emits_data(self):
        s = (TOOLS / "workflows" / "check-before-use.js").read_text(encoding="utf-8")
        for k in ("decisions:", "examples:", "additions:", "measurements:", "data: checks", "check_data.py ingest"):
            self.assertIn(k, s)

    def test_repo_sidecar_renders(self):
        pre = cd.presets()
        doc = "docs/architecture/INTERFEJS_WSPOLPRACY_v2_2026-10-23.md"
        if not cd.sidecar(TOOLS.parent, doc, pre).exists():
            self.skipTest("brak pliku danych w repo")
        r = cd.render(TOOLS.parent, doc, pre)
        self.assertGreater(r["counts"]["decisions"], 0)


if __name__ == "__main__":
    unittest.main()
