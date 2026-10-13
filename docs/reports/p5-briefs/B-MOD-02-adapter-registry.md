# B-MOD-02 — AdapterRegistry<Port>

Raport: `docs/architecture/MODULARITY_AUDIT_2026-10-13.md`. Zależy od: B-MOD-01. Jeden agent Sonnet, osobny worktree, commity punktów kontrolnych, pełna brama (czytaj linię `GATE GREEN/RED`, nie kod wyjścia), wpis w `docs/reports/INDEX.md` z decyzją i sposobem cofnięcia. Bez pytań technicznych do właściciela (CLAUDE.md).

## Zakres
Uogólnić `BackendRegistry` do szablonu `AdapterRegistry<Port>` (fabryka po `adapter`, status implemented/planned). Przenieść na niego media (ASR/OCR), embedding i batch; magazyny zostają na swoim API jako cienki alias.

## Pliki
`include/loom/storage_backend.h:80`, `src/storage/storage_backend.cpp`, `src/media/media_providers.cpp:361-382`, `src/providers/embedding.cpp:41-44`, `src/chat/batch_api.cpp:214`.

## Kroki
1. Szablon w `include/loom/adapter_registry.h` (kopia semantyki `BackendRegistry`, bez zmiany zachowania).
2. Media: usunąć lokalną mapę fabryk; OCR przestaje być zakodowany na `ocr_space`.
3. Embedding: adapter z rejestru zamiast `adapter != "openai"`.
4. Batch: typ z rejestru zamiast `type=="anthropic"`; dodać wpis `concurrent`.

## Testy
Testy kontraktu rejestru (dodanie adaptera testowego bez edycji konsumenta); istniejące testy storage/media/embedding bez zmian; nieznany adapter -> `Unsupported` z listą znanych.

## Kryterium akceptacji
Zero literałów nazw adapterów w konsumentach; dodanie fałszywego ASR w teście jednym wpisem.

## Ryzyko
Niskie.
