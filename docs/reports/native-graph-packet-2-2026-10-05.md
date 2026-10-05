# Wątek 4 — drugi przyrost, 2026-10-05

Status: praca w toku; poprzedni przyrost pozostaje przyjęty bez zmian.
Gałąź: `gpt/native-graph-packet-2-2026-10-05`.
Baza: `66da570d3b5379492e128d940ad474467082c59f`
(`gpt/integrator-state-2026-10-04`, bezpośrednio po `main` `e4109df`).

## Zakres i punkty wejścia

Inwentarz: `docs/reports/data-in-code/thread-4.md` z gałęzi
`gpt/data-profiles-2026-10-04`. Wybrany przyrost: DIC-0383–0389,
dane językowe, reguły naprawy lekkiego stemmera, źródła stopwordów i progi
rozpoznawania języka w `kb::Normalizer`. Wartości domyślne pozostają takie
same; dane mają jedną istniejącą ścieżkę ładowania: `kb::Pack`.

R40 odczytane z wymagań na bazie. R42 odczytane z commitów `1c990a9` /
`3cd5848` gałęzi `claude/chataddhd-cpp-loom-core-IRGRN`; nie były jeszcze
na wskazanym `main` ani na bazie integratora podczas rozpoczęcia przyrostu.
Nazwy kontraktu, operacje UTF-8 i reprezentacja API pozostają w kodzie.
Dane leksykalne i nastawy zostają przeniesione do packa; brak przepisu ma
dawać jawny błąd walidacji, bez ręcznie zapisanych ukrytych domyślnych.

## Weryfikacja

Plan: zamrożony publiczny korpus deweloperski, zbudowany z dotychczasowego
builtin packa i syntetycznych przypadków brzegowych; rzeczywisty snapshot
starego oraz nowego natywnego normalizatora. Porównanie wyników bajt w bajt,
testy nakładek i błędnej konfiguracji, pełny CTest na vendored SQLite,
WERROR, CLI, shared library i serwer. Liczby i dowody zostaną dopisane po
wykonaniu. Wywołania płatne: 0.

## Otwarte przekazania z indeksu

Indeks na `main` uznaje wspólny kontrakt 3/4 za zamknięty. Pozostają
„Dalsze KB pack/store profile i grafowy profil”. Przyrost dotyczy KB packa;
nie zmienia `METHOD_GRAPH.md`, kanonicznego goldena ani przyjętego kodu
packet. Pełna migracja parametrów zapytań store/CABI i grafowe warstwy
domyślnych pozostają osobnymi zadaniami.

## Do wątku N

- **9:** nowa gałąź opiera się na aktualnej dozwolonej bazie integratora;
  poprzedniej gałęzi W4 nie zmieniono. Gotowość zostanie zgłoszona po bramkach.
- **1:** wybrany inwentarz W4 wymaga dodatków w `lexicons/stemming.json`;
  pozostawiamy dotychczasowe słowniki i ich domyślne wyniki.
- **3/12:** packowa nakładka nie jest pełnym R40. Wspólne wyłączenia,
  trwałe wykluczenia i wyjaśnienia warstw trzeba podłączyć przez istniejący
  resolver W12; W4 nie tworzy drugiego mechanizmu warstw.
- **11:** prywatny stan `Normalizer` w publicznym `kb.h` wymaga dodatków
  związanych z przenoszonym przepisem. Query presety store i CABI wymagają
  wspólnej odpowiedzialności za `knowledge_store.h` / `capi_knowledge`;
  w tym przyroście nie zmieniam tych niezależnych ścieżek.
