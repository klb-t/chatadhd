#!/usr/bin/env python3
"""Compact, repeatable source-only diagnostic of three repository documents.

No truth labels, independent fixtures, database, model providers, or repository
mutation. Source bytes are read verbatim; only summaries and locators are emitted.
Profile configuration is a contextual projection, not assessed core knowledge.
"""
from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re

try:
    from . import extract, pipeline, topics
except ImportError:
    import extract
    import pipeline
    import topics

VERSION = "repository-source-diagnostic/2"
SOURCE_PATHS = (
    "docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md",
    "docs/architecture/NOTATKA_GPT_2026-09-26.md",
    "docs/architecture/LOOM_CONCEPTUAL_MODEL.md",
)
PROFILE_PATH = "loom/data/profiles/self.json"
DEPENDENCIES = (
    "loom/tools/structure/repo_experiment.py", "loom/tools/structure/pipeline.py",
    "loom/tools/structure/topics.py", "loom/tools/structure/topics_policy.json",
    "loom/tools/structure/extract.py", "loom/tools/structure/structure_methods.py",
    "loom/tools/structure/operations.json", "loom/tools/structure/scoped_projection.py",
    "loom/tools/structure/boundary_alternatives.py",
)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def hashes(root: Path, paths) -> dict:
    return {path: sha((root / path).read_bytes()) for path in paths}


def locator(path: str, source_hash: str, pointer: str = "") -> dict:
    return {"source": "sha256:" + source_hash, "member": path,
            "json_pointer": pointer, "basis": "repository_file_snapshot"}


def profile_projection(root: Path) -> tuple[list[dict], dict, dict]:
    raw = (root / PROFILE_PATH).read_bytes()
    profile = json.loads(raw)
    source_hash = sha(raw)
    entities, nodes, edges = [], [], []
    for index, project in enumerate(profile["projects"]):
        project_locator = locator(PROFILE_PATH, source_hash, f"/projects/{index}")
        aliases = []
        for alias_index, source_alias in enumerate(project.get("aliases", [])):
            context = source_alias.get("requires_context", {})
            aliases.append({"text": source_alias["t"],
                            "ambiguous": source_alias.get("ambiguous", False),
                            "requires_any": deepcopy(context.get("any", [])),
                            "min_context": context.get("min", 1 if source_alias.get("ambiguous") else 0),
                            "excludes": deepcopy(source_alias.get("negative_context", [])),
                            "source_locator": locator(PROFILE_PATH, source_hash, f"/projects/{index}/aliases/{alias_index}"),
                            "source_alias": deepcopy(source_alias)})
        # The name is metadata, never an additional ungated alias. This matters
        # for ambiguous short names such as Loom and LEM.
        entities.append({"id": project["id"], "name": project["name"], "aliases": aliases,
                         "source_locator": project_locator})
        nodes.append({"id": project["id"], "kind": "project", "role": "",
                      "label": project["name"], "claim_ids": [],
                      "qualifiers": {}, "profile_record": deepcopy(project),
                      "provenance": {"locator": project_locator, "interpretation": "profile_project_declaration_only"}})
        if project.get("merged_into") is not None:
            edges.append({"source": project["id"], "target": project["merged_into"],
                          "predicate": "merged_into", "qualifiers": {}, "claim_ids": [],
                          "provenance": {"locator": locator(PROFILE_PATH, source_hash, f"/projects/{index}/merged_into"),
                                         "value": project["merged_into"],
                                         "interpretation": "profile_lineage_declaration_only_not_alias_or_assessed_claim"}})
    graph = {"nodes": nodes, "edges": edges, "origin": "profile_configuration_projection",
             "assessment_status": "not_assessed", "source_locator": locator(PROFILE_PATH, source_hash)}
    summary = {"path": PROFILE_PATH, "sha256": source_hash, "project_nodes": len(nodes),
               "merged_into_edges": len(edges), "alias_entries": sum(len(e["aliases"]) for e in entities),
               "ambiguous_alias_entries": sum(bool(a["ambiguous"]) for e in entities for a in e["aliases"]),
               "core_claim_ids_used": 0,
               "alias_conversion": "t -> text; requires_context.any/min -> requires_any/min_context; negative_context -> excludes",
               "matching_semantics": "case-insensitive literal Unicode boundaries, including context cues; no stemming, lemmatization or prefix matching",
               "difference_from_native": "native match-key normalization and stem-like pack cues are not reproduced; any literal negative cue vetoes rather than native negative-dominance scoring",
               "meaning": "configured context candidates; no project identity, corpus relevance or lineage truth established"}
    return entities, graph, summary


def source_turns(path: str, source_hash: str, text: str, mode: str) -> list[dict]:
    if mode == "whole_document":
        ranges = [(0, len(text))]
    elif mode == "paragraph_turns":
        boundaries = [0] + [m.end() for m in re.finditer(r"\n[\t ]*\n", text)] + [len(text)]
        ranges = [(a, b) for a, b in zip(boundaries, boundaries[1:]) if a < b]
    else:
        raise ValueError("source mode must be whole_document or paragraph_turns")
    turns = []
    for index, (start, end) in enumerate(ranges):
        turns.append({"id": f"{path}:{index}:{start}:{end}", "role": "document",
                      "text": text[start:end],
                      "metadata": {"source_locator": locator(path, source_hash),
                                   "source_char_start": start, "source_char_end": end,
                                   "source_byte_start": len(text[:start].encode("utf-8")),
                                   "source_byte_end": len(text[:end].encode("utf-8")),
                                   "source_sha256": source_hash,
                                   "source_layout": mode,
                                   "turn_semantics": "exact_document_slice_not_historical_chat_turn"}})
    if "".join(t["text"] for t in turns) != text:
        raise ValueError("source partition did not preserve every character")
    return turns


def extraction_summary(results: list[dict]) -> dict:
    coverage_keys = ("physical_units", "recognized_envelopes", "logical_candidates",
                     "eligible_characters", "recognized_envelope_characters")
    summary = {key: sum(x["coverage"][key] for x in results) for key in coverage_keys}
    summary["unknown_units"] = sum(len(x["unknown"]) for x in results)
    summary["unknown_characters"] = summary["eligible_characters"] - summary["recognized_envelope_characters"]
    summary["envelope_character_coverage"] = (summary["recognized_envelope_characters"] / summary["eligible_characters"]
                                               if summary["eligible_characters"] else None)
    summary["unknown_reasons"] = dict(sorted(Counter(u["reason"] for x in results for u in x["unknown"]).items()))
    summary["recognized_methods"] = dict(sorted(Counter(c["method"] for x in results for c in x["candidates"]).items()))
    summary["semantic_accuracy"] = None
    return summary


def source_candidate_refs(result: dict, text: str, turns: list[dict]) -> list[dict]:
    by_id = {turn["id"]: turn for turn in turns}
    refs = []
    for extraction in result["extractions"]:
        for candidate in extraction["candidates"]:
            span = candidate["source_span"]
            turn = by_id[span["turn_id"]]
            base = turn["metadata"]["source_char_start"]
            start, end = base + span["char_start"], base + span["char_end"]
            if text[start:end] != span["quote"]:
                raise ValueError("candidate global source span mismatch")
            refs.append({"candidate_id": candidate["id"], "method": candidate["method"],
                         "source_char_start": start, "source_char_end": end,
                         "source_line": text.count("\n", 0, start) + 1,
                         "quote_sha256": sha(text[start:end].encode("utf-8")),
                         "logical_formula_available": candidate["formula_candidate"] is not None})
    return refs


def pipeline_summary(result: dict, text: str, turns: list[dict]) -> dict:
    segmentation = result["segmentation"]
    observations = segmentation["observations"]
    by_turn = {t["id"]: t for t in turns}
    for observation in observations:
        source = observation["source"]
        turn = by_turn[source["turn_id"]]
        start, end = source["char_start"], source["char_end"]
        if turn["text"][start:end] != observation["text"]:
            raise ValueError("observation turn slice mismatch")
        offset = turn["metadata"]["source_char_start"]
        if text[offset + start:offset + end] != observation["text"]:
            raise ValueError("observation raw document slice mismatch")
    mentions = [mention for observation in observations for mention in observation["mentions"]]
    accepted = [mention for mention in mentions if mention["accepted"]]
    context = segmentation["context_projection"]
    summary = {**extraction_summary(result["extractions"]),
               "source_turns": len(turns), "topic_observations": len(observations),
               "topic_segments": len(segmentation["segments"]),
               "focus_basis_counts": dict(sorted(Counter(o["focus_basis"] for o in observations).items())),
               "segment_boundary_counts": dict(sorted(Counter(s["boundary"] for s in segmentation["segments"]).items())),
               "local_alias_matches": len(mentions), "accepted_alias_matches": len(accepted),
               "rejected_alias_matches": len(mentions) - len(accepted),
               "alias_rejection_reasons": dict(sorted(Counter(m["reason"] for m in mentions if not m["accepted"]).items())),
               "accepted_alias_matches_by_project": dict(sorted(Counter(m["entity_id"] for m in accepted).items())),
               "observations_with_focus": sum(bool(o["focus_entity_ids"]) for o in observations),
               "graph_context_candidate_count": sum(len(c["candidates"]) for c in context["contexts"]),
               "graph_context_omitted_by_budget": sum(c["omitted_by_budget"] for c in context["contexts"]),
               "interpretation_context_proposals": len(result["interpretation_context_proposals"]),
               "scoped_projection_count": len(result.get("scope_projections", [])),
               "scoped_projection_limit_count": len(result.get("scope_projection_limits", [])),
               "boundary_alternative_diagnostic": deepcopy(result.get("boundary_alternatives", {}).get("diagnostic", {})),
               "segments_without_recognized_operations": len(segmentation["segments"]) - len({
                   x["scope"]["segment_id"] for x in result["extractions"] if x["candidates"]}),
               "blocked_unchecked_interpretations": len(result["reasoning"]["blocked_interpretations"]),
               "comparison_budget": deepcopy(result["comparison_budget"]),
               "comparison_alignment_states": dict(sorted(Counter(
                   value["alignment"]["status"] for pair in result["comparisons"]
                   for value in pair["perspectives"].values()).items())),
               "recognized_source_refs": source_candidate_refs(result, text, turns),
               "input_hash": result["input_hash"], "topic_policy_hash": segmentation["policy_hash"],
               "profile_context_graph_hash": context["graph_hash"],
               "graph_mutations_applied": result["graph_mutations_applied"], "claims_created": result["claims_created"],
               "source_spans_verified": True}
    # Diagnose only observed boundary mechanics, without inventing semantic labels.
    cut_reasons = Counter()
    for observation in observations:
        source = observation["source"]
        end = source["char_end"]
        turn_text = by_turn[source["turn_id"]]["text"]
        if end < len(turn_text):
            left = turn_text[:end].rstrip()
            if left and left[-1] == ";":
                cut_reasons["after_semicolon"] += 1
            if end and turn_text[end - 1] == "\n":
                cut_reasons["after_newline"] += 1
    summary["boundary_mechanics"] = dict(sorted(cut_reasons.items()))
    return summary


def run(root: Path, *, modes=("whole_document", "paragraph_turns"), pair_budget=16) -> dict:
    if type(pair_budget) is not int or not 0 <= pair_budget <= 256:
        raise ValueError("pair_budget must be in [0,256]")
    tracked = (*DEPENDENCIES, PROFILE_PATH, *SOURCE_PATHS)
    before = hashes(root, tracked)
    entities, graph, profile = profile_projection(root)
    documents = []
    for path in SOURCE_PATHS:
        raw = (root / path).read_bytes()
        text = raw.decode("utf-8")
        source_hash = sha(raw)
        if text.encode("utf-8") != raw:
            raise ValueError("source decoding changed bytes")
        source = {"path": path, "sha256": source_hash, "bytes": len(raw), "characters": len(text),
                  "source_locator": locator(path, source_hash)}
        physical = extract.extract_record({"id": path + ":physical", "source_id": "sha256:" + source_hash,
                                           "text": text, "locator": locator(path, source_hash)})
        document = {"source": source, "raw_physical_lines": extraction_summary([physical]), "pipeline_views": {}}
        for mode in modes:
            turns = source_turns(path, source_hash, text, mode)
            record = {"id": path + ":" + mode, "source_id": "sha256:" + source_hash,
                      "metadata": {"source": deepcopy(source), "layout": mode,
                                   "source_is_architecture_document_not_conversation_export": True},
                      "turns": turns, "entities": deepcopy(entities), "graph": deepcopy(graph), "claims": []}
            result = pipeline.analyze(record, pair_budget=pair_budget)
            document["pipeline_views"][mode] = pipeline_summary(result, text, turns)
        documents.append(document)
    after = hashes(root, tracked)
    if before != after:
        changed = sorted(path for path in before if before[path] != after[path])
        raise RuntimeError("dependencies changed while measuring; rerun: " + ", ".join(changed))
    return {"version": VERSION, "diagnostic_only": True,
            "components": {"pipeline": pipeline.VERSION, "topics": topics.VERSION, "extractor": extract.VERSION},
            "sources": documents, "profile_projection": profile,
            "dependency_sha256": {path: before[path] for path in DEPENDENCIES},
            "parameters": {"modes": list(modes), "pair_budget_per_document_mode": pair_budget},
            "limitations": ["architecture_sources_not_independent_validation_or_owner_exports",
                            "no_gold_labels_no_semantic_accuracy_no_calibration",
                            "literal_profile_cues_differ_from_native_match_keys",
                            "profile_graph_is_configuration_projection_not_assessed_core_claims",
                            "grammar_coverage_is_not_extracted_meaning_recall",
                            "source_order_pair_sample_not_ranked_retrieval_or_population_frequency",
                            "paragraph_turns_are_exact_source_slices_not_historical_conversation_turns",
                            "topic_cuts_can_break_primary_envelopes; separate_boundary_alternatives_require_scope_review",
                            "markdown_and_wrapped_lines_remain_unmodified_and_may_block_grammar"],
            "source_bytes_rewritten": 0, "semantic_accuracy": None, "calibration": None,
            "production_claims_created": 0, "production_graph_mutations": 0}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--output", type=Path, help="compact JSON report; no source copies")
    parser.add_argument("--mode", choices=("whole_document", "paragraph_turns", "both"), default="both")
    parser.add_argument("--pair-budget", type=int, default=16)
    args = parser.parse_args()
    modes = ("whole_document", "paragraph_turns") if args.mode == "both" else (args.mode,)
    result = run(args.root.resolve(), modes=modes, pair_budget=args.pair_budget)
    content = json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.output.with_suffix(args.output.suffix + ".tmp")
        temporary.write_text(content, encoding="utf-8")
        temporary.replace(args.output)
    else:
        print(content, end="")


if __name__ == "__main__":
    main()
