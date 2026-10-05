"""Independent, synthetic producer-capture tests; shared repo remains read-only."""
from __future__ import annotations

import base64
from copy import deepcopy
from datetime import datetime, timezone
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
from unittest.mock import patch


HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(HERE.parents[2]))
spec = importlib.util.spec_from_file_location("_seeding_method_graph_proposal", HERE / "method_graph.py")
adapter = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = adapter
spec.loader.exec_module(adapter)
verifier_spec = importlib.util.spec_from_file_location("_seeding_local_verifier", HERE / "verify_method_graph.py")
verifier = importlib.util.module_from_spec(verifier_spec)
sys.modules[verifier_spec.name] = verifier
verifier_spec.loader.exec_module(verifier)
REPOSITORY = Path(adapter.codec.__file__).resolve().parents[4]

WHEN = "2026-10-05T08:00:00+00:00"
PROJECTED = "2026-10-05T09:00:00+00:00"
OBSERVED_UTC = "2026-10-05T14:00:00+00:00"


class ObservedUtcClock(datetime):
    @classmethod
    def now(cls, tz=None):
        if tz is None:
            raise AssertionError("the verifier must request an aware UTC clock")
        return datetime.fromisoformat(OBSERVED_UTC).astimezone(tz)


def raw_json(value):
    return (json.dumps(value, ensure_ascii=False, indent=3) + "\n\n").encode()


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def frozen_run(root, *, program_marker="first", policy_marker="first", profile_marker="first", historical=False):
    """Known records and exact frozen bytes, independent of seeding execution."""
    root.mkdir()
    policy = {"methods": ["custom/m~", "random/m~"], "random_seeds": [7, 8],
              "candidate_budgets": [1], "caller_setting": {"version": policy_marker}}
    profile = {"schema": "loom.seeding.recipe/1", "version": "synthetic-only", "dimensions": [],
               "extensions": {"marker": profile_marker, "opaque": {"$op": "recorded-only"}}}
    overlay = {"extensions": {"overlay": ["a/b~", None]}}
    effective = deepcopy(profile)
    effective["extensions"].update(overlay["extensions"])
    files = {
        "policy_frozen.json": raw_json(policy),
        "protocol_frozen.md": b"Synthetic development-only capture.\r\nNo model.\r\n",
        "prototype_frozen.py": ("# exact frozen program marker: " + program_marker + "\n").encode(),
        "fixture_frozen.json": raw_json({"operators": [], "projects": [{"id": "all"}]}),
    }
    manifest = {"predicted_at": WHEN, "evaluation_class": "development_corpus_lopo", "paid_api_calls": 0,
                "hashes": {"policy": digest(files["policy_frozen.json"]),
                           "protocol": digest(files["protocol_frozen.md"]),
                           "code": digest(files["prototype_frozen.py"]),
                           "fixture": digest(files["fixture_frozen.json"])}}
    if not historical:
        files.update({"profile_frozen.json": b"\xef\xbb\xbf\n" + raw_json(profile),
                      "profile_overlays/000.json": raw_json(overlay), "effective_profile.json": raw_json(effective),
                      "modules_frozen/prototype.py": files["prototype_frozen.py"],
                      "modules_frozen/recipe.py": b"# captured projection module; no execution\n"})
        manifest["modules"] = {name: digest(raw) for name, raw in files.items() if name.startswith("modules_frozen/")}
        manifest["profile"] = {"schema": profile["schema"], "version": profile["version"],
                               "effective_path": "effective_profile.json", "effective_sha256": digest(files["effective_profile.json"]),
                               "source_files": [{"order": 0, "role": "base", "frozen_path": "profile_frozen.json",
                                                 "sha256": digest(files["profile_frozen.json"])},
                                                {"order": 1, "role": "overlay", "frozen_path": "profile_overlays/000.json",
                                                 "sha256": digest(files["profile_overlays/000.json"])}]}
    candidate = {"label": "x/one~", "evidence_class": "extrapolated", "validation_status": "candidate",
                 "expected_properties": [{"relation": "future/tag", "label": "x/one~"}],
                 "premises_and_provenance": [{"donor_project": "donor/a~", "properties": {"known_at": None}}],
                 "assessment": {"calibrated_probability": None, "score": 0.375}}
    alternate = dict(candidate, label="y~two")
    predictions = {"manifest": manifest, "cases": [
        {"case_id": "all/all/000", "project": "all", "task": "all", "control": False,
         "visible_edges": {"all": []}, "methods": {"custom/m~": {"0": [candidate]},
         "random/m~": {"7": [candidate, alternate], "8": ["y~two", "x/one~"]}}},
        {"case_id": "all/all/001", "project": "all", "task": "all", "control": True,
         "visible_edges": {"all": ["x/one~"]}, "methods": {"custom/m~": {"0": []},
         "random/m~": {"7": [], "8": []}}}]}
    observed = {"tp": 1, "emitted": 1, "hidden_elements": 1, "precision": 1.0, "recall": 1.0,
                "fp": 0, "fn": 0, "cases": 2, "errors": []}
    random_expected = {"expected_tp": 0.5, "emitted": 1, "hidden_elements": 1,
                       "expected_precision": 0.5, "expected_recall": 0.5}
    scores = {"1": {"custom/m~": observed, "random_expected": random_expected,
                     "random_repetitions": {"7": observed, "8": dict(observed, tp=0, fp=1, fn=1, precision=0.0, recall=0.0)}}}
    results = {"manifest": manifest, "scores": {}, "ambiguous_score_aliases": ["all"], "score_groups": [
        {"kind": "all", "id": None, "scores": deepcopy(scores)},
        {"kind": "dimension", "id": "all", "scores": deepcopy(scores)},
        {"kind": "project", "id": "all", "scores": deepcopy(scores)}]}
    files["predictions.json.gz"] = gzip.compress(raw_json(predictions), mtime=123)
    files["results.json.gz"] = gzip.compress(raw_json(results), mtime=456)
    for name, raw in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw)
    return files, predictions, results


def reload_outputs(root, predictions, results):
    for name, document in (("predictions.json.gz", predictions), ("results.json.gz", results)):
        (root / name).write_bytes(gzip.compress(raw_json(document), mtime=0))


def expected_records(predictions, results):
    """Independent explicit traversal; no adapter selector or pointer helper."""
    def escape(text):
        return str(text).replace("~", "~0").replace("/", "~1")
    expected = {}
    for index, case in enumerate(predictions["cases"]):
        for method, seeds in case["methods"].items():
            for seed, value in seeds.items():
                expected[("predictions", f"/cases/{index}/methods/{escape(method)}/{escape(seed)}")] = value
    for index, group in enumerate(results["score_groups"]):
        for budget, metrics in group["scores"].items():
            for method, value in metrics.items():
                base = f"/score_groups/{index}/scores/{escape(budget)}/{escape(method)}"
                if method == "random_repetitions":
                    for seed, repetition in value.items():
                        expected[("results", base + "/" + escape(seed))] = repetition
                else:
                    expected[("results", base)] = value
    return expected


class CapturedMethodGraphTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="seeding-method-tests-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.run = self.root / "run"
        self.files, self.predictions, self.results = frozen_run(self.run)
        self.projection = adapter.load_projection(HERE / "profiles/method_graph.json")

    def artifact(self, projection=None, mode="records", run=None):
        projection = self.projection if projection is None else projection
        captured = adapter.capture_run(self.run if run is None else run, projection)
        return adapter.build_artifact(captured, projection, mode=mode, projected_at=PROJECTED)

    def high_entropy_capture(self):
        predictions = deepcopy(self.predictions)
        candidate = deepcopy(predictions["cases"][0]["methods"]["custom/m~"]["0"][0])
        candidate["captured_payload"] = [hashlib.sha256(f"verifier-high-entropy-{index}".encode()).hexdigest()
                                         for index in range(1500)]
        self.assertGreater(len(raw_json(candidate["captured_payload"])), 100_000)
        predictions["cases"][0]["methods"]["custom/m~"]["0"] = [candidate]
        reload_outputs(self.run, predictions, self.results)
        self.files["predictions.json.gz"] = (self.run / "predictions.json.gz").read_bytes()
        self.files["results.json.gz"] = (self.run / "results.json.gz").read_bytes()
        return adapter.capture_run(self.run, self.projection)

    def test_record_projection_exports_every_ranking_metric_pointer_and_value_hash(self):
        captured = adapter.capture_run(self.run, self.projection)
        expected = expected_records(self.predictions, self.results)
        self.assertEqual(len(expected), 18)
        selected = list(adapter.selected_records(captured, self.projection, "records"))
        self.assertEqual({(row["input"], row["json_pointer"]): row["value"] for row in selected}, expected)
        self.assertEqual(len(selected), 18)
        self.assertTrue(any(row["value"] == [] for row in selected))
        self.assertTrue(any("custom~1m~0" in row["json_pointer"] for row in selected))
        self.assertTrue(all(row["evaluation"] == (row["input"] == "results") for row in selected))
        artifact = self.artifact()
        recovered = adapter.recover_results(artifact)
        self.assertEqual(len(recovered), 18)
        sources = {row["observation"]["id"]: row["observation"] for row in artifact["packet"]["sources"]}
        actual = {}
        entities = {row["id"]: row for row in artifact["packet"]["entities"]}
        for result_id in artifact["result_entity_ids"]:
            attrs = entities[result_id]["attrs"]
            role = sources[attrs["source_ref"]]["attrs"]["role"]
            key = (role, attrs["json_pointer"])
            self.assertNotIn(key, actual)
            actual[key] = recovered[result_id]
            self.assertEqual(attrs["value_sha256"], adapter.codec.digest(expected[key]))
        self.assertEqual(actual, expected)

    def test_bulk_and_records_recover_exact_raw_gzip_and_all_source_bytes(self):
        for mode in ("bulk", "records"):
            with self.subTest(mode=mode):
                artifact = self.artifact(mode=mode)
                recovered = adapter.recover_files(artifact)
                self.assertEqual(recovered["predictions"], self.files["predictions.json.gz"])
                self.assertEqual(recovered["results"], self.files["results.json.gz"])
                for role, filename in (("policy", "policy_frozen.json"), ("program", "prototype_frozen.py"),
                                       ("protocol", "protocol_frozen.md"), ("fixture", "fixture_frozen.json"),
                                       ("effective_profile", "effective_profile.json"),
                                       ("profile-source:0", "profile_frozen.json"),
                                       ("profile-source:1", "profile_overlays/000.json"),
                                       ("module:modules_frozen/recipe.py", "modules_frozen/recipe.py")):
                    self.assertEqual(recovered[role], self.files[filename])
                if mode == "bulk":
                    self.assertEqual(len(artifact["result_entity_ids"]), 2)
                    self.assertCountEqual(adapter.recover_results(artifact).values(), [self.predictions, self.results])
                    evaluation_predicate = artifact["contract"]["vocabulary"]["predicates"]["evaluation_record"]
                    self.assertFalse(any(claim["predicate"] == evaluation_predicate for claim in artifact["packet"]["claims"]))

    def test_native_results_have_real_provenance_claims_and_no_fabricated_model_measurements(self):
        artifact = self.artifact()
        contract, trace, packet = artifact["contract"], artifact["trace"], artifact["packet"]
        binding = contract["bindings"]
        entities = {row["id"]: row for row in packet["entities"]}
        sources = {row["observation"]["id"]: row for row in packet["sources"]}
        predicates = contract["vocabulary"]["predicates"]
        self.assertEqual(trace["prepared_at"], WHEN)
        self.assertEqual(trace["projected_at"], PROJECTED)
        self.assertTrue(all(row["first_seen"] == PROJECTED and row["last_seen"] == PROJECTED
                            for row in entities.values()))
        self.assertTrue(all(row["known_at"] == PROJECTED and row["observation"]["date"] == PROJECTED
                            for row in sources.values()))
        originals = [row["observation"]["attrs"] for row in sources.values()
                     if "original_prepared_at" in row["observation"]["attrs"]]
        self.assertEqual(len(originals), len(adapter.capture_run(self.run, self.projection)["files"]))
        self.assertTrue(all(attrs["original_prepared_at"] == WHEN and
                            attrs["known_at_semantics"] == "bytes observed at projection" for attrs in originals))
        raw_predictions = adapter.recover_files(artifact)["predictions"]
        self.assertEqual(json.loads(gzip.decompress(raw_predictions))["manifest"]["predicted_at"], WHEN)
        self.assertEqual(len({binding["method_identity_id"], binding["method_version_id"], binding["run_id"]}), 3)
        self.assertEqual({item["result_entity_id"] for item in trace["result_bindings"]}, set(artifact["result_entity_ids"]))
        for result_id in artifact["result_entity_ids"]:
            self.assertNotIn("model_origin", entities[result_id]["attrs"])
            for role, endpoint in (("produced_in_run", binding["run_id"]),
                                   ("produced_by_method_version", binding["method_version_id"])):
                edges = [claim for claim in packet["claims"] if claim["subject"] == result_id
                         and claim["predicate"] == predicates[role] and claim["object"] == endpoint]
                self.assertEqual(len(edges), 1)
                self.assertIsNone(edges[0]["value"])
                self.assertEqual(edges[0]["assessment"]["status"], "active")
                self.assertEqual(edges[0]["qualifiers"]["extra"]["confidence_scope"], "structure_only")
        for row in packet["claims"]:
            self.assertNotEqual(row["assessment"]["origin"], "model_knowledge")
            self.assertTrue(row["assessment"]["basis"]["support"])
        metrics = {result_id for result_id in artifact["result_entity_ids"]
                   if sources[entities[result_id]["attrs"]["source_ref"]]["observation"]["attrs"]["role"] == "results"}
        evaluations = [claim for claim in packet["claims"] if claim["predicate"] == predicates["evaluation_record"]]
        self.assertEqual(len(metrics), 12)
        self.assertEqual(len(evaluations), 12)
        self.assertEqual({claim["object"] for claim in evaluations}, metrics)
        for claim in evaluations:
            self.assertEqual(claim["subject"], binding["method_version_id"])
            self.assertIsNone(claim["value"])
            self.assertEqual(claim["assessment"]["status"], "active")
            self.assertEqual([support["observation"] for support in claim["assessment"]["basis"]["support"]],
                             [entities[claim["object"]]["attrs"]["source_ref"]])
        for optional in ("model_identity_id", "prompt_version_id", "compiler_transform_id"):
            self.assertIsNone(binding.get(optional))
        self.assertIsNone(trace["measurements"]["producer_cpu_seconds"])
        self.assertIsNone(trace["measurements"]["producer_cost_usd"])
        self.assertEqual(trace["measurements"]["adapter_provider_calls"], 0)
        self.assertEqual(trace["measurements"]["result_records"], 18)
        self.assertFalse(artifact["producer_evidence"]["producer_execution_verified"])
        self.assertEqual(entities[binding["run_id"]]["attrs"], trace)

    def test_profile_recipe_parameters_and_frozen_sources_are_addressable_entities(self):
        artifact = self.artifact()
        contract, packet = artifact["contract"], artifact["packet"]
        entities = {row["id"]: row for row in packet["entities"]}
        kinds, predicates = contract["vocabulary"]["kinds"], contract["vocabulary"]["predicates"]
        recipe_id = contract["bindings"]["recipe_version_id"]
        recipe = entities[recipe_id]["attrs"]["definition"]
        self.assertEqual(recipe["profile"], json.loads(self.files["effective_profile.json"]))
        self.assertEqual(recipe["parameters"], json.loads(self.files["policy_frozen.json"]))
        self.assertEqual(recipe["profile_status"], "recorded")
        for role, hash_key in (("method_version_id", "method_version"), ("parameter_set_version_id", "parameter_set"),
                               ("recipe_version_id", "recipe")):
            attrs = entities[contract["bindings"][role]]["attrs"]
            self.assertEqual(attrs["definition_sha256"], adapter.codec.digest(attrs["definition"]))
            self.assertEqual(contract["definition_hashes"][hash_key], attrs["definition_sha256"])
        profiles = [row for row in packet["entities"] if row["kind"] == kinds["profile_version"]]
        self.assertEqual(len(profiles), 1)
        self.assertEqual(profiles[0]["attrs"]["definition"], recipe["profile"])
        self.assertTrue(any(claim["subject"] == recipe_id and claim["predicate"] == predicates["uses_profile"]
                            and claim["object"] == profiles[0]["id"] for claim in packet["claims"]))
        versions = [row for row in packet["entities"] if row["kind"] == kinds["source_version"]]
        by_role = {row["label"]: row for row in versions}
        self.assertEqual(set(by_role), set(recipe["captured_runtime"]))
        for role, row in by_role.items():
            self.assertEqual(row["attrs"]["definition"], recipe["captured_runtime"][role])
            self.assertTrue(any(claim["subject"] == recipe_id and claim["object"] == row["id"]
                                and claim["predicate"] == predicates["uses_source_version"] for claim in packet["claims"]))

    def test_vocabulary_can_change_as_data_without_losing_records_or_bindings(self):
        renamed = deepcopy(self.projection)
        renamed["vocabulary"] = {section: {role: "caller/" + section + "~" + role for role in values}
                                 for section, values in renamed["vocabulary"].items()}
        a, b = self.artifact(), self.artifact(renamed)
        self.assertEqual(a["contract"]["bindings"]["method_identity_id"], b["contract"]["bindings"]["method_identity_id"])
        self.assertEqual(a["contract"]["bindings"]["method_version_id"], b["contract"]["bindings"]["method_version_id"])
        self.assertCountEqual(adapter.recover_results(a).values(), adapter.recover_results(b).values())
        declared_kinds = set(renamed["vocabulary"]["kinds"].values())
        declared_predicates = set(renamed["vocabulary"]["predicates"].values())
        self.assertTrue(all(entity["kind"] in declared_kinds for entity in b["packet"]["entities"]))
        self.assertTrue(all(claim["predicate"] in declared_predicates for claim in b["packet"]["claims"]))
        self.assertEqual(sum(claim["predicate"] == renamed["vocabulary"]["predicates"]["evaluation_record"]
                             for claim in b["packet"]["claims"]), 12)
        toggled = deepcopy(renamed)
        for rule in toggled["projections"]["records"]:
            rule["evaluation"] = not rule.get("evaluation", False)
        ranking_evaluations = self.artifact(toggled)
        evaluation_predicate = toggled["vocabulary"]["predicates"]["evaluation_record"]
        claims = [claim for claim in ranking_evaluations["packet"]["claims"]
                  if claim["predicate"] == evaluation_predicate]
        self.assertEqual(len(claims), 6)
        entities = {row["id"]: row for row in ranking_evaluations["packet"]["entities"]}
        self.assertTrue(all(entities[claim["object"]]["attrs"]["projection_role"] == "ranking" for claim in claims))
        disabled = deepcopy(toggled)
        for rule in disabled["projections"]["records"]:
            rule["evaluation"] = False
        no_evaluations = self.artifact(disabled)
        self.assertFalse(any(claim["predicate"] == evaluation_predicate for claim in no_evaluations["packet"]["claims"]))
        self.assertEqual(a["contract"]["bindings"]["method_version_id"], no_evaluations["contract"]["bindings"]["method_version_id"])
        self.assertCountEqual(adapter.recover_results(a).values(), adapter.recover_results(no_evaluations).values())

    def test_role_pointer_recipe_exclusion_and_quote_settings_are_consumed_from_data(self):
        renamed = deepcopy(self.projection)
        roles = {name: "caller/" + name + "~" for name in renamed["inputs"]}
        renamed["inputs"] = {roles[name]: spec for name, spec in renamed["inputs"].items()}
        renamed["manifest"].update(input=roles["predictions"], pointer="/meta~1bundle~0",
                                   timestamp_pointer="/dates~1meta/observed~0at", input_hash_pointer="/input~1identity/sha~0")
        renamed["output_manifests"] = [{"input": roles["results"], "pointer": "/meta~1bundle~0"}]
        renamed["parameters"]["input"] = roles["policy"]
        renamed["hash_bindings"] = {name: roles[role] for name, role in renamed["hash_bindings"].items()}
        renamed["source_roles"] = {"outputs": [roles["predictions"], roles["results"]],
                                    "recipe_exclude": [roles["predictions"], roles["results"], roles["fixture"], roles["policy"]]}
        renamed["profiles"].update(effective_role=roles["effective_profile"],
                                   descriptor_pointer="/profile~1decl", modules_pointer="/code~0modules")
        for rules in renamed["projections"].values():
            for rule in rules:
                rule["input"] = roles[rule["input"]]
                rule["path"] = [{"cases": "turns/", "methods": "channels~", "score_groups": "measurements~/",
                                 "scores": "by_budget/", "random_repetitions": "orders~/"}.get(token, token)
                                for token in rule["path"]]
                for condition in ("when_pointer", "unless_pointer"):
                    if condition in rule:
                        rule[condition] = "/measurements~0~1"
                if "exclude_final_keys" in rule:
                    rule["exclude_final_keys"] = ["orders~/"]
        manifest = deepcopy(self.predictions["manifest"])
        manifest["dates/meta"] = {"observed~at": manifest.pop("predicted_at")}
        manifest["input/identity"] = {"sha~": manifest["hashes"]["fixture"]}
        manifest["profile/decl"] = manifest.pop("profile")
        manifest["code~modules"] = manifest.pop("modules")
        prediction_document = {"meta/bundle~": manifest, "turns/": deepcopy(self.predictions["cases"])}
        for case in prediction_document["turns/"]:
            case["channels~"] = case.pop("methods")
        result_document = {"meta/bundle~": manifest, "measurements~/": deepcopy(self.results["score_groups"])}
        for group in result_document["measurements~/"]:
            group["by_budget/"] = group.pop("scores")
            for values in group["by_budget/"].values():
                values["orders~/"] = values.pop("random_repetitions")
        reload_outputs(self.run, prediction_document, result_document)
        for quote_chars in (7, None):
            with self.subTest(quote_chars=quote_chars):
                renamed["support"]["quote_chars"] = quote_chars
                captured = adapter.capture_run(self.run, renamed)
                artifact = adapter.build_artifact(captured, renamed, projected_at=PROJECTED)
                self.assertEqual(artifact["trace"]["prepared_at"], WHEN)
                self.assertEqual(artifact["trace"]["input_sha256"], manifest["hashes"]["fixture"])
                self.assertEqual(artifact["trace"]["effective_parameters"], json.loads(self.files["policy_frozen.json"]))
                self.assertCountEqual(adapter.recover_results(artifact).values(),
                                      expected_records(self.predictions, self.results).values())
                self.assertEqual(len(artifact["result_entity_ids"]), 18)
                recipe_id = artifact["contract"]["bindings"]["recipe_version_id"]
                recipe = next(row["attrs"]["definition"] for row in artifact["packet"]["entities"] if row["id"] == recipe_id)
                self.assertNotIn(roles["policy"], recipe["captured_runtime"])
                self.assertIn(roles["program"], recipe["captured_runtime"])
                sources = {row["observation"]["id"]: row["observation"] for row in artifact["packet"]["sources"]}
                for claim in artifact["packet"]["claims"]:
                    for support in claim["assessment"]["basis"]["support"]:
                        expected_quote = sources[support["observation"]]["text"][:quote_chars]
                        self.assertEqual(support["quote"], expected_quote)

    def test_projection_version_captures_exact_profile_and_loaded_modules_separate_from_producer(self):
        path = self.root / "supplied-projection.json"
        profile_raw = b"\xef\xbb\xbf\n\n" + raw_json(dict(self.projection)) + b" \n"
        path.write_bytes(profile_raw)
        supplied = adapter.load_projection(path)
        artifact = self.artifact(supplied)
        bindings = artifact["contract"]["bindings"]
        projection_id = bindings["projection_version_id"]
        projection_entity = next(row for row in artifact["packet"]["entities"] if row["id"] == projection_id)
        definition = projection_entity["attrs"]["definition"]
        self.assertEqual(projection_entity["first_seen"], PROJECTED)
        self.assertEqual(definition["profile"], supplied)
        self.assertEqual(definition["profile_sha256"], adapter.codec.digest(supplied))
        self.assertEqual(projection_entity["attrs"]["definition_sha256"], adapter.codec.digest(definition))
        self.assertEqual(artifact["contract"]["definition_hashes"]["projection"], adapter.codec.digest(definition))
        files = adapter.recover_files(artifact)
        self.assertEqual(files["projection-profile-bytes"], profile_raw)
        sources = {row["observation"]["id"]: row for row in artifact["packet"]["sources"]}
        for name, original in (("adapter", Path(adapter.__file__)), ("packet_codec", Path(adapter.codec.__file__)),
                               ("canonical_codec", Path(adapter.codec.safe.__file__))):
            with self.subTest(module=name):
                raw = original.read_bytes()
                self.assertEqual(files["projection-module:" + name], raw)
                self.assertEqual(definition["runtime"][name]["raw_sha256"], digest(raw))
                source = sources[definition["runtime"][name]["source_ref"]]
                self.assertEqual(source["known_at"], PROJECTED)
                self.assertEqual(source["observation"]["date"], PROJECTED)
        self.assertEqual(definition["runtime"]["profile_source"]["raw_sha256"], digest(profile_raw))
        self.assertTrue(any(claim["subject"] == bindings["run_id"] and claim["object"] == projection_id
                            and claim["predicate"] == supplied["vocabulary"]["predicates"]["uses_projection"]
                            for claim in artifact["packet"]["claims"]))
        changed = deepcopy(supplied)
        changed["support"]["quote_chars"] = 7
        other = self.artifact(changed)
        other_bindings = other["contract"]["bindings"]
        self.assertEqual(bindings["method_identity_id"], other_bindings["method_identity_id"])
        self.assertEqual(bindings["method_version_id"], other_bindings["method_version_id"])
        self.assertEqual(bindings["recipe_version_id"], other_bindings["recipe_version_id"])
        self.assertNotEqual(projection_id, other_bindings["projection_version_id"])
        self.assertNotEqual(bindings["run_id"], other_bindings["run_id"])
        self.assertCountEqual(adapter.recover_results(artifact).values(), adapter.recover_results(other).values())

    def test_historical_run_explicitly_preserves_unrecorded_profile(self):
        historical = self.root / "historical"
        frozen_run(historical, historical=True)
        artifact = self.artifact(run=historical)
        binding = artifact["contract"]["bindings"]
        recipe = next(row for row in artifact["packet"]["entities"] if row["id"] == binding["recipe_version_id"])["attrs"]["definition"]
        self.assertEqual(recipe["profile_status"], "unrecorded")
        self.assertIsNone(recipe["profile"])
        self.assertEqual(artifact["trace"]["user_overrides"], {"status": "unrecorded", "profile_sources": None})
        self.assertFalse(any(row["kind"] == artifact["contract"]["vocabulary"]["kinds"]["profile_version"]
                             for row in artifact["packet"]["entities"]))
        self.assertEqual(len(adapter.recover_results(artifact)), 18)

    def test_method_identity_stable_and_version_changes_with_consumed_program_policy_profile(self):
        baseline = self.artifact()["contract"]["bindings"]
        for role in ("program", "policy", "profile"):
            with self.subTest(role=role):
                run = self.root / role
                frozen_run(run, **{role + "_marker": "changed"})
                changed = self.artifact(run=run)["contract"]["bindings"]
                self.assertEqual(changed["method_identity_id"], baseline["method_identity_id"])
                self.assertNotEqual(changed["method_version_id"], baseline["method_version_id"])
                self.assertNotEqual(changed["recipe_version_id"], baseline["recipe_version_id"])
                if role == "policy":
                    self.assertNotEqual(changed["parameter_set_version_id"], baseline["parameter_set_version_id"])

    def test_capture_rejects_tampered_inputs_profile_overlays_modules_and_output_manifests(self):
        paths = [("policy_frozen.json", "manifest_source_hash_mismatch"),
                 ("prototype_frozen.py", "manifest_source_hash_mismatch"),
                 ("effective_profile.json", "effective_profile_hash_mismatch"),
                 ("profile_overlays/000.json", "frozen_source_hash_mismatch"),
                 ("modules_frozen/recipe.py", "frozen_source_hash_mismatch")]
        for name, error in paths:
            with self.subTest(name=name):
                path = self.run / name
                original = path.read_bytes()
                path.write_bytes(original + b" \n")
                with self.assertRaisesRegex(ValueError, error):
                    adapter.capture_run(self.run, self.projection)
                path.write_bytes(original)
        results = deepcopy(self.results)
        results["manifest"]["predicted_at"] = PROJECTED
        reload_outputs(self.run, self.predictions, results)
        with self.assertRaisesRegex(ValueError, "output_manifests_differ"):
            adapter.capture_run(self.run, self.projection)

    def test_invalid_operations_duplicate_pointers_and_record_or_source_tampering_reject(self):
        with self.assertRaisesRegex(ValueError, "unavailable_input_role"):
            adapter.capture_run(self.run, self.projection, input_paths={"undeclared/caller~": self.run / "policy_frozen.json"})
        declared = deepcopy(self.projection)
        declared["inputs"]["declared/caller~"] = {"paths": [], "required": True}
        custom = adapter.capture_run(self.run, declared, input_paths={"declared/caller~": self.run / "policy_frozen.json"})
        self.assertEqual(custom["files"]["declared/caller~"]["raw"], self.files["policy_frozen.json"])
        invalid = deepcopy(self.projection)
        invalid["projections"]["records"][0]["execute"] = "no-code-from-data"
        path = self.root / "invalid.json"
        path.write_bytes(raw_json(invalid))
        with self.assertRaisesRegex(ValueError, "unavailable_projection_operation"):
            adapter.load_projection(path)
        invalid_flag = deepcopy(self.projection)
        invalid_flag["projections"]["records"][0]["evaluation"] = "true"
        path.write_bytes(raw_json(invalid_flag))
        with self.assertRaisesRegex(ValueError, "invalid_evaluation_flag"):
            adapter.load_projection(path)
        duplicate = deepcopy(self.projection)
        duplicate["projections"]["records"].append(deepcopy(duplicate["projections"]["records"][0]))
        captured = adapter.capture_run(self.run, duplicate)
        with self.assertRaisesRegex(ValueError, "duplicate_projected_record"):
            list(adapter.selected_records(captured, duplicate))
        artifact = self.artifact()
        bad_record = deepcopy(artifact)
        result = next(row for row in bad_record["packet"]["entities"] if row["id"] == artifact["result_entity_ids"][0])
        result["attrs"]["value_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "captured_record_claim_mismatch"):
            adapter.recover_results(bad_record)
        captured_predicate = bad_record["contract"]["vocabulary"]["predicates"]["captured_record"]
        captured_claim = next(row for row in bad_record["packet"]["claims"] if row["subject"] == result["id"]
                              and row["predicate"] == captured_predicate)
        captured_claim["value"] = deepcopy(result["attrs"])
        with self.assertRaisesRegex(ValueError, "captured_record_hash_mismatch"):
            adapter.recover_results(bad_record)
        bad_source = deepcopy(artifact)
        source = next(row["observation"] for row in bad_source["packet"]["sources"]
                      if row["observation"]["attrs"].get("role") == "predictions")
        raw = bytearray(base64.b64decode(source["text"]))
        raw[4] ^= 1  # Gzip header timestamp, keeping the stream decodable.
        source["text"] = base64.b64encode(raw).decode()
        with self.assertRaisesRegex(ValueError, "captured_source_hash_mismatch"):
            adapter.recover_files(bad_source)
        with self.assertRaisesRegex(ValueError, "captured_source_hash_mismatch"):
            adapter.recover_results(bad_source)

    def test_duplicate_declared_outputs_and_ambiguous_source_roles_reject(self):
        artifact = self.artifact()
        duplicate_output = deepcopy(artifact)
        duplicate_output["result_entity_ids"].append(duplicate_output["result_entity_ids"][0])
        with self.assertRaisesRegex(ValueError, "duplicate_result_ids"):
            adapter.recover_results(duplicate_output)
        duplicate_source = deepcopy(artifact)
        item = deepcopy(next(row for row in duplicate_source["packet"]["sources"]
                             if row["observation"]["attrs"].get("role") == "predictions"))
        item["observation"]["id"] += ":another-source-same-role"
        duplicate_source["packet"]["sources"].append(item)
        with self.assertRaisesRegex(ValueError, "duplicate_captured_source_role"):
            adapter.recover_files(duplicate_source)

    def test_estimate_matches_exact_encoded_bytes_and_configured_confirmation_threshold(self):
        captured = adapter.capture_run(self.run, self.projection)
        fixed = adapter.build_artifact(captured, self.projection, mode="bulk", projected_at="2000-01-01T00:00:00+00:00")
        raw = adapter.canonical(fixed)
        baseline = len(self.files["predictions.json.gz"]) + len(self.files["results.json.gz"])
        for format_ in ("json", "json.gz"):
            with self.subTest(format=format_):
                report = adapter.estimate(captured, self.projection, mode="bulk", format_=format_)
                encoded = gzip.compress(raw, mtime=0) if format_ == "json.gz" else raw
                self.assertEqual(report["baseline_output_bytes"], baseline)
                self.assertEqual(report["export_bytes"], len(encoded))
                self.assertEqual(report["decoded_export_bytes"], len(raw))
                self.assertEqual(report["additional_output_ratio"], len(encoded) / baseline)
                self.assertEqual(report["total_output_ratio"], 1 + len(encoded) / baseline)
                self.assertEqual(report["result_count"], 2)
                self.assertIsNone(report["producer_cpu_seconds"])
                self.assertGreaterEqual(report["estimated_cpu_seconds"], 0)
        # These thresholds are caller data; they must actually alter the decision.
        permit = deepcopy(self.projection)
        permit["export"]["confirmation_ratio"] = 10 ** 9
        request = deepcopy(self.projection)
        request["export"]["confirmation_ratio"] = 1
        self.assertFalse(adapter.estimate(captured, permit, mode="bulk")["requires_confirmation"])
        self.assertTrue(adapter.estimate(captured, request, mode="bulk")["requires_confirmation"])
        absent_baseline = deepcopy(self.projection)
        absent_baseline["source_roles"]["outputs"] = []
        with patch.object(adapter, "build_artifact", wraps=adapter.build_artifact) as build:
            unknown = adapter.estimate(captured, absent_baseline, mode="bulk")
            build.assert_called_once()
        self.assertEqual(unknown["baseline_output_bytes"], 0)
        self.assertEqual(unknown["comparison_status"], "baseline_unavailable")
        self.assertIsNone(unknown["additional_output_ratio"])
        self.assertIsNone(unknown["total_output_ratio"])
        self.assertIsNone(unknown["expected_work_ratio_lower_bound"])
        self.assertFalse(unknown["requires_confirmation"])
        self.assertGreater(unknown["export_bytes"], 0)
        self.assertEqual(unknown["estimate_kind"], "exact-serialized-export")

    def test_large_full_quotes_require_confirmation_before_constructing_packet(self):
        predictions = deepcopy(self.predictions)
        candidate = deepcopy(predictions["cases"][0]["methods"]["custom/m~"]["0"][0])
        # Actual distinct bytes: unlike a repeated fill string this remains large
        # after gzip, so the preflight reflects a real support-duplication cost.
        candidate["captured_payload"] = [hashlib.sha256(f"synthetic-high-entropy-{index}".encode()).hexdigest()
                                         for index in range(1500)]
        predictions["cases"][0]["methods"]["custom/m~"]["0"] = [candidate]
        self.assertGreater(len(raw_json(candidate["captured_payload"])), 100_000)
        reload_outputs(self.run, predictions, self.results)
        captured = adapter.capture_run(self.run, self.projection)
        projection = deepcopy(self.projection)
        projection["support"]["quote_chars"] = None
        self.assertEqual(projection["export"]["confirmation_ratio"], 10)
        with patch.object(adapter, "build_artifact", side_effect=AssertionError("preflight built expensive packet")) as build:
            report = adapter.estimate(captured, projection, mode="records")
            build.assert_not_called()
        self.assertTrue(report["requires_confirmation"])
        self.assertEqual(report["estimate_kind"], "serialization-work-lower-bound-before-build")
        self.assertIsNone(report["export_bytes"])
        self.assertIsNone(report["decoded_export_bytes"])
        self.assertIsNone(report["estimated_cpu_seconds"])
        self.assertIsNone(report["producer_cpu_seconds"])
        expected = expected_records(predictions, self.results)
        work = sum((4 if role == "results" else 3) * len(base64.b64encode(captured["files"][role]["raw"]))
                   for role, _ in expected)
        work += sum(len(base64.b64encode(row["raw"])) for row in captured["files"].values())
        decoded_baseline = len(gzip.decompress((self.run / "predictions.json.gz").read_bytes()))
        decoded_baseline += len(gzip.decompress((self.run / "results.json.gz").read_bytes()))
        self.assertEqual(report["expected_serialization_work_bytes_lower_bound"], work)
        self.assertEqual(report["expected_work_ratio_lower_bound"], work / decoded_baseline)
        self.assertGreaterEqual(report["expected_work_ratio_lower_bound"], 10)
        self.assertEqual(report["result_count"], 18)
        with patch.object(adapter, "build_artifact", wraps=adapter.build_artifact) as build:
            normal = adapter.estimate(captured, self.projection, mode="records")
            build.assert_called_once()
        self.assertEqual(normal["estimate_kind"], "exact-serialized-export")
        fixed = adapter.build_artifact(captured, self.projection, mode="records", projected_at="2000-01-01T00:00:00+00:00")
        exact_raw = adapter.canonical(fixed)
        self.assertEqual(normal["decoded_export_bytes"], len(exact_raw))
        self.assertEqual(normal["export_bytes"], len(gzip.compress(exact_raw, mtime=0)))

    def test_json_pointer_indices_and_tilde_escapes_preserve_unambiguous_references(self):
        document = {"array": ["zero", "one"], "a/b~c": {"01": "ordinary-object-key"},
                    "bad~x": "this literal key still requires escaping"}
        self.assertEqual(adapter.pointer(document, "/array/0"), "zero")
        self.assertEqual(adapter.pointer(document, "/array/1"), "one")
        self.assertEqual(adapter.pointer(document, "/a~1b~0c/01"), "ordinary-object-key")
        self.assertEqual(adapter.pointer(document, "/bad~0x"), "this literal key still requires escaping")
        walked = list(adapter.walk(document, ["a/b~c", "01"]))
        self.assertEqual(walked, [("/a~1b~0c/01", "ordinary-object-key")])
        self.assertEqual(adapter.pointer(document, walked[0][0]), walked[0][1])
        for path in ("/array/-1", "/array/01"):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "invalid_json_pointer_array_index"):
                adapter.pointer(document, path)
            with self.subTest(walk=path), self.assertRaisesRegex(ValueError, "invalid_json_pointer_array_index"):
                list(adapter.walk(document, path.split("/")[1:]))
        with self.assertRaisesRegex(ValueError, "invalid_json_pointer_escape"):
            adapter.pointer(document, "/bad~x")

    def test_cli_dry_run_is_read_only_confirmation_required_and_existing_output_immutable(self):
        self.high_entropy_capture()
        projection = deepcopy(self.projection)
        profile_path = self.root / "projection.json"
        profile_path.write_bytes(raw_json(projection))
        before = {path.relative_to(self.run).as_posix(): path.read_bytes() for path in self.run.rglob("*") if path.is_file()}
        env = os.environ.copy()
        env.update(PYTHONDONTWRITEBYTECODE="1", PYTHONPATH=str(REPOSITORY))
        command = [sys.executable, str(HERE / "method_graph.py"), "--run", str(self.run),
                   "--projection", str(profile_path), "--mode", "bulk"]
        def invoke(arguments):
            return subprocess.run([*command, *arguments], cwd=self.root, env=env,
                                  capture_output=True, text=True, timeout=20)
        physical_dry = invoke([])
        self.assertEqual(physical_dry.returncode, 0, physical_dry.stdout + physical_dry.stderr)
        physical_estimate = json.loads(physical_dry.stdout)
        self.assertLess(physical_estimate["total_output_ratio"], 10)
        self.assertFalse(physical_estimate["requires_confirmation"])
        # Exercise explicit confirmation for a stricter caller preset while
        # keeping the actual export below the owner's order-of-magnitude gate.
        projection["export"]["confirmation_ratio"] = 1
        profile_path.write_bytes(raw_json(projection))
        dry = invoke([])
        self.assertEqual(dry.returncode, 0, dry.stdout + dry.stderr)
        self.assertTrue(json.loads(dry.stdout)["requires_confirmation"])
        self.assertEqual({path.relative_to(self.run).as_posix(): path.read_bytes() for path in self.run.rglob("*")
                          if path.is_file()}, before)

        output = self.root / "export.json.gz"
        blocked = invoke(["--output", str(output)])
        self.assertNotEqual(blocked.returncode, 0)
        self.assertIn("requires confirmation", blocked.stderr)
        self.assertFalse(output.exists())
        confirmed = invoke(["--output", str(output), "--confirm-large-export"])
        self.assertEqual(confirmed.returncode, 0, confirmed.stdout + confirmed.stderr)
        first_bytes = output.read_bytes()
        exported = json.loads(gzip.decompress(first_bytes))
        self.assertEqual(adapter.recover_files(exported)["predictions"], self.files["predictions.json.gz"])
        self.assertEqual(len(adapter.recover_results(exported)), 2)
        repeated = invoke(["--output", str(output), "--confirm-large-export"])
        self.assertNotEqual(repeated.returncode, 0)
        self.assertEqual(output.read_bytes(), first_bytes)
        self.assertEqual({path.relative_to(self.run).as_posix(): path.read_bytes() for path in self.run.rglob("*")
                          if path.is_file()}, before)

    def test_verifier_preflight_rejects_large_quotes_before_any_artifact_build(self):
        captured = self.high_entropy_capture()
        for quote_chars in (None, 10 ** 9):
            with self.subTest(quote_chars=quote_chars):
                projection = deepcopy(self.projection)
                projection["support"]["quote_chars"] = quote_chars
                with patch.object(verifier, "datetime", ObservedUtcClock), \
                        patch.object(verifier.bridge, "build_artifact", side_effect=AssertionError("build preceded approval")) as build:
                    with self.assertRaises(verifier.ExportConfirmationRequired) as caught:
                        verifier.preflight_export(captured, projection)
                    preflight = caught.exception.preflight
                    self.assertTrue(preflight["estimate"]["requires_confirmation"])
                    self.assertGreaterEqual(preflight["estimate"]["expected_work_ratio_lower_bound"], 10)
                    self.assertIsNone(preflight["estimate"]["export_bytes"])
                    self.assertEqual(preflight["usage_decision"]["decision"], "awaiting_confirmation")
                    self.assertFalse(preflight["usage_decision"]["caller_confirmed"])
                    self.assertIsNone(preflight["usage_decision"]["confirmation_source"])
                    self.assertEqual(preflight["projected_at"], OBSERVED_UTC)
                    self.assertEqual(preflight["verification_clock"]["mode"], "observed_utc")
                    self.assertFalse(preflight["verification_clock"]["simulation"])
                    with self.assertRaises(verifier.ExportConfirmationRequired):
                        verifier.build_verified_artifact(captured, projection, preflight)
                    build.assert_not_called()

    def test_verifier_current_clock_and_explicit_fixture_clock_are_captured_without_backdating(self):
        captured = self.high_entropy_capture()
        with patch.object(verifier, "datetime", ObservedUtcClock):
            current = verifier.preflight_export(captured, self.projection)
            self.assertLess(current["estimate"]["total_output_ratio"], 10)
            self.assertEqual(current["usage_decision"]["decision"], "permitted_without_confirmation")
            artifact = verifier.build_verified_artifact(captured, self.projection, current)
            self.assertEqual(current["projected_at"], OBSERVED_UTC)
            self.assertFalse(current["verification_clock"]["simulation"])
            fixture = verifier.preflight_export(captured, self.projection, fixture_clock="2026-10-01T16:30:00+02:00")
            simulated = verifier.build_verified_artifact(captured, self.projection, fixture)
            self.assertEqual(fixture["projected_at"], "2026-10-01T14:30:00+00:00")
            self.assertEqual(fixture["verification_clock"]["mode"], "fixture_simulation")
            self.assertTrue(fixture["verification_clock"]["simulation"])
            self.assertNotIn("verification_clock", artifact["packet"]["task"])
            self.assertEqual(simulated["packet"]["task"]["verification_clock"], fixture["verification_clock"])
            for source in simulated["packet"]["sources"]:
                attrs = source["observation"]["attrs"]
                self.assertEqual(attrs["verification_clock"], fixture["verification_clock"])
                self.assertEqual(attrs["known_at_semantics"],
                                 "simulated projection time; no actual source observation time claimed")
            plain = adapter.build_artifact(captured, self.projection, projected_at=fixture["projected_at"])
            self.assertNotEqual(simulated["packet"]["packet_id"], plain["packet"]["packet_id"])
            self.assertEqual(adapter.recover_results(simulated), adapter.recover_results(plain))
            for role, row in captured["files"].items():
                self.assertEqual(adapter.recover_files(simulated)[role], row["raw"])
            with self.assertRaisesRegex(ValueError, "fixture_clock_requires_timezone"):
                verifier.preflight_export(captured, self.projection, fixture_clock="2026-10-01T16:30:00")
        for built, preflight in ((artifact, current), (simulated, fixture)):
            self.assertEqual(built["producer_evidence"]["verification_clock"], preflight["verification_clock"])
            self.assertEqual(built["trace"]["projected_at"], preflight["projected_at"])
            self.assertEqual(built["trace"]["prepared_at"], WHEN)
            self.assertTrue(all(row["first_seen"] == preflight["projected_at"] and row["last_seen"] == preflight["projected_at"]
                                for row in built["packet"]["entities"]))
            self.assertTrue(all(row["known_at"] == preflight["projected_at"] and row["observation"]["date"] == preflight["projected_at"]
                                for row in built["packet"]["sources"]))
            self.assertEqual(adapter.codec.validate_packet(built["packet"]), built["packet"])

    def test_verifier_cli_records_awaiting_confirmation_before_native_consumer_import(self):
        self.high_entropy_capture()
        projection = deepcopy(self.projection)
        projection["support"]["quote_chars"] = None
        profile_path = self.root / "guard-projection.json"
        profile_path.write_bytes(raw_json(projection))
        evidence = self.root / "guard-evidence"
        with patch.object(verifier, "datetime", ObservedUtcClock), \
                patch.object(verifier.bridge, "build_artifact", side_effect=AssertionError("blocked CLI built artifact")) as build, \
                patch.object(verifier.importlib.util, "spec_from_file_location", side_effect=AssertionError("blocked CLI imported native consumer")) as imported, \
                patch("sys.stdout"), patch("sys.stderr"):
            with self.assertRaises(SystemExit) as caught:
                verifier.main(["--run", str(self.run), "--projection", str(profile_path),
                               "--library", str(self.root / "not-loaded.so"), "--evidence-dir", str(evidence)])
            self.assertEqual(caught.exception.code, 2)
            build.assert_not_called()
            imported.assert_not_called()
        summary = json.loads((evidence / "verification.json").read_bytes())
        self.assertFalse(summary["passed"])
        self.assertFalse(summary["artifact_built"])
        self.assertEqual(summary["native_verification"], "not_requested")
        self.assertEqual(summary["usage_decision"]["decision"], "awaiting_confirmation")
        self.assertEqual(summary["verification_clock"]["projected_at"], OBSERVED_UTC)
        self.assertEqual({path.name for path in evidence.iterdir()}, {"verification.json"})



if __name__ == "__main__":
    unittest.main()
