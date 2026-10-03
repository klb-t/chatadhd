"""Author fixtures for the logic checker, independent of extraction accuracy."""
from copy import deepcopy
import json
from pathlib import Path
import subprocess
import sys
import unittest

try:
    from .logic_check import check_bundle
    from .test_candidate_graph import Drafts, quantified
except ImportError:
    from logic_check import check_bundle
    from test_candidate_graph import Drafts, quantified


def example_input():
    """A hand-authored graph/query, not a parser for the source sentence."""
    d = Drafts("Jeżeli pada, to droga jest mokra. Pada. Czy droga jest mokra?")
    s = d.scope("@s")
    p, q = d.app("@p", s), d.app("@q", s)
    d.operation("@if", s, "conditional", {"antecedent": [p], "consequent": [q]})
    d.operation("@reverse", s, "conditional", {"antecedent": [q], "consequent": [p]})
    d.operation("@notp", s, "negation", {"body": [p]})
    d.operation("@notq", s, "negation", {"body": [q]})
    d.operation("@both", s, "conjunction", {"member": [p, q]})
    d.finish("@if")
    d.bundle["roots"] = ["@if", "@reverse", "@notp", "@notq", "@both", p, q]
    return {"bundle": d.bundle, "source_packet": d.packet,
            "premise_roots": ["@if", p], "conclusion_root": q}


class LogicCheckTests(unittest.TestCase):
    def test_manual_truth_decisions(self):
        cases = [
            (["@if", "@p"], "@q", "entailed", 4),
            (["@if", "@notq"], "@notp", "entailed", 4),
            (["@if", "@q"], "@p", "undetermined", 4),
            (["@reverse", "@p"], "@q", "undetermined", 4),
            (["@both"], "@p", "entailed", 4),
            (["@notq"], "@q", "contradicted", 2),
            (["@p", "@notp"], "@q", "inconsistent_premises", 4),
            ([], "@q", "undetermined", 2),
            (["@q"], "@q", "entailed", 2),
        ]
        for premises, conclusion, expected, assignments in cases:
            with self.subTest(premises=premises, conclusion=conclusion):
                request = example_input()
                request.update(premise_roots=premises, conclusion_root=conclusion)
                result = check_bundle(**request)
                self.assertEqual(result["status"], expected, result)
                self.assertEqual(result["assignments_checked"], assignments)
                self.assertTrue(result["no_persistence"] and result["no_promotion"])
                self.assertFalse(result["source_interpretation_validated"])

    def test_counterexamples_satisfy_premises(self):
        request = example_input()
        request["premise_roots"] = ["@if", "@q"]
        request["conclusion_root"] = "@p"
        result = check_bundle(**request)
        self.assertEqual(result["witnesses"]["conclusion_true"], {"@p": True, "@q": True})
        self.assertEqual(result["witnesses"]["conclusion_false"], {"@p": False, "@q": True})
        self.assertEqual(result["premise_models"], 2)
        request["premise_roots"] = ["@p", "@notp"]
        inconsistent = check_bundle(**request)
        self.assertFalse(inconsistent["premise_consistent"])
        self.assertEqual(set(inconsistent["witnesses"].values()), {None})

    def test_shared_handle_only_and_preserved_input(self):
        request = example_input()
        for e in request["bundle"]["entity_drafts"]:
            if e["handle"] in {"@p", "@q", "@p_pred", "@q_pred"}:
                e["label"] = "same label"
                if "symbol" in e["attrs"]:
                    e["attrs"]["symbol"] = "P"
        request.update(premise_roots=["@p"], conclusion_root="@q")
        original = deepcopy(request)
        result = check_bundle(**request)
        self.assertEqual(result["status"], "undetermined")
        self.assertEqual(request, original)
        self.assertEqual(result["retained_input"]["bundle"], request["bundle"])
        self.assertEqual(result["assumptions"]["conditional"], "material_implication")
        self.assertEqual(len(result["atoms"]), 2)
        for atom in result["atoms"]:
            self.assertTrue(atom["support"][0]["locator"])
            self.assertEqual(len(atom["support"][0]["observation_text_hash"]), 64)
        self.assertIn("@both", result["ignored_roots"])

    def test_valid_wrong_interpretation_is_not_source_verification(self):
        request = example_input()
        text = "The source does not say that the conclusion follows."
        request["source_packet"]["observations"][0]["text"] = text
        # Every quote remains exact, but supplied semantics deliberately have no
        # justification in the replacement source. The checker cannot detect it.
        def replace(value):
            if isinstance(value, dict):
                if {"observation", "byte_start", "byte_len", "quote"} == set(value):
                    value.update(byte_start=0, byte_len=len(text.encode()), quote=text)
                else:
                    for item in value.values():
                        replace(item)
            elif isinstance(value, list):
                for item in value:
                    replace(item)
        replace(request["bundle"])
        result = check_bundle(**request)
        self.assertEqual(result["status"], "entailed")
        self.assertFalse(result["source_interpretation_validated"])
        self.assertFalse(result["source_truth_validated"])

    def test_quantified_bundle_is_unsupported(self):
        d = quantified()
        result = check_bundle(d.bundle, d.packet, [], "@qx")
        self.assertTrue(result["validation"]["valid"])
        self.assertEqual(result["status"], "unsupported")

    def test_denotes_and_prior_claims_are_not_silent_inference(self):
        request = example_input()
        request["source_packet"]["entities"].append({"id": "en_old", "kind": "concept"})
        relation = deepcopy(request["bundle"]["claim_drafts"][0])
        relation.update(handle="@denotes", subject="@p_pred", predicate="denotes", object="en_old")
        request["bundle"]["claim_drafts"].append(relation)
        result = check_bundle(**request)
        self.assertTrue(result["validation"]["valid"])
        self.assertEqual(result["status"], "unsupported")
        request = example_input()
        request["source_packet"]["claims"].append({"id": "cl_old", "assessment": {
            "basis": {}, "premises": {}, "evidence_class": "observed", "origin": "user",
            "confidence": 1, "status": "active"}})
        request["bundle"]["claim_drafts"][0]["assessment"]["premises"]["claims"] = ["cl_old"]
        result = check_bundle(**request)
        self.assertTrue(result["validation"]["valid"])
        self.assertEqual(result["status"], "unsupported")

    def test_full_assignment_bound_and_excess_atoms(self):
        for count, expected in [(12, "entailed"), (13, "limit")]:
            d = Drafts("Hand-supplied conjunction of independent atomic statements.")
            scope = d.scope("@s")
            atoms = [d.app("@a" + str(i), scope) for i in range(count)]
            d.operation("@all", scope, "conjunction", {"member": atoms})
            d.finish("@all")
            d.bundle["roots"].append(atoms[0])
            result = check_bundle(d.bundle, d.packet, ["@all"], atoms[0])
            self.assertEqual(result["status"], expected)
            self.assertEqual(result["assignments_checked"], 4096 if count == 12 else 0)

    def test_contexts_and_negative_structural_polarity_are_unsupported(self):
        for scope_type, context in [("quotation", "quoted"), ("hypothesis", "hypothetical"), ("assertion", "unknown")]:
            with self.subTest(context=context):
                request = example_input()
                next(e for e in request["bundle"]["entity_drafts"] if e["kind"] == "scope")["attrs"] = {
                    "scope_type": scope_type, "assertion_context": context}
                for c in request["bundle"]["claim_drafts"]:
                    c["qualifiers"]["extra"]["assertion_context"] = context
                result = check_bundle(**request)
                self.assertTrue(result["validation"]["valid"])
                self.assertEqual(result["status"], "unsupported")
        request = example_input()
        request["bundle"]["claim_drafts"][0]["qualifiers"]["extra"]["polarity"] = "negative"
        self.assertEqual(check_bundle(**request)["status"], "unsupported")

    def test_declared_roots_and_selection_shape(self):
        for update in [{"premise_roots": ["@p", "@p"]}, {"premise_roots": "@p"},
                       {"premise_roots": [False]}, {"conclusion_root": []},
                       {"conclusion_root": "@p_pred"}]:
            request = example_input()
            request.update(update)
            self.assertEqual(check_bundle(**request)["status"], "rejected")

    def test_limits_and_exhaustive_search_boundary(self):
        for limits in [{"max_atoms": True}, {"max_atoms": 13}, {"max_atoms": 0},
                       {"max_assignments": 4097}, {"bad": 1}, []]:
            self.assertEqual(check_bundle(**example_input(), limits=limits)["status"], "rejected")
        for limits in [{"max_atoms": 1}, {"max_assignments": 3}]:
            result = check_bundle(**example_input(), limits=limits)
            self.assertEqual(result["status"], "limit")
            self.assertEqual(result["assignments_checked"], 0)
        self.assertEqual(check_bundle(**example_input(), limits={"max_assignments": 4})["status"], "entailed")

    def test_dangling_reference_and_bad_source_fail_validation(self):
        request = example_input()
        request["bundle"]["entity_drafts"][0]["support"][0]["quote"] = "forged"
        result = check_bundle(**request)
        self.assertEqual(result["status"], "rejected")
        self.assertEqual(result["retained_input"]["bundle"], request["bundle"])
        self.assertFalse(result["validation"]["valid"])

    def test_json_preflight_is_bounded_and_fail_closed(self):
        for bad in [float("nan"), {1: "not a JSON key"}, {"x": object()}, "\ud800"]:
            self.assertEqual(check_bundle(bad, {}, [], "@p")["status"], "rejected")
        deep = []
        for _ in range(140):
            deep = [deep]
        result = check_bundle(deep, {}, [], "@p")
        self.assertEqual(result["status"], "rejected")
        self.assertIsNone(result["retained_input"])
        self.assertEqual(result["retention"]["status"], "not_copied")

    def test_cli_reproducible_query(self):
        script = Path(__file__).with_name("logic_check.py")
        output = subprocess.run([sys.executable, str(script)], input=json.dumps(example_input()),
                                capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(output.stdout)["status"], "entailed")
        invalid = subprocess.run([sys.executable, str(script)], input='{"bundle":',
                                 capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(invalid.stdout)["status"], "rejected")


if __name__ == "__main__":
    if sys.argv[1:] == ["--example"]:
        print(json.dumps(example_input(), ensure_ascii=False))
    else:
        unittest.main()
