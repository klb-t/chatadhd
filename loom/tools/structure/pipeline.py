#!/usr/bin/env python3
"""Read-only source -> local topics -> thought projections -> graph context.

This joins the experiments without turning grammar matches or graph similarity
into assessed Claims. Every result retains its source; no database is opened.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
from itertools import combinations
import json
from pathlib import Path

try:
    from . import extract, structure_methods, topics
except ImportError:
    import extract
    import structure_methods
    import topics

VERSION = "source-structure-pipeline/1"


def _hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                                    separators=(",", ":")).encode()).hexdigest()


def _budget(value, name):
    if type(value) is not int or value < 0:
        raise ValueError(name + " must be a nonnegative integer")
    return value


def analyze(record: dict, *, policy=None, pair_budget=128) -> dict:
    """Analyze one conversation, preserving all input and every unknown span.

    Input is topics.analyze's record schema. Pair comparisons cover recognized
    observations in different proposed segments, in source order, up to a
    visible budget. This is a deterministic exploration sample, not top-k
    retrieval and not a measurement of population-wide pattern frequency.
    """
    _budget(pair_budget, "pair_budget")
    segmentation = topics.analyze(record, policy)
    graph_context = segmentation["context_projection"]
    contexts = {c["observation_id"]: c for c in graph_context["contexts"]}
    turns = {turn["id"]: turn for turn in record.get("turns", [])}
    extractions, proposals, blocked = [], [], []
    for observation in segmentation["observations"]:
        source = observation["source"]
        turn = turns[source["turn_id"]]
        text = turn["text"]
        start, end = source["char_start"], source["char_end"]
        if text[start:end] != observation["text"]:
            raise ValueError("segmentation source span mismatch")
        result = extract.extract_record({
            "id": observation["id"], "source_id": record.get("id"),
            "conversation_id": record.get("id"), "scope_status": "candidate",
            "scope_evidence": {"boundary": observation["boundary"],
                               "focus_evidence_observation_ids": deepcopy(observation["focus_evidence_observation_ids"])},
            "turn_id": source["turn_id"], "role": turn.get("role"),
            "segment_id": observation["segment_id"], "text": observation["text"],
            "source_span": deepcopy(source),
            "focus_entity_ids": deepcopy(observation["focus_entity_ids"]),
            "focus_basis": observation["focus_basis"],
        })
        # Derive turn coordinates only after checking the exact parent slice.
        # Never confuse local extraction offsets with original archive offsets.
        for candidate in result["candidates"]:
            local = candidate["span"]
            a, b = start + local["char_start"], start + local["char_end"]
            if text[a:b] != local["quote"]:
                raise ValueError("extraction source span mismatch")
            candidate["source_span"] = {
                "turn_id": source["turn_id"], "char_start": a, "char_end": b,
                "byte_start": len(text[:a].encode("utf-8")),
                "byte_end": len(text[:b].encode("utf-8")),
                "sha256": source["sha256"], "coordinate_space": "input.turns[].text",
                "quote": text[a:b],
            }
            # Attach current observation focus, never final segment anchor union.
            proposal = {
                "kind": "interpretation_context_review", "candidate_id": candidate["id"],
                "observation_id": observation["id"], "segment_id": observation["segment_id"],
                "source": deepcopy(candidate["source_span"]),
                "subject_candidates": deepcopy(observation["focus_entity_ids"]),
                "focus_basis": observation["focus_basis"],
                "existing_graph_context": deepcopy(contexts[observation["id"]]),
                "base_graph_hash": graph_context["graph_hash"],
                "status": "candidate", "persistable_claim": False,
                "automatic_mutation": False, "confidence": None,
                "requires": ["interpretation_assessment", "subject_identity_assessment",
                             "source_grounding_verification", "core_model_validation"],
            }
            proposal["id"] = "proposal_" + _hash(proposal)[:20]
            proposals.append(proposal)
            if candidate["formula_candidate"] is not None:
                blocked.append({"candidate_id": candidate["id"],
                                "reason": "unchecked_interpretation_has_no_assessment",
                                "scope": deepcopy(candidate["scope"])})
        extractions.append(result)
    represented = [x for x in extractions if x["candidates"]]
    comparisons, eligible_pairs = [], 0
    for left, right in combinations(represented, 2):
        if left["scope"]["segment_id"] == right["scope"]["segment_id"]:
            continue
        eligible_pairs += 1
        if len(comparisons) >= pair_budget:
            continue
        comparisons.append({
            "left_observation_id": left["record_id"], "right_observation_id": right["record_id"],
            "left_segment_id": left["scope"]["segment_id"],
            "right_segment_id": right["scope"]["segment_id"],
            "perspectives": {projection: extract.compare_extractions(left, right, projection=projection)
                             for projection in ("operations", "logical_candidates")},
            "interpretation": "structural_analogy_candidate_not_identity_or_validity",
            "confidence": None, "persistable_claim": False,
        })
    eligible = sum(e["coverage"]["eligible_characters"] for e in extractions)
    recognized = sum(e["coverage"]["recognized_envelope_characters"] for e in extractions)
    totals = {key: sum(e["coverage"][key] for e in extractions)
              for key in ("physical_units", "recognized_envelopes", "logical_candidates")}
    return {
        "version": VERSION, "input": deepcopy(record), "input_hash": _hash(record),
        "components": {"topics": topics.VERSION, "extraction": extract.VERSION,
                       "methods": structure_methods.VERSION},
        "segmentation": segmentation, "extractions": extractions,
        "comparisons": comparisons,
        "comparison_budget": {"maximum_pairs": pair_budget, "eligible_pairs": eligible_pairs,
                              "evaluated_pairs": len(comparisons),
                              "omitted_pairs": eligible_pairs - len(comparisons),
                              "selection": "source_order_cross_segment_not_ranked"},
        "interpretation_context_proposals": proposals,
        "reasoning": {"candidates": [], "blocked_interpretations": blocked,
                      "status": "requires_assessed_interpretations_and_core_proof_verifier"},
        "coverage": {**totals, "eligible_characters": eligible,
                     "recognized_envelope_characters": recognized,
                     "envelope_character_coverage": recognized / eligible if eligible else None,
                     "observations": len(extractions), "segments": len(segmentation["segments"]),
                     "semantic_accuracy": None,
                     "interpretation": "grammar_coverage_not_semantic_accuracy"},
        "graph_mutations_applied": 0, "claims_created": 0,
        "confidence": None, "calibration_status": "unavailable",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--policy", type=Path)
    parser.add_argument("--pair-budget", type=int, default=128)
    args = parser.parse_args()
    record = json.loads(args.input.read_text(encoding="utf-8"))
    policy = json.loads(args.policy.read_text(encoding="utf-8")) if args.policy else None
    result = analyze(record, policy=policy, pair_budget=args.pair_budget)
    content = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.output.with_suffix(args.output.suffix + ".tmp")
        temporary.write_text(content, encoding="utf-8")
        temporary.replace(args.output)
    else:
        print(content, end="")


if __name__ == "__main__":
    main()
