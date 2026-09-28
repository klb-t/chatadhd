"""Author-owned offline contract checks; no model or independent fixtures."""
from copy import deepcopy
import hashlib
import json
import unittest

try:
    from .grounded_frames import prepare_anchors, compose_frames, compile_frames
except ImportError:
    from grounded_frames import prepare_anchors, compose_frames, compile_frames


def span(text, quote=None, start=0):
    quote = text if quote is None else quote
    at = text.index(quote, start)
    return {"observation": "ob_author", "byte_start": len(text[:at].encode("utf-8")),
            "byte_len": len(quote.encode("utf-8")), "quote": quote}


def fixture():
    text = "Żółw: if P, then Q."
    packet = {"schema": "loom.source_packet/1", "snapshot_id": "snap-author",
              "observations": [{"id": "ob_author", "unit": "un_author", "kind": "sentence", "text": text,
                                "locator": {"source": "sha256:" + hashlib.sha256(text.encode()).hexdigest(),
                                            "member": "author.txt", "byte_start": 0, "byte_len": len(text.encode())},
                                "lang": "en", "date": "", "ordinal": 0, "artifact_type": "text",
                                "speaker": "user", "attrs": {}}],
              "entities": [], "claims": [], "metadata": {"source_identity_verification": "author_supplied"}}
    def anchor(handle, kind, label, attrs=None, quote=None):
        return {"handle": handle, "kind": kind, "label": label, "attrs": attrs or {},
                "support": [span(text, quote)]}
    anchors = [anchor("@s", "scope", "assertion", {"scope_type": "assertion", "assertion_context": "asserted"}),
               anchor("@if", "expression_occurrence", "conditional", quote="if P, then Q"),
               anchor("@p", "expression_occurrence", "P", quote="P"),
               anchor("@q", "expression_occurrence", "Q", quote="Q"),
               anchor("@P", "term_occurrence", "P", {"term_type": "predicate", "symbol": "P"}, "P"),
               anchor("@Q", "term_occurrence", "Q", {"term_type": "predicate", "symbol": "Q"}, "Q")]
    first = {"schema": "loom.grounded_anchors/1", "packet_id": packet["snapshot_id"], "anchors": anchors,
             "coverage": [], "unknowns": []}
    def frame(ident, operation, operands):
        return {"anchor": ident, "operation": operation, "scope": "@s", "operands": operands,
                "polarity": "positive", "assertion_context": "asserted", "support": [ident], "premises": []}
    frames = [frame("@if", "conditional", [{"port": "antecedent", "ordinal": 0, "target": "@p"},
                                           {"port": "consequent", "ordinal": 0, "target": "@q"}]),
              frame("@p", "predicate_application", [{"port": "predicate", "ordinal": 0, "target": "@P"}]),
              frame("@q", "predicate_application", [{"port": "predicate", "ordinal": 0, "target": "@Q"}])]
    links = [{"subject": a["handle"], "predicate": "in_scope", "object": "@s", "scope": "@s",
              "polarity": "positive", "assertion_context": "asserted", "support": [a["handle"]], "premises": []}
             for a in anchors if a["kind"] != "scope"]
    reading = {"id": "reading-a", "frames": frames, "links": links, "roots": ["@if"],
               "coverage": [{"support": ["@s"], "status": "represented", "reason": "author-specified structure", "drafts": ["@if"]}],
               "unknowns": []}
    prepared = prepare_anchors(packet, first)
    assert prepared["valid"], prepared
    second = {"schema": "loom.grounded_frames/1", "packet_id": packet["snapshot_id"],
              "anchors_hash": prepared["prepared"]["anchors_hash"], "alternatives": [reading]}
    return packet, first, second


def all_operations_fixture():
    packet, first, second = fixture()
    text = "For each x, if P(x), then Q(x) and not R(x)."
    packet["observations"][0]["text"] = text
    packet["observations"][0]["locator"].update(source="sha256:" + hashlib.sha256(text.encode()).hexdigest(), byte_len=len(text.encode()))
    anchors = [{"handle": "@s", "kind": "scope", "label": "assertion",
                "attrs": {"scope_type": "assertion", "assertion_context": "asserted"}, "support": [span(text)]},
               {"handle": "@body", "kind": "scope", "label": "quantifier scope",
                "attrs": {"scope_type": "quantifier", "assertion_context": "asserted"}, "support": [span(text)]},
               {"handle": "@binder", "kind": "binder", "label": "x", "attrs": {"symbol": "x"}, "support": [span(text, "x")]}]
    for handle in ("@forall", "@if", "@and", "@not", "@p", "@q", "@r"):
        anchors.append({"handle": handle, "kind": "expression_occurrence", "label": handle[1:], "attrs": {}, "support": [span(text)]})
    for symbol in ("P", "Q", "R"):
        anchors.append({"handle": "@" + symbol, "kind": "term_occurrence", "label": symbol,
                        "attrs": {"term_type": "predicate", "symbol": symbol}, "support": [span(text, symbol)]})
        anchors.append({"handle": "@x" + symbol, "kind": "term_occurrence", "label": "x",
                        "attrs": {"term_type": "variable", "symbol": "x"}, "support": [span(text, "x", text.index(symbol))]})
    first["anchors"] = anchors
    reading = second["alternatives"][0]
    reading["frames"] = []
    def add(handle, op, operands, scope="@body", **extra):
        reading["frames"].append({"anchor": handle, "operation": op, "scope": scope,
                                  "operands": [{"port": p, "ordinal": n, "target": target} for p, n, target in operands],
                                  "polarity": "positive", "assertion_context": "asserted", "support": [handle], "premises": [], **extra})
    add("@forall", "quantifier", [("binder", 0, "@binder"), ("body", 0, "@if")], "@s", quantifier_kind="forall", introduced_scope="@body")
    add("@if", "conditional", [("antecedent", 0, "@p"), ("consequent", 0, "@and")])
    add("@and", "conjunction", [("member", 0, "@q"), ("member", 1, "@not")])
    add("@not", "negation", [("body", 0, "@r")])
    for symbol in ("P", "Q", "R"):
        add("@" + symbol.lower(), "predicate_application", [("predicate", 0, "@" + symbol), ("argument", 0, "@x" + symbol)])
    reading["links"] = []
    def link(subject, predicate, object_, scope):
        reading["links"].append({"subject": subject, "predicate": predicate, "object": object_, "scope": scope,
                                  "polarity": "positive", "assertion_context": "asserted", "support": [subject], "premises": []})
    link("@body", "scope_parent", "@s", "@body")
    for anchor in anchors:
        if anchor["kind"] != "scope":
            target = "@s" if anchor["handle"] == "@forall" else "@body"
            link(anchor["handle"], "in_scope", target, target)
    for symbol in ("P", "Q", "R"):
        link("@x" + symbol, "bound_to", "@binder", "@body")
    reading["roots"] = ["@forall"]
    reading["coverage"][0]["drafts"] = ["@forall"]
    second["anchors_hash"] = prepare_anchors(packet, first)["prepared"]["anchors_hash"]
    return packet, first, second


class AnchorCompositionTests(unittest.TestCase):
    def expand(self, packet, first, second):
        anchored = prepare_anchors(packet, first)
        self.assertTrue(anchored["valid"], anchored)
        return compose_frames(packet, anchored["prepared"], second)

    def test_hash_is_public_and_inputs_are_not_mutated(self):
        packet, first, second = fixture()
        before = deepcopy((packet, first, second))
        normalized = lambda x: json.dumps(x, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
        packet_hash = hashlib.sha256(normalized(packet)).hexdigest()
        expected = hashlib.sha256(normalized({"packet_hash": packet_hash, "anchoring": first})).hexdigest()
        self.assertEqual(second["anchors_hash"], expected)
        self.assertTrue(self.expand(packet, first, second)["valid"])
        self.assertEqual((packet, first, second), before)

    def test_unicode_offsets_are_bytes_not_character_indices(self):
        packet, first, _ = fixture()
        first["anchors"][2]["support"][0]["byte_start"] = packet["observations"][0]["text"].index("P")
        report = prepare_anchors(packet, first)
        self.assertFalse(report["valid"])
        self.assertEqual(report["errors"][0]["code"], "quote_mismatch")

    def test_source_change_invalidates_prepared_anchor_packet(self):
        packet, first, second = fixture()
        prepared = prepare_anchors(packet, first)["prepared"]
        packet["metadata"]["extra"] = "different context identity"
        report = compose_frames(packet, prepared, second)
        self.assertFalse(report["valid"])
        self.assertEqual(report["errors"][0]["code"], "changed_anchors")

    def test_composer_cannot_replace_anchors_or_inject_source_spans(self):
        packet, first, second = fixture()
        second["alternatives"][0]["frames"][0]["support"] = [first["anchors"][0]["support"][0]]
        self.assertFalse(self.expand(packet, first, second)["valid"])
        packet, first, second = fixture()
        second["anchors"] = []
        self.assertFalse(self.expand(packet, first, second)["valid"])

    def test_new_operand_anchor_is_rejected_before_compiler(self):
        packet, first, second = fixture()
        second["alternatives"][0]["frames"][0]["operands"][0]["target"] = "@invented"
        report = self.expand(packet, first, second)
        self.assertFalse(report["valid"])
        self.assertEqual(report["errors"][0]["code"], "anchor_reference")

    def test_syntax_handles_cannot_become_assessment_premises(self):
        packet, first, second = fixture()
        second["alternatives"][0]["frames"][0]["premises"] = ["@p"]
        self.assertFalse(self.expand(packet, first, second)["valid"])

    def test_unknown_operation_is_not_a_wildcard(self):
        packet, first, second = fixture()
        second["alternatives"][0]["frames"][0]["operation"] = "analogy"
        report = self.expand(packet, first, second)
        self.assertFalse(report["valid"])
        self.assertEqual(report["errors"][0]["code"], "unsupported_operation")

    def test_located_unknowns_survive_each_stage(self):
        packet, first, second = fixture()
        first["unknowns"] = [{"support": first["anchors"][0]["support"], "reason": "speaker identity not checked"}]
        second["anchors_hash"] = prepare_anchors(packet, first)["prepared"]["anchors_hash"]
        second["alternatives"][0]["unknowns"] = [{"support": ["@if"], "reason": "reading remains unreviewed"}]
        report = self.expand(packet, first, second)
        unknowns = report["alternatives"][0]["bundle"]["unknowns"]
        self.assertEqual(len(unknowns), 2)
        self.assertEqual(unknowns[1]["support"], first["anchors"][1]["support"])

    def test_alternatives_are_separate_and_operand_direction_survives(self):
        packet, first, second = fixture()
        alternative = deepcopy(second["alternatives"][0])
        alternative["id"] = "reading-b"
        targets = alternative["frames"][0]["operands"]
        targets[0]["target"], targets[1]["target"] = targets[1]["target"], targets[0]["target"]
        second["alternatives"].append(alternative)
        report = self.expand(packet, first, second)
        self.assertEqual(len(report["alternatives"]), 2)
        self.assertNotEqual(report["alternatives"][0]["bundle"]["claim_drafts"], report["alternatives"][1]["bundle"]["claim_drafts"])

    def test_frame_serialization_order_does_not_change_drafts(self):
        packet, first, second = fixture()
        initial = self.expand(packet, first, second)["alternatives"][0]["bundle"]
        second["alternatives"][0]["id"] = "other-review-label"
        second["alternatives"][0]["frames"].reverse()
        second["alternatives"][0]["links"].reverse()
        self.assertEqual(self.expand(packet, first, second)["alternatives"][0]["bundle"], initial)

    def test_repeated_symbol_text_does_not_merge_occurrences(self):
        packet, first, second = all_operations_fixture()
        report = self.expand(packet, first, second)
        self.assertTrue(report["valid"], report)
        terms = [x for x in report["alternatives"][0]["bundle"]["entity_drafts"] if x["attrs"].get("term_type") == "variable"]
        self.assertEqual(len(terms), 3)
        self.assertEqual({x["label"] for x in terms}, {"x"})

    def test_limits_reject_without_silently_omitting_anchors(self):
        packet, first, _ = fixture()
        report = prepare_anchors(packet, first, limits={"max_anchors": 1})
        self.assertFalse(report["valid"])
        self.assertEqual(report["errors"][0]["code"], "array_limit")


class SharedCompilerTests(unittest.TestCase):
    def test_unknown_only_reading_is_valid_abstention_with_empty_graph(self):
        packet, first, second = fixture()
        first.update(anchors=[], coverage=[], unknowns=[{"support": [span(packet["observations"][0]["text"])],
                                                        "reason": "source meaning was not represented"}])
        second["anchors_hash"] = prepare_anchors(packet, first)["prepared"]["anchors_hash"]
        second["alternatives"] = [{"id": "unknown", "frames": [], "links": [], "roots": [], "coverage": [], "unknowns": []}]
        report = compile_frames(packet, first, second)
        self.assertTrue(report["valid"], report.get("errors"))
        compiled = report["alternatives"][0]["compiled"]
        self.assertEqual(compiled["graph"]["nodes"], [])
        self.assertEqual(compiled["coverage"]["representation_status"], "unrepresented")

    def test_allowlisted_context_and_full_assessment_remain_unchanged(self):
        packet, first, second = fixture()
        packet["entities"] = [{"id": "e_prior", "kind": "concept", "label": "Previously named predicate", "attrs": {"older_snapshot": "old"}}]
        assessment = {"basis": {"support": [], "derivation": None}, "premises": {"claims": [], "principles": [], "assumptions": []},
                      "evidence_class": "user", "origin": "user", "confidence": 1.0, "status": "active",
                      "counter": {"claims": [], "observations": []}, "consequences": {"claims": [], "predictions": [], "checks": []},
                      "open": {"slots": [], "questions": [], "fill_query": None}, "expected_property": None, "check_state": "n/a", "alternatives": []}
        packet["claims"] = [{"id": "cl_prior", "subject": "e_prior", "predicate": "user_named", "object": "", "value": "P",
                             "qualifiers": {"scope": "old", "extra": {}}, "assessment": assessment}]
        first["anchors"] = [a for a in first["anchors"] if a["handle"] != "@P"]
        reading = second["alternatives"][0]
        reading["links"] = [link for link in reading["links"] if link["subject"] != "@P"]
        reading["frames"][1]["operands"][0]["target"] = "e_prior"
        reading["frames"][1]["premises"] = ["cl_prior"]
        second["anchors_hash"] = prepare_anchors(packet, first)["prepared"]["anchors_hash"]
        original = deepcopy(packet)
        report = compile_frames(packet, first, second)
        self.assertTrue(report["valid"], report.get("errors"))
        self.assertEqual(packet, original)
        retained = report["alternatives"][0]["compiled"]["retained_input"]["source_packet"]
        self.assertEqual(retained, original)
        self.assertEqual(retained["claims"][0]["assessment"], assessment)

    def test_conditional_and_all_five_operations_compile(self):
        for factory in (fixture, all_operations_fixture):
            packet, first, second = factory()
            report = compile_frames(packet, first, second)
            self.assertTrue(report["valid"], report.get("errors"))
            self.assertTrue(report["no_inference"] and report["no_persistence"])
            compiled = report["alternatives"][0]["compiled"]
            self.assertTrue(compiled["graph"]["nodes"])
            self.assertTrue(compiled["no_inference"] and compiled["no_persistence"])

    def test_missing_scope_membership_is_common_compiler_rejection(self):
        packet, first, second = fixture()
        second["alternatives"][0]["links"].pop()
        report = compile_frames(packet, first, second)
        self.assertFalse(report["valid"])
        self.assertEqual(report["stages"]["anchors"], "valid")
        self.assertEqual(report["stages"]["composition"], "valid")
        self.assertEqual(report["stages"]["compiler"], "rejected")

    def test_nonallowlisted_context_entity_is_common_compiler_rejection(self):
        packet, first, second = fixture()
        link = deepcopy(second["alternatives"][0]["links"][0])
        link.update(subject="@P", predicate="denotes", object="e_unavailable")
        second["alternatives"][0]["links"].append(link)
        report = compile_frames(packet, first, second)
        self.assertFalse(report["valid"])
        self.assertEqual(report["stage"], "compiler")


if __name__ == "__main__":
    unittest.main()
