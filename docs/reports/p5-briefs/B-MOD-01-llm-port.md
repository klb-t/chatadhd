# B-MOD-01 — LlmPort: jedno miejsce wołania modelu

Raport: `docs/architecture/MODULARITY_AUDIT_2026-10-13.md`. Zależy od: —. Jeden agent Sonnet, osobny worktree, commity punktów kontrolnych, pełna brama (czytaj linię `GATE GREEN/RED`, nie kod wyjścia), wpis w `docs/reports/INDEX.md` z decyzją i sposobem cofnięcia. Bez pytań technicznych do właściciela (CLAUDE.md).

## Zakres
Wydzielić port `LlmPort` (`complete`, `stream`) budowany z `ProviderManifest` (URL, nagłówki, schemat auth, sekret z `auth_secret`). Szesć kopii wywołania `/chat/completions` wołają port. Domyślne zachowanie (OpenRouter, `api_key`, `base_url`) bez zmian dla użytkownika.

## Pliki
`loom/include/loom/providers.h`, nowy `loom/include/loom/llm_port.h`, `src/providers/`; wołający: `src/chat/chat_engine.cpp:1215`, `src/semantic/semantic_llm.cpp:126`, `src/worker/semantic_worker.cpp:712`, `src/chat/batch_api.cpp:43`, `src/archive/pipeline.cpp:286`, `src/context/context_engine.cpp:581`; `src/providers/providers.cpp:192,412`; `src/core/config.cpp:199`.

## Kroki
1. Port + implementacja OpenAI-zgodna czytająca manifest.
2. Najpierw semantic_llm (najmniejszy), potem worker, archive, context_engine, na końcu chat_engine (strumień).
3. Usunąć `if (m.id=="openrouter")`; adres z manifestu, nagłówki `HTTP-Referer`/`X-Title` do danych manifestu.
4. Rola `llm` w manifeście wybierana kluczem konfiguracji (na razie jeden wybór globalny; zakresy w B-MOD-03).

## Testy
Test przechwyconego żądania (A/B przed/po: identyczny URL, nagłówki, body) dla każdego z 6 wywołań; manifest z innym `base_url`/sekretem zmienia żądanie bez rekompilacji; strumień SSE; błąd 401 -> `ValueError` jak dziś.

## Kryterium akceptacji
`grep -rn '/chat/completions' loom/src` trafia tylko w jeden plik portu; `grep 'get_string("api_key")'` poza portem = 0; CTest zielony.

## Ryzyko
Średnie: ścieżka czatu i strumień; wykonywać jedno wywołanie na commit.
