"""Author-owned immutable context/delta mechanisms, not semantic quality gold."""
import base64
from copy import deepcopy
import hashlib
import json
import unittest

from context_delta import apply_view, canonical, prepare_context, undo_view, validate_delta

CUT = "2026-01-03T00:00:00Z"


def snapshot():
    return {"schema": "loom.source_packet/1", "snapshot_id": "snapshot_a",
            "observations": [
                {"id": "old", "text": "Aster keeps state.", "unit": "unit_1", "locator": {"source": "archive.txt"},
                 "observed_at": "2026-01-01T00:00:00Z", "source_group": "source_a"},
                {"id": "current", "text": "It waits. Żaba returns. Beacon arrives.", "unit": "unit_1", "locator": {"source": "archive.txt"},
                 "observed_at": "2026-01-02T00:00:00Z", "source_group": "source_a"},
                {"id": "future", "text": "Future identity.", "observed_at": "2026-01-04T00:00:00Z"}],
            "entities": [{"id": "entity_a", "label": "Aster", "observed_at": "2026-01-01T00:00:00Z"}],
            "claims": [{"id": "claim_a", "subject": "entity_a", "predicate": "keeps", "object": "", "value": "state",
                        "observed_at": "2026-01-01T01:00:00Z", "qualifiers": {"scope": "s1"},
                        "assessment": {"evidence_class": "observed", "status": "contested", "confidence": 0.4,
                                       "basis": {"support": [{"observation": "old", "quote": "Aster keeps state."}]}}}],
            "threads": [{"id": "thread_a", "label": "Aster", "observed_at": "2026-01-01T00:00:00Z"},
                        {"id": "thread_b", "label": "Buffer", "observed_at": "2026-01-01T00:00:00Z"},
                        {"id": "thread_future", "label": "Later anchor", "observed_at": "2026-01-04T00:00:00Z"}],
            "assignments": [], "opaque_original": {"keep": [1, "all fields"]}}


def span(snap, ident, quote, observation="current"):
    text = next(item["text"] for item in snap["observations"] if item["id"] == observation)
    return {"id": ident, "observation": observation, "byte_start": len(text[:text.index(quote)].encode("utf-8")),
            "byte_len": len(quote.encode("utf-8")), "quote": quote}


def prepare(snap=None):
    snap = snapshot() if snap is None else snap
    spans = [span(snap, "early", "It waits."), span(snap, "middle", "Żaba returns."), span(snap, "late", "Beacon arrives.")]
    return prepare_context(snap, spans, [{"claim_id": "claim_a", "selection_reason": "explicit source continuity"}], time_cut=CUT)


def delta(packet, ident="delta_1"):
    return {"id": ident, "base_hash": packet["base_hash"], "packet_hash": packet["packet_hash"], "additions": [], "threads": [], "links": [], "references": []}


def reference(ident="ref_early", status="unresolved", alternatives=None, selected=None, evidence=None):
    return {"id": ident, "span_id": "early", "status": status, "alternatives": alternatives or [],
            "selected": selected, "evidence_span_ids": evidence or []}


def thread_link(ident, kind="thread_membership", target="thread_a"):
    return {"id": ident, "type": kind, "source": {"kind": "span", "id": "middle"},
            "target": {"kind": "thread", "id": target}, "current_span_ids": ["middle"],
            "existing_claim_ids": [], "reason": "candidate local continuation"}


class ContextDeltaTests(unittest.TestCase):
    def test_common_packet_preserves_assessment_status_groups_and_selection_reason(self):
        snap = snapshot()
        report = prepare(snap)
        self.assertEqual(report["status"], "ready")
        packet = report["packet"]
        self.assertEqual(packet["schema"], "loom.source_packet/1")
        self.assertEqual(packet["claims"], snap["claims"])
        context = packet["context_claims"][0]
        self.assertEqual(context["assessment"], snap["claims"][0]["assessment"])
        self.assertEqual(context["status"], "contested")
        self.assertEqual(context["source_groups"], ["source_a"])
        self.assertEqual(context["selection_reason"], "explicit source continuity")
        self.assertEqual(context["observation_dependencies"][0]["observation"], "old")
        self.assertEqual({item["observation"] for item in packet["current_spans"]}, {"current"})
        self.assertNotIn("future", {item["id"] for item in packet["observations"]})
        self.assertNotIn("thread_future", {item["id"] for item in packet["context_threads"]})

    def test_utf8_spans_are_exact_and_failure_retains_submitted_input(self):
        snap = snapshot()
        good = span(snap, "unicode", "Żaba returns.")
        self.assertEqual(prepare_context(snap, [good], [], time_cut=CUT)["status"], "ready")
        bad = {**good, "byte_len": len(good["quote"])}
        report = prepare_context(snap, [bad], [], time_cut=CUT)
        self.assertEqual(report["status"], "invalid")
        self.assertEqual(report["retained_input"]["current_spans"], [bad])
        self.assertIn("quote_does_not_match", report["errors"][0]["reason"])

    def test_future_current_observations_and_old_claim_support_are_refused(self):
        snap = snapshot()
        report = prepare_context(snap, [span(snap, "f", "Future identity.", "future")], [], time_cut=CUT)
        self.assertEqual(report["status"], "invalid")
        snap["claims"][0]["assessment"]["basis"]["support"][0]["observation"] = "future"
        report = prepare(snap)
        self.assertEqual(report["status"], "invalid")
        self.assertIn("future_claim_support_refused", {e["reason"] for e in report["errors"]})

    def test_unknown_time_stays_unknown_and_created_is_not_observation_time(self):
        snap = snapshot()
        del snap["observations"][1]["observed_at"]
        snap["observations"][1]["created"] = "2020-01-01T00:00:00Z"
        report = prepare(snap)
        self.assertEqual(report["status"], "ready")
        self.assertTrue(all(s["temporal"]["status"] == "unknown" for s in report["packet"]["current_spans"]))
        self.assertTrue(report["warnings"])
        proposed = delta(report["packet"])
        proposed["references"] = [reference()]
        self.assertEqual(validate_delta(report["packet"], proposed)["status"], "valid")
        proposed["references"] = [reference(status="anchored", alternatives=[{"kind": "thread", "id": "thread_a"}],
                                             selected={"kind": "thread", "id": "thread_a"}, evidence=["early"])]
        self.assertEqual(validate_delta(report["packet"], proposed)["status"], "invalid")

    def test_unknown_pronoun_allows_multiple_alternatives_or_none_without_selection(self):
        packet = prepare()["packet"]
        for ref in [reference(), reference(status="ambiguous", alternatives=[{"kind": "thread", "id": "thread_a"}, {"kind": "thread", "id": "thread_b"}])]:
            proposed = delta(packet)
            proposed["references"] = [ref]
            self.assertEqual(validate_delta(packet, proposed)["status"], "valid")
        proposed["references"][0]["selected"] = {"kind": "thread", "id": "thread_a"}
        self.assertEqual(validate_delta(packet, proposed)["status"], "invalid")

    def test_late_anchor_cannot_resolve_earlier_span_even_under_later_time_cut(self):
        packet = prepare()["packet"]
        proposed = delta(packet)
        proposed["references"] = [reference(status="anchored", alternatives=[{"kind": "thread", "id": "thread_a"}],
                                             selected={"kind": "thread", "id": "thread_a"}, evidence=["late"])]
        report = validate_delta(packet, proposed)
        self.assertEqual(report["status"], "invalid")
        self.assertIn("later_or_temporally_unknown_anchor", report["errors"][0]["reason"])

    def test_claim_support_availability_must_precede_the_reference_not_just_cut(self):
        for stamp in ["2026-01-02T12:00:00Z", None]:
            snap = snapshot()
            snap["observations"][0]["observed_at"] = stamp
            packet = prepare(snap)["packet"]
            proposed = delta(packet)
            proposed["references"] = [reference(status="anchored", alternatives=[{"kind": "claim", "id": "claim_a"}],
                                                 selected={"kind": "claim", "id": "claim_a"}, evidence=["early"])]
            self.assertEqual(validate_delta(packet, proposed)["status"], "invalid")

    def test_later_evidence_cannot_be_attached_to_earlier_comparison_source(self):
        packet = prepare()["packet"]
        for kind in ["correction", "contradiction", "analogy"]:
            proposed = delta(packet)
            proposed["links"] = [{"id": "late_comparison", "type": kind,
                                   "source": {"kind": "span", "id": "early"},
                                   "target": {"kind": "claim", "id": "claim_a"},
                                   "current_span_ids": ["late"], "existing_claim_ids": ["claim_a"], "reason": "candidate comparison"}]
            self.assertEqual(validate_delta(packet, proposed)["status"], "invalid")
            proposed["links"][0]["current_span_ids"] = ["early", "late"]
            self.assertEqual(validate_delta(packet, proposed)["status"], "invalid")
            proposed["links"][0]["current_span_ids"] = ["early"]
            self.assertEqual(validate_delta(packet, proposed)["status"], "valid")

    def test_overlapping_topics_and_all_link_types_remain_distinct(self):
        snap, packet = snapshot(), prepare()["packet"]
        proposed = delta(packet)
        proposed["links"] = [thread_link("m1"), thread_link("m2", target="thread_b"), thread_link("c1", "continuation"), thread_link("r1", "return")]
        proposed["additions"] = [{"id": "draft_a", "draft": {"subject": "entity_a", "predicate": "preserves", "value": "scope"},
                                  "current_span_ids": ["middle"], "existing_claim_ids": ["claim_a"]}]
        for kind in ["correction", "contradiction", "analogy"]:
            proposed["links"].append({"id": kind, "type": kind, "source": {"kind": "addition", "id": "draft_a"},
                                       "target": {"kind": "claim", "id": "claim_a"}, "current_span_ids": ["middle"],
                                       "existing_claim_ids": ["claim_a"], "reason": "separate candidate relationship"})
        outcome = apply_view(snap, packet, proposed)
        self.assertEqual(outcome["status"], "applied")
        self.assertEqual(len(outcome["view"]["overlay"]["links"]), 7)
        self.assertEqual(outcome["view"]["snapshot"], snap)
        self.assertEqual(outcome["view"]["snapshot"]["claims"][0]["assessment"]["status"], "contested")
        self.assertFalse(outcome["view"]["persistable_claim"])

    def test_apply_undo_restores_original_bytes_canonical_json_and_source_refs(self):
        snap = snapshot()
        original = (json.dumps(snap, ensure_ascii=False, indent=3) + "\n").encode("utf-8")
        spans = [span(snap, "middle", "Żaba returns.")]
        packet = prepare_context(original, spans, [{"claim_id": "claim_a", "selection_reason": "compare old context"}], time_cut=CUT)["packet"]
        proposed = delta(packet)
        proposed["links"] = [thread_link("m1")]
        view = apply_view(original, packet, proposed)["view"]
        undone = undo_view(view)
        self.assertEqual(base64.b64decode(undone["snapshot_bytes_b64"]), original)
        self.assertEqual(undone["canonical_json"], canonical(snap))
        self.assertEqual(undone["snapshot"], snap)
        self.assertEqual(undone["source_refs"][0]["current_spans"][0]["quote"], "Żaba returns.")
        self.assertEqual(undone["source_refs"][0]["context_claims"][0]["claim_id"], "claim_a")

    def test_idempotence_and_conflicting_delta_and_record_ids_do_not_overwrite(self):
        snap, packet = snapshot(), prepare()["packet"]
        proposed = delta(packet)
        proposed["links"] = [thread_link("m1")]
        first = apply_view(snap, packet, proposed)["view"]
        again = apply_view(snap, packet, proposed, previous_view=first)
        self.assertEqual(again["status"], "unchanged")
        self.assertEqual(first, again["view"])
        conflict = deepcopy(proposed)
        conflict["links"][0]["target"]["id"] = "thread_b"
        failed = apply_view(snap, packet, conflict, previous_view=first)
        self.assertEqual(failed["status"], "invalid")
        self.assertEqual(failed["view"], first)
        conflict["id"] = "delta_2"
        self.assertEqual(apply_view(snap, packet, conflict, previous_view=first)["status"], "invalid")

    def test_apply_refuses_ids_from_unselected_base_records(self):
        snap, packet = snapshot(), prepare()["packet"]
        proposed = delta(packet)
        proposed["additions"] = [{"id": "future", "draft": {"text": "new candidate"}, "current_span_ids": ["early"], "existing_claim_ids": []}]
        self.assertEqual(validate_delta(packet, proposed)["status"], "valid", "unselected base IDs are not disclosed in model packet")
        self.assertEqual(apply_view(snap, packet, proposed)["status"], "invalid", "base replay also prevents collisions outside selected context")

    def test_base_hash_and_original_packet_replay_prevent_rewriting(self):
        snap, packet = snapshot(), prepare()["packet"]
        proposed = delta(packet)
        changed = deepcopy(snap)
        changed["opaque_original"]["keep"] = []
        self.assertEqual(apply_view(changed, packet, proposed)["status"], "invalid")
        forged = deepcopy(packet)
        forged["context_claims"][0]["assessment"]["status"] = "active"
        forged["packet_hash"] = hashlib.sha256(canonical({k: v for k, v in forged.items() if k != "packet_hash"}).encode()).hexdigest()
        self.assertEqual(validate_delta(forged, proposed)["status"], "invalid")

    def test_delta_cannot_rebind_same_local_span_id_in_a_different_packet(self):
        snap = snapshot()
        a = prepare_context(snap, [span(snap, "same_id", "It waits.")], [], time_cut=CUT)["packet"]
        b = prepare_context(snap, [span(snap, "same_id", "Beacon arrives.")], [], time_cut=CUT)["packet"]
        proposed = delta(a)
        proposed["additions"] = [{"id": "draft_a", "draft": {"text": "about the earlier span"}, "current_span_ids": ["same_id"], "existing_claim_ids": []}]
        self.assertEqual(validate_delta(a, proposed)["status"], "valid")
        self.assertEqual(validate_delta(b, proposed)["status"], "invalid")
        self.assertEqual(a["base_hash"], b["base_hash"])
        self.assertNotEqual(a["packet_hash"], b["packet_hash"])

    def test_model_packet_excludes_undo_sidecar_and_future_source_bodies(self):
        snap = snapshot()
        snap["observations"][2]["text"] = "FUTURE_SENTINEL_SECRET_CONTEXT"
        report = prepare(snap)
        serialized = canonical(report["packet"])
        self.assertNotIn("FUTURE_SENTINEL_SECRET_CONTEXT", serialized)
        self.assertNotIn("Later anchor", serialized)
        self.assertNotIn("snapshot_bytes_b64", serialized)
        self.assertNotIn("canonical_json", serialized)
        self.assertIn("FUTURE_SENTINEL_SECRET_CONTEXT", base64.b64decode(report["audit"]["snapshot_bytes_b64"]).decode())

    def test_later_delta_cannot_rewrite_existing_reference_assignment(self):
        snap, packet = snapshot(), prepare()["packet"]
        first = delta(packet)
        first["references"] = [reference()]
        view = apply_view(snap, packet, first)["view"]
        later = delta(packet, "delta_2")
        later["references"] = [reference("other_id", status="ambiguous", alternatives=[{"kind": "thread", "id": "thread_a"}, {"kind": "thread", "id": "thread_b"}])]
        self.assertEqual(apply_view(snap, packet, later, previous_view=view)["status"], "invalid")
        new_spans = [span(snap, "renamed_early", "It waits.")]
        renamed_packet = prepare_context(snap, new_spans, [], time_cut=CUT)["packet"]
        renamed_delta = delta(renamed_packet, "renamed_delta")
        renamed_delta["references"] = [{**reference("renamed_ref"), "span_id": "renamed_early"}]
        self.assertEqual(apply_view(snap, renamed_packet, renamed_delta, previous_view=view)["status"], "invalid")
        snap["assignments"] = [{"span_id": "early", "status": "unresolved"}]
        packet = prepare(snap)["packet"]
        first["base_hash"] = packet["base_hash"]
        first["packet_hash"] = packet["packet_hash"]
        self.assertEqual(validate_delta(packet, first)["status"], "invalid")

    def test_invalid_and_destructive_requests_retain_input_and_do_not_mutate(self):
        snap, packet = snapshot(), prepare()["packet"]
        proposed = delta(packet)
        proposed["delete_claims"] = ["claim_a"]
        before = deepcopy((snap, packet, proposed))
        report = apply_view(snap, packet, proposed)
        self.assertEqual(report["status"], "invalid")
        self.assertEqual(report["retained_input"], proposed)
        self.assertEqual((snap, packet, proposed), before)
        proposed.pop("delete_claims")
        proposed["links"] = [thread_link("identity_merge", "identity_merge")]
        self.assertEqual(validate_delta(packet, proposed)["status"], "invalid")
        invalid = prepare_context(b'{"broken"', [], [], time_cut=CUT)
        self.assertEqual(base64.b64decode(invalid["retained_input"]["snapshot_bytes_b64"]), b'{"broken"')

    def test_new_threads_are_proposals_with_source_anchors_and_no_retroactive_membership(self):
        packet = prepare()["packet"]
        proposed = delta(packet)
        proposed["threads"] = [{"id": "new_topic", "label": "Beacon", "current_span_ids": ["late"]}]
        proposed["links"] = [thread_link("m1", target="new_topic")]
        self.assertEqual(validate_delta(packet, proposed)["status"], "invalid")
        proposed["links"][0]["source"]["id"] = "late"
        proposed["links"][0]["current_span_ids"] = ["late"]
        self.assertEqual(validate_delta(packet, proposed)["status"], "valid")

    def test_explicit_time_metadata_overrides_and_timecut_requires_timezone(self):
        snap = snapshot()
        snap["context_metadata"] = {"observation_times": {"current": "2026-01-04T00:00:00Z"}}
        self.assertEqual(prepare(snap)["status"], "invalid")
        self.assertEqual(prepare_context(snapshot(), [], [], time_cut="2026-01-03")["status"], "invalid")


if __name__ == "__main__":
    unittest.main()
