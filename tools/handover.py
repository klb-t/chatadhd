#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""handover — jeden rekord, wiele renderów (pakiet IW-11, 2026-10-26).

Projekt: docs/architecture/INTERFEJS_WSPOLPRACY_v2_2026-10-23.md §2.8, §4 IW-11. Rekord `loom.handover/1` składa się
skryptem (0 tokenów, bez sieci, bez modelu) z: zapisów biegów (`docs/reports/runs/runs.jsonl` + rekordy), głowy kolejki
(`docs/reports/QUEUE_S1.md`), pozycji spec czekających na właściciela (`understanding: proposed`), mapy miejsc
(`docs/ops/places.json`), zadań zaplanowanych i narzędzi sesji (dane w `tools/routines/`), stanu gita oraz tekstów
stałych z presetów. Ma paragon (koszt odbudowy kontekstu) i predykat „tylko między pakietami”. Karta teleportu, prompt
startowy, karta pakietu, rekord sekretarza, „Od Twojego wyjścia” i status dla właściciela to RENDERY tego rekordu
(przykłady z otwartej listy) — nie czwarty magazyn.

  python3 tools/handover.py build [--save] [--run-id R]   rekord (JSON); --save → docs/ops/handover.json (supersedes)
  python3 tools/handover.py render <rodzaj> [-o PLIK] [--since ISO] [--run-id R] [--from REKORD.json]
        rodzaje: teleport | start_prompt | package_card | secretary | since_you_left | owner_status (lista otwarta)
  python3 tools/handover.py compare <ręczna_karta.md> [--from REKORD.json] [--json]
        równoważność renderu teleport z ręczną kartą: pokrycie sekcji i faktów (`…`); kod 0 = równoważne wg presetu

Presety: tools/routines/handover.json. Testy: python3 -m unittest tools.tests.test_handover
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
import run_record as rr  # noqa: E402

SCHEMA_ID = "loom.handover/1"
PRESETS = HERE / "routines" / "handover.json"
RENDERS = {}


def presets(path: str | None = None) -> dict:
    return json.loads(Path(path or PRESETS).read_text(encoding="utf-8"))


def _git(root: Path, *args) -> str | None:
    try:
        r = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return r.stdout.strip() if r.returncode == 0 else None


def _load_json(p: Path, default=None):
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return default


def _hash(obj) -> str:
    return "sha256:" + hashlib.sha256(json.dumps(obj, ensure_ascii=False, sort_keys=True).encode("utf-8")).hexdigest()


# ------------------------------------------------------------------------------------------------------------ źródła

def git_state(root: Path) -> dict:
    out = {"head": _git(root, "rev-parse", "--short=8", "HEAD"), "branch": _git(root, "rev-parse", "--abbrev-ref", "HEAD"),
           "remotes": {}}
    refs = _git(root, "for-each-ref", "--format=%(refname:short) %(objectname:short=8) %(committerdate:iso-strict)",
                "refs/remotes/origin") or ""
    for line in refs.splitlines():
        parts = line.split(" ")
        if len(parts) >= 2 and not parts[0].endswith("/HEAD") and parts[0] != "origin":
            out["remotes"][parts[0].removeprefix("origin/")] = {"sha": parts[1], "date": parts[2] if len(parts) > 2 else None}
    return out


def runs_state(root: Path, pre: dict) -> dict:
    cfg = rr.load_config()
    idx = rr.read_index(root, cfg)
    live, recent, esc = [], [], []
    rows = sorted(idx.values(), key=lambda r: r.get("updated") or r.get("started") or "", reverse=True)
    for row in rows:
        try:
            rec, _ = rr.load(root, cfg, row["run_id"])
        except (rr.RunError, OSError, ValueError, SystemExit):
            continue
        last = (rec.get("events") or [{}])[-1]
        item = {"run_id": row["run_id"], "routine": row.get("routine"), "title": row.get("title"), "state": rec.get("state"),
                "where": rec.get("where"), "started": rec.get("started"), "finished": rec.get("finished"),
                "summary": rec.get("summary"), "last_event": {"kind": last.get("kind"), "at": last.get("at"), "text": last.get("text")}}
        if rec.get("state") in ("running", "paused"):
            live.append(item)
            for e in rec.get("escalations") or []:
                esc.append({**(e if isinstance(e, dict) else {"text": e}), "run_id": row["run_id"]})
        elif len(recent) < int(pre.get("recent_runs", 12)):
            recent.append(item)
    return {"total": len(idx), "live": live, "recent": recent, "escalations_open": esc}


def queue_head(root: Path, pre: dict) -> list[dict]:
    p = root / pre.get("queue_file", "docs/reports/QUEUE_S1.md")
    try:
        lines = p.read_text(encoding="utf-8").splitlines()
    except OSError:
        return []
    out, n = [], int(pre.get("queue_heading_chars", 260))
    for i, ln in enumerate(lines, 1):
        if ln.startswith("## "):
            h = ln[3:].strip()
            out.append({"line": i, "heading": h if len(h) <= n else h[: n - 1] + "…"})
            if len(out) >= int(pre.get("queue_head_sections", 10)):
                break
    return out


def spec_waiting(root: Path, pre: dict) -> list[dict]:
    spec = _load_json(root / "docs/spec/spec.json", {}) or {}
    states = set(pre.get("spec_understanding_waiting", ["proposed"]))
    out = []
    for n in spec.get("nodes", []):
        if (n.get("understanding") or {}).get("state") in states:
            title = n.get("title") or ((n.get("summary") or {}).get("text") or "").replace("\n", " ")[:120]
            out.append({"id": n["id"], "alias": (n.get("aliases") or [n["id"]])[0], "title": title,
                        "understanding": n["understanding"].get("state")})
    return out


def places(root: Path) -> dict:
    try:
        import page_counters
        out = page_counters.places_summary(root)
    except Exception as e:  # noqa: BLE001 — brak mapy miejsc = sekcja z uwagą, nie błąd rekordu
        return {"line": None, "items": [], "places": [], "advisory": f"mapa miejsc niedostępna: {e}"}
    raw = (_load_json(root / "docs/ops/places.json", {}) or {}).get("places", [])
    keep = ("id", "kind", "title", "state", "observed_at", "stop_channel", "url")
    out["places"] = [{k: p.get(k) for k in keep if p.get(k) is not None} for p in raw]
    return out


def session_tools(root: Path, pre: dict) -> list[dict]:
    d = _load_json(root / pre.get("tools_file", "tools/routines/tools.json"), {}) or {}
    want = pre.get("session_tools") or []
    by = {t["id"]: t for t in d.get("tools", [])}
    return [{"id": i, "does": by[i].get("does"), "run": by[i].get("run"), "availability": by[i].get("availability") or {}}
            for i in want if i in by]


def build(root: Path, pre: dict, now: str | None = None) -> dict:
    now = now or rr.now_iso()
    runs = runs_state(root, pre)
    rc = pre.get("rebuild_cost") or {}
    live_ctx = 1 + sum(1 for r in runs["live"] if r["state"] == "running")
    sp = pre.get("switch_predicate") or {}
    blocking = [r["run_id"] for r in runs["live"] if r["state"] in sp.get("running_states", ["running"])]
    rec = {
        "schema": SCHEMA_ID,
        "at": now,
        "git": git_state(root),
        "runs": runs,
        "queue_head": queue_head(root, pre),
        "spec_waiting": spec_waiting(root, pre),
        "places": places(root),
        "tasks": (_load_json(root / pre.get("tasks_file", "tools/routines/scheduled_tasks.json"), {}) or {}).get("tasks", []),
        "session_tools": session_tools(root, pre),
        "static": pre.get("static") or {},
        "receipt": [{"currency": rc.get("currency", "tokens"), "value": [live_ctx * rc.get("per_live_context_min", 0),
                                                                          live_ctx * rc.get("per_live_context_max", 0)],
                     "unit": "range", "what": f"odbudowa kontekstu: {live_ctx} żywych kontekstów", "source": rc.get("source"),
                     "reliability": rc.get("reliability"), "at": now}],
        "switch": {"ok": not blocking, "blocking_runs": blocking, "predicate": sp.get("text")},
        "supersedes": None,
    }
    prev = _load_json(root / pre.get("out_file", "docs/ops/handover.json"))
    if prev:
        rec["supersedes"] = prev.get("hash")
    rec["hash"] = _hash({k: v for k, v in rec.items() if k not in ("at", "hash", "supersedes")})
    return rec


def save(root: Path, pre: dict, rec: dict) -> Path:
    p = root / pre.get("out_file", "docs/ops/handover.json")
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_suffix(".tmp")
    tmp.write_text(json.dumps(rec, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    tmp.replace(p)
    return p


# ---------------------------------------------------------------------------------------------------------- rendery

def render(kind: str):
    def deco(fn):
        RENDERS[kind] = fn
        return fn
    return deco


def _sec(sid: str, n: int, title: str) -> list[str]:
    return [f"<!-- section: {sid} -->", f"## {n}. {title}"]


def _avail(t: dict) -> str:
    parts = []
    for place, a in (t.get("availability") or {}).items():
        s = f"{place}: {a.get('state', '?')} ({a.get('observed', '?')})"
        if a.get("substitute"):
            s += f"; zamiennik: {a['substitute']}"
        parts.append(s)
    return " · ".join(parts) or "brak obserwacji"


@render("teleport")
def r_teleport(rec: dict, **_) -> str:
    st = rec["static"]
    g = rec["git"]
    L = ["# Teleport S1 — karta startowa koordynatora (render rekordu `handover`)", "",
         f"Stan na: **{rec['at'][:16].replace('T', ' ')} UTC** (wygenerowane: `python3 tools/handover.py render teleport`; "
         f"rekord `docs/ops/handover.json`, hash `{rec['hash'][7:19]}`). Jeśli data jest stara, prawdę ma `git log`, "
         "zapisy biegów i głowa `docs/reports/QUEUE_S1.md`.", ""]
    n = 1
    if "who" in st:
        L += _sec("who", n, st["who"]["title"]) + [st["who"]["text"], ""]; n += 1
    if "read_order" in st:
        L += _sec("read_order", n, st["read_order"]["title"]) + [f"{i}. {x}" for i, x in enumerate(st["read_order"]["items"], 1)] + [""]; n += 1
    L += _sec("state", n, "Stan w chwili teleportu"); n += 1
    rem = g.get("remotes") or {}
    if "main" in rem:
        L.append(f"- `origin/main` = `{rem['main']['sha']}` ({rem['main'].get('date') or '?'}).")
    L.append(f"- Ta kopia: gałąź `{g.get('branch')}` na `{g.get('head')}`.")
    others = [f"`{b}` = `{v['sha']}`" for b, v in sorted(rem.items(), key=lambda kv: kv[1].get("date") or "", reverse=True)
              if b != "main"][:8]
    if others:
        L.append("- Gałęzie na `origin` (najnowsze): " + ", ".join(others) + ".")
    live = rec["runs"]["live"]
    L.append(f"- Biegi w toku/wstrzymane: {len(live)}" + (":" if live else "."))
    for r in live:
        L.append(f"  - `{r['run_id']}` {r.get('routine')} „{r.get('title')}” — {r['state']}, {r.get('where') or 'miejsce ?'}, "
                 f"od {str(r.get('started'))[:16]}; ostatnio: {r['last_event'].get('kind')} {r['last_event'].get('text') or ''}".rstrip())
    sw = rec.get("switch") or {}
    L.append(f"- Przełączenie teraz: {'można' if sw.get('ok') else 'NIE — biegi w toku: ' + ', '.join('`' + x + '`' for x in sw.get('blocking_runs', []))}"
             f" ({sw.get('predicate')}).")
    rc = (rec.get("receipt") or [{}])[0]
    if rc.get("value"):
        lo, hi = (f"{v:,}".replace(",", "\u00a0") for v in rc["value"])
        L.append(f"- Paragon teleportu: {rc['what']} ≈ {lo}–{hi} tokenów ({rc.get('source')}).")
    sw_ = rec["spec_waiting"]
    L.append(f"- Czeka na właściciela (spec `proposed`, nie pytaj ponownie — sprawdź odpowiedzi): {len(sw_)}"
             + (": " + ", ".join(x["alias"] for x in sw_) + "." if sw_ else "."))
    esc = rec["runs"]["escalations_open"]
    if esc:
        L.append(f"- Eskalacje otwarte w biegach: {len(esc)} — " + "; ".join(
            f"`{e['run_id']}`: {e.get('why') or e.get('question') or e.get('text') or e.get('kind')}" for e in esc[:6]))
    L.append(f"- Głowa kolejki (`QUEUE_S1.md`, {len(rec['queue_head'])} sekcji):")
    for q in rec["queue_head"]:
        L.append(f"  - {q['heading']}")
    if rec["tasks"]:
        L.append("- Zadania zaplanowane (dane: `tools/routines/scheduled_tasks.json`; treści: "
                 "`docs/ops/ZADANIA_ZAPLANOWANE_2026-10-23.md`):")
        for t in rec["tasks"]:
            L.append(f"  - „{t['name']}” `{t.get('cron')}` — {t.get('state')} (obserwacja {t.get('observed_at')}"
                     + (f", `{t['trigger_id']}`" if t.get("trigger_id") else "") + ")")
    pl = rec.get("places") or {}
    if pl.get("line"):
        L.append(f"- {pl['line'][0].upper() + pl['line'][1:]} (`docs/ops/places.json`, obserwacje z datą):")
    for p in pl.get("places") or []:
        stop = p.get("stop_channel")
        stop = stop if isinstance(stop, str) else (json.dumps(stop, ensure_ascii=False) if stop else None)
        L.append(f"  - `{p['id']}` {p.get('title', '')} — {p.get('state')} (obserwacja {p.get('observed_at')})"
                 + (f"; stop: {stop}" if stop else "") + (f"; {p['url']}" if p.get("url") else ""))
    L.append("")
    if "first_commands" in st:
        fc = st["first_commands"]
        L += _sec("first_commands", n, fc["title"]) + ["```", fc["code"], "```", fc.get("note", ""), ""]; n += 1
    L += _sec("tools", n, "Narzędzia sesji — dostępność per miejsce (z `tools/routines/tools.json`)"); n += 1
    L += ["| Narzędzie | Do czego | Dostępność (obserwacja) |", "|---|---|---|"]
    for t in rec["session_tools"]:
        L.append(f"| `{t['id']}` | {t.get('does')} | {_avail(t)} |")
    L.append("")
    if "reversal" in st:
        L += _sec("reversal", n, st["reversal"]["title"]) + [st["reversal"]["text"], ""]; n += 1
    if "takeover" in st:
        tk = st["takeover"]
        L += _sec("takeover", n, tk["title"]) + ["", f"> {tk['quote']}", "", tk["text"], ""]; n += 1
    return "\n".join(L).rstrip() + "\n"


@render("start_prompt")
def r_start_prompt(rec: dict, **_) -> str:
    live = rec["runs"]["live"]
    return (f"Jesteś koordynatorem S1 projektu ChatADHD (przejęcie {rec['at'][:16]} UTC). Przeczytaj CLAUDE.md, potem "
            f"docs/ops/TELEPORT_S1.generated.md (render rekordu handover {rec['hash'][7:19]}). "
            f"Biegi w toku: {len(live)}" + (" (" + ", ".join(r['run_id'] for r in live) + ")" if live else "") + ". "
            f"Czeka na właściciela: {len(rec['spec_waiting'])} parafraz. Pierwsza komenda: "
            "python3 tools/handover.py render since_you_left. Nie restartuj biegów w toku; korekty jako addendum.\n")


def _run_card(rec_run: dict) -> str:
    L = [f"### Karta biegu `{rec_run['run_id']}` — {rec_run.get('routine', {}).get('id')} ({rec_run.get('state')})",
         f"- miejsce: {rec_run.get('where')}; od {rec_run.get('started')} do {rec_run.get('finished') or '—'}",
         f"- podsumowanie: {rec_run.get('summary') or '—'}"]
    for s in rec_run.get("steps", []):
        L.append(f"- krok `{s['id']}`: {s.get('state')}" + (f" — {s.get('out')}" if isinstance(s.get("out"), str) else ""))
    for k in ("commits", "escalations", "decisions", "original_ideas"):
        if rec_run.get(k):
            L.append(f"- {k}: " + "; ".join(json.dumps(x, ensure_ascii=False) if not isinstance(x, str) else x for x in rec_run[k]))
    return "\n".join(L) + "\n"


@render("package_card")
def r_package_card(rec: dict, root: Path = REPO, run_id: str | None = None, **_) -> str:
    cfg = rr.load_config()
    if not run_id:
        cands = [r for r in rec["runs"]["recent"] + rec["runs"]["live"] if r.get("routine") == "package.run"]
        if not cands:
            return "(brak biegu package.run)\n"
        run_id = cands[0]["run_id"]
    r, _ = rr.load(root, cfg, run_id)
    return _run_card(r)


@render("secretary")
def r_secretary(rec: dict, root: Path = REPO, **_) -> str:
    cfg = rr.load_config()
    cands = [r for r in rec["runs"]["live"] + rec["runs"]["recent"] if r.get("routine") in ("secretary.run", "operator.run")]
    if not cands:
        return "(brak biegu sekretarza/operatora w ostatnich zapisach)\n"
    r, _ = rr.load(root, cfg, cands[0]["run_id"])
    return _run_card(r)


@render("since_you_left")
def r_since(rec: dict, root: Path = REPO, since: str | None = None, pre: dict | None = None, **_) -> str:
    cfg = rr.load_config()
    if not since:
        h = float((pre or {}).get("since_you_left_hours", 6))
        since = (rr.parse_time(rec["at"]) - _dt.timedelta(hours=h)).strftime("%Y-%m-%dT%H:%M:%SZ")
    lines = rr.render_status_lines(root, cfg, since=since)
    L = [f"Od Twojego wyjścia ({since[:16].replace('T', ' ')} UTC):"]
    L += [f"- {x}" for x in lines] or ["- nic z licznikiem się nie zmieniło"]
    if rec["runs"]["escalations_open"]:
        L.append(f"Czeka na koordynatora: {len(rec['runs']['escalations_open'])} eskalacji w biegach.")
    if rec["spec_waiting"]:
        L.append(f"Czeka na Ciebie: {len(rec['spec_waiting'])} parafraz do zatwierdzenia albo poprawy.")
    return "\n".join(L) + "\n"


def _pl(n: int, one, few, many):
    if n == 1:
        return one
    return few if n % 10 in (2, 3, 4) and n % 100 not in (12, 13, 14) else many


@render("owner_status")
def r_owner(rec: dict, **_) -> str:
    live = rec["runs"]["live"]
    s = [f"Stan na {rec['at'][11:16]} UTC."]
    n = len(live)
    s.append("Nic teraz nie pracuje." if not n else "Pracuje jeden bieg." if n == 1 else f"Pracuje {n} {_pl(n, 'bieg', 'biegi', 'biegów')}.")
    w = len(rec["spec_waiting"])
    if w:
        s.append(f"Na Twoją decyzję {'czeka' if w == 1 or _pl(w, 1, 0, 1) else 'czekają'} {w} {_pl(w, 'parafraza', 'parafrazy', 'parafraz')}.")
    pl = (rec.get("places") or {}).get("line")
    if pl:
        s.append(f"Miejsca: {re.sub(r'^miejsca:\s*', '', pl)}.")
    return " ".join(s) + "\n"


# ---------------------------------------------------------------------------------------------------- porównanie

def compare(generated: str, handwritten: str, pre: dict) -> dict:
    cmp = pre.get("compare") or {}
    hmap = cmp.get("heading_map") or {}
    gen_ids = set(re.findall(r"<!-- section: (\S+) -->", generated))
    heads = [h.strip() for h in re.findall(r"^## (.+)$", handwritten, re.M)]
    covered, missing = [], []
    for h in heads:
        sid = next((v for k, v in hmap.items() if k.lower() in h.lower()), None)
        (covered if sid and sid in gen_ids else missing).append({"heading": h, "section": sid})
    facts = sorted(set(re.findall(cmp.get("fact_re", r"`([^`\n]{3,80})`"), handwritten)))
    present = [f for f in facts if f in generated]
    absent = [f for f in facts if f not in generated]
    share = len(present) / len(facts) if facts else 1.0
    ok = not missing and share >= float(cmp.get("fact_min_share", 0.5))
    return {"equivalent": ok, "sections": {"covered": covered, "missing": missing},
            "facts": {"total": len(facts), "present": len(present), "share": round(share, 3), "absent": absent},
            "note": "nieobecne fakty to zwykle stan datowany ręcznej karty (stare sha, godziny 23.10) — render ma stan z danych"}


# ------------------------------------------------------------------------------------------------------------- CLI

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(REPO))
    ap.add_argument("--presets")
    ap.add_argument("--now")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("--save", action="store_true"); b.add_argument("--run-id")
    r = sub.add_parser("render"); r.add_argument("kind"); r.add_argument("-o", "--out"); r.add_argument("--since")
    r.add_argument("--run-id"); r.add_argument("--from", dest="src")
    c = sub.add_parser("compare"); c.add_argument("handwritten"); c.add_argument("--from", dest="src")
    c.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    root = Path(a.root).resolve()
    pre = presets(a.presets)

    def record():
        if getattr(a, "src", None):
            return json.loads(Path(a.src).read_text(encoding="utf-8"))
        return build(root, pre, a.now)

    if a.cmd == "build":
        rec = record()
        if a.save:
            p = save(root, pre, rec)
            print(p)
            if a.run_id:
                cfg = rr.load_config()
                rr.add_to_list(root, cfg, a.run_id, "cards", {"handover": str(p.relative_to(root)) if p.is_relative_to(root) else str(p),
                                                             "hash": rec["hash"], "supersedes": rec["supersedes"]})
        else:
            print(json.dumps(rec, ensure_ascii=False, indent=1))
        return 0
    if a.cmd == "render":
        rec = record()
        fn = RENDERS.get(a.kind)
        if fn is None:
            text = json.dumps(rec, ensure_ascii=False, indent=1) + "\n"
            print(f"uwaga: nieznany render „{a.kind}” — wypisuję rekord (znane: {', '.join(sorted(RENDERS))})", file=sys.stderr)
        else:
            text = fn(rec, root=root, since=a.since, run_id=a.run_id, pre=pre)
        if a.out:
            out = Path(a.out) if Path(a.out).is_absolute() else root / a.out
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_text(text, encoding="utf-8")
            print(out)
        else:
            print(text, end="")
        return 0
    if a.cmd == "compare":
        rec = record()
        gen = r_teleport(rec)
        hw = Path(a.handwritten) if Path(a.handwritten).is_absolute() else root / a.handwritten
        if not hw.exists():
            print(f"uwaga: brak ręcznej karty {hw} — nie ma z czym porównać", file=sys.stderr)
            return 1
        res = compare(gen, hw.read_text(encoding="utf-8"), pre)
        if a.json:
            print(json.dumps(res, ensure_ascii=False, indent=1))
        else:
            print(f"równoważne: {'tak' if res['equivalent'] else 'nie'}; sekcje {len(res['sections']['covered'])}/"
                  f"{len(res['sections']['covered']) + len(res['sections']['missing'])}; fakty {res['facts']['present']}/"
                  f"{res['facts']['total']} ({res['facts']['share']:.0%})")
            for m in res["sections"]["missing"]:
                print(f"  brak sekcji: {m['heading']}")
            if res["facts"]["absent"]:
                print("  fakty nieobecne w renderze: " + ", ".join(res["facts"]["absent"][:40]))
        return 0 if res["equivalent"] else 1
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
