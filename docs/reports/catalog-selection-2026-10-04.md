# Wątek 6 — katalog / wybór z archiwum, 2026-10-04

Baza pierwszej sesji: `161cc22`; wznowienie po szturchnięciu właściciela:
rebase na `30ad7d37337d6641cb7714b03e9feff0e6e25d25`. Gałąź
`gpt/catalog-selection-2026-10-04`; zachowane zmiany INTERFEJSU.
Cały wątek nadal nie jest ukończony. Nowy przyrost i wszystkie nowe pomiary
dotyczą wyłącznie DEV; historyczny checkpoint zachowano w
`archive/2026-10-04/catalog-selection-integration-held`. Wyniku blind nie poprawiano
ani nie powtarzano. Końcowe bramki wznowienia podano poniżej.

## Co weszło

Natywny kanał dostarczonych wektorów działa przed selekcją, również dla jednostek
bez trafienia leksykalnego. Ślad JSON zawiera hashe profilu, rekordu źródłowego,
zapytania, wektora i konfiguracji oraz deklarowane nazwy modelu/metody.
Nazwy nie są weryfikowane przez silnik. Ślady nie tworzą encji, aliasów ani
członkostwa projektu; wynik to ranking, nie skalibrowana pewność.
To nie jest jeszcze wymagane przez właściciela pochodzenie w grafie:
na `main` brakuje wspólnego wdrożenia kontraktu metody/wersji/przebiegu 3 i 4 oraz trwałego
zapisu pełnych wejść i wyników. Obecny identyfikator przebiegu nie wiąże całego
korpusu, a JSON w bazie nie stanowi niezmiennej historii.
Przeczytano kanoniczny `loom/src/packet/METHOD_GRAPH.md` z gałęzi 4;
nowy ślad nie wprowadza konkurencyjnej ontologii.

`catalog_semantic_candidates` konfiguruje aktywację, model/metodę/kanał, wektory,
`bias`, `weight`, `tau_relevant` i `fusion: union|additive`. Nie dodano limitu
liczby rekordów, wymiarów ani wielkości skończonych wartości. Additive łączy logity
przed zaokrągleniem. Wyłączenie przywraca wcześniejsze wyniki przy niezmienionych
pozostałych wejściach, profilu, pakiecie, katalogu i decyzjach właściciela; błędne lub stare
wejście jest odrzucane przed zapisem ocen/powiązań. Decyzje właściciela zachowują
pierwszeństwo. API pokazuje przyjęcie dostarczonych wektorów i braki kanału
(nie dostępność producenta/modelu) oraz rozróżnia brak wektora
od wyniku poniżej progu. [Kontrakt i użycie](../../loom/src/catalog/SEMANTIC_CANDIDATES.md).

## Liczby DEV

Publiczny korpus: 45 istotnych rozmów, 5 pułapek leksykalnych i 15 innych rozmów;
3 dokumenty pomocnicze liczone osobno. Presety i progi jakości bez zmian.

| Miara | Przed | Po, kanał wyłączony |
|---|---:|---:|
| Wybrane istotne rozmowy | 31/45 | 31/45 |
| Fałszywe trafienia | 0/20 | 0/20 |
| Precision | 1,0000 | 1,0000 |
| Recall | 0,6889 | 0,6889 |
| AUC końcowego rankingu | 0,9656 | 0,9656 |
| Hits@45 | 42/45 | 42/45 |
| Dokumenty pomocnicze | 3/3 | 3/3 |

Oceny, etykiety, cechy, powody i decyzje wszystkich 68 jednostek są identyczne.
[Przed](../../loom/src/catalog/tests/results/2026-10-04/dev-before.json),
[po](../../loom/src/catalog/tests/results/2026-10-04/dev-after.json).

Regresje mechanizmu: 14/14 pominięć przechodzi po dostarczeniu jawnie sztucznych,
przypisanych z etykiet wektorów; zachowane 5/5 potwierdzonych ratunków leksykalnych,
20/20 przypadków szumu bez dostarczonych wektorów nadal odrzuconych, 68/68 decyzji przywróconych
po wyłączeniu, w tym 3 przypadki wcześniejszego `sel.tiny`.
**To nie jest pomiar poprawy recall modelu ani odrzucania szumu przez model.** Wektory
produkcyjne nie powstają z etykiet DEV. [Inwentarz 19 regresji](../../loom/src/catalog/tests/dev_regressions.json)
i [pokwitowanie mechanizmu](../../loom/src/catalog/tests/results/2026-10-04/mechanism.json).

Natywne regresje pierwszej sesji: **15/15 przypadków, 228/228 asercji**, `-Werror`.
Historyczny przebieg CTest: **104/106 zaliczone**, 826,73 s. `research.structure` oraz
`research.contracts` przekroczyły istniejące 60 s. Testy importu, natywne,
CLI i pozostałe oceny przeszły. [Pełny log](../../loom/src/catalog/tests/results/2026-10-04/ctest-full.log).
Osobne wykonania diagnostyczne zakończyły się bez błędów asercji:
859 przypadków structure w 64,747 s i 209 contracts w 119,841 s.
Nie zastępują one bramki CTest. Próba diagnostyczna z tmpfs dała 1/2;
nie jest końcową weryfikacją, ponieważ handoff wymaga CTest bez zewnętrznego
`PYTHONPATH` i `TMPDIR`. Nie zmieniano timeoutów ani progów.
Archiwum integratora `4df4c569` odnotowuje te same dwa timeouty na niezmienionym
`main` (106/108 w jego konfiguracji). To potwierdzenie wcześniejszego występowania,
nie zastępstwo zielonej bramki tej gałęzi ani dowód jednej przyczyny.
**Korekta:** konfiguracja pierwszej sesji miała `LOOM_BUILD_SERVER=OFF`, więc
nie obejmowała `server.smoke` ani `server.chat_active_task`. Nazywanie jej pełnym
CTest było błędem. Wznowienie włącza serwer i wymaga 108/108; stare logi zostają.

## Przyrost DEV po wznowieniu

`relevance_recipe` dekoduje istniejące dane pakietu bez drugiego loadera.
Zniknęły fallbacki BM25 `1.2 / 0.75 / 99` oraz dwa odrębne fallbacki wagi
rozszerzeń `0.8` w score/query. Oba konsumują `term_class_weights.expansion`.
Brak parametrów powoduje jawny błąd przed zmianą ocen/powiązań lub dodaniem
terminów. API score pokazuje efektywne wagi, klasy, kanały i BM25 z hashami
parametrów oraz pakietu. To ślad JSON, nie niezmienna historia ani graf metod.
[Kontrakt](../../loom/src/catalog/RELEVANCE_RECIPE.md).

Zamknięte pozycje inwentarza: **DIC-0470, DIC-0473, DIC-0480**. Pozostałych
28 grup nie uznaje się za zmigrowane; część ma już dane, a część wymaga
nowych deskryptorów/generatora lub zmiany kontraktu. Pakiet i domyślna polityka
pozostają identyczne. Istniejący walidator KB poza zakresem 6 nadal narzuca
zakres bias [-100,100] i k1 [0,10]; nowy dekoder nie dodaje takich limitów.

Sprawdzono dwa jawne warianty interceptu przez natywną nakładkę pełnego pliku,
z identycznymi aliasami, wagami, BM25, regułami selekcji i progami jakości.

| Miara DEV | Domyślne przed | Domyślne po | Nakładka bias −2,9 |
|---|---:|---:|---:|
| TP / FN | 31 / 14 | 31 / 14 | 33 / 12 |
| FP / TN | 0 / 20 | 0 / 20 | 0 / 20 |
| Recall | 0,6889 | 0,6889 | 0,7333 |
| Precision | 1,0000 | 1,0000 | 1,0000 |
| AUC końcowego rankingu | 0,9656 | 0,9656 | 0,9667 |
| AUC leksykalny | 0,7772 | 0,7772 | 0,7800 |
| AUC lokalnego TF-IDF | 0,9811 | 0,9811 | 0,9811 |
| Hits@45 końcowy / leksykalny / TF-IDF | 42 / 36 / 43 | 42 / 36 / 43 | 42 / 36 / 43 |

Nakładka odzyskuje `nf-02-storage` i `nf-12-encryption-and-lost-again`, nie traci
żadnej wcześniejszej decyzji; pięć pierwotnych ratunków leksykalnych zachowane.
Nie używa wektorów przypisanych z etykiet. **To kalibracja na DEV, nie niezależny
sprawdzian modelu.** Nie promowano jej na preset domyślny. Wszystkie 68 domyślnych
decyzji, ocen, etykiet, cech i powodów są identyczne przed/po; zmienił się wyłącznie
raport efektywnych parametrów. [Pełne wejścia i wiersze](../../loom/src/catalog/tests/results/2026-10-04/dev-followup/README.md).

Odtwarzalny `verify_dev_followup.py`: **57/57 kontroli**, w tym pełna zgodność
68 całych wierszy, wszystkich kanałów ocen, aliasów, profili, progów, danych i
wejść runtime; zmienia się tylko bias nakładki. Nie uruchamia korpusu ani modelu.

Wariant −2,5 dał 38/45 i FP 0/20, ale pogorszył AUC leksykalne
0,777222→0,775556 oraz TF-IDF 0,981111→0,980000. Odrzucony bez luzowania ratchet;
komplet wyników i odtworzenie są na
[osobnej gałęzi archive](https://github.com/klb-t/chatadhd/tree/archive/2026-10-04/catalog-dev-bias-negative/loom/src/catalog/experiments/dev_bias_negative).

Wznowione testy: natywne **18/18, 260/260 asercji**, dekoder **7/7, 255/255**,
obie kompilacje `-Werror`, mock poprzedniego kanału nadal 14/14 i przywrócenie 68/68.
Końcowy CTest z serwerem: **108/108, 354,07 s**; `research.contracts` 34,00 s,
oba zestawy serwera wykonane. Bez zewnętrznego `PYTHONPATH`/`TMPDIR`, zmian progów
lub timeoutów. [Log](../../loom/src/catalog/tests/results/2026-10-04/dev-followup/ctest-108-final.log)
i pełny raw/XML/manifest w katalogu dowodów. Build web: zielony, 85 modułów;
zależności zainstalowano offline. Wstępne 106/106 po wznowieniu jest diagnostyczne
(brak dwóch testów serwera i przebudowa query), nie zastępuje końcowych 108.
Pierwsze pełne 107/108 z timeoutem zachowano. Przed końcową powtórką zwolniono
1,23 GB własnych artefaktów tmpfs przez przeniesienie na dysk z zachowaniem hashy
i dowiązań; nie zmieniano źródeł ani zasobów innych wątków. Nie dowodzi to
wyłącznej przyczyny timeoutu. Świeży fetch potwierdził niezmieniony `main`.

Niezmieniony licznik przypadków z dowodu integratora potwierdza **659 natywnych
przypadków / 24 465 asercji oraz 1276 Python / 0 skips**. 107 zestawów wykonało
przypadki; istniejący `unit.test_catalog_scale` jest opt-in i wykonał 0/0,
więc nie liczymy go jako pokrycia. Zestawy `context_engine` i `knowledge`
wykonały po 18 przypadków (1364 / 150 asercji).
Oryginalny XML CTest ucinał 16 stdoutów do 1024 B, przez co pierwszy count guard
odrzucił niepełny dowód. Zachowano go razem z oryginalnym XML; osobny pochodny XML
uzupełnia tylko output z pełnego `LastTest.log` tego samego przebiegu.
Statusy/czasy/liczniki niezmienione, hashe i odtworzenie zapisane; guard PASS.
Nowe 18/260 i 7/255 katalogu są osobne i nie wchodzą do sumy 659.

## Pierwsze spojrzenie

**pierwsze spojrzenie** wykonano dokładnie raz na publicznym, fikcyjnym
`blind_catalog_v2`, commit `5d85034e2353a3a6e2b2beacd1549b8cd0566584`.
Kod i ustawienia zamrożono na `7a0d3ed6`; SHA-256 biblioteki
`5ded94c8771d79105c32d8b7de38c80a7500ec7cb475198e49148a9bb49a1fe5`.
Pełne decyzje zapisano przed odczytaniem etykiet. Brak wektorów zewnętrznych;
ocena używa niezmienionego profilu DEV, którego zbiór aliasów różni się od
później odczytanej deklaracji blind. Wynik opisuje transfer profilu,
nie poprawę jakości modelu ani benchmark z osobnym profilem blind.

| Miara | Pierwsze spojrzenie |
|---|---:|
| TP / FN / FP / TN | 35 / 20 / 10 / 29 |
| Precision / recall | 0,7778 / 0,6364 |
| Wybrane pułapki leksykalne | 5/15 = 0,3333 |
| Wybrany szum ogólny | 5/24 |
| Dokumenty pomocnicze, osobno | 2/3 |

Warunek udziału wybranych pułapek ≤0,05 **nie jest spełniony**.
[Pełne decyzje, runner i hashe](../../loom/src/catalog/tests/results/2026-10-04/blind-first-look/README.md)
zachowano bez poprawek i powtórek. `eval/real-holdout-key` nie czytano.

## Odrzucone i niewykonane

Symulacja nearest-seed na zamrożonych cechach DEV pogarsza 31→30/45 przy FP 0/20:
2 odzyskane rozmowy, 3 utracone. Nie weszła do kodu produkcyjnego.
Odtwarzalny kod, wejścia i wszystkie wiersze znajdują się na
[gałęzi archiwalnej](https://github.com/klb-t/chatadhd/tree/archive/2026-10-04/catalog-nearest-seed-negative/loom/src/catalog/experiments/nearest_seed_negative).
Zachowano też pierwszą błędną normalizację ref50; poprawne porównanie używa ref75.
To symulacja wymiany cech, nie pomiar nowej natywnej implementacji.

**14 pominięć DEV pozostaje nierozwiązanych w natywnej konfiguracji domyślnej.**
Nie ma autoryzowanego niezależnego producenta/zapisanych wektorów do oceny jakości.
Podłączenie `ProviderRegistry.embed`, cache i polityki zużycia wymaga pracy poza
zakresem tego wątku; UI i migracje nie były edytowane. Zero płatnych wywołań i
zero prywatnych eksportów. Domyślnej polityki nie zmieniono: nowy zysk dotyczy
kalibracji na tym samym DEV i nie stanowi niezależnego dowodu do promocji presetu.
Pozostałe grupy inwentarza i grafowe pochodzenie metod są nadal otwarte;
bieżący checkpoint nie oznacza ukończenia całego wątku.

## Odtworzenie

```sh
cd loom
cmake --preset dev -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_BUILD_SERVER=ON -DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-keep-memory -DCMAKE_SHARED_LINKER_FLAGS=-Wl,--no-keep-memory
cmake --build --preset dev -j1
ctest --preset dev --parallel 1
cd ..
bash loom/src/catalog/tests/run_native.sh loom/build/dev /tmp/catalog-native
python3 loom/src/catalog/tests/evaluate_dev.py --library loom/build/dev/libloom.so.0.1.0 --output /tmp/catalog-dev.json
python3 loom/src/catalog/tests/evaluate_dev.py --library loom/build/dev/libloom.so.0.1.0 --output /tmp/catalog-dev-calibrated.json --relevance-overlay loom/src/catalog/tests/results/2026-10-04/dev-followup/relevance-dev-bias.json
python3 loom/src/catalog/tests/test_catalog_replay.py --library loom/build/dev/libloom.so.0.1.0 --baseline loom/src/catalog/tests/results/2026-10-04/dev-before.json --output /tmp/catalog-mechanism.json
```

Vendored SQLite i opcja linkera służyły dostępności zależności oraz ograniczeniu
pamięci builda. Nie zmieniają progów testowych. Wcześniejsze przebiegi ujawniły
ENOSPC/OOM i stare obiekty/archiwum pod dowiązaniami roboczymi; nie liczono ich
jako weryfikacji. Testy w katalogu należącym do tego wątku uruchamiane są osobno,
ponieważ rejestracja CTest jest poza jego zakresem.

## Do wątku N

- **3:** podłącz niezależnego producenta wektorów/cache do opisanego API;
  oceń jakość bez wektorów przypisanych z etykiet. Przyjmij wraz z 4 wspólny
  kontrakt `loom.method_graph/1` / `loom.method_run_trace/1` z
  `loom/src/packet/METHOD_GRAPH.md`; efektywne parametry katalogu są dostępne
  w `relevance_recipe`. Obecny kanał przyjmuje wejście, nie produkuje embeddingów.
- **4:** po wspólnej bramce z 3 udostępnij trwały zapis wejść/wyników i wiązanie
  całego korpusu z przebiegiem według tego samego kontraktu. Ranking nie jest
  twierdzeniem o tożsamości. Usuń arbitralne limity bias/k1 w walidatorze KB
  (`loom/src/kb/pack.cpp:602,611`); nowy dekoder 6 ich nie narzuca.
- **8:** zachowano timeout `research.contracts` 60,01 s oraz późniejszy
  diagnostyczny PASS 209 przypadków / 58,82 s, bez zmiany ENV/progu.
  Równoległe cudze buildy i presja pamięci są obserwacją, nie dowodem wyłącznej
  przyczyny. Rejestracja dwóch nowych zestawów katalogu w centralnym CTest jest
  poza zakresem 6; obecnie wykonuje się je osobno. Stare 106 nie obejmowało serwera.
- **9:** cały dawny checkpoint pozostaje wstrzymany: historyczne pułapki blind
  5/15 i otwarte wymagania grafowe nie są naprawione nową kalibracją DEV.
  Nowe commity `d02d201` / `b687464` to oddzielny przyrost danych/regresji;
  domyślnie 31/45, opcjonalna nakładka DEV 33/45, bez nowego blind.
  CTest **108/108** i web zielone; **nie oznacza to ukończenia całego wątku**.
  Odrzucony −2,5 ma osobne archiwum. Zaktualizuj INDEX na podstawie tego raportu;
  żadnych wyników negatywnych nie promuj jako pozytywnych.
- **11:** uwzględnij [audyt 31 grup](catalog-selection-2026-10-04-handoff.md),
  zamknięte DIC-0470/0473/0480 i **28 nadal otwartych**. Rozróżniaj presety,
  wymagania matematyczne i istniejące ustawienia. Wykorzystano istniejący loader
  pakietu/nakładek; nie dodano drugiego runtime-profile store. Definicje metod
  katalogu mają trafić do uzgodnionego grafu; nakładki/wykluczenia R39–R40 nie są
  zrealizowane przez sam hash efektywnych parametrów.
