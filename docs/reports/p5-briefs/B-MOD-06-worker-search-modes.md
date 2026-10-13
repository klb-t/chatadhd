# B-MOD-06 — Tryby workera, batch i selectora jako wpisy rejestru

Raport: `docs/architecture/MODULARITY_AUDIT_2026-10-13.md`. Zależy od: B-MOD-02, B-MOD-03. Jeden agent Sonnet, osobny worktree, commity punktów kontrolnych, pełna brama (czytaj linię `GATE GREEN/RED`, nie kod wyjścia), wpis w `docs/reports/INDEX.md` z decyzją i sposobem cofnięcia. Bez pytań technicznych do właściciela (CLAUDE.md).

## Zakres
Tryby `regex`/`llm`/`batch` workera semantycznego i warstwy selectora (keyword/tfidf/embedding) jako wpisy `AdapterRegistry`; wybór z profilu `worker`/`ports`. Selector przyjmuje listę warstw (kompozycja), nie `int tier`.

## Pliki
`src/worker/semantic_worker.cpp:244,850`, `src/knowledge/engine.cpp:296`, `src/archive/pipeline.cpp:156`, `src/search/selector.cpp:104,223`, `include/loom/selector.h`.

## Kroki
1. Interfejs `AnalysisMode`; trzy implementacje wyciągnięte z ciała workera.
2. Selector: `SelectorEngine(std::vector<SearchTier>)`, stary konstruktor jako adapter.
3. Odczyt trybu z profilu.

## Testy
Testy workera (każdy tryb), zgodność selectora z `verify_selector.py`, mieszanie dwóch trybów w jednym przebiegu.

## Kryterium akceptacji
Brak `use_llm ? "llm" : "regex"` i `int tier` w API publicznym.

## Ryzyko
Niskie.
