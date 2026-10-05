#!/usr/bin/env python3
"""Replay the retained source snapshots offline, without their original git refs.

The capture must pass all recorded input hashes before compilation. Native
outputs must be byte-identical to the original actual outputs. This does not
rerun repository tests or execute commands supplied by a captured manifest.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


NATIVE_SOURCES = (
    "loom/src/model/runtime_profile.cpp", "loom/src/util/fs.cpp",
    "loom/src/util/log.cpp", "loom/src/util/random_ids_time.cpp",
    "loom/src/util/json.cpp", "loom/src/util/sha256.cpp",
    "loom/src/util/utf8.cpp", "loom/src/util/unicode.cpp", "loom/src/util/result.cpp",
    "loom/src/onboarding/layers.cpp", "loom/src/onboarding/runtime_adapter.cpp",
)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def safe_file(root, relative):
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("unsafe captured path")
    resolved = (root / path).resolve()
    if not resolved.is_relative_to(root.resolve()):
        raise ValueError("captured path escapes the archive")
    return resolved


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captured", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="c++")
    args = parser.parse_args()
    captured, output = args.captured.resolve(), args.output.resolve()
    if output.exists():
        parser.error("output already exists; preserve it and choose a new destination")
    receipt = json.loads((captured / "receipt.json").read_bytes())
    if receipt.get("schema") != "loom.runtime_layers_mixed_capture/1" or receipt.get("valid") is not True:
        parser.error("capture is not a complete positive source-paired proof")
    fixture = captured / "fixture.json"
    if digest(fixture.read_bytes()) != receipt["fixture_sha256"]:
        parser.error("fixture hash mismatch")
    for variant in receipt["variants"]:
        snapshot = captured / (variant["variant"] + "-inputs")
        for entry in variant["inputs"]:
            raw = safe_file(snapshot, entry["path"]).read_bytes()
            if len(raw) != entry["bytes"] or digest(raw) != entry["sha256"]:
                parser.error("captured input mismatch: " + entry["path"])
    for entry in receipt["verified_source_provenance"]:
        raw = safe_file(captured / "after-provenance-inputs", entry["path"]).read_bytes()
        if digest(raw) != entry["raw_sha256"]:
            parser.error("source provenance snapshot mismatch")
    output.mkdir(parents=True)
    result = {"schema": "loom.runtime_layers_mixed_replay/1", "valid": False,
        "original_receipt_sha256": digest((captured / "receipt.json").read_bytes()),
        "compiler_version": subprocess.check_output([args.compiler, "--version"]).decode(),
        "provider_calls": 0, "database_created": False, "variants": []}
    try:
        with tempfile.TemporaryDirectory(prefix="w11-runtime-layers-replay-") as temporary:
            for variant in receipt["variants"]:
                name = variant["variant"]
                if name not in ("before", "after"):
                    raise ValueError("unknown captured variant")
                snapshot = captured / (name + "-inputs")
                binary = Path(temporary) / name
                argv = [args.compiler, "-std=c++20", "-O0", "-g0", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(snapshot / "loom/include"), "-isystem", str(snapshot / "loom/third_party/nlohmann"),
                    str(snapshot / "runner.cpp"), *[str(snapshot / source) for source in NATIVE_SOURCES],
                    "-pthread", "-o", str(binary)]
                log = output / (name + ".compile.log")
                with log.open("wb") as stream:
                    compiled = subprocess.run(argv, stdout=stream, stderr=subprocess.STDOUT)
                row = {"variant": name, "compile_argv": argv, "compile_exit": compiled.returncode,
                       "compile_log_sha256": digest(log.read_bytes())}
                result["variants"].append(row)
                if compiled.returncode:
                    raise RuntimeError(name + " compilation failed; full log retained")
                stdout, stderr = output / (name + ".json"), output / (name + ".stderr.log")
                with stdout.open("wb") as out, stderr.open("wb") as err:
                    executed = subprocess.run([str(binary), str(fixture)], stdout=out, stderr=err)
                row.update({"run_exit": executed.returncode, "binary_sha256": digest(binary.read_bytes()),
                    "binary_included": False, "stdout_sha256": digest(stdout.read_bytes()),
                    "stderr_sha256": digest(stderr.read_bytes())})
                if executed.returncode:
                    raise RuntimeError(name + " execution failed; full output retained")
                if digest(stdout.read_bytes()) != variant["run"]["stdout"]["sha256"]:
                    raise RuntimeError(name + " replay output differs from the original captured bytes")
                row["byte_identical_to_original"] = True
        result["valid"] = True
        print("Both retained source variants replayed with byte-identical native outputs.")
    except (OSError, ValueError, RuntimeError) as error:
        result["error"] = str(error)
        print(str(error))
    finally:
        (output / "receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    return 0 if result["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
