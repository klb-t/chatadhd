#!/usr/bin/env python3
"""Run one isolated offline CLI import and record elapsed/peak RSS/SQL correctness.

Uses a new data directory per variant unless --existing-data-dir selects a
partial import for an actual resume measurement. Results must be fresh.
Before/after ID and source-state verification is outside the timed CLI process.
--baseline omits newly added audit
planning price flags while retaining existing --audit behavior. --repeat checks
the completed-source cache. Capture stdout to disk, not in Python memory.
Kernel wait4 reports the CLI's peak RSS and CPU times; GNU time is not required.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import sqlite3
import subprocess
import sys
import time


def find_database(directory):
    candidates = [path for path in directory.rglob("*") if path.is_file() and path.suffix in {".db", ".sqlite", ".sqlite3"}]
    for path in candidates:
        try:
            connection = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
            if connection.execute("SELECT COUNT(*) FROM sqlite_master WHERE name='conversations'").fetchone()[0]:
                connection.close()
                return path
            connection.close()
        except sqlite3.Error:
            pass
    raise RuntimeError("No Loom SQLite database found")


def capture_existing_data(directory, fixture):
    """Capture small synthetic benchmark identities, without writing the DB."""
    path = find_database(directory)
    with sqlite3.connect(f"file:{path}?mode=ro", uri=True) as db:
        conversation_ids = [row[0] for row in db.execute("SELECT id FROM conversations ORDER BY id")]
        message_ids = [row[0] for row in db.execute("SELECT id FROM messages ORDER BY id")]
        rows = db.execute("SELECT id,blob_hash,size,metadata FROM loom_sources ORDER BY id").fetchall()
        sources = [{"id": source_id, "blob_hash": blob_hash, "source_bytes": source_bytes,
            "status": json.loads(metadata or "{}").get("import_status")}
            for source_id, blob_hash, source_bytes, metadata in rows]
    matched = [row for row in sources if row["blob_hash"] == fixture["sha256"]
        and row["source_bytes"] == fixture["source_bytes"]]
    return {"path": str(path), "conversations": len(conversation_ids), "messages": len(message_ids),
        "conversation_ids": conversation_ids, "message_ids": message_ids,
        "source_ids": [row["id"] for row in sources], "sources": sources,
        "matched_source_ids": [row["id"] for row in matched],
        "prior_partial": any(row["status"] == "partial" for row in matched)}


def read_cli_summary(path):
    """Read the small import receipt, preserving partial results as failures."""
    try:
        result = json.loads(path.read_text())
        report = result.get("export_report") or {}
        return {"source_id": result.get("source_id"), "resumed": result.get("resumed"),
            "already_imported": result.get("already_imported"), "cancelled": result.get("cancelled"),
            "messages": result.get("messages"),
            "retained_conversation_count": result.get("retained_conversation_count"),
            "export_error_count": len(report.get("errors") or [])}
    except (OSError, ValueError, AttributeError, TypeError) as error:
        return {"error": str(error)}


def check_database(directory, fixture, baseline=False):
    path = find_database(directory)
    db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    counts = {table: db.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0] for table in ["conversations", "messages"]}
    normalized_chars = sum(len(text or "") for (text,) in db.execute("SELECT text FROM messages"))
    counts["normalized_text_characters"] = normalized_chars
    counts["foreign_key_check_rows"] = len(db.execute("PRAGMA foreign_key_check").fetchall())
    counts["integrity_check"] = db.execute("PRAGMA quick_check").fetchone()[0]
    counts["conversation_source_indices"] = db.execute("SELECT COUNT(DISTINCT json_extract(metadata,'$.export.source_index')) FROM conversations").fetchone()[0]
    counts["message_pointer_count"] = db.execute("SELECT COUNT(*) FROM messages WHERE json_extract(metadata,'$.export.json_pointer') IS NOT NULL").fetchone()[0]
    if db.execute("SELECT COUNT(*) FROM sqlite_master WHERE name='loom_import_checkpoints'").fetchone()[0]:
        counts["checkpoints"] = db.execute("SELECT COUNT(*) FROM loom_import_checkpoints").fetchone()[0]
    rows = db.execute("SELECT id,blob_hash,size,metadata FROM loom_sources WHERE parser LIKE '%.export'").fetchall()
    counts["sources"] = [{"id": source_id, "blob_hash": blob_hash, "source_bytes": source_bytes,
        "status": json.loads(metadata).get("import_status")} for source_id, blob_hash, source_bytes, metadata in rows]
    # Source preservation is checked independently of the importer ledger. A
    # digest in a database row alone does not prove the stored bytes survived.
    blob_paths = list(directory.rglob(fixture["sha256"]))
    source_preserved = False
    for blob_path in blob_paths:
        digest = hashlib.sha256()
        with blob_path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() == fixture["sha256"] and blob_path.stat().st_size == fixture["source_bytes"]:
            source_preserved = True
            break
    counts["source_blob_verified"] = source_preserved
    if fixture["payload_site"] == "metadata":
        # One message payload at a time; never decode the complete export.
        expected_digest = hashlib.sha256(b"x" * fixture["payload_bytes_per_message"]).hexdigest()
        payloads_verified = 0
        for (payload,) in db.execute("SELECT json_extract(metadata,'$.export.raw.metadata.synthetic_payload') FROM messages"):
            if (isinstance(payload, str) and len(payload) == fixture["payload_bytes_per_message"]
                    and hashlib.sha256(payload.encode("utf-8")).hexdigest() == expected_digest):
                payloads_verified += 1
        counts["raw_message_payloads_verified"] = payloads_verified
    checks = {name: counts[name] == fixture[name] for name in ["conversations", "messages", "normalized_text_characters"]}
    checks["integrity"] = counts["integrity_check"] == "ok" and counts["foreign_key_check_rows"] == 0
    checks["source_blob"] = source_preserved and any(source["blob_hash"] == fixture["sha256"]
        and source["source_bytes"] == fixture["source_bytes"] for source in counts["sources"])
    if fixture["payload_site"] == "metadata":
        checks["raw_message_payloads"] = counts["raw_message_payloads_verified"] == fixture["messages"]
    if not baseline:
        checks["source_indices"] = counts["conversation_source_indices"] == fixture["conversations"]
        checks["message_pointers"] = counts["message_pointer_count"] == fixture["messages"]
        checks["checkpoints"] = counts.get("checkpoints") == fixture["conversations"]
        checks["completed_source"] = len(counts["sources"]) == 1 and counts["sources"][0]["status"] == "complete"
    db.close()
    return {"path": str(path), "file_bytes": path.stat().st_size, "counts": counts, "checks": checks, "correct": all(checks.values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("source", type=Path)
    parser.add_argument("result_dir", type=Path)
    parser.add_argument("--baseline", action="store_true")
    parser.add_argument("--repeat", action="store_true")
    parser.add_argument("--timeout", type=float, default=1800)
    parser.add_argument("--source-commit", default=None)
    parser.add_argument("--build-description", default=None)
    parser.add_argument("--existing-data-dir", type=Path,
        help="Resume a prior synthetic import in this data directory; ID checks are outside timing")
    args = parser.parse_args()
    if args.result_dir.exists() and any(args.result_dir.iterdir()):
        parser.error("result directory must be fresh and empty; previous evidence will not be overwritten")
    args.result_dir.mkdir(parents=True, exist_ok=True)
    directory = args.existing_data_dir if args.existing_data_dir is not None else args.result_dir / "data"
    if args.existing_data_dir is not None and not directory.is_dir():
        parser.error("existing data directory must already exist")
    fixture = json.loads(args.source.with_suffix(args.source.suffix + ".fixture.json").read_text())
    existing_start = capture_existing_data(directory, fixture) if args.existing_data_dir is not None else None
    if existing_start is not None:
        (args.result_dir / "existing-data-start.json").write_text(json.dumps(existing_start, indent=2) + "\n")
    binary_digest = hashlib.sha256()
    with args.binary.open("rb") as binary_stream:
        for chunk in iter(lambda: binary_stream.read(1024 * 1024), b""):
            binary_digest.update(chunk)
    binary_identity = {"path": str(args.binary.resolve()), "sha256": binary_digest.hexdigest(),
        "bytes": args.binary.stat().st_size, "source_commit": args.source_commit,
        "build_description": args.build_description}
    command = [str(args.binary.resolve()), "--data-dir", str(directory.resolve()), "--json", "--quiet", "import", str(args.source.resolve()), "--export-mode", "on", "--audit"]
    if not args.baseline:
        command += ["--audit-input-price", "0.10", "--audit-output-price", "0.40"]
    measurements = []
    for run_index in range(2 if args.repeat else 1):
        time_log = args.result_dir / f"run-{run_index}.time.txt"
        stdout = args.result_dir / f"run-{run_index}.stdout.json"
        stderr = args.result_dir / f"run-{run_index}.stderr.txt"
        started = time.monotonic()
        with stdout.open("wb") as out, stderr.open("wb") as err:
            process = subprocess.Popen(command,
                stdout=out, stderr=err, start_new_session=True)
            timed_out = False
            terminate_started = None
            while True:
                collected, status, usage = os.wait4(process.pid, os.WNOHANG)
                if collected:
                    process.returncode = os.waitstatus_to_exitcode(status)
                    exit_code = "timeout" if timed_out else process.returncode
                    break
                elapsed = time.monotonic() - started
                if elapsed > args.timeout and not timed_out:
                    timed_out = True
                    terminate_started = time.monotonic()
                    os.killpg(process.pid, signal.SIGTERM)
                elif terminate_started is not None and time.monotonic() - terminate_started > 10:
                    os.killpg(process.pid, signal.SIGKILL)
                    terminate_started = None
                time.sleep(0.1)
        peak_rss_kib = usage.ru_maxrss / 1024 if sys.platform == "darwin" else usage.ru_maxrss
        elapsed_seconds = time.monotonic() - started
        time_log.write_text(f"Collector: os.wait4; peak RSS in KiB on {sys.platform}\n"
            f"Elapsed seconds: {elapsed_seconds}\nPeak RSS KiB: {peak_rss_kib}\n"
            f"User seconds: {usage.ru_utime}\nSystem seconds: {usage.ru_stime}\nExit code: {exit_code}\n")
        measurements.append({
            "run_index": run_index, "exit_code": exit_code, "elapsed_seconds": elapsed_seconds,
            "collector": "os.wait4; kernel ru_maxrss", "peak_rss_kib": peak_rss_kib,
            "user_seconds": usage.ru_utime, "system_seconds": usage.ru_stime,
            "stdout_bytes": stdout.stat().st_size,
            "stdout_result": read_cli_summary(stdout),
        })
        if exit_code != 0:
            break
    verification_started = time.monotonic()
    try:
        database = check_database(directory, fixture, args.baseline)
        if existing_start is not None:
            existing_end = capture_existing_data(directory, fixture)
            checks = database["checks"]
            checks["prior_conversation_ids_preserved"] = set(existing_start["conversation_ids"]).issubset(existing_end["conversation_ids"])
            checks["prior_message_ids_preserved"] = set(existing_start["message_ids"]).issubset(existing_end["message_ids"])
            checks["prior_source_ids_preserved"] = set(existing_start["source_ids"]).issubset(existing_end["source_ids"])
            checks["matched_source_id_preserved"] = bool(existing_start["matched_source_ids"]) and set(existing_start["matched_source_ids"]).issubset(existing_end["matched_source_ids"])
            checks["cli_matched_source_id"] = measurements[0]["stdout_result"].get("source_id") in existing_start["matched_source_ids"]
            if existing_start["prior_partial"]:
                checks["resumed_partial_source"] = measurements[0]["stdout_result"].get("resumed") is True
            database["correct"] = all(checks.values())
    except (sqlite3.Error, RuntimeError) as error:
        database = {"correct": False, "error": str(error)}
    receipt = {"schema": "loom.offline_import_measurement/1", "command": command, "baseline": args.baseline,
        "binary": binary_identity,
        "fixture": fixture, "measurements": measurements, "database": database,
        "verification_elapsed_seconds": time.monotonic() - verification_started,
        "caveat": "Peak RSS is the CLI process including source snapshotting, interpretation, audit, and JSON report output; file-system page cache is excluded."}
    if existing_start is not None:
        receipt["existing_data_start_counts"] = existing_start
        receipt["existing_data_dir"] = str(directory.resolve())
    (args.result_dir / "measurement.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))
    return 0 if database["correct"] and all(run["exit_code"] == 0 for run in measurements) else 1


if __name__ == "__main__":
    raise SystemExit(main())
