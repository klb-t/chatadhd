#!/usr/bin/env python3
"""Link only the offline fixture against an already-built core; never build core.

Example:
  python3 loom/src/chat/tests/run_chat_preset_parity.py \
    --build-dir /tmp/w3-selector-2-baseline-build --output-dir /tmp/w3-chat-parity-before
  python3 loom/src/chat/tests/run_chat_preset_parity.py \
    --build-dir /tmp/w3-selector-2-after-build --output-dir /tmp/w3-chat-parity-after \
    --baseline /tmp/w3-chat-parity-before/outputs.json
  python3 loom/src/chat/tests/run_chat_preset_parity.py \
    --build-dir /tmp/w3-selector-2-after-build --output-dir /tmp/w3-chat-profile-tests \
    --fixture profile

The matrix calls the actual public ChatEngine method. No model/provider request
is made. No expected payload implementation is copied into this runner.
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
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8")


def fixture_compile(build: Path, source: Path, output: Path,
                    macro: str = "LOOM_CHAT_PRESET_PARITY_MAIN", private: bool = False) -> list[str]:
    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    template = next(row for row in commands if Path(row["file"]).name == "chat_engine.cpp")
    argv = template.get("arguments") or shlex.split(template["command"])
    old_file = str(Path(template["file"]).resolve())
    filtered: list[str] = []
    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg == "-o":
            index += 2
            continue
        if arg == "-c" or str((Path(template["directory"]) / arg).resolve()) == old_file:
            index += 1
            continue
        filtered.append(arg)
        index += 1
    if private:
        # Custom-profile operations are source-private, with no public ABI
        # addition. Use the current consumer headers for this fixture only.
        loom_root = source.parents[3]
        filtered[1:1] = ["-I" + str(loom_root / "src"), "-I" + str(loom_root / "include")]
    return filtered + ["-D" + macro + "=1", "-c", str(source), "-o", str(output)]


def fixture_link(build: Path, obj: Path, binary: Path) -> tuple[list[str], Path]:
    # Ninja introspection only: -t commands does not execute/build a target.
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines()
    ninja = next(line.split("=", 1)[1] for line in cache if line.startswith("CMAKE_MAKE_PROGRAM:"))
    result = subprocess.run([ninja, "-C", str(build), "-t", "commands", "loom_candidate_graph_native_tool"],
                            check=True, capture_output=True, text=True)
    lines = [line for line in result.stdout.splitlines()
             if "libloom_core.a" in line and " -o loom_candidate_graph_native_tool " in line]
    if len(lines) != 1:
        raise RuntimeError("expected one configured native-tool link command")
    parts = shlex.split(lines[0])
    if parts[:2] == [":", "&&"]:
        parts = parts[2:]
    if parts[-2:] == ["&&", ":"]:
        parts = parts[:-2]
    if any(part in {"&&", ";", "|"} for part in parts):
        raise RuntimeError("unsupported compound link command")
    linked: list[str] = []
    core: Path | None = None
    index = 0
    while index < len(parts):
        arg = parts[index]
        if arg == "-o":
            linked.extend(["-o", str(binary)])
            index += 2
            continue
        if arg.endswith(".o"):
            index += 1
            continue
        if arg.startswith("-Wl,--dependency-file="):
            linked.append("-Wl,--dependency-file=" + str(binary.with_suffix(".link.d")))
            index += 1
            continue
        if Path(arg).name == "libloom_core.a":
            core = (build / arg).resolve()
            if not core.is_file():
                raise RuntimeError("core library is not built; this runner does not build it")
        linked.append(arg)
        index += 1
    if core is None:
        raise RuntimeError("configured link command has no core library")
    linked.insert(1, str(obj))
    return linked, core


def compare(baseline: dict, current: dict) -> dict:
    keys = ("schema", "model_count", "effort_count", "payload_variant_count", "row_count")
    if any(baseline.get(key) != current.get(key) for key in keys):
        raise RuntimeError("parity matrices have different coverage/schema")
    changes = []
    for previous, actual in zip(baseline["rows"], current["rows"], strict=True):
        for key in ("model", "effort", "payload_variant", "payload_before"):
            if previous[key] != actual[key]:
                raise RuntimeError("parity matrix input/order differs")
        if previous["payload_after"] != actual["payload_after"]:
            changes.append({"model": actual["model"], "effort": actual["effort"],
                            "payload_variant": actual["payload_variant"],
                            "before": previous["payload_after"], "after": actual["payload_after"]})
    return {"schema": "loom.chat_reasoning_preset_parity_comparison/1", "rows": current["row_count"],
            "equal_rows": current["row_count"] - len(changes), "changed_rows": len(changes), "changes": changes}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--fixture", choices=("parity", "profile"), default="parity")
    args = parser.parse_args()
    if args.fixture == "profile" and args.baseline:
        parser.error("--baseline compares parity matrices; custom profile checks use --fixture profile alone")
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    fixture_name = "chat_reasoning_profile_test" if args.fixture == "profile" else "chat_preset_parity"
    source = Path(__file__).with_name(fixture_name + ".cpp").resolve()
    obj = output / (fixture_name + ".o")
    binary = output / fixture_name
    macro = "LOOM_CHAT_REASONING_PROFILE_TEST_MAIN" if args.fixture == "profile" else "LOOM_CHAT_PRESET_PARITY_MAIN"
    compile_argv = fixture_compile(build, source, obj, macro, args.fixture == "profile")
    link_argv, core = fixture_link(build, obj, binary)
    manifest = {"schema": "loom.chat_reasoning_preset_parity_invocation/1", "build_dir": str(build),
                "source": str(source), "source_sha256": sha(source), "core_library": str(core),
                "core_sha256": sha(core), "compile_argv": compile_argv, "link_argv": link_argv,
                "fixture": args.fixture, "core_build_requested": False, "provider_calls": 0}
    write_json(output / "manifest.json", manifest)
    for name, argv in (("compile", compile_argv), ("link", link_argv)):
        with (output / f"{name}.log").open("wb") as log:
            result = subprocess.run(argv, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=False)
        manifest[f"{name}_exit_code"] = result.returncode
        write_json(output / "manifest.json", manifest)
        if result.returncode:
            print(f"{name} failed: {output / (name + '.log')}", file=sys.stderr)
            return 1
    manifest["binary_sha256"] = sha(binary)
    with (output / "outputs.json").open("wb") as stream, (output / "run.log").open("wb") as log:
        result = subprocess.run([str(binary)], cwd=build, stdout=stream, stderr=log, check=False)
    manifest["run_exit_code"] = result.returncode
    manifest["output_sha256"] = sha(output / "outputs.json")
    write_json(output / "manifest.json", manifest)
    if result.returncode and args.fixture == "parity":
        return 1
    data = json.loads((output / "outputs.json").read_text(encoding="utf-8"))
    if args.fixture == "profile":
        if data.get("schema") != "loom.chat_reasoning_profile_verification/1":
            raise RuntimeError("custom profile fixture returned an unexpected schema")
        if data["checks_count"] != len(data["checks"]) or data["checks_count"] != data["passed_checks"] + data["failed_checks"]:
            raise RuntimeError("custom profile fixture returned inconsistent check counts")
        if data["passed_checks"] != sum(row["passed"] is True for row in data["checks"]):
            raise RuntimeError("custom profile fixture passed count differs from actual checks")
        for key in ("checks_count", "passed_checks", "failed_checks"):
            manifest[key] = data[key]
        write_json(output / "manifest.json", manifest)
        print(json.dumps({"checks": data["checks_count"], "passed": data["passed_checks"], "failed": data["failed_checks"],
                          "output_sha256": manifest["output_sha256"], "output_dir": str(output)}, sort_keys=True))
        return 1 if result.returncode or data["failed_checks"] else 0
    if data["row_count"] != len(data["rows"]) or data["row_count"] != (
            data["model_count"] * data["effort_count"] * data["payload_variant_count"]):
        raise RuntimeError("matrix did not execute its reported full coverage")
    manifest["rows"] = data["row_count"]
    if args.baseline:
        baseline = args.baseline.resolve()
        comparison = compare(json.loads(baseline.read_text(encoding="utf-8")), data)
        write_json(output / "comparison.json", comparison)
        manifest["baseline_sha256"] = sha(baseline)
        manifest["changed_rows"] = comparison["changed_rows"]
    write_json(output / "manifest.json", manifest)
    print(json.dumps({"rows": data["row_count"], "changed_rows": manifest.get("changed_rows"),
                      "output_sha256": manifest["output_sha256"], "output_dir": str(output)}, sort_keys=True))
    return 1 if manifest.get("changed_rows", 0) else 0


if __name__ == "__main__":
    raise SystemExit(main())
