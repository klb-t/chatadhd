"""IW-11: handover — jeden rekord, rendery; IW-10: operator (mandat jako dane, judgement → propozycja)."""
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import handover as ho  # noqa: E402
import operator_run as op  # noqa: E402
import routine as rt  # noqa: E402
import run_record as rr  # noqa: E402

os.environ["RUN_RECORD_HOOKS"] = "off"

HANDWRITTEN = """# Teleport

## 1. Kim jesteś
Koordynator. `CLAUDE.md`

## 2. Czytaj w tej kolejności
`docs/reports/QUEUE_S1.md`

## 3. Stan w chwili teleportu
`r_live`

## 6. Odwrót
git
"""


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "docs/reports").mkdir(parents=True)
        (self.root / "docs/spec").mkdir(parents=True)
        (self.root / "docs/ops").mkdir(parents=True)
        (self.root / "docs/reports/QUEUE_S1.md").write_text("# Kolejka\n\n## Najnowsze zdarzenie\ntekst\n\n## Starsze\n", encoding="utf-8")
        (self.root / "docs/spec/spec.json").write_text(json.dumps({"nodes": [
            {"id": "s_1", "aliases": ["R99"], "title": "parafraza", "understanding": {"state": "proposed"}},
            {"id": "s_2", "aliases": ["R98"], "title": "inna", "understanding": {"state": "approved"}}]}), encoding="utf-8")
        self.cfg = rr.load_config()
        self.pre = ho.presets()

    def tearDown(self):
        self.tmp.cleanup()


class TestHandover(Base):
    def test_one_record_many_renders(self):
        live = rr.start(self.root, self.cfg, "package.run", params={"title": "pakiet"}, run_id="r_live", text="start")["run_id"]
        done = rr.start(self.root, self.cfg, "secretary.run", run_id="r_sec")["run_id"]
        rr.finish(self.root, self.cfg, done, summary="pusta")
        rec = ho.build(self.root, self.pre, now="2026-10-26T18:00:00Z")
        self.assertEqual(rec["schema"], "loom.handover/1")
        self.assertEqual([r["run_id"] for r in rec["runs"]["live"]], [live])
        self.assertFalse(rec["switch"]["ok"])  # predykat: tylko między pakietami
        self.assertEqual(rec["switch"]["blocking_runs"], [live])
        self.assertEqual([x["alias"] for x in rec["spec_waiting"]], ["R99"])
        self.assertEqual(rec["queue_head"][0]["heading"], "Najnowsze zdarzenie")
        self.assertEqual(rec["receipt"][0]["value"], [400000, 920000])  # 2 żywe konteksty × preset
        tele = ho.r_teleport(rec)
        for sid in ("who", "read_order", "state", "tools", "reversal", "takeover"):
            self.assertIn(f"<!-- section: {sid} -->", tele)
        self.assertIn("`r_live`", tele)
        self.assertIn("R99", tele)
        self.assertIn("r_sec", ho.r_secretary(rec, root=self.root))
        self.assertIn("r_live", ho.r_start_prompt(rec))
        self.assertIn("Pracuje jeden bieg", ho.r_owner(rec))
        since = ho.r_since(rec, root=self.root, since="2026-01-01T00:00:00Z", pre=self.pre)
        self.assertIn("Od Twojego wyjścia", since)
        self.assertIn("1 parafraz", since.replace("1 parafraz do", "1 parafraz do"))

    def test_compare_with_handwritten(self):
        rr.start(self.root, self.cfg, "package.run", run_id="r_live")
        gen = ho.r_teleport(ho.build(self.root, self.pre))
        res = ho.compare(gen, HANDWRITTEN, self.pre)
        self.assertTrue(res["equivalent"], res)
        self.assertEqual(len(res["sections"]["covered"]), 4)
        bad = ho.compare(gen, HANDWRITTEN + "\n## 9. Sekcja, której nie ma w mapie\n", self.pre)
        self.assertFalse(bad["equivalent"])
        self.assertEqual(bad["sections"]["missing"][0]["section"], None)

    def test_save_supersedes_and_cli(self):
        out = subprocess.run([sys.executable, str(TOOLS / "handover.py"), "--root", str(self.root), "build", "--save"],
                             capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        first = json.loads((self.root / "docs/ops/handover.json").read_text(encoding="utf-8"))
        self.assertIsNone(first["supersedes"])
        rr.start(self.root, self.cfg, "package.run", run_id="r_new")
        ho.save(self.root, self.pre, ho.build(self.root, self.pre))
        second = json.loads((self.root / "docs/ops/handover.json").read_text(encoding="utf-8"))
        self.assertEqual(second["supersedes"], first["hash"])
        self.assertNotEqual(second["hash"], first["hash"])
        r = subprocess.run([sys.executable, str(TOOLS / "handover.py"), "--root", str(self.root), "render", "nieznany"],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0)
        self.assertIn("loom.handover/1", r.stdout)
        self.assertIn("nieznany render", r.stderr)

    def test_repo_teleport_equivalent_to_handwritten(self):
        """Akceptacja IW-11: render bieżącego rekordu jest równoważny ręcznej karcie TELEPORT_S1.md (preset progu)."""
        hw = REPO / "docs/ops/TELEPORT_S1.md"
        if not hw.exists():
            self.skipTest("brak ręcznej karty")
        res = ho.compare(ho.r_teleport(ho.build(REPO, self.pre)), hw.read_text(encoding="utf-8"), self.pre)
        self.assertEqual(res["sections"]["missing"], [])
        self.assertTrue(res["equivalent"], res["facts"])


class TestOperator(Base):
    def test_mandate_is_data(self):
        cat = rt.load_json(rt.ROUTINES)
        ids = {r["id"] for r in op.ok_routines(cat)}
        for want in ("start.gate", "ci.read", "ledger.sample", "status.lines", "teleport.refresh", "tasks.recreate", "operator.run"):
            self.assertIn(want, ids)
        self.assertNotIn("vm.wake", ids)
        self.assertNotIn("package.run", ids)

    def test_judgement_becomes_proposal_nothing_executed(self):
        cat = rt.load_json(rt.ROUTINES)
        parent = rr.start(self.root, self.cfg, "operator.run", run_id="r_op")["run_id"]
        prop = rt.operator_gate(self.root, cat, "vm.wake", parent)
        self.assertEqual(prop["kind"], "judgement")
        rec, _ = rr.load(self.root, self.cfg, parent)
        self.assertEqual(rec["escalations"][0]["kind"], "judgement")
        self.assertTrue(rec["escalations"][0]["why"])
        self.assertEqual([x for x in rr.read_index(self.root, self.cfg).values() if x["routine"] == "vm.wake"], [])
        self.assertIsNone(rt.operator_gate(self.root, cat, "status.lines", parent))
        r = subprocess.run([sys.executable, str(TOOLS / "routine.py"), "--root", str(self.root), "run", "package.run",
                            "--as", "operator", "--parent-run", parent, "--param", "brief=x"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 4)

    def test_operator_run_end_to_end(self):
        bin_ = self.root / "bin"
        bin_.mkdir()
        gh = bin_ / "gh"
        gh.write_text("#!/bin/sh\necho '[]'\n", encoding="utf-8")  # bez sieci: gh zwraca pustą listę → przekazanie do sesji
        gh.chmod(gh.stat().st_mode | stat.S_IEXEC)
        env = {**os.environ, "PATH": f"{bin_}{os.pathsep}{os.environ.get('PATH', '')}", "RUN_RECORD_HOOKS": "off"}

        def routine(*args):
            p = subprocess.run([sys.executable, str(TOOLS / "routine.py"), "--root", str(self.root), *args],
                               capture_output=True, text=True, env=env, timeout=300)
            self.assertIn(p.returncode, (0, 1), p.stderr + p.stdout)
            return p.stdout.strip().splitlines()[-1].split(" ")

        rid, state = routine("run", "operator.run", "--where", "cloud:scheduled")
        self.assertEqual(state, "running")  # czeka na część sekretarza (przekazanie)
        rid2, state = routine("resume", rid, "--done", "secretary")
        self.assertEqual(rid2, rid)
        rec, _ = rr.load(self.root, self.cfg, rid)
        self.assertEqual(rec["steps"][-1]["id"], "session_parts")
        s = op.summary(self.root, rid)
        kids = {k["routine"]: k for k in s["children"]}
        for want in ("start.gate", "ci.read", "status.lines", "teleport.refresh"):
            self.assertIn(want, kids)
        self.assertEqual(kids["status.lines"]["state"], "done")
        self.assertEqual(kids["teleport.refresh"]["state"], "done")
        self.assertTrue((self.root / "docs/ops/TELEPORT_S1.generated.md").exists())
        self.assertTrue((self.root / "docs/ops/handover.json").exists())
        _, state = routine("resume", rid, "--done", "session_parts")
        self.assertEqual(state, "done")
        rec, _ = rr.load(self.root, self.cfg, rid)
        card = next(x for x in rec["steps"] if x["id"] == "card")
        self.assertIsInstance(card["out"], dict)
        cad = next(x for x in rec["steps"] if x["id"] == "cadence")["out"]
        self.assertFalse(cad["applied"])  # kadencja: tylko propozycja

    def test_cadence_proposal_from_measurement(self):
        cat = rt.load_json(rt.ROUTINES)
        for i in range(30):
            r = rr.start(self.root, self.cfg, "secretary.run", run_id=f"r_s{i:02d}")["run_id"]
            rr.finish(self.root, self.cfg, r, summary="pusta", quiet=True)
        c = op.cadence(self.root, cat)
        self.assertEqual(c["measured"]["empty"], 30)
        self.assertEqual(c["proposal"], op.operator_presets(cat)["cadence"]["slower_cron"])
        self.assertFalse(c["applied"])


if __name__ == "__main__":
    unittest.main()
