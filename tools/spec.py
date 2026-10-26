#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""spec.py - wymagania wlasciciela jako dane (schemat loom.spec/1).

Zrodlem prawdy sa trzy pliki Markdown (slowa wlasciciela, luki, rejestr R/D) oraz
dwa male pliki danych: docs/spec/ids.json (rejestr numerow) i
docs/spec/understanding.json (zatwierdzenia i poprawki rozumienia). Wszystko inne
(docs/spec/spec.json, docs/spec/SPEC.md) jest WYLICZANE przez `build`.

Podkomendy:
  build      parsuje trzy pliki, przydziela numery nowym pozycjom, pisze spec.json + SPEC.md
  check      spojnosc: unikalne numery, cytaty dosłowne, luki miedzy plikami, liczniki
  show ID    jedna pozycja: cytat dosłowny, sens, status, dowody, plik:linia
  list       lista pozycji (--status, --theme, --understanding, --kind, --file)
  cite FILE  sprawdza, czy brief cytuje istniejace numery (spec:<numer>); --full wypisuje je z cytatami
  add        dopisuje NOWA wypowiedz wlasciciela (sekcja R/D w rejestrze + wezel)
  propose / approve / correct   rozumienie pozycji (regula R61) - nigdy automatycznie
             (IW-3: --ask-after, --criteria, --class, --first-shown: pytanie w pelnym ksztalcie W3)
  shown      chwila pierwszego pokazania pytania
  silent     if_silent po ask_after, tylko przy niskim koszcie odwrotu (preset), z rekordem decyzji i odwrotem
  reopen     odwrot: pozycja wraca do proposed
  inbox      wpisy skrzynki strony (klasa otwarta) -> werdykty; approval/correction bez modelu

Numer kanoniczny jest nieprzezroczysty (s_ + 12 znakow hex), przydzielony RAZ i
zapisany w ids.json; dawne kody (UI12, GR38, R56 ...) sa aliasami i tez dzialaja.
Narzedzie nigdy nie zmienia istniejacego cytatu i nie wywoluje zadnego modelu.
"""
from __future__ import annotations

import argparse
import copy
import datetime as _dt
import hashlib
import json
import os
import re
import sys
import unicodedata
import uuid
from pathlib import Path

SCHEMA = "loom.spec/1"
IDS_SCHEMA = "loom.spec.ids/1"
UND_SCHEMA = "loom.spec.understanding/1"
REPO = Path(__file__).resolve().parent.parent

# Presety (dane, nie reguly): mozna je nadpisac plikiem docs/spec/config.json.
DEFAULT_CONFIG = {
    "sources": {
        "owner": "docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md",
        "slowa": "docs/architecture/SLOWA_WLASCICIELA_2026-10-16.md",
        "luki": "docs/reports/LUKI_2026-10-16.md",
    },
    "out_dir": "docs/spec",
    # rodzaj wynika z przedrostka kodu i z pliku; nic poza tym nie jest zgadywane
    "kind_by_prefix": {"R": "requirement", "D": "decision", "PH": "philosophy"},
    "kind_default": {
        "slowa": "remark",
        "owner": "note",
        "luki": "unclassified",
        "chunk": "statement",
    },
    "owner_uncoded_kind_by_heading_prefix": {"Decision": "decision"},
    # wiersze opisowe asystenta w pozycji SLOWA: nie sa cytatami (trafiaja do commentary)
    "assistant_labels": [
        "Sens", "Sedno", "Relacje", "Źródła wtórne", "Źródło wtórne",
        "Zastrzeżenie", "Uwaga", "Uogólnienie asystenta", "Opracowanie", "Źródła",
        "Zakres", "Echa", "Zasada w bazie filozofii", "Odczyty sesji", "Zapis decyzji",
        "Inne zapisy", "Pochodne zapisy agentów", "Źródło mówi wprost", "Inne miejsca",
        "Zapisy pochodne", "Raport gałęzi", "Zapis w projekcie",
    ],
    "sense_labels": ["Sens", "Sedno"],
    # cytat, przy którym stoi jeden z tych napisów, jest oznaczony secondary=true
    "secondary_markers": [
        "principles.json", "zapis asystenta", "uogólnienie Claude", "Z tego Claude",
        "Zasada w bazie", "odczyt sesji", "relacja", "zrekonstruowane",
    ],
    # w dodatkach bez numerow: od tego wiersza w dol to zapis asystenta, nie cytat
    "chunk_assistant_markers": ["Co z tego wynika", "Wymaganie (", "Wymaganie:"],
    "min_quote_letters": 1,
    "status_labels_pl": {
        "JEST": "jest", "CZĘŚCIOWO": "częściowo", "BRAK": "brak",
        "NIEAKTUALNE": "nieaktualne", "SPRZECZNE": "sprzeczne",
    },
    "understanding_states": ["none", "proposed", "approved", "corrected"],
    # IW-3 (2026-10-26): pytanie w pełnym kształcie (W3) i skrzynka z klasą. Wszystko to PRESETY: każdy napis
    # kosztu, klasy i słowa jest dozwolony, a liczby są do zmiany w docs/spec/config.json albo flagą.
    "question": {
        # koszty odwrotu, przy których `silent` wykonuje if_silent sam (bez czuwania); każdy inny napis = eskalacja
        "low_cost_to_reverse": ["low", "tani", "niski", "cheap", "taniej", "bardzo tani"],
        # twardy limit pytań rozstrzyganych milczeniem w jednym biegu (preset; null = bez limitu)
        "max_silent_per_run": 5,
        # ask_after, gdy pozycja nie ma własnego (null = bez własnego czasu milczenie niczego nie rozstrzyga)
        "default_ask_after": None,
        # tekst if_silent, gdy pozycja nie ma własnego (ten sam co domyślny na stronie, R75)
        "default_if_silent": "przyjmuję wstępnie i jadę dalej; odwrót tani (R75)",
        # skrzynka: klasa wpisu, gdy strona zapisała tylko dawne pole kind (lista otwarta; nieznana = sama nazwa)
        "class_by_kind": {"approve": "approval", "correct": "correction", "remark": "remark"},
        # klasy stosowane bez modelu (klasa -> komenda spec.py); reszta idzie do koordynatora z powodem
        "apply_classes": {"approval": "approve", "correction": "correct"},
        # słowa, które w tekście dowolnej klasy znaczą zatwierdzenie (porównanie po fold(), cały tekst albo pierwsze słowo)
        "approve_words": ["zatwierdzam", "zatwierdzam parafrazę", "zatwierdzone", "approve", "approved"],
    },
    "extra_themes": {
        "addenda": "Uzupełnienia (wypowiedzi bez numeru)",
        "owner_requirements": "Rejestr wymagań R/D",
    },
}

ID_FACTORY = lambda: "s_" + uuid.uuid4().hex[:12]  # noqa: E731  (podmieniane w testach)

ISO = re.compile(r"\d{4}-\d\d-\d\d")
CODE = r"[A-Z]{1,5}\d{1,3}"
NUM_RE = re.compile(r"^(\d+)\. \*\*(" + CODE + r")\b")
SUM_RE = re.compile(r"^- \*\*(" + CODE + r")\*\*\s*(.*)$")
THEME_RE = re.compile(r"^# (.+?) \(`([A-Za-z_0-9]+)`\)\s*$")
LUKI_THEME_RE = re.compile(r"^## (.+?) \(`([A-Za-z_0-9]+)`\)(?:\s+—\s+(.*?))?\s*$")
LUKI_ITEM_RE = re.compile(r"^- \*\*([A-ZĄĆĘŁŃÓŚŹŻ]+)\*\* `([^`]+)`\s*(.*)$")
OWNER_HEAD_RE = re.compile(r"^## ((?:R|D)\d+)\s+—\s+(.*)$")
OPEN, CLOSE = "„", "”"


# ----------------------------------------------------------------- pomocnicze

def slug(s: str) -> str:
    s = unicodedata.normalize("NFKD", s.lower().replace("ł", "l"))
    s = "".join(c for c in s if not unicodedata.combining(c))
    return re.sub(r"[^a-z0-9]+", "-", s).strip("-")[:60].strip("-")


def fold(s: str) -> str:
    """Do porownywania z linii polecen: male litery, bez ogonkow."""
    s = unicodedata.normalize("NFKD", s.lower().replace("ł", "l"))
    return "".join(c for c in s if not unicodedata.combining(c))


def jdump(obj) -> str:
    return json.dumps(obj, ensure_ascii=False, indent=1) + "\n"


def sha256(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def load_config(root: Path) -> dict:
    cfg = copy.deepcopy(DEFAULT_CONFIG)
    p = root / cfg["out_dir"] / "config.json"
    if p.exists():
        user = json.loads(p.read_text(encoding="utf-8"))
        for k, v in user.items():
            if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                cfg[k].update(v)
            else:
                cfg[k] = v
    return cfg


def src_str(rel: str, a: int, b: int | None = None) -> str:
    return f"{rel}:{a}" if b is None or b == a else f"{rel}:{a}-{b}"


def parse_src(s: str):
    path, _, rng = s.rpartition(":")
    if "-" in rng:
        a, b = rng.split("-", 1)
    else:
        a = b = rng
    return path, int(a), int(b)


def norm_span(lines: list[str], a: int, b: int) -> str:
    """Tekst wierszy a..b (numeracja od 1) bez znacznika cytatu blokowego '>'."""
    out = []
    for l in lines[a - 1:b]:
        out.append(re.sub(r"^\s*>\s?", "", l) if l.lstrip().startswith(">") else l)
    return "\n".join(out)


def split_lines(text: str) -> list[str]:
    return text.split("\n")


# ------------------------------------------------------------ skaner cytatow

class QuoteScanner:
    """Wyciaga zewnetrzne cytaty „...” (takze wielowierszowe i zagniezdzone)."""

    def __init__(self):
        self.stack: list[tuple[int, int]] = []
        self.closed_at: dict[int, int] = {}
        self.lead_from: dict[tuple[int, int], int] = {}
        self.buf: dict[int, str] = {}
        self.quotes: list[dict] = []

    @property
    def in_quote(self) -> bool:
        return bool(self.stack)

    def feed(self, ln: int, text: str) -> None:
        self.buf[ln] = text
        for col, ch in enumerate(text):
            if ch == OPEN:
                if not self.stack:
                    self.lead_from[(ln, col)] = self.closed_at.get(ln, -1) + 1
                self.stack.append((ln, col))
            elif ch == CLOSE and self.stack:
                l0, c0 = self.stack.pop()
                if self.stack:
                    continue
                self.closed_at[ln] = col
                if l0 == ln:
                    body = text[c0 + 1:col]
                else:
                    parts = [self.buf[l0][c0 + 1:]]
                    parts += [self.buf.get(k, "") for k in range(l0 + 1, ln)]
                    parts.append(text[:col])
                    body = "\n".join(parts)
                self.quotes.append({
                    "text": body, "line": l0, "end_line": ln,
                    "lead": self.buf[l0][self.lead_from.get((l0, c0), 0):c0], "tail": text[col + 1:],
                })

    def unclosed(self) -> list[int]:
        return [l for l, _ in self.stack]


def letters(s: str) -> int:
    """Liczba liter i cyfr: odsiewa „…” i podobne cudzyslowy bez tresci."""
    return sum(1 for c in s if c.isalnum())


REF_TICK = re.compile(r"`([^`]+)`")
REF_PLAIN = re.compile(r"[\w./-]+\.(?:md|json|py|cpp|txt|h|hpp|pack|tsx|ts):[\d,\s\-–]+")
BRACKET = re.compile(r"\*?\[([^\]\[]{2,60})\]\*?")


def decorate_quote(q: dict, cfg: dict, rel: str) -> dict:
    tail = q["tail"][:300]
    ref = None
    for m in REF_TICK.finditer(tail):
        tok = m.group(1)
        if "/" in tok or re.search(r"\.\w+(:\d|$)", tok) or re.search(r":\d", tok):
            ref = tok
            break
    if ref is None:
        m = REF_PLAIN.search(tail)
        if m:
            ref = m.group(0).strip(" ,")
    attr = [m.group(1) for m in BRACKET.finditer(tail)]
    ctx = q["lead"] + " " + tail
    sec = any(mk.lower() in ctx.lower() for mk in cfg["secondary_markers"])
    lead = re.sub(r"^\s*(?:[-*]\s+)?", "", q["lead"]).strip()[-90:]
    return {
        "text": q["text"],
        "src": src_str(rel, q["line"], q["end_line"]),
        "ref": ref,
        "attr": attr,
        "lead": lead or None,
        "secondary": sec,
    }


# --------------------------------------------------------------------- uzycie

def new_node() -> dict:
    return {
        "id": None, "aliases": [], "kind": None, "title": None, "date": None,
        "theme": None, "theme_inferred": False,
        "understanding": {"state": "none"},
        "quotes": [], "sense": None, "summary": None, "restatement": [],
        "commentary": [], "status": None, "gap_text": None, "evidence": [],
        "next_step": None, "refs": [], "supersedes": [], "superseded_by": [],
        "supersession_notes": [], "tags": [], "heading_note": None, "sources": [],
        "_text": [], "_lines": [], "_rel": None,
    }


def gaps_from(lines: list[str], claimed: set[int], rel: str) -> list[dict]:
    """Niezaklasyfikowane niepuste wiersze, zgrupowane (puste wiersze nie przerywaja)."""
    out, cur = [], None
    heading = None
    for i, l in enumerate(lines, 1):
        if l.startswith("#"):
            heading = l.lstrip("# ").strip()
        if not l.strip():
            continue
        if i in claimed:
            if cur:
                out.append(cur)
                cur = None
            continue
        if cur is None:
            cur = {"file": rel, "first": i, "last": i, "heading": heading, "preview": l.strip()[:110], "lines": 0}
        cur["last"] = i
        cur["lines"] += 1
    if cur:
        out.append(cur)
    for g in out:
        g["src"] = src_str(g["file"], g["first"], g["last"])
    return out


# ---------------------------------------------------------------- parser OWNER

def parse_owner(lines: list[str], rel: str, cfg: dict):
    nodes, claimed = [], set()
    heads = [i for i, l in enumerate(lines, 1) if l.startswith("## ")]
    for i, l in enumerate(lines, 1):
        if l.startswith("#"):
            claimed.add(i)
    for k, h in enumerate(heads):
        end = (heads[k + 1] - 1) if k + 1 < len(heads) else len(lines)
        while end > h and not lines[end - 1].strip():
            end -= 1
        head = lines[h - 1]
        n = new_node()
        m = OWNER_HEAD_RE.match(head)
        if m:
            code, rest = m.group(1), m.group(2).strip()
            n["aliases"] = [code]
            pm = re.match(r"^(.*?)\s+\(([^()]*)\)$", rest)
            if pm:
                n["title"], n["heading_note"] = pm.group(1).strip(), pm.group(2).strip()
            else:
                n["title"] = rest
            dm = ISO.search(n["heading_note"] or "")
            n["date"] = dm.group(0) if dm else None
            pre = re.match(r"[A-Z]+", code).group(0)
            n["kind"] = cfg["kind_by_prefix"].get(pre, cfg["kind_default"]["owner"])
        else:
            title = head[3:].strip()
            n["aliases"] = ["owner:" + slug(title)]
            n["title"] = title
            n["kind"] = cfg["kind_default"]["owner"]
            for pfx, kd in cfg["owner_uncoded_kind_by_heading_prefix"].items():
                if title.startswith(pfx):
                    n["kind"] = kd
        n["theme"] = "owner_requirements"
        n["sources"].append({"role": "owner", "src": src_str(rel, h, end)})
        # cytaty blokowe i akapity opisowe
        i = h + 1
        para: list[tuple[int, str]] = []

        def flush_para():
            nonlocal para
            if para:
                n["restatement"].append({
                    "text": "\n".join(t for _, t in para),
                    "src": src_str(rel, para[0][0], para[-1][0]),
                })
                para = []

        while i <= end:
            l = lines[i - 1]
            if l.lstrip().startswith(">"):
                flush_para()
                j = i
                while j <= end and lines[j - 1].lstrip().startswith(">"):
                    j += 1
                a, b = i, j - 1
                while a <= b and not re.sub(r"^\s*>\s?", "", lines[a - 1]).strip():
                    a += 1
                while b >= a and not re.sub(r"^\s*>\s?", "", lines[b - 1]).strip():
                    b -= 1
                if a <= b:
                    text = norm_span(lines, a, b)
                    n["quotes"].append({
                        "text": text, "src": src_str(rel, a, b), "ref": None,
                        "attr": [], "lead": None, "secondary": False,
                    })
                i = j
                continue
            if l.strip():
                para.append((i, l))
            else:
                flush_para()
            i += 1
        flush_para()
        n["_text"] = [n["title"] or ""] + [q["text"] for q in n["quotes"]] + [p["text"] for p in n["restatement"]]
        for x in range(h, end + 1):
            claimed.add(x)
        nodes.append(n)
    return nodes, claimed


# ---------------------------------------------------------------- parser SLOWA

def label_re(cfg: dict):
    labs = "|".join(re.escape(x) for x in sorted(cfg["assistant_labels"], key=len, reverse=True))
    return re.compile(r"^\s*(?:[-*]\s+)?(?:\*\*)?(" + labs + r")\b[^:\n]{0,40}:\s*(.*)$")


def parse_heading(line: str, code: str):
    """Zwraca (title, date, tags, rest_after_bold)."""
    m = re.match(r"^\d+\. \*\*(.*?)\*\*(.*)$", line)
    bold, after = (m.group(1), m.group(2)) if m else (line, "")
    t = re.sub(r"^" + re.escape(code) + r"\s*(?:—\s*)?", "", bold).strip()
    date = None
    dm = re.match(r"^(\d{4}-\d\d-\d\d)\s*—\s*(.*)$", t)
    if dm:
        date, t = dm.group(1), dm.group(2)
    t = t.strip()
    pm = re.search(r"\s*\((\d{4}-\d\d-\d\d[^)]*)\)\.?$", t)
    if pm:
        date = date or ISO.search(pm.group(1)).group(0)
        t = t[:pm.start()]
    t = t.rstrip(".").strip()
    tags = [x.strip("*").strip() for x in re.findall(r"\*{0,2}\[([^\]]{2,40})\]\*{0,2}", line)]
    if date is None:
        d2 = ISO.search(after)
        if d2:
            date = d2.group(0)
    return (t or None), date, tags, after


def collect_block(lines: list[str], i0: int) -> int:
    """i0 - indeks (od 0) wiersza naglowka; zwraca indeks po ostatnim wierszu pozycji."""
    j = i0 + 1
    while j < len(lines):
        l = lines[j]
        if l.startswith("#") or l.strip() == "---" or NUM_RE.match(l) or SUM_RE.match(l):
            break
        if l.strip() and not l.startswith("   ") and not lines[j - 1].strip():
            break
        j += 1
    while j > i0 + 1 and not lines[j - 1].strip():
        j -= 1
    return j


def parse_slowa(lines: list[str], rel: str, cfg: dict):
    claimed: set[int] = set()
    warnings: list[str] = []
    themes: list[dict] = []
    nodes: list[dict] = []
    lab = label_re(cfg)
    for i, l in enumerate(lines, 1):
        if l.startswith("#") or l.strip() == "---":
            claimed.add(i)
    h1 = [(i, m.group(1), m.group(2)) for i, l in enumerate(lines, 1) if (m := THEME_RE.match(l))]
    first_theme = h1[0][0] if h1 else len(lines) + 1

    # --- czesc wstepna i dodatki bez numerow: kawalki po naglowkach
    heads = [i for i in range(1, first_theme) if lines[i - 1].startswith("#")]
    bounds = [1] + [i for i in heads if i != 1] + [first_theme]
    seen_alias: dict[str, int] = {}
    for a, b in zip(bounds, bounds[1:]):
        chunk_end = b - 1
        while chunk_end >= a and not lines[chunk_end - 1].strip():
            chunk_end -= 1
        if chunk_end < a:
            continue
        head_text = lines[a - 1].lstrip("# ").strip() if lines[a - 1].startswith("#") else ""
        sc = QuoteScanner()
        quotes: list[dict] = []
        commentary: list[dict] = []
        zone = False
        i = a + 1 if lines[a - 1].startswith("#") else a
        while i <= chunk_end:
            l = lines[i - 1]
            if l.lstrip().startswith(">"):
                j = i
                while j <= chunk_end and lines[j - 1].lstrip().startswith(">"):
                    j += 1
                s_, e_ = i, j - 1
                while s_ <= e_ and not re.sub(r"^\s*>\s?", "", lines[s_ - 1]).strip():
                    s_ += 1
                while e_ >= s_ and not re.sub(r"^\s*>\s?", "", lines[e_ - 1]).strip():
                    e_ -= 1
                if s_ <= e_:
                    quotes.append({"text": norm_span(lines, s_, e_), "src": src_str(rel, s_, e_),
                                   "ref": None, "attr": [], "lead": None, "secondary": False})
                i = j
                continue
            if l.strip():
                if not zone and any(l.lstrip(" -*").startswith(mk) or l.lstrip(" -*").startswith("**" + mk)
                                    for mk in cfg["chunk_assistant_markers"]):
                    zone = True
                if zone:
                    commentary.append({"text": l.strip(), "src": src_str(rel, i)})
                else:
                    sc2 = QuoteScanner()
                    sc2.feed(i, l)
                    found = [q for q in sc2.quotes if letters(q["text"]) >= cfg["min_quote_letters"]]
                    for q in found:
                        quotes.append(decorate_quote(q, cfg, rel))
                    commentary.append({"text": l.strip(), "src": src_str(rel, i)})
            i += 1
        if not quotes:
            continue
        n = new_node()
        if a == 1:
            alias = "slowa:preambula"
            n["title"] = "Polecenie właściciela (preambuła pliku)"
        else:
            alias = "slowa:" + slug(head_text)
            n["title"] = head_text
        if alias in seen_alias:
            seen_alias[alias] += 1
            alias = f"{alias}-{seen_alias[alias]}"
        else:
            seen_alias[alias] = 1
        dm = ISO.search(head_text) or ISO.search(lines[1] if len(lines) > 1 and a == 1 else "")
        n.update(aliases=[alias], kind=cfg["kind_default"]["chunk"], theme="addenda",
                 date=dm.group(0) if dm else None)
        n["quotes"], n["commentary"] = quotes, commentary
        n["sources"].append({"role": "slowa", "src": src_str(rel, a, chunk_end)})
        n["_text"] = [head_text] + [q["text"] for q in quotes] + [c["text"] for c in commentary]
        for x in range(a, chunk_end + 1):
            claimed.add(x)
        nodes.append(n)

    # --- strefy tematow
    summaries: dict[str, dict] = {}
    for ti, (tline, ttitle, tid) in enumerate(h1):
        zend = (h1[ti + 1][0] - 1) if ti + 1 < len(h1) else len(lines)
        themes.append({"id": tid, "title": ttitle, "src": src_str(rel, tline)})
        i0 = tline  # indeks 0-based pierwszego wiersza po naglowku
        while i0 < zend:
            l = lines[i0]
            m = NUM_RE.match(l)
            if m:
                code = m.group(2)
                j0 = collect_block(lines, i0)
                j0 = min(j0, zend)
                n = new_node()
                ln0 = i0 + 1
                title, date, tags, after = parse_heading(l, code)
                n.update(aliases=[code], title=title, date=date, tags=tags, theme=tid)
                pre = re.match(r"[A-Z]+", code).group(0)
                n["kind"] = cfg["kind_by_prefix"].get(pre, cfg["kind_default"]["slowa"])
                n["sources"].append({"role": "slowa", "src": src_str(rel, ln0, j0)})
                sc = QuoteScanner()
                raw: list[dict] = []
                sc.feed(ln0, after)
                sc.buf[ln0] = after
                last_comm = None
                for k in range(i0 + 1, j0):
                    ln = k + 1
                    t = lines[k]
                    if not sc.in_quote:
                        lm = lab.match(t)
                        if lm:
                            entry = {"label": lm.group(1), "text": t.strip(), "src": src_str(rel, ln)}
                            n["commentary"].append(entry)
                            last_comm = (entry, ln)
                            continue
                        if (last_comm and t.strip() and ln == last_comm[1] + 1
                                and t.startswith("     ")
                                and not t.lstrip().startswith(("-", "*", OPEN, "`", ">"))):
                            e, _ = last_comm
                            e["text"] += "\n" + t.strip()
                            a_, _b = parse_src(e["src"])[1:]
                            e["src"] = src_str(rel, a_, ln)
                            last_comm = (e, ln)
                            continue
                    last_comm = None
                    sc.feed(ln, t)
                for q in sc.quotes:
                    if letters(q["text"]) >= cfg["min_quote_letters"]:
                        raw.append(decorate_quote(q, cfg, rel))
                if sc.unclosed():
                    warnings.append(f"{rel}:{sc.unclosed()[0]}: nie zamkniety cytat „ w pozycji {code}")
                n["quotes"] = raw
                # sens
                for c in n["commentary"]:
                    if c["label"] in cfg["sense_labels"]:
                        txt = re.sub(r"^\s*(?:[-*]\s+)?(?:\*\*)?" + re.escape(c["label"]) + r"[^:\n]{0,40}:\s*", "", c["text"], count=1)
                        n["sense"] = {"text": txt, "label": c["label"], "src": c["src"]}
                        break
                n["_text"] = [title or "", after] + [q["text"] for q in raw] + [c["text"] for c in n["commentary"]]
                n["_lines"] = [(x, lines[x - 1]) for x in range(ln0, j0 + 1)]
                n["_rel"] = rel
                for x in range(ln0, j0 + 1):
                    claimed.add(x)
                nodes.append(n)
                i0 = j0
                continue
            m = SUM_RE.match(l)
            if m:
                ln = i0 + 1
                end = i0
                while end + 1 < zend and lines[end + 1].startswith("  ") and not SUM_RE.match(lines[end + 1]):
                    end += 1
                txt = "\n".join(x.strip() if k else x for k, x in enumerate(lines[i0:end + 1]))
                txt = re.sub(r"^- \*\*" + re.escape(m.group(1)) + r"\*\*\s*", "", txt)
                if m.group(1) in summaries:
                    warnings.append(f"{rel}:{ln}: powtorzone streszczenie {m.group(1)}")
                summaries[m.group(1)] = {"text": txt, "src": src_str(rel, ln, end + 1)}
                for x in range(ln, end + 2):
                    claimed.add(x)
                i0 = end + 1
                continue
            i0 += 1
    byalias = {n["aliases"][0]: n for n in nodes if n["theme"] != "addenda"}
    for code, s in summaries.items():
        if code in byalias:
            byalias[code]["summary"] = s
            byalias[code]["_text"].append(s["text"])
        else:
            warnings.append(f"{rel}: streszczenie {code} bez pozycji numerowanej")
    seen = {}
    for n in nodes:
        for a in n["aliases"]:
            if a in seen:
                warnings.append(f"{rel}: kod {a} zdefiniowany dwa razy")
            seen[a] = 1
    return nodes, themes, claimed, warnings


# ----------------------------------------------------------------- parser LUKI

def expand_codes(tok: str) -> list[str]:
    """`MM01–MM10` -> MM01..MM10; `A01, A02` -> dwa kody; zwykly kod -> [kod]."""
    out = []
    for part in re.split(r"\s*,\s*", tok.strip()):
        m = re.fullmatch(r"([A-Z]{1,5})(\d+)\s*[–-]\s*(?:([A-Z]{1,5}))?(\d+)", part)
        if m and (m.group(3) in (None, m.group(1))):
            w = len(m.group(2))
            a, b = int(m.group(2)), int(m.group(4))
            if 0 <= b - a <= 200:
                out += [f"{m.group(1)}{x:0{w}d}" for x in range(a, b + 1)]
                continue
        out.append(part)
    return out


def parse_luki(lines: list[str], rel: str, cfg: dict):
    claimed: set[int] = set()
    items: list[dict] = []
    themes: list[dict] = []
    for i, l in enumerate(lines, 1):
        if l.startswith("#"):
            claimed.add(i)
    title_line = lines[0] if lines else ""
    dm = ISO.search(title_line) or ISO.search(rel)
    checked_on = dm.group(0) if dm else None
    header_counts = {}
    for l in lines[:12]:
        if l.startswith("Razem:"):
            header_counts = {m.group(1): int(m.group(2)) for m in re.finditer(r"([A-ZĄĆĘŁŃÓŚŹŻ]{3,})\s+(\d+)", l)}
    cur_theme, cur_section = None, None
    i0 = 0
    n = len(lines)
    while i0 < n:
        l = lines[i0]
        if l.startswith("## "):
            tm = LUKI_THEME_RE.match(l)
            cur_section = l[3:].strip()
            if tm:
                cur_theme = tm.group(2)
                counts = {m.group(1): int(m.group(2)) for m in re.finditer(r"([A-ZĄĆĘŁŃÓŚŹŻ]{3,})\s+(\d+)", tm.group(3) or "")}
                themes.append({"id": cur_theme, "title": tm.group(1), "header_counts": counts})
            else:
                cur_theme = None
            i0 += 1
            continue
        m = LUKI_ITEM_RE.match(l)
        if not m:
            i0 += 1
            continue
        status, tok, text = m.group(1), m.group(2), m.group(3)
        j0 = i0 + 1
        while j0 < n and not lines[j0].startswith("#") and not LUKI_ITEM_RE.match(lines[j0]) and (
                lines[j0].startswith("  ") or not lines[j0].strip() and j0 + 1 < n and lines[j0 + 1].startswith("  ")):
            j0 += 1
        ln0 = i0 + 1
        it = {
            "codes": expand_codes(tok), "token": tok, "status": status, "theme": cur_theme,
            "section": cur_section, "src": src_str(rel, ln0, j0), "line": ln0, "end_line": j0,
            "gap_text": None, "evidence": [], "next_step": None, "text_all": [], "checked_on": checked_on,
        }
        nxt = re.search(r"\s+—\s+następny krok:\s*", text)
        if nxt:
            it["next_step"] = {"text": text[nxt.end():].strip(), "src": src_str(rel, ln0)}
            text = text[:nxt.start()]
        it["gap_text"] = {"text": text.strip(), "src": src_str(rel, ln0)}
        field, cont = None, None
        for k in range(i0 + 1, j0):
            t = lines[k]
            sm = re.match(r"^\s+- (dowód|następny krok):\s*(.*)$", t)
            if sm:
                ent = {"text": sm.group(2).strip(), "src": src_str(rel, k + 1)}
                if sm.group(1) == "dowód":
                    it["evidence"].append(ent)
                else:
                    it["next_step"] = ent
                cont = ent
            elif t.strip() and cont is not None:
                cont["text"] += "\n" + t.strip()
                a_, _b = parse_src(cont["src"])[1:]
                cont["src"] = src_str(rel, a_, k + 1)
            elif t.strip():
                it.setdefault("extra", []).append({"text": t.strip(), "src": src_str(rel, k + 1)})
        it["text_all"] = [it["gap_text"]["text"]] + [e["text"] for e in it["evidence"]] + (
            [it["next_step"]["text"]] if it["next_step"] else [])
        for x in range(ln0, j0 + 1):
            claimed.add(x)
        items.append(it)
        i0 = j0
    return items, themes, claimed, header_counts, checked_on


# ------------------------------------------------------------------ budowanie

def read_sources(root: Path, cfg: dict) -> dict:
    out = {}
    for role, rel in cfg["sources"].items():
        p = root / rel
        if not p.exists():
            raise SystemExit(f"brak pliku zrodlowego: {rel}")
        text = p.read_text(encoding="utf-8")
        out[role] = {"rel": rel, "text": text, "lines": split_lines(text)}
    return out


def load_ids(root: Path, cfg: dict) -> dict:
    p = root / cfg["out_dir"] / "ids.json"
    if p.exists():
        d = json.loads(p.read_text(encoding="utf-8"))
        return dict(d.get("aliases", {}))
    return {}


def load_und(root: Path, cfg: dict) -> dict:
    p = root / cfg["out_dir"] / "understanding.json"
    if p.exists():
        return dict(json.loads(p.read_text(encoding="utf-8")).get("items", {}))
    return {}


def prefix_of(alias: str) -> str | None:
    m = re.match(r"^([A-Z]{1,5})\d", alias)
    return m.group(1) if m else None


def build_model(root: Path, cfg: dict, allocate: bool = True):
    """Zwraca (spec, registry, changed_registry). Z allocate=False nie przydziela numerow."""
    src = read_sources(root, cfg)
    registry = load_ids(root, cfg)
    und = load_und(root, cfg)
    warnings: list[str] = []

    owner_nodes, owner_claim = parse_owner(src["owner"]["lines"], src["owner"]["rel"], cfg)
    slowa_nodes, slowa_themes, slowa_claim, w1 = parse_slowa(src["slowa"]["lines"], src["slowa"]["rel"], cfg)
    luki_items, luki_themes, luki_claim, luki_counts, checked_on = parse_luki(
        src["luki"]["lines"], src["luki"]["rel"], cfg)
    warnings += w1

    nodes = owner_nodes + slowa_nodes
    by_alias: dict[str, dict] = {}
    for n in nodes:
        for a in n["aliases"]:
            if a in by_alias:
                warnings.append(f"alias {a} w dwoch pozycjach")
            by_alias[a] = n
    luki_only: list[dict] = []
    for it in luki_items:
        hit = [by_alias[c] for c in it["codes"] if c in by_alias]
        target = hit[0] if hit else None
        if len({id(h) for h in hit}) > 1:
            warnings.append(f"{it['src']}: pozycja LUKI obejmuje kilka roznych pozycji ({it['token']}); przypisana do {hit[0]['aliases'][0]}")
        if target is None:
            target = new_node()
            target["kind"] = cfg["kind_by_prefix"].get(prefix_of(it["codes"][0]) or "", cfg["kind_default"]["luki"])
            target["aliases"] = []
            luki_only.append(target)
        for c in it["codes"]:
            if c not in target["aliases"]:
                target["aliases"].append(c)
            by_alias.setdefault(c, target)
        if len(it["codes"]) > 1 and it["token"] not in target["aliases"]:
            target["aliases"].append(it["token"])
            by_alias.setdefault(it["token"], target)
        target["gap_text"] = it["gap_text"]
        target["evidence"] = it["evidence"]
        if it["next_step"]:
            target["next_step"] = it["next_step"]
        target["status"] = {
            "value": it["status"], "checked_on": it["checked_on"], "observation": True,
            "src": it["src"],
        }
        target["sources"].append({"role": "luki", "src": it["src"]})
        target["_text"] += it["text_all"]
        if target in luki_only:
            if it["theme"]:
                target["theme"] = it["theme"]
            target["_luki_section"] = it["section"]
        elif target["theme"] != it["theme"] and it["theme"]:
            warnings.append(f"{it['src']}: temat LUKI '{it['theme']}' rozny od tematu SLOWA '{target['theme']}' dla {target['aliases'][0]}")
    nodes += luki_only

    # temat wnioskowany z przedrostka (tylko gdy przedrostek ma w SLOWA dokladnie jeden temat)
    pref_themes: dict[str, set] = {}
    for n in nodes:
        for a in n["aliases"]:
            p = prefix_of(a)
            if p and n["theme"] and not n["theme_inferred"]:
                pref_themes.setdefault(p, set()).add(n["theme"])
    for n in luki_only:
        if n["theme"] is None:
            ps = {prefix_of(a) for a in n["aliases"]} - {None}
            cands = set()
            for p in ps:
                cands |= pref_themes.get(p, set())
            if len(cands) == 1 and len(ps) == 1:
                n["theme"] = next(iter(cands))
                n["theme_inferred"] = True

    # numery kanoniczne
    changed = False
    used = set(registry.values())
    for n in nodes:
        found = [registry[a] for a in n["aliases"] if a in registry]
        if len(set(found)) > 1:
            warnings.append(f"aliasy {n['aliases']} wskazuja rozne numery w ids.json: {sorted(set(found))}")
        if found:
            nid = found[0]
        elif allocate:
            nid = ID_FACTORY()
            while nid in used:
                nid = ID_FACTORY()
            used.add(nid)
        else:
            nid = "s_(nieprzydzielony)"
        n["id"] = nid
        if allocate:
            for a in n["aliases"]:
                if a not in registry:
                    registry[a] = nid
                    changed = True

    # odwolania i zastapienia
    alias_to_node = {}
    for n in nodes:
        for a in n["aliases"]:
            alias_to_node[a] = n
    ref_re = re.compile(r"\b([A-Z]{1,3})(\d{1,3})(?:\s*[–-]\s*(?:([A-Z]{1,3}))?(\d{1,3}))?\b")
    sup_re = re.compile(r"(?:zastąpion\w*|uchylon\w*|odwołan\w*)\s+przez\s+((?:" + CODE + r")(?:\s*(?:i|,|/)\s*" + CODE + r")*)")
    sup2_re = re.compile(r"\bzastępuje\s+(" + CODE + r"(?:\s*(?:i|,)\s*" + CODE + r")*)")
    for n in nodes:
        refs: list[str] = []
        txt = "\n".join(n["_text"])
        for m in ref_re.finditer(txt):
            p, a, p2, b = m.group(1), int(m.group(2)), m.group(3), m.group(4)
            w = len(m.group(2))
            cands = [m.group(1) + m.group(2)]
            if b is not None and (p2 in (None, p)) and 0 <= int(b) - a <= 120:
                cands = [f"{p}{x:0{w}d}" for x in range(a, int(b) + 1)]
            for c in cands:
                t = alias_to_node.get(c)
                if t is not None and t is not n and t["id"] not in refs:
                    refs.append(t["id"])
        n["refs"] = refs
    for n in nodes:
        for ln, line in n["_lines"]:
            for rx, direction in ((sup_re, "by"), (sup2_re, "of")):
                for m in rx.finditer(line):
                    for c in re.findall(CODE, m.group(1)):
                        t = alias_to_node.get(c)
                        if t is None or t is n:
                            continue
                        a_, b_ = (n, t) if direction == "by" else (t, n)  # a_ jest zastapione przez b_
                        if b_["id"] not in a_["superseded_by"]:
                            a_["superseded_by"].append(b_["id"])
                        if a_["id"] not in b_["supersedes"]:
                            b_["supersedes"].append(a_["id"])
                        note = {"older": a_["id"], "newer": b_["id"], "src": src_str(n["_rel"], ln),
                                "text": line[max(0, m.start() - 90):m.end() + 40].strip()}
                        if note not in n["supersession_notes"]:
                            n["supersession_notes"].append(note)

    # rozumienie z understanding.json
    for n in nodes:
        u = und.get(n["id"])
        if u:
            n["understanding"] = u
        n.pop("_text", None)
        n.pop("_lines", None)
        n.pop("_rel", None)
        n["files"] = sorted({s["role"] for s in n["sources"]})
        sec = n.pop("_luki_section", None)
        if sec and n["theme"] is None:
            n["luki_section"] = sec

    # tematy
    themes = []
    seen = set()
    for t in slowa_themes:
        themes.append({"id": t["id"], "title": t["title"]})
        seen.add(t["id"])
    for t in luki_themes:
        if t["id"] not in seen:
            themes.append({"id": t["id"], "title": t["title"]})
            seen.add(t["id"])
    for tid, title in cfg["extra_themes"].items():
        if tid not in seen:
            themes.append({"id": tid, "title": title})

    gaps = (gaps_from(src["owner"]["lines"], owner_claim, src["owner"]["rel"])
            + gaps_from(src["slowa"]["lines"], slowa_claim, src["slowa"]["rel"])
            + gaps_from(src["luki"]["lines"], luki_claim, src["luki"]["rel"]))

    spec = {
        "schema": SCHEMA,
        "generated_by": "tools/spec.py build (nie edytuj ręcznie)",
        "status_policy": "status = DATOWANA OBSERWACJA z pliku LUKI (pole checked_on), nie ograniczenie (R60)",
        "sources": {r: {"path": s["rel"], "sha256": sha256(s["text"]), "lines": len(s["lines"])} for r, s in src.items()},
        "luki_header_counts": luki_counts,
        "luki_theme_header_counts": {t["id"]: t["header_counts"] for t in luki_themes},
        "themes": themes,
        "nodes": nodes,
        "gaps": gaps,
        "warnings": warnings,
    }
    return spec, registry, changed


def write_outputs(root: Path, cfg: dict, spec: dict, registry: dict, changed: bool) -> None:
    out = root / cfg["out_dir"]
    out.mkdir(parents=True, exist_ok=True)
    if changed or not (out / "ids.json").exists():
        (out / "ids.json").write_text(jdump({"schema": IDS_SCHEMA, "aliases": dict(sorted(registry.items()))}), encoding="utf-8")
    (out / "spec.json").write_text(jdump(spec), encoding="utf-8")
    (out / "SPEC.md").write_text(render_md(spec, cfg), encoding="utf-8")


# ---------------------------------------------------------------- widok SPEC.md

def status_of(n: dict) -> str:
    return n["status"]["value"] if n.get("status") else "—"


def status_text(n: dict, cfg: dict) -> str:
    v = status_of(n)
    return "bez statusu" if v == "—" else cfg["status_labels_pl"].get(v, v)


def count_table(title: str, counts: dict) -> list[str]:
    rows = [f"| {title} | liczba |", "|---|---|"]
    for k, v in counts.items():
        rows.append(f"| {k} | {v} |")
    return rows


def render_md(spec: dict, cfg: dict) -> str:
    nodes = spec["nodes"]
    labels = cfg["status_labels_pl"]
    out_dir = cfg["out_dir"]
    L = []
    L.append("<!-- WYGENEROWANY PLIK. Nie edytuj ręcznie. Odtworzenie: python3 tools/spec.py build -->")
    L.append("# Specyfikacja wymagań właściciela (widok)")
    L.append("")
    L.append("Ten plik jest **wygenerowany** z `docs/spec/spec.json` poleceniem `python3 tools/spec.py build`. "
             "Nie wpisuje się w niego nic ręcznie: poprawka to nowa albo zmieniona pozycja w plikach źródłowych "
             "(`python3 tools/spec.py add ...`), potem `build`. Pozycję pokazuje `python3 tools/spec.py show <numer>`; "
             "w briefach cytujemy numer `s_...` (albo dawny kod, np. `UI12`, `R56`).")
    L.append("")
    L.append("Status pochodzi z pliku luk i jest **obserwacją z podanej daty**, nie ograniczeniem.")
    L.append("")
    L.append(f"Pozycji razem: **{len(nodes)}**.")
    L.append("")
    sc: dict[str, int] = {}
    for n in nodes:
        k = status_of(n)
        lab = "bez statusu (nie ma w LUKI)" if k == "—" else labels.get(k, k)
        sc[lab] = sc.get(lab, 0) + 1
    L += count_table("Status (z LUKI)", dict(sorted(sc.items(), key=lambda kv: -kv[1])))
    L.append("")
    kc: dict[str, int] = {}
    uc: dict[str, int] = {}
    fc: dict[str, int] = {}
    for n in nodes:
        kc[n["kind"]] = kc.get(n["kind"], 0) + 1
        s = n["understanding"].get("state", "none")
        uc[s] = uc.get(s, 0) + 1
        k = " + ".join({"owner": "rejestr R/D", "slowa": "SLOWA", "luki": "LUKI"}.get(f, f) for f in n["files"])
        fc[k] = fc.get(k, 0) + 1
    L += count_table("Rodzaj", dict(sorted(kc.items(), key=lambda kv: -kv[1])))
    L.append("")
    L += count_table("Rozumienie (R61)", dict(sorted(uc.items(), key=lambda kv: -kv[1])))
    L.append("")
    L += count_table("W których plikach", dict(sorted(fc.items(), key=lambda kv: -kv[1])))
    L.append("")
    tc: dict[str, int] = {}
    tt = {t["id"]: t["title"] for t in spec["themes"]}
    for n in nodes:
        tc[n["theme"]] = tc.get(n["theme"], 0) + 1
    order = [t["id"] for t in spec["themes"]] + [None]
    L.append("| Temat | liczba |")
    L.append("|---|---|")
    for t in order:
        if t in tc:
            L.append(f"| {tt.get(t, 'bez tematu')} | {tc[t]} |")
    L.append("")
    srcs = {r: s["path"] for r, s in spec["sources"].items()}
    up = os.path.relpath(".", out_dir)
    for t in order:
        group = [n for n in nodes if n["theme"] == t]
        if not group:
            continue
        L.append(f"## {tt.get(t, 'Bez tematu')}")
        L.append("")
        for n in group:
            title = n["title"]
            if not title and n.get("summary"):
                s = n["summary"]["text"].replace("\n", " ")
                title = s[:100] + ("…" if len(s) > 100 else "")
            if not title and n.get("gap_text"):
                s = n["gap_text"]["text"].replace("\n", " ")
                title = s[:100] + ("…" if len(s) > 100 else "")
            alias = ", ".join(n["aliases"][:3])
            st = status_of(n)
            first = n["sources"][0]["src"] if n["sources"] else None
            link = ""
            if first:
                p, a, _ = parse_src(first)
                link = f" · [źródło]({os.path.relpath(p, out_dir)}#L{a})"
            und = n["understanding"].get("state", "none")
            und_s = "" if und == "none" else f" · rozumienie: {und}"
            L.append(f"- `{n['id']}` **{alias}** — {title or '(bez tytułu)'} — _{status_text(n, cfg)}_{und_s}{link}")
        L.append("")
    return "\n".join(L).rstrip("\n") + "\n"


# ------------------------------------------------------------------- polecenia

def load_spec(root: Path, cfg: dict) -> dict:
    p = root / cfg["out_dir"] / "spec.json"
    if not p.exists():
        raise SystemExit("brak docs/spec/spec.json - uruchom: python3 tools/spec.py build")
    return json.loads(p.read_text(encoding="utf-8"))


def resolve(spec: dict, key: str, registry: dict | None = None):
    k = key.strip()
    for n in spec["nodes"]:
        if n["id"] == k or k in n["aliases"]:
            return n
    kf = fold(k)
    for n in spec["nodes"]:
        if fold(n["id"]) == kf or kf in {fold(a) for a in n["aliases"]}:
            return n
    return None


def cmd_build(root, cfg, args):
    spec, registry, changed = build_model(root, cfg, allocate=True)
    write_outputs(root, cfg, spec, registry, changed)
    nodes = spec["nodes"]
    print(f"build: {len(nodes)} pozycji, {len(registry)} aliasów w ids.json"
          f"{' (dopisano nowe numery)' if changed else ''}")
    print(f"zapisano {cfg['out_dir']}/spec.json, SPEC.md")
    ng = sum(g["lines"] for g in spec["gaps"])
    print(f"niezaklasyfikowane wiersze: {ng} w {len(spec['gaps'])} fragmentach (spec.json -> gaps)")
    for w in spec["warnings"]:
        print("OSTRZEŻENIE:", w)
    return 0


def verify_text(srcs: dict, text: str, src: str, what: str, errors: list, warns: list) -> None:
    path, a, b = parse_src(src)
    lines = srcs.get(path)
    if lines is None:
        errors.append(f"{what}: nieznany plik {path}")
        return
    if a < 1 or b > len(lines):
        errors.append(f"{what}: {src} poza plikiem ({len(lines)} wierszy)")
        return
    if text in norm_span(lines, a, b):
        return
    whole = "\n".join(re.sub(r"^\s*>\s?", "", l) if l.lstrip().startswith(">") else l for l in lines)
    if text in whole:
        warns.append(f"{what}: tekst jest w pliku, ale nie pod {src} (przesunięty) - uruchom build")
    else:
        errors.append(f"{what}: tekst NIE jest dosłownym fragmentem {src}: {text[:70]!r}")


def cmd_check(root, cfg, args):
    errors: list[str] = []
    warns: list[str] = []
    spec = load_spec(root, cfg)
    src = read_sources(root, cfg)
    srcs = {s["rel"]: s["lines"] for s in src.values()}
    if spec.get("schema") != SCHEMA:
        errors.append(f"schemat {spec.get('schema')} != {SCHEMA}")
    nodes = spec["nodes"]
    # 1. numery
    ids = [n["id"] for n in nodes]
    dup = {i for i in ids if ids.count(i) > 1}
    for d in sorted(dup):
        errors.append(f"numer {d} występuje w wielu pozycjach")
    for n in nodes:
        if not re.fullmatch(r"s_[0-9a-f]{12}", n["id"] or ""):
            errors.append(f"zły format numeru: {n['id']}")
    amap: dict[str, str] = {}
    for n in nodes:
        for a in n["aliases"]:
            if a in amap and amap[a] != n["id"]:
                errors.append(f"alias {a} w dwóch pozycjach: {amap[a]} i {n['id']}")
            amap[a] = n["id"]
    registry = load_ids(root, cfg)
    for a, i in amap.items():
        if registry.get(a) != i:
            errors.append(f"alias {a} -> {i} nie zgadza się z ids.json ({registry.get(a)})")
    stale = sorted(set(registry) - set(amap))
    for a in stale:
        warns.append(f"ids.json: alias {a} nie ma już pozycji w źródłach (numer zostaje zarezerwowany)")
    # 2. aktualność: spec.json == build w pamięci
    if all(spec["sources"].get(r, {}).get("sha256") == sha256(s["text"]) for r, s in src.items()):
        pass
    else:
        errors.append("źródła zmieniły się od ostatniego build (suma sha256) - uruchom: python3 tools/spec.py build")
    fresh, _reg, _ch = build_model(root, cfg, allocate=False)
    if jdump(fresh) != jdump(spec) and not any("sha256" in e for e in errors):
        errors.append("spec.json nie jest zgodny z wynikiem build (źródła, ids.json albo understanding.json się zmieniły) - uruchom build")
    # 3. cytaty i inne teksty dosłowne
    nq = 0
    for n in nodes:
        who = n["aliases"][0] if n["aliases"] else n["id"]
        for q in n["quotes"]:
            nq += 1
            verify_text(srcs, q["text"], q["src"], f"{who} cytat", errors, warns)
        for fld in ("sense", "summary", "gap_text", "next_step"):
            v = n.get(fld)
            if v:
                verify_text(srcs, v["text"], v["src"], f"{who} {fld}", errors, warns)
        for fld in ("restatement", "commentary", "evidence"):
            for v in n.get(fld) or []:
                verify_text(srcs, v["text"], v["src"], f"{who} {fld}", errors, warns)
    # 4. rozumienie
    und = load_und(root, cfg)
    node_ids = set(ids)
    for i, u in und.items():
        if i not in node_ids:
            errors.append(f"understanding.json: numer {i} nie ma pozycji")
        if u.get("state") not in cfg["understanding_states"]:
            errors.append(f"understanding.json: {i} zły stan {u.get('state')}")
        hist = u.get("history", [])
        if u.get("state") == "approved" and not any(h.get("state") in ("proposed", "corrected") for h in hist):
            errors.append(f"understanding.json: {i} zatwierdzone bez wcześniejszej propozycji lub poprawki")
    # 5. LUKI vs SLOWA/OWNER
    luki_ids = [n for n in nodes if "luki" in n["files"]]
    orphans = [n for n in nodes if n["files"] == ["luki"]]
    only_slowa = [n for n in nodes if n["files"] == ["slowa"]]
    nl = sum(1 for n in nodes if "luki" in n["files"])
    ns = sum(1 for n in nodes if "slowa" in n["files"])
    in_slowa_coded = [n for n in nodes if "slowa" in n["files"] and n["theme"] != "addenda"]
    no = sum(1 for n in nodes if "owner" in n["files"])
    # refs
    ids_set = node_ids
    for n in nodes:
        for r in n["refs"] + n["supersedes"] + n["superseded_by"]:
            if r not in ids_set:
                errors.append(f"{n['id']}: odwołanie do nieistniejącego {r}")
    # naglowki LUKI vs liczby
    sc: dict[str, int] = {}
    for n in nodes:
        if n["status"]:
            v = n["status"]["value"]
            sc[v] = sc.get(v, 0) + 1
    hdr = spec.get("luki_header_counts") or {}
    # liczniki wedlug *pozycji* LUKI (zakres MM01–MM10 liczy sie raz)
    for k, v in hdr.items():
        if sc.get(k, 0) != v:
            warns.append(f"nagłówek LUKI mówi {k} {v}, a w pozycjach jest {sc.get(k, 0)} (nagłówek jest nieaktualny)")
    for tid, hc in (spec.get("luki_theme_header_counts") or {}).items():
        real: dict[str, int] = {}
        for n in nodes:
            if n["status"] and n["theme"] == tid and not n["theme_inferred"]:
                real[n["status"]["value"]] = real.get(n["status"]["value"], 0) + 1
        if hc and real != hc:
            warns.append(f"nagłówek LUKI tematu {tid}: {hc}, w pozycjach: {real}")
    for w in spec.get("warnings", []):
        warns.append("parser: " + w)
    # --- raport
    labels = cfg["status_labels_pl"]
    print(f"pozycji: {len(nodes)}; cytatów dosłownych sprawdzonych: {nq}")
    print(f"w pliku SLOWA: {ns} (z numerem: {len(in_slowa_coded)}), w LUKI: {nl}, w rejestrze R/D: {no}")
    print(f"tylko w SLOWA (bez LUKI): {len([n for n in nodes if 'slowa' in n['files'] and 'luki' not in n['files'] and n['theme'] != 'addenda'])}"
          f" (+ {len([n for n in nodes if n['theme'] == 'addenda'])} dodatków bez numeru)")
    print(f"tylko w LUKI (sieroty, bez SLOWA/R): {len(orphans)}")
    print("statusy (obserwacje z dat):", ", ".join(f"{labels.get(k, k)} {v}" for k, v in sorted(sc.items(), key=lambda kv: -kv[1])),
          f"; bez statusu: {len([n for n in nodes if not n['status']])}")
    tt = {t["id"]: t["title"] for t in spec["themes"]}
    tcount: dict = {}
    for n in nodes:
        tcount[n["theme"]] = tcount.get(n["theme"], 0) + 1
    print("tematy:", ", ".join(f"{tt.get(k, 'bez tematu')} {v}" for k, v in tcount.items()))
    uc: dict = {}
    for n in nodes:
        uc[n["understanding"].get("state", "none")] = uc.get(n["understanding"].get("state", "none"), 0) + 1
    print("rozumienie:", ", ".join(f"{k} {v}" for k, v in sorted(uc.items())))
    if args.verbose or True:
        for o in orphans:
            print(f"  sierota LUKI: {', '.join(o['aliases'])} ({o['sources'][0]['src']})")
        sl = [n for n in nodes if "slowa" in n["files"] and "luki" not in n["files"] and n["theme"] != "addenda"]
        if sl:
            print("  tylko w SLOWA:", " ".join(n["aliases"][0] for n in sl))
    ng = sum(g["lines"] for g in spec["gaps"])
    print(f"niezaklasyfikowane wiersze: {ng} w {len(spec['gaps'])} fragmentach")
    if args.gaps:
        for g in spec["gaps"]:
            print(f"  [{g['src']}] ({g['lines']} w.) pod „{g['heading']}”: {g['preview']}")
    for w in warns:
        print("OSTRZEŻENIE:", w)
    if args.strict and orphans:
        errors.append(f"tryb --strict: {len(orphans)} sierot LUKI")
    for e in errors:
        print("BŁĄD:", e)
    print("check:", "BŁĘDY" if errors else "czysto", f"({len(errors)} błędów, {len(warns)} ostrzeżeń)")
    return 1 if errors else 0


def show_node(n: dict, spec: dict, cfg: dict) -> str:
    L = []
    byid = {x["id"]: x for x in spec["nodes"]}
    L.append(f"{n['id']}  ({', '.join(n['aliases'])})")
    L.append(f"rodzaj: {n['kind']}   data: {n['date'] or '-'}   temat: "
             f"{next((t['title'] for t in spec['themes'] if t['id'] == n['theme']), 'bez tematu')}"
             f"{' (wywnioskowany z przedrostka)' if n['theme_inferred'] else ''}")
    L.append(f"tytuł: {n['title'] or '-'}")
    if n["tags"]:
        L.append("znaczniki: " + ", ".join(n["tags"]))
    if n["heading_note"]:
        L.append(f"uwaga w nagłówku: {n['heading_note']}")
    u = n["understanding"]
    L.append(f"rozumienie: {u.get('state', 'none')}")
    if u.get("text"):
        L.append(f"  propozycja ({u.get('by')}, {u.get('date')}): {u['text']}")
    if u.get("corrected_text"):
        L.append(f"  poprawka ({u.get('by')}, {u.get('date')}): {u['corrected_text']}")
    for fld, lab in (("class", "klasa pytania"), ("first_shown", "pokazane"), ("ask_after", "milczenie rozstrzyga po"),
                     ("if_silent", "bez odpowiedzi"), ("cost_to_reverse", "koszt odwrotu")):
        if u.get(fld):
            L.append(f"  {lab}: {u[fld]}")
    for c in u.get("criteria") or []:
        L.append(f"  kryterium: {c}")
    if u.get("decision"):
        d = u["decision"]
        L.append(f"  decyzja ({d.get('by')}, {d.get('at')}): {d.get('text')} — odwrót: {d.get('reversal')}")
    for i, q in enumerate(n["quotes"], 1):
        flag = " [wtórny: relacja agenta / zapis asystenta / zasada z bazy]" if q.get("secondary") else ""
        L.append(f"cytat {i} — {q['src']}{flag}")
        if q.get("lead"):
            L.append(f"  (przed cytatem: {q['lead']})")
        for ln in q["text"].split("\n"):
            L.append("  > " + ln)
        if q.get("ref"):
            L.append(f"  źródło cytowane: {q['ref']}")
        if q.get("attr"):
            L.append("  oznaczenia: " + "; ".join(q["attr"]))
    if not n["quotes"]:
        L.append("cytaty: brak (pozycja bez cytatu dosłownego)")
    if n["sense"]:
        L.append(f"sens ({n['sense']['label']}, {n['sense']['src']}): {n['sense']['text']}")
    if n["summary"]:
        L.append(f"streszczenie wymagania ({n['summary']['src']}): {n['summary']['text']}")
    for p in n["restatement"]:
        L.append(f"opis w rejestrze ({p['src']}): {p['text']}")
    if n["status"]:
        s = n["status"]
        L.append(f"status: {s['value']} — OBSERWACJA z dnia {s['checked_on']} ({s['src']}), nie ograniczenie")
        if n["gap_text"]:
            L.append(f"  luka: {n['gap_text']['text']}")
        for e in n["evidence"]:
            L.append(f"  dowód ({e['src']}): {e['text']}")
        if n["next_step"]:
            L.append(f"  następny krok ({n['next_step']['src']}): {n['next_step']['text']}")
    else:
        L.append("status: brak (pozycja nie występuje w LUKI)")
    for fld, lab in (("supersedes", "zastępuje"), ("superseded_by", "zastąpione przez")):
        if n[fld]:
            L.append(f"{lab}: " + ", ".join(byid[i]["aliases"][0] + " (" + i + ")" for i in n[fld] if i in byid))
    for sn in n["supersession_notes"]:
        L.append(f"  zapis o zastąpieniu ({sn['src']}): …{sn['text']}…")
    if n["refs"]:
        L.append("odwołania: " + ", ".join((byid[i]["aliases"][0] if i in byid else i) for i in n["refs"]))
    L.append("źródła: " + "; ".join(f"{s['role']} {s['src']}" for s in n["sources"]))
    return "\n".join(L)


def cmd_show(root, cfg, args):
    spec = load_spec(root, cfg)
    rc = 0
    for key in args.ids:
        n = resolve(spec, key)
        if n is None:
            print(f"nie ma pozycji: {key}", file=sys.stderr)
            rc = 1
            continue
        print(show_node(n, spec, cfg))
        if len(args.ids) > 1:
            print()
    return rc


def cmd_list(root, cfg, args):
    spec = load_spec(root, cfg)
    sel = []
    for n in spec["nodes"]:
        if args.status and fold(status_of(n)) != fold(args.status):
            continue
        if args.theme and n["theme"] != args.theme and fold(next((t["title"] for t in spec["themes"] if t["id"] == n["theme"]), "")) != fold(args.theme):
            continue
        if args.understanding and n["understanding"].get("state") != args.understanding:
            continue
        if args.kind and n["kind"] != args.kind:
            continue
        if args.file and args.file not in n["files"]:
            continue
        sel.append(n)
    for n in sel:
        title = n["title"] or (n["summary"] or n["gap_text"] or {"text": ""})["text"].replace("\n", " ")[:80]
        print(f"{n['id']}  {n['aliases'][0]:<10} {status_text(n, cfg):<12} {n['kind']:<12} {title}")
    print(f"-- {len(sel)} pozycji", file=sys.stderr)
    return 0


CITE_RE = re.compile(r"spec:(s_[0-9a-f]{12}|[A-Za-z][A-Za-z0-9_:\-–]*[A-Za-z0-9])")


def cmd_cite(root, cfg, args):
    """Brief cytuje numery jako spec:<numer> (kanoniczny s_... albo dawny kod, np. spec:UI12)."""
    spec = load_spec(root, cfg)
    text = sys.stdin.read() if args.file == "-" else Path(args.file).read_text(encoding="utf-8")
    seen, bad = [], 0
    for tok in CITE_RE.findall(text):
        if tok in seen:
            continue
        seen.append(tok)
        n = resolve(spec, tok)
        if n is None:
            print(f"BRAK  spec:{tok} - nie ma takiej pozycji")
            bad += 1
            continue
        if args.full:
            print(show_node(n, spec, cfg))
            print()
        else:
            print(f"OK    spec:{tok} -> {n['id']} {n['aliases'][0]} [{status_text(n, cfg)}] {n['title'] or ''}")
    print(f"-- {len(seen)} cytowanych numerów, {bad} nieznanych", file=sys.stderr)
    return 1 if bad else 0


def next_owner_section_end(lines: list[str]) -> int:
    """Indeks (od 0) wiersza, przed którym wstawić nową sekcję R/D."""
    last = None
    for i, l in enumerate(lines):
        if OWNER_HEAD_RE.match(l):
            last = i
    if last is None:
        return len(lines)
    for j in range(last + 1, len(lines)):
        if lines[j].startswith("## "):
            return j
    return len(lines)


def cmd_add(root, cfg, args):
    if not re.fullmatch(r"[RD]\d+", args.id):
        raise SystemExit("--id musi mieć postać R<liczba> albo D<liczba>, np. R64")
    qf = Path(args.quote_file)
    quote = qf.read_text(encoding="utf-8").rstrip("\n")
    if not quote.strip():
        raise SystemExit("plik z cytatem jest pusty")
    if not ISO.fullmatch(args.date):
        raise SystemExit("--date musi mieć postać RRRR-MM-DD")
    spec_exists = (root / cfg["out_dir"] / "spec.json").exists()
    registry = load_ids(root, cfg)
    if args.id in registry:
        raise SystemExit(f"odmowa: {args.id} już istnieje (ids.json); istniejącego cytatu się nie zmienia")
    own = root / cfg["sources"]["owner"]
    text = own.read_text(encoding="utf-8")
    lines = split_lines(text)
    for l in lines:
        m = OWNER_HEAD_RE.match(l)
        if m and m.group(1) == args.id:
            raise SystemExit(f"odmowa: {args.id} już istnieje w {cfg['sources']['owner']}; istniejącego cytatu się nie zmienia")
    if spec_exists:
        sp = load_spec(root, cfg)
        if resolve(sp, args.id):
            raise SystemExit(f"odmowa: {args.id} już istnieje w spec.json")
    title = args.title or "(bez tytułu — dopisać)"
    sect = [f"## {args.id} — {title} ({args.by}, {args.date})"]
    sect += ["> " + ln if ln else ">" for ln in quote.split("\n")]
    if args.sense:
        sect += ["", args.sense.strip()]
    sect += [""]
    idx = next_owner_section_end(lines)
    new = lines[:idx] + sect + lines[idx:]
    if args.dry_run:
        print("\n".join(sect))
        print(f"-- dry-run: wstawiono by przed wierszem {idx + 1} pliku {cfg['sources']['owner']}")
        return 0
    own.write_text("\n".join(new), encoding="utf-8")
    spec, reg, changed = build_model(root, cfg, allocate=True)
    write_outputs(root, cfg, spec, reg, changed)
    n = resolve(spec, args.id)
    print(f"dopisano {args.id} -> {n['id']} (plik {cfg['sources']['owner']}, sekcja przed wierszem {idx + 1})")
    return 0


def save_und(root, cfg, und: dict) -> None:
    out = root / cfg["out_dir"]
    out.mkdir(parents=True, exist_ok=True)
    (out / "understanding.json").write_text(jdump({"schema": UND_SCHEMA, "items": dict(sorted(und.items()))}), encoding="utf-8")


def _und_cmd(root, cfg, args, action):
    spec = load_spec(root, cfg)
    n = resolve(spec, args.id)
    if n is None:
        raise SystemExit(f"nie ma pozycji: {args.id}")
    und = load_und(root, cfg)
    cur = copy.deepcopy(und.get(n["id"]) or {"state": "none", "history": []})
    cur.setdefault("history", [])
    date = args.date or _dt.date.today().isoformat()
    by = args.by
    extra = {}
    if action in ("propose", "correct"):
        # R89 P2: pola opcjonalne dla kolejki "Czeka na Ciebie"; wartosci otwarte (dowolny napis).
        # Brak flagi = zostaje poprzednia wartosc; pusty napis ("") usuwa pole.
        # IW-3: ask_after (od first_shown), class pytania; criteria to lista (powtarzalna flaga; jedno "" czyści).
        for fld, val in (("if_silent", getattr(args, "if_silent", None)),
                         ("cost_to_reverse", getattr(args, "cost_to_reverse", None)),
                         ("ask_after", getattr(args, "ask_after", None)),
                         ("class", getattr(args, "question_class", None))):
            if val is None:
                continue
            if val.strip():
                cur[fld] = val.strip()
                extra[fld] = val.strip()
            else:
                cur.pop(fld, None)
        crit = getattr(args, "criteria", None)
        if crit is not None:
            crit = [c.strip() for c in crit if c.strip()]
            if crit:
                cur["criteria"] = crit
                extra["criteria"] = crit
            else:
                cur.pop("criteria", None)
        if cur.get("ask_after") and parse_duration(cur["ask_after"]) is None and parse_when(cur["ask_after"]) is None:
            print(f"uwaga (advisory): ask_after „{cur['ask_after']}” nie jest czasem ani okresem, który `silent` rozumie "
                  "(przykłady: 24h, 2d, 90m, 1d12h, PT6H, 2026-10-27T09:00Z) — pytanie nie rozstrzygnie się milczeniem", file=sys.stderr)
    if action == "propose":
        # nowe pytanie (albo nowy tekst) = pokazane od teraz; --first-shown nadpisuje, `shown` poprawia później
        if getattr(args, "first_shown", None) or cur.get("text") != args.text or not cur.get("first_shown"):
            cur["first_shown"] = getattr(args, "first_shown", None) or now_utc()
        cur.update(state="proposed", text=args.text, by=by, date=date)
        cur.pop("corrected_text", None)
        cur.pop("decision", None)
    elif action == "correct":
        cur.update(state="corrected", corrected_text=args.text, by=by, date=date)
    elif action == "approve":
        if cur.get("state") not in ("proposed", "corrected") and not (cur.get("state") == "approved" and cur.get("by") == "silence"):
            raise SystemExit("odmowa: nie ma czego zatwierdzić (brak propozycji ani poprawki); najpierw propose")
        cur.update(state="approved", by=by, date=date)
    cur["history"].append({k: v for k, v in {"state": cur["state"], "by": by, "date": date,
                                               "text": args.text if action != "approve" else None,
                                               **extra}.items() if v is not None})
    und[n["id"]] = cur
    save_und(root, cfg, und)
    spec2, reg, changed = build_model(root, cfg, allocate=True)
    write_outputs(root, cfg, spec2, reg, changed)
    print(f"{n['aliases'][0]} ({n['id']}): rozumienie = {cur['state']} ({by}, {date})")
    return 0


# ------------------------------------------------- IW-3: pytanie, milczenie, skrzynka (0 tokenów, bez modelu)

DUR_RE = re.compile(r"(\d+(?:[.,]\d+)?)\s*(dni|dzień|dzien|days?|d|godz|h|hours?|min|m|minutes?|s|sec|w|tyg)", re.I)
DUR_UNIT = {"d": 86400, "dni": 86400, "dzień": 86400, "dzien": 86400, "day": 86400, "days": 86400,
            "h": 3600, "godz": 3600, "hour": 3600, "hours": 3600, "m": 60, "min": 60, "minute": 60, "minutes": 60,
            "s": 1, "sec": 1, "w": 604800, "tyg": 604800}


def now_utc() -> str:
    return _dt.datetime.now(_dt.timezone.utc).replace(microsecond=0).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_when(s) -> _dt.datetime | None:
    """Chwila ISO (z Z albo bez strefy = UTC; sama data = północ UTC) albo None."""
    if not s:
        return None
    t = str(s).strip().replace("Z", "+00:00").replace(" ", "T")
    try:
        d = _dt.datetime.fromisoformat(t)
    except ValueError:
        return None
    return d if d.tzinfo else d.replace(tzinfo=_dt.timezone.utc)


def parse_duration(s) -> _dt.timedelta | None:
    """Okres w otwartym zapisie: 24h, 2d, 90m, 1d12h, „3 dni”, PT6H, P1D, sama liczba = godziny. Nierozpoznany = None."""
    if s is None:
        return None
    t = str(s).strip()
    if re.fullmatch(r"\d+(?:[.,]\d+)?", t):
        return _dt.timedelta(hours=float(t.replace(",", ".")))
    m = re.fullmatch(r"P(?:(\d+)W)?(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?)?", t.upper())
    if m and t.upper() not in ("P", "PT"):
        w, d, h, mi, se = (int(x or 0) for x in m.groups())
        return _dt.timedelta(weeks=w, days=d, hours=h, minutes=mi, seconds=se)
    parts = DUR_RE.findall(t)
    if not parts or DUR_RE.sub("", t).strip(" ,+") != "":
        return None
    sec = sum(float(v.replace(",", ".")) * DUR_UNIT[u.lower()] for v, u in parts)
    return _dt.timedelta(seconds=sec)


def question_due(u: dict, qcfg: dict) -> _dt.datetime | None:
    """Kiedy milczenie rozstrzyga: first_shown + ask_after (okres) albo ask_after jako chwila. None = nigdy."""
    aa = u.get("ask_after") or qcfg.get("default_ask_after")
    if not aa:
        return None
    when = parse_when(aa) if not parse_duration(aa) else None
    if when:
        return when
    dur = parse_duration(aa)
    base = parse_when(u.get("first_shown")) or parse_when(u.get("date"))
    if dur is None or base is None:
        return None
    return base + dur


def is_low_cost(u: dict, qcfg: dict, low: list[str] | None = None) -> bool:
    words = {fold(str(x)).strip() for x in (qcfg.get("low_cost_to_reverse") if low is None else low) or []}
    return fold(str(u.get("cost_to_reverse") or "")).strip() in words


def _fmt(d: _dt.datetime) -> str:
    return d.astimezone(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _record_decision(root: Path, run_id: str | None, text: str, reversal: str, cost, by: str, at: str) -> None:
    if not run_id:
        return
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import run_record
    rcfg = run_record.load_config()
    run_record.add_decision(root, rcfg, run_id, text, reversal=reversal, cost_to_reverse=cost, by=by, at=at)


def _rebuild(root, cfg) -> None:
    spec2, reg, changed = build_model(root, cfg, allocate=True)
    write_outputs(root, cfg, spec2, reg, changed)


def cmd_shown(root, cfg, args):
    """Zapisuje chwilę pierwszego pokazania pytania (np. gdy strona naprawdę je pokazała); --force nadpisuje."""
    spec = load_spec(root, cfg)
    und = load_und(root, cfg)
    at = args.at or now_utc()
    for key in args.ids:
        n = resolve(spec, key)
        if n is None:
            raise SystemExit(f"nie ma pozycji: {key}")
        cur = und.get(n["id"])
        if not cur or cur.get("state") != "proposed":
            print(f"{key}: nie czeka (stan {cur.get('state') if cur else 'none'}) — pomijam")
            continue
        if cur.get("first_shown") and not args.force:
            print(f"{key}: first_shown już jest ({cur['first_shown']})")
            continue
        cur["first_shown"] = at
        print(f"{key}: first_shown = {at}")
    save_und(root, cfg, und)
    _rebuild(root, cfg)
    return 0


def cmd_silent(root, cfg, args):
    """Wykonuje if_silent pytań, którym minął ask_after od first_shown, gdy koszt odwrotu jest niski (preset);
    resztę wypisuje jako eskalację. Bez modelu. Wynik: JSON {applied, escalated, waiting, skipped_limit}."""
    q = cfg["question"]
    now = parse_when(args.now) if args.now else _dt.datetime.now(_dt.timezone.utc)
    if now is None:
        raise SystemExit(f"zła chwila --now: {args.now}")
    limit = q.get("max_silent_per_run") if args.limit is None else (None if args.limit < 0 else args.limit)
    spec = load_spec(root, cfg)
    byid = {n["id"]: n for n in spec["nodes"]}
    und = load_und(root, cfg)
    out = {"now": _fmt(now), "applied": [], "escalated": [], "waiting": [], "skipped_limit": []}
    for nid in sorted(und, key=lambda i: (question_due(und[i], q) or now, i)):
        u = und[nid]
        if u.get("state") != "proposed":
            continue
        alias = (byid.get(nid, {}).get("aliases") or [nid])[0]
        due = question_due(u, q)
        row = {"id": nid, "alias": alias, "cost_to_reverse": u.get("cost_to_reverse") or "",
               "due": _fmt(due) if due else None, "if_silent": u.get("if_silent") or q.get("default_if_silent")}
        if due is None or due > now:
            out["waiting"].append(row)
            continue
        if not is_low_cost(u, q, args.low):
            row["why"] = (f"koszt odwrotu „{row['cost_to_reverse'] or '(brak)'}” nie jest na liście niskich "
                          f"({', '.join(args.low if args.low is not None else q.get('low_cost_to_reverse') or [])}) — decyzja należy do koordynatora/właściciela")
            out["escalated"].append(row)
            continue
        if limit is not None and len(out["applied"]) >= limit:
            row["why"] = f"twardy limit {limit} rozstrzygnięć milczeniem na bieg (preset question.max_silent_per_run)"
            out["skipped_limit"].append(row)
            continue
        at = _fmt(now)
        reversal = f"python3 tools/spec.py reopen {alias}"
        decision = {"text": f"{alias}: milczenie po {u.get('ask_after') or q.get('default_ask_after')} od pokazania "
                            f"({u.get('first_shown') or u.get('date')}); wykonane if_silent: {row['if_silent']}",
                    "if_silent": row["if_silent"], "reversal": reversal, "cost_to_reverse": row["cost_to_reverse"],
                    "by": args.by, "at": at, "basis": "ask_after"}
        row["decision"] = decision
        out["applied"].append(row)
        if args.dry_run:
            continue
        u.update(state="approved", by="silence", date=at[:10], decision=decision)
        u.setdefault("history", []).append({"state": "approved", "by": "silence", "date": at[:10], "at": at,
                                            "decision": decision})
        _record_decision(root, args.run_id, decision["text"], reversal, row["cost_to_reverse"], args.by, at)
    if out["applied"] and not args.dry_run:
        save_und(root, cfg, und)
        _rebuild(root, cfg)
    print(json.dumps(out, ensure_ascii=False, indent=1))
    return 0


def cmd_reopen(root, cfg, args):
    """Odwrót: pozycja wraca do proposed z poprzednią parafrazą (np. po rozstrzygnięciu milczeniem)."""
    spec = load_spec(root, cfg)
    n = resolve(spec, args.id)
    if n is None:
        raise SystemExit(f"nie ma pozycji: {args.id}")
    und = load_und(root, cfg)
    cur = und.get(n["id"])
    if not cur or cur.get("state") == "none" or not cur.get("text"):
        raise SystemExit("odmowa: nie ma czego otworzyć ponownie (brak parafrazy)")
    at = args.at or now_utc()
    prev = {k: cur.get(k) for k in ("state", "by", "date")}
    cur.update(state="proposed", by=args.by, date=at[:10], first_shown=at)
    reversed_dec = cur.pop("decision", None)
    h = {"state": "proposed", "by": args.by, "date": at[:10], "at": at, "reopened_from": prev}
    if reversed_dec:
        h["reverses"] = reversed_dec
    if args.why:
        h["why"] = args.why
    cur.setdefault("history", []).append(h)
    save_und(root, cfg, und)
    _rebuild(root, cfg)
    _record_decision(root, args.run_id, f"{n['aliases'][0]}: odwrót ({prev['state']} → proposed)" + (f" — {args.why}" if args.why else ""),
                     f"python3 tools/spec.py approve {n['aliases'][0]}", cur.get("cost_to_reverse"), args.by, at)
    print(f"{n['aliases'][0]} ({n['id']}): rozumienie = proposed (odwrót z {prev['state']})")
    return 0


def inbox_class(entry: dict, qcfg: dict) -> str:
    c = entry.get("class")
    if c and str(c).strip():
        return str(c).strip()
    k = str(entry.get("kind") or "").strip()
    return (qcfg.get("class_by_kind") or {}).get(k, k or "statement")


def says_approve(text: str, qcfg: dict) -> bool:
    t = fold(text or "").strip().strip(".!,; ")
    if not t:
        return False
    words = {fold(w).strip() for w in qcfg.get("approve_words") or []}
    return t in words or t.split()[0].strip(".!,;:") in words


def cmd_inbox(root, cfg, args):
    """Wpisy skrzynki spec_inbox (eksport JSON: lista albo {docs: [...]}, każdy z id) → werdykty, bez modelu.
    Klasy z apply_classes stosuje (approve/correct), tekst będący słowem zatwierdzenia = approve w każdej klasie,
    resztę eskaluje z powodem. Wypisuje JSON werdyktów do zapisania z powrotem w bazie strony (state + verdict)."""
    q = cfg["question"]
    raw = json.loads((sys.stdin.read() if args.file == "-" else Path(args.file).read_text(encoding="utf-8")))
    entries = raw.get("docs", raw) if isinstance(raw, dict) else raw
    verdicts = []
    for e in entries:
        if not isinstance(e, dict):
            continue
        data = e.get("data") if isinstance(e.get("data"), dict) else e
        if (data.get("state") or "new") != "new":
            continue
        klass = inbox_class(data, q)
        node = data.get("node") or data.get("alias")
        text = (data.get("text") or "").strip()
        v = {"doc_id": e.get("id") or data.get("id"), "node": node, "class": klass, "at": args.at or now_utc()}
        action = (q.get("apply_classes") or {}).get(klass)
        if action != "correct" and says_approve(text, q):
            action = "approve"
        if action == "correct" and not text:
            action = None
        if not node or not action:
            v.update(state="escalated", verdict="escalated",
                     why=("wpis bez pozycji" if not node else f"klasa „{klass}” wymaga osądu (nie ma jej w question.apply_classes)"))
            verdicts.append(v)
            continue
        argv = [action, node, "--by", args.by]
        if action == "correct":
            argv += ["--text", text]
        v["command"] = "python3 tools/spec.py " + " ".join(json.dumps(a, ensure_ascii=False) if " " in a else a for a in argv)
        if args.dry_run:
            v.update(state="applied", verdict="would-apply")
        else:
            try:
                with _quiet():
                    main(["--root", str(root), *argv])
                v.update(state="applied", verdict="applied")
            except SystemExit as ex:
                v.update(state="rejected", verdict="rejected", why=str(ex))
        verdicts.append(v)
    print(json.dumps({"verdicts": verdicts}, ensure_ascii=False, indent=1))
    return 0


class _quiet:
    def __enter__(self):
        import io
        self._o = sys.stdout
        sys.stdout = io.StringIO()

    def __exit__(self, *a):
        sys.stdout = self._o
        return False


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="spec.py", description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(REPO), help="korzeń repozytorium (domyślnie: ten)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("build")
    c = sub.add_parser("check")
    c.add_argument("--strict", action="store_true", help="sieroty LUKI są błędem")
    c.add_argument("--gaps", action="store_true", help="wypisz niezaklasyfikowane fragmenty")
    c.add_argument("--verbose", action="store_true")
    s = sub.add_parser("show")
    s.add_argument("ids", nargs="+")
    l = sub.add_parser("list")
    l.add_argument("--status")
    l.add_argument("--theme")
    l.add_argument("--understanding", choices=DEFAULT_CONFIG["understanding_states"])
    l.add_argument("--kind")
    l.add_argument("--file", choices=["owner", "slowa", "luki"])
    ci = sub.add_parser("cite")
    ci.add_argument("file", help="plik briefu albo - dla stdin")
    ci.add_argument("--full", action="store_true", help="wypisz pozycje z cytatami dosłownymi")
    a = sub.add_parser("add")
    a.add_argument("--id", required=True)
    a.add_argument("--date", required=True)
    a.add_argument("--quote-file", required=True)
    a.add_argument("--title")
    a.add_argument("--sense")
    a.add_argument("--by", default="owner")
    a.add_argument("--dry-run", action="store_true")
    for name in ("propose", "correct"):
        p = sub.add_parser(name)
        p.add_argument("id")
        p.add_argument("--text", required=True)
        p.add_argument("--by", default="claude" if name == "propose" else "owner")
        p.add_argument("--date")
        p.add_argument("--if-silent", dest="if_silent", metavar="TEKST",
                       help="co zrobie bez Twojej odpowiedzi (pokazane na stronie w kolejce; pusty napis usuwa; brak = bez zmian)")
        p.add_argument("--cost-to-reverse", dest="cost_to_reverse", metavar="TEKST",
                       help="koszt odwrotu, wartosc otwarta (przyklady: low, normal, high; strona domyslnie stawia 'high' na poczatku kolejki)")
        p.add_argument("--ask-after", dest="ask_after", metavar="OKRES",
                       help="po jakim czasie od pierwszego pokazania milczenie rozstrzyga (przyklady: 24h, 2d, 90m, PT6H, albo chwila ISO); pusty napis usuwa")
        p.add_argument("--criteria", action="append", metavar="TEKST",
                       help="kryterium rozstrzygniecia (powtarzalne; lista zastepuje poprzednia; jedno puste usuwa)")
        p.add_argument("--class", dest="question_class", metavar="KLASA",
                       help="klasa pytania, lista otwarta (przyklady: understanding, money, private-data, x10, product-direction)")
        if name == "propose":
            p.add_argument("--first-shown", dest="first_shown", metavar="ISO", help="chwila pierwszego pokazania (preset: teraz)")
    p = sub.add_parser("approve")
    p.add_argument("id")
    p.add_argument("--by", default="owner")
    p.add_argument("--date")
    p.set_defaults(text=None)
    sh = sub.add_parser("shown", help="IW-3: zapisz chwile pierwszego pokazania pytan (first_shown)")
    sh.add_argument("ids", nargs="+")
    sh.add_argument("--at")
    sh.add_argument("--force", action="store_true")
    si = sub.add_parser("silent", help="IW-3: wykonaj if_silent pytan po ask_after (tylko niski koszt odwrotu; reszta = eskalacja); bez modelu")
    si.add_argument("--now", help="chwila ISO (testy; preset: teraz)")
    si.add_argument("--low", nargs="*", default=None, metavar="KOSZT", help="koszty odwrotu uznane za niskie (preset: question.low_cost_to_reverse)")
    si.add_argument("--limit", type=int, default=None, help="twardy limit na bieg (preset: question.max_silent_per_run; -1 = bez limitu)")
    si.add_argument("--by", default="secretary")
    si.add_argument("--run-id", help="zapis biegu (tools/run_record.py), do ktorego trafia rekord decyzji")
    si.add_argument("--dry-run", action="store_true")
    ro = sub.add_parser("reopen", help="IW-3: odwrot - pozycja wraca do proposed (np. po rozstrzygnieciu milczeniem)")
    ro.add_argument("id")
    ro.add_argument("--by", default="owner")
    ro.add_argument("--at")
    ro.add_argument("--why")
    ro.add_argument("--run-id")
    ib = sub.add_parser("inbox", help="IW-3: wpisy spec_inbox (JSON) -> werdykty; approval/correction bez modelu, reszta eskalowana")
    ib.add_argument("file", help="plik JSON z wpisami albo - dla stdin")
    ib.add_argument("--by", default="owner (strona)")
    ib.add_argument("--at")
    ib.add_argument("--dry-run", action="store_true")
    h = sub.add_parser("html", help="żywy, zwijany widok drzewa (R73) -> docs/spec/SPEC.html; publikowany jako artefakt")
    h.add_argument("--focus", nargs="*", default=[], help="pozycje w edycji (id/alias): rozwinięte i podświetlone")
    h.add_argument("--recent-hours", type=int, default=48)
    h.add_argument("--out")
    h.add_argument("--queue-first", nargs="*", default=None, metavar="KOSZT",
                   help="koszty odwrotu, ktore ida na poczatek kolejki 'Czeka na Ciebie' (preset: high; bez wartosci = bez pierwszenstwa)")
    args = ap.parse_args(argv)
    root = Path(args.root).resolve()
    cfg = load_config(root)
    if args.cmd == "build":
        return cmd_build(root, cfg, args)
    if args.cmd == "check":
        return cmd_check(root, cfg, args)
    if args.cmd == "show":
        return cmd_show(root, cfg, args)
    if args.cmd == "list":
        return cmd_list(root, cfg, args)
    if args.cmd == "cite":
        return cmd_cite(root, cfg, args)
    if args.cmd == "add":
        return cmd_add(root, cfg, args)
    if args.cmd in ("shown", "silent", "reopen", "inbox"):
        return {"shown": cmd_shown, "silent": cmd_silent, "reopen": cmd_reopen, "inbox": cmd_inbox}[args.cmd](root, cfg, args)
    if args.cmd == "html":
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import spec_html
        argv2 = ["--spec", str(root / "docs/spec/spec.json"), "--recent-hours", str(args.recent_hours)]
        if args.out:
            argv2 += ["--out", args.out]
        if args.focus:
            argv2 += ["--focus", *args.focus]
        if args.queue_first is not None:
            argv2 += ["--queue-first", *args.queue_first]
        return spec_html.main(argv2)
    return _und_cmd(root, cfg, args, args.cmd)


if __name__ == "__main__":
    sys.exit(main())
