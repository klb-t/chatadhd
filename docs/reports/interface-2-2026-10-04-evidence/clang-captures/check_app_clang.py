#!/usr/bin/env python3
"""Compile the real server translation unit with/without the optional W2 header.

Use an existing CMake compile_commands.json and a fresh evidence directory.
No source edits, build-system edits, links or complete compiler matrix.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def save(path: Path, data: str | bytes) -> None:
    if isinstance(data, str):
        data = data.encode("utf-8")
    with path.open("xb") as output:
        output.write(data)


def run(command: list[str], cwd: Path, output: Path, label: str) -> dict:
    serialized = json.dumps(command, ensure_ascii=False, separators=(",", ":"))
    save(output / f"{label}.command.json", serialized + "\n")
    save(output / f"{label}.command.sh", shlex.join(command) + "\n")
    completed = subprocess.run(command, cwd=cwd, capture_output=True, timeout=300, check=False)
    save(output / f"{label}.stdout.txt", completed.stdout)
    save(output / f"{label}.stderr.txt", completed.stderr)
    return {
        "command": command,
        "command_sha256": sha256(serialized.encode()),
        "cwd": str(cwd),
        "exit_code": completed.returncode,
        "stdout_sha256": sha256(completed.stdout),
        "stderr_sha256": sha256(completed.stderr),
        "stdout_bytes": len(completed.stdout),
        "stderr_bytes": len(completed.stderr),
    }


def include_flags(arguments: list[str], cwd: Path, original: Path, replacement: Path) -> list[str]:
    changed = []
    replacements = 0
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument in {"-I", "-isystem", "-iquote"}:
            value = arguments[index + 1]
            absolute = (cwd / value).resolve()
            changed.extend([argument, str(replacement) if absolute == original else value])
            replacements += absolute == original
            index += 2
            continue
        if argument.startswith("-I") and len(argument) > 2:
            absolute = (cwd / argument[2:]).resolve()
            if absolute == original:
                changed.append("-I" + str(replacement))
                replacements += 1
            else:
                changed.append(argument)
        else:
            changed.append(argument)
        index += 1
    if not replacements:
        raise RuntimeError("The selected app.cpp command has no Loom include path to replace.")
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--compile-commands", type=Path, required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--out", type=Path, required=True)
    options = parser.parse_args()
    repo = options.repo.resolve()
    source = repo / "loom/server/src/app.cpp"
    native_include = repo / "loom/include"
    optional_header = native_include / "loom/usage_policy.h"
    if not optional_header.is_file():
        raise RuntimeError("Run after W2 rebase: loom/include/loom/usage_policy.h is absent.")
    output = options.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    commands_bytes = options.compile_commands.read_bytes()
    entries = json.loads(commands_bytes)
    entry = next(item for item in entries if Path(item["file"]).resolve() == source)
    cwd = Path(entry["directory"]).resolve()
    original_command = entry.get("arguments") or shlex.split(entry["command"])
    flags = []
    index = 1
    while index < len(original_command):
        argument = original_command[index]
        if argument == "-o":
            index += 2
            continue
        if argument == "-c" or (cwd / argument).resolve() == source:
            index += 1
            continue
        flags.append(argument)
        index += 1
    clang = shutil.which(options.clang)
    if not clang:
        raise RuntimeError(f"Compiler is not installed: {options.clang}")
    save(output / "compile_commands.json", commands_bytes)
    save(output / "selected_compile_entry.json", json.dumps(entry, indent=2) + "\n")
    source_bytes = source.read_bytes()
    save(output / "app.cpp", source_bytes)
    manifest = []
    for header in sorted(native_include.rglob("*")):
        if header.is_file():
            payload = header.read_bytes()
            manifest.append({"path": str(header.relative_to(native_include)), "bytes": len(payload), "sha256": sha256(payload)})
    save(output / "header_manifest.json", json.dumps(manifest, indent=2) + "\n")
    # A literal copy supplies every normal header; only the optional header is omitted.
    isolated_include = output / "include-without-policy"
    shutil.copytree(native_include, isolated_include)
    (isolated_include / "loom/usage_policy.h").unlink()
    version = run([clang, "--version"], cwd, output, "compiler-version")
    results = []
    for name, arguments, expected_header in [
        ("with-W2-header", flags, True),
        ("without-W2-header", include_flags(flags, cwd, native_include, isolated_include), False),
    ]:
        command = [clang, *arguments, "-Werror", "-Wunused-lambda-capture", "-fsyntax-only", str(source)]
        result = run(command, cwd, output, name)
        # Verify the actual conditional branch, without substituting our own macro.
        macro = run([clang, *arguments, "-Werror", "-Wunused-lambda-capture", "-dM", "-E", str(source)], cwd, output, name + "-macros")
        has_header = "#define LOOM_SERVER_HAS_USAGE_POLICY 1" in (output / f"{name}-macros.stdout.txt").read_text()
        result.update({"name": name, "expected_policy_header": expected_header,
                       "actual_policy_header": has_header, "macro_probe": macro,
                       "passed": result["exit_code"] == 0 and macro["exit_code"] == 0 and has_header == expected_header})
        results.append(result)
    git = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True, check=False)
    summary = {
        "schema": "loom.clang_optional_usage_policy_tu_evidence/1",
        "source": str(source), "source_sha256": sha256(source_bytes),
        "source_unchanged_during_check": source.read_bytes() == source_bytes,
        "script_sha256": sha256(Path(__file__).read_bytes()),
        "compile_commands_sha256": sha256(commands_bytes),
        "git_head": git.stdout.strip(), "compiler": version,
        "runtime_environment": {"LD_LIBRARY_PATH": os.environ.get("LD_LIBRARY_PATH", "")},
        "required_flags": ["-Werror", "-Wunused-lambda-capture", "-fsyntax-only"],
        "input_compile_flags_retained": flags,
        "boundary": "Single app.cpp syntax check; no linker or full compiler matrix result.",
        "results": results,
        "passed": source.read_bytes() == source_bytes and version["exit_code"] == 0 and all(result["passed"] for result in results),
    }
    save(output / "RESULTS.json", json.dumps(summary, indent=2) + "\n")
    for result in results:
        print(f"{result['name']}: exit={result['exit_code']} policy_header={result['actual_policy_header']} passed={result['passed']}")
    print(output / "RESULTS.json")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
