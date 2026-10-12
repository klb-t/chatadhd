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
