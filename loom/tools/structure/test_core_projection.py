"""Author-owned canonical core object examples, not parser ASTs or eval fixtures."""
from copy import deepcopy
import unittest

try:
    from .core_projection import project_core
    from .structure_methods import align
except ImportError:
    from core_projection import project_core
    from structure_methods import align


def entity(ident, label, kind="component"):
    return {"id": ident, "kind": kind, "canonical_key": label.casefold(), "label": label,
            "labels": {}, "aliases": [], "parent": "", "first_seen": "", "last_seen": "",
            "evidence_class": "observed", "origin": "archive", "confidence": .8, "status": "active", "attrs": {}}


def claim(ident, subject, predicate, obj, observation, scope="scope-default"):
    return {"id": ident, "subject": subject, "predicate": predicate, "object": obj, "value": None,
            "qualifiers": {"valid_from": "", "valid_to": "", "version": "", "branch": "", "scope": scope, "lang": "en", "extra": {}},
            "assessment": {"basis": {"support": [{"observation": observation, "locator": {"source": "source-one", "member": "text.txt", "byte_start": 0, "byte_len": 1},
                                                  "quote": "Source statement.", "extractor": "author-core-example", "quality": .8}], "derivation": None},
                           "evidence_class": "observed", "origin": "archive", "confidence": .8,
                           "premises": {"claims": [], "principles": [], "assumptions": []},
                           "counter": {"observations": [], "claims": []}, "status": "active",
                           "consequences": {"claims": [], "predictions": [], "checks": []},
                           "open": {"slots": [], "questions": [], "fill_query": None},
                           "expected_property": None, "check_state": "n/a", "alternatives": []}}


def example(prefix="a"):
    entities = [entity(prefix + "1", "Pump"), entity(prefix + "2", "Power")]
    observation = {"id": prefix + "ob", "unit": prefix + "unit", "kind": "utterance", "text": "Source statement.",
                   "locator": {"source": "source-one", "member": "text.txt", "byte_start": 0, "byte_len": 17},
                   "lang": "en", "date": "", "ordinal": 0, "artifact_type": "conversation", "speaker": "user", "attrs": {}}
    return {"run_id": "run-" + prefix, "entities": entities, "observations": [observation],
            "claims": [claim(prefix + "cl", entities[0]["id"], "requires", entities[1]["id"], observation["id"], prefix + "scope")]}


class CoreProjectionTests(unittest.TestCase):
    def test_source_authority_preserved_and_reference_map_is_exact(self):
        source = example()
        source["claims"][0]["future_attribute"] = {"unknown_nested": [1, False]}
        original = deepcopy(source)
        result = project_core(source)
        self.assertEqual(source, original)
        self.assertEqual(result["source"], original)
        self.assertEqual(result["source_claim_ids"], ["acl"])
        root = result["claim_node_ids"]["acl"]
        self.assertEqual(result["reference_map"][root]["reference"], {"collection": "claims", "id": "acl"})
        self.assertFalse(result["inference_eligible"])
        self.assertFalse(result["persistable_claim"])
        self.assertIsNone(result["confidence"])
        self.assertEqual(result["gates"]["acl"]["source_contract"], "locally_consistent")

    def test_structural_names_can_change_but_semantic_identity_cannot(self):
        a, b = example("a"), example("b")
        b["entities"][0]["label"] = "Singer"
        b["entities"][0]["canonical_key"] = "singer"
        b["entities"][1]["label"] = "Breath"
        b["entities"][1]["canonical_key"] = "breath"
        self.assertEqual(align(project_core(a, "structural")["structure"], project_core(b, "structural")["structure"])["status"], "isomorphic")
        self.assertEqual(align(project_core(a, "semantic")["structure"], project_core(b, "semantic")["structure"])["status"], "different")
        self.assertTrue(project_core(a, "structural")["dropped_attributes"])

    def test_subject_object_direction_and_binding_are_not_bags_of_words(self):
        source = example()
        source["claims"].append(claim("second", "a2", "supplies", "a1", "aob", "ascope"))
        changed = deepcopy(source)
        changed["claims"][1]["object"] = "a2"
        a, b = project_core(source, "structural"), project_core(changed, "structural")
        self.assertEqual(align(a["structure"], b["structure"])["status"], "different")
        predicates = {edge["predicate"] for edge in a["structure"]["edges"]}
        self.assertIn("subject", predicates)
        self.assertIn("object", predicates)

    def test_scope_membership_can_rename_but_must_not_merge(self):
        source = example()
        source["claims"].append(claim("second", "a2", "supplies", "a1", "aob", "ascope"))
        split = deepcopy(source)
        split["claims"][1]["qualifiers"]["scope"] = "another-scope"
        self.assertEqual(align(project_core(source, "structural")["structure"], project_core(split, "structural")["structure"])["status"], "different")
        roots = project_core(source, "structural")
        scopes = [node for node in roots["structure"]["nodes"] if node["kind"] == "core_scope"]
        self.assertEqual(len(scopes), 1)
        self.assertEqual(scopes[0]["lexical_identity"], {"namespace": "scope", "value": "ascope"})

    def test_status_negation_modality_branch_and_confidence_are_hard_labels(self):
        base = example()
        variants = []
        for key, value in (("status", "contested"), ("confidence", .2), ("evidence_class", "user")):
            changed = deepcopy(base)
            changed["claims"][0]["assessment"][key] = value
            variants.append(changed)
        for extra in ({"negated": True}, {"quoted": True}, {"modality": "possible"}):
            changed = deepcopy(base)
            changed["claims"][0]["qualifiers"]["extra"] = extra
            variants.append(changed)
        changed = deepcopy(base)
        changed["claims"][0]["qualifiers"]["branch"] = "other-branch"
        variants.append(changed)
        for index, changed in enumerate(variants):
            with self.subTest(variant=index):
                self.assertEqual(align(project_core(base, "structural")["structure"], project_core(changed, "structural")["structure"])["status"], "different")

    def test_predicate_entity_kind_and_universal_roles_are_preserved(self):
        base = example()
        for defect in ("predicate", "kind", "role"):
            left, right = deepcopy(base), deepcopy(base)
            if defect == "predicate":
                right["claims"][0]["predicate"] = "contradicts"
            elif defect == "kind":
                right["entities"][0]["kind"] = "character"
            else:
                left["slot_values"] = [{"instance": "instance", "slot": "output", "ord": 0, "claim": "acl", "role": "output", "conflict": False}]
                right["slot_values"] = [{"instance": "instance", "slot": "output", "ord": 0, "claim": "acl", "role": "constraint", "conflict": False}]
            with self.subTest(defect=defect):
                self.assertEqual(align(project_core(left, "structural")["structure"], project_core(right, "structural")["structure"])["status"], "different")

    def test_premises_counter_and_consequences_are_distinct_edges(self):
        source = example()
        assessed = source["claims"][0]["assessment"]
        assessed["premises"]["claims"] = ["other-claim"]
        assessed["premises"]["principles"] = ["principle-one"]
        assessed["counter"]["claims"] = ["counter-claim"]
        assessed["consequences"]["claims"] = ["next-claim"]
        result = project_core(source)
        edge_types = {edge["predicate"] for edge in result["structure"]["edges"]}
        self.assertTrue({"premises:claims", "premises:principles", "counter:claims", "consequences:claims"} <= edge_types)
        unresolved = {item["id"] for item in result["unknowns"] if item["reason"] == "referenced_record_missing"}
        self.assertTrue({"other-claim", "principle-one", "counter-claim", "next-claim"} <= unresolved)

    def test_topology_is_declared_lossy_and_never_hides_its_losses(self):
        source = example()
        changed = deepcopy(source)
        changed["claims"][0]["assessment"]["status"] = "rejected"
        left, right = project_core(source, "topology_control"), project_core(changed, "topology_control")
        self.assertEqual(align(left["structure"], right["structure"])["status"], "isomorphic")
        self.assertFalse(left["restriction_preserving_mode"])
        self.assertEqual(left["interpretation"], "lossy_topology_control")
        self.assertTrue(any(item["reason"] == "deliberately_lossy_topology_control" for item in left["dropped_attributes"]))
        self.assertEqual(right["source"]["claims"][0]["assessment"]["status"], "rejected")

    def test_invalid_core_assessment_is_exposed_not_repaired(self):
        source = example()
        source["claims"][0]["assessment"]["basis"]["support"] = []
        result = project_core(source)
        self.assertEqual(result["gates"]["acl"]["source_contract"], "invalid")
        self.assertIn("observed_requires_observation_support", result["gates"]["acl"]["errors"])
        self.assertEqual(result["source"]["claims"][0]["assessment"]["basis"]["support"], [])

    def test_absent_extrapolated_and_transfer_source_restrictions_remain(self):
        for evidence in ("absent", "extrapolated"):
            source = example()
            c = source["claims"][0]
            c["assessment"]["evidence_class"] = evidence
            if evidence == "absent":
                c["object"], c["value"] = "", None
                c["assessment"]["basis"]["support"] = []
            else:
                c["assessment"]["basis"]["derivation"] = {"operator": "op-one", "operator_version": 1, "morphism": "m-one", "depth": 1}
            result = project_core(source)
            self.assertIn("not_a_core_inference_premise", result["gates"]["acl"]["restrictions"])
            if evidence == "extrapolated":
                self.assertIn("transfer_depth_restriction", result["gates"]["acl"]["restrictions"])

    def test_duplicate_runs_missing_claim_bodies_and_invalid_selection_rejected(self):
        with self.assertRaises(ValueError):
            project_core({"claims": []})
        with self.assertRaises(ValueError):
            project_core({"run_id": "r", "claims": ["claim-id-only"]})
        source = example()
        source["claims"].append(deepcopy(source["claims"][0]))
        with self.assertRaises(ValueError):
            project_core(source)
        with self.assertRaises(ValueError):
            project_core(example(), claim_ids=["missing"])

    def test_deterministic_ids_and_trace_under_source_array_reordering(self):
        source = example()
        source["claims"].append(claim("second", "a2", "supplies", "a1", "aob", "ascope"))
        shuffled = deepcopy(source)
        shuffled["claims"].reverse()
        shuffled["entities"].reverse()
        a, b = project_core(source, "structural"), project_core(shuffled, "structural")
        self.assertEqual(a["structure"], b["structure"])
        self.assertEqual(a["reference_map"], b["reference_map"])
        self.assertTrue(all(set(node["claim_ids"]) <= {"acl", "second"} for node in a["structure"]["nodes"]))

    def test_alternatives_and_entity_parent_are_source_relations(self):
        source = example()
        source["entities"][0]["parent"] = "a2"
        source["claims"][0]["assessment"]["alternatives"] = [{"object": "a1", "value": None, "score": .2}]
        result = project_core(source)
        kinds = {node["kind"] for node in result["structure"]["nodes"]}
        edge_types = {edge["predicate"] for edge in result["structure"]["edges"]}
        self.assertIn("core_alternative", kinds)
        self.assertIn("alternative", edge_types)
        self.assertIn("parent", edge_types)

    def test_documented_entity_provenance_arrays_use_typed_references(self):
        source = example()
        source["entities"][0]["attrs"] = {"units": ["aunit"], "observations": ["aob"], "method": "mined", "unknown_ids": ["opaque-id"]}
        result = project_core(source, "structural")
        entity_node = next(node for node in result["structure"]["nodes"] if node.get("lexical_identity") == {"namespace": "entity", "value": "a1"})
        self.assertEqual(entity_node["qualifiers"]["attributes"]["attrs"], {"method": "mined", "unknown_ids": ["opaque-id"]})
        self.assertEqual(result["source"]["entities"][0]["attrs"], source["entities"][0]["attrs"])
        labels = {edge["predicate"] for edge in result["structure"]["edges"]}
        self.assertTrue({"native_provenance:units", "native_provenance:observations"} <= labels)

    def test_projection_graph_accepts_existing_graph_search_contract(self):
        try:
            from .graph_search import comparison_graph
        except ImportError:
            from graph_search import comparison_graph
        result = project_core(example())
        self.assertTrue(comparison_graph(result["structure"])["graph"]["nodes"])


if __name__ == "__main__":
    unittest.main()
