#!/usr/bin/env python3
"""No user-visible text in code (owner, 2026-10-30). Scans loom/web/src/**/*.tsx for string literals a person would read
(JSX text, visible attributes, capitalised sentence-like literals in JSX lines) and compares them with the baseline
tools/ui_strings_baseline.json (DATA: file -> text -> {count, reason}). A literal that is not in the baseline FAILS; the baseline may
only shrink (--update-baseline refuses to add). Heuristic, like lint_asan_range_for: it finds candidates, the baseline records the reason.

  lint_ui_strings.py                  check (exit 1 on a new literal)
  lint_ui_strings.py --report         per-file counts of what the baseline still holds
  lint_ui_strings.py --update-baseline  drop baseline entries that no longer occur (never adds)
  lint_ui_strings.py --missing        keys missing per language, from one collected list of all catalog texts
  lint_ui_strings.py --init-baseline  (once) write a baseline from the current tree
"""
import json, re, sys, collections, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "loom/web/src"
BASE = ROOT / "tools/ui_strings_baseline.json"
PACKS = [ROOT / "loom/data/profiles/conversation_view.pack"]

ATTR = re.compile(r'\b(?:placeholder|title|aria-label|aria-description|alt|label|tooltip)=(?:"([^"{}]*[A-Za-z]{2,}[^"{}]*)"|\{\s*(?:"([^"]*[A-Za-z]{2,}[^"]*)"|\'([^\']*[A-Za-z]{2,}[^\']*)\')\s*\})')
JSXTEXT = re.compile(r'>([^<>{}=;&|()\n`]*[A-Za-z]{2,}[^<>{}=;&|()\n`]*)<')
JSXTEXT_EOL = re.compile(r'>\s*([A-Z][^<>{}=;&|\n`]*[a-z]{2,}[^<>{}=;&|\n`]*)$')   # text running to the end of a line inside an element
LIT = re.compile(r'"([A-Z][a-z]+[^"\n]*(?: [^"\n]*|[.…!?:]))"')   # "Sentence like", "Text…"
SKIP_LINE = re.compile(r'^\s*(?://|\*|/\*|import |export type|type |interface )|data-testid|className=|console\.|throw |log\.|\.test\(|new Error|aria-live|role=|href=|key=')
LETTERS = re.compile(r'[A-Za-zÀ-ɏ]{2,}')

def scan(path):
    found = collections.Counter()
    text = path.read_text(encoding="utf8")
    for line in text.splitlines():
        if SKIP_LINE.search(line): continue
        line = re.sub(r'(?<![:"\'])\s//\s.*$', '', line)   # a trailing code comment is not shown to anyone
        s = line.strip()
        if "<" not in s and "{" not in s and not s.startswith(('"', "'")): continue
        for m in ATTR.finditer(line):
            v = next(g for g in m.groups() if g is not None)
            if LETTERS.search(v): found[v.strip()] += 1
        for m in JSXTEXT.finditer(line):
            v = m.group(1).strip()
            if v and LETTERS.search(v) and not re.fullmatch(r"[\w\s.,:/-]*(?:=>|\))?", v) is None and not v.startswith(("=", ")", ",")): found[v] += 1
        if "<" in s and not s.endswith(">") or s.endswith(">"):
            m = JSXTEXT_EOL.search(line)
            if m and LETTERS.search(m.group(1)) and "<" in line[:m.start()] : found[m.group(1).strip()] += 1
        stripped = ATTR.sub("", line)
        if "<" in s or s.startswith("?") or s.startswith(":") or "? " in s or "&& " in s:
            for m in LIT.finditer(stripped):
                v = m.group(1)
                if " " in v or v.endswith(("…", ".", "!", "?")): found[v] += 1
    for v in [v for v in found if v == "Promise" or v.startswith("new ") or re.fullmatch(r"[a-z_]+", v) or ", g: Record" in v]:
        del found[v]   # type names, constructors and identifiers that look like words
    return found

def current():
    out = {}
    for p in sorted(SRC.rglob("*.tsx")):
        f = scan(p)
        if f: out[str(p.relative_to(ROOT))] = f
    return out

def load_base():
    return json.loads(BASE.read_text(encoding="utf8")) if BASE.exists() else {"entries": {}}

def check():
    base = load_base()["entries"]; bad = []
    for f, items in current().items():
        for t, n in items.items():
            allowed = base.get(f, {}).get(t, {}).get("count", 0)
            if n > allowed: bad.append(f"{f}: user-visible text in code ({n} > {allowed} allowed): {t!r}")
    if bad:
        print("\n".join(bad))
        print(f"\n{len(bad)} new user-visible literal(s). Put the text in a catalog (loom/data/profiles/conversation_view.pack, feature ui_shell, languages en/pl/...) and read it with the shell text hook t(id).")
        return 1
    n = sum(v["count"] for e in base.values() for v in e.values())
    print(f"lint_ui_strings: ok, no new literal ({n} in the baseline, tools/ui_strings_baseline.json)")
    return 0

def write_base(entries_by_file, reasons=None):
    reasons = reasons or {}
    ent = {f: {t: {"count": n, "reason": reasons.get(f, {}).get(t, "existing before 2026-10-30; to be moved to the catalogs")} for t, n in sorted(it.items())} for f, it in sorted(entries_by_file.items())}
    BASE.write_text(json.dumps({"_note": "DATA: user-visible literals still in code. The list may only shrink: a new literal fails tools/lint_ui_strings.py.", "entries": ent}, ensure_ascii=False, indent=1) + "\n", encoding="utf8")

def update():
    base = load_base()["entries"]; cur = current(); keep = {}
    for f, items in base.items():
        for t, v in items.items():
            n = min(v["count"], cur.get(f, {}).get(t, 0))
            if n: keep.setdefault(f, {})[t] = n
    reasons = {f: {t: v.get("reason") for t, v in it.items()} for f, it in base.items()}
    write_base(keep, reasons); print("baseline now holds", sum(n for it in keep.values() for n in it.values()))
    return 0

def missing():
    langs = set(); table = {}
    for p in PACKS:
        for e in json.loads(p.read_text(encoding="utf8"))["entries"]:
            v = e.get("value")
            if isinstance(v, dict) and "locales" in v:
                langs |= set(v["locales"]); table[v["feature"]] = v["locales"]
    total = sorted(langs); rc = 0
    allkeys = sum(len(set().union(*[set(c) for c in loc.values()])) for loc in table.values())
    print(f"texts collected: {allkeys} keys in {len(table)} catalogs; languages: {', '.join(total)}")
    for lg in total:
        gaps = {ft: sorted(set().union(*[set(c) for c in loc.values()]) - set(loc.get(lg, {}))) for ft, loc in table.items()}
        gaps = {k: v for k, v in gaps.items() if v}
        print(f"  {lg}: {sum(len(v) for v in gaps.values())} missing" + ("".join(f"\n    {k}: {len(v)}" for k, v in gaps.items())))
    return rc

if __name__ == "__main__":
    a = sys.argv[1:]
    if "--missing" in a: sys.exit(missing())
    if "--init-baseline" in a: write_base(current()); sys.exit(0)
    if "--update-baseline" in a: sys.exit(update())
    if "--report" in a:
        for f, it in load_base()["entries"].items(): print(f"{sum(v['count'] for v in it.values()):4d}  {f}")
        sys.exit(0)
    sys.exit(check())
