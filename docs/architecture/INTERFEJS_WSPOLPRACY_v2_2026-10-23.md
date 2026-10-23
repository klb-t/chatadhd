# Interfejs współpracy v2 — synteza dyskusji trzech kątów (R90; Fable, 2026-10-23)

Stan na **2026-10-23 ≈ 18:xx UTC**. Buduje na (nie powtarza): synteza R89 `INTERFEJS_WSPOLPRACY_2026-10-23.md` (interfejs =
perspektywa grafu aplikacji + cienki widok na telefon; role jako presety; routing korekt jako dane; pakiety P1–P11; §9 kontrola),
trzy projekty z dyskusją `INTERFEJS_PANEL_2026-10-23/{wlasciciel,koordynator,ekonomia}.md` (W1–W10, K1–K9, E1–E10, każdy z §6
„Po dyskusji”), stan narzędzi: R88 v1 (skrzynka `spec_inbox` + zadanie „Sekretarz spec”), P1 (rekord biegu sekretarza `spec_runs`),
P2 (kolejka „Czeka na Ciebie”, pola `if_silent`, `cost_to_reverse`) — `docs/spec/README.md`; `docs/ops/TELEPORT_S1.md`;
`tools/workflows/single-package.js`. Jego słowa: `python3 tools/spec.py show R88 R89 R90 R73 R75 R61 R62 R63`.

Konwencja: każda liczba i nazwa niżej to **przykład** albo **preset** (z `_history`, R83/R84), fakty o teraźniejszości mają datę
(R60), jego słowa tylko dosłownie z R-id, każda lista jest otwarta. Kontrola Haiku przed użyciem (4 antywzorce + R60) tego
dokumentu: **nie wykonana w tym pakiecie** — następny krok, jak §9 w syntezie R89.

## §0 Dla właściciela w 10 liniach

1. Jeden ekran na telefonie: liczniki **czeka na Ciebie · zrobione od Twojej wizyty · w toku · nie wyszło · oryginalne od agentów ·
   miejsca · koszt dnia** (lista otwarta), każdy rozwijalny — zamiast 4 674 linii dokumentów stanu (QUEUE 645, INDEX 1 822,
   CLAUDE.md 677, rejestr 909, SPEC.md 621; stan 23.10).
2. Każde pytanie do Ciebie niesie odpowiedź, którą przyjmę bez Ciebie, czas, po którym ją przyjmę, i koszt odwrotu; milczenie =
   zadeklarowany skutek, wykonany bez modelu po drodze. Dziś czeka 12 parafraz (P2, 23.10 12:37); z tych pól istnieją dwa (`if_silent`, `cost_to_reverse`), brakuje czasu i kryteriów.
3. Jednym dotknięciem (przykłady): Zatwierdzam · Popraw (głosem) · To przykład · To nie moje słowa · Stop (z pokazanym kosztem) ·
   Wolna ręka na zakres · Co pamiętasz o… · Odpal rutynę. Między dotknięciem a zapisem nie stoi żaden model.
4. Twoje słowa trafiają do agentów **dosłownie** (`spec:<id>`), nie w moich parafrazach — dziś cytuje je 0 z 21 briefów.
5. Linie statusu pisze bieg (started / finished / building / failed / original_ideas — Twoja lista z 00:1x), nie Fable: dziś 37 z 60
   ostatnich commitów gałęzi to księgowość koordynatora. Dyskusja = osobny strumień, otwierasz go Ty.
6. Koszt widzisz **przy geście** (zanim zatrzymasz, zatwierdzisz start, podtrzymasz maszynę) jako klasę lub zakres i jako jedną
   sumę dnia; paragon per pozycja o dotknięcie dalej; waluty: punkty, Twój czas (słowa wysłane), godziny VM, dysk — lista otwarta.
7. Mapa miejsc odpowiada na Twoje pytania z R90 („czy ogarniesz stamtąd wszystko?”, „Masz podgląd na wszystkie sesje, nie?”,
   „nie pamiętam czy ageds też ma serwer?”): chmura S1, S1-VM, S2, sekretarz, strona, CI, usługi (ChatADHD, Watchdog, AGEDS…).
   VM podtrzymywana **na czas biegu**, na bezczynności gaśnie (preset) — jedno pytanie tak/nie do Ciebie w §7.
8. Po przerwie jeden ekran „Od Twojego wyjścia”: co zdecydowałem wstępnie (z odwrotem), co skończone, co nie wyszło, co czeka.
9. Przestaje kosztować Twoją uwagę: czytanie kolejki i QUEUE, szukanie nazw, pilnowanie, co biegnie i gdzie, odpowiadanie na
   pytania, które mają oczywistą odpowiedź, poprawianie tej samej klasy błędu („to przykład” — 5 razy jednego dnia, 23.10).
10. Pierwszy pakiet: **zapis biegu** (Sonnet, ≈ 40 wywołań — preset; ≈ 0,5 pkt — szacunek z bazy pakietu 0,43–0,5 pkt, nasze-06); 11 pakietów, 9 bez Fable (§4). Nic nie jest
    ukryte: wszystko widoczne, zwinięte wg presetu, z wierszem „N zwiniętych — pokaż”.

## §1 Rozstrzygnięcia konfliktów z dyskusji

| # | Konflikt (strony) | Decyzja (preset) | Powód: filozofia / dowód (data) | Odwrót |
|---|---|---|---|---|
| 1 | **Dom zdarzeń biegu**: repo + lustro (K) vs baza strony od razu (W) vs ledger jsonl (E) | **Kierunek decyduje o magazynie pierwotnym**: jego gesty → baza strony (telefon dociera za 0 tokenów); nasze biegi i próbki → plik w repo (każde miejsce pisze za 0; commit lokalny natychmiast, push na gałąź dokumentów co N biegów, preset N=1); sekretarz lustruje w obie strony w swojej kadencji; każdy licznik niesie „stan z HH:MM” i źródło | bieg na VM nie ma bazy strony, telefon nie ma repo (fakt 23.10); bieg niezacommitowany ginie z maszyną (restarty kontenera, 23.10); `loom.yml`/`python.yml` biegną tylko na push do `main` z filtrem ścieżek → push na gałąź dokumentów = 0 min CI (fakt 23.10) | jedno miejsce, gdy VM dostanie dostęp do bazy strony (fakt do zmierzenia) |
| 2 | **Jedna skrzynka czy dwie** (`spec_inbox` W vs `routine_requests` K vs routing klasą E) | **Jedna skrzynka** `spec_inbox`: każdy jego gest = datowana wypowiedź z polem `class` (statement / approval / correction / marker / routine_request / sample / … — lista otwarta); konsumenci (sekretarz, operator, koordynator) filtrują | dwie skrzynki = dwa stany „czeka”, których on nie rozróżni (W 6.3 #1); dwa odpytywania = dwa prefiksy na kadencję (E) | osobna kolekcja per klasa (indeks), ten sam kształt dokumentu |
| 3 | **Kto wykonuje `if_silent` po terminie**; operator (K6) = sekretarz? | **Jedno zadanie zaplanowane** (sekretarz = operator v1) z rozszerzonym promptem: wykonuje `if_silent` dla klas o niskim koszcie odwrotu, resztę eskaluje; termin `ask_after` → **zadanie jednorazowe** (`run_once_at`) zamiast czuwania; `ask_after` liczy się od **pierwszego pokazania** (znacznik wizyty), z twardym limitem (preset) | każde odpalenie świeżej sesji płaci prefiks ≈ 39 tys. tokenów (nasze-02); pytanie z 2 w nocy nie może rozstrzygnąć się milczeniem, zanim je zobaczył (E 6.1); „Zatwierdzam” nie wymaga osądu — między dotknięciem a `spec.py approve` nie stoi model (W 6.2 p.6) | kadencja sekretarza jako preset; drugie zadanie, gdy pomiar pokaże, że jedno nie nadąża |
| 4 | **Podtrzymanie VM** (E „nie podtrzymywać do jego tak/nie” vs K „na czas biegu”) | **Podtrzymanie na czas biegu**: bieg = rekord `routine_run` w stanie `running` z `where: vm` (agent, bramka, build); tętno i sekretarz **nie** są biegiem; godziny VM na paragonie tego biegu; na bezczynności — nie (preset); jego pytanie tak/nie dotyczy wyłącznie bezczynności (§7); próg potwierdzany przez niego: prognoza godzin/dobę > ×10 bazy z rekonesansu | R90 dosłownie: „nie wiem czy chodzący Code to blokuje, Zadbaj o te wszystkie rzeczy” — polityka 30 min celuje w koszt bezczynności, nie w zabijanie biegu; podtrzymanie biegu, który sam uruchomił, to koszt biegu, nie nowy wydatek poza budżetem; stary preset E wymyślał ograniczenie (domyślnie tracić pracę) | wyłączyć timer `vm.keepalive`; albo preset „zawsze” po jego słowie |
| 5 | **Model w rutynie** (K „rola w rutynie, tabela osobno” vs E3 „licznik przed startem musi znać model”) | Rutyna niesie **rolę**; tabela rola→model to osobne, datowane dane (R89 §4.2), luz R63 = parametr tej tabeli per model koordynatora; **`start_gate` rozwiązuje rola→model→miejsce PRZED startem i zapisuje wynik w biegu, briefie i na paragonie**; brak modelu w `agent()` = advisory z pokazanym dziedziczonym, nie błąd | cennik ról kalibruje się z tego, co faktycznie biegło — błąd 11:23 (626 tys. tokenów na Fable zamiast presetu roli) to brak tego zapisu; twardy błąd = wymyślone ograniczenie | `start_gate` w trybie „tylko loguj” |
| 6 | **Paragon dla niego** (E1 przy każdej pozycji vs W przy geście) | **Przy geście + suma dnia** (preset per urządzenie); paragon per pozycja istnieje jako dane, o dotknięcie dalej; „przy każdej pozycji” jako preset do włączenia | SLOWA:66 „nie bierz bezwzględnych liczb za bardzo serio pokazuje tylko orientacyjnie” — liczba przy każdej linii to szum; E1 zostaje kształtem rekordu bez zmian | włączyć preset „przy każdej pozycji”; mierzyć, czy otwiera paragony |
| 7 | **`attention_plan.json`** (E2 osobny plik vs waluta + próg) | **Nie powstaje**: czas czytania = waluta na paragonie liczona z tego, co WYSYŁAMY (słowa × preset tempa, `reliability: low`), próg = jeden wiersz routingu (E9) z `_history`; licznik „dziś do Ciebie: N słów” zostaje | baza niezmierzona (słów/dobę za 21–23.10 nikt nie policzył) → plik byłby zgadywaniem z własnym cyklem utrzymania; dwa pliki planu = dwie prawdy | osobny plik, gdy pomiar pokaże zależność progu od pory/klasy |
| 8 | **Budżet uwagi / „każda linia musi zmieniać licznik” jako filtr** (E2/E9, W teza) | Budżet uwagi i reguła „linia = zmiana licznika” to **licznik i advisory dla koordynatora oraz preset filtru widoku** („nie gadaj tyle” ↔ „gadaj”), nigdy mechanizm, który czegoś nie pokaże; u niego wszystko widoczne, zwinięte wg routingu, wiersz „N zwiniętych — pokaż” | ZASADA WŁAŚCICIELA: filtr wybierający za niego ukrywa; ukrycie kosztuje zaufanie bez odwrotu, zwinięcie 0 | — (filtr to preset, zmiana = jego słowo) |
| 9 | **Pora / „tryb nocny”** (W8 flaga vs E9 wiersz; wnioskowanie z wizyt) | **Wiersz routingu** klasa × pora × budżet jako dane; jego słowo („idę spać”, „wróciłem” — przykłady) przestawia wartość; wnioskowanie z czasów wizyt może tylko **zaproponować** wiersz (Tak/Nie, `if_silent: bez zmian`), nigdy przestawić | cicha decyzja o tym, co i kiedy widzi, ukrywa; wiersz nie ma „trybu” do wyłączania — odwrót = usunięcie wiersza | usunąć wiersz |
| 10 | **Zasięg „stop” z telefonu** (W8 pauza z kosztem vs E5 miejsca vs K1 `interrupt` niesprawdzony na VM) | „stop” **adresuje miejsca**: lista tego, co drogie biegnie, per miejsce, z kosztem utopionym z paragonu biegu; preset: pauza wszędzie, odpowiedź jednym dotknięciem per wiersz; kanał do VM = **flaga w repo czytana przez tętno** (opóźnienie = interwał tętna, preset); do czasu E5+K1: dzisiejsze zachowanie + zapis kto/kiedy | 2 przypadkowe dotknięcia jednego dnia (R89 „odruchowo stop nacisnąłem”, R90 „coś przypadkiem nacisnąłem”); `send_message` do sesji VM odmówiony przez platformę 23.10 | preset „stop zawsze natychmiast, wszędzie” |
| 11 | **Co jedzie do agenta jako „filozofia”** (K3 całe `CLAUDE.md` vs E10 węzły spec wg tematu) | **Oba, mierzone**: `CLAUDE.md` jako prefiks wspólny (trafienie cache) + `spec:` wg tematu pakietu w części własnej; miara = znaleziska kontroli przed użyciem na pakiet przed/po | `CLAUDE.md` ≈ 39 tys. tokenów = 7,3 % wydatku agentów (nasze-02) — ale `spec:` w 0/21 briefów; decyzja z góry byłaby zgadywaniem | wrócić do samego `CLAUDE.md` |
| 12 | **Pomiar jego uwagi** (W: wizyty, dotknięcia, czas do gestu; E: tylko to, co wysyłamy) | Preset = **tylko znacznik ostatniej wizyty** (konieczny dla W9 i dla `ask_after` od pokazania), jako węzeł w bazie strony; reszta wyłączona do jego słowa; to, co wysyłamy, mierzymy po swojej stronie | odwrotne domyślne wymagałoby zgody, o którą nie pytamy w nocy; „no telemetry” (CLAUDE.md) | jego słowo włącza więcej (preset `attention_samples`) |
| 13 | **Warsztat K9 jako ekran dla niego** (K vs W 6.2 p.4) | Warsztat jest koordynatora/operatora; dla niego biegi = **wiersz liczników** („w toku: 2 biegi · 14 min · ≈ 0,4 pkt” — przykład) + wariant zapytania W5 „co biegnie” → gałąź biegów; **wszystkie rutyny widoczne i uruchamialne przez niego** z kartą `start_gate` przed startem; kolejność na pierwszym ekranie = preset zasiany tym, o co prosił (R71, R90 „odpal”, R62 „podkręć”) | R90 „wyeksponować wszystkie narzędzia”; nic nie jest niedostępne z definicji, kolejność to preset, przeszłość to ziarno, nie zamknięcie (K 6.2) | kolejność alfabetyczna / inna (preset) |
| 14 | **`confirm: owner` jako pauza biegu** (K `apk.release` vs W3) | Bieg czeka na niego **tylko** w klasach z CLAUDE.md (jego pieniądze poza budżetem, jego dane u nowego dostawcy, ×10) i przy drogim odwrocie; reszta = pytanie W3 z odpowiedzią domyślną, `ask_after`, `if_silent` — decyzja wstępna z odwrotem; predykat klasa × koszt odwrotu to dane | R75 „ostrożnie podejmij wstępną taką żeby się dało z małymi kosztami w razie w wycofać”; R62 „działaj śmiało” | preset „czekaj zawsze” per klasa |
| 15 | **Operator: kadencja i stan runnera** (K6 co godzinę vs E koszt pustego biegu; K2 runner) | Kadencja = preset związany z pomiarem pustego biegu (P1 `spec_runs`); runner v1 `tools/routine.py` **bez własnego stanu** poza zapisem biegu (bez kolejki, retry, harmonogramu) | 24 prefiksy/dobę za czuwanie co godzinę (E 6.2 p.8); każda cecha runnera = cecha do przeniesienia do jądra drugi raz (P7) | kadencja; runner w jądrze (port `tool_source`) |
| 16 | **Aplikacja ChatADHD jako konsola** (W10) | Opcja „później” z datowanym kosztem (build + synchronizacja grafu spec z telefonem), zapisana w E9 jako kanał; nie skreślona | R90 „ten nasz Interfejs do komunikacji to przeglądarkowy, nie?”; jedyna maszyna buildu zajęta pakietami M1 (23.10); „absence today is not absence forever” | wstawić do kolejki po P7 |

## §2 Model interfejsu (abstrakcje; układ ekranu — preset)

### 2.1 Jedno źródło: zapis biegu (`loom.routine_run/1`), trzech czytelników

Środek ciężkości po dyskusji: nie rutyna, lecz **zapis biegu** — jeden rekord pisany przez skrypt (0 tokenów na ścieżce zapisu),
z którego trzy kąty czytają trzy rzeczy. Pola (lista otwarta, kształt ≈ `loom.action_run/1` z jądra):

| Grupa pól | Przykłady pól | Kto czyta |
|---|---|---|
| tożsamość | `run_id`, `routine` (id + wersja + hash), `params`, `context_pack_hash`, `brief_hash`, `where` (węzeł miejsca), `resolved` {rola → model → miejsce, przez `start_gate`, data} | koordynator, nowa sesja, kontrola |
| przebieg | `started`, `finished`, `state` (running / paused / done / failed — otwarte), `steps[]` {id, state, in_hash, out_hash, where, clock, tokens}, `commits[]`, `cards[]`, `escalations[]`, `advisories[]`, `original_ideas[]` | koordynator; W1/W7 (licznik „w toku”, „nie wyszło”, „oryginalne”) |
| zdarzenia | `events[]` {at, kind (started / finished / building / failed / original_ideas / decision / … — jego lista z 00:1x jako ziarno), `counter_delta` {licznik, o ile}, class} | **W1/W7/W9** — linia statusu = render zdarzenia z niezerową deltą (preset filtru) |
| koszt | `receipt[]` {currency, value, unit, source, at, reliability} — tokeny z `usage_report`, estymata ze `start_gate`, słowa wysłane do niego × preset tempa, godziny VM, GB, minuty CI — lista otwarta | **E1**; W8 „stop” z kosztem utopionym; suma dnia |
| decyzje | `decisions[]` {text, reversal, cost_to_reverse, by, at}, `markers_applied[]` (znaczniki „to przykład” zużyte przez addendum) | W6 (diff decyzji), §5 |
| przekazanie | `handover` — render z tych pól, nie czwarty magazyn | E7: karta pakietu, karta teleportu, rekord sekretarza, **„Od Twojego wyjścia”** (W9) |

Dom (konflikt 1): plik `docs/reports/runs/<rok-mies>/<run_id>.json` + indeks `runs.jsonl` na gałęzi dokumentów; `spec_runs` z P1 to
pierwsza instancja (scalić). Lustro w bazie strony pisze sekretarz. Zasada ekonomii z dyskusji: **ścieżki zapisu są bez tokenów**
(jego gest → spec, bieg → rekord, próbka → ledger); model dokłada warstwę odczytu osobno, w swoim czasie, z własnym paragonem.

### 2.2 Artefakty z proweniencją (uzupełnienie tabeli R89 §4.1)

| Węzeł (rodzaj — przykład) | Pola proweniencji (otwarte) | Dom v1 (23.10) → v2 |
|---|---|---|
| `routine` (K2) | `version`, `since`, `supersedes`, `changelog[]`, `presets{_history}`, `judgement[]`, `operator_ok` | `tools/routines/*.json` → akcja złożona `actions.pack` (P7) |
| `routine_run` (K7) | jak 2.1 | `docs/reports/runs/` → węzły grafu `routine_run --part_of--> package --implements--> requirement` |
| `context_pack` (K3) | `refs[]` {file/spec/doc/index/ledger/schema, hash}, `more: allowed, recorded` | `tools/routines/context_packs.json` |
| `brief` (K4) | 8 dzisiejszych pól + `spec[]`, `design_doc`, `decisions[]`, `measurements[]`, `hypothesis`, `plan_calls`, `context_pack`, `card_schema` | `tools/workflows/briefs/*.json` (już w repo) |
| `place` / `service` (E5) | waluta miejsca, polityka bezczynności, `tools[]` z datą obserwacji i zamiennikiem, `heartbeat`, `hosted_on`, czy trzyma jego dane | `docs/ops/places.json` → `environments.h` (jądro ma węzły `environment:*`) |
| `gesture` (W8+K9) | `{gesture, patterns[], effect, cost, reversal, since, render[]}` — ten sam kształt co warunek wstępny `{check, why, since}` (K5) | `tools/routines/words.json` → `chat_ops.pack` / `intent_routing.pack` |
| `question` (W3) | `if_silent`, `cost_to_reverse`, `ask_after` (od pierwszego pokazania), `criteria[]`, `class`, `first_shown`, gesty | `understanding.json` (dwa pola już są, 23.10) |
| `mandate` (W8 „wolna ręka”) | zakres, do kiedy, wyjątki stałe (CLAUDE.md), tryb R75 | `understanding.json` / spec → graf; runner czyta go **przed** każdą bramką `confirm` |
| `handover` (E7) | render z `routine_run` + głowa kolejki + spec `proposed` + miejsca; `supersedes` | generowany plik (`TELEPORT_S1.md` = ręczna instancja 23.10) |
| `inbox entry` (R88 v1) | `node`, `class` (otwarte), `text` dosłownie, `at`, `device`, `state`, `verdict` | baza strony `spec_inbox` (działa 23.10) |

### 2.3 Role jako presety (uzupełnienie R89 §4.2)

| Rola (przykład) | Co robi w v2 | Model (preset, datowany; tabela osobno) | Tokeny na ścieżce zapisu |
|---|---|---|---|
| sekretarz = **operator v1** | czyta skrzynkę (wszystkie klasy), stosuje gesty bez osądu (`approve`, znacznik, `routine_request` z `operator_ok`), wykonuje `if_silent` niskiego kosztu odwrotu, lustruje biegi → baza strony, pisze własny `routine_run` | Sonnet, co godzinę o :23 (działa 23.10) + `run_once_at` per `ask_after` | 0 (zapis = komendy `spec.py`, pliki) |
| koordynator | czyta zapisy biegów i eskalacje, pisze briefy jako rekordy, podejmuje `judgement`; nie pisze linii statusu | Fable (ustawił on) | — |
| wykonawca pakietu | dostaje brief = prefiks wspólny (pakiet kontekstu, hash) + część własna; cytuje `spec:<id>`; pisze kartę do `routine_run` | Sonnet (preset roli) | 0 (karta = plik) |
| kontroler przed użyciem | emituje **dane** (decyzje, przykłady, dodatki, pomiary, znaleziska), nie prozę (W6) | Haiku (14–17 wywołań, nasze-24) | 0 |
| `start_gate` | krok bez osądu: rola→model→miejsce, estymata, okno, dysk/żywotność miejsca, co przekroczy → start / advisory / „czeka na Ciebie” (tylko klasy CLAUDE.md) | skrypt (0 tokenów) albo operator | 0 |

### 2.4 Rutyny, makra, szablony jako dane z wersją i zapisem biegu

- **Rutyna** `loom.routine/1` = akcja złożona (jak `actions.pack`): `steps[]` z `when` / `variants` / `inputs`, warunki wstępne jako
  **odczyty pól węzła `place`** `{check, why, since}`, bramki `confirm` w kształcie `question` (2.2), krok `start_gate` na początku,
  `where` na każdym kroku, `record` → render (`status.lines`, `queue.section`, `handover`). Wersjonowanie (K8): `version`, `since`,
  `supersedes`, `changelog`, presety z `_history`; bieg nazywa wersję; stara zostaje.
- **Jego przykład (R90, dosłownie: „przykład: przy starcie nowego podwykonawcy całą filozofię mu zapodać do kontekstu. przypominam
  przykład to jest”)** = JEDNA instancja: krok `context_pack: podwykonawca` w rutynie `package.run`, gdzie pakiet kontekstu to lista
  odwołań z hashem (`CLAUDE.md`, `spec:R58 R59 R60 R83 R84` przez `spec.py cite --full`, `principles.json`, dokument obszaru po nagłówku,
  wpisy INDEX po kluczu, schemat karty — przykład zestawu). Inne rutyny (przykłady, dopełniane tym, co już jest w `tools/workflows/`):
  `check.before_use`, `gate.full`, `ci.read`, `ff.main`, `smoke.read`, `apk.release`, `pace.check`→`start_gate`, `ledger.sample`,
  `secretary.run`, `addendum.apply`, `teleport.refresh`, `tasks.recreate`, `vm.keepalive`, `vm.recon`, `status.lines`.
- **Szablon briefu** `loom.brief/1` + `brief.render` → prompt z częścią wspólną **bajt w bajt** (hash w zapisie biegu — jedyny tani dowód
  trafienia cache) i częścią własną na końcu; `spec.py cite` jako krok walidacji; brief w repo przed startem (praktyka `briefs/README.md`).
- **Makra = słowa sterujące**: jedna tabela `words.json` zasiana jego słownikiem z cytatów („stop” R89, „wolna ręka” R89, „rób to co
  wiesz” R75, „podkręć” R62, „nie gadaj tyle” CLAUDE.md, „odpal” R90, „idę spać” R75, „co pamiętasz” R73 — przykłady, on dopisuje
  jednym zdaniem, warianty ASR), **dwa rendery**: telefon (gesty W8) i warsztat (przyciski/makra K9). Rekord gestu niesie skutek, koszt,
  odwrót, datę; każde użycie zapisane z pochodzeniem; potwierdzenie tylko, gdy koszt przekracza próg (preset).

### 2.5 Narzędzia wyeksponowane (K1 + R90 „wyeksponować wszystkie narzędzia”)

Katalog `tools.json`: jedno działanie na narzędzie — id, co robi, parametry, efekty (`reads_only`, pisze repo, pisze jego dane, kosztuje
tokeny, … — otwarte), klasa kosztu, **dostępność per miejsce z datą obserwacji i zamiennikiem** (ziarno: `TELEPORT_S1.md` §5 — „Nic z tego
nie było sprawdzone na VM”, 23.10). Dla niego: wszystko widoczne i uruchamialne (karta `start_gate` przed startem; `confirm` tylko wg
konfliktu 14); kolejność — preset zasiany jego prośbami; reszta o dotknięcie dalej. Efekty nieznane = najgorszy przypadek (preset, jak
`tool_sources.h`).

### 2.6 Kanały i dwie waluty (uzupełnienie E9/E1)

| Kanał (przykład) | Niesie | Koszt tokenów | Koszt jego czasu | Stan 23.10 |
|---|---|---|---|---|
| strona na telefonie (artefakt) | liczniki, kolejka, gesty, „co pamiętasz”, mapa miejsc, suma dnia | 0 (render z repo + bazy strony) | niski, w jego porze | działa (`spec_inbox`, kolejka P2) |
| czat sesji | dyskusja (otwiera on), eskalacje klas CLAUDE.md | tura koordynatora ≈ 0,28–0,58 mln kontekstu | wysoki | działa; styl: linie statusu |
| repo / GitHub | prawda o kodzie i biegach, karty, CI | 0 (minuty CI tylko na `main`) | tylko gdy chce | działa |
| powiadomienie push | „czeka na Ciebie: 1, drogi odwrót” | 0 | sekundy | do sprawdzenia (platforma) |
| głos → cytat | jego słowa jako `statement` | ASR (tani) | 0 czytania | brak (R89 O9) |
| aplikacja ChatADHD | te same perspektywy z grafu | 0 | niski | opcja „później” (konflikt 16) |

Routing klasa × pora × budżet → kanał i forma, jako wiersze danych z `_history`; każda wiadomość poza presetem ma na paragonie powód.
**Dwie waluty** (punkty konta, jego czas) to przykłady z otwartej listy `receipt[]`; jego czas liczony po naszej stronie (słowa wysłane),
nigdy jako filtr. Koszt pokazywany **przy geście** i jako suma dnia (konflikt 6). Próbka okna (`rate_limit_event`, jego „46 %” jako
`source: owner` rozpoznane przez sekretarza i potwierdzone jednym dotknięciem, `if_silent: przyjmij`) to księga dnia, łączona po czasie,
nie przypisywana do biegu.

### 2.7 Wiele maszyn i sesji (E5/E6 + R90 „administrujesz tamtą maszyną”)

| Miejsce (przykład) | Waluta miejsca | Co tam biegnie (preset) | Kanał „stop” | Stan 23.10 |
|---|---|---|---|---|
| chmura S1 (`session_015y…`) | punkty konta; kontener (dysk ~95 % = restart) | koordynator, pakiety, bramki, zadania zaplanowane (sekretarz), `watch_url` | `interrupt` / `send_message` | działa |
| S1-VM `devbox-20260923` (GCP) | godziny × stawka (jego pieniądze), dysk; polityka: wyłączenie po 30 min bezczynności (jego ustawienie) | administracja, rekonesans, tętno, build/bramka na zlecenie bez tokenów (po rekonesansie) | flaga w repo czytana przez tętno | Claude Code v2.1.309, Opus 5.5 (zrzut 17:3x); `send_message` odmówiony; narzędzia niesprawdzone |
| S2 (`session_01Armc…`) | punkty konta | smoke na emulatorze (GitHub Actions) | `send_message` | działa od 13.10 |
| zadanie „Sekretarz spec” | prefiks per odpalenie (≈ 39 tys. tokenów) | skrzynka, lustro, `if_silent`, `start_gate` | wyłączenie zadania | działa (co godzinę o :23) |
| strona-artefakt + baza | 0 tokenów | jego gesty, liczniki, render | — | działa |
| GitHub CI | minuty | `loom.yml`, `python.yml` na `main` | `gh run cancel` | działa |
| usługi: ChatADHD server+web, Watchdog-JH16, AGEDS (lista z R90, otwarta) | CPU/RAM/dysk VM | `hosted_on` VM; stan żyje / śpi / nie postawiono; czy trzyma jego dane (kształt B) | — | nie zmierzone — rekonesans (IW-8) |

Reguły (presety): praca bez tokenów idzie do miejsca bez tokenów (E6), krok ma `{where, clock, tokens}`; podtrzymanie VM wg
konfliktu 4; „stop” adresuje miejsca (konflikt 10); mapa miejsc = jeden wiersz liczników dla niego („miejsca: 3 żyją · 1 śpi ·
2 nie postawiono” — przykład), rozwijalny.

### 2.8 Teleport i powrót: jeden `handover`, cztery rendery

Karta teleportu (sesja → sesja), karta pakietu (agent → koordynator), rekord sekretarza (bieg → koordynator) i **„Od Twojego
wyjścia”** (nocna zmiana → on) to cztery rendery jednego rekordu z zapisów biegów + głowy kolejki + spec `proposed` + mapy miejsc,
z własnym paragonem (koszt odbudowy kontekstu 0,2–0,46 mln tokenów na żywy kontekst — pomiar z CLAUDE.md; praca, która przepadnie)
i warunkiem „tylko między pakietami” jako predykatem.

### 2.9 Droga gestu (uzupełnienie R89 §4.6)

```
on: dotknięcie / tekst / głos / słowo sterujące  →  spec_inbox {class, text dosłownie, at, device}      (0 tokenów)
  → sekretarz=operator (kadencja + run_once_at): class=approval → spec.py approve; marker → krawędź; routine_request z
    operator_ok → start_gate → bieg; statement → węzeł raw + warstwa odczytu (tytuł, continues/corrects/exemplifies)
  → bieg pisze routine_run (events z counter_delta, receipt, decisions)  →  sekretarz lustruje → baza strony
  → strona renderuje liczniki / kolejkę / „Od Twojego wyjścia” / gałąź „co pamiętasz”      (0 tokenów)
  → koordynator czyta zapisy i eskalacje klas: pieniądze · jego dane · ×10 · drogi odwrót · sprzeczność z filozofią
```

## §3 Co przestaje istnieć w dzisiejszym przepływie

| Dziś (23.10) | Dowód kosztu (data) | Co to zastępuje |
|---|---|---|
| Linie statusu i sekcje QUEUE pisane ręcznie przez Fable | 37/60 ostatnich commitów gałęzi = księgowość (`queue:`, `ledger:`, `ops:`, …) | render `status.lines` z `events[]` biegu (IW-1) |
| Brief pisany od zera z wklejanymi regułami | 21 briefów, mediana 6 628 znaków, 146 KB; „CACHE RULE” 20/21, „context small” 18/21 — szablon w głowie koordynatora | `loom.brief/1` + pakiet kontekstu z hashem (IW-2, IW-7) |
| Jego słowa docierają do agenta w parafrazach | `spec:` w 0/21 briefów, choć `spec.py cite --full` istnieje od SPEC1 | `spec:<id>` w części własnej briefu (IW-2) |
| Tura koordynatora na „jak idzie”, „co biegnie”, „co pamiętasz o…” | tura ≈ 0,28 mln (R89 §4.2) do 377–587 tys. kontekstu (ledger 21.10 06:08) | odpytywany jest ZAPIS biegu i strona (0 tokenów) (IW-1, IW-4) |
| Pytanie w czacie bez odpowiedzi domyślnej i czasu | 12 parafraz `proposed` czeka (23.10); P2 ma `if_silent`, brak `ask_after`/`criteria` | `question` W3 + `run_once_at` (IW-3) |
| On czyta dokumenty stanu | 4 674 linii (QUEUE 645, INDEX 1 822, CLAUDE.md 677, rejestr 909, SPEC.md 621) | liczniki + „Od Twojego wyjścia” (IW-4) |
| Korekta przez przepisanie reguły; restart agenta | 5× „to przykład” jednego dnia; 3 restarty ≈ 0,9 pkt (nasze-08) | znacznik jako znalezisko → addendum (IW-9) |
| Preflight w głowie koordynatora | 11:23: etap na Fable zamiast presetu roli, 626 tys. tokenów | `start_gate` z zapisem rozwiązanego modelu (IW-6) |
| Dwie ręczne tabele: narzędzia per maszyna (TELEPORT §5), zadania do odtworzenia | „Nic z tego nie było sprawdzone na VM”; „zadania NIE jadą same” (23.10) | katalog narzędzi + `place.tools[]` + `tasks.recreate` (IW-2, IW-8) |
| Karta teleportu pisana ręcznie | `TELEPORT_S1.md` 62 linie ręcznie 08:0x; `VM_START_PROMPT` druga ręczna instancja | render `handover` (IW-11) |
| „Tryb nocny” jako flaga; `attention_plan.json` | nie powstały — i nie powstaną (konflikty 7, 9) | wiersz routingu + waluta czasu |
| Pełny log czytany przez agenta | 1,5 mln tokenów/agent (S2, 21.10) | wycinek (R89 P9 — bez zmian, poza tym dokumentem) |

## §4 Pakiety (≈ 50 wywołań każdy — preset; kolejność — preset; akceptacja + pomiar „po”)

| # | Pakiet | Zakres (przykłady wartości) | Akceptacja | Pomiar „po” | Kto | Scala |
|---|---|---|---|---|---|---|
| **IW-1** | **Zapis biegu v1** | schemat `loom.routine_run/1` (2.1) jako JSON-schema w `tools/routines/schemas/`; skrypt `tools/run_record.py` (start / step / event / receipt / finish; 0 tokenów); `docs/reports/runs/<rok-mies>/<id>.json` + `runs.jsonl`; `status.lines` renderer (jego 5 rodzajów jako ziarno); `single-package.js` i prompt sekretarza piszą rekord; `spec_runs` z P1 scalone jako pierwsza instancja | 3 biegi zapisane (2 sekretarza, 1 pakiet); sekcja QUEUE wygenerowana z rekordu nieodróżnialna w formie od ręcznej; testy schematu | linie QUEUE ręczne vs generowane w 48 h; czas nowej sesji do odpowiedzi „co biegnie” (baza: czyta QUEUE) | Sonnet | K7, P1, P5 (karta) |
| IW-2 | Katalog narzędzi + pakiet kontekstu + `spec:` w briefie | `tools/routines/tools.json` (ziarno TELEPORT §5 + narzędzia repo, dostępność per miejsce z datą); `context_packs.json` (`podwykonawca`, `kontroler`, `sesja`); `tools/context_pack.py` → prefiks z hashem; `single-package.js` przyjmuje `context_pack` i `spec[]`, waliduje `spec.py cite` (advisory) | następny brief ma `spec:` ≥ 1 i hash prefiksu w karcie; diff prefiksu dwóch briefów = 0 | `spec:` 0/21 → 1/1; `cache_creation` na bieg vs rozmiar prefiksu (baza 7,3 %); znaleziska kontroli przed/po | Sonnet | K1, K3, P3, P4 |
| IW-3 | Pytanie w pełnym kształcie + jedna skrzynka z klasą + `if_silent` bez czuwania | `spec.py propose/correct --ask-after --criteria`; `spec_html.py` renderuje kryteria i czas od `first_shown`; `spec_inbox.class` (otwarte) z `verdict`; prompt sekretarza: wykonuje `if_silent` niskiego kosztu, eskaluje resztę, zakłada `run_once_at` na `ask_after`; twardy limit (preset) | jedno pytanie rozstrzygnięte milczeniem z rekordem decyzji i odwrotem; „Zatwierdzam” → `approve` bez modelu; test e2e 400 px | pytań/dobę; udział rozstrzygniętych milczeniem; odwrotów po milczeniu (cel: rzadkie) | Sonnet | W3, K6 (część), konflikty 2, 3 |
| IW-4 | Liczniki W1 + „Od Twojego wyjścia” + mapa miejsc v1 | `spec_html.py`: sekcja liczników z `runs.jsonl` + `spec_inbox` + `docs/ops/places.json` (statyczna tabela miejsc i usług z datą); znacznik ostatniej wizyty jako węzeł bazy strony (preset: tylko on); render W9 po progu (przykład > 6 h); „N zwiniętych — pokaż”; „dziś do Ciebie: N słów” | liczniki zgodne z rekordami (test); W9 renderuje się po symulowanej przerwie; brak liczby liczonej przez model | dotknięć do pierwszej decyzji; pierwsza wiadomość po powrocie: pytanie o stan czy decyzja; słowa/dobę do niego (baza 21–23.10 do policzenia) | Sonnet | W1, W9, E4, E5 (v1), E2 (licznik) |
| IW-5 | Słowa sterujące i gesty jako jedna tabela | `tools/routines/words.json` zasiane cytatami (2.4), rekord gestu `{gesture, patterns, effect, cost, reversal, since, render}`; sekretarz rozpoznaje słowa w `statement` (warianty ASR) i stosuje bez modelu, gdy skutek ma koszt < próg; węzeł `mandate`; „stop” → zapis kto/kiedy + lista biegów `running` z paragonu | „to przykład” i „wolna ręka” ze strony dają rekordy bez modelu; „stop” przy biegu pokazuje koszt z `receipt[]`; test tabeli słów | trafienia/chybienia; przypadkowe „stop” bez straty (cel 100 %); mandatów i odwrotów na tydzień | Sonnet | W8, K9 (makra), W4 (część) |
| IW-6 | `start_gate` + tabela rola→model datowana | `tools/start_gate.py` (uogólnia `pace.py`): rola→model→miejsce, estymata z `cost_per_package`, okno, dysk/żywotność miejsca, co przekroczy → karta JSON; `single-package.js` wywołuje przed `agent()`; brak `model` = advisory z dziedziczonym; wynik do `routine_run.resolved` i do briefu; `roles.json` z `_history` i luzem R63 | 3 starty z kartami; test przypadku advisory; `--log-only` | etapy na innym modelu niż preset roli (baza: 1 workflow, 626 tys.); `calls_used/plan_calls` (baza 2–5×) | Sonnet | E3, E8 (funkcja okna), konflikt 5 |
| IW-7 | Rutyny jako dane + runner v1 + brief jako rekord | `loom.routine/1`, `loom.brief/1`, `brief.render` (prefiks bajt w bajt); `tools/routine.py` renderuje i zapisuje bieg, bez stanu własnego; pierwsze rutyny: `package.run`, `check.before_use`, `secretary.run`, `status.lines`; wersjonowanie z `_history` | jeden pakiet przeprowadzony runnerem end-to-end z rekordem; brief K2c-1 rozłożony na pakiet kontekstu + część własną ≈ 2 tys. znaków (przykład) | udział biegów przez rutynę vs ad hoc; długość części własnej; `cache_creation` | Sonnet (1–2 pakiety) | K2, K4, K8, E10 |
| IW-8 | **Rekonesans VM jako pierwszy bieg `where: vm`** | rutyna `vm.recon` wykonana przez sesję S1-VM: śmieci po instalacjach, dysk, usługi (ChatADHD, Watchdog-JH16, AGEDS — co istnieje, co ma serwer), **co liczy się jako aktywność dla polityki 30 min** (ssh? CPU? proces `claude`?), narzędzia sesji sprawdzone → `places.json` z datami; tętno (`systemd` timer) pisze rekord do repo; `vm.keepalive` tylko przy `running where: vm`; flaga „stop” w repo; prompt-wprowadzenie z R90 jako instancja pakietu kontekstu `sesja` | węzeł `place` VM z datowanymi polami; 24 h tętna w repo; jedna zmierzona odpowiedź „co jest aktywnością”; lista śmieci z propozycją (nic nie usunięte bez rekordu) | baza godzin VM/dobę (→ próg ×10); zgaśnięć pod pracą; czas od zgaśnięcia do wykrycia; wolne GB | sesja S1-VM (model wg jego ustawienia, 23.10: Opus 5.5) | E5, E6, K 6.3 #2, R90 (1)(2)(4)(5) |
| IW-9 | Kontrola emituje dane; znacznik → addendum | `check-before-use.js` wypisuje JSON (decyzje, przykłady, dodatki, pomiary, znaleziska) obok dokumentu; strona renderuje dokument jako diff decyzji (W6), pełny tekst o dotknięcie dalej; `addendum.apply` czyta znaczniki `class=marker` ze skrzynki jako znaleziska; agent w biegu nie restartowany | jeden dokument projektowy w renderze diffu; jeden znacznik → rekord addendum | czy otwiera pełny tekst; znaczników na dokument; czas znacznik → addendum; powroty tej samej klasy błędu | Haiku (kontrola) + Sonnet (skrypt) | W4, W6, K 6.1 |
| IW-10 | Operator = rozszerzony mandat sekretarza | rutyny `operator_ok` w zadaniu zaplanowanym: `start_gate`, `ci.read`, `ledger.sample`, `status.lines`, `teleport.refresh`, `tasks.recreate`; `judgement` → propozycja do koordynatora (nic nie wykonane); kadencja z `_history` związana z pomiarem pustego biegu | 5 biegów operatora bez błędu; 1 eskalacja z powodem | tury koordynatora na pakiet przed/po (baza ≈ 20 × 0,28 mln); koszt pustego biegu z `spec_runs` | Sonnet; koordynator tylko czyta kartę | K6, P1 |
| IW-11 | `handover` — jeden rekord, cztery rendery | `tools/handover.py` z `runs.jsonl` + głowa QUEUE + spec `proposed` + `places.json` → teleport / karta pakietu / rekord sekretarza / „Od Twojego wyjścia”; paragon teleportu; `supersedes` | `TELEPORT_S1.md` wygenerowany równoważny ręcznemu; W9 z tego samego skryptu | minuty do pierwszej produktywnej komendy po teleportach; pytań nowej sesji do starej/do niego | Sonnet | E7, W9, K 6.1 |

**Pierwszy = IW-1 (zapis biegu), dlaczego:** to najtańszy krok po R88 v1 i P2 — jeden skrypt i jeden schemat, zero jądra, zero
buildu (jedyna maszyna buildu jest zajęta M1, 23.10), sekretarz już biegnie i już pisze `spec_runs` (P1), więc IW-1 tylko uogólnia
istniejący zapis. Jest też **warunkiem** reszty: bez niego liczniki W1/W9 liczyłyby się z prozy QUEUE, paragony E1 nie miałyby domu,
`handover` nie miałby źródła, a pierwszy pomiar oszczędności (linie QUEUE ręczne vs generowane) jest widoczny po 48 h. K1+K3
(IW-2), które kąt koordynatora stawiał pierwsze, mierzy się dopiero na NASTĘPNYM pakiecie — więc idzie drugie, równolegle, gdy
tempo pozwala (`tools/pace.py`). Kolejność dalej: IW-3 → IW-4 (one dają jemu ekran) → IW-5, IW-6 → IW-7 → IW-9, IW-10, IW-11.
IW-8 biegnie **niezależnie** w sesji S1-VM, gdy tylko jej karta zaufania zostanie przyjęta (fakt 23.10: odmawiana), bo bez jego
wyników `where: vm` zostaje presetem `cloud`. Z R89 bez zmian: P6, P7 (spec jako węzły grafu — IW-7/IW-2 są jego danymi wejściowymi),
P8 (routing korekt złożonych — nie dotyka jego dotknięć), P9, P10, P11. Pakiet bez pomiaru „po” dostaje advisory w karcie, nie
blokadę. **9 z 11 pakietów może wykonać Sonnet/Haiku bez koordynatora** (koordynator czyta karty); IW-8 — sesja VM; Fable tylko
przy sprzeczności z filozofią zgłoszonej przez kontrolę.

## §5 Decyzje z odwrotem (koordynator, 2026-10-23; każda to preset)

| Decyzja | Dlaczego | Odwrót |
|---|---|---|
| Zapis biegu jest jednym źródłem; liczniki, paragony i karty przekazania to rendery | trzy kąty czytają trzy rzeczy z tego samego rekordu; 37/60 commitów to księgowość, której nikt nie czyta jako danych | proza QUEUE jak dziś (render może ją nadal produkować) |
| Ścieżki zapisu bez tokenów; model tylko jako warstwa odczytu z paragonem | jego gest i jego cytat są nietykalne; każda tura modelu na ścieżce zapisu to koszt i ryzyko interpretacji | model na ścieżce zapisu jako jawna opcja z paragonem |
| Kierunek decyduje o magazynie: gesty → baza strony, biegi → repo; sekretarz lustruje | konflikt 1; każde miejsce pisze za 0 tam, gdzie dociera | jedno miejsce po pomiarze dostępności |
| Jedna skrzynka z `class`; jedna tabela słów z dwoma renderami | dwa stany „czeka” i dwie tabele słów = dwa źródła błędów | rozdzielenie kolekcji/tabel po pomiarze |
| `ask_after` od pierwszego pokazania; `if_silent` przez `run_once_at` | pytanie nie może rozstrzygnąć się, zanim je zobaczył; 0 pollingu | kadencja sekretarza |
| Podtrzymanie VM na czas biegu (`running where: vm`), nie na bezczynności; tętno i sekretarz nie są biegiem | konflikt 4; R90 „Zadbaj” + polityka celuje w bezczynność | wyłączyć timer; „zawsze” po jego słowie |
| Rola w rutynie, tabela rola→model osobno; `start_gate` zapisuje rozwiązany model; brak modelu = advisory | kalibracja cennika z tego, co biegło; twardy błąd = ograniczenie | `--log-only` |
| Koszt przy geście + suma dnia; paragon per pozycja o dotknięcie dalej | SLOWA:66; szum vs decyzja | preset „przy każdej pozycji” |
| Nic nie jest filtrem tego, co widzi: wszystko widoczne, zwinięte, „N zwiniętych — pokaż”; budżet uwagi = licznik + advisory | ZASADA WŁAŚCICIELA; ukrycie bez odwrotu | — |
| Pora i uwaga jako wiersze routingu przestawiane jego słowem; wnioskowanie tylko proponuje | cicha decyzja ukrywa | usunąć wiersz |
| „stop” adresuje miejsca; kanał do VM = flaga w repo czytana przez tętno | 2 przypadkowe dotknięcia 23.10; `send_message` do VM odmówiony | „stop natychmiast wszędzie” |
| Pomiar jego zachowania = tylko znacznik ostatniej wizyty; to, co wysyłamy, mierzymy sami | zgoda, o którą nie pytamy w nocy | jego słowo włącza więcej |
| Wszystkie rutyny i narzędzia widoczne i uruchamialne przez niego z kartą `start_gate`; kolejność = preset zasiany jego prośbami | R90 „wyeksponować wszystkie narzędzia”; nic niedostępne z definicji | inna kolejność |
| Bieg czeka na niego tylko w klasach CLAUDE.md i przy drogim odwrocie; reszta = decyzja wstępna W3 | R75, R62 | „czekaj zawsze” per klasa |
| Runner v1 bez stanu własnego; dom rutyn `tools/routines/` w repo; jądro przez import P7 | KOD ≠ DANE; każda cecha runnera to cecha do przeniesienia drugi raz; maszyna buildu zajęta | od razu w jądrze |
| `CLAUDE.md` jako prefiks wspólny + `spec:` wg tematu; mierzone na następnym pakiecie | konflikt 11; decyzja z góry = zgadywanie | samo `CLAUDE.md` |
| Aplikacja jako konsola (W10) = opcja „później” z datowanym kosztem w E9 | R90 „przeglądarkowy, nie?”; build zajęty | wstawić po P7 |
| Pierwszy pakiet IW-1; IW-8 niezależnie w sesji VM | §4 | kolejność w `QUEUE_S1.md` |

## §6 Oryginalne pomysły z trzech kątów i dyskusji (każdy z tanim eksperymentem i miarą)

| # | Pomysł | Skąd | Tani eksperyment | Miara |
|---|---|---|---|---|
| 1 | **Linia statusu = render zdarzenia z `counter_delta`**: koordynator nie pisze linii, bieg je pisze, a jego lista z 00:1x to schemat zdarzeń, nie prośba o styl | K7 + W1/W7 (dyskusja) | IW-1: 48 h, generowane obok ręcznych | ręczne/generowane; linie bez zmiany licznika (cel 0 — preset filtru) |
| 2 | **Jeden `handover`, cztery rendery** — „Od Twojego wyjścia” jest przekazaniem zmiany od nocnej zmiany do niego | E7 + W9 (powstało w dyskusji) | IW-11: `TELEPORT_S1.md` wygenerowany z danych | pytań nowej sesji; jego pierwsza wiadomość po powrocie |
| 3 | **„stop” adresuje miejsca** i pokazuje koszt utopiony z paragonu biegu; odpowiedź jednym dotknięciem per wiersz | W8 + E5 + K1 (dyskusja) | IW-5: „stop” przy biegu testowym | przypadkowe „stop” bez straty |
| 4 | **Gest = ten sam kształt co warunek wstępny** `{…, why/effect, cost, reversal, since}`: polityka z pokazanym skutkiem zamiast wiedzy plemiennej | W (6.1, z K5) | IW-5: `words.json` + `places.json` w jednym schemacie | ile reguł CLAUDE.md da się zapisać tym kształtem (preset: zliczyć) |
| 5 | **Jedna tabela słów zasiana jego słownikiem z cytatów**, dwa rendery (telefon, warsztat) | W (6.1, z K9) | IW-5: 8 słów z R-id, test wariantów ASR | trafienia/chybienia; słowa dopisane przez niego |
| 6 | **`ask_after` od pierwszego pokazania + `run_once_at`** — pytanie nie rozstrzyga się, zanim je zobaczył; 0 pollingu | E (6.1) | IW-3: jedno pytanie z terminem | rozstrzygnięć milczeniem przed zobaczeniem (cel 0) |
| 7 | **Ścieżki zapisu bez tokenów** jako zasada ekonomii; model = warstwa odczytu z własnym paragonem | E (uogólnienie W 6.2 p.6) | IW-1/IW-3: zliczyć wywołania modelu na ścieżce zapisu | 0 na ścieżce zapisu; tokeny warstwy odczytu per gest |
| 8 | **Kierunek decyduje o magazynie** (telefon → baza strony, miejsca → repo), sekretarz lustruje | E 6.4 #1 | IW-1 + IW-4: licznik z „stan z HH:MM” | opóźnienie lustra; rozjazdy |
| 9 | **Rozwiązany model zapisany na paragonie kalibruje cennik ról**; luz R63 jako parametr tabeli | K 6.1 + E 6.1 | IW-6: 5 biegów z `resolved` | odchylenie estymata/koszt per rola |
| 10 | **Jego liczba jako próbka `source: owner`** rozpoznana w zdaniu i potwierdzona dotknięciem | E4 + W (6.1) | regex „% tygodniowego” w sekretarzu + wiersz w kolejce | próbek/dobę od niego; poprawek |
| 11 | **Hash pakietu kontekstu = dowód trafienia cache**; prefiks bajt w bajt mierzony `cache_creation` | K3/K4 + E10 | IW-2: dwa agenty, diff prefiksu = 0 | `cache_creation` vs rozmiar prefiksu |
| 12 | **Mapa miejsc jako wiersz liczników odpowiada na pytania z R90** bez tury Fable | W (6.1, z E5) | IW-4: `places.json` statyczny | pytań o miejsca w czacie (cel: w dół) |
| 13 | **Znacznik „to przykład” jako znalezisko kontroli i addendum**; agent nie restartowany | W4 + K 6.1 | IW-9: jeden znacznik → addendum | czas znacznik→addendum; powroty klasy błędu |
| 14 | **Rekonesans VM jako pierwszy bieg nowego formatu**: ten sam rekord jest testem K7 i ziarnem E5 (nowe w syntezie) | §4 IW-8 | `vm.recon` pisze `routine_run` + `places.json` | czy rekord wystarczył nowej sesji bez pytań |
| 15 | **Wnioskowanie pory tylko proponuje wiersz** („ustawić noc 01–07? Tak/Nie”) | W/E 6.2 | IW-4: propozycja z `localStorage` jako pytanie W3 | przyjęte/odrzucone propozycje |
| 16 | **„Co pamiętasz o…” jako zapytanie zwracające gałąź** (0 tokenów) zamiast tury Fable | W5 + E 6.1 | filtr stanów w wyszukiwarce strony | pytań o rzeczy obecne w spec w czacie |

## §7 Pytania tak/nie dla właściciela (tylko pieniądze · prywatne dane · sprzeczność z wymaganiem)

| # | Pytanie | Klasa | Preset do czasu odpowiedzi (`if_silent`) | Liczba |
|---|---|---|---|---|
| 1 | **Czy podtrzymywać VM także na bezczynności** (gdy nie biegnie żaden zapisany bieg `where: vm`)? Podtrzymanie na czas biegu robię bez pytania (koszt biegu na paragonie; R90 „Zadbaj o te wszystkie rzeczy”). | pieniądze | **nie** — VM gaśnie wg Twojej polityki 30 min; każdy bieg na VM dostaje advisory „maszyna może zgasnąć o HH:MM” | godziny/dobę i stawka — po rekonesansie (IW-8); do tego czasu liczba nieznana, więc pytanie czeka w kolejce bez terminu |

Brak innych pytań: baza strony i zadania zaplanowane to ten sam dostawca, co sesje (żadne dane nie idą do nowego dostawcy); pomiar jego
zachowania ograniczony do znacznika wizyty (jego słowo włącza więcej); wszystko inne to decyzje techniczne z odwrotem (§5).
Nie-pytanie, ale **dotknięcie tylko jego**: karta zaufania sesji VM (platforma odmawia `send_message`/startu bez niej, stan 23.10) —
bez niej IW-8 nie rusza, `where: vm` zostaje `cloud`.

## §8 Luki (pomiary do zrobienia, nie problemy)

| # | Luka | Dlaczego ważna | Zamyka |
|---|---|---|---|
| L1 | Słowa/dobę do niego (baza z QUEUE i czatu 21–23.10) — nikt nie policzył | waluta jego czasu bez bazy; licznik W1 | IW-4 (skrypt zliczający) |
| L2 | Koszt pustego i niepustego biegu sekretarza (`spec_runs` z P1: ile biegów zapisano — do odczytu) | kadencja operatora; IW-10 | IW-1 scala i liczy |
| L3 | Narzędzia sesji na VM: nic nie sprawdzone (TELEPORT §5); co liczy się jako aktywność dla 30 min; czy baza strony osiągalna z VM; czy `watch_url`/POST ze strony działa | `where: vm`, kanał „stop”, lustro, budzenie | IW-8 |
| L4 | Czy AGEDS i Watchdog-JH16 mają serwer / repo / stan (R90 pytanie) | mapa usług | IW-8 (rekonesans + repozytoria) |
| L5 | `cache_creation` per agent vs rozmiar prefiksu; czy świeże sesje zadań dzielą cache | wartość IW-2/IW-7 i kadencji | IW-2 |
| L6 | Tury koordynatora oznaczone klasą kroku (mechaniczny / osąd) — dziś dwa punkty, nie baza | liczba dla IW-10 | IW-1 (`events.class`) |
| L7 | Mnożnik Fable w limicie; koszt pakietu Haiku (R89 L2) | routing pakietów po koszcie | próbki `rate_limit_event` przy biegach o znanym `resolved` |
| L8 | Czy powiadomienie push jest możliwe z artefaktu (platforma) | kanał „czeka na Ciebie: 1” | sprawdzenie w jednym pakiecie strony |
| L9 | Twardy limit `ask_after` i tempo czytania (słowa/min) — presety bez pomiaru | W3, waluta czasu | kalibracja z jego odpowiedzi (R59: on poprawia) |
| L10 | Kontrola Haiku tego dokumentu (4 antywzorce + R60) — nie wykonana | check-before-use (CLAUDE.md) | następny krok koordynatora |
| L11 | Odpowiedniość pakietów R89 P3–P5 ↔ IW-1/2/6/11: P3 i P4 wchłonięte, P5 częściowo (kompakt przed pauzą zostaje osobno) | żeby nie powstały dwa pakiety na to samo | `QUEUE_S1.md` przy planowaniu |
