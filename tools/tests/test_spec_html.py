"""Testy generatora żywej strony specyfikacji (tools/spec_html.py), pakiet R89 P2: kolejka „Czeka na Ciebie”.

Uruchomienie:  python3 -m unittest tools/tests/test_spec_html.py
Testy w przeglądarce (Chromium + Playwright) są pomijane, gdy ich tu nie ma (patrz spec_html_render.js).
"""
from __future__ import annotations

import datetime as dt
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import spec_html  # noqa: E402

NOW = dt.datetime(2026, 10, 23, 12, 0)
RENDER = HERE / "spec_html_render.js"


def mk(nid, alias, state="none", date="2026-10-23", **und_extra):
    und = {"state": state}
    if state != "none":
        und.update(text=f"Parafraza pozycji {alias}.", by="claude", date=date, history=[{"state": state, "by": "claude", "date": date}])
        und.update(und_extra)
    return {"id": nid, "aliases": [alias], "title": f"Tytuł {alias}", "kind": "requirement", "theme": "t1", "date": date,
            "quotes": [], "understanding": und, "status": None}


def spec_of(*nodes):
    return {"themes": [{"id": "t1", "title": "Temat 1"}], "nodes": list(nodes), "sources": {}, "status_policy": None}


def build(spec, **kw):
    return spec_html.build_html(spec, kw.pop("focus", []), 48, NOW, "SPEC.html", **kw)


def payload_of(html: str) -> dict:
    m = re.search(r'<script type="application/json" id="data">(.*?)</script>', html, re.S)
    return json.loads(m.group(1).replace("<\\/", "</"))


def inline_script(html: str) -> str:
    return re.findall(r"<script(?![^>]*application/json)[^>]*>(.*?)</script>", html, re.S)[-1]


class QueueOrder(unittest.TestCase):
    def test_two_proposed_one_none_in_preset_order(self):
        a = mk("s_a", "R10", "proposed", date="2026-10-20", cost_to_reverse="high", if_silent="czekam, nic nie ruszam")
        b = mk("s_b", "R11", "proposed", date="2026-10-23")
        c = mk("s_c", "R12")
        h = build(spec_of(b, c, a))  # kolejność w pliku nie ma znaczenia
        pl = payload_of(h)
        self.assertEqual([r["id"] for r in pl["queue"]], ["s_a", "s_b"])  # drogi odwrót najpierw, mimo starszej daty
        ra, rb = pl["queue"]
        self.assertEqual((ra["if_silent"], ra["if_silent_source"]), ("czekam, nic nie ruszam", "node"))
        self.assertEqual((rb["if_silent"], rb["if_silent_source"]), (spec_html.QUEUE_PRESET["if_silent"]["default"], "preset"))
        self.assertEqual(pl["counts"]["waiting"], 2)
        self.assertIn("high", pl["queue_preset"]["sort_note"])
        self.assertIn("Czeka na Ciebie", h)

    def test_date_desc_ties_by_number_and_open_cost_words(self):
        n = [mk("s_1", "R1", "proposed", date="2026-10-21"), mk("s_2", "R2", "proposed", date="2026-10-22"),
             mk("s_3", "R3", "proposed", date="2026-10-22"), mk("s_4", "R4", "proposed", date="2026-10-19", cost_to_reverse="high"),
             mk("s_5", "R5", "proposed", date="2026-10-18", cost_to_reverse="bardzo-drogi")]
        ids = lambda **kw: [r["id"] for r in payload_of(build(spec_of(*n), **kw))["queue"]]
        self.assertEqual(ids(), ["s_4", "s_3", "s_2", "s_1", "s_5"])                       # preset: high, potem data malejąco
        self.assertEqual(ids(queue_first=[]), ["s_3", "s_2", "s_1", "s_4", "s_5"])         # bez pierwszeństwa
        self.assertEqual(ids(queue_first=["bardzo-drogi", "high"]), ["s_4", "s_5", "s_3", "s_2", "s_1"])  # każdy napis dozwolony
        self.assertEqual(ids(queue_first=["HIGH"]), ["s_4", "s_3", "s_2", "s_1", "s_5"])  # wielkość liter bez znaczenia

    def test_preset_text_is_not_wrong_for_expensive_reversal(self):
        pl = payload_of(build(spec_of(mk("s_a", "R1", "proposed", cost_to_reverse="high"), mk("s_b", "R2", "proposed"))))
        by = {r["id"]: r for r in pl["queue"]}
        self.assertNotIn("odwrót tani", by["s_a"]["if_silent"])
        self.assertIn("odwrót tani", by["s_b"]["if_silent"])

    def test_only_proposed_waits(self):
        pl = payload_of(build(spec_of(mk("s_a", "R1", "approved"), mk("s_b", "R2", "corrected", corrected_text="x"), mk("s_c", "R3"))))
        self.assertEqual(pl["queue"], [])
        self.assertEqual(pl["counts"]["waiting"], 0)

    def test_dates_union_for_since_last_visit(self):
        n = mk("s_a", "R1", "proposed", date="2026-10-20")
        n["status"] = {"value": "JEST", "checked_on": "2026-10-16"}
        n["understanding"]["history"].append({"state": "proposed", "date": "2026-10-23T08:15"})
        pl = payload_of(build(spec_of(n)))
        self.assertEqual(pl["nodes"][0]["_dates"], ["2026-10-16", "2026-10-20", "2026-10-23T08:15"])


class Markup(unittest.TestCase):
    def setUp(self):
        self.html = build(spec_of(mk("s_a", "R1", "proposed"), mk("s_b", "R2", "proposed"), mk("s_c", "R3")))
        self.js = inline_script(self.html)

    def test_queue_section_above_recent_and_hidden_until_it_has_rows(self):
        i, j = self.html.index('id="queue"'), self.html.index('id="recent"')
        self.assertLess(i, j)
        self.assertRegex(self.html, r'<section class="queue" id="queue" hidden>')

    def test_same_three_controls_built_in_one_place_and_one_write(self):
        for label in ("Zatwierdzam parafrazę", "Popraw swoimi słowami", "Uwaga / dopisek"):
            self.assertEqual(self.js.count(label), 1, label)          # budowniczy przycisków jest jeden: wspólny dla pozycji i kolejki
        for k in ('data-k="approve"', 'data-k="correct"', 'data-k="remark"'):
            self.assertEqual(self.js.count(k), 1, k)
        self.assertEqual(self.js.count(".add("), 1)                    # jedno miejsce zapisu do spec_inbox
        self.assertIn("data-decide-for", self.js)
        self.assertIn("mountDecide(box)", self.js)
        self.assertRegex(self.js, r"page_generated: D\.generated")     # kształt dokumentu jak dotąd
        for k in ("node:", "alias:", "kind,", "text,", "at:", "state: 'new'"):
            self.assertIn(k, self.js)

    def test_texts_in_script(self):
        for t in ("Bez Twojej odpowiedzi:", "więcej", "nowe", "czeka na Ciebie", "Oznacz jako widziane"):
            self.assertIn(t, self.js)

    def test_local_storage_only_inside_try(self):
        uses = list(re.finditer(r"window\.localStorage", self.js))
        self.assertGreaterEqual(len(uses), 2)
        for m in uses:
            before = self.js[max(0, m.start() - 120):m.start()]
            self.assertIn("try {", before)

    def test_no_queue_section_content_when_nothing_waits(self):
        h = build(spec_of(mk("s_c", "R3")))
        self.assertEqual(payload_of(h)["queue"], [])
        self.assertRegex(h, r'<section class="queue" id="queue" hidden>')


class InlineScript(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "brak node")
    def test_node_check(self):
        h = build(spec_of(mk("s_a", "R1", "proposed")))
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "inline.js"
            f.write_text(inline_script(h), encoding="utf-8")
            r = subprocess.run(["node", "--check", str(f)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)


@unittest.skipUnless(shutil.which("node"), "brak node")
class InBrowser(unittest.TestCase):
    """Strona naprawdę narysowana w Chromium na szerokości telefonu (pomijane, gdy Playwright/Chromium nie ma)."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp())

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def render(self, spec, **opt):
        f = self.tmp / "page.html"
        f.write_text(build(spec, focus=opt.pop("focus", [])), encoding="utf-8")
        r = subprocess.run(["node", str(RENDER), json.dumps({"html": str(f), "width": 400, **opt})], capture_output=True, text=True, timeout=150)
        if r.returncode == 3:
            self.skipTest("brak Playwright albo Chromium")
        self.assertEqual(r.returncode, 0, r.stderr)
        out = json.loads(r.stdout)
        self.assertEqual(out["errors"], [])
        self.assertEqual(out["scrollWidth"], out["clientWidth"])  # bez przewijania w poziomie
        return out

    def test_queue_visible_with_rows_in_order_and_header_count(self):
        o = self.render(spec_of(mk("s_a", "R10", "proposed", date="2026-10-20", cost_to_reverse="high"), mk("s_b", "R11", "proposed"), mk("s_c", "R12")))
        self.assertFalse(o["queueHidden"])
        self.assertEqual(o["queueHeader"], "Czeka na Ciebie (2)")
        self.assertEqual([r["alias"] for r in o["rows"]], ["R10", "R11"])
        self.assertIn("czeka na Ciebie 2", o["countsLine"])
        self.assertIn("Bez Twojej odpowiedzi:", o["rows"][0]["silent"])

    def test_hidden_when_nothing_waits(self):
        o = self.render(spec_of(mk("s_c", "R12"), mk("s_d", "R13", "approved")))
        self.assertTrue(o["queueHidden"])
        self.assertEqual(o["rows"], [])

    def test_works_without_local_storage(self):
        o = self.render(spec_of(mk("s_a", "R10", "proposed")), noStorage=True)
        self.assertEqual(o["queueHeader"], "Czeka na Ciebie (1)")
        self.assertEqual(o["newInTree"], 0)

    def test_new_chips_since_last_visit(self):
        sp = spec_of(mk("s_a", "R10", "proposed", date="2026-10-23"), mk("s_b", "R11", "proposed", date="2026-10-01"))
        o = self.render(sp, visit="2026-10-10T00:00:00Z")
        self.assertEqual({r["alias"]: r["newChip"] for r in o["rows"]}, {"R10": True, "R11": False})
        self.assertIn("1 w kolejce", o["qNew"])

    def test_click_writes_exactly_one_entry_and_escalated_joins_queue(self):
        esc = {"node": "s_a", "alias": "R10", "state": "escalated", "kind": "correct", "text": "inaczej", "reason": "niejasne", "at": "2026-10-23T08:00:00Z"}
        sp = spec_of(mk("s_a", "R10", "proposed"), mk("s_b", "R11", "none"))
        o = self.render(sp, stubDb=[esc], click="approve")
        self.assertEqual(len(o["adds"]), 1)
        e = o["adds"][0]
        self.assertEqual((e["node"], e["alias"], e["kind"], e["text"], e["state"]), ("s_a", "R10", "approve", "", "new"))
        self.assertEqual(sorted(e), ["alias", "at", "class", "kind", "node", "page_generated", "state", "text"])
        self.assertEqual(e["class"], "approval")  # IW-3: klasa wpisu (lista otwarta) obok dawnego kind
        self.assertEqual(o["queueHeader"], "Czeka na Ciebie (1)")  # ta sama pozycja: parafraza i wpis w jednym wierszu
        self.assertTrue(o["rows"][0]["answered"])

    def test_escalated_only_node_gets_its_own_row(self):
        esc = {"node": "s_b", "alias": "R11", "state": "escalated", "kind": "remark", "text": "dopisek", "at": "2026-10-23T08:00:00Z"}
        o = self.render(spec_of(mk("s_a", "R10", "proposed"), mk("s_b", "R11")), stubDb=[esc])
        self.assertEqual([r["alias"] for r in o["rows"]], ["R10", "R11"])
        self.assertEqual(o["queueHeader"], "Czeka na Ciebie (2)")

    def test_search_filters_queue(self):
        o = self.render(spec_of(mk("s_a", "R10", "proposed"), mk("s_b", "R11", "proposed")), search="R11")
        self.assertEqual({r["alias"]: r["hidden"] for r in o["rows"]}, {"R10": True, "R11": False})
        self.assertIn("pasuje 1", o["queueHeader"])

    def test_full_question_criteria_class_and_time_at_400px(self):
        """IW-3: kryteria, klasa, czas od pierwszego pokazania i chwila rozstrzygnięcia milczeniem, na szerokości telefonu."""
        shown = (dt.datetime.now(dt.timezone.utc) - dt.timedelta(hours=3)).strftime("%Y-%m-%dT%H:%M:%SZ")
        a = mk("s_a", "R10", "proposed", first_shown=shown, ask_after="24h", cost_to_reverse="low", **{"class": "understanding"},
               criteria=["zgodne z R59 — każda konfiguracja", "odwrót jedną komendą " + "bardzo-długie-słowo" * 6])
        b = mk("s_b", "R11", "proposed", first_shown=shown, ask_after="1h", cost_to_reverse="high")
        o = self.render(spec_of(a, b))
        r = {x["alias"]: x for x in o["rows"]}
        self.assertEqual(len(r["R10"]["criteria"]), 2)
        self.assertEqual(r["R10"]["qclass"], "understanding")
        self.assertIn("pokazane 3 h temu", r["R10"]["qtime"])
        self.assertIn("milczenie rozstrzyga za 21 h", r["R10"]["qtime"])
        self.assertIn("trafi do koordynatora", r["R11"]["qtime"])


class QuestionFields(unittest.TestCase):
    def test_queue_row_carries_question_and_due(self):
        a = mk("s_a", "R10", "proposed", first_shown="2026-10-26T08:00:00Z", ask_after="1d", cost_to_reverse="tani",
               criteria=["k1"], **{"class": "money"})
        row = spec_html.build_queue([a])[0]
        self.assertEqual((row["due"], row["silence_applies"], row["class"], row["criteria"]),
                         ("2026-10-27T08:00:00Z", True, "money", ["k1"]))
        row = spec_html.build_queue([mk("s_b", "R11", "proposed")])[0]
        self.assertNotIn("due", row)  # bez ask_after nie ma chwili rozstrzygnięcia (preset default_ask_after = null)
        pre = {**spec_html.QUEUE_PRESET, "question": {**spec_html.QUEUE_PRESET["question"], "default_ask_after": "6h"}}
        row = spec_html.build_queue([mk("s_b", "R11", "proposed", first_shown="2026-10-26T08:00:00Z")], pre)[0]
        self.assertEqual((row["due"], row["silence_applies"]), ("2026-10-26T14:00:00Z", False))


if __name__ == "__main__":
    unittest.main()
