"""IW-5: tabela słów sterujących (tools/words.py + tools/routines/words.json). python3 -m unittest discover -s tools/tests"""
import io
import json
import random
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import words  # noqa: E402

TABLE = json.loads((HERE.parent / "routines" / "words.json").read_text(encoding="utf-8"))


def run(*argv):
    buf = io.StringIO()
    with redirect_stdout(buf):
        rc = words.main(list(argv))
    return rc, json.loads(buf.getvalue())


class TableTest(unittest.TestCase):
    def test_check_ok_and_shape(self):
        r = words.do_check(TABLE)
        self.assertTrue(r["ok"], r["errors"])
        for g in TABLE["gestures"]:
            for k in ("gesture", "patterns", "effect", "cost", "reversal", "since", "render"):
                self.assertIn(k, g)

    def test_seed_words_with_spec(self):
        want = {"stop": "R89", "free_hand": "R89", "do_what_you_know": "R75", "pace_up": "R62", "less_talk": "CLAUDE.md Thread style",
                "launch": "R90", "going_to_sleep": "R75", "what_do_you_remember": "R73"}
        got = {g["id"]: g.get("spec") for g in TABLE["gestures"]}
        for k, v in want.items():
            self.assertEqual(got.get(k), v)

    def test_every_quote_is_verbatim_in_spec(self):
        spec = (HERE.parent.parent / "docs" / "spec" / "spec.json").read_text(encoding="utf-8")
        claude = " ".join((HERE.parent.parent / "CLAUDE.md").read_text(encoding="utf-8").split())
        for g in TABLE["gestures"]:
            q = (g.get("quote") or {}).get("text")
            if q:
                self.assertTrue(q in spec or q in claude, f"{g['id']}: cytat nie jest dosłowny: {q}")


class MatchTest(unittest.TestCase):
    def ids(self, text):
        return [h["gesture"] for h in words.match(TABLE, text) if not h["weak"]]

    def test_asr_variants(self):
        for t in ("Stop", "stop.", "STOP!", "  stop  ", "zatrzymaj"):
            self.assertEqual(self.ids(t), ["stop"], t)
        for t in ("tutaj masz wolną rękę", "tutaj masz wolna reke", "Wolna ręka, na UI", "masz wolną  rękę"):
            self.assertIn("free_hand", self.ids(t), t)
        self.assertIn("what_do_you_remember", self.ids("co pamietasz o grafie"))
        self.assertIn("pace_up", self.ids("podkrec troche"))

    def test_word_boundary(self):
        self.assertEqual(self.ids("zatrzymałem się"), [])
        self.assertEqual(self.ids("stopień trudności"), [])

    def test_stop_inside_long_sentence_is_weak(self):
        hits = words.match(TABLE, "odruchowo stop nacisnąłem i nie wiem czy ci wszystkiego nie zatrzymałem")
        self.assertEqual([(h["gesture"], h["weak"]) for h in hits], [("stop", True)])

    def test_two_gestures_and_remainder(self):
        hits = words.match(TABLE, "to ja mogę iść spać, a ty rób to co wiesz")
        self.assertEqual([h["gesture"] for h in hits], ["going_to_sleep", "do_what_you_know"])
        hits = words.match(TABLE, "co pamiętasz o Loomie?")
        self.assertEqual(hits[0]["remainder"], "o Loomie")

    def test_property_fold_invariance(self):
        """Każdy wzorzec (bez re:) trafia swój gest po dowolnej zmianie wielkości liter, polskich znaków i interpunkcji."""
        rnd = random.Random(7)
        strip = str.maketrans("ąćęłńóśźżĄĆĘŁŃÓŚŹŻ", "acelnoszzACELNOSZZ")
        for g in TABLE["gestures"]:
            for p in g["patterns"]:
                for _ in range(5):
                    t = p.translate(strip) if rnd.random() < .5 else p
                    t = "".join(c.upper() if rnd.random() < .3 else c for c in t) + rnd.choice(["", ".", "!", " …"])
                    self.assertIn(g["id"], [h["gesture"] for h in words.match(TABLE, t)], t)


class ApplyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        runs = self.root / "docs" / "reports" / "runs"
        (runs / "2026-10").mkdir(parents=True)
        rec = {"run_id": "r_a", "state": "running", "receipt": [
            {"currency": "punkty tygodnia", "value": 0.5, "unit": "pkt"}, {"currency": "punkty tygodnia", "value": 0.25, "unit": "pkt"}]}
        (runs / "2026-10" / "r_a.json").write_text(json.dumps(rec), encoding="utf-8")
        lines = [{"run_id": "r_a", "path": "docs/reports/runs/2026-10/r_a.json", "title": "T", "state": "running"},
                 {"run_id": "r_b", "path": "x", "state": "running"}, {"run_id": "r_b", "path": "x", "state": "done"}]
        (runs / "runs.jsonl").write_text("\n".join(json.dumps(x) for x in lines) + "\n", encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def state(self):
        return json.loads((self.root / "docs" / "spec" / "gestures_state.json").read_text(encoding="utf-8"))

    def test_stop_lists_running_with_sunk_cost_and_reverses(self):
        rc, r = run("--root", str(self.root), "apply", "stop", "--at", "2026-10-26T18:00:00Z")
        eff = r["results"][0]["effects"][0]
        self.assertEqual([x["run_id"] for x in eff["running"]], ["r_a"])
        self.assertAlmostEqual(eff["running"][0]["sunk"]["punkty tygodnia [pkt]"], 0.75)
        st = self.state()
        self.assertEqual(st["stops"][0]["by"], "owner")
        self.assertEqual(st["stops"][0]["at"], "2026-10-26T18:00:00Z")
        run("--root", str(self.root), "reverse", r["use_id"])
        self.assertEqual(self.state()["stops"][0]["state"], "reversed")

    def test_accidental_stop_writes_nothing_but_log(self):
        rc, r = run("--root", str(self.root), "apply", "odruchowo stop nacisnąłem i nie wiem czy ci wszystkiego nie zatrzymałem")
        self.assertEqual(r["results"][0]["verdict"], "weak")
        self.assertFalse((self.root / "docs" / "spec" / "gestures_state.json").exists())
        log = (self.root / "docs" / "reports" / "runs" / "words_uses.jsonl").read_text(encoding="utf-8")
        self.assertIn("weak", log)

    def test_free_hand_creates_mandate_without_model(self):
        rc, r = run("--root", str(self.root), "apply", "tutaj masz wolną rękę: interfejs", "--at", "2026-10-26T18:00:00Z")
        m = self.state()["mandates"][0]
        self.assertEqual((m["kind"], m["mode"], m["scope"], m["until"]), ("mandate", "free_hand", "interfejs", "2026-10-27T18:00:00Z"))
        self.assertIn("CLAUDE.md", m["exceptions_ref"])

    def test_example_marker_needs_node(self):
        rc, r = run("--root", str(self.root), "apply", "to przykład")
        self.assertEqual(r["results"][0]["verdict"], "escalated")
        rc, r = run("--root", str(self.root), "apply", "to przykład", "--node", "R58")
        self.assertEqual(self.state()["markers"][0]["node"], "R58")

    def test_costly_gesture_needs_confirm(self):
        rc, r = run("--root", str(self.root), "apply", "odpal gate.full")
        self.assertEqual(r["results"][0]["verdict"], "needs_confirm")
        rc, r = run("--root", str(self.root), "apply", "odpal gate.full", "--confirmed")
        self.assertEqual(self.state()["requests"][0]["subject"], "gate.full")

    def test_preference_reverse_restores_previous(self):
        rc, r = run("--root", str(self.root), "apply", "nie gadaj tyle")
        self.assertEqual(self.state()["preferences"]["thread_style"]["value"], "status_lines_only")
        run("--root", str(self.root), "reverse", r["use_id"])
        self.assertNotIn("thread_style", self.state()["preferences"])

    def test_unknown_effect_escalates_and_miss_is_logged(self):
        t = json.loads(json.dumps(TABLE))
        t["gestures"].append({"id": "x", "gesture": "X", "patterns": ["abrakadabra"], "effect": [{"do": "teleport"}],
                              "cost": {"points": 0}, "reversal": "-", "since": "2026-10-26", "render": []})
        r = words.do_apply(t, self.root, "abrakadabra")
        self.assertEqual(r["results"][0]["verdict"], "escalated")
        self.assertEqual(words.do_apply(t, self.root, "dzień dobry")["outcome"], "miss")
        s = words.do_stats(t, self.root, None)
        self.assertEqual(s["uses"]["miss"], 1)

    def test_inbox_only_new_statements(self):
        f = self.root / "inbox.json"
        f.write_text(json.dumps({"docs": [{"id": "d1", "data": {"class": "statement", "text": "co pamiętasz o R91"}},
                                          {"id": "d2", "data": {"class": "approval", "text": "stop"}},
                                          {"id": "d3", "data": {"class": "statement", "text": "stop", "state": "applied"}}]}),
                     encoding="utf-8")
        rc, r = run("--root", str(self.root), "inbox", str(f))
        self.assertEqual([v["doc_id"] for v in r["verdicts"]], ["d1"])
        self.assertEqual(r["verdicts"][0]["results"][0]["effects"][0]["command"], "python3 tools/spec.py list o R91")

    def test_render_phone_order(self):
        labels = [x["label"] for x in words.do_render(TABLE, "phone")]
        self.assertEqual(labels[:3], ["Zatwierdzam", "Popraw (głosem)", "To przykład"])


if __name__ == "__main__":
    unittest.main()
