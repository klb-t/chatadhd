"""Author-owned mechanism checks; no independent fixture or quality claims."""
from copy import deepcopy
import hashlib
import unittest

from topics import analyze, project_context, segment_conversation


def turn(ident, text, role="user"):
    return {"id": ident, "text": text, "role": role}


class TopicTests(unittest.TestCase):
    def test_late_context_cannot_authorize_earlier_ambiguous_name(self):
        entity = {"id": "relay", "aliases": [{"text": "Relay", "requires_any": ["compiler"]}]}
        earlier = turn("t1", "Relay is nearby.")
        prefix = segment_conversation([earlier], [entity])
        full = segment_conversation([earlier, turn("t2", "New topic: Relay compiler configuration.")], [entity])
        self.assertEqual(prefix["observations"], full["observations"][:1])
        self.assertFalse(full["observations"][0]["mentions"][0]["accepted"])
        self.assertEqual(full["observations"][-1]["focus_entity_ids"], ["relay"])

    def test_sentence_and_semicolon_context_do_not_leak(self):
        entities = [{"id": "relay", "aliases": [{"text": "Relay", "requires_any": ["compiler"]}]}]
        for separator in [". ", "; "]:
            result = segment_conversation([turn("t", "Relay looks interesting" + separator + "A compiler processes modules.")], entities)
            self.assertFalse(result["observations"][0]["mentions"][0]["accepted"])

    def test_mid_turn_switch_keeps_local_focus(self):
        result = segment_conversation([turn("t", "Folio stores notebooks, separately, Boreal schedules launches.")],
                                      [{"id": "f", "aliases": ["Folio"]}, {"id": "b", "aliases": ["Boreal"]}])
        self.assertEqual([o["focus_entity_ids"] for o in result["observations"]], [["f"], ["b"]])
        self.assertEqual(result["segments"][1]["boundary"], "explicit_reset")

    def test_explicit_return_uses_a_prior_segment_without_merging_it(self):
        result = segment_conversation([turn("a", "Folio stores notebooks."), turn("b", "Unrelated: garden soil needs watering."),
                                       turn("c", "Back to Folio: notebook search.")], [{"id": "f", "aliases": ["Folio"]}])
        self.assertEqual(result["segments"][-1]["return_to_segment_ids"], [result["segments"][0]["id"]])
        self.assertEqual(len(result["segments"]), 3)
        self.assertEqual(result["observations"][1]["focus_entity_ids"], [])

    def test_corroborated_continuation_has_local_and_anchor_evidence(self):
        result = segment_conversation([turn("a", "Folio notebook index uses storage pages."),
                                       turn("b", "The notebook index now supports compressed pages.")],
                                      [{"id": "f", "aliases": ["Folio"]}])
        self.assertEqual(result["observations"][1]["focus_basis"], "corroborated_continuity_candidate")
        self.assertEqual(result["observations"][1]["focus_evidence_observation_ids"], [o["id"] for o in result["observations"]])

    def test_bare_pronoun_does_not_establish_identity(self):
        result = segment_conversation([turn("a", "Folio and Boreal differ."), turn("b", "It is ready.")],
                                      [{"id": "f", "aliases": ["Folio"]}, {"id": "b", "aliases": ["Boreal"]}])
        observation = result["observations"][1]
        self.assertEqual(observation["focus_entity_ids"], [])
        self.assertEqual(observation["reference_status"], "unresolved")
        self.assertEqual(observation["possible_continuation_entity_ids"], ["b", "f"])

    def test_distant_context_and_negative_context_are_not_identity(self):
        entities = [{"id": "relay", "aliases": [{"text": "Relay", "requires_any": ["compiler"], "excludes": ["race"]}]}]
        text = "Relay " + "filler " * 20 + "compiler"
        result = segment_conversation([turn("a", text), turn("b", "Relay compiler race.")], entities)
        self.assertEqual([o["mentions"][0]["accepted"] for o in result["observations"]], [False, False])
        self.assertEqual(result["observations"][1]["mentions"][0]["reason"], "negative_local_context")

    def test_short_unicode_and_symbol_alias_boundaries(self):
        result = segment_conversation([turn("a", "xEP EPx EP_ aEPą EP. C++ C++17")],
                                      [{"id": "ep", "aliases": ["EP"]}, {"id": "cpp", "aliases": ["C++"]}])
        matches = [m for o in result["observations"] for m in o["mentions"]]
        self.assertEqual([(m["entity_id"], m["surface"]) for m in matches], [("ep", "EP"), ("cpp", "C++")])

    def test_source_character_and_byte_offsets_are_exact(self):
        text = "Żółw śpi. New topic: Boreal 😀 flies."
        result = segment_conversation([turn("a", text)], [{"id": "b", "aliases": ["Boreal"]}])
        for obs in result["observations"]:
            source = obs["source"]
            self.assertEqual(text[source["char_start"]:source["char_end"]], obs["text"])
            self.assertEqual(text.encode()[source["byte_start"]:source["byte_end"]].decode(), obs["text"])
            self.assertEqual(source["sha256"], hashlib.sha256(text.encode()).hexdigest())

    def test_focus_expiration_cannot_be_renewed_by_bare_cues(self):
        result = segment_conversation([turn("a", "Folio exists."), turn("b", "Yes."), turn("c", "It works."),
                                       turn("d", "This is fine.")], [{"id": "f", "aliases": ["Folio"]}])
        self.assertEqual(result["observations"][-1]["boundary"], "focus_expired")
        self.assertEqual(result["observations"][-1]["possible_continuation_entity_ids"], [])

    def test_lexical_novelty_flags_unnamed_new_topic(self):
        result = segment_conversation([turn("a", "Folio indexes notes."),
                                       turn("b", "Weather balloons measure atmospheric pressure during storm research.")],
                                      [{"id": "f", "aliases": ["Folio"]}])
        self.assertEqual(result["observations"][1]["boundary"], "lexical_novelty_candidate")
        self.assertEqual(result["observations"][1]["focus_entity_ids"], [])

    def test_validation_rejects_duplicate_source_ids_and_bad_policy(self):
        with self.assertRaises(ValueError):
            segment_conversation([turn("a", "one"), turn("a", "two")], [])
        with self.assertRaises(ValueError):
            segment_conversation([], [], {"invented_threshold": 0})


class ContextTests(unittest.TestCase):
    def setUp(self):
        self.entities = [{"id": "f", "aliases": ["Folio"]}]
        self.graph = {"nodes": [{"id": "f", "claim_ids": ["old-project"]}, {"id": "storage", "claim_ids": ["old-storage"]},
                                {"id": "disk", "claim_ids": ["old-disk"]}],
                      "edges": [{"source": "f", "target": "storage", "predicate": "has_part", "claim_ids": ["edge-proof"],
                                 "qualifiers": {"scope": "branch-one"}},
                                {"source": "storage", "target": "disk", "predicate": "uses"}]}

    def test_one_hop_context_preserves_existing_claims_and_scope(self):
        report = segment_conversation([turn("a", "Folio notebook index.")], self.entities)
        before = deepcopy(self.graph)
        result = project_context(report, self.graph)
        self.assertEqual(self.graph, before)
        context = result["contexts"][0]
        self.assertEqual([c["node_id"] for c in context["candidates"]], ["f", "storage"])
        self.assertEqual(context["candidates"][1]["via"][0]["claim_ids"], ["edge-proof"])
        self.assertEqual(context["candidates"][1]["via"][0]["qualifiers"], {"scope": "branch-one"})
        self.assertEqual(context["eligibility_as_premise"], "not_assessed")
        self.assertEqual(result["mutations_applied"], 0)

    def test_claim_crossing_topic_boundary_remains_scope_review(self):
        text = "Folio index. Unrelated: weather changes."
        report = segment_conversation([turn("a", text)], self.entities)
        result = project_context(report, self.graph, [{"id": "new", "source": {"turn_id": "a", "char_start": 0, "char_end": len(text)}}])
        claim = next(p for p in result["update_proposals"] if p["kind"] == "claim_context_review")
        self.assertTrue(claim["crosses_topic_boundary"])
        self.assertTrue(claim["requires_scope_review"])
        self.assertFalse(claim["automatic_mutation"])
        self.assertFalse(claim["persistable_claim"])

    def test_claim_cannot_borrow_later_segment_anchor(self):
        text = "A vague beginning. Folio index."
        result = analyze({"turns": [turn("a", text)], "entities": self.entities, "graph": self.graph,
                          "claims": [{"id": "early", "source": {"turn_id": "a", "char_start": 0, "char_end": 17}}]})
        claim = next(p for p in result["context_projection"]["update_proposals"] if p["kind"] == "claim_context_review")
        self.assertEqual(claim["subject_candidates"], [])

    def test_invalid_claim_span_is_blocked(self):
        report = segment_conversation([turn("a", "Folio.")], self.entities)
        result = project_context(report, self.graph, [{"id": "bad", "source": {"turn_id": "a", "char_start": 0, "char_end": 999}}])
        self.assertEqual(result["blocked_claims"], [{"claim_id": "bad", "reason": "invalid_source_span"}])

    def test_view_compaction_retains_every_observation(self):
        report = segment_conversation([turn("a", "Folio index."), turn("b", "Folio index.")], self.entities)
        result = project_context(report, self.graph)
        compact = next(p for p in result["update_proposals"] if p["kind"] == "compact_repeated_observation_view")
        self.assertEqual(compact["observation_ids"], [o["id"] for o in report["observations"]])
        self.assertTrue(compact["preserve_all_observations"])
        self.assertFalse(compact["merge_claims"])

    def test_node_budget_does_not_hide_omissions(self):
        report = segment_conversation([turn("a", "Folio.")], self.entities, {"context_node_budget": 1})
        result = project_context(report, self.graph)
        self.assertEqual(result["contexts"][0]["omitted_by_budget"], 1)

    def test_determinism_and_input_immutability(self):
        record = {"id": "c", "turns": [turn("a", "Folio.")], "entities": self.entities, "graph": self.graph}
        before = deepcopy(record)
        self.assertEqual(analyze(record), analyze(record))
        self.assertEqual(record, before)


if __name__ == "__main__":
    unittest.main()
