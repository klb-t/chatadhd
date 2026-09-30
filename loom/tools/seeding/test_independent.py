"""Independent development-artifact recount and adversarial seeding audit.

The recount builds labels directly from the fixture and never imports prototype
metrics/build_graph.  Counterexamples measure mechanisms, not model quality.
"""
from __future__ import annotations

import copy
import gzip
import hashlib
import json
from pathlib import Path
import random
import unittest


ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
TASKS = ("roles", "capabilities", "features")
RUNS = ("synthetic_lopo_v1_first", "synthetic_lopo_v2_first",
        "synthetic_lopo_v2_metadata_corrected")


def independent_edges(project):
    return {
        "roles": set(role for role, value in project.get("universal_roles", {}).items()
                     if value.get("observed")),
        "capabilities": set(d["operator"] for d in project.get("decisions", []) if d.get("operator")),
        "features": set(f["label"] for f in project.get("features_status", [])),
        "principles": set(p for d in project.get("decisions", []) for p in d.get("principle_evidence", [])),
    }


def ranked_labels(case, method, seed=0):
    return [p["label"] if isinstance(p, dict) else p
            for p in case["methods"][method][str(seed)]]


def recount(cases, gold, method, budget, seed=0):
    tp = emitted = positives = abstained = covered = 0
    for case in cases:
        answer = gold[case["case_id"]]
        pool = ranked_labels(case, method, seed)
        chosen = pool[:budget]
        tp += int(answer is not None and answer in chosen)
        emitted += len(chosen)
        positives += int(answer is not None)
        abstained += int(not chosen)
        covered += int(answer is not None and answer in pool)
    return {"tp": tp, "emitted": emitted, "hidden_elements": positives,
            "fp": emitted - tp, "fn": positives - tp,
            "precision": tp / emitted if emitted else None,
            "recall": tp / positives if positives else None,
            "abstained_cases": abstained, "cases": len(cases),
            "vocabulary_covered_hidden_elements": covered}


class FrozenArtifactsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture_path = ROOT / "loom/tests/fixtures/eval/synthetic_dev/ground_truth.json"
        cls.fixture = json.loads(cls.fixture_path.read_text())
        cls.edges = {p["id"]: independent_edges(p) for p in cls.fixture["projects"]}
        cls.artifacts = {}
        for name in RUNS:
            directory = HERE / "results" / name
            cls.artifacts[name] = (
                json.load(gzip.open(directory / "predictions.json.gz", "rt")),
                json.load(gzip.open(directory / "results.json.gz", "rt")))
        cls.gold = {}
        for project, edges in cls.edges.items():
            for task in TASKS:
                for index, hidden in enumerate([*sorted(edges[task]), None]):
                    cls.gold[f"{project}/{task}/{index:03d}"] = hidden

    def test_frozen_hashes_and_original_v1_bytes(self):
        for name, (predictions, results) in self.artifacts.items():
            directory = HERE / "results" / name
            self.assertEqual(predictions["manifest"], results["manifest"])
            hashes = predictions["manifest"]["hashes"]
            for key, path in (("fixture", self.fixture_path),
                              ("policy", directory / "policy_frozen.json"),
                              ("protocol", directory / "protocol_frozen.md"),
                              ("code", directory / "prototype_frozen.py")):
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), hashes[key], (name, key))
        directory = HERE / "results" / RUNS[0]
        for line in (directory / "uncompressed_sha256.txt").read_text().splitlines():
            digest, filename = line.split(maxsplit=1)
            raw = gzip.decompress((directory / (Path(filename).name + ".gz")).read_bytes())
            self.assertEqual(hashlib.sha256(raw).hexdigest(), digest)

    def test_masks_and_visible_target_edges_independently_reconstructed(self):
        self.assertEqual(len(self.gold), 61)
        self.assertEqual(sum(answer is not None for answer in self.gold.values()), 46)
        for predictions, _ in self.artifacts.values():
            self.assertEqual({c["case_id"] for c in predictions["cases"]}, set(self.gold))
            for case in predictions["cases"]:
                hidden = self.gold[case["case_id"]]
                expected = copy.deepcopy(self.edges[case["project"]])
                expected[case["task"]].discard(hidden)
                self.assertEqual(case["visible_edges"], {k: sorted(v) for k, v in expected.items()})
                self.assertEqual(case["control"], hidden is None)
                self.assertEqual(set(case), {"case_id", "project", "task", "control", "visible_edges", "methods"})

    def test_every_group_budget_and_all_32_seed_metrics_recount(self):
        for name, (predictions, results) in self.artifacts.items():
            for group, budgets in results["scores"].items():
                cases = [c for c in predictions["cases"] if
                         group == "all" or c["task"] == group or c["project"] == group]
                for budget_text, scores in budgets.items():
                    budget = int(budget_text)
                    for method in ("partial_mapping", "most_frequent"):
                        expected = recount(cases, self.gold, method, budget)
                        self.assertEqual({k: scores[method][k] for k in expected}, expected,
                                         (name, group, budget, method))
                    for seed in range(32):
                        expected = recount(cases, self.gold, "random", budget, seed)
                        observed = scores["random_repetitions"][str(seed)]
                        self.assertEqual({k: observed[k] for k in expected}, expected,
                                         (name, group, budget, seed))
                    expected_tp = sum(min(budget, len(pool)) / len(pool)
                                      for c in cases
                                      if (answer := self.gold[c["case_id"]]) is not None
                                      and answer in (pool := ranked_labels(c, "random")))
                    self.assertAlmostEqual(scores["random_expected"]["expected_tp"], expected_tp)

    def test_donor_isolation_provenance_and_no_target_prose(self):
        for predictions, _ in self.artifacts.values():
            for case in predictions["cases"]:
                for method in ("partial_mapping", "most_frequent", "random"):
                    for candidate in case["methods"][method]["0"]:
                        self.assertEqual(candidate["evidence_class"], "extrapolated")
                        self.assertEqual(candidate["validation_status"], "candidate")
                        self.assertIsNone(candidate["assessment"]["calibrated_probability"])
                        for witness in candidate["premises_and_provenance"]:
                            donor = witness["donor_project"]
                            self.assertNotEqual(donor, case["project"])
                            self.assertIn(candidate["label"], self.edges[donor][case["task"]])
                            self.assertNotIn(candidate["label"], case["visible_edges"][case["task"]])
                            self.assertTrue(witness["properties"]["oracle_path"].startswith(f"projects/{donor}/"))
                            for edge in witness["mapping"]["preserved_relations"]:
                                self.assertIn(edge["label"], case["visible_edges"][edge["relation"]])
                                self.assertIn(edge["label"], self.edges[donor][edge["relation"]])
                            for loc in witness["properties"]["source_locators"]:
                                self.assertLessEqual(set(loc), {"provider", "conv_id", "node_id", "date"})

    def test_32_random_seed_orders_reproduced_from_donor_vocabulary(self):
        for predictions, _ in self.artifacts.values():
            for case in predictions["cases"]:
                pool = sorted(set().union(*(e[case["task"]] for project, e in self.edges.items()
                                            if project != case["project"]))
                              - set(case["visible_edges"][case["task"]]))
                for seed in range(32):
                    expected = pool.copy()
                    key = hashlib.sha256(f"{seed}:{case['case_id']}".encode()).digest()
                    random.Random(int.from_bytes(key, "big")).shuffle(expected)
                    self.assertEqual(ranked_labels(case, "random", seed), expected)

    def test_metadata_correction_and_baseline_invariance(self):
        p1, r1 = self.artifacts[RUNS[0]]
        p2, r2 = self.artifacts[RUNS[1]]
        p3, r3 = self.artifacts[RUNS[2]]
        self.assertEqual(r2["scores"], r3["scores"])
        for group, budgets in r1["scores"].items():
            for budget, scores in budgets.items():
                for method in ("most_frequent", "random_expected", "random_repetitions"):
                    self.assertEqual(scores[method], r2["scores"][group][budget][method])
        for c1, c2, c3 in zip(p1["cases"], p2["cases"], p3["cases"]):
            self.assertEqual(c1["case_id"], c2["case_id"])
            self.assertEqual(c1["visible_edges"], c2["visible_edges"])
            self.assertEqual(c2["visible_edges"], c3["visible_edges"])
            for method in ("partial_mapping", "most_frequent", "random"):
                for candidate in c3["methods"][method]["0"]:
                    self.assertEqual(candidate["known_at"], p3["manifest"]["predicted_at"])
                    self.assertIsNone(candidate["premises_known_at"])
                    for witness in candidate["premises_and_provenance"]:
                        self.assertIsNone(witness["properties"]["known_at"])
                        self.assertEqual(witness["properties"]["known_at_status"], "not_supplied_by_oracle")

    def test_simpler_frequency_ranking_has_identical_capability_predictions(self):
        predictions, _ = self.artifacts[RUNS[2]]
        totals = {1: [0, 0, 0], 3: [0, 0, 0]}
        for case in predictions["cases"]:
            mapping = case["methods"]["partial_mapping"]["0"]
            frequency = sorted(mapping, key=lambda p: (-len(p["premises_and_provenance"]), p["label"]))
            for budget in totals:
                labels = [p["label"] for p in frequency[:budget]]
                if case["task"] == "capabilities":
                    self.assertEqual(labels, [p["label"] for p in mapping[:budget]])
                answer = self.gold[case["case_id"]]
                totals[budget][0] += int(answer is not None and answer in labels)
                totals[budget][1] += len(labels)
                totals[budget][2] += int(answer is not None)
        self.assertEqual(totals, {1: [14, 55, 46], 3: [16, 149, 46]})

    def test_preserved_ranking_control_matches_independent_rerank(self):
        directory = HERE / "results" / "synthetic_lopo_v3_ranking_control_first"
        predictions = json.load(gzip.open(directory / "predictions.json.gz", "rt"))
        results = json.load(gzip.open(directory / "results.json.gz", "rt"))
        old_results = self.artifacts[RUNS[2]][1]
        self.assertEqual(predictions["manifest"], results["manifest"])
        for key, path in (("fixture", self.fixture_path),
                          ("policy", directory / "policy_frozen.json"),
                          ("protocol", directory / "protocol_frozen.md"),
                          ("code", directory / "prototype_frozen.py")):
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                             predictions["manifest"]["hashes"][key])
        for group, budgets in old_results["scores"].items():
            cases = [c for c in predictions["cases"] if
                     group == "all" or c["task"] == group or c["project"] == group]
            for budget_text, scores in budgets.items():
                observed = results["scores"][group][budget_text]
                for method, value in scores.items():
                    self.assertEqual(observed[method], value)
                expected = recount(cases, self.gold, "premise_filtered_frequency", int(budget_text))
                self.assertEqual({k: observed["premise_filtered_frequency"][k] for k in expected}, expected)
        for case in predictions["cases"]:
            mapping = case["methods"]["partial_mapping"]["0"]
            expected = sorted(mapping, key=lambda p: (-len(p["premises_and_provenance"]), p["label"]))
            control = case["methods"]["premise_filtered_frequency"]["0"]
            self.assertEqual([p["label"] for p in control], [p["label"] for p in expected])
            for original, reranked in zip(expected, control):
                self.assertEqual(original["premises_and_provenance"], reranked["premises_and_provenance"])

    def test_application_alternative_run_preserves_scores_and_exact_decision_sources(self):
        directory = HERE / "results" / "synthetic_lopo_v4_application_alternatives_first"
        predictions = json.load(gzip.open(directory / "predictions.json.gz", "rt"))
        results = json.load(gzip.open(directory / "results.json.gz", "rt"))
        control_directory = HERE / "results" / "synthetic_lopo_v3_ranking_control_first"
        previous = json.load(gzip.open(control_directory / "results.json.gz", "rt"))
        self.assertEqual(results["scores"], previous["scores"])
        self.assertEqual(predictions["manifest"], results["manifest"])
        for key, path in (("fixture", self.fixture_path),
                          ("policy", directory / "policy_frozen.json"),
                          ("protocol", directory / "protocol_frozen.md"),
                          ("code", directory / "prototype_frozen.py")):
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                             predictions["manifest"]["hashes"][key])
        projects = {p["id"]: p for p in self.fixture["projects"]}
        applications_checked = 0
        for case in predictions["cases"]:
            if case["task"] != "capabilities":
                continue
            for method in ("partial_mapping", "premise_filtered_frequency"):
                for candidate in case["methods"][method]["0"]:
                    for witness in candidate["premises_and_provenance"]:
                        donor = witness["donor_project"]
                        self.assertNotEqual(donor, case["project"])
                        definitions = [d for d in projects[donor].get("decisions", [])
                                       if d.get("operator") == candidate["label"]]
                        applicable = [d for d in definitions if d.get("principle_evidence") and
                                      set(d["principle_evidence"]) <= set(case["visible_edges"]["principles"])]
                        observed = witness["applicable_application_witnesses"]
                        self.assertEqual([a["decision_id"] for a in observed], [d["id"] for d in applicable])
                        self.assertTrue(observed)
                        applications_checked += len(observed)
                        for record, decision in zip(observed, applicable):
                            self.assertEqual(record["principles"], sorted(set(decision["principle_evidence"])))
                            source = record["application_source"]
                            self.assertEqual(source["oracle_path"], f"projects/{donor}/decisions/{decision['id']}")
                            self.assertIsNone(source["known_at"])
                            units = decision.get("units", []) + ([decision["unit"]] if "unit" in decision else [])
                            expected_locators = [{k: unit[k] for k in ("provider", "conv_id", "node_id", "date")
                                                  if k in unit} for unit in units]
                            self.assertEqual(source["source_locators"], expected_locators)
                        self.assertEqual([(p["donor_decision"], p["donor_principle"])
                                          for p in witness["operator_premise_mapping"]],
                                         [(d["id"], p) for d in applicable for p in sorted(set(d["principle_evidence"]))])
        self.assertEqual(applications_checked, 22)


class CounterexampleTests(unittest.TestCase):
    def test_mask_keeps_oracle_principles_from_the_hidden_application(self):
        from loom.tools.seeding.prototype import build_graph, target_view
        project = {"id": "t", "kind": "generic", "decisions": [
            {"id": "answer", "operator": "op.hidden", "principle_evidence": ["pr.signature"]}]}
        target = target_view(build_graph(project, {"op.hidden": {"solution": "hidden"}}),
                             "capabilities", "op.hidden")
        self.assertEqual(target.edges["capabilities"], frozenset())
        self.assertEqual(target.edges["principles"], frozenset({"pr.signature"}))
        self.assertEqual(target.properties, {})

    def test_matching_principles_do_not_check_operator_situation(self):
        from loom.tools.seeding.prototype import Graph, predict
        policy = json.loads((HERE / "policy_local_premises.json").read_text())
        edges = {"roles": frozenset(), "features": frozenset(),
                 "principles": frozenset({"budget"}), "capabilities": frozenset()}
        target = Graph("free_reversible_step", "generic", edges, {})
        donor = Graph("expensive_irreversible_step", "generic",
                      dict(edges, capabilities=frozenset({"gate"})), {
            "capabilities:gate": {"justifying_principles": ["budget"],
                                  "expected_properties": [{"relation": "cost_gate"}], "known_at": None}})
        candidate = predict([donor], target, "capabilities", "partial_mapping", policy,
                            "2026-09-30T00:00:00Z", "counterexample")[0]
        self.assertEqual(candidate["label"], "gate")
        self.assertIn("target-specific applicability", candidate["unverified_properties"])

    def test_distinct_application_premises_remain_alternative_witnesses(self):
        from loom.tools.seeding.prototype import Graph, build_graph, predict
        policy = json.loads((HERE / "policy_local_premises.json").read_text())
        policy = dict(policy, capability_mapping="preserve_application_alternatives")
        project = {"id": "donor", "kind": "generic", "decisions": [
            {"id": "first", "operator": "shared", "principle_evidence": ["pA"]},
            {"id": "second", "operator": "shared", "principle_evidence": ["pB"]}]}
        donor = build_graph(project, {"shared": {"solution": "same operator"}})
        self.assertEqual(donor.properties["capabilities:shared"]["justifying_principles"], ["pA", "pB"])
        target = Graph("target", "generic", {"roles": frozenset(), "features": frozenset(),
                       "capabilities": frozenset(), "principles": frozenset({"pA"})}, {})
        candidates = predict([donor], target, "capabilities", "partial_mapping", policy,
                             "2026-09-30T00:00:00Z", "counterexample")
        self.assertEqual([c["label"] for c in candidates], ["shared"])
        witness = candidates[0]["premises_and_provenance"][0]
        preserved = witness["operator_premise_mapping"]
        self.assertEqual({p["donor_principle"] for p in preserved}, {"pA"})
        self.assertEqual({p["donor_decision"] for p in preserved}, {"first"})
        self.assertEqual({a["decision_id"] for a in witness["properties"]["application_premise_alternatives"]},
                         {"first", "second"})
        self.assertEqual([a["decision_id"] for a in witness["applicable_application_witnesses"]], ["first"])
        self.assertEqual(preserved[0]["application_source"]["oracle_path"], "projects/donor/decisions/first")
        self.assertIsNone(preserved[0]["application_source"]["known_at"])

    def test_an_application_conjunction_still_requires_all_its_premises(self):
        from loom.tools.seeding.prototype import Graph, build_graph, predict
        policy = json.loads((HERE / "policy_local_premises.json").read_text())
        policy = dict(policy, capability_mapping="preserve_application_alternatives")
        project = {"id": "donor", "kind": "generic", "decisions": [
            {"id": "together", "operator": "shared", "principle_evidence": ["pA", "pB"]}]}
        donor = build_graph(project, {"shared": {"solution": "same operator"}})
        target = Graph("target", "generic", {"roles": frozenset(), "features": frozenset(),
                       "capabilities": frozenset(), "principles": frozenset({"pA"})}, {})
        self.assertEqual(predict([donor], target, "capabilities", "partial_mapping", policy,
                                "2026-09-30T00:00:00Z", "counterexample"), [])


if __name__ == "__main__":
    unittest.main()
