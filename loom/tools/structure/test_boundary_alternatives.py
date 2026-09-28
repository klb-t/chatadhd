"""Source-boundary regressions using author-owned examples only."""
from copy import deepcopy
import unittest

try:
    from . import topics
    from .extract import extract_record
    from .boundary_alternatives import recover_envelopes
    from .structure_methods import infer
except ImportError:
    import topics
    from extract import extract_record
    from boundary_alternatives import recover_envelopes
    from structure_methods import infer


def primary_inputs(record, policy=None):
    segmentation = topics.analyze(record, policy)
    extractions = [extract_record({"id": observation["id"], "source_id": record.get("id"),
                                   "conversation_id": record.get("id"),
                                   "turn_id": observation["source"]["turn_id"],
                                   "segment_id": observation["segment_id"], "text": observation["text"],
                                   "source_span": deepcopy(observation["source"])})
                   for observation in segmentation["observations"]]
    return segmentation, extractions


def conversation(text):
    return {"id": "developer-conversation", "turns": [{"id": "turn-1", "role": "user", "text": text}], "entities": []}


class BoundaryAlternativeTests(unittest.TestCase):
    def test_semicolon_envelope_lost_primary_recovered_as_alternative(self):
        record = conversation("Goal: record music; constraint: preserve dynamics.")
        segmentation, primary = primary_inputs(record)
        self.assertEqual(sum(len(item["candidates"]) for item in primary), 0)
        result = recover_envelopes(record, segmentation, primary)
        self.assertEqual(len(result["alternatives"]), 1)
        alternative = result["alternatives"][0]
        self.assertEqual(alternative["operation"], "goal_constraint")
        self.assertEqual(len(alternative["overlapping_observations"]), 2)
        self.assertTrue(alternative["spans_segmentation_cut"])
        self.assertTrue(alternative["requires_scope_review"])
        self.assertFalse(alternative["counted_in_primary_coverage"])
        self.assertIsNone(alternative["formula_candidate"])
        self.assertTrue(result["diagnostic"]["primary_coverage_unchanged"])

    def test_exact_unicode_turn_and_slot_offsets_survive_leading_line(self):
        record = conversation("Wstęp bez interpretacji.\n  Cel: nagrać żurawie; ograniczenie: zachować ciszę.  ")
        segmentation, primary = primary_inputs(record)
        result = recover_envelopes(record, segmentation, primary)
        self.assertEqual(len(result["alternatives"]), 1)
        source = result["alternatives"][0]["source_span"]
        text = record["turns"][0]["text"]
        self.assertEqual(text[source["char_start"]:source["char_end"]], source["quote"])
        self.assertEqual(text.encode()[source["byte_start"]:source["byte_end"]].decode(), source["quote"])
        self.assertGreater(source["char_start"], 0)
        self.assertEqual(result["unknown_physical_lines"][0]["span"]["quote"], "Wstęp bez interpretacji.")

    def test_primary_same_turnspan_operation_is_suppressed(self):
        record = conversation("Pump is active.\nGoal: finish; constraint: keep detail.")
        segmentation, primary = primary_inputs(record)
        result = recover_envelopes(record, segmentation, primary)
        self.assertEqual(len(result["primary_matches_suppressed"]), 1)
        self.assertEqual(result["primary_matches_suppressed"][0]["operation"], "assertion")
        self.assertEqual([item["operation"] for item in result["alternatives"]], ["goal_constraint"])

    def test_identical_text_in_distinct_turns_is_not_cross_turn_deduplicated(self):
        text = "Goal: finish; constraint: keep detail."
        record = {"id": "conversation", "turns": [{"id": "one", "text": text}, {"id": "two", "text": text}], "entities": []}
        segmentation, primary = primary_inputs(record)
        result = recover_envelopes(record, segmentation, primary)
        self.assertEqual(len(result["alternatives"]), 2)
        self.assertEqual({x["source_span"]["turn_id"] for x in result["alternatives"]}, {"one", "two"})

    def test_explicit_reset_keeps_local_focus_sets_separate(self):
        record = conversation("Goal: finish Orion; constraint: preserve Vega.")
        record["entities"] = [{"id": "orion", "aliases": ["Orion"]}, {"id": "vega", "aliases": ["Vega"]}]
        # A configurable reset marker is a segmentation policy, not a parser edit.
        segmentation, primary = primary_inputs(record, {"reset_cues": ["constraint"]})
        self.assertEqual(segmentation["observations"][1]["boundary"], "explicit_reset")
        # Final segment anchor summaries are deliberately poisonous here: the
        # alternative must use only observation-local focus records.
        for segment in segmentation["segments"]:
            segment["anchor_entity_ids"].append("future-unrelated-anchor")
        result = recover_envelopes(record, segmentation, primary)
        alternative = result["alternatives"][0]
        self.assertTrue(alternative["crosses_topic_boundary"])
        self.assertIsNone(alternative["scope"]["segment_id"])
        self.assertEqual([x["focus_entity_ids"] for x in alternative["overlapping_observations"]], [["orion"], ["vega"]])
        self.assertFalse(alternative["identity_verified"])
        self.assertTrue(alternative["requires_scope_review"])
        self.assertFalse(alternative["inference_eligible"])

    def test_unknown_source_and_inputs_preserved_without_new_trust(self):
        record = conversation("Perhaps it matters?\nGoal: finish; constraint: keep detail.")
        segmentation, primary = primary_inputs(record)
        original = deepcopy((record, segmentation, primary))
        result = recover_envelopes(record, segmentation, primary)
        self.assertEqual((record, segmentation, primary), original)
        self.assertEqual(result["input"], record)
        self.assertEqual(result["turn_extractions"][0]["text"], record["turns"][0]["text"])
        self.assertEqual(len(result["unknown_physical_lines"]), 1)
        self.assertNotIn("logic", result)
        self.assertFalse(result["persistable_claim"])
        self.assertFalse(result["inference_eligible"])
        for alternative in result["alternatives"]:
            self.assertNotIn("assessment", alternative)
            self.assertEqual(alternative["extraction_status"], "unchecked")

    def test_missing_topic_mapping_remains_unassigned(self):
        record = conversation("Pump is active.")
        result = recover_envelopes(record, {"observations": []}, [])
        alternative = result["alternatives"][0]
        self.assertEqual(alternative["observation_mapping_status"], "no_observation_overlap")
        self.assertEqual(alternative["scope"]["overlapping_segment_ids"], [])
        candidate = {"claim_id": alternative["id"], "formula": alternative["formula_candidate"],
                     "extraction_status": alternative["extraction_status"]}
        self.assertFalse(infer([candidate])["candidates"])
        self.assertFalse(alternative["inference_eligible"])

    def test_bad_source_mapping_and_conflicting_primary_payload_rejected(self):
        for defect in ("byte", "conversation", "hash", "primary_conversation", "primary_payload"):
            record = conversation("Pump is active.")
            segmentation, primary = primary_inputs(record)
            if defect == "byte":
                segmentation["observations"][0]["source"]["byte_end"] -= 1
            elif defect == "conversation":
                segmentation["conversation_id"] = "foreign"
            elif defect == "hash":
                segmentation["sources"][0]["sha256"] = "wrong"
            elif defect == "primary_conversation":
                primary[0]["input"]["conversation_id"] = "foreign"
            else:
                primary[0]["candidates"][0]["slots"]["subject"]["text"] = "Valve"
            with self.subTest(defect=defect), self.assertRaises(ValueError):
                recover_envelopes(record, segmentation, primary)

    def test_deterministic_alternative_projection(self):
        record = conversation("Goal: finish; constraint: keep detail.")
        segmentation, primary = primary_inputs(record)
        self.assertEqual(recover_envelopes(record, segmentation, primary), recover_envelopes(record, segmentation, primary))


if __name__ == "__main__":
    unittest.main()
