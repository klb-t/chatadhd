"""Independent developer examples; no evaluation fixture or answer key is read."""
from copy import deepcopy
import unittest

try:
    from .extract import extract_record, extract_claim, candidate_logic, compare_extractions
    from .structure_methods import infer
except ImportError:
    from extract import extract_record, extract_claim, candidate_logic, compare_extractions
    from structure_methods import infer


def extraction(text, **extra):
    return extract_record({"id": "developer-example", "source_id": "local-example", "text": text, **extra})


class ExtractionTests(unittest.TestCase):
    def test_exact_unicode_source_and_slot_spans(self):
        text = "  Jeśli żuraw jest gotowy, to dźwig jest aktywny.  \nNie wiem.\n"
        result = extraction(text, turn_id="t1", topic_id="lifting", source_span={"char_start": 410})
        self.assertEqual(result["status"], "partial")
        self.assertEqual(result["input"]["source_span"], {"char_start": 410})
        self.assertEqual(result["scope"]["topic_id"], "lifting")
        self.assertEqual(result["text"], text)
        for entry in result["candidates"] + result["unknown"]:
            span = entry["span"]
            self.assertEqual(text[span["char_start"]:span["char_end"]], span["quote"])
            self.assertEqual(text.encode()[span["byte_start"]:span["byte_end"]].decode(), span["quote"])
        for slot in result["candidates"][0]["slots"].values():
            span = slot["span"]
            self.assertEqual(text[span["char_start"]:span["char_end"]], slot["text"])
            self.assertEqual(text.encode()[span["byte_start"]:span["byte_end"]].decode(), slot["text"])

    def test_condition_keeps_repeated_identity_within_formula(self):
        result = extraction("If crane is ready, then crane is active.")
        candidate = result["candidates"][0]
        self.assertEqual(candidate["formula_candidate"]["left"]["args"], ["crane"])
        self.assertEqual(candidate["formula_candidate"]["right"]["args"], ["crane"])
        self.assertEqual(candidate["formula_status"], "unchecked")
        self.assertIsNone(candidate["confidence"])

    def test_operation_envelopes_english_and_polish(self):
        examples = [
            ("If light is red, then stop, else proceed.", "branch"),
            ("Jeśli światło jest czerwone, to stój, w przeciwnym razie jedź.", "branch"),
            ("Release the package, unless review fails.", "exception"),
            ("Wydaj paczkę, chyba że kontrola zawiedzie.", "exception"),
            ("All copper wires are conductors.", "universal_inclusion"),
            ("Wszystkie mikrofony są urządzeniami.", "universal_inclusion"),
            ("A sonnet is a kind of poem.", "type_inclusion"),
            ("Sonet jest rodzajem wiersza.", "type_inclusion"),
            ("Graphite is more conductive than wood.", "comparison"),
            ("Grafit jest bardziej przewodzący niż drewno.", "comparison"),
            ("Goal: record the concert; constraint: retain quiet dynamics.", "goal_constraint"),
            ("Cel: nagrać koncert; ograniczenie: zachować ciche fragmenty.", "goal_constraint"),
        ]
        for text, operation in examples:
            with self.subTest(text=text):
                result = extraction(text)
                self.assertEqual(result["status"], "parsed")
                self.assertEqual(result["candidates"][0]["operation"], operation)

    def test_unsupported_content_and_empty_input_survive(self):
        text = "Perhaps the old plan could work?\nWe tried three paths."
        result = extraction(text)
        self.assertEqual(result["status"], "abstained")
        self.assertEqual(len(result["unknown"]), 2)
        self.assertEqual(result["input"]["text"], text)
        self.assertEqual(result["coverage"]["envelope_character_coverage"], 0)
        self.assertIsNone(extraction(" \n ")["coverage"]["envelope_character_coverage"])

    def test_quantifier_and_negation_not_coerced(self):
        for text in ("All birds are not mammals.", "All some birds are flyers.",
                     "If pump is not active, then valve is open.",
                     "Jeśli pompa nie jest aktywna, to zawór jest otwarty."):
            with self.subTest(text=text):
                result = extraction(text)
                self.assertEqual(len(result["candidates"]), 1)
                self.assertIsNone(result["candidates"][0]["formula_candidate"])
                self.assertEqual(candidate_logic(result, accept_unchecked=True), [])
        for text in ("Nobody is ready.", "It is ready.", "Żaden jest gotowy."):
            with self.subTest(text=text):
                self.assertEqual(extraction(text)["status"], "abstained")

    def test_nested_attachment_and_multiple_sentences_abstain(self):
        for text in ("If pump is ready, then if valve is open, then drain.",
                     "Pump is active. Valve is open.",
                     "If pump is active, then valve is open, unless pressure rises."):
            with self.subTest(text=text):
                self.assertEqual(extraction(text)["status"], "abstained")

    def test_universal_shape_is_not_claim_of_induction(self):
        candidate = extraction("All lanterns are devices.")["candidates"][0]
        self.assertEqual(candidate["operation"], "universal_inclusion")
        self.assertEqual(candidate["operation_family"], "category_inclusion")
        self.assertEqual(candidate["operation_status"], "representation_only_no_performed_operation_inferred")
        formula = candidate["formula_candidate"]
        self.assertEqual(formula["op"], "forall")
        self.assertEqual(formula["body"]["left"]["predicate"], "class:lanterns")

    def test_upstream_assessment_and_unknown_metadata_preserved(self):
        record = {"id": "ob1", "text": "Pump is active.", "assessment": {"confidence": .71},
                  "locator": {"source": "sha256:example", "byte_start": 100}, "future": {"nested": [1, 2]}}
        original = deepcopy(record)
        result = extract_record(record)
        self.assertEqual(record, original)
        self.assertEqual(result["input"], original)
        self.assertEqual(result["source_id"], "sha256:example")
        self.assertIsNone(result["confidence"])
        self.assertFalse(result["persistable_claim"])

    def test_claim_only_extracts_verified_quote(self):
        observation = {"id": "ob1", "text": "Intro. Pump is active. Extra.", "locator": {"source": "src"}}
        support = {"observation": "ob1", "quote": "Pump is active."}
        claim = {"id": "cl1", "assessment": {"basis": {"support": [support]}, "confidence": .7}}
        result = extract_claim(claim, {"ob1": observation})
        self.assertEqual(result["extractions"][0]["text"], support["quote"])
        self.assertEqual(result["extractions"][0]["input"]["observation_text"], observation["text"])
        self.assertEqual(result["extractions"][0]["input"]["support_span_in_observation"]["char_start"], 7)
        self.assertEqual(result["claim"], claim)

    def test_claim_missing_mismatching_repeated_quote_abstains(self):
        for observations, quote, reason in [
            ({}, "Pump is active.", "observation_unavailable"),
            ({"ob": {"text": "Pump is idle."}}, "Pump is active.", "support_quote_not_in_observation"),
            ({"ob": {"text": "Pump is active. Pump is active."}}, "Pump is active.", "support_quote_location_ambiguous"),
            ({"ob": {"text": "Pump is active."}}, "", "support_quote_unavailable"),
        ]:
            with self.subTest(reason=reason):
                claim = {"id": "cl", "assessment": {"basis": {"support": [{"observation": "ob", "quote": quote}]}}}
                result = extract_claim(claim, observations)
                self.assertEqual(result["status"], "abstained")
                self.assertEqual(result["unknown"][0]["reason"], reason)
        with self.assertRaises(ValueError):
            extract_record({"id": "cl", "value": "Pump is active."})

    def test_logic_projection_requires_opt_in_and_cannot_infer(self):
        result = extraction("If pump is active, then valve is open.\nPump is active.")
        with self.assertRaises(ValueError):
            candidate_logic(result)
        logic = candidate_logic(result, accept_unchecked=True)
        inference = infer(logic)
        self.assertEqual(len(inference["blocked"]), 2)
        self.assertFalse(inference["candidates"])
        self.assertTrue(all(entry["confidence"] is None for entry in logic))

    def test_topic_and_turn_scope_never_merged(self):
        left = extraction("Pump is active.", turn_id="t1", topic_id="machine")
        right = extraction("Pump is active.", turn_id="t2", topic_id="metaphor")
        self.assertNotEqual(left["candidates"][0]["id"], right["candidates"][0]["id"])
        a, b = candidate_logic(left, accept_unchecked=True), candidate_logic(right, accept_unchecked=True)
        self.assertNotEqual(a[0]["qualifiers"]["scope"], b[0]["qualifiers"]["scope"])
        self.assertEqual(len(left["candidates"]), 1)

    def test_text_to_graph_distinguishes_identity_negative(self):
        source = extraction("If pump is active, then pump is ready.")
        positive = extraction("If singer is calm, then singer is prepared.")
        negative = extraction("If pump is active, then valve is ready.")
        positive_result = compare_extractions(source, positive, projection="logical_candidates")
        negative_result = compare_extractions(source, negative, projection="logical_candidates")
        self.assertEqual(positive_result["alignment"]["status"], "isomorphic")
        self.assertEqual(negative_result["alignment"]["status"], "different")
        self.assertLess(positive_result["lexical_cosine"], negative_result["lexical_cosine"])
        self.assertIsNone(positive_result["coverage"]["left"]["semantic_accuracy"])

    def test_empty_projection_is_unrepresented_not_zero_similarity_evidence(self):
        result = compare_extractions(extraction("I wonder why?"), extraction("Perhaps this works."))
        self.assertEqual(result["alignment"]["status"], "unrepresented")
        self.assertIsNone(result["alignment"]["score"])

    def test_deterministic_without_mutating_input(self):
        record = {"id": "x", "source_id": "s", "text": "Goal: finish; constraint: keep detail.", "attrs": [1]}
        original = deepcopy(record)
        self.assertEqual(extract_record(record), extract_record(record))
        self.assertEqual(record, original)


if __name__ == "__main__":
    unittest.main()
