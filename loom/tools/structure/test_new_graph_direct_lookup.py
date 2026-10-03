"""Independent authored mechanism examples for direct source-graph lookup.

No fixture packets, model outputs, validation gold, network, or credentials are
read. Literal source binding is tested separately from semantic correctness.
"""
from copy import deepcopy
import unittest

try:
    from . import new_graph_direct_lookup as direct
except ImportError:
    import new_graph_direct_lookup as direct


T1 = "2026-09-01T10:00:00Z"
T2 = "2026-09-02T10:00:00Z"
T3 = "2026-09-03T10:00:00Z"
T4 = "2026-09-04T10:00:00Z"


def query(source="a", target="c", *, relation="implies", speaker="author", as_of=T4):
    return {"id": "authored-direct-query", "relation": relation, "source": source,
            "target": target, "attributed_to": speaker, "as_of": as_of,
            "scope": "explicit_source"}


class AuthoredGraph:
    """Small data builder with independently computed, full-turn byte spans."""

    def __init__(self):
        self.case = {"id": "authored-direct-case", "source_id": "authored-source",
                     "turns": [], "node_inventory": [
                         {"id": ident, "text": "proposition " + ident, "aliases": []}
                         for ident in ("a", "b", "c", "d", "e", "not:a", "not:b", "not:c")]}
        self.compiled = {"case_id": self.case["id"], "state": "completed",
                         "source_assertions": [], "status_events": []}

    def add(self, ident, source="a", target="c", *, polarity="positive",
            relation="implies", speaker="author", known_at=T1, text=None):
        if text is None:
            text = f"Żuraw {ident}: {speaker} {polarity} {source} {relation} {target}."
        turn_id = "turn-" + ident
        self.case["turns"].append({"id": turn_id, "speaker": speaker,
                                  "known_at": known_at, "text": text})
        evidence = [{"source_id": self.case["source_id"], "turn_id": turn_id,
                     "quote": text, "char_start": 0, "char_end": len(text),
                     "byte_start": 0, "byte_end": len(text.encode("utf-8")),
                     "coordinate_space": "turn.text"}]
        row = {"id": ident, "relation": relation, "source": source, "target": target,
               "polarity": polarity, "attributed_to": speaker, "known_at": known_at,
               "evidence": evidence, "basis_class": "observed_source_assertion",
               "content_truth": "unverified"}
        self.compiled["source_assertions"].append(row)
        return row

    def replace(self, old, new):
        row = next(r for r in self.compiled["source_assertions"] if r["id"] == new)
        event = {"assertion_id": old, "status": "superseded", "superseded_by": new,
                 "known_at": row["known_at"], "evidence": deepcopy(row["evidence"])}
        self.compiled["status_events"].append(event)
        return event

    def lookup(self, q=None, policy=None):
        return direct.lookup(self.case, self.compiled, q or query(), policy)


class DirectGraphLookupMechanismTests(unittest.TestCase):
    def assert_completed(self, result, label):
        self.assertEqual(result["state"], "completed")
        self.assertEqual(result["label"], label)
        self.assertEqual(result["query_id"], "authored-direct-query")
        self.assertEqual(result["content_truth"], "unverified")
        self.assertEqual(result["pipeline_mode"],
                         "retrospective_full_conversation_extraction_then_source_cutoff")
        self.assertFalse(result["causal_prefix_extraction_verified"])

    def assert_unavailable(self, result):
        self.assertEqual(result["state"], "unavailable")
        self.assertIsNone(result["label"])
        self.assertEqual(result["paths"], [])

    def test_direct_positive_has_one_assertion_source_path_not_multi_hop_proof(self):
        graph = AuthoredGraph()
        row = graph.add("positive")
        result = graph.lookup()
        self.assert_completed(result, "supported")
        path, = result["paths"]
        self.assertEqual(path["assertion_ids"], ["positive"])
        self.assertEqual(path["assertions"], [row])
        self.assertEqual(path["known_at"], T1)
        self.assertEqual(path["evidence"], row["evidence"])

    def test_direct_negative_refutes_source_assertion_not_content_truth(self):
        graph = AuthoredGraph()
        graph.add("negative", polarity="negative")
        result = graph.lookup()
        self.assert_completed(result, "refuted")
        self.assertEqual(result["paths"][0]["assertion_ids"], ["negative"])

    def test_empty_graph_and_unmatched_graph_are_unknown(self):
        graph = AuthoredGraph()
        self.assert_completed(graph.lookup(), "unknown")
        graph.add("unrelated", "b", "d")
        result = graph.lookup()
        self.assert_completed(result, "unknown")
        self.assertEqual(result["paths"], [])

    def test_two_hop_implication_never_becomes_direct_source_assertion(self):
        graph = AuthoredGraph()
        graph.add("ab", "a", "b")
        graph.add("bc", "b", "c")
        result = graph.lookup()
        self.assert_completed(result, "unknown")
        self.assertEqual(result["paths"], [])

    def test_reflexive_query_needs_an_explicit_source_self_edge(self):
        graph = AuthoredGraph()
        self.assert_completed(graph.lookup(query("a", "a")), "unknown")
        graph.add("explicit-self", "a", "a")
        result = graph.lookup(query("a", "a"))
        self.assert_completed(result, "supported")
        self.assertEqual(result["paths"][0]["assertion_ids"], ["explicit-self"])

    def test_exact_predicate_direction_and_target_are_required(self):
        for row in (("a", "c", "causes"), ("c", "a", "implies"),
                    ("a", "b", "implies"), ("b", "c", "implies")):
            with self.subTest(row=row):
                graph = AuthoredGraph()
                graph.add("different", row[0], row[1], relation=row[2])
                self.assert_completed(graph.lookup(), "unknown")

    def test_attribution_is_exact_even_if_reporter_and_quoted_speaker_differ(self):
        graph = AuthoredGraph()
        row = graph.add("reported", speaker="quoted-speaker",
                        text="Reporter quotes quoted-speaker: a implies c.")
        graph.case["turns"][0]["speaker"] = "reporter"
        self.assert_completed(graph.lookup(), "unknown")
        result = graph.lookup(query(speaker="quoted-speaker"))
        self.assert_completed(result, "supported")
        self.assertEqual(result["paths"][0]["assertions"], [row])

    def test_future_assertion_is_excluded_and_cutoff_is_inclusive(self):
        graph = AuthoredGraph()
        graph.add("future", known_at=T2)
        self.assert_completed(graph.lookup(query(as_of=T1)), "unknown")
        self.assert_completed(graph.lookup(query(as_of=T2)), "supported")

    def test_latest_negative_overrides_label_but_not_preserved_positive_history(self):
        graph = AuthoredGraph()
        graph.add("positive")
        graph.add("negative", polarity="negative", known_at=T2)
        result = graph.lookup()
        self.assert_completed(result, "refuted")
        self.assertEqual(set(result["history_assertion_ids"]), {"positive", "negative"})
        self.assertEqual(set(result["active_assertion_ids"]), {"positive", "negative"})
        self.assertEqual(set(result["matching_active_assertion_ids"]), {"positive", "negative"})
        self.assertEqual(result["latest_match_ids"], ["negative"])

    def test_latest_positive_overrides_older_negative_without_truth_promotion(self):
        graph = AuthoredGraph()
        graph.add("negative", polarity="negative")
        graph.add("positive", known_at=T2)
        result = graph.lookup()
        self.assert_completed(result, "supported")
        self.assertEqual(result["latest_match_ids"], ["positive"])

    def test_repeated_latest_positives_remain_distinct_source_paths(self):
        graph = AuthoredGraph()
        graph.add("old")
        graph.add("latest-a", known_at=T2)
        graph.add("latest-b", known_at=T2)
        result = graph.lookup()
        self.assert_completed(result, "supported")
        self.assertEqual(set(result["latest_match_ids"]), {"latest-a", "latest-b"})
        self.assertEqual({tuple(p["assertion_ids"]) for p in result["paths"]},
                         {("latest-a",), ("latest-b",)})

    def test_same_latest_instant_opposite_polarities_are_conflicting(self):
        graph = AuthoredGraph()
        graph.add("positive", known_at=T2)
        graph.add("negative", polarity="negative", known_at="2026-09-02T12:00:00+02:00")
        result = graph.lookup()
        self.assert_completed(result, "conflicting")
        self.assertEqual(set(result["latest_match_ids"]), {"positive", "negative"})

    def test_time_order_and_cutoff_use_instants_not_lexical_timestamp_order(self):
        graph = AuthoredGraph()
        graph.add("positive", known_at="2026-09-02T12:00:00+02:00")
        graph.add("negative", polarity="negative", known_at="2026-09-02T05:00:01-05:00")
        self.assert_completed(graph.lookup(query(as_of=T2)), "supported")
        result = graph.lookup(query(as_of=T3))
        self.assert_completed(result, "refuted")
        self.assertEqual(result["latest_match_ids"], ["negative"])

    def test_future_correction_keeps_old_prefix_view_then_removes_old_at_cutoff(self):
        graph = AuthoredGraph()
        graph.add("old")
        graph.add("replacement", "a", "d", known_at=T2)
        graph.replace("old", "replacement")
        before = graph.lookup(query(as_of=T1))
        self.assert_completed(before, "supported")
        self.assertEqual(before["eligible_status_events"], [])
        after = graph.lookup(query(as_of=T2))
        self.assert_completed(after, "unknown")
        self.assertIn("old", after["history_assertion_ids"])
        self.assertNotIn("old", after["active_assertion_ids"])
        self.assertEqual(len(after["eligible_status_events"]), 1)

    def test_all_applicable_events_remove_superseded_ids_in_a_chain(self):
        graph = AuthoredGraph()
        graph.add("old")
        graph.add("middle", "a", "d", known_at=T2)
        graph.add("new", "a", "e", known_at=T3)
        graph.replace("old", "middle")
        graph.replace("middle", "new")
        result = graph.lookup()
        self.assert_completed(result, "unknown")
        self.assertEqual(set(result["active_assertion_ids"]), {"new"})
        self.assertEqual(set(result["history_assertion_ids"]), {"old", "middle", "new"})

    def test_superseding_latest_negative_can_reveal_explicitly_active_old_positive(self):
        graph = AuthoredGraph()
        graph.add("old-positive")
        graph.add("negative", polarity="negative", known_at=T2)
        graph.add("replacement", "a", "d", known_at=T3)
        graph.replace("negative", "replacement")
        result = graph.lookup()
        self.assert_completed(result, "supported")
        self.assertEqual(result["latest_match_ids"], ["old-positive"])
        self.assertNotIn("negative", result["active_assertion_ids"])

    def test_cross_attribution_supersession_is_visible_as_warning_not_repaired(self):
        graph = AuthoredGraph()
        graph.add("old")
        graph.add("new", "a", "d", known_at=T2, speaker="other")
        graph.replace("old", "new")
        result = graph.lookup()
        self.assert_completed(result, "unknown")
        self.assertNotIn("old", result["active_assertion_ids"])
        self.assertIn(direct.CROSS_ATTRIBUTION, result["warnings"])

    def test_unsupported_withdrawal_is_unavailable_and_has_explicit_warning(self):
        for status in ("withdrawn", "superseded"):
            with self.subTest(status=status):
                graph = AuthoredGraph()
                graph.add("old")
                replacement = graph.add("withdrawal-source", "a", "d", known_at=T2)
                graph.compiled["status_events"].append({
                    "assertion_id": "old", "status": status, "known_at": T2,
                    "evidence": deepcopy(replacement["evidence"])})
                result = graph.lookup()
                self.assert_unavailable(result)
                self.assertIn(direct.UNSUPPORTED_WITHDRAWAL, result["warnings"])

    def test_future_invalid_event_does_not_contaminate_earlier_view(self):
        graph = AuthoredGraph()
        graph.add("positive")
        graph.compiled["status_events"].append({
            "assertion_id": "missing", "status": "withdrawn", "known_at": T3,
            "evidence": []})
        self.assert_completed(graph.lookup(query(as_of=T1)), "supported")

    def test_negated_operand_ids_are_opaque_and_never_support_contraposition(self):
        graph = AuthoredGraph()
        graph.add("ab", "a", "b")
        self.assert_completed(graph.lookup(query("not:b", "not:a")), "unknown")
        graph.add("explicit-negated", "not:a", "not:c")
        self.assert_completed(graph.lookup(query("not:a", "not:c")), "supported")
        self.assert_completed(graph.lookup(query("a", "c")), "unknown")

    def test_inventory_alias_and_text_similarity_never_create_node_id_equality(self):
        graph = AuthoredGraph()
        graph.case["node_inventory"][2]["aliases"] = ["d"]
        graph.case["node_inventory"][3]["text"] = graph.case["node_inventory"][2]["text"]
        graph.add("ad", "a", "d")
        self.assert_completed(graph.lookup(), "unknown")

    def test_source_binding_is_not_independent_semantic_verification(self):
        graph = AuthoredGraph()
        graph.add("semantically-wrong", text="Żuraw reports no directed relation here.")
        result = graph.lookup()
        self.assert_completed(result, "supported")
        self.assertEqual(result["content_truth"], "unverified")

    def test_exact_source_quote_coordinate_and_turn_binding_are_revalidated(self):
        mutations = [("source_id", "another-source"), ("turn_id", "missing-turn"),
                     ("quote", "unseen quote"), ("char_end", 1), ("byte_end", 1),
                     ("coordinate_space", "another-space")]
        for key, value in mutations:
            with self.subTest(key=key):
                graph = AuthoredGraph()
                row = graph.add("edge")
                row["evidence"][0][key] = value
                self.assert_unavailable(graph.lookup())

    def test_assertion_timestamp_must_match_its_actual_supporting_turn(self):
        graph = AuthoredGraph()
        row = graph.add("edge")
        row["known_at"] = T2
        self.assert_unavailable(graph.lookup())

    def test_duplicate_assertion_identity_is_unavailable(self):
        graph = AuthoredGraph()
        row = graph.add("edge")
        graph.compiled["source_assertions"].append(deepcopy(row))
        self.assert_unavailable(graph.lookup())

    def test_invalid_timestamp_does_not_escape_as_exception(self):
        for timestamp in ("not-time", "2026-09-01T10:00:00"):
            with self.subTest(timestamp=timestamp):
                graph = AuthoredGraph()
                graph.add("edge", known_at=timestamp)
                self.assert_unavailable(graph.lookup())

    def test_missing_case_or_compilation_is_unavailable(self):
        graph = AuthoredGraph()
        for case, compiled in ((None, graph.compiled), (graph.case, None)):
            with self.subTest(case=case, compiled=compiled):
                self.assert_unavailable(direct.lookup(case, compiled, query()))

    def test_mismatched_case_binding_or_failed_compilation_is_unavailable(self):
        for key, value in (("case_id", "different-case"), ("state", "unavailable")):
            with self.subTest(key=key):
                graph = AuthoredGraph()
                graph.compiled[key] = value
                self.assert_unavailable(graph.lookup())

    def test_formal_scope_and_invalid_cutoff_are_unavailable(self):
        for key, value in (("scope", "formal_implication"), ("as_of", "not-time")):
            with self.subTest(key=key):
                q = query()
                q[key] = value
                self.assert_unavailable(AuthoredGraph().lookup(q))

    def test_inputs_and_returned_nested_path_data_are_not_aliased(self):
        graph = AuthoredGraph()
        graph.add("edge")
        q, policy = query(), direct.load_policy()
        before = deepcopy((graph.case, graph.compiled, q, policy))
        result = graph.lookup(q, policy)
        self.assertEqual((graph.case, graph.compiled, q, policy), before)
        result["paths"][0]["assertions"][0]["evidence"][0]["quote"] = "changed"
        self.assertEqual((graph.case, graph.compiled, q, policy), before)

    def test_assertion_and_event_order_do_not_change_result(self):
        graph = AuthoredGraph()
        graph.add("old")
        graph.add("middle", "a", "d", known_at=T2)
        graph.add("new", "a", "e", known_at=T3)
        graph.replace("old", "middle")
        graph.replace("middle", "new")
        baseline = graph.lookup()
        graph.compiled["source_assertions"].reverse()
        graph.compiled["status_events"].reverse()
        graph.case["turns"].reverse()
        self.assertEqual(graph.lookup(), baseline)


if __name__ == "__main__":
    unittest.main()
