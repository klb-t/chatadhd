# B-MOD-08 — `loom_invoke` i trasy z tabeli akcji

Raport: `docs/architecture/MODULARITY_AUDIT_2026-10-13.md`. Zależy od: B-MOD-07. Jeden agent Sonnet, osobny worktree, commity punktów kontrolnych, pełna brama (czytaj linię `GATE GREEN/RED`, nie kod wyjścia), wpis w `docs/reports/INDEX.md` z decyzją i sposobem cofnięcia. Bez pytań technicznych do właściciela (CLAUDE.md).

## Zakres
Jeden punkt wejścia ABI `loom_invoke(port, action, json)` zamiast kierowania akcji magazynu przez `loom_github_sync`; trasy serwera i polecenia CLI generowane z tabeli akcji (addytywnie; stare symbole zostają jako aliasy).

## Pliki
`include/loom/loom.h`, `src/capi/capi_media.cpp:129-135`, `src/storage/storage_command.cpp`, `server/src/app.cpp:745-758`, `cli/main.cpp`, `web/src/api/loom-jni.ts`.

## Kroki
1. Tabela akcji (port, action, schemat, HTTP verb, ścieżka).
2. `loom_invoke` + eksport w `loom.h`; aktualizacja `test_abi_compat`.
3. Server rejestruje trasy z tabeli dla akcji storage; reszta później.
4. Usunąć dispatch storage z `loom_github_sync` po okresie aliasu.

## Testy
`test_abi_compat.py`; trasy storage identyczne odpowiedzią; CLI `storage` bez zmian; JNI most.

## Kryterium akceptacji
Akcja storage dostępna przez `loom_invoke`; `loom_github_sync` robi tylko GitHub.

## Ryzyko
Wysokie: kontrakt ABI; tylko zmiany addytywne w tej fali.
