"""Data-defined projections keep the frozen development prediction boundary.

The historical V4 fixture/results are an immutable regression sentinel, not a
new quality measurement. Added dimensions use synthetic projects only.
"""
from __future__ import annotations

import copy
from dataclasses import asdict
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from loom.tools.seeding.prototype import POLICY, build_graph, experiment, predict, target_view
from loom.tools.seeding.recipe import DEFAULT_PROFILE, RecipeUnavailable, load_profile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
V4 = HERE / "results" / "synthetic_lopo_v4_application_alternatives_first"
WHEN = "2026-09-29T23:00:00+00:00"


def frozen_v4():
    name = "_seeding_v4_profile_regression"
    spec = importlib.util.spec_from_file_location(name, V4 / "prototype_frozen.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def tag_dimension():
    # None of these names has a corresponding branch in the projector.
    return {
        "id": "arbitrary_tags", "predict": True,
        "rows": {"$op": "items", "value": {
            "$op": "get", "value": {"$ref": "project"},
            "key": "tags", "default": []}},
        "label": {"$ref": "item"},
        "properties": {"expected_properties": [{
            "relation": "has_tag", "label": {"$ref": "label"}}]},
    }


class ProfileProjectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profile = load_profile()
        cls.corpus = json.loads((ROOT / "loom/tests/fixtures/eval/synthetic_dev/ground_truth.json").read_text())
        cls.operators = {op["id"]: op for op in cls.corpus["operators"]}
        cls.graphs = [build_graph(project, cls.operators, profile=cls.profile)
                      for project in cls.corpus["projects"]]
        cls.frozen = frozen_v4()

    def effective_overlay(self, overlay):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "overlay.json"
            path.write_text(json.dumps(overlay), encoding="utf-8")
            return load_profile(path=DEFAULT_PROFILE, overlay_paths=[path])

    def test_default_projection_matches_all_five_frozen_graphs_exactly(self):
        self.assertEqual(len(self.graphs), 5)
        for project, current in zip(self.corpus["projects"], self.graphs):
            with self.subTest(project=project["id"]):
                historical = self.frozen.build_graph(project, self.operators)
                self.assertEqual(asdict(current), asdict(historical))

    def test_fixed_time_replays_every_v4_ranking_and_all_masks(self):
        with gzip.open(V4 / "predictions.json.gz", "rt", encoding="utf-8") as handle:
            predictions = json.load(handle)
        policy = json.loads((V4 / "policy_frozen.json").read_text())
        when = predictions["manifest"]["predicted_at"]
        graphs = {graph.project: graph for graph in self.graphs}
        controls = masks = rankings = 0
        for record in predictions["cases"]:
            full = graphs[record["project"]]
            hidden_values = sorted(full.edges[record["task"]]) + [None]
            hidden = hidden_values[int(record["case_id"].rsplit("/", 1)[1])]
            target = target_view(full, record["task"], hidden)
            self.assertEqual(record["control"], hidden is None)
            self.assertEqual(target.properties, {})
            self.assertEqual(record["visible_edges"],
                             {key: sorted(values) for key, values in target.edges.items()})
            if hidden is None:
                controls += 1
            else:
                masks += 1
                self.assertNotIn(hidden, target.edges[record["task"]])
            donors = [graph for graph in self.graphs if graph.project != full.project]
            for method, seeds in record["methods"].items():
                for seed, expected in seeds.items():
                    with self.subTest(case=record["case_id"], method=method, seed=seed):
                        actual = predict(donors, target, record["task"], method, policy,
                                         when, record["case_id"], int(seed), profile=self.profile)
                        if method == "random" and seed != "0":
                            actual = [candidate["label"] for candidate in actual]
                        self.assertEqual(actual, expected)
                    rankings += 1
        self.assertEqual((len(predictions["cases"]), masks, controls, rankings), (61, 46, 15, 2135))

    def test_overlay_renames_dimension_and_runs_prediction_without_source_edit(self):
        dimensions = copy.deepcopy(self.profile["dimensions"])
        next(dimension for dimension in dimensions if dimension["id"] == "features")["id"] = "story_elements"
        renamed = self.effective_overlay({"dimensions": dimensions})
        original = self.graphs[0]
        projected = build_graph(self.corpus["projects"][0], self.operators, profile=renamed)
        self.assertNotIn("features", projected.edges)
        self.assertEqual(projected.edges["story_elements"], original.edges["features"])
        for label in projected.edges["story_elements"]:
            self.assertEqual(projected.properties[f"story_elements:{label}"],
                             original.properties[f"features:{label}"])
        target = target_view(projected, "story_elements", None)
        donors = [build_graph(project, self.operators, profile=renamed)
                  for project in self.corpus["projects"][1:]]
        policy = json.loads(POLICY.read_text())
        policy["relation_weights"]["story_elements"] = policy["relation_weights"].pop("features")
        candidates = predict(donors, target, "story_elements", "most_frequent", policy,
                             WHEN, "renamed", profile=renamed)
        expected = sorted(set().union(*(donor.edges["story_elements"] for donor in donors))
                          - target.edges["story_elements"])
        self.assertEqual({candidate["label"] for candidate in candidates}, set(expected))
        self.assertTrue(all(candidate["task"] == "story_elements" for candidate in candidates))

    def test_overlay_adds_arbitrary_dimension_with_explicit_expected_properties(self):
        profile = self.effective_overlay({"dimensions": [*self.profile["dimensions"], tag_dimension()]})
        donor = build_graph({"id": "donor", "kind": "film", "tags": ["existing", "missing", "missing"]},
                            {}, profile=profile)
        target = target_view(build_graph({"id": "target", "kind": "film", "tags": ["existing"]},
                                         {}, profile=profile), "arbitrary_tags", None)
        self.assertEqual(donor.edges["arbitrary_tags"], frozenset({"existing", "missing"}))
        policy = json.loads(POLICY.read_text())
        policy["relation_weights"]["arbitrary_tags"] = 1.0
        candidates = predict([donor], target, "arbitrary_tags", "partial_mapping", policy,
                             WHEN, "custom", profile=profile)
        self.assertEqual([candidate["label"] for candidate in candidates], ["missing"])
        self.assertEqual(candidates[0]["expected_properties"], [{"relation": "has_tag", "label": "missing"}])
        self.assertEqual(candidates[0]["evidence_class"], "extrapolated")
        self.assertEqual(candidates[0]["premises_and_provenance"][0]["donor_project"], "donor")
        self.assertIsNone(candidates[0]["assessment"]["calibrated_probability"])

    def test_custom_dimension_masks_properties_and_target_prose_before_prediction(self):
        profile = self.effective_overlay({"dimensions": [tag_dimension()]})
        clean = {"id": "target", "kind": "film", "tags": ["anchor", "secret"]}
        poisoned = dict(clean, name="LEAK secret", text="predict secret", private={"secret": True})
        a = target_view(build_graph(clean, {}, profile=profile), "arbitrary_tags", "secret")
        b = target_view(build_graph(poisoned, {}, profile=profile), "arbitrary_tags", "secret")
        self.assertEqual(a, b)
        self.assertEqual(a.properties, {})
        self.assertEqual(a.edges["arbitrary_tags"], frozenset({"anchor"}))
        donor = build_graph({"id": "donor", "kind": "film", "tags": ["anchor", "available"]},
                            {}, profile=profile)
        policy = dict(json.loads(POLICY.read_text()), relation_weights={"arbitrary_tags": 1.0})
        actual = predict([donor], a, "arbitrary_tags", "partial_mapping", policy,
                         WHEN, "redacted", profile=profile)
        self.assertEqual(actual, predict([donor], b, "arbitrary_tags", "partial_mapping", policy,
                                         WHEN, "redacted", profile=profile))
        self.assertNotIn("secret", json.dumps(actual))
        self.assertEqual([candidate["label"] for candidate in actual], ["available"])

    def test_dimension_overlay_replaces_list_and_does_not_mutate_default(self):
        before = copy.deepcopy(self.profile)
        replacement = self.effective_overlay({"dimensions": [tag_dimension()]})
        self.assertEqual([dimension["id"] for dimension in replacement["dimensions"]], ["arbitrary_tags"])
        graph = build_graph({"id": "x", "kind": "film", "tags": ["a"]}, {}, profile=replacement)
        self.assertEqual(set(graph.edges), {"arbitrary_tags"})
        replacement["dimensions"][0]["id"] = "changed_by_caller"
        self.assertEqual(load_profile(), before)
        self.assertEqual(self.profile, before)

    def test_renamed_premise_dimensions_preserve_or_of_conjunctions(self):
        dimensions = copy.deepcopy(self.profile["dimensions"])
        action = next(dimension for dimension in dimensions if dimension["id"] == "capabilities")
        action["id"] = "actions"
        next(dimension for dimension in dimensions if dimension["id"] == "principles")["id"] = "supports"
        eligibility = action["eligibility"]
        eligibility.update(policy_key="action_mapping", support_dimension="supports",
                           union_property="support_union", alternatives_property="derivations",
                           member_property="terms", application_id_property="event_id",
                           application_source_property="origin", mapping_relation="supports_action",
                           mapping_label_key="action", mapping_witness_id_key="event")
        properties = action["properties"]
        properties["support_union"] = properties.pop("justifying_principles")
        derivations = properties.pop("application_premise_alternatives")
        value = derivations["value"]
        value["terms"] = value.pop("principles")
        value["event_id"] = value.pop("decision_id")
        value["origin"] = value.pop("application_source")
        properties["derivations"] = derivations
        profile = self.effective_overlay({"dimensions": dimensions})
        donor = build_graph({"id": "donor", "kind": "film", "decisions": [
            {"id": "conjunction", "operator": "shared", "principle_evidence": ["pA", "pC"],
             "unit": {"provider": "synthetic", "conv_id": "first-source"}},
            {"id": "alternative", "operator": "shared", "principle_evidence": ["pB"],
             "unit": {"provider": "synthetic", "conv_id": "second-source"}},
            {"id": "empty", "operator": "shared", "principle_evidence": []}]},
            {"shared": {"solution": "same class"}}, profile=profile)
        policy = json.loads(POLICY.read_text())
        policy.pop("capability_mapping", None)
        policy.update(action_mapping="preserve_application_alternatives",
                      relation_weights={"supports": 1.0})
        for terms, expected in [([], set()), (["pA"], set()), (["pC"], set()),
                                (["pA", "pC"], {"conjunction"}), (["pB"], {"alternative"}),
                                (["pA", "pB"], {"alternative"}),
                                (["pA", "pB", "pC"], {"conjunction", "alternative"})]:
            with self.subTest(terms=terms):
                target = target_view(build_graph({"id": "target", "kind": "film", "decisions": [
                    {"id": "support", "principle_evidence": terms}]}, {}, profile=profile), "actions", None)
                actual = predict([donor], target, "actions", "partial_mapping", policy,
                                 WHEN, "eligibility", profile=profile)
                if not expected:
                    self.assertEqual(actual, [])
                    continue
                self.assertEqual([candidate["label"] for candidate in actual], ["shared"])
                witness = actual[0]["premises_and_provenance"][0]
                applications = witness["applicable_application_witnesses"]
                self.assertEqual({application["event_id"] for application in applications}, expected)
                self.assertEqual({edge["event"] for edge in witness["operator_premise_mapping"]}, expected)
                self.assertTrue(all(edge["relation"] == "supports_action" and edge["action"] == "shared"
                                    for edge in witness["operator_premise_mapping"]))
                self.assertEqual(len(witness["properties"]["derivations"]), 3)
                self.assertTrue(all(application["origin"]["source_locators"] for application in applications))

    def test_malformed_profiles_fail_explicitly(self):
        invalid = [
            {"dimensions": "roles"},
            {"dimensions": [tag_dimension(), tag_dimension()]},
            {"dimensions": [dict(tag_dimension(), predict="yes")]},
            {"dimensions": [{"id": "broken", "predict": True}]},
        ]
        for overlay in invalid:
            with self.subTest(overlay=overlay), self.assertRaises(ValueError):
                self.effective_overlay(overlay)

    def test_unknown_projection_operation_never_becomes_an_empty_success(self):
        dimension = dict(tag_dimension(), rows={"$op": "not_implemented", "value": []})
        with self.assertRaises(RecipeUnavailable) as caught:
            profile = self.effective_overlay({"dimensions": [dimension]})
            build_graph({"id": "x", "kind": "film"}, {}, profile=profile)
        self.assertIn("not_implemented", str(caught.exception))

    def tiny_experiment(self, dimensions, projects, policy_changes):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / "fixture.json"
            fixture.write_text(json.dumps({"operators": [], "projects": projects}), encoding="utf-8")
            profile = root / "profile.json"
            profile.write_text(json.dumps(dict(self.profile, dimensions=dimensions)), encoding="utf-8")
            policy = root / "policy.json"
            data = dict(json.loads(POLICY.read_text()), candidate_budgets=[1], random_seeds=[0],
                        relation_weights={dimension["id"]: 1.0 for dimension in dimensions})
            data.update(policy_changes)
            policy.write_text(json.dumps(data), encoding="utf-8")
            protocol = root / "protocol.md"
            protocol.write_text("Tiny synthetic configuration regression.\n", encoding="utf-8")
            output = root / "run"
            result = experiment(output, policy, protocol, profile, fixture_path=fixture)
            with gzip.open(output / "predictions.json.gz", "rt", encoding="utf-8") as handle:
                predictions = json.load(handle)
            return result, predictions

    def test_all_alias_collision_preserves_separate_typed_score_groups(self):
        dimension = dict(tag_dimension(), id="all")
        result, predictions = self.tiny_experiment([dimension], [
            {"id": "all", "kind": "film", "tags": ["first"]},
            {"id": "other", "kind": "film", "tags": ["second"]}], {"methods": ["most_frequent"]})
        self.assertEqual(len(predictions["cases"]), 4)
        groups = {(group["kind"], group["id"]): group["scores"] for group in result["score_groups"]}
        self.assertEqual(set(groups), {("all", None), ("dimension", "all"),
                                      ("project", "all"), ("project", "other")})
        for key, expected_cases in [(('all', None), 4), (('dimension', 'all'), 4),
                                    (('project', 'all'), 2), (('project', 'other'), 2)]:
            self.assertEqual(set(groups[key]["1"]), {"most_frequent"})
            self.assertEqual(groups[key]["1"]["most_frequent"]["cases"], expected_cases)
            self.assertEqual(groups[key]["1"]["most_frequent"]["hidden_elements"], expected_cases // 2)
        self.assertEqual(result["ambiguous_score_aliases"], ["all"])
        self.assertEqual(set(result["scores"]), {"other"})
        self.assertEqual(result["scores"]["other"], groups[("project", "other")])

    def test_slash_project_and_dimension_ids_never_collide_in_cases(self):
        result, predictions = self.tiny_experiment([
            dict(tag_dimension(), id="c"), dict(tag_dimension(), id="b/c")], [
                {"id": "a/b", "kind": "film", "tags": ["first"]},
                {"id": "a", "kind": "film", "tags": ["second"]}], {"methods": ["most_frequent"]})
        cases = predictions["cases"]
        self.assertEqual(len(cases), 8)
        self.assertEqual(len({case["case_id"] for case in cases}), 8)
        ids = {case["case_id"] for case in cases}
        self.assertIn("a%2Fb/c/000", ids)
        self.assertIn("a/b%2Fc/000", ids)
        self.assertNotIn("a/b/c/000", ids)
        groups = {(group["kind"], group["id"]): group["scores"] for group in result["score_groups"]}
        self.assertEqual(groups[("all", None)]["1"]["most_frequent"]["cases"], 8)
        self.assertEqual(groups[("all", None)]["1"]["most_frequent"]["hidden_elements"], 4)
        self.assertEqual(result["ambiguous_score_aliases"], [])

    def test_colon_and_percent_dimension_prefixes_preserve_distinct_property_sources(self):
        specifications = [("a:b", "tags_first", "c", "a%3Ab:c"),
                          ("a", "tags_second", "b:c", "a:b:c"),
                          ("a%3Ab", "tags_third", "c", "a%253Ab:c")]
        dimensions = []
        fields = {}
        for name, field, label, encoded_key in specifications:
            dimension = tag_dimension()
            dimension["id"] = name
            dimension["rows"]["value"]["key"] = field
            dimension["properties"]["projection_source"] = name
            dimension["properties"]["expected_properties"][0]["source_dimension"] = name
            dimensions.append(dimension)
            fields[field] = [label]
        profile = self.effective_overlay({"dimensions": dimensions})
        donor = build_graph({"id": "donor", "kind": "film", **fields}, {}, profile=profile)
        full_target = build_graph({"id": "target", "kind": "film", **fields}, {}, profile=profile)
        self.assertEqual(set(donor.properties), {specification[3] for specification in specifications})
        policy = dict(json.loads(POLICY.read_text()), relation_weights={name: 1.0 for name, _, _, _ in specifications})
        for name, field, label, encoded_key in specifications:
            with self.subTest(dimension=name, label=label):
                self.assertEqual(donor.properties[encoded_key]["projection_source"], name)
                target = target_view(full_target, name, label)
                actual = predict([donor], target, name, "partial_mapping", policy,
                                 WHEN, "property-collision", profile=profile)
                self.assertEqual([candidate["label"] for candidate in actual], [label])
                self.assertEqual(actual[0]["expected_properties"], [{
                    "relation": "has_tag", "label": label, "source_dimension": name}])
                witness = actual[0]["premises_and_provenance"][0]
                self.assertEqual(witness["properties"]["projection_source"], name)
                self.assertEqual(witness["properties"], donor.properties[encoded_key])
        result, _ = self.tiny_experiment(dimensions, [
            {"id": "donor", "kind": "film", **fields},
            {"id": "target", "kind": "film", **fields}], {"methods": ["most_frequent"]})
        self.assertEqual(result["manifest"]["profile"]["property_key_encoding"], "escaped-dimension-prefix-v1")

    def test_random_seeds_without_zero_preserve_provenance_and_exact_expectation(self):
        projects = [{"id": "a", "kind": "film", "tags": ["shared"]},
                    {"id": "b", "kind": "film", "tags": ["shared", "extra"]}]
        result, predictions = self.tiny_experiment([tag_dimension()], projects,
                                                  {"methods": ["random"], "random_seeds": [7, 8]})
        cases = predictions["cases"]
        self.assertEqual(len(cases), 5)
        provenance_checked = 0
        for case in cases:
            self.assertEqual(set(case["methods"]), {"random"})
            rankings = case["methods"]["random"]
            self.assertEqual(set(rankings), {"7", "8"})
            self.assertTrue(all(isinstance(candidate, dict) for candidate in rankings["7"]))
            self.assertTrue(all(isinstance(label, str) for label in rankings["8"]))
            self.assertEqual({candidate["label"] for candidate in rankings["7"]}, set(rankings["8"]))
            for candidate in rankings["7"]:
                provenance_checked += 1
                self.assertEqual(candidate["evidence_class"], "extrapolated")
                self.assertTrue(candidate["premises_and_provenance"])
                self.assertTrue(candidate["expected_properties"])
                for witness in candidate["premises_and_provenance"]:
                    self.assertNotEqual(witness["donor_project"], case["project"])
        self.assertGreater(provenance_checked, 0)
        score = next(group for group in result["score_groups"] if group["kind"] == "all")["scores"]["1"]
        self.assertEqual(set(score), {"random_expected", "random_repetitions"})
        self.assertEqual(set(score["random_repetitions"]), {"7", "8"})
        expected = score["random_expected"]
        self.assertEqual((expected["expected_tp"], expected["emitted"], expected["hidden_elements"]),
                         (1.5, 3, 3))
        self.assertEqual(expected["expected_precision"], 0.5)
        self.assertEqual(expected["expected_recall"], 0.5)
        source_labels = {project["id"]: sorted(project["tags"]) + [None] for project in projects}
        for seed, repetition in score["random_repetitions"].items():
            self.assertEqual((repetition["cases"], repetition["emitted"], repetition["hidden_elements"]), (5, 3, 3))
            tp = 0
            for case in cases:
                hidden = source_labels[case["project"]][int(case["case_id"].rsplit("/", 1)[1])]
                ranking = case["methods"]["random"][seed]
                labels = [candidate["label"] if isinstance(candidate, dict) else candidate for candidate in ranking[:1]]
                tp += int(hidden is not None and hidden in labels)
            self.assertEqual((repetition["tp"], repetition["fp"], repetition["fn"]), (tp, 3 - tp, 3 - tp))
            self.assertEqual(repetition["precision"], tp / 3)
            self.assertEqual(repetition["recall"], tp / 3)
            self.assertEqual(repetition["abstained_cases"], 2)

        empty_result, empty_predictions = self.tiny_experiment([tag_dimension()], projects,
            {"methods": ["random"], "random_seeds": []})
        self.assertTrue(all(case["methods"] == {"random": {}} for case in empty_predictions["cases"]))
        empty_score = next(group for group in empty_result["score_groups"]
                           if group["kind"] == "all")["scores"]["1"]
        self.assertEqual(empty_score["random_expected"], {"status": "unavailable", "reason": "no_random_rankings"})
        self.assertEqual(empty_score["random_repetitions"], {})

    def test_new_run_freezes_exact_inputs_and_modules_for_independent_cli_replay(self):
        def digest(raw):
            return hashlib.sha256(raw).hexdigest()

        def without_run_time(value, when):
            if isinstance(value, dict):
                return {key: "<run-time>" if key in {"known_at", "predicted_at"} and item == when
                        else without_run_time(item, when) for key, item in value.items()}
            if isinstance(value, list):
                return [without_run_time(item, when) for item in value]
            return value

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = root / "fixture.json"
            fixture_raw = (" \n" + json.dumps({"operators": [], "projects": [
                {"id": "a", "kind": "film", "tags": ["anchor", "first"]},
                {"id": "b", "kind": "film", "tags": ["anchor", "second"]}]}, indent=3) + "\n\n").encode()
            fixture.write_bytes(fixture_raw)
            policy = root / "policy.json"
            policy_data = dict(json.loads(POLICY.read_text()), candidate_budgets=[2], random_seeds=[0, 1],
                               relation_weights={"arbitrary_tags": 1.0})
            policy_raw = (json.dumps(policy_data, indent=4) + "\n").encode()
            policy.write_bytes(policy_raw)
            protocol = root / "protocol.md"
            protocol_raw = b"Tiny synthetic projection protocol.\r\nImmutable bytes.\r\n"
            protocol.write_bytes(protocol_raw)

            base = copy.deepcopy(self.profile)
            base["extensions"] = {"opaque": {"$op": "not-a-runtime-instruction", "unicode": "żółć ∑"},
                                  "order": "base"}
            base_raw = b"\xef\xbb\xbf" + ("\n" + json.dumps(base, ensure_ascii=False, indent=3) + "\n\n").encode()
            profile_path = root / "custom-profile.json"
            profile_path.write_bytes(base_raw)
            first_overlay = {"dimensions": [tag_dimension()], "extensions": {
                "order": "first", "nested": {"first": 1}}}
            second_overlay = {"extensions": {"order": "last", "nested": {"second": 2}}}
            overlay_paths = [root / "first-overlay.json", root / "second-overlay.json"]
            overlay_raw = [b"\xef\xbb\xbf" + (json.dumps(first_overlay, indent=2) + "\n").encode(),
                           ("\n  " + json.dumps(second_overlay, separators=(",", ":")) + "\n\n").encode()]
            for path, raw in zip(overlay_paths, overlay_raw):
                path.write_bytes(raw)
            effective = load_profile(profile_path, overlay_paths)
            self.assertEqual(effective["extensions"]["order"], "last")
            self.assertEqual(effective["extensions"]["nested"], {"first": 1, "second": 2})
            self.assertEqual(effective["extensions"]["opaque"], base["extensions"]["opaque"])

            run = root / "run"
            result = experiment(run, policy, protocol, profile_path, overlay_paths, fixture)
            manifest = result["manifest"]
            self.assertEqual(manifest["output_schema_version"], "candidate-profile-projection-v3")
            self.assertEqual(manifest["profile"]["prediction_dimensions"], ["arbitrary_tags"])
            self.assertEqual(manifest["paid_api_calls"], 0)
            self.assertFalse(manifest["canonical_graph_mutated"])
            for name, raw, hash_key in [("fixture_frozen.json", fixture_raw, "fixture"),
                                       ("policy_frozen.json", policy_raw, "policy"),
                                       ("protocol_frozen.md", protocol_raw, "protocol")]:
                self.assertEqual((run / name).read_bytes(), raw)
                self.assertEqual(manifest["hashes"][hash_key], digest(raw))
            profile_sources = manifest["profile"]["source_files"]
            self.assertEqual([source["order"] for source in profile_sources], [0, 1, 2])
            self.assertEqual([source["role"] for source in profile_sources], ["base", "overlay", "overlay"])
            self.assertEqual([source["frozen_path"] for source in profile_sources],
                             ["profile_frozen.json", "profile_overlays/000.json", "profile_overlays/001.json"])
            for source, raw in zip(profile_sources, [base_raw, *overlay_raw]):
                self.assertEqual((run / source["frozen_path"]).read_bytes(), raw)
                self.assertEqual(source["sha256"], digest(raw))
            effective_path = run / manifest["profile"]["effective_path"]
            self.assertEqual(json.loads(effective_path.read_bytes()), effective)
            self.assertEqual(manifest["profile"]["effective_sha256"], digest(effective_path.read_bytes()))
            modules = manifest["modules"]
            self.assertEqual(set(modules), {"modules_frozen/prototype.py", "modules_frozen/recipe.py",
                                            "modules_frozen/__init__.py", "modules_frozen/profiles/default.json",
                                            "modules_frozen/profiles/schema.json"})
            for frozen_path, sha256 in modules.items():
                original = HERE / Path(frozen_path).relative_to("modules_frozen")
                self.assertEqual((run / frozen_path).read_bytes(), original.read_bytes())
                self.assertEqual(digest((run / frozen_path).read_bytes()), sha256)
            self.assertEqual(modules["modules_frozen/prototype.py"], manifest["hashes"]["code"])
            with gzip.open(run / "predictions.json.gz", "rt", encoding="utf-8") as handle:
                original_predictions = json.load(handle)
            self.assertEqual(original_predictions["manifest"], manifest)
            self.assertEqual(len(original_predictions["cases"]), 6)
            self.assertNotIn("answers", original_predictions)
            self.assertEqual(set(result["scores"]), {"all", "arbitrary_tags", "a", "b"})
            self.assertTrue(all(set(budgets) == {"2"} for budgets in result["scores"].values()))

            # Remove every supplied input: the CLI replay gets only archived bytes.
            for path in [fixture, policy, protocol, profile_path, *overlay_paths]:
                path.unlink()
            replay = root / "replayed"
            env = os.environ.copy()
            env.pop("PYTHONPATH", None)
            process = subprocess.run([
                sys.executable, str(run / "modules_frozen/prototype.py"),
                "--output", str(replay), "--fixture", str(run / "fixture_frozen.json"),
                "--policy", str(run / "policy_frozen.json"), "--protocol", str(run / "protocol_frozen.md"),
                "--recipe", str(run / "profile_frozen.json"),
                "--recipe-overlay", str(run / "profile_overlays/000.json"),
                "--profile-overlay", str(run / "profile_overlays/001.json")],
                cwd=root, env=env, capture_output=True, text=True, timeout=20)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            self.assertIn("arbitrary_tags", process.stdout)
            with gzip.open(replay / "predictions.json.gz", "rt", encoding="utf-8") as handle:
                replayed_predictions = json.load(handle)
            with gzip.open(replay / "results.json.gz", "rt", encoding="utf-8") as handle:
                replayed_result = json.load(handle)
            self.assertEqual(replayed_result["scores"], result["scores"])
            self.assertEqual(without_run_time(original_predictions["cases"], manifest["predicted_at"]),
                             without_run_time(replayed_predictions["cases"], replayed_result["manifest"]["predicted_at"]))
            self.assertEqual(replayed_result["manifest"]["modules"], modules)
            self.assertEqual(replayed_result["manifest"]["profile"]["effective_sha256"],
                             manifest["profile"]["effective_sha256"])


if __name__ == "__main__":
    unittest.main()
