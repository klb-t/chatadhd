#!/usr/bin/env python3
"""Recover capped CTest JUnit stdout from the matching complete LastTest.log.

This does not run tests, change a result, invent a case count or certify coverage.
It preserves the original XML bytes outside system-out text. Both original
inputs remain necessary evidence; run the independent coverage guard afterward.
Unsupported, ambiguous or inconsistent input is rejected before any output write.
"""
from __future__ import annotations

import argparse
from datetime import datetime
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


SEPARATOR = "----------------------------------------------------------\n"
HEADER = re.compile(r"^(\d+)/(\d+) Testing: ([^\r\n]+)\n\1/\2 Test: \3\n", re.MULTILINE)
CAP = re.compile(
    r"\A(.*)\.\.\.\n\[This part of the test output was removed since it exceeds "
    r"the threshold of (\d+) bytes\.\]\n\Z", re.DOTALL)
XML_OUT = re.compile(rb"<system-out>(.*?)</system-out>", re.DOTALL)
MONTHS = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def xml_legal(text: str) -> bool:
    return all(ord(c) in (9, 10, 13) or 0x20 <= ord(c) <= 0xD7FF or
               0xE000 <= ord(c) <= 0xFFFD or 0x10000 <= ord(c) <= 0x10FFFF for c in text)


def date_fields(text: str) -> tuple[str, int, str, str]:
    match = re.fullmatch(r"([A-Z][a-z]{2}) +(\d{1,2}) (\d{2}:\d{2}) (\S+)", text)
    require(match is not None, f"unsupported LastTest timestamp: {text!r}")
    return match[1], int(match[2]), match[3], match[4]


def recover(junit_raw: bytes, log_raw: bytes) -> tuple[bytes, dict]:
    # Strict UTF-8 and XML 1.0 are intentional: no replacement decoding,
    # normalization or stripping of unsupported control characters.
    original_text = junit_raw.decode("utf-8")
    log = log_raw.decode("utf-8")
    require(xml_legal(original_text) and xml_legal(log), "XML-illegal source characters")
    root = ET.fromstring(junit_raw)
    require(root.tag == "testsuite", "only CTest's single testsuite format is supported")
    cases = root.findall("testcase")
    require(len(cases) > 0 and len(list(root.iter("testcase"))) == len(cases),
            "empty or nested testcase collection")
    names = [case.get("name", "") for case in cases]
    require(all(names) and len(set(names)) == len(names), "missing or duplicate JUnit names")
    require(int(root.get("tests", "-1")) == len(cases), "JUnit suite test count mismatch")
    for field in ("disabled", "skipped", "errors"):
        require(int(root.get(field, "0")) == 0, f"unsupported JUnit {field} count")

    headers = list(HEADER.finditer(log))
    require(len(headers) == len(cases), "LastTest entry count mismatch or ambiguous headers")
    require([h[3] for h in headers] == names, "LastTest/JUnit names or order differ")
    require([int(h[1]) for h in headers] == list(range(1, len(cases) + 1)) and
            all(int(h[2]) == len(cases) for h in headers), "LastTest ordinal/count mismatch")
    begin = log[:headers[0].start()]
    require(begin.startswith("Start testing: ") and begin.endswith("\n" + SEPARATOR),
            "missing or ambiguous LastTest run header")
    start = begin[len("Start testing: "):-(len(SEPARATOR) + 1)]
    month, day, minute, zone = date_fields(start)
    stamp = datetime.fromisoformat(root.get("timestamp", ""))
    require((MONTHS[stamp.month - 1], stamp.day, stamp.strftime("%H:%M")) ==
            (month, day, minute), "LastTest/JUnit run start mismatch")

    spans = list(XML_OUT.finditer(junit_raw))
    require(len(spans) == len(cases), "missing or ambiguous raw system-out fields")
    outputs = []
    records = []
    failed = 0
    run_end = None
    for index, (case, header, span) in enumerate(zip(cases, headers, spans)):
        name = names[index]
        require(case.get("status") == "run" and not case.findall("skipped") and
                not case.findall("error"), f"{name}: unsupported execution state")
        failures = case.findall("failure")
        require(len(failures) <= 1, f"{name}: ambiguous failure state")
        expected_status = "Failed" if failures else "Passed"
        failed += bool(failures)
        fields = case.findall("system-out")
        require(len(fields) == 1 and not list(fields[0]), f"{name}: ambiguous system-out")
        original = fields[0].text or ""
        raw_value = ET.fromstring(b"<value>" + span[1] + b"</value>").text or ""
        require(original == raw_value, f"{name}: raw XML output span mismatch")
        stop = headers[index + 1].start() if index + 1 < len(headers) else len(log)
        block = log[header.end():stop]
        marker = "Output:\n" + SEPARATOR
        require(block.count(marker) == 1 and block.count("<end of output>\n") == 1,
                f"{name}: missing or ambiguous output delimiters")
        before, rest = block.split(marker)
        full, after = rest.split("<end of output>\n")
        opening = re.fullmatch(
            r"Command: [^\n]*\nDirectory: [^\n]*\n\"" + re.escape(name) +
            r"\" start time: ([^\n]+)\n", before)
        require(opening is not None, f"{name}: unsupported entry metadata")
        date_fields(opening[1])
        closing = re.fullmatch(
            r"Test time = +([0-9]+\.[0-9]{2}) sec\n" + re.escape(SEPARATOR) +
            r"Test (Passed|Failed)\.\n\"" + re.escape(name) +
            r"\" end time: ([^\n]+)\n\"" + re.escape(name) +
            r"\" time elapsed: (\d{2}:\d{2}:\d{2})\n" + re.escape(SEPARATOR) +
            r"\n(?:End testing: ([^\n]+)\n)?", after)
        require(closing is not None, f"{name}: unsupported or ambiguous closing metadata")
        require(closing[2] == expected_status, f"{name}: LastTest/JUnit outcome mismatch")
        date_fields(closing[3])
        require(abs(Decimal(case.get("time", "NaN")) - Decimal(closing[1])) <= Decimal("0.005"),
                f"{name}: LastTest/JUnit duration mismatch")
        if index + 1 == len(cases):
            require(closing[5] is not None, "missing LastTest run end")
            run_end = closing[5]
            require(date_fields(run_end)[3] == zone, "LastTest run timezone labels differ")
        else:
            require(closing[5] is None, f"{name}: premature LastTest run end")

        capped = False
        if original != full:
            cap = CAP.fullmatch(original)
            require(cap is not None and full.startswith(cap[1]),
                    f"{name}: original output is neither identical nor a matching capped prefix")
            require(len(full.encode("utf-8")) > int(cap[2]) and
                    len(cap[1].encode("utf-8")) <= int(cap[2]),
                    f"{name}: inconsistent capped-output threshold")
            capped = True
        require("[This part of the test output was removed since it exceeds the threshold" not in full,
                f"{name}: LastTest output is itself capped")
        outputs.append(full)
        records.append({"name": name, "status": expected_status.lower(), "replaced": capped,
                        "original_stdout_sha256": sha(original.encode("utf-8")),
                        "full_stdout_sha256": sha(full.encode("utf-8")),
                        "full_stdout_bytes": len(full.encode("utf-8"))})
    require(int(root.get("failures", "-1")) == failed, "JUnit failure count mismatch")

    chunks = []
    previous = 0
    for span, full, record in zip(spans, outputs, records):
        chunks.append(junit_raw[previous:span.start(1)])
        if record["replaced"]:
            # XML parsers normalize literal CR; a character reference preserves it.
            escaped = full.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\r", "&#13;")
            chunks.append(escaped.encode("utf-8"))
        else:
            chunks.append(junit_raw[span.start(1):span.end(1)])
        previous = span.end(1)
    chunks.append(junit_raw[previous:])
    restored = b"".join(chunks)
    check = ET.fromstring(restored)
    require([case.findtext("system-out", "") for case in check.findall("testcase")] == outputs,
            "restored stdout did not round-trip exactly")
    receipt = {"schema": "loom.ctest_stdout_restoration/1",
               "source_identity": "names_order_status_duration_timestamp_and_stdout_prefix_match",
               "original_junit_sha256": sha(junit_raw), "last_test_log_sha256": sha(log_raw),
               "restored_junit_sha256": sha(restored), "run_start": start, "run_end": run_end,
               "entries": len(cases), "replaced_entries": sum(row["replaced"] for row in records),
               "unchanged_entries": sum(not row["replaced"] for row in records),
               "status_metadata_preserved": True, "coverage_verified": False, "records": records}
    return restored, receipt


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--junit", type=Path, required=True)
    parser.add_argument("--last-test-log", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        paths = [args.junit, args.last_test_log, args.output, args.receipt]
        for index, path in enumerate(paths):
            for other in paths[:index]:
                require(path.resolve() != other.resolve() and not
                        (path.exists() and other.exists() and path.samefile(other)),
                        "input, output and receipt paths must be distinct")
        restored, receipt = recover(args.junit.read_bytes(), args.last_test_log.read_bytes())
        receipt["inputs"] = {"junit": str(args.junit), "last_test_log": str(args.last_test_log)}
        receipt["output"] = str(args.output)
        args.output.write_bytes(restored)
        args.receipt.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"restored {receipt['replaced_entries']}/{receipt['entries']} stdout fields; results preserved")
    except (OSError, UnicodeError, ValueError, ET.ParseError, InvalidOperation) as error:
        print(f"CTest stdout restoration rejected: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
