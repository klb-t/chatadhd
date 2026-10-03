"""Author-owned core-to-retrieval integration; no independent fixture access."""
from copy import deepcopy
import unittest

from graph_flow import run_flow
from test_core_projection import example


def source_and_specs():
    left, right = example("a"), example("b")
    source = {"run_id": "one-run"}
    for collection in ("entities", "observations", "claims"):
        source[collection] = left[collection] + right[collection]
    specs = [{"id": prefix, "claim_ids": [prefix + "cl"], "source_group": "same-import"} for prefix in ("a", "b")]
    return source, specs


class GraphFlowTests(unittest.TestCase):
    def test_core_objects_to_analogy_witness_and_source_refs(self):
        source, specs = source_and_specs()
        original = deepcopy(source)
        output = run_flow(source, specs, ["a"])
        result = output["retrieval"][0]["result"]["results"][0]
        self.assertEqual(result["id"], "b")
        self.assertEqual(result["verification"]["status"], "matched")
        self.assertTrue(result["verification"]["node_mapping"])
        self.assertTrue(result["verification"]["lexical_renaming"])
        for record in output["projections"].values():
            self.assertTrue(record["reference_map"])
            self.assertFalse(record["scope_verified"])
        self.assertEqual(source, original)
        self.assertEqual(output["semantics"]["claims_promoted"], 0)

    def test_polarity_difference_never_hidden_by_analogy(self):
        source, specs = source_and_specs()
        source["claims"][1]["qualifiers"]["extra"]["negated"] = True
        result = run_flow(source, specs, ["a"])["retrieval"][0]["result"]
        self.assertEqual(result["filter_survivors"], 0)
        self.assertEqual(result["filtered_out"][0]["id"], "b")

    def test_budgets_are_omissions_not_false_nonmatches(self):
        source, specs = source_and_specs()
        output = run_flow(source, specs, ["a", "b"], max_records=1)
        self.assertEqual(output["retrieval"][1]["status"], "not_run_budget")
        self.assertEqual(output["omissions"], [{"id": "b", "reason": "record_budget"}])
        output = run_flow(source, specs, ["a"], max_graph_nodes=1)
        self.assertEqual(output["coverage"]["searched_records"], 0)
        self.assertTrue(all(x["reason"] == "graph_node_budget" for x in output["omissions"]))
        output = run_flow(source, specs, ["a"], state_budget=1)
        self.assertEqual(output["retrieval"][0]["result"]["results"][0]["verification"]["status"], "budget_exhausted")

    def test_optional_motifs_preserve_declared_source_groups(self):
        source, specs = source_and_specs()
        output = run_flow(source, specs, [], motif_parameters={"max_enumerations": 64})
        self.assertIsNotNone(output["motifs"])
        for candidate in output["motifs"]["candidates"]:
            self.assertLessEqual(candidate["support"]["independent_support"], 1)
        self.assertFalse(output["semantics"]["match_is_inference"])


if __name__ == "__main__":
    unittest.main()
