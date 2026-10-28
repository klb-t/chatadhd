#!/usr/bin/env python3
"""Cheap, model-free check: which finished automation runs produced nothing?

Reads docs/reports/runs/runs.jsonl (last record per run_id) and each run's own JSON.
A finished run counts as EMPTY when it has no commits, decisions, escalations,
applied markers, cards or a non-empty summary. Output lists per routine how many
of the last N finished runs were empty, so a routine that always comes out empty
gets attention. Advisory only: exit code 0 always. Presets: --last 10, --flag 5.
"""
import argparse, json, pathlib, collections

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = {"commits", "decisions", "escalations", "markers_applied", "cards", "original_ideas", "advisories"}

def is_empty(run: dict) -> bool:
    if any(run.get(k) for k in DATA):
        return False
    summ = run.get("summary")
    if isinstance(summ, dict):
        vals = [v for k, v in summ.items() if k not in ("note", "session") and v not in (None, 0, "", [], {})]
        return not vals
    if isinstance(summ, str):
        return summ.strip().lower() in ("", "pusta", "empty", "none", "nic", "no change", "brak zmian")
    return not summ

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--last", type=int, default=10, help="how many finished runs per routine to look at (preset 10)")
    ap.add_argument("--flag", type=int, default=5, help="flag a routine when at least this many of them are empty (preset 5)")
    a = ap.parse_args()
    latest = {}
    for line in (ROOT / "docs/reports/runs/runs.jsonl").read_text().splitlines():
        if line.strip():
            r = json.loads(line); latest[r["run_id"]] = r
    done = [r for r in latest.values() if r.get("state") == "done"]
    done.sort(key=lambda r: r.get("finished") or "")
    by_routine = collections.defaultdict(list)
    for r in done:
        by_routine[r.get("routine", "?")].append(r)
    print(f"{'routine':<18}{'empty':>6}{'of':>5}  flag")
    for routine, runs in sorted(by_routine.items()):
        recent = runs[-a.last:]
        empty = 0
        for r in recent:
            path = ROOT / r.get("path", "")
            try:
                full = json.loads(path.read_text())
            except (OSError, ValueError):
                full = {}
            empty += is_empty(full)
        flag = "<- look at this" if len(recent) and empty >= a.flag else ""
        print(f"{routine:<18}{empty:>6}{len(recent):>5}  {flag}")

if __name__ == "__main__":
    main()
