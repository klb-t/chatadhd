#!/usr/bin/env python3
"""Second native diagnostic: predicate-balanced assertion views, no gold fixtures."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
import hashlib
import json
from pathlib import Path
import time

try:
    from .assertion_view import project_assertions, restore_core_graph, assessment_dimensions, DEPENDENCY_EDGES
    from .core_projection import project_core
    from .core_snapshot import snapshot as read_snapshot
    from .graph_flow import _names
    from .graph_search import rank_candidates
    from .graph_patterns import discover_patterns
    from .native_graph_experiment import select_records, canonical
except ImportError:
    from assertion_view import project_assertions, restore_core_graph, assessment_dimensions, DEPENDENCY_EDGES
    from core_projection import project_core
    from core_snapshot import snapshot as read_snapshot
    from graph_flow import _names
    from graph_search import rank_candidates
    from graph_patterns import discover_patterns
    from native_graph_experiment import select_records, canonical

VERSION = "native-assertion-diagnostic/1"
DEPENDENCIES = ("native_assertion_experiment.py", "assertion_view.py", "core_projection.py", "core_snapshot.py",
                "graph_flow.py", "graph_search.py", "graph_patterns.py", "structure_methods.py", "native_graph_experiment.py")


def hashes():
    root = Path(__file__).parent
    return {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in DEPENDENCIES}


def choose_records(source, maximum, context_limit):
    """Balance seed predicates then units. Grouping context is explicitly assumed."""
    all_specs, baseline = select_records(source, len(source["claims"]))
    claims = {c["id"]: c for c in source["claims"]}
    info = {row["claim_ids"][0]: row["scope"] for row in all_specs}
    eligible = set(info)
    buckets = defaultdict(lambda: defaultdict(deque))
    cooccurrence = defaultdict(list)
    for ident in sorted(eligible):
        unit = info[ident]["primary_unit"] or "<unknown>"
        buckets[claims[ident]["predicate"]][unit].append(ident)
        if unit != "<unknown>":
            cooccurrence[(unit, claims[ident]["subject"])].append(ident)
    queues = {}
    for predicate, units in sorted(buckets.items()):
        queue = deque()
        while any(units.values()):
            for unit in sorted(units):
                if units[unit]:
                    queue.append(units[unit].popleft())
        queues[predicate] = queue
    selected = []
    while len(selected) < maximum and any(queues.values()):
        for predicate in sorted(queues):
            if queues[predicate] and len(selected) < maximum:
                selected.append(queues[predicate].popleft())
    records, omissions = [], Counter()
    for seed in selected:
        c = claims[seed]
        dependency_rows = []
        for field in ("premises", "counter", "consequences"):
            for target in c["assessment"].get(field, {}).get("claims", []):
                dependency_rows.append({"relation": field + ":claims", "target_claim_id": target,
                                        "body_exported": target in claims, "eligible_for_selection": target in eligible})
        dependency_ids = sorted({row["target_claim_id"] for row in dependency_rows if row["eligible_for_selection"] and row["target_claim_id"] != seed})
        # Distinct predicate context first; same-unit/same-subject membership is
        # recorded as cooccurrence metadata only. No graph edge is invented.
        group_key = (info[seed]["primary_unit"], c["subject"])
        contextual_ids = sorted((ident for ident in cooccurrence.get(group_key, []) if ident != seed and ident not in dependency_ids),
                                key=lambda ident: (claims[ident]["predicate"] == c["predicate"], claims[ident]["predicate"], ident))
        chosen_dependencies = dependency_ids[:context_limit]
        chosen_context = contextual_ids[:max(0, context_limit - len(chosen_dependencies))]
        omissions["eligible_dependency_context_omitted_by_limit"] += len(dependency_ids) - len(chosen_dependencies)
        omissions["cooccurrence_context_omitted_by_limit"] += len(contextual_ids) - len(chosen_context)
        omissions["dependency_targets_ineligible_or_missing"] += sum(not row["eligible_for_selection"] for row in dependency_rows)
        chosen = [seed] + chosen_dependencies + chosen_context
        units = sorted({u for ident in chosen for u in info[ident]["unit_ids"]})
        records.append({"id": "assertion:" + seed, "seed_claim_id": seed, "seed_predicate": c["predicate"],
                        "claim_ids": chosen, "source_group": "unit:" + info[seed]["primary_unit"] if info[seed]["primary_unit"] else None,
                        "domain": None,
                        "context": {"explicit_dependency_references": dependency_rows,
                                    "selected_dependency_claim_ids": chosen_dependencies,
                                    "selected_cooccurrence_claim_ids": chosen_context,
                                    "cooccurrence_candidate_count": len(contextual_ids),
                                    "dependency_candidate_count": len(dependency_ids),
                                    "basis": "same_primary_support_unit_and_subject_only; not_an_argument_or_verified_topic",
                                    "unit_ids": units, "cross_unit_dependency_context": len(units) > 1,
                                    "source_ids": sorted({sid for ident in chosen for sid in info[ident]["source_ids"]}),
                                    "direct_support_observation_ids": sorted({ob for ident in chosen for ob in info[ident]["observation_ids"]}),
                                    "source_independence_verified": False}})
    dependencies_all, dependencies_eligible = Counter(), Counter()
    for ident, c in sorted(claims.items()):
        for field in ("premises", "counter", "consequences"):
            count = len(c["assessment"].get(field, {}).get("claims", []))
            dependencies_all[field + ":claims"] += count
            if ident in eligible:
                dependencies_eligible[field + ":claims"] += count
    selected_claims = {ident for row in records for ident in row["claim_ids"]}
    return records, {"all_core_claims": len(claims), "eligible_observed_active_claims": len(eligible),
                     "source_evidence_status_counts": baseline["source_evidence_status_counts"],
                     "excluded_by_reason": baseline["excluded_by_reason"],
                     "eligible_predicate_counts": baseline["eligible_predicate_counts"],
                     "seed_predicate_counts": dict(sorted(Counter(claims[ident]["predicate"] for ident in selected).items())),
                     "seed_unit_counts": dict(sorted(Counter(info[ident]["primary_unit"] or "<unknown>" for ident in selected).items())),
                     "selected_records": len(records), "selected_unique_claims_with_context": len(selected_claims),
                     "eligible_seeds_omitted_by_record_budget": len(eligible) - len(selected),
                     "eligible_claims_omitted_from_all_views": len(eligible - selected_claims),
                     "selected_root_occurrences": sum(len(row["claim_ids"]) for row in records),
                     "duplicate_claim_set_record_count": len(records) - len({tuple(sorted(row["claim_ids"])) for row in records}),
                     "context_omissions_with_repeats": dict(sorted(omissions.items())),
                     "all_source_explicit_claim_relation_counts": dict(sorted(dependencies_all.items())),
                     "eligible_explicit_claim_relation_counts": dict(sorted(dependencies_eligible.items())),
                     "selection": "round_robin_predicate_then_resolved_primary_support_unit_then_ascending_claim_ID",
                     "context_limit_per_seed": context_limit, "maximum_records": maximum,
                     "same_subject_context_is_argument": False, "source_independence_verified": False,
                     "unknown_eligible_support": baseline["unknown_eligible_support"]}


def one_mode(source, records, mode, literal_policy, state_budget, top_k):
    started = time.monotonic()
    views, graphs, names, rows = {}, {}, {}, []
    unknowns = Counter()
    for record in records:
        ident = record["id"]
        core = project_core(source, mode=mode, claim_ids=record["claim_ids"])
        view = project_assertions(core, literal_policy=literal_policy)
        # Mandatory diagnostic integrity check, not a semantic validation test.
        restore_core_graph(view)
        graphs[ident], names[ident] = view["structure"], _names(view["structure"])
        unknowns.update(row["reason"] for row in view["unknowns"])
        old_observations = {node["id"] for node in core["structure"]["nodes"] if node["kind"] == "core_observation"}
        new_observations = {node["id"] for node in view["structure"]["nodes"] if node["kind"] == "core_observation"}
        old_observation_ids = {core["reference_map"][node]["reference"]["id"] for node in old_observations}
        direct = set(record["context"]["direct_support_observation_ids"])
        rows.append({"record_id": ident, **view["counts"], "removed_nodes": len(view["sidecar"]["removed_nodes"]),
                     "removed_edges": len(view["sidecar"]["removed_edges"]), "loss_entries": len(view["loss_map"]),
                     "source_contracts": {key: value["source_contract"] for key, value in view["source_gates"].items()},
                     "source_observation_references": len(old_observations), "assertion_observation_ports": len(new_observations),
                     "source_extra_observations_beyond_direct_support": len(old_observation_ids - direct),
                     "restoration_verified": True})
        # Retain the small assessment report input. Full source/sidecars remain
        # reproducible from snapshot + record IDs, not copied into diagnostic JSON.
        views[ident] = {key: view[key] for key in ("claim_node_ids", "source_assessments", "source_gates")}
    projection_seconds = time.monotonic() - started
    retrieval_start = time.monotonic()
    filters, statuses = Counter(), Counter()
    pairs = survivors = omitted = states = 0
    overlap_matches = disjoint_matches = cross_group_matches = 0
    witnesses, specs = [], {row["id"]: row for row in records}
    goal = "structural_analogy" if mode == "structural" else "literal_semantic"
    for ident in sorted(graphs):
        candidates = []
        for other in sorted(graphs):
            if other == ident:
                continue
            candidate = {"id": other, "graph": graphs[other]}
            if mode == "structural":
                candidate["label_projection"] = {"pattern": names[ident], "host": names[other]}
            candidates.append(candidate)
        result = rank_candidates(graphs[ident], candidates, goal=goal, limit=top_k, verify_budget=state_budget)
        pairs += result["input_candidates"]
        survivors += result["filter_survivors"]
        omitted += result["omitted_by_top_k"]
        filters.update(row["reason"] for row in result["filtered_out"])
        for item in result["results"]:
            verification = item["verification"]
            statuses[verification["status"]] += 1
            states += verification.get("states_explored", 0)
            if verification.get("matched") is True:
                other = item["id"]
                overlap = sorted(set(specs[ident]["claim_ids"]) & set(specs[other]["claim_ids"]))
                overlap_matches += bool(overlap)
                disjoint_matches += not overlap
                cross_group_matches += specs[ident]["source_group"] != specs[other]["source_group"]
                if len(witnesses) < 8:
                    witnesses.append({"query_record_id": ident, "host_record_id": other, "overlapping_claim_ids": overlap,
                                      "same_declared_unit_group": specs[ident]["source_group"] == specs[other]["source_group"],
                                      "scores": item["scores"], "node_mapping_count": len(verification["node_mapping"]),
                                      "assessment_dimensions": assessment_dimensions(views[ident], views[other], verification["node_mapping"])})
    retrieval_seconds = time.monotonic() - retrieval_start
    motif_start = time.monotonic()
    motif_parameters = {"radii": [1, 2], "roots": ["node"], "max_enumerations": 1536,
                        "max_nodes": 24, "min_nodes": 2, "min_edges": 1, "alignment_budget": 500,
                        "max_verifications": 2000, "min_occurrences": 2}
    motifs = discover_patterns([{"id": row["id"], "structure": graphs[row["id"]], "source_group": row["source_group"], "domain": None} for row in records], **motif_parameters)
    motif_counts, motif_rows = Counter(), []
    for item in motifs["candidates"]:
        graph = item["representative"]
        claim_count = sum(node["kind"] == "core_claim" for node in graph["nodes"])
        labels = {edge["predicate"] for edge in graph["edges"]}
        has_dependency = bool(labels & DEPENDENCY_EDGES)
        category = "no_selected_claim_roots" if claim_count == 0 else "one_selected_claim_root" if claim_count == 1 else "multiple_claims_with_explicit_dependency" if has_dependency else "multiple_claims_without_explicit_dependency"
        motif_counts[category] += 1
        if claim_count >= 2 and len(motif_rows) < 12:
            motif_rows.append({"id": item["id"], "selected_claim_roots": claim_count, "classification": category,
                               "edge_predicates": sorted(labels), "source_claim_ids": item["source_claim_ids"],
                               "support": item["support"], "specificity": item["specificity"]})
    motif_seconds = time.monotonic() - motif_start
    return {"mode": mode, "literal_policy": literal_policy, "shape_goal": goal,
            "comparison_admissible": literal_policy == "exact_literals",
            "projection_records": rows, "unknown_reasons_across_views": dict(sorted(unknowns.items())),
            "retrieval": {"candidate_pairs": pairs, "safe_filter_survivors": survivors, "safe_filter_rejections_by_reason": dict(sorted(filters.items())),
                          "omitted_by_top_k": omitted, "verification_status_counts": dict(sorted(statuses.items())),
                          "verification_states_explored": states, "matched_pairs_with_overlapping_selected_claims": overlap_matches,
                          "matched_pairs_with_disjoint_selected_claims": disjoint_matches, "matched_pairs_across_declared_unit_groups": cross_group_matches,
                          "witness_sample_limit": 8, "witness_sample": witnesses, "semantic_accuracy": None},
            "motifs": {"parameters": motifs["parameters"], "statistics": motifs["statistics"], "coverage": motifs["coverage"],
                       "comparison": motifs["comparison"], "classification_counts": dict(sorted(motif_counts.items())),
                       "multiple_claim_candidate_sample": motif_rows, "sample_limit": 12,
                       "renaming": "none; exact names in explicit assertion view", "semantic_accuracy": None},
            "timing_seconds": {"projection_and_roundtrip": round(projection_seconds, 6), "retrieval": round(retrieval_seconds, 6),
                               "motifs": round(motif_seconds, 6), "total": round(time.monotonic() - started, 6)},
            "semantics": {"source_assessments_unchanged": True, "aggregate_confidence": None, "match_is_inference": False,
                          "literal_control_can_establish_analogy_or_entailment": False, "unit_groups_independent": False}}


def run_experiment(source, *, record_budget=24, context_limit=3, state_budget=1000, top_k=3):
    for name, value, low, high in (("record_budget", record_budget, 1, 128), ("context_limit", context_limit, 0, 8),
                                   ("state_budget", state_budget, 0, 100000), ("top_k", top_k, 0, 128)):
        if type(value) is not int or not low <= value <= high:
            raise ValueError(name + " outside allowed bounds")
    before = hashes()
    records, selection = choose_records(source, record_budget, context_limit)
    modes = {}
    for mode, policy in (("semantic", "exact_literals"), ("structural", "exact_literals"), ("structural", "literal_type_control")):
        modes[mode + ":" + policy] = one_mode(source, records, mode, policy, state_budget, top_k)
    if before != hashes():
        raise RuntimeError("experiment dependencies changed during run")
    return {"version": VERSION, "run_id": source["run_id"], "source_snapshot_sha256": hashlib.sha256(canonical(source).encode()).hexdigest(),
            "source_snapshot_body_sha256": source.get("snapshot", {}).get("body_sha256"), "dependency_sha256": before,
            "parameters": {"record_budget": record_budget, "context_limit": context_limit, "state_budget_per_pair": state_budget, "top_k": top_k},
            "selection": selection, "record_specs": records, "modes": modes,
            "coverage_limits": ["observed-active sampled roots only; other records remain in source snapshot",
                                "cooccurrence is source grouping, never an argument edge",
                                "record overlaps and omitted contexts are counted; no source independence inferred",
                                "motifs use exact lexical names; retrieval explicitly permits consistent renaming only in structural mode",
                                "literal_type_control is an unsafe ablation, not evidence of analogy or entailment",
                                "no independent gold labels, domain labels or accuracy claims"],
            "graph_mutations_applied": 0, "claims_promoted": 0, "native_build_performed": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--database", type=Path)
    inputs.add_argument("--snapshot", type=Path)
    parser.add_argument("--run", required=True)
    parser.add_argument("--record-budget", type=int, default=24)
    parser.add_argument("--context-limit", type=int, default=3)
    parser.add_argument("--state-budget", type=int, default=1000)
    parser.add_argument("--top-k", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = read_snapshot(args.database, args.run) if args.database else json.loads(args.snapshot.read_text(encoding="utf-8"))
    if source["run_id"] != args.run:
        parser.error("snapshot run_id differs from explicit --run")
    report = run_experiment(source, record_budget=args.record_budget, context_limit=args.context_limit,
                            state_budget=args.state_budget, top_k=args.top_k)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")


if __name__ == "__main__":
    main()
