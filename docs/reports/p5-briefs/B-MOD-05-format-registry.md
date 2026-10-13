# B-MOD-05 — FormatRegistry importera

Raport: `docs/architecture/MODULARITY_AUDIT_2026-10-13.md`. Zależy od: B-MOD-02. Jeden agent Sonnet, osobny worktree, commity punktów kontrolnych, pełna brama (czytaj linię `GATE GREEN/RED`, nie kod wyjścia), wpis w `docs/reports/INDEX.md` z decyzją i sposobem cofnięcia. Bez pytań technicznych do właściciela (CLAUDE.md).

## Zakres
Rejestr formatów (`detect` + `body`) i tabela dostawców eksportu (handlery per `provider`) zamiast łańcuchów `if`. Nazwy platform (`chatgpt`/`claude`/`openai`/`anthropic`) przenieść do presetów danych (R42).

## Pliki
`src/import/importer_core.cpp:126-141,510-568`, `src/import/export_archive.cpp:165-720`, `src/catalog/scan.cpp:76-90`, `src/catalog/extract_text.cpp:68`, `src/catalog/import.cpp:41`, `src/extract/extract.cpp:126`, `src/extract/sources.cpp:43`, `src/archive/ingest.cpp:628`.

## Kroki
1. Najpierw `importer_core` (formaty plikowe).
2. Potem `export_archive`: struktura `ProviderHandlers` z rejestru, rozdzielić `openai` i `anthropic` do własnych jednostek.
3. Detekcja platformy z presetu (`import_presets.inc`).

## Testy
Wszystkie testy zgodności importu bez zmian; nowy format testowy dodany jednym wpisem; nieznany -> węzeł `unrecognized` (G7).

## Kryterium akceptacji
`grep -rn '== "openai"\|== "claude"' loom/src` = 0 poza danymi.

## Ryzyko
Średnie: duży kod, dużo fixtures; podzielić na 2–3 commity.
