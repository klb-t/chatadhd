"""Independent authored mechanism cases for bounded formal implication paths.

These examples measure neither extraction quality nor truth of source content.
They read no development, validation, or holdout experiment packets.
"""
from copy import deepcopy
from itertools import combinations, permutations
import unittest

try:
    from .graph_formal_paths import solve_paths
except ImportError:
    from graph_formal_paths import solve_paths


T1 = "2026-09-01T10:00:00Z"
T2 = "2026-09-02T10:00:00Z"
T3 = "2026-09-03T10:00:00Z"
T4 = "2026-09-04T10:00:00Z"


def assertion(ident, source, target, *, speaker="author", known_at=T1,
              polarity="positive", relation="implies"):
    return {
        "id": ident, "relation": relation, "source": source, "target": target,
        "polarity": polarity, "attributed_to": speaker, "known_at": known_at,
        "content_truth": "unverified", "basis_class": "observed_source_assertion",
        "evidence": [{"source_id": "authored-mechanism-source", "turn_id": ident,
                      "quote": f"{source} implies {target}",
                      "locator": {"synthetic": True, "notes": [ident]}}],
    }


def query(source="a", target="c", *, speaker="author", as_of=T4):
    return {"id": "authored-query", "relation": "implies", "source": source,
            "target": target, "attributed_to": speaker, "as_of": as_of,
            "scope": "formal_implication"}


def supersession(old, new, known_at=T2):
    return {"assertion_id": old, "status": "superseded", "superseded_by": new,
            "known_at": known_at,
            "evidence": [{"source_id": "authored-mechanism-source", "turn_id": new,
                          "quote": f"Replace {old} with {new}."}]}


def support_sets(result):
    return {tuple(path["premise_assertion_ids"]) for path in result["paths"]}


class FormalPathMechanismTests(unittest.TestCase):
    def solve(self, assertions=(), events=(), q=None, policy=None):
        return solve_paths(list(assertions), list(events), q or query(), policy)

    def policy(self, **changes):
        policy = deepcopy(self.solve()["policy"])
        policy.update(changes)
        return policy

    def assert_unknown(self, result):
        self.assertEqual(result["state"], "completed")
        self.assertTrue(result["complete"])
        self.assertEqual(result["label"], "unknown")
        self.assertEqual(result["paths"], [])
        self.assertEqual(result["basis_class"], "none")
        self.assertEqual(result["content_truth"], "unverified")
        self.assertIsNone(result["known_at"])

    def assert_unavailable(self, result):
        self.assertEqual(result["state"], "unavailable")
        self.assertFalse(result["complete"])
        self.assertIsNone(result["label"])
        self.assertEqual(result["paths"], [])
        self.assertEqual(result["content_truth"], "unverified")
        self.assertTrue(result["reason"])

    def test_transitivity_preserves_order_provenance_and_latest_premise_time(self):
        rows = [assertion("bc", "b", "c", known_at=T2), assertion("ab", "a", "b")]
        result = self.solve(rows)
        self.assertEqual(result["state"], "completed")
        self.assertTrue(result["complete"])
        self.assertEqual(result["label"], "supported")
        self.assertEqual(result["basis_class"], "inferred")
        self.assertEqual(result["content_truth"], "unverified")
        self.assertEqual(result["known_at"], T2)
        self.assertEqual(result["query_id"], "authored-query")
        self.assertEqual(result["premise_mode"], "authored_mechanism")
        path, = result["paths"]
        self.assertEqual(path["premise_assertion_ids"], ["ab", "bc"])
        self.assertEqual(path["node_ids"], ["a", "b", "c"])
        self.assertEqual(path["known_at"], T2)
        self.assertEqual(path["basis_class"], "inferred")
        self.assertEqual(path["content_truth"], "unverified")
        self.assertEqual(path["provenance"], [
            {"assertion_id": row["id"], "attributed_to": row["attributed_to"],
             "known_at": row["known_at"], "evidence": row["evidence"]}
            for row in reversed(rows)])

    def test_direct_and_longer_independent_supports_are_all_retained(self):
        result = self.solve([assertion("ac", "a", "c"), assertion("ab", "a", "b"),
                             assertion("bc", "b", "c")])
        self.assertEqual(support_sets(result), {("ac",), ("ab", "bc")})

    def test_diamond_and_parallel_assertions_have_distinct_premise_supports(self):
        rows = [assertion("ab-1", "a", "b"), assertion("ab-2", "a", "b"),
                assertion("bc", "b", "c"), assertion("ad", "a", "d"),
                assertion("dc", "d", "c")]
        self.assertEqual(support_sets(self.solve(rows)),
                         {("ab-1", "bc"), ("ab-2", "bc"), ("ad", "dc")})

    def test_earliest_complete_support_time_is_not_latest_alternative_time(self):
        rows = [assertion("ac", "a", "c", known_at=T3),
                assertion("ab", "a", "b"), assertion("bc", "b", "c", known_at=T2)]
        result = self.solve(rows)
        self.assertEqual(result["known_at"], T2)
        self.assertEqual({p["known_at"] for p in result["paths"]}, {T2, T3})

    def test_no_path_is_unknown_and_never_a_content_refutation(self):
        self.assert_unknown(self.solve())
        self.assert_unknown(self.solve([assertion("ab", "a", "b")]))
        self.assert_unknown(self.solve([assertion("ca", "c", "a")]))

    def test_source_equals_target_does_not_invent_reflexivity(self):
        for rows in ([], [assertion("aa", "a", "a")],
                     [assertion("ab", "a", "b"), assertion("ba", "b", "a")]):
            with self.subTest(rows=rows):
                self.assert_unknown(self.solve(rows, q=query("a", "a")))

    def test_cycles_do_not_create_nonminimal_or_repeated_node_proofs(self):
        rows = [assertion("ab", "a", "b"), assertion("ba", "b", "a"),
                assertion("bc", "b", "c"), assertion("bb", "b", "b")]
        result = self.solve(rows)
        self.assertEqual(support_sets(result), {("ab", "bc")})
        for path in result["paths"]:
            self.assertEqual(len(path["node_ids"]), len(set(path["node_ids"])))

    def test_speaker_boundary_prevents_mixed_attribution_proof(self):
        rows = [assertion("ab", "a", "b"), assertion("bc", "b", "c", speaker="other")]
        self.assert_unknown(self.solve(rows))
        self.assert_unknown(self.solve(rows, q=query(speaker="other")))
        rows.append(assertion("bc-author", "b", "c"))
        self.assertEqual(support_sets(self.solve(rows)), {("ab", "bc-author")})

    def test_other_relation_types_do_not_supply_implication_premises(self):
        for relation in ("causes", "supports", "precedes", "contradicts"):
            with self.subTest(relation=relation):
                rows = [assertion("ab", "a", "b", relation=relation),
                        assertion("bc", "b", "c")]
                self.assert_unknown(self.solve(rows))

    def test_future_assertions_are_excluded_but_cutoff_is_inclusive(self):
        rows = [assertion("ab", "a", "b"), assertion("bc", "b", "c", known_at=T2)]
        self.assert_unknown(self.solve(rows, q=query(as_of=T1)))
        self.assertEqual(support_sets(self.solve(rows, q=query(as_of=T2))), {("ab", "bc")})

    def test_timestamp_offsets_are_compared_as_instants(self):
        at_cutoff = "2026-09-02T12:00:00+02:00"
        after_cutoff = "2026-09-02T05:00:01-05:00"
        rows = [assertion("ab", "a", "b", known_at=at_cutoff),
                assertion("bc", "b", "c", known_at=T1)]
        result = self.solve(rows, q=query(as_of=T2))
        self.assertEqual(support_sets(result), {("ab", "bc")})
        self.assertEqual(result["known_at"], at_cutoff)
        rows[0]["known_at"] = after_cutoff
        self.assert_unknown(self.solve(rows, q=query(as_of=T2)))

    def test_relation_negation_is_not_a_positive_edge_or_complement_node(self):
        rows = [assertion("ab-negative", "a", "b", polarity="negative"),
                assertion("bc", "b", "c")]
        self.assert_unknown(self.solve(rows))
        self.assert_unknown(self.solve(rows, q=query("not:b", "not:a")))

    def test_no_contraposition_and_negated_node_names_remain_opaque(self):
        self.assert_unknown(self.solve([assertion("ab", "a", "b")],
                                      q=query("not:b", "not:a")))
        rows = [assertion("opaque-1", "not:a", "b"), assertion("opaque-2", "b", "not:c")]
        result = self.solve(rows, q=query("not:a", "not:c"))
        self.assertEqual(support_sets(result), {("opaque-1", "opaque-2")})
        self.assert_unknown(self.solve(rows, q=query("a", "c")))

    def test_negative_counterassertion_does_not_implicitly_veto_positive_path(self):
        rows = [assertion("ac-positive", "a", "c"),
                assertion("ac-negative", "a", "c", polarity="negative", known_at=T2)]
        result = self.solve(rows)
        self.assertEqual(result["label"], "supported")
        self.assertEqual(support_sets(result), {("ac-positive",)})
        self.assertEqual(result["content_truth"], "unverified")

    def test_later_assertion_does_not_supersede_without_explicit_event(self):
        rows = [assertion("old", "a", "c"), assertion("new", "a", "d", known_at=T2)]
        self.assertEqual(support_sets(self.solve(rows)), {("old",)})

    def test_supersession_applies_at_event_time_and_preserves_historical_support(self):
        rows = [assertion("old", "a", "c"), assertion("new", "a", "d", known_at=T2)]
        event = supersession("old", "new")
        self.assertEqual(support_sets(self.solve(rows, [event], query(as_of=T1))), {("old",)})
        self.assert_unknown(self.solve(rows, [event], query(as_of=T2)))
        self.assertEqual(support_sets(self.solve(rows, [event], query("a", "d", as_of=T2))),
                         {("new",)})

    def test_supersession_chain_is_temporal_and_independent_of_event_order(self):
        rows = [assertion("old", "a", "c"), assertion("middle", "a", "d", known_at=T2),
                assertion("new", "a", "e", known_at=T3)]
        events = [supersession("old", "middle"), supersession("middle", "new", T3)]
        result = self.solve(rows, events, query("a", "d", as_of=T2))
        self.assertEqual(support_sets(result), {("middle",)})
        self.assertEqual(result, self.solve(reversed(rows), reversed(events),
                                            query("a", "d", as_of=T2)))
        self.assert_unknown(self.solve(rows, events, query("a", "d", as_of=T3)))
        self.assertEqual(support_sets(self.solve(rows, events, query("a", "e", as_of=T3))),
                         {("new",)})

    def test_inputs_and_nested_provenance_are_not_mutated_or_aliased(self):
        rows = [assertion("ac", "a", "c")]
        q, policy = query(), self.policy()
        before = deepcopy((rows, q, policy))
        result = self.solve(rows, q=q, policy=policy)
        self.assertEqual((rows, q, policy), before)
        result["paths"][0]["provenance"][0]["evidence"][0]["locator"]["notes"].append("changed")
        result["policy"]["max_paths"] = 1
        self.assertEqual((rows, q, policy), before)

    def test_declared_premise_modes_do_not_promote_content_truth(self):
        for mode in ("authored_mechanism", "oracle_annotation", "model_predicted_source_assertion"):
            with self.subTest(mode=mode):
                result = solve_paths([assertion("ac", "a", "c")], [], query(), premise_mode=mode)
                self.assertEqual(result["premise_mode"], mode)
                self.assertEqual(result["basis_class"], "inferred")
                self.assertEqual(result["content_truth"], "unverified")
                self.assertIsNone(result["world_truth_accuracy"])
                self.assertTrue(result["no_graph_promotion"])

    def test_input_order_does_not_change_paths_or_report(self):
        rows = [assertion("ab", "a", "b"), assertion("bc", "b", "c"),
                assertion("ac", "a", "c"), assertion("dead", "x", "y")]
        baseline = self.solve(rows)
        for order in (reversed(rows), [rows[i] for i in (2, 0, 3, 1)]):
            self.assertEqual(self.solve(order), baseline)

    def test_path_length_bound_is_unavailable_when_a_longer_proof_is_possible(self):
        rows = [assertion("ab", "a", "b"), assertion("bc", "b", "c")]
        self.assert_unavailable(self.solve(rows, policy=self.policy(max_path_length=1)))
        result = self.solve(rows, policy=self.policy(max_path_length=2))
        self.assertEqual(support_sets(result), {("ab", "bc")})
        self.assertTrue(result["complete"])

    def test_path_count_exhaustion_does_not_publish_partial_alternatives(self):
        rows = [assertion("ac", "a", "c"), assertion("ab", "a", "b"),
                assertion("bc", "b", "c")]
        self.assert_unavailable(self.solve(rows, policy=self.policy(max_paths=1)))
        result = self.solve(rows, policy=self.policy(max_paths=2))
        self.assertEqual(support_sets(result), {("ac",), ("ab", "bc")})
        self.assertTrue(result["complete"])

    def test_state_budget_exhaustion_is_not_completed_no_path(self):
        rows = [assertion("ab", "a", "b"), assertion("bc", "b", "c")]
        result = self.solve(rows, policy=self.policy(max_search_states=1))
        self.assert_unavailable(result)
        self.assertLessEqual(result["search_states"], 1)

    def test_invalid_assertion_identity_and_time_are_rejected(self):
        row = assertion("ac", "a", "c")
        with self.assertRaises(ValueError):
            self.solve([row, deepcopy(row)])
        for key, value in (("id", ""), ("source", ""), ("known_at", "not-a-time"),
                           ("known_at", "2026-09-01T10:00:00"), ("evidence", [])):
            with self.subTest(key=key, value=value):
                bad = deepcopy(row)
                bad[key] = value
                with self.assertRaises(ValueError):
                    self.solve([bad])

    def test_premises_cannot_claim_content_verification_or_inferred_observation(self):
        for key, value in (("content_truth", "verified"), ("basis_class", "inferred")):
            with self.subTest(key=key):
                row = assertion("ac", "a", "c")
                row[key] = value
                with self.assertRaises(ValueError):
                    self.solve([row])

    def test_invalid_query_relation_scope_and_cutoff_are_rejected(self):
        for key, value in (("scope", "content_truth"), ("relation", "contradicts"),
                           ("as_of", "not-a-time")):
            with self.subTest(key=key):
                bad = query()
                bad[key] = value
                with self.assertRaises(ValueError):
                    self.solve(q=bad)

    def test_invalid_supersession_references_and_timing_are_rejected(self):
        rows = [assertion("old", "a", "c"), assertion("new", "a", "d", known_at=T2)]
        for event in (supersession("missing", "new"), supersession("old", "missing"),
                      supersession("old", "new", T1), supersession("old", "new", T3)):
            with self.subTest(event=event):
                with self.assertRaises(ValueError):
                    self.solve(rows, [event])

    def test_other_speaker_cannot_supersede_requested_speaker_assertion(self):
        rows = [assertion("old", "a", "c"),
                assertion("new", "a", "d", speaker="other", known_at=T2)]
        with self.assertRaises(ValueError):
            self.solve(rows, [supersession("old", "new")])

    def test_malformed_reference_and_premise_mode_types_raise_value_errors(self):
        rows = [assertion("old", "a", "c"), assertion("new", "a", "d", known_at=T2)]
        for key in ("assertion_id", "superseded_by"):
            event = supersession("old", "new")
            event[key] = ["not-a-scalar-id"]
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    self.solve(rows, [event])
        with self.assertRaises(ValueError):
            solve_paths([], [], query(), premise_mode=["authored_mechanism"])

    def test_policy_changes_require_declared_supported_semantics(self):
        for key, value in (("temporal", "ignore_time"), ("attribution", "any_speaker"),
                           ("inference_rule", "contraposition"), ("min_edges", 0),
                           ("max_paths", 0), ("max_search_states", -1)):
            with self.subTest(key=key, value=value):
                with self.assertRaises(ValueError):
                    self.solve(policy=self.policy(**{key: value}))

    def test_explicit_large_path_cap_has_no_hidden_python_recursion_limit(self):
        # This is an authored mechanism stressor. The only formal support is a
        # chain longer than Python's customary call-stack depth; policy allows
        # it, so an undeclared RecursionError is not a valid result.
        edge_count = 1100
        rows = [assertion(f"edge-{i:04}", f"node-{i}", f"node-{i + 1}")
                for i in range(edge_count)]
        result = self.solve(rows, q=query("node-0", f"node-{edge_count}"),
                            policy=self.policy(max_path_length=edge_count))
        self.assertEqual(result["state"], "completed")
        self.assertEqual(result["label"], "supported")
        path, = result["paths"]
        self.assertEqual(len(path["premise_assertion_ids"]), edge_count)
        self.assertEqual(path["premise_assertion_ids"][0], "edge-0000")
        self.assertEqual(path["premise_assertion_ids"][-1], "edge-1099")

    def test_exhaustive_tiny_graphs_match_permutation_oracle(self):
        # An independent oracle lists intermediate-node permutations rather than
        # traversing adjacency. Exhaust all three-node directed graphs (cycles
        # included) and all four-node forward DAGs (branching alternatives).
        domains = [("abc", [(a, b) for a in "abc" for b in "abc" if a != b]),
                   ("abcd", list(combinations("abcd", 2)))]
        checked = 0
        for nodes, possible_edges in domains:
            source, target = nodes[0], nodes[-1]
            intermediates = nodes[1:-1]
            for mask in range(1 << len(possible_edges)):
                selected = {edge for i, edge in enumerate(possible_edges) if mask & (1 << i)}
                rows = [assertion(a + b, a, b) for a, b in sorted(selected)]
                expected = set()
                for length in range(len(intermediates) + 1):
                    for middle in permutations(intermediates, length):
                        path_nodes = (source, *middle, target)
                        edges = tuple(zip(path_nodes, path_nodes[1:]))
                        if set(edges) <= selected:
                            expected.add(tuple(a + b for a, b in edges))
                result = self.solve(rows, q=query(source, target))
                with self.subTest(nodes=nodes, mask=mask):
                    self.assertEqual(result["state"], "completed")
                    self.assertTrue(result["complete"])
                    self.assertEqual(support_sets(result), expected)
                    self.assertEqual(result["label"], "supported" if expected else "unknown")
                checked += 1
        self.assertEqual(checked, 128)


if __name__ == "__main__":
    unittest.main()
