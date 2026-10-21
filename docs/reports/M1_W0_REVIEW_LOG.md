# M1-W0 — dziennik poprawek po przeglądach (2026-10-21, gałąź `claude/m1-w0`)

Zapis techniczny każdej rundy poprawek po przeglądzie M1-W0 (pliki, testy, liczby, decyzje). Do r2:b1 leżał w raporcie
dla właściciela (`docs/reports/M1_CHECKPOINT_0.md`); przegląd r2:b2 uznał, że tam nie pasuje (raport ma mówić słowami
właściciela, co da się zrobić na telefonie), więc sekcje przeniesiono tu bez zmian treści. Wpisy w `docs/reports/INDEX.md`
(„S1 / M1-W0 (poprawki po przeglądzie …)”) mają dla każdej rundy pliki, generaty, fakty testów i jak cofnąć.

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

## Co doszło w poprawkach r1:b4 (2026-10-21, fakty z przebiegu)

- **Jeden zapis liczb na karcie:** łańcuch dziedziczenia pisał „Instalacja przesłonięte 0.7”, a „Wartość: 0,7” — teraz wartości
  poziomów idą przez słowo ekranu `decimal_separator` (pl „,”, en „.”; `presentation.graph_screen`), tak jak jądro pisze
  `value_human` (`numberText` w `node-card-state.ts`); „Zdejmij wartość: Rozmowa (0,3)” i „wróci: Osoba (0,5)” tak samo. Bez słowa
  zostaje kropka (nigdy id). Zrzuty: `m1-navigator-card-for-whom.png`, `m1-navigator-card-setting.png`, `m1-navigator-card-unset.png`.
- **Linia pod paskiem ma rzeczownik:** „budżet 50 węzłów na poziom · głębia 1 · pokazano 25 z 25 wierszy” (było „25 z 25”);
  słowo `children`, pl/en. Widać na każdym zrzucie nawigatora.
- **Kontrakt `gate0` dla S2 (`M1_W0_TESTIDS.md`) mówi, co musi być przed blokiem:** `ui_language = pl` w config (preset `auto` =
  język przeglądarki, WebView emulatora ma `en-US` — bez tego kroki 3 i 4 padały: „In all: 109 fields · 0 yours”, „Background
  analysis”) i powłoka Androida (`shell-menu-connection` jest tylko z `LoomBridge`). W JSON doszedł klucz `preconditions`
  (`{"config": {"ui_language": "pl"}, "android_shell": true}`), wzorce `text` są dwujęzyczne (pl|en, z „1 pole”), a oczekiwanie
  połączenia ma `"android_only": true`. Blok nadal parsuje się jako `{"gate0": [5 kroków]}`.
- **Dwa zgłoszenia już zrobione wcześniej (sprawdzone w kodzie, bez zmian):** × 44 × 44 px z etykietą ze słów powłoki — r1:b3
  (`styles.css:136`, e2e mierzą cały `.drawer`); start z `#czat`/`#warsztat` czyta config (`ui_language`, motyw, układ) — r1:b2
  (`App.tsx:108–110`, krok e2e „karta ustawienia po starcie z `#czat/<id>` po polsku”).
- Testy (fakty): `tsc` + `npm run build` czyste; `gen_onboarding_pack.py --check` i `gen_runtime_profiles.py --check` czyste; jądro
  dev przebudowane przyrostowo po `builtin.inc`; jednostkowe web: node-card-state 8/8 (liczby w łańcuchu „0,5”/„0,7”, bez słowa
  „0.7”), navigator-state 9/9, navigator-map 9/9, settings-human 7/7, role-card 5/5; `test:m1-navigator` 12/12 (porównanie
  łańcucha i „Zdejmij wartość” przez to samo słowo separatora), `test:m1-settings` 9/9; 18 zrzutów zapisanych ponownie przez te
  testy. Nie uruchomione: `ctest`, pełne `npm run e2e`, `android-smoke`, sterownik S2 z nowym blokiem (sprawdzony tylko parserem
  i wzorcami na trzech próbkach tekstu).

## Co doszło w poprawkach r1:b5 (2026-10-21, fakty z przebiegu)

- **Ścieżka nie ucina jedynej nazwy poziomu:** ostatni okruszek („ROM (pamięć tylko do odczytu)”, „Profile i ustawienia”) zawija
  się na drugą linię (`white-space: normal`, `graph-navigator.css`); wcześniejsze okruszki skracają się pierwsze (wielokropek, nie
  poniżej 56 px). Zrzuty: `m1-navigator-rom.png`, `m1-navigator-card-rom.png`, `m1-navigator-card-setting.png`.
- **Klasa modelu bez przypadku:** wzorzec klasy taniej „mini” łapał „gemini” (gemini-2.5-pro wychodził „tani”, a powód na karcie
  roli to przyznawał). Wzorzec to teraz „-mini” z myślnikiem (`guardian.pack`): gemini-2.5-flash jest tani przez „flash” (wzorce
  „flash” i „lite” już były), gemini-2.5-pro jest frontier przez „pro”, gpt-4o-mini i gpt-5-mini tanie przez „-mini” (klasa tania
  pierwsza). Powód na karcie: „tani, bo nazwa zawiera „flash” (wzorzec to fragment nazwy; „-mini” ma myślnik, żeby nie łapać
  „gemini”)”; `tier_rule` mówi to samo. `test_guardian` (M1-D1) oczekuje nowych klas. Zrzut: `m1-settings-role-card.png`.
- **Powiązania mówią o konkretnym węźle:** nagłówek grupy to czasownik i węzły („jest częścią: ROM (pamięć tylko do odczytu)”, a od
  strony ROM „zawiera: Rozruch…”), nie znaczenie rodzaju krawędzi („Źródło jest częścią celu.”). Jądro dosyła w `graph.card`
  `inverse` i `inverse_label` rodzaju (`graph_views` `edge_kinds.inverse`, nowe `Catalogue::edge_inverse`); etykieta `part_of` to
  „jest częścią” / „is part of” (była „część”). Rodzaj bez odwrotności ma jeden nagłówek i strzałki kierunku przy wierszach.
  Zrzut: `m1-navigator-card-rom.png`.
- **Wstęp Ustawień liczy grupy, które rysuje:** „8 grup. W każdej pola z opisem…” — `{{groups}}` z `doc.groups.length` przez formy
  liczby mnogiej `count.groups.*` (pl one/few/many/other, en one/other), tak jak `count.fields`. Zrzut: `m1-settings-groups.png`.
- **Warsztat bez dwóch „Graf”:** stare wejście do grafu nazywa się „Wejście do grafu (stare)” (słowo `workshop_graph_entry`), „Graf”
  zostaje nawigatorowi; stary panel pamięci bierze słowo `menu_memory` („Pamięć”), które już było. Pozostałe etykiety Warsztatu
  (Logs, Operations, Import, Context, Methods, Analysis, Onboarding) nadal po angielsku — nie mają słów w katalogu; osobna paczka.
- Testy (fakty): `tsc` + `npm run build` czyste; `gen_runtime_profiles.py --check` i `gen_onboarding_pack.py --check` czyste; jądro
  dev przebudowane przyrostowo (`projection.h/.cpp`, `test_guardian.cpp`, oba `.inc`); `ctest -R "guardian|graph_projection|onboarding|ports|graph_views|annotations"` 13/13 (M1-D1 klasy modelu z nowymi oczekiwaniami); jednostkowe web: node-card-state
  8/8 (`inverseLabel`), settings-human 7/7 (formy „1 grupa / 2 grupy / 5 grup / 22 grupy”, „8 groups”), navigator-state 9/9,
  navigator-map 9/9, role-card 5/5, shell 9/9, composer-menu 13/13; `test:m1-navigator` 12/12 (krok karty sprawdza nagłówki per
  kierunek, nazwy węzłów i brak zdania znaczenia), `test:m1-settings` 9/9; 12 zrzutów zapisanych ponownie przez te testy. Nie
  uruchomione: pełny `ctest`, pełne `npm run e2e`, `android-smoke`, sonda Warsztatu w przeglądarce (słowo sprawdzone w bundlu).

## Co doszło w poprawkach r1:b6 (2026-10-21, fakty z przebiegu)

- **Brakujące słowo pokazuje swój identyfikator, nie angielski ekran:** tłumacz ekranów (`shell/text.ts`) bierze katalog języka
  w całości albo nie (`featureInLanguage(…, { partial: true })`): gdyby polskiemu katalogowi zabrakło słowa, na ekranie stoi jego
  id (np. `new_chat`), reszta po polsku — wcześniej cały ekran przełączał się po cichu na angielski. Propozycje w czacie zachowują
  dawną zasadę „jeden język na odpowiedź” (bez opcji). Dziś katalogi są kompletne (testy parzystości kluczy); to zabezpieczenie,
  nie naprawa czegoś widocznego. Test: `shell.test.mjs` „a word a language lacks shows its id”.
- **Każda nakładka jest oknem dialogowym dla czytnika ekranu:** szuflada panelu (Graf, Ustawienia, Ty, Ta rozmowa, Warsztat…),
  arkusz Wiedzy i arkusze Widoki/Analiza mają `role="dialog"`, `aria-modal="true"` i `aria-label` = tytuł; fokus wchodzi do
  szuflady przy otwarciu (chyba że pole w środku samo go wzięło) i wraca na przycisk ⋮ przy zamknięciu (`shell/use-dialog-focus.ts`;
  pozycja menu znika razem z menu, więc powrót idzie na ⋮). Krzyżyk arkuszy mówi „Zamknij: <ekran>” jak w panelach. Pole
  wyszukiwania nawigatora ma nazwę (`aria-label`), nie tylko podpowiedź. E2E sprawdza rolę, nazwę, fokus w środku po otwarciu
  i na ⋮ po Wstecz.
- **„nieznane (powód)” we własnej linii pod etykietą:** licznik nieznany (`data-known="false"`, klasa `gnav-count-unknown`)
  zajmuje całą szerokość pod nazwą i jest czytany od lewej; liczba zostaje w linii nazwy po prawej. Zrzut
  `m1-navigator-root-list.png`; e2e mierzy geometrię obu przypadków.
- **Etykiety na mapie — bez zmian:** od r1:b1 `placeLabels` rysuje tylko etykiety, które nie nachodzą na inne (fokus i dotknięty
  znacznik pierwsze; test „labels on the plane are drawn only where they can be read”); na bieżącym `m1-navigator-map.png` nie ma
  nakładających się napisów — „Nowe działa…”, „Gdzie traf…” są wśród znaczników bez etykiety, jedno dotknięcie od pełnej nazwy
  w linii pod mapą. Uwaga z przeglądu dotyczyła wcześniejszego zrzutu.
- **Start z `#czat/<id>` wczytuje konfigurację — bez zmian:** od r1:b2 gałąź linku w `App.tsx` woła `api.getConfig()`
  (język, motyw, układ nawigatora); uwaga z przeglądu cytowała stary kod.
- **Lista testid-ów:** doszły `shum-hidden`, `shum-proposals`, `data-known` na `nav-count` i atrybuty dialogu szuflady.
- **`menu.test.mjs`** był czerwony od r1:b5 (etykieta „graph-entry” zmieniona na `workshop_graph_entry`, wzorzec testu nie):
  wzorzec poprawiony, 8/8.
- Przebieg: `npm run build` czysty; testy jednostkowe shell 10/10, menu 8/8, navigator-state 9/9, navigator-map 9/9,
  node-card-state 8/8, settings-human 7/7, role-card 5/5, composer-menu 13/13, history 13/13; `test:m1-navigator` 12/12 (nowe
  asercje: rola i nazwa dialogu, fokus, nazwa pola szukania, geometria licznika), `test:m1-settings` 9/9; 2 zrzuty zapisane
  ponownie (`m1-navigator-root.png`, `m1-navigator-root-list.png`). Jądro bez zmian (`ctest` nie uruchamiany). Nie uruchomione:
  pełne `npm run e2e`, `android-smoke`.

## Co doszło w poprawkach r1:b7 (2026-10-21, fakty z przebiegu)

- **Żadne słowo na telefonie nie nazywa pakietu ani profilu:** zdanie o klasie modelu na karcie roli (`class.planned`) mówi
  teraz „planowane: dziś klasa wynika z wbudowanych wzorców nazw i progów cen; nie ma jeszcze pola, które zapisuje Twoje
  klasy ani progi” (było „z profilu guardian” — nazwa pliku, której właściciel nie zna). Gdzie te wzorce leżą (`guardian.pack`,
  nakładka profilu), zostaje w opisie stanu funkcji w packu (`status.today` — tekst dla raportu, nie dla ekranu) i w INDEX.
- **`bar.planned` i „(context.modify, M1-K1)” — już ich nie było:** oba zniknęły w r1:b2 (`2ac45ea3`: żaden przycisk paska nie
  mówi „planowane”, Perspektywa ▾ i Działania działają; `card.level.turn_planned` = „planowane — silnik nie ma dziś akcji na
  wartość jednej tury”). Uwaga z przeglądu cytowała słowa sprzed r1:b2; sprawdzone grepem po packu i `git log -S`, bez zmiany.
- **Opis stanu nawigatora mówił nieprawdę:** `status.today` ekranu grafu obiecywał mapę, Perspektywę, Działania i Widok
  „w kolejnych pakietach M1-W0”; wszystkie cztery są na ekranie (mapa i Widok ▾ od P4, Perspektywa ▾ i Działania od r1:b2).
  Zdanie zastąpione opisem tego, co jest. Tekst nie jest pokazywany na telefonie (karta roli czyta stan roli z `ports.pack`,
  nie stan funkcji), ale zasada „teksty mówią, co kod robi DZIŚ” dotyczy też packa.
- Przebieg: `gen_onboarding_pack.py` i `gen_runtime_profiles.py --check` czyste (`builtin.inc`, `conversation-view.json`
  zapisane ponownie); jądro `dev` przebudowane przyrostowo; `npm run build` czysty; testy jednostkowe role-card 5/5, shell 10/10;
  `ctest` (onboarding, presentation store, perspective store, kompat generacji packa) 10/10; `test:m1-settings` 9/9,
  `test:m1-navigator` 12/12; żaden zrzut się nie zmienił (zdanie o klasie leży pod zgięciem zrzutu `m1-settings-role-card.png`).
  Nie uruchomione: pełne `npm run e2e`, pełny `ctest`, `android-smoke`.

## Co doszło w poprawkach r2:b1 (2026-10-21, fakty z przebiegu)

- **Karta bez pustego nagłówka „Skąd jest wartość”:** nagłówek i łańcuch pokazują się tylko, gdy jądro narysowało łańcuch
  poziomów (`inheritance`). Pozycja ROM („Rozruch”) i pole profilu (Ty › „Kim jesteś”) dostają jedno zdanie: „To nie jest
  ustawienie — nie ma tu łańcucha poziomów, z którego dziedziczy się wartość.” (`card.no_value`, testid `card-no-chain`).
  Zrzut `m1-navigator-card-rom.png` i nowy `m1-navigator-card-profile.png`.
- **„Ta rozmowa w grafie” na rozmowie z wiadomościami:** e2e importuje rozmowę z trzema wiadomościami (`POST /api/import`,
  importer nazywa ją „[Import] M1 card”) i sprawdza, że pod rozmową są jej wiadomości jako wiersze, w kolejności jądra:
  „Ty: Jak zaplanować tydzień?”, „Model: Zacznij od trzech priorytetów.”, „Ty: Dzięki, a co z budżetem?” — każda z chipem
  „z bazy rozmów” (nowe słowo `chip.db`; dotąd na ekranie stałoby `chip.db` — e2e to wyłapało, bo rozmowa była pusta), chipem
  dowodu („powiedziałeś to” / „odpowiedź modelu”) i stanem („aktywne”). Zrzut `m1-navigator-this_conversation.png` wymieniony;
  zdanie o tym w §1. Przy okazji słowa dla warstw, które jądro wysyła bez chipa: `chip.onboarding` „z wywiadu”,
  `chip.memory.json` „z Twojej pamięci” (mechanizm, nie plik — zasada r1:b7).
- **Ty › pola profilu bez „Preset (ROM)” i z drogą do odpowiedzi:** wiersz i karta pola profilu (`kind: profile_slot`) mają
  chip z dowodu, który jądro wysyła (`evidence.human`: „pytanie z wywiadu — bez odpowiedzi” / „Twoja odpowiedź w wywiadzie”),
  a nie słowo warstwy `rom`; drugi chip dowodu znika (te same słowa raz). Karta mówi, zanim wypisze działania ogólne:
  „Odpowiesz w wywiadzie: ⋮ › Warsztat › Onboarding” (po odpowiedzi: „Twoja odpowiedź z wywiadu — zmienisz ją tam: …”),
  testid `card-profile-interview`. Czego jądro jeszcze nie daje (wpis niżej w „Czego jeszcze NIE ma”): „po co” z treści
  pytania wywiadu, działanie „Odpowiedz (wywiad)” na karcie, przycisk otwierający wywiad z karty (szuflada nawigatora nie ma
  dziś dojścia do paneli Warsztatu — stąd zdanie z drogą, nie przycisk).
- **Liczby w formie zależnej od języka:** jeden pomocnik (`loom/web/src/shell/plural.ts`, `Intl.PluralRules`) dla nawigatora i
  Ustawień; słowa `count.nodes.*` / `count.rows.*` (pl one/few/many/other, en one/other). Na ekranie: „zwinięto 1 węzeł — zmień
  budżet”, „zwinięto 2 węzły”, „zwinięto 5 węzłów”; pasek: „budżet na poziom: 1 węzeł · głębia 1 · 1 wiersz, pokazano 1”,
  „… 22 wiersze, pokazano 4”. Test jednostkowy 1 / 2 / 5 / 22 (pl) i 1 / 2 / 5 (en); niepoprawny tag języka → forma „other”.
- **Jedna odpowiedź na „moje czy preset?”:** gdy chip karty mówi „preset zapisany w Twoich ustawieniach”
  (`provenance.chip.same_as_preset`), wiersz zwycięski łańcucha mówi te same słowa (nie „daje wartość”), a niższy poziom z tą
  samą liczbą — „ta sama wartość” (nie „przesłonięte”; `data-role` zachowuje rolę jądra). Test jednostkowy na prawdziwej
  karcie jądra dla `temperature`, zapisanej przez e2e do `loom/web/tests/shell/fixtures/graph-card-temperature.json`:
  chip = wiersz „Osoba”, „Instalacja 0,7” = „ta sama wartość”; Ustawienia liczą „Twoje” bez takich pól (ta sama reguła
  `isYours`). Zrzuty `m1-navigator-card-setting.png`, `m1-navigator-card-for-whom.png`.
- Przebieg: `gen_onboarding_pack.py` i `gen_runtime_profiles.py --check` czyste (`builtin.inc`, `conversation-view.json`
  zapisane ponownie); jądro `dev` przebudowane przyrostowo; `npm run build` czysty; testy jednostkowe shell 86/86 (w tym
  navigator-state 18, node-card-state z fixture); `ctest -R "onboarding|presentation|graph_projection|perspective"` 12/12;
  `test:m1-navigator` 12/12 (nowe kroki: import + wiersze rozmowy, chipy i karta pola profilu, brak nagłówka łańcucha na ROM),
  `test:m1-settings` 9/9; 12 zrzutów nawigatora odświeżonych, 1 nowy. Zauważone, nie zmienione (poza zakresem partii): karta
  pola profilu bez odpowiedzi pokazuje „Wartość: nieznane” obok chipa stanu „jeszcze nieznane” (jądro wysyła `value_human`
  = słowo stanu). Nie uruchomione: pełne `npm run e2e`, pełny `ctest`, `android-smoke`.

## Co sprawdzają testy — fakty z przebiegów do r2:b1 (dawniej sekcja raportu)

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

## Co doszło w poprawkach r2:b2 (2026-10-21, fakty z przebiegu)

Pięć ustaleń przeglądu, wszystkie poprawione (żadnego nie odrzucono). Brak WIP-u z przerwanego przebiegu: HEAD był gotowym
commitem r2:b1 (docs), drzewo czyste — pakiet zaczął od ustaleń.

- **Mapa: dotknięcie środka kółka wybierało sąsiada (blokujące).** Na gęstym poziomie (korzeń: 25 dzieci co ~21 px przy 390 px)
  cele 44 px nachodzą na siebie i wygrywał przycisk późniejszy w DOM. `navigator-map.ts`: `nearestMark(marks, p)` — znak o
  środku najbliższym punktowi dotknięcia (remis = pierwszy w kolejności); `NavigatorMap.tsx`: `tapAt(e, own)` w `onClick`
  przycisku `.gnav-map-hit` bierze `clientX/Y` → punkt w polu mapy → najbliższy znak; aktywacja z klawiatury (`detail === 0`,
  bez współrzędnych) zostaje przy znaku własnego przycisku. Przyciski 44 px zostają (dostępność, testy). Test jednostkowy
  (`navigator-map.test.mjs`, 10/10): trzy znaki co 20 px — środek `a` daje `a`, nie `c`. Krok e2e `m1-navigator` „the dense
  root map (r2:b2)”: w korzeniu (≥ 20 znaków, nachodzące pary `.gnav-map-hit` policzone > 0) dotyka środka **każdego** kółka
  (`touchscreen.tap` na środku `circle`, mierzonym tuż przed dotknięciem, bo otwarta karta przesuwa mapę) i sprawdza
  `data-selected` tego znaku, dokładnie jeden zaznaczony, `nav-map-selected` i `node-card` z tym adresem.
- **`test:chat-first` padał na obu widokach.** Krok „Back and Escape…” czekał na `panel-graph`, a `layouts.json` (df6a97c2)
  wysyła „Ta rozmowa” do `navigator:conv:current` → `panel-navigator`. Poprawione na `panel-navigator` + asercja
  `graph-navigator[data-focus]` zaczyna się od `conv:`. `test:chat-first` dopisany do `.github/workflows/loom.yml` (po
  `test:m1-settings`), żeby CI łapało ten dryf.
- **Panel `role=dialog aria-modal` nie trzymał fokusu.** `use-dialog-focus.ts`: `focusablesOf(root)` + nasłuch `keydown` Tab /
  Shift+Tab na oknie — z ostatniego elementu na pierwszy i odwrotnie (z samego okna Shift+Tab → ostatni); `inertAttr(on)` =
  `{ inert: "" }` (React 18 nie zna atrybutu; string). `App.tsx`: układ czat — `ChatShell behindOverlay={activePanel !== null ||
  knowledgeOpen}`; Warsztat — `inert` na `header.app-header`, `aside.sidebar`, `.main-panel` przy otwartym panelu. `ChatShell.tsx`:
  `inert` na `.shell-root` (prop `behindOverlay`), a przy własnym arkuszu na `.shell-bar`, `.shell-drawer`, `.shell-main`.
  e2e `chat-first`: przy otwartych Ustawieniach `chat-shell[inert]`, 40× Tab i 12× Shift+Tab — każdy ląduje w `panel-settings`
  (przed poprawką recenzent mierzył 30/60 poza oknem); przy arkuszu Widoki `shell-bar[inert]` i `.shell-main[inert]`.
- **`M1_W0_TESTIDS.md` wymieniał `nav-bar-planned`.** Wiersz paska bez niego i bez „linii po dotknięciu przycisku bez obsługi”;
  dopisane `nav-actions-note` (`role=status`, `actions.no_focus`) oraz nowy wiersz „Perspektywa ▾” (`nav-perspective-menu`,
  `nav-perspective-<id>`, `nav-perspective-none`, `nav-perspectives`); `nav-view-note` był już w wierszu „Widok ▾”. Nowy test
  `tests/shell/testids-doc.test.mjs` (`npm run test:testids-doc`, w `loom.yml` po `test:role-card`): każdy id w backtickach z
  wierszy tabel i łańcuchów `→` dokumentu musi być w `loom/web/src` jako literał albo przez prefiks szablonu (`nav-row-${…}`);
  ograniczenie: id generowany (`nav-bar-<x>`) przechodzi po prefiksie, więc drugi test przypina `nav-bar-planned` jawnie.
  Pierwszy przebieg testu złapał tylko `loom-server` (nazwa binarki w prozie, nie id) — stąd zawężenie do wierszy tabel.
- **Raport właściciela niósł sześć dzienników r1:bN i stary licznik zrzutów.** Sekcje r1:b2…r2:b1 i dawna sekcja „Co sprawdzają
  testy (fakty z tego przebiegu)” przeniesione tu bez zmian treści; w `M1_CHECKPOINT_0.md` zostały §1–12, „Czego jeszcze NIE ma”
  i krótkie „Co sprawdzają testy (w skrócie)” słowami, z poprawnym licznikiem: **21** PNG (nie 12; recenzent liczył 20 przed
  `m1-navigator-card-profile.png` z r2:b1), 13 w tekście §1–11, 8 pozostałych wymienionych z jednym zdaniem każdy. Katalog zrzutów
  nie był przycinany (każdy < 105 KB, 390×844; wszystkie powstają w tych samych e2e).

Przebieg: `npm run build` (tsc + vite) czysty; `node --test tests/shell/*.test.mjs` 89/89 (w tym `navigator-map` 10/10,
`testids-doc` 2/2); `test:chat-first` 18/18 (390×844 i 1280×800); `test:m1-navigator` 13/13 (nowy krok gęstej mapy);
`test:m1-settings` 9/9 i `npm run e2e` 18/18 jako regresja po `inert` (`run.mjs` nadpisuje przy każdym przebiegu swoje śledzone
zrzuty w `loom/web/e2e/screenshots/` — przywrócone `git checkout`, nie należą do tej paczki). Jądro bez zmian (`ctest` nie uruchamiany), żaden
pack nie ruszony. Zrzuty nie zmienione (poprawki nie zmieniają pikseli: najbliższy znak, `inert`, fokus). Zauważone poza zakresem:
e2e `m1-navigator` zapisuje przy każdym przebiegu `tests/shell/fixtures/graph-card-temperature.json` z nowym `scope_id` rozmowy
(`c_…`) — plik przywrócony `git checkout`, bo zmiana to tylko id z tego przebiegu; następny pakiet może znormalizować id przy
zapisie.
