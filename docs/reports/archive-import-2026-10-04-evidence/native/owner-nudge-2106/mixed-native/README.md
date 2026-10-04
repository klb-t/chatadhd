# Pełny przebieg po ponownym linkowaniu serwera

**115/115 wpisów CTest zaliczone, exit 0, 0 pominięć unittest**.
Pełny log podaje 290,39 s; pomiar runnera obejmujący proces CTest wynosi
290,601 s. To osobny przebieg po naprawie wygenerowanego artefaktu serwera.
[Pierwszy negatywny przebieg i naprawę](../mixed-native-first-failure/README.md)
zachowano oddzielnie i nie zmieniono ich pierwotnego receipt.

| Zakres | Wykonane i zaliczone |
| --- | --- |
| Native | 87 grup plików, 706 przypadków, 26 375 asercji |
| Python unittest | 26 grup, 1295 przypadków, 0 pominięć |
| Nowe native W5 | 6 grup, 47 przypadków, 1910 asercji |
| Zwykły suite `compat.test_archive_cost` | 19 przypadków: 11 istniejących + 8 utrwalonych regresji |

Opcjonalny benchmark `unit.test_catalog_scale` wykazał 0 przypadków.
Nie usunięto go ani nie zaliczono jako wykonanych przypadków. Wszystkie
7 nowych grup W5 są niepuste. Dwa dodatkowe wpisy smoke nie podają
mianownika unittest/doctest. `independent-aggregation.json` to oddzielne
przeliczenie ukończonego logu przez `aggregate.py`, ze sprawdzeniem wszystkich
115 indeksów i nazw względem rejestracji. Nie zmienia oryginalnego receipt.

## Źródła, artefakty i rzeczywisty zakres runnera

Źródła W5: `4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2`; baza W2:
`30ad7d37337d6641cb7714b03e9feff0e6e25d25`. Manifesty przed i po wykonaniu
mają identyczną mapę **1203 plików**, SHA
`f52e19ef60b8e19f0954c929c13e5c134a51d0df87a38720a1e7be52f16135f6`.
Runner potwierdził zgodność z commitem, stabilność źródeł i wszystkich
9 zbudowanych artefaktów. `independent-identities.json` dokumentuje dodatkowe
sprawdzenie zapisanych manifestów oraz aktualnych źródeł i binariów po
zakończeniu przebiegu. Nie kopiujemy binariów ani baz.

Core zachował SHA
`d4093c76c886d1b2b30b8bac4f9dc6eeeee5e994dce316dfc7ecc81ff56b2028`.
Ponownie zlinkowany serwer ma ELF, tryb 0755, 32 631 544 bajty i SHA
`cc8804bb4be6225b0518103b5ebaa4318f3e1ba3b67038da22f9594cc65f52c4`.
Receipt i log linkowania zachowano jako kontekst przygotowania artefaktu.

`full_gate.py` jest dokładnym wykonanym runnerem (SHA
`acd608fbf83c8d8e48565edc742c401387ef5623c29294a8be8052ecd88c29fc`).
Wykonał **rebuild już skonfigurowanego katalogu**, rejestrację i pełny
CTest. `configure.txt`, `build-first.txt.gz` oraz początkowy manifest
pochodzą z wcześniejszego przygotowania katalogu, **poza runnerem**.
Wstępny log Ninja kończy się krokiem 247/248. Końcowy rebuild tej próby
nie wymagał pracy. Log GLOB/kompilacji dwóch wykrytych plików z poprzedniej
próby pozostaje w pakiecie negatywnym; nie przypisujemy go tej próbie.

`CMakeCache.txt.gz` zachowuje rzeczywisty profil GCC 13.3.0,
Release `-O0 -DNDEBUG`, WERROR, vendored SQLite 3.47.2, OpenSSL,
shared/CLI/server/tests. `verify_native.imported.py` jest zachowaną
zależnością runnera, zgodną z podanym commitem. `tool-versions.json`
zachowuje osobne odczyty wersji narzędzi wykonane po zakończeniu testów.
Nie było płatnych wywołań
modeli. Testy serwera korzystają z lokalnych atrap (scenariusz czatu:
5 lokalnych wywołań, 0 zdalnych).

## Sprawdzenie dowodu i odtworzenie

`SHA256.json` wiąże wszystkie pliki pakietu. `compressed-inputs.json`
wiąże oryginalne i skompresowane rozmiary oraz SHA; gzip jest bezstratny.
Po `gzip -dk` przywracane są oryginalne nazwy logów z receipt.

```sh
python3 aggregate.py --log ctest-full.txt.gz \
  --registration ctest-registration.txt.gz --out /tmp/mixed-native-counts.json
```

Poniższe configure jest **przenośną receptą z profilu cache**, nie
dosłownym zapisem pierwotnego polecenia. Uruchomić z klonu z powyższymi
bajtami źródeł; katalogi build/output mają być nowe. Nie stosuje się
nakładek źródeł ani modyfikacji testów.

```sh
cmake -S loom -B /tmp/loom-w5-mixed-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release '-DCMAKE_C_FLAGS_RELEASE=-O0 -DNDEBUG' \
  '-DCMAKE_CXX_FLAGS_RELEASE=-O0 -DNDEBUG' -DLOOM_WERROR=ON \
  -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_BUILD_TESTS=ON \
  -DLOOM_BUILD_CLI=ON -DLOOM_BUILD_SERVER=ON -DLOOM_SHARED=ON
python3 docs/reports/archive-import-2026-10-04-evidence/native/owner-nudge-2106/mixed-native/full_gate.py \
  --repo "$PWD" --build-dir /tmp/loom-w5-mixed-build \
  --out-dir /tmp/loom-w5-mixed-replay \
  --code-commit 4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2 \
  --main-commit 30ad7d37337d6641cb7714b03e9feff0e6e25d25 \
  --build-jobs 2 --test-jobs 1
```

Można podać `--cmake PATH` i `--ctest PATH`, zmienić presety liczby zadań
i katalogi. Runner usuwa odziedziczone `PYTHONPATH`/`TMPDIR`, zachowując
zwykłe środowisko testów z CMake/CTest.
