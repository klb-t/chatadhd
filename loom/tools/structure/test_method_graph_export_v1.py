"""Native/shared-format and epistemic-boundary regressions, no provider calls."""
from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

from jsonschema import Draft202012Validator

try:
    from . import method_graph_export_v1 as tool
except ImportError:
    import method_graph_export_v1 as tool


def write_json(path, value):
    raw = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode()
    Path(path).write_bytes(raw)
    return tool.byte_hash(raw)


def fixture(folder, two_presets=False):
    folder = Path(folder)
    method = {"id": "test-method", "method_id": "test.offline_codec", "version": "1",
              "recipe_material": "decode exact source bytes", "recipe_sha256": tool.byte_hash(b"decode exact source bytes"),
              "prompt_sha256": {}, "parameters": {"size": 1},
              "preset": {"id": "test-default", "version": "1", "values": {"size": 1}}, "components": []}
    declarations = [method]
    if two_presets:
        changed = deepcopy(method)
        changed["id"] = "test-method-larger"
        changed["parameters"] = {"size": 1000000000}
        changed["preset"] = {"id": "caller-larger", "version": "1", "values": {"size": 1000000000}}
        declarations.append(changed)
    declaration_hash = write_json(folder / "methods.json", declarations)
    for i, descriptor in enumerate(declarations):
        descriptor["declaration_source"] = {"path": "methods.json", "sha256": declaration_hash}
        descriptor["declaration_pointer"] = "/" + str(i)
    receipt = {"execution_kind": "scripted_not_model", "planned": 3, "valid": 2, "quality": None}
    receipt_hash = write_json(folder / "receipt.json", receipt)
    run = {"id": "synthetic-run", "method_ref": "test-method", "source": {"path": "receipt.json", "sha256": receipt_hash},
           "observed_on": "2026-10-04", "measurement_kind": "scripted_mechanism",
           "population": {"planned": 3, "available": 2, "missing": 1, "unit": "scripted cases",
                          "dependent_observations": "Synthetic mechanism cases, not model trials."},
           "metrics": [{"id": "mechanical_validity", "axis": "mechanism", "location": "/valid",
                        "method": "counted_ratio", "numerator_pointer": "/valid", "denominator_pointer": "/planned",
                        "note": "Scripted parser mechanics, no model accuracy.", "unit": "fraction"}]}
    return {"public_artifacts_only": True, "exported_on": "2026-10-04", "methods": declarations, "runs": [run]}, receipt


class MethodGraphExport(unittest.TestCase):
    def test_native_graphpacket_and_exact_shared_metric_shape(self):
        with tempfile.TemporaryDirectory() as folder:
            config, _ = fixture(folder)
            result = tool.export(config, root=folder)
            tool.codec.validate_packet(result)
            schema = tool.read_json(tool.ROOT / "loom/tools/structure/agentic_graph_v1/graph_packet.schema.json")
            Draft202012Validator(schema).validate(result)
            self.assertEqual(result["schema"], "loom.graph_packet/1")
            metric = next(c for c in result["claims"] if c["predicate"] == "method_evaluation")
            self.assertEqual(metric["value"]["numerator"], 2)
            self.assertEqual(metric["value"]["denominator"], 3)
            tool.validate_shared({"validity": metric["value"]}, "metrics")
            self.assertEqual(metric["assessment"]["evidence_class"], "derived")
            self.assertEqual(metric["qualifiers"]["extra"]["measurement_axis"], "mechanism")
            self.assertEqual(result["provenance"]["claims"][metric["id"]]["origin"]["kind"], "recorded")

    def test_produced_by_resolves_exact_version_and_unrestricted_parameters(self):
        with tempfile.TemporaryDirectory() as folder:
            config, _ = fixture(folder, two_presets=True)
            result = tool.export(config, root=folder)
            versions = [e for e in result["entities"] if e["kind"] == "research_method_version"]
            configured = [e for e in result["entities"] if e["kind"] == "configured_research_method"]
            self.assertEqual(len(versions), 1)
            self.assertEqual(len(configured), 2)
            self.assertIn(1000000000, [e["attrs"]["parameters"]["size"] for e in configured])
            by_id = {e["id"]: e for e in result["entities"]}
            relation = next(c for c in result["claims"] if c["predicate"] == "produced_by")
            target = by_id[relation["object"]]
            self.assertEqual(target["attrs"]["parameters"], {"size": 1})
            self.assertEqual(target["parent"], versions[0]["id"])
            self.assertEqual(relation["qualifiers"]["extra"]["consumer_contract_status"], "proposal_pending_threads_3_4")

    def test_scripted_or_planning_scores_cannot_become_model_quality(self):
        with tempfile.TemporaryDirectory() as folder:
            config, _ = fixture(folder)
            config["runs"][0]["metrics"][0]["axis"] = "model_quality"
            with self.assertRaisesRegex(ValueError, "mechanism_or_planning"):
                tool.export(config, root=folder)
            config["runs"][0]["measurement_kind"] = "historical_actual_model_response_replay"
            with self.assertRaisesRegex(ValueError, "scripted_receipt"):
                tool.export(config, root=folder)

    def test_receipt_bytes_recipe_material_and_declaration_drift_fail(self):
        with tempfile.TemporaryDirectory() as folder:
            config, _ = fixture(folder)
            Path(folder, "receipt.json").write_text("{}")
            with self.assertRaisesRegex(ValueError, "receipt_sha256"):
                tool.export(config, root=folder)
        with tempfile.TemporaryDirectory() as folder:
            config, _ = fixture(folder)
            config["methods"][0]["parameters"]["size"] = 2
            with self.assertRaisesRegex(ValueError, "declaration_source_mismatch"):
                tool.export(config, root=folder)
        with tempfile.TemporaryDirectory() as folder:
            config, _ = fixture(folder)
            config["methods"][0]["recipe_material"] = "different recipe"
            with self.assertRaisesRegex(ValueError, "recipe_material"):
                tool.export(config, root=folder)

    def test_metric_source_pointer_and_counts_are_checked(self):
        with tempfile.TemporaryDirectory() as folder:
            config, _ = fixture(folder)
            config["runs"][0]["metrics"][0]["denominator_pointer"] = "/valid"
            config["runs"][0]["metrics"][0]["numerator_pointer"] = "/planned"
            with self.assertRaisesRegex(ValueError, "ratio_counts"):
                tool.export(config, root=folder)
            config["runs"][0]["metrics"][0]["numerator_pointer"] = "/absent"
            with self.assertRaises(KeyError):
                tool.export(config, root=folder)

    def test_dates_evidence_and_unknown_semantic_result_are_explicit(self):
        with tempfile.TemporaryDirectory() as folder:
            config, _ = fixture(folder)
            config["runs"][0]["metrics"] = [{"id": "semantic_quality", "axis": "unavailable",
                "location": "/quality", "method": "unavailable", "value_pointer": "/quality",
                "note": "No live model result exists.", "unit": "fraction"}]
            result = tool.export(config, root=folder)
            evaluation = next(c for c in result["claims"] if c["predicate"] == "method_evaluation")
            self.assertIsNone(evaluation["value"]["value"])
            self.assertEqual(evaluation["qualifiers"]["valid_from"], "2026-10-04")
            self.assertEqual(evaluation["value"]["evidence"]["location"], "/quality")
            self.assertTrue(evaluation["qualifiers"]["extra"]["run_id"])
            config["runs"][0]["observed_on"] = "20261004"
            with self.assertRaises(ValueError):
                tool.export(config, root=folder)

    def test_existing_model_profiles_preserved_exactly_without_fake_method_model(self):
        with tempfile.TemporaryDirectory() as folder:
            config, _ = fixture(folder)
            source_path = tool.ROOT / "docs/research/model_research_2026-10-04/study/historical_replay/profiles.json"
            raw = source_path.read_bytes()
            Path(folder, "profiles.json").write_bytes(raw)
            document = tool.read_json(source_path)
            config["model_profiles"] = [{"source": {"path": "profiles.json", "sha256": tool.byte_hash(raw)},
                                         "observed_on": "2026-10-04", "method_ref": "test-method"}]
            result = tool.export(config, root=folder)
            preserved = [e["attrs"]["profile"] for e in result["entities"] if e["kind"] == "model_profile"]
            self.assertEqual(preserved, document["profiles"])
            self.assertTrue(all(p["key"]["model"] == "typesafe/jev-1.13" for p in preserved))
            self.assertTrue(all(c["qualifiers"]["extra"]["run_id"] for c in result["claims"]
                                if c["predicate"] == "method_evaluation"))

    def test_export_is_deterministic_and_does_not_modify_receipts_or_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            config, _ = fixture(folder)
            original = deepcopy(config)
            files_before = {p.name: p.read_bytes() for p in Path(folder).iterdir()}
            first = tool.export(config, root=folder)
            second = tool.export(config, root=folder)
            self.assertEqual(first, second)
            self.assertEqual(config, original)
            self.assertEqual(files_before, {p.name: p.read_bytes() for p in Path(folder).iterdir()})
            self.assertFalse(first["task"]["canonical_store_written"])

    def test_arm_counts_keep_failures_and_refuse_shortened_or_duplicate_rows(self):
        source = tool.source_record("fixture.json", b"{}", "2026-10-04")
        receipt = {"requests": 3, "rows": [
            {"id": "one", "arm": "a", "valid": True},
            {"id": "two", "arm": "a", "valid": False},
            {"id": "three", "arm": "b", "valid": True}]}
        spec = {"id": "validity", "location": "/rows", "records_pointer": "/rows",
                "complete_count_pointer": "/requests", "identity_pointer": "/id",
                "match": {"pointer": "/arm", "equals": "a"}, "success_pointer": "/valid",
                "method": "counted_ratio", "unit": "fraction", "note": "Exact arm; include unavailable/failed outcomes."}
        metric = tool.make_metric(spec, receipt, source)
        self.assertEqual((metric["numerator"], metric["denominator"]), (1, 2))
        shortened = deepcopy(receipt); shortened["rows"].pop()
        with self.assertRaisesRegex(ValueError, "incomplete_saved"):
            tool.make_metric(spec, shortened, source)
        duplicate = deepcopy(receipt); duplicate["rows"][2]["id"] = "one"
        with self.assertRaisesRegex(ValueError, "duplicate_saved"):
            tool.make_metric(spec, duplicate, source)

    def test_json_value_support_keeps_exact_utf8_span_and_pointer(self):
        text = '{ "żaba": ["ą", { "a/b": 7 }], "other": 7 }\n'
        start, end = tool.json_span(text, "/żaba/1/a~1b")
        self.assertEqual(text[start:end], "7")
        self.assertGreater(len(text[:start].encode("utf-8")), start)
        self.assertEqual(text[end:], ' }], "other": 7 }\n')

    def test_every_evaluation_has_actual_producer_edge_and_separate_intended_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            config, _ = fixture(folder, two_presets=True)
            config["runs"][0]["intended_method_ref"] = "test-method-larger"
            result = tool.export(config, root=folder)
            entities = {e["id"]: e for e in result["entities"]}
            evaluations = [e["id"] for e in entities.values() if e["kind"] == "research_evaluation"]
            self.assertEqual(len(evaluations), 1)
            actual = next(c for c in result["claims"] if c["predicate"] == "produced_by" and c["subject"] == evaluations[0])
            self.assertEqual(entities[actual["object"]]["attrs"]["parameters"], {"size": 1})
            intended = next(c for c in result["claims"] if c["predicate"] == "intended_configuration")
            self.assertEqual(entities[intended["object"]]["attrs"]["parameters"], {"size": 1000000000})
            self.assertFalse(intended["qualifiers"]["extra"]["configuration_executed_by_real_model"])

    def test_declared_missing_record_identity_stays_in_denominator(self):
        source = tool.source_record("fixture.json", b"{}", "2026-10-04")
        receipt = {"requests": 2, "rows": [{"id": "one", "valid": True}, {"id": "two", "valid": False}]}
        spec = {"id": "validity", "location": "/rows", "records_pointer": "/rows",
                "complete_count_pointer": "/requests", "identity_pointer": "/id",
                "match": {"pointer": "/id", "one_of": ["one", "two", "missing"]},
                "success_pointer": "/valid", "method": "counted_ratio", "unit": "fraction",
                "note": "Planned missing observations cannot improve the denominator."}
        metric = tool.make_metric(spec, receipt, source)
        self.assertEqual((metric["numerator"], metric["denominator"]), (1, 3))
        spec["match"]["one_of"] = ["one", "one"]
        with self.assertRaisesRegex(ValueError, "selection_ids"):
            tool.make_metric(spec, receipt, source)

    def test_arm_label_list_cannot_masquerade_as_planned_identity_denominator(self):
        source = tool.source_record("fixture.json", b"{}", "2026-10-04")
        receipt = {"requests": 2, "rows": [{"id": "one", "arm": "a", "valid": True},
                                           {"id": "two", "arm": "a", "valid": False}]}
        spec = {"id": "validity", "location": "/rows", "records_pointer": "/rows",
                "complete_count_pointer": "/requests", "identity_pointer": "/id",
                "match": {"pointer": "/arm", "one_of": ["a"]}, "success_pointer": "/valid",
                "method": "counted_ratio", "unit": "fraction", "note": "Both arm records must count."}
        with self.assertRaisesRegex(ValueError, "identity_selection_must_use_identity_pointer"):
            tool.make_metric(spec, receipt, source)
        spec["match"] = {"pointer": "/arm", "equals": "a"}
        result = tool.make_metric(spec, receipt, source)
        self.assertEqual((result["numerator"], result["denominator"]), (1, 2))

    def test_event_identity_and_exact_request_response_bindings_cannot_be_swapped(self):
        with tempfile.TemporaryDirectory() as folder:
            config, receipt = fixture(folder)
            receipt["rows"] = [{"request_id": "r1", "valid": True}]
            config["runs"][0]["source"]["sha256"] = write_json(Path(folder, "receipt.json"), receipt)
            request_hash = "a" * 64
            req_sha = write_json(Path(folder, "requests.json"), {"requests": [{"id": "r1", "request_sha256": request_hash}]})
            resp_sha = write_json(Path(folder, "responses.json"), [{"request_sha256": request_hash, "origin": "scripted"}])
            binding = {
                "request": {"source": {"path": "requests.json", "sha256": req_sha}, "location": "/requests/0",
                            "matches": [{"pointer": "/id", "equals": "r1"},
                                        {"pointer": "/request_sha256", "equals": request_hash}]},
                "response": {"source": {"path": "responses.json", "sha256": resp_sha}, "location": "/0",
                             "matches": [{"pointer": "/request_sha256", "equals": request_hash}]}}
            config["runs"][0]["events"] = [{"id": "r1", "identity_pointer": "/request_id",
                                          "location": "/rows/0", "binding": binding}]
            config["runs"][0]["selected_request_ids"] = ["r1"]
            result = tool.export(config, root=folder)
            event = next(e for e in result["entities"] if e["kind"] == "research_event")
            self.assertEqual(event["attrs"]["recorded_result"]["request_id"], "r1")
            self.assertTrue(any(c["predicate"] == "produced_by" and c["subject"] == event["id"] for c in result["claims"]))
            binding["response"]["matches"][0]["equals"] = "b" * 64
            with self.assertRaisesRegex(ValueError, "request_response_binding"):
                tool.export(config, root=folder)
            binding["response"]["matches"][0]["equals"] = request_hash
            config["runs"][0]["selected_request_ids"] = ["r2"]
            with self.assertRaisesRegex(ValueError, "cover_selected_request"):
                tool.export(config, root=folder)


if __name__ == "__main__":
    unittest.main()
