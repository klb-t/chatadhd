#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Kontrola emituje dane; znacznik → addendum (pakiet IW-9, 2026-10-26).

Projekt: docs/architecture/INTERFEJS_WSPOLPRACY_v2_2026-10-23.md §2.3 (kontroler emituje dane, nie prozę), §4 IW-9, W6.
0 tokenów, bez sieci, bez modelu. Dane kontroli dokumentu leżą OBOK niego: `<dokument>.check.json` (schemat
`loom.check_data/1`): decyzje, przykłady, dodatki, pomiary, znaleziska (z kontroli i z jego znaczników), werdykt, R60,
`previous` = decyzje poprzedniej wersji dokumentu (do diffu decyzji). Pola i listy są otwarte.

  python3 tools/check_data.py extract <dok> [--rev REV] [--save] [--json]
        ekstrakcja bez modelu (tania warstwa zastępcza; źródło: extract); --rev czyta dokument z gita
  python3 tools/check_data.py ingest <wynik.json> [--doc D] [--model M] [--run-id R]
        wynik check-before-use.js (cały `return`, jeden element `checks`/`data` albo lista) → plik obok dokumentu
  python3 tools/check_data.py render <dok> [--against REV] [--json]
        diff decyzji (W6): + nowe, ~ zmienione, − usunięte; znaleziska otwarte; pełny tekst = ścieżka dokumentu
  python3 tools/check_data.py markers --doc D [--inbox spec_inbox.json] [--gestures docs/spec/gestures_state.json]
        jego znaczniki (klasa `marker` ze skrzynki, aktywne znaczniki z words.py) → znaleziska pochodzenia `marker`
  python3 tools/check_data.py addendum --doc D [--run-id R]
        otwarte znaleziska → bieg `addendum.apply` (zapis loom.routine_run/1, stan paused = praca dla Sonneta);
        gdy dokument ma bieg w toku, addendum CZEKA na jego koniec (agent nie jest restartowany)
  python3 tools/check_data.py resolve --doc D --finding ID --state applied|rejected [--why TEKST] [--run-id R]
  python3 tools/check_data.py page                   dane dla strony spec (glob z presetów)

Presety: tools/routines/check_data.json. Testy: python3 -m unittest tools.tests.test_check_data
"""
from __future__ import annotations

import argparse
import datetime as _dt
import glob
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
import run_record  # noqa: E402

SCHEMA_ID = "loom.check_data/1"
PRESETS_FILE = HERE / "routines" / "check_data.json"


def presets(path: str | None = None) -> dict:
    return json.loads(Path(path or PRESETS_FILE).read_text(encoding="utf-8"))


def now_iso() -> str:
    return run_record.now_iso()


def sha(text: str) -> str:
    return "sha256:" + hashlib.sha256(text.encode("utf-8")).hexdigest()


def rel(root: Path, doc: str | Path) -> str:
    p = Path(doc)
    if p.is_absolute():
        try:
            return str(p.resolve().relative_to(root.resolve()))
        except ValueError:
            return str(p)
    return str(p)


def sidecar(root: Path, doc: str, pre: dict) -> Path:
    p = root / doc
    return p.with_name(p.stem + pre.get("sidecar_suffix", ".check.json"))


def read_doc(root: Path, doc: str, rev: str | None = None) -> str:
    if rev:
        r = subprocess.run(["git", "-C", str(root), "show", f"{rev}:{doc}"], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"błąd: git show {rev}:{doc}: {r.stderr.strip()}")
        return r.stdout
    return (root / doc).read_text(encoding="utf-8")


# ---------------------------------------------------------------------------------------------- ekstrakcja (0 tokenów)

def _cells(line: str) -> list[str]:
    s = line.strip()
    if s.startswith("|"):
        s = s[1:]
    if s.endswith("|"):
        s = s[:-1]
    return [c.strip() for c in re.split(r"(?<!\\)\|", s)]


def _snip(text: str, n: int) -> str:
    t = re.sub(r"\s+", " ", text).strip()
    return t if len(t) <= n else t[: n - 1] + "…"


def extract(text: str, pre: dict) -> dict:
    """Decyzje (wiersze tabel i punkty list pod nagłówkiem decyzji), przykłady, dodatki (nagłówki), pomiary."""
    dec_re = re.compile(pre["decision_heading_re"])
    add_re = re.compile(pre["addition_heading_re"])
    rev_re = re.compile(pre["reversal_header_re"])
    ex_re = re.compile(pre["example_re"])
    me_re = re.compile(pre["measurement_re"])
    n = int(pre.get("snippet_chars", 220))
    caps = pre.get("max_items", {})
    out = {"decisions": [], "examples": [], "additions": [], "measurements": []}
    section, in_dec, header, fence = "", False, None, False
    for i, line in enumerate(text.splitlines(), 1):
        if line.strip().startswith("```"):
            fence = not fence
            continue
        if fence:
            continue
        m = re.match(r"^(#{1,6})\s+(.*)", line)
        if m:
            section = m.group(2).strip()
            in_dec = bool(dec_re.search(section))
            header = None
            if add_re.search(section):
                out["additions"].append({"text": _snip(section, n), "line": i, "section": section})
            continue
        if in_dec and line.lstrip().startswith("|"):
            cells = _cells(line)
            if all(re.fullmatch(r":?-{2,}:?", c or "--") for c in cells):
                continue
            if header is None:
                header = cells
                continue
            d = {"text": cells[0], "line": i, "section": section}
            if len(cells) > 1 and header and len(header) > 1 and not rev_re.search(header[1]):
                d["why"] = cells[1]
            for h, c in zip(header, cells):
                if rev_re.search(h):
                    d["reversal"] = c
            out["decisions"].append(d)
            continue
        if in_dec and re.match(r"^\s*(?:[-*]|\d+[.)])\s+\S", line):
            out["decisions"].append({"text": _snip(re.sub(r"^\s*(?:[-*]|\d+[.)])\s+", "", line), 400), "line": i,
                                     "section": section})
            continue
        if not line.lstrip().startswith("|"):
            header = None
        if ex_re.search(line) and len(out["examples"]) < int(caps.get("examples", 400)):
            out["examples"].append({"text": _snip(line.strip("|# "), n), "line": i, "section": section})
        if len(out["measurements"]) < int(caps.get("measurements", 400)):
            for mm in me_re.finditer(line):
                a, b = max(0, mm.start() - 80), min(len(line), mm.end() + 60)
                out["measurements"].append({"value": mm.group(0).strip(), "text": _snip(line[a:b].strip("| "), n),
                                            "line": i, "section": section})
    return out


# ------------------------------------------------------------------------------------------------- zapis obok dokumentu

def finding_id(f: dict) -> str:
    key = "\x1f".join(str(f.get(k, "")) for k in ("origin", "pattern", "quote", "node", "text"))
    return "f_" + hashlib.sha256(key.encode("utf-8")).hexdigest()[:10]


def load_sidecar(root: Path, doc: str, pre: dict) -> dict | None:
    p = sidecar(root, doc, pre)
    if not p.exists():
        return None
    return json.loads(p.read_text(encoding="utf-8"))


def save_data(root: Path, doc: str, data: dict, pre: dict, keep_states: bool = True) -> Path:
    """Scala z istniejącym plikiem: znaleziska po id (stan zachowany, gdy keep_states — nowy wynik kontroli nie cofa
    rozstrzygnięć), poprzednia wersja dokumentu → previous. keep_states=False: zapis zmiany stanu (resolve/addendum)."""
    p = sidecar(root, doc, pre)
    old = load_sidecar(root, doc, pre)
    data = {"schema": SCHEMA_ID, "doc": doc, **data}
    for f in data.get("findings", []):
        f.setdefault("origin", "check")
        f.setdefault("state", "open")
        f.setdefault("id", finding_id(f))
    if old:
        if old.get("doc_hash") != data.get("doc_hash"):
            data["previous"] = {k: old.get(k) for k in ("doc_hash", "checked_at", "source", "decisions", "rev")}
        elif old.get("previous") and "previous" not in data:
            data["previous"] = old["previous"]
        known = {f["id"]: f for f in data.get("findings", [])}
        for f in old.get("findings", []):
            if f["id"] in known:
                if not keep_states:
                    continue
                for k in ("state", "why_state", "addendum_run", "resolved_at"):
                    if k in f:
                        known[f["id"]][k] = f[k]
            elif f.get("origin") == "marker" or f.get("state") != "open" or old.get("doc_hash") == data.get("doc_hash"):
                data.setdefault("findings", []).append(f)
        hist = list(old.get("_history", []))
        hist.append({"at": old.get("checked_at"), "source": old.get("source"), "doc_hash": old.get("doc_hash")})
        data["_history"] = hist[-20:]
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    tmp.replace(p)
    return p


def from_extract(root: Path, doc: str, pre: dict, rev: str | None = None) -> dict:
    text = read_doc(root, doc, rev)
    d = extract(text, pre)
    return {"doc_hash": sha(text), "rev": rev, "checked_at": now_iso(),
            "source": {"kind": "extract", "model": None, "tokens": 0}, **d,
            "findings": [], "r60": None, "verdict": None}


def normalize_check(c: dict, root: Path, model: str | None, run_id: str | None) -> tuple[str, dict]:
    doc = rel(root, c.get("doc", ""))
    if not doc:
        raise SystemExit("błąd: wynik kontroli bez pola doc (podaj --doc)")
    text = (root / doc).read_text(encoding="utf-8") if (root / doc).exists() else ""
    d = {"doc_hash": sha(text) if text else c.get("doc_hash"), "checked_at": c.get("checked_at") or now_iso(),
         "source": {"kind": "check", "model": model or c.get("model") or "haiku", "run_id": run_id}}
    for k in ("decisions", "examples", "additions", "measurements", "findings"):
        d[k] = list(c.get(k) or [])
    d["r60"] = c.get("r60_answer", c.get("r60"))
    d["verdict"] = c.get("verdict")
    for k, v in c.items():  # pola spoza listy zostają (lista otwarta)
        if k not in d and k not in ("doc", "r60_answer"):
            d[k] = v
    return doc, d


def iter_checks(obj) -> list[dict]:
    if isinstance(obj, list):
        return [x for o in obj for x in iter_checks(o)]
    if isinstance(obj, dict):
        for key in ("data", "checks"):
            if isinstance(obj.get(key), list) and obj[key] and isinstance(obj[key][0], dict) and "doc" in obj[key][0] \
                    and any(k in obj[key][0] for k in ("findings", "decisions")):
                return [x for x in obj[key]]
        if "doc" in obj:
            return [obj]
    return []


# ---------------------------------------------------------------------------------------------------------- diff (W6)

def _key(text: str) -> str:
    t = re.sub(r"[`*_„”\"'()\[\]]", "", text.lower())
    return re.sub(r"\s+", " ", t).strip()[:48]


def diff_decisions(prev: list[dict], cur: list[dict]) -> dict:
    pk = {_key(d.get("text", "")): d for d in prev}
    ck = {_key(d.get("text", "")): d for d in cur}
    added = [d for k, d in ck.items() if k not in pk]
    removed = [d for k, d in pk.items() if k not in ck]
    changed, same = [], []
    for k, d in ck.items():
        if k in pk:
            o = pk[k]
            fields = [f for f in ("text", "why", "reversal") if (o.get(f) or "") != (d.get(f) or "")]
            (changed if fields else same).append({"now": d, "before": o, "fields": fields} if fields else d)
    return {"added": added, "changed": changed, "removed": removed, "unchanged": len(same)}


def render(root: Path, doc: str, pre: dict, against: str | None = None) -> dict:
    data = load_sidecar(root, doc, pre)
    if data is None:
        data = from_extract(root, doc, pre)
    if against:
        prev = from_extract(root, doc, pre, rev=against)["decisions"]
        prev_label = f"git {against}"
    else:
        prev = ((data.get("previous") or {}).get("decisions")) or []
        prev_label = "poprzednia kontrola" if data.get("previous") else "brak poprzedniej wersji (wszystkie decyzje nowe)"
    dd = diff_decisions(prev, data.get("decisions", []))
    open_f = [f for f in data.get("findings", []) if f.get("state", "open") in ("open", "queued")]
    return {"doc": doc, "against": prev_label, "source": data.get("source"), "checked_at": data.get("checked_at"),
            "verdict": data.get("verdict"), "r60": data.get("r60"), "diff": dd, "findings_open": open_f,
            "counts": {k: len(data.get(k, [])) for k in ("decisions", "examples", "additions", "measurements", "findings")},
            "full_text": doc}


def render_md(r: dict) -> str:
    d = r["diff"]
    L = [f"# Diff decyzji: {r['doc']}", "",
         f"Względem: {r['against']} · źródło danych: {(r.get('source') or {}).get('kind')} · {r.get('checked_at')}",
         f"Decyzje: +{len(d['added'])} nowe · ~{len(d['changed'])} zmienione · −{len(d['removed'])} usunięte · "
         f"={d['unchanged']} bez zmian", ""]
    for x in d["added"]:
        L.append(f"+ {x.get('text')}" + (f"  — odwrót: {x['reversal']}" if x.get("reversal") else "") + f"  (l. {x.get('line')})")
    for x in d["changed"]:
        L.append(f"~ {x['now'].get('text')}  [{', '.join(x['fields'])}] (l. {x['now'].get('line')})")
        for f in x["fields"]:
            L.append(f"    było {f}: {x['before'].get(f) or '—'}")
            L.append(f"    jest {f}: {x['now'].get(f) or '—'}")
    for x in d["removed"]:
        L.append(f"− {x.get('text')}")
    if r["findings_open"]:
        L += ["", f"Znaleziska otwarte ({len(r['findings_open'])}):"]
        for f in r["findings_open"]:
            L.append(f"! [{f.get('origin')}/{f.get('state')}] l. {f.get('line', '?')} {f.get('pattern', '')}: "
                     f"{_snip(f.get('quote') or f.get('text') or '', 160)}")
    if r.get("verdict"):
        L += ["", f"Werdykt kontroli: {r['verdict']}"]
    L += ["", f"Pełny tekst: {r['full_text']}"]
    return "\n".join(L) + "\n"


# ----------------------------------------------------------------------------------------------- znaczniki i addendum

def marker_findings(root: Path, doc: str, pre: dict, inbox: list[dict] | None, gestures: dict | None) -> list[dict]:
    """Znacznik dotyczy dokumentu, gdy jego `node`/`doc`/tekst wskazuje ścieżkę albo nazwę pliku dokumentu."""
    name = Path(doc).name
    stem = Path(doc).stem
    classes = set(pre.get("marker_inbox_classes", ["marker"]))

    def about(e: dict) -> bool:
        hay = " ".join(str(e.get(k) or "") for k in ("node", "doc", "target", "text", "subject"))
        return doc in hay or name in hay or stem in hay

    out = []
    for e in inbox or []:
        if e.get("class") in classes and about(e):
            out.append({"origin": "marker", "pattern": pre.get("marker_pattern_label", "marker"),
                        "severity": e.get("severity", "blocking"), "text": e.get("text"), "quote": e.get("quote"),
                        "line": e.get("line"), "node": e.get("node"), "at": e.get("at"), "device": e.get("device"),
                        "inbox_id": e.get("id") or e.get("_id"), "why": "jego znacznik (skrzynka spec_inbox)"})
    for m in (gestures or {}).get("markers", []):
        if m.get("state", "active") == "active" and about(m):
            out.append({"origin": "marker", "pattern": f"{pre.get('marker_pattern_label', 'marker')}: {m.get('marker')}",
                        "severity": "blocking", "text": m.get("text") or m.get("statement"), "node": m.get("node"),
                        "at": m.get("at"), "use_id": m.get("use_id"), "why": "jego słowo sterujące (words.py)"})
    for f in out:
        f["state"] = "open"
        f["id"] = finding_id(f)
    return out


def add_markers(root: Path, doc: str, pre: dict, found: list[dict]) -> tuple[Path, int]:
    data = load_sidecar(root, doc, pre)
    if data is None:
        data = from_extract(root, doc, pre)
    data = {k: v for k, v in data.items() if k not in ("schema", "doc")}
    have = {f["id"] for f in data.get("findings", [])}
    new = [f for f in found if f["id"] not in have]
    data.setdefault("findings", []).extend(new)
    return save_data(root, doc, data, pre, keep_states=False), len(new)


def running_runs_about(root: Path, doc: str, pre: dict) -> list[dict]:
    cfg = run_record.load_config()
    states = set((pre.get("addendum") or {}).get("running_target_states", ["running", "paused"]))
    out = []
    for rid, row in run_record.read_index(root, cfg).items():
        if row.get("state") not in states or row.get("routine") == (pre.get("addendum") or {}).get("routine"):
            continue
        try:
            rec, _ = run_record.load(root, cfg, rid)
        except (SystemExit, FileNotFoundError, OSError):
            continue
        if doc in json.dumps(rec.get("params") or {}, ensure_ascii=False):
            out.append({"run_id": rid, "state": row.get("state"), "routine": row.get("routine")})
    return out


def addendum(root: Path, doc: str, pre: dict, run_id: str | None = None) -> dict:
    """Otwarte znaleziska → rekord biegu addendum.apply. Nic nie wykonuje (praca dla Sonneta, krok handoff)."""
    cfg = run_record.load_config()
    data = load_sidecar(root, doc, pre)
    if data is None:
        raise SystemExit(f"błąd: brak danych kontroli dla {doc} (najpierw ingest/extract --save/markers)")
    todo = [f for f in data.get("findings", []) if f.get("state", "open") == "open"]
    ad = pre.get("addendum") or {}
    waits = running_runs_about(root, doc, pre)
    if not todo:
        return {"doc": doc, "run_id": None, "findings": 0, "note": "brak otwartych znalezisk"}
    if run_id is None:
        run_id = run_record.start(root, cfg, ad.get("routine", "addendum.apply"),
                                  params={"doc": doc, "findings": [f["id"] for f in todo], "role": ad.get("model_role")},
                                  where=None, text=f"addendum: {doc}, {len(todo)} znalez.")["run_id"]
    else:
        run_record.set_field(root, cfg, run_id, "params", {"doc": doc, "findings": [f["id"] for f in todo],
                                                           "role": ad.get("model_role")})
    for f in todo:
        run_record.add_to_list(root, cfg, run_id, "escalations" if f.get("severity") == "escalate" else "advisories",
                               {"finding": f["id"], "origin": f.get("origin"), "line": f.get("line"),
                                "pattern": f.get("pattern"), "text": _snip(f.get("quote") or f.get("text") or "", 200)})
        f["state"] = "queued"
        f["addendum_run"] = run_id
    note = None
    if waits:
        note = "; ".join(ad.get("note_running", "czeka na {run_id}").format(run_id=w["run_id"]) for w in waits)
        run_record.set_field(root, cfg, run_id, "waits_for", [w["run_id"] for w in waits])
    run_record.add_step(root, cfg, run_id, "apply", state="paused" if waits else "handoff",
                        extra={"do": "handoff", "role": ad.get("model_role"),
                               "text": f"Sonnet: zastosuj znaleziska {[f['id'] for f in todo]} w {doc} (najmniejsze "
                                       f"przeformułowanie; odrzucone z powodem: check_data.py resolve)" + (f"; {note}" if note else "")})
    run_record.add_event(root, cfg, run_id, "addendum_queued", text=f"{doc}: {len(todo)} znalez." + (f" — {note}" if note else ""))
    run_record.set_field(root, cfg, run_id, "state", "paused")
    payload = {k: v for k, v in data.items() if k not in ("schema", "doc")}
    save_data(root, doc, payload, pre, keep_states=False)
    return {"doc": doc, "run_id": run_id, "findings": len(todo), "waits_for": [w["run_id"] for w in waits], "note": note}


def resolve(root: Path, doc: str, pre: dict, fid: str, state: str, why: str | None, run_id: str | None) -> dict:
    data = load_sidecar(root, doc, pre)
    if data is None:
        raise SystemExit(f"błąd: brak danych kontroli dla {doc}")
    hit = [f for f in data.get("findings", []) if f["id"] == fid or f["id"].startswith(fid)]
    if len(hit) != 1:
        raise SystemExit(f"błąd: znalezisko {fid}: {len(hit)} trafień")
    f = hit[0]
    f["state"], f["resolved_at"] = state, now_iso()
    if why:
        f["why_state"] = why
    payload = {k: v for k, v in data.items() if k not in ("schema", "doc")}
    save_data(root, doc, payload, pre, keep_states=False)
    rid = run_id or f.get("addendum_run")
    if rid:
        cfg = run_record.load_config()
        run_record.add_event(root, cfg, rid, f"finding_{state}", text=f"{f['id']}" + (f": {why}" if why else ""))
        left = [x for x in data["findings"] if x.get("addendum_run") == rid and x.get("state") in ("open", "queued")]
        if not left:
            run_record.finish(root, cfg, rid, state="done", summary=f"addendum {doc}: wszystkie znaleziska rozstrzygnięte")
    return {"id": f["id"], "state": state}


def page_data(root: Path, pre: dict) -> list[dict]:
    out = []
    for pat in pre.get("page_glob", []):
        for p in sorted(glob.glob(str(root / pat))):
            data = json.loads(Path(p).read_text(encoding="utf-8"))
            doc = data.get("doc") or ""
            r = render(root, doc, pre)
            full = ""
            if (root / doc).exists():
                full = (root / doc).read_text(encoding="utf-8")[: int(pre.get("page_full_text_max_chars", 60000))]
            out.append({**r, "full": full})
    return out


# ----------------------------------------------------------------------------------------------------------------- CLI

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(REPO))
    ap.add_argument("--presets")
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("extract"); e.add_argument("doc"); e.add_argument("--rev"); e.add_argument("--save", action="store_true")
    e.add_argument("--json", action="store_true")
    i = sub.add_parser("ingest"); i.add_argument("file"); i.add_argument("--doc"); i.add_argument("--model"); i.add_argument("--run-id")
    r = sub.add_parser("render"); r.add_argument("doc"); r.add_argument("--against"); r.add_argument("--json", action="store_true")
    m = sub.add_parser("markers"); m.add_argument("--doc", required=True); m.add_argument("--inbox"); m.add_argument("--gestures")
    a = sub.add_parser("addendum"); a.add_argument("--doc", required=True); a.add_argument("--run-id")
    s = sub.add_parser("resolve"); s.add_argument("--doc", required=True); s.add_argument("--finding", required=True)
    s.add_argument("--state", required=True); s.add_argument("--why"); s.add_argument("--run-id")
    sub.add_parser("page")
    args = ap.parse_args(argv)
    root = Path(args.root).resolve()
    pre = presets(args.presets)
    doc = rel(root, getattr(args, "doc", None) or "") if getattr(args, "doc", None) else None

    if args.cmd == "extract":
        d = from_extract(root, doc, pre, args.rev)
        if args.save:
            print(save_data(root, doc, d, pre))
        if args.json or not args.save:
            print(json.dumps({"doc": doc, **d} if args.json else {k: len(v) if isinstance(v, list) else v for k, v in d.items()},
                             ensure_ascii=False, indent=1))
        return 0
    if args.cmd == "ingest":
        obj = json.loads(Path(args.file).read_text(encoding="utf-8"))
        checks = iter_checks(obj)
        if not checks and args.doc:
            checks = [{**obj, "doc": args.doc}]
        if not checks:
            print("błąd: nie rozpoznano wyniku kontroli (oczekiwane pola doc + findings/decisions)", file=sys.stderr)
            return 2
        for c in checks:
            if args.doc:
                c = {**c, "doc": args.doc}
            d, data = normalize_check(c, root, args.model, args.run_id)
            print(save_data(root, d, data, pre))
        return 0
    if args.cmd == "render":
        r = render(root, doc, pre, args.against)
        print(json.dumps(r, ensure_ascii=False, indent=1) if args.json else render_md(r), end="" if not args.json else "\n")
        return 0
    if args.cmd == "markers":
        inbox = json.loads(Path(args.inbox).read_text(encoding="utf-8")) if args.inbox else []
        if isinstance(inbox, dict):
            inbox = inbox.get("items") or inbox.get("docs") or inbox.get("entries") or []
        gp = Path(args.gestures) if args.gestures else root / "docs/spec/gestures_state.json"
        gestures = json.loads(gp.read_text(encoding="utf-8")) if gp.exists() else {}
        found = marker_findings(root, doc, pre, inbox, gestures)
        p, n = add_markers(root, doc, pre, found)
        print(json.dumps({"doc": doc, "markers": len(found), "new": n, "file": str(p)}, ensure_ascii=False))
        return 0
    if args.cmd == "addendum":
        print(json.dumps(addendum(root, doc, pre, args.run_id), ensure_ascii=False))
        return 0
    if args.cmd == "resolve":
        print(json.dumps(resolve(root, doc, pre, args.finding, args.state, args.why, args.run_id), ensure_ascii=False))
        return 0
    if args.cmd == "page":
        print(json.dumps(page_data(root, pre), ensure_ascii=False))
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
