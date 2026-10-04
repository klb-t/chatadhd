#!/usr/bin/env python3
"""Replay the offline native prompt registry probe against a built core."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[4]
LOOM = REPO / "loom"
EXPECTED_CASES = 42


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=LOOM / "build" / "dev")
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--evidence", type=Path, help="save the JSON evidence at this path")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    fixture = LOOM / "src/extract/tests/prompt_contract_probe.cpp.fixture"
    hashes = {}
    for path in [fixture, LOOM / "src/extract/prompt_contract.cpp", LOOM / "src/extract/prompt_contract.h",
                 LOOM / "src/extract/prompt_contract_data.inc", *sorted((LOOM / "data/prompts").glob("*.prompt"))]:
        hashes[str(path.relative_to(REPO))] = hashlib.sha256(path.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix="loom-prompt-probe-") as tmp:
        directory = Path(tmp)
        executable = directory / "prompt_contract_probe"
        command = [args.cxx, "-x", "c++", "-std=c++20", "-g0", "-Wall", "-Wextra", "-Werror",
                   "-Iloom/include", "-Iloom/src", "-isystem", "loom/third_party/nlohmann",
                   str(fixture.relative_to(REPO)), "-x", "none",
                   str(build / "libloom_core.a"), str(build / "libloom_sqlite3_amalgamation.a"),
                   str(build / "libloom_miniz.a"), "-lssl", "-lcrypto", "-lpthread"]
        if sys.platform.startswith("linux"):
            command.append("-Wl,--no-keep-memory")
        command.extend(["-o", str(executable)])
        compiled = subprocess.run(command, cwd=REPO, text=True, capture_output=True)
        result = subprocess.run([str(executable), str(directory / "overlay")], cwd=REPO,
                                text=True, capture_output=True) if compiled.returncode == 0 else None
        output = result.stdout if result else ""
        passed = sum(line.startswith("PASS ") for line in output.splitlines())
        summary = re.search(r"^checks (\d+)/(\d+)$", output, re.MULTILINE)
        ok = result is not None and result.returncode == 0 and summary is not None and (
            passed == int(summary[1]) == int(summary[2]) == EXPECTED_CASES)

        def portable(text: str) -> str:
            return text.replace(str(REPO), "<repo>").replace(str(build), "<build>").replace(tmp, "<scratch>")

        evidence = {
            "schema": "loom.prompt_contract_probe/1", "offline": True,
            "expected_cases": EXPECTED_CASES, "executed_cases": int(summary[2]) if summary else passed,
            "passed_cases": passed, "success": ok,
            "compile_exit_code": compiled.returncode, "run_exit_code": result.returncode if result else None,
            "compile_command": [portable(x) for x in command],
            "source_sha256": hashes, "stdout": output.splitlines(),
            "diagnostics": portable(compiled.stderr + (result.stderr if result else "")),
            "replay": "python3 loom/src/extract/tests/run_prompt_contract_probe.py --build-dir loom/build/dev",
        }
    encoded = json.dumps(evidence, ensure_ascii=False, indent=2) + "\n"
    if args.evidence:
        args.evidence.parent.mkdir(parents=True, exist_ok=True)
        args.evidence.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
