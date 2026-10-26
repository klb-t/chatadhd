#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Operator = rozszerzony mandat sekretarza (pakiet IW-10, 2026-10-26).

Projekt: docs/architecture/INTERFEJS_WSPOLPRACY_v2_2026-10-23.md §2.3 (sekretarz = operator v1), §4 IW-10.
Operator to zadanie zaplanowane (Sonnet), które wykonuje BEZ OSĄDU tylko rutyny z `operator_ok: true`
(tools/routines/routines.json); wszystko, co wymaga osądu (`judgement`), staje się propozycją do koordynatora w zapisie
biegu (eskalacja) — nic nie jest wykonane. Runner: `python3 tools/routine.py run <id> --as operator --parent-run <RID>`.
0 tokenów, bez sieci, bez modelu; liczby tylko z zapisów biegów.

  python3 tools/operator_run.py list                 rutyny z operator_ok (mandat operatora jako dane)
  python3 tools/operator_run.py stats [--routine R…] koszt pustego i niepustego biegu z zapisów (sekretarz, operator)
  python3 tools/operator_run.py cadence              propozycja kadencji z pomiaru pustego biegu (nigdy nie zmienia zadania)
  python3 tools/operator_run.py summary <RID>        karta biegu operatora: biegi podrzędne, eskalacje, propozycje
  python3 tools/operator_run.py prompt               tekst promptu zadania zaplanowanego (tools/routines/operator_prompt.md)

Presety: `presets.operator` rutyny operator.run w routines.json (kadencja z `_history`, progi pustego biegu).
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import statistics
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
import routine as rt  # noqa: E402
import run_record as rr  # noqa: E402

PROMPT_FILE = HERE / "routines" / "operator_prompt.md"


def catalogue(path: str | None = None) -> dict:
    return rt.load_json(Path(path) if path else rt.ROUTINES)


def operator_routine(cat: dict) -> dict:
    return rt.find_routine(cat, "operator.run")


def operator_presets(cat: dict) -> dict:
    return (operator_routine(cat).get("presets") or {}).get("operator") or {}


def ok_routines(cat: dict) -> list[dict]:
    latest = {}
    for r in cat.get("routines", []):
        if r["id"] not in latest or rt._vkey(r.get("version", 0)) > rt._vkey(latest[r["id"]].get("version", 0)):
            latest[r["id"]] = r
    return [r for r in latest.values() if r.get("operator_ok") is True]


def _t(s):
    try:
        return rr.parse_time(s)
    except Exception:  # noqa: BLE001 — zły czas w starym rekordzie = brak pomiaru, nie błąd
        return None


def is_empty(rec: dict) -> bool:
    """Pusty bieg = żadne zdarzenie nie zmieniło licznika (finish --quiet) i nic nie zostało zastosowane."""
    if any(ev.get("counter_delta") for ev in rec.get("events", [])):
        return False
    if rec.get("escalations") or rec.get("decisions") or rec.get("commits"):
        return False
    return True


def stats(root: Path, routines: list[str]) -> dict:
    cfg = rr.load_config()
    out = {}
    for rid, row in rr.read_index(root, cfg).items():
        if row.get("routine") not in routines:
            continue
        try:
            rec, _ = rr.load(root, cfg, rid)
        except (rr.RunError, OSError, ValueError, SystemExit):
            continue
        s = out.setdefault(row["routine"], {"runs": 0, "empty": 0, "nonempty": 0, "dur_empty": [], "dur_nonempty": [],
                                            "tokens_empty": [], "tokens_nonempty": [], "escalations": 0, "first": None, "last": None})
        s["runs"] += 1
        e = is_empty(rec)
        s["empty" if e else "nonempty"] += 1
        a, b = _t(rec.get("started")), _t(rec.get("finished"))
        if a and b:
            s["dur_empty" if e else "dur_nonempty"].append((b - a).total_seconds())
        tok = sum(float(x.get("value") or 0) for x in rec.get("receipt", []) if str(x.get("currency", "")).startswith("tokens"))
        if tok:
            s["tokens_empty" if e else "tokens_nonempty"].append(tok)
        s["escalations"] += len(rec.get("escalations") or [])
        st = rec.get("started")
        s["first"] = min(filter(None, [s["first"], st])) if st else s["first"]
        s["last"] = max(filter(None, [s["last"], st])) if st else s["last"]
    for s in out.values():
        for k in ("dur_empty", "dur_nonempty", "tokens_empty", "tokens_nonempty"):
            v = s.pop(k)
            s[k.replace("dur_", "median_s_").replace("tokens_", "median_tokens_")] = statistics.median(v) if v else None
            s["n_" + k] = len(v)
        s["empty_share"] = round(s["empty"] / s["runs"], 3) if s["runs"] else None
    return out


def cadence(root: Path, cat: dict) -> dict:
    """Propozycja (nigdy zmiana): przy udziale pustych biegów ≥ próg i co najmniej min_runs pomiarach — rzadziej; przy
    eskalacjach czekających dłużej niż kadencja — częściej. Progi to presety z `_history`."""
    pre = operator_presets(cat)
    c = pre.get("cadence") or {}
    st = stats(root, pre.get("measure_routines", ["secretary.run", "operator.run"]))
    runs = sum(s["runs"] for s in st.values())
    empty = sum(s["empty"] for s in st.values())
    share = (empty / runs) if runs else None
    prop = {"current": c.get("cron"), "measured": {"runs": runs, "empty": empty, "empty_share": share}, "proposal": None,
            "why": None, "applied": False}
    if runs < int(c.get("min_runs", 24)):
        prop["why"] = f"za mało pomiarów ({runs} < {c.get('min_runs', 24)}, preset) — kadencja bez zmian"
    elif share is not None and share >= float(c.get("empty_share_slower", 0.8)):
        prop["proposal"] = c.get("slower_cron")
        prop["why"] = f"puste biegi {share:.0%} ≥ {c.get('empty_share_slower')} (preset) → rzadziej"
    elif share is not None and share <= float(c.get("empty_share_faster", 0.2)):
        prop["proposal"] = c.get("faster_cron")
        prop["why"] = f"puste biegi {share:.0%} ≤ {c.get('empty_share_faster')} (preset) → częściej"
    else:
        prop["why"] = "udział pustych biegów między progami — kadencja bez zmian"
    return prop


def summary(root: Path, run_id: str) -> dict:
    cfg = rr.load_config()
    rec, _ = rr.load(root, cfg, run_id)
    kids = []
    for rid, row in rr.read_index(root, cfg).items():
        if rid == run_id:
            continue
        try:
            r, _ = rr.load(root, cfg, rid)
        except (rr.RunError, OSError, ValueError, SystemExit):
            continue
        if (r.get("params") or {}).get("parent_run") == run_id:
            kids.append({"run_id": rid, "routine": row.get("routine"), "state": r.get("state"),
                         "escalations": r.get("escalations") or [], "advisories": len(r.get("advisories") or [])})
    esc = list(rec.get("escalations") or []) + [dict(e, run_id=k["run_id"]) for k in kids for e in k["escalations"]]
    return {"run_id": run_id, "state": rec.get("state"), "children": [{k: v for k, v in x.items() if k != "escalations"} for x in kids],
            "escalations": esc, "proposals": [e for e in esc if e.get("kind") == "judgement"],
            "failed": [k["run_id"] for k in kids if k["state"] == "failed"]}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(REPO))
    ap.add_argument("--routines")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    s = sub.add_parser("stats"); s.add_argument("--routine", action="append")
    sub.add_parser("cadence")
    m = sub.add_parser("summary"); m.add_argument("run_id")
    sub.add_parser("prompt")
    a = ap.parse_args(argv)
    root = Path(a.root).resolve()
    cat = catalogue(a.routines)
    if a.cmd == "list":
        for r in sorted(ok_routines(cat), key=lambda r: r["id"]):
            print(f"{r['id']}@{r.get('version')}  — {r.get('title', '')}")
        return 0
    if a.cmd == "stats":
        rs = a.routine or operator_presets(cat).get("measure_routines", ["secretary.run", "operator.run"])
        print(json.dumps(stats(root, rs), ensure_ascii=False, indent=1))
        return 0
    if a.cmd == "cadence":
        print(json.dumps(cadence(root, cat), ensure_ascii=False, indent=1))
        return 0
    if a.cmd == "summary":
        print(json.dumps(summary(root, a.run_id), ensure_ascii=False, indent=1))
        return 0
    if a.cmd == "prompt":
        print(PROMPT_FILE.read_text(encoding="utf-8"), end="")
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
