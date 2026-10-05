#!/usr/bin/env python3
"""Restore complete system-out streams from the same CTest LastTest.log.

This offline evidence transformation never executes tests or manifest commands.
It fails closed unless all raw records match the original JUnit and CTest
manifest by index, name, command, working directory, passing status and time.
Every original stream must either match exactly or be a proven CTest prefix
truncation. Only system-out contents change; all other XML bytes are retained.
"""

from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import shlex
import sys
import tempfile
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape


SEPARATOR = b"-" * 58
HEADER = re.compile(rb"^(\d+)/(\d+) Testing: ([^\r\n]+)\n", re.MULTILINE)
SYSTEM_OUT = re.compile(rb"<system-out>([\s\S]*?)</system-out>")
TRUNCATED = re.compile(
    rb"([\s\S]*)\.\.\.\n\[This part of the test output was removed since it exceeds "
    rb"the threshold of (\d+) bytes\.\]\n"
)
MONTHS = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_record(path: Path, data: bytes) -> dict:
    return {"file": path.name, "bytes": len(data), "sha256": digest(data)}


def parse_xml(data: bytes) -> ET.Element:
    require(b"<!DOCTYPE" not in data and b"<!ENTITY" not in data, "DTD/entity declarations are not accepted")
    root = ET.fromstring(data)
    require(root.tag == "testsuite", "expected one direct testsuite")
    return root


def xml_metadata(node: ET.Element) -> dict:
    """Canonical metadata excluding only the output contents being restored."""
    return {
        "tag": node.tag,
        "attributes": dict(node.attrib),
        "text": None if node.tag == "system-out" else node.text,
        "tail": node.tail,
        "children": [xml_metadata(child) for child in node],
    }


def xml_text(body: bytes, name: str) -> str:
    value = body.decode("utf-8", errors="strict")
    require(
        all(c in "\t\n\r" or 0x20 <= ord(c) <= 0xD7FF or 0xE000 <= ord(c) <= 0xFFFD or 0x10000 <= ord(c) <= 0x10FFFF for c in value),
        f"{name}: output contains an invalid XML 1.0 character",
    )
    return value


def timestamp_minute(timestamp: str) -> str:
    parsed = datetime.fromisoformat(timestamp)
    return f"{MONTHS[parsed.month - 1]} {parsed.day:02d} {parsed.hour:02d}:{parsed.minute:02d}"


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    temporary.replace(path)


def restore(args: argparse.Namespace) -> dict:
    paths = {name: Path(getattr(args, name)).resolve() for name in ("raw_log", "junit", "manifest", "output", "receipt")}
    input_paths = {paths[name] for name in ("raw_log", "junit", "manifest")}
    for name in ("original_coverage", "verifier", "guard_provenance"):
        if getattr(args, name):
            input_paths.add(Path(getattr(args, name)).resolve())
    require(paths["output"] != paths["receipt"], "output and receipt paths must differ")
    require(paths["output"] not in input_paths and paths["receipt"] not in input_paths, "refusing to overwrite an input")
    raw = paths["raw_log"].read_bytes()
    original = paths["junit"].read_bytes()
    manifest_bytes = paths["manifest"].read_bytes()
    require(b"\r" not in raw and b"\x00" not in raw, "raw log must use LF and contain no NUL; no newline normalization is performed")
    root = parse_xml(original)
    tests = root.findall("testcase")
    require(len(list(root)) == len(tests), "unexpected testsuite child")
    manifest = json.loads(manifest_bytes)
    require(manifest.get("kind") == "ctestInfo", "manifest is not a CTest show-only JSON manifest")
    manifest_tests = manifest["tests"]
    total = len(tests)
    require(total > 0 and len(manifest_tests) == total, "manifest/JUnit count mismatch")
    require(root.get("tests") == str(total), "JUnit tests attribute mismatch")
    for name in ("failures", "disabled", "skipped", "errors"):
        require(root.get(name, "0") == "0", f"JUnit reports {name}")
    names = [test.attrib["name"] for test in tests]
    require(len(set(names)) == total, "duplicate JUnit names")
    require([test["name"] for test in manifest_tests] == names, "JUnit/manifest order or names differ")
    spans = list(HEADER.finditer(raw))
    require(len(spans) == total, "raw Testing-header count mismatch")
    prefix = raw[:spans[0].start()]
    start_match = re.fullmatch(rb"Start testing: ([^\n]+)\n" + SEPARATOR + rb"\n", prefix)
    require(start_match is not None, "unexpected raw-log preamble")
    raw_start = start_match[1].decode("utf-8")
    require(raw_start.rsplit(" ", 1)[0] == timestamp_minute(root.attrib["timestamp"]), "JUnit timestamp and raw start minute disagree")
    end_match = re.search(rb"\nEnd testing: ([^\n]+)\n\Z", raw)
    require(end_match is not None, "unexpected raw-log ending")
    raw_end = end_match[1].decode("utf-8")
    content_end = end_match.start() + 1
    original_outputs = list(SYSTEM_OUT.finditer(original))
    require(len(original_outputs) == total, "expected one explicit system-out element per JUnit test")
    rows = []
    bodies = []
    modes: Counter[str] = Counter()
    for offset, (header, test, manifest_test) in enumerate(zip(spans, tests, manifest_tests), start=1):
        index, declared_total = int(header[1]), int(header[2])
        name = header[3].decode("utf-8")
        require(index == offset and declared_total == total, f"{name}: raw index/total mismatch")
        require(name == names[offset - 1], f"raw/JUnit name mismatch at index {offset}")
        require(test.get("status") == "run", f"{name}: original JUnit is not an executed test")
        require(not any(test.find(tag) is not None for tag in ("failure", "error", "skipped")), f"{name}: original JUnit reports failure/error/skip")
        require(len(test.findall("system-out")) == 1 and not test.findall("system-err"), f"{name}: unexpected output element layout")
        block_start = header.start()
        block_end = spans[offset].start() if offset < total else content_end
        block = raw[block_start:block_end]
        escaped_name = re.escape(header[3])
        escaped_index = str(offset).encode() + b"/" + str(total).encode()
        pattern = (
            re.escape(escaped_index) + rb" Testing: " + escaped_name + rb"\n"
            + re.escape(escaped_index) + rb" Test: " + escaped_name + rb"\n"
            + rb"Command: (?P<command>[^\n]+)\nDirectory: (?P<directory>[^\n]+)\n"
            + rb'"' + escaped_name + rb'" start time: (?P<start>[^\n]+)\n'
            + rb"Output:\n" + SEPARATOR + rb"\n(?P<body>[\s\S]*)<end of output>\n"
            + rb"Test time =\s+(?P<time>\d+\.\d+) sec\n" + SEPARATOR + rb"\n"
            + rb"Test Passed\.\n"
            + rb'"' + escaped_name + rb'" end time: (?P<end>[^\n]+)\n'
            + rb'"' + escaped_name + rb'" time elapsed: (?P<elapsed>\d+:\d+:\d+)\n'
            + SEPARATOR + rb"\n\n"
        )
        match = re.fullmatch(pattern, block)
        require(match is not None, f"{name}: malformed or non-passing raw record")
        require(len(re.findall(rb"^<end of output>\n", block, re.MULTILINE)) == 1, f"{name}: ambiguous output delimiter")
        command_text = match["command"].decode("utf-8")
        command = shlex.split(command_text, posix=True)
        require(command == manifest_test.get("command"), f"{name}: raw/manifest command mismatch")
        properties = [prop["value"] for prop in manifest_test.get("properties", []) if prop["name"] == "WORKING_DIRECTORY"]
        require(len(properties) == 1, f"{name}: manifest must have exactly one working directory")
        directory = match["directory"].decode("utf-8")
        require(directory == properties[0], f"{name}: raw/manifest working directory mismatch")
        junit_time = test.attrib["time"]
        time_delta = abs(Decimal(junit_time) - Decimal(match["time"].decode("ascii")))
        require(time_delta <= Decimal("0.005001"), f"{name}: raw/JUnit durations differ beyond raw two-decimal rounding")
        body = match["body"]
        text = xml_text(body, name)
        old_body = (test.find("system-out").text or "").encode("utf-8")
        threshold = None
        if old_body == body:
            mode = "exact-original-stream"
        else:
            truncation = TRUNCATED.fullmatch(old_body)
            require(truncation is not None, f"{name}: differing stream is not a recognized CTest truncation")
            retained_prefix, threshold_bytes = truncation.groups()
            threshold = int(threshold_bytes)
            require(len(retained_prefix) == threshold, f"{name}: recorded truncation prefix length disagrees with threshold")
            require(len(body) > threshold and body.startswith(retained_prefix), f"{name}: raw output does not extend the exact truncated prefix")
            mode = "verified-ctest-prefix-truncation"
        modes[mode] += 1
        body_start = block_start + match.start("body")
        body_end = block_start + match.end("body")
        require(raw[body_start:body_end] == body, f"{name}: byte offset extraction mismatch")
        rows.append({
            "index": offset,
            "total": total,
            "name": name,
            "manifest_index_zero_based": offset - 1,
            "junit_index_zero_based": offset - 1,
            "status": {"raw": "Test Passed.", "junit": dict(test.attrib)},
            "command": command,
            "raw_command_text": command_text,
            "working_directory": directory,
            "raw_start": match["start"].decode("utf-8"),
            "raw_end": match["end"].decode("utf-8"),
            "raw_elapsed": match["elapsed"].decode("ascii"),
            "duration": {"original_junit_seconds": junit_time, "raw_rounded_seconds": match["time"].decode("ascii"), "absolute_difference_seconds": str(time_delta)},
            "raw_record": {"byte_start": block_start, "byte_end_exclusive": block_end, "bytes": len(block), "sha256": digest(block)},
            "raw_output": {"byte_start": body_start, "byte_end_exclusive": body_end, "bytes": len(body), "sha256": digest(body)},
            "original_junit_output": {"decoded_utf8_bytes": len(old_body), "decoded_utf8_sha256": digest(old_body)},
            "comparison": mode,
            "truncation_threshold_bytes": threshold,
            "derived_output_matches_raw_bytes": True,
        })
        bodies.append(escape(text).encode("utf-8"))
    pieces = []
    previous = 0
    for span, body in zip(original_outputs, bodies):
        pieces.extend((original[previous:span.start(1)], body))
        previous = span.end(1)
    pieces.append(original[previous:])
    derived = b"".join(pieces)
    derived_root = parse_xml(derived)
    require(xml_metadata(derived_root) == xml_metadata(root), "derived JUnit changed metadata outside system-out contents")
    require(SYSTEM_OUT.sub(b"<system-out></system-out>", derived) == SYSTEM_OUT.sub(b"<system-out></system-out>", original), "non-output XML bytes changed")
    for test, row in zip(derived_root.findall("testcase"), rows):
        restored = (test.find("system-out").text or "").encode("utf-8")
        start, end = row["raw_output"]["byte_start"], row["raw_output"]["byte_end_exclusive"]
        require(restored == raw[start:end], f"{row['name']}: decoded derived output is not byte-identical to raw output")
    inputs = {
        "raw_log": source_record(paths["raw_log"], raw),
        "original_junit": source_record(paths["junit"], original),
        "manifest": source_record(paths["manifest"], manifest_bytes),
    }
    if args.original_coverage:
        path = Path(args.original_coverage)
        data = path.read_bytes()
        coverage = json.loads(data)
        require(coverage.get("valid") is False, "original negative coverage must remain negative")
        inputs["original_negative_coverage"] = source_record(path, data)
    if args.verifier:
        path = Path(args.verifier)
        data = path.read_bytes()
        inputs["unchanged_coverage_verifier"] = source_record(path, data)
        if args.guard_provenance:
            provenance_path = Path(args.guard_provenance)
            provenance_bytes = provenance_path.read_bytes()
            provenance = json.loads(provenance_bytes)
            require(digest(data) == provenance["sha256"], "coverage verifier differs from recorded upstream snapshot")
            inputs["guard_provenance"] = source_record(provenance_path, provenance_bytes)
    receipt = {
        "schema": "loom.ctest_output_restoration/1",
        "valid": True,
        "script": source_record(Path(__file__), Path(__file__).read_bytes()),
        "operation": "offline XML system-out enrichment from the same original CTest run; no test or command execution",
        "inputs": inputs,
        "raw_run": {"start_as_recorded": raw_start, "end_as_recorded": raw_end, "junit_timestamp_preserved": root.attrib["timestamp"], "timezone_conversion": "none; raw CST text and original JUnit timestamp retained"},
        "summary": {"test_records": total, "unique_names": total, "passing_raw_records": total, "executed_junit_records": total, "exact_original_streams": modes["exact-original-stream"], "verified_prefix_truncations": modes["verified-ctest-prefix-truncation"], "streams_with_newline_differences": 0},
        "proof": {
            "index_name_order_total_match": True,
            "manifest_commands_and_working_directories_match": True,
            "raw_passed_status_and_original_junit_run_status_match": True,
            "original_junit_metadata_and_all_non_output_xml_bytes_preserved": True,
            "all_decoded_derived_streams_equal_raw_byte_slices": True,
            "untruncated_original_streams_equal_raw_byte_slices": True,
            "truncated_original_streams_equal_raw_prefixes_at_recorded_byte_threshold": True,
            "newline_or_encoding_normalization": "none; raw LF UTF-8 body bytes preserved exactly after XML decoding",
            "raw_duration_rounding_tolerance_seconds": "0.005001; raw log prints two decimals, original higher-precision JUnit duration is preserved",
            "byte_offset_convention": "zero-based offsets in the preserved raw log; end exclusive",
        },
        "output": source_record(paths["output"], derived),
        "records": rows,
    }
    atomic_write(paths["output"], derived)
    atomic_write(paths["receipt"], (json.dumps(receipt, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    return receipt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-log", required=True)
    parser.add_argument("--junit", required=True, help="original positive, possibly truncated JUnit")
    parser.add_argument("--manifest", required=True, help="original CTest show-only JSON manifest")
    parser.add_argument("--output", required=True, help="new derived JUnit; original inputs are never overwritten")
    parser.add_argument("--receipt", required=True)
    parser.add_argument("--original-coverage", help="preserved original negative verifier JSON")
    parser.add_argument("--verifier", help="unchanged coverage-verifier snapshot")
    parser.add_argument("--guard-provenance", help="recorded verifier snapshot provenance")
    args = parser.parse_args()
    try:
        receipt = restore(args)
    except (ValueError, KeyError, OSError, ET.ParseError, json.JSONDecodeError) as error:
        print(f"restoration refused: {error}", file=sys.stderr)
        return 1
    print(json.dumps(receipt["summary"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
