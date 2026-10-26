#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Zapis biegu `loom.routine_run/1` (pakiet IW-1, 2026-10-26).

Jeden rekord na bieg (rutyna, pakiet agenta, zadanie zaplanowane, sesja), pisany tym skryptem:
0 tokenów na ścieżce zapisu, bez sieci, bez modelu. Projekt: docs/architecture/INTERFEJS_WSPOLPRACY_v2_2026-10-23.md §2.1.

Dom: <root>/docs/reports/runs/<rrrr-mm>/<run_id>.json + indeks <root>/docs/reports/runs/runs.jsonl.
Indeks jest DOPISYWANY (jedna linia = migawka biegu po zmianie); czytelnik bierze ostatnią linię danego run_id.
Dopisywanie zamiast przepisywania: dwie maszyny dopisujące równolegle scalają się w git bez konfliktu treści
(odwrót: `reindex` przepisuje indeks do jednej linii na bieg).

Komendy (lista: python3 tools/run_record.py -h):
  start     nowy bieg; wypisuje run_id
  step      dodaje albo uzupełnia krok (po id)
  event     zdarzenie (rodzaj otwarty) z opcjonalną deltą licznika
  receipt   pozycja paragonu (waluta otwarta)
  decision  decyzja z odwrotem
  add       dopisuje wartość do dowolnej listy rekordu (commits, cards, advisories, original_ideas, …)
  set       ustawia dowolne pole najwyższego poziomu (wartość JSON albo tekst)
  finish    kończy bieg (stan otwarty; preset: done)
  show / list / validate / reindex
  render    status-lines | queue  (linie statusu i sekcje kolejki z rekordów)
  import-card       karta single-package (JSON) → rekord biegu pakietu
  import-spec-runs  wiersze `spec_runs` z bazy strony spec (P1) → rekordy biegów sekretarza

Presety: tools/routines/run_record.json (albo --config / RUN_RECORD_CONFIG). Katalog: --root / RUN_RECORD_ROOT (domyślnie
korzeń repo). Testy: python3 -m unittest discover -s tools/tests
"""
from __future__ import annotations

import argparse
import contextlib
import datetime as _dt
import hashlib
import json
import os
import re
import sys
import uuid
from pathlib import Path

SCHEMA_ID = "loom.routine_run/1"
HERE = Path(__file__).resolve().parent
REPO = HERE.parent
DEFAULT_CONFIG = HERE / "routines" / "run_record.json"
SCHEMA_FILE = HERE / "routines" / "schemas" / "routine_run.schema.json"

# Wartości awaryjne, gdy plik presetów zniknie (te same co w run_record.json).
FALLBACK_CONFIG = {
    "runs_dir": "docs/reports/runs",
    "index_file": "runs.jsonl",
    "id_prefix": "r_",
    "finish_event_kind": {"done": "finished", "failed": "failed", "_default": "finished"},
    "default_counter_delta": {},
    "status_lines": {
        "filter": "nonzero_delta", "kinds": [],
        "labels": {"_default": "{kind}"},
        "line": "{label} {hhmm} UTC — {title}{text_sep}{text}",
        "queue_heading": "## {title} {label} ({hhmm} UTC, `{run_id}`{extras}): {text}",
    },
}


class RunError(Exception):
    pass


# ---------------------------------------------------------------- konfiguracja i ścieżki

def _merge(base: dict, over: dict) -> dict:
    out = dict(base)
    for k, v in over.items():
        out[k] = _merge(out[k], v) if isinstance(v, dict) and isinstance(out.get(k), dict) else v
    return out


def load_config(path: str | None = None) -> dict:
    """Warstwy: wartości awaryjne ← presety repo (run_record.json) ← plik użytkownika (--config / RUN_RECORD_CONFIG)."""
    cfg = json.loads(json.dumps(FALLBACK_CONFIG))
    layers = [DEFAULT_CONFIG]
    user = path or os.environ.get("RUN_RECORD_CONFIG")
    if user:
        layers.append(Path(user))
    for p in layers:
        if p.exists():
            cfg = _merge(cfg, json.loads(p.read_text(encoding="utf-8")))
    return cfg


def runs_dir(root: Path, cfg: dict) -> Path:
    return Path(root) / cfg["runs_dir"]


def index_path(root: Path, cfg: dict) -> Path:
    return runs_dir(root, cfg) / cfg["index_file"]


def now_iso() -> str:
    return _dt.datetime.now(_dt.timezone.utc).replace(tzinfo=None).isoformat() + "Z"


def parse_time(s: str) -> _dt.datetime:
    t = s.strip()
    if t.endswith("Z"):
        t = t[:-1]
    if "+" in t[10:]:
        t = t[: 10 + t[10:].index("+")]
    if len(t) == 16:  # 2026-10-24T23:23
        t += ":00"
    return _dt.datetime.fromisoformat(t)


def new_run_id(cfg: dict) -> str:
    return f"{cfg.get('id_prefix', 'r_')}{uuid.uuid4().hex[:12]}"


def sha256_text(text: str) -> str:
    return "sha256:" + hashlib.sha256(text.encode("utf-8")).hexdigest()


def sha256_file(path: str) -> str:
    return "sha256:" + hashlib.sha256(Path(path).read_bytes()).hexdigest()


@contextlib.contextmanager
def locked(root: Path, cfg: dict):
    d = runs_dir(root, cfg)
    d.mkdir(parents=True, exist_ok=True)
    fh = open(d / ".lock", "a+")
    try:
        try:
            import fcntl
            fcntl.flock(fh, fcntl.LOCK_EX)
        except ImportError:  # Windows: bez blokady (advisory w dokumentacji)
            pass
        yield
    finally:
        fh.close()


def _atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(path)


def dumps(obj) -> str:
    return json.dumps(obj, ensure_ascii=False, indent=1) + "\n"


# ---------------------------------------------------------------- indeks

def read_index(root: Path, cfg: dict) -> dict[str, dict]:
    """Ostatnia migawka każdego biegu (kolejność: pierwsze pojawienie się)."""
    out: dict[str, dict] = {}
    p = index_path(root, cfg)
    if not p.exists():
        return out
    for n, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            # linia uszkodzona (np. po ręcznym scaleniu) — pomijana z ostrzeżeniem, nie blokuje odczytu
            print(f"ostrzeżenie: {p}:{n} nie jest JSON-em, pominięta", file=sys.stderr)
            continue
        rid = row.get("run_id")
        if rid:
            out[rid] = row
    return out


def index_row(rec: dict, path: Path, root: Path) -> dict:
    try:
        rel = str(path.relative_to(root))
    except ValueError:
        rel = str(path)
    return {
        "run_id": rec["run_id"],
        "path": rel,
        "routine": rec["routine"].get("id"),
        "version": rec["routine"].get("version"),
        "title": title_of(rec),
        "state": rec["state"],
        "where": rec.get("where"),
        "started": rec["started"],
        "finished": rec.get("finished"),
        "updated": rec.get("updated"),
        "events": len(rec.get("events", [])),
    }


def record_path(root: Path, cfg: dict, rec: dict) -> Path:
    ym = rec["started"][:7]
    return runs_dir(root, cfg) / ym / f"{rec['run_id']}.json"


def find_path(root: Path, cfg: dict, run_id: str) -> Path:
    row = read_index(root, cfg).get(run_id)
    if row:
        p = Path(row["path"])
        p = p if p.is_absolute() else Path(root) / p
        if p.exists():
            return p
    hits = sorted(runs_dir(root, cfg).glob(f"*/{run_id}.json"))
    if hits:
        return hits[0]
    raise RunError(f"nie ma biegu {run_id} w {runs_dir(root, cfg)}")


def load(root: Path, cfg: dict, run_id: str) -> tuple[dict, Path]:
    p = find_path(root, cfg, run_id)
    return json.loads(p.read_text(encoding="utf-8")), p


def save(root: Path, cfg: dict, rec: dict, path: Path | None = None, at: str | None = None) -> Path:
    rec["updated"] = at or now_iso()
    errs = validate_record(rec)
    if errs:
        raise RunError("rekord niezgodny ze schematem: " + "; ".join(errs[:5]))
    path = path or record_path(root, cfg, rec)
    _atomic_write(path, dumps(rec))
    ip = index_path(root, cfg)
    ip.parent.mkdir(parents=True, exist_ok=True)
    with open(ip, "a", encoding="utf-8") as fh:
        fh.write(json.dumps(index_row(rec, path, Path(root)), ensure_ascii=False) + "\n")
    return path


# ---------------------------------------------------------------- walidacja

_TYPES = {"object": dict, "array": list, "string": str, "boolean": bool, "null": type(None)}


def _is_type(v, t: str) -> bool:
    if t == "number":
        return isinstance(v, (int, float)) and not isinstance(v, bool)
    if t == "integer":
        return isinstance(v, int) and not isinstance(v, bool)
    return isinstance(v, _TYPES[t])


def _mini_validate(v, s: dict, root: dict, path: str, errs: list) -> None:
    """Podzbiór JSON Schema użyty w routine_run.schema.json (gdy pakietu jsonschema brak)."""
    if "$ref" in s:
        ref = s["$ref"]
        node = root
        for part in ref.lstrip("#/").split("/"):
            node = node[part]
        _mini_validate(v, node, root, path, errs)
        return
    if "const" in s and v != s["const"]:
        errs.append(f"{path}: oczekiwano {s['const']!r}")
        return
    if "type" in s:
        ts = s["type"] if isinstance(s["type"], list) else [s["type"]]
        if not any(_is_type(v, t) for t in ts):
            errs.append(f"{path}: typ {type(v).__name__}, oczekiwano {'/'.join(ts)}")
            return
    if isinstance(v, str):
        if len(v) < s.get("minLength", 0):
            errs.append(f"{path}: pusty tekst")
        if "pattern" in s and not re.search(s["pattern"], v):
            errs.append(f"{path}: nie pasuje do {s['pattern']}")
    if isinstance(v, dict):
        for k in s.get("required", []):
            if k not in v:
                errs.append(f"{path}: brak pola {k}")
        props = s.get("properties", {})
        extra = s.get("additionalProperties", True)
        for k, val in v.items():
            if k in props:
                _mini_validate(val, props[k], root, f"{path}.{k}", errs)
            elif isinstance(extra, dict):
                _mini_validate(val, extra, root, f"{path}.{k}", errs)
            elif extra is False:
                errs.append(f"{path}: pole {k} niedozwolone")
    if isinstance(v, list) and "items" in s:
        for i, item in enumerate(v):
            _mini_validate(item, s["items"], root, f"{path}[{i}]", errs)


_SCHEMA_CACHE: dict | None = None


def schema() -> dict:
    global _SCHEMA_CACHE
    if _SCHEMA_CACHE is None:
        _SCHEMA_CACHE = json.loads(SCHEMA_FILE.read_text(encoding="utf-8"))
    return _SCHEMA_CACHE


def validate_record(rec, engine: str = "auto") -> list[str]:
    """Lista błędów (pusta = zgodny). engine: auto (jsonschema, jeśli jest) | mini | jsonschema."""
    s = schema()
    if engine in ("auto", "jsonschema"):
        try:
            import jsonschema  # type: ignore
        except ImportError:
            if engine == "jsonschema":
                raise
        else:
            v = jsonschema.Draft202012Validator(s)
            return [f"{'/'.join(map(str, e.absolute_path)) or '$'}: {e.message}" for e in v.iter_errors(rec)]
    errs: list[str] = []
    _mini_validate(rec, s, s, "$", errs)
    return errs


# ---------------------------------------------------------------- operacje na rekordzie

def title_of(rec: dict) -> str:
    p = rec.get("params") or {}
    return str(p.get("title") or p.get("id") or rec["routine"].get("id"))


def parse_value(text: str):
    """Wartość z linii komend: JSON, gdy się parsuje, inaczej tekst."""
    try:
        return json.loads(text)
    except (json.JSONDecodeError, TypeError):
        return text


def parse_kv(items: list[str] | None) -> dict:
    out = {}
    for it in items or []:
        if "=" not in it:
            raise RunError(f"oczekiwano klucz=wartość, jest {it!r}")
        k, v = it.split("=", 1)
        out[k] = parse_value(v)
    return out


def parse_delta(items: list[str] | None) -> dict:
    out = {}
    for k, v in parse_kv(items).items():
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            raise RunError(f"delta licznika {k} musi być liczbą, jest {v!r}")
        out[k] = v
    return out


def make_event(cfg: dict, kind: str, at: str, text: str | None = None, delta: dict | None = None,
               klass: str | None = None, use_default_delta: bool = True) -> dict:
    ev: dict = {"at": at, "kind": kind}
    if text:
        ev["text"] = text
    if delta is None and use_default_delta:
        delta = cfg.get("default_counter_delta", {}).get(kind)
    if delta:
        ev["counter_delta"] = dict(delta)
    if klass:
        ev["class"] = klass
    return ev


def start(root: Path, cfg: dict, routine: str, *, version=None, routine_hash=None, params=None, where=None,
          context_pack_hash=None, brief_hash=None, resolved=None, run_id=None, at=None, text=None,
          spec=None, provenance=None, delta=None, use_default_delta=True) -> dict:
    at = at or now_iso()
    rid = run_id or new_run_id(cfg)
    with locked(root, cfg):
        if run_id and read_index(root, cfg).get(run_id):
            raise RunError(f"bieg {run_id} już istnieje")
        rec = {
            "schema": SCHEMA_ID,
            "run_id": rid,
            "routine": {"id": routine, "version": version, "hash": routine_hash},
            "params": params or {},
            "context_pack_hash": context_pack_hash,
            "brief_hash": brief_hash,
            "where": where,
            "resolved": resolved,
            "started": at,
            "finished": None,
            "state": "running",
            "summary": None,
            "steps": [], "commits": [], "cards": [], "escalations": [], "advisories": [], "original_ideas": [],
            "events": [make_event(cfg, "started", at, text, delta, None, use_default_delta)],
            "receipt": [], "decisions": [], "markers_applied": [],
            "spec": list(spec or []),
        }
        if provenance:
            rec["provenance"] = provenance
        save(root, cfg, rec, at=at)
    state_hooks(root, cfg, rec, None)
    return rec


def mutate(root: Path, cfg: dict, run_id: str, fn, at: str | None = None) -> dict:
    with locked(root, cfg):
        rec, p = load(root, cfg, run_id)
        before = rec.get("state")
        fn(rec)
        save(root, cfg, rec, p, at=at)
    if rec.get("state") != before:
        state_hooks(root, cfg, rec, before)
    return rec


def state_hooks(root: Path, cfg: dict, rec: dict, before) -> None:
    """Polecenie po zmianie stanu biegu (preset state_change_hooks, IW-8: vm.sync dla spot-guard). Poza blokadą;
    błąd polecenia = ostrzeżenie na stderr, nigdy błąd zapisu."""
    h = cfg.get("state_change_hooks") or {}
    if not h.get("enabled") or os.environ.get("RUN_RECORD_HOOKS") == "off":
        return
    if h.get("only_repo_root", True) and Path(root).resolve() != REPO.resolve():
        return
    where = rec.get("where") or ""
    import fnmatch
    if not any(fnmatch.fnmatch(where, pat) for pat in h.get("where_match", [])):
        return
    argv = [a.replace("{repo}", str(REPO)).replace("{root}", str(root)).replace("{run_id}", rec["run_id"])
            for a in h.get("argv", [])]
    if not argv:
        return
    import subprocess
    try:
        r = subprocess.run(argv, capture_output=True, text=True, timeout=float(h.get("timeout_s", 90)))
        if r.returncode != 0:
            print(f"ostrzeżenie: hak stanu {argv[1:3]}: {r.stderr.strip()[-300:]}", file=sys.stderr)
    except (OSError, subprocess.TimeoutExpired) as e:
        print(f"ostrzeżenie: hak stanu: {e}", file=sys.stderr)


def add_step(root, cfg, run_id, step_id, *, state=None, in_hash=None, out_hash=None, where=None, tokens=None,
             extra=None, at=None):
    at = at or now_iso()

    def fn(rec):
        steps = rec.setdefault("steps", [])
        st = next((s for s in steps if s["id"] == step_id), None)
        if st is None:
            st = {"id": step_id, "clock": {"started": at}}
            steps.append(st)
        for k, v in (("state", state), ("in_hash", in_hash), ("out_hash", out_hash), ("where", where),
                     ("tokens", tokens)):
            if v is not None:
                st[k] = v
        st.update(extra or {})
        st.setdefault("clock", {})["updated"] = at
        if state and state not in ("running", "started"):
            st["clock"].setdefault("ended", at)
    return mutate(root, cfg, run_id, fn, at)


def add_event(root, cfg, run_id, kind, *, text=None, delta=None, klass=None, at=None, use_default_delta=True):
    at = at or now_iso()
    return mutate(root, cfg, run_id, lambda rec: rec["events"].append(
        make_event(cfg, kind, at, text, delta, klass, use_default_delta)), at)


def add_receipt(root, cfg, run_id, currency, value, *, unit=None, source=None, reliability=None, extra=None,
                at=None):
    at = at or now_iso()
    item = {"currency": currency, "value": value, "unit": unit, "source": source, "at": at,
            "reliability": reliability, **(extra or {})}
    return mutate(root, cfg, run_id, lambda rec: rec.setdefault("receipt", []).append(item), at)


def add_decision(root, cfg, run_id, text, *, reversal=None, cost_to_reverse=None, by=None, at=None):
    at = at or now_iso()
    item = {"text": text, "reversal": reversal, "cost_to_reverse": cost_to_reverse, "by": by, "at": at}

    def fn(rec):
        rec.setdefault("decisions", []).append(item)
        rec["events"].append(make_event(cfg, "decision", at, text))
    return mutate(root, cfg, run_id, fn, at)


def add_to_list(root, cfg, run_id, field, value, at=None):
    def fn(rec):
        cur = rec.setdefault(field, [])
        if not isinstance(cur, list):
            raise RunError(f"pole {field} nie jest listą")
        cur.append(value)
    return mutate(root, cfg, run_id, fn, at)


PROTECTED = {"schema", "run_id"}


def set_field(root, cfg, run_id, field, value, at=None):
    if field in PROTECTED:
        raise RunError(f"pola {field} nie zmienia się po starcie")

    def fn(rec):
        rec[field] = value
    return mutate(root, cfg, run_id, fn, at)


def finish(root, cfg, run_id, *, state="done", summary=None, text=None, delta=None, at=None,
           use_default_delta=True, quiet=False):
    """quiet: bieg bez skutku (np. pusty bieg sekretarza) nie zmienia liczników — delty wszystkich jego zdarzeń
    są usuwane, więc nie daje linii statusu; rekord i paragon zostają."""
    at = at or now_iso()
    kinds = cfg.get("finish_event_kind", {})
    kind = kinds.get(state, kinds.get("_default", "finished"))

    def fn(rec):
        rec["state"] = state
        rec["finished"] = at
        if summary is not None:
            rec["summary"] = summary
        rec["events"].append(make_event(cfg, kind, at, text or summary, delta, None, use_default_delta and not quiet))
        if quiet:
            for ev in rec["events"]:
                ev.pop("counter_delta", None)
    return mutate(root, cfg, run_id, fn, at)


# ---------------------------------------------------------------- importy

def import_card(root, cfg, card: dict, *, routine="package.run", run_id=None, params=None, where=None,
                at=None, model=None, done_state="done", stopped_state="paused") -> dict:
    """Karta `single-package.js` (done, commits, ci, summary, decisions[], not_done[]) → rekord.

    Gdy run_id istnieje, karta dopisuje się do biegu; inaczej powstaje nowy bieg (start = at)."""
    at = at or now_iso()
    exists = bool(run_id) and run_id in read_index(root, cfg)
    if not exists:
        p = dict(params or {})
        rec = start(root, cfg, routine, params=p, where=where, run_id=run_id, at=at,
                    resolved={"model": model} if model else None,
                    provenance={"from": "single-package card"})
        run_id = rec["run_id"]

    def fn(rec):
        rec["cards"].append(card)
        for c in card.get("commits") or []:
            if c not in rec["commits"]:
                rec["commits"].append(c)
        for d in card.get("decisions") or []:
            item = d if isinstance(d, dict) else {"text": str(d)}
            item.setdefault("by", "agent")
            item.setdefault("at", at)
            rec["decisions"].append(item)
        for idea in card.get("original_ideas") or []:
            rec["original_ideas"].append(idea)
        if card.get("context_pack_hash") and not rec.get("context_pack_hash"):
            rec["context_pack_hash"] = card["context_pack_hash"]
        for s in card.get("spec") or []:
            if s not in rec.setdefault("spec", []):
                rec["spec"].append(s)
        for n in card.get("not_done") or []:
            rec["advisories"].append({"kind": "not_done", "text": n})
    mutate(root, cfg, run_id, fn, at)
    state = done_state if card.get("done") else stopped_state
    return finish(root, cfg, run_id, state=state, summary=card.get("summary"), at=at)


def spec_run_id(doc_id: str) -> str:
    return "r_spec_" + re.sub(r"[^A-Za-z0-9_.:-]", "_", doc_id)


def import_spec_runs(root, cfg, rows: list[dict], *, routine="secretary.run", version="R88-v1") -> list[str]:
    """Wiersze `spec_runs` (P1: at, new, applied, escalated, commands[], note, session) → rekordy biegów sekretarza.

    Idempotentne: run_id wyprowadzony z id dokumentu (albo z `at`), istniejące biegi są pomijane."""
    have = read_index(root, cfg)
    made = []
    for row in rows:
        doc_id = str(row.get("_id") or row.get("id") or row.get("doc_id") or row["at"])
        rid = spec_run_id(doc_id)
        if rid in have:
            continue
        at = row["at"]
        if len(at.rstrip("Z")) == 16:
            at = at.rstrip("Z") + ":00Z"
        counts = {k: row.get(k) for k in ("new", "applied", "escalated") if row.get(k) is not None}
        empty = not any(counts.values()) and not row.get("commands")
        # Pusty bieg nie zmienia żadnego licznika (preset): nie daje linii statusu, zostaje w rekordzie i paragonie.
        quiet = {"delta": {}, "use_default_delta": False} if empty else {}
        start(root, cfg, routine, version=version, run_id=rid, at=at, where="cloud:scheduled",
              params={"title": "Sekretarz spec", **counts},
              resolved={"model": "sonnet", "via": "trigger", "session": row.get("session")},
              provenance={"from": "spec_runs", "doc_id": doc_id, "row": row}, **quiet)
        for i, c in enumerate(row.get("commands") or [], 1):
            add_step(root, cfg, rid, f"cmd{i}", state="done", extra={"command": c}, at=at)
        if row.get("escalated"):
            add_to_list(root, cfg, rid, "escalations", {"count": row["escalated"]}, at=at)
        finish(root, cfg, rid, state="done", summary=row.get("note") or ("pusta" if empty else None), at=at,
               **quiet)
        made.append(rid)
    return made


# ---------------------------------------------------------------- rendery

def _label(cfg, kind):
    labels = cfg["status_lines"].get("labels", {})
    return labels.get(kind, labels.get("_default", "{kind}")).format(kind=kind)


def _hhmm(at: str) -> str:
    try:
        return parse_time(at).strftime("%H:%M")
    except ValueError:
        return at[11:16]


def _keep(cfg, ev, filt=None, kinds=None) -> bool:
    filt = filt or cfg["status_lines"].get("filter", "nonzero_delta")
    kinds = kinds if kinds is not None else cfg["status_lines"].get("kinds", [])
    if filt == "all":
        return True
    if filt == "kinds":
        return ev["kind"] in kinds
    # nonzero_delta (preset): linia statusu tylko, gdy zdarzenie zmienia jakiś licznik
    return any(v != 0 for v in (ev.get("counter_delta") or {}).values())


def all_records(root, cfg) -> list[dict]:
    recs = []
    for rid, row in read_index(root, cfg).items():
        try:
            recs.append(load(root, cfg, rid)[0])
        except (RunError, json.JSONDecodeError) as e:
            print(f"ostrzeżenie: {rid}: {e}", file=sys.stderr)
    return recs


def render_status_lines(root, cfg, *, since=None, filt=None, kinds=None, routine=None) -> list[str]:
    tmpl = cfg["status_lines"]["line"]
    items = []
    for rec in all_records(root, cfg):
        if routine and rec["routine"].get("id") != routine:
            continue
        for ev in rec.get("events", []):
            if since and parse_time(ev["at"]) < parse_time(since):
                continue
            if not _keep(cfg, ev, filt, kinds):
                continue
            text = ev.get("text") or ""
            items.append((parse_time(ev["at"]), tmpl.format(
                label=_label(cfg, ev["kind"]), hhmm=_hhmm(ev["at"]), title=title_of(rec), text=text,
                text_sep=": " if text else "", run_id=rec["run_id"], kind=ev["kind"], at=ev["at"])))
    items.sort(key=lambda x: x[0])
    return [s for _, s in items]


def _extras(rec: dict) -> str:
    parts = []
    model = (rec.get("resolved") or {}).get("model")
    if model:
        parts.append(str(model))
    for c in (rec.get("commits") or [])[:2]:
        parts.append(f"`{str(c)[:8]}`")
    for r in rec.get("receipt") or []:
        v = r["value"]
        v = int(v) if isinstance(v, float) and v.is_integer() else v
        parts.append(f"{v} {r.get('unit') or r['currency']}")
    if rec.get("finished"):
        try:
            mins = round((parse_time(rec["finished"]) - parse_time(rec["started"])).total_seconds() / 60)
            if mins:
                parts.append(f"{mins} min")
        except ValueError:
            pass
    return "".join("; " + p for p in parts)


def render_queue(root, cfg, *, since=None, routine=None, include_empty=False) -> list[str]:
    """Sekcje kolejki w formie ręcznych wpisów QUEUE_S1.md: `## <tytuł> <STAN> (HH:MM UTC, …): treść`, najnowsze pierwsze."""
    tmpl = cfg["status_lines"]["queue_heading"]
    out = []
    for rec in all_records(root, cfg):
        if routine and rec["routine"].get("id") != routine:
            continue
        last = rec["events"][-1]
        if since and parse_time(last["at"]) < parse_time(since):
            continue
        if not include_empty and not _keep(cfg, last):
            continue
        text = rec.get("summary") or last.get("text") or ""
        out.append((parse_time(last["at"]), tmpl.format(
            title=title_of(rec), label=_label(cfg, last["kind"]), hhmm=_hhmm(last["at"]), run_id=rec["run_id"],
            extras=_extras(rec), text=text, state=rec["state"])))
    out.sort(key=lambda x: x[0], reverse=True)
    return [s for _, s in out]


# ---------------------------------------------------------------- CLI

def _root(args) -> Path:
    return Path(args.root or os.environ.get("RUN_RECORD_ROOT") or REPO)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="run_record.py", description=__doc__.split("\n")[0])
    ap.add_argument("--root", help="korzeń, pod którym leży katalog biegów (preset: repo)")
    ap.add_argument("--config", help="plik presetów (preset: tools/routines/run_record.json)")
    ap.add_argument("--at", help="czas zdarzenia (ISO UTC); preset: teraz")
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("start")
    s.add_argument("--routine", required=True, help="id rutyny, opcjonalnie id@wersja")
    s.add_argument("--routine-hash")
    s.add_argument("--routine-file", help="plik rutyny/briefu, którego hash trafia do routine.hash")
    s.add_argument("--param", action="append", help="klucz=wartość (JSON albo tekst), powtarzalne")
    s.add_argument("--where")
    s.add_argument("--context-pack-hash")
    s.add_argument("--brief-hash")
    s.add_argument("--brief-file", help="plik briefu; jego hash trafia do brief_hash")
    s.add_argument("--resolved", help="JSON {rola, model, miejsce, …}")
    s.add_argument("--spec", action="append", help="numer pozycji spec, powtarzalne")
    s.add_argument("--run-id")
    s.add_argument("--text")

    st = sub.add_parser("step")
    st.add_argument("run_id")
    st.add_argument("--id", required=True)
    st.add_argument("--state")
    st.add_argument("--in-hash")
    st.add_argument("--out-hash")
    st.add_argument("--where")
    st.add_argument("--tokens", type=float)
    st.add_argument("--field", action="append", help="dodatkowe pole kroku klucz=wartość")

    e = sub.add_parser("event")
    e.add_argument("run_id")
    e.add_argument("--kind", required=True, help="otwarte; ziarno w presetach")
    e.add_argument("--text")
    e.add_argument("--delta", action="append", help="licznik=liczba, powtarzalne; brak = delta z presetu rodzaju")
    e.add_argument("--no-default-delta", action="store_true")
    e.add_argument("--class", dest="klass")

    r = sub.add_parser("receipt")
    r.add_argument("run_id")
    r.add_argument("--currency", required=True)
    r.add_argument("--value", required=True, type=float)
    r.add_argument("--unit")
    r.add_argument("--source")
    r.add_argument("--reliability")
    r.add_argument("--field", action="append")

    d = sub.add_parser("decision")
    d.add_argument("run_id")
    d.add_argument("--text", required=True)
    d.add_argument("--reversal")
    d.add_argument("--cost-to-reverse")
    d.add_argument("--by")

    a = sub.add_parser("add")
    a.add_argument("run_id")
    a.add_argument("field")
    a.add_argument("value", help="JSON albo tekst")

    se = sub.add_parser("set")
    se.add_argument("run_id")
    se.add_argument("field")
    se.add_argument("value", help="JSON albo tekst")

    f = sub.add_parser("finish")
    f.add_argument("run_id")
    f.add_argument("--state", default="done", help="otwarte; preset done")
    f.add_argument("--summary")
    f.add_argument("--text")
    f.add_argument("--delta", action="append")
    f.add_argument("--quiet", action="store_true", help="bieg bez skutku: usuwa delty liczników (brak linii statusu)")

    sh = sub.add_parser("show")
    sh.add_argument("run_id")

    li = sub.add_parser("list")
    li.add_argument("--state")
    li.add_argument("--routine")
    li.add_argument("--json", action="store_true")

    v = sub.add_parser("validate")
    v.add_argument("paths", nargs="*", help="pliki rekordów; brak = wszystkie z indeksu")
    v.add_argument("--engine", default="auto", help="auto | mini | jsonschema")

    sub.add_parser("reindex")

    rd = sub.add_parser("render")
    rd.add_argument("what", help="status-lines | queue")
    rd.add_argument("--since")
    rd.add_argument("--filter", help="nonzero_delta (preset) | all | kinds")
    rd.add_argument("--kinds", help="lista rodzajów po przecinku dla --filter kinds")
    rd.add_argument("--routine")
    rd.add_argument("--include-empty", action="store_true", help="queue: także biegi bez zmiany licznika")

    ic = sub.add_parser("import-card")
    ic.add_argument("card", help="plik JSON karty albo - (stdin)")
    ic.add_argument("--run-id")
    ic.add_argument("--routine", default="package.run")
    ic.add_argument("--param", action="append")
    ic.add_argument("--where")
    ic.add_argument("--model")

    isr = sub.add_parser("import-spec-runs")
    isr.add_argument("paths", nargs="+", help="pliki JSON: wiersz, lista wierszy albo katalog z plikami <doc_id>.json")

    args = ap.parse_args(argv)
    cfg = load_config(args.config)
    root = _root(args)
    at = args.at
    try:
        if args.cmd == "start":
            rid, _, ver = args.routine.partition("@")
            rhash = args.routine_hash or (sha256_file(args.routine_file) if args.routine_file else None)
            bhash = args.brief_hash or (sha256_file(args.brief_file) if args.brief_file else None)
            rec = start(root, cfg, rid, version=ver or None, routine_hash=rhash, params=parse_kv(args.param),
                        where=args.where, context_pack_hash=args.context_pack_hash, brief_hash=bhash,
                        resolved=json.loads(args.resolved) if args.resolved else None, run_id=args.run_id,
                        at=at, text=args.text, spec=args.spec)
            print(rec["run_id"])
        elif args.cmd == "step":
            add_step(root, cfg, args.run_id, args.id, state=args.state, in_hash=args.in_hash, out_hash=args.out_hash,
                     where=args.where, tokens=args.tokens, extra=parse_kv(args.field), at=at)
        elif args.cmd == "event":
            add_event(root, cfg, args.run_id, args.kind, text=args.text,
                      delta=parse_delta(args.delta) if args.delta else None, klass=args.klass, at=at,
                      use_default_delta=not args.no_default_delta)
        elif args.cmd == "receipt":
            add_receipt(root, cfg, args.run_id, args.currency, args.value, unit=args.unit, source=args.source,
                        reliability=parse_value(args.reliability) if args.reliability else None,
                        extra=parse_kv(args.field), at=at)
        elif args.cmd == "decision":
            add_decision(root, cfg, args.run_id, args.text, reversal=args.reversal,
                         cost_to_reverse=args.cost_to_reverse, by=args.by, at=at)
        elif args.cmd == "add":
            add_to_list(root, cfg, args.run_id, args.field, parse_value(args.value), at=at)
        elif args.cmd == "set":
            set_field(root, cfg, args.run_id, args.field, parse_value(args.value), at=at)
        elif args.cmd == "finish":
            finish(root, cfg, args.run_id, state=args.state, summary=args.summary, text=args.text,
                   delta=parse_delta(args.delta) if args.delta else None, at=at, quiet=args.quiet)
        elif args.cmd == "show":
            print(dumps(load(root, cfg, args.run_id)[0]), end="")
        elif args.cmd == "list":
            rows = [r for r in read_index(root, cfg).values()
                    if (not args.state or r["state"] == args.state) and (not args.routine or r["routine"] == args.routine)]
            for r in rows:
                print(json.dumps(r, ensure_ascii=False) if args.json else
                      f"{r['run_id']}\t{r['state']}\t{r['started']}\t{r['routine']}\t{r.get('title') or ''}")
        elif args.cmd == "validate":
            paths = [Path(p) for p in args.paths] or [find_path(root, cfg, rid) for rid in read_index(root, cfg)]
            bad = 0
            for p in paths:
                errs = validate_record(json.loads(Path(p).read_text(encoding="utf-8")), args.engine)
                if errs:
                    bad += 1
                    print(f"BŁĄD {p}: " + "; ".join(errs[:5]))
            print(f"{len(paths) - bad}/{len(paths)} zgodnych ze schematem {SCHEMA_ID}")
            return 1 if bad else 0
        elif args.cmd == "reindex":
            with locked(root, cfg):
                rows = []
                for p in sorted(runs_dir(root, cfg).glob("*/*.json")):
                    rec = json.loads(p.read_text(encoding="utf-8"))
                    rows.append((rec["started"], index_row(rec, p, root)))
                rows.sort(key=lambda x: x[0])
                _atomic_write(index_path(root, cfg),
                              "".join(json.dumps(r, ensure_ascii=False) + "\n" for _, r in rows))
            print(f"indeks: {len(rows)} biegów")
        elif args.cmd == "render":
            kinds = args.kinds.split(",") if args.kinds else None
            if args.what == "status-lines":
                lines = render_status_lines(root, cfg, since=args.since, filt=args.filter, kinds=kinds,
                                            routine=args.routine)
            elif args.what == "queue":
                lines = render_queue(root, cfg, since=args.since, routine=args.routine,
                                     include_empty=args.include_empty)
                lines = [x + "\n" for x in lines]
            else:
                raise RunError(f"nieznany render {args.what!r} (są: status-lines, queue)")
            print("\n".join(lines))
        elif args.cmd == "import-card":
            text = sys.stdin.read() if args.card == "-" else Path(args.card).read_text(encoding="utf-8")
            rec = import_card(root, cfg, json.loads(text), routine=args.routine, run_id=args.run_id,
                              params=parse_kv(args.param), where=args.where, at=at, model=args.model)
            print(rec["run_id"])
        elif args.cmd == "import-spec-runs":
            rows = []
            for p in map(Path, args.paths):
                files = sorted(p.glob("*.json")) if p.is_dir() else [p]
                for fp in files:
                    data = json.loads(fp.read_text(encoding="utf-8"))
                    for row in (data if isinstance(data, list) else [data]):
                        row.setdefault("_id", fp.stem if p.is_dir() else row.get("at"))
                        rows.append(row)
            made = import_spec_runs(root, cfg, rows)
            print(f"zaimportowano {len(made)} biegów sekretarza (pominięte istniejące: {len(rows) - len(made)})")
        return 0
    except RunError as e:
        print(f"błąd: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
