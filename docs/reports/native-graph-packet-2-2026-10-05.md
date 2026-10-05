# Wątek 4 — drugi przyrost, 2026-10-05

Status: **GOTOWY DO ODBIORU przez W9**; bieżący krok zakończony na polecenie właściciela. Poprzedni przyrost bez zmian.
Commit kodu do odbioru: **`9c9110c45c6a6f0f331eb5122b79a4a75356e1ba`**. Końcowy HEAD tej gałęzi dodaje wyłącznie ten raport i pełne pokwitowania bramek.
Gałąź: `gpt/native-graph-packet-2-2026-10-05`.
Baza po rzeczywistym rebase: aktualny podczas fetch `main`
`6ae0e28b8405b43aa9d8995ad8273a0bcb0b34c4`. Pierwotna dozwolona baza
integratora `66da570` ma identyczne drzewo produktu `loom/`.

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
archiwum. Pełny końcowy build: **PASS** (GCC 13.3, Debug `-g0`, vendored
SQLite 3.47.2, WERROR, CLI, shared library i serwer). Końcowy pomiar po
wycofaniu nowego sztucznego zakresu `[0,1]` dla progów języka również PASS:
progi są dowolnymi skończonymi liczbami, a wyniki domyślne bez zmian.
**Pełny CTest: 117/117 PASS**; niezmieniony guard wykonania PASS.
Rzeczywiście wykonano **723 przypadki C++ / 26 784 asercje** oraz
**1322 przypadki Python / 0 pominiętych** (Python 3.12.14). Pack: **27
przypadków / 569 asercji**, czyli 14 istniejących + 13 nowych. Jedyny
zestaw 0/0 to istniejący, opcjonalny `unit.test_catalog_scale`; pozostałe
116 wpisów są rzeczywiście wykonane. Test lineage wykonał 2 przypadki
i 15 asercji, po pobraniu 166 historycznych blobów odpowiadających tylko
jego dwóm publicznym snapshotom; nie pominięto historii.

Wspólne zasoby wymusiły `-j1` dla buildu i cienkie archiwum GNU. To
format artefaktu, nie zmiana produktu; zapisano hash 143 obiektów rdzenia,
komendy, konfigurację oraz pełne logi. CTest uruchomiono `-j2`, bez
zmiany progów, usuwania testów ani ucinania wyjścia JUnit.

Rebase i końcowa selekcja zmieniły wyłącznie dokumentację/metadane
historii. Drzewo `loom/` przed i po jest identyczne:
`173a1aee71f0446c1c3ee01b4e869df3ed191cae`; źródła mają te same hashe
co końcowy build. Dowód: `rebase-receipt.json` i `final-build-binding.json.gz`.
Nie powtarzano tego samego buildu/testów po rebase bez zmian produktu.
Cały wykonany stan zachowano w archiwum `before-rebase` na `edf338b`.
Przygotowany przyrost nie obejmuje dalszego kodu z nowszej gałęzi integratora.

Pełne pliki trzech ujemnych wariantów z przeglądu są zachowane na
`archive/2026-10-05/native-graph-packet-2-review-negatives` (`19d72fe`).
To statyczne ustalenia, nie raport wykonanych nieudanych testów: pomijane
rejestracje pod filtrem CTest, błędna szerokość usuwania niepoprawnego UTF-8
w opt-in oraz brak sprawdzanej konstrukcji dla mniejszego packa. Wszystkie
trzy poprawione; komentarz i pełne warianty pozwalają odtworzyć przegląd.
Sztuczny, odrzucony zakres `[0,1]` dla progów zachowano w pełnej postaci
na `archive/2026-10-05/native-graph-packet-2-bounded-cutoffs` (`49f7602`).
Ujemne logi infrastruktury są wyłącznie w archiwum, usunięte także
z całej wybranej historii gałęzi. Wywołania płatne: 0; bez ślepego korpusu
i danych prywatnych.

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

## Niedokończone i punkt startu następnej osoby

Nie zaczynać kolejnych migracji w tej sesji. Po odbiorze tego przyrostu
zacząć od aktualnego inwentarza `docs/reports/data-in-code/thread-4.md`
i przekazań w `INDEX.md`: presety query/store i CAPI wymagają uzgodnienia
własności publicznego API z W11. Potem podłączyć efektywny przepis przez
resolver warstw W12 i wersję metody z kontraktu `METHOD_GRAPH.md` (W3),
z krawędzią pochodzenia wyniku. Nie tworzyć drugiego loadera ani ukrytych
fallbacków. Normalizator tworzenia rekordów, rozszerzenie języków i
pozostałe grupy inwentarza nie są zrobione. Starsze nakładki stemming `/1`
wymagają jawnej aktualizacji do `/2`; zmiana prywatnego stanu C++
`Normalizer` wymaga rekompilacji jego klientów. C ABI nie zmieniono.
Web i macierz Clang/ASan nie były uruchamiane w tej sesji (W9/W8).

## Do wątku N

- **9:** **gotowy przyrost**, kod `9c9110c45c6a6f0f331eb5122b79a4a75356e1ba` na `main` `6ae0e28`.
  Odebrać całą gałąź wraz z końcowym pokwitowaniem dokumentacyjnym.
  Pełne CTest i guard PASS; własny web/mixed gate po późniejszych zmianach
  innych wątków pozostają odbiorem integratora. Starej przyjętej gałęzi W4
  nie ruszano. Nie zamykać wszystkich 27 grup inwentarza tym przyrostem.
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
