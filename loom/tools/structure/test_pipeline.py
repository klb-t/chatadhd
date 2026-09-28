"""Author-owned integration checks, separate from independent semantic quality."""
from copy import deepcopy
import unittest

from pipeline import analyze


class PipelineTests(unittest.TestCase):
    def test_late_anchor_does_not_rewrite_prior_interpretation(self):
        record = {"id": "conversation", "turns": [
            {"id": "t", "role": "user", "text": "Żaba jest zielona. Now a new topic: Project Cedar is a database. Cedar is durable."}],
            "entities": [{"id": "cedar", "aliases": ["Cedar"]}],
            "graph": {"nodes": [{"id": "cedar", "claim_ids": ["old-claim"]}], "edges": []}}
        original = deepcopy(record)
        output = analyze(record)
        self.assertEqual(record, original)
        first = output["interpretation_context_proposals"][0]
        self.assertEqual(first["source"]["quote"], "Żaba jest zielona.")
        self.assertEqual(first["subject_candidates"], [])
        last = output["interpretation_context_proposals"][-1]
        self.assertEqual(last["subject_candidates"], ["cedar"])
        self.assertEqual(last["existing_graph_context"]["candidates"][0]["existing_claim_ids"], ["old-claim"])
        self.assertEqual(output["graph_mutations_applied"], 0)
        self.assertEqual(output["claims_created"], 0)
        for result in output["extractions"]:
            for candidate in result["candidates"]:
                span = candidate["source_span"]
                raw = record["turns"][0]["text"]
                self.assertEqual(raw.encode()[span["byte_start"]:span["byte_end"]].decode(), span["quote"])

    def test_unchecked_formula_never_becomes_premise(self):
        output = analyze({"id": "c", "turns": [{"id": "t", "role": "user", "text":
            "If pump is hot, then pump is stopped. Pump is hot."}], "entities": []})
        self.assertEqual(output["coverage"]["logical_candidates"], 2)
        self.assertEqual(len(output["reasoning"]["blocked_interpretations"]), 2)
        self.assertEqual(output["reasoning"]["candidates"], [])
        for item in output["interpretation_context_proposals"]:
            self.assertFalse(item["persistable_claim"])
            self.assertFalse(item["automatic_mutation"])

    def test_cross_topic_shape_and_budget_are_visible(self):
        record = {"id": "c", "turns": [
            {"id": "a", "text": "If pump is hot, then pump is stopped."},
            {"id": "b", "text": "If singer is tired, then singer is silent."},
            {"id": "c", "text": "If valve is warm, then pipe is closed."}],
            "entities": [{"id": "pump", "aliases": ["pump"]},
                         {"id": "singer", "aliases": ["singer"]},
                         {"id": "valve", "aliases": ["valve"]}]}
        output = analyze(record, pair_budget=1)
        self.assertEqual(output["comparison_budget"]["eligible_pairs"], 3)
        self.assertEqual(output["comparison_budget"]["omitted_pairs"], 2)
        self.assertEqual(output["comparisons"][0]["perspectives"]["logical_candidates"]["alignment"]["status"], "isomorphic")
        self.assertEqual(output, analyze(record, pair_budget=1))

    def test_unknown_text_and_metadata_survive(self):
        record = {"id": "c", "opaque": {"future": [1, 2]}, "turns": [
            {"id": "t", "role": "user", "attachment": "zip://x", "text": "Nie wiem, czy to zadziała…"}], "entities": []}
        output = analyze(record)
        self.assertEqual(output["input"], record)
        self.assertEqual(output["coverage"]["recognized_envelopes"], 0)
        self.assertTrue(output["extractions"][0]["unknown"])
        self.assertEqual(output["comparison_budget"]["eligible_pairs"], 0)

    def test_invalid_comparison_budget_rejected(self):
        for invalid in (-1, True, 0.5):
            with self.assertRaises(ValueError):
                analyze({"turns": [], "entities": []}, pair_budget=invalid)

    def test_scoped_binding_is_explicit_and_stays_untrusted(self):
        output = analyze({"id": "c", "turns": [{"id": "t", "text":
            "If motor is hot, then motor is stopped. Motor is hot."}],
            "entities": [{"id": "motor", "aliases": ["motor"]}]}, pair_budget=0)
        modes = {x["binding"]["mode"]: x for x in output["scope_projections"]}
        self.assertEqual(set(modes), {"separate", "literal_within_scope"})
        literal = modes["literal_within_scope"]
        self.assertLess(len(literal["structure"]["nodes"]), len(modes["separate"]["structure"]["nodes"]))
        self.assertTrue(literal["binding"]["cross_statement_symbols"])
        self.assertFalse(literal["inference_eligible"])
        self.assertFalse(literal["binding"]["identity_verified"])
        self.assertEqual(output["reasoning"]["candidates"], [])

    def test_scope_limit_preserves_all_source_extractions(self):
        text = " ".join(["Motor is hot."] * 65)
        output = analyze({"id": "c", "turns": [{"id": "t", "text": text}],
                          "entities": [{"id": "motor", "aliases": ["Motor"]}]}, pair_budget=0)
        self.assertEqual(output["coverage"]["recognized_envelopes"], 65)
        self.assertEqual(len(output["scope_projection_limits"]), 2)
        self.assertEqual(output["scope_projections"], [])
        self.assertEqual(output["input"]["turns"][0]["text"], text)
        self.assertEqual(output["comparison_budget"]["eligible_pairs"], 0)


if __name__ == "__main__":
    unittest.main()
