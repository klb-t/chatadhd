"""IW-7: rutyny jako dane, runner bez stanu własnego, brief jako rekord."""
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import context_pack as cp  # noqa: E402
import routine as rt  # noqa: E402
import run_record as rr  # noqa: E402

PY = sys.executable


def cat_with(*routines):
    base = rt.load_json(rt.ROUTINES)
    return {**base, "routines": list(routines)}


class TestCatalogue(unittest.TestCase):
    def test_repo_catalogue_clean(self):
        self.assertEqual(rt.check_catalogue(rt.load_json(rt.ROUTINES)), [])

    def test_first_routines_present(self):
        cat = rt.load_json(rt.ROUTINES)
        for rid in ("package.run", "check.before_use", "secretary.run", "status.lines", "vm.heartbeat", "vm.sync", "vm.wake"):
            self.assertEqual(rt.find_routine(cat, rid)["id"], rid)

    def test_versions_old_stays_latest_default(self):
        a = {"id": "x", "version": 1, "since": "d", "steps": []}
        b = {"id": "x", "version": 2, "since": "d", "supersedes": 1, "steps": []}
        c = {"id": "x", "version": 10, "since": "d", "supersedes": 2, "steps": []}
        cat = cat_with(a, c, b)
        self.assertEqual(rt.find_routine(cat, "x")["version"], 10)  # 10 > 2 liczbowo, nie tekstowo
        self.assertEqual(rt.find_routine(cat, "x@1"), a)
        self.assertEqual(rt.check_catalogue(cat), [])
        self.assertTrue(any("supersedes" in m for m in rt.check_catalogue(cat_with(b))))
        with self.assertRaises(rt.RoutineError):
            rt.find_routine(cat, "x@7")

    def test_unknown_step_kind_is_advisory(self):
        cat = cat_with({"id": "y", "version": 1, "since": "d", "steps": [{"id": "s", "do": "teleport"}]})
        msgs = rt.check_catalogue(cat)
        self.assertEqual(len(msgs), 1)
        self.assertIn("handoff", msgs[0])


class TestPredicates(unittest.TestCase):
    CTX = {"params": {"a": 3, "s": "abc", "empty": ""}, "place": {"kind": "vm", "state": "alive"}}

    def test_ops(self):
        E = lambda p: rt.evaluate(p, self.CTX)  # noqa: E731
        self.assertTrue(E({"path": "place.kind", "op": "eq", "value": "vm"}))
        self.assertTrue(E({"path": "params.a", "op": "gt", "value": 2}))
        self.assertFalse(E({"path": "params.a", "op": "lt", "value": 2}))
        self.assertTrue(E({"path": "params.s", "op": "matches", "value": "^a"}))
        self.assertTrue(E({"path": "params.nope", "op": "missing"}))
        self.assertTrue(E({"path": "params.empty", "op": "missing"}))
        self.assertFalse(E({"path": "params.nope", "op": "exists"}))
        self.assertTrue(E({"path": "place.state", "op": "in", "value": ["alive", "sleeps"]}))
        self.assertTrue(E({"all": [{"path": "params.a", "op": "ge", "value": 3}, {"not": {"path": "params.s", "op": "eq", "value": "x"}}]}))
        self.assertTrue(E({"any": [False, {"path": "params.s", "op": "truthy"}]}))
        self.assertTrue(E(None))

    def test_unknown_op_and_type_error_do_not_block(self):
        adv = []
        self.assertTrue(rt.evaluate({"path": "params.a", "op": "rhymes", "value": 1}, self.CTX, adv))
        self.assertTrue(rt.evaluate({"path": "params.s", "op": "gt", "value": 1}, self.CTX, adv))
        self.assertEqual(len(adv), 2)

    def test_subst(self):
        ctx = {"params": {"x": "1", "n": 2}, "run_id": "r_1"}
        self.assertEqual(rt.subst(["a{params.x}", "{run_id}", "{params.n}"], ctx), ["a1", "r_1", "2"])
        with self.assertRaises(rt.RoutineError):
            rt.subst("{params.zz}", ctx)


class RunnerBase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.env = mock.patch.dict(os.environ, {"RUN_RECORD_HOOKS": "off"})
        self.env.start()
        self.out = []
        places = self.root / "places.json"
        places.write_text(json.dumps({"places": [{"id": "vm:t", "kind": "vm", "state": "alive"}]}))
        self.places = places
        self.gestures = self.root / "gestures.json"

    def tearDown(self):
        self.env.stop()
        self.tmp.cleanup()

    def runner(self, cat, **kw):
        return rt.Runner(self.root, cat, places_file=self.places, gestures_file=self.gestures, out=self.out.append, **kw)

    def rec(self, rid):
        return rr.load(self.root, rr.load_config(), rid)[0]

    def steps(self, rid):
        return {s["id"]: s for s in self.rec(rid)["steps"]}


class TestRunner(RunnerBase):
    R = {"id": "demo", "version": 1, "since": "2026-10-26", "where": "{params.where}",
         "params": {"msg": {"default": "hej"}, "where": {"default": "vm:t"}},
         "preconditions": [{"check": {"path": "place.state", "op": "eq", "value": "alive"}, "why": "maszyna żyje", "since": "2026-10-26"}],
         "steps": [
             {"id": "one", "do": "cmd", "argv": [PY, "-c", "print('{params.msg}')"], "capture": "one"},
             {"id": "skip_me", "do": "cmd", "argv": [PY, "-c", "raise SystemExit(9)"], "when": {"path": "params.never", "op": "truthy"}},
             {"id": "ask", "do": "confirm", "question": "dalej?", "if_silent": "proceed", "cost_to_reverse": "niski"},
             {"id": "agent", "do": "handoff", "text": "praca poza runnerem dla {run_id}"},
             {"id": "two", "do": "cmd", "argv": [PY, "-c", "print('{out.one}'.upper())"], "capture": "two"},
         ]}

    def test_pause_resume_from_record_only(self):
        cat = cat_with(self.R)
        rec = self.runner(cat).start("demo", {"msg": "ala"})
        rid = rec["run_id"]
        self.assertEqual(rec["state"], "paused")
        st = self.steps(rid)
        self.assertEqual(st["one"]["out"], "ala")
        self.assertEqual(st["skip_me"]["state"], "skipped")
        self.assertEqual(st["ask"]["state"], "waiting")
        self.assertEqual(self.rec(rid)["escalations"][0]["question"], "dalej?")
        self.assertEqual(self.rec(rid)["where"], "vm:t")
        # nowy Runner (żadnego stanu w pamięci) wznawia z rekordu
        rec = self.runner(cat).resume(rid, answers={"ask": "yes"})
        self.assertEqual(rec["state"], "running")  # handoff: praca trwa poza runnerem = bieg w toku
        self.assertEqual(self.steps(rid)["agent"]["state"], "handoff")
        self.assertIn(rid, self.steps(rid)["agent"]["text"])
        rec = self.runner(cat).resume(rid, done=["agent"])
        self.assertEqual(rec["state"], "done")
        self.assertEqual(self.steps(rid)["two"]["out"], "ALA")
        self.assertEqual(rr.validate_record(self.rec(rid)), [])
        kinds = [e["kind"] for e in self.rec(rid)["events"]]
        self.assertEqual(kinds.count("resumed"), 1)
        self.assertEqual(kinds.count("handoff"), 1)
        self.assertEqual(kinds[-1], "finished")

    def test_yes_and_mandate_pass_confirm(self):
        cat = cat_with({**self.R, "steps": self.R["steps"][:3]})
        self.assertEqual(self.runner(cat, yes=True).start("demo", {})["state"], "done")
        self.gestures.write_text(json.dumps({"mandates": [{"id": "m1", "state": "active", "until": "2999-01-01T00:00:00Z", "scope": "wszystko"}]}))
        rec = self.runner(cat).start("demo", {})
        self.assertEqual(rec["state"], "done")
        self.assertEqual(self.steps(rec["run_id"])["ask"]["by"], "m1")

    def test_declined_confirm_skips_and_continues(self):
        cat = cat_with({**self.R, "steps": self.R["steps"][:3] + self.R["steps"][4:]})
        rid = self.runner(cat).start("demo", {})["run_id"]
        rec = self.runner(cat).resume(rid, answers={"ask": "no"})
        self.assertEqual(rec["state"], "done")
        self.assertEqual(self.steps(rid)["ask"]["state"], "declined")

    def test_failed_cmd_fails_run(self):
        cat = cat_with({"id": "f", "version": 1, "since": "d", "steps": [{"id": "x", "do": "cmd", "argv": [PY, "-c", "raise SystemExit(4)"]}]})
        rec = self.runner(cat).start("f", {})
        self.assertEqual(rec["state"], "failed")
        self.assertEqual(self.steps(rec["run_id"])["x"]["exit"], 4)

    def test_precondition_advisory_or_stop(self):
        bad = {**self.R, "preconditions": [{"check": {"path": "place.state", "op": "eq", "value": "sleeps"}, "why": "śpi", "since": "d"}],
               "steps": self.R["steps"][:1]}
        rec = self.runner(cat_with(bad)).start("demo", {})
        self.assertEqual(rec["state"], "done")
        self.assertTrue(any("śpi" in a["text"] for a in self.rec(rec["run_id"])["advisories"]))
        stop = {**bad, "preconditions": [{**bad["preconditions"][0], "on_fail": "stop"}]}
        self.assertEqual(self.runner(cat_with(stop)).start("demo", {})["state"], "failed")

    def test_dry_run_runs_nothing(self):
        cat = cat_with({"id": "d", "version": 1, "since": "d", "steps": [{"id": "x", "do": "cmd", "argv": [PY, "-c", "raise SystemExit(4)"]}]})
        rec = self.runner(cat, dry=True).start("d", {})
        self.assertEqual(rec["state"], "done")
        self.assertIn("dry_run", self.steps(rec["run_id"])["x"])

    def test_missing_required_param(self):
        cat = cat_with({"id": "p", "version": 1, "since": "d", "params": {"brief": {"required": True}}, "steps": []})
        with self.assertRaises(rt.RoutineError):
            self.runner(cat).start("p", {})

    def test_changed_routine_is_advised_on_resume(self):
        cat = cat_with(self.R)
        rid = self.runner(cat).start("demo", {})["run_id"]
        changed = cat_with({**self.R, "title": "zmieniona"})
        self.runner(changed).resume(rid, answers={"ask": "yes"})
        self.assertTrue(any("zmieniła się" in a["text"] for a in self.rec(rid)["advisories"]))


class TestBrief(unittest.TestCase):
    def test_prefix_byte_for_byte_and_shared(self):
        b1 = rt.load_json(REPO / "tools/workflows/briefs/iw3_brief.json")
        b2 = {"schema": "loom.brief/1", "id": "X", "context_pack": b1["context_pack"], "scope": "inne", "accept": "a"}
        r1, r2 = rt.brief_render(b1), rt.brief_render(b2)
        pack = cp.render(REPO, rt.load_json(cp.PACKS), b1["context_pack"], rt.load_json(cp.TOOLS))
        self.assertEqual(r1["args"]["context_prefix"], pack["text"])
        self.assertEqual(r1["context_pack_hash"], r2["context_pack_hash"])
        self.assertNotEqual(r1["own_hash"], r2["own_hash"])
        self.assertEqual(r1["context_pack_hash"], cp.sha(r1["args"]["context_prefix"]))

    def test_structured_fields_go_to_own_part(self):
        b = {"schema": "loom.brief/1", "id": "X", "scope": "S", "accept": "A", "decisions": ["d1"], "hypothesis": "h",
             "plan_calls": 40, "extra_field": 1}
        r = rt.brief_render(b)
        self.assertIn("Decyzje już podjęte:\n- d1", r["args"]["scope"])
        self.assertIn("Plan wywołań: 40", r["args"]["scope"])
        self.assertEqual(r["args"]["extra_field"], 1)  # pola spoza presetu przechodzą
        self.assertNotIn("context_prefix", r["args"])

    def test_k2c1_split(self):
        old = rt.load_json(REPO / "tools/workflows/briefs/k2c1_brief.json")
        new = rt.load_json(REPO / "tools/workflows/briefs/k2c1_brief.v2.json")
        self.assertEqual(new["schema"], "loom.brief/1")
        r = rt.brief_render(new)
        self.assertLess(r["own_chars"], len(old["scope"]) / 2)
        self.assertEqual(r["args"]["id"], old["id"])
        self.assertTrue(r["args"]["context_prefix"].startswith("=== PAKIET KONTEKSTU jadro"))
        self.assertIn("DYSCYPLINA ASAN", r["args"]["context_prefix"])
        split = rt.brief_split(old, "jadro", "własna")
        self.assertEqual((split["scope"], split["context_pack"], split["accept"]), ("własna", "jadro", old["accept"]))

    @unittest.skipUnless(shutil.which("node"), "node niedostępny")
    def test_prompt_starts_with_prefix(self):
        r = rt.brief_render(rt.load_json(REPO / "tools/workflows/briefs/k2c1_brief.v2.json"))
        prompts = rt.prompts_of(r["args"])
        self.assertTrue(prompts[0]["prompt"].startswith(r["args"]["context_prefix"]))
        self.assertIn("Package K2c-1", prompts[0]["prompt"])


class TestPackageRunEndToEnd(RunnerBase):
    def test_package_run_with_card(self):
        cat = rt.load_json(rt.ROUTINES)
        out = self.root / "args.json"
        r = self.runner(cat)
        rec = r.start("package.run", {"brief": str(REPO / "tools/workflows/briefs/iw3_brief.json"), "skip_gate": True,
                                      "out": str(out), "where": "vm:t"})
        rid = rec["run_id"]
        self.assertEqual(rec["state"], "running")
        self.assertEqual(rec["where"], "vm:t")
        args = json.loads(out.read_text())
        self.assertEqual(args["run_id"], rid)
        self.assertEqual(self.rec(rid)["context_pack_hash"], args["context_pack_hash"])
        self.assertTrue(self.rec(rid)["brief_hash"].startswith("sha256:"))
        card = self.root / "card.json"
        card.write_text(json.dumps({"done": True, "commits": ["abc"], "ci": "not pushed", "summary": "ok", "not_done": [],
                                    "run_id": rid}))
        rec = self.runner(cat).resume(rid, done=["agent"], params={"card": str(card)})
        self.assertEqual(rec["state"], "done")
        st = self.steps(rid)
        self.assertEqual(st["start_gate"]["state"], "skipped")
        self.assertEqual(st["card"]["state"], "done")
        self.assertIn("abc", json.dumps(self.rec(rid)["commits"]))
        self.assertEqual([e["kind"] for e in self.rec(rid)["events"]].count("finished"), 1)  # karta kończy bieg raz


if __name__ == "__main__":
    unittest.main()
