"""Mechanism and adversarial tests; these do not measure semantic model quality."""
import copy
import gzip
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from loom.tools.seeding.prototype import (Graph, POLICY, build_graph, experiment, mapping,
    metrics, predict, random_expectation, target_view)


POLICY_DATA = json.loads(POLICY.read_text())
WHEN = "2026-09-29T23:00:00+00:00"


def graph(project, roles=(), capabilities=(), principles=(), features=()):
    edges = {"roles": frozenset(roles), "capabilities": frozenset(capabilities),
             "principles": frozenset(principles), "features": frozenset(features)}
    props = {f"{task}:{label}": {
        "expected_properties": [{"relation": task, "label": label, "requires_validation": True}],
        "known_at": "2025-01-01T00:00:00Z", "oracle_path": f"{project}/{task}/{label}"}
        for task in ("roles", "capabilities", "features") for label in edges[task]}
    return Graph(project, "generic", edges, props)


class PrototypeTests(unittest.TestCase):
    def test_mapping_records_exact_preserved_relations(self):
        target = target_view(graph("target", roles=("intent",)), "roles", None)
        donor = graph("donor", roles=("intent", "resource"))
        aligned = mapping(target, donor, POLICY_DATA)
        self.assertEqual(aligned["score"], 0.5)
        self.assertEqual([(e["relation"], e["label"]) for e in aligned["preserved_relations"]],
                         [("roles", "intent")])

    def test_only_donor_vocabulary_can_be_emitted(self):
        target = target_view(graph("target", roles=("intent",), features=("secret",)), "features", "secret")
        donor = graph("donor", roles=("intent",), features=("public",))
        candidates = predict([donor], target, "features", "partial_mapping", POLICY_DATA, WHEN, "case")
        self.assertEqual([c["label"] for c in candidates], ["public"])
        self.assertNotIn("secret", json.dumps(candidates))

    def test_no_anchor_abstains_while_frequency_can_win(self):
        target = target_view(graph("target", roles=("resource",)), "roles", "resource")
        donor = graph("donor", roles=("resource",))
        structural = predict([donor], target, "roles", "partial_mapping", POLICY_DATA, WHEN, "case")
        frequency = predict([donor], target, "roles", "most_frequent", POLICY_DATA, WHEN, "case")
        self.assertEqual(structural, [])
        self.assertEqual(frequency[0]["label"], "resource")

    def test_target_cannot_be_a_donor(self):
        target = target_view(graph("same", roles=("intent",)), "roles", None)
        with self.assertRaisesRegex(ValueError, "leaked"):
            predict([graph("same")], target, "roles", "partial_mapping", POLICY_DATA, WHEN, "case")

    def test_full_target_properties_rejected(self):
        with self.assertRaisesRegex(ValueError, "oracle properties"):
            predict([], graph("target", roles=("intent",)), "roles", "partial_mapping", POLICY_DATA, WHEN, "case")

    def test_alternatives_carry_expectations_and_donor_provenance(self):
        target = target_view(graph("target", roles=("intent",)), "roles", None)
        donor = graph("donor", roles=("intent", "resource", "part"))
        candidates = predict([donor], target, "roles", "partial_mapping", POLICY_DATA, WHEN, "case")
        self.assertEqual(len(candidates), 2)
        for candidate in candidates:
            self.assertEqual(candidate["evidence_class"], "extrapolated")
            self.assertEqual(candidate["validation_status"], "candidate")
            self.assertEqual(candidate["known_at"], WHEN)
            self.assertEqual(candidate["premises_known_at"], "2025-01-01T00:00:00Z")
            self.assertEqual(candidate["predicted_at"], WHEN)
            self.assertIsNone(candidate["assessment"]["calibrated_probability"])
            alt = candidate["alternatives"][0]
            self.assertTrue(alt["expected_properties"])
            self.assertEqual(alt["premises_and_provenance"][0]["donor_project"], "donor")

    def test_redaction_ignores_adversarial_target_prose_and_hidden_properties(self):
        project = {"id": "target", "kind": "generic", "universal_roles": {
            "intent": {"observed": [{"quote": "unrelated", "date": "2024-01-01"}]}},
            "features_status": [{"id": "private", "label": "hidden", "units": [{"quote": "secret"}]}]}
        a = target_view(build_graph(project, {}), "features", "hidden")
        poisoned = copy.deepcopy(project)
        poisoned["features_status"][0]["units"] = [{"quote": "LEAK secret answer", "date": "2999-01-01"}]
        poisoned["name"] = "LEAK hidden"
        poisoned["universal_roles"]["intent"]["observed"][0]["quote"] = "explicitly says hidden"
        b = target_view(build_graph(poisoned, {}), "features", "hidden")
        self.assertEqual(a, b)
        donor = graph("donor", roles=("intent",), features=("available",))
        self.assertEqual(predict([donor], a, "features", "partial_mapping", POLICY_DATA, WHEN, "case"),
                         predict([donor], b, "features", "partial_mapping", POLICY_DATA, WHEN, "case"))

    def test_annotated_inferable_and_absent_roles_never_become_input(self):
        project = {"id": "target", "kind": "generic", "universal_roles": {
            "part": {"observed": [], "inferable": [{"value": "hidden module"}]},
            "check": {"observed": [], "absent": [{"question": "tests?"}]}}}
        self.assertEqual(build_graph(project, {}).edges["roles"], frozenset())

    def test_precision_includes_negative_controls_and_recall_has_own_denominator(self):
        cases = [{"case_id": "positive", "methods": {"most_frequent": {"0": [{"label": "right"}]}}},
                 {"case_id": "negative", "methods": {"most_frequent": {"0": [{"label": "extra"}]}}}]
        score = metrics(cases, {"positive": "right", "negative": None}, "most_frequent", 1)
        self.assertEqual((score["tp"], score["emitted"], score["hidden_elements"]), (1, 2, 1))
        self.assertEqual(score["precision"], 0.5)
        self.assertEqual(score["recall"], 1.0)

    def test_random_expectation_handles_cold_vocabulary_and_negative_controls(self):
        cases = [{"case_id": k, "methods": {"random": {"0": [{"label": "a"}, {"label": "b"}]}}}
                 for k in ("hit", "cold", "negative")]
        score = random_expectation(cases, {"hit": "a", "cold": "unseen", "negative": None}, 1)
        self.assertEqual(score["expected_tp"], 0.5)
        self.assertEqual(score["hidden_elements"], 2)
        self.assertEqual(score["emitted"], 3)

    def test_random_order_deterministic_and_preserves_candidate_set(self):
        target = target_view(graph("target", roles=("intent",)), "roles", None)
        donor = graph("donor", roles=("intent", "resource", "part", "check"))
        a = predict([donor], target, "roles", "random", POLICY_DATA, WHEN, "case", 31)
        b = predict([donor], target, "roles", "random", POLICY_DATA, WHEN, "case", 31)
        self.assertEqual(a, b)
        self.assertEqual({x["label"] for x in a}, {"resource", "part", "check"})

    def test_no_input_mutation(self):
        donor = graph("donor", roles=("intent", "resource"))
        target = target_view(graph("target", roles=("intent",)), "roles", None)
        before = copy.deepcopy((donor, target, POLICY_DATA))
        predict([donor], target, "roles", "partial_mapping", POLICY_DATA, WHEN, "case")
        self.assertEqual(before, (donor, target, POLICY_DATA))

    def test_local_premises_prevent_irrelevant_shared_roles_from_justifying_operator(self):
        donor = graph("donor", roles=("intent",), capabilities=("cost_gate",), principles=("cost",))
        donor.properties["capabilities:cost_gate"]["justifying_principles"] = ["cost"]
        target = target_view(graph("target", roles=("intent",), principles=("different",)), "capabilities", None)
        policy = dict(POLICY_DATA, capability_mapping="preserve_justifying_principles")
        self.assertEqual(predict([donor], target, "capabilities", "partial_mapping", policy, WHEN, "case"), [])
        # Other channels still propose: the policy is not a cross-channel veto.
        self.assertEqual(predict([donor], target, "capabilities", "most_frequent", policy, WHEN, "case")[0]["label"], "cost_gate")

    def test_local_mapping_preserves_all_required_premises(self):
        donor = graph("donor", capabilities=("gate",), principles=("cost", "owner"))
        donor.properties["capabilities:gate"]["justifying_principles"] = ["cost", "owner"]
        policy = dict(POLICY_DATA, capability_mapping="preserve_justifying_principles")
        partial = target_view(graph("target", principles=("cost",)), "capabilities", None)
        self.assertEqual(predict([donor], partial, "capabilities", "partial_mapping", policy, WHEN, "case"), [])
        target = target_view(graph("target", principles=("cost", "owner")), "capabilities", None)
        candidate = predict([donor], target, "capabilities", "partial_mapping", policy, WHEN, "case")[0]
        preserved = candidate["premises_and_provenance"][0]["operator_premise_mapping"]
        self.assertEqual({p["donor_principle"] for p in preserved}, {"cost", "owner"})

    def test_empty_local_premises_abstain_not_vacuously_true(self):
        donor = graph("donor", roles=("intent",), capabilities=("unknown",))
        target = target_view(graph("target", roles=("intent",)), "capabilities", None)
        policy = dict(POLICY_DATA, capability_mapping="preserve_justifying_principles")
        self.assertEqual(predict([donor], target, "capabilities", "partial_mapping", policy, WHEN, "case"), [])

    def test_outputs_precede_scoring_and_run_is_immutable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / "fixture.json"
            fixture.write_text(json.dumps({"operators": [], "projects": [
                {"id": name, "kind": "generic", "universal_roles": {
                    "intent": {"observed": [{"date": "2024-01-01", "quote": name}]}}}
                for name in ("a", "b")]}))
            policy = root / "policy.json"
            policy.write_text(json.dumps(dict(POLICY_DATA, candidate_budgets=[1], random_seeds=[0, 1])))
            protocol = root / "protocol.md"
            protocol.write_text("frozen")
            output = root / "run"
            original_metrics = metrics
            call_count = 0
            def verify_boundary(*args, **kwargs):
                nonlocal call_count
                call_count += 1
                self.assertTrue((output / "predictions.json.gz").exists())
                self.assertFalse((output / "results.json.gz").exists())
                predictions = json.load(gzip.open(output / "predictions.json.gz", "rt"))
                self.assertNotIn("answers", predictions)
                return original_metrics(*args, **kwargs)
            before = fixture.read_bytes()
            with patch("loom.tools.seeding.prototype.FIXTURE", fixture), \
                    patch("loom.tools.seeding.prototype.metrics", side_effect=verify_boundary):
                experiment(output, policy, protocol)
                with self.assertRaises(FileExistsError):
                    experiment(output, policy, protocol)
            self.assertGreater(call_count, 0)
            self.assertEqual(fixture.read_bytes(), before)
            self.assertTrue((output / "results.json.gz").exists())
            self.assertEqual((output / "protocol_frozen.md").read_text(), "frozen")

    def test_random_compressed_repetition_uses_same_denominators(self):
        cases = [{"case_id": "p", "methods": {"random": {"0": [{"label": "a"}, {"label": "b"}],
                                                          "1": ["a", "b"]}}}]
        self.assertEqual(metrics(cases, {"p": "a"}, "random", 1, 0),
                         metrics(cases, {"p": "a"}, "random", 1, 1))

    def test_source_creation_date_does_not_fabricate_historical_know_time(self):
        project = {"id": "donor", "kind": "generic", "universal_roles": {
            "intent": {"observed": [{"date": "2024-01-01", "quote": "source"}]}}}
        donor = build_graph(project, {})
        properties = donor.properties["roles:intent"]
        self.assertIsNone(properties["known_at"])
        self.assertEqual(properties["source_created_at_max"], "2024-01-01")
        target = target_view(graph("target"), "roles", None)
        candidate = predict([donor], target, "roles", "most_frequent", POLICY_DATA, WHEN, "case")[0]
        self.assertEqual(candidate["known_at"], WHEN)
        self.assertIsNone(candidate["premises_known_at"])

    def test_comparable_frequency_preserves_mapping_candidate_pool(self):
        target = target_view(graph("target", roles=("intent",), principles=("cost",)), "capabilities", None)
        donor = graph("donor", roles=("intent",), capabilities=("valid", "irrelevant"), principles=("cost", "other"))
        donor.properties["capabilities:valid"]["justifying_principles"] = ["cost"]
        donor.properties["capabilities:irrelevant"]["justifying_principles"] = ["other"]
        policy = dict(POLICY_DATA, capability_mapping="preserve_justifying_principles")
        weighted = predict([donor], target, "capabilities", "partial_mapping", policy, WHEN, "case")
        control = predict([donor], target, "capabilities", "premise_filtered_frequency", policy, WHEN, "case")
        self.assertEqual([p["label"] for p in weighted], ["valid"])
        self.assertEqual([p["label"] for p in control], ["valid"])
        self.assertEqual(weighted[0]["premises_and_provenance"], control[0]["premises_and_provenance"])
        unanchored = target_view(graph("target"), "capabilities", None)
        self.assertEqual(predict([donor], unanchored, "capabilities", "premise_filtered_frequency", policy, WHEN, "case"), [])

    def test_comparable_frequency_can_reverse_weighted_order_on_same_pool(self):
        target = target_view(graph("target", roles=("intent",)), "roles", None)
        close = graph("near", roles=("intent", "rare"))
        far_a = graph("far_a", roles=("intent", "common"), features=tuple(f"x{i}" for i in range(10)))
        far_b = graph("far_b", roles=("intent", "common"), features=tuple(f"y{i}" for i in range(10)))
        donors = [close, far_a, far_b]
        weighted = predict(donors, target, "roles", "partial_mapping", POLICY_DATA, WHEN, "case")
        control = predict(donors, target, "roles", "premise_filtered_frequency", POLICY_DATA, WHEN, "case")
        self.assertEqual({p["label"] for p in weighted}, {p["label"] for p in control})
        self.assertEqual(weighted[0]["label"], "rare")
        self.assertEqual(control[0]["label"], "common")
        self.assertEqual(control[0]["assessment"]["score_kind"], "donor_count")

    def test_application_alternatives_preserve_separate_derivations(self):
        donor_project = {"id": "donor", "kind": "generic", "decisions": [
            {"id": "first", "operator": "shared", "principle_evidence": ["pA"],
             "unit": {"provider": "synthetic", "conv_id": "first-source", "date": "2024-01-01"}},
            {"id": "second", "operator": "shared", "principle_evidence": ["pB"],
             "unit": {"provider": "synthetic", "conv_id": "second-source", "date": "2024-02-01"}}]}
        donor = build_graph(donor_project, {"shared": {"solution": "same class"}})
        policy = dict(POLICY_DATA, capability_mapping="preserve_application_alternatives")
        for principles, expected_decisions in [(('pA',), {'first'}), (('pB',), {'second'}),
                                                (('pA', 'pB'), {'first', 'second'})]:
            target = target_view(graph("target", principles=principles), "capabilities", None)
            candidate = predict([donor], target, "capabilities", "partial_mapping", policy, WHEN, "case")[0]
            self.assertEqual(candidate["assessment"]["supporting_donor_projects"], 1)
            witness = candidate["premises_and_provenance"][0]
            self.assertEqual({a["decision_id"] for a in witness["applicable_application_witnesses"]}, expected_decisions)
            self.assertEqual({e["donor_decision"] for e in witness["operator_premise_mapping"]}, expected_decisions)
            self.assertEqual(len(witness["properties"]["application_premise_alternatives"]), 2)
            self.assertTrue(all(e["application_source"]["source_locators"] for e in witness["operator_premise_mapping"]))
        target = target_view(graph("target", principles=('neither',)), "capabilities", None)
        self.assertEqual(predict([donor], target, "capabilities", "partial_mapping", policy, WHEN, "case"), [])
        # Historical union remains reproducible as an explicit competing mode.
        legacy = dict(POLICY_DATA, capability_mapping="preserve_justifying_principles")
        target = target_view(graph("target", principles=('pA',)), "capabilities", None)
        self.assertEqual(predict([donor], target, "capabilities", "partial_mapping", legacy, WHEN, "case"), [])

    def test_alternative_mode_does_not_split_one_conjunction_or_accept_unknown_premises(self):
        policy = dict(POLICY_DATA, capability_mapping="preserve_application_alternatives")
        for evidence in [['pA', 'pB'], []]:
            donor_project = {"id": "donor", "kind": "generic", "decisions": [
                {"id": "one", "operator": "shared", "principle_evidence": evidence}]}
            donor = build_graph(donor_project, {"shared": {"solution": "same class"}})
            target = target_view(graph("target", roles=('intent',), principles=('pA',)), "capabilities", None)
            self.assertEqual(predict([donor], target, "capabilities", "partial_mapping", policy, WHEN, "case"), [])

    def test_unknown_capability_policy_cannot_silently_disable_filter(self):
        donor = graph("donor", roles=('intent',), capabilities=('unsupported',))
        target = target_view(graph("target", roles=('intent',)), "capabilities", None)
        policy = dict(POLICY_DATA, capability_mapping='misspelled_policy')
        with self.assertRaisesRegex(ValueError, 'refusing unfiltered fallback'):
            predict([donor], target, "capabilities", "partial_mapping", policy, WHEN, "case")


if __name__ == "__main__":
    unittest.main()
