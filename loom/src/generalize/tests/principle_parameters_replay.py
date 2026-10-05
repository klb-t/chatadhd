#!/usr/bin/env python3
"""Offline, paired native replay of DIC0322/0323/0324 data migration.

The before modules come from a full Git commit, the after modules from a frozen
working tree, and both link one identical immutable set of built libraries.
No live provider, private data, holdout corpus or CTest gate is involved.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import math
from pathlib import Path
import subprocess
import sys
import tarfile


ROOT = Path(__file__).resolve().parents[4]
LOOM = ROOT / "loom"
MODULES = (
    "loom/src/generalize/principles.cpp",
    "loom/src/generalize/common.cpp",
    "loom/src/generalize/internal.h",
)
DEFAULT_CASES = (
    "synthetic_dev", "synthetic_dev_without_priors", "seed_default",
    "discovered_low_default", "discovered_high_default",
)
KEYS = (
    "seed_jaccard_multiplier", "repeat_confidence_residual_factor",
    "discovered_confidence_cap", "discovered_confidence_base",
    "discovered_confidence_score_factor",
)


def sha256(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def canonical(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False).encode()


def run(command: list[str], *, log: Path, receipts: list[dict]) -> bytes:
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    with log.open("ab") as output:
        output.write(canonical({"command": command}) + b"\n")
        output.write(result.stdout + b"\n")
    receipts.append({"command": command, "returncode": result.returncode,
                     "stdout_sha256": sha256(result.stdout),
                     "stdout_bytes": len(result.stdout)})
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}); full output: {log}")
    return result.stdout


def archive_sources(files: dict[str, bytes], destination: Path) -> None:
    with destination.open("wb") as output:
        with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w") as archive:
                for name, content in sorted(files.items()):
                    info = tarfile.TarInfo(name)
                    info.size = len(content)
                    info.mtime = 0
                    info.mode = 0o644
                    archive.addfile(info, io.BytesIO(content))


def find_library(build: Path, name: str, *, optional: bool = False) -> Path | None:
    matches = sorted(build.rglob(name))
    if len(matches) != 1:
        if optional and not matches:
            return None
        raise RuntimeError(f"expected one {name} below {build}, got {matches}")
    return matches[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", default="e4109df7e4af22b461def5f7d62e268d9b9a8825")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    evidence = args.evidence.resolve()
    if evidence.exists():
        raise RuntimeError("evidence directory must be new; preserve previous positive and negative runs")
    evidence.mkdir(parents=True)
    commands: list[dict] = []
    summary = {"schema": "loom.principle_parameter_replay/1", "status": "running",
               "before": args.before, "commands": commands,
               "source_sha256": {}, "library_sha256": {}, "checks": []}
    log = evidence / "commands.log"
    source_archive = {}
    watched: dict[Path, str] = {}
    try:
        base = subprocess.check_output(["git", "rev-parse", args.before], cwd=ROOT, text=True).strip()
        summary["before"] = base
        paths = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", base, "loom/data"],
                                        cwd=ROOT, text=True).splitlines()
        before_docs = {}
        for path in paths:
            if not path.endswith(".json"):
                continue
            content = subprocess.check_output(["git", "show", f"{base}:{path}"], cwd=ROOT)
            before_docs[path.removeprefix("loom/data/")] = json.loads(content)
            source_archive[f"before/{path}"] = content
            summary["source_sha256"][f"before/{path}"] = sha256(content)
        after_docs = {}
        for path in sorted((LOOM / "data").rglob("*.json")):
            content = path.read_bytes()
            watched[path] = sha256(content)
            after_docs[path.relative_to(LOOM / "data").as_posix()] = json.loads(content)
            relative = path.relative_to(ROOT).as_posix()
            source_archive[f"after/{relative}"] = content
            summary["source_sha256"][f"after/{relative}"] = sha256(content)
        for side, docs in (("before", before_docs), ("after", after_docs)):
            source_dir = evidence / "sources" / side / "generalize"
            source_dir.mkdir(parents=True)
            bundle = canonical(docs)
            (evidence / f"{side}_pack_documents.json").write_bytes(bundle)
            source_archive[f"{side}/pack_documents.json"] = bundle
            for path in MODULES:
                content = (subprocess.check_output(["git", "show", f"{base}:{path}"], cwd=ROOT)
                           if side == "before" else (ROOT / path).read_bytes())
                if side == "after":
                    watched[ROOT / path] = sha256(content)
                (source_dir / Path(path).name).write_bytes(content)
                source_archive[f"{side}/{path}"] = content
                summary["source_sha256"][f"{side}/{path}"] = sha256(content)
        fixture_path = Path(__file__).with_name("principle_parameters.cpp.fixture")
        fixture = fixture_path.read_bytes()
        watched[fixture_path] = sha256(fixture)
        (evidence / "sources" / "probe.cpp").write_bytes(fixture)
        source_archive["probe.cpp"] = fixture
        source_archive["replay.py"] = Path(__file__).read_bytes()
        fixture_loader = LOOM / "tests" / "test_generalize_fixture.h"
        watched[fixture_loader] = sha256(fixture_loader.read_bytes())
        source_archive["test_generalize_fixture.h"] = fixture_loader.read_bytes()
        fixture_dir = LOOM / "tests" / "fixtures" / "eval" / "synthetic_dev"
        for name in ("ground_truth.json", "chatgpt_export.zip", "claude_export.zip"):
            path = fixture_dir / name
            watched[path] = sha256(path.read_bytes())
            source_archive[f"synthetic_dev/{name}"] = path.read_bytes()
        archive_sources(source_archive, evidence / "sources.tar.gz")
        summary["source_archive_sha256"] = sha256((evidence / "sources.tar.gz").read_bytes())
        libraries = [find_library(build, "libloom_core.a"), find_library(build, "libloom_miniz.a")]
        sqlite = find_library(build, "libloom_sqlite3_amalgamation.a", optional=True)
        if sqlite:
            libraries.append(sqlite)
        for library in libraries:
            assert library is not None
            digest = sha256(library.read_bytes())
            watched[library] = digest
            summary["library_sha256"][str(library)] = digest
        def unchanged() -> None:
            changed = [str(path) for path, expected in watched.items() if sha256(path.read_bytes()) != expected]
            if changed:
                raise RuntimeError(f"input changed during paired proof: {changed}")
        output = {}
        for side in ("before", "after"):
            unchanged()
            executable = evidence / f"probe_{side}"
            source_dir = evidence / "sources" / side / "generalize"
            command = [args.compiler, "-std=c++20", "-O0", "-g0", "-pthread",
                       "-I" + str(LOOM / "include"), "-I" + str(LOOM / "src"),
                       "-I" + str(LOOM / "tests"), "-I" + str(LOOM / "third_party" / "nlohmann"),
                       '-DLOOM_TEST_FIXTURES="' + str(LOOM / "tests" / "fixtures") + '"',
                       str(evidence / "sources" / "probe.cpp"), str(source_dir / "principles.cpp"),
                       str(source_dir / "common.cpp"), *[str(path) for path in libraries],
                       "-ldl", "-lm", "-lssl", "-lcrypto", "-Wl,--no-keep-memory"]
            if not sqlite:
                command.append("-lsqlite3")
            command += ["-o", str(executable)]
            run(command, log=log, receipts=commands)
            unchanged()
            content = run([str(executable), str(evidence / f"{side}_pack_documents.json")],
                          log=log, receipts=commands)
            (evidence / f"{side}_results.json").write_bytes(content)
            output[side] = json.loads(content)
            summary[f"{side}_results_sha256"] = sha256(content)
            unchanged()
        checks: list[dict] = summary["checks"]
        def check(name: str, passed: bool, **details: object) -> None:
            checks.append({"name": name, "passed": passed, **details})
        before, after = output["before"], output["after"]
        for case in DEFAULT_CASES:
            left, right = before["results"][case], after["results"][case]
            check(f"default_byte_identity:{case}", left["ok"] and right["ok"] and
                  left["serialized_report"] == right["serialized_report"],
                  before_sha256=sha256(left.get("serialized_report", "").encode()),
                  after_sha256=sha256(right.get("serialized_report", "").encode()),
                  before_count=len(left.get("report", {}).get("principles", [])),
                  after_count=len(right.get("report", {}).get("principles", [])))
        seed_id = after["seed_id"]
        def confidence(result: dict, seed: bool = False) -> float:
            principles = result["report"]["principles"]
            if seed:
                principles = [item for item in principles if item["id"] == seed_id]
            if len(principles) != 1:
                raise RuntimeError(f"expected exactly one target principle, got {principles}")
            return principles[0]["confidence"]
        initial = after["seed_initial_confidence"]
        expected_default = 1.0 - (1.0 - initial) * math.pow(0.7, 3)
        seed_default = after["results"]["seed_default"]
        check("seed_default_formula", math.isclose(confidence(seed_default, True), expected_default, rel_tol=0.0, abs_tol=1e-15),
              expected=expected_default, actual=confidence(seed_default, True))
        for key in KEYS:
            changed = after["results"]["changed"][key]
            case = ("discovered_high_default" if key == "discovered_confidence_cap" else
                    "discovered_low_default" if key.startswith("discovered_") else "seed_default")
            baseline = after["results"][case]
            check(f"setting_affects_result:{key}", changed["ok"] and changed["serialized_report"] != baseline["serialized_report"])
            check(f"before_ignored_setting:{key}", before["results"]["changed"][key]["serialized_report"] ==
                  before["results"][case]["serialized_report"])
            for kind, result in after["results"]["invalid"][key].items():
                check(f"invalid_explicit_error:{key}:{kind}", result.get("ok") is False and
                      key in result.get("message", ""), phase=result.get("phase"),
                      code=result.get("code"), message=result.get("message"))
        changed_seed = after["results"]["changed"]["seed_jaccard_multiplier"]
        check("zero_multiplier_removes_seed_match", changed_seed["report"]["seed_status"][seed_id] == "seed_only" and
              all(item["id"] != seed_id for item in changed_seed["report"]["principles"]))
        expected_changed = {
            "repeat_confidence_residual_factor": 1.0 - (1.0 - initial) * math.pow(0.9, 3),
            "discovered_confidence_cap": 1.0 - math.pow(1.0 - 0.4, 3),
            "discovered_confidence_base": 1.0 - math.pow(1.0 - (0.4 + 0.1 * 2.0), 3),
            "discovered_confidence_score_factor": 1.0 - math.pow(1.0 - (0.25 + 0.2 * 2.0), 3),
        }
        for key, expected in expected_changed.items():
            actual = confidence(after["results"]["changed"][key], key == "repeat_confidence_residual_factor")
            check(f"changed_formula:{key}", math.isclose(actual, expected, rel_tol=0.0, abs_tol=1e-15),
                  expected=expected, actual=actual)
        unchanged()
        summary["passed"] = sum(item["passed"] for item in checks)
        summary["total"] = len(checks)
        summary["status"] = "pass" if summary["passed"] == summary["total"] else "fail"
    except Exception as exc:
        summary["status"] = "fail"
        summary["failure"] = f"{type(exc).__name__}: {exc}"
    (evidence / "receipt.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({key: summary[key] for key in ("status", "passed", "total", "failure") if key in summary}))
    return 0 if summary["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
