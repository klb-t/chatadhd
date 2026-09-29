"""Reproducible timing + output-identity harness of the knowledge pipeline.

  stage    freeze a repository input (tracked HEAD text files) into a directory
  run      run `loom knowledge run` over a frozen input with LOOM_STAGE_PROFILE=1,
           collect wall time, per-sub-phase timers/counters (stderr "[prof]" and
           "[count]" lines written by the extract/resolve/generalize stages) and a
           digest of everything the run produced:
             - the stage output hashes,
             - sha256 of every product file,
             - sha256 of every loom_kb_* / loom_cat_* row (sorted, timestamps of
               bookkeeping tables excluded)
  compare  compare two `run` reports: identical digests? speed-ups per phase

The digest is what makes a performance change verifiable as output-identical.
Absolute source paths participate in run identity, so a frozen input must be
reused at the SAME path for two runs that are to be compared.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import sqlite3
import subprocess
import time
from pathlib import Path
from typing import Any

from . import realrun
from .common import new_workspace

PROF = re.compile(r"^\[prof\] (\S+) ([0-9.]+) ms$")
COUNT = re.compile(r"^\[count\] (\S+) (-?\d+)$")


def stage(repo: Path, dest: Path) -> dict[str, Any]:
    manifest = realrun.stage_snapshot(repo, dest)
    (dest.parent / (dest.name + ".manifest.json")).write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    return {"revision": manifest["revision"], "files": len(manifest["files"])}


def sha_file(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# Bookkeeping columns that legitimately differ between two runs.
VOLATILE_COLUMNS = {"created_at", "updated_at", "started_at", "finished_at", "ts", "time", "elapsed_ms"}


def db_digest(db: Path) -> dict[str, Any]:
    con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    tables = [r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table' AND (name LIKE 'loom_kb_%' OR name LIKE 'loom_cat_%') ORDER BY name")]
    per: dict[str, Any] = {}
    total = hashlib.sha256()
    for t in tables:
        cols = [r[1] for r in con.execute(f"PRAGMA table_info({t})") if r[1] not in VOLATILE_COLUMNS]
        if not cols:
            continue
        rows = con.execute(f"SELECT {', '.join(cols)} FROM {t}").fetchall()
        enc = sorted(json.dumps(r, ensure_ascii=False, default=str, sort_keys=True) for r in rows)
        h = hashlib.sha256("\n".join(enc).encode("utf-8")).hexdigest()
        per[t] = {"rows": len(rows), "sha256": h}
        total.update(t.encode() + b"\0" + h.encode() + b"\0")
    con.close()
    return {"tables": per, "sha256": total.hexdigest()}


def products_digest(out: Path) -> dict[str, Any]:
    files = {}
    for p in sorted(out.rglob("*")):
        if p.is_file():
            files[str(p.relative_to(out))] = sha_file(p)
    total = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()
    return {"files": len(files), "sha256": total, "per_file": files}


def run(loom: str, config: dict[str, Any], work: Path, label: str = "run") -> dict[str, Any]:
    run_dir = new_workspace(work, label + "-")
    data = run_dir / "data"
    data.mkdir(parents=True)
    out_dir = run_dir / "products"
    cfg = {**config, "out_dir": str(out_dir)}
    cfg_path = data / "knowledge_config.json"
    cfg_path.write_text(json.dumps(cfg, ensure_ascii=False, indent=1), encoding="utf-8")
    cmd = [loom, "--json", "--quiet", "--data-dir", str(data), "knowledge", "run", "--config", str(cfg_path)]
    env = {**os.environ, "CHATADHD_DATA": str(data), "LOOM_STAGE_PROFILE": "1"}
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=7200)
    wall = time.perf_counter() - t0
    try:
        res = json.loads(p.stdout)
    except json.JSONDecodeError as e:
        raise RuntimeError(f"no JSON (exit {p.returncode}): {p.stderr[-2000:]}") from e
    if res.get("status") != "done":
        raise RuntimeError(f"status {res.get('status')}: {res.get('error')}\n{p.stderr[-2000:]}")
    timers: dict[str, float] = {}
    counters: dict[str, int] = {}
    for line in p.stderr.splitlines():
        if m := PROF.match(line):
            timers[m.group(1)] = timers.get(m.group(1), 0.0) + float(m.group(2))
        elif m := COUNT.match(line):
            counters[m.group(1)] = counters.get(m.group(1), 0) + int(m.group(2))
    report = {
        "label": label,
        "run": res["run"],
        "wall_s": round(wall, 3),
        "timers_ms": {k: round(v, 1) for k, v in sorted(timers.items())},
        "counters": dict(sorted(counters.items())),
        "stage_hashes": {s["stage"]: s["output_hash"] for s in res["stages"]},
        "stage_stats": {s["stage"]: s["stats"] for s in res["stages"]},
        "db": db_digest(data / "chatadhd.db"),
        "products": products_digest(out_dir),
        "work_dir": str(run_dir),
    }
    return report


def identical(a: dict[str, Any], b: dict[str, Any]) -> dict[str, Any]:
    ta, tb = a["db"]["tables"], b["db"]["tables"]
    return {
        "run_ids": a["run"] == b["run"],
        "stage_hashes": a["stage_hashes"] == b["stage_hashes"],
        "db": a["db"]["sha256"] == b["db"]["sha256"],
        "db_tables_differing": sorted(t for t in set(ta) | set(tb) if ta.get(t) != tb.get(t)),
        "products": a["products"]["sha256"] == b["products"]["sha256"],
        "products_differing": sorted(f for f in set(a["products"]["per_file"]) | set(b["products"]["per_file"])
                                     if a["products"]["per_file"].get(f) != b["products"]["per_file"].get(f))[:20],
    }


def compare(a: dict[str, Any], b: dict[str, Any]) -> str:
    ident = identical(a, b)
    lines = [f"identical: {json.dumps(ident)}", f"wall: {a['wall_s']} s -> {b['wall_s']} s ({a['wall_s'] / max(b['wall_s'], 1e-9):.2f}x)"]
    for k in sorted(set(a["timers_ms"]) | set(b["timers_ms"])):
        x, y = a["timers_ms"].get(k), b["timers_ms"].get(k)
        sp = f"{x / y:.2f}x" if x and y else "-"
        lines.append(f"  {k:44s} {x if x is not None else '-':>12} ms -> {y if y is not None else '-':>12} ms  {sp}")
    return "\n".join(lines)
