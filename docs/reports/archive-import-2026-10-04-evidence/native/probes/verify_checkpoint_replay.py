#!/usr/bin/env python3
"""Replay the two original integrator W5 probes against an actual built core.

No source overlay, replacement importer, network calls or link maps. Results
must use a fresh external directory. The only C++ probe adaptation is an
explicit resume=true because the library's compatibility default is now false.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys


def identity(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def source_manifest(repo):
    paths = [repo / "loom/CMakeLists.txt"]
    for relative in ("loom/include", "loom/src", "loom/third_party/nlohmann", "loom/third_party/miniz", "loom/third_party/sqlite"):
        paths.extend(path for path in (repo / relative).rglob("*")
            if path.is_file() and "__pycache__" not in path.parts)
    return {str(path.relative_to(repo)): identity(path) for path in sorted(set(paths))}


def run_logged(command, path):
    with path.open("wb") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    return {"command": command, "exit_code": result.returncode, "log": path.name}


def verify_results(directory, valid):
    result = json.loads((directory / "results.json").read_text())
    first, second, third = [result[name] for name in ("first", "second", "third")]
    checks = {"first_partial": first["export_report"].get("partial") is True,
        "first_errors_visible": bool(first["export_report"].get("errors")),
        "same_source_id": first["source_id"] == second["source_id"] == third["source_id"],
        "second_resumed": second.get("resumed") is True}
    ids = [[row["id"] for row in item["conversations"]] for item in (first, second, third)]
    checks["conversation_ids_preserved"] = ids[0] == ids[1] == ids[2] and len(ids[0]) == 1
    with sqlite3.connect(f"file:{directory / 'probe.db'}?mode=ro", uri=True) as db:
        counts = {"conversations": db.execute("SELECT COUNT(*) FROM conversations").fetchone()[0],
            "messages": db.execute("SELECT COUNT(*) FROM messages").fetchone()[0],
            "project_nodes": db.execute("SELECT COUNT(*) FROM nodes WHERE kind='export:project'").fetchone()[0],
            "project_document_nodes": db.execute("SELECT COUNT(*) FROM nodes WHERE kind='export:project_doc'").fetchone()[0],
            "project_document_links": db.execute("SELECT COUNT(*) FROM links l JOIN nodes d ON d.id=l.src JOIN nodes p ON p.id=l.dst WHERE d.kind='export:project_doc' AND p.kind='export:project' AND l.link_type='part_of'").fetchone()[0],
            "integrity_check": db.execute("PRAGMA quick_check").fetchone()[0],
            "foreign_key_errors": len(db.execute("PRAGMA foreign_key_check").fetchall())}
        node_ids = [row[0] for row in db.execute("SELECT id FROM nodes ORDER BY id")]
    checks["no_duplicate_conversation_or_message"] = counts["conversations"] == counts["messages"] == 1
    checks["database_integrity"] = counts["integrity_check"] == "ok" and counts["foreign_key_errors"] == 0
    if valid:
        checks.update({"second_complete": second["export_report"].get("partial") is False,
            "second_errors_empty": not second["export_report"].get("errors"),
            "third_completed_source_cache": third.get("already_imported") is True,
            "source_complete": result["final_source_metadata"].get("import_status") == "complete",
            "project_restored": counts["project_nodes"] == 1,
            "document_restored": counts["project_document_nodes"] == 1,
            "link_restored": counts["project_document_links"] == 1})
    else:
        for name, item in zip(("first", "second", "third"), (first, second, third)):
            checks[name + "_stays_partial"] = item["export_report"].get("partial") is True
            checks[name + "_errors_visible"] = bool(item["export_report"].get("errors"))
            checks[name + "_not_completed_cache"] = item.get("already_imported") is False
        checks["source_stays_partial"] = result["final_source_metadata"].get("import_status") == "partial"
        checks["no_fabricated_project_or_document"] = counts["project_nodes"] == counts["project_document_nodes"] == 0
    return {"checks": checks, "passed": all(checks.values()), "counts": counts,
        "source_id": first["source_id"], "conversation_ids": ids[0], "final_node_ids": node_ids,
        "results": identity(directory / "results.json"), "source_zip": identity(directory / "source.zip")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--repo", type=Path, help="Public Git clone used for optional source-commit comparison")
    parser.add_argument("--source-commit", help="Expected source revision, verified for actual source/header bytes")
    parser.add_argument("--cxx", default=None)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    build = args.build_dir.resolve()
    output = args.out_dir.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("out-dir must be fresh and empty; previous failures cannot be overwritten")
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        parser.error("build-dir must contain a completed native CMake build")
    cache_lines = cache.read_text().splitlines()
    home = next((line.split("=", 1)[1] for line in cache_lines
        if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL=")), None)
    if home is None:
        parser.error("CMake cache has no source-directory binding")
    source = Path(home).resolve().parent
    repo = args.repo.resolve() if args.repo else source
    compiler = args.cxx or shutil.which("c++")
    if not compiler:
        parser.error("No C++ compiler found; supply --cxx")
    libraries = [build / name for name in ("libloom_core.a", "libloom_sqlite3_amalgamation.a", "libloom_miniz.a")]
    if not all(path.is_file() for path in libraries):
        parser.error("Build must supply actual core, vendored SQLite and miniz static libraries")
    output.mkdir(parents=True, exist_ok=True)
    original = json.loads((here / "adaptation.json").read_text())
    for name, expected in original["files"].items():
        raw = (here / "original" / name).read_bytes()
        adapted = (here / name).read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected["original_sha256"] or hashlib.sha256(adapted).hexdigest() != expected["adapted_sha256"] or adapted.replace(b"  options.resume = true;\n", b"") != raw:
            parser.error("Probe source or its one-line adaptation differs from recorded originals")
    before_sources = source_manifest(source)
    before_libraries = {str(path): identity(path) for path in libraries}
    receipt = {"schema": "loom.import_checkpoint_replay_verification/1", "provider_calls": 0,
        "source_root": str(source), "expected_source_commit": args.source_commit,
        "adaptation": original, "libraries_before": before_libraries,
        "cmake_cache": identity(cache), "compiler": compiler, "commands": [], "scenarios": {}, "passed": False}
    (output / "source-before.json").write_text(json.dumps(before_sources, indent=2) + "\n")
    try:
        if args.source_commit:
            mismatches = []
            for path, expected in before_sources.items():
                if path.startswith(("loom/src/", "loom/include/")):
                    result = subprocess.run(["git", "-C", str(repo), "show", args.source_commit + ":" + path], capture_output=True)
                    if result.returncode or hashlib.sha256(result.stdout).hexdigest() != expected["sha256"]:
                        mismatches.append(path)
            receipt["source_commit_mismatches"] = mismatches
            if mismatches:
                raise RuntimeError("Actual source tree differs from expected revision; inspect receipt mismatches")
        flags = ["-std=c++20", "-O0", "-g0", "-pthread", "-DLOOM_HAVE_OPENSSL=1", "-DCPPHTTPLIB_OPENSSL_SUPPORT=1", "-DJSON_USE_IMPLICIT_CONVERSIONS=1", "-I" + str(source / "loom/include"), "-I" + str(source / "loom/src")]
        for dependency in ("nlohmann", "miniz", "sqlite", "cpp-httplib"):
            flags += ["-isystem", str(source / "loom/third_party" / dependency)]
        for stem, valid in (("probe", False), ("probe_link_failure", True)):
            binary = output / stem
            command = [compiler, *flags, str(here / (stem + ".cpp")), *map(str, libraries), "-ldl", "-lm", "-lssl", "-lcrypto", "-Wl,--no-keep-memory", "-Wl,--reduce-memory-overheads", "-o", str(binary)]
            command_receipt = run_logged(command, output / (stem + "-compile.txt"))
            receipt["commands"].append(command_receipt)
            if command_receipt["exit_code"]:
                raise RuntimeError("Probe compilation failed: " + stem)
            command_receipt = run_logged([str(binary), str(output / (stem + "-data"))], output / (stem + "-run.txt"))
            receipt["commands"].append(command_receipt)
            if command_receipt["exit_code"]:
                raise RuntimeError("Probe execution failed: " + stem)
            verified = verify_results(output / (stem + "-data"), valid)
            verified["binary"] = identity(binary)
            receipt["scenarios"][stem] = verified
        after_sources = source_manifest(source)
        after_libraries = {str(path): identity(path) for path in libraries}
        (output / "source-after.json").write_text(json.dumps(after_sources, indent=2) + "\n")
        receipt["libraries_after"] = after_libraries
        receipt["inputs_stable"] = before_sources == after_sources and before_libraries == after_libraries
        receipt["passed"] = receipt["inputs_stable"] and len(receipt["scenarios"]) == 2 and all(value["passed"] for value in receipt["scenarios"].values())
    except Exception as error:
        receipt["error"] = str(error)
    receipt["evidence"] = {path.name: identity(path) for path in sorted(output.glob("*.txt"))}
    receipt["evidence"].update({path.name: identity(path) for path in sorted(output.glob("source-*.json"))})
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": receipt["passed"], "scenarios": {name: value["passed"] for name, value in receipt["scenarios"].items()}, "error": receipt.get("error"), "receipt": str(output / "receipt.json")}, indent=2))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
