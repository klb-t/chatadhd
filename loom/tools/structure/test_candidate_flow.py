from copy import deepcopy
import unittest

from candidate_flow import run_candidate_flow
from grounded_frames import prepare_anchors
from context_delta import prepare_context, undo_view


def record(ident="one", text="P(a)", predicate="P", argument="a"):
    span = {"observation": "o_" + ident, "byte_start": 0,
            "byte_len": len(text.encode("utf-8")), "quote": text}
    packet = {"schema": "loom.source_packet/1", "snapshot_id": ident,
              "observations": [{"id": span["observation"], "unit": "u_" + ident,
                                "text": text, "locator": {"source": "s_" + ident}}],
              "entities": [], "claims": []}
    entities = [{"handle": "@s", "kind": "scope", "label": text,
                 "attrs": {"scope_type": "assertion", "assertion_context": "asserted"}},
                {"handle": "@p", "kind": "expression_occurrence", "label": text, "attrs": {}},
                {"handle": "@f", "kind": "term_occurrence", "label": predicate,
                 "attrs": {"term_type": "predicate", "symbol": predicate}},
                {"handle": "@a", "kind": "term_occurrence", "label": argument,
                 "attrs": {"term_type": "constant", "symbol": argument}}]
    for entity in entities:
        entity["support"] = [deepcopy(span)]
    claims = []

    def claim(subject, relation, object_="", value=None, **extra):
        claims.append({"handle": "@c" + str(len(claims)), "subject": subject,
                       "predicate": relation, "object": object_, "value": value,
                       "qualifiers": {"scope": "@s", "extra": {"polarity": "positive",
                                      "assertion_context": "asserted", **extra}},
                       "assessment": {"basis": {"support": [deepcopy(span)]}, "premises": {"claims": []}}})
    claim("@p", "operation_type", value="predicate_application")
    claim("@p", "operand", "@f", port="predicate", ordinal=0)
    claim("@p", "operand", "@a", port="argument", ordinal=0)
    for handle in ("@p", "@f", "@a"):
        claim(handle, "in_scope", "@s")
    return {"id": ident, "strategy": "direct", "source_packet": packet,
            "bundle": {"schema": "loom.candidate_graph/1", "packet_id": ident,
                       "entity_drafts": entities, "claim_drafts": claims, "roots": ["@p"],
                       "coverage": [{"support": [span], "status": "represented", "reason": "author supplied interpretation",
                                     "drafts": ["@p"]}], "unknowns": []}}


def framed(direct):
    out = deepcopy(direct)
    bundle = out.pop("bundle")
    out["strategy"] = "frames"
    out["anchoring"] = {"schema": "loom.grounded_anchors/1", "packet_id": bundle["packet_id"],
                        "anchors": bundle["entity_drafts"], "coverage": bundle["coverage"], "unknowns": []}
    prepared = prepare_anchors(out["source_packet"], out["anchoring"])["prepared"]
    common = {"scope": "@s", "polarity": "positive", "assertion_context": "asserted", "support": ["@p"], "premises": []}
    reading = {"id": "reading", "frames": [{"anchor": "@p", "operation": "predicate_application",
               "operands": [{"port": "predicate", "ordinal": 0, "target": "@f"},
                            {"port": "argument", "ordinal": 0, "target": "@a"}], **common}],
               "links": [{"subject": h, "predicate": "in_scope", "object": "@s", **common} for h in ("@p", "@f", "@a")],
               "roots": ["@p"], "coverage": [], "unknowns": []}
    out["composition"] = {"schema": "loom.grounded_frames/1", "packet_id": bundle["packet_id"],
                          "anchors_hash": prepared["anchors_hash"], "alternatives": [reading]}
    return out


class CandidateFlowTests(unittest.TestCase):
    def test_direct_frames_match_same_source_not_independent(self):
        a = record()
        b = framed(a)
        b["id"] = "two"
        a["source_group"], b["source_group"] = "group-a", "group-b"
        before = deepcopy([a, b])
        result = run_candidate_flow([a, b], ["one"])
        row = result["retrieval"][0]["result"]["results"][0]
        self.assertEqual(row["verification"]["status"], "matched")
        self.assertTrue(row["evidence_overlap"]["known_overlap"])
        self.assertTrue(row["evidence_overlap"]["declared_groups_differ"])
        self.assertIsNone(row["evidence_overlap"]["independent_recurrence"])
        self.assertEqual([a, b], before)
        self.assertEqual(result["semantics"]["claims_promoted"], 0)

    def test_explicit_occurrence_abstraction_does_not_infer_spelling_identity(self):
        a, b = record(), record("two", "Q(b)", "Q", "b")
        exact = run_candidate_flow([a, b], ["one"])
        self.assertEqual(exact["retrieval"][0]["result"]["results"], [])
        structural = run_candidate_flow([a, b], ["one"], mode="occurrence_shape")
        row = structural["retrieval"][0]["result"]["results"][0]
        self.assertEqual(row["verification"]["status"], "matched")
        self.assertFalse(row["evidence_overlap"]["known_overlap"])
        self.assertIsNone(row["evidence_overlap"]["independent_recurrence"])
        self.assertTrue(all(v["view"]["losses"] for v in structural["variants"].values()))

    def test_alternatives_are_separate_and_not_their_own_recurrence(self):
        a = framed(record())
        alternative = deepcopy(a["composition"]["alternatives"][0])
        alternative["id"] = "second"
        a["composition"]["alternatives"].append(alternative)
        result = run_candidate_flow([a], [a["id"]])
        self.assertEqual(len(result["variants"]), 2)
        self.assertEqual(len(result["retrieval"]), 2)
        self.assertTrue(all(r["result"]["input_candidates"] == 0 for r in result["retrieval"]))
        self.assertIsNone(result["records"][a["id"]]["selected_interpretation"])

    def test_invalid_quotes_abstention_and_budgets_stay_visible(self):
        invalid = record("bad")
        invalid["bundle"]["claim_drafts"][0]["assessment"]["basis"]["support"][0]["quote"] = "wrong"
        abstention = record("unknown")
        bundle = abstention["bundle"]
        span = bundle["coverage"][0]["support"][0]
        bundle.update(entity_drafts=[], claim_drafts=[], roots=[],
                      coverage=[{"support": [span], "status": "unsupported", "reason": "cannot interpret", "drafts": []}],
                      unknowns=[{"support": [span], "reason": "unrepresented source"}])
        result = run_candidate_flow([invalid, abstention, record("z")], ["bad", "unknown", "z"], max_records=2)
        self.assertEqual(result["variants"], {})
        self.assertEqual({o["reason"] for o in result["omissions"]}, {"invalid_candidate", "valid_abstention", "record_budget"})
        self.assertTrue(all(r["status"] == "not_searched" for r in result["retrieval"]))

    def test_future_context_rejection_has_no_unfiltered_fallback(self):
        a = record()
        packet = a.pop("source_packet")
        packet["observations"][0]["observed_at"] = "2030-01-01T00:00:00Z"
        a["context"] = {"base_snapshot": packet,
                        "current_spans": [{"id": "span", **a["bundle"]["coverage"][0]["support"][0]}],
                        "claim_refs": [], "time_cut": "2026-09-28T00:00:00Z"}
        result = run_candidate_flow([a], ["one"])
        self.assertEqual(result["records"]["one"]["status"], "context_rejected")
        self.assertEqual(result["variants"], {})

    def test_malformed_context_does_not_abort_valid_neighbors(self):
        a, b = record(), record("two")
        a.pop("source_packet")
        for malformed in ({}, [], None):
            a["context"] = malformed
            result = run_candidate_flow([a, b], ["one", "two"])
            self.assertEqual(result["records"]["one"]["status"], "context_rejected")
            self.assertEqual(result["records"]["two"]["status"], "compiled")

    def test_context_packet_compiles_and_overlay_is_reversible(self):
        a = record()
        packet = a.pop("source_packet")
        packet["observations"][0]["observed_at"] = "2026-01-01T00:00:00Z"
        spans = [{"id": "span", **a["bundle"]["coverage"][0]["support"][0]}]
        cut = "2026-09-28T00:00:00Z"
        prepared = prepare_context(packet, spans, [], time_cut=cut)["packet"]
        delta = {"id": "delta", "base_hash": prepared["base_hash"], "packet_hash": prepared["packet_hash"],
                 "additions": [{"id": "note", "draft": {"candidate_record": "one"},
                                "current_span_ids": ["span"], "existing_claim_ids": []}],
                 "threads": [], "links": [], "references": []}
        a["context"] = {"base_snapshot": packet, "current_spans": spans, "claim_refs": [],
                        "time_cut": cut, "delta": delta}
        result = run_candidate_flow([a], ["one"])
        report = result["records"]["one"]
        self.assertEqual(report["status"], "compiled")
        self.assertEqual(report["context_application"]["status"], "applied")
        self.assertEqual(undo_view(report["context_application"]["view"])["snapshot"], packet)
        retained = report["compilation"]["retained_input"]["source_packet"]
        self.assertNotIn("origin", retained)
        self.assertEqual(retained, report["context_preparation"]["packet"])

    def test_current_span_boundary_applies_to_compiled_draft_support(self):
        a = record(text="P(a). Later.")
        packet = a.pop("source_packet")
        selected = {"id": "first", "observation": "o_one", "byte_start": 0, "byte_len": 5, "quote": "P(a)."}
        a["context"] = {"base_snapshot": packet, "current_spans": [selected], "claim_refs": [],
                        "time_cut": "2026-09-28T00:00:00Z"}
        result = run_candidate_flow([a], ["one"])
        self.assertEqual(result["variants"], {})
        self.assertTrue(result["records"]["one"]["compilation"]["valid"])
        self.assertEqual(result["omissions"][0]["reason"], "support_outside_current_spans")
        # The union may cover contiguous selected intervals; a gap may not.
        a["context"]["current_spans"].append({"id": "rest", "observation": "o_one", "byte_start": 5,
                                              "byte_len": 7, "quote": " Later."})
        self.assertEqual(run_candidate_flow([a], ["one"])["records"]["one"]["status"], "compiled")


if __name__ == "__main__":
    unittest.main()
