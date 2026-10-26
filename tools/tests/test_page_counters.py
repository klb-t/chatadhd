"""Testy IW-4: liczniki W1 z zapisów biegów, „Od Twojego wyjścia” (W9) po symulowanej przerwie, mapa miejsc v1.
Liczby liczy skrypt (tools/page_counters.py), nie model. Testy w przeglądarce pomijane bez Playwright/Chromium.

Uruchomienie:  python3 -m unittest tools/tests/test_page_counters.py
"""
from __future__ import annotations

import datetime as dt
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import page_counters as pc  # noqa: E402
import run_record as rr  # noqa: E402
import spec_html  # noqa: E402
from test_spec_html import mk, spec_of  # noqa: E402

UTC = dt.timezone.utc
RENDER = HERE / "spec_html_render.js"
PLACES = {
    "schema": "loom.places/1",
    "state_labels": {"alive": {"one": "żyje", "few": "żyją", "many": "żyje"}, "sleeps": "śpi", "not_deployed": "nie postawiono"},
    "summary_order": ["alive", "sleeps", "not_deployed"],
    "places": [
        {"id": "vm:x", "title": "VM x", "state": "alive", "observed_at": "2026-10-26",
         "sessions": [{"id": "gcp", "title": "sesja gcp", "state": "alive"}, {"id": "watchdog", "title": "sesja watchdog", "state": "alive"}],
         "services": [{"id": "ageds", "title": "AGEDS", "state": "not_deployed"}]},
        {"id": "cloud:s1", "title": "chmura", "state": "sleeps"},
    ],
}


def make_root() -> Path:
    root = Path(tempfile.mkdtemp(prefix="pc_"))
    (root / "docs/ops").mkdir(parents=True)
    (root / "docs/ops/places.json").write_text(json.dumps(PLACES, ensure_ascii=False), encoding="utf-8")
    cfg = rr.load_config()
    a = rr.start(root, cfg, "package.run", params={"id": "A"}, at="2026-10-26T01:00:00Z")
    rr.finish(root, cfg, a["run_id"], summary="A gotowe", at="2026-10-26T02:00:00Z")
    b = rr.start(root, cfg, "package.run", params={"id": "B"}, at="2026-10-26T03:00:00Z")
    rr.add_event(root, cfg, b["run_id"], "original_ideas", text="pomysł", at="2026-10-26T04:00:00Z")
    rr.add_receipt(root, cfg, b["run_id"], "words_to_owner", 120, at="2026-10-26T05:00:00Z")
    rr.add_receipt(root, cfg, b["run_id"], "words_to_owner", 999, at="2026-10-20T05:00:00Z")  # inny dzień
    c = rr.start(root, cfg, "package.run", params={"id": "C"}, at="2026-10-26T06:00:00Z")
    rr.finish(root, cfg, c["run_id"], state="failed", summary="C nie wyszło", at="2026-10-26T07:00:00Z")
    rr.add_event(root, cfg, c["run_id"], "note", text="bez delty", at="2026-10-26T07:30:00Z", use_default_delta=False)
    return root


class Counters(unittest.TestCase):
    def setUp(self):
        self.root = make_root()
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        self.now = dt.datetime(2026, 10, 26, 12, 0, tzinfo=UTC)

    def test_counters_equal_sum_of_record_deltas(self):
        out = pc.collect(self.root, self.now)
        got = {c["key"]: c["value"] for c in out["counters"]}
        # niezależne liczenie wprost z rekordów
        cfg = rr.load_config()
        want = {}
        for rec in rr.all_records(self.root, cfg):
            for ev in rec.get("events") or []:
                for k, v in (ev.get("counter_delta") or {}).items():
                    want[k] = want.get(k, 0) + v
        self.assertEqual(got, want)
        self.assertEqual(got, {"w_toku": 1, "zrobione": 1, "nie_wyszlo": 1, "oryginalne": 1})
        self.assertEqual([c["key"] for c in out["counters"]], ["w_toku", "zrobione", "nie_wyszlo", "oryginalne"])
        self.assertEqual(out["run_states"], {"done": 1, "failed": 1, "running": 1})
        self.assertEqual(out["words_today"], 120)
        # zdarzenia dla W9: tylko z niezerową deltą (preset), od najnowszych
        self.assertNotIn("note", [e["kind"] for e in out["events"]])
        self.assertEqual(out["events"][0]["kind"], "failed")
        self.assertEqual(len(out["events"]), 6)
        allev = pc.collect(self.root, self.now, {"events_filter": "all"})["events"]
        self.assertEqual(len(allev), 7)

    def test_places_line_and_items(self):
        p = pc.collect(self.root, self.now)["places"]
        self.assertEqual(p["line"], "miejsca: 3 żyją · 1 śpi · 1 nie postawiono")
        self.assertEqual([i["id"] for i in p["items"]], ["vm:x", "vm:x/gcp", "vm:x/watchdog", "vm:x/ageds", "cloud:s1"])
        self.assertEqual(pc.plural_label({"one": "a", "few": "b", "many": "c"}, 12), "c")
        self.assertEqual(pc.plural_label({"one": "a", "few": "b", "many": "c"}, 22), "b")

    def test_real_places_file_is_valid(self):
        p = pc.places_summary(HERE.parents[1])
        self.assertTrue(p["line"].startswith("miejsca: "))
        ids = [i["id"] for i in p["items"]]
        for s in ("gcp", "watchdog", "ageds", "chatadhd"):
            self.assertIn(f"vm:devbox-20260923/{s}", ids)
        self.assertTrue(all(i.get("observed_at") for i in p["items"]))  # każdy stan to datowana obserwacja

    def test_page_payload_carries_counters(self):
        w1 = pc.collect(self.root, self.now)
        html = spec_html.build_html(spec_of(mk("s_a", "R10", "proposed")), [], 48, self.now.replace(tzinfo=None), "SPEC.html", w1=w1)
        self.assertIn('id="w1"', html)
        self.assertIn('"w9_after_hours": 6', html)


@unittest.skipUnless(shutil.which("node"), "brak node")
class W9InBrowser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = make_root()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.root, ignore_errors=True)

    def render(self, **opt):
        now = dt.datetime.now(UTC)
        w1 = pc.collect(self.root, now)
        # zdarzenia przesunięte tak, by leżały w ostatnich godzinach (przerwa symulowana względem prawdziwego zegara przeglądarki)
        shift = now - dt.datetime(2026, 10, 26, 8, 0, tzinfo=UTC)
        for e in w1["events"]:
            e["at"] = (dt.datetime.fromisoformat(e["at"].replace("Z", "+00:00")) + shift).strftime("%Y-%m-%dT%H:%M:%SZ")
        if "w9_visible" in opt:
            w1["preset"]["w9_visible"] = opt.pop("w9_visible")
        f = self.root / "page.html"
        f.write_text(spec_html.build_html(spec_of(mk("s_a", "R10", "proposed")), [], 48, now.replace(tzinfo=None), "SPEC.html", w1=w1), encoding="utf-8")
        opt.setdefault("probe", ["#w9 h2", "#w9-sum", "#w1-line", "#w1-places summary", "#w9-more"])
        r = subprocess.run(["node", str(RENDER), json.dumps({"html": str(f), "width": 400, **opt})], capture_output=True, text=True, timeout=150)
        if r.returncode == 3:
            self.skipTest("brak Playwright albo Chromium")
        self.assertEqual(r.returncode, 0, r.stderr)
        out = json.loads(r.stdout)
        self.assertEqual(out["errors"], [])
        self.assertEqual(out["scrollWidth"], out["clientWidth"])
        return out

    @staticmethod
    def ago(hours):
        return (dt.datetime.now(UTC) - dt.timedelta(hours=hours)).strftime("%Y-%m-%dT%H:%M:%SZ")

    def test_w1_line_and_places(self):
        o = self.render()
        self.assertIn("w toku 1", o["probe"]["#w1-line"])
        self.assertIn("dziś do Ciebie: 120 słów", o["probe"]["#w1-line"].replace(" ", " "))
        self.assertEqual(o["probe"]["#w1-places summary"], "miejsca: 3 żyją · 1 śpi · 1 nie postawiono")
        self.assertIsNone(o["probe"]["#w9 h2"])  # pierwsza wizyta: bez W9

    def test_w9_after_simulated_break_with_fold(self):
        o = self.render(visit=self.ago(7), w9_visible=2)
        self.assertTrue(o["probe"]["#w9 h2"].startswith("Od Twojego wyjścia"))
        self.assertIn("nie wyszło +1", o["probe"]["#w9-sum"])
        self.assertEqual([i["hidden"] for i in o["w9Items"]].count(False), 2)
        self.assertRegex(o["probe"]["#w9-more"], r"^\d+ zwiniętych — pokaż$")
        o = self.render(visit=self.ago(7), w9_visible=2, clickSel="#w9-more")
        self.assertFalse(any(i["hidden"] for i in o["w9Items"]))

    def test_no_w9_after_short_break(self):
        o = self.render(visit=self.ago(1))
        self.assertIsNone(o["probe"]["#w9 h2"])

    def test_db_visit_marker_wins_and_is_written_by_owner_only(self):
        o = self.render(visit=self.ago(1), stubDb={"spec_inbox": [], "spec_visits": [{"__id": "owner", "at": self.ago(10)}]})
        self.assertIsNotNone(o["probe"]["#w9 h2"])  # znacznik z bazy (10 h) zamiast przeglądarki (1 h)
        marks = [a for a in o["adds"] if a.get("__coll") == "spec_visits"]
        self.assertEqual(len(marks), 1)
        self.assertIn("skrzynka:", o["probe"]["#w1-line"])
        o = self.render(stubDb={"spec_inbox": [], "spec_visits": []}, notOwner=True)
        self.assertEqual([a for a in o["adds"] if a.get("__coll") == "spec_visits"], [])


if __name__ == "__main__":
    unittest.main()
