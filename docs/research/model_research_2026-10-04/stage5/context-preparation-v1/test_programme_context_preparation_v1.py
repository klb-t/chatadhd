"""Fabricated hash-bound preparation tests; no real corpora or model outputs."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import programme_context_preparation_v1 as r


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode()


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def expression(source, pointer="", encoding="value"):
    return {"$from": source, "pointer": pointer, "encoding": encoding}


class ContextPreparationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="fabricated-context-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.output = self.directory / "prepared"
        self.config_path = self.directory / "configuration.json"
        self.selection_path = self.directory / "selection.json"
        self.units_path = self.directory / "units.json"
        self.input_path = self.directory / "fabricated-source.json"
        self.template_path = self.directory / "fabricated-template.json"
        self.floor_path = self.directory / "fabricated-floor.json"
        self.case_ids = ["fabricated-case-C", "fabricated-case-A", "fabricated-case-Z"]
        self.method_ids = ["fabricated-method-blue", "fabricated-method-green"]
        self.payloads = {
            case: {"source_id": case, "text": "żółw e\u0301 é and literal \\n " + case,
                   "nested": {"unchanged": [True, False, 0, "∞"]}}
            for case in self.case_ids}
        self.input_document = {
            "cases": [{"id": case, "payload": self.payloads[case]} for case in reversed(self.case_ids)],
            "unused_document_metadata": {"label": "DO-NOT-PROPAGATE-LABEL", "rubric": "DO-NOT-PROPAGATE-RUBRIC"}}
        self.input_path.write_bytes((json.dumps(self.input_document, ensure_ascii=False, indent=3) + "\n").encode())
        self.body = {
            "model": "fabricated/pinned-model",
            "provider": {"only": ["fabricated-provider"], "allow_fallbacks": False,
                         "require_parameters": True, "max_price": {"prompt": "0.9", "completion": "1.8"}},
            "messages": [{"role": "system", "content": "Exact invented instrukcja: e\u0301 ≠ é"},
                         {"role": "user", "content": "PLACEHOLDER"}],
            "max_tokens": 77, "temperature": .3, "top_p": .95, "seed": 41,
            "stream": False, "usage": {"include": True}, "reasoning": {"enabled": False},
            "response_format": {"type": "json_schema", "json_schema": {
                "name": "fabricated_schema", "strict": True, "schema": {
                    "type": "object", "properties": {"answer": {"type": "string"}},
                    "required": ["answer"], "additionalProperties": False}}},
            "caller_extension": {"opaque": ["retain", {"flag": False, "number": 0}]}}
        self.template_document = {"request": {"body": self.body, "template_note": "invented-note"}}
        self.template_path.write_bytes(canonical(self.template_document))
        self.selection_path.write_bytes(canonical({"selected": {"blue": "chosen-blue", "green": "chosen-green"}}))
        self.units = {"chat": {"prompt_fixed_allowance": 123, "prompt_per_message_allowance": 5, "request": 2},
                      "jev": {"prompt_fixed_allowance": 17, "completion": 0, "request": 3}}
        self.units_path.write_bytes(canonical(self.units))
        self.floor_path.write_bytes(canonical({"usd_floor": "0.015"}))
        preserved = ["/model", "/provider", "/temperature", "/reasoning", "/response_format",
                     "/messages/0/content", "/max_tokens", "/top_p", "/seed", "/caller_extension"]
        methods = []
        for method, selection_key in zip(self.method_ids, ("blue", "green")):
            methods.append({
                "method_id": method, "selection_pointer": "/selected/" + selection_key,
                "selected_configuration_id": "chosen-" + selection_key,
                "input_keys": ["source"],
                "template": {"file": str(self.template_path), "sha256": digest(self.template_path.read_bytes()), "pointer": "/request"},
                "body_pointer": "/body",
                "minimum_reservation": {"file": str(self.floor_path), "sha256": digest(self.floor_path.read_bytes()), "pointer": "/usd_floor"},
                "updates": [{"pointer": "/messages/1/content", "value": expression("input", "/source", "canonical_json")}],
                "preserve_body_pointers": preserved,
                "row_template": {"id": expression("request_id"), "body": expression("body"),
                                 "reservation_usd": expression("minimum_reservation_usd"),
                                 "metadata": {"case_id": expression("case_id"), "method_id": expression("method_id"),
                                              "input_sha256": expression("input_sha256"),
                                              "request_sha256": expression("request_sha256"),
                                              "template_note": expression("template", "/template_note")}}})
        self.config = {
            "schema": "loom.context_preparation/1", "programme_id": "fabricated-programme", "stage_id": "fabricated-context-stage",
            "selection": {"file": str(self.selection_path), "sha256": digest(self.selection_path.read_bytes())},
            "units_policy": {"file": str(self.units_path), "sha256": digest(self.units_path.read_bytes())},
            "inputs": [{"key": "source", "file": str(self.input_path), "sha256": digest(self.input_path.read_bytes()),
                        "rows_pointer": "/cases", "rows_mode": "array", "id_pointer": "/id", "value_pointer": "/payload",
                        "allowed_row_keys": ["id", "payload"]}],
            "expected_case_ids": self.case_ids, "methods": methods,
            "order_dimensions": ["case", "method"],
            "prepared": {"header": {"schema": "loom.fabricated_prepared_inputs/1",
                                      "experiment_id": "fabricated-source-inputs",
                                      "metadata": {"invented_header": "keep-me"}}, "rows_key": "requests"}}
        self.write_config()

    def write_config(self):
        self.config_path.write_bytes(canonical(self.config))

    def prepare(self):
        self.write_config()
        return r.prepare(self.config_path, self.output)

    def verify(self):
        return r.verify(self.config_path, self.output)

    def prepared_rows(self):
        return json.loads((self.output / "source-prepared.json").read_bytes())[self.config["prepared"]["rows_key"]]

    def snapshot_output(self):
        return {str(path.relative_to(self.output)): path.read_bytes() for path in self.output.rglob("*") if path.is_file()}

    def test_configured_population_order_and_exact_preserved_template_fields(self):
        manifest = self.prepare()
        rows = self.prepared_rows()
        self.assertEqual(len(manifest["operations"]), 6)
        self.assertEqual(len({row["id"] for row in rows}), 6)
        self.assertEqual([(row["metadata"]["case_id"], row["metadata"]["method_id"]) for row in rows],
                         [(case, method) for case in self.case_ids for method in self.method_ids])
        for row in rows:
            case = row["metadata"]["case_id"]
            expected = deepcopy(self.body)
            expected["messages"][1]["content"] = canonical(self.payloads[case]).decode()
            self.assertEqual(canonical(row["body"]), canonical(expected))
            self.assertEqual(row["metadata"]["request_sha256"], digest(canonical(row["body"])))
            self.assertEqual(row["metadata"]["template_note"], "invented-note")
        source = json.loads((self.output / "source-prepared.json").read_bytes())
        self.assertEqual(source["schema"], self.config["prepared"]["header"]["schema"])
        self.assertEqual(source["experiment_id"], self.config["prepared"]["header"]["experiment_id"])
        self.assertEqual(source["metadata"]["invented_header"], "keep-me")
        self.assertEqual(self.verify(), manifest)

    def test_unicode_normalization_literal_escapes_and_unit_policy_binding(self):
        manifest = self.prepare()
        for operation in manifest["operations"]:
            raw = (self.output / operation["request_file"]).read_bytes()
            body = json.loads(raw)
            self.assertEqual(raw, canonical(body))
            self.assertEqual(operation["request_sha256"], digest(raw))
            self.assertIn("e\u0301 ≠ é", body["messages"][0]["content"])
            self.assertIn("żółw e\u0301 é", body["messages"][1]["content"])
            self.assertIn("literal \\n", json.loads(body["messages"][1]["content"])["text"])
            self.assertEqual(operation["units_upper_bounds"]["prompt"], len(raw) + 123 + 5 * len(body["messages"]))
            self.assertEqual(operation["units_upper_bounds"]["completion"], 77)
            self.assertEqual(operation["units_upper_bounds"]["request"], 2)
            self.assertEqual(operation["minimum_reservation_usd"], "0.015")
        self.assertEqual(manifest["metadata"]["units_policy_sha256"], digest(canonical(self.units)))

    def test_final_whole_row_digest_is_independent_of_body_digest(self):
        self.config["prepared"]["row_digest_field"] = "request_sha256"
        for method in self.config["methods"]:
            method["row_template"].update(
                input=expression("input"), input_sha256=expression("input_sha256"),
                runtime={"key": "invented-single", "agents": 1, "parameters": {"flag": False}},
                request_sha256="STALE-FABRICATED-DIGEST")
        manifest = self.prepare()
        rows = self.prepared_rows()
        for row, operation in zip(rows, manifest["operations"]):
            expected = digest(canonical({key: value for key, value in row.items() if key != "request_sha256"}))
            self.assertEqual(row["request_sha256"], expected)
            self.assertNotEqual(row["request_sha256"], "STALE-FABRICATED-DIGEST")
            self.assertEqual(operation["request_sha256"], digest(canonical(row["body"])))
            self.assertEqual(row["metadata"]["request_sha256"], operation["request_sha256"])
            self.assertNotEqual(row["request_sha256"], operation["request_sha256"])
            changed = deepcopy(row)
            changed["runtime"]["parameters"]["flag"] = True
            self.assertNotEqual(digest(canonical({k: v for k, v in changed.items() if k != "request_sha256"})), expected)
        self.assertEqual(self.verify(), manifest)

    def test_fabricated_followup_evaluator_reports_zero_prepared_binding_errors(self):
        # Exercise the real prepared/body/response binding consumer while replacing
        # the semantic instrument with a fixture. No source corpora or replies read.
        from loom.tools.structure import programme_followup_results_v1 as evaluator
        self.config["prepared"]["row_digest_field"] = "request_sha256"
        manifest = self.prepare()
        source_raw = (self.output / "source-prepared.json").read_bytes()
        rows = self.prepared_rows()
        by_id = {row["id"]: row for row in rows}
        adapted_hash = digest((self.output / "manifest.json").read_bytes())
        normalized = {"schema": "loom.programme_results/1", "planned_operations": len(rows),
                      "planned_operation_ids": [op["operation_id"] for op in manifest["operations"]],
                      "manifest_sha256": adapted_hash, "requests": [], "responses": [], "rows": []}
        for index, operation in enumerate(manifest["operations"]):
            row = by_id[operation["metadata"]["prepared_request_id"]]
            response_hash = digest(("fabricated-first-envelope-" + str(index)).encode())
            normalized["requests"].append({
                "operation_id": operation["operation_id"], "request_sha256": operation["request_sha256"],
                "body": deepcopy(row["body"]), "metadata": {"prepared_request_id": row["id"],
                                                               "source_manifest_sha256": digest(source_raw)}})
            normalized["responses"].append({
                "operation_id": operation["operation_id"], "response_sha256": response_hash,
                "projection": {"model": "fabricated/observed-model", "choices": [
                    {"message": {"content": "invented fixture response Ż"}}]}})
            normalized["rows"].append({"operation_id": operation["operation_id"],
                "request_sha256": operation["request_sha256"], "manifest_sha256": adapted_hash,
                "response_sha256": response_hash, "response_model": "fabricated/observed-model"})
        config_raw = canonical({"limits": {"invented": 1}})
        policy = {"schema": "loom.programme_followup_results_policy/1", "normalized_schema": normalized["schema"],
                  "sources": {"manifest": {"sha256": digest(source_raw)}, "config": {"sha256": digest(config_raw)}},
                  "instrument_files_sha256": {},
                  "evaluator": {"kind": "frontier_comparison", "resource_limits_path": ["limits"], "backend": "fabricated-fixture"},
                  "prepared": {"requests_path": [self.config["prepared"]["rows_key"]], "id_path": ["id"],
                               "body_path": ["body"], "row_digest_field": "request_sha256"},
                  "content": {"disallowed_projection_fields": [], "model_field": "model", "choices_field": "choices",
                              "text_path": ["message", "content"]}}
        with patch.object(evaluator, "_instrument_files", return_value={"fabricated-fixture": "a" * 64}), \
             patch.object(evaluator, "_frontier", return_value={"mechanical_validity": True}) as instrument:
            report = evaluator.evaluate(canonical(normalized), source_raw, config_raw, policy)
        self.assertTrue(report["input_integrity_valid"])
        self.assertEqual(report["input_integrity_errors"], [])
        self.assertEqual(instrument.call_count, 6)
        self.assertTrue(all(not row["errors"] for row in report["rows"]))
        self.assertTrue(all(not choice["binding_errors"] for row in report["rows"] for choice in row["choices"]))
        self.assertIsNone(report["semantic_accuracy"])

    def test_all_source_hash_mutations_fail_before_outputs(self):
        for source in (self.template_path, self.input_path, self.selection_path, self.units_path, self.floor_path):
            original = source.read_bytes()
            source.write_bytes(original + b" ")
            with self.subTest(source=source.name), self.assertRaises(ValueError):
                self.prepare()
            self.assertFalse(self.output.exists())
            source.write_bytes(original)

    def test_selected_configuration_mismatch_rejected(self):
        self.config["methods"][0]["selected_configuration_id"] = "not-selected"
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_selection_can_explicitly_bind_a_list_of_ids(self):
        selection = json.loads(self.selection_path.read_bytes())
        selection["selected"]["blue"] = ["other-selected", "chosen-blue"]
        self.selection_path.write_bytes(canonical(selection))
        self.config["selection"]["sha256"] = digest(self.selection_path.read_bytes())
        self.assertEqual(len(self.prepare()["operations"]), 6)

    def test_input_duplicate_missing_and_unknown_cases_rejected(self):
        originals = deepcopy(self.input_document)
        variants = [
            originals["cases"] + [deepcopy(originals["cases"][0])],
            originals["cases"][:-1],
            originals["cases"] + [{"id": "unplanned-case", "payload": {"text": "fabricated"}}],
        ]
        for rows in variants:
            document = deepcopy(originals)
            document["cases"] = rows
            self.input_path.write_bytes(canonical(document))
            self.config["inputs"][0]["sha256"] = digest(self.input_path.read_bytes())
            with self.subTest(rows=len(rows)), self.assertRaises(ValueError):
                self.prepare()
            self.assertFalse(self.output.exists())

    def test_expected_case_or_method_duplicates_rejected(self):
        original = deepcopy(self.config)
        variants = []
        changed = deepcopy(original)
        changed["expected_case_ids"].append(changed["expected_case_ids"][0])
        variants.append(changed)
        changed = deepcopy(original)
        changed["methods"].append(deepcopy(changed["methods"][0]))
        variants.append(changed)
        for changed in variants:
            self.config = changed
            with self.assertRaises(ValueError):
                self.prepare()
            self.assertFalse(self.output.exists())

    def test_single_input_rows_union_by_key_and_configured_method_order(self):
        self.config["inputs"] = []
        for index, case in enumerate(reversed(self.case_ids)):
            path = self.directory / ("fabricated-single-" + str(index) + ".json")
            path.write_bytes(canonical({"record": {"id": case, "payload": self.payloads[case]}}))
            self.config["inputs"].append({"key": "source", "file": str(path), "sha256": digest(path.read_bytes()),
                "rows_pointer": "/record", "rows_mode": "single", "id_pointer": "/id", "value_pointer": "/payload",
                "allowed_row_keys": ["id", "payload"]})
        self.config["order_dimensions"] = ["method", "case"]
        self.prepare()
        self.assertEqual([(row["metadata"]["case_id"], row["metadata"]["method_id"]) for row in self.prepared_rows()],
                         [(case, method) for method in self.method_ids for case in self.case_ids])

    def test_prepared_rows_key_is_caller_data(self):
        self.config["prepared"]["rows_key"] = "caller_operations"
        manifest = self.prepare()
        self.assertEqual(len(manifest["operations"]), 6)
        document = json.loads((self.output / "source-prepared.json").read_bytes())
        self.assertIn("caller_operations", document)
        self.assertNotIn("requests", document)
        self.assertEqual(len(self.prepared_rows()), 6)
        self.assertEqual(self.verify(), manifest)

    def test_duplicate_case_across_single_input_bindings_rejected(self):
        self.config["inputs"].append(deepcopy(self.config["inputs"][0]))
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_unconfigured_gold_and_rubric_siblings_are_not_read_or_propagated(self):
        sentinels = {self.directory / "fabricated_gold.json", self.directory / "fabricated_rubric.json"}
        for path in sentinels:
            path.write_bytes(b"DO-NOT-READ-SIBLING")
        original_read = Path.read_bytes
        def guarded(path):
            if path in sentinels:
                raise AssertionError("unconfigured invented gold/rubric sibling read")
            return original_read(path)
        with patch.object(Path, "read_bytes", guarded):
            self.prepare()
        serialized = (self.output / "source-prepared.json").read_text()
        self.assertNotIn("DO-NOT-PROPAGATE-LABEL", serialized)
        self.assertNotIn("DO-NOT-PROPAGATE-RUBRIC", serialized)

    def test_allowed_row_keys_can_assert_no_unconfigured_labels(self):
        document = deepcopy(self.input_document)
        document["cases"][0]["gold_label"] = "fabricated-extra-label"
        self.input_path.write_bytes(canonical(document))
        self.config["inputs"][0]["sha256"] = digest(self.input_path.read_bytes())
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_update_may_not_create_missing_pointer_or_change_preserved_value(self):
        original = deepcopy(self.config)
        for pointer in ("/missing_parent/new_value", "/provider/only/0", "/response_format/json_schema/strict"):
            self.config = deepcopy(original)
            self.config["methods"][0]["updates"] = [{"pointer": pointer, "value": "changed"}]
            with self.subTest(pointer=pointer), self.assertRaises(ValueError):
                self.prepare()
            self.assertFalse(self.output.exists())

    def test_prepared_row_cannot_understate_bound_minimum_floor(self):
        self.config["methods"][0]["row_template"]["reservation_usd"] = "0.00001"
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_row_template_cannot_misrepresent_computed_request_body(self):
        self.config["methods"][0]["row_template"]["body"] = deepcopy(self.body)
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_negative_or_nonfinite_floor_rejected_before_outputs(self):
        for value in ("-1", "NaN", True, None):
            self.floor_path.write_bytes(canonical({"usd_floor": value}))
            for method in self.config["methods"]:
                method["minimum_reservation"]["sha256"] = digest(self.floor_path.read_bytes())
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.prepare()
            self.assertFalse(self.output.exists())

    def test_preparation_is_byte_idempotent_and_verify_rejects_drift(self):
        manifest = self.prepare()
        before = self.snapshot_output()
        self.assertEqual(self.prepare(), manifest)
        self.assertEqual(self.snapshot_output(), before)
        self.assertEqual(self.verify(), manifest)
        path = self.output / manifest["operations"][0]["request_file"]
        path.write_bytes(path.read_bytes() + b" ")
        with self.assertRaises(ValueError):
            self.verify()

    def test_conflicting_existing_output_preflights_before_any_new_artifact(self):
        self.prepare()
        source = deepcopy(self.input_document)
        source["cases"][0]["payload"]["text"] += " new context"
        self.input_path.write_bytes(canonical(source))
        self.config["inputs"][0]["sha256"] = digest(self.input_path.read_bytes())
        before = self.snapshot_output()
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertEqual(self.snapshot_output(), before)

    def test_corrupt_existing_prepared_output_cannot_be_overwritten(self):
        self.prepare()
        path = self.output / "source-prepared.json"
        path.write_bytes(b"fabricated conflicting output")
        before = self.snapshot_output()
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertEqual(self.snapshot_output(), before)


if __name__ == "__main__":
    unittest.main()
