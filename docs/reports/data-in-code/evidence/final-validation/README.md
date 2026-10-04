# W11 — końcowa weryfikacja na rebazowanej gałęzi

Kod i dane: `49e5e65cb734e60d5bcaff9bb51a1ca7ffcb6ee2`, drzewo `fba5007dee09face5e119332e8ca8ac9cd54cccb`, baza main `7282437b1c88933977f64b3468b9f42f7b400494`. Lokalne SHA po rebase jest zapisane w receipt; publikacja zachowała dokładne drzewa. Późniejszy commit raportów nie zmienia wykonawczych wejść.

`rebase-input-parity.json` potwierdza identyczną zawartość 1190 śledzonych wejść Loom przed i po dokumentacyjnym rebase. Przywrócono tylko timestamp identycznych plików, aby zachować poprawny cache kompilacji. `build.log` potwierdza aktualny build bez zaległych prac. Pełny końcowy link poprzedził rebase; jego biblioteki i wszystkie produkty mają hashe w `receipt.json` i są identyczne z bibliotekami sześciu sond before/after.

Build: GCC, Debug z `-g0`, Werror, vendored SQLite, shared library, CLI, server i testy. `build-options.json` zapisuje konkretne opcje. `-g0` ogranicza symbole debug; nie zmienia optymalizacji ani progów testów. Web: `npm run build`, pełny log w `web-build.log`, bez zmian źródeł web.

```sh
cmake -S loom -B build-w11 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_DEBUG=-g0 -DLOOM_WERROR=ON -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_SHARED=ON -DLOOM_BUILD_TESTS=ON -DLOOM_BUILD_CLI=ON -DLOOM_BUILD_SERVER=ON
cmake --build build-w11 -j2
python3 loom/src/model/gen_runtime_profiles.py --check
ctest --test-dir build-w11 --show-only=json-v1 > manifest.json
env -u PYTHONPATH -u TMPDIR ctest --test-dir build-w11 --output-on-failure --no-tests=error -j1 --test-output-size-passed 100000000 --test-output-size-failed 100000000 --output-junit ctest.xml
python3 verify_ctest_thread8.py --manifest manifest.json --junit ctest.xml --preset vendored --output coverage.json
```

W rzeczywistym przebiegu CTest wykonano pełny zestaw bez zmiany domyślnych limitów **zapisu stdout**: 122/122 zielonych wpisów, exit0, 533,26s. `ctest.log` i `ctest.xml` są jego oryginalnymi dodatnimi plikami. Wbudowany limit1024B skrócił 16 wyjść JUnit, co poprawnie odrzucił niezmieniony strażnik W8 (`coverage.json/log`). Zachowany pełny `LastTest.full.log` tego samego przebiegu pozwala odtworzyć ich stdout: nazwy, kolejność, komendy, katalogi, statusy i czasy muszą zgadzać się z manifestem/JUnit; 106 strumieni jest identycznych, 16 stanowi dokładny prefiks1024B z jawnym markerem CTest. `restore_ctest_output.py` i jego receipt opisują to sprawdzenie. `ctest-full-output.xml` jest **pochodnym dowodem tego samego uruchomienia**, nie nowym uruchomieniem ani zastępczym testem. Oryginały pozostają dostępne. Polecenie powyżej używa większego limitu zapisu wyjścia, aby kolejne odtworzenie nie wymagało tej operacji; nie zmienia asercji, limitów czasu ani dopuszczalnych skipów.

`verify_ctest_thread8.py` jest niezmienioną migawką `.github/scripts/verify_ctest.py` wątku8/2544cf3. `guard-provenance.json` podaje hash. Jedyny domyślny niewykonany wpis to historyczny opt-in `unit.test_catalog_scale`; nie przedstawiamy go jako wykonania przypadków. Pełne rzeczywiste liczby są w `coverage-full.json`.

Pierwszy CTest z tej kompilacji nie uruchomił 95 wpisów native przez mode0644 na ELF. `permissions-negative/` zachowuje cały log/XML, hash i opis korekty. Przywrócono bit wykonania wyłącznie tej binarce i zweryfikowano niezmieniony SHA, po czym ponowiono cały zestaw. Bez usuwania testów, zmiany progów lub kodu w odpowiedzi na ten błąd.

Sondy sześciu całych bibliotek są w `../fullcore161/`; pełne archiwum i pomiary w `../archive/`. Starsze dowody zakresu nie są sumowane jako dodatkowe przypadki.
