# Wątek 11 — profil wykonania regex

Data: 2026-10-04. Autor: agent `inventory_native_other/regex_profile`.
Zakres: DIC-0061; wyłącznie interpreter `loom/src/re`, jego nagłówek,
profil `loom/data/runtime/re.pack` i testy. Bez płatnych wywołań.

## Co zmieniono

Domyślny budżet 10 000 000 instrukcji VM przeniesiono z inicjalizatora
`Regex::Program` do kanonicznego profilu `re`. Wartość jest presetem.
Schemat dopuszcza cały zakres `uint64_t`; maksimum wynika wyłącznie
z reprezentacji licznika. Zero zachowuje wcześniejsze znaczenie:
wyczerpanie budżetu przed pierwszą instrukcją.

| API C++ | Zachowanie |
| --- | --- |
| `Regex::compile(pattern, flags)` | Dotychczasowe API, używa wbudowanego profilu `re`. |
| `Regex::compile(pattern, flags, profile)` | Profil danego wywołania kompilacji; błędny profil zwraca `Result` z błędem. |
| `regex.with_profile(profile)` | Niezależna kopia skompilowanego programu. Można zachować ją dla kontekstu albo użyć do jednego wywołania. Źródło i pozostałe kopie nie zmieniają budżetu. |
| `regex.profile_inspection()` | Efektywne wartości, schemat i hash; uwzględnia późniejsze `set_step_limit`. |
| `regex.step_limit()` | Efektywny budżet VM. |
| `regex.last_search_hit_limit()` | Dotychczasowy jawny ślad wyczerpania, osobny od braku dopasowania. |

Profil obcej domeny jest odrzucany. Wartości są dodatkowo sprawdzane
wobec schematu konsumenta, także gdy wywołujący dostarczył własny,
zbyt łagodny schemat. Brak wymaganej wartości nie powoduje wyjątku
ani ukrytego uzupełnienia. Reguły składni i semantyka regex pozostają
operacjami interpretera.

Nakładka użytkownika ma standardowy kontrakt
`<data_dir>/profiles/re.pack`, `schema: loom.runtime_profile_overlay/1`,
`domain: re`, `overrides: {step_limit: ...}`. Wywołujący przekazuje
wynik `RuntimeProfile::load("re", data_dir, overrides)` do nowych API.
Opcjonalne `patch` w nakładce używa RFC 6902 po scaleniu `overrides`;
usunięcie wymaganego `step_limit` zwraca błąd. Walidacja konsumenta
przez `with_values` sprawdza dokładne wartości i nie odtwarza usuniętych pól.
Nie ma globalnego stanu domeny. Dotychczasowe kopiowanie `Regex`
oraz współdzielenie ręcznego `set_step_limit` przez takie kopie zachowano
dla zgodności; `with_profile` zapewnia niezależny program.

## Liczby przed i po

Publiczny, niezmieniany pomiędzy kompilacjami probe:
`loom/tests/compat/profile_regex_parity.cc`. Dane są syntetyczne i offline.

| Sprawdzenie | Przed | Po |
| --- | --- | --- |
| Wartość domyślna budżetu | 10 000 000 | 10 000 000 |
| Wiersze probe | 523 | 523 |
| Przypadki ręcznie ustawianych budżetów | 4 | 4 |
| Rozmiar JSON | 254 166 bajtów | 254 166 bajtów |
| SHA-256 JSON | `d54343319fe07946c4e2d023a9bb09ccc8461bcd03aa6b92548c4351b6163381` | `d54343319fe07946c4e2d023a9bb09ccc8461bcd03aa6b92548c4351b6163381` |

`cmp` potwierdza identyczność bajtową. Probe obejmuje 13 poprawnych wzorców,
10 tekstów (puste, Unicode, emoji, wielowierszowe, długi tekst), trzy
pozycje startowe, `search/match/fullmatch`, grupy, `finditer/findall/sub`,
trzy błędy parsera i ręczne budżety 0/1/10/1000 wraz ze współdzieleniem
przez dotychczasowe kopie.

Pełne wyniki: [before.json](evidence/regex/before.json),
[after.json](evidence/regex/after.json).
Testy skupione: **18/18 przypadków, 125/125 asercji, 0 pominiętych**;
[focused-tests.log](evidence/regex/focused-tests.log). Obejmują dotychczasowe
przypadki oraz preset, zero i maksimum `uint64_t`, niezależność profilu,
hash efektywnego budżetu, zachowane flagi/lookahead i odrzucenie obcego,
niepełnego lub zbyt łagodnego profilu. Przeszły również oba sprawdzenia
składni z flagami ostrzeżeń projektu i `-Werror`, walidacja JSON oraz
`git diff --check`.

## Pochodzenie i odtworzenie pomiaru

Pierwotny pomiar był **pomiarem zgodności ograniczonym do modułu**,
nie dowodem pełnego testu zintegrowanego. Wykorzystano istniejącą
bibliotekę sprzed zmian z commitu
`9d15d2dd0733274356f768816e207e26e13845e4`, ponieważ świeża kompilacja
zamrożonej bazy `161cc22dfb84fe863389d6b90323bd44516a68dc` trwała równolegle.
Poniższe sprawdzenie zwróciło pusty diff: implementacje regex i jego
zależności Unicode/UTF-8 są w tych bazach identyczne.

```sh
git diff --stat 9d15d2d 161cc22 -- \
  loom/src/re loom/include/loom/re/regex.h \
  loom/src/util/unicode.cpp loom/src/util/utf8.cpp
```

Probe „po” podlinkowano z nowymi obiektami produkcyjnymi `regex.cpp`
i `runtime_profile.cpp`, następnie z niezmienioną biblioteką rdzenia.
Świeżą równoważną bibliotekę „przed” można przygotować tak:

```sh
task_root=$(mktemp -d)
git worktree add --detach "$task_root/before" 9d15d2d
cmake -S "$task_root/before/loom" -B "$task_root/before-build" \
  -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_WITH_OPENSSL=ON
cmake --build "$task_root/before-build" --target loom_core -j 1
baseline_headers="$task_root/before/loom/include"
baseline_build="$task_root/before-build"
```

W faktycznym pomiarze `baseline_headers` wskazywało na zamrożony checkout
`/workspace/scratch/72fc60ad1cc5/chatadhd-baseline/loom/include`, a
`baseline_build` na istniejący build
`/workspace/scratch/98e6ad903811/baseline/loom/build/dev`.
Z katalogu głównego bieżącego repo, po regeneracji wbudowanych profili:

```sh
python3 loom/src/model/gen_runtime_profiles.py
c++ -std=c++20 -I"$baseline_headers" -Iloom/third_party/nlohmann \
  loom/tests/compat/profile_regex_parity.cc \
  "$baseline_build/libloom_core.a" \
  "$baseline_build/libloom_sqlite3_amalgamation.a" \
  "$baseline_build/libloom_miniz.a" -pthread -ldl -lm -lssl -lcrypto \
  -o "$task_root/regex-before"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann -Iloom/src \
  -c loom/src/re/regex.cpp -o "$task_root/regex.o"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann -Iloom/src \
  -c loom/src/model/runtime_profile.cpp -o "$task_root/runtime-profile.o"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann \
  loom/tests/compat/profile_regex_parity.cc \
  "$task_root/regex.o" "$task_root/runtime-profile.o" \
  "$baseline_build/libloom_core.a" \
  "$baseline_build/libloom_sqlite3_amalgamation.a" \
  "$baseline_build/libloom_miniz.a" -pthread -ldl -lm -lssl -lcrypto \
  -o "$task_root/regex-after"
"$task_root/regex-before" > "$task_root/before.json"
"$task_root/regex-after" > "$task_root/after.json"
cmp "$task_root/before.json" "$task_root/after.json"
sha256sum "$task_root/before.json" "$task_root/after.json"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann \
  -Iloom/third_party/doctest -Iloom/tests \
  loom/tests/main.cpp loom/tests/test_regex.cpp \
  "$task_root/regex.o" "$task_root/runtime-profile.o" \
  "$baseline_build/libloom_core.a" \
  "$baseline_build/libloom_sqlite3_amalgamation.a" \
  "$baseline_build/libloom_miniz.a" -pthread -ldl -lm -lssl -lcrypto \
  -o "$task_root/regex-tests"
"$task_root/regex-tests"
```

Nie wykonano tutaj pełnego `ctest`: integrator wątku 11 uruchamia świeży
build i pełne testy wszystkich jego zmian. Nie zmieniano konsumentów
w zakresach innych wątków. Dotychczasowe `compile(pattern, flags)` nie
zna katalogu danych użytkownika i świadomie pozostaje na wbudowanym
presecie; obsługa nakładki wymaga przekazania profilu przez właściciela
kontekstu. Nie przenoszono do danych znaków składni, opkodów VM ani reguł
semantyki języka regex: to mechanizm, a nie polityka.

## Dodatkowy pomiar wobec świeżej bazy 161cc22

Po ukończeniu świeżej kompilacji zamrożonej bazy
`161cc22dfb84fe863389d6b90323bd44516a68dc` powtórzono probe z jej
biblioteką. Pliki [before-161.json](evidence/regex/before-161.json)
i [after-161.json](evidence/regex/after-161.json) są identyczne bajtowo
między sobą oraz z wcześniejszym wynikiem: **254 166 bajtów, 523 wiersze,
4 przypadki budżetu**, SHA-256
`d54343319fe07946c4e2d023a9bb09ccc8461bcd03aa6b92548c4351b6163381`.
To usuwa zależność pomiaru od starszej bazy 9d15d2d. Jest to nadal
porównanie modułu: wariant „po” podlinkowano z bieżącymi obiektami
produkcyjnymi regex/profili oraz świeżym archiwum niezmienionych
zależności; pełne `ctest` całej gałęzi jest osobną kontrolą integracji.

W faktycznym powtórzeniu użyto nagłówków
`/workspace/scratch/72fc60ad1cc5/chatadhd-baseline/loom/include`,
archiwów `/workspace/scratch/72fc60ad1cc5/baseline-build` oraz obiektów
`regex-profile.o` i `runtime-profile-residual.o` z bieżących źródeł.
Pierwotne wyniki 9d15d2d pozostawiono w całości.
Odtworzenie nowej bazy i pomiaru:

```sh
primary_root=$(mktemp -d)
git worktree add --detach "$primary_root/before" 161cc22
cmake -S "$primary_root/before/loom" -B "$primary_root/before-build" \
  -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_WITH_OPENSSL=ON
cmake --build "$primary_root/before-build" --target loom_core -j 1
baseline_headers="$primary_root/before/loom/include"
baseline_build="$primary_root/before-build"
c++ -std=c++20 -I"$baseline_headers" -Iloom/third_party/nlohmann \
  loom/tests/compat/profile_regex_parity.cc \
  "$baseline_build/libloom_core.a" \
  "$baseline_build/libloom_sqlite3_amalgamation.a" \
  "$baseline_build/libloom_miniz.a" -pthread -ldl -lm -lssl -lcrypto \
  -o "$primary_root/regex-before"
python3 loom/src/model/gen_runtime_profiles.py
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann -Iloom/src \
  -c loom/src/re/regex.cpp -o "$primary_root/regex.o"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann -Iloom/src \
  -c loom/src/model/runtime_profile.cpp -o "$primary_root/runtime-profile.o"
c++ -std=c++20 -Iloom/include -Iloom/third_party/nlohmann \
  loom/tests/compat/profile_regex_parity.cc \
  "$primary_root/regex.o" "$primary_root/runtime-profile.o" \
  "$baseline_build/libloom_core.a" \
  "$baseline_build/libloom_sqlite3_amalgamation.a" \
  "$baseline_build/libloom_miniz.a" -pthread -ldl -lm -lssl -lcrypto \
  -o "$primary_root/regex-after"
"$primary_root/regex-before" > "$primary_root/before-161.json"
"$primary_root/regex-after" > "$primary_root/after-161.json"
cmp "$primary_root/before-161.json" "$primary_root/after-161.json"
sha256sum "$primary_root/before-161.json" "$primary_root/after-161.json"
```

## Do wątku 1

W analizatorach, które mają katalog danych/profil kontekstu, przekazać
`RuntimeProfile::load("re", data_dir)` do `compile` albo `with_profile`.
Po wyczerpaniu odczytać `last_search_hit_limit`; nie zaliczać tego jako
zwykłego braku dopasowania.

## Do wątku 2

Budżet instrukcji VM jest konfigurowalny w profilu `re`; zakres schematu
nie jest limitem polityki. Ewentualną estymację i potwierdzenie wzrostu
×10 wykonywać przed operacją w kontekście wywołującego. Interpreter nie
posiada globalnego strażnika zużycia.

## Do wątku 3

Metodom korzystającym z regex przekazać profil kontekstu. `with_profile`
pozwala stosować jednorazowy wariant bez zmiany innych użyć programu.
Zapisać hash z `profile_inspection` i flagę wyczerpania jako ślad metody.

## Do wątku 10

Edycja `step_limit` i podgląd efektywnego profilu są opisane schematem
`re.pack`. Zero oznacza zerowy budżet, a maksimum `uint64_t` jest granicą
reprezentacji. Panel nie powinien wprowadzać własnego mniejszego maksimum.

## Do wątku 9

Przed integracją wymagana jest regeneracja `runtime_profiles_embedded.inc`
z nowym `re.pack`, świeży build i pełne `ctest`. Ten raport poświadcza
wyłącznie powyższe testy skupione i pomiary zgodności modułu wobec
dwóch zapisanych baz, w tym świeżo zbudowanej 161cc22.
Pełne syntetyczne wyniki dodatnie zachowano w gałęzi; brak odrzuconych
wariantów lub płatnych wywołań w tym zakresie.
