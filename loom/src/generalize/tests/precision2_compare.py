#!/usr/bin/env python3
"""Read-only comparison of completed public knowledge_eval runs.

Never runs Loom, builds, modifies a database, or scans a source repository.
Expected invocation uses the SAME knowledge_eval.py for both CLIs, the same
synthetic exports, and the e410 baseline repository for BOTH selfhost calls.
All tolerated differences are narrowly named bookkeeping identities, recorded
alongside the original byte digests. Content/evidence dates, confidence,
selection, source text, units, relations, parameters and ordering are preserved.
Stage input/output hashes are reported verbatim, never rewritten.

Example:
  python3 precision2_compare.py --synthetic-before before-card.json \
    --synthetic-after after-card.json --selfhost-before before-products \
    --selfhost-after after-products --out comparison.json
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sqlite3

BASE_PIN = "e4109df7e4af22b461def5f7d62e268d9b9a8825"


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def digest(value):
    return hashlib.sha256(value).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def json_value(value):
    if isinstance(value, bytes):
        return {"sql_blob_hex": value.hex()}
    return value


def rows_digest(rows):
    ordered = sorted(canonical(row) for row in rows)
    return digest(b"\n".join(ordered))


def quote(name):
    return '"' + name.replace('"', '""') + '"'


CAT_BOOKKEEPING = {
    "loom_cat_units": {"created"},
    "loom_cat_profiles": {"created"},
    "loom_cat_overrides": {"created"},
    "loom_cat_imports": {"created", "task_id"},
    "loom_cat_sources": {"scanned_at"},
    "loom_cat_checkpoint": {"updated"},
}


def database_snapshot(path, source_root=None):
    """Keep body bytes and indexed fields; only named SQL bookkeeping differs.

    Products have explicit metadata-only identity projection validated against
    Product::make_id. Runs are compared by status and complete summary.counts;
    raw run metadata rows remain hashed and recorded independently.
    """
    path = Path(path).resolve()
    uri = path.as_uri() + "?mode=ro"
    source_root = str(Path(source_root).resolve()) if source_root else None
    output = {"path": str(path), "tables": {}, "normalizations": {}, "runs": []}
    with sqlite3.connect(uri, uri=True) as connection:
        connection.execute("PRAGMA query_only=ON")
        tables = [row[0] for row in connection.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND "
            "(name LIKE 'loom_kb_%' OR name LIKE 'loom_cat_%') ORDER BY name")]
        if "loom_kb_runs" not in tables:
            raise ValueError(f"missing knowledge run table in {path}")
        for table in tables:
            columns = [row[1] for row in connection.execute(f"PRAGMA table_info({quote(table)})")]
            raw = [dict(zip(columns, map(json_value, row))) for row in
                   connection.execute(f"SELECT * FROM {quote(table)}")]
            logical = []
            removals = set(CAT_BOOKKEEPING.get(table, set()))
            if "run_id" in columns:
                removals.add("run_id")
            for original in raw:
                row = {key: value for key, value in original.items() if key not in removals}
                if table == "loom_kb_runs":
                    summary = json.loads(original["summary"])
                    if summary.get("run") != original["run_id"]:
                        raise ValueError("knowledge-run summary has inconsistent run identity")
                    row = {"status": original["status"], "summary_counts": summary["counts"]}
                    output["runs"].append({key: original.get(key) for key in
                        ("run_id", "pack_hash", "archive_run_id", "created")})
                elif table == "loom_kb_products":
                    body = json.loads(row["body"])
                    if body["id"] != original["id"] or body["run"] != original["run_id"]:
                        raise ValueError("product body disagrees with SQL identity")
                    expected = "pd_" + digest("\x1f".join(
                        (body["kind"], body["instance"], body["run"])).encode())[:16]
                    if body["id"] != expected:
                        raise ValueError("product identity is not the documented kind/instance/run hash")
                    if not isinstance(body.get("artifact"), str):
                        raise ValueError("invalid product artifact identity")
                    for key in ("id", "run", "artifact"):
                        body.pop(key)
                    row.pop("id")
                    row["body"] = canonical(body).decode()
                elif table == "loom_cat_sources" and source_root:
                    value = row.get("path", "")
                    if value == source_root or value.startswith(source_root + "/"):
                        row["path"] = "<exact-selfhost-source-root>" + value[len(source_root):]
                logical.append(row)
            output["tables"][table] = {
                "columns": columns, "rows": len(raw),
                "raw_sha256": rows_digest(raw), "logical_sha256": rows_digest(logical),
            }
            if removals or table in {"loom_kb_runs", "loom_kb_products", "loom_cat_sources"}:
                output["normalizations"][table] = {
                    "sql_columns_removed": sorted(removals),
                    "special_projection": ("status + complete summary.counts; full raw row digest retained"
                        if table == "loom_kb_runs" else
                        "verified product id/run and artifact metadata only; all semantic body fields preserved"
                        if table == "loom_kb_products" else
                        "exact source-root prefix only; relative path/content identity retained"
                        if table == "loom_cat_sources" and source_root else None),
                }
    return output


def compare_databases(before, after, before_root=None, after_root=None):
    left, right = database_snapshot(before, before_root), database_snapshot(after, after_root)
    tables = {}
    for table in sorted(set(left["tables"]) | set(right["tables"])):
        a, b = left["tables"].get(table), right["tables"].get(table)
        tables[table] = {"before": a, "after": b,
            "raw_equal": a is not None and b is not None and a == b,
            "logical_equal": a is not None and b is not None and
                a["columns"] == b["columns"] and a["rows"] == b["rows"] and
                a["logical_sha256"] == b["logical_sha256"]}
    return {"before": left, "after": right, "tables": tables,
            "logical_equal": all(row["logical_equal"] for row in tables.values()),
            "raw_equal": all(row["raw_equal"] for row in tables.values())}


def compare_products(before, after, before_run, after_run, selfhost=False):
    before, after = Path(before), Path(after)
    # These three files are injected by realrun.copy_products and compared as
    # metadata separately. No other files, hidden names or directories ignored.
    metadata = {"input_manifest.json", "run_result.json", ".loom-archive"} if selfhost else set()
    def collect(directory, run):
        if not directory.is_dir() or not (directory / "SELF.md").is_file():
            raise ValueError(f"missing completed products directory/SELF.md: {directory}")
        result = {}
        for path in sorted(directory.rglob("*")):
            if path.is_symlink():
                raise ValueError(f"unexpected product symlink: {path}")
            if not path.is_file():
                continue
            relative = path.relative_to(directory).as_posix()
            if relative in metadata:
                continue
            content = path.read_bytes()
            normalized = content
            replaced = 0
            if relative == "SELF.md":
                sentence = f"Generated by Loom's knowledge layer for run `{run}`.\n".encode()
                if content.count(sentence) != 1:
                    raise ValueError("SELF.md does not contain exactly the expected generated-run sentence")
                normalized = content.replace(sentence,
                    b"Generated by Loom's knowledge layer for run `<knowledge-run>`.\n", 1)
                replaced = 1
            result[relative] = {"bytes": len(content), "sha256": digest(content),
                "logical_sha256": digest(normalized), "run_sentence_replacements": replaced}
        return result
    left, right = collect(before, before_run), collect(after, after_run)
    names = sorted(set(left) | set(right))
    rows = {name: {"before": left.get(name), "after": right.get(name),
        "raw_equal": name in left and name in right and left[name]["sha256"] == right[name]["sha256"],
        "logical_equal": name in left and name in right and
            left[name]["logical_sha256"] == right[name]["logical_sha256"]} for name in names}
    return {"files": len(names), "before_files": len(left), "after_files": len(right),
            "raw_equal": all(row["raw_equal"] for row in rows.values()),
            "logical_equal": all(row["logical_equal"] for row in rows.values()), "per_file": rows}


def single_run(db):
    with sqlite3.connect(Path(db).resolve().as_uri() + "?mode=ro", uri=True) as connection:
        rows = connection.execute("SELECT run_id FROM loom_kb_runs").fetchall()
    if len(rows) != 1:
        raise ValueError("expected exactly one knowledge run in each fresh evaluation database")
    return rows[0][0]


def compare_synthetic(before_path, after_path):
    left, right = load(before_path), load(after_path)
    a, b = json.loads(json.dumps(left)), json.loads(json.dumps(right))
    removed = {"before_work_dir": a.pop("work_dir"), "after_work_dir": b.pop("work_dir")}
    # Exact known catalog score operation identity; no recursive run/id scrub.
    for side, card in (("before", a), ("after", b)):
        score = card.get("stages", {}).get("catalog", {}).get("score", {})
        if "run_id" in score:
            if not isinstance(score["run_id"], str) or not score["run_id"].startswith("run_"):
                raise ValueError("unexpected catalog score operation identity")
            removed[side + "_catalog_score_run_id"] = score.pop("run_id")
    variants = {}
    for variant in ("a", "a2", "b"):
        ba = Path(left["work_dir"]) / variant / "chatadhd.db"
        bb = Path(right["work_dir"]) / variant / "chatadhd.db"
        row = {"db": compare_databases(ba, bb)}
        row["products"] = compare_products(Path(left["work_dir"]) / (variant + "_out"),
            Path(right["work_dir"]) / (variant + "_out"), single_run(ba), single_run(bb))
        row["logical_equal"] = row["db"]["logical_equal"] and row["products"]["logical_equal"]
        variants[variant] = row
    card_equal = canonical(a) == canonical(b)
    return {"before_card_sha256": digest(Path(before_path).read_bytes()),
        "after_card_sha256": digest(Path(after_path).read_bytes()),
        "normalizations": removed, "scorecard_logical_equal": card_equal,
        "normalized_before_sha256": digest(canonical(a)), "normalized_after_sha256": digest(canonical(b)),
        "variants": variants, "logical_equal": card_equal and all(row["logical_equal"] for row in variants.values())}


def compare_selfhost(before, after):
    before, after = Path(before), Path(after)
    ma, mb = load(before / "input_manifest.json"), load(after / "input_manifest.json")
    manifest_equal = canonical(ma) == canonical(mb)
    pinned = ma.get("revision") == BASE_PIN and mb.get("revision") == BASE_PIN
    ra, rb = load(before / "run_result.json"), load(after / "run_result.json")
    wa, wb = Path(ra["evaluation"]["work_dir"]), Path(rb["evaluation"]["work_dir"])
    if canonical(ra["evaluation"]["inputs"]) != canonical(ma) or canonical(rb["evaluation"]["inputs"]) != canonical(mb):
        raise ValueError("copied source manifest differs from actual run input manifest")
    if single_run(wa / "data/chatadhd.db") != ra["run"] or single_run(wb / "data/chatadhd.db") != rb["run"]:
        raise ValueError("selfhost report disagrees with the sole fresh database run")
    db = compare_databases(wa / "data/chatadhd.db", wb / "data/chatadhd.db", wa / "sources", wb / "sources")
    products = compare_products(before, after, ra["run"], rb["run"], selfhost=True)
    stages_a, stages_b = ra["stages"], rb["stages"]
    names_a, names_b = [s["stage"] for s in stages_a], [s["stage"] for s in stages_b]
    if len(names_a) != len(set(names_a)) or len(names_b) != len(set(names_b)):
        raise ValueError("duplicate stage entry would hide ordering/identity differences")
    order_equal = names_a == names_b
    stats_a, stats_b = {}, {}
    catalog_ids = {}
    for side, stages, target in (("before", stages_a, stats_a), ("after", stages_b, stats_b)):
        for stage in stages:
            stats = json.loads(json.dumps(stage["stats"]))
            if stage["stage"] == "catalog" and "run_id" in stats.get("score", {}):
                catalog_ids[side] = stats["score"].pop("run_id")
            target[stage["stage"]] = stats
    stats_equal = canonical(stats_a) == canonical(stats_b)
    outputs_a = {s["stage"]: s["output_hash"] for s in stages_a}
    outputs_b = {s["stage"]: s["output_hash"] for s in stages_b}
    return {"source_manifest_exact_equal": manifest_equal, "source_revision_pinned": pinned,
        "source_files": len(ma["files"]), "source_bytes": sum(row["bytes"] for row in ma["files"]),
        "manifest_before_sha256": digest(canonical(ma)), "manifest_after_sha256": digest(canonical(mb)),
        "run_metadata": {"before": {key: ra.get(key) for key in ("run", "pack_hash", "task_id", "status", "error")},
                         "after": {key: rb.get(key) for key in ("run", "pack_hash", "task_id", "status", "error")}},
        "catalog_score_run_id_normalizations": catalog_ids,
        "stage_order_exact_equal": order_equal, "stage_order_before": names_a, "stage_order_after": names_b,
        "stage_stats_logical_equal": stats_equal, "stage_stats_before": stats_a, "stage_stats_after": stats_b,
        "raw_stage_input_hashes": {"before": {s["stage"]: s["input_hash"] for s in stages_a},
                                   "after": {s["stage"]: s["input_hash"] for s in stages_b}},
        "raw_stage_output_hashes": {"before": outputs_a, "after": outputs_b},
        "raw_stage_output_hashes_equal": outputs_a == outputs_b,
        "db": db, "products": products,
        "logical_equal": manifest_equal and pinned and order_equal and stats_equal and db["logical_equal"] and
            products["logical_equal"] and ra["status"] == rb["status"] == "done" and
            ra["error"] is None and rb["error"] is None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--synthetic-before", type=Path)
    parser.add_argument("--synthetic-after", type=Path)
    parser.add_argument("--selfhost-before", type=Path)
    parser.add_argument("--selfhost-after", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error("--out must be new; never overwrite a previous positive or negative comparison")
    result = {"schema": "loom.precision2_default_parity/1", "baseline_revision": BASE_PIN,
        "script_sha256": digest(Path(__file__).read_bytes()),
        "scope": "Public DEV and sanitized fixed-HEAD sources; default parity, not improved accuracy",
        "limitations": ["Stage input hashes include changed pack/run/config identities and are reported raw.",
            "Product run/artifact identities and SQL bookkeeping have named projections; source/body/evidence bytes stay exact.",
            "No blanket JSON run/id/date/hash scrub; unexpected differences fail comparison.",
            "Raw SQLite file bytes include implementation/WAL metadata and are not equated to logical rows."]}
    try:
        if bool(args.synthetic_before) != bool(args.synthetic_after):
            raise ValueError("both synthetic scorecards are required")
        if bool(args.selfhost_before) != bool(args.selfhost_after):
            raise ValueError("both selfhost product directories are required")
        if not args.synthetic_before and not args.selfhost_before:
            raise ValueError("supply at least one completed before/after evaluation pair")
        if args.synthetic_before:
            result["synthetic"] = compare_synthetic(args.synthetic_before, args.synthetic_after)
        if args.selfhost_before:
            result["selfhost"] = compare_selfhost(args.selfhost_before, args.selfhost_after)
        result["passed"] = all(result[name]["logical_equal"] for name in ("synthetic", "selfhost") if name in result)
    except Exception as error:
        result["passed"] = False
        result["error"] = f"{type(error).__name__}: {error}"
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"passed": result["passed"], "out": str(args.out), "error": result.get("error")}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
