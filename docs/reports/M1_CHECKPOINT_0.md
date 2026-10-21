# M1 — Brama 0: co możesz już dotknąć na telefonie (2026-10-21, gałąź `claude/m1-w0`)

Twoje słowa o ostatnim APK: „cokolwiek sensownego robi, ale nie te 15%, tylko bliżej trzech – czterech” — tamten
APK miał tylko polecenia w czacie. To jest pierwsza rzecz, którą możesz **dotknąć**: graf, ustawienia po ludzku
i role modeli. Poniżej krok po kroku, co dziś działa, a na końcu wprost, czego jeszcze nie ma i dlaczego.

Skąd są zrzuty: z **prawdziwej aplikacji** (jądro `loom-server` na pustym katalogu danych, bez modelu, bez sieci) w
przeglądarce o rozmiarze telefonu (390×844, dotyk). To nie jest jeszcze emulator Androida — ten przebieg (`android-smoke`,
pozycja M1-A0) robi sesja S2 na podstawie `docs/reports/M1_W0_TESTIDS.md`. Każdy zrzut w `docs/reports/M1_CHECKPOINT_0/`
powstał w teście, który sprawdza to samo, co widać (`loom/web/e2e/m1-navigator-native.mjs`, `m1-settings-native.mjs`):
żadna liczba na ekranie nie jest wpisana na sztywno — wszystkie pochodzą z odpowiedzi jądra i z danych (`*.pack`).

## 1. Menu ⋮ — bez „Pamięci” i „Wiedzy”

![menu](M1_CHECKPOINT_0/m1-menu.png)

Dotknij **⋮** w prawym górnym rogu. Pozycje: **Graf · Ustawienia · Ty · Ta rozmowa w grafie · Widoki i okna · Analiza w tle ·
Warsztat (narzędzia zaawansowane)**; na Androidzie dochodzi **Połączenie**. Nie ma „Pamięci” ani „Wiedzy” — zgodnie z Twoją
uwagą, że to mylące: pamięć to graf widziany od węzła **Ty**, a wiedza to warstwa dowodów w grafie. Dawne panele nie zniknęły —
są w **Warsztacie**. Menu „+” przy polu wiadomości (P33) zostało bez zmian. **Graf · Ty · Ta rozmowa w grafie** otwierają
**ten sam nawigator**, tylko w innym miejscu: Graf od korzenia, Ty od węzła **Ty** (Twój profil, preferencje, zainteresowania,
„Co pamiętam”), Ta rozmowa od węzła otwartej rozmowy (bez otwartej rozmowy — od korzenia, nigdy od węzła, którego nie ma).

![Ty w grafie](M1_CHECKPOINT_0/m1-navigator-me.png)

## 2. Graf — korzeń: wiersze z jądra, licznik albo „nieznane (powód)”

![korzeń grafu](M1_CHECKPOINT_0/m1-navigator-root-list.png)

**⋮ › Graf** otwiera nawigator. Każdy wiersz to obszar, który odpowiada jądro (`graph.navigate`) — nic nie jest wpisane w kodzie.
W wierszu: nazwa, **licznik** (ROM 13, Ty 14) albo uczciwe **„nieznane (żadne źródło nie wypisuje jeszcze elementów tego
obszaru)”** — nigdy „0”, gdy aplikacja po prostu nie umie policzyć; **chip pochodzenia** („Preset (ROM)”); jedno zdanie
**po co**. Na dole pasek: **Perspektywa ▾ · Szerzej · Węziej · Głębiej · Płycej · Działania · Widok ▾** (nad paskiem gestów
Androida — `safe-area-inset-bottom`). Szerzej/Węziej podwaja/dzieli budżet wierszy na poziom (zwinięte wiersze są wymienione,
nie ukryte), Głębiej/Płycej zmienia głębokość. Lupa w wierszu ścieżki szuka na bieżącym poziomie. Linia pod paskiem mówi, co
widzisz: **„budżet 50 węzłów na poziom · głębia 1 · 25 z 25”** — ile wierszy mieści się na jednym poziomie (to ustawienie
„Budżet węzłów na poziom” w Ustawienia › Interfejs), ile poziomów w głąb i ile z wszystkich wierszy jest na ekranie.

## 3. ROM — 13 pozycji po polsku, ze stanem

![ROM](M1_CHECKPOINT_0/m1-navigator-rom.png)

Dotknij **ROM (pamięć tylko do odczytu)**. Ścieżka u góry: **Graf › ROM…** (dotknięcie członu cofa, **‹** cofa o jeden).
Pod ścieżką jedno zdanie, po co jest ten obszar. 13 pozycji (Rozruch, Meta-ontologia, Interpreter akcji, Sterowniki i
adaptery, Ustawienia fabryczne, …) — każda z chipem **Preset (ROM)** i stanem **jest w aplikacji / częściowo w aplikacji / planowane** (czy ta część
aplikacji już działa) oraz zdaniem, co robi. **⋯** przy wierszu albo dotknięcie liścia otwiera **kartę węzła** pod listą: chip pochodzenia i stan
słowami, po co, „Skąd jest wartość”, „Powiązania”, „Co możesz zrobić” (działania jądra słowami; te bez wykonawcy mają chip „planowane”).
Karta pozycji ROM nie ma osobnej linii „Wartość” — jej wartością z jądra jest sam stan, a ten mówi chip (nie ma na ekranie
gołego `partial`).

![karta Rozruchu](M1_CHECKPOINT_0/m1-navigator-card-rom.png)

## 4. Karta ustawienia — wartość, po co, co zmienia, skąd

![karta Temperatury](M1_CHECKPOINT_0/m1-navigator-card-setting.png)

Graf › Profile i ustawienia › Rozmowa › **Temperatura (swoboda odpowiedzi)**: chip „preset zapisany w Twoich ustawieniach”,
**Wartość: 0,7**, **Po co**, **Co zmienia** (od następnej odpowiedzi; analiza w tle i archiwum mają
własne wartości), **Zasada edycji** (fakultatywna — możesz zmienić albo zostawić puste), **Czyta to** (przepływy, które
czytają pole). Skąd jest wartość, mówi **jeden** opis — łańcuch poziomów w karcie (pkt 5); nie ma nad nim drugiej linii
„Warstwy: Preset → Ty” innymi słowami, bo dwie nazwy jednego poziomu czytały się jak sprzeczność („Preset → Ty” nad
„Preset aplikacji — nic tu nie ustawiono”).

## 5. Zmień → „Dla kogo?” → cały łańcuch dziedziczenia

![łańcuch dziedziczenia](M1_CHECKPOINT_0/m1-navigator-card-for-whom.png)

**Zmień** otwiera edytor wg typu pola (liczba, tekst, przełącznik). Potem **„Dla kogo?”**: poziomy, które pole dopuszcza
(dla Temperatury: Ty i ta rozmowa), a **„więcej poziomów”** pokazuje cały łańcuch — z powodem, gdy na poziom nie da się
dziś zapisać (np. „bez projektu”). Zapis dla rozmowy pyta o potwierdzenie (reguła jądra), zapis dla Ciebie idzie od razu.
**„Skąd jest wartość”** wylicza każdy poziom: Silnik (niezmienniki) → Preset aplikacji → Instalacja (przesłonięte, 0.7) →
Organizacja (bez organizacji) → Grupa grup → Zespół → **Osoba — daje wartość (0,7)** (podświetlona) → Rola w kontekście →
Projekt (gałąź) → rozmowa → tura. **„Zdejmij wartość: Ta rozmowa (0,3) — wróci: Osoba (0,7)”** wyłącza regułę rozmowy
(zostaje w historii, nic nie jest kasowane).

## 6. Mapa — te same węzły, dotknięcie = nazwa i karta

![mapa](M1_CHECKPOINT_0/m1-navigator-map.png)

Pod listą jest **mapa** tego samego poziomu (rysowana silnikiem widoków, układ sektorowy: ognisko w środku, dzieci wokół).
Dotknij kółka — pod mapą pojawia się pełna nazwa i chip, a niżej ta sama karta co z listy; **Wejdź ›** schodzi poziom niżej.
Podpis przy kółku jest tylko tam, gdzie mieści się w mapie i nie zasłania innego podpisu (najpierw ognisko i dotknięte
kółko, potem kolejne wiersze); kółko bez podpisu nie jest ukryte — jego pełna nazwa jest pod mapą po dotknięciu.
**+ / −** i gest dwoma palcami powiększają, **⌂** wraca do środka. Każde kółko ma cel dotyku 44 px. Prostokąt mapy jest
zgłaszany powłoce Androida (`LoomBridge.setGestureExclusion`), żeby przesuwanie mapy nie było brane za gest „wstecz”.
**Widok ▾** przełącza Lista / Mapa / Razem — wybór zapisuje się jako Twoje ustawienie `navigator_layout`
(„Zapisane dla Ciebie: Lista i mapa razem”). Lista i mapa zwijają się osobno („Ukryj listę”, „Ukryj mapę”), nic nie znika.

## 7. Ustawienia — osiem grup po ludzku

![grupy ustawień](M1_CHECKPOINT_0/m1-settings-groups.png)

**⋮ › Ustawienia**: osiem grup z jądra (`settings.resolved`), w kolejności danych: **Ty · Rozmowa · Modele · Kontekst ·
Interfejs · Prywatność · Koszty · Silnik**, każda z „**N pól · M Twoje**” i zdaniem, co w niej jest. Dziś: **109 pól**
(liczba z packa `settings_fields.pack`, test sumuje grupy przeciw niej). „Twoje” to pola, którym nadałeś wartość Ty
(dla siebie, rozmowy, projektu) — preset i preset zapisany w Twoich ustawieniach się nie liczą. Przełącznik **Pokaż tylko
zmienione** zostawia same Twoje pola.

## 8. Grupa — pola z opisem i pochodzeniem

![grupa Rozmowa](M1_CHECKPOINT_0/m1-settings-rozmowa.png)

Dotknij grupy (np. **Rozmowa**): „**‹ Wróć do grup ustawień**”, podgrupy (Ogólne, …), każde pole z **polską nazwą**,
**wartością słowami** (0,7; 4096; „włączone”), **chipem pochodzenia** jądra i zdaniem **po co**. Dotknięcie pola rozwija
**Co zmienia**, **Czytają to przepływy**, **Zmiana**; **⋯** otwiera tę samą kartę co w grafie (zapis przeładowuje liczniki).
Pole bez czytelnika w kodzie ma chip **planowane** i zdanie, dlaczego (z danych) — żadne pole nie jest ukryte.

## 9. Modele › Role — kto wykonuje którą pracę

![role](M1_CHECKPOINT_0/m1-settings-roles.png)

Na górze grupy **Modele** stoi tabela ról z jądra (`roles.resolved`): Rozmowa, Analiza w tle, Sekretarz, Rozpoznanie
polecenia, Obraz, Głos, Osadzenia, Mowa→tekst, Tekst ze zdjęcia, Model dla działania, Formaty plików — każda z **modelem
słowami**, **chipem klasy** (tani / średni / frontier / brak modelu) i stanem („wybór przez rolę: częściowo / planowane” — bez chipu, gdy rola już wybiera model; jedno zdanie nad tabelą mówi, co znaczą chipy) oraz zdaniem, co ta rola robi.

## 10. Karta roli — odpowiedź na „dlaczego jest do wyboru model do analizy, jak są różne flowy”

![karta roli](M1_CHECKPOINT_0/m1-settings-role-chain.png)

Dotknij **Analiza w tle**. Karta mówi **z danych**: **Po co**, **Dziś** (które prace naprawdę wybierają model przez tę rolę),
**Dziś** (odpowiedź słowami; „Dlaczego tak jest” to zdanie po ludzku z danych ról (`reason_human`); notatka techniczna jądra — „Zapis techniczny (pliki, klucze)” — jest złożona pod **Szczegóły techniczne**, zamknięte, dopóki jej nie dotkniesz); **Model** i **Klasa** z powodem słowo w słowo z jądra („tani, bo nazwa zawiera „mini” …”); **Jak
wybieramy model** — łańcuch polityki krok po kroku (1. wprost: pole „Model analizy w tle”; 2. rola; 3. brak — analiza
lokalna, bez kosztu), z chipem **„ten krok wybiera”** przy tym, który zadecydował; **Przepływ dla każdej klasy modelu** —
tani/średni/nieznany = „Klasyczne wywołanie i wąskie pytanie”, frontier = „Jedno wywołanie: cała rozmowa naraz”, oba z chipem
**kalibracja** tam, gdzie dane tak mówią, a klasa Twojego modelu jest zaznaczona.

![przepływy czytające rolę](M1_CHECKPOINT_0/m1-settings-role-readers.png)

**Które przepływy czytają tę rolę** — **wszystkie**, które pracują jako ta rola albo czytają jej ustawienie
(lista z katalogu działań, dziś 15: Przeanalizuj wiadomość w tle, Analiza zbiorcza w tle, Dopracuj elementy archiwum,
Określ cel promptu, … „Kto wykonuje którą pracę (czyta semantic_model)”). To jest odpowiedź na Twoje pytanie: jedno pole
czyta kilkanaście prac, a **klasa wybranego modelu decyduje, który przepływ ruszy**. Niżej **Tryb pracy w tle** (zbiorczo /
taniej / modelem / lokalnie, z progiem 500 oczekujących) i **koszt** albo powód „nieznana”.

## 11. Silnik › Wszystkie pola — nic nie zniknęło

W grupie **Silnik** na dole jest zwijane **„Pokaż Wszystkie pola”**: dawny ekran ustawień w całości (tabela pól pod nazwami
technicznymi, połączenie, modele, klucze, JSON). Prośba o wpisanie klucza (karta na starcie, podpowiedzi w czacie) otwiera
Ustawienia od razu tutaj.

## 12. Wstecz

Przycisk **wstecz** (Androida, przeglądarki, Escape na komputerze) zamyka **jedną warstwę na raz**, od góry: kartę →
krok ścieżki (Rozmowa → Profile i ustawienia → Graf) → panel; w Ustawieniach: kartę roli → grupę → panel. Na dole stosu
oddaje decyzję powłoce (`window.__loomBack()` zwraca `false` — tam Android pyta, czy wyjść; to pozycja M1-A0).

## Co doszło w poprawkach r1:b2 (2026-10-21, fakty z przebiegu)

- **Perspektywa ▾** na pasku otwiera listę perspektyw jądra (`GET perspectives` / `loom_perspectives`: Zależności, Wywołania,
  Historia wersji, Teleologia, Struktura, Środowiska i dostęp, Dziedziczenie ustawienia — tyle, ile jądro ma, w jego kolejności i po
  polsku). Zaznaczasz jedną lub kilka; wiersze są wtedy tym, dokąd te relacje prowadzą z ogniska (`graph.navigate` z `perspectives`,
  tak głęboko, jak mówi głębia). Zaznaczenie zostaje, gdy wchodzisz głębiej i wracasz; „żadna — własne dzieci węzła” wraca do
  zwykłej listy. Linia pod paskiem mówi, które perspektywy są włączone; pusta odpowiedź mówi „Żadna relacja z wybranych
  perspektyw nie wychodzi z tego węzła.” Zrzut: `m1-navigator-perspective.png`.
- **Działania** otwiera kartę ogniska przewiniętą do „Co możesz zrobić” (działania jądra z `graph.card`, słowami). W korzeniu nie
  ma węzła, więc pasek mówi: „Działania są na karcie węzła: otwórz wiersz przez ⋯ albo wejdź w jeden.” Żaden przycisk paska nie
  mówi już „planowane”. Zrzut: `m1-navigator-actions.png`.
- **Karta roli:** „Dziś” jest odpowiedzią; notatka techniczna jądra (nazwy plików, klucze) leży zamknięta pod „Szczegóły techniczne”
  (od r1:b3 jądro przysyła `reason_human` z `ports.pack`: „Dlaczego tak jest” widać od razu po ludzku, pod „Szczegóły techniczne” leży „Zapis techniczny (pliki, klucze)”). Chipy stanu mówią, czego dotyczą: „wybór przez rolę: planowane / częściowo”,
  a nad tabelą ról stoi jedno zdanie, co znaczą chipy. Lista „Kto czyta” nazywa klucz etykietą pola („czyta Model analizy w tle”),
  goły klucz jest tylko w podpowiedzi (`title`).
- **Słowa bez identyfikatorów:** „planowane — silnik nie ma dziś akcji na wartość jednej tury” i „Zapisane dla Ciebie: Razem” bez
  nawiasów z nazwami akcji, pozycji planu i kluczy (te są w INDEX).
- **Start z linku** (`#czat/<id>`, `#warsztat`) wczytuje konfigurację jak zwykły start: język słów (`ui_language`), motyw i układ
  nawigatora (`navigator_layout`) są Twoimi ustawieniami, nie językiem przeglądarki. E2E karty ustawień startuje teraz z linku.

## Co doszło w poprawkach r1:b3 (2026-10-21, fakty z przebiegu)

- **× każdego ekranu ma 44 × 44 px** (było 38 × 38 — jedyny element pod progiem w 9 stanach szuflady) i etykietę ze słów powłoki:
  „Zamknij: Ustawienia” (`panel_close`, pl/en), nie angielskie „Close panel”. Testy e2e mierzą teraz cały `.drawer`, nie tylko
  korzeń ekranu.
- **Karta roli bez żargonu na wierzchu, z danych:** każdy z 11 stanów ról w `ports.pack` ma obok notatki inżynierskiej (`reason`)
  zdanie po ludzku (`reason_human`), np. „Większość prac analizy powstała, zanim były role, i czyta pole „Model analizy w tle”
  wprost z ustawień zamiast pytać tej roli”. Jądro je przysyła, karta pokazuje „Dlaczego tak jest: …” od razu, a notatkę z nazwami
  plików jako „Zapis techniczny (pliki, klucze)” pod „Szczegóły techniczne”. Ten sam podział na karcie węzła: „Zdejmij wartość: Ty”
  mówi „planowane — silnik umie dziś zmienić wartość w Twoich ustawieniach, ale nie umie jej zdjąć”, a `config.json`/`settings.set`
  leżą pod „Szczegóły techniczne”. Zrzuty odświeżone: `m1-settings-role-card.png`, `m1-navigator-card-unset.png`.
- **Pierwsze kroki (CI `package.yml/first-steps --ui`, `docs/PIERWSZE_KROKI.md` §5):** krok 5 szedł do dawnego ekranu pól wprost
  z menu, a od P5 menu ⋮ › Ustawienia otwiera osiem grup; skrypt idzie teraz Ustawienia › Silnik › „Wszystkie pola” › pola, a
  dokument opisuje tę drogę i kartę pierwszego uruchomienia. Sonda tych samych kliknięć na serwerze dev (1100 × 760 i 390 × 844):
  `settings-panel` zaraz po menu = false, po Silnik › Wszystkie pola = true (`cfg-base-url`, `cfg-default-model`,
  `secret-api_key`, `secret-api_key-save` widoczne).
- **Dwa zgłoszenia już zrobione w r1:b1** (`8c747ce9`): „Ta rozmowa w grafie” otwiera nawigator na `conv:<otwarta rozmowa>`
  (korzeń, gdy żadna nie jest otwarta), nie dawny `panel:graph`; siedem testów M1 (`navigator-state`, `node-card-state`,
  `navigator-map`, `settings-human`, `role-card`, `m1-navigator`, `m1-settings`) jest w `loom.yml`.
- Testy (fakty): `tsc` + `npm run build` czyste; jądro dev przebudowane przyrostowo (po `ports.pack`/`projection.cpp`/`test_ports.cpp`, potem po `builtin.inc`); `ctest -R "ports|graph_projection|onboarding"` 11/11; testy jednostkowe web (0 fail): shell-history 13, shell-menu 8, navigator-state 9, node-card-state 8, navigator-map 9, settings-human 7, shell 9, composer-menu 13, role-card 5; `test:m1-navigator` 12/12, `test:m1-settings` 9/9 (× mierzony w całym `.drawer`, „Dlaczego tak jest” widoczne bez dotknięcia, „Zapis techniczny” złożony do dotknięcia); sonda kroku 5 pierwszych kroków na serwerze dev (1100 × 760 i 390 × 844) zielona, `close-panel` 44 × 44 „Zamknij: Ustawienia”. Nie uruchomione: `first-steps-check.sh --ui` z archiwum, pełne `npm run e2e`, `android-smoke`.

## Czego jeszcze NIE ma (i dlaczego)

- **Przełącznik „Ustawiam dla: mnie / tej rozmowy / projektu…”** nad listą pól — dziś poziom wybierasz w karcie przy
  każdej zmianie („Dla kogo?”). **Chip „Propozycja modelu”** — jądro mówi wprost: „Dziś żadne pole nie ma propozycji
  strażnika …” (brak mapowania hipotezy na klucz w `guardian.pack`).
- **„Zdejmij wartość: Osoba”** jest wyłączone z powodem: jądro nie ma dziś akcji, która usuwa klucz z Twoich ustawień
  (`settings.unset`, pozycja M1-K1); zdjęcie warstwy rozmowy/projektu działa (reguła zostaje wyłączona, nie skasowana).
- **Przepływ zmieniający się na żywo** przy wpisywaniu modelu roli i **„przypisz klasę ręcznie / zmień progi”** — chipy
  „planowane” z powodem (klasa wynika ze wzorców nazw i progów cen; nie ma jeszcze pola, które zapisuje Twoje klasy).
  Zmiana modelu roli = ta sama karta pola co w grafie. Bez wybranego modelu karta roli mówi, że przepływu jeszcze nie ma.
- **Obszary „nieznane (powód)”** w korzeniu grafu (Samomodel, Technologie i adaptery, Interfejsy, …): żadne źródło
  nie wypisuje jeszcze ich elementów — licznik pojawi się razem ze źródłem, nie wcześniej. Na mapie podpisy są skracane
  do 12 znaków i rysowane tylko tam, gdzie mieszczą się w mapie bez zasłaniania innych (cała nazwa pod mapą po dotknięciu).
- **Nie na emulatorze Androida.** Powłoka (wstecz przez `OnBackPressedDispatcher`, `adjustResize`, prostokąty gestów,
  pytanie o wyjście) i przebieg `android-smoke` to M1-A0 (sesja S2). Kroki dla sterownika są w bloku `gate0` w
  `docs/reports/M1_W0_TESTIDS.md`.
- Perspektywa ▾ to lista jądra i jego krawędzie; **sektory / obok siebie** i renderery z `view_renderers` to M1-W3. `actions.for`
  bierze element (klasa, cechy), nie adres grafu — Działania pokazują działania z `graph.card`, które jądro liczy dla węzła.

## Co sprawdzają testy (fakty z tego przebiegu)

- **r1:b2 (2026-10-21):** `test:m1-navigator` 12/12 — w korzeniu Działania mówi, gdzie są działania (bez karty); Perspektywa ▾ = opcje
  równe `GET /api/perspectives` (kolejność, polskie nazwy), dwie zaznaczone → wiersze równe `graph.navigate` z `perspectives`
  (ten sam budżet i głębia), linia „Perspektywy: Struktura · Zależności”, „żadna” → wiersze równe zwykłej odpowiedzi; Działania w
  ROM → karta `area:rom` z `data-section=actions`, sekcja działań w oknie, działania jądra słowami; karta ustawienia po starcie z
  `#czat/<id>` po polsku. `test:m1-settings` 9/9 — zdanie o chipach nad tabelą, chip stanu = słowo z katalogu z dwukropkiem,
  „Szczegóły techniczne” zamknięte → otwarte → notatka jądra, „Kto czyta” z etykietą pola, bez gołego klucza w tekście, klucz w
  `title`. Jednostkowe: `test:navigator-state` 9/9 (perspektywy, cel Działań), `test:role-card` 5/5 (chip z podmiotem),
  `test:navigator-map` 9/9 (`view.saved` bez klucza).

- `npm run test:m1-navigator` (`e2e/m1-navigator-native.mjs`, 11 kroków): korzeń = odpowiedź jądra, ROM 13 wierszy po
  polsku ze stanem, szukanie, Szerzej/Węziej/Głębiej/Płycej, karta ROM i karta Temperatury z całym łańcuchem, Zmień → Dla kogo?
  → zapis dla rozmowy (potwierdzenie) i dla Ciebie, Zdejmij wartość, mapa (te same węzły, dotknięcie, +/−/pinch, prostokąt dla
  powłoki, Widok ▾ zapisuje), Wstecz po kolei. `npm run test:m1-settings` (`e2e/m1-settings-native.mjs`, 9 kroków): menu =
  wpisy `layouts.json` bez Pamięci/Wiedzy, 8 grup w kolejności danych z licznikami zsumowanymi przeciw jądru i packowi,
  Rozmowa › Temperatura z chipem jądra i przepływami, Pokaż tylko zmienione, pole planowane z powodem, tabela ról = `roles.resolved`,
  karta Analizy w tle (łańcuch słowo w słowo, klasa z powodem, przepływ każdej klasy, każdy czytelnik), Wstecz, Silnik › Wszystkie pola.
- W każdym kroku: **brak poziomego przewijania** (`scrollWidth ≤ 390`), **każdy przycisk ≥ 44 px** (dawny ekran pod
  Wszystkie pola jest wyłączony ze sprawdzenia — ma 35–40 px i nie był ruszany), **żaden id słowa na ekranie** (brak słowa
  w katalogu pokazałby `card.xxx`, nigdy cichy angielski).
- Zrzuty: 12 plików PNG 390×844 (widok, nie cała strona; każdy < 110 KB) w `docs/reports/M1_CHECKPOINT_0/`, zapisane przez
  te testy z `M1_SHOTS_DIR=docs/reports/M1_CHECKPOINT_0`.
- Poprawki po obejrzeniu zrzutów (P7): Graf otwierał się **przewinięty do mapy** (ostatni wiersz na górze) — zwijany blok
  przewijał się do siebie przy pierwszym rysowaniu; teraz robi to tylko po dotknięciu „Pokaż …”. Zrzut menu ⋮ łapał animację
  w połowie (menu półprzezroczyste nad kartą klucza) — zrzuty czekają na koniec animacji. Chipy stanu w wierszach ROM mówiły
  samo **„jest”** i **„częściowo”** (jest — co?); teraz mówią **„jest w aplikacji” / „częściowo w aplikacji”**, a linia pod
  paskiem **„budżet 50 węzłów na poziom”** zamiast „budżet 50 na poziom” (to samo słowo, co nazwa ustawienia). Tylko słowa
  w katalogu (`presentation.graph_screen`, `presentation.settings_human`), żaden kod ekranu nie zmieniony.
- Przebieg końcowy (2026-10-21, po tej poprawce): `npm run build` czysty; **wszystkie 56 skryptów `test:*`** z `package.json`
  zielone (w tym oba m1: 11/11 i 9/9, z nowymi zrzutami) i **`npm run e2e`** (`run.mjs`) zielony. Jeden skrypt,
  `test:product-integration`, za pierwszym razem stanął nie na błędzie ekranu, lecz na swojej zasadzie „nie nadpisuję
  wcześniejszego dowodu”: `e2e/onboarding-host.mjs` zastał plik wyników z 16 października w domyślnym katalogu obok
  repozytorium (wszystkie 21/21 grup przeszły); z własnym katalogiem (`ONBOARDING_HOST_EVIDENCE_DIR`) cały skrypt przeszedł.
  Po poprawce słów cały zestaw powtórzony od nowa na końcowym drzewie: **59/59 zielone, 0 błędów**.
