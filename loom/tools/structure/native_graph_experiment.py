#!/usr/bin/env python3
"""Bounded, reproducible diagnostic over an explicit native core snapshot.

No benchmark fixtures, domains, source independence, or semantic accuracy labels
are supplied or inferred. This is a source-schema/projection/search diagnostic.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
import hashlib
import json
from pathlib import Path
import time

try:
    from .core_snapshot import snapshot as read_snapshot
    from .graph_flow import run_flow
except ImportError:
    from core_snapshot import snapshot as read_snapshot
    from graph_flow import run_flow

VERSION = "native-core-graph-diagnostic/1"
DEPENDENCIES = ("native_graph_experiment.py", "core_snapshot.py", "core_projection.py",
                "graph_flow.py", "graph_search.py", "graph_patterns.py", "structure_methods.py")


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def dependency_hashes():
    root = Path(__file__).parent
    return {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in DEPENDENCIES}


def select_records(source, maximum):
    """Balance declared primary support units, then subjects, without content labels."""
    observations = {item["id"]: item for item in source["observations"]}
    claims = {item["id"]: item for item in source["claims"]}
    buckets, eligible, excluded, support = defaultdict(lambda: defaultdict(list)), [], Counter(), {}
    source_classes = Counter()
    unknown_support = []
    for ident, claim in sorted(claims.items()):
        assessment = claim["assessment"]
        evidence, status = assessment.get("evidence_class"), assessment.get("status")
        source_classes[(evidence, status)] += 1
        if evidence != "observed":
            excluded["evidence_not_observed:" + str(evidence)] += 1
            continue
        if status != "active":
            excluded["status_not_active:" + str(status)] += 1
            continue
        eligible.append(ident)
        units, source_ids, observation_ids, missing = set(), set(), set(), []
        for reference in assessment.get("basis", {}).get("support", []):
            observation_id = reference.get("observation")
            if observation_id:
                observation_ids.add(observation_id)
            observation = observations.get(observation_id)
            if observation:
                if observation.get("unit"):
                    units.add(observation["unit"])
                if observation.get("locator", {}).get("source"):
                    source_ids.add(observation["locator"]["source"])
            else:
                missing.append(observation_id)
            if reference.get("locator", {}).get("source"):
                source_ids.add(reference["locator"]["source"])
        primary_unit = min(units) if units else None
        support[ident] = {"primary_unit": primary_unit, "unit_ids": sorted(units),
                          "source_ids": sorted(source_ids), "observation_ids": sorted(observation_ids),
                          "missing_observation_ids": missing, "subject_id": claim["subject"]}
        if missing or not units:
            unknown_support.append({"claim_id": ident, "missing_observation_ids": missing,
                                    "support_unit_missing": not units})
        buckets[primary_unit or "<unresolved-unit>"][claim["subject"]].append(ident)
    unit_queues = {}
    for unit, by_subject in sorted(buckets.items()):
        remaining = {subject: deque(sorted(values)) for subject, values in sorted(by_subject.items())}
        queue = deque()
        while any(remaining.values()):
            for subject in sorted(remaining):
                if remaining[subject]:
                    queue.append(remaining[subject].popleft())
        unit_queues[unit] = queue
    selected = []
    while len(selected) < maximum and any(unit_queues.values()):
        for unit in sorted(unit_queues):
            if unit_queues[unit] and len(selected) < maximum:
                selected.append(unit_queues[unit].popleft())
    records = []
    for ident in selected:
        info = support[ident]
        records.append({"id": "native:" + ident, "claim_ids": [ident],
                        "source_group": "unit:" + info["primary_unit"] if info["primary_unit"] else None,
                        "domain": None,
                        "scope": {"basis": "declared_grouping_for_diagnostic_not_verified_topic_scope",
                                  **info, "claim_qualifiers": claims[ident]["qualifiers"],
                                  "source_group_independence_verified": False}})
    unit_counts = []
    for unit in sorted(buckets):
        ids = [ident for values in buckets[unit].values() for ident in values]
        unit_counts.append({"unit_id": None if unit == "<unresolved-unit>" else unit,
                            "eligible_claims": len(ids), "sampled_claims": sum(ident in selected for ident in ids),
                            "eligible_subjects": len(buckets[unit]),
                            "source_ids": sorted({s for ident in ids for s in support[ident]["source_ids"]})})
    return records, {"all_core_claims": len(claims), "eligible_observed_active_claims": len(eligible),
                     "source_evidence_status_counts": [{"evidence_class": evidence, "status": status, "count": count}
                                                       for (evidence, status), count in sorted(source_classes.items())],
                     "excluded_by_reason": dict(sorted(excluded.items())),
                     "sampled_claims": len(selected), "eligible_claims_omitted_by_record_budget": len(eligible) - len(selected),
                     "eligible_predicate_counts": dict(sorted(Counter(claims[ident]["predicate"] for ident in eligible).items())),
                     "sampled_predicate_counts": dict(sorted(Counter(claims[ident]["predicate"] for ident in selected).items())),
                     "sampled_entity_object_claims": sum(bool(claims[ident]["object"]) for ident in selected),
                     "sampled_literal_value_claims": sum(claims[ident]["value"] is not None for ident in selected),
                     "sampled_distinct_literal_values": len({canonical(claims[ident]["value"]) for ident in selected if claims[ident]["value"] is not None}),
                     "sampled_direct_support_observations_distinct": len({ob for ident in selected for ob in support[ident]["observation_ids"]}),
                     "selection": "round_robin_primary_support_unit_then_subject; ascending_canonical_claim_ID_within_subject",
                     "primary_unit_rule": "smallest_resolved_unit_ID_when_claim_has_multiple_support_units",
                     "unit_groups": unit_counts, "unknown_eligible_support": unknown_support,
                     "source_domains_assigned": 0, "source_independence_verified": False}


def compact_flow(flow, seconds):
    filters, verifications, unknowns = Counter(), Counter(), Counter()
    survivor_count = candidate_count = omitted_top_k = states = 0
    witnesses, closures, retained_opaque = [], [], Counter()
    specs = {item["id"]: item for item in flow["record_specs"]}
    for query in flow["retrieval"]:
        result = query["result"]
        if result is None:
            verifications[query["status"]] += 1
            continue
        candidate_count += result["input_candidates"]
        survivor_count += result["filter_survivors"]
        omitted_top_k += result["omitted_by_top_k"]
        filters.update(item["reason"] for item in result["filtered_out"])
        for item in result["results"]:
            verification = item["verification"]
            verifications[verification["status"]] += 1
            states += verification.get("states_explored", 0)
            if verification.get("matched") is True and len(witnesses) < 8:
                witnesses.append({"query_record_id": query["query_id"], "host_record_id": item["id"],
                                  "node_mapping_count": len(verification["node_mapping"]),
                                  "edge_mapping_count": len(verification["edge_mapping"]),
                                  "lexical_renaming_count": len(verification["lexical_renaming"]),
                                  "scores": item["scores"], "states_explored": verification["states_explored"]})
    for ident, projection in sorted(flow["projections"].items()):
        observed = sorted({ref["reference"]["id"] for ref in projection["reference_map"].values()
                           if ref.get("reference", {}).get("collection") == "observations"})
        direct = set(specs[ident]["scope"]["observation_ids"])
        extra = sorted(set(observed) - direct)
        unknowns.update(item["reason"] for item in projection["unknowns"])
        retained_opaque.update(item["namespace"] for item in projection["unknowns"]
                               if item["reason"] == "optional_record_semantics_retained_opaque")
        closures.append({"record_id": ident, "claim_ids": projection["source_claim_ids"],
                         "nodes": projection["node_count"], "edges": projection["edge_count"],
                         "direct_support_observations": len(direct), "projected_observation_references": len(observed),
                         "extra_observation_reference_ids": extra,
                         "observation_closure_ratio": len(observed) / len(direct) if direct else None,
                         "loss_registry_entries": len(projection["dropped_attributes"]),
                         "source_contracts": {key: value["source_contract"] for key, value in projection["gates"].items()}})
    motifs = flow["motifs"]
    motif_rows = []
    for item in motifs["candidates"][:12] if motifs else []:
        support = item["support"]
        motif_rows.append({"id": item["id"], "source_claim_ids": item["source_claim_ids"],
                           "occurrence_count": support["occurrence_count"], "record_count": support["record_count"],
                           "declared_source_group_count": len(support["source_groups"]),
                           "source_groups": support["source_groups"], "domain_count": support["domain_count"],
                           "source_independence_verified": False,
                           "unknown_domain_record_ids": support["unknown_domain_record_ids"],
                           "specificity": item["specificity"],
                           "core_claim_vertices": sum(node["kind"] == "core_claim" for node in item["representative"]["nodes"]),
                           "edge_predicates": sorted({edge["predicate"] for edge in item["representative"]["edges"]})})
    return {"parameters": flow["parameters"], "coverage": flow["coverage"], "omissions": flow["omissions"],
            "elapsed_seconds": round(seconds, 6),
            "retrieval": {"candidate_pairs": candidate_count, "safe_filter_survivors": survivor_count,
                          "safe_filter_rejections_by_reason": dict(sorted(filters.items())),
                          "omitted_by_top_k": omitted_top_k, "verification_status_counts": dict(sorted(verifications.items())),
                          "verification_states_explored": states, "witness_sample": witnesses,
                          "witness_sample_limit": 8, "semantic_accuracy": None},
            "source_closure": {"records": closures,
                               "extra_observation_references_total_with_repeats": sum(len(row["extra_observation_reference_ids"]) for row in closures),
                               "records_with_extra_observation_references": sum(bool(row["extra_observation_reference_ids"]) for row in closures),
                               "maximum_observation_closure_ratio": max((row["observation_closure_ratio"] or 0 for row in closures), default=0),
                               "interpretation": "shared_entity_provenance_expansion_not_new_support_for_selected_claim"},
            "unknown_reasons_across_views": dict(sorted(unknowns.items())),
            "opaque_optional_record_namespaces": dict(sorted(retained_opaque.items())),
            "motifs": {"parameters": motifs["parameters"], "statistics": motifs["statistics"],
                       "coverage": motifs["coverage"], "comparison": motifs["comparison"],
                       "candidate_sample": motif_rows, "candidate_sample_limit": 12,
                       "motifs_with_core_claim_vertices": sum(any(node["kind"] == "core_claim" for node in item["representative"]["nodes"]) for item in motifs["candidates"]),
                       "motifs_with_multiple_core_claim_vertices": sum(sum(node["kind"] == "core_claim" for node in item["representative"]["nodes"]) > 1 for item in motifs["candidates"]),
                       "source_independence_verified": False, "semantic_accuracy": None} if motifs else None,
            "semantics": flow["semantics"]}


def run_experiment(source, *, record_budget=24, state_budget=1000, top_k=3):
    if type(record_budget) is not int or not 1 <= record_budget <= 128:
        raise ValueError("record_budget must be an integer in [1,128]")
    before = dependency_hashes()
    records, selection = select_records(source, record_budget)
    query_ids = [item["id"] for item in records]
    motif_parameters = {"radii": [1], "roots": ["node"], "max_enumerations": 512,
                        "max_nodes": 10, "min_nodes": 2, "min_edges": 1,
                        "alignment_budget": 500, "max_verifications": 1000, "min_occurrences": 2}
    modes = {}
    for mode in ("semantic", "structural"):
        start = time.monotonic()
        flow = run_flow(source, records, query_ids, mode=mode, top_k=top_k,
                        state_budget=state_budget, max_records=record_budget,
                        max_graph_nodes=2048, motif_parameters=motif_parameters)
        modes[mode] = compact_flow(flow, time.monotonic() - start)
    after = dependency_hashes()
    if before != after:
        raise RuntimeError("experiment dependencies changed during execution; refusing mixed-version report")
    return {"version": VERSION, "run_id": source["run_id"],
            "source_snapshot_body_sha256": source.get("snapshot", {}).get("body_sha256"),
            "source_snapshot_sha256": hashlib.sha256(canonical(source).encode()).hexdigest(),
            "source_snapshot_metadata": source.get("snapshot"),
            "dependency_sha256": before, "selection": selection, "record_specs": records,
            "modes": modes,
            "coverage_limits": ["sampled observed-active subset only; other source claims retained in snapshot",
                                "unit grouping is a declared grouping assumption, not verified independent support",
                                "no domain labels inferred; unknown domains remain unknown",
                                "bounded motif enumeration and search may be incomplete",
                                "source entity provenance may enlarge a selected claim view",
                                "no independent gold labels or semantic accuracy measure"],
            "graph_mutations_applied": 0, "claims_promoted": 0, "native_build_performed": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--database", type=Path)
    inputs.add_argument("--snapshot", type=Path)
    parser.add_argument("--run", required=True)
    parser.add_argument("--record-budget", type=int, default=24)
    parser.add_argument("--state-budget", type=int, default=1000)
    parser.add_argument("--top-k", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    start = time.monotonic()
    source = read_snapshot(args.database, args.run) if args.database else json.loads(args.snapshot.read_text(encoding="utf-8"))
    if source.get("run_id") != args.run:
        parser.error("snapshot run_id differs from explicit --run")
    input_seconds = time.monotonic() - start
    result = run_experiment(source, record_budget=args.record_budget, state_budget=args.state_budget, top_k=args.top_k)
    result["snapshot_load_seconds"] = round(input_seconds, 6)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")
    print(json.dumps({"output": str(args.output), "selected_claims": result["selection"]["sampled_claims"],
                      "mode_seconds": {mode: value["elapsed_seconds"] for mode, value in result["modes"].items()}}, sort_keys=True))


if __name__ == "__main__":
    main()
