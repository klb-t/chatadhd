# Wątek 8 — porządki repo, 2026-10-04

Gałąź: `gpt/repo-hygiene-2026-10-04`, baza po rebase `7282437` (aktualny `main`
z opublikowanym indeksem integratora; wcześniejsze receipts pozostają przypięte do swoich źródeł). [PR #11](https://github.com/klb-t/chatadhd/pull/11) jest
roboczy z jedną blokadą poza zakresem. Wątek 8 nie przesuwa `main`.

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
Drugi CI, na poprawce `6523c1b`, zakończony:
[run 37211656572](https://github.com/klb-t/chatadhd/actions/runs/37211656572).
**dev i ASan są zielone**, każdy 108/108 CTest (100,83 s / 219,25 s), ze
strażnikiem dowodu. Dev: 659 przypadków natywnych / 24 465 asercji i 1276
Python / 0 pominięć. ASan: 659 / 24 464 oraz 1252 Python / 24 jawnie
niedostępne przypadki FFI. JNI 1/1 i cały web/przeglądarki przeszły.
[Pełny świeży dowód](../verification/repo-hygiene-2026-10-04/RESULTS.md)
zawiera oryginalne XML, hashe źródła/binarek i pełne logi. Cała macierz CI
pozostaje czerwona wyłącznie przez błąd kompilacji Clanga poniżej.

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
Sesja wznowiona po szturchnięciu właściciela: wykonano `fetch`, odczytano
indeks `7282437` oraz zadania W8. Rebase zakończony bez konfliktów; dawny
tip `2544cf3` zachowany na `archive/repo-hygiene-before-rebase-2026-10-04`.
Przyrost main zmienił wyłącznie dokumentację, bez zmian kodu/testów/CMake/web.
W tej sesji trwa osobny przyrost DIC-0692 oraz polityki strażnika z danych;
jego nowe dowody nie zastępują ani nie przemianowują wcześniejszych wyników CI.

## Do wątku 10

Usuń nieużywane przechwycenia `[this]` z dwóch lambd tras `/api/logs` i
`/api/version` w `loom/server/src/app.cpp:768,772` na main `7282437` (najmniejsza poprawka: `[]`).
Na tipie W10 `2f25145` te same lambdy mają linie **835 i 839**.
Dodatkowy przegląd źródłowy wskazuje wariant bez nagłówka W2: lambda
`/api/usage-policy` na linii **515** używa `this` tylko wewnątrz
`LOOM_SERVER_HAS_USAGE_POLICY`; sprawdź Clang zarówno z W2, jak i bez niego.
Ten trzeci punkt jest wnioskiem ze źródła, nie wynikiem wykonanego builda.
W8 zachowuje Clang, vendored SQLite i `WERROR=ON`; nie wycisza ostrzeżeń.
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
Przeczytano nowy raport `data-in-code/thread-8.md` z `c21e664`: **DIC-0692**
dotyczy wymiarów `roles/capabilities/features` i mapowania grafu w
`loom/tools/seeding/prototype.py:24`. Wznowiono implementację profilu wymiarów/projekcji i walidowanej nakładki
w `loom/tools/seeding` (zakres W8). Zachowujemy zamrożone moduły oraz wyniki
bajt w bajt. Wspólna reprezentacja metod/przebiegów w grafie nadal wymaga
kontraktu W3/W4; profil konfiguracji seeding nie zastępuje tego kontraktu.
[Jawny inwentarz strażnika](repo-hygiene-evidence-policy-inventory-2026-10-04.md)
rozdziela uniwersalne formaty dowodów od presetów dostępności; migracja w toku.

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
- Uzgodnij format metody / wersji / przebiegu i krawędź „wytworzony przez”
  między W3 i W4 przed scaleniem. Profil/projekcje DIC-0692 są w implementacji W8; do uzgodnienia pozostaje
  most do wspólnego grafu metod. Świeży tip W10 (`2f25145`) jeszcze nie
  poprawia wskazanych przechwyceń Clanga. Po poprawce wymagana pełna macierz CI.
