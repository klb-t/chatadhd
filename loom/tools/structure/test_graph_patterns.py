"""Author-owned motif mechanisms, distinct from independent validation."""
from copy import deepcopy
import unittest

from graph_patterns import discover_patterns, fingerprint


def chain(prefix="", scope="local", polarity="positive"):
    nodes = [{"id": prefix + ident, "kind": kind, "role": role,
              "qualifiers": {"scope": scope}, "claim_ids": [prefix + "claim-" + ident]}
             for ident, kind, role in [("a", "condition", "constraint"), ("b", "action", "transformation"),
                                       ("c", "result", "output")]]
    edges = [{"source": prefix + left, "target": prefix + right, "predicate": predicate,
              "qualifiers": {"polarity": polarity}, "claim_ids": [prefix + "claim-" + left + right]}
             for left, right, predicate in [("a", "b", "permits"), ("b", "c", "produces")]]
    return {"nodes": nodes, "edges": edges}


def record(ident, graph=None, source_group=None, domain=None):
    result = {"id": ident, "structure": graph if graph is not None else chain(ident + "-")}
    if source_group is not None:
        result["source_group"] = source_group
    if domain is not None:
        result["domain"] = domain
    return result


def regular_graph(kind):
    # Two six-node, degree-three graphs: triangular prism and complete
    # bipartite graph. Uniform rooted 1-round WL collides; exact graphs differ.
    pairs = [(0, 1), (1, 2), (2, 0), (3, 4), (4, 5), (5, 3), (0, 3), (1, 4), (2, 5)] if kind == "prism" else [
        (a, b) for a in range(3) for b in range(3, 6)]
    return {"nodes": [{"id": str(i), "kind": "item", "role": "part", "claim_ids": [kind + str(i)]} for i in range(6)],
            "edges": [{"source": str(a), "target": str(b), "predicate": "adjacent"}
                      for u, v in pairs for a, b in [(u, v), (v, u)]]}


def collision_experiment():
    return discover_patterns([record("prism", regular_graph("prism"), "source-prism", "d1"),
                              record("bipartite", regular_graph("bipartite"), "source-bipartite", "d2")],
                             radii=(2,), roots=("node",), max_nodes=6, wl_rounds=1)


class PatternTests(unittest.TestCase):
    def test_fingerprint_invariant_to_ids_and_array_order(self):
        left, right = chain("left-"), chain("right-")
        right["nodes"].reverse()
        right["edges"].reverse()
        self.assertEqual(fingerprint(left), fingerprint(right))
        self.assertEqual(fingerprint(left, {"kind": "node", "node_id": "left-b"}),
                         fingerprint(right, {"kind": "node", "node_id": "right-b"}))

    def test_verified_recurrence_has_every_claim_and_source_witness(self):
        records = [record("one", source_group="s1", domain="music"), record("two", source_group="s2", domain="software")]
        report = discover_patterns(records, radii=(2,), roots=("node",))
        self.assertEqual(len(report["candidates"]), 3)
        for pattern in report["candidates"]:
            self.assertEqual(pattern["support"]["independent_support"], 2)
            self.assertEqual(pattern["support"]["domain_count"], 2)
            self.assertEqual(len(pattern["source_claim_ids"]), 10)
            for occurrence in pattern["occurrences"]:
                self.assertEqual(len(occurrence["witness"]["node_mapping"]), 3)
                self.assertEqual(len(occurrence["witness"]["source_edge_witness"]), 2)
                self.assertEqual(len(occurrence["source_claim_ids"]), 5)
            self.assertFalse(pattern["persistable_claim"])
            self.assertEqual(pattern["validation_status"], "not_promoted")

    def test_radius_saturation_and_overlapping_copies_do_not_inflate_independence(self):
        report = discover_patterns([record("one", source_group="same-source", domain="d"),
                                    record("copy", source_group="same-source", domain="d")], radii=(2, 3), roots=("node",))
        self.assertEqual(report["statistics"]["duplicate_radius_views"], 6)
        self.assertEqual(report["statistics"]["unique_motifs"], 6)
        self.assertTrue(all(p["support"]["independent_support"] == 1 for p in report["candidates"]))
        self.assertTrue(all(len(o["radii"]) == 2 for p in report["candidates"] for o in p["occurrences"]))

    def test_unknown_source_groups_and_domains_stay_unknown(self):
        report = discover_patterns([record("one"), record("two")], radii=(2,), roots=("node",))
        support = report["candidates"][0]["support"]
        self.assertEqual(support["independent_support"], 0)
        self.assertEqual(support["domain_count"], 0)
        self.assertEqual(support["ungrouped_record_ids"], ["one", "two"])

    def test_direction_polarity_and_scope_are_not_dropped(self):
        base = chain("a-")
        variants = [chain("b-", scope="different"), chain("b-", polarity="negative")]
        reversed_edge = chain("b-")
        reversed_edge["edges"][0]["source"], reversed_edge["edges"][0]["target"] = (
            reversed_edge["edges"][0]["target"], reversed_edge["edges"][0]["source"])
        variants.append(reversed_edge)
        for other in variants:
            report = discover_patterns([record("a", base), record("b", other)], radii=(2,), roots=("node",))
            self.assertEqual(report["candidates"], [])

    def test_local_match_retains_excluded_boundary_without_claiming_complete_scope(self):
        report = discover_patterns([record("a"), record("b")], radii=(0,), roots=("edge",))
        self.assertEqual(len(report["candidates"]), 2)
        for pattern in report["candidates"]:
            for occurrence in pattern["occurrences"]:
                self.assertEqual(len(occurrence["boundary_edges"]), 1)
                edge = occurrence["boundary_edges"][0]["edge"]
                self.assertIn("claim_ids", edge)
                self.assertEqual(occurrence["scope_completeness"], "not_established_by_neighborhood")

    def test_homonyms_with_different_lexical_identity_do_not_merge(self):
        a, b = chain("a-"), chain("b-")
        for graph, meaning in [(a, "financial"), (b, "river")]:
            graph["nodes"][0].update(label="bank", lexical_identity={"namespace": "sense", "value": meaning})
        report = discover_patterns([record("a", a), record("b", b)], radii=(2,), roots=("node",))
        self.assertEqual(report["candidates"], [])

    def test_specificity_is_separate_from_more_occurrences_and_domains(self):
        two = discover_patterns([record("a", source_group="a", domain="d1"), record("b", source_group="b", domain="d2")],
                                radii=(2,), roots=("node",))
        three = discover_patterns([record("a", source_group="a", domain="d1"), record("b", source_group="b", domain="d2"),
                                    record("c", source_group="c", domain="d3")], radii=(2,), roots=("node",))
        for left, right in zip(two["candidates"], three["candidates"]):
            self.assertEqual(left["specificity"], right["specificity"])
            self.assertEqual(left["support"]["domain_count"], 2)
            self.assertEqual(right["support"]["domain_count"], 3)

    def test_cheap_wl_bucket_collision_is_split_by_exact_verification(self):
        report = collision_experiment()
        self.assertEqual(report["statistics"]["unique_motifs"], 12)
        self.assertEqual(report["comparison"]["cheap_recurring_fingerprint_buckets"], 1)
        self.assertEqual(report["comparison"]["verified_recurring_motifs"], 2)
        self.assertEqual(report["statistics"]["verified_nonmatches"], 6)
        self.assertEqual(report["statistics"]["verified_matches"], 10)
        self.assertTrue(report["coverage"]["verification_complete"])
        self.assertTrue(all(p["support"]["independent_support"] == 1 for p in report["candidates"]))

    def test_enumeration_and_node_budgets_report_omissions(self):
        report = discover_patterns([record("a"), record("b")], max_enumerations=1)
        self.assertFalse(report["coverage"]["enumeration_complete"])
        self.assertGreater(report["coverage"]["omitted_enumerations"], 0)
        self.assertFalse(report["coverage"]["all_requested_neighborhoods_represented"])
        bounded = discover_patterns([record("a")], roots=("edge",), radii=(0,), max_nodes=1, min_nodes=1)
        self.assertEqual(bounded["statistics"]["oversized_neighborhoods"], 2)

    def test_verification_exhaustion_never_merges_an_unchecked_fingerprint(self):
        for parameters in [{"max_verifications": 0}, {"alignment_budget": 1}]:
            report = discover_patterns([record("a"), record("b")], radii=(2,), roots=("node",), **parameters)
            self.assertEqual(report["candidates"], [])
            self.assertFalse(report["coverage"]["verification_complete"])
            self.assertGreater(report["statistics"]["verification_unknown"] + report["statistics"]["verification_skipped_by_budget"], 0)

    def test_parallel_edges_and_root_edge_identity_survive(self):
        a, b = chain("a-"), chain("b-")
        a["edges"].append(deepcopy(a["edges"][0]))
        b["edges"].append(deepcopy(b["edges"][0]))
        report = discover_patterns([record("a", a), record("b", b)], radii=(2,), roots=("edge",))
        self.assertTrue(report["candidates"])
        self.assertTrue(all(p["specificity"]["edge_count"] == 3 for p in report["candidates"]))
        for pattern in report["candidates"]:
            for occurrence in pattern["occurrences"]:
                mapping = occurrence["witness"]["edge_mapping"]
                self.assertEqual(len({m["host_edge"] for m in mapping}), 3)

    def test_unknown_semantic_fields_are_rejected_and_inputs_unchanged(self):
        records = [record("a"), record("b")]
        before = deepcopy(records)
        first = discover_patterns(records)
        self.assertEqual(first, discover_patterns(records))
        self.assertEqual(records, before)
        records[0]["structure"]["nodes"][0]["polarity"] = "negative"
        with self.assertRaises(ValueError):
            discover_patterns(records)


if __name__ == "__main__":
    unittest.main()
