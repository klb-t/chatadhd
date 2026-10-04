#!/usr/bin/env python3
"""Run the original semantic regression plus nine graph-path cases, all offline."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from method_graph_probe import native_store_probe

REPO = Path(__file__).resolve().parents[4]
LOOM = REPO / "loom"
NEW_CASES = 9


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=LOOM / "build/dev")
    parser.add_argument("--library", type=Path, help="existing shared library with N3 store export")
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--evidence", type=Path, help="save source-bound JSON evidence")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    library = (args.library or build / "libloom.so.0.1.0").resolve()
    original = LOOM / "src/extract/tests/semantic_prompt_integration.cpp.fixture"
    fixture = LOOM / "src/extract/tests/method_graph_semantic.cpp.fixture"
    usage_header = LOOM / "include/loom/usage_policy.h"
    original_cases = 15 if usage_header.is_file() else 16
    expected_cases = original_cases + NEW_CASES
    source_paths = [Path(__file__).resolve(), fixture, original,
        LOOM / "src/extract/tests/method_graph_probe.py", LOOM / "src/extract/semantic.cpp",
        LOOM / "src/extract/semantic_usage.h", LOOM / "src/extract/prompt_contract.cpp",
        LOOM / "src/extract/prompt_contract.h", LOOM / "src/extract/prompt_contract_data.inc",
        LOOM / "src/extract/prompt_method_graph.cpp", LOOM / "src/extract/prompt_method_graph.h",
        usage_header, *sorted((LOOM / "data/prompts").glob("*")),
        *sorted((LOOM / "src/extract").glob("prompt_method_*.inc"))]
    hashes = {str(path.relative_to(REPO)): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in source_paths if path.is_file()}
    evidence: dict = {"schema": "loom.method_graph_semantic_probe/1", "offline": True,
        "paid_calls": 0, "live_provider_calls": 0, "transport": "ScriptedTransport",
        "source_sha256": hashes, "expected_cases": expected_cases,
        "original_cases": original_cases, "new_cases": NEW_CASES,
        "conditional_case": "missing-W2 compatibility case only compiles when usage_policy.h is absent"}
    evidence["linked_library_sha256"] = {name: hashlib.sha256((build / name).read_bytes()).hexdigest()
        for name in ("libloom_core.a", "libloom_sqlite3_amalgamation.a", "libloom_miniz.a")}
    with tempfile.TemporaryDirectory(prefix="loom-semantic-method-probe-") as tmp:
        scratch = Path(tmp)
        binary = scratch / "method_graph_semantic_probe"
        command = [args.cxx, "-std=c++20", "-O0", "-g0", "-Iloom/include", "-Iloom/src",
            "-Iloom/tests", "-isystem", "loom/third_party/nlohmann", "-isystem", "loom/third_party/doctest",
            "-Iloom/third_party/miniz", "-x", "c++", str(fixture.relative_to(REPO)), "-x", "none",
            str(build / "libloom_core.a"), str(build / "libloom_sqlite3_amalgamation.a"),
            str(build / "libloom_miniz.a"), "-pthread", "-ldl", "-lm", "-lssl", "-lcrypto"]
        if sys.platform.startswith("linux"):
            command.append("-Wl,--no-keep-memory")
        command.extend(["-o", str(binary)])
        compiled = subprocess.run(command, cwd=REPO, text=True, capture_output=True)
        result = subprocess.run([str(binary), "--no-colors=1"], cwd=REPO, text=True,
            capture_output=True) if compiled.returncode == 0 else None
        stdout = result.stdout if result else ""
        cases = re.search(r"test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed\s*\|\s*(\d+) skipped", stdout)
        assertions = re.search(r"assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed", stdout)
        counts = [int(value) for value in cases.groups()] if cases else [0, 0, 0, 0]
        assertion_counts = [int(value) for value in assertions.groups()] if assertions else [0, 0, 0]
        ok = result is not None and result.returncode == 0 and counts == [expected_cases, expected_cases, 0, 0]
        ok &= assertion_counts[0] > 0 and assertion_counts[0] == assertion_counts[1] and assertion_counts[2] == 0
        artifact_lines = [line[len("ACTUAL_METHOD_GRAPH "):] for line in stdout.splitlines()
                          if line.startswith("ACTUAL_METHOD_GRAPH ")]
        artifact = json.loads(artifact_lines[0]) if len(artifact_lines) == 1 else None

        def portable(value: str) -> str:
            return value.replace(str(REPO), "<repo>").replace(str(build), "<build>").replace(tmp, "<scratch>")

        evidence.update({"compile_command": [portable(value) for value in command],
            "compile_exit_code": compiled.returncode,
            "run_exit_code": result.returncode if result else None,
            "executed_cases": counts[0], "passed_cases": counts[1], "failed_cases": counts[2],
            "skipped_cases": counts[3], "assertions": assertion_counts[0],
            "passed_assertions": assertion_counts[1], "failed_assertions": assertion_counts[2],
            "native_semantic_success": bool(ok),
            "stdout": [line for line in stdout.splitlines() if not line.startswith("ACTUAL_METHOD_GRAPH ")],
            "diagnostics": portable(compiled.stderr + (result.stderr if result else ""))})
        if artifact:
            evidence["actual_semantic_artifact"] = artifact
        if ok and artifact:
            try:
                evidence["library_sha256"] = hashlib.sha256(library.read_bytes()).hexdigest()
                evidence["native_store"] = native_store_probe({"graphs": [artifact]}, library, scratch, expected_cases=8)
            except Exception as error:
                evidence["native_store"] = {"success": False, "error": str(error)}
        evidence["success"] = bool(ok and evidence.get("native_store", {}).get("success", False))
    evidence["replay"] = "python3 loom/src/extract/tests/method_graph_semantic_probe.py --build-dir loom/build/dev"
    encoded = json.dumps(evidence, ensure_ascii=False, indent=2) + "\n"
    if args.evidence:
        args.evidence.parent.mkdir(parents=True, exist_ok=True)
        args.evidence.write_text(encoded, encoding="utf-8")
    display = {key: value for key, value in evidence.items()
               if key not in ("actual_semantic_artifact", "native_store")}
    if "native_store" in evidence:
        display["native_store"] = {key: value for key, value in evidence["native_store"].items()
                                   if key != "receipts"}
    print(json.dumps(display, ensure_ascii=False, indent=2))
    return 0 if evidence["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
