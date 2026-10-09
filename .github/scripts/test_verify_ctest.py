"""Regression fixtures for coverage receipts, not product implementation tests."""
from contextlib import redirect_stderr
import copy
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

from verify_ctest import DEFAULT_POLICY, load_policy, main, verify


def native(cases=2, assertions=7):
    return (f"[doctest] test cases: {cases} | {cases} passed | 0 failed | 624 skipped\n"
            f"[doctest] assertions: {assertions} | {assertions} passed | 0 failed |\n"
            "[doctest] Status: SUCCESS!\n")


def python(cases=3, skipped=0):
    return f"Ran {cases} tests in 0.01s\n\nOK" + (f" (skipped={skipped})" if skipped else "") + "\n"


def fixture(entries):
    manifest = {"kind": "ctestInfo", "version": {"major": 1, "minor": 0},
                "tests": [{"name": name} for name, _ in entries]}
    xml = ET.Element("testsuite", tests=str(len(entries)), failures="0", skipped="0")
    for name, output in entries:
        case = ET.SubElement(xml, "testcase", name=name, status="run")
        ET.SubElement(case, "system-out").text = output
    return manifest, xml


class EvidenceGateTests(unittest.TestCase):
    def test_positive_inner_cases_are_counted_separately_from_entries(self):
        m, x = fixture([("unit.test_context_engine", native(18, 102)),
                        ("research.structure", python(11)), ("cli.smoke", "cli smoke: ok")])
        result = verify(m, x, "dev")
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["executed_entries"], 3)
        self.assertEqual(result["executed_native_cases"], 18)
        self.assertEqual(result["executed_python_cases"], 11)

    def test_historical_zero_success_is_rejected(self):
        for name in ("unit.test_context_engine", "unit.test_knowledge"):
            with self.subTest(name=name):
                m, x = fixture([(name, native(0, 0))])
                result = verify(m, x, "dev")
                self.assertFalse(result["valid"])
                self.assertIn("positive cases and assertions", " ".join(result["errors"]))

    def test_resource_contract_requires_executed_inner_cases_without_skips(self):
        m, x = fixture([("resource_graph.contract", python(7))])
        result = verify(m, x, "dev")
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["executed_python_cases"], 7)
        for output in (python(0), python(7, 1), "", "resource checks passed"):
            with self.subTest(output=output):
                m, x = fixture([("resource_graph.contract", output)])
                self.assertFalse(verify(m, x, "dev")["valid"])

    def test_positive_cases_with_no_assertions_do_not_establish_verification(self):
        m, x = fixture([("unit.test_resolve_lineage", native(2, 0))])
        self.assertFalse(verify(m, x, "dev")["valid"])

    def test_scale_opt_in_is_classified_unexecuted(self):
        m, x = fixture([("unit.test_catalog_scale", native(0, 0)), ("unit.test_db", native())])
        result = verify(m, x, "dev")
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["unexecuted_entries"], ["unit.test_catalog_scale"])
        self.assertEqual(result["executed_entries"], 1)
        self.assertEqual(result["executed_native_cases"], 2)

    def test_enabled_scale_must_have_real_assertions(self):
        m, x = fixture([("unit.test_catalog_scale", native(1, 6))])
        result = verify(m, x, "dev")
        self.assertTrue(result["valid"])
        self.assertEqual(result["unexecuted_entries"], [])

    def test_missing_truncated_and_duplicate_summaries_fail(self):
        for output in ("", "[doctest] Status: SUCCESS!", native() + native(),
                       native() + "[This part of the test output was removed since it exceeds the threshold]"):
            with self.subTest(output=output):
                m, x = fixture([("unit.test_db", output)])
                self.assertFalse(verify(m, x, "dev")["valid"])

    def test_manifest_duplicate_missing_and_extra_entries_fail(self):
        for mutation in ("duplicate_manifest", "duplicate_junit", "missing", "extra"):
            with self.subTest(mutation=mutation):
                m, x = fixture([("unit.test_db", native())])
                if mutation == "duplicate_manifest":
                    m["tests"].append({"name": "unit.test_db"})
                elif mutation == "duplicate_junit":
                    x.append(ET.fromstring(ET.tostring(x[0])))
                elif mutation == "missing":
                    m["tests"].append({"name": "unit.test_graph"})
                else:
                    x[0].set("name", "unit.test_other")
                self.assertFalse(verify(m, x, "dev")["valid"])

    def test_empty_manifest_and_outer_failure_or_skip_fail(self):
        self.assertFalse(verify(*fixture([]), "dev")["valid"])
        for tag in ("failure", "error", "skipped"):
            with self.subTest(tag=tag):
                m, x = fixture([("unit.test_db", native())])
                ET.SubElement(x[0], tag)
                self.assertFalse(verify(m, x, "dev")["valid"])

    def test_outer_suite_failure_counts_fail_even_without_child(self):
        m, x = fixture([("unit.test_db", native())])
        x.set("failures", "1")
        self.assertFalse(verify(m, x, "dev")["valid"])

    def test_aggregated_outer_skip_and_invalid_xml_root_fail(self):
        m, x = fixture([("unit.test_db", native())])
        outer = ET.Element("testsuites", skipped="1")
        outer.append(x)
        self.assertFalse(verify(m, outer, "dev")["valid"])
        x.tag = "arbitrary-document"
        self.assertFalse(verify(m, x, "dev")["valid"])

    def test_python_zero_missing_and_unexpected_skips_fail(self):
        for preset in ("dev", "vendored", "asan"):
            for output in (python(0), python(3, 1), "", "Ran 3 tests in 0.1s\nFAILED (errors=1)"):
                with self.subTest(preset=preset, output=output):
                    m, x = fixture([("research.structure", output)])
                    self.assertFalse(verify(m, x, preset)["valid"])

    def test_asan_existing_shared_library_unavailability_is_explicit(self):
        unavailable = "test_ffi ... skipped 'LOOM_LIBRARY not set (build with LOOM_SHARED=ON)'\n"
        entries = [("compat.test_abi_compat", unavailable + python(3, 2)),
                   ("compat.test_graph_packet_store", unavailable + python(18, 18)),
                   ("compat.test_chat_active_task_http", unavailable + python(6, 6))]
        m, x = fixture(entries)
        result = verify(m, x, "asan")
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["executed_entries"], 1)
        self.assertEqual(result["executed_python_cases"], 1)
        self.assertEqual(result["skipped_python_cases"], 26)
        self.assertEqual(len(result["unexecuted_entries"]), 2)
        for preset in ("dev", "vendored"):
            self.assertFalse(verify(m, x, preset)["valid"])

    def test_asan_exception_does_not_allow_partial_or_other_skips(self):
        for name, output in (("compat.test_abi_compat", python(3, 1)),
                             ("compat.test_graph_packet_store", python(18, 1)),
                             ("compat.test_other", python(2, 2))):
            with self.subTest(name=name):
                m, x = fixture([(name, output)])
                self.assertFalse(verify(m, x, "asan")["valid"])

    def test_asan_named_exception_requires_documented_unavailability(self):
        m, x = fixture([("compat.test_graph_packet_store", python(18, 18))])
        self.assertFalse(verify(m, x, "asan")["valid"])

    def test_smoke_explicit_skip_is_not_success(self):
        for name in ("server.smoke", "cli.smoke", "eval.knowledge"):
            m, x = fixture([(name, "SKIP: missing dependency")])
            self.assertFalse(verify(m, x, "dev")["valid"])

    def test_cli_writes_reviewable_evidence_and_nonzero_exit(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td)
            m, x = fixture([("unit.test_context_engine", native(0, 0))])
            (path / "manifest.json").write_text(json.dumps(m))
            (path / "results.xml").write_bytes(ET.tostring(x))
            with redirect_stderr(io.StringIO()):
                status = main(["--manifest", str(path / "manifest.json"),
                               "--junit", str(path / "results.xml"),
                               "--output", str(path / "evidence.json"), "--preset", "dev"])
            self.assertEqual(status, 1)
            result = json.loads((path / "evidence.json").read_text())
            self.assertFalse(result["valid"])
            self.assertEqual(set(result["input_sha256"]), {"manifest", "junit"})


class PolicyLoaderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.defaults = json.loads(DEFAULT_POLICY.read_text())

    def write_policy(self, data):
        path = self.path / "policy.json"
        path.write_text(json.dumps(data, indent=2) + "\n")
        return path

    def test_version_and_exact_bytes_are_in_evidence(self):
        data = copy.deepcopy(self.defaults)
        data["revision"] = 2
        path = self.write_policy(data)
        policy = load_policy(path)
        m, x = fixture([("unit.test_db", native())])
        result = verify(m, x, "dev", policy)
        self.assertTrue(result["valid"])
        self.assertEqual(result["policy_provenance"]["revision"], 2)
        self.assertEqual(result["policy_provenance"]["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual(result["policy_provenance"]["path"], str(path.resolve()))

    def test_loaded_policy_cannot_drift_after_hashing(self):
        policy = load_policy(self.write_policy(self.defaults))
        unavailable = "test_ffi ... skipped 'LOOM_LIBRARY not set (build with LOOM_SHARED=ON)'\n"
        m, x = fixture([("compat.test_graph_packet_store", unavailable + python(18, 18))])
        original = verify(m, x, "dev", policy)
        self.assertFalse(original["valid"])
        self.assertEqual(original["executed_python_cases"], 0)
        entries = policy.data["presets"]["dev"]["unavailable_entries"]
        self.assertIsInstance(entries, tuple)
        with self.assertRaises(AttributeError):
            entries.append(self.defaults["presets"]["asan"]["unavailable_entries"][0])
        with self.assertRaises(TypeError):
            policy.data["presets"]["dev"]["unavailable_entries"] = ()
        with self.assertRaises(TypeError):
            policy.data["presets"]["asan"]["unavailable_entries"][0]["required_output"] = "anything"
        with self.assertRaises(TypeError):
            policy.data["runner_bindings"][0]["match"]["prefix"] = "compat."
        with self.assertRaises(TypeError):
            policy.provenance["revision"] = 999
        self.assertEqual(verify(m, x, "dev", policy), original)
        m, x = fixture([("unit.test_db", native())])
        first = verify(m, x, "dev", policy)
        first["policy_provenance"]["revision"] = 999
        self.assertEqual(verify(m, x, "dev", policy)["policy_provenance"]["revision"], self.defaults["revision"])

    def test_new_declared_preset_and_runner_need_no_code_branch(self):
        data = copy.deepcopy(self.defaults)
        data["presets"]["packet-ci"] = {"unavailable_entries": []}
        data["runner_bindings"].append({"match": {"prefix": "packet."}, "runner": "doctest"})
        policy = load_policy(self.write_policy(data))
        m, x = fixture([("packet.native", native(4, 24))])
        result = verify(m, x, "packet-ci", policy)
        self.assertTrue(result["valid"], result["errors"])
        self.assertEqual(result["executed_native_cases"], 4)
        self.assertEqual(result["executed_native_assertions"], 24)
        self.assertFalse(verify(m, x, "undeclared-preset", policy)["valid"])

    def test_explicit_policy_cli_accepts_declared_preset(self):
        data = copy.deepcopy(self.defaults)
        data["presets"]["custom-dev"] = {"unavailable_entries": []}
        policy_path = self.write_policy(data)
        m, x = fixture([("research.example", python(2))])
        (self.path / "manifest.json").write_text(json.dumps(m))
        (self.path / "junit.xml").write_bytes(ET.tostring(x))
        with redirect_stderr(io.StringIO()):
            status = main(["--policy", str(policy_path), "--preset", "custom-dev",
                           "--manifest", str(self.path / "manifest.json"),
                           "--junit", str(self.path / "junit.xml"),
                           "--output", str(self.path / "result.json")])
        self.assertEqual(status, 0)
        self.assertEqual(json.loads((self.path / "result.json").read_text())["executed_python_cases"], 2)

    def test_malformed_schema_values_and_generic_ignore_are_rejected(self):
        for mutation in ("unknown_schema", "bool_revision", "unknown_field", "wildcard", "empty_reason",
                         "negative_skips", "disable_execution", "unsupported_runner", "missing_revision"):
            with self.subTest(mutation=mutation):
                data = copy.deepcopy(self.defaults)
                entries = data["presets"]["asan"]["unavailable_entries"]
                if mutation == "unknown_schema": data["schema"] = "unsupported/2"
                elif mutation == "bool_revision": data["revision"] = True
                elif mutation == "unknown_field": data["ignore"] = ["*"]
                elif mutation == "wildcard": entries[0]["name"] = "compat.*"
                elif mutation == "empty_reason": entries[0]["required_output"] = ""
                elif mutation == "negative_skips": entries[2]["skipped_cases"] = -1
                elif mutation == "disable_execution": entries[2]["requires_execution"] = False
                elif mutation == "unsupported_runner": data["default_runner"] = "assume_passed"
                elif mutation == "missing_revision": del data["revision"]
                with self.assertRaises(ValueError):
                    load_policy(self.write_policy(data))

    def test_duplicate_and_conflicting_policy_bindings_are_rejected(self):
        for mutation in ("runner", "opt_in", "availability", "runner_mismatch"):
            with self.subTest(mutation=mutation):
                data = copy.deepcopy(self.defaults)
                if mutation == "runner": data["runner_bindings"].append(data["runner_bindings"][0])
                elif mutation == "opt_in": data["opt_in_entries"].append(data["opt_in_entries"][0])
                elif mutation == "availability":
                    entries = data["presets"]["asan"]["unavailable_entries"]
                    entries.append(entries[0])
                else: data["runner_bindings"][1]["runner"] = "script"
                with self.assertRaises(ValueError):
                    load_policy(self.write_policy(data))

    def test_duplicate_json_keys_and_missing_files_do_not_fall_back(self):
        path = self.path / "duplicate.json"
        path.write_text('{"revision":1,"revision":2}')
        with self.assertRaisesRegex(ValueError, "duplicate policy JSON key"):
            load_policy(path)
        with self.assertRaises(FileNotFoundError):
            load_policy(self.path / "does-not-exist.json")

    def test_declared_unavailability_requires_exact_name_and_matching_reason(self):
        data = copy.deepcopy(self.defaults)
        data["presets"]["instrumented"] = copy.deepcopy(data["presets"]["asan"])
        policy = load_policy(self.write_policy(data))
        for name, reason, accepted in (
                ("compat.test_graph_packet_store", "LOOM_LIBRARY not set", True),
                ("compat.test_graph_packet_store", "fixture unavailable for another reason", False),
                ("compat.test_other", "LOOM_LIBRARY not set", False)):
            with self.subTest(name=name, reason=reason):
                m, x = fixture([(name, reason + "\n" + python(18, 18))])
                result = verify(m, x, "instrumented", policy)
                self.assertEqual(result["valid"], accepted)
                self.assertEqual(result["executed_python_cases"], 0)
                if accepted:
                    self.assertEqual(result["executed_entries"], 0)
                    self.assertEqual(result["unexecuted_entries"], [name])

    def test_policy_exception_cannot_hide_outer_failure_or_missing_manifest_entry(self):
        policy = load_policy()
        output = "LOOM_LIBRARY not set\n" + python(18, 18)
        m, x = fixture([("compat.test_graph_packet_store", output)])
        ET.SubElement(x[0], "failure")
        self.assertFalse(verify(m, x, "asan", policy)["valid"])
        m["tests"].append({"name": "unit.test_db"})
        self.assertFalse(verify(m, x, "asan", policy)["valid"])

    def test_invalid_policy_cli_writes_failure_evidence(self):
        path = self.write_policy({"ignore": ["*"]})
        m, x = fixture([("unit.test_db", native())])
        (self.path / "manifest.json").write_text(json.dumps(m))
        (self.path / "junit.xml").write_bytes(ET.tostring(x))
        with redirect_stderr(io.StringIO()):
            status = main(["--policy", str(path), "--preset", "dev",
                           "--manifest", str(self.path / "manifest.json"),
                           "--junit", str(self.path / "junit.xml"),
                           "--output", str(self.path / "result.json")])
        self.assertEqual(status, 1)
        result = json.loads((self.path / "result.json").read_text())
        self.assertFalse(result["valid"])
        self.assertIn("invalid evidence policy", result["errors"][0])

    def test_valid_policy_provenance_survives_missing_test_input(self):
        path = self.write_policy(self.defaults)
        (self.path / "manifest.json").write_text(json.dumps({"kind": "ctestInfo", "tests": []}))
        with redirect_stderr(io.StringIO()):
            status = main(["--policy", str(path), "--preset", "dev",
                           "--manifest", str(self.path / "manifest.json"),
                           "--junit", str(self.path / "missing.xml"),
                           "--output", str(self.path / "result.json")])
        self.assertEqual(status, 1)
        result = json.loads((self.path / "result.json").read_text())
        self.assertFalse(result["valid"])
        self.assertEqual(result["policy_provenance"]["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())

    def test_malformed_policy_schema_cli_writes_negative_evidence(self):
        policy_path = self.write_policy(self.defaults)
        schema_path = self.path / "malformed-schema.json"
        schema_path.write_text('{"type":"not-a-json-schema-type"}')
        m, x = fixture([("unit.test_db", native())])
        (self.path / "manifest.json").write_text(json.dumps(m))
        (self.path / "junit.xml").write_bytes(ET.tostring(x))
        with patch("verify_ctest.POLICY_SCHEMA", schema_path), redirect_stderr(io.StringIO()):
            status = main(["--policy", str(policy_path), "--preset", "dev",
                           "--manifest", str(self.path / "manifest.json"),
                           "--junit", str(self.path / "junit.xml"),
                           "--output", str(self.path / "result.json")])
        self.assertEqual(status, 1)
        result = json.loads((self.path / "result.json").read_text())
        self.assertFalse(result["valid"])
        self.assertIn("invalid evidence policy schema", result["errors"][0])


if __name__ == "__main__":
    unittest.main()
