"""Safety boundaries for coordinate assistance, not semantic quality tests."""
from copy import deepcopy
import unittest

try:
    from .anchor_assist import assist_bundle
    from . import live_pilot as pilot
except ImportError:
    from anchor_assist import assist_bundle
    import live_pilot as pilot


def packet(text):
    return {"schema": "loom.source_packet/1", "snapshot_id": "snapshot",
            "observations": [{"id": "ob", "text": text, "unit": "u", "locator": {"source": "test"}}],
            "entities": [], "claims": []}


def bundle(span):
    return {"schema": "loom.candidate_graph/1", "packet_id": "snapshot",
            "entity_drafts": [{"handle": "@e", "kind": "term_occurrence", "label": "unchanged",
                               "attrs": {"term_type": "constant", "symbol": "unchanged"}, "support": [span]}],
            "claim_drafts": [], "roots": [], "coverage": [], "unknowns": []}


def span(quote, start=0, length=1):
    return {"observation": "ob", "byte_start": start, "byte_len": length, "quote": quote}


class AnchorAssistTests(unittest.TestCase):
    def test_unicode_unique_quote_changes_only_coordinates_without_mutation(self):
        source = packet("🟨 Żółć.")
        original = bundle(span("Żółć", 2, 4))
        frozen = deepcopy(original)
        assisted, audit = assist_bundle(source, original)
        self.assertEqual(original, frozen)
        expected = deepcopy(original)
        expected["entity_drafts"][0]["support"][0].update(byte_start=5, byte_len=8)
        self.assertEqual(assisted, expected)
        self.assertEqual(audit["spans"][0]["old"], frozen["entity_drafts"][0]["support"][0])
        self.assertEqual(audit["decision_counts"], {"coordinates_updated": 1})
        repeated, second_audit = assist_bundle(source, assisted)
        self.assertEqual(repeated, assisted)
        self.assertEqual(second_audit["decision_counts"], {"already_exact": 1})

    def test_overlapping_and_repeated_quotes_never_choose_an_occurrence(self):
        for text, quote in (("aaa", "aa"), ("red red", "red")):
            original = bundle(span(quote, 0, len(quote)))
            assisted, audit = assist_bundle(packet(text), original)
            self.assertEqual(assisted, original)
            self.assertEqual(audit["decision_counts"], {"quote_ambiguous_unchanged": 1})

    def test_no_case_folding_unicode_normalization_or_whitespace_repair(self):
        for text, quote in (("RED", "red"), ("e\u0301", "é"), ("two  words", "two words")):
            original = bundle(span(quote))
            assisted, audit = assist_bundle(packet(text), original)
            self.assertEqual(assisted, original)
            self.assertEqual(audit["decision_counts"], {"quote_absent_unchanged": 1})

    def test_no_schema_field_or_foreign_observation_repair(self):
        for value in (span("x", True), span("x", 0.0), {**span("x"), "extra": 1},
                      {"observation": "ob", "quote": "x", "byte_start": 1}, span(""),
                      {**span("x"), "observation": "foreign"}):
            original = bundle(value)
            assisted, audit = assist_bundle(packet("x"), original)
            self.assertEqual(assisted, original)
            self.assertNotIn("coordinates_updated", audit["decision_counts"])

    def test_only_declared_support_locations_change(self):
        original = bundle(span("x", 9, 5))
        original["entity_drafts"][0]["attrs"]["support"] = [span("x", 9, 5)]
        original["claim_drafts"] = [{"assessment": {"basis": {"support": [span("x", 9, 5)]}}}]
        original["coverage"] = [{"support": [span("x", 9, 5)]}]
        original["unknowns"] = [{"support": [span("x", 9, 5)]}]
        assisted, audit = assist_bundle(packet("x"), original)
        self.assertEqual(audit["decision_counts"], {"coordinates_updated": 4})
        self.assertEqual(assisted["entity_drafts"][0]["attrs"], original["entity_drafts"][0]["attrs"])

    def test_foreign_packet_and_duplicate_observation_ids_are_rejected(self):
        original = bundle(span("x")); original["packet_id"] = "foreign"
        with self.assertRaises(ValueError):
            assist_bundle(packet("x"), original)
        source = packet("x"); source["observations"] *= 2
        with self.assertRaises(ValueError):
            assist_bundle(source, bundle(span("x")))

    def test_duplicate_json_never_reaches_coordinate_assistance(self):
        with self.assertRaisesRegex(ValueError, "duplicate JSON key"):
            pilot.parse(b'{"entity_drafts":[],"entity_drafts":[{}]}')


if __name__ == "__main__":
    unittest.main()
