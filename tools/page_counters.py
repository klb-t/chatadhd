#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""page_counters.py — liczniki W1, zdarzenia do „Od Twojego wyjścia” (W9) i mapa miejsc v1 (IW-4, 2026-10-26).

Wszystko liczy skrypt z plików (0 tokenów, żadnej liczby od modelu):
  * zapisy biegów `docs/reports/runs/` (indeks `runs.jsonl` + rekordy `loom.routine_run/1`): suma `counter_delta`
    zdarzeń = liczniki (w_toku, zrobione, nie_wyszlo, oryginalne, … — lista otwarta, z danych), stany biegów, ostatnie
    zdarzenia z chwilą (strona filtruje je po znaczniku ostatniej wizyty), słowa do niego z paragonów;
  * `docs/ops/places.json`: miejsca, sesje i usługi z datowanym stanem → jeden wiersz „miejsca: N żyją · …”.
Skrzynkę `spec_inbox` i znacznik wizyty liczy strona z bazy (tam żyją), tą samą zasadą.

Użycie:  python3 tools/page_counters.py [--root R] [--now ISO]     (wypisuje JSON, ten sam, który dostaje strona)
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_record  # noqa: E402

# PRESETY (dane, nie reguły): każda wartość do zmiany bez dotykania silnika.
PRESET = {
    "places_file": "docs/ops/places.json",
    # ile ostatnich zdarzeń trafia na stronę (strona pokazuje te po ostatniej wizycie); null = wszystkie
    "events_max": 300,
    # zdarzenia bez delty licznika też idą do W9? (preset: tylko z niezerową deltą, jak linie statusu IW-1)
    "events_filter": "nonzero_delta",
    # W9 „Od Twojego wyjścia” rysuje się, gdy od ostatniej wizyty minęło więcej niż tyle godzin (przykład z §4: > 6 h)
    "w9_after_hours": 6,
    # ile pozycji W9 widać od razu; reszta „N zwiniętych — pokaż”
    "w9_visible": 5,
    # kto zapisuje znacznik wizyty: owner (tylko właściciel artefaktu), anyone (każdy piszący), none (nikt)
    "visit_marker_who": "owner",
    # waluty paragonu liczone jako „słowa do Ciebie” (lista otwarta; słowa wysłane do niego × preset tempa = jego czas)
    "words_currencies": ["words_to_owner", "slowa_do_wlasciciela", "słowa"],
    # kolejność i podpisy liczników; liczniki spoza listy też się pokazują (z nazwą klucza)
    "counter_labels": {"w_toku": "w toku", "zrobione": "zrobione", "nie_wyszlo": "nie wyszło", "oryginalne": "oryginalne"},
}


def _parse(at) -> _dt.datetime | None:
    if not at:
        return None
    try:
        d = _dt.datetime.fromisoformat(str(at).replace("Z", "+00:00"))
    except ValueError:
        return None
    return d if d.tzinfo else d.replace(tzinfo=_dt.timezone.utc)


def _iso(d: _dt.datetime) -> str:
    return d.astimezone(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def runs_counters(root: Path, now: _dt.datetime, preset: dict | None = None) -> dict:
    pre = {**PRESET, **(preset or {})}
    cfg = run_record.load_config()
    idx = run_record.read_index(root, cfg)
    counters: dict[str, float] = {}
    states: dict[str, int] = {}
    events = []
    words_today = 0.0
    words_receipts = 0
    missing = []
    today = now.astimezone(_dt.timezone.utc).date()
    wc = {str(x) for x in pre["words_currencies"]}
    for rid, row in idx.items():
        states[row.get("state") or "?"] = states.get(row.get("state") or "?", 0) + 1
        p = root / row["path"] if row.get("path") else None
        if not p or not p.exists():
            missing.append(rid)
            continue
        rec = json.loads(p.read_text(encoding="utf-8"))
        title = run_record.title_of(rec)
        for ev in rec.get("events") or []:
            delta = ev.get("counter_delta") or {}
            for k, v in delta.items():
                if isinstance(v, (int, float)):
                    counters[k] = counters.get(k, 0) + v
            if pre["events_filter"] == "nonzero_delta" and not any(v for v in delta.values() if isinstance(v, (int, float))):
                continue
            events.append({"at": ev.get("at"), "kind": ev.get("kind"), "text": ev.get("text"), "class": ev.get("class"),
                           "delta": delta, "run_id": rid, "title": title, "routine": row.get("routine")})
        for r in rec.get("receipt") or []:
            if str(r.get("currency")) in wc:
                words_receipts += 1
                d = _parse(r.get("at"))
                if d and d.date() == today and isinstance(r.get("value"), (int, float)):
                    words_today += r["value"]
    events.sort(key=lambda e: str(e.get("at") or ""), reverse=True)
    if pre["events_max"] is not None:
        events = events[: pre["events_max"]]
    labels = pre["counter_labels"]
    order = [k for k in labels if k in counters] + sorted(k for k in counters if k not in labels)
    return {
        "generated": _iso(now),
        "counters": [{"key": k, "label": labels.get(k, k), "value": int(counters[k]) if float(counters[k]).is_integer() else counters[k]}
                     for k in order],
        "run_states": dict(sorted(states.items())),
        "runs": len(idx),
        "events": events,
        "words_today": int(words_today) if float(words_today).is_integer() else words_today,
        "words_receipts": words_receipts,
        "missing_records": missing,
    }


def plural_label(label, n: int) -> str:
    """Podpis stanu: napis albo formy {one, few, many} (polska odmiana po liczbie; brak formy = pierwsza dostępna)."""
    if not isinstance(label, dict):
        return str(label)
    form = "one" if n == 1 else ("few" if n % 10 in (2, 3, 4) and n % 100 not in (12, 13, 14) else "many")
    return str(label.get(form) or next(iter(label.values()), ""))


def places_summary(root: Path, preset: dict | None = None) -> dict:
    pre = {**PRESET, **(preset or {})}
    p = root / pre["places_file"]
    if not p.exists():
        return {"file": pre["places_file"], "missing": True, "items": [], "counts": {}, "line": ""}
    data = json.loads(p.read_text(encoding="utf-8"))
    labels = data.get("state_labels") or {}
    items = []
    for pl in data.get("places") or []:
        items.append({"id": pl.get("id"), "level": "place", "title": pl.get("title"), "state": pl.get("state") or "unknown",
                      "observed_at": pl.get("observed_at"), "source": pl.get("source"), "url": pl.get("url")})
        for grp, lvl in (("sessions", "session"), ("services", "service")):
            for x in pl.get(grp) or []:
                items.append({"id": f"{pl.get('id')}/{x.get('id')}", "level": lvl, "parent": pl.get("id"), "title": x.get("title"),
                              "state": x.get("state") or "unknown", "observed_at": x.get("observed_at"),
                              "source": x.get("source"), "url": x.get("url")})
    counts: dict[str, int] = {}
    for it in items:
        counts[it["state"]] = counts.get(it["state"], 0) + 1
    order = [s for s in data.get("summary_order") or [] if s in counts] + sorted(s for s in counts if s not in (data.get("summary_order") or []))
    line = "miejsca: " + " · ".join(f"{counts[s]} {plural_label(labels.get(s, s), counts[s])}" for s in order) if items else ""
    labels = {s: plural_label(labels.get(s, s), 1) for s in counts}  # podpis pojedynczego wiersza na stronie
    oldest = min((it["observed_at"] for it in items if it.get("observed_at")), default=None)
    return {"file": pre["places_file"], "items": items, "counts": counts, "order": order, "labels": labels, "line": line,
            "oldest_observation": oldest}


def collect(root: Path = REPO, now: _dt.datetime | None = None, preset: dict | None = None) -> dict:
    now = now or _dt.datetime.now(_dt.timezone.utc)
    if now.tzinfo is None:
        now = now.replace(tzinfo=_dt.timezone.utc)
    pre = {**PRESET, **(preset or {})}
    out = runs_counters(root, now, pre)
    out["places"] = places_summary(root, pre)
    out["preset"] = {k: pre[k] for k in ("w9_after_hours", "w9_visible", "visit_marker_who", "events_filter")}
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="page_counters.py", description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(REPO))
    ap.add_argument("--now")
    a = ap.parse_args(argv)
    now = _parse(a.now) if a.now else None
    out = collect(Path(a.root), now)
    out["events"] = out["events"][:20]
    print(json.dumps(out, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
