"""Author-owned mechanism tests, not independent validation or extraction tests."""
import copy
import unittest

from structure_methods import (align, alpha_key, compare, formula_graph, infer,
                               operation_registry, pattern_profile, validate_formula, wl_features)


def atom(predicate, *args):
    return {"op": "atom", "predicate": predicate, "args": list(args)}


def implication(left, right):
    return {"op": "implies", "left": left, "right": right}


def neg(formula):
    return {"op": "not", "arg": formula}


def forall(var, body):
    return {"op": "forall", "var": var, "body": body}


def claim(ident, formula, **qualifiers):
    return {"claim_id": ident, "formula": formula, "qualifiers": qualifiers,
            "assessment": {"evidence_class": "user", "origin": "user", "status": "active"}}


def outputs(result):
    return {alpha_key(c["formula"]) for c in result["candidates"]}


class ProjectionTests(unittest.TestCase):
    def test_consistent_renaming_and_reordering(self):
        a = {"logic": [claim("c1", implication(atom("Cold", "r"), atom("Safe", "r"))),
                        claim("c2", atom("Cold", "r"))]}
        b = {"logic": [claim("other2", atom("Approved", "v")),
                        claim("other1", implication(atom("Approved", "v"), atom("Open", "v")))]}
        result = compare(a, b)
        self.assertEqual(result["alignment"]["status"], "isomorphic")
        self.assertAlmostEqual(result["wl_cosine"], 1.0)
        self.assertEqual(result["semantic_alignment"]["status"], "different")
        self.assertEqual(result["validity"], "not_assessed")

    def test_affirming_consequent_structure_differs(self):
        rule = implication(atom("P"), atom("Q"))
        a = {"logic": [claim("rule", rule), claim("fact", atom("P"))]}
        b = {"logic": [claim("rule", rule), claim("fact", atom("Q"))]}
        self.assertEqual(compare(a, b)["alignment"]["status"], "different")

    def test_polarity_quantifier_modality_are_preserved(self):
        base = forall("x", atom("P", "x"))
        for variant in [neg(base), {"op": "exists", "var": "x", "body": atom("P", "x")},
                        {"op": "modal", "mode": "possible", "arg": base}]:
            self.assertEqual(compare({"logic": [claim("a", base)]},
                                     {"logic": [claim("b", variant)]})["alignment"]["status"], "different")

    def test_argument_order_with_identity_anchor(self):
        a = [claim("a", atom("R", "x", "y")), claim("b", atom("S", "x"))]
        b = [claim("a", atom("R", "y", "x")), claim("b", atom("S", "x"))]
        self.assertEqual(compare({"logic": a}, {"logic": b})["alignment"]["status"], "different")
        # The shared unary anchor makes argument-position change distinguishable.
        self.assertEqual(compare({"logic": a}, {"logic": b})["semantic_alignment"]["status"], "different")

    def test_alpha_equivalence_and_bound_free_distinction(self):
        a = [claim("a", forall("x", atom("R", "x", "c")))]
        b = [claim("b", forall("y", atom("R", "y", "c")))]
        self.assertEqual(compare({"logic": a}, {"logic": b})["semantic_alignment"]["status"], "isomorphic")
        self.assertEqual(alpha_key(a[0]["formula"]), alpha_key(b[0]["formula"]))

    def test_node_ids_and_edge_order_do_not_affect_wl(self):
        g = formula_graph([claim("a", implication(atom("P"), atom("Q")))])
        h = copy.deepcopy(g)
        mapping = {n["id"]: f"reordered{i}" for i, n in enumerate(reversed(g["nodes"]))}
        for n in h["nodes"]:
            n["id"] = mapping[n["id"]]
        for e in h["edges"]:
            e["source"], e["target"] = mapping[e["source"]], mapping[e["target"]]
        h["nodes"].reverse()
        h["edges"].reverse()
        self.assertEqual(wl_features(g), wl_features(h))
        self.assertEqual(align(g, h)["status"], "isomorphic")

    def test_budget_does_not_report_false_nonmatch(self):
        g = formula_graph([claim("a", atom("P", "x"))])
        self.assertEqual(align(g, g, budget=1)["status"], "budget_exhausted")
        self.assertIsNone(align(g, g, budget=1)["score"])

    def test_wl_limitation_is_visible(self):
        # Uniform undirected C6 vs two C3: identical 1-WL colors, nonisomorphic.
        def cycles(groups):
            nodes = [{"id": str(i), "kind": "item", "role": "part"} for i in range(6)]
            edges = []
            for group in groups:
                for i, src in enumerate(group):
                    dst = group[(i + 1) % len(group)]
                    edges.extend([{"source": str(src), "target": str(dst), "predicate": "adjacent"},
                                  {"source": str(dst), "target": str(src), "predicate": "adjacent"}])
            return {"nodes": nodes, "edges": edges}
        a, b = cycles([[0, 1, 2, 3, 4, 5]]), cycles([[0, 1, 2], [3, 4, 5]])
        self.assertEqual(wl_features(a), wl_features(b))
        self.assertEqual(align(a, b)["status"], "different")

    def test_coverage_is_not_specificity_inverse(self):
        g = formula_graph([claim("a", atom("P", "x"))])
        first = pattern_profile(g, [{"id": "o1", "domain": "d1"}])
        second = pattern_profile(g, [{"id": "o1", "domain": "d1"}, {"id": "o2", "domain": "d2"}])
        self.assertEqual(first["specificity_description"], second["specificity_description"])
        self.assertNotEqual(first["universality_observation"], second["universality_observation"])


class InferenceTests(unittest.TestCase):
    def test_universal_instantiation_then_mp(self):
        premises = [claim("rule", forall("x", implication(atom("A", "x"), atom("B", "x")))),
                    claim("fact", atom("A", "t"))]
        result = infer(premises)
        target = next(c for c in result["candidates"] if c["formula"] == atom("B", "t"))
        self.assertEqual(target["premise_claim_ids"], ["fact", "rule"])
        self.assertEqual(target["rule_id"], "modus_ponens")
        self.assertFalse(target["persistable_claim"])
        self.assertIsNone(target["confidence"])

    def test_modus_tollens(self):
        result = infer([claim("r", implication(atom("A"), atom("B"))), claim("f", neg(atom("B")))])
        self.assertIn(alpha_key(neg(atom("A"))), outputs(result))

    def test_no_affirming_consequent_or_denying_antecedent(self):
        for fact, target in [(atom("B"), atom("A")), (neg(atom("A")), neg(atom("B")))]:
            result = infer([claim("r", implication(atom("A"), atom("B"))), claim("f", fact)])
            self.assertNotIn(alpha_key(target), outputs(result))

    def test_no_cross_scope_inference(self):
        result = infer([claim("r", implication(atom("A"), atom("B")), scope="s1"),
                        claim("f", atom("A"), scope="s2")])
        self.assertEqual(result["candidates"], [])

    def test_exceptions_causal_modals_existentials_abstain(self):
        for entry in [claim("r", implication(atom("A"), atom("B")), defeasible=True),
                      claim("r", {"op": "causes", "left": atom("A"), "right": atom("B")}),
                      claim("r", {"op": "exists", "var": "x", "body": atom("A", "x")}),
                      claim("r", {"op": "modal", "mode": "possible", "arg": atom("A")})]:
            result = infer([entry, claim("f", atom("A"))])
            self.assertEqual(result["candidates"], [])
            self.assertEqual(len(result["blocked"]), 1)

    def test_prohibited_premises(self):
        for evidence in ["extrapolated", "absent", "inferred"]:
            c = claim("p", atom("A"))
            c["assessment"]["evidence_class"] = evidence
            result = infer([c, claim("r", implication(atom("A"), atom("B")))])
            self.assertEqual(result["candidates"], [])
            self.assertEqual(result["blocked"][0]["reason"], "ineligible_evidence_class")

    def test_capture_avoiding_substitution(self):
        # Instantiate x with free constant y under forall y; binder must rename.
        f = forall("x", forall("y", atom("R", "x", "y")))
        result = infer([claim("r", f), claim("constant", atom("C", "y"))], max_rounds=1)
        candidate = result["candidates"][0]["formula"]
        self.assertNotEqual(candidate["var"], "y")
        self.assertEqual(candidate["body"]["args"][0], "y")
        self.assertEqual(candidate["body"]["args"][1], candidate["var"])

    def test_contradiction_does_not_explode(self):
        result = infer([claim("p", atom("P")), claim("np", neg(atom("P"))),
                        claim("rule", implication(atom("P"), atom("Anything")))])
        self.assertEqual(result["candidates"], [])
        self.assertEqual(len(result["blocked"]), 2)

    def test_derived_contradiction_is_quarantined_before_reuse(self):
        result = infer([claim("p", atom("P")),
                        claim("pq", implication(atom("P"), atom("Q"))),
                        claim("pnq", implication(atom("P"), neg(atom("Q")))),
                        claim("qr", implication(atom("Q"), atom("R")))])
        self.assertNotIn(alpha_key(atom("R")), outputs(result))
        self.assertNotIn(alpha_key(atom("Q")), outputs(result))
        self.assertEqual({alpha_key(c["formula"]) for c in result["contested_candidates"]},
                         {alpha_key(atom("Q")), alpha_key(neg(atom("Q")))})

    def test_late_contradiction_retracts_prior_dependents(self):
        result = infer([claim("p", atom("P")), claim("s", atom("S")),
                        claim("pq", implication(atom("P"), atom("Q"))),
                        claim("qr", implication(atom("Q"), atom("R"))),
                        claim("st", implication(atom("S"), atom("T"))),
                        claim("tu", implication(atom("T"), atom("U"))),
                        claim("unq", implication(atom("U"), neg(atom("Q"))))], max_rounds=4)
        self.assertNotIn(alpha_key(atom("R")), outputs(result))
        withdrawn = next(c for c in result["contested_candidates"] if c["formula"] == atom("R"))
        self.assertEqual(withdrawn["withheld_reason"], "dependency_on_contested_claim")

    def test_bound_variables_never_supply_ground_constants(self):
        f = forall("x", implication(atom("P", "x"), atom("Q", "x")))
        result = infer([claim("r", f)])
        self.assertEqual(result["candidates"], [])
        result = infer([claim("r", f), claim("all", forall("y", atom("P", "y")))])
        self.assertEqual(result["candidates"], [])

    def test_explicit_free_variable_objects_are_rejected(self):
        # The compact grammar has constants and bound variables, no open-formula
        # variable constructor. Do not silently turn an unknown constructor into
        # a ground constant.
        with self.assertRaises(ValueError):
            infer([claim("open", {"op": "atom", "predicate": "P", "args": [{"var": "x"}]})])

    def test_malformed_formula_is_not_silently_weakened(self):
        with self.assertRaises(ValueError):
            validate_formula({"op": "atom", "predicate": "P", "args": [], "negated": True})

    def test_registry_is_explicit_about_missing_families(self):
        registry = operation_registry()
        self.assertIn("unrepresented", registry["unknown_operation_policy"])
        self.assertTrue(any(op["implementation"] == "representation_only" for op in registry["operations"]))


if __name__ == "__main__":
    unittest.main()
