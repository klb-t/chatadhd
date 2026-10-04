# Wątek 6 — katalog / wybór z archiwum, 2026-10-04

Baza: `161cc22dfb84fe863389d6b90323bd44516a68dc`; gałąź
`gpt/catalog-selection-2026-10-04`. Zachowane zmiany INTERFEJSU.
Status: **wstrzymane do integracji** — pełny CTest nie jest zielony, a blind
nie spełnia warunku odrzucania pułapek leksykalnych.

## Co weszło

Natywny kanał dostarczonych wektorów działa przed selekcją, również dla jednostek
bez trafienia leksykalnego. Ślad JSON zawiera hashe profilu, rekordu źródłowego,
zapytania, wektora i konfiguracji oraz deklarowane nazwy modelu/metody.
Nazwy nie są weryfikowane przez silnik. Ślady nie tworzą encji, aliasów ani
członkostwa projektu; wynik to ranking, nie skalibrowana pewność.
To nie jest jeszcze wymagane przez właściciela pochodzenie w grafie:
brakuje wspólnego kontraktu metody/wersji/przebiegu wątków 3 i 4 oraz trwałego
zapisu pełnych wejść i wyników. Obecny identyfikator przebiegu nie wiąże całego
korpusu, a JSON w bazie nie stanowi niezmiennej historii.

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

Natywne regresje: **15/15 przypadków, 228/228 asercji**, kompilacja z `-Werror`.
Pełny CTest: **104/106 zaliczone**, 826,73 s. `research.structure` oraz
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

**14 rzeczywistych pominięć DEV pozostaje nierozwiązanych w konfiguracji domyślnej.**
Nie ma autoryzowanego niezależnego producenta/zapisanych wektorów do oceny jakości.
Podłączenie `ProviderRegistry.embed`, cache i polityki zużycia wymaga pracy poza
zakresem tego wątku; UI i migracje nie były edytowane. Zero płatnych wywołań i
zero prywatnych eksportów. Pliki pakietu/polityki pozostawiono bez zmian, bo nie
uzyskano mierzalnego zysku uzasadniającego nowy preset.

## Odtworzenie

```sh
cd loom
cmake --preset dev -DLOOM_USE_SYSTEM_SQLITE=OFF -DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-keep-memory -DCMAKE_SHARED_LINKER_FLAGS=-Wl,--no-keep-memory
cmake --build --preset dev -j1
ctest --preset dev --parallel 1
cd ..
bash loom/src/catalog/tests/run_native.sh loom/build/dev /tmp/catalog-native
python3 loom/src/catalog/tests/evaluate_dev.py --library loom/build/dev/libloom.so.0.1.0 --output /tmp/catalog-dev.json
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
  kontrakt metody/wersji/przebiegu i krawędzi „wytworzony przez”.
- **4:** uzgodnij ten kontrakt z 3; zapewnij trwałe wejścia/wyniki i wiązanie
  całego korpusu z przebiegiem. Ranking nie jest twierdzeniem o tożsamości.
- **7/8:** zbadaj dwa timeouty zestawów research na zachowanych logach;
  naprawa ich kodu/rejestracji leży poza zakresem 6. Bez wydłużania progów.
- **9:** nie integruj jako zielonej gałęzi: CTest 104/106 i pułapki blind 5/15.
  Uwzględnij status w INDEX oraz zależność od wspólnego formatu 3/4.
- **11:** uwzględnij [audyt 31 grup](catalog-selection-2026-10-04-handoff.md),
  zwłaszcza rozróżnienie presetów od wymagań matematycznych i już istniejących
  ustawień. Definicje metod katalogu mają trafić do uzgodnionego grafu.
