#!/usr/bin/env python3
"""Link the same public-API fixture against an already built actual core.

  python3 loom/src/context/tests/goal_cues_parity.py \\
    --build-dir /tmp/w3-selector-2-baseline-build --output-dir /tmp/w3-cues-before
  python3 loom/src/context/tests/goal_cues_parity.py \\
    --build-dir /tmp/w3-selector-2-after-build --output-dir /tmp/w3-cues-after \\
    --baseline /tmp/w3-cues-before/outputs.json

This compiles only the synthetic fixture, never configures/builds core. Before
and after retain full Goal.to_json and exact canonical result bytes. No expected
classifier implementation is copied, and no provider call is authorized.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys


def sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8")


def fixture_compile(build: Path, source: Path, obj: Path) -> list[str]:
    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    template = next(row for row in commands if Path(row["file"]).name == "context_engine.cpp")
    arguments = list(template["arguments"]) if "arguments" in template else shlex.split(template["command"])
    old_source = Path(template["file"]).resolve()
    kept: list[str] = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument == "-o":
            index += 2
        elif argument == "-c" or (Path(template["directory"]) / argument).resolve() == old_source:
            index += 1
        else:
            kept.append(argument)
            index += 1
    return kept + ["-DLOOM_GOAL_CUES_PARITY_MAIN=1", "-c", str(source), "-o", str(obj)]


def fixture_link(build: Path, obj: Path, binary: Path) -> tuple[list[str], Path]:
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
    ninja = next(line.split("=", 1)[1] for line in cache if line.startswith("CMAKE_MAKE_PROGRAM:"))
    # Introspection executes no build target. Preserve the actual configured
    # SQLite/OpenSSL/platform dependencies instead of inventing a link recipe.
    inspected = subprocess.run([ninja, "-C", str(build), "-t", "commands", "loom_candidate_graph_native_tool"],
                               check=True, capture_output=True, text=True)
    commands = [line for line in inspected.stdout.splitlines()
                if "libloom_core.a" in line and " -o loom_candidate_graph_native_tool " in line]
    if len(commands) != 1:
        raise RuntimeError("expected one configured native-tool link command")
    arguments = shlex.split(commands[0])
    if arguments[:2] == [":", "&&"]:
        arguments = arguments[2:]
    if arguments[-2:] == ["&&", ":"]:
        arguments = arguments[:-2]
    if any(argument in {"&&", ";", "|"} for argument in arguments):
        raise RuntimeError("unsupported compound native link command")
    linked: list[str] = []
    core: Path | None = None
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument == "-o":
            linked.extend(["-o", str(binary)])
            index += 2
            continue
        if argument.endswith(".o"):
            index += 1
            continue
        if argument.startswith("-Wl,--dependency-file="):
            linked.append("-Wl,--dependency-file=" + str(binary.with_suffix(".link.d")))
        else:
            if Path(argument).name == "libloom_core.a":
                core = (build / argument).resolve()
                if not core.is_file():
                    raise RuntimeError("core library is not built; this runner never builds it")
            linked.append(argument)
        index += 1
    if core is None:
        raise RuntimeError("configured link command contains no core library")
    linked.insert(1, str(obj))
    return linked, core


def compare(before: dict, after: dict, exact_bytes_equal: bool) -> dict:
    for key in ("schema", "native_pack_hashes", "builtin_goal_type_count", "row_count", "goal_count", "error_count", "provider_calls"):
        if before.get(key) != after.get(key):
            raise RuntimeError("parity inputs/coverage differ at " + key)
    changed = []
    for previous, current in zip(before["rows"], after["rows"], strict=True):
        for key in ("pack_variation", "case", "request", "forced_type"):
            if previous[key] != current[key]:
                raise RuntimeError("parity input/order differs at " + key)
        if previous["result_canonical"] != current["result_canonical"]:
            changed.append({"pack_variation": current["pack_variation"], "case": current["case"],
                            "before": previous["result"], "after": current["result"]})
    return {"schema": "loom.goal_cues_parity_comparison/1", "rows": after["row_count"],
            "equal_rows": after["row_count"] - len(changed), "changed_rows": len(changed),
            "exact_output_bytes_equal": exact_bytes_equal, "changes": changed}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    source = Path(__file__).with_suffix(".cpp").resolve()
    obj, binary = output / "goal_cues_parity.o", output / "goal_cues_parity"
    compile_command = fixture_compile(build, source, obj)
    link_command, core = fixture_link(build, obj, binary)
    manifest = {"schema": "loom.goal_cues_parity_invocation/1", "source": str(source),
                "source_sha256": sha(source), "runner_sha256": sha(Path(__file__).resolve()),
                "build_dir": str(build), "core_library": str(core), "core_sha256": sha(core),
                "compile_argv": compile_command, "link_argv": link_command,
                "core_build_requested": False, "provider_calls": None}
    write_json(output / "manifest.json", manifest)
    for name, command in (("compile", compile_command), ("link", link_command)):
        with (output / f"{name}.log").open("wb") as log:
            result = subprocess.run(command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=False)
        manifest[name + "_exit_code"] = result.returncode
        write_json(output / "manifest.json", manifest)
        if result.returncode:
            print(f"{name} failed: {output / (name + '.log')}", file=sys.stderr)
            return 1
    manifest["binary_sha256"] = sha(binary)
    with (output / "outputs.json").open("wb") as stream, (output / "run.log").open("wb") as log:
        result = subprocess.run([str(binary)], cwd=build, stdout=stream, stderr=log, check=False)
    manifest["run_exit_code"] = result.returncode
    manifest["output_sha256"] = sha(output / "outputs.json")
    manifest["core_sha256_after"] = sha(core)
    manifest["source_sha256_after"] = sha(source)
    write_json(output / "manifest.json", manifest)
    if result.returncode:
        return 1
    if manifest["core_sha256_after"] != manifest["core_sha256"] or manifest["source_sha256_after"] != manifest["source_sha256"]:
        raise RuntimeError("core or fixture changed during parity execution")
    data = json.loads((output / "outputs.json").read_text(encoding="utf-8"))
    if data["row_count"] != len(data["rows"]) or data["row_count"] != data["goal_count"] + data["error_count"]:
        raise RuntimeError("fixture did not execute its reported full coverage")
    if data["row_count"] != 18 + data["builtin_goal_type_count"]:
        raise RuntimeError("fixture matrix is incomplete")
    if data["provider_calls"] != 0:
        raise RuntimeError("fixture observed provider calls")
    manifest["rows"] = data["row_count"]
    manifest["provider_calls"] = data["provider_calls"]
    if args.baseline:
        baseline = args.baseline.resolve()
        exact = baseline.read_bytes() == (output / "outputs.json").read_bytes()
        comparison = compare(json.loads(baseline.read_text(encoding="utf-8")), data, exact)
        write_json(output / "comparison.json", comparison)
        manifest["baseline_sha256"] = sha(baseline)
        manifest["changed_rows"] = comparison["changed_rows"]
        manifest["exact_output_bytes_equal"] = exact
    write_json(output / "manifest.json", manifest)
    print(json.dumps({"rows": data["row_count"], "changed_rows": manifest.get("changed_rows"),
                      "exact_output_bytes_equal": manifest.get("exact_output_bytes_equal"),
                      "output_dir": str(output)}, sort_keys=True))
    return 1 if manifest.get("changed_rows", 0) or manifest.get("exact_output_bytes_equal") is False else 0


if __name__ == "__main__":
    raise SystemExit(main())
