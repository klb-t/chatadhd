#!/usr/bin/env python3
"""Replay frozen guard inputs against the pre-migration and current guard.

Inputs are public synthetic regressions and previously archived CI receipts.
This does not execute CTest or synthesize missing JUnit.
"""
import argparse
import ast
import copy
import hashlib
import importlib.util
import inspect
import io
import json
from pathlib import Path
import sys
import unittest
import xml.etree.ElementTree as ET


def module_at(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def source_receipt(path, repo):
    raw = path.read_bytes()
    return {"path": str(path.relative_to(repo)), "sha256": hashlib.sha256(raw).hexdigest(), "bytes": len(raw)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[4])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()
    evidence = repo / "docs/verification/repo-hygiene-followup-2026-10-04/evidence-policy"
    before_path = evidence / "before/verify_ctest.py"
    before_tests_path = evidence / "before/test_verify_ctest.py"
    after_path = repo / ".github/scripts/verify_ctest.py"
    after_tests_path = repo / ".github/scripts/test_verify_ctest.py"
    before = module_at("verify_ctest", before_path)
    before_impl = before.verify
    after = module_at("verify_ctest_after", after_path)
    policy = after.load_policy()
    fixtures = []

    def compare_results(manifest, junit, preset):
        old_result = before_impl(copy.deepcopy(manifest), ET.fromstring(ET.tostring(junit)), preset)
        new_result = after.verify(copy.deepcopy(manifest), ET.fromstring(ET.tostring(junit)), preset, policy)
        comparable = copy.deepcopy(new_result)
        comparable.pop("policy_provenance")
        return old_result, new_result, old_result == comparable

    def capture(manifest, junit, preset):
        old_result, new_result, equal = compare_results(manifest, junit, preset)
        test_name = next((frame.function for frame in inspect.stack()
                          if frame.function.startswith("test_")), "unknown")
        fixtures.append({"test": test_name, "preset": preset,
                         "manifest": copy.deepcopy(manifest),
                         "junit": ET.tostring(junit, encoding="unicode"),
                         "before": old_result, "after": new_result,
                         "equal_except_policy_provenance": equal})
        return old_result

    # The frozen tests retain their original imports and bodies. Hook their
    # normal verifier calls to replay those exact generated inputs in both.
    before.verify = capture
    legacy_tests = module_at("legacy_guard_regressions", before_tests_path)
    stream = io.StringIO()
    legacy_result = unittest.TextTestRunner(stream=stream, verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromModule(legacy_tests))
    before.verify = before_impl

    def methods(path):
        tree = ast.parse(path.read_text())
        return {node.name: ast.dump(node, include_attributes=False)
                for parent in ast.walk(tree) if isinstance(parent, ast.ClassDef) and parent.name == "EvidenceGateTests"
                for node in parent.body if isinstance(node, ast.FunctionDef) and node.name.startswith("test_")}

    methods_unchanged = methods(before_tests_path) == methods(after_tests_path)
    ci_pairs = []
    manifest_paths = {path for directory in (repo / "docs/verification", repo / "docs/archive")
                      for path in directory.rglob("ctest-manifest.json")}
    for manifest_path in sorted(manifest_paths):
        if not any(part.startswith("ci-") for part in manifest_path.parts):
            continue
        preset = manifest_path.parent.parent.name
        junit_path = manifest_path.with_name("ctest.xml")
        row = {"preset": preset, "manifest": source_receipt(manifest_path, repo)}
        if not junit_path.exists():
            row.update(comparison_status="no_original_junit", original_guard_passed=False,
                       boundary="No XML synthesized; LastTest.log-derived counts are a separate receipt.")
        else:
            old_result, new_result, equal = compare_results(
                json.loads(manifest_path.read_bytes()), ET.parse(junit_path).getroot(), preset)
            row.update(junit=source_receipt(junit_path, repo), comparison_status="compared",
                       before=old_result, after=new_result, equal_except_policy_provenance=equal)
        ci_pairs.append(row)

    legacy_equal = all(row["equal_except_policy_provenance"] for row in fixtures)
    ci_equal = all(row["equal_except_policy_provenance"] for row in ci_pairs
                   if row["comparison_status"] == "compared")
    result = {
        "schema": "loom.evidence_policy_migration_comparison/1",
        "boundary": "Offline replay of existing bytes only; no new native/CI/model execution and no reconstructed JUnit.",
        "sources": [source_receipt(path, repo) for path in
                    (before_path, before_tests_path, after_path, after_tests_path,
                     after.DEFAULT_POLICY, after.POLICY_SCHEMA)],
        "policy_provenance": dict(policy.provenance),
        "legacy_regressions": {
            "tests_run": legacy_result.testsRun, "successful": legacy_result.wasSuccessful(),
            "original_16_method_bodies_unchanged": methods_unchanged,
            "fixture_calls": len(fixtures), "all_equal_except_policy_provenance": legacy_equal,
            "log": stream.getvalue(), "fixtures": fixtures,
        },
        "archived_ci": ci_pairs,
        "equivalent_on_all_available_inputs": legacy_result.wasSuccessful() and methods_unchanged and legacy_equal and ci_equal,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"legacy_tests": legacy_result.testsRun, "fixture_calls": len(fixtures),
                      "complete_ci_pairs": sum(row["comparison_status"] == "compared" for row in ci_pairs),
                      "missing_original_junit": sum(row["comparison_status"] == "no_original_junit" for row in ci_pairs),
                      "equivalent": result["equivalent_on_all_available_inputs"]}, indent=2))
    return 0 if result["equivalent_on_all_available_inputs"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
