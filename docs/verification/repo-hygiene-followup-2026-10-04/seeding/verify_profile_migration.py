#!/usr/bin/env python3
"""Recount the exact inspected synthetic V4 baseline after profile migration.

Run from any directory. This reads synthetic development data only and never
writes historical results or calls a model. The JSON receipt goes to stdout.
"""
from dataclasses import asdict
from datetime import datetime, timezone
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from loom.tools.seeding import prototype as current
from loom.tools.seeding.recipe import DEFAULT_PROFILE, load_profile


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()


run = ROOT / "loom/tools/seeding/results/synthetic_lopo_v4_application_alternatives_first"
with gzip.open(run / "predictions.json.gz", "rt") as handle:
    saved = json.load(handle)
with gzip.open(run / "results.json.gz", "rt") as handle:
    saved_scores = json.load(handle)["scores"]
module_name = "_frozen_v4_before_profile_migration"
spec = importlib.util.spec_from_file_location(module_name, run / "prototype_frozen.py")
historical = importlib.util.module_from_spec(spec)
sys.modules[module_name] = historical
spec.loader.exec_module(historical)
corpus = json.loads(current.FIXTURE.read_bytes())
operators = {operator["id"]: operator for operator in corpus["operators"]}
profile = load_profile()
policy = json.loads((run / "policy_frozen.json").read_bytes())
graphs = [current.build_graph(project, operators, profile) for project in corpus["projects"]]
for project, graph in zip(corpus["projects"], graphs):
    assert asdict(graph) == asdict(historical.build_graph(project, operators))
by_id = {graph.project: graph for graph in graphs}
before_hash = hashlib.sha256()
after_hash = hashlib.sha256()
count = 0
answers = {}
actual_cases = []
for case in saved["cases"]:
    graph = by_id[case["project"]]
    hidden = [*sorted(graph.edges[case["task"]]), None][int(case["case_id"].rsplit("/", 1)[1])]
    answers[case["case_id"]] = hidden
    target = current.target_view(graph, case["task"], hidden)
    assert not target.properties
    assert {key: sorted(value) for key, value in target.edges.items()} == case["visible_edges"]
    training = [donor for donor in graphs if donor.project != graph.project]
    actual = {key: value for key, value in case.items() if key != "methods"} | {"methods": {}}
    for method, ranks in case["methods"].items():
        actual["methods"][method] = {}
        for seed, expected in ranks.items():
            observed = current.predict(training, target, case["task"], method, policy,
                saved["manifest"]["predicted_at"], case["case_id"], int(seed), profile)
            if method == "random" and seed != "0":
                observed = [candidate["label"] for candidate in observed]
            assert observed == expected, (case["case_id"], method, seed)
            before_hash.update(canonical([case["case_id"], method, seed, expected]) + b"\n")
            after_hash.update(canonical([case["case_id"], method, seed, observed]) + b"\n")
            actual["methods"][method][seed] = observed
            count += 1
    actual_cases.append(actual)
metric_count = 0
for group, budgets in saved_scores.items():
    selected = [case for case in actual_cases if group == "all" or case["task"] == group or case["project"] == group]
    for budget, methods in budgets.items():
        for method, expected in methods.items():
            if method == "random_expected":
                assert current.random_expectation(selected, answers, int(budget)) == expected
                metric_count += 1
            elif method == "random_repetitions":
                for seed, value in expected.items():
                    assert current.metrics(selected, answers, "random", int(budget), int(seed)) == value
                    metric_count += 1
            else:
                assert current.metrics(selected, answers, method, int(budget)) == expected
                metric_count += 1
assert count == 2135 and before_hash.digest() == after_hash.digest()
original_audit = ROOT / "docs/verification/repo-hygiene-2026-10-04/seeding-audit.json"
historical_files = json.loads(original_audit.read_bytes())["files"]
for record in historical_files:
    raw = (ROOT / record["path"]).read_bytes()
    assert len(raw) == record["size_bytes"] and sha(raw) == record["sha256"]
modules = ("prototype.py", "recipe.py", "profiles/default.json", "profiles/schema.json", "test_profiles.py")
receipt = {
    "schema": "loom.verification.seeding_profile_migration/1",
    "measured_at_utc": datetime.now(timezone.utc).isoformat(),
    "evaluation_class": "inspected_synthetic_development_regression",
    "paid_calls": 0,
    "method_graph_bridge": "pending_agreed_contract",
    "baseline": {"run": str(run.relative_to(ROOT)), "frozen_code_sha256": sha((run / "prototype_frozen.py").read_bytes()),
        "predictions_sha256": sha((run / "predictions.json.gz").read_bytes()), "predicted_at": saved["manifest"]["predicted_at"]},
    "graphs_exact": len(graphs), "cases_exact": len(actual_cases),
    "positive_masks": sum(answer is not None for answer in answers.values()),
    "negative_controls": sum(answer is None for answer in answers.values()),
    "ranked_outputs_exact": count, "metric_dictionaries_exact": metric_count,
    "before_rank_matrix_sha256": before_hash.hexdigest(), "after_rank_matrix_sha256": after_hash.hexdigest(),
    "historical_result_files_unchanged": len(historical_files),
    "historical_result_bytes_unchanged": sum(record["size_bytes"] for record in historical_files),
    "active_source_sha256": {name: sha((ROOT / "loom/tools/seeding" / name).read_bytes()) for name in modules},
    "default_prediction_dimensions": list(current.TASKS),
    "quality_gain_claimed": False,
}
print(json.dumps(receipt, ensure_ascii=False, sort_keys=True, indent=2))
