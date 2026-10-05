# Wątek 8 — porządki repo, 2026-10-04

Gałąź: `gpt/repo-hygiene-2026-10-04`, baza `161cc22` (aktualny `main`
po pracy INTERFEJS). [PR #11](https://github.com/klb-t/chatadhd/pull/11) jest
roboczy. Wątek 8 nie przesuwa `main`.

## Zrobione i pomiary

| Obszar | Przed | Po |
|---|---|---|
| Aktywne workflow | 5, w tym 3 piloty OpenRouter | 2; trzy stare definicje zachowane bajt w bajt z hashami i instrukcją odtworzenia |
| `context_engine` w dowodzie z 2026-10-03 | 0 przypadków, 0 asercji | świeży CI `dev`: 18 przypadków, 1364 asercje |
| `knowledge` w tym samym dowodzie | 0 przypadków, 0 asercji | świeży CI `dev`: 18 przypadków, 150 asercji |
| `resolve_lineage` | 2 przypadki, 0 asercji | świeży CI `dev`: 2 przypadki, 15 asercji; checkout zachowuje historię |
| Strażnik dowodu | samo powodzenie wpisu CTest mogło ukryć 0 przypadków | kontrola manifestu, pełnych podsumowań, przypadków/asercji i pominięć; 16/16 regresji |
| `seeding/results` | 26 plików, 13 800 361 bajtów | bez zmian: wymagane przez `research.seeding`; 36/36 testów |
| Szkic prezentacyjnego README | brak w raporcie W8 | [gotowy szkic](repo-hygiene-readme-draft-2026-10-04.md), bez edycji istniejących README |

CI instaluje rzeczywiste zależności kontraktów i uruchamia obecne testy web,
w tym profile i importowaną treść. Zapisuje manifest, pełny XML, rzeczywiste
liczniki i hashe zbudowanych plików. Preset ASan zachowuje istniejący brak
biblioteki FFI; niedostępne przypadki są jawnie odliczane. Opt-in test skali
pozostaje **niewykonany**, a nie potwierdzeniem przetworzenia 1 GB.

## Weryfikacja i otwarte blokady

Pierwszy CI: [run 37210513736](https://github.com/klb-t/chatadhd/actions/runs/37210513736),
źródło `ee97311`. `dev` wykonał **108/108 wpisów CTest** w 106,91 s,
ASan **108/108** w 296,19 s. Z kompletnego logu `dev` policzono 659
przypadków natywnych / 24 465 asercji oraz 1276 przypadków Python / 0 pominięć.
Pełny `LastTest.log` potwierdza powyższe liczniki; błędna względna ścieżka
XML zatrzymała następny krok. Ścieżkę poprawiono na bezwzględną; rzeczywisty
mały sprawdzian `ctest --preset` wraz ze strażnikiem przeszedł.
Drugi CI, na poprawce `6523c1b`, nadal trwa:
[run 37211656572](https://github.com/klb-t/chatadhd/actions/runs/37211656572).
Jego wynik zostanie dopisany po zakończeniu; nie deklaruję zielonego całego CI.

Clang zatrzymał build na dwóch nieużywanych przechwyceniach `this` w
`loom/server/src/app.cpp:768,772`. Nie obniżono ostrzeżeń i nie zmieniono
pliku poza zakresem. Negatywne logi i metadane pozostają w
[archiwum dowodów](../archive/repo-hygiene-2026-10-04/README.md).

Lokalny build został przerwany przez brak miejsca na współdzielonym dysku,
a następnie wykrył pusty obiekt `fts.cpp.o`. Zachowano logi wszystkich prób;
ten build nie jest dowodem testów. Lokalny produkcyjny build web przeszedł
(85 modułów), podobnie jak pięć testów offline web.

Nie wykonano płatnych wywołań, nie czytano klucza holdout ani ślepego korpusu.
Nie przeniesiono aktywnych fixture seeding. Nie zmieniono STATE, istniejących
README, UI ani kontraktów profili.

## Do wątku 10

Usuń nieużywane przechwycenia `[this]` z dwóch lambd tras `/api/logs` i
`/api/version` w `loom/server/src/app.cpp:768,772` (najmniejsza poprawka: `[]`).
Pełny log Clanga i dokładne polecenia są w
`docs/archive/repo-hygiene-2026-10-04/ci-first/vendored-job.*`.

## Do wątku 11

W inwentarzu uwzględnij istniejącą politykę dostępności testów w
`.github/scripts/verify_ctest.py`: mapowanie ASan na niedostępne przypadki FFI,
liczbę dwóch pominiętych przypadków ABI oraz osobny opt-in `catalog_scale`.
Ewentualne przeniesienie do danych musi zachować dokładne powody/liczniki,
odliczanie niewykonanych przypadków i odrzucanie nieoczekiwanych pominięć.
Parsery podsumowań doctest/unittest są formatami dowodu, nie metodami analizy
semantycznej. Metody analizy jako byty grafu opisano w roadmapie szkicu README
jako wymaganie do uzgodnienia przez wątki 3 i 4, nie przyjętą funkcjonalność.

## Do wątku 9

- Zintegruj liniowo dopiero po poprawce wątku 10, świeżym rebase, pełnym CTest
  i buildzie web. Wątek 8 pozostawia `main` bez zmian.
- Wstaw szkic README po przyjęciu kolejnych wątków, aktualizując zdolności
  i pomiary do rzeczywiście przyjętego źródła. Nie przedstawiaj trwających
  badań ani selektora jako ukończonych. Zachowaj rozróżnienie metod jako bytów
  grafu i pomiarów jako twierdzeń o tych bytach.
- Raport W2 opisuje 19 grup policy poza CTest: wymagają osobnego dowodu lub
  uzgodnionej rejestracji. Raport W4 opisuje 110 wpisów (+2); sprawdź nazwy
  nowych wpisów i objęcie ich kontrolą wewnętrznych przypadków. Dla nowych
  nazw poza rozpoznanymi rodzinami strażnik raportuje tylko status skryptu.
- Rejestracja CMake i centralny eksport ABI z raportów W2/W4 wymagają
  przydzielenia właściciela przez integratora. Wątek 8 ich nie edytuje.
