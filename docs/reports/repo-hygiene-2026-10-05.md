# Wątek 8 — aktualna macierz, 2026-10-05

Wznowienie na `main=e4109df7e4af22b461def5f7d62e268d9b9a8825`.
Szesnaście commitów W8 przeszło czysty rebase; wypchnięty tip rebase:
`76c78ece83f6945112977dd4316f8acafdd865ee`.
Stary tip zachowano pod `archive/repo-hygiene-before-main-rebase-2026-10-05`.
Poprzednie [dowody](repo-hygiene-2026-10-04.md) pozostają historyczne.

## Aktualny przyrost i weryfikacja w toku

Pełna macierz: GCC/dev, Clang/vendored oraz natywny ASan/UBSan.
Bez zmiany progów, sanitizer options, CMake i przypadków testowych.
Nowy `compat.test_packet` musi wykonać przypadki; nie dodano wyjątku skip.
ASan zachowuje `LOOM_SHARED=OFF` dla instrumentowanych plików wykonywalnych.
Workflow dodatkowo buduje zwykłą bibliotekę GCC z tego samego commit/tree dla
wszystkich testów ctypes. Osobne pokwitowanie zachowuje jej hash i konfigurację.
To wykonanie FFI bez sanitizera, a nie sanitizer coverage C API.

W momencie fetch `main` nadal zawiera dwa błędne `[this]` w serwerze.
Poprawka W10: `23c6e74c50b87c2157ffc317d45fca79aade27fa`, tip
`fa7538d650da2f4ad37f5ff9254b60a6ee72938f`.
W8 nie kopiuje starego W10 `app.cpp`, gdyż utraciłby nową trasę `/api/packet`.
Wyniki na własnej gałęzi oraz kombinowanym źródle walidacyjnym będą rozdzielone.
Automatyczne czyste `git merge-tree` W8+W10 (bez ręcznej edycji implementacji)
zachowano w `archive/repo-hygiene-w10-validation-2026-10-05`:
`b31b4d439594bca0a051a2e95557a3fd06c39141`, tree
`611cc9acf0c9085a6b125d4164df98f16462679c`.
Gotowość jeszcze niezgłoszona: pełne bramki są w toku.

Pierwsze dwie kompilacje ASan (`-g`, równolegle2 i potem1) zakończyły się
`Killed signal terminated program cc1plus`; cgroup ma limit8GiB i raportuje
OOM kills. Oryginalne logi zachowano w `docs/archive/repo-hygiene-matrix-2026-10-05/`.
Nowa próba oraz preset CI używają minimalnych informacji Debug `-g1`: pozostają
linie stosu, asercje, ostrzeżenia i wszystkie flagi ASan/UBSan. Nie zmieniono
optymalizacji ani progów. Kompilacja Clanga została kontrolowanie wstrzymana,
żeby zwolnić pamięć dla pozostałych konfiguracji; jej log również zachowano.

## Do wątku 4

`unit.test_packet` wykonuje cztery przypadki rdzenia pod ASan. Obecne testy
`loom_packet` są tylko Python/ctypes. Potrzebny osobny natywny harness C API:
init/free, poprawne i błędne wejścia oraz polityka. Companion FFI zapewnia
wykonanie tych istniejących testów, ale nie zamyka tej luki sanitizerowej.

## Do wątku 10

Poprawka Clanga potwierdzona w źródle gałęzi; potrzebny odbiór przez W9.
Zachować nową trasę `/api/packet` podczas rebase serwera.

## Do wątku 9

Macierz w toku. Nie traktować dawnych 108/108 jako dowodu aktualnego kodu.
W8 zachowuje własny zakres; odbiór source fix W10 pozostaje zadaniem integratora.
