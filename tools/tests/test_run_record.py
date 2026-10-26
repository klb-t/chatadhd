# -*- coding: utf-8 -*-
"""Testy tools/run_record.py (IW-1): schemat, zapis, indeks, rendery, importy. Bez sieci, bez modeli.

Uruchomienie:  python3 -m unittest discover -s tools/tests -v
"""
import contextlib
import io
import json
import random
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import run_record as rr  # noqa: E402

try:
    import jsonschema  # noqa: F401
    HAVE_JSONSCHEMA = True
except ImportError:
    HAVE_JSONSCHEMA = False

# Ręczne nagłówki z docs/reports/QUEUE_S1.md (kształt 23.10) — wzorzec formy, z którym porównujemy render.
MANUAL_HEADINGS = [
    "## Naprawa dwu testów GOTOWA (17:41 UTC, `51bfe946` na `claude/m1-w1` po merge main 06119372; Sonnet, 17 wywołań, "
    "99 tys. tokenów, 10 min): test motywu czeka na odpowiedź PUT /api/config.",
    "## Kontrola syntezy panelu GOTOWA (20:20 UTC, `wf_04712c04-50f`, Haiku + Sonnet, 397 tys. tokenów, 16 min): "
    "29 znalezisk.",
]
QUEUE_FORM = re.compile(r"^## .+? \((\d{2}:\d{2}) UTC, `[^`]+`[^)]*\): .+$")


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="rr_"))
        self.cfg = rr.load_config()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = rr.main(["--root", str(self.tmp), *argv])
        return code, out.getvalue(), err.getvalue()


class TestSchema(Base):
    def test_schema_file_is_valid_json_schema(self):
        s = rr.schema()
        self.assertEqual(s["$id"], "loom.routine_run/1")
        if HAVE_JSONSCHEMA:
            jsonschema.Draft202012Validator.check_schema(s)

    def test_minimal_record_valid_and_missing_fields_reported(self):
        rec = rr.start(self.tmp, self.cfg, "x", at="2026-10-26T00:00:00Z")
        for eng in ["mini"] + (["jsonschema"] if HAVE_JSONSCHEMA else []):
            self.assertEqual(rr.validate_record(rec, eng), [], eng)
            bad = dict(rec)
            del bad["events"]
            self.assertTrue(rr.validate_record(bad, eng), eng)
            bad = dict(rec, state="")
            self.assertTrue(rr.validate_record(bad, eng), eng)
            bad = dict(rec, schema="other/1")
            self.assertTrue(rr.validate_record(bad, eng), eng)

    def test_open_lists_accept_unknown_fields_states_and_kinds(self):
        rec = rr.start(self.tmp, self.cfg, "x", at="2026-10-26T00:00:00Z")
        rec["nowe_pole"] = {"cokolwiek": [1, 2]}
        rec["state"] = "czeka_na_vm"
        rec["events"].append({"at": "2026-10-26T00:01:00Z", "kind": "rodzaj_nieznany", "dowolne": True})
        rec["receipt"].append({"currency": "minuty_jego_czytania", "value": 3, "at": "2026-10-26T00:01:00Z"})
        self.assertEqual(rr.validate_record(rec, "mini"), [])

    @unittest.skipUnless(HAVE_JSONSCHEMA, "jsonschema niedostępny")
    def test_mini_validator_agrees_with_jsonschema_on_random_mutations(self):
        """Własność: mini-walidator (bez zależności) i jsonschema zgadzają się co do ważności losowych mutacji."""
        base = rr.start(self.tmp, self.cfg, "x", at="2026-10-26T00:00:00Z", params={"a": 1})
        rng = random.Random(1026)
        values = [None, 0, 1.5, True, "", "t", [], [1], {}, {"k": "v"}]
        keys = list(base.keys()) + ["extra"]
        for _ in range(400):
            rec = json.loads(json.dumps(base))
            for _ in range(rng.randint(1, 3)):
                k = rng.choice(keys)
                if rng.random() < 0.15 and k in rec:
                    del rec[k]
                elif k == "events" and rng.random() < 0.5:
                    rec["events"] = [rng.choice([{"at": "t", "kind": "k"}, {"kind": "k"}, {"at": "t", "kind": ""},
                                                 {"at": "t", "kind": "k", "counter_delta": {"a": "x"}}, 3])]
                else:
                    rec[k] = rng.choice(values)
            a = bool(rr.validate_record(rec, "mini"))
            b = bool(rr.validate_record(rec, "jsonschema"))
            self.assertEqual(a, b, json.dumps(rec, ensure_ascii=False)[:300])


class TestRecord(Base):
    def test_full_lifecycle_via_cli(self):
        code, rid, _ = self.cli("--at", "2026-10-26T10:00:00Z", "start", "--routine", "package.run@1",
                                "--param", "id=IW-1", "--param", "title=Zapis biegu v1",
                                "--resolved", '{"model": "opus"}', "--spec", "R90")
        self.assertEqual(code, 0)
        rid = rid.strip()
        self.assertTrue(rid.startswith("r_") and len(rid) == 14)
        steps = [
            ("--at", "2026-10-26T10:05:00Z", "step", rid, "--id", "schema", "--state", "done", "--tokens", "100"),
            ("--at", "2026-10-26T10:06:00Z", "step", rid, "--id", "schema", "--out-hash", "sha256:aa"),
            ("--at", "2026-10-26T10:10:00Z", "event", rid, "--kind", "building", "--text", "testy"),
            ("--at", "2026-10-26T10:11:00Z", "event", rid, "--kind", "original_ideas", "--text", "indeks dopisywany"),
            ("--at", "2026-10-26T10:12:00Z", "receipt", rid, "--currency", "tokens", "--value", "99000",
             "--unit", "tys. tokenów", "--source", "usage_report"),
            ("--at", "2026-10-26T10:13:00Z", "decision", rid, "--text", "indeks dopisywany", "--reversal", "reindex"),
            ("add", rid, "commits", '"abcdef123456"'),
            ("add", rid, "advisories", "zwykły tekst"),
            ("set", rid, "where", "vm"),
            ("--at", "2026-10-26T10:40:00Z", "finish", rid, "--summary", "schemat i skrypt"),
        ]
        for argv in steps:
            self.assertEqual(self.cli(*argv)[0], 0, argv)
        rec, path = rr.load(self.tmp, self.cfg, rid)
        self.assertEqual(path.parent.name, "2026-10")
        self.assertEqual(rec["state"], "done")
        self.assertEqual(rec["routine"], {"id": "package.run", "version": "1", "hash": None})
        self.assertEqual(len(rec["steps"]), 1)
        self.assertEqual(rec["steps"][0]["out_hash"], "sha256:aa")
        self.assertEqual(rec["steps"][0]["tokens"], 100)
        self.assertEqual([e["kind"] for e in rec["events"]],
                         ["started", "building", "original_ideas", "decision", "finished"])
        self.assertEqual(rec["events"][-1]["counter_delta"], {"w_toku": -1, "zrobione": 1})
        self.assertEqual(rec["advisories"], ["zwykły tekst"])
        self.assertEqual(rec["where"], "vm")
        self.assertEqual(rec["spec"], ["R90"])
        self.assertEqual(self.cli("validate")[0], 0)
        self.assertEqual(self.cli("validate", "--engine", "mini")[0], 0)

    def test_protected_fields_and_unknown_run(self):
        rec = rr.start(self.tmp, self.cfg, "x")
        self.assertEqual(self.cli("set", rec["run_id"], "run_id", '"y"')[0], 2)
        self.assertEqual(self.cli("event", "r_nieistnieje", "--kind", "x")[0], 2)
        self.assertEqual(self.cli("start", "--routine", "x", "--run-id", rec["run_id"])[0], 2)

    def test_invalid_mutation_is_refused_and_file_unchanged(self):
        rec = rr.start(self.tmp, self.cfg, "x")
        before = rr.find_path(self.tmp, self.cfg, rec["run_id"]).read_text()
        self.assertEqual(self.cli("set", rec["run_id"], "events", '"nie lista"')[0], 2)
        self.assertEqual(rr.find_path(self.tmp, self.cfg, rec["run_id"]).read_text(), before)

    def test_delta_must_be_numeric_and_no_default_delta(self):
        rec = rr.start(self.tmp, self.cfg, "x")
        self.assertEqual(self.cli("event", rec["run_id"], "--kind", "k", "--delta", "a=b")[0], 2)
        self.cli("event", rec["run_id"], "--kind", "original_ideas", "--no-default-delta")
        self.assertNotIn("counter_delta", rr.load(self.tmp, self.cfg, rec["run_id"])[0]["events"][-1])

    def test_finish_quiet_removes_counter_deltas(self):
        rid = rr.start(self.tmp, self.cfg, "secretary.run")["run_id"]
        self.assertEqual(self.cli("finish", rid, "--summary", "pusta", "--quiet")[0], 0)
        self.assertEqual(rr.render_status_lines(self.tmp, self.cfg), [])
        self.assertEqual(rr.render_queue(self.tmp, self.cfg), [])
        self.assertEqual(len(rr.render_queue(self.tmp, self.cfg, include_empty=True)), 1)

    def test_index_is_append_only_last_snapshot_wins_and_reindex_compacts(self):
        a = rr.start(self.tmp, self.cfg, "a", at="2026-09-30T23:59:00Z")
        b = rr.start(self.tmp, self.cfg, "b", at="2026-10-01T00:00:00Z")
        rr.finish(self.tmp, self.cfg, a["run_id"], state="failed")
        lines = rr.index_path(self.tmp, self.cfg).read_text().splitlines()
        self.assertEqual(len(lines), 3)
        idx = rr.read_index(self.tmp, self.cfg)
        self.assertEqual(idx[a["run_id"]]["state"], "failed")
        self.assertEqual(idx[b["run_id"]]["state"], "running")
        self.assertIn("2026-09", idx[a["run_id"]]["path"])
        # uszkodzona linia (np. po ręcznym scaleniu) nie blokuje odczytu
        with open(rr.index_path(self.tmp, self.cfg), "a") as fh:
            fh.write("<<<<<<< konflikt\n")
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(len(rr.read_index(self.tmp, self.cfg)), 2)
        self.assertEqual(self.cli("reindex")[0], 0)
        self.assertEqual(len(rr.index_path(self.tmp, self.cfg).read_text().splitlines()), 2)

    def test_custom_config_overrides_presets(self):
        cfg_file = self.tmp / "cfg.json"
        cfg_file.write_text(json.dumps({"runs_dir": "inne/biegi", "id_prefix": "bieg_",
                                        "status_lines": {"labels": {"started": "ZACZYNAM"}}}))
        code, rid, _ = self.cli("--config", str(cfg_file), "start", "--routine", "x")
        self.assertTrue(rid.startswith("bieg_"))
        self.assertTrue(any((self.tmp / "inne" / "biegi").glob("*/*.json")))
        code, out, _ = self.cli("--config", str(cfg_file), "render", "status-lines")
        self.assertIn("ZACZYNAM", out)


class TestRender(Base):
    def _package(self):
        rec = rr.start(self.tmp, self.cfg, "package.run", params={"title": "Naprawa dwu testów"},
                       resolved={"model": "Sonnet"}, at="2026-10-23T17:31:00Z")
        rid = rec["run_id"]
        rr.add_to_list(self.tmp, self.cfg, rid, "commits", "51bfe946aa")
        rr.add_receipt(self.tmp, self.cfg, rid, "calls", 17, unit="wywołań", at="2026-10-23T17:40:00Z")
        rr.add_receipt(self.tmp, self.cfg, rid, "tokens", 99, unit="tys. tokenów", at="2026-10-23T17:40:00Z")
        rr.finish(self.tmp, self.cfg, rid, summary="test motywu czeka na odpowiedź PUT", at="2026-10-23T17:41:00Z")
        return rid

    def test_queue_section_has_the_form_of_manual_entries(self):
        for h in MANUAL_HEADINGS:
            self.assertRegex(h, QUEUE_FORM)
        rid = self._package()
        out = rr.render_queue(self.tmp, self.cfg)
        self.assertEqual(len(out), 1)
        self.assertRegex(out[0], QUEUE_FORM)
        self.assertEqual(out[0], f"## Naprawa dwu testów GOTOWE (17:41 UTC, `{rid}`; Sonnet; `51bfe946`; 17 wywołań; "
                                 "99 tys. tokenów; 10 min): test motywu czeka na odpowiedź PUT")

    def test_status_lines_only_for_events_that_change_a_counter(self):
        rid = self._package()
        rr.add_event(self.tmp, self.cfg, rid, "building", text="bez delty", at="2026-10-23T17:35:00Z")
        rr.add_event(self.tmp, self.cfg, rid, "original_ideas", text="pomysł", at="2026-10-23T17:36:00Z")
        lines = rr.render_status_lines(self.tmp, self.cfg)
        self.assertEqual(lines, ["START 17:31 UTC — Naprawa dwu testów",
                                 "ORYGINALNE 17:36 UTC — Naprawa dwu testów: pomysł",
                                 "GOTOWE 17:41 UTC — Naprawa dwu testów: test motywu czeka na odpowiedź PUT"])
        self.assertEqual(len(rr.render_status_lines(self.tmp, self.cfg, filt="all")), 4)
        self.assertEqual(rr.render_status_lines(self.tmp, self.cfg, filt="kinds", kinds=["building"]),
                         ["BUDUJĘ 17:35 UTC — Naprawa dwu testów: bez delty"])
        self.assertEqual(len(rr.render_status_lines(self.tmp, self.cfg, since="2026-10-23T17:40:00Z")), 1)

    def test_counter_sums_match_records(self):
        """Własność: suma delt „w_toku” = liczba biegów w stanie running (dla stanów z presetu)."""
        rng = random.Random(7)
        ids = []
        for i in range(30):
            ids.append(rr.start(self.tmp, self.cfg, "r", at=f"2026-10-2{i % 9}T00:00:00Z")["run_id"])
        for rid in ids:
            c = rng.random()
            if c < 0.4:
                rr.finish(self.tmp, self.cfg, rid, state="done")
            elif c < 0.6:
                rr.finish(self.tmp, self.cfg, rid, state="failed")
        recs = rr.all_records(self.tmp, self.cfg)
        total = sum((e.get("counter_delta") or {}).get("w_toku", 0) for r in recs for e in r["events"])
        self.assertEqual(total, sum(1 for r in recs if r["state"] == "running"))
        failed = sum((e.get("counter_delta") or {}).get("nie_wyszlo", 0) for r in recs for e in r["events"])
        self.assertEqual(failed, sum(1 for r in recs if r["state"] == "failed"))


class TestImports(Base):
    def test_import_card_from_single_package(self):
        card = {"done": True, "commits": ["aaa111", "bbb222"], "ci": "not pushed", "summary": "zrobione",
                "decisions": ["indeks dopisywany; odwrót: reindex"], "not_done": ["prompt sekretarza w zadaniu"],
                "context_pack_hash": "sha256:cc", "spec": ["R90"]}
        p = self.tmp / "card.json"
        p.write_text(json.dumps(card))
        code, rid, _ = self.cli("--at", "2026-10-26T12:00:00Z", "import-card", str(p), "--param", "id=IW-1",
                                "--model", "opus")
        self.assertEqual(code, 0)
        rec = rr.load(self.tmp, self.cfg, rid.strip())[0]
        self.assertEqual(rec["state"], "done")
        self.assertEqual(rec["commits"], ["aaa111", "bbb222"])
        self.assertEqual(rec["context_pack_hash"], "sha256:cc")
        self.assertEqual(rec["advisories"], [{"kind": "not_done", "text": "prompt sekretarza w zadaniu"}])
        self.assertEqual(rec["decisions"][0]["by"], "agent")
        # karta przerwanego agenta → stan „paused” (preset), bieg istniejący dostaje kartę dopisaną
        rid2 = rr.start(self.tmp, self.cfg, "package.run")["run_id"]
        rr.import_card(self.tmp, self.cfg, {"done": False, "commits": [], "ci": "-", "summary": "s",
                                            "not_done": ["x"]}, run_id=rid2)
        rec2 = rr.load(self.tmp, self.cfg, rid2)[0]
        self.assertEqual(rec2["state"], "paused")
        self.assertEqual(len(rec2["cards"]), 1)

    def test_import_spec_runs_idempotent_quiet_when_empty(self):
        d = self.tmp / "spec_runs"
        d.mkdir()
        (d / "20261023T1123.json").write_text(json.dumps(
            {"applied": 0, "at": "2026-10-23T11:23:41Z", "commands": [], "escalated": 0, "new": 0,
             "note": "pusta", "session": None}))
        (d / "20261024T2323.json").write_text(json.dumps(
            {"applied": 2, "at": "2026-10-24T23:23Z", "commands": ["spec.py approve R90", "spec.py add R99"],
             "escalated": 1, "new": 3, "note": "praca", "session": "session_x"}))
        code, out, _ = self.cli("import-spec-runs", str(d))
        self.assertIn("zaimportowano 2", out)
        code, out, _ = self.cli("import-spec-runs", str(d))
        self.assertIn("zaimportowano 0", out)
        empty = rr.load(self.tmp, self.cfg, "r_spec_20261023T1123")[0]
        work = rr.load(self.tmp, self.cfg, "r_spec_20261024T2323")[0]
        self.assertEqual(empty["routine"]["id"], "secretary.run")
        self.assertEqual(work["started"], "2026-10-24T23:23:00Z")
        self.assertEqual([s["command"] for s in work["steps"]], ["spec.py approve R90", "spec.py add R99"])
        self.assertEqual(work["escalations"], [{"count": 1}])
        self.assertEqual(work["provenance"]["from"], "spec_runs")
        lines = rr.render_status_lines(self.tmp, self.cfg)
        self.assertTrue(lines and all("23:23" in x for x in lines), lines)  # pusty bieg nie daje linii statusu
        self.assertEqual(self.cli("validate")[0], 0)


class TestRepoRecords(unittest.TestCase):
    """Rekordy zapisane w repo są zgodne ze schematem (jeśli już są)."""

    def test_repo_runs_validate(self):
        cfg = rr.load_config()
        files = sorted(rr.runs_dir(rr.REPO, cfg).glob("*/*.json"))
        for f in files:
            self.assertEqual(rr.validate_record(json.loads(f.read_text(encoding="utf-8")), "mini"), [], str(f))


if __name__ == "__main__":
    unittest.main()
