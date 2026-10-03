#!/usr/bin/env python3
"""Replay grounded candidate strategies, then compare explicit graph views.

Offline orchestration only: no provider request, canonical write or promotion.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path

try:
    from .candidate_graph import compile_bundle
    from .grounded_frames import compile_frames
    from .context_delta import prepare_context, apply_view
    from .graph_search import rank_candidates
except ImportError:
    from candidate_graph import compile_bundle
    from grounded_frames import compile_frames
    from context_delta import prepare_context, apply_view
    from graph_search import rank_candidates

VERSION = "candidate-replay-flow/1"
LOCAL_KINDS = {"expression_occurrence", "term_occurrence", "binder", "scope"}


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def digest(value):
    return hashlib.sha256(canonical(value).encode("utf-8")).hexdigest()


def comparison_view(compiled, mode):
    """Declare lexical losses; preserve actual shared vertices and binding edges.

    Local display labels and printed symbols are not asserted identity in the
    candidate contract. Their repetition must not create an identity constraint.
    Existing canonical identity is retained exactly in both modes.
    """
    if mode not in {"exact", "occurrence_shape"}:
        raise ValueError("mode must be exact or occurrence_shape")
    graph, losses = deepcopy(compiled["graph"]), []
    if mode == "occurrence_shape":
        for node in graph["nodes"]:
            if node["kind"] not in LOCAL_KINDS:
                continue
            for parent, key, path in ((node, "label", "label"),
                                      (node["qualifiers"], "symbol", "qualifiers.symbol")):
                if key in parent:
                    losses.append({"node_id": node["id"], "path": path,
                                   "original": parent.pop(key),
                                   "reason": "source_notation_is_not_asserted_occurrence_identity"})
    return {"graph": graph, "mode": mode, "losses": losses,
            "source_graph_hash": digest(compiled["graph"]),
            "meaning": "typed_occurrence_shape_only" if mode != "exact" else "exact_supplied_labels",
            "identity_inferred_from_spelling": False,
            "existing_identity_renamed": False, "inference_eligible": False}


def _evidence(compiled):
    packet = compiled["retained_input"]["source_packet"]
    observations = {o["id"]: o for o in packet["observations"]}
    used, quote_hashes, prior = set(), set(), set()
    for entity in compiled["drafts"]["entities"]:
        for span in entity["support"]:
            used.add(span["observation"])
            quote_hashes.add(hashlib.sha256(span["quote"].encode("utf-8")).hexdigest())
    for claim in compiled["drafts"]["claims"]:
        prior.update(claim["assessment"]["premises"]["claims"])
        for span in claim["assessment"]["basis"]["support"]:
            used.add(span["observation"])
            quote_hashes.add(hashlib.sha256(span["quote"].encode("utf-8")).hexdigest())
    return {"observations": {i: {"text_hash": hashlib.sha256(observations[i]["text"].encode("utf-8")).hexdigest(),
                                 "locator_hash": digest(observations[i]["locator"])} for i in sorted(used)},
            "quote_hashes": sorted(quote_hashes), "prior_claim_ids": sorted(prior)}


def context_support_errors(compiled, packet):
    """Keep draft evidence inside current spans; old context is not new support."""
    intervals = {}
    for span in packet["current_spans"]:
        intervals.setdefault(span["observation"], []).append((span["byte_start"], span["byte_start"] + span["byte_len"]))
    merged = {}
    for observation, ranges in intervals.items():
        combined = []
        for start, stop in sorted(ranges):
            if combined and start <= combined[-1][1]:
                combined[-1] = (combined[-1][0], max(combined[-1][1], stop))
            else:
                combined.append((start, stop))
        merged[observation] = combined
    errors = []
    for collection in ("entities", "claims"):
        for draft in compiled["drafts"][collection]:
            spans = draft["support"] if collection == "entities" else draft["assessment"]["basis"]["support"]
            for span in spans:
                start, stop = span["byte_start"], span["byte_start"] + span["byte_len"]
                if not any(a <= start and stop <= b for a, b in merged.get(span["observation"], [])):
                    errors.append({"draft_id": draft["id"], "support": deepcopy(span),
                                   "reason": "draft_support_outside_selected_current_spans"})
    return errors


def evidence_overlap(left, right):
    a, b = left["evidence"], right["evidence"]
    shared = sorted(set(a["observations"]) & set(b["observations"]))
    conflicting = [i for i in shared if a["observations"][i] != b["observations"][i]]
    same_locators = sorted({(v["locator_hash"], v["text_hash"]) for v in a["observations"].values()} &
                           {(v["locator_hash"], v["text_hash"]) for v in b["observations"].values()})
    prior = sorted(set(a["prior_claim_ids"]) & set(b["prior_claim_ids"]))
    quotes = sorted(set(a["quote_hashes"]) & set(b["quote_hashes"]))
    groups = [left.get("source_group"), right.get("source_group")]
    return {"same_input_record": left["record_id"] == right["record_id"],
            "shared_observation_ids": shared, "conflicting_observation_ids": conflicting,
            "shared_located_text_hashes": [list(pair) for pair in same_locators],
            "shared_prior_claim_ids": prior, "repeated_quote_hashes": quotes,
            "declared_groups": groups,
            "declared_groups_differ": groups[0] != groups[1] if all(isinstance(g, str) and g for g in groups) else None,
            "known_overlap": bool(shared or same_locators or prior or left["record_id"] == right["record_id"]),
            "independent_recurrence": None,
            "interpretation": "overlap diagnostics; different IDs/groups and no detected overlap do not prove independence"}


def run_candidate_flow(records, query_ids, *, mode="exact", top_k=5,
                       state_budget=10000, max_records=32, max_graph_nodes=2048):
    """Compile replayed responses without choosing an interpretation alternative.

    Each record uses source_packet, or context preparation over an explicit base
    snapshot. In the latter case only its model-facing packet feeds A/B. The
    context overlay remains separate from the candidate graph and cannot promote
    it. Invalid context never falls back to an unfiltered source packet.
    """
    if mode not in {"exact", "occurrence_shape"}:
        raise ValueError("unsupported comparison mode")
    for name, value, low in (("top_k", top_k, 0), ("state_budget", state_budget, 0),
                            ("max_records", max_records, 1), ("max_graph_nodes", max_graph_nodes, 1)):
        if type(value) is not int or value < low:
            raise ValueError("invalid bound: " + name)
    if not isinstance(records, list) or not all(isinstance(r, dict) for r in records):
        raise ValueError("records must be objects")
    ids = [r.get("id") for r in records]
    if any(not isinstance(i, str) or not i for i in ids) or len(ids) != len(set(ids)):
        raise ValueError("record IDs must be nonempty and unique")
    if not isinstance(query_ids, list) or any(not isinstance(i, str) or i not in ids for i in query_ids) or len(query_ids) != len(set(query_ids)):
        raise ValueError("queries must be unique declared record IDs")
    selected = sorted(records, key=lambda r: r["id"])[:max_records]
    omissions = [{"record_id": i, "reason": "record_budget"} for i in sorted(set(ids) - {r["id"] for r in selected})]
    reports, variants = {}, {}
    for record in selected:
        ident = record["id"]
        report = reports[ident] = {"strategy": record.get("strategy"), "input_hash": digest(record)}
        if ("source_packet" in record) == ("context" in record):
            report.update(status="rejected", errors=["provide source_packet XOR context"])
            continue
        if "context" in record:
            context = record["context"]
            required = {"base_snapshot", "current_spans", "claim_refs", "time_cut"}
            if not isinstance(context, dict) or not required <= set(context) or set(context) - required - {"delta"}:
                report.update(status="context_rejected", errors=["context requires base_snapshot, current_spans, claim_refs, time_cut and optional delta"])
                continue
            prepared = prepare_context(context["base_snapshot"], context["current_spans"],
                                       context["claim_refs"], time_cut=context["time_cut"])
            report["context_preparation"] = prepared
            if prepared["status"] != "ready":
                report["status"] = "context_rejected"
                continue
            packet = prepared["packet"]
            if "delta" in context:
                applied = apply_view(context["base_snapshot"], packet, context["delta"])
                report["context_application"] = applied
                if applied["status"] not in {"applied", "unchanged"}:
                    report["status"] = "context_delta_rejected"
                    continue
        else:
            packet = record["source_packet"]
        if record.get("strategy") == "direct":
            compiled = compile_bundle(record.get("bundle"), packet)
            report["compilation"] = compiled
            alternatives = [{"id": "direct", "compiled": compiled}]
        elif record.get("strategy") == "frames":
            framed = compile_frames(packet, record.get("anchoring"), record.get("composition"))
            report["compilation"] = framed
            alternatives = framed["alternatives"]
        else:
            report.update(status="rejected", errors=["unknown strategy"])
            continue
        accepted = 0
        for alternative in alternatives:
            compiled = alternative["compiled"]
            key = canonical([ident, alternative["id"]])
            if not compiled["valid"]:
                omissions.append({"record_id": ident, "alternative": alternative["id"], "reason": "invalid_candidate"})
                continue
            if "context" in record:
                scope_errors = context_support_errors(compiled, packet)
                if scope_errors:
                    report.setdefault("context_support_rejections", []).append({"alternative": alternative["id"], "errors": scope_errors})
                    omissions.append({"record_id": ident, "alternative": alternative["id"], "reason": "support_outside_current_spans"})
                    continue
            if not compiled["graph"]["nodes"] or compiled["coverage"].get("representation_status") == "unrepresented":
                omissions.append({"record_id": ident, "alternative": alternative["id"], "reason": "valid_abstention"})
                continue
            if len(compiled["graph"]["nodes"]) > max_graph_nodes:
                omissions.append({"record_id": ident, "alternative": alternative["id"], "reason": "graph_node_budget"})
                continue
            variants[key] = {"id": key, "record_id": ident, "alternative": alternative["id"],
                             "view": comparison_view(compiled, mode), "evidence": _evidence(compiled),
                             "coverage": deepcopy(compiled["coverage"]),
                             "source_group": record.get("source_group"), "domain": record.get("domain")}
            accepted += 1
        report.update(status="compiled" if accepted else "not_searchable", searchable_alternatives=accepted,
                      selected_interpretation=None)
    retrieval = []
    for ident in query_ids:
        queries = [v for v in variants.values() if v["record_id"] == ident]
        if not queries:
            retrieval.append({"record_id": ident, "status": "not_searched", "result": None})
        for query in queries:
            candidates = [{"id": key, "graph": v["view"]["graph"]} for key, v in sorted(variants.items())
                          if v["record_id"] != ident]
            ranked = rank_candidates(query["view"]["graph"], candidates, goal="literal_semantic",
                                     limit=top_k, verify_budget=state_budget)
            for row in ranked["results"]:
                row["evidence_overlap"] = evidence_overlap(query, variants[row["id"]])
            retrieval.append({"record_id": ident, "variant_id": query["id"], "status": "searched", "result": ranked})
    return {"version": VERSION, "parameters": {"mode": mode, "top_k": top_k, "state_budget": state_budget,
             "max_records": max_records, "max_graph_nodes": max_graph_nodes},
            "records": reports, "variants": variants, "retrieval": retrieval, "omissions": omissions,
            "coverage": {"declared_records": len(records), "compiled_records": sum(r["status"] == "compiled" for r in reports.values()),
                         "searchable_variants": len(variants), "requested_queries": len(query_ids)},
            "semantics": {"provider_calls": 0, "canonical_mutations": 0, "claims_promoted": 0,
                          "inference_eligible": False, "semantic_accuracy": None,
                          "match_establishes_independent_recurrence": False,
                          "input_kind": "supplied_response_replay", "alternatives_merged": False}}


def main():
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("replay", type=Path, help="JSON records, query_ids and optional parameters")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = json.loads(args.replay.read_text(encoding="utf-8"))
    report = run_candidate_flow(source["records"], source["query_ids"], **source.get("parameters", {}))
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, allow_nan=False)


if __name__ == "__main__":
    main()
