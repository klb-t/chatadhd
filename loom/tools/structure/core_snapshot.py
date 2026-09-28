#!/usr/bin/env python3
"""Read a single committed SQLite view of an explicit native knowledge run.

This exports source model bodies for experiments; it creates neither a new
knowledge store nor an archival substitute for the original source blobs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sqlite3

VERSION = "core-readonly-snapshot/1"
BODY_TABLES = ("observations", "entities", "claims", "principles", "operators",
               "morphisms", "instances", "areas", "decisions", "forks",
               "status_records", "predictions", "models", "products")
REQUIRED = {"observations", "entities", "claims"}


def _parse(value: str) -> dict:
    def invalid(token):
        raise ValueError("non-finite JSON value " + token)
    result = json.loads(value, parse_constant=invalid)
    if not isinstance(result, dict):
        raise ValueError("canonical body must be a JSON object")
    return result


def snapshot(database: str | Path, run_id: str, *, max_rows: int = 1_000_000) -> dict:
    """Export one run; missing data and row limits fail instead of truncating.

    A normal SQLite read-only connection sees committed WAL pages. BEGIN pins
    one read transaction across all tables. immutable=1 would be incorrect for
    a live WAL database and is deliberately not used.
    """
    if not isinstance(run_id, str) or not run_id.strip():
        raise ValueError("an explicit nonempty run_id is required")
    if type(max_rows) is not int or max_rows < 1:
        raise ValueError("max_rows must be a positive integer")
    path = Path(database).resolve(strict=True)
    if not path.is_file():
        raise ValueError("database must be an existing file")
    con = sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)
    con.row_factory = sqlite3.Row
    try:
        con.execute("PRAGMA query_only=ON")
        con.execute("BEGIN")
        tables = {r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        if "loom_kb_runs" not in tables:
            raise ValueError("database has no native knowledge runs")
        row = con.execute("SELECT * FROM loom_kb_runs WHERE run_id=?", (run_id,)).fetchone()
        if row is None:
            raise ValueError("unknown knowledge run " + run_id)
        run = dict(row)
        for key in ("inputs", "summary"):
            run[key] = _parse(run[key])
        output = {"run_id": run_id, "run": run}
        missing, counts = [], {}
        remaining = max_rows

        def read(sql):
            nonlocal remaining
            rows = con.execute(sql, (run_id,)).fetchmany(remaining + 1)
            if len(rows) > remaining:
                raise ValueError("snapshot exceeds max_rows; no partial result returned")
            remaining -= len(rows)
            return rows

        for name in BODY_TABLES:
            table = "loom_kb_" + name  # closed constant list, never caller SQL
            if table not in tables:
                if name in REQUIRED:
                    raise ValueError("missing required table " + table)
                missing.append(table)
                output[name] = []
                continue
            bodies = []
            for item in read("SELECT id,body FROM " + table + " WHERE run_id=? ORDER BY id"):
                body = _parse(item["body"])
                if body.get("id") != item["id"]:
                    raise ValueError("indexed id differs from body id in " + table)
                bodies.append(body)
            output[name] = bodies
            counts[name] = len(bodies)

        output["slot_values"] = []
        if "loom_kb_slot_values" in tables:
            rows = read("SELECT instance_id,slot,ord,body FROM loom_kb_slot_values "
                        "WHERE run_id=? ORDER BY instance_id,slot,ord")
            for item in rows:
                body = _parse(item["body"])
                if body.get("slot") != item["slot"]:
                    raise ValueError("indexed slot differs from slot body")
                if "instance" in body and body["instance"] != item["instance_id"]:
                    raise ValueError("indexed instance differs from slot body")
                output["slot_values"].append({**body, "instance": item["instance_id"]})
            counts["slot_values"] = len(rows)
        else:
            missing.append("loom_kb_slot_values")
        meta = dict(con.execute("SELECT key,value FROM loom_kb_meta")) if "loom_kb_meta" in tables else {}
        con.rollback()
    finally:
        con.close()
    canonical = json.dumps(output, ensure_ascii=False, sort_keys=True,
                           separators=(",", ":"), allow_nan=False).encode()
    output["snapshot"] = {
        "version": VERSION, "consistency": "single_sqlite_read_transaction_including_committed_wal",
        "schema_metadata": meta, "body_sha256": hashlib.sha256(canonical).hexdigest(),
        "counts": counts, "missing_optional_tables": missing,
        "run_completed": run.get("status") == "done", "max_rows": max_rows,
        "omitted_derived_indexes": ["aliases (in Entity.aliases)", "claim_support (in Claim.assessment.basis.support)"],
        "omitted_global_tables": ["judgements (run records replayed_seq)", "candidates", "policy_versions", "llm_cache"],
        "source_bytes_included": False, "semantics_validated": False,
        "meaning": "consistent source model bodies, not an independent correctness assessment or a full data backup",
    }
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("--run", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--max-rows", type=int, default=1_000_000)
    args = parser.parse_args()
    result = snapshot(args.database, args.run, max_rows=args.max_rows)
    # Never accidentally replace the database or another existing artifact.
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")


if __name__ == "__main__":
    main()
