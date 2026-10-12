# Przypadki graniczne „aż do przesady” — generator, niezmienniki, testy metamorficzne (T10, 2026-10-11)

Sesja S3 (Fable 5.1) według `docs/reports/THREADS_2026-10-11.md` §3 (T10) i §9.
Nadrzędny: `DUCH_GRAFU_2026-10-11.md` (§5 jest zalążkiem tego pliku). Wejścia:
`OWNER_REQUIREMENTS_2026-09-26.md` (R39–R53 jako przykłady kalibracyjne),
`GRAPH_ENTRY_PARTIES_ECONOMY_2026-10-10.md` (osie §11), `LOOM_CONCEPTUAL_MODEL.md`
(słownik: źródło, jednostka, obserwacja, twierdzenie, ocena, klasa dowodu,
niezmienniki I1–I10), `ACCEPTANCE_TESTS_2026-09-29.md` (forma kryteriów),
`CLAUDE.md` (zasada właściciela). Nie czytano transkryptów, dowodów ZIP ani kodu
poza nagłówkami wymienionymi w §3 (luki „dziś” pochodzą z inwentarza bramek
w `GRAPH_ENTRY…` §1 i z tabeli T1–T8 w `THREADS…`; są datowane na 2026-10-11
i tracą ważność w miarę zamykania pakietów WP-A…WP-G).

Pliki z sekcji 4 THREADS, których nie ma na gałęzi (`ecosystem-audit-2026-10-09/pass4`,
`data-in-code/thread-11.md`), pominięto; forma testów metamorficznych jest odtworzona
z `ACCEPTANCE_TESTS_2026-09-29.md` i z reguł pomiaru w notatce z 29.09 (§16).

## 0. Jak czytać

**Przypadek graniczny** w sensie właściciela (DUCH §5): nietypowa sytuacja, którą
silnik ma zinterpretować **mechanicznie** (preferencjami, zakresami, metodami),
osadzić w grafie i znaleźć nieoczywiste powiązania. Nie jest to test jednostkowy
i nie ma oracle’a („oczekiwany wynik” nie istnieje, THREADS §7.4). Dlatego każdy
przypadek niesie:

| Pole | Znaczenie |
|---|---|
| **Scenariusz** | wypowiedź lub sytuacja, jak ją poda właściciel (także z telefonu, z błędami ASR) |
| **Interpretacja** | co silnik musi zrozumieć, zanim cokolwiek zrobi |
| **W grafie** | gdzie to ląduje: jakie węzły, krawędzie, kwalifikatory (słownik LOOM_CONCEPTUAL_MODEL) |
| **Rządzi** | które ustawienia/preferencje i w jakim zakresie (łańcuch §6 GRAPH_ENTRY) |
| **Powiązania** | czego silnik ma szukać, w tym nieoczywistego |
| **Niezmiennik** | co nie może się zmienić, co musi być zachowane, co musi być wyjaśnialne — to jest treść testu |
| **MR** | relacja metamorficzna (§4): transformacja wejścia → wymagana relacja między wynikami |
| **Luka dziś / Łamie** | czy przypadek wyraża się w obecnych bytach; jeśli nie, którą abstrakcję z §3 łamie i dlaczego |

Oznaczenia: **Łamie A-n** = przypadek nie daje się wyrazić bez abstrakcji A-n
(§3); **Mieści się** = daje się wyrazić dzisiejszymi bytami, brakuje tylko
implementacji albo danych; **Częściowo** = wyraża się, ale bez jednego
kwalifikatora. Zgodnie z DUCH §5: *jeśli przypadek nie daje się wyrazić, brakuje
abstrakcji, nie przypadku*.

Identyfikatory: `EC-<grupa><nr>`. Grupy A–G to grupy z DUCH §5; H–J wyłoniły się
z operatorów przesady (§1) i nie mają odpowiednika w zalążku.

## 1. Generator: osie × operatory przesady

Przypadki nie są listą wymyśloną ad hoc. Powstały z osi §11 GRAPH_ENTRY
(podmioty, obszary wejścia, pakiety właściwości, ustawienia, samomodel, decyzja)
i z bytów DUCH §1, do których zastosowano **operatory przesady**. Operatory są
danymi tej metody; metoda N-torowa (R53, T11) ma je dostać jako prompt i
rozszerzać zbiór o nowe przypadki, porównując zgodność, nowość i sprzeczność
między modelami.

| Operator | Co robi z przykładem właściciela | Przykład |
|---|---|---|
| `scale` | ×1000 rozmiaru, liczby, powtórzeń | 2 GB plik → 50 000 rozmów → eksperyment na wszystkim |
| `negate` | odwraca polecenie lub preferencję | „ucz się” → „nie ucz się, ale nie kasuj” |
| `time_shift` | przesuwa w przeszłość, przyszłość, zmienia kolejność poznania | „co myślałem rok temu” → „co wiedziałem rok temu o tym, co stało się dwa lata temu” |
| `collectivize` | jeden podmiot → wielu, z wagami, rolami, zmianą składu | rodzina → rodzina po rozwodzie, decyzja sprzed zmiany |
| `split` | jedna rzecz → dwie (konto, urządzenie, magazyn, instalacja) | jedno konto → drugie konto, inna rola publiczna |
| `merge` | dwie rzeczy, które okazują się jedną | dwa eksporty tej samej rozmowy |
| `contradict` | dwa źródła, dwa podmioty, dwa tory mówią co innego | trzy modele, pokaż tylko niezgodności |
| `withhold` | brak informacji, brak zdolności, brak dostawcy | tor z modelem, który zniknął w połowie |
| `self_reference` | silnik działa na sobie | import własnej bazy; metoda ocenia metody |
| `noise` | błędy ASR, literówki, brak diakrytyków, urwane zdanie | „nie wysyłaj” usłyszane jako „wysyłaj” |
| `absurd_setting` | wartość ustawienia do granic (zasada właściciela) | budżet 1e9; wszystko automatyczne, ale wszystko widoczne |
| `irreversible` | skutek, którego nie da się cofnąć | klucz wyciekł; podpis na 20 lat; zgubione hasło |

Kompozycje operatorów dają grupy H–J. Każdy przypadek poniżej ma w nawiasie
operatory, z których powstał, żeby metoda N-torowa mogła generować dalej
według tej samej gramatyki.

## 2. Przypadki

### A. Pamięć i czas

#### EC-A01 — Zmiana zdania (`time_shift`)
**Scenariusz.** „Co myślałem o X rok temu, a co dziś, i kiedy zmieniłem zdanie?”
**Interpretacja.** Pytanie o dwa stany wiedzy w dwóch chwilach i o zdarzenie między nimi; „zmieniłem zdanie” to różnica między twierdzeniami podmiotu, nie między wiedzą systemu.
**W grafie.** Twierdzenia podmiotu o X z `valid_from/valid_to`, obserwacje z lokatorami, krawędź `supersedes` między twierdzeniami, decyzja (jeśli była) jako węzeł z datą.
**Rządzi.** Zakres i szczegółowość kontekstu (osoba → rozmowa); preferencja „pokazuj cytaty”.
**Powiązania.** Nieoczywiste: wydarzenie (commit, rozmowa z kimś, lektura) tuż przed zmianą; inne tematy, które zmieniły się w tym samym okresie.
**Niezmiennik.** Każde z dwóch stanowisk ma cytat z lokatorem; „zmiana” wskazuje najwcześniejszą obserwację nowego stanowiska; żadne stanowisko nie jest przedstawione jako obecne bez daty.
**MR.** MR-5 (cięcie czasu): zapytanie „jak na T−1 rok” = replay z grafu zbudowanego tylko ze źródeł ≤ T−1 rok.
**Luka dziś / Łamie.** Twierdzenia mają `valid_from/to`, ale nie `known_at`; bez tego „kiedy zmieniłem zdanie” miesza czas zdarzenia z czasem poznania. **Łamie A1.**

#### EC-A02 — Wiedza retroaktywna (`time_shift` × `contradict`)
**Scenariusz.** Dziś z importu wynika, że dwa lata temu podjąłem decyzję D, o której graf dotąd nie wiedział.
**Interpretacja.** Twierdzenie ważne od dawna, poznane dziś. Zapytanie „co wiedziałem rok temu” nie może go zawierać; zapytanie „co było prawdą rok temu” musi.
**W grafie.** Twierdzenie z `valid_from = −2 lata`, `known_at = dziś`, pochodzenie = import.
**Rządzi.** Ustawienie widoku czasu: domyślnie „co było prawdą”, przełączane na „co wiedziałem”.
**Powiązania.** Inferencje z zeszłego roku, które byłyby inne, gdyby D było znane: oznaczyć jako „do przeliczenia” (przepis).
**Niezmiennik.** Dwa widoki czasu dają różne odpowiedzi i oba są wyjaśnialne; stare inferencje nie są ciche nadpisane, lecz dostają `contested` lub status przeliczenia.
**MR.** MR-5 w dwóch osiach: cięcie po `valid` i po `known`.
**Łamie A1, A5** (brak `known_at`; brak grafu zależności „co zależało od braku D”).

#### EC-A03 — Zły dzień: nie ucz się, nie kasuj (`negate`)
**Scenariusz.** „Ta rozmowa była w złym dniu; nie ucz się z niej, ale jej nie kasuj.”
**Interpretacja.** Źródło zostaje obserwowalne i przeszukiwalne; przestaje być przesłanką inferencji i uczenia preferencji. To nie jest `excluded` (widoczność) ani `deleted`.
**W grafie.** Węzeł źródła z kwalifikatorem kwarantanny `learn: false` (zakres: ta rozmowa), inicjator `user`, data; pochodne twierdzenia, które mają to źródło w przesłankach, dostają status zawieszony.
**Rządzi.** Polityka uczenia per źródło (nowe ustawienie); stan podmiotu w chwili rozmowy (A12) jako przesłanka decyzji.
**Powiązania.** Inne rozmowy z tego dnia/okresu o podobnym stanie; preferencje, które powstały tylko z tej rozmowy.
**Niezmiennik.** Bajty źródła nienaruszone (I1); wyszukiwanie pełnotekstowe nadal trafia; żadna preferencja nie ma tego źródła jako jedynej przesłanki w stanie `active`; operacja odwracalna jednym ustawieniem.
**MR.** MR-14 (kwarantanna): zbiór aktywnych inferencji po = zbiór przed minus te z tym źródłem w domknięciu przesłanek; obserwacje identyczne.
**Łamie A4, A5** (statusy wiadomości `active|excluded|version|deleted` nie wyrażają „widoczne, ale nie przesłanka”; brak domknięcia „co ode mnie zależy”).

#### EC-A04 — Zły dzień odwołany (`negate` × `time_shift`)
**Scenariusz.** Po miesiącu: „jednak ucz się z tamtej rozmowy”.
**Interpretacja.** Zdjęcie kwarantanny; inferencje liczone bez niej są teraz niekompletne.
**W grafie.** Zdarzenie zdjęcia kwarantanny; przepisy z wejściem obejmującym to źródło oznaczone do przeliczenia; wyniki sprzed zdjęcia zachowane z `known_at`.
**Rządzi.** Tryb przeliczenia (automatyczny / propozycja / ręczny) jako ustawienie; strażnik ×10 na koszt przeliczenia.
**Niezmiennik.** Przeliczenie dotyka tylko domknięcia zależności od tego źródła; reszta grafu bajt w bajt bez zmian; historia obu stanów zachowana.
**MR.** MR-14 w obie strony (kwarantanna ∘ zdjęcie = identyczność na obserwacjach; na inferencjach deterministycznych identyczność bajtowa, MR-9).
**Łamie A5** (przeliczenie przyrostowe wymaga grafu zależności, nie tylko `produced_by`).

#### EC-A05 — Zapomnij na zawsze (`negate` × `irreversible`)
**Scenariusz.** „Zapomnij o T na zawsze i nigdy więcej nie pytaj.”
**Interpretacja.** `never` (R39) dla tematu, nie tylko slotu: nie pytać, nie wnioskować, nie importować do grafu wiedzy — ale archiwa są niezmienne (I1). Konflikt zasad do rozstrzygnięcia jawnie, nie po cichu.
**W grafie.** Węzeł tematu T ze stanem `never`, marker wykluczenia trwały (R40), zasięg: pytania, inferencje, selektor kontekstu, importy przyszłe; archiwa: obserwacje zostają w magazynie, ale są wyłączone z indeksów i widoków (`excluded_by_user`), opcjonalnie crypto-shredding kategorii (E10).
**Rządzi.** Zasięg `never` (co obejmuje) jako ustawienie z presetem; retencja archiwów.
**Powiązania.** Twierdzenia pośrednio ujawniające T (inferencja z korelacji) — mają zniknąć z aktywnych.
**Niezmiennik.** Po `never`: żadne zapytanie bez parametrów ani onboarding nie wraca do T; aktualizacja packa nie przywraca; eksport grafu nie zawiera T w twierdzeniach; log mówi, że T istnieje w archiwum i jest wykluczony (bez treści).
**MR.** MR-15 (wykluczenie przeżywa aktualizację); MR-10 (projekcja: payloady do dostawców nie różnią się od stanu, w którym T nigdy nie istniało).
**Łamie A13** (R40 ma wykluczenie dla domyślnych, nie dla tematów/slotów z zasięgiem do inferencji i importu).

#### EC-A06 — Pokaż, co o mnie wiesz, i pozwól poprawić (`self_reference`)
**Scenariusz.** „Pokaż mi wszystko, co o mnie wiesz, skąd to wiesz, i pozwól mi to poprawić.”
**Interpretacja.** Widok pakietu właściwości podmiotu z pochodzeniem każdego slotu; edycja = osąd właściciela (I4), który nie kasuje inferencji, tylko ją odrzuca z historią.
**W grafie.** Sloty ze stanami `unknown/declined/never`, wartości z `podane/wywnioskowane/potwierdzone/odrzucone`, zdarzenia edycji jako obserwacje klasy `user`.
**Rządzi.** Polityka przyjmowania propozycji (zapytaj / kandydat / automatycznie); widoczność inferencji odrzuconych.
**Powiązania.** Sloty wypełnione przez inferencję z jednego źródła (niska odporność); sloty sprzeczne między kontekstami.
**Niezmiennik.** Każdy slot ma pochodzenie z lokatorem albo „podane przez użytkownika dnia…”; po poprawce wartość `user` wygrywa, a odrzucona inferencja jest odtwarzalna w historii; odbudowa grafu odtwarza poprawkę (I4 replay).
**MR.** MR-9 (odbudowa po zdarzeniach użytkownika daje ten sam widok); MR-16 (wyjaśnienie → wartość).
**Mieści się** (R39 + onboarding „co aplikacja wie”); brakuje zakresów slotów (A3) i stanu podmiotu (A12) jako kolumn widoku.

#### EC-A07 — Rozmowa + commit + notatka z samochodu (`merge` × `noise`)
**Scenariusz.** „Połącz tę rozmowę z commitem, który z niej wyniknął, i z notatką głosową z samochodu.”
**Interpretacja.** Trzy źródła o różnych lokatorach (eksport, repo, nagranie z ASR), różnych zegarach (strefa, offline) i różnej jakości (ASR z błędami). „Wyniknął” to krawędź przyczynowa z pewnością, nie obserwacja.
**W grafie.** Jednostki trzech źródeł; krawędzie `derived_from` (commit ← rozmowa) jako inferencja z oczekiwaną własnością („commit zmienia pliki wymienione w rozmowie”); notatka z n-best ASR (A11); czas notatki z kwalifikatorem „zegar urządzenia, bez sieci”.
**Rządzi.** Pochodzenie krawędzi użytkownika (`user` wygrywa, ale krawędź zapisana jako asercja użytkownika, nie obserwacja); polityka ASR (zachować surowy tekst i alternatywy).
**Powiązania.** Inne commity w oknie czasu; inne notatki z tego samego dnia; różnica między treścią rozmowy a tym, co commit faktycznie zrobił (sprzeczność).
**Niezmiennik.** Trzy lokatory rozwiązują się do bajtów ze zgodnym hashem; krawędź użytkownika ma klasę `user`, krawędź silnika `inferred` z własnością sprawdzalną; surowy ASR nigdy nie jest nadpisany interpretacją.
**MR.** MR-3 (podział: notatka jako osobny plik vs załącznik rozmowy daje te same jednostki); MR-4 (szum ASR nie zmienia krawędzi, zmienia pewność).
**Łamie A11** (wiadomość = jeden napis; brak reprezentacji wypowiedzi z alternatywami i pewnością).

#### EC-A08 — Przypomnij w przyszłości (`time_shift`)
**Scenariusz.** „Za pół roku przypomnij, że mam zweryfikować decyzję D.”
**Interpretacja.** Twierdzenie o przyszłym zdarzeniu + wyzwalacz (R33) o zakresie osoby; nie jest to alarm w kodzie, lecz węzeł z datą i metodą wykonania.
**W grafie.** Węzeł zadania z `valid_from = +6 mies.`, krawędź `about` → D, wyzwalacz jako ustawienie z inicjatorem `user`.
**Rządzi.** Dozwolone skutki wyzwalaczy (R33: żadnych pętli wydatków); kanał przypomnienia.
**Powiązania.** Nowe obserwacje o D przed terminem (przypomnienie może stać się bezprzedmiotowe → propozycja, nie decyzja).
**Niezmiennik.** Wyzwalacz nie wykonuje nic płatnego bez budżetu; przypomnienie cytuje D i obserwacje, które pojawiły się od chwili ustawienia; usunięcie D nie gubi przypomnienia (dostaje `contested`).
**MR.** MR-5 (przesunięcie zegara systemu o +6 mies. w teście odpala dokładnie ten jeden wyzwalacz).
**Mieści się** (R33 + decyzja jako węzeł); brakuje wyzwalaczy jako danych w zakresach (A3).

#### EC-A09 — Zegar kłamie (`noise` × `time_shift`)
**Scenariusz.** Eksport z telefonu, którego zegar stał na 1970; notatki z innej strefy czasowej; dwie wiadomości z identycznym znacznikiem.
**Interpretacja.** Czas deklarowany w źródle ≠ czas zdarzenia; silnik nie poprawia źródła, tylko dodaje twierdzenie o czasie z pewnością.
**W grafie.** Obserwacja czasu (surowa) + twierdzenie `occurred_at` klasy `inferred` z własnością `date_between` wyprowadzoną z sąsiadów (kolejność w eksporcie, odwołania do wydarzeń).
**Rządzi.** Preferencja strefy czasowej per urządzenie/instalacja; polityka rozstrzygania kolejności przy remisie (deterministyczna, jawna, w Decisions `loom/README.md`).
**Powiązania.** Inne źródła z tego samego urządzenia w tym samym okresie (ta sama wada zegara → jedna poprawka dla wielu).
**Niezmiennik.** Surowy znacznik zachowany; kolejność wiadomości w rozmowie niezmienna niezależnie od poprawki czasu; poprawka ma własność sprawdzalną i może zostać `violated`.
**MR.** MR-2 (permutacja plików eksportu nie zmienia kolejności w rozmowie); MR-9.
**Mieści się** (`inferred` + Expected Property `date_between`).

#### EC-A10 — Edycja sprzed roku (`time_shift` × `split`)
**Scenariusz.** Właściciel edytuje swoją wiadomość sprzed roku; powstaje gałąź (R14).
**Interpretacja.** Stara gałąź zostaje źródłem; inferencje z niej nie znikają; nowa gałąź dostaje własne przebiegi. Pytanie „co myślałem rok temu” wskazuje starą gałąź, bo nowa ma `known_at = dziś`.
**W grafie.** Grupa wersji; krawędź `forked_from`; twierdzenia z gałęzi A i B z kwalifikatorem gałęzi; `known_at` edycji.
**Rządzi.** Czy gałąź historyczna jest domyślnie przeszukiwana (ustawienie zakresu).
**Powiązania.** Twierdzenia, które różnią się tylko gałęzią (diff semantyczny), nie tekstowo.
**Niezmiennik.** Stara gałąź bajt w bajt; żaden przebieg metody nie miesza przesłanek z dwóch gałęzi bez jawnego kwalifikatora.
**MR.** MR-8 (dodanie gałęzi nie usuwa twierdzeń); MR-5.
**Częściowo** (wersje istnieją; `known_at` nie, A1).

### B. Podmioty i decyzje

#### EC-B01 — Rodzina ważąca przeprowadzkę (`collectivize`)
**Scenariusz.** „Rodzina decyduje o przeprowadzce: zbierz za i przeciw z naszych rozmów, mama ma wagę 2, dzieci głos doradczy, pokaż, gdzie się nie zgadzamy.”
**Interpretacja.** Kolektyw doraźny z rolami i wagami; przesłanki z wielu źródeł różnych podmiotów; „nie zgadzamy się” = sprzeczność między twierdzeniami podmiotów, nie błąd.
**W grafie.** Podmiot `rodzina` (grupa, doraźna, więź rodzinna, kontekst decyzji); `member_of` z rolami; `decides_with(weight=2)`; decyzja jako węzeł z alternatywami, przesłankami per podmiot, mapą niezgody.
**Rządzi.** Reguła konfliktu `weighted_vote` na zakresie tej decyzji; prywatność dzieci (czyje rozmowy wolno czytać komu).
**Powiązania.** Przesłanki, których nikt nie powiedział wprost (koszty szkoły z innego projektu); zmiana zdania członka w czasie (EC-A01 per podmiot).
**Niezmiennik.** Każde „za/przeciw” ma podmiot, cytat i lokator; waga nie zmienia treści przesłanek, tylko agregację; mapa niezgody jest symetryczna (jeśli A≠B, to B≠A); głos doradczy jest widoczny, lecz nie liczy się w agregacie — i to jest wyjaśnione.
**MR.** MR-19 (zmiana wagi po decyzji nie zmienia zapisanej decyzji); MR-2 (kolejność rozmów nie zmienia agregatu).
**Łamie A2, A16, A22** (brak podmiotów wielopoziomowych, decyzji z wagami uczestników i sprzeczności między podmiotami jako bytu).

#### EC-B02 — Waga zależna od rodzaju decyzji (`collectivize` × `absurd_setting`)
**Scenariusz.** „Mama ma wagę 2 w finansach, 1 w szkole dzieci, a w wakacjach decyduje młodsze dziecko.”
**Interpretacja.** `decides_with` ma wagę per rodzaj decyzji (klasa), nie globalną.
**W grafie.** Krawędź `decides_with` z kwalifikatorem `decision_kind`; klasy decyzji jako węzły ontologii użytkownika (J01).
**Rządzi.** `weighted_vote` z wagami rozstrzyganymi przez zakres (rodzaj decyzji → kolektyw → osoba).
**Niezmiennik.** Zmiana wagi w jednym rodzaju nie zmienia agregatów w innych; wyjaśnienie agregatu nazywa rodzaj i wagi.
**MR.** MR-6 (nadpisanie w zakresie rodzaju decyzji).
**Łamie A2, A3** (wagi per rodzaj = ustawienie w łańcuchu zakresów, którego nie ma).

#### EC-B03 — Dziecko dorasta (`time_shift` × `collectivize`)
**Scenariusz.** Dziecko kończy 18 lat; „od teraz pełny głos”. Decyzja z 2024 była z głosem doradczym.
**Interpretacja.** Rola w kontekście ma czas; historyczna decyzja wskazuje rolę z tamtej chwili.
**W grafie.** `acts_as` z `valid_from`; decyzja → rola@czas, nie → osoba.
**Niezmiennik.** Przeliczenie decyzji z 2024 po zmianie roli daje identyczny wynik (MR-19); nowe decyzje liczą nową wagę; obie wersje roli widoczne.
**Łamie A2** (rola jako węzeł z czasem, nie etykieta).

#### EC-B04 — Formalny w pracy, nieformalny w domu, po angielsku w projekcie (`split`)
**Scenariusz.** „W pracy jestem formalny, w domu nie; w tym projekcie piszę po angielsku.” Potem: projekt firmowy omawiany w domu w niedzielę.
**Interpretacja.** Preferencje stylu i języka z regułą `by_context`; kontekst to koniunkcja (rola × projekt × miejsce × czas), której predykat może dać dwa wyniki naraz.
**W grafie.** Sloty `komunikacja.styl`, `komunikacja.język` z wartościami w zakresach rola/projekt; predykaty kontekstu jako dane.
**Rządzi.** Reguła konfliktu dla przecięcia kontekstów (`ask` raz, potem zapisane nadpisanie na zakresie „projekt X w domu”).
**Powiązania.** Historyczne rozmowy, w których właściciel sam zmienił styl w takim przecięciu (dowód preferencji).
**Niezmiennik.** Wartość efektywna ma wyjaśnienie z nazwą predykatu i zakresu; przecięcie nierozstrzygnięte nie wybiera cicho — informuje i wykonuje domyślne, zapisując pytanie.
**MR.** MR-6, MR-16.
**Łamie A3** (dwie warstwy R40 nie wyrażają `by_context` ani przecięcia).

#### EC-B05 — Drugie konto, ta sama osoba, inna rola publiczna (`split` × `irreversible`)
**Scenariusz.** „To moje drugie konto; ta sama osoba, inna rola publiczna.” Potem: „nie łącz ich w żadnym widoku, który mogę komuś pokazać”.
**Interpretacja.** Tożsamość osoby ≠ tożsamość konta; `same_as` byłoby błędem (R35); wiedza o powiązaniu jest sama w sobie wrażliwa.
**W grafie.** Osoba —`acts_as`→ persona A, persona B; krawędź powiązania z kategorią wrażliwości „tożsamość”; widoki z projekcją, która tę krawędź usuwa.
**Rządzi.** Prywatność kategorii „powiązanie kont” (never do dostawców; widoczność per widok).
**Powiązania.** Twierdzenia o personie B, które przez styl/zdarzenia zdradzają personę A (inferencja do zgłoszenia, nie do zapisania jako fakt).
**Niezmiennik.** Projekcja „do pokazania” nie zawiera żadnej ścieżki A↔B (test grafowy: brak drogi); scalenie nigdy nie następuje; eksport persony B nie zawiera twierdzeń persony A.
**MR.** MR-10 (projekcja prywatności: różnica między widokiem pełnym a pokazywalnym = dokładnie krawędzie kategorii „tożsamość” i to, co tylko przez nie osiągalne).
**Łamie A9** (spektrum tożsamości poza `same_as`/lineage).

#### EC-B06 — Agent GPT jako członek zespołu, nieufany w kosztach (`collectivize` × `withhold`)
**Scenariusz.** „Agent GPT z wczoraj zrobił to i to; traktuj go jak członka zespołu, ale nie ufaj mu w sprawach kosztów.”
**Interpretacja.** Podmiot typu agent AI z zaufaniem per rodzaj twierdzenia; jego raporty są obserwacjami o niższym autorytecie w dziedzinie „koszt”.
**W grafie.** Podmiot `agent:gpt-thread-N`; `trusts(kind=cost, weight=0)`; twierdzenia agenta z `origin=system`/`model_knowledge` i podmiotem autora.
**Rządzi.** Autorytet w kolizji twierdzeń (LOOM §2.3) rozszerzony o podmiot autora i rodzaj.
**Powiązania.** Twierdzenia agenta o kosztach sprzeczne z rejestrem zużycia (W2) — rejestr wygrywa i jest to wyjaśnione.
**Niezmiennik.** Twierdzenia agenta o kosztach nigdy nie są jedyną przesłanką decyzji budżetowej; raport ekonomii cytuje rejestr, nie agenta; zaufanie w innych dziedzinach nienaruszone.
**MR.** MR-6 (zmiana zaufania w jednej dziedzinie nie zmienia innych).
**Łamie A2** (`trusts` per rodzaj jako krawędź podmiotów).

#### EC-B07 — „Ja” w cudzym eksporcie (`merge` × `split`)
**Scenariusz.** Partnerka udostępnia eksport swoich rozmów, w których „ja” to ona, a właściciel jest osobą wspomnianą.
**Interpretacja.** Rola `user` w źródle ≠ właściciel instalacji; podmiot *o którym* ≠ podmiot *który działa*.
**W grafie.** Źródło z `author_party = partnerka`; wiadomości `role=user` przypisane do niej; wzmianki o właścicielu jako `mentions`; preferencje wyciągnięte z tego źródła przypisane partnerce, nie właścicielowi.
**Rządzi.** Zgoda partnerki (kategoria prywatności „cudze dane”), zakres uczenia preferencji per podmiot źródła.
**Powiązania.** Te same wydarzenia widziane z dwóch stron (sprzeczności między relacjami = A22).
**Niezmiennik.** Żadna preferencja właściciela nie powstaje z wypowiedzi partnerki; żadna wiadomość partnerki nie jest wysłana do dostawcy bez reguły prywatności jej podmiotu.
**MR.** MR-10 na podmiocie źródła; MR-11 (zmiana etykiety autora nie zmienia przypisań).
**Łamie A2** (importer przyjmuje, że `user` = użytkownik instalacji).

#### EC-B08 — Organizacja przypina wartość, osoba chce inaczej (`contradict` × `absurd_setting`)
**Scenariusz.** Firma `locked_by` „dostawcy: tylko Azure”; właściciel w tym samym projekcie chce Claude przez OpenRouter.
**Interpretacja.** Blokada z inicjatorem `user@organizacja` (R43); nie jest to limit silnika; wyjście: zmiana zakresu (projekt prywatny) albo prośba o wyjątek — oba widoczne.
**W grafie.** Ustawienie z wartością na zakresie organizacji, reguła `locked_by`, inicjator; próba nadpisania zapisana jako odrzucona z wyjaśnieniem.
**Rządzi.** `locked_by` + `has_authority_over`.
**Niezmiennik.** Wynik operacji zawiera `initiator` i `reason`; nic nie jest po cichu ominięte ani po cichu zablokowane; w innym zakresie (projekt prywatny) ta sama operacja przechodzi.
**MR.** MR-6 (przeniesienie rozmowy do zakresu bez blokady zmienia wynik dokładnie w tym polu); MR-16.
**Łamie A3, A14**.

#### EC-B09 — Kolektyw się rozpadł (`collectivize` × `time_shift` × `irreversible`)
**Scenariusz.** Decyzja rodzinna z 2024; w 2026 rodzina zmieniła skład (rozwód). „Pokaż decyzję i kto za nią odpowiadał.”
**Interpretacja.** Decyzja wskazuje kolektyw w składzie z chwili decyzji; obecny skład to inny węzeł lub ten sam z historią członkostwa.
**W grafie.** `member_of` z `valid_to`; decyzja → kolektyw@2024.
**Niezmiennik.** MR-19: zmiana składu nie zmienia zapisanej decyzji ani jej wag; widok „kto dziś” i „kto wtedy” rozróżnione; prywatność byłego członka (jego cytaty) według jego reguł, nie reguł kolektywu.
**Łamie A2, A16**.

#### EC-B10 — Ja dziś, ja zwykle (`collectivize` wewnątrz jednej osoby)
**Scenariusz.** „Dziś krótko i konkretnie, mam zły dzień.” (ADHD jako stan, nie cecha.)
**Interpretacja.** Stan podmiotu w tej chwili zmienia preferencje selektora i formy odpowiedzi na zakresie rozmowy/dnia; nie zmienia preferencji „zwykle”.
**W grafie.** Slot `komunikacja.długość` z osią czasu (teraz/zwykle), stan podmiotu jako kwalifikator wypowiedzi (A12); twierdzenia z tej rozmowy z kwalifikatorem stanu.
**Rządzi.** Zakres „tura/dzień” nad „osoba”; `most_specific_wins`; czas wygaśnięcia stanu jako ustawienie.
**Powiązania.** Korelacja stanu z porą, dniem tygodnia, poprzednimi rozmowami (propozycja wzorca, EC-C04).
**Niezmiennik.** Po wygaśnięciu zakresu wartość wraca do „zwykle” bez śladu w „zwykle”; preferencje wyuczone z rozmów w stanie S są oznaczone S i ważone niżej w uczeniu „zwykle”.
**MR.** MR-6 z wygaśnięciem; MR-14 (stan jako łagodna kwarantanna).
**Łamie A12, A17** (stan podmiotu w chwili podania; zasób „uwaga” jako byt).

### C. Metody, tory, eksperymenty

#### EC-C01 — Pięć promptów na fragment (`scale` podstawowy)
**Scenariusz.** „Puść ten fragment przez pięć promptów i powiedz, który najlepiej wyciąga decyzje z moich notatek, i ile to kosztowało.”
**Interpretacja.** Eksperyment 5-torowy; kryterium „najlepiej wyciąga decyzje” = ugruntowanie + pokrycie + zgodność, nie gust.
**W grafie.** Eksperyment jako węzeł; 5 torów (metoda, wersja promptu z hashem, model, parametry, budżet); przebiegi z `produced_by`; oceny wielokryterialne; rekomendacja jako propozycja presetu.
**Rządzi.** Podgląd kosztu, ×10; kryteria oceny jako dane; polityka przyjęcia presetu (zakres).
**Powiązania.** Poprzednie eksperymenty na podobnych fragmentach (dowody metod, R48).
**Niezmiennik.** Koszt = suma torów (MR-13); kolejność torów nieistotna (MR-12); każda „decyzja” wyciągnięta przez tor ma cytat sprawdzalny w fragmencie albo jest oznaczona „bez cytatu”; rekomendacja nie zmienia żadnego ustawienia bez przyjęcia.
**Łamie A6** (brak toru jako danych, eksperymentu jako węzła i oceny wielokryterialnej; P5 ExperimentSpec otwarte).

#### EC-C02 — Trzy modele, tylko niezgodności (`contradict`)
**Scenariusz.** „Zapytaj trzy modele o to samo i pokaż tylko to, w czym się nie zgadzają.”
**Interpretacja.** Odpowiedzi w formie grafowej, porównanie na twierdzeniach, nie na tekście; „niezgoda” = sprzeczne twierdzenia albo twierdzenie obecne u jednego, nieobecne u innych (dwa rodzaje, pokazane osobno).
**W grafie.** Trzy pakiety odpowiedzi; krawędzie `contradicts` i `only_in` między nimi; widok różnicy.
**Rządzi.** Które modele (preset), prywatność per dostawca (trzy różne egressy), koszt.
**Niezmiennik.** Zbiór zgodny + zbiór niezgodny + zbiory „tylko u X” = suma odpowiedzi (bez strat); niezgoda jest symetryczna; żadne twierdzenie modelu nie wchodzi do wiedzy bez przyjęcia.
**MR.** MR-12; MR-20 (grafowa vs liniowa); MR-10 ×3.
**Łamie A6, A22**.

#### EC-C03 — Edytowałem prompt; przelicz tylko to, co się zmieniło (`self_reference` × `scale`)
**Scenariusz.** Zmiana jednego zdania w prompcie analizy używanym w 20 000 przebiegów.
**Interpretacja.** Nowa wersja metody (hash); stare wyniki ważne pod starą wersją; przeliczenie to przepis, nie dane; koszt może być ×1000 → strażnik.
**W grafie.** Metoda → wersja v2; przebiegi v1 zostają; zbiór „do przeliczenia” = domknięcie wejść zależnych od v1 (A5); podgląd kosztu.
**Rządzi.** Tryb przeliczenia (nic / propozycja / automatycznie w budżecie), ×10, próbka najpierw (eksperyment, EC-C01 na 50 przebiegach).
**Niezmiennik.** Żaden wynik v1 nie zostaje nadpisany; każdy wynik ma wersję; ten sam wynik przy identycznym wejściu i wersji nie jest liczony dwa razy (cache, MR-1); podgląd kosztu ≥ 0 i wyjaśnialny (ile przebiegów × średnia).
**MR.** MR-1, MR-9, MR-13.
**Łamie A5** (graf zależności i przeliczenie przyrostowe).

#### EC-C04 — Wzorce, których nie nazwałem (`self_reference` × `withhold`)
**Scenariusz.** „Znajdź w archiwach wzorce, których jeszcze nie nazwałem.”
**Interpretacja.** Węzły bez nazwy klasy: skupienia twierdzeń/struktur (R22–R23) z pewnością, z propozycją nazwy jako osobnym twierdzeniem modelu.
**W grafie.** Węzeł `pattern` ze statusem `unnamed`, członkowie z wagami, propozycje nazw z pochodzeniem modelu; po nazwaniu przez użytkownika: klasa użytkownika (J01).
**Rządzi.** Metoda (strukturalna, wektorowa, model frontier), budżet, polityka przyjęcia.
**Powiązania.** Wzorzec obecny w wielu projektach (uniwersalność vs szczegółowość, R21).
**Niezmiennik.** Wzorzec bez nazwy jest pełnoprawnym węzłem (da się do niego odwołać, zapytać, odrzucić); nazwa modelu nigdy nie staje się nazwą klasy bez osądu użytkownika; odrzucenie wzorca nie kasuje jego członków.
**MR.** MR-11 (nazwanie nie zmienia członków); MR-8.
**Łamie A21**.

#### EC-C05 — Odpowiedz grafem / tekstem, ale graf zapisz (`split` wyjścia)
**Scenariusz.** „Odpowiedz grafem, nie tekstem.” / „Odpowiedz tekstem, ale graf i tak zapisz.”
**Interpretacja.** Forma wyjścia dla człowieka i forma dla grafu to dwa niezależne pokrętła (DUCH §3.4); druga zawsze działa, pierwsza jest preferencją.
**W grafie.** Odpowiedź liniowa (obserwacja modelu) + pakiet grafowy osadzony z pochodzeniem; krawędź `rendered_from`.
**Rządzi.** Tryb wyjścia (zakres rozmowy/tury), polityka osadzania (ask/candidate/auto).
**Niezmiennik.** MR-20: każde twierdzenie liniowe ma odpowiednik grafowy albo oznaczenie „bez źródła”; wyłączenie widoku tekstu nie zmienia pakietu; pakiet jest ten sam niezależnie od trybu widoku (bajtowo, przy tym samym przebiegu).
**Częściowo** (chat-graph istnieje; brak trybu „graf jako jedyne wyjście” i gwarancji MR-20 — A19).

#### EC-C06 — Jev tylko po krawędziach typu Y (`withhold` zakresu)
**Scenariusz.** „Jev ma oceniać relewancję tego twierdzenia dla tego węzła tylko po krawędziach typu Y.”
**Interpretacja.** Jev uogólniony (twierdzenie × węzeł × rodzaj krawędzi); ograniczenie rodzaju to parametr presetu, inicjator `user`.
**W grafie.** Przebieg Jev z parametrem `edge_kinds=[Y]`; wynik jako ocena relewancji, nie prawdy (LOOM §11.9).
**Niezmiennik.** Wynik nazwany `relevance_to_query`; zmiana zbioru rodzajów zmienia tylko przebiegi z tym parametrem; relewancja po Y nie jest promowana do obserwacji.
**MR.** MR-6; monotoniczność: Y ⊂ Y′ → zbiór kandydatów(Y) ⊆ zbiór kandydatów(Y′).
**Mieści się** (preset per rodzaj krawędzi, GRAPH_ENTRY §7).

#### EC-C07 — Model zniknął w połowie eksperymentu (`withhold`)
**Scenariusz.** Tor 3 z 5 używa modelu, który dostawca wycofał po 40 % przebiegów.
**Interpretacja.** `invariant:external`; eksperyment częściowy jest dowodem, nie śmieciem; tor 3 ma status `incomplete` z przyczyną.
**W grafie.** Przebiegi wykonane zostają; tor z `capability: unavailable`; ocena z adnotacją o pokryciu.
**Niezmiennik.** Koszt = suma wykonanych (MR-13); ocena porównawcza wyklucza tor 3 jawnie, nie cicho; wznowienie z innym modelem to nowy tor, nie podmiana.
**MR.** MR-12 na torach kompletnych; MR-8.
**Łamie A6, A14** (stan `incomplete` z inicjatorem jako część toru).

#### EC-C08 — Krytyk i obrońca (`collectivize` torów)
**Scenariusz.** „Tor A odpowiada, tor B go krytykuje, tor C broni, potem ja.” Użytkownik wymyśla własny tryb.
**Interpretacja.** Kompozycja torów to dane (język torów): sekwencja, zależności, kryteria; silnik nie ma tego trybu „pod przypadek”.
**W grafie.** Eksperyment z grafem torów (DAG), krawędzie `input_of`; użytkownik jako tor ostatni (ocena ręczna).
**Niezmiennik.** DAG torów jest walidowany (brak cykli) przed wyceną; koszt podglądu = suma po DAG-u; każdy tor cytuje wejścia; zapis trybu jako preset do ponownego użycia.
**MR.** MR-13; MR-2 na torach niezależnych; MR-9 (odtworzenie z zapisanych odpowiedzi bez nowych wywołań).
**Łamie A6** (język opisu torów).

#### EC-C09 — Eksperyment na wszystkim (`scale`)
**Scenariusz.** 10 000 rozmów × 5 torów; właściciel: „jedź”.
**Interpretacja.** Wzrost ×10 względem bazy → stan `requires_confirmation`, nie blokada; po potwierdzeniu: wykonanie w budżecie, odporne na przerwanie, wznawialne.
**W grafie.** Eksperyment z postępem; pokwitowania zużycia per tor; potwierdzenie jako zdarzenie z inicjatorem.
**Rządzi.** Strażnik ×10 (`owner_rule`), budżet (zakres osoby/projektu), kolejność (próbka warstwowa najpierw jako preset).
**Niezmiennik.** Podgląd przed wysyłką; potwierdzenie raz na skok, nie na każdy przebieg; przerwanie w dowolnym miejscu nie gubi wykonanych; wznowienie nie wysyła ponownie (idempotencja operacji, W2).
**MR.** MR-13; MR-1 na wznowieniu.
**Częściowo** (W2 + usage_policy istnieją; strażnik nie obejmuje wszystkich ścieżek — T6; A14/A15).

#### EC-C10 — Dwanaście powtórek (`scale` × `contradict`)
**Scenariusz.** Ten sam prompt, model i wejście, 12 razy; „jak stabilna jest ta metoda?”
**Interpretacja.** Stabilność to kryterium (R48); odpowiedzi niedeterministyczne zostają wszystkie (R50: ich jedynym opisem jest ona sama); powtórki nie są niezależnymi przypadkami (reguły pomiaru §16).
**W grafie.** 12 przebiegów tej samej wersji; metryka stabilności jako twierdzenie o metodzie z dowodem.
**Niezmiennik.** Cache nie zwraca pierwszej odpowiedzi jako drugiej (MR-1 dotyczy deterministycznych; dla niedeterministycznych idempotencja dotyczy *operacji*, nie *wyniku*); liczba niezależnych przypadków w raporcie = 1.
**Mieści się**; wymaga rozróżnienia determinizmu metody jako danych (A5).

#### EC-C11 — Preset przyjęty, a badanie mówi „gorszy” (`contradict` × `self_reference`)
**Scenariusz.** Właściciel przyjął preset P na projekcie X; nowe badanie rekomenduje Q.
**Interpretacja.** Rekomendacja to twierdzenie o metodzie; ustawienie to osąd użytkownika (I4). Rekomendacja się zmienia, ustawienie nie — ale użytkownik widzi rozjazd.
**W grafie.** Rekomendacja v2 → Q; ustawienie → P z inicjatorem `user`; krawędź `diverges_from` między nimi.
**Niezmiennik.** Żadne badanie nie zmienia ustawienia; widok wejściowy (obszar Metody) pokazuje liczbę rozjazdów `{known:true}`; przyjęcie Q to nowe zdarzenie z datą.
**Mieści się** (R41, R48); brak licznika rozjazdów w widoku (T3).

#### EC-C12 — Metoda ocenia metody (`self_reference`)
**Scenariusz.** Sędzia (model) ocenia 5 torów; potem: „a kto ocenia sędziego?”
**Interpretacja.** Ocena oceny; profil sędziego (R37) z danymi; lepszy wynik sędziego nie zmienia faktów (ACCEPTANCE #10).
**W grafie.** Metoda `judge` z profilem; oceny jako twierdzenia o przebiegach z `produced_by = judge@v`; ablacja z/bez sędziego jako eksperyment.
**Niezmiennik.** Wymiana sędziego nie zmienia odpowiedzi torów (bajtowo); zmienia tylko oceny; regres sędziego wykrywalny na zamrożonym zbiorze.
**MR.** MR-12; MR-9.
**Mieści się** (R37, R41).

### D. Dane, import, duplikaty, skala

#### EC-D01 — Dwa eksporty, częściowo te same rozmowy (`merge`)
**Scenariusz.** „Zaimportuj eksport z OpenAI i Anthropic; to częściowo te same rozmowy; nie dubluj, ale nic nie trać.”
**Interpretacja.** Dwa źródła, dwa lokatory; „ta sama rozmowa” to inferencja (podobieństwo treści, czas) z własnością sprawdzalną, nie identyczność bajtów; „nie dubluj” = jedna jednostka logiczna z dwoma lokatorami, „nic nie trać” = oba źródła nienaruszone, różnice (metadane, załączniki, ucięcia) zachowane.
**W grafie.** Dwa węzły `export:member`; węzeł rozmowy logicznej z krawędziami `projection_of` do obu (R35: rzut, nie `same_as`); różnice jako twierdzenia `differs_in`.
**Rządzi.** Preset importu `reimport_and_link` / `reuse_prior` / `reimport` (T1); próg podobieństwa jako preset; polityka przyjęcia linków (ask/candidate/auto).
**Powiązania.** Rozmowa obecna w jednym eksporcie, a w drugim ucięta (R30: nie zmyślać brakującej części).
**Niezmiennik.** Suma jednostek po imporcie = suma z obu źródeł (MR-8); liczba rozmów logicznych ≤ suma; każdy link ma pewność i własność; `force` tworzy kopię z adnotacją, nigdy cichą.
**MR.** MR-3 (eksport A+B jako jeden ZIP vs dwa pliki → te same jednostki i linki); MR-2 (kolejność importu nie zmienia linków); MR-7.
**Częściowo** (T1 w toku; `projection_of` brak — A9, A10).

#### EC-D02 — Nieznany format (`withhold`)
**Scenariusz.** „Tego formatu nie znasz — i tak go weź, pokaż mi, co z niego zrozumiałeś.”
**Interpretacja.** R44: status `unrecognized`/`partial`; „co zrozumiałeś” = to, co rozpoznały metody ogólne (tekst, daty, adresy, struktura JSON) bez adaptera.
**W grafie.** Węzeł źródła ze statusem rozpoznania, bajty w BlobStore, próby rozpoznania z wersją; twierdzenia ogólne z klasą `derived` (daty, liczby) albo `inferred` (role).
**Rządzi.** Które metody ogólne uruchamiać na nierozpoznanym (preset), budżet (model na nieznanym = koszt).
**Powiązania.** Podobieństwo struktury do znanych formatów (morfizm częściowy) → propozycja adaptera.
**Niezmiennik.** Nic nie jest pominięte (liczba bajtów wejścia = liczba bajtów zachowanych); każde twierdzenie z nierozpoznanego ma klasę ≤ `inferred`; nowy adapter później rozpoznaje bez utraty adnotacji użytkownika (EC-D10).
**MR.** MR-1; MR-8; MR-9 (ponowne rozpoznanie z tym samym adapterem daje identyczny wynik).
**Częściowo** (T1; A10).

#### EC-D03 — Plik 2 GB: nie czytaj, powiedz, co w nim jest (`scale` × `withhold`)
**Scenariusz.** „Ten plik ma 2 GB; nie czytaj wszystkiego, powiedz, co w nim jest i co by kosztowało.”
**Interpretacja.** Katalog bez importu (R1): strumieniowanie, jednostki z lokatorami i szkicami, wycena per metoda; „nie czytaj” to budżet, nie zakaz.
**W grafie.** Źródło + jednostki katalogu (bez treści) + twierdzenia o rozmiarach/typach + wycena jako twierdzenie o koszcie z metodą.
**Rządzi.** Budżet katalogowania (CPU/czas, lokalne), presety selekcji (R18), ×10 na wycenie modelowej.
**Powiązania.** Jednostki podobne do już zaimportowanych (duplikaty przed importem, R50).
**Niezmiennik.** Pamięć szczytowa niezależna od rozmiaru pliku (strumień); wycena ma rozbicie per metoda i `{known:false}` tam, gdzie brak profilu kosztu; żaden bajt treści nie trafia do dostawcy na etapie katalogu.
**MR.** MR-7 (plik ×2 przez konkatenację → liczniki ×2, wycena ×2, pamięć stała); MR-3.
**Mieści się** (katalog istnieje; wycena per metoda wymaga A15).

#### EC-D04 — Telefon i laptop to jeden graf (`split` instalacji)
**Scenariusz.** „Dane z telefonu i z laptopa to jeden graf; konflikt rozstrzygaj tak, jak ustawiłem.”
**Interpretacja.** Dwie instalacje piszą niezależnie; scalanie według reguły (dane); konflikt to sprzeczność w grafie, nie nadpisanie.
**W grafie.** Instalacja jako podmiot; zdarzenia z identyfikatorem instalacji i zegarem logicznym; scalone twierdzenia z dwoma pochodzeniami; konflikty `contested` z regułą rozstrzygnięcia jako ustawieniem.
**Rządzi.** Reguła scalania per kategoria (ostatni wygrywa / zapytaj / obie wersje / instalacja X ma pierwszeństwo), zakres instalacji w łańcuchu.
**Powiązania.** Ta sama rozmowa kontynuowana na dwóch urządzeniach (gałęzie, nie duplikaty).
**Niezmiennik.** MR-18: scalenie przemienne i łączne; żadne zdarzenie nie ginie; identyfikatory treściowe (I5) dają te same węzły dla tych samych treści po obu stronach; reguła scalania jest wyjaśniona przy każdym konflikcie.
**Łamie A18** (brak instalacji jako podmiotu i zegara logicznego; DB jednoprocesowa z RLock).

#### EC-D05 — Ten sam eksport trzy razy, raz z `force` (`scale` × `merge`)
**Scenariusz.** Właściciel importuje ten sam ZIP w poniedziałek, wtorek i (z `force`) w środę.
**Interpretacja.** Import 2 → zwrot poprzedniego (R50, identyczny wynik); import 3 z `force` → kopia celowa; raport ekonomii proponuje link.
**W grafie.** Jedno źródło (hash), trzy zdarzenia importu; kopia z `force` jako węzeł z krawędzią `duplicate_of` i propozycją linkowania (koszt/korzyść).
**Niezmiennik.** MR-1 (import 2 nie zmienia grafu poza zdarzeniem); MR-7 (po `force`: bajty źródła raz, metadane ×2, raport ekonomii pokazuje dokładnie tę nadwyżkę); propozycja nie wykonuje się sama w trybie `report`.
**Mieści się** (bramka importu + T4).

#### EC-D06 — Eksport w eksporcie (`self_reference` × `merge`)
**Scenariusz.** ZIP zawiera ZIP; rozmowa cytuje w całości inną rozmowę (wklejoną); eksport zawiera eksport ChatADHD.
**Interpretacja.** Rekurencja źródeł z głębokością; cytat całej rozmowy to obserwacja w rozmowie A, która może być rzutem rozmowy B (R35), nie drugą rozmową.
**W grafie.** Źródło zagnieżdżone z lokatorem złożonym (członek → członek); krawędź `quotes` A→B z zakresem bajtów; jeśli B znana: `projection_of`.
**Rządzi.** Głębokość rekurencji importu (preset, bez sufitu, z podglądem), polityka linkowania cytatów.
**Niezmiennik.** Lokator złożony rozwiązuje się do bajtów; rozmowa B nie jest liczona dwa razy w statystykach, ale cytat w A zostaje obserwacją A; brak nieskończonej pętli przy ZIP, który zawiera siebie (hash odwiedzony → `duplicate_of`).
**MR.** MR-3 (ZIP w ZIP vs wypakowany obok → te same jednostki, inne lokatory); MR-7.
**Częściowo** (lokator ma członka; zagnieżdżenie i `quotes` z zakresem — A9, A10).

#### EC-D07 — Ucięta historia, usunięte wiadomości (`withhold`)
**Scenariusz.** Eksport z dostawcy zawiera rozmowy z „[message deleted]” i wątki bez początku.
**Interpretacja.** R30: eksport nie jest zapisem transmisji; brak to `absent`/`unknown`, nie zero i nie zmyślona treść.
**W grafie.** Wiadomość-zaślepka z klasą `absent` i obserwacją markera; rozmowa z `provenance=reconstructed` tam, gdzie zrekonstruowano kolejność.
**Niezmiennik.** Liczba wiadomości w widoku = liczba w źródle + liczba zaślepek, każda oznaczona; żadna analiza nie traktuje zaślepki jako treści; raport importu wymienia liczby `absent` z lokatorami.
**MR.** MR-8; MR-3.
**Mieści się** (`absent`, `provenance ∈ {recorded, reconstructed, unknown}`).

#### EC-D08 — Pięćdziesiąt tysięcy rozmów (`scale`)
**Scenariusz.** Import 50 000 rozmów; linkowanie katalogu O(N²) (ACCEPTANCE 2026-09-29, ustalenie 4).
**Interpretacja.** Skala to przypadek graniczny silnika, nie użytkownika; budżet czasu lokalnego jest ustawieniem; brak zdolności degraduje (linki policzone dla próbki, reszta `{known:false}`), nie blokuje importu.
**W grafie.** Jednostki wszystkie; linki z pokryciem (ile par sprawdzono) jako twierdzenie o przebiegu.
**Niezmiennik.** Import kończy się niezależnie od N (linkowanie osobnym, wznawialnym przebiegiem); pokrycie linków jest zgłoszone liczbą, nie ukryte; MR-7 na liczbach jednostek.
**Mieści się** (I9 uczciwość zdolności); wymaga kandydatów (LSH) — wątek 6, nie abstrakcja.

#### EC-D09 — Plik uszkodzony w połowie (`noise` × `withhold`)
**Scenariusz.** JSON poprawny do bajtu 900 MB, potem śmieci.
**Interpretacja.** Prefiks `recognized`, ogon `opaque` z hashem i zakresem bajtów; nic nie przepada; import nie jest cofnięty.
**W grafie.** Dwa zakresy jednego źródła z różnymi statusami rozpoznania.
**Niezmiennik.** Bajty wejścia = bajty rozpoznane + bajty opaque; parser strumieniowy nie trzyma całości; ponowna próba po naprawie pliku to nowe źródło (inny hash) z krawędzią `repairs`.
**MR.** MR-3 (plik ucięty w punkcie X vs w Y: jednostki z prefiksu wspólnego identyczne).
**Częściowo** (T1; A10).

#### EC-D10 — Nowy adapter po roku (`time_shift` × `withhold`)
**Scenariusz.** Rok po imporcie nierozpoznanego formatu pojawia się adapter.
**Interpretacja.** Ponowne rozpoznanie z zachowaniem adnotacji użytkownika (tagi, wykluczenia, linki ręczne) na jednostkach, które przetrwały; `known_at` nowych twierdzeń = dziś.
**W grafie.** Druga próba rozpoznania; jednostki z nowego adaptera zmapowane na stare przez lokatory bajtowe; adnotacje przepięte; stare twierdzenia `superseded`, nie usunięte.
**Niezmiennik.** Adnotacje użytkownika: liczba przed = liczba po (żadna nie ginie; nieprzepięte zgłoszone z lokatorem); MR-9 na nowym adapterze.
**Łamie A10** (historia prób rozpoznania na rekordzie i mapowanie lokatorów między próbami).

#### EC-D11 — Załącznik, którego nie ma (`withhold`)
**Scenariusz.** Rozmowa odwołuje się do `file-abc.png`, którego nie ma w ZIP; inny załącznik ma 0 bajtów.
**Interpretacja.** Odnośnik wiszący to obserwacja (tekst odnośnika) + `absent` cel; plik 0 B to źródło o hash pustym, nie błąd.
**W grafie.** Węzeł załącznika z `absent`, lokator wskazujący odnośnik; 0 B jako `opaque` z rozmiarem 0.
**Niezmiennik.** Liczba odnośników = liczba rozwiązanych + liczba `absent`; późniejszy import pliku o pasującej nazwie/hashu → propozycja dowiązania (nie automat, chyba że ustawienie).
**Mieści się** (R44 §2 „nierozwiązane odnośniki”).

#### EC-D12 — Import własnej bazy (`self_reference`)
**Scenariusz.** Właściciel importuje `chatadhd.db` z telefonu (aplikacja Python) do Loom na laptopie.
**Interpretacja.** Źródło = baza innej instalacji; rozmowy mogą już istnieć (synchronizacja przez GitHub); to EC-D04 przez plik.
**W grafie.** Źródło SQLite; jednostki = wiersze; linki `projection_of` do istniejących rozmów po identyfikatorach (ten sam format ID) i treści.
**Niezmiennik.** Identyczny ID + identyczna treść → jeden węzeł (MR-1); identyczny ID + inna treść → `contested` z dwoma pochodzeniami, nigdy nadpisanie; metadane grafu Python (`nodes`, `links`) wchodzą jako twierdzenia z `produced_by = python-regex@wersja`.
**Łamie A18** częściowo (instalacja jako pochodzenie).

### E. Prywatność, magazyn, bezpieczeństwo

#### EC-E01 — Trzy kategorie, trzy miejsca (`split` magazynu)
**Scenariusz.** „Zdrowie zostaje na telefonie; praca może iść do chmury firmowej; nic o rodzinie nie idzie do żadnego dostawcy.”
**Interpretacja.** Trzy reguły: rozmieszczenie (zdrowie → lokalnie), magazyn (praca → chmura firmowa), egress (rodzina → żaden dostawca). To trzy różne osie, nie jedna.
**W grafie.** Kategorie jako węzły; reguły jako ustawienia z zakresem osoby; każdy rekord z kategorią (per fragment, EC-E02); lokatory z magazynem.
**Rządzi.** `placement`, `storage`, `egress` per kategoria; `intersection` gdy organizacja też ogranicza.
**Powiązania.** Rekordy, których kategoria jest wywnioskowana (nie podana) — niska pewność → najbardziej restrykcyjna reguła (preset).
**Niezmiennik.** MR-10: payload do dostawcy po zmianie reguły „rodzina” różni się dokładnie o rekordy tej kategorii; MR-17: po przeniesieniu „praca” do chmury każdy lokator rozwiązuje się do tego samego hasha; żaden rekord „zdrowie” nie ma lokatora poza telefonem (test grafowy).
**Łamie A7, A8** (magazyn jako wymiar lokatora; rejestr egressu per kategoria).

#### EC-E02 — Jedna wiadomość, dwie kategorie (`merge` kategorii)
**Scenariusz.** Wiadomość: „po wizycie u lekarza zrobię deploy wersji 2.3”.
**Interpretacja.** Kategoria to własność fragmentu (obserwacji), nie wiadomości; wysyłka pracy nie może zabrać zdrowia.
**W grafie.** Dwie obserwacje w jednej jednostce, każda z kategorią i pewnością; wiadomość jako całość ma kategorię = suma.
**Rządzi.** Metoda klasyfikacji prywatności (preset), próg pewności, reguła przy niepewności (restrykcyjna).
**Niezmiennik.** Projekcja „praca” zawiera fragment o deployu i nie zawiera fragmentu o lekarzu (ani w cytatach, ani w streszczeniach); streszczenie wiadomości dla dostawcy pracy jest liczone tylko z dozwolonych fragmentów (MR-10 na poziomie obserwacji).
**Łamie A7** (kategoria per obserwacja, nie per rekord) — częściowo mieści się w modelu obserwacji.

#### EC-E03 — Podpis na 20 lat (`irreversible` × `time_shift`)
**Scenariusz.** „Podpisz tę decyzję tak, żeby za 20 lat dało się sprawdzić, że to ja.”
**Interpretacja.** Podpis odporny kwantowo (ML-DSA, R51), klucz w gestii użytkownika, weryfikacja bez tej aplikacji (format standardowy); „to ja” = klucz powiązany z podmiotem przez zdarzenie o znanej dacie.
**W grafie.** Decyzja jako węzeł; podpis jako obserwacja (bajty + algorytm + identyfikator klucza); klucz jako węzeł z rotacją; krawędź `signed_by` → podmiot@czas.
**Rządzi.** Algorytmy (preset hybrydowy), gestia klucza (lokalnie / sprzęt / escrow), rotacja; czy podpisywać automatycznie decyzje danej klasy.
**Niezmiennik.** Weryfikacja podpisu nie wymaga grafu (standalone); rotacja klucza nie unieważnia starych podpisów (łańcuch kluczy jako dane); zmiana treści decyzji po podpisie to nowa wersja, stara zostaje podpisana.
**MR.** MR-9 (odbudowa grafu nie zmienia bajtów podpisu); MR-19.
**Łamie A7** (klucze i magazyn jako byty grafu; T5 projektuje).

#### EC-E04 — Pokaż, gdzie leżą dane; przenieś kategorię (`split` × `scale`)
**Scenariusz.** „Pokaż, gdzie fizycznie leżą moje dane, i przenieś tę kategorię gdzie indziej.” Kategoria ma 40 GB; w połowie przenoszenia kończy się miejsce.
**Interpretacja.** Widok magazynu (R51) z liczbami; przeniesienie to przebieg z postępem, idempotentny, odporny na przerwanie; brak miejsca = `invariant:external` z podaniem, ile zostało.
**W grafie.** Magazyny jako węzły; lokatory z `stored_at`; przebieg przeniesienia z pokwitowaniami per obiekt.
**Niezmiennik.** W każdej chwili każdy obiekt ma ≥ 1 kompletną kopię (kopiuj → weryfikuj hash → dopiero usuń); po przerwaniu widok pokazuje dokładnie, co gdzie jest; MR-17 po dokończeniu.
**Łamie A7**.

#### EC-E05 — Klucz wyciekł (`irreversible`)
**Scenariusz.** „Klucz API wyciekł; co było wysłane, komu, kiedy?”
**Interpretacja.** Pytanie do rejestru egressu: każda wysyłka z czasem, dostawcą, kluczem (identyfikatorem, nie wartością), kategoriami danych i hashem payloadu.
**W grafie.** Zdarzenia egressu jako węzły; krawędzie do rekordów wysłanych (albo ich hashów); klucz jako węzeł ze statusem `compromised` od T.
**Rządzi.** Retencja rejestru egressu (zakres), co rejestr trzyma (hash / pełny payload), rotacja po wycieku jako propozycja.
**Powiązania.** Nieprzypisane zużycie po stronie dostawcy (rejestr W2: `acknowledged_excess`) → okno wycieku.
**Niezmiennik.** Każda wysyłka od T0 jest w rejestrze (liczba wysyłek = liczba wpisów; test na ScriptedTransport); odpowiedź na pytanie nie wymaga dostawcy; klucz oznaczony `compromised` nie jest używany przez żaden tor bez jawnego nadpisania z inicjatorem.
**Łamie A8** (W2 rejestruje zużycie, nie treść/kategorie wysyłek).

#### EC-E06 — Inferencja z danych, których nie wolno wysłać (`contradict` × `self_reference`)
**Scenariusz.** Lokalnie wywnioskowano „użytkownik ma bezsenność” z rozmów kategorii zdrowie. Tor do dostawcy pracy chce dodać to do kontekstu „preferencje komunikacji”.
**Interpretacja.** Pochodne dziedziczą kategorię przesłanek (najbardziej restrykcyjną); inferencja o zdrowiu jest zdrowiem.
**W grafie.** Twierdzenie z kategorią wyprowadzoną z domknięcia przesłanek; egress sprawdza domknięcie, nie etykietę.
**Niezmiennik.** MR-10 na domknięciu: payload do dostawcy pracy nie zawiera żadnego twierdzenia, w którego przesłankach jest rekord zdrowia (test na grafie zależności); wyjątek wymaga inicjatora `user` i jest zapisany.
**Łamie A5, A7** (kategoria z domknięcia wymaga grafu zależności).

#### EC-E07 — Trzy magazyny, jeden offline (`split` × `withhold`)
**Scenariusz.** Kategoria „projekty” replikowana na serwer, Drive i telefon; Drive niedostępny tydzień; w tym czasie edycje na telefonie i serwerze.
**Interpretacja.** Replikacja z regułą konfliktu jako dane; brak magazynu to degradacja, nie blokada zapisu; po powrocie: scalanie jak EC-D04.
**W grafie.** Magazyny ze stanem dostępności (`{known:false}` gdy nie sprawdzono); zdarzenia zapisu per magazyn; różnica replik jako twierdzenie.
**Niezmiennik.** Zapis udaje się, jeśli choć jeden magazyn kategorii przyjął (ustawienie: kworum jako preset); widok pokazuje, które repliki są w tyle i o ile; MR-18 po powrocie.
**Łamie A7, A18**.

#### EC-E08 — Tryb paranoiczny (`absurd_setting`)
**Scenariusz.** „Załóż, że telefon ma Pegasusa; co i tak jest bezpieczne, a co nie?”
**Interpretacja.** R19: model zagrożeń jako dane; mapa ryzyka dla bieżącej konfiguracji; silnik nie udaje, że wie więcej niż wie (`{known:false}` dla kanałów bocznych).
**W grafie.** Modele atakującego jako węzły; ścieżki danych z oceną wysiłku; konfiguracja → mapa jako przebieg.
**Niezmiennik.** Mapa wymienia każdą ścieżkę danych z jedną z trzech etykiet: kontrolujemy / nie kontrolujemy / nie wiemy; zmiana jednego ustawienia zmienia mapę tylko na ścieżkach, które przez nie przechodzą (MR-6).
**Mieści się** jako dane (R19); brak packa.

#### EC-E09 — Zgubione hasło (`irreversible` × `absurd_setting`)
**Scenariusz.** Użytkownik włączył szyfrowanie bez odzyskiwania i zgubił hasło.
**Interpretacja.** Zasada właściciela: nie decydujemy za użytkownika → tryb „bez odzyskiwania” jest dozwolony, ale jego wybór musiał mieć jawne ostrzeżenie zapisane jako zdarzenie; po utracie: `invariant:security` (nie da się), z inicjatorem = decyzja użytkownika z datą.
**W grafie.** Ustawienie z historią wyboru; dane jako `opaque` z hashem; graf metadanych (co istniało, ile) zostaje, jeśli metadane nie były szyfrowane (osobne ustawienie).
**Niezmiennik.** Komunikat podaje datę i treść ostrzeżenia, które użytkownik zaakceptował; żadna „tylna furtka” nie istnieje (test: brak ścieżki kodu odszyfrowującej bez klucza); metadane dostępne zgodnie z ustawieniem ich szyfrowania.
**Mieści się** (R43 inicjator + historia ustawień).

#### EC-E10 — Osoba trzecia prosi o zapomnienie (`negate` × `irreversible` × `collectivize`)
**Scenariusz.** Były członek rodziny: „usuń wszystko o mnie”. Archiwa są niezmienne (I1).
**Interpretacja.** Konflikt niezmiennika I1 z prawem osoby; rozwiązanie jako ustawienie: wykluczenie trwałe + crypto-shredding kopii (klucz kategorii zniszczony) + zachowanie hashy, żeby odbudowa nie przywróciła.
**W grafie.** Podmiot ze stanem `never` ustawionym przez inny podmiot (uprawnienie `has_authority_over` własne dane); rekordy `excluded_by_user` z kluczem zniszczonym; zdarzenie z inicjatorem.
**Niezmiennik.** Po operacji żadna projekcja ani eksport nie zawiera treści podmiotu; hashe zostają (I1 na poziomie hasha); twierdzenia innych podmiotów, które go wspominają, zostają z zaślepką `redacted` i wyjaśnieniem; operacja nieodwracalna jest oznaczona i potwierdzona raz.
**MR.** MR-15; MR-10 (projekcja pełna vs po usunięciu = dokładnie rekordy podmiotu).
**Łamie A2, A13** (never dla cudzego podmiotu; wykluczenie z zasięgiem do bajtów).

### F. Ekonomia i sterowanie

#### EC-F01 — Ile kosztowało myślenie w tym tygodniu (`self_reference` × `scale`)
**Scenariusz.** „Ile kosztowało myślenie w tym tygodniu, o czym, i co z tego było warte?”
**Interpretacja.** Agregacja zużycia po osi czasu × temat × metoda; „warte” = wynik użyty później (cytowany w decyzji, przyjęty preset, odczytany) — metryka pochodna, nie osąd modelu.
**W grafie.** Pokwitowania zużycia z osiami (metoda, podmiot, projekt, temat, dostawca, okres); krawędzie `used_in` od wyników do późniejszych zdarzeń.
**Rządzi.** Definicja „warte” jako preset (ustawienie); waluta i cennik jako dane z datą.
**Niezmiennik.** Suma po dowolnej osi = suma całkowita (MR-13 agregacja); koszt z `provenance=declared` oznaczony osobno od `measured`; „warte” ma wyjaśnienie (które zdarzenia użycia).
**Łamie A15** (osie pokwitowań; W2 ma operację i rezerwację).

#### EC-F02 — Przepal tysiąc dolarów, pytaj przy ×10 (`absurd_setting`)
**Scenariusz.** „Przepal na to tysiąc dolarów, ale pytaj przy każdym skoku ×10.”
**Interpretacja.** Budżet 1000 na zakresie zadania + strażnik ×10 względem bazy kroczącej; pierwszy skok od 0 → baza nieistniejąca: pytanie raz przy pierwszym wydatku (preset), potem przy każdym ×10.
**W grafie.** Budżet jako ustawienie z zakresem i inicjatorem; pokwitowania; potwierdzenia jako zdarzenia.
**Niezmiennik.** Liczba pytań = liczba skoków ×10 + 1 (lub 0, jeśli preset „pierwszy bez pytania”); żadne pytanie poza skokiem; wydatek nigdy nie przekracza 1000 bez potwierdzenia; suma pokwitowań ≤ 1000 + ostatnia operacja w toku.
**MR.** MR-13 (sekwencja operacji o kosztach c_i: stan `requires_confirmation` dokładnie przy c_i ≥ 10·baza_i, niezależnie od ścieżki: czat, wsad, graf, eksperyment).
**Częściowo** (usage_policy ma bazę kroczącą; brak na wszystkich ścieżkach — T6; A14).

#### EC-F03 — Wszystko automatyczne, ale widzę każdą decyzję po fakcie (`absurd_setting` × `self_reference`)
**Scenariusz.** „Ustaw, żeby wszystko było automatyczne, ale żebym widział każdą decyzję po fakcie.”
**Interpretacja.** Polityka przyjęcia = `auto` na wszystkich zakresach + dziennik decyzji systemu jako widok (samomodel wykonywalny).
**W grafie.** Każda operacja systemu jako węzeł z metodą, parametrami, wynikiem, kosztem (DUCH §0: „wyprodukowane przez”); widok „decyzje od ostatniego razu”.
**Niezmiennik.** Liczba operacji wykonanych = liczba wpisów widoku; każdy wpis ma „cofnij” albo wyjaśnienie, dlaczego nieodwracalne; brak wpisów bez metody.
**MR.** MR-9 (odtworzenie dziennika z grafu); MR-16.
**Łamie A23** (operacje systemu jako węzły; dziś ślady przebiegów istnieją dla metod, nie dla wszystkich operacji).

#### EC-F04 — Dlaczego ta wartość (`self_reference`)
**Scenariusz.** „Dlaczego ta wartość ustawienia jest taka? Kto ją ustawił i jak ją zmienić?”
**Interpretacja.** Wyjaśnienie wartości efektywnej: zakresy, reguła, inicjator, data, i akcja „zmień na zakresie X”.
**Niezmiennik.** MR-16: z wyjaśnienia da się odtworzyć wartość; wyjaśnienie wymienia każdy zakres, który miał wartość (także przegrane); „jak zmienić” wskazuje najwęższy zakres, na którym użytkownik ma uprawnienie.
**Łamie A3** (wyjaśnienie dla dwóch warstw istnieje; dla łańcucha nie).

#### EC-F05 — Miliard dolarów (`absurd_setting`)
**Scenariusz.** Budżet = 1 000 000 000; model najdroższy; wszystko automatyczne.
**Interpretacja.** Zasada właściciela: ustawienie musi to pozwolić; jedyny hamulec: ×10 względem bazy. Przy bazie 0 pierwsze wywołanie to „∞×” → pytanie raz; potem każde ×10.
**Niezmiennik.** Żaden sufit w kodzie (test: wartość 1e9 przechodzi walidację i jest zapisana bez obcięcia; typ liczbowy nie przepełnia); strażnik działa identycznie dla 10 $ i 1e9 $ (MR-13 ze skalowaniem: przemnożenie wszystkich kosztów i budżetu przez k nie zmienia liczby pytań).
**Mieści się** (zasada); test potrzebny na typach i serializacji.

#### EC-F06 — Dedup opłacalny dziś, nie jutro (`time_shift` × `contradict`)
**Scenariusz.** Raport ekonomii proponuje linkowanie przy cenie tokenów X; po miesiącu cena spada 5×.
**Interpretacja.** Propozycja ma przesłanki cenowe z datą; po zmianie cen rekomendacja może się odwrócić; wykonane linki nie są cofane (koszt cofnięcia też liczony).
**W grafie.** Propozycja jako węzeł z bilansem i cennikiem@data; decyzja użytkownika; nowa propozycja „rozlinkuj” z własnym bilansem.
**Niezmiennik.** Bilans jest odtwarzalny z cennika@data (MR-9); nowa rekomendacja nie zmienia stanu bez przyjęcia; obie rekomendacje widoczne z datami.
**Mieści się** (R50 + cennik jako dane z datą — A15).

#### EC-F07 — Przepis droższy niż dane (`absurd_setting` × `self_reference`)
**Scenariusz.** Indeks FTS można odtworzyć w 40 minut CPU; właściciel ma 50 GB wolnego i prąd z baterii.
**Interpretacja.** Wolfram/MDL (GRAPH_ENTRY §9): generator ≠ zawsze taniej; decyzja z bilansu pamięć–czas–energia, zasób „bateria” jako ograniczenie podmiotu (A17).
**Niezmiennik.** Decyzja „materializuj vs przepis” ma bilans z liczbami i `{known:false}` gdzie brak pomiaru; zmiana jednego wejścia bilansu (np. koszt energii) zmienia tylko decyzje, które od niego zależą (MR-6).
**Łamie A17** (zasoby urządzenia jako byt).

#### EC-F08 — Budżet ustawiony przez trzy podmioty (`collectivize` × `absurd_setting`)
**Scenariusz.** Organizacja: max 100/mies.; osoba: max 1e9; projekt: min 50 (musi mieć na analizy).
**Interpretacja.** Reguła `min`/`max` per ustawienie; `min` z projektu i `max` z organizacji mogą być sprzeczne (min 50 > max 100? nie; ale min 200 > max 100 → konflikt do `ask` z wyjaśnieniem).
**Niezmiennik.** Wartość efektywna = f(wartości, reguła), wyjaśniona; sprzeczność nie wybiera cicho; MR-6.
**Łamie A3**.

### G. Niepewność, wejście, ASR

#### EC-G01 — Nie wiem, czego chcę (`withhold` intencji)
**Scenariusz.** „Nie wiem, czego chcę — zaproponuj.”
**Interpretacja.** Brak celu to cel typu `propose` (goal type w danych); selektor szuka otwartych pytań, nieprzyjętych propozycji, niedokończonych rozmów, wzorców (EC-C04); odpowiedź = lista propozycji z pochodzeniem, nie wykonanie.
**W grafie.** Zapytanie z celem `propose`; propozycje jako węzły; wybór użytkownika jako zdarzenie uczące.
**Niezmiennik.** Nic nie zostaje wykonane ani wysłane płatnie bez wyboru (chyba że ustawienie `auto`); każda propozycja ma „skąd” (otwarte pytanie / wzorzec / stare zadanie).
**Mieści się** (goal types są danymi).

#### EC-G02 — Nie rozumiem swojego pytania; dopytaj (`withhold` × `self_reference`)
**Scenariusz.** „Nie rozumiem swojego pytania; dopytaj mnie.”
**Interpretacja.** Tryb wywiadu (jak onboarding R39): model zadaje numerowane pytania tak/nie albo krótkie, zapisuje odpowiedzi jako uściślenia (R34: skonsolidowana instrukcja), kończy, gdy użytkownik powie „ok”.
**Niezmiennik.** Każda tura wywiadu zmienia aktywną specyfikację zadania z mapą do wypowiedzi; po „ok” kompilacja zawiera uściślenia, nie historię nieporozumień (MR: usunięcie z historii odrzuconych wariantów nie zmienia skompilowanej instrukcji).
**Mieści się** (R34; `ContextEngine` nie na ścieżce czatu — ACCEPTANCE #1).

#### EC-G03 — ASR: literówki, bez polskich znaków, urwane (`noise`)
**Scenariusz.** „zaimportuj eksport z opena i i antropika nie dubluj ale nic nie” (urwane).
**Interpretacja.** Wypowiedź z n-best i pewnością; normalizacja (diakrytyki, synonimy z leksykonu jako dane) → intencja + argumenty + brakujący argument („nic nie…” → `absent`); echo interpretacji w jednym zdaniu; pytanie tylko przy materialnej różnicy.
**W grafie.** Wypowiedź surowa (obserwacja), interpretacja (inferencja z własnością „użytkownik potwierdzi”), krawędź `read_as`.
**Rządzi.** Próg echa/pytania (preset: akcje nieodwracalne zawsze pytają, EC-G05); leksykon synonimów (dane, język).
**Niezmiennik.** MR-4: warianty szumu tej samej wypowiedzi (z/bez diakrytyków, 1–2 literówki, synonim) dają tę samą intencję i te same argumenty albo jawne pytanie; nigdy inną akcję bez pytania; surowy tekst nigdy nie jest nadpisany.
**Łamie A11**.

#### EC-G04 — Zrób to, co ostatnio, ale inaczej (`self_reference` × `withhold`)
**Scenariusz.** „Zrób to, co ostatnio, ale inaczej.”
**Interpretacja.** „Ostatnio” = ostatni przebieg metody w tym kontekście (zakres); „inaczej” = inny tor (model/prompt/parametry) z tego samego presetu rodziny; niejednoznaczność „inaczej” → propozycja 3 wariantów, nie zgadywanie.
**W grafie.** Nowy przebieg z krawędzią `variant_of` → poprzedni; różnica parametrów jako twierdzenie.
**Niezmiennik.** Nowy przebieg różni się od poprzedniego co najmniej jednym jawnym parametrem i ma do niego krawędź; wejście identyczne (MR: „ostatnio” rozwiązane deterministycznie do tego samego przebiegu przy powtórzeniu pytania).
**Mieści się** (ślady przebiegów); „inaczej” jako operator na presecie — A6.

#### EC-G05 — Zgubiona negacja (`noise` × `irreversible`)
**Scenariusz.** ASR: „wyślij zdrowie do gpt” zamiast „nie wysyłaj…”; albo „usuń wszystko z 2024” z pewnością 0,6.
**Interpretacja.** Akcja nieodwracalna lub sprzeczna z trwałą preferencją wymaga echa i potwierdzenia tak/nie z numerem, niezależnie od pewności ASR; sprzeczność z `never` zdrowia → inicjator musi być `user` jawnie.
**Niezmiennik.** Żadna operacja klasy `irreversible` ani sprzeczna z `never` nie wykonuje się z wejścia ASR bez potwierdzenia; potwierdzenie zapisane; MR-4: wariant z negacją i bez dają różne echa, a bez potwierdzenia ten sam skutek (żaden).
**Łamie A11, A14**.

#### EC-G06 — Dwa języki i kod w głosie (`noise` × `merge`)
**Scenariusz.** „Dodaj do kontekstu funkcję resolve underscore lineage z resolve kropka cpp i wyjaśnij po polsku.”
**Interpretacja.** Rozpoznanie identyfikatorów kodu w mowie (leksykon: „underscore”, „kropka”) jako dane; dwa języki → preferencja języka odpowiedzi z zakresu.
**Niezmiennik.** Identyfikator rozwiązany do symbolu w repo (albo `absent` z propozycjami); MR-4 na wariantach („resolve lineage w resolve cpp”); język odpowiedzi z ustawienia, nie z języka pytania, chyba że ustawienie mówi „jak pytanie”.
**Łamie A11** (leksykon rozpoznawania mowy jako dane; interpretacja jako węzeł).

#### EC-G07 — Polecenie sprzeczne z preferencją trwałą (`contradict` × `negate`)
**Scenariusz.** `never` wysyłki zdrowia; dziś: „wyślij ten wynik badania do Claude, chcę drugą opinię”.
**Interpretacja.** Użytkownik ma prawo (R43: inicjator `user`); sprzeczność z własną regułą → pytanie tak/nie z numerem i wybór zakresu wyjątku (ta wiadomość / ta rozmowa / na stałe).
**W grafie.** Wyjątek jako nadpisanie na wybranym zakresie z inicjatorem i datą; egress zapisany z odniesieniem do wyjątku.
**Niezmiennik.** Reguła `never` nie zmienia się bez jawnego wyboru „na stałe”; wyjątek na zakresie tury wygasa po turze (MR-6 z wygaśnięciem); rejestr egressu wskazuje wyjątek.
**Łamie A3, A8, A13**.

#### EC-G08 — Kliknięcie w grafie jako wejście (`merge` kanałów)
**Scenariusz.** Właściciel przeciąga krawędź w wizualizacji: „ta rozmowa należy do projektu Y”.
**Interpretacja.** Ta sama droga co tekst (DUCH §3): wejście → interpretacja → osadzenie z pochodzeniem `user` → propozycje powiązań.
**Niezmiennik.** Krawędź z UI ma identyczną reprezentację jak krawędź z polecenia tekstowego „połącz rozmowę z projektem Y” (MR: oba wejścia → ten sam zapis bajtowo poza kanałem wejścia); cofnięcie z obu kanałów działa tak samo.
**Mieści się** (R29–R31); brak testu równoważności kanałów.

### H. Samoodniesienie — graf o sobie

#### EC-H01 — Importuj własne logi (`self_reference`)
**Scenariusz.** Właściciel importuje `logs/` aplikacji i ten dokument jako źródła.
**Interpretacja.** Źródło o systemie; twierdzenia o samomodelu (błędy, koszty, wersje) z klasą `observed`, origin `repo`/`system`; dokument projektowy jako artefakt typu `specification` z R-węzłami.
**W grafie.** Obszar Samomodel zasilany z importu, nie tylko z packa; krawędzie `describes` → operacje, metody.
**Niezmiennik.** Twierdzenie o systemie z logu nie nadpisuje twierdzenia z packa; sprzeczność (pack mówi „działa”, log mówi „błąd”) → `contested` widoczne w widoku wejściowym; MR-8.
**Mieści się** (R2 self-discovery); brak klas węzłów samomodelu jako danych (T3).

#### EC-H02 — Eksperyment o eksperymentach (`self_reference` × `scale`)
**Scenariusz.** „Które z moich eksperymentów z zeszłego miesiąca dały preset, którego potem użyłem?”
**Interpretacja.** Metadane eksperymentów są zwykłymi danymi grafu; zapytanie strukturalne po krawędziach `produced_by`, `adopted_as`, `used_in`.
**Niezmiennik.** Odpowiedź policzalna bez modelu (czysto grafowa); zgodna z raportem ekonomii „warte” (EC-F01) na tym samym zbiorze (MR: dwie drogi, jeden wynik).
**Łamie A6, A15** (krawędzie przyjęcia i użycia).

#### EC-H03 — Dlaczego tak odpowiedziałeś (`self_reference`)
**Scenariusz.** „Dlaczego odpowiedziałeś tak? Co wysłałeś, co pominąłeś i dlaczego?”
**Interpretacja.** Cztery przekroje audytu (R26): historia / co obowiązuje / co w grafie / co wysłano; selektor zapisuje powód włączenia i wykluczenia każdego elementu.
**W grafie.** Ślad przebiegu z `ContextSet` (elementy + powody + budżet) i hash payloadu.
**Niezmiennik.** Payload odtwarzalny z `ContextSet` (hash zgodny, MR-9); każdy element kontekstu ma powód; „pominięte z budżetu” wymienia najwyżej ocenione pominięte z liczbą; `ContextSet` na ścieżce czatu jest tym, co wysłano (ACCEPTANCE #1).
**Częściowo** (ContextEngine istnieje, nie na ścieżce czatu).

#### EC-H04 — Własne pomyłki (`self_reference` × `time_shift`)
**Scenariusz.** Aplikacja scaliła dwie osoby jako jedną; użytkownik rozdziela; „zapamiętaj, że się mylisz w takich przypadkach”.
**Interpretacja.** Pomyłka jako zdarzenie w samomodelu; profil metody (R37) dostaje przypadek słabości z danymi; kompensacja jako wersjonowany preset, nie reguła w kodzie.
**W grafie.** Zdarzenie `split` z inicjatorem; krawędź `error_of` → przebieg metody; profil metody z nowym dowodem; propozycja progu/kompensacji.
**Niezmiennik.** Rozdzielenie odtwarza oba węzły z pełnym pochodzeniem (nic z scalenia nie ginie; MR-11 w obie strony); profil metody zmienia się tylko o ten przypadek; kompensacja jest ablacyjnie testowalna (ACCEPTANCE #17).
**Mieści się** (R37, I4).

#### EC-H05 — Zalążek zmieniony, pack zaktualizowany (`self_reference` × `time_shift`)
**Scenariusz.** Użytkownik usunął z zalążka metodę M i zmienił opis klasy „osoba”; aktualizacja aplikacji przynosi nową wersję packa z M i nowymi slotami „osoby”.
**Interpretacja.** R40: wykluczenie trwałe M przeżywa; nowe sloty to propozycje albo automat według ustawienia; zmiana opisu użytkownika wygrywa z packiem (I4) i jest scalana (`merge`).
**Niezmiennik.** MR-15 (M nie wraca); liczba nowych propozycji = liczba nowych elementów packa dotykających obszarów bez wykluczenia; opis użytkownika bajtowo zachowany; wyjaśnienie każdego slotu mówi „pack v2” albo „użytkownik”.
**Mieści się** (R40 + onboarding layers); test aktualizacji packa istnieje dla prezentacji, nie dla ontologii (T3).

#### EC-H06 — Widok wejściowy za duży (`scale` × `self_reference`)
**Scenariusz.** Po roku widok wejściowy ma 14 obszarów po 10⁶ węzłów; telefon.
**Interpretacja.** Widok wejściowy to zapytanie z budżetem (R45): liczniki, nie listy; `expand` per obszar; liczniki liczone przyrostowo albo `{known:false, stale_since}`.
**Niezmiennik.** Rozmiar odpowiedzi widoku niezależny od rozmiaru grafu (stała liczba obszarów × stałe pola); licznik nieaktualny jest oznaczony datą, nie udaje świeżego; MR-7 (×2 danych → liczniki ×2, rozmiar odpowiedzi ten sam).
**Mieści się** (T3 projekt).

#### EC-H07 — Nowe wymaganie sprzeczne ze starym (`contradict` × `self_reference`)
**Scenariusz.** Właściciel wypowiada R54 sprzeczne z R20 („jednak zawsze kopiuj, nigdy nie linkuj”).
**Interpretacja.** R-węzły w samomodelu; sprzeczność to projekcja ewolucji (słowa właściciela przy R43–R50), nie błąd; nowe `supersedes` stare z datą; stare zostaje.
**Niezmiennik.** Oba węzły istnieją; `supersedes` ma datę i cytat; presety wyprowadzone z R20 dostają status „do przeglądu”, nie zmieniają się same (I4 dotyczy ustawień, R-węzeł to twierdzenie o filozofii).
**Mieści się** (claims z `supersedes`); R-węzły jako dane (T3).

### I. Instalacje, urządzenia, agenci

#### EC-I01 — Telefon offline tydzień (`split` × `time_shift`)
**Scenariusz.** Telefon bez sieci przez tydzień: 40 rozmów lokalnych; laptop w tym czasie zmienił ustawienia i zaimportował eksport.
**Interpretacja.** EC-D04 w czasie: zdarzenia obu instalacji z zegarem logicznym; ustawienia scalane regułą per ustawienie; rozmowy niezależne (brak konfliktu); duplikaty eksportu wobec rozmów telefonu → propozycje.
**Niezmiennik.** MR-18; żadne ustawienie nie jest nadpisane bez reguły; `known_at` twierdzeń z telefonu = czas lokalny telefonu z kwalifikatorem „offline” (EC-A09).
**Łamie A18, A1**.

#### EC-I02 — Dwie wersje aplikacji, jeden graf (`split` × `contradict`)
**Scenariusz.** Laptop ma pack v12, telefon pack v9; obie piszą.
**Interpretacja.** Wersja packa jest pochodzeniem każdego twierdzenia pochodnego; różnice packów to różne wersje metod; scalanie nie wymaga tej samej wersji.
**Niezmiennik.** Twierdzenie z v9 i v12 o tym samym wejściu to dwa przebiegi, nie konflikt; wykluczenia trwałe obowiązują niezależnie od wersji (MR-15 między instalacjami); format zdarzeń zgodny wstecz lub jawnie `unrecognized` (R44 dotyczy też własnych danych).
**Łamie A18**.

#### EC-I03 — Agent zapisał tysiąc propozycji w nocy (`collectivize` × `scale`)
**Scenariusz.** Agent AI z uprawnieniem `auto` dodał 1000 krawędzi; rano: „cofnij wszystko, co zrobił, i pokaż, co by było bez niego”.
**Interpretacja.** Agent to podmiot; jego zapisy mają pochodzenie podmiotu; cofnięcie hurtowe po pochodzeniu = kwarantanna podmiotu (EC-A03 na podmiocie); „bez niego” = widok z filtrem pochodzenia.
**Niezmiennik.** Cofnięcie jest zdarzeniem, nie usunięciem (krawędzie → `retracted`, odtwarzalne); widok „bez agenta” = graf minus domknięcie jego zapisów (MR-14 na podmiocie); polityka `auto` dla agenta zmienia się tylko jawnie.
**Łamie A2, A4, A5**.

#### EC-I04 — Ta sesja jako podmiot (`self_reference`)
**Scenariusz.** Decyzje techniczne sesji Claude (CLAUDE.md „Decisions”) mają być w `INDEX.md` i w grafie jako węzły podmiotu `agent:claude-session-…` z „jak cofnąć”.
**Interpretacja.** Sesja = agent AI (poziom podmiotu); jej decyzje = decyzje z przesłankami (R-węzły) i statusem; dokumenty = źródła.
**Niezmiennik.** Każda decyzja sesji ma inicjatora (sesja), przesłankę (R-węzeł) i odwrócenie; właściciel może ją `supersede` jednym zdaniem (I4).
**Mieści się** (decyzja jako węzeł — A16 dla kolektywów; dla pojedynczego agenta wystarczy `decides`).

#### EC-I05 — Instalacja w imieniu organizacji (`collectivize` × `split`)
**Scenariusz.** Serwer web działa dla firmy; właściciel loguje się prywatnie i służbowo z tej samej instalacji.
**Interpretacja.** Podmiot działający ≠ instalacja; zakresy organizacja → osoba → rola; płatnik (kto płaci) jako osobna oś pokwitowań (A15); prywatność per rola.
**Niezmiennik.** Pokwitowanie ma płatnika; rekordy roli prywatnej nie są dostępne z roli służbowej bez reguły (MR-10 per rola); wyjaśnienie wartości nazywa zakres roli.
**Łamie A2, A3, A15**.

#### EC-I06 — Telefon skradziony (`irreversible` × `split`)
**Scenariusz.** „Telefon skradziony; unieważnij jego klucze; co z tego, co tam było?”
**Interpretacja.** Instalacja jako podmiot ze statusem `revoked`; klucze instalacji w łańcuchu kluczy z unieważnieniem; dane w magazynach zewnętrznych bezpieczne, jeśli zaszyfrowane kluczem użytkownika, nie instalacji (ustawienie gestii).
**W grafie.** Zdarzenie unieważnienia; widok magazynu: co było tylko na telefonie (`stored_at` = tylko ta instalacja) → lista strat z liczbami.
**Niezmiennik.** Po unieważnieniu żaden magazyn nie przyjmuje zapisów z tej instalacji (test: podpis instalacji odrzucony); lista „tylko na telefonie” policzalna z grafu bez telefonu; nic nie jest zmyślone jako „bezpieczne” bez `{known}`.
**Łamie A7, A18**.

### J. Ontologia i struktury użytkownika

#### EC-J01 — Własna klasa i własna krawędź (`absurd_setting` ontologii)
**Scenariusz.** „Mam klasę ‘rytuał’ i krawędź ‘wyzwala’; poranna kawa wyzwala planowanie.”
**Interpretacja.** DUCH §1: struktury zdefiniowane przez użytkownika są równorzędne z predefiniowanymi i wykrytymi; klasa jako węzeł z pakietem właściwości użytkownika; krawędź z semantyką opisową (dane) i opcjonalnym morfizmem do znanych (`causes`?) jako propozycją.
**W grafie.** Węzeł `class:rytuał` (pochodzenie `user`); `instance_of`; typ krawędzi `wyzwala` w danych z opisem; morfizm-propozycja.
**Niezmiennik.** Klasa użytkownika działa w selektorze, widokach i Jev tak samo jak klasa z packa (MR: zamiana klasy packowej na równoważną użytkownika nie zmienia wyników selektora poza etykietą); aktualizacja packa nie dotyka klas użytkownika (MR-15).
**Łamie A20**.

#### EC-J02 — Osoba i firma naraz (`merge` klas)
**Scenariusz.** Jednoosobowa działalność: ten sam byt jest osobą i organizacją.
**Interpretacja.** `instance_of` wielokrotne; pakiety obu klas; sloty wspólne (adres) scalane regułą, sloty sprzeczne (język formalny firmy vs osoby) rozstrzygane kontekstem.
**Niezmiennik.** Brak duplikacji węzła; pytania onboardingu o sloty wspólne zadane raz; `by_context` rozróżnia role.
**Łamie A2, A20** (klasy jako węzły z wieloma `instance_of`).

#### EC-J03 — Nie zgadzam się z klasyfikacją (`contradict` × `negate`)
**Scenariusz.** Model oznaczył rozmowę jako „finanse”; użytkownik: „to nie finanse, to rodzina”.
**Interpretacja.** Osąd użytkownika wygrywa (I4), inferencja → `rejected` z historią; profil metody dostaje przypadek; przy ponownym przeliczeniu (nowa wersja) osąd jest odtwarzany, nie nadpisywany.
**Niezmiennik.** MR-9 z osądem: odbudowa + nowa wersja metody → klasyfikacja = „rodzina” (osąd replayowany); odrzucona inferencja widoczna w historii; prywatność zmienia się zgodnie z nową kategorią natychmiast (MR-10).
**Mieści się** (I4).

#### EC-J04 — Slot zależny od kontekstu i czasu (`split` × `time_shift`)
**Scenariusz.** „Po polsku w domu teraz, po angielsku w projekcie X od 2025, a w 2023 wszędzie po angielsku.”
**Interpretacja.** Slot `język` z osią czasu (teraz/zwykle/kiedyś) i zakresem (projekt); historia wartości jako twierdzenia z `valid_from`.
**Niezmiennik.** Zapytanie „jaki język w projekcie X w 2024” daje „angielski” z wyjaśnieniem; „w domu w 2023” → „angielski (wszędzie)”; zmiana „teraz” nie edytuje historii (MR-8).
**Łamie A1, A3** (slot z czasem i zakresem).

#### EC-J05 — Slot `never`, a import ma dane do slotu (`negate` × `merge`)
**Scenariusz.** Slot „stan zdrowia” = `never`; import eksportu zawiera rozmowę z lekarzem.
**Interpretacja.** `never` = nie pytaj, nie wnioskuj; rozmowa wchodzi do archiwum (R44), kategoria zdrowie; żadna inferencja nie wypełnia slotu; pytanie onboardingu się nie pojawia.
**Niezmiennik.** Slot pozostaje `never` po imporcie (MR-15 na slocie); twierdzenia klasy `inferred` o zdrowiu podmiotu nie powstają (zbiór pusty; test na ScriptedTransport i regex); rozmowa jest przeszukiwalna lokalnie.
**Łamie A13**.

#### EC-J06 — Uogólnienie użytkownika tworzy kategorię (`absurd_setting` × `self_reference`)
**Scenariusz.** „Wszystko, co mówię o finansach, jest poufne.”
**Interpretacja.** Zasada użytkownika (R10 obszar + uogólnienie): generuje kategorię prywatności dla każdej obserwacji sklasyfikowanej jako finanse, również przyszłej; kandydat `candidate` → zatwierdzony.
**Niezmiennik.** Każda nowa obserwacja „finanse” dziedziczy regułę bez ponownego pytania; zmiana zasady działa wstecz tylko na projekcje, nie na rejestr egressu (co wysłano, wysłano — EC-E05 pokazuje); MR-10.
**Mieści się** (R10, R39); kategoria z zasady — A7 (kategoria per obserwacja).

## 3. Brakujące abstrakcje silnika (z luk, nie lista funkcji)

Każda pozycja to pojęcie, bez którego przypadki w kolumnie „Łamią” nie dają
się wyrazić w bytach DUCH §1–§4. Nie są to funkcje do zaimplementowania, lecz
**byty i kwalifikatory**, które mają istnieć w danych i w kontrakcie, a dopiero
potem w kodzie. Kolumna „Dziś” odnosi się do stanu 2026-10-11 (THREADS §2/§8,
GRAPH_ENTRY §1).

| ID | Abstrakcja | Czego brakuje dziś | Przypadki, które ją łamią | Gdzie wchodzi |
|---|---|---|---|---|
| **A1** | **Bitemporalność**: każde twierdzenie ma `valid_from/to` (czas świata) **i** `known_at` (czas poznania); zapytania z cięciem po obu osiach | twierdzenia mają tylko `valid_*`; `created` to czas rekordu, nie wiedzy | A01, A02, A10, I01, J04 | KnowledgeStore, GraphPacket, selektor (tryb „jak na T”) |
| **A2** | **Podmiot** jako węzeł z poziomami, rolą w kontekście (z czasem), relacjami `decides_with`/`trusts` z wagą per rodzaj, i rozróżnieniem „o którym” / „który działa i płaci” | użytkownik = profil onboardingu; `role=user` w imporcie = właściciel; brak `parties.pack` | B01–B03, B06, B07, B09, E10, I03, I05, J02 | `graph/parties.pack` (T3), importer (podmiot źródła), W2 (płatnik) |
| **A3** | **Łańcuch zakresów** z regułą konfliktu per ustawienie i wyjaśnieniem wartości efektywnej (R47) | R40: dwie warstwy; brak `by_context`, `locked_by`, `min/max`, wygaśnięcia zakresu tury | B02, B04, B08, F04, F08, G07, I05, J04 | `graph/scopes.pack` (T3), `DefaultLayers` → resolver zakresów (T13) |
| **A4** | **Kwarantanna epistemiczna**: źródło/podmiot/okres „widoczny, ale nie przesłanka” (osobno od widoczności i usunięcia), odwracalna | statusy wiadomości `active/excluded/version/deleted`; brak „nie ucz się” | A03, A04, I03 | DB (kwalifikator źródła), selektor, worker |
| **A5** | **Graf zależności pochodnych i przeliczenie przyrostowe**: od `produced_by` do „co ode mnie zależy” (domknięcie), z determinizmem metody jako daną | `produced_by` i ślady przebiegów istnieją; brak domknięcia i „dirty set” | A02, A04, C03, C10, E06, I03 | provenance + MethodRegistry (T2 worker jako wykonawca) |
| **A6** | **Tor jako dane** (metoda, wersja, model, prompt, parametry, budżet, kryteria) + **eksperyment jako węzeł** (DAG torów, oceny wielokryterialne, przyjęcie presetu na zakresie, `incomplete` z przyczyną) | P5 ExperimentSpec otwarte; brak języka torów | C01, C02, C07, C08, G04, H02 | `presets/experiment*.pack`, T11 (metoda N-torowa), T14 ścieżka P5 |
| **A7** | **Lokator z magazynem i kategorią per obserwacja**: `stored_at` jako wymiar lokatora, rozmieszczenie/replikacja/egress per kategoria, klucze jako węzły | lokator = źródło + członek + zakres; magazyn niejawny (BlobStore/DB); kategoria per rekord co najwyżej | E01–E04, E06, E07, I06, J06 | T5 (inwentarz → projekt), `storage.pack` |
| **A8** | **Rejestr egressu**: co (hash/kategorie), komu, kiedy, pod jakim kluczem, z jakiego wyjątku/zgody | W2 rejestruje zużycie i operacje, nie treść/kategorie | E01, E05, G07 | W2 rozszerzony o wymiar treści; T5 |
| **A9** | **Spektrum tożsamości**: `same_as` / `projection_of` / `same_person_other_account` / `acts_as` / lineage, każde z czasem i wrażliwością | R35 częściowo (lineage); brak `projection_of` dla rozmów i wrażliwości krawędzi tożsamości | B05, D01, D06 | resolve + importer (T1 duplikaty) |
| **A10** | **Status rozpoznania na rekordzie** z historią prób i mapowaniem lokatorów między próbami | WP-B w toku; brak historii prób i przepięcia adnotacji | D01, D02, D09, D10 | T1 |
| **A11** | **Wypowiedź z niepewnością**: surowy tekst + n-best + pewność + język + leksykon mowy jako dane; interpretacja jako osobny węzeł `read_as` | wiadomość = jeden napis; ASR w Pythonie zwraca tekst | A07, G03, G05, G06 | DB (metadane wiadomości), `lexicons/asr.pack`, T14 odporność na ASR |
| **A12** | **Stan podmiotu w chwili podania** jako kwalifikator twierdzeń i slotów (ADHD jako stan) | brak | B10, A03 | pakiety właściwości (T3), selektor |
| **A13** | **Trwała negacja z zasięgiem**: `never` dla tematu/slotu/podmiotu obejmujące pytanie, inferencję, import, projekcje; marker przeżywa pack i instalacje | R40 wykluczenie tylko dla domyślnych; R39 stany slotów bez zasięgu do inferencji | A05, E10, G07, J05 | onboarding layers → KnowledgeStore (writer respektuje), importer |
| **A14** | **Bramka jako wynik z inicjatorem** na każdej ścieżce + stan `requires_confirmation` jako stan, nie blokada | R43 zinwentaryzowane; strażnik tylko na odpowiedzi grafowej (T6) | B08, C07, C09, F02, G05 | T6, usage_policy na czacie/wsadzie |
| **A15** | **Pokwitowanie z osiami** (metoda, podmiot, płatnik, projekt, temat, dostawca, okres) + cennik z datą + „użyte w” | W2: operacja, rezerwacja, baza krocząca | D03, F01, F06, H02, I05 | W2 + T4 raport ekonomii |
| **A16** | **Decyzja kolektywu**: uczestnicy w rolach@czas, wagi, przesłanki per podmiot, alternatywy, mapa niezgody, konsekwencje | `decides` dla jednego podmiotu | B01, B09 | ontologia (T3), T13 |
| **A17** | **Zasób ograniczony podmiotu/urządzenia** (uwaga, czas, bateria, miejsce) jako byt wpływający na selektor i bilanse | brak | B10, F07 | pakiety właściwości, T4 bilans |
| **A18** | **Instalacja jako podmiot**: identyfikator, zegar logiczny, scalanie regułą per kategoria, konflikt jako `contested` | DB jednoprocesowa; GitHub sync bez reguł | D04, D12, E07, I01, I02, I06 | T5 (sync), nowy pakiet |
| **A19** | **Tryb wyjścia**: graf jako jedyne wyjście; gwarancja MR-20 między liniową a grafową | chat-graph osadza; brak trybu i testu równoważności | C05 | chat engine, web |
| **A20** | **Klasy użytkownika**: klasa jako węzeł z pakietem, `instance_of` wielokrotne, krawędzie użytkownika z opisem, odporne na pack | `Entity.kind` to etykieta; brak `classes.pack` | J01, J02 | T3 |
| **A21** | **Wzorzec bez nazwy** jako pełnoprawny węzeł z członkami i propozycjami nazw | brak | C04 | metody strukturalne (R22–R23) |
| **A22** | **Sprzeczność między podmiotami/torami** jako byt (nie tylko `contested` twierdzenia) | `contested` istnieje dla wartości twierdzenia | B01, C02 | KnowledgeStore (krawędź `contradicts` z podmiotami) |
| **A23** | **Samomodel wykonywalny**: każda operacja systemu jako węzeł z metodą, parametrami, kosztem, odwróceniem | ślady dla metod analizy; nie dla importu/ustawień/sync | F03 | provenance na wszystkich operacjach |

Zależności między abstrakcjami (kolejność wdrażania wynika z nich, nie z listy
funkcji): A2 → A3 → A13/A16; A5 → A4/A6/E06; A7 → A8/A18; A1 niezależna, ale
A4, A6 i A18 bez niej dają niewyjaśnialne wyniki. A11 jest warunkiem T14.

## 4. Katalog relacji metamorficznych (MR)

Każda relacja: transformacja wejścia `T` → wymagana relacja wyników `R`.
Testy metamorficzne nie potrzebują oracle’a: sprawdzają `R(f(x), f(T(x)))`.
Reguły pomiaru z notatki 29.09 §16 obowiązują (zamrożone dane, jawne różnice
wariantów, powtórki nie są niezależnymi przypadkami).

| MR | Transformacja | Wymagana relacja | Przypadki |
|---|---|---|---|
| MR-1 | powtórzenie operacji z tym samym wejściem i wersją | graf identyczny bajtowo poza zdarzeniem „powtórzono”; dla metod niedeterministycznych: operacja idempotentna, wynik nie jest zwracany jako nowy | C03, C09, C10, D02, D05, D12 |
| MR-2 | permutacja kolejności wejść (plików, torów, źródeł, rozmów) | ten sam graf modulo identyfikatory zdarzeń i czas | A09, B01, C08, D01 |
| MR-3 | podział/scalenie wejścia (jeden ZIP vs dwa; ucięcie w X vs Y) | ten sam zbiór jednostek i twierdzeń; różni się tylko pochodzenie | A07, D01, D03, D06, D07, D09 |
| MR-4 | szum ASR (diakrytyki, 1–2 literówki, synonim z leksykonu, urwanie) | ta sama intencja i argumenty albo jawne pytanie; nigdy inna akcja bez pytania; surowy tekst zachowany | A07, G03, G05, G06 |
| MR-5 | cięcie czasu (po `valid`, po `known`, przesunięcie zegara) | zapytanie „jak na T” = replay z grafu zbudowanego ze zdarzeń ≤ T | A01, A02, A08, A10 |
| MR-6 | nadpisanie w zakresie (dodanie/usunięcie wartości; wygaśnięcie) | wynik efektywny zmienia się tylko w tym zakresie; wyjaśnienie zawiera zakres; usunięcie przywraca poprzedni stan bajtowo | B02, B04, B06, B08, C06, E08, F07, F08, G07 |
| MR-7 | przeskalowanie (×k kopii, ×k danych) | liczniki ×k; bajty źródła raz (przy link); wyniki analiz niezmienne; rozmiar odpowiedzi widoku stały | D01, D03, D05, D06, D08, H06 |
| MR-8 | dodanie źródła/gałęzi/klasy | monotoniczność: żadne wcześniejsze twierdzenie nie znika (może stać się `contested`) | A10, C04, D01, D02, D07, H01, J04 |
| MR-9 | usunięcie stanu pochodnego i odbudowa z przepisów + replay osądów | identyczne wyniki (I5) dla metod deterministycznych; zapisane odpowiedzi dla niedeterministycznych bez nowych wywołań; osądy odtworzone | A04, A06, A09, C03, C08, C12, E03, F03, F06, H03, J03 |
| MR-10 | zmiana reguły prywatności kategorii/podmiotu/roli | payload/projekcja różni się dokładnie o elementy tej kategorii (na poziomie obserwacji i domknięcia przesłanek) | A05, B05, B07, C02, E01, E02, E06, E10, I05, J03, J06 |
| MR-11 | zmiana etykiety/aliasu/nazwy wzorca; rozdzielenie scalonych | te same twierdzenia i krawędzie; wyszukiwanie po starej nazwie trafia | B07, C04, H04 |
| MR-12 | permutacja torów eksperymentu | ocena każdego toru niezmienna | C01, C02, C07, C12 |
| MR-13 | koszt: sekwencja operacji; przemnożenie kosztów i budżetu przez k | koszt eksperymentu = suma torów; `requires_confirmation` dokładnie przy ×10 względem bazy, niezależnie od ścieżki (czat/wsad/graf/eksperyment) i od k | C01, C03, C07, C08, C09, F01, F02, F05 |
| MR-14 | kwarantanna źródła/podmiotu/stanu i jej zdjęcie | aktywne inferencje po = przed minus domknięcie przesłanek; obserwacje identyczne; kwarantanna ∘ zdjęcie = identyczność | A03, A04, B10, I03 |
| MR-15 | aktualizacja packa / wersji aplikacji / instalacji | wykluczenia trwałe i `never` nie wracają; propozycje tylko gdy ustawienie pozwala; klasy użytkownika nietknięte | A05, E10, H05, I02, J01, J05 |
| MR-16 | wyjaśnienie wartości efektywnej / wpisu dziennika | z wyjaśnienia da się odtworzyć wartość (explain → recompute = wartość) | A06, B04, B08, F03, F04 |
| MR-17 | przeniesienie kategorii do innego magazynu | każdy lokator kategorii rozwiązuje się do tych samych bajtów (hash); graf niezmieniony poza `stored_at` | E01, E04 |
| MR-18 | scalanie dwóch instalacji | przemienne i łączne; żadne zdarzenie nie ginie; konflikt = `contested`, nigdy nadpisanie | D04, E07, I01 |
| MR-19 | zmiana wag/ról/składu po decyzji | zapisana decyzja i jej wyjaśnienie bajtowo bez zmian; nowe decyzje liczą nowe | B01, B03, B09, E03 |
| MR-20 | odpowiedź grafowa vs liniowa z tego samego przebiegu | każde twierdzenie liniowe ma odpowiednik grafowy z cytatem albo oznaczenie „bez źródła”; różnica zbiorów zgłoszona; pakiet identyczny niezależnie od trybu widoku | C02, C05 |

## 5. Jak to wchodzi do grafu (R53: jako propozycje, nie fakty)

Przypadki są danymi. Proponowany pack (do utworzenia przez agenta po przyjęciu
tego dokumentu; dokument nie tworzy go sam): `loom/data/validation/edge_cases.pack`,
schemat `loom.edge_case/1`:

```json
{
  "schema": "loom.edge_case/1",
  "id": "EC-A03",
  "group": "memory_time",
  "operators": ["negate"],
  "scenario": {"pl": "…", "asr_variants": ["…", "…"]},
  "interpretation": "…",
  "graph_place": {"nodes": ["source"], "edges": ["learn:false"], "qualifiers": ["initiator", "known_at"]},
  "governing_settings": [{"key": "learning.source_policy", "scope": "conversation"}],
  "sought_links": ["same_period_sources", "single_source_preferences"],
  "invariant": ["source_bytes_unchanged", "fts_hits_unchanged", "no_active_preference_with_sole_premise"],
  "metamorphic": [{"mr": "MR-14", "transform": "quarantine_then_lift", "relation": "identity_on_observations"}],
  "breaks": ["A4", "A5"],
  "status": "proposal",
  "provenance": {"author": "agent:claude-session-S3", "date": "2026-10-11", "source": "docs/architecture/EDGE_CASES_2026-10-11.md"}
}
```

Zasady:
- każdy wpis ma `status: proposal` do czasu przyjęcia przez właściciela (jedno
  słowo) albo automatycznie według ustawienia polityki przyjęcia;
- `breaks` wskazuje abstrakcje z §3, które są osobnymi węzłami (`loom.abstraction/1`)
  z krawędzią `required_by`;
- metoda N-torowa (T11) dostaje §1 (operatory) i ten pack jako zalążek i dopisuje
  nowe wpisy z `provenance.author = model:<id>`, oznaczając zgodność/nowość/sprzeczność
  wobec istniejących;
- testy metamorficzne są generowane z pola `metamorphic` (MR + transformacja →
  szkielet testu doctest/pytest z zamrożonym wejściem), nigdy ręcznie przepisywane
  z prozy.

## 6. Mapowanie na wątki i co zmienia w briefach

| Wątek | Przypadki do testów w tym wątku | Zmiana w briefie wynikająca z przypadków |
|---|---|---|
| T1 WP-B import | D01, D02, D05, D06, D07, D09, D10, D11, B07 | status rozpoznania z **historią prób** (A10); `projection_of` zamiast `same_as` dla rozmów z dwóch eksportów (A9); podmiot źródła ≠ właściciel (A2) |
| T2 WP-C worker | A04, C03, C10, I03 | przeliczenie po domknięciu zależności (A5), nie po statusie; determinizm metody jako dana |
| T3 WP-D widok wejściowy + ontologia | A06, B01–B03, H01, H05–H07, J01, J02, J04 | `parties.pack`, `scopes.pack`, `classes.pack` z osi; rola@czas; licznik rozjazdów rekomendacja/ustawienie; liczniki `{known:false, stale_since}` |
| T4 WP-E ekonomia | D03, D05, F01, F06, F07, H02 | osie pokwitowań (A15); cennik z datą; „użyte w” |
| T5 WP-F magazyn | E01–E07, E09, E10, I06 | `stored_at` w lokatorze (A7); rejestr egressu (A8); crypto-shredding jako ustawienie; łańcuch kluczy |
| T6 strażnik ×10 | C09, F02, F05 | MR-13 jako test: identyczne zachowanie na czacie, wsadzie, grafie, eksperymencie; skalowanie przez k |
| T7 Python | A09, D12, G03 | surowy ASR i metadane zegara zachowane w `messages.metadata` bez zmiany schematu |
| T11 N-torowa | §1, §5 | operatory przesady jako prompt; wynik jako wpisy packa |
| T13 onboarding | A05, B04, B10, J05, J06 | zasięg `never` (A13); stan podmiotu (A12); `by_context` |
| T14 akceptacja | wszystkie z MR-4 i MR-13 | ścieżki z telefonu w `ACCEPTANCE_TESTS_2026-10-11.md` |

Przypadki bez wątku dziś: A01/A02/J04 (A1 bitemporalność), D04/E07/I01/I02
(A18 instalacje), C05 (A19), F03 (A23). Decyzja (Claude, odwracalna): A1 wchodzi
do T3 jako kwalifikator ontologii (pole, nie migracja); A18 czeka na T5 (sync);
A19 i A23 to osobne pakiety po T8.
