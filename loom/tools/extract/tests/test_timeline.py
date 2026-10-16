"""Global timeline: mixed zones/epoch/ISO, stable ties, buckets, gaps and bursts (0 = off),
cross-service continuation detected and NOT merged, keys as presets, shift/anonymise."""
import random

from _support import EXPECTED_EXTRACT as E, EXPORTS, EXTRACT, ExtractTest
from loom.tools.extract import timestamps as ts
from loom.tools.extract.timeline import build_timeline


class TimestampsTest(ExtractTest):
    def test_parse_forms(self):
        self.assertEqual(ts.parse(1700000000).iso, "2023-11-14T22:13:20Z")
        self.assertEqual(ts.parse("2026-05-05T10:00:00+02:00").iso, "2026-05-05T08:00:00Z")
        st = ts.parse("2026-05-05T10:30:00", assume_zone="UTC")
        self.assertEqual((st.iso, st.inferred), ("2026-05-05T10:30:00Z", True))
        st2 = ts.parse("2026-05-05T10:30:00", assume_zone="Europe/Warsaw")
        self.assertEqual(st2.iso, "2026-05-05T08:30:00Z")
        self.assertIsNone(ts.parse(None).utc)
        self.assertEqual(ts.parse(None, mtime=1700000000.0).how, "mtime")
        self.assertEqual(ts.parse_duration("3d").total_seconds(), 3 * 86400)
        self.assertEqual(ts.parse_date_bound("2026-05").isoformat()[:10], "2026-05-01")
        self.assertEqual(ts.parse_date_bound("2026-05", end=True).isoformat()[:10], "2026-06-01", "end bound is exclusive")


class TimelineTest(ExtractTest):
    def test_activity_axis_is_utc_sorted_with_originals_kept(self):
        eng, units = self.units(EXTRACT / "mixed_dir", EXTRACT / "claude_code/projects/-home-user-proj")
        tl, rows = build_timeline(units, eng.ds, key="activity", gap_days=0, burst_ratio=0)
        self.assertEqual(tl["schema"], "loom.extract.timeline/1")
        stamps = [r["t"] for r in rows if r["t"]]
        self.assertEqual(stamps, sorted(stamps))
        self.assertTrue(all(r["kind"] == "message" for r in rows))
        with_src = [r for r in rows if r.get("t_src") not in (None, r["t"])]
        self.assertTrue(with_src, "native stamps travel next to the UTC value")
        self.assertEqual(tl["gaps"], [], "gap_days 0 = do not report")
        self.assertEqual(tl["bursts"], [], "burst_ratio 0 = do not report")

    def test_ties_are_stable_across_input_order_and_runs(self):
        eng, units = self.units(EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip")
        tl1, rows1 = build_timeline(units, eng.ds, key="activity")
        shuffled = list(units)
        random.Random(7).shuffle(shuffled)
        tl2, rows2 = build_timeline(shuffled, eng.ds, key="activity")
        self.assertEqual([(r["t"], r["uid"]) for r in rows1], [(r["t"], r["uid"]) for r in rows2])
        self.assertEqual(tl1["ties"], tl2["ties"])
        self.assertEqual(tl1["ties"][:2], ["t", "service"])

    def test_keys_are_presets_and_unknown_key_is_an_error(self):
        eng, units = self.units(EXPORTS / "openai_2026_sharded.zip")
        for key in ("created", "updated", "first_message", "last_message", "activity"):
            tl, rows = build_timeline(units, eng.ds, key=key)
            self.assertEqual(tl["key"], key)
        with self.assertRaises(ValueError):
            build_timeline(units, eng.ds, key="bogus")

    def test_buckets_gaps_bursts(self):
        eng, units = self.units(EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip")
        tl, rows = build_timeline(units, eng.ds, key="activity", bucket="week", gap_days=30, burst_ratio=3.0)
        self.assertEqual(tl["buckets"]["unit"], "week")
        self.assertTrue(tl["buckets"]["rows"])
        total = sum(b["messages"] for b in tl["buckets"]["rows"])
        self.assertEqual(total, len([r for r in rows if r["t"]]))
        self.assertTrue(tl["gaps"], "a 500-day hole between the legacy and the 2025 conversations")
        self.assertGreater(tl["gaps"][0]["days"], 30)
        for b in tl["bursts"]:
            self.assertGreaterEqual(b["ratio_to_median"], 3.0)
        tl_m, _ = build_timeline(units, eng.ds, key="activity", bucket="month")
        self.assertLessEqual(len(tl_m["buckets"]["rows"]), len(tl["buckets"]["rows"]))

    def test_cross_service_continuation_is_a_candidate_not_a_merge(self):
        eng, units = self.units(EXTRACT / "continuation/chatgpt_part.zip", EXTRACT / "continuation/claude_part.zip")
        tl, rows = build_timeline(units, eng.ds)
        o = E["continuation"]
        self.assertEqual(len(tl["continuations"]), 1)
        c = tl["continuations"][0]
        by = {u.uid: u for u in units}
        self.assertEqual((by[c["from"]].title, by[c["to"]].title), (o["from_title"], o["to_title"]))
        self.assertEqual((c["from_service"], c["to_service"]), (o["from_service"], o["to_service"]))
        self.assertLessEqual(c["gap_minutes"], o["gap_minutes_max"])
        self.assertGreaterEqual(len(c["shared_terms"]), o["shared_terms_min"])
        self.assertTrue(c["inferred"])
        self.assertEqual(len([u for u in units if u.kind == "conversation"]), 2, "never merged automatically")
        tl_off, _ = build_timeline(units, eng.ds, continuations=False)
        self.assertEqual(tl_off["continuations"], [])

    def test_window_shift_anonymise_granularity(self):
        eng, units = self.units(EXTRACT / "versions/chatgpt_2025-10-01.zip")
        tl, rows = build_timeline(units, eng.ds, key="activity")
        base = [r["t"] for r in rows]
        tl_s, rows_s = build_timeline(units, eng.ds, key="activity", shift_seconds=3600)
        self.assertTrue(all(ts.from_iso(b) + ts.parse_duration("1h") == ts.from_iso(s) for b, s in zip(base, [r["t"] for r in rows_s])))
        tl_a, rows_a = build_timeline(units, eng.ds, key="activity", anonymise=True)
        self.assertTrue(rows_a[0]["t"].startswith("1970-01-01T00:00:00"), "relative axis starts at the epoch")
        d_base = ts.from_iso(base[-1]) - ts.from_iso(base[0])
        d_anon = ts.from_iso(rows_a[-1]["t"]) - ts.from_iso(rows_a[0]["t"])
        self.assertEqual(d_base, d_anon, "gaps are preserved")
        lo = ts.parse_date_bound("2025-06-16")
        tl_w, rows_w = build_timeline(units, eng.ds, key="activity", window=(lo, None))
        self.assertTrue(all(ts.from_iso(r["t"]) >= lo for r in rows_w))
        self.assertLess(len(rows_w), len(rows))
        tl_g, rows_g = build_timeline(units, eng.ds, key="activity", granularity="day")
        self.assertTrue(all(r["t"].endswith("T00:00:00Z") for r in rows_g))
