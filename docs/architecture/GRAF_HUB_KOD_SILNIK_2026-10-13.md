# Graf jako centralny hub, kod jako uniwersalny silnik (właściciel, 2026-10-13)

Dokument nadrzędny dla dalszych prac nad modułowością (P5, briefy `docs/reports/p5-briefs/`).
Uzupełnia `DUCH_GRAFU_2026-10-11.md` i `EVERYTHING_IN_THE_GRAPH_2026-10-11.md`; przy sprzeczności wygrywa ten.

## 1. Słowa właściciela (zapis wierny)

- Wywołanie modelu ma wyglądać tak: **„Użyj wybranego modelu (lub kolejno modeli, jeśli wariantujemy) z takimi
  parametrami, kontekstem i promptem.”**
- Wszystkie dane, czyli: **wybrani dostawcy w ustawieniach (linkują do adapterów)**, **workflow określony dla danego
  węzła typu wiadomość w zbiorze typu konwersacja** — każdy element, na którym można wykonać charakterystyczne
  działanie, ma **link do obiektu z podzbioru workflows / actions / api calls**.
- Loom zwraca o każdym polu: **czy ma wartość domyślną** i **jaka jest zasada edycji: fakultatywnie / obligatoryjnie /
  inna zasada**.
- **Graf jako centralny hub i dojście do wszystkich danych w całym ekosystemie; kod jako uniwersalny silnik.**
  Większość wywołań ma brzmieć: „użyj tego węzła z grafu i tamtego, wrzuć do tego i zastosuj dla niego właściwą
  według grafu akcję”.
- **Urządzenia wejścia/wyjścia również są w grafie**, nie tylko pamięć. Graf obejmuje wszystkie dane i przepływy
  między nimi, źródła, metadane.
- **Pamięci (dowolna kombinacja) to zdecydowana większość grafu, ale nie cały graf.** Model samego siebie
  i podstawowe przepływy leżą w plikach programu, nie w magazynie — „taki niby ROM”.

### Doprecyzowanie właściciela (2026-10-13): „użyj modelu” to bardzo ogólna rutyna

Obejmuje między innymi (to tylko oczywiste przykłady):
- wysłanie wiadomości do konwersacji; wiadomość grafowa (graph native) w konwersacji;
- analizę strukturalną i semantyczną — różne przepływy;
- użycie przez skrypt wewnętrzny uruchamiany w aplikacji albo przez agenta;
- klasyfikację i ważenie krawędzi przez Jev;
- analizę wejścia użytkownika: czy to wiadomość, czy polecenie do interfejsu; jeśli polecenie — analizę
  możliwości wykonania, kosztów itd.;
- typowe zastosowania innych modeli: wygeneruj wideo, muzykę, obraz.

Wniosek techniczny: akcja „użyj modelu” nie zna czatu ani tekstu. Przyjmuje węzły dowolnego rodzaju (tekst, obraz,
dźwięk, wideo, dane strukturalne), zwraca węzły dowolnego rodzaju (z plikami w magazynie i pochodzeniem), a model
deklaruje w grafie, co przyjmuje i co zwraca. Każde zastosowanie z listy to osobny workflow w grafie, który tę
akcję wywołuje — nowe zastosowanie = nowy workflow (dane), nie nowy kod.

## 2. Jak to czytam technicznie (Claude)

Jedno zdanie: **silnik wykonuje tylko jedną rzecz — „weź te węzły, zastosuj do nich akcję wskazaną w grafie,
wynik zapisz jako węzeł z pochodzeniem”.** Wszystko inne (który model, który dostawca, jaki prompt, który magazyn,
które urządzenie) to dane w grafie.

| Pojęcie właściciela | W grafie | W kodzie |
|---|---|---|
| element z charakterystycznym działaniem (wiadomość, konwersacja, plik, ustawienie…) | węzeł z linkiem `ma_akcję` → węzeł akcji | nic poza wykonaniem akcji |
| akcja / workflow / api call | węzeł z wejściami, wyjściami, krokami (kolejno, wariantowo, równolegle) | interpreter kroków |
| „użyj modelu(-i) z parametrami, kontekstem, promptem” | akcja `użyj_modelu`: lista modeli (jeden albo kilka wariantów), parametry, przepis kontekstu, prompt — wszystko jako węzły | jeden adapter na rodzaj API dostawcy |
| wybrani dostawcy w ustawieniach | węzeł ustawienia → link do węzła adaptera | kod adaptera (sterownik) |
| pole: domyślna wartość + zasada edycji | metadane pola: `domyślna: jest/brak`, `edycja: fakultatywna / obligatoryjna / tylko_odczyt / wymaga_potwierdzenia / …` | walidacja według tych metadanych |
| urządzenia wejścia/wyjścia (mikrofon, aparat, ekran, klawiatura, plik, sieć) | węzły urządzeń z akcjami (nagraj, rozpoznaj mowę, OCR, wyświetl) | sterowniki urządzeń |
| pamięci (dowolna kombinacja) | podłączone magazyny, każdy jako węzeł z rolą i ustawieniami | wtyczki magazynów (P4) |
| „niby ROM” | **pamięć tylko do odczytu, dostarczana z programem, montowana jako pierwsza** — w grafie jak każda inna | pliki programu |

**Nie ma sprzeczności.** „ROM” to po prostu jeszcze jedna pamięć: tylko do odczytu, podłączana zawsze i jako pierwsza.
Graf adresuje jej węzły tak samo jak węzły z innych pamięci. Użytkownik niczego w ROM nie zmienia, ale może każdy
jej element **przesłonić** swoją wersją w zapisywalnej pamięci (to jest dzisiejszy mechanizm presetów i nakładek).
Tak „graf obejmuje wszystko”, a bajty części grafu leżą w plikach programu.

## 3. Co jeszcze musi być w „ROM” (lista Claude’a)

Kryterium: bez tego graf nie wystartuje, nie odczyta pamięci albo nie da się go naprawić.

| # | Zawartość ROM | Dlaczego tam, a nie w pamięci użytkownika |
|---|---|---|
| 1 | **Rozruch**: jak znaleźć i otworzyć pamięci (kolejność katalogów danych, KOD≠DANE), jak zamontować kolejne | zanim pamięć jest otwarta, nie da się z niej czytać |
| 2 | **Meta-ontologia**: czym jest węzeł, krawędź, akcja, pole, pochodzenie, wersja; typy podstawowe | silnik musi umieć przeczytać każdy graf, także pusty |
| 3 | **Interpreter akcji** i jego operacje elementarne (czytaj/zapisz węzeł, wywołaj adapter, kolejno/wariantowo/równolegle, warunek, potwierdzenie) | to jest „uniwersalny silnik” — kod |
| 4 | **Sterowniki**: adaptery API dostawców, magazynów, formatów plików, urządzeń, kryptografia | kod wykonywalny; w grafie jest ich opis i link |
| 5 | **Ustawienia fabryczne** (presety) i domyślne przepływy: czat, import, analiza | punkt startu i „przywróć domyślne” |
| 6 | **Model samego siebie**: wersja, możliwości, koszty, znane ograniczenia, filozofia i wymagania R1–R53 | aplikacja ma o sobie wiedzieć niezależnie od użytkownika (R52) |
| 7 | **Bezpieczniki nie do wyłączenia przypadkiem**: potwierdzenie ×10, ochrona przed utratą danych, nigdy nie nadpisuj nieprzeczytanego pliku | jedyny wyjątek z zasady właściciela; reszta jest ustawieniem |
| 8 | **Walidatory i migracje**: jak czytać stare formaty, jak przenieść dane między wersjami | stare dane muszą zostać czytelne |
| 9 | **Tryb ratunkowy**: minimalny interfejs do ustawień i pamięci, gdy wszystko inne zawiodło (brak sieci, zła konfiguracja, uszkodzona pamięć) | inaczej zła konfiguracja zamyka drogę do jej naprawy |
| 10 | **Korzenie zaufania**: algorytmy kryptograficzne, klucze do weryfikacji aktualizacji i podpisów | nie mogą pochodzić z danych, które mają weryfikować |
| 11 | **Generatory tożsamości i czasu**: format identyfikatorów, zegar, wersjonowanie | spójność adresów w całym ekosystemie |
| 12 | **Podstawowe teksty interfejsu** (pl, en) | aplikacja musi się odezwać przed załadowaniem pamięci |
| 13 | **Katalog ROM**: spis wszystkich powyższych elementów z adresami w grafie | dzięki niemu ROM jest widoczny w grafie jak każda pamięć |

Wszystko spoza tej listy (ustawienia, konta, dostawcy, akcje użytkownika, rozmowy, wiedza) leży w pamięciach.

## 4. Co to zmienia w pracy (decyzje Claude’a)

1. Fala 1 napraw modułowości (P5) zaczyna od **jednej akcji „użyj modelu”**, którą wywołują wszystkie miejsca
   używające dziś modelu (dziś jest 6 osobnych kopii). Akcja przyjmuje: model albo listę modeli (wariantowanie),
   parametry, przepis kontekstu, prompt — wszystko jako odnośniki do węzłów.
2. Fala 1b: **metadane pól** (domyślna jest/brak, zasada edycji) zwracane przez Loom dla każdego ustawienia.
3. Fala 2: **link `ma_akcję`** z typów węzłów (wiadomość, konwersacja, plik, urządzenie…) do akcji; silnik wybiera
   akcję z grafu zamiast z kodu.
4. Fala 3: **urządzenia wejścia/wyjścia jako węzły** (mikrofon, aparat, ekran, pliki, sieć) z akcjami.
5. Fala 4: **ROM jako pamięć tylko do odczytu** z katalogiem — obecne pliki danych w `loom/data/` stają się jej
   zawartością, montowaną jako pierwsza; nakładki użytkownika jak dziś.
6. Każda fala: jeden agent naraz, brama, zapis w `docs/reports/PLAN_2026-10-13.md`.

## 5. Jakie akcje wolno wykonać na jakim elemencie (właściciel pyta, Claude proponuje, 2026-10-13)

Właściciel: na elementach wykonuje się nie tylko „użyj modelu”, ale **każdy flow adresowalny w grafie** — dostępny
dosłownie albo łatwy do ekstrapolacji (co to znaczy, jakie progi — też w grafie); stosowanie według zasad.
Pytanie: czy klasa ma wyliczać dozwolone akcje (w grupach), czy akcje mają flagi, a klasa mówi, jakie flagi
akceptuje, wymaga, olewa?

**Propozycja: klasa nie wylicza akcji.** Lista akcji nigdy nie będzie znana z góry, więc wyliczanie zawsze będzie
niepełne. Zamiast tego cztery rzeczy, każda w grafie:

| Co | Gdzie | Przykład |
|---|---|---|
| **Cechy** (to, czym element jest i co ma) | na klasie, dziedziczone; węzeł może dodać własne | „jest wiadomością”, „ma tekst”, „ma plik audio”, „należy do konwersacji”, „edytowalny” |
| **Wymagania akcji** (czego akcja potrzebuje i co daje) | na akcji | „podsumuj”: wymaga „ma tekst”, daje „podsumowanie”; „transkrybuj”: wymaga „ma audio”, daje „ma tekst” |
| **Flagi skutków akcji** | na akcji | tylko czyta / zapisuje / wysyła poza urządzenie / kosztuje / nieodwracalna / dane prywatne |
| **Zasady** | w grafie, z zakresami (użytkownik, projekt, konwersacja — R47) | „akcje wysyłające poza urządzenie dla klasy »dziennik« — pytaj”; „dla wiadomości domyślna akcja = odpowiedz modelem X”; „nigdy »usuń« dla klasy »dowód«” |

Reguła silnika: **akcja pasuje do elementu, jeśli element ma cechy, których akcja wymaga.** Nowa akcja od razu
pasuje do wszystkich istniejących klas, które spełniają jej wymagania — bez ruszania definicji klas. Zasady potem
mówią, co z pasujących akcji jest domyślne, co wymaga potwierdzenia, a co jest wyłączone. Bez zasady akcja jest
dozwolona (R43: blokada musi mieć inicjatora); potwierdzenie ×10 i inne bezpieczniki z „ROM” obowiązują zawsze.

**„Łatwe do ekstrapolowania”** = silnik szuka łańcucha akcji, który dorobi brakującą cechę (np. nagranie →
„transkrybuj” → tekst → „podsumuj”). Progi — najdłuższy łańcuch, największy koszt, najniższa pewność — są
ustawieniami w grafie (presety, nie limity). Taki łańcuch silnik **proponuje** z kosztem; nie wykonuje go po cichu.

**Grupy akcji** służą do przeglądania (menu) i do zasad hurtem („wszystko, co wysyła poza urządzenie”), a nie do
decydowania, co pasuje. Twoje „flagi” to dokładnie flagi skutków; „akceptuje / wymaga / olewa” rozkłada się na
cechy (po stronie elementu), wymagania (po stronie akcji) i zasady (osobno, z zakresami).

Wpływ na plan: fala 2 = cechy + wymagania akcji + flagi skutków + zasady + akcja domyślna; fala 2b = ekstrapolacja
(szukanie łańcuchów z progami). Opis pól ustawień (S2/R2, „zasada edycji”) jest szczególnym przypadkiem zasad.
