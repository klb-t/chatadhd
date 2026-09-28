"""Author examples for explicit assertion projection, not validation fixtures."""
from copy import deepcopy
import unittest

try:
    from .assertion_view import project_assertions, restore_core_graph, assessment_dimensions
    from .core_projection import project_core
    from .graph_search import comparison_graph
    from .structure_methods import align
    from .test_core_projection import example, claim
except ImportError:
    from assertion_view import project_assertions, restore_core_graph, assessment_dimensions
    from core_projection import project_core
    from graph_search import comparison_graph
    from structure_methods import align
    from test_core_projection import example, claim


def view(source, **options):
    return project_assertions(project_core(source, mode="structural"), **options)


class AssertionViewTests(unittest.TestCase):
    def test_provenance_closure_is_reversible_and_not_in_shape(self):
        source = example()
        source["entities"][0]["attrs"] = {"units": ["unit-other"], "observations": ["ob-other"], "method": "author"}
        original = deepcopy(source)
        core = project_core(source, mode="structural")
        result = project_assertions(core)
        self.assertEqual(restore_core_graph(result), core["structure"])
        self.assertEqual(result["source"], source)
        self.assertEqual(source, original)
        self.assertFalse(any(node["kind"] in {"core_support", "core_observation", "core_unit", "core_source"} for node in result["structure"]["nodes"]))
        self.assertTrue(result["sidecar"]["removed_edges"])
        self.assertEqual({item["edge_id"] for item in result["loss_map"] if "edge_id" in item}, {edge["id"] for edge in result["sidecar"]["removed_edges"]})
        comparison_graph(result["structure"], goal="literal_semantic")

    def test_assessment_changes_report_dimensions_but_not_assertion_shape(self):
        a = example()
        b = deepcopy(a)
        b["claims"][0]["assessment"].update(status="contested", confidence=.2)
        left, right = view(a), view(b)
        self.assertEqual(align(left["structure"], right["structure"])["status"], "isomorphic")
        report = assessment_dimensions(left, right, {left["claim_node_ids"]["acl"]: right["claim_node_ids"]["acl"]})
        self.assertFalse(report["mapped_claim_assessments"][0]["dimensions"]["status"]["equal"])
        self.assertFalse(report["mapped_claim_assessments"][0]["dimensions"]["confidence"]["equal"])
        self.assertIsNone(report["aggregate_score"])
        self.assertEqual(right["source_assessments"]["acl"]["status"], "contested")
        self.assertFalse(right["inference_eligible"])
        self.assertFalse(right["persistable_claim"])

    def test_qualifier_negation_quantifier_branch_and_validity_remain_hard(self):
        source = example()
        for field, value in (("extra", {"negated": True}), ("extra", {"quantifier": "some"}),
                             ("branch", "counterfactual"), ("valid_from", "2026-01-01")):
            with self.subTest(field=field, value=value):
                changed = deepcopy(source)
                changed["claims"][0]["qualifiers"][field] = value
                self.assertEqual(align(view(source)["structure"], view(changed)["structure"])["status"], "different")

    def test_entity_kind_and_shared_identity_are_not_erased(self):
        source = example()
        source["claims"].append(claim("other", "a2", "supplies", "a1", "aob", "ascope"))
        changed = deepcopy(source)
        changed["claims"][1]["object"] = "a2"
        self.assertEqual(align(view(source)["structure"], view(changed)["structure"])["status"], "different")
        changed = deepcopy(source)
        changed["entities"][0]["kind"] = "person"
        self.assertEqual(align(view(source)["structure"], view(changed)["structure"])["status"], "different")
        self.assertTrue(any(node.get("lexical_identity") == {"namespace": "entity", "value": "a1"} for node in view(source)["structure"]["nodes"]))

    def test_unselected_and_missing_claim_references_never_become_selected_facts(self):
        source = example()
        source["claims"].append(claim("other", "a2", "supplies", "a1", "aob", "ascope"))
        source["claims"][0]["assessment"]["premises"]["claims"] = ["other", "missing"]
        core = project_core(source, mode="structural", claim_ids=["acl"])
        result = project_assertions(core)
        self.assertEqual(sum(node["kind"] == "core_claim" for node in result["structure"]["nodes"]), 1)
        self.assertEqual(sum(node["kind"] == "core_claim_reference" for node in result["structure"]["nodes"]), 2)
        self.assertEqual(result["counts"]["external_claim_reference_ports"], 2)
        self.assertTrue(all(not item["interpreted_as_fact"] for item in result["reference_ports"]))
        self.assertEqual(restore_core_graph(result), core["structure"])
        self.assertTrue(any(item["reason"] == "referenced_record_missing" and item["id"] == "missing" for item in result["unknowns"]))

    def test_premise_counter_consequence_derivation_and_slot_roles_preserved(self):
        source = example()
        assessment = source["claims"][0]["assessment"]
        assessment["premises"]["claims"] = ["p"]
        assessment["counter"]["claims"] = ["c"]
        assessment["counter"]["observations"] = ["aob"]
        assessment["consequences"]["claims"] = ["r"]
        assessment["basis"]["derivation"] = {"operator": "operator", "operator_version": 1, "morphism": "mapping", "depth": 1}
        source["slot_values"] = [{"instance": "i", "slot": "condition", "ord": 0, "claim": "acl", "role": "constraint", "conflict": False}]
        result = view(source)
        self.assertTrue({"premises:claims", "counter:claims", "counter:observations", "consequences:claims", "derivation", "operator", "morphism", "slot_assignment"} <= {edge["predicate"] for edge in result["structure"]["edges"]})
        self.assertTrue(any(node.get("role") == "constraint" for node in result["structure"]["nodes"]))
        observed = next(node for node in result["structure"]["nodes"] if node["kind"] == "core_observation")
        self.assertTrue(observed["qualifiers"]["reference_only"])
        self.assertNotIn("attributes", observed["qualifiers"])

    def test_literal_ablation_is_explicit_unsafe_and_reversible(self):
        source = example()
        source["claims"][0].update(object="", value="Do not unlock")
        changed = deepcopy(source)
        changed["claims"][0]["value"] = "Unlock"
        self.assertEqual(align(view(source)["structure"], view(changed)["structure"])["status"], "different")
        a, b = view(source, literal_policy="literal_type_control"), view(changed, literal_policy="literal_type_control")
        self.assertEqual(align(a["structure"], b["structure"])["status"], "isomorphic")
        self.assertFalse(a["comparison_admissible"])
        self.assertFalse(a["analogy_or_entailment_established"])
        self.assertEqual(restore_core_graph(a), project_core(source, mode="structural")["structure"])
        self.assertTrue(any(item["action"] == "unsafe_type_only_ablation" for item in a["loss_map"]))

    def test_source_tampering_and_topology_input_rejected(self):
        core = project_core(example(), mode="structural")
        core["source"]["claims"][0]["value"] = "tampered"
        with self.assertRaises(ValueError):
            project_assertions(core)
        with self.assertRaises(ValueError):
            project_assertions(project_core(example(), mode="topology_control"))


if __name__ == "__main__":
    unittest.main()
