# Pierwszy pełny przebieg: zachowany wynik negatywny

Pełny CTest wykonał **115/115 wpisów: 113 zaliczone, 2 niezaliczone**,
exit 8, czas CTest 393,26 s. `server.smoke` i `server.chat_active_task`
otrzymały `PermissionError` przy uruchamianiu wygenerowanego
`server/loom-server`; obserwowany tryb pliku wynosił 0644.
`receipt.json` jest kopią bajt w bajt pierwotnego potwierdzenia. Nie
uzupełniono go wynikami obliczonymi później. Pełny log zachowano bezstratnie
w `ctest-full.txt.gz`.

`independent-aggregation.json` pochodzi z **osobnego przeliczenia po
nieudanym przebiegu** przez `aggregate.py`. Parser sprawdza wszystkie
numery i nazwy ukończonych wpisów względem rejestracji; liczy tylko wiersze
z prefiksem CTest, aby nie podwoić powtórzonych komunikatów błędu.

| Zakres | Rzeczywiście zaraportowany mianownik |
| --- | --- |
| Native | 87 grup plików, 706/706 przypadków, 26 375/26 375 asercji |
| Opcjonalny benchmark | `unit.test_catalog_scale`: 0 przypadków; zachowany i jawnie wykazany |
| Python unittest | 26 grup, 1295 prób przypadków, 0 pominięć |
| Python w zaliczonych grupach | 1294 przypadki; pozostały przypadek `server.chat_active_task` zakończył się błędem uruchomienia procesu |
| Nowe grupy W5 | Wszystkie 7 zaliczone i niepuste; w tym `compat.test_archive_cost`: 19 przypadków |

Dokładne liczniki nowych grup znajdują się w osobnej agregacji. Dwa
pozostałe wpisy CTest, `cli.smoke` i `server.smoke`, nie raportują mianownika
unittest/doctest. Nie traktujemy 1295 prób Pythona jako 1295 zaliczonych testów.

## Pochodzenie i granice potwierdzenia

Źródła: W5 `4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2`, baza W2
`30ad7d37337d6641cb7714b03e9feff0e6e25d25`. Początkowy manifest zawiera
1203 pliki; SHA mapy plików:
`f52e19ef60b8e19f0954c929c13e5c134a51d0df87a38720a1e7be52f16135f6`.
`full_gate.py` jest dokładnym wykonanym runnerem. `verify_native.imported.py`
zachowuje jego zależność z repozytorium; jej zgodność z podanym commitem
sprawdzono przy tworzeniu tego pakietu.

`configure.txt`, `build-first.txt.gz` i `preconfigure-source-before.json.gz`
pochodzą z wcześniejszego przygotowania nowego katalogu budowania,
**poza runnerem**. Wstępny log Ninja kończy się krokiem 247/248; nie
przypisujemy runnerowi tych czynności. Sam runner wykonał ponowny build,
rejestrację i pełny CTest. `build-final.txt` zachowuje rzeczywiste ponowne
wykrycie dwóch plików testów przez GLOB, kompilację i linkowanie.
`CMakeCache.txt.gz` odpowiada hashowi w pierwotnym receipt. Runner przerwał
się po exit 8: nie ma końcowego manifestu ani twierdzenia o stabilności
wszystkich binariów podczas pierwszego przebiegu. Nie kopiujemy binariów
ani baz. Testy używają lokalnych atrap; nie było płatnych wywołań modeli.

## Naprawa wygenerowanego artefaktu, osobny wynik

1. `server-mode-correction.json`: zmiana 0644 → 0755 zachowała SHA
   `e6cd27065ec3b7f3832ae1a2139580444b4e7cb219323ae1e0f30228a73524d4`.
   `server-focused-after-mode.txt` nadal wykazał 2/2 błędy,
   tym razem `Exec format error`.
2. `invalid-artifact-inspection.json` dokumentuje pierwsze 64 bajty
   nagłówka jako zerowe. **Nie cały plik był zerowy**; receipt przebudowy
   ma `all_zero: false`. Przyczyna powstania nieprawidłowego artefaktu nie
   została ustalona.
3. `server-artifact-rebuild-receipt.json` i log zachowują ponowne
   linkowanie z tych samych źródeł, exit 0. Archiwum core zachowało SHA
   `d4093c76c886d1b2b30b8bac4f9dc6eeeee5e994dce316dfc7ecc81ff56b2028`.
   Nowy serwer ma nagłówek ELF, tryb 0755, 32 631 544 bajty i SHA
   `cc8804bb4be6225b0518103b5ebaa4318f3e1ba3b67038da22f9594cc65f52c4`.
4. `server-focused-after-rebuild.txt`: **2/2 zaliczone, 3,53 s**.
   To sprawdzenie obu grup serwera, nie wynik ponownego pełnego CTest.
   Scenariusz czatu raportuje 5 lokalnych wywołań atrapy i 0 zdalnych.

## Odtworzenie i kontrola zapisanych danych

`SHA256.json` wiąże każdy plik pakietu; `compressed-inputs.json` podaje
SHA i rozmiary zarówno oryginalnych, jak i skompresowanych bajtów. Oryginalne
nazwy logów z receipt otrzymuje się po `gzip -dk`.

```sh
python3 aggregate.py --log ctest-full.txt.gz \
  --registration ctest-registration.txt.gz --out /tmp/first-failure-counts.json
```

Poniższa recepta jest przenośnym odtworzeniem profilu z cache, **nie zapisem
dosłownego pierwotnego polecenia configure**. Uruchomić z klonu zawierającego
powyższe bajty źródeł; katalogi build/output powinny być nowe. Nie odtwarza
się uszkodzenia pliku serwera ani nie wymusza oczekiwanego wyniku negatywnego.

```sh
cmake -S loom -B /tmp/loom-w5-mixed-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release '-DCMAKE_C_FLAGS_RELEASE=-O0 -DNDEBUG' \
  '-DCMAKE_CXX_FLAGS_RELEASE=-O0 -DNDEBUG' -DLOOM_WERROR=ON \
  -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_BUILD_TESTS=ON \
  -DLOOM_BUILD_CLI=ON -DLOOM_BUILD_SERVER=ON -DLOOM_SHARED=ON
python3 docs/reports/archive-import-2026-10-04-evidence/native/owner-nudge-2106/mixed-native-first-failure/full_gate.py \
  --repo "$PWD" --build-dir /tmp/loom-w5-mixed-build \
  --out-dir /tmp/loom-w5-mixed-replay \
  --code-commit 4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2 \
  --main-commit 30ad7d37337d6641cb7714b03e9feff0e6e25d25 \
  --build-jobs 2 --test-jobs 1
```

Runner obsługuje opcjonalne `--cmake PATH` i `--ctest PATH`. Presety liczby
zadań można zmienić. Nie stosuje nakładek źródeł i usuwa odziedziczone
`PYTHONPATH`/`TMPDIR`; pozostawia normalne ustawienia środowiska z CTest.
