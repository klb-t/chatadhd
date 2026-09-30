#!/usr/bin/env python3
"""Counterexamples against current pure research operations; no fixture reads."""
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from loom.tools.structure import graph_panel_live as panel
from loom.tools.structure import new_graph_direct_lookup as direct
from loom.tools.structure import graph_source_commitment_projection as projection
from loom.tools.structure import graph_formal_paths as formal

T1 = "2026-09-01T10:00:00Z"
T2 = "2026-09-02T10:00:00Z"
T3 = "2026-09-03T10:00:00Z"


def canonical(x):
    return json.dumps(x, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def toy():
    return {"id": "philosophy-toy-01", "language": "PL", "source_id": "authored-watch-source",
            "turns": [{"id": "t1", "speaker": "Ada", "known_at": T1,
                       "text": "Jeśli A, to B. Żółć 🧪 jest tu osobnym tematem."},
                      {"id": "t2", "speaker": "Bo", "known_at": T2,
                       "text": "Ja, Bo, zaprzeczam, że A implikuje B."},
                      {"id": "t3", "speaker": "Ada", "known_at": T3,
                       "text": "Ja, Ada, wycofuję A do B i mówię teraz: B implikuje C."}],
            "node_inventory": [{"id": i, "text": i, "aliases": []}
                               for i in ("a", "b", "c", "not:a", "not:b")],
            "judgment_queries": []}


def edge(ident, source="a", target="b", *, actor="Ada", turn="t1", polarity="positive", quote=None):
    case = toy()
    row = next(t for t in case["turns"] if t["id"] == turn)
    return {"id": ident, "relation": "implies", "source": source, "target": target,
            "polarity": polarity, "attributed_to": actor, "known_at": row["known_at"],
            "evidence": [{"turn_id": turn, "quote": row["text"] if quote is None else quote}]}


def compile_graph(rows, events=()):
    return panel.compile_extraction({"source_assertions": list(rows), "status_events": list(events)}, toy())


def q(source="a", target="b", *, actor="Ada", cutoff=T3, scope="explicit_source"):
    return {"id": "authored-watch-query", "relation": "implies", "source": source, "target": target,
            "attributed_to": actor, "as_of": cutoff, "scope": scope}


def event(old="e1", new="e2", turn="t2"):
    row = next(t for t in toy()["turns"] if t["id"] == turn)
    return {"assertion_id": old, "status": "superseded", "superseded_by": new,
            "known_at": row["known_at"], "evidence": [{"turn_id": turn, "quote": row["text"]}]}


CHECKS = []


def check(ident, kind="invariant"):
    def reg(fn):
        CHECKS.append((ident, kind, fn))
        return fn
    return reg


@check("raw_query_future_append_invariance")
def _():
    case = toy()
    before = panel.query_payload(case, q(cutoff=T1))
    extended = deepcopy(case)
    extended["turns"].append({"id": "future", "speaker": "Ada", "known_at": "2099-01-01T00:00:00Z",
                              "text": "Future explicit contradiction and new entity."})
    after = panel.query_payload(extended, q(cutoff=T1))
    assert canonical(before) == canonical(after)
    assert len(before["turns"]) == 1
    return {"equal_body": True, "supplied_turns": 1, "future_turns_supplied": 0}


@check("compiler_input_immutability")
def _():
    case = toy(); raw = {"source_assertions": [edge("e1")], "status_events": []}
    before = canonical({"case": case, "candidate": raw})
    result = panel.compile_extraction(raw, case)
    assert canonical({"case": case, "candidate": raw}) == before
    assert result["source_assertions"][0]["content_truth"] == "unverified"
    return {"input_mutated": False, "compiled_assertions": 1}


@check("utf8_source_locator")
def _():
    case = toy(); span = panel.locate_evidence({"turn_id": "t1", "quote": "Żółć 🧪"}, case)
    text = case["turns"][0]["text"]
    assert text[span["char_start"]:span["char_end"]] == span["quote"]
    assert text.encode()[span["byte_start"]:span["byte_end"]].decode() == span["quote"]
    assert span["byte_end"] - span["byte_start"] > span["char_end"] - span["char_start"]
    return span


@check("missing_edge_unknown")
def _():
    result = direct.lookup(toy(), compile_graph([]), q())
    assert result["label"] == "unknown" and result["content_truth"] == "unverified"
    return {"label": result["label"], "world_truth": result["content_truth"]}


@check("directed_edge_no_reverse")
def _():
    result = direct.lookup(toy(), compile_graph([edge("e1")]), q("b", "a"))
    assert result["label"] == "unknown"
    return {"label": result["label"]}


@check("source_time_is_not_instrument_availability")
def _():
    result = direct.lookup(toy(), compile_graph([edge("e1")]), q())
    assert result["causal_prefix_extraction_verified"] is False
    assert result["source_known_at_semantics"] == "supporting_source_turn_timestamp_not_model_claim_availability"
    assert result["source_binding_is_semantic_proof"] is False
    assert result["paths"][0]["provenance"]["typed_claim_semantically_verified"] is False
    assert result["no_graph_promotion"] is True
    return {k: result[k] for k in ("causal_prefix_extraction_verified", "source_known_at_semantics",
                                   "source_binding_is_semantic_proof", "no_graph_promotion")}


@check("exact_locator_can_bind_unsupported_reverse", "diagnostic")
def _():
    raw = edge("wrong-reverse", "b", "a", quote="Żółć 🧪")
    compiled = compile_graph([raw]); result = direct.lookup(toy(), compiled, q("b", "a"))
    assert len(compiled["source_assertions"]) == 1 and result["label"] == "supported"
    assert result["paths"][0]["provenance"]["typed_claim_semantically_verified"] is False
    return {"compiled": True, "lookup_label": result["label"], "quote_semantically_supports_edge": False,
            "meaning": "Exact locator is not semantic entailment certification; no model made this candidate."}


@check("full_turn_can_bind_unsupported_attribution", "diagnostic")
def _():
    compiled = compile_graph([edge("wrong-actor", actor="Invented actor")])
    result = direct.lookup(toy(), compiled, q(actor="Invented actor"))
    assert compiled["invalid_assertions"] == 0 and result["label"] == "supported"
    return {"compiled": True, "lookup_label": result["label"], "source_actor_supported": False,
            "meaning": "Compiler validates nonempty attribution, not semantic speaker resolution."}


@check("source_projection_preserves_original_candidates")
def _():
    compiled = compile_graph([edge("e1"), edge("e2", actor="Bo", turn="t2", polarity="negative")], [event()])
    before = canonical(compiled)
    view, audit = projection.project(compiled, toy())
    assert canonical(compiled) == before
    assert view["source_assertions"] == compiled["source_assertions"]
    assert audit["raw_candidate_events"] == compiled["status_events"]
    assert len(audit["withheld_events"]) == 1 and view["status_events"] == []
    result = direct.lookup(toy(), view, q())
    assert result["label"] == "supported"
    return {"original_assertions": len(compiled["source_assertions"]), "original_events": len(compiled["status_events"]),
            "withheld_events": 1, "raw_event_retained": True, "derived_label": result["label"]}


@check("same_source_supersession_changes_view_only")
def _():
    compiled = compile_graph([edge("e1"), edge("e2", "b", "c", turn="t3")], [event(turn="t3")])
    before = canonical(compiled); view, audit = projection.project(compiled, toy())
    result = direct.lookup(toy(), view, q())
    assert result["label"] == "unknown" and len(view["source_assertions"]) == 2
    assert canonical(compiled) == before and len(audit["raw_candidate_events"]) == 1
    return {"derived_label": result["label"], "old_assertion_deleted": False}


@check("withdrawal_abi_honesty")
def _():
    compiled = compile_graph([edge("e1")])
    compiled["status_events"] = [{"assertion_id": "e1", "status": "withdrawn", "superseded_by": None,
                                   "known_at": T2, "evidence": []}]
    result = direct.lookup(toy(), compiled, q())
    assert result["state"] == "unavailable" and result["label"] is None
    assert result["reason"] == "unsupported_withdrawal_only_abi"
    return {"state": result["state"], "reason": result["reason"], "invented_negative_assertion": False}


@check("formal_inference_class_and_all_alternatives")
def _():
    compiled = compile_graph([edge("ab", "a", "b"), edge("bc", "b", "c", turn="t3"), edge("ac", "a", "c", turn="t3")])
    before = canonical(compiled)
    result = formal.solve_paths(compiled["source_assertions"], [], q("a", "c", scope="formal_implication"),
                                premise_mode="model_predicted_source_assertion")
    assert result["basis_class"] == "inferred" and result["content_truth"] == "unverified"
    assert {tuple(p["premise_assertion_ids"]) for p in result["paths"]} == {("ab", "bc"), ("ac",)}
    assert canonical(compiled) == before and result["world_truth_accuracy"] is None
    assert "confidence" not in result
    return {"label": result["label"], "basis_class": result["basis_class"], "paths": 2,
            "world_truth_accuracy": None, "confidence_fabricated": False}


@check("formal_signed_nodes_no_contraposition")
def _():
    rows = compile_graph([edge("ab")])["source_assertions"]
    result = formal.solve_paths(rows, [], q("not:b", "not:a", scope="formal_implication"))
    assert result["label"] == "unknown"
    return {"contraposition_invented": False, "label": result["label"]}


@check("formal_extrapolation_rejected_as_premise")
def _():
    rows = compile_graph([edge("ab")])["source_assertions"]; rows[0]["basis_class"] = "extrapolated"
    try:
        formal.solve_paths(rows, [], q(scope="formal_implication"))
    except ValueError as exc:
        assert str(exc) == "premise_is_not_unverified_source_assertion"
        return {"rejected": True, "reason": str(exc)}
    raise AssertionError("Extrapolated row was accepted as an observed premise")


@check("formal_bound_is_incomplete_unavailable")
def _():
    rows = compile_graph([edge("ab", "a", "b"), edge("bc", "b", "c", turn="t3")])["source_assertions"]
    policy = formal.load_policy(); policy["max_path_length"] = 1
    result = formal.solve_paths(rows, [], q("a", "c", scope="formal_implication"), policy)
    assert result["state"] == "unavailable" and result["complete"] is False and result["label"] is None
    return {"state": result["state"], "complete": result["complete"], "reason": result["reason"]}


@check("formal_conflict_explicit_declared_policy")
def _():
    rows = compile_graph([edge("ab+"), edge("ab-", polarity="negative")])["source_assertions"]
    result = formal.solve_paths(rows, [], q(scope="formal_implication"))
    assert result["positive_negative_conflict_assertion_ids"] == ["ab+"]
    assert result["policy"]["conflict_policy"] == "retain_positive_with_diagnostic"
    assert result["content_truth"] == "unverified"
    return {"label": result["label"], "conflict_visible": True,
            "policy": result["policy"]["conflict_policy"], "world_truth": result["content_truth"]}


@check("past_formal_query_future_cross_actor_event", "diagnostic")
def _():
    past = compile_graph([edge("ab")])
    before = formal.solve_paths(past["source_assertions"], [], q(cutoff=T1, scope="formal_implication"))
    future = compile_graph([edge("ab"), edge("other", actor="Bo", turn="t2", polarity="negative")],
                           [event("ab", "other")])
    try:
        formal.solve_paths(future["source_assertions"], future["status_events"],
                           q(cutoff=T1, scope="formal_implication"))
    except ValueError as exc:
        assert str(exc) == "cross_speaker_supersession"
        return {"past_baseline_label": before["label"], "after_future_append": "exception",
                "reason": str(exc), "future_event_time": T2, "query_cutoff": T1,
                "meaning": "Whole-graph event validation precedes temporal selection; this is not a causal prefix-extracted comparison."}
    raise AssertionError("Expected whole-graph validation diagnostic did not occur; review new implementation")


def main():
    source_paths = [Path(panel.__file__), Path(direct.__file__), Path(projection.__file__), Path(formal.__file__),
                    HERE / "PROTOCOL_01.md", Path(__file__)]
    preflight = {"schema": "loom.philosophy_watch_mechanism_freeze/1",
                 "frozen_at_utc": datetime.now(timezone.utc).isoformat(),
                 "files_sha256": {str(p.relative_to(ROOT)): sha(p) for p in source_paths},
                 "check_inventory": [{"id": i, "kind": k} for i, k, _ in CHECKS],
                 "sealed_fixture_reads": 0, "paid_requests": 0, "canonical_writes": 0}
    freeze_path = HERE / "FREEZE_01.json"
    with freeze_path.open("x", encoding="utf-8") as f:
        json.dump(preflight, f, ensure_ascii=False, indent=2); f.write("\n")
    rows = []
    for ident, kind, fn in CHECKS:
        try:
            trace = fn(); state = "pass" if kind == "invariant" else "diagnostic_reproduced"
        except Exception as exc:
            trace = {"exception_type": type(exc).__name__, "exception_message": str(exc)}
            state = "failed"
        rows.append({"id": ident, "kind": kind, "state": state, "trace": trace})
    out = {"schema": "loom.philosophy_watch_mechanism_results/1",
           "finished_at_utc": datetime.now(timezone.utc).isoformat(),
           "freeze_sha256": sha(freeze_path), "planned_checks": len(rows),
           "invariant_pass": sum(r["state"] == "pass" for r in rows),
           "diagnostics_reproduced": sum(r["state"] == "diagnostic_reproduced" for r in rows),
           "failed": sum(r["state"] == "failed" for r in rows), "checks": rows,
           "measurement_scope": "authored mechanism counterexamples, not model quality"}
    with (HERE / "FIRST_MECHANISM_RESULTS.json").open("x", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2); f.write("\n")
    print(json.dumps({k: out[k] for k in ("planned_checks", "invariant_pass", "diagnostics_reproduced", "failed")}))
    return int(out["failed"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
