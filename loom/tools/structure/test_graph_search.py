"""Author-owned graph matching checks; no independent fixture or core writes."""
from copy import deepcopy
import unittest

from graph_search import (comparison_graph, match_subgraph, rank_candidates,
                          topology_only_control)


def node(ident, kind="item", role="part", label=None, **qualifiers):
    result = {"id": ident, "kind": kind, "role": role, "qualifiers": qualifiers, "claim_ids": []}
    if label is not None:
        result["label"] = label
    return result


def edge(src, dst, predicate="relates", **qualifiers):
    return {"source": src, "target": dst, "predicate": predicate, "qualifiers": qualifiers, "claim_ids": []}


def graph(nodes, edges=()):
    return {"nodes": nodes, "edges": list(edges)}


def abstract_labels(pattern, host):
    return {side: {n["id"]: {"label": n["kind"]} for n in g["nodes"] if "label" in n}
            for side, g in (("pattern", pattern), ("host", host))}


class GraphSearchTests(unittest.TestCase):
    def test_pattern_inside_larger_graph_and_noninduced_extra_edges(self):
        pattern = graph([node("p", "actor"), node("q", "artifact")], [edge("p", "q", "creates")])
        host = graph([node("a", "actor"), node("b", "artifact"), node("d", "event")],
                     [edge("a", "b", "creates"), edge("b", "a", "feedback"), edge("a", "d", "triggers")])
        result = match_subgraph(pattern, host)
        self.assertTrue(result["matched"])
        self.assertEqual(result["node_mapping"], {"p": "a", "q": "b"})
        self.assertEqual(result["edge_mapping"], [{"pattern_edge": 0, "host_edge": 0}])

    def test_direction_and_predicate_remain_semantic(self):
        p = graph([node("a", "actor"), node("b", "artifact")], [edge("a", "b", "creates")])
        reverse = graph(deepcopy(p["nodes"]), [edge("b", "a", "creates")])
        different = graph(deepcopy(p["nodes"]), [edge("a", "b", "destroys")])
        self.assertFalse(match_subgraph(p, reverse)["matched"])
        self.assertFalse(match_subgraph(p, different)["matched"])

    def test_injective_mapping_rejects_collapsed_vertices(self):
        p = graph([node("a"), node("b")], [edge("a", "b")])
        h = graph([node("x")], [edge("x", "x")])
        self.assertEqual(match_subgraph(p, h)["status"], "different")

    def test_parallel_edges_require_distinct_host_edges(self):
        p = graph([node("a"), node("b")], [edge("a", "b"), edge("a", "b")])
        one = graph([node("x"), node("y")], [edge("x", "y")])
        three = graph([node("x"), node("y")], [edge("x", "y"), edge("x", "y"), edge("x", "y")])
        self.assertFalse(match_subgraph(p, one)["matched"])
        result = match_subgraph(p, three)
        self.assertTrue(result["matched"])
        self.assertEqual(len({e["host_edge"] for e in result["edge_mapping"]}), 2)

    def test_self_loop_cannot_be_satisfied_by_neighbors(self):
        p = graph([node("a")], [edge("a", "a")])
        h = graph([node("x"), node("y")], [edge("x", "y"), edge("y", "x")])
        self.assertFalse(match_subgraph(p, h)["matched"])

    def test_budget_exhaustion_is_unknown(self):
        p = graph([node("a"), node("b")], [edge("a", "b")])
        for budget in (0, 1):
            result = match_subgraph(p, p, state_budget=budget)
            self.assertEqual(result["status"], "budget_exhausted")
            self.assertIsNone(result["matched"])
            self.assertLessEqual(result["states_explored"], budget)
        impossible = graph([node("x", "different")])
        self.assertEqual(match_subgraph(p, impossible, state_budget=0)["status"], "different")

    def test_empty_pattern_is_unrepresented_not_retrieval_success(self):
        self.assertEqual(match_subgraph(graph([]), graph([node("a")]))["status"], "unrepresented")

    def test_explicit_analogy_projection_keeps_type_and_loss_report(self):
        p = graph([node("a", label="pump"), node("b", label="fluid")], [edge("a", "b")])
        h = graph([node("x", label="publisher"), node("y", label="message")], [edge("x", "y")])
        self.assertFalse(match_subgraph(p, h)["matched"])
        result = match_subgraph(p, h, goal="structural_analogy", label_projection=abstract_labels(p, h))
        self.assertTrue(result["matched"])
        self.assertEqual(len(result["projection_report"]["pattern"]["changes"]), 2)
        self.assertTrue(result["lexical_renaming"])
        h["nodes"][0]["kind"] = "different_type"
        self.assertFalse(match_subgraph(p, h, goal="structural_analogy", label_projection=abstract_labels(p, h))["matched"])

    def test_repeated_lexical_names_cannot_split_under_erasure(self):
        p = graph([node("a", label="same"), node("b", label="same")], [edge("a", "b")])
        h = graph([node("x", label="first"), node("y", label="second")], [edge("x", "y")])
        result = match_subgraph(p, h, goal="structural_analogy", label_projection=abstract_labels(p, h))
        self.assertFalse(result["matched"])

    def test_distinct_lexical_names_cannot_collapse_under_erasure(self):
        p = graph([node("a", label="first"), node("b", label="second")], [edge("a", "b")])
        h = graph([node("x", label="same"), node("y", label="same")], [edge("x", "y")])
        self.assertFalse(match_subgraph(p, h, goal="structural_analogy", label_projection=abstract_labels(p, h))["matched"])

    def test_symbol_field_equality_is_preserved_globally(self):
        p = graph([node("a", symbol="A"), node("b", symbol="A")], [edge("a", "b")])
        h = graph([node("x", symbol="X"), node("y", symbol="Y")], [edge("x", "y")])
        projection = {"pattern": {n["id"]: {"symbol": "term"} for n in p["nodes"]},
                      "host": {n["id"]: {"symbol": "term"} for n in h["nodes"]}}
        self.assertFalse(match_subgraph(p, h, goal="structural_analogy", label_projection=projection)["matched"])

    def test_scope_identity_nodes_can_rename_without_erasing_binding(self):
        p = graph([node("a"), node("s", "scope", "")], [edge("a", "s", "scoped_in")])
        h = graph([node("x"), node("t", "scope", "")], [edge("x", "t", "scoped_in")])
        p["nodes"][1]["lexical_identity"] = {"namespace": "scope", "value": "conversation_a"}
        h["nodes"][1]["lexical_identity"] = {"namespace": "scope", "value": "conversation_b"}
        projection = {"pattern": {"s": {"lexical_identity": "scope"}}, "host": {"t": {"lexical_identity": "scope"}}}
        self.assertFalse(match_subgraph(p, h)["matched"])
        self.assertTrue(match_subgraph(p, h, goal="structural_analogy", label_projection=projection)["matched"])
        h["edges"][0]["predicate"] = "outside_scope"
        self.assertFalse(match_subgraph(p, h, goal="structural_analogy", label_projection=projection)["matched"])

    def test_qualifier_scope_polarity_modality_and_nested_values_are_exact(self):
        p = graph([node("a", label="old", scope="s", polarity="positive", modality="asserted", detail={"quantifier": "all"})])
        for key, value in [("scope", "other"), ("polarity", "negative"), ("modality", "possible"), ("detail", {"quantifier": "some"})]:
            h = deepcopy(p)
            h["nodes"][0]["id"] = "b"
            h["nodes"][0]["label"] = "new"
            h["nodes"][0]["qualifiers"][key] = value
            self.assertFalse(match_subgraph(p, h, goal="structural_analogy", label_projection=abstract_labels(p, h))["matched"])

    def test_unrepresented_fields_and_illegal_projection_reject(self):
        p = graph([node("a")])
        bad = deepcopy(p)
        bad["nodes"][0]["polarity"] = "negative"
        with self.assertRaises(ValueError):
            match_subgraph(p, bad)
        with self.assertRaises(ValueError):
            match_subgraph(p, p, goal="structural_analogy", label_projection={"pattern": {"a": {"scope": "any"}}, "host": {}})
        with self.assertRaises(ValueError):
            match_subgraph(p, p, label_projection={"pattern": {}, "host": {}})

    def test_safe_filters_keep_true_match_with_changed_wl_neighborhood(self):
        p = graph([node("a"), node("b")], [edge("a", "b")])
        h = graph([node("x"), node("y"), node("z")],
                  [edge("x", "y"), edge("x", "z"), edge("y", "z"), edge("z", "x")])
        ranked = rank_candidates(p, [{"id": "distractors", "graph": h}], verify_budget=100)
        self.assertEqual(ranked["filter_survivors"], 1)
        self.assertTrue(ranked["results"][0]["verification"]["matched"])
        self.assertFalse(ranked["results"][0]["filters"]["wl_used_as_hard_filter"])
        self.assertLess(ranked["results"][0]["scores"]["wl_cosine"], 1.0)

    def test_ranking_reports_filter_and_top_k_omissions(self):
        p = graph([node("a")])
        ranked = rank_candidates(p, [{"id": "z", "graph": p}, {"id": "a", "graph": p},
                                     {"id": "bad", "graph": graph([node("b", "other")])}], limit=1)
        self.assertEqual(ranked["results"][0]["id"], "a")
        self.assertEqual(ranked["omitted_by_top_k"], 1)
        self.assertEqual(len(ranked["filtered_out"]), 1)

    def test_topology_control_is_explicitly_unsafe(self):
        p = graph([node("a", polarity="positive")])
        h = graph([node("b", polarity="negative")])
        self.assertFalse(match_subgraph(p, h)["matched"])
        result = topology_only_control(p, h)
        self.assertTrue(result["matched"])
        self.assertFalse(result["semantic_use_permitted"])
        self.assertIn("unsafe", result["goal"])

    def test_metadata_ids_order_do_not_change_match_and_inputs_remain_unchanged(self):
        p = graph([node("a", "actor"), node("b", "artifact")], [edge("a", "b")])
        h = graph([node("x", "actor"), node("y", "artifact")], [edge("x", "y")])
        h["nodes"].reverse()
        h["nodes"][0]["claim_ids"] = ["different-source"]
        h["nodes"][0]["provenance"] = {"scope": "source_metadata_not_semantic_scope"}
        before = deepcopy((p, h))
        self.assertTrue(match_subgraph(p, h)["matched"])
        self.assertEqual((p, h), before)

    def test_absent_and_empty_label_are_different(self):
        p = graph([node("a")])
        h = graph([node("b", label="")])
        self.assertFalse(match_subgraph(p, h)["matched"])
        self.assertNotEqual(comparison_graph(p)["graph"]["nodes"][0]["qualifiers"],
                            comparison_graph(h)["graph"]["nodes"][0]["qualifiers"])


if __name__ == "__main__":
    unittest.main()
