#!/usr/bin/env python3
"""Offline, paired DIC-0301 native replay; requires frozen prebuilt archives.

Baseline common.cpp/principles.cpp/internal.h/cues.json are exact Git blobs.
Both phases link explicit modules before the same current static archives and
use explicit old/new cue documents. This is default-preservation and overlay
evidence, not a new quality claim or a substitute for the full CTest suite.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

BASE_PIN = "e4109df7e4af22b461def5f7d62e268d9b9a8825"


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run(command, *, cwd, log, stdout=None):
    with log.open("ab") as errors:
        errors.write((json.dumps([str(x) for x in command]) + "\n").encode())
        errors.flush()
        if stdout is None:
            subprocess.run(command, cwd=cwd, stdout=errors, stderr=errors, check=True)
        else:
            with stdout.open("wb") as output:
                subprocess.run(command, cwd=cwd, stdout=output, stderr=errors, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="fresh scratch directory; never overwritten")
    parser.add_argument("--base-pin", default=BASE_PIN)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[4]
    build = args.core_build.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    legacy, current = output / "legacy", output / "current"
    legacy.mkdir()
    current.mkdir()
    tests = Path(__file__).resolve().parent
    fixture = tests / "dic0301.cpp.fixture"
    oracle = tests / "dic0301_legacy_classes.json.fixture"
    sources = {
        "common.cpp": "loom/src/generalize/common.cpp",
        "principles.cpp": "loom/src/generalize/principles.cpp",
        "internal.h": "loom/src/generalize/internal.h",
        "cues.json": "loom/data/lexicons/cues.json",
        "thresholds.json": "loom/data/policy/thresholds.json",
    }
    for name, path in sources.items():
        (legacy / name).write_bytes(subprocess.check_output(["git", "show", f"{args.base_pin}:{path}"], cwd=repo))
        (current / name).write_bytes((repo / path).read_bytes())
    libraries = [build / name for name in ("libloom_core.a", "libloom_sqlite3_amalgamation.a", "libloom_miniz.a")]
    if not all(path.is_file() for path in libraries):
        raise RuntimeError("missing prebuilt archives; complete the coordinated native build first")
    # Include every pack/corpus/header dependency in the frozen-source receipt.
    manifest = json.loads((repo / "loom/data/pack.json").read_text())
    pack_files = [repo / "loom/data/pack.json", *(repo / "loom/data" / item["path"] for item in manifest["files"])]
    corpus_files = sorted((repo / "loom/tests/fixtures/eval/synthetic_dev").rglob("*"))
    mechanism_files = sorted((repo / "loom/src/generalize").glob("*.cpp")) + sorted((repo / "loom/src/generalize").glob("*.h"))
    frozen_paths = sorted(set([fixture, oracle, Path(__file__).resolve(),
        repo / "loom/tests/test_generalize_fixture.h", repo / "loom/include/loom/generalize.h",
        repo / "loom/src/kb/pack_embedded.inc", *pack_files, *mechanism_files, *libraries,
        *(path for path in corpus_files if path.is_file())]))
    frozen_hashes = {path: sha(path) for path in frozen_paths}
    snapshots = {}
    for path in frozen_paths:
        if path in libraries:
            continue
        target = output / "source_snapshot" / path.relative_to(repo)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
        assert sha(target) == frozen_hashes[path], "source changed during snapshot"
        snapshots[str(path.relative_to(repo))] = str(target.relative_to(output))
    # Persist inputs before the first compile so a negative/interrupted replay
    # retains its exact source and native-library identities, even after fixes.
    labels = {str(path.relative_to(repo)) if path.is_relative_to(repo) else str(path): digest
        for path, digest in frozen_hashes.items()}
    (output / "inputs.json").write_text(json.dumps({
        "schema": "loom.test.dic0301_inputs/1", "base_pin": args.base_pin,
        "repository_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(),
        "sha256": labels, "snapshots": snapshots,
    }, ensure_ascii=False, indent=1) + "\n")
    expected = json.loads(oracle.read_text())
    actual = json.loads((current / "cues.json").read_text())["classes"]
    old = json.loads((legacy / "cues.json").read_text())["classes"]
    assert len(expected) == 7 and sum(len(v["phrases"]) for v in expected.values()) == 120
    assert all(actual[name] == value and name not in old for name, value in expected.items())
    compiler = shutil.which(os.environ.get("CXX", "c++"))
    if not compiler:
        raise RuntimeError("C++ compiler unavailable")
    flags = [compiler, "-std=c++20", "-O0", "-g0", "-I" + str(repo / "loom/include"),
        "-I" + str(repo / "loom/src"), "-I" + str(repo / "loom/tests"),
        "-isystem", str(repo / "loom/third_party/nlohmann"),
        '-DLOOM_TEST_FIXTURES="' + str(repo / "loom/tests/fixtures") + '"']
    log = output / "commands.log"
    objects = {phase: [] for phase in ("before", "after")}
    for phase in objects:
        for name in ("common", "principles"):
            source = (legacy if phase == "before" else current) / (name + ".cpp")
            obj = output / (phase + "_" + name + ".o")
            run(flags + ["-c", str(source), "-o", str(obj)], cwd=repo, log=log)
            objects[phase].append(obj)
    link = [str(path) for path in libraries] + ["-lssl", "-lcrypto", "-lpthread",
        "-Wl,--no-keep-memory,--reduce-memory-overheads"]
    for phase in objects:
        executable = output / ("dic0301_" + phase)
        compile_flags = flags + (["-DDIC0301_BEFORE"] if phase == "before" else [])
        run(compile_flags + ["-x", "c++", str(fixture), "-x", "none"] + [str(obj) for obj in objects[phase]] +
            link + ["-o", str(executable)], cwd=repo, log=log)
        run([str(executable), str(oracle), str((legacy if phase == "before" else current) / "cues.json"),
            str(current / "thresholds.json"), str(output / (phase + "_overlay"))],
            cwd=repo, log=log, stdout=output / (phase + ".json"))
    before, after = (json.loads((output / (phase + ".json")).read_text()) for phase in objects)
    preserved = {key: before[key] == after[key] for key in ("defaults", "dev_principles", "empty_pack_override", "replacement")}
    counts = after["counts"]
    passed = all(preserved.values()) and counts["deletion_no_resurrection"] == 7 and \
        counts["durable_deletion_no_resurrection"] == 7 and counts["durable_replacement_only"] == 7 and \
        counts["malformed_overlay_rejected"] == 7 and before["counts"]["deletion_no_resurrection"] == 0 and \
        before["counts"]["durable_deletion_no_resurrection"] == 0
    assert all(sha(path) == digest for path, digest in frozen_hashes.items()), "source or archive changed during replay"
    receipts = [*frozen_paths, *(directory / name for directory in (legacy, current) for name in sources),
        *(path for phase_objects in objects.values() for path in phase_objects),
        *(output / (phase + suffix) for phase in objects for suffix in (".json", "")), log]
    # Executables use the dic0301_ prefix, unlike JSON reports.
    receipts = [path for path in receipts if path.is_file()] + [output / "dic0301_before", output / "dic0301_after"]
    def label(path):
        if path.is_relative_to(repo): return str(path.relative_to(repo))
        if path.is_relative_to(output): return str(path.relative_to(output))
        return str(path)
    report = {
        "schema": "loom.test.dic0301/2", "base_pin": args.base_pin,
        "repository_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip(),
        "passed": passed, "before_counts": before["counts"], "after_counts": counts,
        "preservation": preserved, "typed_phrase_samples": sum(len(row["samples"]) for row in after["defaults"]),
        "dev_principles": len(after["dev_principles"]["principles"]),
        "input_hashes_stable_during_replay": True,
        "limitations": [
            "Whole-file pack overlay deletion survives reload; this is not the shared R40 graph exclusion-marker mechanism.",
            "Empty phrases[] remains rejected by the unchanged Pack validator; class deletion disables matching.",
            "Both explicit before/current modules use the same frozen native archives and explicit cue documents.",
            "Both phases receive the same explicit current thresholds document; the exact base producer ignores new recipe fields and the after producer uses their unchanged defaults.",
            "Full build/test_kb_pack separately verifies the newly embedded pack; this focused replay is not full CTest.",
            "Public fictional synthetic_dev only; unchanged defaults are not a new accuracy measurement."],
        "sha256": {label(path): sha(path) for path in receipts},
    }
    (output / "summary.json").write_text(json.dumps(report, indent=1, ensure_ascii=False) + "\n")
    print(json.dumps({key: report[key] for key in ("passed", "before_counts", "after_counts", "preservation",
        "typed_phrase_samples", "dev_principles")}, indent=1))
    if not passed: raise SystemExit(1)


if __name__ == "__main__":
    main()
