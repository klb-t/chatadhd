#!/usr/bin/env python3
"""Capture an offline, source-paired W12 DefaultLayers/W11 RuntimeProfile proof.

Two isolated executables compile the same evidence runner and pinned actual W12
sources against (1) immutable git W11 inputs and (2) current W11 inputs. No CMake,
database, provider, model or production registry is invoked. Every input copy,
compile/run stream and unsuccessful outcome is preserved. Existing output
directories are refused so negative evidence cannot be silently overwritten.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile


NATIVE_SOURCES = (
    "loom/src/model/runtime_profile.cpp",
    "loom/src/util/fs.cpp",
    "loom/src/util/log.cpp",
    "loom/src/util/random_ids_time.cpp",
    "loom/src/util/json.cpp",
    "loom/src/util/sha256.cpp",
    "loom/src/util/utf8.cpp",
    "loom/src/util/unicode.cpp",
    "loom/src/util/result.cpp",
)
W12_FILES = (
    "loom/include/loom/onboarding_layers.h",
    "loom/src/onboarding/layers.cpp",
    "loom/src/onboarding/runtime_adapter.cpp",
    "loom/src/onboarding/runtime_adapter.h",
)
QUOTED_INCLUDE = re.compile(rb'^\s*#\s*include\s*"([^"\n]+)"', re.MULTILINE)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode()


def git(root, *args):
    result = subprocess.run(["git", *args], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace"))
    return result.stdout


def safe_path(path):
    path = Path(path)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("unsafe snapshot path: " + str(path))
    return path.as_posix()


def copy_inputs(root, destination, variant, baseline, w12, runner_bytes):
    records, pending, copied = [], list(NATIVE_SOURCES), set()

    def read(path):
        if path in W12_FILES:
            return git(root, "show", f"{w12}:{path}"), {"source": "git", "commit": w12}
        if variant == "before":
            return git(root, "show", f"{baseline}:{path}"), {"source": "git", "commit": baseline}
        return (root / path).read_bytes(), {"source": "working_tree", "head": git(root, "rev-parse", "HEAD").decode().strip()}

    def copy(path):
        path = safe_path(path)
        if path in copied:
            return
        raw, origin = read(path)
        target = destination / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
        records.append({"path": path, "bytes": len(raw), "sha256": digest(raw), **origin})
        copied.add(path)
        if path.endswith((".cpp", ".h", ".inc")):
            for match in QUOTED_INCLUDE.finditer(raw):
                include = match[1].decode()
                candidate = "loom/include/" + include if include.startswith("loom/") else (Path(path).parent / include).as_posix()
                pending.append(safe_path(candidate))

    pending.extend(W12_FILES)
    pending.append("loom/third_party/nlohmann/nlohmann/json.hpp")
    while pending:
        copy(pending.pop())
    runner_target = destination / "runner.cpp"
    runner_target.write_bytes(runner_bytes)
    records.append({"path": "runner.cpp", "bytes": runner_target.stat().st_size,
                    "sha256": digest(runner_target.read_bytes()), "source": "evidence_runner"})
    return records


def capture_command(argv, cwd, log):
    with log.open("wb") as stream:
        result = subprocess.run(argv, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT)
    raw = log.read_bytes()
    return {"argv": argv, "cwd": str(cwd), "exit": result.returncode,
            "combined_output": {"file": log.name, "bytes": len(raw), "sha256": digest(raw)}}


def native_run(binary, fixture, output, error):
    argv = [str(binary), str(fixture)]
    with output.open("wb") as stdout, error.open("wb") as stderr:
        result = subprocess.run(argv, stdout=stdout, stderr=stderr)
    return {"argv": argv, "exit": result.returncode,
            "stdout": {"file": output.name, "bytes": output.stat().st_size, "sha256": digest(output.read_bytes())},
            "stderr": {"file": error.name, "bytes": error.stat().st_size, "sha256": digest(error.read_bytes())}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--w12", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="c++")
    args = parser.parse_args()
    root, output = args.root.resolve(), args.output.resolve()
    if output.exists():
        parser.error("output already exists; preserve it and choose a new directory")
    baseline = git(root, "rev-parse", args.baseline).decode().strip()
    w12 = git(root, "rev-parse", args.w12).decode().strip()
    output.mkdir(parents=True)
    runner = Path(__file__).resolve().with_name("runner.cpp")
    runner_bytes = runner.read_bytes()
    source_pack_bytes = git(root, "show", f"{w12}:loom/data/profiles/user.pack")
    source_pack = json.loads(source_pack_bytes)
    fixture = {"schema": "loom.runtime_layers_mixed_fixture/1",
        "key_prefix": "evidence.runtime.", "pack_prefix": "evidence.runtime_layers.",
        "new_key_suffix": "/new-fixture-entry", "known_at": "2026-10-05T00:00:00Z",
        "provenance": {"kind": "synthetic_fixture", "actor": "independent_mixed_runtime_layers_probe"},
        "policy": source_pack["policy"],
        "opaque_state": {"opaque_fixture_state": {"source_bytes": "unchanged fixture metadata", "future_member": [1, None, True]}}}
    fixture_file = output / "fixture.json"
    fixture_file.write_bytes(encoded(fixture))
    (output / "w12-source-user.pack").write_bytes(source_pack_bytes)
    receipt = {"schema": "loom.runtime_layers_mixed_capture/1", "valid": False,
        "baseline_commit": baseline, "w12_commit": w12,
        "compiler": args.compiler, "compiler_version": subprocess.check_output([args.compiler, "--version"]).decode(),
        "runner_sha256": digest(runner_bytes), "capture_script_sha256": digest(Path(__file__).read_bytes()),
        "fixture_sha256": digest(fixture_file.read_bytes()), "policy_source": {
            "git_path": "loom/data/profiles/user.pack", "git_commit": w12,
            "raw_sha256": digest(source_pack_bytes), "json_pointer": "/policy"},
        "provider_calls": 0, "database_created": False, "root_cmake_changed": False,
        "production_bindings_claimed": False, "variants": []}
    try:
        results = {}
        snapshots = {}
        # Freeze BOTH complete source graphs before compiling either variant.
        for variant in ("before", "after"):
            snapshot = output / (variant + "-inputs")
            snapshot.mkdir()
            inputs = copy_inputs(root, snapshot, variant, baseline, w12, runner_bytes)
            (output / (variant + "-inputs.json")).write_bytes(encoded(inputs))
            snapshots[variant] = snapshot
            receipt["variants"].append({"variant": variant, "inputs": inputs})
        with tempfile.TemporaryDirectory(prefix="w11-runtime-layers-") as binary_directory:
            for variant_receipt in receipt["variants"]:
                variant = variant_receipt["variant"]
                snapshot = snapshots[variant]
                binary = Path(binary_directory) / (variant + "-runner")
                command = [args.compiler, "-std=c++20", "-O0", "-g0", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(snapshot / "loom/include"), "-isystem", str(snapshot / "loom/third_party/nlohmann"),
                    str(snapshot / "runner.cpp"),
                    *[str(snapshot / path) for path in NATIVE_SOURCES],
                    str(snapshot / "loom/src/onboarding/layers.cpp"),
                    str(snapshot / "loom/src/onboarding/runtime_adapter.cpp"), "-pthread", "-o", str(binary)]
                variant_receipt["compile"] = capture_command(command, root, output / (variant + ".compile.log"))
                if variant_receipt["compile"]["exit"]:
                    raise RuntimeError(variant + " compilation failed; complete log and inputs retained")
                variant_receipt["binary_sha256"] = digest(binary.read_bytes())
                variant_receipt["binary_included"] = False
                variant_receipt["run"] = native_run(binary, fixture_file, output / (variant + ".json"), output / (variant + ".stderr.log"))
                if variant_receipt["run"]["exit"]:
                    raise RuntimeError(variant + " proof failed; complete outputs and inputs retained")
                results[variant] = json.loads((output / (variant + ".json")).read_bytes())
        before = {row["domain"]: row for row in results["before"]["results"]}
        after = {row["domain"]: row for row in results["after"]["results"]}
        for variant, rows in (("before", before), ("after", after)):
            if len(rows) != len(results[variant]["results"]) or len(rows) != results[variant]["domains"]:
                raise RuntimeError("duplicate or inconsistent domain count: " + variant)
        if not before.keys() <= after.keys():
            raise RuntimeError("a baseline domain disappeared")
        shared = []
        for domain, old in before.items():
            new = after[domain]
            fields = ("definition", "defaults", "default_hash", "definition_sha256", "defaults_sha256", "bindings")
            if any(old[field] != new[field] for field in fields):
                raise RuntimeError("baseline definition/default/hash/binding drift: " + domain)
            shared.append({"domain": domain, "default_hash": new["default_hash"],
                "definition_sha256": new["definition_sha256"], "defaults_sha256": new["defaults_sha256"], "identical": True})
        provenance = []
        for domain, row in after.items():
            if not row["inspection_descriptor_verified"]:
                raise RuntimeError("current inspection lacks exact descriptor/defaults: " + domain)
            if not isinstance(row["inspection_source_provenance"], list) or not row["inspection_source_provenance"]:
                raise RuntimeError("current domain has no source provenance: " + domain)
            for source in row["inspection_source_provenance"]:
                path = safe_path(source["path"])
                raw = (root / path).read_bytes()
                if digest(raw) != source["raw_sha256"]:
                    raise RuntimeError("source provenance raw hash mismatch: " + domain + " " + path)
                target = output / "after-provenance-inputs" / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(raw)
                provenance.append({"domain": domain, **source, "verified": True})
        for variant in receipt["variants"]:
            for source in variant["inputs"]:
                if source["source"] == "evidence_runner" and source["sha256"] != receipt["runner_sha256"]:
                    raise RuntimeError("paired variant changed evidence runner bytes")
                if source["source"] == "working_tree" and digest((root / source["path"]).read_bytes()) != source["sha256"]:
                    raise RuntimeError("working-tree input changed after immutable snapshot: " + source["path"])
        receipt["summary"] = {"before_domains": len(before), "after_domains": len(after),
            "shared_identical_domains": len(shared), "added_domains": sorted(after.keys() - before.keys()),
            "before_binding_groups": results["before"]["binding_groups"], "after_binding_groups": results["after"]["binding_groups"],
            "before_assertions": results["before"]["assertions"], "after_assertions": results["after"]["assertions"],
            "source_provenance_records_verified": len(provenance)}
        receipt["shared_domain_parity"] = shared
        receipt["verified_source_provenance"] = provenance
        receipt["valid"] = True
        print(json.dumps(receipt["summary"], sort_keys=True))
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        receipt["error"] = str(error)
        print(str(error), file=sys.stderr)
    finally:
        (output / "receipt.json").write_bytes(encoded(receipt))
    return 0 if receipt["valid"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
