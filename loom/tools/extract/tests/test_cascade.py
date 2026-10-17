"""Cascade (bands, explanations, masks, manual wins, shadow, model levels recorded not called),
expand presets, manual decisions with learning proposals, clusters."""
import json

from _support import EXPORTS, EXTRACT, ExtractTest
from loom.tools.extract import cluster
from loom.tools.extract.cascade import Cascade, make_grains
from loom.tools.extract.criteria import build_criteria, criteria_from_terms, load_criteria, mask_check
from loom.tools.extract.decisions import Decisions
from loom.tools.extract.expand import Selection, expand
from loom.tools.extract.lexical import lexicon

TERMS = ["analizator", "archiwów", "rekurencja", "katalogach", "inwentarz"]


class CascadeTest(ExtractTest):
    def _units(self):
        return self.units(EXTRACT / "versions/chatgpt_2025-10-01.zip")

    def test_lexical_include_with_reasons_and_exclude(self):
        eng, units = self._units()
        crit = criteria_from_terms(TERMS, eng.ds, eng.lex)
        res = Cascade(units, crit, eng.ds, eng.lex, cascade_id="lexical_only").run()
        by_ext = {u.ext_id: res.units[u.uid] for u in units if u.kind == "conversation"}
        self.assertEqual(by_ext["c1"].decision, "include", by_ext["c1"].as_dict())
        self.assertEqual(by_ext["c4"].decision, "exclude")
        self.assertEqual(by_ext["c1"].source, "L1")
        reasons = by_ext["c1"].reasons
        self.assertTrue(reasons)
        self.assertTrue(any("form" in r or "term" in r for r in reasons), reasons[:3])
        self.assertTrue(any("pos" in r or "position" in r or "at" in r for r in reasons), reasons[:3])
        self.assertEqual(res.counts()["include"], 1)
        self.assertEqual(res.cascade_id, "lexical_only")
        rows = res.explain_rows()
        self.assertTrue(all("score" in r and "decision" in r for r in rows))

    def test_polish_inflection_and_folding(self):
        eng, units = self._units()
        crit = criteria_from_terms(["archiwum", "katalog", "rekurencji"], eng.ds, eng.lex)
        res = Cascade(units, crit, eng.ds, eng.lex, cascade_id="lexical_only", bands={"include": 0.3}).run()
        c1 = next(d for u, d in res.units.items() if d.unit.ext_id == "c1")
        matched = {r.get("form") or r.get("term") for r in c1.reasons}
        self.assertTrue(matched, "the inflected forms archiwów / katalogach / rekurencja are found")
        self.assertEqual(c1.decision, "include")
        lex = lexicon()
        self.assertEqual(lex.fold("Zażółć gęślą"), "zazolc gesla")

    def test_bands_are_presets_and_ambiguous_is_reported(self):
        eng, units = self._units()
        crit = criteria_from_terms(["rekurencja"], eng.ds, eng.lex)
        res = Cascade(units, crit, eng.ds, eng.lex, cascade_id="lexical_only", bands={"include": 0.99, "exclude": 0.01}).run()
        c1 = next(d for d in res.units.values() if d.unit.ext_id == "c1")
        self.assertEqual(c1.decision, "ambiguous")
        self.assertGreater(c1.score, 0.0)

    def test_exclude_keyword_and_regex(self):
        eng, units = self._units()
        crit = build_criteria({"id": "x", "keywords": TERMS, "exclude": {"keywords": [{"term": "inwentarz"}]}}, eng.ds, eng.lex)
        res = Cascade(units, crit, eng.ds, eng.lex, cascade_id="lexical_only").run()
        c1 = next(d for d in res.units.values() if d.unit.ext_id == "c1")
        self.assertEqual(c1.decision, "exclude")
        self.assertTrue(any("exclude" in str(r).lower() for r in c1.reasons), c1.reasons)
        crit2 = build_criteria({"id": "r", "regex": [{"pattern": r"(?i)importer\s+chatgpt", "weight": 3.0}]}, eng.ds, eng.lex)
        res2 = Cascade(units, crit2, eng.ds, eng.lex, cascade_id="lexical_only", bands={"include": 0.3}).run()
        self.assertEqual(next(d for d in res2.units.values() if d.unit.ext_id == "c2").decision, "include")

    def test_masks_exclude_never_include(self):
        eng, units = self._units()
        c4 = next(u for u in units if u.ext_id == "c4")
        ok, why = mask_check(c4, {"date": {"from": "2030-01-01", "to": None}})
        self.assertFalse(ok)
        self.assertTrue(why)
        ok2, _ = mask_check(c4, {"services": ["anthropic"]})
        self.assertFalse(ok2)
        ok3, _ = mask_check(c4, {"with_attachments": True, "services": ["openai"]})
        self.assertTrue(ok3)
        crit = build_criteria({"id": "m", "keywords": TERMS, "masks": {"services": ["anthropic"]}}, eng.ds, eng.lex)
        res = Cascade(units, crit, eng.ds, eng.lex).run()
        self.assertEqual(res.counts()["include"], 0)
        self.assertTrue(all(d.decision == "exclude" and d.source in ("L0", "manual") or d.source for d in res.units.values()))

    def test_manual_decision_wins_and_records_what_was(self):
        eng, units = self._units()
        c4 = next(u for u in units if u.ext_id == "c4")
        c1 = next(u for u in units if u.ext_id == "c1")
        dec = Decisions()
        dec.mark(c4.uid, "include", "to o aplikacji")
        dec.mark(c1.uid, "exclude", "nie to")
        crit = criteria_from_terms(TERMS, eng.ds, eng.lex)
        res = Cascade(units, crit, eng.ds, eng.lex, cascade_id="lexical_only", manual=dec.as_manual()).run()
        self.assertEqual(res.units[c4.uid].decision, "include")
        self.assertEqual(res.units[c4.uid].source, "manual")
        self.assertEqual(res.units[c4.uid].manual["was"]["decision"], "exclude")
        self.assertEqual(res.units[c1.uid].decision, "exclude")
        self.assertEqual(res.units[c1.uid].manual["was"]["decision"], "include")

    def test_vectors_level_reports_its_tier_and_shadow(self):
        eng, units = self._units()
        from loom.tools.extract.ops import _window_index
        crit = criteria_from_terms(TERMS, eng.ds, eng.lex)
        idx = _window_index(eng)
        res = Cascade(units, crit, eng.ds, eng.lex, vectors=idx).run()
        self.assertIsNotNone(res.vectors)
        self.assertIn(res.vectors["tier"], (1, 2, 3))
        self.assertIn("tier_name", res.vectors)
        self.assertEqual(set(res.shadow), {"lexical_only", "vectors_only", "both"})
        lv = {l["id"]: l for l in res.levels}
        self.assertTrue(lv["L2"]["evaluated"] > 0)

    def test_model_levels_are_recorded_never_called(self):
        # a user cascade in the overlay (<data dir>/extract/cascades/): the cheap model on every passed grain
        self.write(self.tmp / "no-overlay" / "cascades" / "test_model.json", json.dumps({
            "schema": "loom.extract.cascade/1", "id": "test_model",
            "levels": [{"id": "L0", "flow": "masks", "on": "all", "decides": ["exclude"]},
                       {"id": "L1", "flow": "lexical", "on": "passed", "decides": ["include", "exclude", "ambiguous"]},
                       {"id": "L3", "flow": "cheap_classify", "on": "passed", "enabled": False},
                       {"id": "L4", "flow": "strong_judge", "on": "ambiguous", "enabled": False}],
            "combine": "weighted_max", "bands": {"include": 0.75, "exclude": 0.25}, "manual": "wins", "unresolved": "report"}))
        eng, units = self._units()
        self.assertEqual(eng.ds.entry("cascades", "test_model")["_source"].split(":")[0], "overlay")
        crit = criteria_from_terms(TERMS, eng.ds, eng.lex)  # c1 passes the lexical level, c2/c4 are excluded
        res = Cascade(units, crit, eng.ds, eng.lex, cascade_id="test_model").run()
        self.assertEqual(res.model_pending, {}, "disabled levels are skipped without --model")
        self.assertTrue(all(l["skipped"] > 0 for l in res.levels if l["id"] == "L3"))
        res2 = Cascade(units, crit, eng.ds, eng.lex, cascade_id="test_model", model_enabled=True).run()
        self.assertIn("cheap_classify", res2.model_pending)
        self.assertGreater(res2.model_pending["cheap_classify"]["grains"], 0)
        self.assertGreater(res2.model_pending["cheap_classify"]["chars"], 0)
        self.assertNotIn("strong_judge", res2.model_pending, "on: ambiguous — nothing was ambiguous")
        # the socket guard in setUp would have raised on any network use

    def test_grains_window_branch_message(self):
        eng, units = self.units(EXPORTS / "openai_2026_sharded.zip")
        forked = next(u for u in units if u.kind == "conversation" and u.tree.get("forks"))
        self.assertEqual(len(make_grains(forked, "conversation", ["text"])), 1)
        self.assertEqual(len(make_grains(forked, "message", ["text"])), len(forked.messages))
        self.assertEqual(len(make_grains(forked, "branch", ["text"])), len(forked.branches()))
        self.assertGreaterEqual(len(make_grains(forked, "window", ["text"], window=2, step=1)), 2)
        crit = criteria_from_terms(["bazy", "danych"], eng.ds, eng.lex)
        res = Cascade(units, crit, eng.ds, eng.lex, cascade_id="lexical_only", grain="window", bands={"include": 0.3}).run()
        inc = [d for d in res.units.values() if d.decision == "include"]
        self.assertTrue(inc)
        self.assertTrue(all(d.selected_messages is not None for d in inc), "window grain selects messages, not whole conversations")
        self.assertTrue(all(0 < d.coverage <= 1 for d in inc))

    def test_preset_criteria_file_loads(self):
        eng, units = self._units()
        crit = load_criteria("chatadhd", eng.ds, eng.lex)
        self.assertEqual(crit.id, "chatadhd")
        self.assertTrue(crit.keywords)
        self.assertEqual(crit.source, "preset")
        path = self.write(self.tmp / "crit.json", '{"id": "own", "keywords": ["rekurencja"], "grain": "window"}')
        own = load_criteria(str(path), eng.ds, eng.lex)
        self.assertEqual((own.id, own.grain), ("own", "window"))


class ExpandTest(ExtractTest):
    def test_presets(self):
        eng, units = self.units(EXPORTS / "openai_2026_sharded.zip")
        u = next(x for x in units if x.kind == "conversation" and len(x.messages) >= 6 and x.tree.get("forks"))
        mid = u.current_path()[2].id
        for preset, check in (("none", lambda s: s.units[u.uid] == {mid}),
                              ("window:1", lambda s: s.units[u.uid] is not None and len(s.units[u.uid]) >= 2),
                              ("branch", lambda s: s.units[u.uid] is not None and len(s.units[u.uid]) >= 3),
                              ("conversation", lambda s: s.units[u.uid] is None)):
            with self.subTest(preset):
                sel = Selection()
                sel.add(u.uid, {mid}, "test")
                r = expand(sel, units, preset)
                self.assertTrue(check(r.selection), (preset, r.selection.as_dict()))
                self.assertEqual(r.preset, preset)

    def test_time_window_and_linked(self):
        eng, units = self.units(EXTRACT / "continuation/chatgpt_part.zip", EXTRACT / "continuation/claude_part.zip")
        a = next(u for u in units if u.service == "openai")
        sel = Selection()
        sel.add(a.uid, None, "test")
        r = expand(sel, units, "time_window:2h")
        self.assertEqual(len(r.selection.units), 2, "the Claude conversation 25 minutes later is within 2h")
        tl = eng.op("timeline")
        r2 = expand(sel, units, "conversation+linked", continuations=tl["timeline"]["continuations"])
        self.assertEqual(r2.linked_conversations, 1)
        with self.assertRaises(ValueError):
            expand(sel, units, "bogus")


class DecisionsTest(ExtractTest):
    def test_roundtrip_and_marks(self):
        d = Decisions.load(self.tmp / "d.json")
        self.assertEqual(d.source, "new")
        d.mark("sha256:aaa", "include", "tak", was={"decision": "ambiguous", "score": 0.5})
        d.mark("sha256:bbb", "exclude", "nie", message="m3")
        d.mark("sha256:aaa", "exclude", "zmiana zdania")
        self.assertEqual(len(d.entries), 2, "a second mark of the same grain replaces the first")
        d.save()
        d2 = Decisions.load(self.tmp / "d.json")
        self.assertEqual(d2.as_manual(), {"sha256:aaa": {"decision": "exclude", "reason": "zmiana zdania", "grain": "conversation"}})
        self.assertEqual(d2.message_marks(), {"sha256:bbb": {"m3": "exclude"}})
        self.assertTrue(d2.unmark("sha256:bbb", message="m3"))
        self.assertFalse(d2.unmark("sha256:zzz"))
        with self.assertRaises(ValueError):
            d2.mark("x", "maybe")

    def test_proposals_from_corrections_need_support(self):
        eng, units = self.units(EXTRACT / "versions/chatgpt_2025-10-01.zip", EXTRACT / "continuation/chatgpt_part.zip")
        by = {u.ext_id: u for u in units if u.kind == "conversation"}
        d = Decisions()
        d.mark(by["c2"].uid, "include", "", was={"decision": "ambiguous", "score": 0.5})
        self.assertEqual(d.propose(units, eng.ds, eng.lex, min_corrections=2), [], "one correction is not a pattern")
        d.mark(by["c4"].uid, "include", "", was={"decision": "ambiguous", "score": 0.6})
        props = d.propose(units, eng.ds, eng.lex, min_corrections=2)
        band = next(p for p in props if p["kind"] == "band")
        self.assertEqual((band["band"], band["value"], band["support"]), ("include", 0.495, 2))
        # two excluded corrections sharing "analizator archiwów" → an exclude-keyword proposal with support 2
        d.mark(by["c1"].uid, "exclude", "", was={"decision": "include", "score": 0.9})
        d.mark(by["cont1"].uid, "exclude", "", was={"decision": "include", "score": 0.9})
        props = d.propose(units, eng.ds, eng.lex, min_corrections=2)
        exk = [p for p in props if p["kind"] == "exclude_keyword"]
        self.assertTrue(exk, props)
        self.assertTrue(all(p["support"] == 2 for p in exk))
        self.assertIn("analizator", {p["term"] for p in exk})
        overlay = d.apply_proposals({"id": "x"})
        self.assertTrue(overlay["exclude"]["keywords"])
        self.assertEqual(d.corrections(), [e for e in d.entries if e.get("was")])


class ClustersTest(ExtractTest):
    def test_deterministic_clusters_and_cluster_criteria(self):
        eng, units = self.units(EXPORTS / "openai_2026_sharded.zip", EXPORTS / "anthropic_2026_full.zip", EXTRACT / "versions/chatgpt_2025-10-01.zip")
        a = cluster.discover(units, eng.ds, eng.lex)
        b = cluster.discover(list(reversed(units)), eng.ds, eng.lex)
        self.assertEqual(a["clusters"], b["clusters"], "same input, any order → same clusters")
        self.assertEqual(a["schema"], "loom.extract.clusters/1")
        self.assertTrue(a["clusters"])
        for c in a["clusters"]:
            self.assertGreaterEqual(c["size"], a["settings"]["min_conversations"])
            self.assertTrue(c["top_terms"])
            self.assertTrue(c["period"]["from"] <= c["period"]["to"])
        self.assertEqual(a["counts"]["clustered"] + len(a["unclustered"]), a["counts"]["conversations"])
        crit = cluster.cluster_criteria(a["clusters"][0], eng.ds, eng.lex)
        self.assertTrue(crit.keywords)
        self.assertEqual(crit.clusters, [a["clusters"][0]["id"]])
        agg = cluster.discover(units, eng.ds, eng.lex, aggregate=True)
        self.assertTrue(all(c["sample_titles"] == [] for c in agg["clusters"]))
        one = cluster.discover(units, eng.ds, eng.lex, max_clusters=1)
        self.assertEqual(len(one["clusters"]), 1)


class DecisionsIdentityTest(ExtractTest):
    def test_a_mark_is_carried_to_a_newer_export_of_the_same_conversation(self):
        """F24 (requirement 17): c2 got new messages and a new title in the newer export, so its uid changed; the
        mark made on the older export is applied by (service, ext_id) and reported as carried."""
        old = self.engine(EXTRACT / "versions/chatgpt_2025-06-20.zip")
        old.ensure_units()
        c2_old = next(u for u in old.conversations if u.ext_id == "c2")
        old.op("mark", {"unit": c2_old.uid, "decision": "include", "reason": "ważna"})
        path = old.decisions.save(self.tmp / "decisions.json")
        new = self.engine(EXTRACT / "versions/chatgpt_2025-10-01.zip")
        new.ensure_units()
        c2_new = next(u for u in new.conversations if u.ext_id == "c2")
        self.assertNotEqual(c2_new.uid, c2_old.uid, "the content changed, so the uid did")
        r = new.op("select.manual", {"decisions": str(path)})
        self.assertEqual(r["counts"]["carried"], 1)
        self.assertEqual(r["counts"]["unknown_units"], 0)
        self.assertIn(c2_new.uid, new.selection.units)
        self.assertTrue(any("przeniesiono" in str(ev.get("text")) for ev in new.events if ev["kind"] == "note"))
        crit = new.op("select.criteria", {"terms": ["nic-takiego-nie-ma"], "cascade": "lexical_only", "mode": "replace"})
        self.assertEqual(new.unit_decisions[c2_new.uid]["decision"], "include", "the cascade applies the carried mark too")
        self.assertIsNotNone(crit)
