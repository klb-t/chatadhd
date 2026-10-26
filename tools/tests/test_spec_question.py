"""Testy IW-3: pytanie w pełnym kształcie (ask_after, criteria, class, first_shown), rozstrzygnięcie milczeniem z rekordem
decyzji i odwrotem, skrzynka z klasą (approval/correction bez modelu, reszta eskalowana). Bez sieci, bez modeli.

Uruchomienie:  python3 -m unittest tools/tests/test_spec_question.py
"""
from __future__ import annotations

import datetime as dt
import json
import shutil
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import spec  # noqa: E402
import run_record as rr  # noqa: E402
from test_spec import load, make_root, node, run  # noqa: E402

T0 = "2026-10-26T08:00:00Z"


class Base(unittest.TestCase):
    def setUp(self):
        self.root = make_root()
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        run(self.root, "build")

    def und(self, alias):
        return node(load(self.root), alias)["understanding"]

    def silent(self, *extra):
        rc, out = run(self.root, "silent", *extra)
        return json.loads(out)


class Duration(unittest.TestCase):
    def test_open_forms(self):
        P = spec.parse_duration
        self.assertEqual(P("24h"), dt.timedelta(hours=24))
        self.assertEqual(P("2d"), dt.timedelta(days=2))
        self.assertEqual(P("1d12h"), dt.timedelta(hours=36))
        self.assertEqual(P("90m"), dt.timedelta(minutes=90))
        self.assertEqual(P("3 dni"), dt.timedelta(days=3))
        self.assertEqual(P("PT6H"), dt.timedelta(hours=6))
        self.assertEqual(P("P1DT1H"), dt.timedelta(hours=25))
        self.assertEqual(P("1.5"), dt.timedelta(hours=1.5))
        for bad in ("jutro", "", "PT", "24x"):
            self.assertIsNone(P(bad), bad)

    def test_due_from_first_shown_or_absolute(self):
        q = spec.DEFAULT_CONFIG["question"]
        self.assertEqual(spec.question_due({"first_shown": T0, "ask_after": "24h"}, q), spec.parse_when("2026-10-27T08:00:00Z"))
        self.assertEqual(spec.question_due({"first_shown": T0, "ask_after": "2026-10-30T00:00Z"}, q), spec.parse_when("2026-10-30T00:00:00Z"))
        self.assertIsNone(spec.question_due({"first_shown": T0}, q))  # bez ask_after milczenie niczego nie rozstrzyga (preset)
        self.assertIsNotNone(spec.question_due({"first_shown": T0}, {**q, "default_ask_after": "6h"}))


class FullQuestion(Base):
    def test_propose_stores_question_fields_and_first_shown(self):
        run(self.root, "propose", "R1", "--text", "A.", "--date", "2026-10-26", "--first-shown", T0, "--ask-after", "24h",
            "--criteria", "zgodne z R59", "--criteria", "odwrót jedną komendą", "--class", "understanding",
            "--if-silent", "przyjmuję", "--cost-to-reverse", "low")
        u = self.und("R1")
        self.assertEqual((u["first_shown"], u["ask_after"], u["class"]), (T0, "24h", "understanding"))
        self.assertEqual(u["criteria"], ["zgodne z R59", "odwrót jedną komendą"])
        self.assertEqual(u["history"][-1]["criteria"], ["zgodne z R59", "odwrót jedną komendą"])
        run(self.root, "correct", "R1", "--text", "B.")  # bez flag: zostaje
        self.assertEqual(self.und("R1")["criteria"], ["zgodne z R59", "odwrót jedną komendą"])
        run(self.root, "propose", "R1", "--text", "C.", "--criteria", "", "--ask-after", "")
        u = self.und("R1")
        self.assertNotIn("criteria", u)
        self.assertNotIn("ask_after", u)
        self.assertNotEqual(u["first_shown"], T0)  # nowy tekst = nowe pokazanie
        rc, out = run(self.root, "check")
        self.assertEqual(rc, 0, out)
        rc, out = run(self.root, "show", "R1")
        self.assertIn("pokazane:", out)

    def test_same_text_keeps_first_shown_and_shown_command(self):
        run(self.root, "propose", "R1", "--text", "A.", "--first-shown", T0)
        run(self.root, "propose", "R1", "--text", "A.", "--cost-to-reverse", "low")
        self.assertEqual(self.und("R1")["first_shown"], T0)
        run(self.root, "shown", "R1", "--at", "2026-10-26T09:00:00Z")
        self.assertEqual(self.und("R1")["first_shown"], T0)  # już był; bez --force nie nadpisuje
        run(self.root, "shown", "R1", "--at", "2026-10-26T09:00:00Z", "--force")
        self.assertEqual(self.und("R1")["first_shown"], "2026-10-26T09:00:00Z")


class Silence(Base):
    def test_one_question_resolved_by_silence_with_decision_record_and_reversal(self):
        cfg = rr.load_config()
        rec = rr.start(self.root, cfg, "secretary.run", at="2026-10-27T09:23:00Z")
        run(self.root, "propose", "R1", "--text", "A.", "--first-shown", T0, "--ask-after", "24h",
            "--if-silent", "przyjmuję A i jadę dalej", "--cost-to-reverse", "low")
        # przed czasem: czeka
        res = self.silent("--now", "2026-10-27T07:59:00Z", "--run-id", rec["run_id"])
        self.assertEqual([r["alias"] for r in res["waiting"]], ["R1"])
        self.assertEqual(self.und("R1")["state"], "proposed")
        # po czasie: rozstrzygnięte milczeniem
        res = self.silent("--now", "2026-10-27T09:23:00Z", "--run-id", rec["run_id"])
        self.assertEqual([r["alias"] for r in res["applied"]], ["R1"])
        u = self.und("R1")
        self.assertEqual((u["state"], u["by"]), ("approved", "silence"))
        d = u["decision"]
        self.assertEqual(d["reversal"], "python3 tools/spec.py reopen R1")
        self.assertEqual((d["if_silent"], d["cost_to_reverse"], d["basis"]), ("przyjmuję A i jadę dalej", "low", "ask_after"))
        r2, _ = rr.load(self.root, cfg, rec["run_id"])
        self.assertEqual(r2["decisions"][-1]["reversal"], "python3 tools/spec.py reopen R1")
        self.assertEqual(rr.validate_record(r2), [])
        rc, out = run(self.root, "check")
        self.assertEqual(rc, 0, out)
        # drugi bieg niczego nie powtarza
        self.assertEqual(self.silent("--now", "2026-10-28T00:00:00Z")["applied"], [])
        # odwrót
        run(self.root, "reopen", "R1", "--at", "2026-10-27T12:00:00Z", "--why", "jednak nie")
        u = self.und("R1")
        self.assertEqual((u["state"], u["text"], u["first_shown"]), ("proposed", "A.", "2026-10-27T12:00:00Z"))
        self.assertNotIn("decision", u)
        self.assertEqual(u["history"][-1]["reverses"]["by"], "secretary")
        # zatwierdzenie właściciela po milczeniu jest dozwolone (potwierdza)
        run(self.root, "silent", "--now", "2026-10-29T00:00:00Z")
        run(self.root, "approve", "R1")
        self.assertEqual(self.und("R1")["by"], "owner")

    def test_expensive_or_unknown_cost_escalates_never_applies(self):
        run(self.root, "propose", "R1", "--text", "A.", "--first-shown", T0, "--ask-after", "1h", "--cost-to-reverse", "high")
        run(self.root, "propose", "R2", "--text", "B.", "--first-shown", T0, "--ask-after", "1h")
        run(self.root, "propose", "R3", "--text", "C.", "--first-shown", T0, "--cost-to-reverse", "low")  # bez ask_after
        res = self.silent("--now", "2026-10-30T00:00:00Z")
        self.assertEqual(res["applied"], [])
        self.assertEqual(sorted(r["alias"] for r in res["escalated"]), ["R1", "R2"])
        self.assertEqual([r["alias"] for r in res["waiting"]], ["R3"])
        # lista niskich kosztów jest otwarta: flaga ją zastępuje
        res = self.silent("--now", "2026-10-30T00:00:00Z", "--low", "high", "--dry-run")
        self.assertEqual([r["alias"] for r in res["applied"]], ["R1"])
        self.assertEqual(self.und("R1")["state"], "proposed")  # dry-run

    def test_hard_limit_is_a_preset(self):
        for a in ("R1", "R2"):
            run(self.root, "propose", a, "--text", "x", "--first-shown", T0, "--ask-after", "1h", "--cost-to-reverse", "tani")
        res = self.silent("--now", "2026-10-30T00:00:00Z", "--limit", "1")
        self.assertEqual((len(res["applied"]), len(res["skipped_limit"])), (1, 1))
        res = self.silent("--now", "2026-10-30T00:00:00Z", "--limit", "-1")
        self.assertEqual(len(res["applied"]), 1)


class Inbox(Base):
    def inbox(self, entries, *extra):
        f = self.root / "inbox.json"
        f.write_text(json.dumps(entries, ensure_ascii=False), encoding="utf-8")
        rc, out = run(self.root, "inbox", str(f), "--at", "2026-10-26T10:00:00Z", *extra)
        return json.loads(out)["verdicts"]

    def test_zatwierdzam_is_approve_without_model(self):
        run(self.root, "propose", "R1", "--text", "A.")
        run(self.root, "propose", "R2", "--text", "B.")
        v = self.inbox([{"id": "d1", "node": "R1", "kind": "approve", "text": "", "state": "new"},
                        {"id": "d2", "node": "R2", "class": "statement", "text": "Zatwierdzam.", "state": "new"},
                        {"id": "d3", "node": "R1", "kind": "approve", "state": "applied"}])
        self.assertEqual([(x["doc_id"], x["class"], x["verdict"]) for x in v],
                         [("d1", "approval", "applied"), ("d2", "statement", "applied")])
        self.assertEqual((self.und("R1")["state"], self.und("R2")["state"]), ("approved", "approved"))

    def test_correction_applied_other_classes_escalated_open_list(self):
        run(self.root, "propose", "R1", "--text", "A.")
        v = self.inbox({"docs": [{"id": "d1", "data": {"node": "R1", "kind": "correct", "text": "Raczej B.", "state": "new"}},
                                 {"id": "d2", "data": {"node": "R1", "class": "marker", "text": "to przykład", "state": "new"}},
                                 {"id": "d3", "data": {"node": "R1", "class": "routine_request", "text": "odpal", "state": "new"}},
                                 {"id": "d4", "data": {"node": "R9", "class": "approval", "state": "new"}}]})
        by = {x["doc_id"]: x for x in v}
        self.assertEqual(by["d1"]["verdict"], "applied")
        self.assertEqual(self.und("R1")["corrected_text"], "Raczej B.")
        self.assertEqual((by["d2"]["verdict"], by["d3"]["verdict"]), ("escalated", "escalated"))
        self.assertIn("marker", by["d2"]["why"])
        self.assertEqual(by["d4"]["verdict"], "rejected")  # nie ma pozycji R9

    def test_dry_run_changes_nothing(self):
        run(self.root, "propose", "R1", "--text", "A.")
        v = self.inbox([{"id": "d1", "node": "R1", "class": "approval", "state": "new"}], "--dry-run")
        self.assertEqual(v[0]["verdict"], "would-apply")
        self.assertEqual(self.und("R1")["state"], "proposed")


if __name__ == "__main__":
    unittest.main()
