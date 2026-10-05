#!/usr/bin/env python3
"""Validate CTest entry coverage and the cases actually executed inside each entry.

Use complete (untruncated) JUnit output. This gate does not change any product
quality threshold. The only availability exceptions describe existing presets:
the opt-in catalogue scale case, and shared-library checks in non-shared ASan.
"""
from __future__ import annotations

import argparse
from collections import Counter
from collections.abc import Mapping
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys
from types import MappingProxyType
from typing import Any
import xml.etree.ElementTree as ET

from jsonschema import Draft202012Validator
from jsonschema.exceptions import SchemaError


SCHEMA = "loom.ctest_evidence/1"
DEFAULT_POLICY = Path(__file__).resolve().parents[1] / "ctest-evidence-policy.json"
POLICY_SCHEMA = Path(__file__).resolve().parents[1] / "ctest-evidence-policy.schema.json"
CASE_SUMMARY = re.compile(
    r"^\[doctest\] test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*"
    r"(\d+) failed\s*\|(?:\s*(\d+) skipped)?\s*$", re.MULTILINE)
ASSERT_SUMMARY = re.compile(
    r"^\[doctest\] assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*"
    r"(\d+) failed\s*\|\s*$", re.MULTILINE)
PYTHON_COUNT = re.compile(r"^Ran (\d+) tests? in .+$", re.MULTILINE)
PYTHON_OK = re.compile(r"^OK(?: \(([^\n]*)\))?\s*$", re.MULTILINE)
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


@dataclass(frozen=True)
class EvidencePolicy:
    # JSON objects are read-only mappings; JSON arrays are tuples at every depth.
    data: Mapping[str, Any]
    provenance: Mapping[str, str | int]

    def runner(self, name: str) -> str:
        for binding in self.data["runner_bindings"]:
            match = binding["match"]
            if (("name" in match and name == match["name"])
                    or ("prefix" in match and name.startswith(match["prefix"]))):
                return binding["runner"]
        return self.data["default_runner"]

    def opt_in(self, name: str) -> Mapping[str, Any] | None:
        return next((entry for entry in self.data["opt_in_entries"]
                     if entry["name"] == name), None)

    def unavailable(self, preset: str, name: str) -> Mapping[str, Any] | None:
        settings = self.data["presets"].get(preset)
        if settings is None:
            return None
        return next((entry for entry in settings["unavailable_entries"]
                     if entry["name"] == name), None)


def _unique_json_object(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate policy JSON key: {key}")
        result[key] = value
    return result


def _immutable(value: Any) -> Any:
    """Copy JSON objects to read-only mappings and arrays to immutable tuples."""
    if isinstance(value, dict):
        return MappingProxyType({key: _immutable(child) for key, child in value.items()})
    if isinstance(value, list):
        return tuple(_immutable(child) for child in value)
    return value


def load_policy(path: Path = DEFAULT_POLICY) -> EvidencePolicy:
    """Load a local descriptor; schema validation never fetches external data."""
    path = Path(path)
    raw = path.read_bytes()
    schema_raw = POLICY_SCHEMA.read_bytes()
    data = json.loads(raw, object_pairs_hook=_unique_json_object)
    schema = json.loads(schema_raw, object_pairs_hook=_unique_json_object)
    try:
        Draft202012Validator.check_schema(schema)
    except SchemaError as exc:
        raise ValueError(f"invalid evidence policy schema: {exc.message}") from exc
    violations = sorted(Draft202012Validator(schema).iter_errors(data),
                        key=lambda error: tuple(str(part) for part in error.absolute_path))
    if violations:
        details = "; ".join(
            f"/{'/'.join(str(part) for part in error.absolute_path)}: {error.message}"
            for error in violations)
        raise ValueError(f"invalid evidence policy: {details}")
    policy = EvidencePolicy(_immutable(data), _immutable({
        "schema": data["schema"], "revision": data["revision"],
        "sha256": hashlib.sha256(raw).hexdigest(),
        "schema_sha256": hashlib.sha256(schema_raw).hexdigest(),
        "path": str(path.resolve()),
    }))
    matches = [json.dumps(binding["match"], sort_keys=True)
               for binding in data["runner_bindings"]]
    if len(matches) != len(set(matches)):
        raise ValueError("invalid evidence policy: duplicate runner binding")
    opt_in_names = [entry["name"] for entry in data["opt_in_entries"]]
    if len(opt_in_names) != len(set(opt_in_names)):
        raise ValueError("invalid evidence policy: duplicate opt-in entry")
    for entry in data["opt_in_entries"]:
        if policy.runner(entry["name"]) != entry["runner"]:
            raise ValueError(f"invalid evidence policy: opt-in runner mismatch for {entry['name']}")
    for preset, settings in data["presets"].items():
        names = [entry["name"] for entry in settings["unavailable_entries"]]
        if len(names) != len(set(names)):
            raise ValueError(f"invalid evidence policy: duplicate unavailable entry in {preset}")
        for name in names:
            if policy.runner(name) != "unittest":
                raise ValueError(f"invalid evidence policy: unavailable runner mismatch for {name}")
    return policy


def verify(manifest: dict, junit: ET.Element, preset: str,
           policy: EvidencePolicy | None = None) -> dict:
    """Return observed counts and errors; a valid result has no errors."""
    policy = load_policy() if policy is None else policy
    errors: list[str] = []
    observations: list[dict] = []
    if preset not in policy.data["presets"]:
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

        runner = policy.runner(name)
        if runner == "doctest":
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
                opt_in = policy.opt_in(name)
                if (opt_in is not None and count == opt_in["when"]["cases"]
                        and assertions == opt_in["when"]["assertions"]):
                    row.update(classification="unexecuted", reason=opt_in["reason"])
                elif count <= 0 or assertions <= 0:
                    errors.append(f"{name}: requires positive cases and assertions; got {count}/{assertions}")
        elif runner == "unittest":
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
                unavailable = policy.unavailable(preset, name)
                if (unavailable is not None and count > 0
                        and unavailable["required_output"] in output):
                    if unavailable["mode"] == "all_discovered_skipped" and skipped == count:
                        allowed = True
                        row.update(classification="unexecuted", reason=unavailable["reason"])
                    elif (unavailable["mode"] == "exact_skipped"
                          and skipped == unavailable["skipped_cases"] and executed > 0):
                        allowed = True
                        row["availability_note"] = unavailable["availability_note"]
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
        "policy_provenance": dict(policy.provenance),
        "valid": not errors,
        "manifest_entries": len(expected_names),
        "junit_entries": len(cases),
        "executed_entries": sum(row["classification"] == "executed" for row in observations),
        "unexecuted_entries": [row["name"] for row in observations if row["classification"] == "unexecuted"],
        "executed_native_cases": sum(row.get("cases", 0) for row in observations
                                     if policy.runner(row["name"]) == "doctest" and row["classification"] == "executed"),
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
    parser.add_argument("--preset", required=True)
    parser.add_argument("--policy", type=Path, default=DEFAULT_POLICY,
                        help="local versioned evidence policy (default: %(default)s)")
    args = parser.parse_args(argv)
    policy = None
    try:
        policy = load_policy(args.policy)
        manifest_bytes = args.manifest.read_bytes()
        junit_bytes = args.junit.read_bytes()
        result = verify(json.loads(manifest_bytes), ET.fromstring(junit_bytes), args.preset, policy)
        result["input_sha256"] = {
            "manifest": hashlib.sha256(manifest_bytes).hexdigest(),
            "junit": hashlib.sha256(junit_bytes).hexdigest(),
        }
    except (OSError, ValueError, ET.ParseError, TypeError, AttributeError) as exc:
        result = {"schema": SCHEMA, "preset": args.preset, "valid": False,
                  "errors": [f"cannot validate input: {exc}"]}
    if policy is not None:
        result["policy_provenance"] = dict(policy.provenance)
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
