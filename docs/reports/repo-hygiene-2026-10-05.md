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

Bieżący `main` nie zawiera jeszcze dwóch poprawek `[this]` w serwerze.
W10 dostarczył je w `23c6e74c50b87c2157ffc317d45fca79aade27fa`.
W8 nie kopiuje starego `app.cpp`, bo utraciłby nową trasę `/api/packet`.
Końcowy build Clang/vendored korzysta z wąskiego archiwum
`archive/repo-hygiene-clang-fix-validation-2026-10-05`,
`cbf374b0d53f1bc56357f9f6716bfb74ffff041a`, tree
`661079afb8703fb8010425f7d1aa2f7d5c6cee6e`: wyłącznie dwa zastosowalne,
oryginalne capture hunki W10 ponad W8 `fedaf6b` (2 dodane / 2 usunięte linie).
Starsza pełna kompozycja W8/W10 `b31b4d4` pozostaje archiwalna i nie jest
źródłem końcowej macierzy.

| Konfiguracja | Build | Pełny CTest na aktualnym przyroście |
| --- | --- | --- |
| GCC/dev, WERROR, server/shared ON | PASS | Pierwsza próba 109/110; pełna powtórka w toku |
| Clang18/vendored, WERROR, server/shared ON + capture fix W10 | PASS | Do wykonania po zamrożeniu przyrostu Python |
| GCC13/ASan+UBSan, WERROR, server ON/shared OFF | PASS | Do wykonania z osobnym zwykłym FFI companion |

Pierwszy GCC CTest jest kompletny i zachowany w
`docs/archive/repo-hygiene-matrix-2026-10-05/gcc-first-attempt/`.
`research.structure` przekroczył istniejący limit 60 s; guard poprawnie odrzucił
próbę. Diagnostyka wykazała też brak obiektu historycznego Git w klonie
single-branch. Fetch archiwalnych gałęzi udostępnił oryginalny
`b118c80e981c08ec6d7f9ab6aacc177979186cf2`, z dokładnym wymaganym tree
`b122b30314009a4b7d793a2c634e5b65d6835397`; nie odzyskano brakującego aliasu
e8bbae. Bez edycji testów izolowany CTest przeszedł 1/1 w 33,28 s. Jego skrócony
output nie dowodzi liczby wewnętrznych przypadków i nie zastępuje pełnej bramki.

Wcześniejsze kompilacje zachowano z pełnymi logami: GCC linker memory kill,
ASan `cc1plus` OOM przy `-g` (parallel2 i1), oraz ENOSPC przy archiwizacji
normalnej i później linkowaniu CLI. Cgroup ma 8 GiB i wspólny dysk 32 GiB.
ASan używa `-g1`, zachowując linie stosu, asercje i komplet flag sanitizera.
Lokalne Clang/ASan korzystają z cienkich archiwów `ar qcT`, wyłącznie zmiana
magazynowania obiektów. Końcowe oba buildy są kompletne, także serwery.
Do odzyskania miejsca wyzerowano wyłącznie zakończone własne intermediates;
ponowny build w tych katalogach wymaga clean/regenerate. Chwilowo spakowane
własne pliki wykonywalne odtworzono byte-for-byte przed testami. Cache, binarne
hashe, logi i osobne receipts zachowują konfigurację oraz źródło wykonania.

`run_case.py` przypina commit/tree/helper, manifest pełnego preset oraz binaria
i wszystkie tracked runtime inputs przed i po. Rozbieżność unieważnia dowód.
Zachowuje 10 MiB outputu na zestaw, pełny LastTest.log i wynik niezmienionego
guard. Równoległość ustawia wyłącznie caller `--jobs`; brak nowych filtrów,
progów i wyjątków skip. Jawne lokalne recipe archiwów trafiają do receipt,
nie wymagamy nieistniejących cache keys dla zwykłych domyślnych archiwów.

Nowy bridge seeding → `loom.method_graph/1` jest opisany osobno w
[raporcie](repo-hygiene-seeding-method-bridge-2026-10-05.md). Po zastosowaniu
66/66 unittest w 9,243 s. Review wskazał dwie luki w helperze weryfikacji
(preflight ×10 i domyślny czas projekcji); naprawa przed finalnym freeze.
Płatnych wywołań: 0. Seeding/results używane przez testy pozostają w miejscu.
Gotowość jeszcze niezgłoszona: pełne bramki są w toku.

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
