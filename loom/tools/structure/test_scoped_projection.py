"""Author-owned composition examples; no independent fixtures or outcomes read."""
from copy import deepcopy
import unittest

try:
    from .extract import extract_record
    from .scoped_projection import project_scope, compare_scopes
    from .structure_methods import infer
except ImportError:
    from extract import extract_record
    from scoped_projection import project_scope, compare_scopes
    from structure_methods import infer


def observations(texts, conversation="conversation-a", segment="segment-a"):
    return [extract_record({"id": "observation-" + str(index), "source_id": "archive-a",
                            "conversation_id": conversation, "segment_id": segment,
                            "turn_id": "turn-" + str(index), "text": text,
                            "scope_status": "candidate", "scope_evidence": {"method": "developer_declared"}})
            for index, text in enumerate(texts)]


class ScopedProjectionTests(unittest.TestCase):
    def test_shared_symbols_are_assumptions_and_source_is_preserved(self):
        source = observations(["If pump is active, then pump is ready.", "Pump is active."])
        original = deepcopy(source)
        result = project_scope(source, binding="literal_within_scope")
        self.assertEqual(source, original)
        self.assertEqual(result["extractions"], original)
        self.assertFalse(result["binding"]["identity_verified"])
        shared = {(x["kind"], x["symbol"]): x for x in result["binding"]["cross_statement_symbols"]}
        self.assertEqual(shared[("constant", "pump")]["status"], "assumed_same_referent")
        self.assertEqual(len(shared[("constant", "pump")]["candidate_ids"]), 2)
        self.assertEqual(result["scope"]["uncertainties"][0]["scope_status"], "candidate")
        self.assertFalse(result["limits"]["statement_order_represented"])

    def test_literal_binding_recovers_multi_statement_identity_discriminator(self):
        a = observations(["If pump is active, then pump is ready.", "Pump is active."])
        b = observations(["If singer is calm, then singer is prepared.", "Singer is calm."], "conversation-b", "segment-b")
        c = observations(["If pump is active, then pump is ready.", "Valve is active."], "conversation-c", "segment-c")
        separate_a = project_scope(a)
        separate_c = project_scope(c)
        separate = compare_scopes(separate_a, separate_c)["comparisons"]
        self.assertEqual(separate["logical_structural"]["alignment"]["status"], "isomorphic")
        scoped_a = project_scope(a, "literal_within_scope")
        positive = compare_scopes(scoped_a, project_scope(b, "literal_within_scope"))["comparisons"]
        negative = compare_scopes(scoped_a, project_scope(c, "literal_within_scope"))["comparisons"]
        self.assertEqual(positive["logical_structural"]["alignment"]["status"], "isomorphic")
        self.assertEqual(positive["logical_literal_semantic"]["alignment"]["status"], "different")
        self.assertEqual(negative["logical_structural"]["alignment"]["status"], "different")
        # The outer operation inventory cannot see this identity difference.
        self.assertEqual(negative["atomic_operations"]["alignment"]["status"], "isomorphic")

    def test_unknowns_and_nonlogical_operations_remain_in_scope(self):
        source = observations(["Pump is active.", "Maybe the speaker meant another pump?", "Goal: record music; constraint: keep dynamics."])
        result = project_scope(source, "literal_within_scope")
        self.assertEqual(result["coverage"]["source_records"], 3)
        self.assertEqual(result["coverage"]["physical_units"], 3)
        self.assertEqual(result["coverage"]["logical_formulas"], 1)
        self.assertEqual(len(result["unknown_statements"]), 1)
        self.assertEqual(len(result["logical_omissions"]), 1)
        self.assertEqual(result["unknown_statements"][0]["span"]["quote"], source[1]["text"])
        self.assertIsNone(result["coverage"]["semantic_accuracy"])

    def test_missing_or_blank_explicit_scope_is_rejected(self):
        for key in ("conversation_id", "segment_id"):
            for value in (None, "", " ", 123):
                source = observations(["Pump is active."])
                source[0]["input"][key] = value
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    project_scope(source)
        with self.assertRaises(ValueError):
            project_scope([])

    def test_cross_conversation_or_segment_merge_is_rejected(self):
        first = observations(["Pump is active."])[0]
        for conversation, segment in (("different-conversation", "segment-a"), ("conversation-a", "different-segment")):
            second = observations(["Pump is ready."], conversation, segment)[0]
            second["record_id"] = "other-record"
            with self.subTest(conversation=conversation, segment=segment), self.assertRaises(ValueError):
                project_scope([first, second], "literal_within_scope")

    def test_conflicting_redundant_scope_is_rejected(self):
        for target in ("extraction", "candidate", "top_level"):
            source = observations(["Pump is active."])
            if target == "extraction":
                source[0]["scope"]["segment_id"] = "another-segment"
            elif target == "candidate":
                source[0]["candidates"][0]["scope"]["segment_id"] = "another-segment"
            else:
                source[0]["conversation_id"] = "another-conversation"
            with self.subTest(target=target), self.assertRaises(ValueError):
                project_scope(source)

    def test_duplicates_are_rejected_without_inflating_evidence(self):
        source = observations(["Pump is active."])
        with self.assertRaises(ValueError):
            project_scope(source + deepcopy(source))
        source = observations(["Pump is active.", "Valve is ready."])
        source[1]["candidates"][0]["id"] = source[0]["candidates"][0]["id"]
        with self.assertRaises(ValueError):
            project_scope(source)

    def test_source_record_turn_and_slot_conflicts_are_rejected(self):
        for defect in ("source", "record", "candidate_turn", "extraction_topic", "slot_text"):
            source = observations(["Pump is active."])
            candidate = source[0]["candidates"][0]
            if defect == "source":
                source[0]["source_id"] = candidate["source_id"] = "foreign-source"
            elif defect == "record":
                source[0]["record_id"] = candidate["record_id"] = "foreign-record"
            elif defect == "candidate_turn":
                candidate["scope"]["turn_id"] = "foreign-turn"
            elif defect == "extraction_topic":
                source[0]["scope"]["topic_id"] = "foreign-topic"
            else:
                candidate["slots"]["subject"]["text"] = "Valve"
            with self.subTest(defect=defect), self.assertRaises(ValueError):
                project_scope(source)

    def test_atomic_graph_is_rebuilt_and_unknown_cannot_override_owner(self):
        source = observations(["Pump is active.", "Maybe it matters?"])
        source[0]["structure"] = {"nodes": [], "edges": []}
        source[1]["unknown"][0]["record_id"] = "foreign-record"
        result = project_scope(source)
        self.assertGreater(len(result["atomic_structure"]["nodes"]), 0)
        self.assertEqual(result["unknown_statements"][0]["record_id"], source[1]["record_id"])
        self.assertEqual(result["extractions"][1]["unknown"][0]["record_id"], "foreign-record")

    def test_quotes_offsets_and_missing_unknowns_are_checked(self):
        for defect in ("quote", "byte", "text", "missing_unknown"):
            source = observations(["Żuraw jest gotowy.\nNie wiem."])
            if defect == "quote":
                source[0]["candidates"][0]["span"]["quote"] = "invented"
            elif defect == "byte":
                source[0]["candidates"][0]["span"]["byte_end"] -= 1
            elif defect == "text":
                source[0]["input"]["text"] += "Untracked."
            else:
                source[0]["unknown"] = []
            with self.subTest(defect=defect), self.assertRaises(ValueError):
                project_scope(source)

    def test_projected_candidates_cannot_enter_inference(self):
        source = observations(["If pump is active, then pump is ready.", "Pump is active."])
        for extraction in source:
            extraction["input"]["assessment"] = {"evidence_class": "user", "confidence": 1.0}
            for candidate in extraction["candidates"]:
                candidate["assessment"] = {"evidence_class": "user", "confidence": 1.0}
        result = project_scope(source, "literal_within_scope")
        self.assertFalse(result["inference_eligible"])
        self.assertFalse(result["persistable_claim"])
        self.assertTrue(all("assessment" not in entry for entry in result["projection_inputs"]))
        checked = infer(result["projection_inputs"])
        self.assertFalse(checked["candidates"])
        self.assertEqual(len(checked["blocked"]), 2)

    def test_quantifier_binders_are_not_cross_statement_symbols(self):
        result = project_scope(observations(["All lamps are devices.", "All lanterns are devices."]), "literal_within_scope")
        binders = [node for node in result["structure"]["nodes"] if node["kind"] == "bound_variable"]
        self.assertEqual(len(binders), 2)
        self.assertFalse(any(x["symbol"] == "_member" for x in result["binding"]["cross_statement_symbols"]))

    def test_all_abstained_is_unrepresented_not_evidence_of_difference(self):
        a = project_scope(observations(["Perhaps it depends?"]))
        b = project_scope(observations(["We cannot tell from here."], "b", "b"))
        result = compare_scopes(a, b)
        self.assertEqual(a["status"], "logically_unrepresented")
        for comparison in result["comparisons"].values():
            self.assertEqual(comparison["alignment"]["status"], "unrepresented")
            self.assertIsNone(comparison["alignment"]["score"])

    def test_binding_mismatch_and_budgets_are_explicit(self):
        source = observations(["Pump is active.", "Pump is ready."])
        with self.assertRaises(ValueError):
            compare_scopes(project_scope(source), project_scope(source, "literal_within_scope"))
        for kwargs in ({"max_formulas": 1}, {"max_graph_nodes": 1}, {"max_graph_nodes": 513}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                project_scope(source, **kwargs)
        projected = project_scope(source)
        result = compare_scopes(projected, projected, budget=1)
        self.assertEqual(result["budget_per_projection"], 1)
        self.assertTrue(all(x["alignment"]["explored"] <= 1 for x in result["comparisons"].values()))

    def test_deterministic_output_preserves_input_order_without_encoding_it(self):
        source = observations(["Pump is active.", "Pump is ready."])
        self.assertEqual(project_scope(source), project_scope(source))
        reversed_source = list(reversed(source))
        result = compare_scopes(project_scope(source, "literal_within_scope"), project_scope(reversed_source, "literal_within_scope"))
        self.assertEqual(result["comparisons"]["logical_structural"]["alignment"]["status"], "isomorphic")


if __name__ == "__main__":
    unittest.main()
