# Wątek 4 — drugi przyrost, 2026-10-05

Status: praca w toku; poprzedni przyrost pozostaje przyjęty bez zmian.
Gałąź: `gpt/native-graph-packet-2-2026-10-05`.
Baza: `66da570d3b5379492e128d940ad474467082c59f`
(`gpt/integrator-state-2026-10-04`, bezpośrednio po `main` `e4109df`).

## Zakres i punkty wejścia

Inwentarz: `docs/reports/data-in-code/thread-4.md` z gałęzi
`gpt/data-profiles-2026-10-04`. Wybrany przyrost: sześć pełnych grup DIC-0383/0384/0385/0387/0388/0389
i część DIC-0386 (źródła oraz pola stopwordów; nie nowe języki),
dane językowe, reguły naprawy lekkiego stemmera, źródła stopwordów i progi
rozpoznawania języka w `kb::Normalizer`. Wartości domyślne pozostają takie
same; 14 nastaw przepisu jest teraz danymi, a dwa getterowe fallbacki
`min_token`/`min_stem` zostały usunięte; dane mają jedną istniejącą ścieżkę ładowania: `kb::Pack`.

R40 odczytane z wymagań na bazie. R42 odczytane z commitów `1c990a9` /
`3cd5848` gałęzi `claude/chataddhd-cpp-loom-core-IRGRN`; nie były jeszcze
na wskazanym `main` ani na bazie integratora podczas rozpoczęcia przyrostu.
Nazwy kontraktu, operacje UTF-8 i reprezentacja API pozostają w kodzie.
Dane leksykalne i nastawy zostają przeniesione do packa; brak przepisu ma
dawać jawny błąd walidacji, bez ręcznie zapisanych ukrytych domyślnych.

## Weryfikacja

Pomiar bazowy: **1236 tokenów + 300 fraz**, rzeczywiste wywołania
`Normalizer`, PASS. Zamrożony publiczny korpus deweloperski powstał z
dotychczasowego builtin packa i syntetycznych przypadków brzegowych.
SHA256 korpusu: `0707ce7bda21413fec0f6c209d4c48991f341375cf90d1f7646b1cf2cbf8335c`.
SHA256 wyniku bazowego: `c72968941cdbac69987d26bad16fd0892bd8faeffec79252877c67056e239af3`.
Dowody: [katalog](native-graph-packet-2-2026-10-05-evidence/).

Probe i launcher używają natywnego kodu oraz rzeczywistego loadera i walidatora
Pack. Bazowy pomiar linkuje archiwum 94 ukończonych obiektów ze źródeł bazy;
nie jest przedstawiany jako pełna bramka starego rdzenia. Pełny bazowy build
`-j3` przerwał OOM; powtórzenie `-j1` świadomie skrócono do zależności probe.
Oba pełne logi są w archiwum `19d72fe`; manifest obiektów i komendy
pozostają w pozytywnych dowodach.

Porównanie po zmianie: **PASS, 1536/1536 rekordów identycznych bajt w bajt**;
SHA256 wyniku po zmianie jest identyczny z bazowym. Pomiar używa pełnego
archiwum zmienionego rdzenia, nie atrapy. Zapisano komendy i hash źródeł oraz
archiwum. Kod wypchnięty w `d7e9968`; pozostaje pełny build i CTest na
vendored SQLite, WERROR, CLI, shared library i serwer. Do odbioru jeszcze
nie zgłoszono. Dodano 13 regresji nakładek i błędnej konfiguracji; wymagany
CTest powinien wykonać 14 dotychczasowych + 13 nowych przypadków packa.
Pełne pliki trzech ujemnych wariantów z przeglądu są zachowane na
`archive/2026-10-05/native-graph-packet-2-review-negatives` (`19d72fe`).
To statyczne ustalenia, nie raport wykonanych nieudanych testów: pomijane
rejestracje pod filtrem CTest, błędna szerokość usuwania niepoprawnego UTF-8
w opt-in oraz brak sprawdzanej konstrukcji dla mniejszego packa. Wszystkie
trzy poprawione; komentarz i pełne warianty pozwalają odtworzyć przegląd.
Wywołania płatne: 0.

Kontrakt nowego przepisu: [NORMALIZER_RECIPE.md](../../loom/src/kb/NORMALIZER_RECIPE.md).
Format `loom.kb.stemming/2` jawnie wymaga przepisu; starszą nakładkę `/1`
trzeba zaktualizować z bieżącego szablonu, zachowując własne tablice stemmera.

## Otwarte przekazania z indeksu

Indeks na `main` uznaje wspólny kontrakt 3/4 za zamknięty. Pozostają
„Dalsze KB pack/store profile i grafowy profil”. Przyrost dotyczy KB packa;
nie zmienia `METHOD_GRAPH.md`, kanonicznego goldena ani przyjętego kodu
packet. Pełna migracja parametrów zapytań store/CABI i grafowe warstwy
domyślnych pozostają osobnymi zadaniami. Nie rozszerzono `Lang`, nie przeniesiono
słowników kategoriami relacji ani pozostałych sufitów walidacji innych dokumentów.

Po świeżym fetch przeczytano też aktualne przekazania W3 i W11 (`1b7379c`)
oraz W12: definicje metod i profile runtime muszą korzystać z jednego resolvera;
W11 nadal oczekuje normalizatora **tworzenia nowych rekordów** z zachowaniem
historycznego wire. `Normalizer::create` z tego przyrostu jest sprawdzaną
konstrukcją normalizatora **tekstu** i nie zamyka tamtego zadania.

## Do wątku N

- **9:** nowa gałąź opiera się na aktualnej dozwolonej bazie integratora;
  poprzedniej gałęzi W4 nie zmieniono. Gotowość zostanie zgłoszona po bramkach.
- **1:** wybrany inwentarz W4 wymaga dodatków w `lexicons/stemming.json`;
  pozostawiamy dotychczasowe słowniki i ich domyślne wyniki.
- **3/12:** packowa nakładka nie jest pełnym R40. Wspólne wyłączenia,
  trwałe wykluczenia i wyjaśnienia warstw trzeba podłączyć przez istniejący
  resolver W12; W4 nie tworzy drugiego mechanizmu warstw. Konsument opcjonalnej
  metody powinien używać `Normalizer::create`, propagować `Unavailable` przy
  braku przepisu i zapisać efektywny przepis oraz hash jego packa w konkretnej
  wersji metody. Pełny pack hash obejmuje też jej słowniki/zależności.
  Pozostaje kanoniczny kontrakt `loom/src/packet/METHOD_GRAPH.md`.
- **11:** prywatny stan `Normalizer` w publicznym `kb.h` wymaga dodatków
  związanych z przenoszonym przepisem. Query presety store i CABI wymagają
  wspólnej odpowiedzialności za `knowledge_store.h` / `capi_knowledge`;
  w tym przyroście nie zmieniam tych niezależnych ścieżek.
