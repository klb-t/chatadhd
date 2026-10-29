#!/usr/bin/env python3
"""Skan na pułapkę z CLAUDE.md (widzi ją tylko AddressSanitizer): pętla `for (x : f(...)[k])` / `.at(k)` / `.value(k)` po wyniku funkcji zwróconym
PRZEZ WARTOŚĆ - tymczasowy obiekt umiera przed pętlą, żyje tylko ostatnie odwołanie. Heurystyka po DODANYCH liniach różnicy (do ręcznego przeglądu, bez
fałszywych "wszystko dobrze"): zgłasza każdą linię `for (... : <coś z wywołaniem ( ) i potem [ albo .at( albo .value(>`.

  python3 tools/lint_asan_range_for.py [<baza>]      # domyślnie origin/main; wyjście 1, gdy coś znaleziono (do przejrzenia, nie zawsze błąd)
"""
import re, subprocess, sys

PAT = re.compile(r"\bfor\s*\(.*:\s*[^;{}]*\([^()]*\)\s*(\[|\.at\(|\.value\()")
base = sys.argv[1] if len(sys.argv) > 1 else "origin/main"
diff = subprocess.run(["git", "diff", "-U0", f"{base}...HEAD", "--", "loom/src", "loom/include", "loom/tests", "loom/server", "loom/cli"],
                      capture_output=True, text=True, check=False).stdout
cur, hits = None, []
for line in diff.splitlines():
    if line.startswith("+++ "):
        cur = line[6:]
    elif line.startswith("+") and not line.startswith("+++") and cur and cur.endswith((".cpp", ".h", ".hpp", ".inc")):
        body = line[1:]
        if body.lstrip().startswith("//"):
            continue
        if PAT.search(body):
            hits.append((cur, body.strip()))
for f, b in hits:
    print(f"{f}: {b[:200]}")
print(f"{len(hits)} podejrzanych linii (przejrzyj: bezpieczne jest tylko, gdy wyrażenie po ':' zwraca referencję do obiektu, który żyje dalej)")
sys.exit(1 if hits else 0)
