"""Regression fixtures for coverage receipts, not product implementation tests."""
from contextlib import redirect_stderr
import io
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

from verify_ctest import main, verify


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


if __name__ == "__main__":
    unittest.main()
