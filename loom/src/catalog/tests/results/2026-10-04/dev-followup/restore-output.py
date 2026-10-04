#!/usr/bin/env python3
"""Restore truncated CTest system-out from the same run's complete LastTest.log.

No tests are executed. Original files, XML status/counter/time attributes and
all XML content outside the named truncated system-out texts stay unchanged.
The separate receipt records raw input/output hashes and exact prefix matches.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re
import shlex
import xml.etree.ElementTree as ET


HEADER = re.compile(r"(?m)^(\d+)/(\d+) Testing: ([^\n]+)\n")
OUTPUT_START = "Output:\n----------------------------------------------------------\n"
OUTPUT_END = "<end of output>\nTest time = "
TRUNCATED = re.compile(
    r"\.\.\.\n\[This part of the test output was removed since it exceeds the "
    r"threshold of (\d+) bytes\.\]\n\Z")


def sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def metadata(root: ET.Element, changed: set[str]) -> bytes:
    copied = deepcopy(root)
    for case in copied.iter("testcase"):
        if case.get("name") in changed:
            case.find("system-out").text = ""
    return ET.tostring(copied, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("manifest", "junit", "last-test", "output", "receipt"):
        parser.add_argument("--" + arg, type=Path, required=True)
    args = parser.parse_args()
    inputs = {"manifest": args.manifest, "original_junit": args.junit,
              "last_test": args.last_test}
    raw = {key: path.read_bytes() for key, path in inputs.items()}
    for output in (args.output, args.receipt):
        assert output.resolve() not in {p.resolve() for p in inputs.values()}, "output overwrites input"
    manifest = json.loads(raw["manifest"])
    expected = {test["name"]: test["command"] for test in manifest["tests"]}
    assert len(expected) == len(manifest["tests"]), "duplicate manifest name"
    root = ET.fromstring(raw["original_junit"])
    cases = list(root.iter("testcase"))
    by_name = {case.get("name"): case for case in cases}
    assert len(by_name) == len(cases), "duplicate JUnit name"
    assert set(expected) == set(by_name), "manifest/JUnit names differ"
    text = raw["last_test"].decode("utf-8")
    assert text.startswith("Start testing:"), "LastTest start missing"
    assert re.search(r"(?m)^End testing: .+\n?\Z", text), "LastTest end missing"
    headers = list(HEADER.finditer(text))
    assert len(headers) == len(expected), "LastTest entry count differs"
    assert len({h[3] for h in headers}) == len(headers), "duplicate LastTest name"
    assert {h[3] for h in headers} == set(expected), "LastTest names differ"
    restored = []
    full_outputs = {}
    for index, header in enumerate(headers):
        ordinal, total, name = header.groups()
        assert int(ordinal) == index + 1 and int(total) == len(expected), "LastTest sequence differs"
        block = text[header.end():headers[index + 1].start() if index + 1 < len(headers) else len(text)]
        assert block.startswith(f"{ordinal}/{total} Test: {name}\n"), "LastTest identity differs"
        command = re.search(r"(?m)^Command: (.*)$", block)
        assert command and shlex.split(command[1]) == expected[name], "LastTest command differs"
        assert block.count(OUTPUT_START) == 1, "ambiguous LastTest output start"
        payload_and_footer = block.split(OUTPUT_START, 1)[1]
        assert payload_and_footer.count(OUTPUT_END) == 1, "ambiguous LastTest output end"
        output, footer = payload_and_footer.split(OUTPUT_END, 1)
        assert "\nTest Passed.\n" in footer and "\nTest Failed.\n" not in footer, "LastTest status not Passed"
        case = by_name[name]
        assert case.get("status", "run") == "run", "JUnit status not run"
        assert not any(case.find(t) is not None for t in ("failure", "error", "skipped")), "JUnit outer failure/skip"
        full_outputs[name] = output
        system_out = case.find("system-out")
        assert system_out is not None, "JUnit system-out missing"
        original = system_out.text or ""
        match = TRUNCATED.search(original)
        if match is None:
            assert "[This part of the test output was removed" not in original, "unknown truncation form"
            assert original == output, "untruncated output differs from LastTest"
            continue
        original_prefix = original[:match.start()]
        assert output.startswith(original_prefix), "truncated prefix differs from LastTest"
        restored.append({
            "name": name, "raw_ordinal": int(ordinal), "same_command": True,
            "same_status": True, "original_prefix_matches": True,
            "original_prefix_utf8_bytes": len(original_prefix.encode("utf-8")),
            "original_capture_threshold_bytes": int(match[1]),
            "original_system_out_sha256": sha(original.encode("utf-8")),
            "restored_system_out_sha256": sha(output.encode("utf-8")),
            "restored_system_out_utf8_bytes": len(output.encode("utf-8")),
        })
    changed = {row["name"] for row in restored}
    before = metadata(root, changed)
    for name in changed:
        by_name[name].find("system-out").text = full_outputs[name]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    ET.ElementTree(root).write(args.output, encoding="utf-8", xml_declaration=True)
    roundtrip = ET.parse(args.output).getroot()
    after = metadata(roundtrip, changed)
    assert before == after, "XML outside restored system-out changed"
    for name in changed:
        assert next(c for c in roundtrip.iter("testcase") if c.get("name") == name).findtext("system-out") == full_outputs[name]
    for key, path in inputs.items():
        assert path.read_bytes() == raw[key], "original input changed"
    receipt = {
        "operation": "restore_truncated_system_out_from_same_run_lasttest",
        "tests_executed_by_this_operation": 0,
        "source_inputs": {key: {"path": str(path.resolve()), "sha256": sha(raw[key]),
                                "bytes": len(raw[key])} for key, path in inputs.items()},
        "script": {"path": str(Path(__file__).resolve()), "sha256": sha(Path(__file__).read_bytes())},
        "manifest_entries": len(expected), "raw_entries": len(headers), "junit_entries": len(cases),
        "restored_entries": restored,
        "unchanged_entries": sorted(set(expected) - changed),
        "original_inputs_unchanged": True,
        "non_output_xml_content_unchanged": True,
        "non_output_xml_content_sha256": sha(before),
        "loss_boundary": "Derived XML is reserialized by ElementTree; only truncated system-out text is replaced. Original XML and complete LastTest remain authoritative inputs. No statuses, counters, thresholds, times, cases, names or commands are changed.",
        "output": {"path": str(args.output.resolve()), "sha256": sha(args.output.read_bytes()),
                   "bytes": args.output.stat().st_size},
    }
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    print(f"Restored {len(changed)} truncated system-out texts from {len(headers)} same-run entries; original inputs and all other XML content unchanged.")


if __name__ == "__main__":
    main()
