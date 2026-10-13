# B-MOD-07 — RuntimeParts: wstrzykiwalny korzeń kompozycji

Raport: `docs/architecture/MODULARITY_AUDIT_2026-10-13.md`. Zależy od: B-MOD-02. Jeden agent Sonnet, osobny worktree, commity punktów kontrolnych, pełna brama (czytaj linię `GATE GREEN/RED`, nie kod wyjścia), wpis w `docs/reports/INDEX.md` z decyzją i sposobem cofnięcia. Bez pytań technicznych do właściciela (CLAUDE.md).

## Zakres
Rozbić `Runtime::create` na `RuntimeParts` (fabryki portów z wartościami domyślnymi) tak, by testy i wdrożenia (serwer, telefon) podmieniały moduł bez edycji `runtime.cpp`.

## Pliki
`src/runtime.cpp:159-281`, `include/loom/runtime.h`.

## Kroki
1. Struktura `RuntimeParts` (opcjonalne fabryki: http, llm, storage, media, embed, vcs).
2. `create` używa domyślnych, jeśli puste.
3. Migrować jedną część na raz, zaczynając od http (już jest `opts.http`).

## Testy
Wszystkie testy Runtime; test z częścią zastępczą (fałszywy LLM) przez `RuntimeParts`.

## Kryterium akceptacji
Brak nowych `make_unique` w `runtime.cpp` dla portów.

## Ryzyko
Średnie: kolejność inicjalizacji i referencje.
