# W11 — zamrożony strażnik W8 do końcowej walidacji

Migawka kopiuje **niezmienione bajty** trzech plików z W8 `f64e0efcc290b255f9abc5b99516a366a3129c8b`, drzewo `45f586233a9182d44896659a9983f4da7465f016`. `receipt.json` zawiera SHA-256, rozmiary i identyfikatory blobów Git. `source-commit.txt` zachowuje surową treść obiektu commitu; `source-files.ls-tree.txt` wiąże ścieżki z blobami. Nie zmieniono CI ani aktywnych skryptów repozytorium.

| Niezmieniony plik | SHA-256 |
|---|---|
| `.github/scripts/verify_ctest.py` | `ecfb45f0883d2aeb8d61647fb90ec3c0ee161ec4b998f0a9b075dba7efa1bb35` |
| `.github/ctest-evidence-policy.json` | `1dc457125c40c0e225365e20c8839d9726d71dfa5872a8be1d890707666526a4` |
| `.github/ctest-evidence-policy.schema.json` | `29ff3cd357e02cd0fc8139cacb86293fcfab5803c9f0001eb8aec53b4f4be6e0` |

Zachowano układ `.github/scripts/` i plików sąsiednich. `--policy` wskazuje deskryptor; ścieżka schematu pozostaje wyznaczona względem skryptu, bez osobnego argumentu CLI. Strażnik korzysta ze standardowej biblioteki Pythona i z `jsonschema`. `requirements-observed.txt` zapisuje pięć zainstalowanych wersji wraz z zależnościami pośrednimi; szczegóły są w receipt. Nie kopiowano pakietów ani rozszerzeń binarnych i nie instalowano zależności. Odtworzenie offline wymaga dostępnego środowiska z tymi pakietami.

Wykonano wyłącznie kontrolę AST/syntaktyczną JSON, porównanie kopii z blobami Git, `python3 -B …/verify_ctest.py --help` oraz `/root/.local/bin/ctest --version/--help`. Wszystkie zakończyły się powodzeniem. Zachowano pełne stdout/stderr. CTest ma wersję **4.4.4** i obsługuje wymienione poniżej opcje zapisu JUnit. Nie uruchomiono builda, CTest, testów strażnika ani web. Końcowe liczby nowego przebiegu oraz zgodność dwóch wersji strażnika są **jeszcze niewykazane**.

Polityka `vendored` ma pustą listę niedostępnych testów Pythona. Zachowuje dokładnie jeden historyczny opt-in: `unit.test_catalog_scale` z **0 przypadków i 0 asercji** jest oznaczony jako `unexecuted` i nie powiększa wykonanej pokrywy. Dodatni wynik tego wpisu jest liczony normalnie. Pozostałe wpisy doctest wymagają dodatnich przypadków i asercji. Liczba przypadków pominiętych przez filtr doctest nie jest sumowana jako wykonanie. Strażnik raportuje obserwowane `executed_native_cases`, `executed_native_assertions`, `executed_python_cases`, `skipped_python_cases` oraz klasyfikację każdego wpisu; nie ma argumentów narzucających oczekiwane sumy.

Odczytany cache rzeczywistego builda ma `CMAKE_BUILD_TYPE=Debug`, `CMAKE_CXX_FLAGS_DEBUG=-g0`, `LOOM_WERROR=ON`, `LOOM_SHARED=ON`, `LOOM_USE_SYSTEM_SQLITE=OFF`, testy/CLI/server włączone. Dlatego właściwe ustawienie polityki to **`--preset vendored`**. Nie jest to twierdzenie, że build wykonano przez `cmake --preset`; argument strażnika wybiera tylko deklarowaną politykę dostępności. Polityka nie wprowadza wyjątków dla trybu Debug ani `-g0`.

## Polecenia dla właściciela końcowej bramki

Poniższe polecenia są przygotowane do wykonania przez root po ukończeniu builda. Ścieżki odzwierciedlają aktualne środowisko; nie zostały wykonane przy przygotowaniu migawki. Zachować oba statusy strażników oraz oryginalne manifest/JUnit/logi, także przy wyniku ujemnym. Użycie dużego limitu zapisu stdout nie zmienia testów, asercji ani progów produktu.

```sh
REPO_W11=/workspace/scratch/72fc60ad1cc5/chatadhd
BUILD_W11=/workspace/scratch/72fc60ad1cc5/current-build
PROOF_W11="$REPO_W11/docs/reports/data-in-code/evidence/continuation-2026-10-05/final-validation"
GUARD_W11="$PROOF_W11/validator-snapshot"

/root/.local/bin/ctest --test-dir "$BUILD_W11" --show-only=json-v1 > "$PROOF_W11/ctest-manifest.json" 2> "$PROOF_W11/ctest-manifest.stderr"

env -u PYTHONPATH -u TMPDIR /root/.local/bin/ctest --test-dir "$BUILD_W11" --output-on-failure --no-tests=error -j1 --test-output-size-passed 100000000 --test-output-size-failed 100000000 --output-junit "$PROOF_W11/ctest.xml" > "$PROOF_W11/ctest.stdout" 2> "$PROOF_W11/ctest.stderr"
CTEST_W11_STATUS=$?
printf '%s\n' "$CTEST_W11_STATUS" > "$PROOF_W11/ctest.exit"
cp "$BUILD_W11/Testing/Temporary/LastTest.log" "$PROOF_W11/LastTest.full.log"

python3 -B "$GUARD_W11/.github/scripts/verify_ctest.py" --policy "$GUARD_W11/.github/ctest-evidence-policy.json" --manifest "$PROOF_W11/ctest-manifest.json" --junit "$PROOF_W11/ctest.xml" --preset vendored --output "$PROOF_W11/coverage.json" > "$PROOF_W11/coverage.stdout" 2> "$PROOF_W11/coverage.stderr"
GUARD_W11_STATUS=$?
printf '%s\n' "$GUARD_W11_STATUS" > "$PROOF_W11/coverage.exit"

python3 -B "$REPO_W11/docs/reports/data-in-code/evidence/final-validation/verify_ctest_thread8.py" --manifest "$PROOF_W11/ctest-manifest.json" --junit "$PROOF_W11/ctest.xml" --preset vendored --output "$PROOF_W11/coverage-legacy.json" > "$PROOF_W11/coverage-legacy.stdout" 2> "$PROOF_W11/coverage-legacy.stderr"
LEGACY_W11_STATUS=$?
printf '%s\n' "$LEGACY_W11_STATUS" > "$PROOF_W11/coverage-legacy.exit"
```

Drugi strażnik jest wcześniejszą niezmienioną migawką W8, zachowaną w poprzednim `evidence/final-validation/`. Jej aktualny SHA-256 i blob Git zapisano w `receipt.json` (`existing_legacy_guard`). Oba strażniki powinny otrzymać **te same rzeczywiste** bajty manifestu i JUnit. Należy porównać ich `valid`, `manifest_entries`, `junit_entries`, `executed_entries`, `unexecuted_entries`, cztery sumy przypadków/asercji/skipów oraz obserwacje `entries`; nowych pól proweniencji nie należy przedstawiać jako różnicy w pokrywie. Nie zakładać przyszłych sum na podstawie starszego przebiegu.

Nowy wynik ma `policy_provenance` ze SHA-256 deskryptora i schematu. Prawidłowa polityka zachowuje tę proweniencję również przy błędnym manifeście. `input_sha256` jest dostępne przy parsowalnym, lecz strukturalnie błędnym manifeście; błędy odczytu/parsing JSON/XML lub typu mogą zwrócić wynik ujemny bez tego pola. Receipt końcowego przebiegu musi więc niezależnie zahashować wszystkie istniejące surowe pliki wejściowe, także ujemne. Nie wolno zastępować błędnego wejścia ani dopisywać syntetycznego stdout.

Jeżeli JUnit okaże się skrócony, zachować ujemny wynik strażnika i pełny `LastTest.full.log` tego samego przebiegu. Brak pełnego wyjścia uniemożliwia ustalenie pokrywy; migawka nie luzuje tej kontroli.

## Do wątku N

- **Do wątku 9:** po ukończeniu aktualnego builda wykonać pełny rzeczywisty CTest, oba niezmienione strażniki na identycznych wejściach i zapisać pełne statusy, liczby oraz hashe. Ten dokument nie potwierdza jeszcze końcowej bramki.
- **Do wątku 8:** migawka `f64e0ef` zachowuje politykę, schemat, opt-in katalogu i sposób liczenia przypadków; wykonano tylko kontrole statyczne/help, bez zmiany źródeł CI.
