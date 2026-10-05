# Wątek 11: presety narzędzi plikowych i logowania

Data: 2026-10-04. Zakres: DIC-0062–0064, `loom/src/util/fs.cpp`, `loom/src/util/log.cpp`, ich nagłówki i testy. DIC-0061 opisuje osobno [implementation-regex.md](implementation-regex.md). Inwentarz zamknięto przed edycją kodu w `bab7be3`. Nie wykonywano płatnych wywołań.

## Migracja i wartości domyślne

`loom/data/runtime/util.pack`, domena `util`, zawiera 17 ustawień/presetów w dwóch grupach:

| Grupa | Wartości domyślne |
|---|---|
| `temp` | `prefix="loom_"`, `base_root=""` (katalog tymczasowy systemu), `fallback_root="/tmp"`, `attempts=16`, `id_chars=12` |
| `log` | `ring_capacity=500`, `recent_lines=500`, `default_level=20`, `stderr_enabled=true`, nazwy środowiska `LOOM_LOG_LEVEL`/`LOOM_LOG_STDERR`, wartość wyłączająca stderr `"0"`, mapa etykiet 10→DEBUG/20→INFO/30→WARN/40→ERROR, etykieta domyślna INFO, mapa aliasów debug/warning/warn/error, szerokość etykiety 5, szablon `{{time}} [{{level}}] {{logger}}: {{message}}` |

Liczba prób, rozmiary bufora, szerokości i długości identyfikatora są danymi. Maksimum w schemacie to granica reprezentacji licznika, a nie limit produktu. `attempts=0` nie tworzy katalogu i zwraca jawny błąd wyczerpania skonfigurowanych prób. `temp.base_root` ustawione jawnie wyłącza odkrywanie katalogu systemowego; fallback jest używany tylko przy błędzie odkrywania.

## API i nakładka

Standardowy plik użytkownika: `<data_dir>/profiles/util.pack`, koperta `loom.runtime_profile_overlay/1`, domena `util`, pola `overrides` i opcjonalne `patch` RFC 6902. Wywołujący ładuje `RuntimeProfile::load("util", data_dir)` i przekazuje wynik do API. Konsumenci sprawdzają dokładne wartości przez `builtin.with_values(profile.values())`, więc celowe usunięcie klucza słownika nie przywraca wpisu z presetu.

| API | Znaczenie |
|---|---|
| `TempDir::create(profile, optional_prefix)` | Per-call tworzenie katalogu z root/próbami/długością ID z profilu, błąd jako `Result<TempDir>`. Jawny prefix wygrywa z presetem. |
| `Record::formatted_checked(profile)` | Inertny szablon jednego rekordu; zmienne time/level/logger/message. Nie wykonuje ponownie tekstu podstawionego do szablonu. |
| `log::level_name(level, profile)` | Etykieta przez mapę danych i domyślny wpis. |
| `log::write_checked(level, logger, message, profile)` | Jednorazowy renderer publikacji; błąd szablonu zachowuje poprzedni bufor i nie publikuje pozorowanej pustej linii. |
| `log::preset_level(profile)`, `preset_stderr(profile)` | Rozwiązują preset i dane środowiska, a klient stosuje wynik przez istniejące jawne settery. |
| `log::recent_checked(profile, optional_count)` | Liczba odczytywanych linii z profilu albo argumentu. |
| `log::set_ring_capacity_checked(profile)` | Stosuje tylko jawne ustawienie pojemności do istniejącego bufora; profilowe zero wyłącza przechowywanie. |

Nie wprowadzono mutowalnego globalnego profilu domeny. Domyślny logger korzysta z niezmiennego wbudowanego presetu; poziom, stderr, bufor i sinki zachowują istniejące jawne ustawienia. `set_ring_capacity(size_t)` zachowuje historyczne zero→jedna linia. Nowe checked API umożliwia zero jako wyłączenie przechowywania bez zmiany starej semantyki.

Zachowano eksportowany konstruktor `TempDir(string_view prefix="loom_")` i układ klasy, ponieważ istniejące binarne/źródłowe wywołania mają prefix w swoim kontrakcie. To ostatni kompatybilnościowy argument źródłowy; mutable preset prefixu jest używany przez `TempDir::create(profile)`. Zachowano też eksportowany symbol `log::recent(size_t)` jako wrapper; nowy wariant bez argumentu korzysta z presetu danych. Dotychczasowe konstruktory TempDir nadal dają `valid()==false` przy niepowodzeniu, zaś nowe API pokazuje oryginalny problem walidacji albo błąd filesystemu.

Przykład:

```json
{
  "schema": "loom.runtime_profile_overlay/1",
  "domain": "util",
  "overrides": {
    "temp": {"base_root": "/wybrany/katalog", "attempts": 32},
    "log": {"record_template": "{{level}}|{{logger}}|{{message}}", "ring_capacity": 0}
  }
}
```

## Liczby i dowody

Zewnętrzna sonda `loom/tests/compat/profile_util_parity.cc` korzysta tylko z wcześniej istniejącego publicznego API. `profile_util_parity.py` uruchamia oba binaria w sześciu wariantach środowiska, zapisuje rzeczywiste stdout/stderr obu uruchomień i dopiero potem porównuje wyniki. Przypadki: domyślny poziom, DEBUG, warn, error, nierozpoznany alias, nieistniejący systemowy TMPDIR z fallbackiem.

| Pomiar | Przed | Po |
|---|---:|---:|
| Warianty środowiska | 6 | 6 |
| Rekordy z różnymi poziomami w każdym wariancie | 5 | 5 |
| Domyślny bufor po 503 zapisach | 500 linii | 500 linii |
| Jawne warianty odczytu bufora | 4 | 4 |
| Po `set_ring_capacity(0)` | 1 linia | 1 linia |
| Zbiorczy pełny JSON | 13295 B | 13295 B |
| SHA-256 | `a6fd124a827744adc68e08fa771cc9fc2b4548128713c9de8ef12aa58b7c1dbf` | ten sam |

Sonda sprawdza też sinki i wyjątek sinka, tłumienie debug przy Info, etykietę nieznanego poziomu, Unicode i tekst przypominający szablon, rozmiar/nazwę katalogu, faktyczny root, sprzątanie, przenoszenie i historycznie nieudaną alokację. Losowe identyfikatory i zegar sprowadzono do deterministycznych podsumowań; nie pomija to treści rekordów ani obserwowanej semantyki.

Pełne dane: [before.json](evidence/util/before.json), [after.json](evidence/util/after.json); osobne surowe wyniki są w tym samym katalogu. Testy: [focused-tests.log](evidence/util/focused-tests.log), **15/15 przypadków, 133/133 asercji, 0 pominiętych**. Dziesięć dotychczasowych przypadków util zachowano. Pięć nowych sprawdza własny root/prefix/ID, błędy tworzenia i schematu, inertny renderer, jawny błąd publikacji oraz usuwanie słownika/presety środowiska/wyłączony bufor.

Źródła i nowe testy przeszły kontrolę składni z ostrzeżeniami projektu i `-Werror`, a zmiany `git diff --check`.

To **wtórny dowód zgodności zakresu**, nie pełny `ctest`. Użyto istniejącego archiwum `9d15d2d` i nagłówków zamrożonego `161cc22`; `git diff 9d15d2d 161cc22 -- loom/src/util loom/include/loom/log.h loom/include/loom/util/fs.h` był pusty. Wyjście po zmianie pochodzi z nowych produkcyjnych obiektów fs/log/runtime_profile linkowanych przed niezmienionym archiwum. Nie konstruowano starego Runtime ani klas o zmienionym układzie ABI. Świeży build całego `161cc22` jest nadal wymagany przez integratora.

## Odtworzenie

Z repo, po regeneracji wbudowanych danych przez integratora:

```sh
TASK_TMP=/workspace/scratch/72fc60ad1cc5
OLD_BUILD=/workspace/scratch/98e6ad903811/baseline/loom/build/dev
BASELINE=/workspace/scratch/72fc60ad1cc5/chatadhd-baseline
git diff --exit-code 9d15d2d 161cc22 -- loom/src/util loom/include/loom/log.h loom/include/loom/util/fs.h
c++ -std=c++20 -I"$BASELINE/loom/include" -Iloom/third_party/nlohmann \
  loom/tests/compat/profile_util_parity.cc \
  "$OLD_BUILD/libloom_core.a" "$OLD_BUILD/libloom_sqlite3_amalgamation.a" "$OLD_BUILD/libloom_miniz.a" \
  -pthread -ldl -lm -lssl -lcrypto -o "$TASK_TMP/util-parity-before"
for unit in fs log; do
  c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann -Iloom/src \
    -c "loom/src/util/$unit.cpp" -o "$TASK_TMP/util-$unit-profile.o"
done
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann -Iloom/src \
  -c loom/src/model/runtime_profile.cpp -o "$TASK_TMP/runtime-profile-util.o"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann \
  loom/tests/compat/profile_util_parity.cc \
  "$TASK_TMP/util-fs-profile.o" "$TASK_TMP/util-log-profile.o" "$TASK_TMP/runtime-profile-util.o" \
  "$OLD_BUILD/libloom_core.a" "$OLD_BUILD/libloom_sqlite3_amalgamation.a" "$OLD_BUILD/libloom_miniz.a" \
  -pthread -ldl -lm -lssl -lcrypto -o "$TASK_TMP/util-parity-after"
python3 loom/tests/compat/profile_util_parity.py --before "$TASK_TMP/util-parity-before" \
  --after "$TASK_TMP/util-parity-after" --out "$TASK_TMP/util-parity-evidence"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann -Iloom/third_party/doctest -Iloom/tests \
  loom/tests/main.cpp loom/tests/test_util.cpp loom/tests/test_util_profiles.cpp \
  "$TASK_TMP/util-fs-profile.o" "$TASK_TMP/util-log-profile.o" "$TASK_TMP/runtime-profile-util.o" \
  "$OLD_BUILD/libloom_core.a" "$OLD_BUILD/libloom_sqlite3_amalgamation.a" "$OLD_BUILD/libloom_miniz.a" \
  -pthread -ldl -lm -lssl -lcrypto -o "$TASK_TMP/util-focused-tests"
"$TASK_TMP/util-focused-tests"
```

## Pozostałe literały i praca niewykonana

Nie zmieniano kontraktów POSIX, trybów plików i atomowego rename, sufiksu `.tmp`, protokołu `$HOME`, wartości enum severity ani sztywnego formatu canonicalnego zegara `HH:MM:SS`. Są operacjami/protokołem, a nie ograniczeniami zużycia. Time formatter pozostaje osobną operacją; renderer może pominąć/przestawić time. Pozostał kompatybilnościowy domyślny argument prefixu TempDir opisany wyżej. Nie okablowano automatycznie nakładek z composition root, Runtime/C API ani ustawień ×10; te miejsca nie należą do tej edycji. Nie deklarujemy pełnego `ctest` ani buildu web.

## Do wątku 2

Strażnik ×10 powinien oszacować liczbę prób/rozmiar identyfikatora, pojemność bufora i koszt formatu przed operacją. Używać jawnych wartości profilu oraz istniejących setterów; `set_ring_capacity_checked` pozwala ustawić zero bez dawnego przekształcenia 0→1. Nie dodawać progów produktu do util. Regex VM ma osobny profil i ślad wyczerpania w raporcie regex.

## Do wątku 10

Ustawienia korzystają z domeny `util` i jej `value_schema`/hashu. Renderer jest szablonem tekstowym, nie kodem. Pokazywać błędy brakujących zmiennych; surowe Record zostaje zachowane. Zero bufora w nowym API oznacza wyłączone przechowywanie. Do usuwania wpisów mapy nazw używać `patch`, nie domyślnego scalania `overrides`.

## Do wątku 9

W composition root ładować `<root>/profiles/util.pack` i przekazywać do TempDir::create oraz scoped renderowania. Presety poziomu/stderr zastosować jawnie przez istniejące settery, pojemność przez checked API; nie instalować globalnego profilu. Przebudować konsumentów nowych nagłówków, wykonać świeżą pełną sondę `161cc22`, pełny `ctest` i build web. Zachować pełne wtórne dowody; nie zastępować bramki integracyjnej wynikiem 15 testów util.
