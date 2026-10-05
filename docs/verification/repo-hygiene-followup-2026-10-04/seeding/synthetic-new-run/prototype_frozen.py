#!/usr/bin/env python3
"""Retrospective dev-only LOPO graph completion with a hard prediction boundary.

Run: python loom/tools/seeding/prototype.py --output <new-directory>
No external dependencies, model calls, or writes to the canonical graph.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import gzip
import json
from pathlib import Path
import random
from typing import Any
from urllib.parse import quote

if __package__:
    from .recipe import (DEFAULT_PROFILE, PROFILE_SCHEMA, prediction_dimensions,
                         project_graph, resolved_profile, load_profile, source_record, profile_snapshot, property_key)
else:
    from recipe import (DEFAULT_PROFILE, PROFILE_SCHEMA, prediction_dimensions,
                        project_graph, resolved_profile, load_profile, source_record, profile_snapshot, property_key)


ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / "loom/tests/fixtures/eval/synthetic_dev/ground_truth.json"
PROTOCOL = ROOT / "docs/research/SEEDING_PROTOCOL_2026-09-29.md"
POLICY = Path(__file__).with_name("policy.json")
TASKS = prediction_dimensions()  # backwards-compatible default, declared in profile data


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical(value: Any) -> str:
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"))


@dataclass(frozen=True)
class Graph:
    project: str
    kind: str
    edges: dict[str, frozenset[str]]
    properties: dict[str, dict[str, Any]]


def build_graph(project: dict[str, Any], operators: dict[str, dict[str, Any]],
                profile: dict[str, Any] | None = None) -> Graph:
    """Annotated graph projected by data; the default excludes inferred/absent roles."""
    edges, properties = project_graph(project, operators, profile)
    return Graph(project["id"], project["kind"], edges, properties)


def target_view(graph: Graph, task: str, hidden: str | None) -> Graph:
    """The predictor cannot access target oracle properties, prose or labels."""
    edges = {k: frozenset(v - {hidden}) if k == task else v for k, v in graph.edges.items()}
    return Graph(graph.project, graph.kind, edges, {})


def mapping(target: Graph, donor: Graph, policy: dict[str, Any]) -> dict[str, Any]:
    preserved = [{"target": target.project, "donor": donor.project,
                  "relation": relation, "label": label}
                 for relation in sorted(policy["relation_weights"])
                 for label in sorted(target.edges.get(relation, set()) & donor.edges.get(relation, set()))]
    numerator = sum(policy["relation_weights"][e["relation"]] for e in preserved)
    denominator = sum(weight * len(target.edges.get(relation, set()) | donor.edges.get(relation, set()))
                      for relation, weight in policy["relation_weights"].items())
    return {"preserved_relations": preserved,
            "score": numerator / denominator if denominator else 0.0,
            "unverified_properties": ["domain/kind compatibility", "target-specific implementation",
                                      "truth of transferred content", "temporal independence"]}


def predict(training: list[Graph], target: Graph, task: str, method: str,
            policy: dict[str, Any], predicted_at: str, case_id: str,
            seed: int = 0, profile: dict[str, Any] | None = None) -> list[dict[str, Any]]:
    """Candidate generation: no fixture/oracle/hidden label arguments or reads."""
    if target.properties:
        raise ValueError("target view must contain no oracle properties")
    if any(g.project == target.project for g in training):
        raise ValueError("target project leaked into donor training")
    recipe = resolved_profile(profile)
    dimensions = {d["id"]: d for d in recipe["dimensions"]}
    if task not in prediction_dimensions(recipe) or method not in ("partial_mapping", "most_frequent", "premise_filtered_frequency", "random"):
        raise ValueError("unknown task/method")
    for dimension in dimensions.values():
        if "eligibility" not in dimension:
            continue
        key = dimension["eligibility"]["policy_key"]
        if policy.get(key) not in (None, "preserve_justifying_principles",
                                   "preserve_application_alternatives"):
            raise ValueError(f"unknown {key} policy; refusing unfiltered fallback")
    eligibility = dimensions[task].get("eligibility")
    mode = policy.get(eligibility["policy_key"]) if eligibility else None
    mapped_eligibility = method in ("partial_mapping", "premise_filtered_frequency")
    candidates: dict[str, list[dict[str, Any]]] = {}
    for donor in training:
        alignment = mapping(target, donor, policy)
        valid = len(alignment["preserved_relations"]) >= policy["minimum_preserved_edges"]
        if mapped_eligibility and not valid:
            continue
        for label in sorted(donor.edges[task] - target.edges[task]):
            prop = donor.properties[property_key(task, label)]
            local_premises = []
            applicable_applications = []
            if mapped_eligibility and eligibility and mode == "preserve_justifying_principles":
                principles = set(prop.get(eligibility["union_property"], []))
                if not principles or not principles <= target.edges[eligibility["support_dimension"]]:
                    continue
                local_premises = [{"donor_principle": p, "target_principle": p,
                                   "relation": eligibility["mapping_relation"],
                                   eligibility["mapping_label_key"]: label}
                                  for p in sorted(principles)]
            elif mapped_eligibility and eligibility and mode == "preserve_application_alternatives":
                for application in prop.get(eligibility["alternatives_property"], []):
                    principles = set(application[eligibility["member_property"]])
                    if not principles or not principles <= target.edges[eligibility["support_dimension"]]:
                        continue
                    applicable_applications.append(application)
                    local_premises.extend({"donor_principle": p, "target_principle": p,
                        "relation": eligibility["mapping_relation"], eligibility["mapping_label_key"]: label,
                        eligibility["mapping_witness_id_key"]: application[eligibility["application_id_property"]],
                        "application_source": application[eligibility["application_source_property"]]}
                        for p in sorted(principles))
                if not applicable_applications:
                    continue
            candidates.setdefault(label, []).append({"donor_project": donor.project,
                "mapping": alignment, "properties": prop,
                "operator_premise_mapping": local_premises,
                "applicable_application_witnesses": applicable_applications})
    labels = sorted(candidates)
    if method == "partial_mapping":
        labels.sort(key=lambda label: (-sum(s["mapping"]["score"] for s in candidates[label]),
                                       -len(candidates[label]), label))
    elif method in ("most_frequent", "premise_filtered_frequency"):
        labels.sort(key=lambda label: (-len(candidates[label]), label))
    else:
        key = hashlib.sha256(f"{seed}:{case_id}".encode()).digest()
        random.Random(int.from_bytes(key, "big")).shuffle(labels)
    output = []
    for label in labels:
        witnesses = candidates[label]
        known = [w["properties"]["known_at"] for w in witnesses if w["properties"].get("known_at")]
        expected = [p for w in witnesses for p in w["properties"]["expected_properties"]]
        unique_expected = {canonical(p): p for p in expected}
        output.append({"label": label, "task": task, "target_project": target.project,
            "evidence_class": "extrapolated", "validation_status": "candidate",
            "predicted_at": predicted_at, "known_at": predicted_at,
            "known_at_semantics": "candidate created in this run; historical source know-times are unknown unless supplied",
            "premises_known_at": max(known) if known else None,
            "operator": {"id": "partial_subgraph_relation_transfer/v1" if method == "partial_mapping" else f"baseline/{method}/v1",
                         "policy_version": policy["version"], "task": task},
            "expected_properties": list(unique_expected.values()),
            "premises_and_provenance": witnesses,
            "assessment": {"score_kind": "similarity_sum" if method == "partial_mapping" else "donor_count",
                "score": sum(w["mapping"]["score"] for w in witnesses) if method == "partial_mapping" else len(witnesses),
                "calibrated_probability": None, "supporting_donor_projects": len(witnesses)},
            "unverified_properties": ["target-specific applicability", "implementation status", "causal relation", "truth"],
            "counter_evidence": {"status": "not_evaluated", "items": []}})
    # Keep each alternative's properties/provenance without recursive nesting.
    for item in output:
        item["alternatives"] = [{k: alt[k] for k in ("label", "expected_properties", "premises_and_provenance", "assessment")}
                                for alt in output if alt is not item]
    return output


def metrics(cases: list[dict[str, Any]], answers: dict[str, str | None], method: str,
            budget: int, seed: int = 0) -> dict[str, Any]:
    tp = emitted = positives = abstained = covered = 0
    failures = []
    for case in cases:
        hidden = answers[case["case_id"]]
        candidates = case["methods"][method][str(seed)]
        labels = [p["label"] if isinstance(p, dict) else p for p in candidates[:budget]]
        hit = hidden is not None and hidden in labels
        tp += int(hit)
        emitted += len(labels)
        positives += int(hidden is not None)
        abstained += int(not labels)
        covered += int(hidden is not None and hidden in [p["label"] if isinstance(p, dict) else p for p in candidates])
        if (hidden is not None and not hit) or len(labels) > int(hit):
            failures.append({"case_id": case["case_id"], "expected": hidden, "proposed": labels,
                             "false_negative": hidden is not None and not hit,
                             "false_positive_labels": [x for x in labels if x != hidden]})
    return {"tp": tp, "emitted": emitted, "hidden_elements": positives,
            "precision": tp / emitted if emitted else None,
            "recall": tp / positives if positives else None,
            "fp": emitted - tp, "fn": positives - tp, "abstained_cases": abstained,
            "cases": len(cases), "vocabulary_covered_hidden_elements": covered,
            "errors": failures}


def random_expectation(cases: list[dict[str, Any]], answers: dict[str, str | None], budget: int) -> dict[str, Any]:
    tp = 0.0
    emitted = positives = 0
    for case in cases:
        rankings = case["methods"]["random"]
        if not rankings:
            raise ValueError("random expectation requires at least one saved ranking")
        pool = rankings.get("0", next(iter(rankings.values())))
        labels = [candidate["label"] if isinstance(candidate, dict) else candidate for candidate in pool]
        hidden = answers[case["case_id"]]
        n = min(budget, len(labels))
        emitted += n
        positives += int(hidden is not None)
        if hidden is not None and hidden in labels:
            tp += n / len(labels)
    return {"expected_tp": tp, "emitted": emitted, "hidden_elements": positives,
            "expected_precision": tp / emitted if emitted else None,
            "expected_recall": tp / positives if positives else None,
            "meaning": "exact expectation under uniform candidate ordering; fractional TP"}


def save_json_gzip(path: Path, value: Any) -> None:
    """Exact JSON bytes archived losslessly; deterministic gzip header."""
    raw = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode()
    path.write_bytes(gzip.compress(raw, mtime=0))


def experiment(output: Path, policy_path: Path = POLICY, protocol_path: Path = PROTOCOL,
               profile_path: Path = DEFAULT_PROFILE, overlay_paths: list[Path] | tuple[Path, ...] = (),
               fixture_path: Path | None = None) -> dict[str, Any]:
    # Refuse overwrite: the first output remains recoverable.
    output.mkdir(parents=True, exist_ok=False)
    fixture_raw = Path(fixture_path or FIXTURE).read_bytes()
    policy_raw = Path(policy_path).read_bytes()
    protocol_raw = Path(protocol_path).read_bytes()
    corpus = json.loads(fixture_raw)
    policy = json.loads(policy_raw)
    profile, profile_sources = profile_snapshot(profile_path, overlay_paths)
    tasks = prediction_dimensions(profile)
    # Freeze every local runtime module and the packaged profile/schema. Capturing
    # bytes before prediction also keeps hashes bound to the actual run inputs.
    module_root = Path(__file__).resolve().parent
    module_sources = {name: (module_root / name).read_bytes() for name in
                      ("prototype.py", "recipe.py", "__init__.py", "profiles/default.json", "profiles/schema.json")}
    effective_raw = (json.dumps(profile, ensure_ascii=False, indent=2) + "\n").encode()
    methods = policy.get("methods", ["partial_mapping", "most_frequent", "random"])
    operators = {op["id"]: op for op in corpus["operators"]}
    graphs = [build_graph(p, operators, profile) for p in corpus["projects"]]
    timestamp = datetime.now(timezone.utc).isoformat()
    cases: list[dict[str, Any]] = []
    answers: dict[str, str | None] = {}
    for graph in graphs:
        training = [g for g in graphs if g.project != graph.project]
        for task in tasks:
            hidden_values: list[str | None] = sorted(graph.edges[task])
            if policy["include_no_removal_controls"]:
                hidden_values += [None]
            for index, hidden in enumerate(hidden_values):
                case_id = f"{quote(graph.project, safe='')}/{quote(task, safe='')}/{index:03d}"
                target = target_view(graph, task, hidden)
                record = {"case_id": case_id, "project": graph.project, "task": task,
                          "control": hidden is None,
                          "visible_edges": {k: sorted(v) for k, v in target.edges.items()}, "methods": {}}
                for method in methods:
                    seeds = policy["random_seeds"] if method == "random" else [0]
                    ranked = {str(seed): predict(training, target, task, method,
                        policy, timestamp, case_id, seed, profile=profile) for seed in seeds}
                    # Seed zero, or the first selected seed, owns full candidate
                    # records; other seeds refer to that pool by label, preserving every ordering
                    # without duplicating provenance/alternatives 32 times.
                    if method == "random":
                        provenance_seed = "0" if "0" in ranked else next(iter(ranked), None)
                        ranked = {seed: items if seed == provenance_seed else [p["label"] for p in items]
                                  for seed, items in ranked.items()}
                    record["methods"][method] = ranked
                cases.append(record)
                answers[case_id] = hidden
    sha256 = lambda raw: hashlib.sha256(raw).hexdigest()
    frozen_sources = [
        {"order": index, "role": "base" if index == 0 else "overlay", "source_name": path.name,
         "frozen_path": "profile_frozen.json" if index == 0 else f"profile_overlays/{index - 1:03d}.json",
         "sha256": sha256(raw)}
        for index, (path, raw) in enumerate(profile_sources)]
    manifest = {"protocol_version": policy["version"], "output_schema_version": "candidate-profile-projection-v3",
                "evaluation_class": "development_corpus_lopo",
                "predicted_at": timestamp, "hashes": {"fixture": sha256(fixture_raw), "protocol": sha256(protocol_raw),
                    "policy": sha256(policy_raw), "code": sha256(module_sources["prototype.py"])},
                "profile": {"schema": profile["schema"], "version": profile["version"],
                    "source_files": frozen_sources, "effective_path": "effective_profile.json",
                    "effective_sha256": sha256(effective_raw), "prediction_dimensions": list(tasks),
                    "property_key_encoding": "escaped-dimension-prefix-v1"},
                "modules": {f"modules_frozen/{name}": sha256(raw) for name, raw in module_sources.items()},
                "method_graph_bridge": {"status": "pending_agreed_contract"},
                "canonical_graph_mutated": False, "paid_api_calls": 0}
    predictions = {"manifest": manifest, "cases": cases}
    # Explicit prediction-before-score boundary. No answers are serialized here.
    (output / "protocol_frozen.md").write_bytes(protocol_raw)
    (output / "policy_frozen.json").write_bytes(policy_raw)
    (output / "fixture_frozen.json").write_bytes(fixture_raw)
    (output / "prototype_frozen.py").write_bytes(module_sources["prototype.py"])
    (output / "effective_profile.json").write_bytes(effective_raw)
    for source, (_, raw) in zip(frozen_sources, profile_sources):
        destination = output / source["frozen_path"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(raw)
    for name, raw in module_sources.items():
        destination = output / "modules_frozen" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(raw)
    save_json_gzip(output / "predictions.json.gz", predictions)
    # Typed groups keep arbitrary dimension/project names distinct, including
    # the name "all". Unambiguous old score aliases remain for existing readers.
    groups = [("all", None), *(("dimension", task) for task in tasks),
              *(("project", graph.project) for graph in graphs)]
    alias_counts = Counter("all" if kind == "all" else name for kind, name in groups)
    results: dict[str, Any] = {"manifest": manifest, "scores": {}, "score_groups": [],
                              "ambiguous_score_aliases": sorted(name for name, count in alias_counts.items() if count > 1)}
    for kind, name in groups:
        selected = [case for case in cases if kind == "all" or
                    (kind == "dimension" and case["task"] == name) or
                    (kind == "project" and case["project"] == name)]
        scores = {}
        for budget in policy["candidate_budgets"]:
            values = {}
            for method in methods:
                if method == "random":
                    values["random_expected"] = (random_expectation(selected, answers, budget)
                        if policy["random_seeds"] else {"status": "unavailable", "reason": "no_random_rankings"})
                    values["random_repetitions"] = {str(seed): metrics(selected, answers, method, budget, seed)
                                                     for seed in policy["random_seeds"]}
                else:
                    values[method] = metrics(selected, answers, method, budget)
            scores[str(budget)] = values
        results["score_groups"].append({"kind": kind, "id": name, "scores": scores})
        alias = "all" if kind == "all" else name
        if alias_counts[alias] == 1:
            results["scores"][alias] = scores
    save_json_gzip(output / "results.json.gz", results)
    return results


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--policy", type=Path, default=POLICY)
    parser.add_argument("--protocol", type=Path, default=PROTOCOL)
    parser.add_argument("--profile", "--recipe", type=Path, default=DEFAULT_PROFILE)
    parser.add_argument("--profile-overlay", "--recipe-overlay", type=Path, action="append", default=[])
    parser.add_argument("--fixture", type=Path, default=None)
    args = parser.parse_args()
    results = experiment(args.output, args.policy, args.protocol, args.profile, args.profile_overlay, args.fixture)
    for group in results["score_groups"]:
        if group["kind"] == "project":
            continue
        label = "all" if group["kind"] == "all" else f"dimension/{group['id']}"
        budgets = group["scores"]
        if not budgets:
            print(label, {"status": "no_candidate_budgets"})
            continue
        budget = next(iter(budgets))
        score = budgets[budget]
        print(label, {method: {key: value for key, value in values.items() if key in
              ("tp", "emitted", "hidden_elements", "precision", "recall", "expected_tp", "expected_recall")}
              for method, values in score.items() if method != "random_repetitions"})


if __name__ == "__main__":
    main()
