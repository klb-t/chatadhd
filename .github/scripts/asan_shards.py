#!/usr/bin/env python3
"""Split one CTest manifest into shards described by data (.github/asan_shards.json).

The shard plan is data: shard -> list of test-name patterns (fnmatch globs; an exact
name is a pattern too) plus one "rest" shard that takes everything not assigned, so a
new test lands somewhere on its own. The number of shards is the length of the list.
A test matched by several shards belongs to the first one in file order.

Subcommands
  matrix  --legs LEGS --shards PLAN            print the GitHub matrix (JSON)
  select  --shards PLAN --shard S --manifest M print the ctest -R regex for shard S
  merge   --shards PLAN --manifest M EVIDENCE...  union check over the shards' evidence
  dryrun  --shards PLAN --durations D.json     estimate wall time per shard
"""
from __future__ import annotations

import argparse
from fnmatch import fnmatchcase
import json
from pathlib import Path
import re
import sys

DEFAULT_PLAN = Path(__file__).resolve().parents[1] / "asan_shards.json"


def load_plan(path: Path = DEFAULT_PLAN) -> dict:
    plan = json.loads(Path(path).read_text(encoding="utf-8"))
    shards = plan.get("shards")
    if not isinstance(shards, list) or not shards:
        raise ValueError("shard plan needs a nonempty 'shards' list")
    names = [s.get("name") for s in shards]
    if any(not isinstance(n, str) or not n for n in names) or len(set(names)) != len(names):
        raise ValueError("shard names must be unique nonempty strings")
    if sum(1 for s in shards if s.get("rest")) != 1:
        raise ValueError("exactly one shard must be the 'rest' shard")
    for s in shards:
        if s.get("rest") and s.get("tests"):
            raise ValueError("the rest shard takes what is left; it lists no tests")
        if not s.get("rest") and not isinstance(s.get("tests", []), list):
            raise ValueError(f"shard {s['name']}: 'tests' must be a list")
    return plan


def assign(plan: dict, test_names: list[str]) -> dict[str, list[str]]:
    """Every test lands in exactly one shard (first matching, else the rest shard)."""
    result: dict[str, list[str]] = {s["name"]: [] for s in plan["shards"]}
    rest = next(s["name"] for s in plan["shards"] if s.get("rest"))
    for name in test_names:
        for s in plan["shards"]:
            if not s.get("rest") and any(fnmatchcase(name, p) for p in s.get("tests", [])):
                result[s["name"]].append(name)
                break
        else:
            result[rest].append(name)
    return result


def advisories(plan: dict, test_names: list[str]) -> list[str]:
    """Non-blocking notes: patterns that match nothing, tests matched by several shards."""
    notes = []
    for s in plan["shards"]:
        for p in s.get("tests", []):
            if not any(fnmatchcase(n, p) for n in test_names):
                notes.append(f"advisory: shard {s['name']}: pattern {p!r} matches no test (renamed or removed?)")
    for n in test_names:
        owners = [s["name"] for s in plan["shards"] if not s.get("rest")
                  and any(fnmatchcase(n, p) for p in s.get("tests", []))]
        if len(owners) > 1:
            notes.append(f"advisory: {n} matches shards {owners}; the first one runs it")
    return notes


def ctest_regex(names: list[str]) -> str:
    return "^(" + "|".join(re.escape(n) for n in names) + ")$"


def manifest_names(manifest: dict) -> list[str]:
    return [t["name"] for t in manifest["tests"]]


def matrix(legs: list[dict], plan: dict) -> dict:
    """Expand legs that carry "sharded": true into one leg per shard."""
    include = []
    for leg in legs:
        if leg.get("sharded"):
            for s in plan["shards"]:
                item = {k: v for k, v in leg.items() if k != "sharded"}
                item.update(shard=s["name"], shards=len(plan["shards"]))
                item["name"] = f"{leg['preset']} ({leg['cxx']}) shard {s['name']}"
                include.append(item)
        else:
            include.append(dict(leg))
    return {"include": include}


def merge(plan: dict, full_names: list[str], evidence: list[dict]) -> list[str]:
    """Errors unless the shards' evidence together cover the whole manifest exactly once."""
    errors = []
    expected = assign(plan, full_names)
    seen: dict[str, str] = {}
    for ev in evidence:
        shard = ev.get("shard")
        if shard not in expected:
            errors.append(f"evidence for unknown shard {shard!r}")
            continue
        if not ev.get("valid"):
            errors.append(f"shard {shard}: evidence is not valid")
        got = sorted(ev.get("entry_names", []))
        if got != sorted(expected[shard]):
            errors.append(f"shard {shard}: executed entries differ from the plan "
                          f"(missing {sorted(set(expected[shard]) - set(got))[:5]}, "
                          f"extra {sorted(set(got) - set(expected[shard]))[:5]})")
        for n in got:
            if n in seen:
                errors.append(f"{n} executed in shards {seen[n]} and {shard}")
            seen[n] = shard
    for shard in expected:
        if not any(ev.get("shard") == shard for ev in evidence):
            errors.append(f"no evidence from shard {shard}")
    for n in sorted(set(full_names) - set(seen)):
        errors.append(f"manifest entry executed by no shard: {n}")
    return errors


def simulate(durations: list[float], slots: int = 4) -> float:
    """Wall time of ctest -j<slots> taking tests in the given order."""
    free = [0.0] * slots
    for d in durations:
        i = free.index(min(free))
        free[i] += d
    return max(free)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("matrix", "select", "merge", "dryrun"):
        p = sub.add_parser(name)
        p.add_argument("--shards", type=Path, default=DEFAULT_PLAN)
        if name == "matrix":
            p.add_argument("--legs", type=Path, required=True)
        if name in ("select", "merge"):
            p.add_argument("--manifest", type=Path, required=True)
        if name == "select":
            p.add_argument("--shard", required=True)
        if name == "merge":
            p.add_argument("evidence", nargs="+", type=Path)
        if name == "dryrun":
            p.add_argument("--durations", type=Path, required=True, help='JSON [[name, seconds, number], ...]')
            p.add_argument("--slots", type=int, default=4)
    args = ap.parse_args(argv)
    plan = load_plan(args.shards)
    if args.cmd == "matrix":
        print(json.dumps(matrix(json.loads(args.legs.read_text(encoding="utf-8")), plan)))
        return 0
    if args.cmd == "dryrun":
        rows = json.loads(args.durations.read_text(encoding="utf-8"))
        by_number = sorted(rows, key=lambda r: r[2])
        names = [r[0] for r in by_number]
        dur = {r[0]: r[1] for r in rows}
        parts = assign(plan, names)
        assert sorted(sum(parts.values(), [])) == sorted(names)
        for s, ns in parts.items():
            total = sum(dur[n] for n in ns)
            print(f"{s}: {len(ns)} tests, {total/60:.0f} slot-min, ~{simulate([dur[n] for n in ns], args.slots)/60:.0f} min wall")
        for note in advisories(plan, names):
            print(note)
        return 0
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    names = manifest_names(manifest)
    for note in advisories(plan, names):
        print(note, file=sys.stderr)
    if args.cmd == "select":
        parts = assign(plan, names)
        if args.shard not in parts:
            print(f"unknown shard {args.shard!r}; plan has {list(parts)}", file=sys.stderr)
            return 2
        if not parts[args.shard]:
            print(f"shard {args.shard} has no tests", file=sys.stderr)
            return 2
        print(ctest_regex(parts[args.shard]))
        return 0
    errors = merge(plan, names, [json.loads(p.read_text(encoding="utf-8")) for p in args.evidence])
    for e in errors:
        print(e, file=sys.stderr)
    if not errors:
        print(f"Shards {[json.loads(p.read_text())['shard'] for p in args.evidence]} cover all {len(names)} manifest entries exactly once.")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
