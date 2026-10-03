#!/usr/bin/env python3
"""Relation-biased diagnostic of observed active/contested source Claims.

Recorded counter links are examined as source relations, not automatically
interpreted as logical contradiction or an atomic thought operation.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

try:
    from .native_assertion_experiment import one_mode, hashes as assertion_hashes
    from .native_graph_experiment import canonical
    from .core_snapshot import snapshot as read_snapshot
except ImportError:
    from native_assertion_experiment import one_mode, hashes as assertion_hashes
    from native_graph_experiment import canonical
    from core_snapshot import snapshot as read_snapshot

VERSION = "native-counter-diagnostic/1"
STATUS_POLICIES = {"active_only": {"active"}, "active_and_contested": {"active", "contested"}}


def hashes():
    return {**assertion_hashes(), Path(__file__).name: hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}


def choose_relation_records(source, *, maximum=24, context_limit=3, status_policy="active_and_contested"):
    if status_policy not in STATUS_POLICIES:
        raise ValueError("unknown status_policy")
    for name, value, upper in (("maximum", maximum, 128), ("context_limit", context_limit, 8)):
        if type(value) is not int or not 0 <= value <= upper:
            raise ValueError(name + " outside allowed bounds")
    statuses = STATUS_POLICIES[status_policy]
    claims = {row["id"]: row for row in source["claims"]}
    observations = {row["id"]: row for row in source["observations"]}
    eligible, excluded, classes = set(), Counter(), Counter()
    for ident, claim in claims.items():
        assessment = claim["assessment"]
        evidence, status = assessment.get("evidence_class"), assessment.get("status")
        classes[(evidence, status)] += 1
        if evidence != "observed":
            excluded["evidence_not_observed:" + str(evidence)] += 1
        elif status not in statuses:
            excluded["status_outside_policy:" + str(status)] += 1
        else:
            eligible.add(ident)

    def references(ident):
        result = []
        for field in ("premises", "counter", "consequences"):
            for target in claims[ident]["assessment"].get(field, {}).get("claims", []):
                result.append({"relation": field + ":claims", "target_claim_id": target,
                               "body_exported": target in claims, "eligible_for_selection": target in eligible})
        return result

    def support(ident):
        units, source_ids, observation_ids, missing = set(), set(), set(), []
        for reference in claims[ident]["assessment"].get("basis", {}).get("support", []):
            obs_id = reference.get("observation")
            if obs_id:
                observation_ids.add(obs_id)
            observation = observations.get(obs_id)
            if observation:
                if observation.get("unit"):
                    units.add(observation["unit"])
                if observation.get("locator", {}).get("source"):
                    source_ids.add(observation["locator"]["source"])
            else:
                missing.append(obs_id)
            if reference.get("locator", {}).get("source"):
                source_ids.add(reference["locator"]["source"])
        return {"unit_ids": sorted(units), "source_ids": sorted(source_ids), "observation_ids": sorted(observation_ids),
                "missing_observation_ids": missing, "primary_unit": min(units) if units else None}

    relation_seeds = sorted(ident for ident in eligible if references(ident))
    seeds = relation_seeds[:maximum]
    records, selected_refs, omissions = [], Counter(), Counter()
    for seed in seeds:
        refs = references(seed)
        context_candidates = sorted({row["target_claim_id"] for row in refs if row["eligible_for_selection"] and row["target_claim_id"] != seed})
        context = context_candidates[:context_limit]
        chosen = [seed] + context
        info = {ident: support(ident) for ident in chosen}
        units = sorted({unit for row in info.values() for unit in row["unit_ids"]})
        selected_refs.update(row["relation"] for row in refs)
        omissions["eligible_direct_reference_bodies_omitted_by_context_limit"] += len(context_candidates) - len(context)
        omissions["direct_reference_bodies_missing_or_ineligible"] += sum(not row["eligible_for_selection"] for row in refs)
        records.append({"id": "counter:" + seed, "seed_claim_id": seed, "seed_predicate": claims[seed]["predicate"],
                        "claim_ids": chosen, "source_group": "unit:" + info[seed]["primary_unit"] if info[seed]["primary_unit"] else None,
                        "domain": None, "source_status_by_claim": {ident: claims[ident]["assessment"]["status"] for ident in chosen},
                        "context": {"explicit_dependency_references": refs, "selected_dependency_claim_ids": context,
                                    "selected_cooccurrence_claim_ids": [], "dependency_candidate_count": len(context_candidates),
                                    "cooccurrence_candidate_count": 0, "basis": "explicit_native_claim_reference_only",
                                    "unit_ids": units, "cross_unit_dependency_context": len(units) > 1,
                                    "source_ids": sorted({sid for row in info.values() for sid in row["source_ids"]}),
                                    "direct_support_observation_ids": sorted({ob for row in info.values() for ob in row["observation_ids"]}),
                                    "missing_support_observation_ids": sorted({ob for row in info.values() for ob in row["missing_observation_ids"] if ob is not None}),
                                    "source_independence_verified": False}})
    selected = {ident for row in records for ident in row["claim_ids"]}
    return records, {"status_policy": status_policy, "evidence_policy": "observed_only",
                     "all_core_claims": len(claims), "eligible_claims": len(eligible),
                     "source_evidence_status_counts": [{"evidence_class": evidence, "status": status, "count": count}
                                                       for (evidence, status), count in sorted(classes.items())],
                     "excluded_by_reason": dict(sorted(excluded.items())), "relation_bearing_eligible_seeds": len(relation_seeds),
                     "selected_records": len(records), "relation_bearing_seeds_omitted_by_limit": len(relation_seeds) - len(seeds),
                     "eligible_nonrelation_seeds_not_sampled": len(eligible) - len(relation_seeds),
                     "seed_predicate_counts": dict(sorted(Counter(claims[ident]["predicate"] for ident in seeds).items())),
                     "seed_status_counts": dict(sorted(Counter(claims[ident]["assessment"]["status"] for ident in seeds).items())),
                     "selected_unique_claims_with_context": len(selected),
                     "selected_unique_status_counts": dict(sorted(Counter(claims[ident]["assessment"]["status"] for ident in selected).items())),
                     "selected_root_occurrences": sum(len(row["claim_ids"]) for row in records),
                     "eligible_claims_omitted_from_all_views": len(eligible - selected),
                     "duplicate_claim_set_record_count": len(records) - len({tuple(sorted(row["claim_ids"])) for row in records}),
                     "selected_seed_reference_counts": dict(sorted(selected_refs.items())),
                     "context_omissions_with_repeats": dict(sorted(omissions.items())),
                     "selection": "deliberately_relation_biased; ascending_eligible_claim_ID_with_explicit_claim_reference",
                     "context_selection": "ascending_eligible_direct_target_claim_ID; no_recursive_body_selection",
                     "representative_sample": False, "recorded_counter_is_logical_contradiction": None,
                     "source_independence_verified": False, "assessment_promotion": False}


def run_experiment(source, *, record_budget=24, context_limit=3, state_budget=1000, top_k=3, status_policy="active_and_contested"):
    if type(state_budget) is not int or not 0 <= state_budget <= 100000 or type(top_k) is not int or not 0 <= top_k <= 128:
        raise ValueError("search bound outside allowed range")
    before = hashes()
    records, selection = choose_relation_records(source, maximum=record_budget, context_limit=context_limit, status_policy=status_policy)
    modes = {}
    for mode, policy in (("semantic", "exact_literals"), ("structural", "exact_literals"), ("structural", "literal_type_control")):
        if records:
            modes[mode + ":" + policy] = one_mode(source, records, mode, policy, state_budget, top_k)
    if before != hashes():
        raise RuntimeError("dependencies changed during run")
    return {"version": VERSION, "run_id": source["run_id"], "source_snapshot_sha256": hashlib.sha256(canonical(source).encode()).hexdigest(),
            "source_snapshot_body_sha256": source.get("snapshot", {}).get("body_sha256"), "dependency_sha256": before,
            "parameters": {"record_budget": record_budget, "context_limit": context_limit, "state_budget_per_pair": state_budget,
                           "top_k": top_k, "status_policy": status_policy}, "selection": selection, "record_specs": records, "modes": modes,
            "coverage_limits": ["relation-biased source diagnostic, not representative sampling or independent validation",
                                "source counter links may express assessment conflict; logical contradiction and thought operations are not inferred",
                                "contested status remains contested in full source assessments and compatibility dimensions",
                                "context bound selects bodies only; remaining references remain ports with unknowns",
                                "overlapping records and unit groups are not independent support or known domains",
                                "literal type control cannot establish analogy or entailment"],
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
    parser.add_argument("--status-policy", choices=sorted(STATUS_POLICIES), default="active_and_contested")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = read_snapshot(args.database, args.run) if args.database else json.loads(args.snapshot.read_text(encoding="utf-8"))
    if source["run_id"] != args.run:
        parser.error("snapshot run_id differs from explicit --run")
    result = run_experiment(source, record_budget=args.record_budget, context_limit=args.context_limit,
                            state_budget=args.state_budget, top_k=args.top_k, status_policy=args.status_policy)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")


if __name__ == "__main__":
    main()
