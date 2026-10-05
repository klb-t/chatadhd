# W8 — pełny dowód bieżącego przyrostu, 2026-10-04

Źródło faktycznie wykonanych testów: `57d9daa19591ba7008d3158c7ba49db1f609c110`.
Po ich zakończeniu rebase na `main=ba6eaf6` zmienił wyłącznie dokument R39–R41.
[Kontrola zgodności](local-vendored-gcc/source-correspondence.json) ponownie
potwierdza wszystkie 1164 hashe wejść i sześć hashów wykonanych binariów.
Nie przemianowano źródła oryginalnego pokwitowania.

| Wykonanie | Wynik | Dowód |
|---|---|---|
| Pełny lokalny CTest, vendored SQLite/GCC | 108/108, 102,58 s | [Oryginalny XML](local-vendored-gcc/ctest.xml), [manifest](local-vendored-gcc/ctest-manifest.json), [LastTest](local-vendored-gcc/LastTest.log), [log](local-vendored-gcc/ctest.log) |
| Kontrola faktycznego wykonania | 107 wykonanych wpisów; 659 native / 24 465 asercji; 1290 Python / 0 skips | [Wynik strażnika](local-vendored-gcc/executed-cases.json) |
| Dawne puste zestawy | context_engine18/1364; knowledge18/150; lineage2/15 | Ten sam XML i wynik strażnika |
| Seeding | 50/50; dokładne 5 grafów, 61 przypadków, 2135 rankingów, 648 metryk | [Golden](seeding/golden-proof.json), [niezależny replay root](seeding/root-golden-replay.json), [pełny pakiet](seeding/README.md) |
| Strażnik / narzędzia CI | 28/28 guard; 38/38 wszystkich narzędzi | [Log](evidence-policy/all-github-scripts.log), [replay44+2CI](evidence-policy/comparison.json) |
| Build web | 85 modułów, 2,18 s | [Log](web-build.log) |
| Nowe możliwości W10 | 2 brakujące deklaracje, 0 wykonanych testów produktu; helper6/6 | [Dowód procesów](web-capabilities/README.md) |

`unit.test_catalog_scale` ma 0/0 i jest jawnie **niewykonany**. Nie jest to
potwierdzenie 1 GB skali. Wynik strażnika ma policy/schema/revision i ich hashe.
Żaden test, próg lub ostrzeżenie nie został usunięty albo poluzowany.

## Konfiguracja i odtwarzanie

[Pokwitowanie builda](local-vendored-gcc/build-receipt.json) pinowało runner,
oba narzędzia kompatybilności, CLI, serwer i libloom. [Wejścia](local-vendored-gcc/source-sha256.json)
zawierają pliki śledzone pod loom/engine/core/docs/contracts/.github poza
zamrożonymi wynikami seeding i package-lock (te pozostają przypięte w Git).
Zachowano [konfigurację początkową](local-vendored-gcc/configure.log),
[końcową](local-vendored-gcc/configure-final.log),
[pełny poprawny build](local-vendored-gcc/build-second.log) oraz
[build po zmianie interpretera](local-vendored-gcc/build-final.log).

GCC13.3, vendored SQLite, LOOM_SHARED=ON, LOOM_WERROR=ON, Python3.12.14.
Jedyną zmianą flag Debug było `-g0` dla miejsca; asercje pozostały aktywne.
[Lokalny thin archive](local-vendored-gcc/archive-storage.json) przechowywał
te same 126 obiektów w tej samej kolejności (317927312 → 55337314 B).
Po pełnym buildzie [zwolniono własne intermediates](local-vendored-gcc/released-intermediates.json),
przed generowaniem runtime fixture. Sześć końcowych plików pozostało i zostało
powtórnie sprawdzone. To opis lokalnego magazynowania, nie zmiana CMake repo.

Z repo root można odtworzyć pełny build z tym samym kodem w nowym katalogu:

```bash
cmake --preset vendored -S loom -B /tmp/w8-vendored-gcc \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DLOOM_BUILD_SERVER=ON -DLOOM_SHARED=ON \
  -DCMAKE_C_FLAGS_DEBUG=-g0 -DCMAKE_CXX_FLAGS_DEBUG=-g0 \
  -DPython3_EXECUTABLE=/path/to/python3
cmake --build /tmp/w8-vendored-gcc --parallel 2
ctest --test-dir /tmp/w8-vendored-gcc --show-only=json-v1 > /tmp/w8-manifest.json
ctest --test-dir /tmp/w8-vendored-gcc -j 4 --output-on-failure --no-tests=error \
  --test-output-size-passed 10485760 --test-output-size-failed 10485760 \
  --output-junit /tmp/w8-ctest.xml
python .github/scripts/verify_ctest.py --preset vendored \
  --policy .github/ctest-evidence-policy.json --manifest /tmp/w8-manifest.json \
  --junit /tmp/w8-ctest.xml --output /tmp/w8-executed.json
```

Interpreter musi mieć requests/cryptography i zależności z contract requirements.
Standardowe CI korzysta z Python3.11. Odtworzenie nie wymaga konwersji thin
archive, jeśli jest dostępne miejsce. Archiwalne helpery comparison/golden
sprawdzają istniejące bytes i syntetyczne DEV; nie wysyłają zapytań modeli.

## Granica dowodu

To **GCC/vendored**, nie Clang. Lokalny pełny CTest nie zamyka macierzy CI.
Clang/vendored jest zablokowany przez dwa `[this]` w app.cpp W10;
[rzeczywiste błędy zachowane](../../archive/repo-hygiene-2026-10-04/README.md).
Nowe CI uruchomi W8 po poprawce W10 i rebase. Wcześniejsze dev/ASan/JNI/web
pozostają w [oddzielnym dowodzie](../repo-hygiene-2026-10-04/RESULTS.md),
z oryginalnymi źródłami i datami.

[Nieudane lokalne konfiguracje/puste obiekty](../../archive/repo-hygiene-followup-2026-10-04/local-build/README.md)
zachowano w całości; nie stanowią wykonanych testów.
Wspólny adapter metod do grafu czeka na uzgodnienie W3/W4; nie promujemy
profilu eksperymentu do ukończonego kanonicznego rejestru. Zero płatnych wywołań.
