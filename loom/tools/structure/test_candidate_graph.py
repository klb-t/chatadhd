"""Author-owned compiler contract cases; no independent validation data."""
from copy import deepcopy
import unittest

try:
    from .candidate_graph import compile_bundle, validate_bundle
    from .structure_methods import validate_graph
except ImportError:
    from candidate_graph import compile_bundle, validate_bundle
    from structure_methods import validate_graph


class Drafts:
    def __init__(self, text="If P(x), then not Q(x). Every x has some y."):
        self.packet = {"schema": "loom.source_packet/1", "snapshot_id": "author-snapshot",
                       "observations": [{"id": "ob_a", "unit": "un_a", "text": text,
                                         "locator": {"source": "sha256:author", "member": "note.txt"}, "ordinal": 0}],
                       "entities": [], "claims": [], "unknown_native_field": {"kept": True}}
        self.span = {"observation": "ob_a", "byte_start": 0, "byte_len": len(text.encode("utf-8")), "quote": text}
        self.bundle = {"schema": "loom.candidate_graph/1", "packet_id": "author-snapshot", "entity_drafts": [],
                       "claim_drafts": [], "roots": [], "coverage": [], "unknowns": []}
        self.context = {}
        self.n = 0

    def entity(self, handle, kind, attrs, label=None):
        self.bundle["entity_drafts"].append({"handle": handle, "kind": kind, "label": label or handle[1:],
                                              "attrs": attrs, "support": [deepcopy(self.span)]})
        return handle

    def claim(self, subject, predicate, obj, scope, *, value=None, port=None, ordinal=0, premises=None):
        self.n += 1
        extra = {"polarity": "positive", "assertion_context": self.context[scope]}
        if port is not None:
            extra.update(port=port, ordinal=ordinal)
        c = {"handle": "@c" + str(self.n), "subject": subject, "predicate": predicate, "object": obj, "value": value,
             "qualifiers": {"scope": scope, "extra": extra},
             "assessment": {"basis": {"support": [deepcopy(self.span)]}, "premises": {"claims": premises or []}}}
        self.bundle["claim_drafts"].append(c)
        return c

    def scope(self, handle, parent=None, kind="assertion", context="asserted"):
        self.entity(handle, "scope", {"scope_type": kind, "assertion_context": context})
        self.context[handle] = context
        if parent:
            self.claim(handle, "scope_parent", parent, handle)
        return handle

    def term(self, handle, scope, kind="constant", symbol=None):
        self.entity(handle, "term_occurrence", {"term_type": kind, "symbol": symbol or handle[1:]})
        self.claim(handle, "in_scope", scope, scope)
        return handle

    def binder(self, handle, scope, symbol):
        self.entity(handle, "binder", {"symbol": symbol})
        self.claim(handle, "in_scope", scope, scope)
        return handle

    def operation(self, handle, scope, op, operands):
        self.entity(handle, "expression_occurrence", {})
        self.claim(handle, "in_scope", scope, scope)
        self.claim(handle, "operation_type", "", scope, value=op)
        for port, values in operands.items():
            for index, obj in enumerate(values):
                self.claim(handle, "operand", obj, scope, port=port, ordinal=index)
        return handle

    def app(self, handle, scope, args=()):
        pred = self.term(handle + "_pred", scope, "predicate")
        return self.operation(handle, scope, "predicate_application", {"predicate": [pred], "argument": list(args)})

    def finish(self, root):
        self.bundle["roots"] = [root]
        self.bundle["coverage"] = [{"support": [deepcopy(self.span)], "status": "represented",
                                     "reason": "author-provided explicit graph", "drafts": [root]}]
        return self.bundle


def conditional():
    d = Drafts()
    s = d.scope("@s")
    term = d.term("@x", s)
    p, q = d.app("@p", s, [term]), d.app("@q", s, [term])
    neg = d.operation("@notq", s, "negation", {"body": [q]})
    root = d.operation("@if", s, "conditional", {"antecedent": [p], "consequent": [neg]})
    d.finish(root)
    return d


def quantified():
    d = Drafts("Every x relates to some y; x and y stay distinct.")
    s = d.scope("@s")
    sx = d.scope("@sx", s, "quantifier")
    sy = d.scope("@sy", sx, "quantifier")
    bx, by = d.binder("@bx", sx, "x"), d.binder("@by", sy, "y")
    x, y = d.term("@x", sy, "variable", "x"), d.term("@y", sy, "variable", "y")
    d.claim(x, "bound_to", bx, sy)
    d.claim(y, "bound_to", by, sy)
    body = d.app("@rel", sy, [x, y])
    qy = d.operation("@qy", sx, "quantifier", {"binder": [by], "body": [body]})
    d.claim(qy, "quantifier_kind", "", sx, value="exists")
    d.claim(qy, "introduces_scope", sy, sx)
    qx = d.operation("@qx", s, "quantifier", {"binder": [bx], "body": [qy]})
    d.claim(qx, "quantifier_kind", "", s, value="forall")
    d.claim(qx, "introduces_scope", sx, s)
    d.finish(qx)
    return d


class CandidateGraphTests(unittest.TestCase):
    def check_valid(self, d):
        result = compile_bundle(d.bundle, d.packet)
        self.assertTrue(result["valid"], result["errors"])
        validate_graph(result["graph"])
        return result

    def test_exact_support_native_drafts_and_derived_graph_without_promotion(self):
        d = conditional()
        before = deepcopy((d.bundle, d.packet))
        result = self.check_valid(d)
        self.assertEqual(before, (d.bundle, d.packet))
        self.assertEqual(result["retained_input"]["source_packet"], d.packet)
        self.assertTrue(result["no_inference"] and result["no_persistence"])
        self.assertEqual(result["coverage"]["represented_bytes"], d.span["byte_len"])
        self.assertEqual(result["coverage"]["uncovered_bytes"], 0)
        self.assertEqual(len(result["refmap"]["claims"]), len(d.bundle["claim_drafts"]))
        for c in result["drafts"]["claims"]:
            self.assertNotIn("evidence_class", c["assessment"])
            self.assertNotIn("confidence", c["assessment"])
            self.assertNotIn("status", c["assessment"])
            self.assertEqual(c["assessment"]["basis"]["support"][0]["locator"], d.packet["observations"][0]["locator"])

    def test_quantifier_order_binding_and_shared_references_are_explicit(self):
        d = quantified()
        result = self.check_valid(d)
        claims = result["drafts"]["claims"]
        bound = [c for c in claims if c["predicate"] == "bound_to"]
        self.assertEqual(len(bound), 2)
        self.assertNotEqual(bound[0]["object"], bound[1]["object"])
        qtypes = [c["value"] for c in claims if c["predicate"] == "quantifier_kind"]
        self.assertEqual(set(qtypes), {"forall", "exists"})
        escaped = deepcopy(d.bundle)
        next(c for c in escaped["claim_drafts"] if c["predicate"] == "bound_to" and c["subject"] == "@x")["object"] = "@by"
        bad = compile_bundle(escaped, d.packet)
        self.assertFalse(bad["valid"])
        self.assertEqual(bad["errors"][0]["code"], "binding_capture")

    def test_shadowed_outer_binding_is_rejected(self):
        d = quantified()
        for e in d.bundle["entity_drafts"]:
            if e["handle"] in {"@by", "@y"}:
                e["attrs"]["symbol"] = "x"
        self.assertEqual(validate_bundle(d.bundle, d.packet)["errors"][0]["code"], "binding_capture")

    def test_negated_conditional_and_conditional_negation_keep_different_nesting(self):
        d = conditional()
        first = self.check_valid(d)
        altered = deepcopy(d.bundle)
        for c in altered["claim_drafts"]:
            if c["predicate"] == "operand" and c["subject"] == "@if" and c["qualifiers"]["extra"]["port"] == "consequent":
                c["object"] = "@q"
            elif c["predicate"] == "operand" and c["subject"] == "@notq":
                c["object"] = "@if"
        altered["roots"] = ["@notq"]
        second = compile_bundle(altered, d.packet)
        self.assertTrue(second["valid"], second["errors"])
        self.assertNotEqual(first["graph"], second["graph"])

    def test_missing_operand_unknown_wildcard_and_bad_ordinal_are_rejected(self):
        d = conditional()
        for mutation in ("missing", "unknown", "ordinal"):
            b = deepcopy(d.bundle)
            edge = next(c for c in b["claim_drafts"] if c["predicate"] == "operand" and c["subject"] == "@if")
            if mutation == "missing":
                b["claim_drafts"].remove(edge)
            elif mutation == "unknown":
                edge["object"] = "@unknown"
            else:
                edge["qualifiers"]["extra"]["ordinal"] = 2
            result = compile_bundle(b, d.packet)
            self.assertFalse(result["valid"])
            self.assertEqual(result["graph"]["nodes"], [])
            self.assertEqual(result["retained_input"]["bundle"], b)

    def test_source_forgery_utf8_split_and_budgets_fail_without_losing_input(self):
        d = Drafts("Żółć remains in the source.")
        s = d.scope("@s")
        d.finish(d.app("@p", s))
        self.check_valid(d)
        b = deepcopy(d.bundle)
        b["entity_drafts"][0]["support"][0]["quote"] = "fake"
        self.assertEqual(validate_bundle(b, d.packet)["errors"][0]["code"], "quote_mismatch")
        b["entity_drafts"][0]["support"][0].update(byte_start=1, byte_len=1, quote="x")
        self.assertEqual(validate_bundle(b, d.packet)["errors"][0]["code"], "utf8_boundary")
        result = compile_bundle(d.bundle, d.packet, {"max_entities": 1})
        self.assertFalse(result["valid"])
        self.assertEqual(result["retained_input"]["source_packet"], d.packet)

    def test_located_unknowns_are_valid_abstentions_not_matchable_nodes(self):
        d = Drafts("Maybe something else entirely.")
        d.bundle["coverage"] = [{"support": [d.span], "status": "unsupported", "reason": "unknown operation", "drafts": []}]
        d.bundle["unknowns"] = [{"support": [d.span], "reason": "unsupported operation"}]
        result = self.check_valid(d)
        self.assertEqual(result["graph"]["nodes"], [])
        self.assertEqual(result["coverage"]["representation_status"], "unrepresented")
        self.assertEqual(result["coverage"]["unknown_bytes"], d.span["byte_len"])
        self.assertEqual(result["coverage"]["represented_bytes"], 0)
        d.bundle["unknowns"][0]["support"] = []
        self.assertFalse(validate_bundle(d.bundle, d.packet)["valid"])

    def test_scope_cycles_context_mismatch_and_expression_cycles_reject(self):
        d = conditional()
        for mutation in ("scope", "context", "syntax"):
            b = deepcopy(d.bundle)
            if mutation == "scope":
                d.claim("@s", "scope_parent", "@s", "@s")
                b = deepcopy(d.bundle)
                d.bundle["claim_drafts"].pop()
            elif mutation == "context":
                b["claim_drafts"][0]["qualifiers"]["extra"]["assertion_context"] = "quoted"
            else:
                next(c for c in b["claim_drafts"] if c["predicate"] == "operand" and c["subject"] == "@notq")["object"] = "@if"
            self.assertFalse(compile_bundle(b, d.packet)["valid"])

    def test_existing_context_is_typed_assessed_and_never_collides_with_projection_refs(self):
        d = conditional()
        d.packet["entities"] = [{"id": "prior:c", "kind": "component", "label": "Source"},
                                  {"id": "projection:prior:c", "kind": "component", "label": "Other"}]
        assessment = {"basis": {"support": [], "derivation": None}, "premises": {"claims": []},
                      "evidence_class": "observed", "origin": "archive", "confidence": 0.5, "status": "active",
                      "open": {"questions": ["retained exactly"]}}
        d.packet["claims"] = [{"id": "c", "subject": "prior:c", "predicate": "some_relation", "object": "projection:prior:c", "value": None, "assessment": assessment}]
        d.claim("@x", "denotes", "prior:c", "@s", premises=["c"])
        result = self.check_valid(d)
        by_id = {n["id"]: n for n in result["graph"]["nodes"]}
        self.assertEqual(by_id["prior:c"]["kind"], "existing_entity:component")
        premise_targets = {e["target"] for e in result["graph"]["edges"] if e["predicate"] == "premise"}
        self.assertTrue(all(by_id[x]["kind"] == "existing_claim_reference" for x in premise_targets))
        self.assertEqual(result["retained_input"]["source_packet"]["claims"][0]["assessment"], assessment)
        d.packet["claims"][0]["assessment"]["evidence_class"] = "extrapolated"
        self.assertEqual(validate_bundle(d.bundle, d.packet)["errors"][0]["code"], "premise_ineligible")

    def test_compilation_is_order_independent_and_packet_bytes_affect_identity(self):
        d = conditional()
        first = self.check_valid(d)
        d.bundle["entity_drafts"].reverse()
        d.bundle["claim_drafts"].reverse()
        again = self.check_valid(d)
        self.assertEqual(first["drafts"], again["drafts"])
        self.assertEqual(first["graph"], again["graph"])
        d.packet["unknown_native_field"]["kept"] = "changed"
        changed = self.check_valid(d)
        self.assertNotEqual(first["packet_hash"], changed["packet_hash"])
        self.assertNotEqual(first["refmap"], changed["refmap"])

    def test_declared_roots_and_located_uncovered_source_remain_visible(self):
        d = conditional()
        first = self.check_valid(d)
        d.bundle["roots"].append("@q")
        other = self.check_valid(d)
        self.assertNotEqual(first["graph"], other["graph"])
        q = other["refmap"]["entities"]["@q"]
        self.assertTrue(next(n for n in other["graph"]["nodes"] if n["id"] == q)["qualifiers"]["declared_root"])
        d.bundle["coverage"] = []
        uncovered = self.check_valid(d)["coverage"]
        self.assertEqual(uncovered["uncovered_spans"], [d.span])
        self.assertEqual(uncovered["representation_status"], "partial")

    def test_shared_conjunction_dag_is_bounded_without_enumerating_all_paths(self):
        d = Drafts("A repeated shared expression remains source located.")
        scope = d.scope("@s")
        child = d.app("@p", scope)
        for depth in range(25):
            child = d.operation("@and" + str(depth), scope, "conjunction", {"member": [child, child]})
        d.finish(child)
        self.check_valid(d)
        bad = validate_bundle(d.bundle, d.packet, {"max_depth": 10})
        self.assertFalse(bad["valid"])
        self.assertEqual(bad["errors"][0]["code"], "depth")


if __name__ == "__main__":
    unittest.main()
