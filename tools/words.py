#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Słowa sterujące i gesty (IW-5, 2026-10-26): rozpoznaje słowa właściciela w wypowiedzi i stosuje skutek BEZ MODELU.

Tabela: tools/routines/words.json (rekord gestu {gesture, patterns, effect, cost, reversal, since, render}; wszystko presety
i przykłady, lista otwarta). Projekt: docs/architecture/INTERFEJS_WSPOLPRACY_v2_2026-10-23.md §2.4, §2.9, §4 IW-5.

  match   TEKST                         które gesty trafia (warianty ASR: bez polskich znaków, interpunkcji, wielkości liter)
  apply   TEKST [--node ID] [--by K] [--at ISO] [--device D] [--confirmed] [--dry-run] [--run-id R]
                                        stosuje skutki z kosztem ≤ próg; droższe → „needs_confirm” z kosztem; każde użycie
                                        zapisane z pochodzeniem (uses_log); stan (mandaty, stop, znaczniki…) w state_file
  inbox   PLIK|-                        wpisy spec_inbox klas z presetu inbox_classes → apply; wypisuje werdykty JSON
  reverse USE_ID [--by K]               odwrót użycia (mandat wycofany, stop wycofany, znacznik zdjęty, …)
  add     --id ID --gesture G --pattern P [...] --do RODZAJ [--quote Q --spec S]   on dopisuje słowo jednym zdaniem
  render  [--surface phone]             przyciski/makra danej powierzchni, w kolejności presetu
  stats   [--since ISO]                 trafienia/chybienia/słabe, mandaty i odwroty na tydzień
  check                                 walidacja tabeli (pola, kolizje wzorców — advisory)
Ścieżki (state_file, uses_log, runs_index) są względem --root (preset: korzeń repo; env WORDS_ROOT). Tabela: --table / WORDS_TABLE.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import os
import re
import subprocess
import sys
import unicodedata
import uuid
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
TABLE = HERE / "routines" / "words.json"
EXTRA = str.maketrans({"ł": "l", "Ł": "L", "ß": "ss", "ø": "o", "đ": "d"})


def now_utc() -> str:
    return _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def fold_char(c: str, f: dict) -> str:
    if f.get("strip_punctuation", True) and unicodedata.category(c)[0] in "PS":
        return " "
    if f.get("lowercase", True):
        c = c.lower()
    if f.get("strip_diacritics", True):
        c = "".join(x for x in unicodedata.normalize("NFKD", c.translate(EXTRA)) if not unicodedata.combining(x))
    return c


def fold(text: str, f: dict | None = None) -> str:
    out = "".join(fold_char(c, f or {}) for c in text or "")
    return re.sub(r"\s+", " ", out).strip()


def _folded_with_map(text: str, f: dict):
    chars, idx = [], []
    for i, c in enumerate(text):
        for x in fold_char(c, f):
            chars.append(x)
            idx.append(i)
    return "".join(chars), idx


def _pattern_rx(p: str, f: dict):
    if p.startswith("re:"):
        return re.compile(p[3:])
    words = fold(p, f).split()
    return re.compile(r"(?<!\w)" + r"\s+".join(map(re.escape, words)) + r"(?!\w)") if words else None


def match(table: dict, text: str) -> list:
    pre = table.get("presets") or {}
    f = pre.get("fold") or {}
    folded, idx = _folded_with_map(text or "", f)
    nwords = len(fold(text, f).split())
    hits = []
    for g in table.get("gestures") or []:
        best = None
        for p in g.get("patterns") or []:
            rx = _pattern_rx(p, f)
            m = rx.search(folded) if rx else None
            if m and (best is None or (m.end() - m.start()) > (best[1].end() - best[1].start())):
                best = (p, m)
        if not best:
            continue
        p, m = best
        end = idx[m.end() - 1] + 1 if m.end() > 0 else 0
        weak = bool(g.get("standalone")) and nwords > int(pre.get("standalone_max_words", 6))
        hits.append({"gesture": g["id"], "label": g.get("gesture"), "pattern": p, "weak": weak,
                     "remainder": text[end:].strip(" \t\n,.;:!?-—"), "at_char": idx[m.start()] if idx else 0})
    # wzorzec zawarty w dłuższym trafieniu innego gestu (np. „masz wolną rękę” vs „wolna ręka”) zostaje — to różne gesty
    return sorted(hits, key=lambda h: h["at_char"])


class Store:
    def __init__(self, root: Path, pre: dict):
        self.root = root
        self.state_path = root / pre.get("state_file", "docs/spec/gestures_state.json")
        self.uses_path = root / pre.get("uses_log", "docs/reports/runs/words_uses.jsonl")
        self.runs_index = root / pre.get("runs_index", "docs/reports/runs/runs.jsonl")

    def state(self) -> dict:
        try:
            return json.loads(self.state_path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return {"schema": "loom.gestures_state/1", "mandates": [], "stops": [], "markers": [], "requests": [],
                    "preferences": {}, "presence": None}

    def save(self, st: dict) -> None:
        self.state_path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.state_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(st, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        tmp.replace(self.state_path)

    def log(self, rec: dict) -> None:
        self.uses_path.parent.mkdir(parents=True, exist_ok=True)
        with self.uses_path.open("a", encoding="utf-8") as fh:
            fh.write(json.dumps(rec, ensure_ascii=False) + "\n")

    def uses(self) -> list:
        if not self.uses_path.exists():
            return []
        return [json.loads(x) for x in self.uses_path.read_text(encoding="utf-8").splitlines() if x.strip()]

    def running(self) -> list:
        last = {}
        if self.runs_index.exists():
            for line in self.runs_index.read_text(encoding="utf-8").splitlines():
                try:
                    r = json.loads(line)
                except json.JSONDecodeError:
                    continue
                last[r.get("run_id")] = r
        out = []
        for r in last.values():
            if r.get("state") != "running":
                continue
            sums = defaultdict(float)
            try:
                full = json.loads((self.root / r["path"]).read_text(encoding="utf-8"))
                for x in full.get("receipt") or []:
                    if isinstance(x.get("value"), (int, float)):
                        sums[f"{x.get('currency')} [{x.get('unit') or ''}]".replace(" []", "")] += x["value"]
            except (OSError, KeyError, json.JSONDecodeError):
                pass
            out.append({"run_id": r.get("run_id"), "title": r.get("title"), "where": r.get("where"),
                        "started": r.get("started"), "sunk": dict(sums)})
        return out


def _until(at: str, spec: str) -> str | None:
    m = re.fullmatch(r"(\d+)\s*([mhd])", (spec or "").strip())
    if not m:
        return spec or None
    n, u = int(m.group(1)), m.group(2)
    d = {"m": _dt.timedelta(minutes=n), "h": _dt.timedelta(hours=n), "d": _dt.timedelta(days=n)}[u]
    return (_dt.datetime.fromisoformat(at.replace("Z", "+00:00")) + d).strftime("%Y-%m-%dT%H:%M:%SZ")


def apply_effect(e: dict, g: dict, hit: dict, ctx: dict, st: dict, store: Store, pre: dict, root: Path, dry: bool) -> dict:
    kind = e.get("do")
    base = {"use_id": ctx["use_id"], "gesture": g["id"], "at": ctx["at"], "by": ctx["by"], "text": ctx["text"]}
    rem = hit.get("remainder") or ""
    if kind == "stop_request":
        running = store.running()
        st["stops"].append({**base, "scope": rem or "wszystko w toku", "running": running, "state": "requested"})
        return {"do": kind, "applied": True, "running": running,
                "note": "zapisano prośbę; procesy zatrzymuje koordynator/runner (gest sam niczego nie zabija)"}
    if kind == "mandate":
        mid = "md_" + uuid.uuid4().hex[:12]
        st["mandates"].append({**base, "id": mid, "kind": "mandate", "mode": e.get("mode"),
                               "scope": rem or pre.get("mandate_default_scope"),
                               "until": _until(ctx["at"], e.get("until") or pre.get("mandate_default_until", "24h")),
                               "exceptions_ref": pre.get("mandate_exceptions_ref"), "state": "active"})
        return {"do": kind, "applied": True, "mandate": mid}
    if kind == "presence":
        st["presence"] = {**base, "state": e.get("state")}
        return {"do": kind, "applied": True, "state": e.get("state")}
    if kind == "preference":
        prev = st["preferences"].get(e.get("key"))
        st["preferences"][e.get("key")] = {**base, "value": e.get("value"), "previous": prev}
        return {"do": kind, "applied": True, "key": e.get("key"), "value": e.get("value")}
    if kind == "marker":
        if not ctx.get("node"):
            return {"do": kind, "applied": False, "escalated": "znacznik bez pozycji (--node)"}
        st["markers"].append({**base, "marker": e.get("marker"), "node": ctx["node"], "state": "active"})
        return {"do": kind, "applied": True, "node": ctx["node"]}
    if kind in ("request", "routine_request"):
        st["requests"].append({**base, "kind": kind, "class": e.get("class") or ("routine" if kind == "routine_request" else None),
                               "subject": rem, "to": e.get("to", "coordinator"), "state": "new"})
        out = {"do": kind, "applied": True, "subject": rem}
        if kind == "routine_request":
            out["next"] = "python3 tools/start_gate.py --role <rola> --used <%> (karta przed biegiem)"
        return out
    if kind == "lookup":
        return {"do": kind, "applied": True, "read_only": True, "command": f"{e.get('command', '')} {rem}".strip()}
    if kind == "spec_command":
        if not ctx.get("node"):
            return {"do": kind, "applied": False, "escalated": "brak pozycji spec (--node)"}
        argv = [sys.executable, str(HERE / "spec.py"), "--root", str(root), e.get("command"), ctx["node"], "--by", ctx["by"]]
        if e.get("command") == "correct":
            if not rem:
                return {"do": kind, "applied": False, "escalated": "popraw bez treści"}
            argv += ["--text", rem]
        if dry:
            return {"do": kind, "applied": False, "would_run": argv[1:]}
        r = subprocess.run(argv, capture_output=True, text=True)
        return {"do": kind, "applied": r.returncode == 0, "rc": r.returncode, "stderr": r.stderr.strip()[-300:]}
    return {"do": kind, "applied": False, "escalated": f"nieznany rodzaj skutku „{kind}” (najgorszy przypadek: eskalacja)"}


def do_apply(table: dict, root: Path, text: str, *, node=None, by="owner", at=None, device=None, confirmed=False,
             dry=False, run_id=None, runs_root=None) -> dict:
    pre = table.get("presets") or {}
    store = Store(root, pre)
    st = store.state()
    ctx = {"use_id": "u_" + uuid.uuid4().hex[:12], "at": at or now_utc(), "by": by, "text": text, "node": node}
    gestures = {g["id"]: g for g in table.get("gestures") or []}
    hits = match(table, text)
    results = []
    threshold = float(pre.get("confirm_if_points_above", 0.25))
    for h in hits:
        g = gestures[h["gesture"]]
        pts = float((g.get("cost") or {}).get("points") or 0)
        r = {"gesture": g["id"], "label": g.get("gesture"), "pattern": h["pattern"], "cost": g.get("cost"),
             "reversal": g.get("reversal"), "effects": []}
        if h["weak"]:
            r["verdict"] = "weak"
            r["why"] = f"krótkie słowo w dłuższej wypowiedzi (> {pre.get('standalone_max_words', 6)} słów) — nie stosuję, eskaluję"
        elif pts > threshold and not confirmed:
            r["verdict"] = "needs_confirm"
            r["why"] = f"koszt {pts} pkt > próg {threshold} (presets.confirm_if_points_above); potwierdź: --confirmed"
        else:
            r["effects"] = [apply_effect(e, g, h, ctx, st, store, pre, root, dry) for e in g.get("effect") or []]
            r["verdict"] = "applied" if r["effects"] and all(x.get("applied") for x in r["effects"]) else (
                "partial" if any(x.get("applied") for x in r["effects"]) else "escalated")
        results.append(r)
    use = {"use_id": ctx["use_id"], "at": ctx["at"], "by": by, "device": device, "node": node, "text": text,
           "hits": [{"gesture": r["gesture"], "pattern": r["pattern"], "verdict": r["verdict"]} for r in results],
           "outcome": "miss" if not results else "hit", "dry_run": dry}
    if not dry:
        if any(r["effects"] for r in results):
            store.save(st)
        store.log(use)
        if run_id:
            rr = [sys.executable, str(HERE / "run_record.py")] + (["--root", runs_root] if runs_root else [])
            for r in results:
                subprocess.run(rr + ["event", run_id, "--kind", "gesture", "--text", f"{r['gesture']}: {r['verdict']}"],
                               stdout=subprocess.DEVNULL, check=False)
    return {"use_id": ctx["use_id"], "outcome": use["outcome"], "results": results}


def do_reverse(table: dict, root: Path, use_id: str, by: str, at: str | None) -> dict:
    store = Store(root, table.get("presets") or {})
    st = store.state()
    at = at or now_utc()
    n = 0
    for key in ("mandates", "stops", "markers", "requests"):
        for x in st.get(key) or []:
            if x.get("use_id") == use_id and x.get("state") not in ("reversed",):
                x.update(state="reversed", reversed_by=by, reversed_at=at)
                n += 1
    for k, v in list((st.get("preferences") or {}).items()):
        if isinstance(v, dict) and v.get("use_id") == use_id:
            if v.get("previous") is None:
                del st["preferences"][k]
            else:
                st["preferences"][k] = v["previous"]
            n += 1
    if isinstance(st.get("presence"), dict) and st["presence"].get("use_id") == use_id:
        st["presence"] = None
        n += 1
    store.save(st)
    store.log({"use_id": "u_" + uuid.uuid4().hex[:12], "at": at, "by": by, "reverses": use_id, "outcome": "reversal",
               "count": n})
    return {"reversed": use_id, "entries": n}


def do_stats(table: dict, root: Path, since: str | None) -> dict:
    store = Store(root, table.get("presets") or {})
    c, per_g, weeks = Counter(), Counter(), defaultdict(Counter)
    for u in store.uses():
        if since and u.get("at", "") < since:
            continue
        if u.get("dry_run"):
            continue
        wk = "%d-W%02d" % _dt.date.fromisoformat(u["at"][:10]).isocalendar()[:2]
        c[u.get("outcome")] += 1
        if u.get("outcome") == "reversal":
            weeks[wk]["reversals"] += 1
        for h in u.get("hits") or []:
            per_g[f"{h['gesture']}:{h['verdict']}"] += 1
            if h["verdict"] == "applied" and any(e.get("do") == "mandate" for g in table["gestures"] if g["id"] == h["gesture"]
                                                 for e in g.get("effect") or []):
                weeks[wk]["mandates"] += 1
    return {"uses": dict(c), "per_gesture": dict(per_g), "per_week": {k: dict(v) for k, v in sorted(weeks.items())}}


def do_check(table: dict) -> dict:
    errors, adv = [], []
    f = (table.get("presets") or {}).get("fold") or {}
    seen = {}
    ids = set()
    for g in table.get("gestures") or []:
        gid = g.get("id")
        if not gid or gid in ids:
            errors.append(f"gest bez id albo powtórzone id: {gid}")
        ids.add(gid)
        for k in ("gesture", "patterns", "effect", "cost", "reversal", "since", "render"):
            if k not in g:
                errors.append(f"{gid}: brak pola {k}")
        for p in g.get("patterns") or []:
            fp = p if p.startswith("re:") else fold(p, f)
            if fp in seen and seen[fp] != gid:
                adv.append(f"wzorzec „{p}” w dwóch gestach: {seen[fp]} i {gid} (świadomie?)")
            seen[fp] = gid
        known = set((table.get("presets") or {}).get("effect_kinds_examples") or [])
        for e in g.get("effect") or []:
            if e.get("do") not in known:
                adv.append(f"{gid}: rodzaj skutku „{e.get('do')}” spoza przykładów — words.py go eskaluje (najgorszy przypadek)")
        if not g.get("quote") and not g.get("spec"):
            adv.append(f"{gid}: bez cytatu i bez pozycji spec — skąd to słowo?")
    return {"ok": not errors, "errors": errors, "advisories": adv, "gestures": len(ids)}


def do_render(table: dict, surface: str) -> list:
    out = []
    for g in table.get("gestures") or []:
        for r in g.get("render") or []:
            if r.get("surface") == surface:
                out.append({"gesture": g["id"], "label": r.get("label") or g.get("gesture"), "kind": r.get("kind"),
                            "order": r.get("order"), "cost": (g.get("cost") or {}).get("class"), "shows": r.get("shows")})
    return sorted(out, key=lambda x: (x["order"] is None, x["order"] or 0, x["label"]))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="words.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=os.environ.get("WORDS_ROOT", str(REPO)))
    ap.add_argument("--table", default=os.environ.get("WORDS_TABLE", str(TABLE)))
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("match")
    m.add_argument("text")
    for name in ("apply",):
        p = sub.add_parser(name)
        p.add_argument("text")
    ib = sub.add_parser("inbox")
    ib.add_argument("file")
    for p in (sub.choices["apply"], ib):
        p.add_argument("--node")
        p.add_argument("--by", default="owner")
        p.add_argument("--at")
        p.add_argument("--device")
        p.add_argument("--confirmed", action="store_true")
        p.add_argument("--dry-run", action="store_true")
        p.add_argument("--run-id")
        p.add_argument("--runs-root")
    rv = sub.add_parser("reverse")
    rv.add_argument("use_id")
    rv.add_argument("--by", default="owner")
    rv.add_argument("--at")
    ad = sub.add_parser("add")
    ad.add_argument("--id", required=True)
    ad.add_argument("--gesture", required=True)
    ad.add_argument("--pattern", action="append", required=True)
    ad.add_argument("--do", required=True, help="rodzaj skutku (lista otwarta; przykłady w presets.effect_kinds_examples)")
    ad.add_argument("--effect-json", help="pełny skutek jako JSON (zamiast samego --do)")
    ad.add_argument("--points", type=float, default=0.0)
    ad.add_argument("--cost-class", default="zero")
    ad.add_argument("--reversal", default="words.py reverse <use_id>")
    ad.add_argument("--quote")
    ad.add_argument("--spec")
    ad.add_argument("--surface", default="phone")
    ad.add_argument("--since")
    rd = sub.add_parser("render")
    rd.add_argument("--surface", default="phone")
    sa = sub.add_parser("stats")
    sa.add_argument("--since")
    sub.add_parser("check")
    a = ap.parse_args(argv)
    root = Path(a.root)
    table_path = Path(a.table)
    table = json.loads(table_path.read_text(encoding="utf-8"))
    pr = lambda x: print(json.dumps(x, ensure_ascii=False, indent=1))  # noqa: E731
    if a.cmd == "match":
        pr(match(table, a.text))
    elif a.cmd == "apply":
        pr(do_apply(table, root, a.text, node=a.node, by=a.by, at=a.at, device=a.device, confirmed=a.confirmed,
                    dry=a.dry_run, run_id=a.run_id, runs_root=a.runs_root))
    elif a.cmd == "inbox":
        raw = json.loads(sys.stdin.read() if a.file == "-" else Path(a.file).read_text(encoding="utf-8"))
        entries = raw.get("docs", raw) if isinstance(raw, dict) else raw
        classes = set((table.get("presets") or {}).get("inbox_classes") or [])
        out = []
        for e in entries:
            d = e.get("data") if isinstance(e.get("data"), dict) else e
            if (d.get("state") or "new") != "new" or (d.get("class") or "statement") not in classes:
                continue
            r = do_apply(table, root, d.get("text") or "", node=d.get("node") or a.node, by=a.by, at=d.get("at") or a.at,
                         device=d.get("device"), confirmed=a.confirmed, dry=a.dry_run, run_id=a.run_id, runs_root=a.runs_root)
            out.append({"doc_id": e.get("id") or d.get("id"), **r})
        pr({"verdicts": out})
    elif a.cmd == "reverse":
        pr(do_reverse(table, root, a.use_id, a.by, a.at))
    elif a.cmd == "add":
        if any(g["id"] == a.id for g in table["gestures"]):
            raise SystemExit(f"gest {a.id} już jest — edytuj wpis w {table_path}")
        g = {"id": a.id, "gesture": a.gesture, "patterns": a.pattern,
             "effect": [json.loads(a.effect_json)] if a.effect_json else [{"do": a.do}],
             "cost": {"class": a.cost_class, "points": a.points}, "reversal": a.reversal,
             "since": a.since or now_utc()[:10], "render": [{"surface": a.surface, "label": a.gesture}]}
        if a.spec:
            g["spec"] = a.spec
        if a.quote:
            g["quote"] = {"text": a.quote, "spec": a.spec}
        table["gestures"].append(g)
        table.setdefault("_history", []).append({"at": now_utc(), "by": "words.py add", "what": f"dopisany gest {a.id}"})
        table_path.write_text(json.dumps(table, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        pr(g)
    elif a.cmd == "render":
        pr(do_render(table, a.surface))
    elif a.cmd == "stats":
        pr(do_stats(table, root, a.since))
    elif a.cmd == "check":
        r = do_check(table)
        pr(r)
        return 0 if r["ok"] else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
