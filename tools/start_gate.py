#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""start_gate: przed startem agenta/rutyny rozwiązuje rola → model → miejsce i mówi, co by przekroczył (IW-6, 2026-10-26).

Uogólnia tools/pace.py (którego reguły tempa wywołuje bez zmian). 0 tokenów: skrypt bez sieci i bez modelu.
Projekt: docs/architecture/INTERFEJS_WSPOLPRACY_v2_2026-10-23.md §2.3, §4 (IW-6).

  python3 tools/start_gate.py --role package_executor --used 71 [--used-model fable=100] [--model opus] [--place P]
          [--plan-calls 60] [--inflight 1] [--now ISO] [--disk-free-gb 12] [--log-only] [--json] [-o karta.json]
          [--run-id r_… [--runs-root DIR]] [--start]

Kroki (każdy daje dane do karty, żaden nie zgaduje):
  1. model: --model > preset roli (tools/routines/roles.json) > brak = advisory „dziedziczony model sesji”;
     datowane obserwacje (np. „Fable niedostępny do …”) → advisory + zastępstwo z łańcucha roli (preset on_unavailable);
  2. estymata: cost_per_package z tools/pace_plan.json (klucz modelu, brak → default) × plan_calls / preset plan_calls;
  3. okno: cap modelu z pace_plan.json caps (R91: Fable do 100, reszta do 90 — jego słowa, preset) + reguły tempa pace.py
     dla limitu wspólnego; przekroczenie capu → klasa z presetu over_cap_class (np. Fable po 100 % = kredyty = jego pieniądze);
  4. miejsce: stan z docs/ops/places.json (datowana obserwacja), dysk (≥ preset przed buildem natywnym, CLAUDE.md);
  5. ×10: estymata ≥ x10_ratio × koszt presetu roli → „czeka na Ciebie” (jedyny wyjątek właściciela).
Werdykt (lista otwarta): start · start_with_advisories · wait · owner. --log-only: karta i zapis, kod wyjścia zawsze 0.
--run-id: karta trafia do routine_run.resolved, estymata do receipt[] (tools/run_record.py).
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
ROLES = HERE / "routines" / "roles.json"
PLAN = HERE / "pace_plan.json"
PLACES = REPO / "docs" / "ops" / "places.json"
sys.path.insert(0, str(HERE))
import pace  # noqa: E402  (reguły tempa bez zmian: target_at, ts)

EXIT = {"start": 0, "start_with_advisories": 0, "wait": 2, "owner": 3}


def _load(p: Path, fallback=None):
    try:
        return json.loads(Path(p).read_text(encoding="utf-8"))
    except FileNotFoundError:
        if fallback is not None:
            return fallback
        raise


def _ts(s):
    return pace.ts(s) if "T" in s else datetime.fromisoformat(s).replace(tzinfo=timezone.utc)


def active_observations(roles: dict, model: str, now: datetime, ignore: bool) -> list:
    if ignore:
        return []
    out = []
    for o in roles.get("observations") or []:
        if o.get("model") not in (model, "*") or o.get("state") == "cap":
            continue
        until = o.get("until")
        if until and now >= _ts(until):
            continue
        out.append(o)
    return out


def resolve_model(roles: dict, role: dict, requested: str | None, now: datetime, ignore_obs: bool, adv: list) -> dict:
    presets = roles.get("presets") or {}
    preset = role.get("model")
    model, why = (requested, "--model") if requested else (preset, "preset roli") if preset else (None, None)
    res = {"requested": requested, "role_preset": preset, "model": model, "why": why, "inherited": model is None,
           "observations": []}
    if model is None:
        adv.append(f"brak modelu (ani --model, ani presetu roli): agent dostanie {presets.get('inherit_label', 'model sesji')}")
        return res
    if requested and preset and requested != preset:
        mods = roles.get("models") or {}
        up = (mods.get(requested, {}).get("rank", 0) - mods.get(preset, {}).get("rank", 0))
        slack = presets.get("r63_slack") or {}
        if not (up > 0 and slack.get("allow_stronger_without_advisory") and up <= slack.get("max_steps_up", 0)):
            adv.append(f"model {requested} inny niż preset roli ({preset}) — świadomie? (luz R63: roles.json presets.r63_slack)")
    seen = set()
    while model and model not in seen:
        seen.add(model)
        obs = active_observations(roles, model, now, ignore_obs)
        if not obs:
            break
        res["observations"] += [o.get("id") for o in obs]
        o = obs[0]
        mode = role.get("on_unavailable") or presets.get("on_unavailable", "fallback")
        adv.append(f"obserwacja {o.get('id')} ({o.get('observed_at')}, źródło: {o.get('source')}): {model} {o.get('state')}"
                   f" do {o.get('until') or '?'}; tryb {mode}")
        if mode != "fallback":
            res["unavailable_mode"] = mode
            break
        nxt = next((m for m in role.get("fallback") or [] if m not in seen), None)
        if nxt is None:
            adv.append(f"łańcuch zastępstw roli wyczerpany; zostaje {model} (obserwacja to nie zakaz)")
            break
        res.setdefault("substituted", []).append({"from": model, "to": nxt, "observation": o.get("id")})
        model, res["why"] = nxt, f"zastępstwo za {model} (obserwacja {o.get('id')})"
    res["model"] = model
    res["name"] = (roles.get("models") or {}).get(model, {}).get("name") if model else None
    return res


def estimate(plan: dict, roles: dict, model: str | None, plan_calls: float | None) -> dict:
    costs = plan.get("cost_per_package") or {}
    key = ((roles.get("models") or {}).get(model or "", {}) or {}).get("cost_key") or model
    per = costs.get(key) if isinstance(costs.get(key), (int, float)) else None
    src = f"cost_per_package.{key}" if per is not None else "cost_per_package.default (brak pomiaru dla modelu)"
    per = per if per is not None else float(costs.get("default", 0.5))
    base = float((roles.get("presets") or {}).get("plan_calls") or 50)
    calls = float(plan_calls) if plan_calls else base
    return {"points": round(per * calls / base, 3), "per_package": per, "plan_calls": calls, "source": src}


def place_info(place: str, now_disk: float | None, needs_build: bool, min_gb: float, adv: list, places_file: Path) -> dict:
    info = {"id": place}
    try:
        pl = _load(places_file)
        hit = next((p for p in pl.get("places") or [] if p.get("id") == place or p.get("kind") == place), None)
    except (FileNotFoundError, json.JSONDecodeError):
        hit = None
    if hit:
        info.update(state=hit.get("state"), observed_at=hit.get("observed_at"), source=hit.get("source"))
        if hit.get("state") not in (None, "alive"):
            adv.append(f"miejsce {place}: stan {hit.get('state')} (obserwacja {hit.get('observed_at')})")
    elif place not in ("any", "cloud"):
        adv.append(f"miejsca {place} nie ma w places.json — stan nieznany")
    free = now_disk
    if free is None:
        try:
            free = shutil.disk_usage(str(REPO)).free / 1e9
            info["disk_source"] = f"shutil.disk_usage({REPO}) teraz"
        except OSError:
            free = None
    else:
        info["disk_source"] = "--disk-free-gb"
    info["disk_free_gb"] = round(free, 1) if free is not None else None
    info["needs_build"] = needs_build
    info["disk_ok"] = (not needs_build) or (free is not None and free >= min_gb)
    if needs_build and not info["disk_ok"]:
        adv.append(f"dysk {info['disk_free_gb']} GB < {min_gb} GB przed buildem (preset min_free_disk_gb_for_build)")
    return info


def window(plan: dict, roles: dict, model: str | None, used: float | None, used_model: dict, inflight: int,
           cost: float, now: datetime) -> dict:
    caps = plan.get("caps") or {}
    cap_key = ((roles.get("models") or {}).get(model or "", {}) or {}).get("cap") or "all_models"
    cap = caps.get(cap_key, caps.get("all_models"))
    u = used_model.get(cap_key, used_model.get(model or "", used if cap_key == "all_models" else None))
    w = {"cap_key": cap_key, "cap": cap, "used": u, "inflight": inflight}
    if u is None or cap is None:
        w["cap_ok"] = None
        w["note"] = "brak odczytu zużycia dla tego limitu (--used / --used-model) — nie sprawdzono"
    else:
        w["after"] = round(u + (inflight + 1) * cost, 2)
        w["cap_ok"] = w["after"] <= float(cap)
    if cap_key == "all_models" and used is not None:
        nxt = next((x for x in plan.get("waypoints") or [] if pace.ts(x["at"]) > now), None)
        w["target_now"] = round(pace.target_at(plan, now), 1)
        if nxt is None:
            w["pace_ok"] = True
            w["pace_note"] = "po ostatnim punkcie planu"
        else:
            hours = (pace.ts(nxt["at"]) - now).total_seconds() / 3600
            need = (float(nxt["target"]) - used) / hours if hours > 0 else 0
            r1 = used + (inflight + 1) * cost <= float(nxt["target"]) - float(plan.get("margin", 0))
            r2 = (inflight + 1) * cost <= need * float(plan.get("lookahead_h", 2))
            w.update(next_point=nxt["at"], next_target=nxt["target"], need_per_h=round(need, 3), pace_ok=bool(r1 and r2 and need > 0),
                     rule_checkpoint=r1, rule_rate=r2)
    return w


def gate(a, roles=None, plan=None) -> dict:
    roles = roles or _load(Path(a.roles))
    plan = plan or _load(Path(a.plan))
    presets = roles.get("presets") or {}
    now = pace.ts(a.now) if a.now else datetime.now(timezone.utc)
    adv: list = []
    rname = a.role or presets.get("default_role")
    role = (roles.get("roles") or {}).get(rname)
    if role is None:
        adv.append(f"roli {rname} nie ma w roles.json — traktuję jak rolę bez presetu (lista ról jest otwarta)")
        role = {}
    m = resolve_model(roles, role, a.model, now, a.ignore_observations, adv)
    est = estimate(plan, roles, m["model"], a.plan_calls)
    if est["source"].endswith("(brak pomiaru dla modelu)"):
        adv.append(f"koszt pakietu dla {m['model'] or 'dziedziczonego'} niezmierzony — estymata z default ({est['per_package']})")
    place = a.place or role.get("place") or "any"
    pi = place_info(place, a.disk_free_gb, bool(role.get("needs_build")), float(presets.get("min_free_disk_gb_for_build", 5)),
                    adv, Path(a.places))
    used_model = {}
    for kv in a.used_model or []:
        k, _, v = kv.partition("=")
        used_model[k.strip()] = float(v)
    w = window(plan, roles, m["model"], a.used, used_model, a.inflight, est["points"], now)
    exceeds = []
    verdict = "start"
    over_cls = presets.get("over_cap_class") or {}
    if w.get("cap_ok") is False:
        cls = over_cls.get(w["cap_key"], over_cls.get("_default", "wait"))
        exceeds.append({"what": f"cap {w['cap_key']} {w['cap']} %", "after": w["after"], "class": cls})
        verdict = cls
    if w.get("pace_ok") is False and verdict == "start":
        exceeds.append({"what": "tempo tygodnia (pace.py)", "next_point": w.get("next_point"), "class": "wait"})
        verdict = "wait"
    role_preset_cost = estimate(plan, roles, role.get("model"), None)["points"] if role.get("model") else est["per_package"]
    ratio = float(presets.get("x10_ratio", 10))
    if role_preset_cost and est["points"] >= ratio * role_preset_cost:
        exceeds.append({"what": f"×{ratio:g} względem presetu roli", "estimate": est["points"], "class": "owner"})
        verdict = "owner"
    if not pi["disk_ok"] and verdict == "start":
        exceeds.append({"what": "dysk przed buildem", "free_gb": pi["disk_free_gb"], "class": "wait"})
        verdict = "wait"
    if m.get("unavailable_mode") == "wait" and verdict == "start":
        verdict = "wait"
    if m.get("unavailable_mode") == "ask":
        verdict = "owner"
    if verdict == "start" and adv:
        verdict = "start_with_advisories"
    return {"schema": "loom.start_gate_card/1", "at": now.strftime("%Y-%m-%dT%H:%M:%SZ"), "role": rname,
            "model": m, "place": pi, "estimate": est, "window": w, "exceeds": exceeds, "advisories": adv,
            "verdict": verdict, "log_only": bool(a.log_only)}


def resolved_of(card: dict) -> dict:
    return {"role": card["role"], "model": card["model"]["model"], "inherited": card["model"]["inherited"],
            "place": card["place"]["id"], "via": "start_gate", "at": card["at"], "verdict": card["verdict"],
            "estimate_points": card["estimate"]["points"], "advisories": len(card["advisories"])}


def record(card: dict, run_id: str, runs_root: str | None) -> None:
    rr = [sys.executable, str(HERE / "run_record.py")] + (["--root", runs_root] if runs_root else [])
    subprocess.run(rr + ["set", run_id, "resolved", json.dumps(resolved_of(card), ensure_ascii=False)], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(rr + ["receipt", run_id, "--currency", "punkty tygodnia", "--value", str(card["estimate"]["points"]),
                         "--unit", "pkt", "--source", f"start_gate estymata ({card['estimate']['source']})",
                         "--reliability", "estimate"], check=True, stdout=subprocess.DEVNULL)


def human(card: dict) -> str:
    m, w, e, p = card["model"], card["window"], card["estimate"], card["place"]
    lines = [f"rola {card['role']} → model {m['model'] or '(dziedziczony)'} ({m['why'] or 'brak'}) → miejsce {p['id']}"
             f" [{p.get('state') or '?'}; dysk {p.get('disk_free_gb')} GB]",
             f"estymata {e['points']} pkt ({e['source']}, {e['plan_calls']:g} wywołań)",
             f"okno: cap {w['cap_key']} {w.get('cap')} %, zużyte {w.get('used')}, po starcie {w.get('after', '?')}"
             + (f"; tempo {'ok' if w.get('pace_ok') else 'ZA SZYBKO'} (cel teraz {w.get('target_now')} %)" if 'pace_ok' in w else "")]
    lines += [f"advisory: {x}" for x in card["advisories"]]
    lines += [f"przekroczy: {x['what']} → {x['class']}" for x in card["exceeds"]]
    lines.append(f"WERDYKT: {card['verdict']}{' (tylko zapis)' if card['log_only'] else ''}")
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--role")
    ap.add_argument("--model", help="nadpisanie modelu (preset: z roli)")
    ap.add_argument("--place")
    ap.add_argument("--used", type=float, help="seven_day.utilization limitu wspólnego (all models), w %%")
    ap.add_argument("--used-model", action="append", metavar="KLUCZ=PROC", help="zużycie limitu modelu, np. fable=100 (w %%, powtarzalne)")
    ap.add_argument("--inflight", type=int, default=0)
    ap.add_argument("--plan-calls", type=float)
    ap.add_argument("--now")
    ap.add_argument("--disk-free-gb", type=float)
    ap.add_argument("--ignore-observations", action="store_true")
    ap.add_argument("--log-only", action="store_true", help="karta i zapis, bez blokowania (kod wyjścia 0)")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("-o", "--out")
    ap.add_argument("--run-id")
    ap.add_argument("--runs-root")
    ap.add_argument("--start", action="store_true", help="po werdykcie start* zapisz start w pace_state.json (jak pace.py --start)")
    ap.add_argument("--roles", default=os.environ.get("START_GATE_ROLES", str(ROLES)))
    ap.add_argument("--plan", default=os.environ.get("START_GATE_PLAN", str(PLAN)))
    ap.add_argument("--places", default=os.environ.get("START_GATE_PLACES", str(PLACES)))
    a = ap.parse_args(argv)
    card = gate(a)
    if a.out:
        Path(a.out).write_text(json.dumps(card, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(json.dumps(card, ensure_ascii=False, indent=1) if a.json else human(card))
    if a.run_id:
        record(card, a.run_id, a.runs_root)
    if a.start and card["verdict"].startswith("start"):
        st = _load(pace.STATE, {"starts": []})
        st.setdefault("starts", []).append(card["at"])
        pace.STATE.write_text(json.dumps(st, indent=2) + "\n", encoding="utf-8")
    return 0 if a.log_only else EXIT.get(card["verdict"], 0)


if __name__ == "__main__":
    sys.exit(main())
