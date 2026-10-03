"""Spot checks of the entity / paradigm layer of one knowledge run.

  python3 -m kbeval.spot RUN_DB_DIR/chatadhd.db --seed 20260929 --n 20

Lists a reproducible random sample (fixed seed, sorted ids) of

  * project entities (label, kind, units the entity is mentioned in),
  * project / paradigm instances (subject, paradigm, score, coverage, marks),
  * the computed current version and status of the sampled subjects,

so that the same sample can be judged before and after a change. Nothing here
is a metric: it is the material of the human spot check.
"""
from __future__ import annotations

import argparse
import json
import random
import sqlite3
from pathlib import Path
from typing import Any


def latest_run(con: sqlite3.Connection) -> str:
    row = con.execute("SELECT run_id FROM loom_kb_runs ORDER BY created DESC LIMIT 1").fetchone()
    return row[0]


def bodies(con: sqlite3.Connection, table: str, run: str, where: str = "", args: tuple = ()) -> list[dict[str, Any]]:
    q = f"SELECT body FROM {table} WHERE run_id = ?" + (f" AND {where}" if where else "") + " ORDER BY id"
    return [json.loads(r[0]) for r in con.execute(q, (run, *args))]


def sample(items: list[Any], n: int, seed: int) -> list[Any]:
    rng = random.Random(seed)
    items = sorted(items, key=lambda x: json.dumps(x, sort_keys=True))
    return rng.sample(items, min(n, len(items)))


def report(db: Path, n: int, seed: int) -> str:
    con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    run = latest_run(con)
    ents = {e["id"]: e for e in bodies(con, "loom_kb_entities", run)}
    active_projects = [e for e in ents.values() if e["kind"] == "project" and e.get("status", "active") == "active"]
    inst = bodies(con, "loom_kb_instances", run)
    claims = bodies(con, "loom_kb_claims", run)
    by_subject: dict[str, list[dict[str, Any]]] = {}
    for c in claims:
        by_subject.setdefault(c["subject"], []).append(c)
    out: list[str] = [f"# Spot check of {db} (run {run}, seed {seed})", ""]
    out.append(f"project entities (active): {len(active_projects)}; instances: {len(inst)}; claims: {len(claims)}")
    out.append("")
    out.append(f"## {n} project entities")
    for e in sample([{"id": e["id"], "label": e["label"]} for e in active_projects], n, seed):
        ent = ents[e["id"]]
        units = {c["value"] for c in by_subject.get(e["id"], []) if c["predicate"] == "mentioned_in"}
        out.append(f"- `{ent['label']}` ({len(ent.get('aliases', []))} aliases, {len(units)} units)")
    out.append("")
    out.append(f"## {n} project/paradigm instances")
    proj = [i for i in inst if i.get("paradigm_kind") == "project_kind"]
    for i in sample([{"id": i["id"]} for i in proj], n, seed):
        x = next(v for v in proj if v["id"] == i["id"])
        cov = x.get("coverage", {})
        subj = x.get("subject_label") or x.get("subject")
        versions = sorted({c.get("value") for c in by_subject.get(x["subject"], []) if c["predicate"] == "has_version" and isinstance(c.get("value"), str)})
        cur = [c for c in by_subject.get(x["subject"], []) if c["predicate"] == "has_version"
               and (c.get("qualifiers") or {}).get("extra", {}).get("slot") == "current_version"]
        out.append(f"- `{subj}` -> **{x['paradigm']}** score {x.get('score')}; observed {cov.get('observed')}, "
                   f"absent {cov.get('absent')}, required {cov.get('required_filled')}/{cov.get('required')}; "
                   f"marks {cov.get('match', cov.get('anchor', ''))}; versions {versions[:6]}; "
                   f"computed current {[c.get('value') for c in cur]}")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("db")
    ap.add_argument("--n", type=int, default=20)
    ap.add_argument("--seed", type=int, default=20260929)
    a = ap.parse_args()
    print(report(Path(a.db), a.n, a.seed))
