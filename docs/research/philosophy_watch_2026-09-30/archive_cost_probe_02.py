#!/usr/bin/env python3
"""No-network adversarial numeric/SQLite probes for existing offline estimator."""
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import sqlite3
import sys
from tempfile import TemporaryDirectory

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from loom.tools.eval import archive_cost as cost


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def clean(x):
    if isinstance(x, float) and not math.isfinite(x):
        return {"nonfinite": repr(x)}
    if isinstance(x, dict):
        return {str(k): clean(v) for k, v in x.items()}
    if isinstance(x, (tuple, list)):
        return [clean(v) for v in x]
    return x


STATS = {"conversations": 10, "chars_active": 4000, "chars_versions": 0}
PRICES = {"as_of": "authored-planning-example", "batch_discount": 0.5,
          "models": {"authored-frontier": {"input": 2.0, "output": 8.0, "cache_read": 1.0}}}


def nominal(**changes):
    return cost.estimate(dict(STATS), json.loads(json.dumps(PRICES)), **changes)


def make_db(path, rows):
    with sqlite3.connect(path) as c:
        c.execute("CREATE TABLE messages(conv_id TEXT, role TEXT, status TEXT, text TEXT)")
        c.executemany("INSERT INTO messages VALUES(?,?,?,?)", rows)


def sqlite_probe(filename, rows):
    with TemporaryDirectory(prefix="philosophy-cost-") as folder:
        base = Path(folder); path = base / filename
        make_db(path, rows)
        before = {p.name: sha(p) for p in base.iterdir() if p.is_file()}
        try:
            result = {"returned": cost.archive_stats(path)}
        except Exception as exc:
            result = {"exception_type": type(exc).__name__, "exception_message": str(exc)}
        after = {p.name: sha(p) for p in base.iterdir() if p.is_file()}
        return {"filename": filename, "input_rows": rows, "files_before": before,
                "files_after": after, "source_hash_unchanged": after.get(filename) == before[filename],
                "unexpected_created_files": sorted(set(after) - set(before)), **result}


def main():
    preflight = {"schema": "loom.philosophy_watch_archive_cost_freeze/1",
                 "frozen_at_utc": datetime.now(timezone.utc).isoformat(),
                 "files_sha256": {str(p.relative_to(ROOT)): sha(p) for p in
                                  (Path(cost.__file__), cost.PRICING, Path(__file__), HERE / "PROTOCOL_02_ARCHIVE_COST.md")},
                 "paid_requests": 0, "owner_archive_reads": 0, "canonical_writes": 0}
    with (HERE / "ARCHIVE_COST_FREEZE_02.json").open("x", encoding="utf-8") as f:
        json.dump(preflight, f, ensure_ascii=False, indent=2); f.write("\n")
    rows = []

    def capture(ident, expected, fn):
        try:
            value = fn(); response = {"returned": clean(value)}
        except Exception as exc:
            response = {"exception_type": type(exc).__name__, "exception_message": str(exc)}
        rows.append({"id": ident, "expected_domain_or_assumption": expected, **response})

    capture("nominal", "finite positive planning estimate", lambda: nominal())
    capture("zero_output_zero_prefix", "valid finite zero output/prefix", lambda: nominal(output_ratio=0, prefix_tokens=0))
    capture("negative_output_ratio", "reject negative count ratio, no maximum ratio restriction", lambda: nominal(output_ratio=-20))
    capture("nan_output_ratio", "reject nonfinite count ratio", lambda: nominal(output_ratio=float("nan")))
    capture("infinite_output_ratio", "reject nonfinite count ratio", lambda: nominal(output_ratio=float("inf")))
    capture("negative_prefix_count", "reject negative token count", lambda: nominal(prefix_tokens=-100000))
    capture("boolean_prefix_count", "boolean is not an integer token count", lambda: nominal(prefix_tokens=True))
    capture("nan_fraction", "reject nonfinite fraction", lambda: nominal(fraction=float("nan")))
    capture("infinite_fraction", "reject out-of-range fraction", lambda: nominal(fraction=float("inf")))
    capture("zero_fraction", "declared existing API fraction domain is (0,1]", lambda: nominal(fraction=0))

    def price_case(field, value):
        prices = json.loads(json.dumps(PRICES)); prices["models"]["authored-frontier"][field] = value
        return cost.estimate(dict(STATS), prices, prefix_tokens=0)

    capture("negative_price", "reject negative price", lambda: price_case("input", -100))
    capture("nan_price", "reject nonfinite price", lambda: price_case("input", float("nan")))
    capture("boolean_conversation_count", "boolean is not an archive count",
            lambda: cost.estimate({**STATS, "conversations": True}, PRICES))
    capture("fractional_conversation_selection", "fraction times count is an expectation, not actual selected integer inventory",
            lambda: nominal(fraction=0.01))
    capture("theoretical_cached_first_prefix", "cache_read used on every conversation; no first write/unavailable cache modeling", lambda: nominal())
    capture("theoretical_uniform_batch_discount", "all model bands discounted without endpoint batch capability check", lambda: nominal())
    capture("sqlite_null_status", "NULL status must be explicitly accounted for or reported, not silently omitted",
            lambda: sqlite_probe("ordinary.db", [("c", "user", "active", "abcd"), ("c", "user", None, "hidden")]))
    capture("sqlite_question_mark_filename", "read correct literal filename read-only and create no other file",
            lambda: sqlite_probe("literal?archive.db", [("c", "user", "active", "abcd")]))
    capture("sqlite_hash_filename", "read correct literal filename read-only and create no other file",
            lambda: sqlite_probe("literal#archive.db", [("c", "user", "active", "abcd")]))
    capture("excluded_only_conversation", "count eligible reading-conversation denominator separately from all nondeleted conversations",
            lambda: sqlite_probe("excluded.db", [("excluded-only", "user", "excluded", "abcd")]))
    out = {"schema": "loom.philosophy_watch_archive_cost_results/1",
           "finished_at_utc": datetime.now(timezone.utc).isoformat(),
           "planned_probes": len(rows), "probes": rows,
           "source_hash_unchanged": sha(Path(cost.__file__)) == preflight["files_sha256"][str(Path(cost.__file__).relative_to(ROOT))],
           "scope": "offline numeric/data boundary audit; not model quality or actual billed usage"}
    with (HERE / "ARCHIVE_COST_FIRST_RESULTS.json").open("x", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2, allow_nan=False); f.write("\n")
    print(json.dumps({"planned_probes": len(rows), "source_hash_unchanged": out["source_hash_unchanged"],
                      "numeric_exceptions": sum("exception_type" in r for r in rows)}))


if __name__ == "__main__":
    main()
