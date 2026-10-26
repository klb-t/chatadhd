#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Rutyny jako dane + runner v1 + brief jako rekord (pakiet IW-7, 2026-10-26).

0 tokenów, bez sieci, bez modelu. Projekt: docs/architecture/INTERFEJS_WSPOLPRACY_v2_2026-10-23.md §2.4, §4 IW-7.

  python3 tools/routine.py list                               rutyny i wersje (tools/routines/routines.json)
  python3 tools/routine.py show package.run[@1]               rutyna jako JSON
  python3 tools/routine.py check                              rutyny i briefy: schemat, kroki, wersje (uwagi)
  python3 tools/routine.py run <id[@wersja]> [--param k=v …] [--where W] [--yes] [--dry-run]
        bieg: zapis loom.routine_run/1 (tools/run_record.py), kroki po kolei; `handoff` i `confirm` bez odpowiedzi
        zatrzymują bieg w stanie paused — nic nie czeka w pamięci procesu
  python3 tools/routine.py resume <run_id> [--done KROK] [--answer KROK=yes|no] [--card karta.json] [--param k=v]
        dalszy ciąg z rekordu (runner nie ma stanu własnego; stan = rekord biegu)
  python3 tools/routine.py brief render <brief.json> [-o args.json] [--prompt]
        brief loom.brief/1 → args single-package.js: prefiks pakietu kontekstu bajt w bajt + część własna; hashe
  python3 tools/routine.py brief split <stary_brief.json> --pack <pakiet> [--own-file część.txt] [-o brief.json]
        stary brief → loom.brief/1 (pakiet kontekstu + część własna)

Rodzaje kroków (`do`, lista otwarta): cmd (polecenie, kod wyjścia → stan), brief.render, render (status-lines | queue),
confirm (bramka w kształcie pytania: if_silent, cost_to_reverse; mandat z gestures_state.json przechodzi bramkę),
handoff (praca poza runnerem: agent, osoba, zadanie zaplanowane). Nieznany rodzaj kroku = handoff z uwagą (nie błąd).
Warunki `when` i `preconditions[].check`: predykaty {path, op, value} / {all|any: […]} / {not: …} nad
{params, place, run, out, env}. Niespełniony warunek wstępny = uwaga w biegu; stop tylko gdy on_fail: stop (preset).
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
import run_record as rr  # noqa: E402
import context_pack as cp  # noqa: E402

ROUTINES = HERE / "routines" / "routines.json"
PLACES = REPO / "docs" / "ops" / "places.json"
GESTURES = REPO / "docs" / "spec" / "gestures_state.json"
ROUTINE_SCHEMA = "loom.routine/1"
BRIEF_SCHEMA = "loom.brief/1"
DONE_STATES = {"done", "skipped"}
VAR_RE = re.compile(r"\{([A-Za-z_][\w.\-]*)\}")


class RoutineError(Exception):
    pass


def sha(text: str | bytes) -> str:
    return cp.sha(text)


def load_json(p: Path) -> dict:
    return json.loads(Path(p).read_text(encoding="utf-8"))


def canon(obj) -> str:
    return json.dumps(obj, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


# ---------------------------------------------------------------- katalog rutyn

def _vkey(v) -> tuple:
    return tuple(int(x) if str(x).isdigit() else str(x) for x in str(v).split("."))


def find_routine(cat: dict, ref: str) -> dict:
    rid, _, ver = ref.partition("@")
    cands = [r for r in cat.get("routines", []) if r.get("id") == rid]
    if not cands:
        raise RoutineError(f"nie ma rutyny {rid!r} (są: {', '.join(sorted({r['id'] for r in cat.get('routines', [])}))})")
    if ver:
        hit = [r for r in cands if str(r.get("version")) == ver]
        if not hit:
            raise RoutineError(f"rutyna {rid} nie ma wersji {ver} (są: {', '.join(str(r.get('version')) for r in cands)})")
        return hit[0]
    return max(cands, key=lambda r: _vkey(r.get("version", 0)))


def routine_hash(r: dict) -> str:
    return sha(canon(r))


def check_catalogue(cat: dict) -> list[str]:
    out = []
    seen = set()
    for r in cat.get("routines", []):
        key = (r.get("id"), str(r.get("version")))
        where = f"{key[0]}@{key[1]}"
        if key in seen:
            out.append(f"{where}: powtórzona wersja")
        seen.add(key)
        for f in ("id", "version", "since", "steps"):
            if f not in r:
                out.append(f"{where}: brak pola {f}")
        if r.get("schema", ROUTINE_SCHEMA) != ROUTINE_SCHEMA:
            out.append(f"{where}: schemat {r.get('schema')} (oczekiwany {ROUTINE_SCHEMA}) — uwaga")
        ids = [s.get("id") for s in r.get("steps", [])]
        if len(ids) != len(set(ids)):
            out.append(f"{where}: powtórzone id kroków")
        for s in r.get("steps", []):
            if "id" not in s or "do" not in s:
                out.append(f"{where}: krok bez id albo do: {s}")
            elif s["do"] not in STEP_KINDS:
                out.append(f"{where}/{s['id']}: rodzaj {s['do']!r} nieznany runnerowi — wykona się jako handoff (uwaga)")
        if r.get("supersedes") and not any(x.get("id") == r["id"] and str(x.get("version")) == str(r["supersedes"])
                                           for x in cat.get("routines", [])):
            out.append(f"{where}: supersedes {r['supersedes']} — tej wersji nie ma w katalogu (stara powinna zostać)")
    return out


# ---------------------------------------------------------------- predykaty i szablony

def get_path(ctx: dict, path: str):
    cur = ctx
    for part in path.split("."):
        if isinstance(cur, dict) and part in cur:
            cur = cur[part]
        elif isinstance(cur, list) and part.isdigit() and int(part) < len(cur):
            cur = cur[int(part)]
        else:
            return _MISSING
    return cur


class _Missing:
    def __repr__(self):
        return "<brak>"


_MISSING = _Missing()

OPS = {
    "eq": lambda a, b: a == b, "ne": lambda a, b: a != b,
    "in": lambda a, b: a in (b or []), "nin": lambda a, b: a not in (b or []),
    "gt": lambda a, b: a > b, "ge": lambda a, b: a >= b, "lt": lambda a, b: a < b, "le": lambda a, b: a <= b,
    "truthy": lambda a, b: bool(a), "falsy": lambda a, b: not a,
    "matches": lambda a, b: re.search(str(b), str(a)) is not None,
    "contains": lambda a, b: b in a,
}


def evaluate(pred, ctx: dict, adv: list | None = None) -> bool:
    """Predykat → bool. Nieznany operator albo błąd porównania = uwaga i True (nie wymyślamy blokady)."""
    if pred is None or pred is True:
        return True
    if pred is False:
        return False
    if "all" in pred:
        return all(evaluate(p, ctx, adv) for p in pred["all"])
    if "any" in pred:
        return any(evaluate(p, ctx, adv) for p in pred["any"])
    if "not" in pred:
        return not evaluate(pred["not"], ctx, adv)
    op = pred.get("op", "truthy")
    val = get_path(ctx, pred["path"])
    if op == "exists":
        return val is not _MISSING and val not in (None, "")
    if op == "missing":
        return val is _MISSING or val in (None, "")
    if val is _MISSING:
        val = None if op not in ("truthy", "falsy") else None
    fn = OPS.get(op)
    if fn is None:
        if adv is not None:
            adv.append(f"nieznany operator {op!r} w {pred} — warunek uznany za spełniony")
        return True
    try:
        return bool(fn(val, pred.get("value")))
    except TypeError as e:
        if adv is not None:
            adv.append(f"warunek {pred}: {e} — uznany za spełniony")
        return True


def subst(value, ctx: dict):
    """{params.x}, {run_id}, {root}, {repo}, {where}, {out.krok}, {env.X} — rekurencyjnie w str/list/dict."""
    if isinstance(value, str):
        def rep(m):
            v = get_path(ctx, m.group(1))
            if v is _MISSING:
                raise RoutineError(f"brak wartości {{{m.group(1)}}}")
            return v if isinstance(v, str) else json.dumps(v, ensure_ascii=False)
        return VAR_RE.sub(rep, value)
    if isinstance(value, list):
        return [subst(v, ctx) for v in value]
    if isinstance(value, dict):
        return {k: subst(v, ctx) for k, v in value.items()}
    return value


# ---------------------------------------------------------------- brief

def brief_render(brief: dict, *, root: Path = REPO, cat: dict | None = None, packs: dict | None = None,
                 tools_cat: dict | None = None, gate: dict | None = None) -> dict:
    """loom.brief/1 (albo dawny brief) → args single-package.js + hashe.

    Część wspólna = render pakietu kontekstu (bajt w bajt, tools/context_pack.py); część własna = scope + pola
    strukturalne (design_doc, decisions, measurements, hypothesis, plan_calls, card_schema — etykiety to presety)."""
    cat = cat if cat is not None else load_json(ROUTINES)
    pre = cat.get("presets", {})
    labels = pre.get("brief_labels", {})
    passthrough = pre.get("brief_passthrough", [])
    own = [str(brief.get("scope", "")).rstrip()]
    for key, label in labels.items():
        v = brief.get(key)
        if v in (None, "", [], {}):
            continue
        if isinstance(v, list):
            own.append(f"{label}:\n" + "\n".join(f"- {x if isinstance(x, str) else canon(x)}" for x in v))
        elif isinstance(v, dict):
            own.append(f"{label}: {canon(v)}")
        else:
            own.append(f"{label}: {v}")
    scope = "\n".join(x for x in own if x)
    args = {k: brief[k] for k in passthrough if k in brief}
    for k, v in brief.items():  # pola spoza presetu przechodzą bez zmian (lista otwarta)
        if k not in args and k not in labels and k not in ("schema", "scope", "_comment", "_history", "version", "role",
                                                          "context_prefix", "context_pack_hash"):
            args[k] = v
    args["scope"] = scope
    if gate is not None:
        args["start_gate"] = gate
    prefix_hash = None
    if brief.get("context_pack"):
        packs = packs if packs is not None else load_json(cp.PACKS)
        tools_cat = tools_cat if tools_cat is not None else load_json(cp.TOOLS)
        filled = cp.fill_brief(root, packs, {**brief, "scope": scope}, tools_cat)
        args["context_prefix"] = filled["context_prefix"]
        args["context_pack_hash"] = prefix_hash = filled["context_pack_hash"]
        args["spec"] = filled["spec"]
    elif brief.get("context_prefix"):
        args["context_prefix"] = brief["context_prefix"]
        args["context_pack_hash"] = prefix_hash = brief.get("context_pack_hash") or sha(brief["context_prefix"])
    return {"args": args, "brief_hash": sha(canon(brief)), "context_pack_hash": prefix_hash,
            "own_hash": sha(scope), "own_chars": len(scope), "accept_chars": len(str(brief.get("accept", "")))}


def prompts_of(args: dict) -> list[dict]:
    """Prompt, który single-package.js wyśle agentowi (harness node z tools/tests); None, gdy brak node."""
    node = shutil.which("node")
    if not node:
        return []
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        json.dump(args, f, ensure_ascii=False)
        p = f.name
    try:
        r = subprocess.run([node, str(HERE / "tests" / "single_package_prompts.mjs"), p], capture_output=True, text=True,
                           timeout=60)
        if r.returncode != 0:
            raise RoutineError(f"harness single-package: {r.stderr[-400:]}")
        return json.loads(r.stdout)[0]["prompts"]
    finally:
        os.unlink(p)


def brief_split(old: dict, pack: str, own: str | None = None) -> dict:
    """Stary brief → loom.brief/1. Część własna podana wprost (own) albo cały dawny scope (bez skracania)."""
    out = {"schema": BRIEF_SCHEMA, "version": 1}
    for k, v in old.items():
        if k in ("scope", "context_prefix", "context_pack_hash"):
            continue
        out[k] = v
    out["context_pack"] = pack
    out["scope"] = own if own is not None else old.get("scope", "")
    out.setdefault("spec", [])
    out["_history"] = list(old.get("_history", [])) + [
        {"at": rr.now_iso(), "by": "routine.py brief split", "what": f"rozłożony na pakiet {pack} + część własną",
         "from_hash": sha(canon(old))}]
    return out


# ---------------------------------------------------------------- runner (bez stanu własnego)

class Runner:
    def __init__(self, root: Path = REPO, cat: dict | None = None, places_file: Path | None = None,
                 gestures_file: Path | None = None, cfg: dict | None = None, yes: bool = False, dry: bool = False,
                 out=print):
        self.root = Path(root)
        self.cat = cat if cat is not None else load_json(ROUTINES)
        self.places_file = Path(places_file or os.environ.get("ROUTINE_PLACES", PLACES))
        self.gestures_file = Path(gestures_file or os.environ.get("ROUTINE_GESTURES", GESTURES))
        self.cfg = cfg if cfg is not None else rr.load_config()
        self.yes, self.dry, self.out = yes, dry, out

    # -- kontekst
    def place(self, where: str | None) -> dict:
        if not where:
            return {}
        try:
            places = load_json(self.places_file).get("places", [])
        except (OSError, ValueError):
            return {}
        return next((p for p in places if p.get("id") == where), {}) or \
            next((p for p in places if p.get("id", "").startswith(str(where) + ":") or p.get("kind") == where), {})

    def ctx(self, rec: dict, routine: dict) -> dict:
        params = dict(rec.get("params") or {})
        outs = {s["id"]: s.get("out") for s in rec.get("steps", []) if "out" in s}
        where = rec.get("where")
        return {"params": params, "run_id": rec["run_id"], "root": str(self.root), "repo": str(REPO),
                "where": where or "", "place": self.place(where), "run": {"state": rec.get("state")},
                "out": outs, "env": dict(os.environ), "routine": {"id": routine["id"], "version": routine.get("version")}}

    def mandate_covers(self, step: dict, at: str) -> str | None:
        try:
            st = load_json(self.gestures_file)
        except (OSError, ValueError):
            return None
        scope = step.get("mandate_scope")
        for m in st.get("mandates", []):
            if m.get("state") != "active":
                continue
            if m.get("until") and m["until"] < at:
                continue
            if scope and m.get("scope") and scope not in str(m.get("scope")):
                continue
            return m.get("id") or m.get("use_id") or "mandat"
        return None

    # -- start / resume
    def start(self, ref: str, params: dict, where: str | None = None, at: str | None = None) -> dict:
        r = find_routine(self.cat, ref)
        p = {}
        for k, spec in (r.get("params") or {}).items():
            if isinstance(spec, dict) and "default" in spec:
                p[k] = spec["default"]
        p.update(params)
        missing = [k for k, s in (r.get("params") or {}).items() if isinstance(s, dict) and s.get("required") and k not in p]
        if missing:
            raise RoutineError(f"rutyna {r['id']}: brak parametrów {', '.join(missing)}")
        if where is None and r.get("where"):
            try:
                where = subst(r["where"], {"params": p}) or None
            except RoutineError:
                where = None
        if where:
            p.setdefault("where", where)
        else:
            p.setdefault("where", "")
        rec = rr.start(self.root, self.cfg, r["id"], version=None if r.get("version") is None else str(r["version"]),
                       routine_hash=routine_hash(r),
                       params=p, where=where, at=at, text=r.get("title"))
        return self.advance(rec["run_id"], at=at)

    def resume(self, run_id: str, *, done: list[str] = (), answers: dict | None = None, params: dict | None = None,
               at: str | None = None) -> dict:
        at = at or rr.now_iso()
        rec, _ = rr.load(self.root, self.cfg, run_id)
        if rec.get("state") in ("done", "failed"):
            raise RoutineError(f"bieg {run_id} zakończony ({rec['state']})")
        if params:
            def upd(r):
                r.setdefault("params", {}).update(params)
            rr.mutate(self.root, self.cfg, run_id, upd, at)
        for sid in done:
            rr.add_step(self.root, self.cfg, run_id, sid, state="done", at=at, extra={"by": "resume --done"})
        for sid, ans in (answers or {}).items():
            st = "done" if ans in ("yes", "tak", "y", "t", "1", "true") else "declined"
            rr.add_step(self.root, self.cfg, run_id, sid, state=st, at=at, extra={"answer": ans})
        if rec.get("state") != "running":
            rr.set_field(self.root, self.cfg, run_id, "state", "running", at)
            rr.add_event(self.root, self.cfg, run_id, "resumed", text="wznowiony z rekordu", at=at,
                         use_default_delta=False)
        return self.advance(run_id, at=at)

    def advance(self, run_id: str, at: str | None = None) -> dict:
        at = at or rr.now_iso()
        rec, _ = rr.load(self.root, self.cfg, run_id)
        ref = f"{rec['routine']['id']}@{rec['routine']['version']}" if rec["routine"].get("version") is not None \
            else rec["routine"]["id"]
        r = find_routine(self.cat, ref)
        if rec["routine"].get("hash") and rec["routine"]["hash"] != routine_hash(r):
            self._advise(run_id, f"rutyna {ref} zmieniła się od startu biegu (hash) — bieg idzie wg bieżącej treści tej wersji")
        ctx = self.ctx(rec, r)
        if not any(s["id"] == "_preconditions" for s in rec.get("steps", [])):
            for pc in r.get("preconditions", []):
                adv: list = []
                ok = evaluate(pc.get("check"), ctx, adv)
                for a in adv:
                    self._advise(run_id, a)
                if not ok:
                    msg = f"warunek wstępny niespełniony: {pc.get('why') or pc.get('check')} (od {pc.get('since', '?')})"
                    if pc.get("on_fail") == "stop":
                        rr.finish(self.root, self.cfg, run_id, state="failed", summary=msg, at=at)
                        return rr.load(self.root, self.cfg, run_id)[0]
                    self._advise(run_id, msg)
            rr.add_step(self.root, self.cfg, run_id, "_preconditions", state="done", at=at)
        for step in r.get("steps", []):
            rec, _ = rr.load(self.root, self.cfg, run_id)
            prev = next((s for s in rec.get("steps", []) if s["id"] == step["id"]), None)
            if prev and prev.get("state") in DONE_STATES | {"declined"}:
                continue
            ctx = self.ctx(rec, r)
            adv: list = []
            if not evaluate(step.get("when"), ctx, adv):
                rr.add_step(self.root, self.cfg, run_id, step["id"], state="skipped", at=at)
                continue
            for a in adv:
                self._advise(run_id, a)
            res = self.do_step(run_id, step, ctx, at)
            if res == "pause":
                return rr.load(self.root, self.cfg, run_id)[0]
            if res == "fail":
                rr.finish(self.root, self.cfg, run_id, state="failed", summary=f"krok {step['id']} nie wyszedł", at=at)
                return rr.load(self.root, self.cfg, run_id)[0]
        rec, _ = rr.load(self.root, self.cfg, run_id)
        if rec.get("state") in ("done", "failed"):  # krok sam zakończył bieg (np. import-card) — bez drugiego „finished”
            return rec
        return rr.finish(self.root, self.cfg, run_id, state="done", summary=f"{r['id']}@{r.get('version')}: kroki wykonane",
                         at=at)

    def _advise(self, run_id, text):
        rr.add_to_list(self.root, self.cfg, run_id, "advisories", {"at": rr.now_iso(), "text": text})

    def _pause(self, run_id, step, text, at, state="waiting", run_state="paused"):
        rr.add_step(self.root, self.cfg, run_id, step["id"], state=state, at=at, where=step.get("where") and
                    subst(step["where"], {"params": rr.load(self.root, self.cfg, run_id)[0].get("params", {})}) or None,
                    extra={"text": text})
        rr.set_field(self.root, self.cfg, run_id, "state", run_state, at)
        rr.add_event(self.root, self.cfg, run_id, "paused" if run_state == "paused" else "handoff", text=f"{step['id']}: {text}", at=at, use_default_delta=False)
        self.out(f"PAUZA {run_id} {step['id']}: {text}")

    # -- kroki
    def do_step(self, run_id: str, step: dict, ctx: dict, at: str) -> str:
        kind = step["do"]
        fn = STEP_KINDS.get(kind)
        if fn is None:
            self._advise(run_id, f"krok {step['id']}: rodzaj {kind!r} nieznany runnerowi — przekazany dalej (handoff)")
            fn = Runner.step_handoff
        try:
            return fn(self, run_id, step, ctx, at)
        except RoutineError as e:
            rr.add_step(self.root, self.cfg, run_id, step["id"], state="failed", at=at, extra={"error": str(e)})
            self.out(f"BŁĄD {step['id']}: {e}")
            return "fail"

    def step_cmd(self, run_id, step, ctx, at):
        argv = [a for a in subst(step["argv"], ctx) if a != ""] if step.get("drop_empty", True) else subst(step["argv"], ctx)
        if self.dry:
            rr.add_step(self.root, self.cfg, run_id, step["id"], state="done", at=at, extra={"dry_run": argv})
            self.out("DRY " + " ".join(argv))
            return "ok"
        rr.add_step(self.root, self.cfg, run_id, step["id"], state="running", at=at, where=ctx["where"] or None,
                    in_hash=sha(canon(argv)))
        try:
            p = subprocess.run(argv, cwd=self.root, capture_output=True, text=True,
                               timeout=float(step.get("timeout_s", 3600)))
        except (OSError, subprocess.TimeoutExpired) as e:
            raise RoutineError(f"{argv[0]}: {e}")
        ok_codes = step.get("ok_codes", [0])
        extra = {"exit": p.returncode}
        if step.get("capture"):
            outv = p.stdout.strip()
            if step.get("json"):
                try:
                    outv = json.loads(outv)
                except ValueError:
                    self._advise(run_id, f"krok {step['id']}: wyjście nie jest JSON — zapisane jako tekst")
            extra["out"] = outv if len(json.dumps(outv, ensure_ascii=False)) <= int(step.get("max_out_chars", 20000)) \
                else {"truncated": True, "sha": sha(p.stdout)}
        if p.returncode not in ok_codes:
            extra["stderr_tail"] = p.stderr[-800:]
        state = "done" if p.returncode in ok_codes else "failed"
        rr.add_step(self.root, self.cfg, run_id, step["id"], state=state, at=rr.now_iso(), out_hash=sha(p.stdout),
                    extra=extra)
        self.out(f"{step['id']}: exit {p.returncode}")
        if p.stdout.strip() and not step.get("capture"):
            self.out(p.stdout.rstrip()[-2000:])
        return "ok" if state == "done" or step.get("continue_on_fail") else "fail"

    def step_brief(self, run_id, step, ctx, at):
        path = Path(subst(step["brief"], ctx))
        if not path.is_absolute():
            path = self.root / path
        try:
            brief = load_json(path)
        except (OSError, ValueError) as e:
            raise RoutineError(f"brief {path}: {e}")
        gate = None
        if step.get("gate_from"):
            g = ctx["out"].get(step["gate_from"])
            gate = g if isinstance(g, dict) else None
        res = brief_render(brief, root=REPO, cat=self.cat, gate=gate)
        res["args"].setdefault("run_id", run_id)
        if str(self.root) != str(REPO):
            res["args"].setdefault("runs_root", str(self.root))
        out = Path(subst(step.get("out", "{root}/.routine_out/{run_id}.args.json"), ctx))
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(res["args"], ensure_ascii=False, indent=1) + "\n", encoding="utf-8")

        def fn(rec):
            rec["brief_hash"] = res["brief_hash"]
            if res["context_pack_hash"]:
                rec["context_pack_hash"] = res["context_pack_hash"]
            if gate and not rec.get("resolved"):
                rec["resolved"] = {k: gate.get(k) for k in ("role", "model", "place", "at") if k in gate}
            if not rec.get("spec") and res["args"].get("spec"):
                rec["spec"] = list(res["args"]["spec"])
        rr.mutate(self.root, self.cfg, run_id, fn, at)
        rr.add_step(self.root, self.cfg, run_id, step["id"], state="done", at=at, out_hash=res["own_hash"],
                    extra={"out": {"args_file": str(out), "own_chars": res["own_chars"],
                                   "context_pack_hash": res["context_pack_hash"]}})
        self.out(f"{step['id']}: {out} (część własna {res['own_chars']} znaków, prefiks {res['context_pack_hash']})")
        return "ok"

    def step_render(self, run_id, step, ctx, at):
        what = step.get("what", "status-lines")
        since = subst(step.get("since", ""), ctx) or None
        lines = rr.render_status_lines(self.root, self.cfg, since=since) if what == "status-lines" \
            else rr.render_queue(self.root, self.cfg, since=since)
        for ln in lines:
            self.out(ln)
        rr.add_step(self.root, self.cfg, run_id, step["id"], state="done", at=at, out_hash=sha("\n".join(lines)),
                    extra={"lines": len(lines)})
        return "ok"

    def step_confirm(self, run_id, step, ctx, at):
        q = subst(step.get("question", step["id"]), ctx)
        by = "yes" if self.yes else self.mandate_covers(step, at)
        if by:
            rr.add_step(self.root, self.cfg, run_id, step["id"], state="done", at=at, extra={"answer": "yes", "by": by})
            return "ok"
        silent = step.get("if_silent")
        if silent == "proceed" and step.get("silent_now"):
            rr.add_step(self.root, self.cfg, run_id, step["id"], state="done", at=at, extra={"answer": "if_silent"})
            return "ok"
        rr.add_to_list(self.root, self.cfg, run_id, "escalations", {
            "at": at, "step": step["id"], "question": q, "if_silent": silent,
            "cost_to_reverse": step.get("cost_to_reverse"), "ask_after": step.get("ask_after")})
        self._pause(run_id, step, f"pytanie: {q} (odpowiedź: resume {run_id} --answer {step['id']}=yes|no)", at)
        return "pause"

    def step_handoff(self, run_id, step, ctx, at):
        text = subst(step.get("text", f"krok {step['id']} poza runnerem"), ctx)
        self._pause(run_id, step, text, at, state="handoff",
                    run_state=step.get("run_state", "running"))  # praca trwa poza runnerem (agent na miejscu): bieg w toku
        return "pause"


STEP_KINDS = {"cmd": Runner.step_cmd, "brief.render": Runner.step_brief, "render": Runner.step_render,
              "confirm": Runner.step_confirm, "handoff": Runner.step_handoff}


# ---------------------------------------------------------------- CLI

def operator_gate(root: Path, cat: dict, ref: str, parent_run: str | None, at: str | None = None) -> dict | None:
    """IW-10: operator (sekretarz z rozszerzonym mandatem) uruchamia tylko rutyny z `operator_ok: true`. Każda inna to
    `judgement` → propozycja do koordynatora w biegu nadrzędnym (eskalacja); nic nie jest wykonane. None = wolno."""
    r = find_routine(cat, ref)
    if r.get("operator_ok") is True:
        return None
    prop = {"kind": "judgement", "routine": f"{r['id']}@{r.get('version')}", "at": at or rr.now_iso(),
            "why": "rutyna bez operator_ok — propozycja do koordynatora, nic nie wykonane (IW-10)",
            "proposal": f"python3 tools/routine.py run {r['id']}@{r.get('version')}"}
    if parent_run:
        cfg = rr.load_config()
        rr.add_to_list(root, cfg, parent_run, "escalations", prop, at=at)
        rr.add_event(root, cfg, parent_run, "escalation", text=f"propozycja: {r['id']} (bez operator_ok)", at=at)
    return prop


def _kv(items) -> dict:
    return rr.parse_kv(items)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=os.environ.get("RUN_RECORD_ROOT"), help="korzeń katalogu biegów (preset: repo)")
    ap.add_argument("--routines", default=os.environ.get("ROUTINES_FILE", str(ROUTINES)))
    ap.add_argument("--at")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    s = sub.add_parser("show")
    s.add_argument("ref")
    sub.add_parser("check")
    r = sub.add_parser("run")
    r.add_argument("ref")
    r.add_argument("--param", action="append")
    r.add_argument("--where")
    r.add_argument("--yes", action="store_true", help="bramki confirm przechodzą (jego tak z góry)")
    r.add_argument("--dry-run", action="store_true")
    r.add_argument("--as", dest="as_role", default="",
                   help="operator: tylko rutyny z operator_ok; reszta = propozycja do koordynatora (nic nie wykonane, IW-10)")
    r.add_argument("--parent-run", default="", help="bieg nadrzędny (np. operator.run): tam trafia propozycja/eskalacja")
    rs = sub.add_parser("resume")
    rs.add_argument("run_id")
    rs.add_argument("--done", action="append", default=[])
    rs.add_argument("--answer", action="append", default=[])
    rs.add_argument("--card")
    rs.add_argument("--param", action="append")
    rs.add_argument("--yes", action="store_true")
    b = sub.add_parser("brief")
    bsub = b.add_subparsers(dest="bcmd", required=True)
    br = bsub.add_parser("render")
    br.add_argument("brief")
    br.add_argument("-o", "--out")
    br.add_argument("--prompt", action="store_true", help="pokaż prompt agenta (node + harness single-package)")
    bs = bsub.add_parser("split")
    bs.add_argument("brief")
    bs.add_argument("--pack", required=True)
    bs.add_argument("--own-file")
    bs.add_argument("-o", "--out")
    a = ap.parse_args(argv)
    root = Path(a.root) if a.root else REPO
    cat = load_json(Path(a.routines))
    try:
        if a.cmd == "list":
            for x in sorted(cat.get("routines", []), key=lambda x: (x["id"], _vkey(x.get("version", 0)))):
                print(f"{x['id']}@{x.get('version')}  od {x.get('since')}  — {x.get('title', '')}")
            return 0
        if a.cmd == "show":
            print(json.dumps(find_routine(cat, a.ref), ensure_ascii=False, indent=1))
            return 0
        if a.cmd == "check":
            msgs = check_catalogue(cat)
            for p in sorted((REPO / "tools" / "workflows" / "briefs").glob("*.json")):
                try:
                    d = load_json(p)
                except ValueError as e:
                    msgs.append(f"{p.name}: JSON {e}")
                    continue
                if d.get("schema") == BRIEF_SCHEMA:
                    for f in ("id", "scope", "accept"):
                        if f not in d:
                            msgs.append(f"{p.name}: brak pola {f}")
            for m in msgs:
                print(m)
            print(f"{len(cat.get('routines', []))} rutyn, {len(msgs)} uwag")
            return 0
        if a.cmd == "brief":
            brief = load_json(Path(a.brief))
            if a.bcmd == "render":
                res = brief_render(brief, root=REPO, cat=cat)
                if a.out:
                    Path(a.out).write_text(json.dumps(res["args"], ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
                if a.prompt:
                    for pr in prompts_of(res["args"]):
                        print(pr["prompt"])
                else:
                    print(json.dumps({k: v for k, v in res.items() if k != "args"}, ensure_ascii=False, indent=1))
                return 0
            own = Path(a.own_file).read_text(encoding="utf-8").rstrip("\n") if a.own_file else None
            out = brief_split(brief, a.pack, own)
            txt = json.dumps(out, ensure_ascii=False, indent=1) + "\n"
            if a.out:
                Path(a.out).write_text(txt, encoding="utf-8")
            else:
                print(txt)
            return 0
        runner = Runner(root, cat, yes=getattr(a, "yes", False), dry=getattr(a, "dry_run", False))
        if a.cmd == "run":
            if a.as_role == "operator":
                prop = operator_gate(root, cat, a.ref, a.parent_run or None, at=a.at)
                if prop is not None:
                    print(json.dumps(prop, ensure_ascii=False))
                    return 4
                runner.yes = False  # operator nie odpowiada „tak” za niego; bramki confirm zostają eskalacjami (mandat działa)
            params = _kv(a.param)
            if a.parent_run:
                params.setdefault("parent_run", a.parent_run)
            rec = runner.start(a.ref, params, where=a.where, at=a.at)
        else:
            params = _kv(a.param)
            done = list(a.done)
            if a.card:
                params["card"] = str(Path(a.card).resolve())
            rec = runner.resume(a.run_id, done=done, answers=_kv(a.answer), params=params, at=a.at)
        print(f"{rec['run_id']} {rec['state']}")
        return 0 if rec["state"] in ("done", "paused", "running") else 1
    except (RoutineError, rr.RunError, cp.PackError) as e:
        print(f"błąd: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
