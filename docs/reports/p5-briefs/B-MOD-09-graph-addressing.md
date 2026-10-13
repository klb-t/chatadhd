# B-MOD-09 — Adaptery, wybory i przebiegi jako węzły grafu

Raport: `docs/architecture/MODULARITY_AUDIT_2026-10-13.md`. Zależy od: B-MOD-03. Jeden agent Sonnet, osobny worktree, commity punktów kontrolnych, pełna brama (czytaj linię `GATE GREEN/RED`, nie kod wyjścia), wpis w `docs/reports/INDEX.md` z decyzją i sposobem cofnięcia. Bez pytań technicznych do właściciela (CLAUDE.md).

## Zakres
Każdy adapter, profil wyboru roli i przebieg przez port ma węzeł z `produced_by`; odniesienia do sekretów, nigdy bajty (G1–G6). Wpis grafu (R45) pokazuje aktywne role.

## Pliki
`src/graph/graph_entry.cpp` (providery wpisu), `src/kb/`, `include/loom/graph_entry.h`, `web/src/components/GraphEntryPanel.tsx`.

## Kroki
1. Provider wpisu `ports` (role, adapter, zakres, powód z `explain`).
2. Zapis przebiegu portu: adapter, wersja profilu, koszt.
3. Test G6: skan ładunków węzłów na wzory kluczy = 0.

## Testy
Test G2 (spacer po `produced_by` kończy w źródle), G6, G10 (`explain` na roli).

## Kryterium akceptacji
Z węzła odpowiedzi dojście do adaptera i profilu wyboru.

## Ryzyko
Niskie.
