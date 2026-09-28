"""Independent oracle and metric guards; no matcher implementation imported."""
from copy import deepcopy
import json
import unittest

import graph_native_eval as evaluator


def tiny(labels=("one", "two"), edges=1):
    return {"nodes": [{"id": "a", "kind": "claim", "role": "constraint", "label": labels[0]},
                      {"id": "b", "kind": "claim", "role": "constraint", "label": labels[1]}],
            "edges": [{"source": "a", "target": "b", "predicate": "supports"} for _ in range(edges)]}


class GraphNativeOracleTests(unittest.TestCase):
    def test_frozen_fixture_and_authored_labels_agree_with_exhaustive_oracle(self):
        fixture = evaluator.read_fixture()
        labels = evaluator.oracle_labels(fixture)
        self.assertEqual(len(labels["rows"]), 228)
        self.assertEqual(sum(r["present"] for r in labels["rows"]), 72)

    def test_direction_and_parallel_multiplicity_are_constraints(self):
        pattern = tiny(edges=2)
        host = tiny(edges=1)
        self.assertFalse(evaluator.exhaustive_oracle(pattern, host, goal="template_containment")["present"])
        host = tiny(edges=2)
        for edge in host["edges"]:
            edge["source"], edge["target"] = edge["target"], edge["source"]
        self.assertFalse(evaluator.exhaustive_oracle(pattern, host, goal="template_containment")["present"])

    def test_identity_is_not_noninduced_containment(self):
        pattern = tiny()
        host = deepcopy(pattern)
        host["nodes"].append({"id": "noise", "kind": "event", "role": "event", "label": "aside"})
        host["edges"].append({"source": "noise", "target": "a", "predicate": "mentions"})
        self.assertTrue(evaluator.exhaustive_oracle(pattern, host, goal="template_containment")["present"])
        self.assertFalse(evaluator.exhaustive_oracle(pattern, host, goal="semantic_identity")["present"])

    def test_binding_abstraction_cannot_collapse_distinct_source_identity(self):
        pattern, host = tiny(("original1", "original2")), tiny(("copy", "copy"))
        projection = {"pattern": {i: {"label": "claim"} for i in ("a", "b")},
                      "host": {i: {"label": "claim"} for i in ("a", "b")}}
        self.assertFalse(evaluator.exhaustive_oracle(pattern, host, goal="structural_analogy", projection=projection)["present"])
        host["nodes"][1]["label"] = "other"
        self.assertTrue(evaluator.exhaustive_oracle(pattern, host, goal="structural_analogy", projection=projection)["present"])

    def test_qualifier_polarity_scope_and_nested_values_are_preserved(self):
        pattern, host = tiny(), tiny()
        pattern["nodes"][0]["qualifiers"] = {"polarity": "negative", "scope": {"branch": "earlier"}}
        host["nodes"][0]["qualifiers"] = {"polarity": "negative", "scope": {"branch": "later"}}
        self.assertFalse(evaluator.exhaustive_oracle(pattern, host, goal="template_containment")["present"])

    def test_missing_lexical_label_is_distinct_from_empty_string(self):
        pattern, host = tiny(), tiny()
        pattern["nodes"][0].pop("label")
        host["nodes"][0]["label"] = ""
        self.assertFalse(evaluator.exhaustive_oracle(pattern, host, goal="template_containment")["present"])

    def test_unknown_semantic_fields_and_goals_are_rejected(self):
        for location in ("nodes", "edges"):
            pattern = tiny()
            pattern[location][0]["unmodeled_semantics"] = "negative"
            with self.assertRaisesRegex(ValueError, "unknown semantic"):
                evaluator.exhaustive_oracle(pattern, tiny(), goal="template_containment")
        with self.assertRaisesRegex(ValueError, "unknown evaluation goal"):
            evaluator.witness_valid(tiny(), tiny(), {"a": "a", "b": "b"}, goal="typo")

    def test_witness_needs_distinct_integer_edge_indexes(self):
        graph = tiny(edges=2)
        mapping = {"a": "a", "b": "b"}
        for edge_map in ([{"pattern_edge": 0, "host_edge": 0}, {"pattern_edge": 1, "host_edge": 0}],
                         [{"pattern_edge": False, "host_edge": False}, {"pattern_edge": 1, "host_edge": 1}]):
            self.assertFalse(evaluator.witness_valid(graph, graph, mapping, goal="template_containment", edge_mapping=edge_map))
        self.assertTrue(evaluator.witness_valid(graph, graph, mapping, goal="template_containment",
            edge_mapping=[{"pattern_edge": 0, "host_edge": 0}, {"pattern_edge": 1, "host_edge": 1}]))

    def test_ranking_accounts_for_filtered_positive_and_duplicate_ids(self):
        report = evaluator.ranking_metrics({"a", "b"}, ["noise", "a"], {"noise", "a"})
        self.assertEqual(report["filter_recall"], 0.5)
        self.assertEqual(report["average_precision"], 0.25)
        self.assertEqual(report["reciprocal_rank"], 0.5)
        self.assertEqual(report["top_k"]["3"]["recall"], 0.5)
        with self.assertRaisesRegex(ValueError, "duplicate ranked"):
            evaluator.ranking_metrics({"a"}, ["a", "a"], {"a"})

    def core_sample(self):
        folder = evaluator.DEFAULT_FIXTURE.parent
        source = json.loads((folder / "core_claim_cases.json").read_text())["cases"][0]["export"]
        measured = json.loads((folder / "initial_core_projection_report.json").read_text())["case_results"][0]
        projection = {"mode": "semantic", "structure": deepcopy(measured["graph"]), "source": deepcopy(source)}
        return source, projection, source["claims"][0]

    def test_copied_source_is_not_projected_semantic_retention(self):
        source, projection, claim = self.core_sample()
        self.assertTrue(evaluator.projected_claim_audit(source, projection, claim)["restriction_graph_retained"])
        projection["structure"] = {"nodes": [], "edges": []}
        self.assertEqual(source, projection["source"])
        self.assertFalse(evaluator.projected_claim_audit(source, projection, claim)["restriction_graph_retained"])

    def test_support_identity_requires_exact_incidence_not_equal_counts(self):
        source, projection, claim = self.core_sample()
        for node in projection["structure"]["nodes"]:
            if node.get("lexical_identity", {}).get("namespace") in {"observation", "source"}:
                node["lexical_identity"]["value"] = "wrong-but-still-unique"
        audit = evaluator.projected_claim_audit(source, projection, claim)
        self.assertEqual(audit["support_counts"], audit["expected_support_counts"])
        self.assertFalse(audit["support_identity_retained"])

    def test_scope_identity_needs_scope_entity_membership(self):
        source, projection, claim = self.core_sample()
        projection["structure"]["edges"] = [e for e in projection["structure"]["edges"] if e["predicate"] != "scope_refers_to"]
        self.assertFalse(evaluator.projected_claim_audit(source, projection, claim)["restriction_graph_retained"])


if __name__ == "__main__":
    unittest.main()
