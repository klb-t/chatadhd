#!/usr/bin/env python3
"""Validate CTest entry coverage and the cases actually executed inside each entry.

Use complete (untruncated) JUnit output. This gate does not change any product
quality threshold. The only availability exceptions describe existing presets:
the opt-in catalogue scale case, and shared-library checks in non-shared ASan.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


SCHEMA = "loom.ctest_evidence/1"
PRESETS = ("dev", "asan", "vendored")
ASAN_ALL_SKIPPED = frozenset({
    "compat.test_graph_packet_store", "compat.test_chat_active_task_http",
})
CASE_SUMMARY = re.compile(
    r"^\[doctest\] test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*"
    r"(\d+) failed\s*\|(?:\s*(\d+) skipped)?\s*$", re.MULTILINE)
ASSERT_SUMMARY = re.compile(
    r"^\[doctest\] assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*"
    r"(\d+) failed\s*\|\s*$", re.MULTILINE)
PYTHON_COUNT = re.compile(r"^Ran (\d+) tests? in .+$", re.MULTILINE)
PYTHON_OK = re.compile(r"^OK(?: \(([^\n]*)\))?\s*$", re.MULTILINE)
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def verify(manifest: dict, junit: ET.Element, preset: str) -> dict:
    """Return observed counts and errors; a valid result has no errors."""
    errors: list[str] = []
    observations: list[dict] = []
    if preset not in PRESETS:
        errors.append(f"unsupported preset: {preset}")
    if manifest.get("kind") != "ctestInfo" or not isinstance(manifest.get("tests"), list):
        errors.append("manifest must be CTest --show-only=json-v1 output")
        manifest_tests = []
    else:
        manifest_tests = manifest["tests"]
    expected_names = []
    for test in manifest_tests:
        if not isinstance(test, dict) or not isinstance(test.get("name"), str) or not test["name"]:
            errors.append("manifest contains an entry without a nonempty name")
        else:
            expected_names.append(test["name"])
    if not expected_names:
        errors.append("manifest contains no test entries")
    expected = Counter(expected_names)
    for name, count in sorted(expected.items()):
        if count != 1:
            errors.append(f"duplicate manifest entry: {name} ({count})")

    if junit.tag not in {"testsuite", "testsuites"}:
        errors.append("JUnit root must be testsuite or testsuites")
    cases = list(junit.iter("testcase"))
    actual = Counter(case.get("name", "") for case in cases)
    for name, count in sorted(actual.items()):
        if count != 1:
            errors.append(f"duplicate JUnit entry: {name} ({count})")
    for name in sorted(set(expected) - set(actual)):
        errors.append(f"missing JUnit entry: {name}")
    for name in sorted(set(actual) - set(expected)):
        errors.append(f"unexpected JUnit entry: {name}")
    for suite in (element for element in junit.iter() if element.tag in {"testsuite", "testsuites"}):
        for attribute in ("failures", "errors", "skipped", "disabled"):
            try:
                count = int(suite.get(attribute, "0"))
            except ValueError:
                errors.append(f"invalid JUnit suite {attribute} count")
                continue
            if count != 0:
                errors.append(f"JUnit suite reports {attribute}={count}")

    for case in cases:
        name = case.get("name", "")
        output = ANSI.sub("", "\n".join(
            case.findtext(tag, "") for tag in ("system-out", "system-err")))
        row: dict = {"name": name, "classification": "executed"}
        observations.append(row)
        before = len(errors)
        if case.get("status", "run") != "run":
            errors.append(f"{name}: outer status is {case.get('status')!r}")
        if any(case.find(tag) is not None for tag in ("failure", "error", "skipped")):
            errors.append(f"{name}: outer failure/error/skip")
        if "[This part of the test output was removed" in output or re.search(
                r"\boutput (?:was )?truncated\b", output, re.IGNORECASE):
            errors.append(f"{name}: truncated output cannot establish case coverage")
        if re.search(r"^\s*SKIP:", output, re.MULTILINE):
            errors.append(f"{name}: script reported SKIP")

        if name.startswith("unit."):
            case_summaries = CASE_SUMMARY.findall(output)
            assertion_summaries = ASSERT_SUMMARY.findall(output)
            if len(case_summaries) != 1 or len(assertion_summaries) != 1:
                errors.append(f"{name}: missing or ambiguous doctest summary")
            else:
                count, passed, failed = map(int, case_summaries[0][:3])
                assertions, assertion_passed, assertion_failed = map(int, assertion_summaries[0])
                row.update(cases=count, assertions=assertions)
                if failed or assertion_failed or passed != count or assertion_passed != assertions:
                    errors.append(f"{name}: doctest case/assertion failures")
                if count == 0 and assertions == 0 and name == "unit.test_catalog_scale":
                    row.update(classification="unexecuted", reason="existing opt-in scale test")
                elif count <= 0 or assertions <= 0:
                    errors.append(f"{name}: requires positive cases and assertions; got {count}/{assertions}")
        elif (name.startswith(("compat.", "research."))
              or name in {"server.chat_active_task", "eval.harness"}):
            counts = PYTHON_COUNT.findall(output)
            results = PYTHON_OK.findall(output)
            if len(counts) != 1 or len(results) != 1:
                errors.append(f"{name}: missing or ambiguous successful unittest summary")
            else:
                count = int(counts[0])
                status = results[0].strip()
                skipped = 0
                if status:
                    match = re.fullmatch(r"skipped=(\d+)", status)
                    if match is None:
                        errors.append(f"{name}: unsupported unittest result: {status}")
                    else:
                        skipped = int(match.group(1))
                executed = count - skipped
                row.update(discovered_cases=count, cases=executed, skipped_cases=skipped)
                if count <= 0 or skipped > count:
                    errors.append(f"{name}: invalid/empty unittest count {count}, skipped {skipped}")
                allowed = False
                shared_unavailable = "LOOM_LIBRARY not set" in output
                if preset == "asan" and count > 0 and shared_unavailable:
                    if name in ASAN_ALL_SKIPPED and skipped == count:
                        allowed = True
                        row.update(classification="unexecuted", reason="existing ASan preset has LOOM_SHARED=OFF")
                    elif name == "compat.test_abi_compat" and skipped == 2 and executed > 0:
                        allowed = True
                        row["availability_note"] = "two existing shared-library ABI cases unavailable in ASan"
                if skipped and not allowed:
                    errors.append(f"{name}: {skipped} unittest skips are not allowed for {preset}")
                if executed <= 0 and not allowed:
                    errors.append(f"{name}: no unittest case executed")
        else:
            # Smoke/evaluation commands are scripts, not unittest suites. Their
            # outer exit status and explicit SKIP diagnostics remain mandatory.
            row["case_count"] = "script does not expose an inner case count"
        if len(errors) > before:
            row["classification"] = "invalid"

    return {
        "schema": SCHEMA,
        "preset": preset,
        "valid": not errors,
        "manifest_entries": len(expected_names),
        "junit_entries": len(cases),
        "executed_entries": sum(row["classification"] == "executed" for row in observations),
        "unexecuted_entries": [row["name"] for row in observations if row["classification"] == "unexecuted"],
        "executed_native_cases": sum(row.get("cases", 0) for row in observations
                                     if row["name"].startswith("unit.") and row["classification"] == "executed"),
        "executed_native_assertions": sum(row.get("assertions", 0) for row in observations
                                          if row["classification"] == "executed"),
        "executed_python_cases": sum(row.get("cases", 0) for row in observations
                                     if "discovered_cases" in row and row["classification"] == "executed"),
        "skipped_python_cases": sum(row.get("skipped_cases", 0) for row in observations),
        "entries": observations,
        "errors": errors,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--junit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preset", choices=PRESETS, required=True)
    args = parser.parse_args(argv)
    try:
        manifest_bytes = args.manifest.read_bytes()
        junit_bytes = args.junit.read_bytes()
        result = verify(json.loads(manifest_bytes), ET.fromstring(junit_bytes), args.preset)
        result["input_sha256"] = {
            "manifest": hashlib.sha256(manifest_bytes).hexdigest(),
            "junit": hashlib.sha256(junit_bytes).hexdigest(),
        }
    except (OSError, ValueError, ET.ParseError, TypeError, AttributeError) as exc:
        result = {"schema": SCHEMA, "preset": args.preset, "valid": False,
                  "errors": [f"cannot validate input: {exc}"]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if result["valid"]:
        print(f"Verified {result['executed_entries']} executed CTest entries; "
              f"{len(result['unexecuted_entries'])} explicitly unexecuted entries.")
        return 0
    for error in result["errors"]:
        print(error, file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
