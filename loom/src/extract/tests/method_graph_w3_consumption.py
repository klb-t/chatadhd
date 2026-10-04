#!/usr/bin/env python3
"""Compile actual pinned W3 load/resolve with W1 profiles; no provider or W4 calls."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import sqlite3
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[4]
LOOM = REPO / "loom"
W3_REF = "03c670caa6ee3d8ac2c478186548114f8e83927f"
W3_FILES = ("loom/src/context/method_registry.h", "loom/src/context/method_registry.cpp")
EXPECTED_NATIVE_CHECKS = 29


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=LOOM / "build/dev",
                        help="existing core archives; never builds or changes them")
    parser.add_argument("--scratch-dir", type=Path,
                        help="preserve exported source, binary and scratch-only database here")
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--evidence", type=Path,
                        help="directory receiving receipt.json and full raw.log")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    fixture = LOOM / "src/extract/tests/method_graph_w3_consumption.cpp.fixture"
    adapter = LOOM / "src/extract/prompt_method_graph.cpp"
    source_paths = [Path(__file__).resolve(), fixture, adapter,
        LOOM / "src/extract/prompt_method_graph.h", LOOM / "src/extract/prompt_method_data.inc",
        LOOM / "src/extract/prompt_contract.h", LOOM / "src/extract/prompt_contract.cpp",
        LOOM / "src/extract/prompt_contract_data.inc",
        LOOM / "include/loom/db.h", LOOM / "include/loom/knowledge_semantic.h",
        *sorted(path for path in (LOOM / "data/prompts").glob("*")
                if path.is_file() and path.suffix in {".pack", ".prompt", ".recipe"})]
    source_hashes = {str(path.relative_to(REPO)): sha256(path) for path in source_paths}
    archives = [build / name for name in (
        "libloom_core.a", "libloom_sqlite3_amalgamation.a", "libloom_miniz.a")]
    library_hashes = {path.name: {"sha256": sha256(path), "bytes": path.stat().st_size}
                      for path in archives}
    scratch = args.scratch_dir.resolve() if args.scratch_dir else Path(tempfile.mkdtemp(
        prefix="loom-method-w3-consumption-"))
    scratch.mkdir(parents=True, exist_ok=True)
    exported = scratch / "w3-export"
    exported.mkdir(exist_ok=True)
    database = scratch / "fixture.sqlite"
    if database.exists():
        raise ValueError("use a fresh scratch directory; the fixture database must begin absent")
    evidence: dict = {"schema": "loom.method_graph_w3_source_consumption_probe/1",
        "offline": True, "provider_calls": 0, "paid_calls": 0,
        "producer_execution_verified": False, "w3_joint_execution_verified": False,
        "w4_joint_gate_verified": False, "canonical_store_written": False,
        "scope": "actual pinned W3 source load/resolve, exact root-shaped effective parameters; host availability declarations only",
        "w3_commit": W3_REF, "source_sha256": source_hashes,
        "linked_library_sha256": library_hashes, "foreign_sources": {},
        "expected_native_checks": EXPECTED_NATIVE_CHECKS,
        "negative_jev_resolution_is_a_consumer_limitation": True}
    log: list[str] = []

    def record(command: list[str], process: subprocess.CompletedProcess) -> None:
        log.extend(["$ " + shlex.join(command), "exit=" + str(process.returncode),
                    "--- stdout ---", process.stdout, "--- stderr ---", process.stderr])

    for path in W3_FILES:
        command = ["git", "show", W3_REF + ":" + path]
        source = subprocess.run(command, cwd=REPO, capture_output=True)
        if source.returncode:
            raise RuntimeError(source.stderr.decode())
        destination = exported / Path(path).name
        destination.write_bytes(source.stdout)
        blob_command = ["git", "rev-parse", W3_REF + ":" + path]
        blob = subprocess.check_output(blob_command, cwd=REPO, text=True).strip()
        reconstructed_blob = hashlib.sha1(b"blob " + str(len(source.stdout)).encode() +
            b"\0" + source.stdout).hexdigest()
        if reconstructed_blob != blob:
            raise ValueError("exported Git blob mismatch: " + path)
        evidence["foreign_sources"][path] = {"commit": W3_REF, "git_blob_sha1": blob,
            "sha256": sha256(destination), "bytes": len(source.stdout),
            "export_command": command, "blob_command": blob_command}
        log.append("export " + shlex.join(command) + " -> " + str(destination) +
                   " (Git blob=" + blob + "; SHA256=" + sha256(destination) + ")")
    binary = scratch / "method_graph_w3_consumption"
    command = [args.cxx, "-x", "c++", "-std=c++20", "-g0", "-Wall", "-Wextra", "-Werror",
        "-Iloom/include", "-Iloom/src", "-I" + str(exported),
        "-isystem", "loom/third_party/nlohmann",
        str(fixture.relative_to(REPO)), str(adapter.relative_to(REPO)),
        str(exported / "method_registry.cpp"), "-x", "none",
        *[str(path) for path in archives], "-lssl", "-lcrypto", "-lpthread"]
    if sys.platform.startswith("linux"):
        command.append("-Wl,--no-keep-memory")
    command += ["-o", str(binary)]
    compiled = subprocess.run(command, cwd=REPO, capture_output=True, text=True)
    record(command, compiled)
    run_command = [str(binary), str(database)]
    result = subprocess.run(run_command, cwd=REPO, capture_output=True, text=True) if compiled.returncode == 0 else None
    if result:
        record(run_command, result)
    payload = None
    if result:
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError as failure:
            evidence["stdout_parse_error"] = str(failure)
    evidence.update({"compile_command": command, "compile_exit_code": compiled.returncode,
        "run_command": run_command, "run_exit_code": result.returncode if result else None,
        "compile_stdout": compiled.stdout, "compile_stderr": compiled.stderr,
        "fixture": payload, "run_stdout": result.stdout if result else None,
        "run_stderr": result.stderr if result else None})
    table_counts = {}
    if database.exists():
        with sqlite3.connect("file:" + str(database) + "?mode=ro", uri=True) as db:
            tables = [row[0] for row in db.execute(
                "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'")]
            for table in sorted(tables):
                quoted = '"' + table.replace('"', '""') + '"'
                table_counts[table] = db.execute("SELECT COUNT(*) FROM " + quoted).fetchone()[0]
    relevant = {name: count for name, count in table_counts.items()
                if name.startswith(("loom_kb_", "loom_graph_packet"))}
    # Database::open initializes base schema metadata. Pure load/resolve does
    # not create the KB tables at all; absence is consistent with no side-write.
    data_tables = {name: count for name, count in table_counts.items() if name != "_meta"}
    no_canonical_rows = bool(data_tables) and all(count == 0 for count in data_tables.values())
    evidence["scratch_database_table_counts"] = table_counts
    evidence["canonical_tables_present"] = sorted(relevant)
    evidence["no_canonical_rows_written_verified"] = no_canonical_rows
    evidence["production_sources_unchanged"] = source_hashes == {
        str(path.relative_to(REPO)): sha256(path) for path in source_paths}
    evidence["linked_archives_unchanged"] = library_hashes == {
        path.name: {"sha256": sha256(path), "bytes": path.stat().st_size} for path in archives}
    evidence["success"] = bool(compiled.returncode == 0 and result and result.returncode == 0 and
        payload and payload.get("success") and
        payload.get("executed_checks") == payload.get("passed_checks") == EXPECTED_NATIVE_CHECKS and
        no_canonical_rows and evidence["production_sources_unchanged"] and evidence["linked_archives_unchanged"])
    if args.evidence:
        args.evidence.mkdir(parents=True, exist_ok=True)
        raw = args.evidence / "raw.log"
        raw.write_text("\n".join(log) + "\n", encoding="utf-8")
        evidence["raw_log_sha256"] = sha256(raw)
        (args.evidence / "receipt.json").write_text(json.dumps(evidence,
            ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"success": evidence["success"],
        "checks": f"{payload.get('passed_checks', 0) if payload else 0}/{EXPECTED_NATIVE_CHECKS}",
        "compile_exit": compiled.returncode, "run_exit": result.returncode if result else None,
        "w3_commit": W3_REF, "foreign_sources": evidence["foreign_sources"],
        "known_negative_jev": payload.get("known_negative_jev_resolution", {}).get("actual_error") if payload else None,
        "no_canonical_rows_written_verified": no_canonical_rows,
        "production_sources_unchanged": evidence["production_sources_unchanged"],
        "provider_calls": 0, "paid_calls": 0, "scratch_dir": str(scratch),
        "evidence": str(args.evidence) if args.evidence else None}, ensure_ascii=False, indent=2))
    return 0 if evidence["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
