#!/usr/bin/env python3
"""Retrospective dev-only LOPO graph completion with a hard prediction boundary.

Run: python loom/tools/seeding/prototype.py --output <new-directory>
No external dependencies, model calls, or writes to the canonical graph.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import gzip
import json
from pathlib import Path
import random
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / "loom/tests/fixtures/eval/synthetic_dev/ground_truth.json"
PROTOCOL = ROOT / "docs/research/SEEDING_PROTOCOL_2026-09-29.md"
POLICY = Path(__file__).with_name("policy.json")
TASKS = ("roles", "capabilities", "features")


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


def source_record(item: dict[str, Any], path: str) -> dict[str, Any]:
    """Donor locators only. Dates describe the oracle, not ingest timestamps."""
    units = item.get("units", []) + ([item["unit"]] if "unit" in item else [])
    locators = [{k: u[k] for k in ("provider", "conv_id", "node_id", "date") if k in u}
                for u in units]
    dates = [u["date"] for u in locators if u.get("date")]
    return {"oracle_path": path, "source_locators": locators,
            "known_at": max(dates) if dates else None}


def build_graph(project: dict[str, Any], operators: dict[str, dict[str, Any]]) -> Graph:
    """Annotated training graph. Inferred/absent oracle roles are excluded."""
    edges: dict[str, set[str]] = {k: set() for k in (*TASKS, "principles")}
    props: dict[str, dict[str, Any]] = {}
    base = f"projects/{project['id']}"
    for role, record in project.get("universal_roles", {}).items():
        if not record.get("observed"):
            continue
        edges["roles"].add(role)
        props[f"roles:{role}"] = {
            "expected_properties": [{"relation": "has_universal_role", "role": role,
                                     "specific_content": "unverified_on_target"}],
            **source_record({"units": record["observed"]}, f"{base}/universal_roles/{role}/observed")}
    for feature in project.get("features_status", []):
        label = feature["label"]
        edges["features"].add(label)
        props[f"features:{label}"] = {
            "expected_properties": [{"relation": "has_feature", "label": label,
                                     "implementation_status": "unverified_on_target"}],
            "donor_status_history": feature.get("events", []),
            **source_record(feature, f"{base}/features_status/{feature['id']}")}
    applications: dict[str, list[dict[str, Any]]] = {}
    for decision in project.get("decisions", []):
        edges["principles"].update(decision.get("principle_evidence", []))
        op = decision.get("operator")
        if op:
            edges["capabilities"].add(op)
            applications.setdefault(op, []).append(decision)
    for op, decisions in applications.items():
        records = [source_record(d, f"{base}/decisions/{d['id']}") for d in decisions]
        definition = operators[op]
        props[f"capabilities:{op}"] = {
            "expected_properties": [{"relation": "applies_solution_class",
                                     "solution": definition["solution"],
                                     "requires_validation": True}],
            "justifying_principles": sorted({p for d in decisions for p in d.get("principle_evidence", [])}),
            "operator_definition_path": f"operators/{op}",
            "oracle_path": f"{base}/operator_applications/{op}",
            "source_locators": [loc for r in records for loc in r["source_locators"]],
            "known_at": max((r["known_at"] for r in records if r["known_at"]), default=None)}
    return Graph(project["id"], project["kind"],
                 {k: frozenset(v) for k, v in edges.items()}, props)


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
            seed: int = 0) -> list[dict[str, Any]]:
    """Candidate generation: no fixture/oracle/hidden label arguments or reads."""
    if target.properties:
        raise ValueError("target view must contain no oracle properties")
    if any(g.project == target.project for g in training):
        raise ValueError("target project leaked into donor training")
    if task not in TASKS or method not in ("partial_mapping", "most_frequent", "random"):
        raise ValueError("unknown task/method")
    candidates: dict[str, list[dict[str, Any]]] = {}
    for donor in training:
        alignment = mapping(target, donor, policy)
        valid = len(alignment["preserved_relations"]) >= policy["minimum_preserved_edges"]
        if method == "partial_mapping" and not valid:
            continue
        for label in sorted(donor.edges[task] - target.edges[task]):
            prop = donor.properties[f"{task}:{label}"]
            local_premises = []
            if (method == "partial_mapping" and task == "capabilities"
                    and policy.get("capability_mapping") == "preserve_justifying_principles"):
                principles = set(prop.get("justifying_principles", []))
                if not principles or not principles <= target.edges["principles"]:
                    continue
                local_premises = [{"donor_principle": p, "target_principle": p,
                                   "relation": "justifies_operator", "operator": label}
                                  for p in sorted(principles)]
            candidates.setdefault(label, []).append({"donor_project": donor.project,
                "mapping": alignment, "properties": prop,
                "operator_premise_mapping": local_premises})
    labels = sorted(candidates)
    if method == "partial_mapping":
        labels.sort(key=lambda label: (-sum(s["mapping"]["score"] for s in candidates[label]),
                                       -len(candidates[label]), label))
    elif method == "most_frequent":
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
            "predicted_at": predicted_at, "known_at": max(known) if known else None,
            "known_at_semantics": "latest donor evidence date; target dates unknown in redacted view",
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
        labels = [c["label"] for c in case["methods"]["random"]["0"]]
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


def experiment(output: Path, policy_path: Path = POLICY, protocol_path: Path = PROTOCOL) -> dict[str, Any]:
    # Refuse overwrite: the first output remains recoverable.
    output.mkdir(parents=True, exist_ok=False)
    corpus = json.loads(FIXTURE.read_text())
    policy = json.loads(policy_path.read_text())
    operators = {op["id"]: op for op in corpus["operators"]}
    graphs = [build_graph(p, operators) for p in corpus["projects"]]
    timestamp = datetime.now(timezone.utc).isoformat()
    cases: list[dict[str, Any]] = []
    answers: dict[str, str | None] = {}
    for graph in graphs:
        training = [g for g in graphs if g.project != graph.project]
        for task in TASKS:
            hidden_values: list[str | None] = sorted(graph.edges[task])
            if policy["include_no_removal_controls"]:
                hidden_values += [None]
            for index, hidden in enumerate(hidden_values):
                case_id = f"{graph.project}/{task}/{index:03d}"
                target = target_view(graph, task, hidden)
                record = {"case_id": case_id, "project": graph.project, "task": task,
                          "control": hidden is None,
                          "visible_edges": {k: sorted(v) for k, v in target.edges.items()}, "methods": {}}
                for method in ("partial_mapping", "most_frequent", "random"):
                    seeds = policy["random_seeds"] if method == "random" else [0]
                    ranked = {str(seed): predict(training, target, task, method,
                        policy, timestamp, case_id, seed) for seed in seeds}
                    # Seed zero owns full candidate records; later random seeds
                    # refer to that same pool by label, preserving every ordering
                    # without duplicating provenance/alternatives 32 times.
                    if method == "random":
                        ranked = {seed: items if seed == "0" else [p["label"] for p in items]
                                  for seed, items in ranked.items()}
                    record["methods"][method] = ranked
                cases.append(record)
                answers[case_id] = hidden
    manifest = {"protocol_version": policy["version"], "evaluation_class": "development_corpus_lopo",
                "predicted_at": timestamp, "hashes": {"fixture": digest(FIXTURE), "protocol": digest(protocol_path),
                    "policy": digest(policy_path), "code": digest(Path(__file__))},
                "canonical_graph_mutated": False, "paid_api_calls": 0}
    predictions = {"manifest": manifest, "cases": cases}
    # Explicit prediction-before-score boundary. No answers are serialized here.
    (output / "protocol_frozen.md").write_bytes(protocol_path.read_bytes())
    (output / "policy_frozen.json").write_bytes(policy_path.read_bytes())
    (output / "prototype_frozen.py").write_bytes(Path(__file__).read_bytes())
    save_json_gzip(output / "predictions.json.gz", predictions)
    results: dict[str, Any] = {"manifest": manifest, "scores": {}}
    for group in ("all", *TASKS, *(g.project for g in graphs)):
        selected = [c for c in cases if group == "all" or c["task"] == group or c["project"] == group]
        scores = {}
        for budget in policy["candidate_budgets"]:
            scores[str(budget)] = {"partial_mapping": metrics(selected, answers, "partial_mapping", budget),
                "most_frequent": metrics(selected, answers, "most_frequent", budget),
                "random_expected": random_expectation(selected, answers, budget),
                "random_repetitions": {str(seed): metrics(selected, answers, "random", budget, seed)
                                       for seed in policy["random_seeds"]}}
        results["scores"][group] = scores
    save_json_gzip(output / "results.json.gz", results)
    return results


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--policy", type=Path, default=POLICY)
    parser.add_argument("--protocol", type=Path, default=PROTOCOL)
    args = parser.parse_args()
    results = experiment(args.output, args.policy, args.protocol)
    for group in ("all", *TASKS):
        score = results["scores"][group]["1"]
        print(group, {method: {key: value for key, value in values.items() if key in
              ("tp", "emitted", "hidden_elements", "precision", "recall", "expected_tp", "expected_recall")}
              for method, values in score.items() if method != "random_repetitions"})


if __name__ == "__main__":
    main()
