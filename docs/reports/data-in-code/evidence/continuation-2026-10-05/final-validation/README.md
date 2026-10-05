# W11 — rzeczywista końcowa bramka 2026-10-05

Źródła produktu/testów/danych: commit
`2ac630a72e8ddc5ad126888f7e083a5b30235db8`, drzewo
`409c056b046a3d563c9093f766d9393527fde5d8`, baza main
`e4109df7e4af22b461def5f7d62e268d9b9a8825`.
Po zapisaniu tego kodu zmieniają się jedynie dokumenty i dowody.
`tested-source-snapshot.json` obejmuje 735 plików; `derive_receipt.py` ponownie
porównuje ich rzeczywiste bajty. To snapshot po buildzie, podczas CTest,
z zamrożonymi przed startem plikami R42, nie rzekomy trace kompilatora.

| Wykonana bramka | Wynik |
|---|---|
| Pełny native build, następnie regeneracja globu testów | exit 0 / exit 0 |
| CTest na kompletnym właściwym drzewie | 127/127, exit 0, 885,17 s |
| Dwa niezmienione strażniki W8, te same manifest/JUnit | exit 0 / exit 0, identyczne obserwacje wszystkich wpisów |
| Rzeczywiście wykonane native przypadki / asercje | 783 / 26 985 |
| Rzeczywiście wykonane Python przypadki / skips | 1371 / 0 |
| Jedyny niewykonany historyczny opt-in | `unit.test_catalog_scale`, 0/0; nie powiększa wykonania |
| Offline npm ci / tsc + Vite | exit 0 / exit 0, 85 modułów |
| Runtime generator / usage generator `--check` | exit 0 / exit 0, 22 domeny runtime |
| Whole-core CLI default parity | 19 wspólnych zgodnych, 3 nowe; help/version identyczne; zero nowych plików |

`ctest.xml` jest pełnym **oryginalnym** JUnit tego przebiegu. Nie było negatywu
truncation, restoracji wyjścia ani mieszania różnych przebiegów. `LastTest.full.log`
skopiowano po zakończeniu tego samego CTest. Pełne `.stdout/.stderr` i `.exit`
zachowują rzeczywiste wyjścia i statusy; `.exit` zapisano z obserwowanych zakończeń
sesji shell, a nie z domniemania na podstawie fragmentu logu. `build.stdout`
i `build-glob.stdout` są pełnymi połączonymi strumieniami zapisanymi podczas
kompilacji. `built-binaries.json` zachowuje hash/rozmiar/mode czterech artefaktów;
samych ELF nie dodano do repo.

Cache: GCC 13, Ninja, Debug `-g0`, WERROR ON, vendored SQLite, shared/CLI/server/tests
ON, OpenSSL AUTO. Polityka strażnika `vendored` opisuje dostępność bibliotek,
nie deklaruje wywołania `cmake --preset`. Wrapper `RuntimeProfile` ma nowy układ
C++; te bramki używają przebudowanego całego rdzenia i zgodnych konsumentów.

## Faktycznie wykonane polecenia

Pełny build oraz późniejsze ponowne sprawdzenie globu:

```sh
/root/.local/bin/cmake --build /workspace/scratch/72fc60ad1cc5/current-build -j3
```

CTest (manifest zapisany wcześniej przez `--show-only=json-v1`):

```sh
REPO_W11=/workspace/scratch/72fc60ad1cc5/chatadhd
PROOF_W11="$REPO_W11/docs/reports/data-in-code/evidence/continuation-2026-10-05/final-validation"
BUILD_W11=/workspace/scratch/72fc60ad1cc5/current-build
env -u PYTHONPATH -u TMPDIR /root/.local/bin/ctest --test-dir "$BUILD_W11" \
  --output-on-failure --no-tests=error -j1 \
  --test-output-size-passed 100000000 --test-output-size-failed 100000000 \
  --output-junit "$PROOF_W11/ctest.xml"
```

Duży limit dotyczy zachowania wyjścia dowodowego, nie zmienia testów ani progów.
Oba strażniki na tych samych wejściach:

```sh
GUARD_W11="$PROOF_W11/validator-snapshot"
python3 -B "$GUARD_W11/.github/scripts/verify_ctest.py" \
  --policy "$GUARD_W11/.github/ctest-evidence-policy.json" \
  --manifest "$PROOF_W11/ctest-manifest.json" --junit "$PROOF_W11/ctest.xml" \
  --preset vendored --output "$PROOF_W11/coverage.json"
python3 -B "$REPO_W11/docs/reports/data-in-code/evidence/final-validation/verify_ctest_thread8.py" \
  --manifest "$PROOF_W11/ctest-manifest.json" --junit "$PROOF_W11/ctest.xml" \
  --preset vendored --output "$PROOF_W11/coverage-legacy.json"
python3 -B "$REPO_W11/loom/src/model/gen_runtime_profiles.py" --check
python3 -B "$REPO_W11/loom/src/policy/gen_usage_policy.py" --check
python3 -B "$PROOF_W11/derive_receipt.py" --repo-root "$REPO_W11" --proof "$PROOF_W11"
```

Web wykonano w `loom/web`: `npm ci --offline`, następnie `npm run build`.
Dokładne wersje/dependencies nowych strażników, bloby, wejścia i polityka znajdują
się w niezmienionej migawce `validator-snapshot/`. Jej README zachowuje stan
przygotowania, przed tym rzeczywistym końcowym przebiegiem; ten receipt zapisuje
aktualne wykonanie. Nie modyfikowano aktywnego CI ani polityki W8.

## Odtworzenie

W czystym checkout tego kodu z dostępnymi offline zależnościami CMake/Ninja/GCC,
Python/jsonschema oraz npm cache można skonfigurować nowy build:

```sh
cmake -S loom -B /tmp/w11-native-replay -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_DEBUG=-g0 \
  -DLOOM_WERROR=ON -DLOOM_SHARED=ON -DLOOM_USE_SYSTEM_SQLITE=OFF \
  -DLOOM_BUILD_CLI=ON -DLOOM_BUILD_SERVER=ON -DLOOM_BUILD_TESTS=ON
cmake --build /tmp/w11-native-replay -j3
```

Nowe testy i logi należy zapisać w osobnym katalogu, zachowując ten pierwszy
przebieg. `derive_receipt.py` tylko przelicza istniejące dowody; nie uruchamia
testów i nie tworzy fikcyjnych strumieni. Źródłowy hash commita może zmienić się
przy rebase; dokładne hashe 735 plików oraz publikowane drzewa pozostają punktami
porównania. Zależności offline nie są kopiowane ani zastępowane atrapami buildu.

## Do wątku N

- **9:** jest to dodatnia bramka gałęzi W11 na wskazanej bazie, nie odbiór ani
  zapis na main. Wykonaj własne aktualne bramki po wybraniu przyrostu.
- **8:** oba zamrożone strażniki mają identyczne wejścia/obserwacje. Zachowaj
  sole opt-in, raw logs i brak zaliczenia 0/0 do wykonanej pokrywy.
- **1–6/10/12:** pozytywna bramka nie aktywuje trzech UNWIRED domen importu,
  W2 config bridge ani produkcyjnych warstw/śladów u cudzych konsumentów.
- **11/9:** R42 mechanizm ma pozytywne fixtures, a rzeczywisty audit produktu
  pozostaje osobnym pełnym wynikiem negatywnym. Nie używaj tego receipt do
  pominięcia indywidualnej klasyfikacji literałów lub 85 BLOCKED plików.
