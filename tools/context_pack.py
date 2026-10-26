#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Pakiet kontekstu → prefiks briefu z hashem; katalog narzędzi; sprawdzenie briefu (pakiet IW-2, 2026-10-26).

0 tokenów, bez sieci, bez modelu. Projekt: docs/architecture/INTERFEJS_WSPOLPRACY_v2_2026-10-23.md §2.2, §2.4, §2.5, §4 IW-2.

  python3 tools/context_pack.py list                         pakiety z presetów
  python3 tools/context_pack.py render podwykonawca [--json] prefiks (tekst albo {pack, hash, refs, text})
  python3 tools/context_pack.py hash podwykonawca            sam hash prefiksu
  python3 tools/context_pack.py fill-brief brief.json [-o out.json]
        uzupełnia brief (args single-package.js) o context_prefix + context_pack_hash z pola context_pack
  python3 tools/context_pack.py check-brief brief.json [--strict]
        advisory (nie blokada, chyba że --strict): brak spec:, nieznane numery (spec.py cite), hash prefiksu
        nieaktualny wobec pakietu, nieznany pakiet
  python3 tools/context_pack.py tools [--place vm] [--json]  katalog narzędzi (tools/routines/tools.json)
  python3 tools/context_pack.py check-tools                  katalog: pola, efekty nieznane = najgorszy przypadek

Presety: tools/routines/context_packs.json, tools/routines/tools.json (albo --packs / --tools). Korzeń: --root (repo).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
PACKS = HERE / "routines" / "context_packs.json"
TOOLS = HERE / "routines" / "tools.json"
CITE_RE = re.compile(r"spec:([A-Za-z0-9_.:\-]+[A-Za-z0-9_])")


class PackError(Exception):
    pass


def sha(text: str | bytes) -> str:
    b = text.encode("utf-8") if isinstance(text, str) else text
    return "sha256:" + hashlib.sha256(b).hexdigest()


def load_json(p: Path) -> dict:
    return json.loads(Path(p).read_text(encoding="utf-8"))


# ---------------------------------------------------------------- odwołania

def _section(text: str, heading: str) -> str:
    lines = text.splitlines(keepends=True)
    level = len(heading) - len(heading.lstrip("#"))
    start = None
    for i, l in enumerate(lines):
        if l.rstrip("\n") == heading:
            start = i
            break
    if start is None:
        raise PackError(f"nie ma nagłówka {heading!r}")
    end = len(lines)
    for j in range(start + 1, len(lines)):
        m = re.match(r"^(#+) ", lines[j])
        if m and len(m.group(1)) <= level:
            end = j
            break
    return "".join(lines[start:end]).rstrip("\n") + "\n"


def _spec_text(root: Path, ids: list[str]) -> str:
    r = subprocess.run([sys.executable, str(root / "tools" / "spec.py"), "show", *ids], cwd=root,
                       capture_output=True, text=True)
    if r.returncode != 0:
        raise PackError(f"spec.py show {' '.join(ids)}: {r.stderr.strip() or r.stdout.strip()}")
    return r.stdout.rstrip("\n") + "\n"


def tools_text(cat: dict, place: str | None = None) -> str:
    out = []
    for t in cat.get("tools", []):
        av = t.get("availability") or {}
        if place:
            a = av.get(place)
            where = f"{place}: {a['state']} ({a.get('observed', '?')})" if a else f"{place}: brak obserwacji"
            if a and a.get("substitute"):
                where += f"; zamiennik: {a['substitute']}"
        else:
            where = "; ".join(f"{p}: {a['state']} ({a.get('observed', '?')})" for p, a in av.items()) or "brak obserwacji"
        eff = ", ".join(effects_of(cat, t))
        out.append(f"- {t['id']}: {t['does']} | {t.get('run', '')} | efekty: {eff} | {where}")
    return "\n".join(out) + "\n"


def effects_of(cat: dict, tool: dict) -> list[str]:
    """Efekty narzędzia; nieznane = najgorszy przypadek (preset, jak tool_sources.h)."""
    eff = tool.get("effects")
    if eff:
        return list(eff)
    pre = cat.get("presets", {})
    if pre.get("unknown_effects", "worst_case") == "worst_case":
        return list(pre.get("worst_case_effects", ["unknown"]))
    return ["unknown"]


def resolve_ref(root: Path, ref: dict, tools_cat: dict | None) -> tuple[str, str]:
    """(label, treść) odwołania. Rodzaje: file, section, lines, spec, tools, text (lista otwarta: dopisuje się tu)."""
    kind = ref["kind"]
    if kind == "file":
        return ref.get("label") or ref["path"], (root / ref["path"]).read_text(encoding="utf-8")
    if kind == "section":
        return ref.get("label") or ref["heading"], _section((root / ref["path"]).read_text(encoding="utf-8"), ref["heading"])
    if kind == "lines":
        rx = re.compile(ref["match"])
        hits = [l for l in (root / ref["path"]).read_text(encoding="utf-8").splitlines() if rx.search(l)]
        n = ref.get("max_matches")
        if n:
            hits = hits[:n]
        return ref.get("label") or ref["path"], "\n".join(hits) + "\n"
    if kind == "spec":
        return ref.get("label") or " ".join(ref["ids"]), _spec_text(root, ref["ids"])
    if kind == "tools":
        return ref.get("label") or "katalog narzędzi", tools_text(tools_cat or {}, ref.get("place"))
    if kind == "text":
        return ref.get("label") or "tekst", ref["text"].rstrip("\n") + "\n"
    raise PackError(f"nieznany rodzaj odwołania {kind!r} (są: file, section, lines, spec, tools, text)")


def render(root: Path, packs: dict, name: str, tools_cat: dict | None = None) -> dict:
    if name not in packs.get("packs", {}):
        raise PackError(f"nie ma pakietu {name!r} (są: {', '.join(packs.get('packs', {}))})")
    pre = packs.get("presets", {})
    pack = packs["packs"][name]
    parts = [pre.get("header", "=== PAKIET {pack} ===").format(pack=name)]
    refs = []
    for ref in pack.get("refs", []):
        label, body = resolve_ref(root, ref, tools_cat)
        h = sha(body)
        ident = ref.get("path") or ",".join(ref.get("ids", [])) or ref["kind"]
        refs.append({"kind": ref["kind"], "ref": ident, "hash": h, "mode": ref.get("mode", "full")})
        parts.append(pre.get("ref_header", "--- {kind}: {label} ({ref_hash}) ---").format(
            kind=ref["kind"], label=label, ref_hash=h))
        if ref.get("mode", "full") == "pointer":
            parts.append(pre.get("pointer", "[{path} {ref_hash}]").format(path=ident, ref_hash=h))
        else:
            parts.append(body.rstrip("\n"))
    more = pack.get("more", pre.get("more", {}))
    parts.append(f"(więcej: allowed={str(more.get('allowed', True)).lower()}, recorded={str(more.get('recorded', True)).lower()})")
    parts.append(pre.get("footer", "=== KONIEC {pack} ===").format(pack=name))
    text = "\n".join(parts) + "\n"
    return {"pack": name, "hash": sha(text), "refs": refs, "text": text}


# ---------------------------------------------------------------- brief

def spec_ids_of(brief: dict) -> list[str]:
    ids = [str(s).removeprefix("spec:") for s in brief.get("spec") or []]
    text = json.dumps({k: v for k, v in brief.items() if k != "context_prefix"}, ensure_ascii=False)
    for tok in CITE_RE.findall(text):
        if tok not in ids:
            ids.append(tok)
    return ids


def check_brief(root: Path, packs: dict, brief: dict, tools_cat: dict | None = None) -> list[dict]:
    adv = []
    ids = spec_ids_of(brief)
    if not ids:
        adv.append({"kind": "no_spec", "text": "brief nie cytuje żadnego spec:<numer> (pole spec[] albo spec: w tekście)"})
    else:
        r = subprocess.run([sys.executable, str(root / "tools" / "spec.py"), "cite", "-"], cwd=root,
                           input=" ".join(f"spec:{i}" for i in ids), capture_output=True, text=True)
        for line in r.stdout.splitlines():
            if line.startswith("BRAK"):
                adv.append({"kind": "unknown_spec", "text": line.strip()})
    name = brief.get("context_pack")
    if not name:
        adv.append({"kind": "no_context_pack", "text": "brief nie nazywa pakietu kontekstu (pole context_pack)"})
    elif name not in packs.get("packs", {}):
        adv.append({"kind": "unknown_context_pack", "text": f"nie ma pakietu {name!r}"})
    else:
        cur = render(root, packs, name, tools_cat)
        if brief.get("context_pack_hash") and brief["context_pack_hash"] != cur["hash"]:
            adv.append({"kind": "stale_prefix", "text": f"hash prefiksu w briefie {brief['context_pack_hash'][:19]}… ≠ "
                                                        f"obecny {cur['hash'][:19]}… (pakiet zmienił się od wypełnienia)"})
        if brief.get("context_prefix") and sha(brief["context_prefix"]) != brief.get("context_pack_hash"):
            adv.append({"kind": "prefix_hash_mismatch", "text": "context_prefix nie zgadza się z context_pack_hash"})
    return adv


def fill_brief(root: Path, packs: dict, brief: dict, tools_cat: dict | None = None) -> dict:
    name = brief.get("context_pack")
    if not name:
        raise PackError("brief nie ma pola context_pack")
    r = render(root, packs, name, tools_cat)
    out = dict(brief)
    out["context_prefix"] = r["text"]
    out["context_pack_hash"] = r["hash"]
    out["spec"] = spec_ids_of(brief)
    return out


def check_tools(cat: dict) -> list[str]:
    errs, seen = [], set()
    for i, t in enumerate(cat.get("tools", [])):
        for k in ("id", "does"):
            if not t.get(k):
                errs.append(f"tools[{i}]: brak {k}")
        if t.get("id") in seen:
            errs.append(f"tools[{i}]: powtórzone id {t['id']}")
        seen.add(t.get("id"))
        for place, a in (t.get("availability") or {}).items():
            if not a.get("state") or not a.get("observed"):
                errs.append(f"{t.get('id')}: dostępność {place} bez stanu albo daty obserwacji (R60)")
            elif not re.fullmatch(r"\d{4}-\d{2}-\d{2}.*", a["observed"]):
                errs.append(f"{t.get('id')}: {place}.observed nie jest datą")
    return errs


# ---------------------------------------------------------------- CLI

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="context_pack.py", description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(REPO))
    ap.add_argument("--packs", default=str(PACKS))
    ap.add_argument("--tools", default=str(TOOLS))
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    r = sub.add_parser("render")
    r.add_argument("pack")
    r.add_argument("--json", action="store_true")
    h = sub.add_parser("hash")
    h.add_argument("pack")
    fb = sub.add_parser("fill-brief")
    fb.add_argument("brief")
    fb.add_argument("-o", "--out")
    cb = sub.add_parser("check-brief")
    cb.add_argument("brief")
    cb.add_argument("--strict", action="store_true", help="advisory → kod wyjścia 1")
    t = sub.add_parser("tools")
    t.add_argument("--place")
    t.add_argument("--json", action="store_true")
    sub.add_parser("check-tools")
    a = ap.parse_args(argv)
    root = Path(a.root)
    try:
        packs = load_json(Path(a.packs))
        cat = load_json(Path(a.tools)) if Path(a.tools).exists() else {}
        if a.cmd == "list":
            for name, p in packs.get("packs", {}).items():
                print(f"{name}\t{len(p.get('refs', []))} odwołań\t{p.get('for', '')}")
        elif a.cmd == "render":
            res = render(root, packs, a.pack, cat)
            print(json.dumps(res, ensure_ascii=False, indent=1) if a.json else res["text"], end="" if not a.json else "\n")
        elif a.cmd == "hash":
            print(render(root, packs, a.pack, cat)["hash"])
        elif a.cmd == "fill-brief":
            out = fill_brief(root, packs, load_json(Path(a.brief)), cat)
            text = json.dumps(out, ensure_ascii=False, indent=1) + "\n"
            if a.out:
                Path(a.out).write_text(text, encoding="utf-8")
                print(f"{a.out}: {out['context_pack_hash']}")
            else:
                print(text, end="")
        elif a.cmd == "check-brief":
            adv = check_brief(root, packs, load_json(Path(a.brief)), cat)
            for x in adv:
                print(f"UWAGA [{x['kind']}] {x['text']}")
            print(f"-- {len(adv)} uwag (advisory, nie blokada{'; --strict: blokada' if a.strict else ''})")
            return 1 if (a.strict and adv) else 0
        elif a.cmd == "tools":
            if a.json:
                print(json.dumps(cat, ensure_ascii=False, indent=1))
            else:
                print(tools_text(cat, a.place), end="")
        elif a.cmd == "check-tools":
            errs = check_tools(cat)
            for e in errs:
                print("BŁĄD", e)
            print(f"{len(cat.get('tools', []))} narzędzi, {len(errs)} błędów")
            return 1 if errs else 0
        return 0
    except (PackError, FileNotFoundError, KeyError) as e:
        print(f"błąd: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
