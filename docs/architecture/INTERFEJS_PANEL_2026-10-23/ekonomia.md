# Interfejs współpracy — kąt ekonomii i kanałów: tokeny, czas właściciela, urządzenia, VM, sesje (R90, projekt 3/3, Fable, 2026-10-23)

Jeden z trzech projektów pakietu R90 (pozostałe: kąt właściciela `wlasciciel.md`; kąt koordynatora i rutyn). Buduje na
`INTERFEJS_WSPOLPRACY_2026-10-23.md` (R89: interfejs = perspektywa grafu aplikacji + cienki widok na telefon; §4.7 budżet per
rola; §5 pakiety P1–P11) i na tym, co działa (stan 2026-10-23): strona spec z przyciskami i kolejką „Czeka na Ciebie”, sekretarz
co godzinę (preset), `tools/pace.py` + `pace_plan.json`, `LIMITS_LEDGER.md`, karta `TELEPORT_S1.md`, prompt startowy S1-VM.
Wszystko niżej to presety i przykłady (R58, R83, R84): każda liczba jest pomiarem z datą albo presetem do zmiany w danych; nic nie
jest jego słowami, jeśli nie stoi w cudzysłowie z numerem R; fakt o teraźniejszości ma datę (R60). Kontrola przed użyciem (Haiku,
4 antywzorce) — do zrobienia przez koordynatora przed syntezą.

## §1 Teza

Pracujemy w **dwu walutach naraz** — punktach konta (tokeny) i jego czasie czytania — i marnujemy obie tam, gdzie licznik
pojawia się dopiero *po* wydatku: agent 2–5× ponad plan, zryw 5 agentów, kontekst przepisany po pauzie, etap poprawek na Fable
przez pomyłkę, maszyna, która gasi się pod biegnącym agentem. Interfejs od strony ekonomii to **jeden paragon z otwartą listą
walut przy każdym węźle** (pakiet, decyzja, wiadomość do niego, usługa na VM, teleport) i **licznik przed startem**, nie po:
start agenta, wiadomość do właściciela, uruchomienie usługi — każde pokazuje szacunek w obu walutach, miejsce w oknie i to, co
przekroczy, zanim ruszy; przekroczenie to advisory z „nie pytaj następnym razem”, nie zakaz. „Co gdzie biegnie” (sesja w chmurze,
S1-VM, S2, sekretarz, strona, CI) to węzły grafu z własną walutą miejsca, polityką bezczynności i tętnem — żeby praca bez tokenów
(build, bramka, e2e, czytanie logu) biegła w miejscu, które jej nie liczy w tokenach, a jego czas szedł tylko na decyzje.

## §2 Dowody z jego słów i pomiarów

| Co | Dowód (cytat dosłowny z numerem albo pomiar z datą) |
|---|---|
| Druga waluta: jego czas | R62: „mój czas który bym zmarnował na przewijanie i szukanie jest więcej wart, w szczególności o 2:12 w nocy”; R62 też: „plany zużycia tygodniowego to sobie podkręć trochę, bo prawdopodobnie przez kilkadziesiąt godzin się nie będę odzywał” |
| Celem jest oszczędność i systematyka, nie schemat | R88: „poza wygodą jeszcze oszczędność tokenów”, „żonglerka modelami w zależności od stopnia złożonościi [sic] nietypowości mojej korekty”, „nie bierz tego schematu dosłownie” |
| Patrzeć, jak oszczędzają inni, przez pryzmat filozofii | R89: „popatrzeć jak ludzie oszczędzają tokeny, pomyśleć co warto wprost wykorzystać, co można jeszcze lepiej usprawnić, co oryginalnego” |
| VM gości wszystko, gaśnie po bezczynności, on administruje przez koordynatora | R90: „tam miałeś deployment wszystkich projektów przeprowadzić”, „administrujesz tamtą maszyną”, „po pół godzinie bezczynności się zamyka i nie wiem czy chodzący Code to blokuje, Zadbaj o te wszystkie rzeczy” |
| Przeprowadzka to teleport stanu, nie świeża maszyna | R72: „teleporta bardziej niż buttstrapa”; R71: „jedną komendę do wklejenia przez ssh, żebym znów nie musiał szukać tych nazw wszystkich” |
| Brak jego decyzji nie blokuje; luz dla lepszego modelu | R75: „ostrożnie podejmij wstępną taką żeby się dało z małymi kosztami w razie w wycofać”; R63: „jak zmieniam model na lepszy to domyślnie możesz poluzować troszkę wymagania”; R61 (parafraza w rejestrze): droga praca po zatwierdzonej parafrazie |
| Agenci, nie rozmowa, zjadają konto | nasze-04 (21.10): agenci 95 % wydatku S1 (538 z 566 mln równoważników), sesja główna 5 % |
| Koszt rośnie z kwadratem długości agenta | nasze-01: 98 % odczytów jednego workflow w pięciu agentach po 100–550 tur |
| 24 % wydatku agentów to nie praca | nasze-02 (21.10): 7,3 % prefiks startu (≈ 39 tys. tokenów × 81 agentów), 17,1 % konteksty ≈ 190 tys. przepisane po pauzie > 5 min (build/ctest/e2e wewnątrz tury) |
| Pakiety biegną 2–5× ponad plan | nasze-07; ledger 21–22.10: K1b 167 wywołań (plan ~50), K1c2 154, W2a 182; najtańszy FLAKE1 39 wywołań — brief z hipotezą |
| Zryw równoległy przepala okno | nasze-09: 10.10 pięciu agentów = okno w 8 min (57,5 mln odczytów cache, CLAUDE.md); 23.10 10:11: 5 torów R89 = **24 % okna 5 h po 11 minutach** |
| Kurs walut okna | nasze-05 / ledger 01:28 21.10: jedno pełne okno 5 h ≈ 25 % tygodnia; ≈ 22 mln równoważników na 1 pkt tygodnia (górna granica) |
| Cennik ról (bazy do kalibracji) | nasze-06: pakiet Sonnet 0,43–0,5 pkt; projekt Fable 1,2–1,6 (preset 1,5); czytacz logu Haiku 0,07 pkt, ale 1,5 mln świeżego zapisu każdy (nasze-16) |
| Restart kosztuje jak dwa pakiety | nasze-08 (22.10): trzy zatrzymania i restarty jednego workflow ≈ 19 mln ≈ 0,9 pkt; `resumeFromRunId` odtwarza tylko niezmieniony prefiks |
| Etap poprawek na Fable przez pomyłkę | ledger 23.10 11:23: `check-before-use` 626 tys. tokenów / 37 wywołań / 14 min, poprawki poszły na Fable, bo skrypt nie ustawiał `model` |
| Tura koordynatora jest droga sama w sobie | ledger 21.10 06:08: sesja główna 217 ruchów, średnio 377 tys. kontekstu na ruch (chwilowo 587 tys.) — każda odpowiedź na zdarzenie to przeczytanie całego kontekstu |
| Telemetria jest, ale tylko z sesji i za całe konto | nasze-11: `rate_limit_event` niesie 5 h / 7 dni, niedostępne skryptom; jego własna próbka 23.10 00:12: „dopiero 46% tygodniowego poszło” |
| Maszyna gasi pracę: dysk, nie równoległość | nasze-17 (23.10 04:55, 07:19): dwa restarty kontenera przy ~95 % dysku, dwie bramki i agent w e2e stracone; `nohup` ginie między turami |
| Tempo dnia i plan | ledger 23.10 17:30: tydzień 66 %, punkt 80 % w sob 08:00 → ≈ 14 pkt / 14,5 h ≈ 1 pkt/h; nasze-18: 16 pkt/dobę |
| Konto jest wspólne | ledger 21.10 06:08: od 12.10 S1 ≈ 1934 USD, S2 ≈ 429 USD, sesja „Prompt review” ≈ 397 USD wg cen API — udziału w oknie nie da się rozdzielić, tylko w tokenach |

## §3 Propozycje

Każda: **co** · **dla kogo** · **mechanizm ogólny vs przykład** · **jak mierzyć** · **koszt** · **odwrót**. Numeracja E1… to
etykiety, nie ranking (kolejność — preset).

### E1 Paragon dwuwalutowy przy każdym węźle
- **Co:** jeden rekord kosztu `receipt` z **otwartą listą walut**: `{currency, value, unit, source, at, reliability}`; przykłady walut:
  punkty tygodnia, punkty okna 5 h, tokeny-równoważniki (wagi jako dane z datą), USD wg stawek datowanych, **słowa/minuty jego czytania**,
  minuty zegara, godziny VM, GB dysku, minuty CI. Paragon ma pakiet, decyzja, dokument, wiadomość do niego, usługa na VM, teleport.
- **Dla kogo:** on (na stronie: „ten pakiet: 0,7 pkt · 40 s czytania”), koordynator (karty), skrypty (`pace.py`).
- **Mechanizm vs przykład:** to ten sam profil ekonomii, który jądro liczy dla wiadomości (P38 slice 4, nasze-28) — inna księga,
  ten sam kształt; para „tokeny + czas czytania” jest **przykładem**, silnik przyjmuje każdy zestaw walut, także bez sensu
  (advisory „waluta bez źródła”). Wagi równoważników (odczyt 1, zapis 12,5/20, wyjście 50 — 21.10) to preset z datą.
- **Mierzyć:** udział węzłów z paragonem; różnica estymata vs próbka `rate_limit_event` per doba; słowa do niego na decyzję.
- **Koszt:** mały (schemat + zapis z `usage_report.py` i kart; ≈ 1 pakiet Sonnet ≈ 0,5 pkt).
- **Odwrót:** ukryć walutę w perspektywie (preset); rekordy zostają.

### E2 Budżet uwagi właściciela jako dane (siostra `pace_plan.json`)
- **Co:** `attention_plan.json` — ile czytania na wizytę/dobę przyjmuje (otwarte pola; przykład presetu: 3 min na wizytę, 1 linia
  statusu na zdarzenie), klasy wiadomości z najkrótszą formą odpowiedzi (cyfra / przycisk / własne słowa — lista otwarta) i porą
  (noc: tylko to, co blokuje — przykład). Każda linia do niego **obciąża ten budżet**; strona pokazuje „Do przeczytania: N słów ≈ M min”.
- **Dla kogo:** koordynator (zanim napisze), sekretarz (co eskalować), on (widzi, ile mu dziś „kosztuję”).
- **Mechanizm vs przykład:** predykat na rekordzie wiadomości (`class`, `words`, `blocking`, `hour`) → kanał i forma (E9); przekroczenie
  → advisory koordynatorowi, nie blokada. Wartości to presety; on zmienia jednym słowem („mniej”, „pokazuj wszystko”).
- **Mierzyć:** słowa/dobę do niego (baza: zmierzyć z QUEUE i czatu za 21–23.10, luka), decyzje na 100 słów, czas od pytania do odpowiedzi.
- **Koszt:** mały (plik + licznik słów w generatorze strony i w szablonie statusu).
- **Odwrót:** usunąć plik; strona i czat wracają do stanu z 23.10.

### E3 Licznik przed startem: karta `preflight` dla każdego agenta, etapu i usługi
- **Co:** przed `agent()` / workflow / `systemctl start`: model i jego rola (tabela rola→model z datą, R89 §4.2), szacunek w pkt
  (z `cost_per_package`), plan wywołań, pozycja okna 5 h i tygodnia względem linii, agenci w toku, **co przekroczy** (punkt planu,
  tempo, odstęp — reguły `pace.py`), miejsce (E5) i czy to miejsce ma dysk/żywotność (nasze-17). Wynik: start / start z advisory /
  „czeka na Ciebie” (gdy przekroczenie to jego pieniądze albo ×10).
- **Dla kogo:** koordynator i skrypty workflow (automatycznie), on (karta w kolejce tylko przy przekroczeniu).
- **Mechanizm vs przykład:** `start_gate` = złożenie predykatów na danych (plan tempa, cennik ról, stan okna, miejsce); każdy
  `agent()` w skrypcie **musi nieść `model` jawnie** — brak = advisory z pokazanym dziedziczonym modelem (błąd z 11:23 to przykład
  klasy „etap na innym modelu niż preset roli”, nie reguła „nigdy Fable do poprawek”).
- **Mierzyć:** etapy uruchomione na innym modelu niż preset roli (baza 23.10: 1 workflow, 626 tys. tokenów); udział Fable w tokenach
  (baza 42,5 %, nasze-15); `calls_used/plan_calls` (baza 2–5×).
- **Koszt:** mały–średni (wspólny moduł w `tools/workflows/`, karta jako JSON; ≈ 1 pakiet).
- **Odwrót:** `start_gate` w trybie „tylko loguj”.

### E4 Dwa wskaźniki tempa na stronie i w statusie, próbka także od niego
- **Co:** okno 5 h vs linia, tydzień vs punkty planu, agenci w toku, „następny start możliwy o …”; źródła próbek: `rate_limit_event`
  sesji (nasze-11) **oraz pole „podaj % z aplikacji”** na stronie — jego liczba (jak 46 % o 00:12) jest próbką z `source: owner`.
- **Dla kogo:** on (jedno spojrzenie zamiast czytania ledgeru), koordynator, sekretarz (dopisuje próbki do ledgeru jako dane).
- **Mechanizm vs przykład:** ledger jako `jsonl` (dziś proza w `LIMITS_LEDGER.md`) → ta sama perspektywa „pomiar” co inne węzły;
  linia tempa to funkcja-preset, nie prawo (ledger: „preset, nie prawo”).
- **Mierzyć:** próbki/dobę (baza 23.10: 5 ręcznych), odchylenie od planu w punktach, liczba zrywów (> X pkt w 15 min — X preset).
- **Koszt:** mały (konwersja ledgeru + sekcja strony); zero tokenów w biegu (próbka to tura sesji, która i tak jest).
- **Odwrót:** sekcja ukryta; ledger-proza zostaje.

### E5 Mapa miejsc: „co gdzie biegnie” z walutą miejsca, polityką bezczynności i tętnem
- **Co:** węzły `place` (przykłady: sesja chmurowa S1, S1-VM `devbox-20260923`, S2 emulator, zadanie „Sekretarz spec”, strona-artefakt,
  GitHub CI, jego telefon) z polami: co tam biegnie, **waluta miejsca** (sesje: punkty konta; VM: godziny × stawka, dysk; CI: minuty;
  strona: 0 tokenów), polityka bezczynności, ostatnie tętno, kto może tam startować pracę. Węzły `service` (ChatADHD server+web,
  Watchdog-JH16, AGEDS — lista z R90, otwarta) z krawędzią `hosted_on`, stanem (żyje / śpi / nie postawiono), portem, **czy trzyma
  jego dane** (kształt B — jego tak/nie), zużyciem CPU/RAM/dysku, kosztem/dobę.
- **Dla kogo:** on (jedna tabela: „co mam, co chodzi, co kosztuje, co śpi”), S1-VM jako administrator, koordynator.
- **Mechanizm vs przykład:** miejsce, usługa i polityka to zwykłe węzły grafu z proweniencją (R89 §4.1); **wyłączanie po 30 min
  bezczynności** jest polityką instancji na jego koncie (jego pieniądze) — interfejs pokazuje, *co liczy się jako aktywność* (ssh?
  CPU? proces `claude`? — fakt do zmierzenia w rekonesansie, `VM_START_PROMPT` zad. 1.2) i oferuje podtrzymanie jako **opcję z
  paragonem** („podtrzymanie przy biegnącym agencie: ≈ H godzin/dobę × stawka = … zł; bez podtrzymania: agent ginie jak przy
  restarcie kontenera, nasze-17”). Decyzja jest jego (pieniądze), jako tak/nie z liczbą; preset do czasu odpowiedzi: nie podtrzymywać,
  a każdy start agenta na VM dostaje advisory „maszyna może zgasnąć o …”.
- **Mierzyć:** godziny VM/tydzień i koszt; liczba zgaśnięć pod pracą; czas od zgaśnięcia do wykrycia; wolne GB przed buildem (próg
  ≥ 5 GB — preset z CLAUDE.md).
- **Koszt:** dokument + `systemd`/cron „tętno” na VM (zero tokenów), perspektywa w grafie później (jądro: 1 pakiet).
- **Odwrót:** statyczna tabela w `docs/ops/`.

### E6 Praca bez tokenów idzie do miejsca bez tokenów
- **Co:** operacje długie i bez decyzji modelu (build, ctest, e2e, bramka, czytanie logu CI, odczyt metryk) biegną na VM/CI jako
  **zlecenia** z wynikiem-wycinkiem (R89 P9), a agent nie czeka w turze: zleca, kończy turę kartą, wraca po wyniku (webhook /
  zadanie zaplanowane). Każda operacja ma rekord `{where, clock, tokens}` — widać, ile pakietu to tokeny, a ile zegar.
- **Dla kogo:** skrypty workflow i agenci (automatycznie), on (paragon pakietu rozbity na „model” i „maszyna”).
- **Mechanizm vs przykład:** reguła ~4 min (CLAUDE.md pkt 5) to **preset progu**, mechanizm to zlecenie z adresem miejsca; VM jest
  przykładem miejsca, CI drugim, kontener chmury trzecim (z advisory o dysku).
- **Mierzyć:** udział kontekstów przepisanych po pauzie (baza 17,1 %, nasze-02); tokeny czytaczy logów (baza 1,5 mln/agent, nasze-16).
- **Koszt:** średni (wrapper + kolejka zleceń na VM; 1–2 pakiety Sonnet).
- **Odwrót:** operacja wraca do wnętrza tury (jak dziś).

### E7 Teleport jako generowana karta z własnym paragonem
- **Co:** `tools/teleport.py` buduje kartę `handover` z danych: głowa kolejki, parafrazy `proposed`, briefy, `pace_state`, ostatnia
  próbka ledgeru, worktree i gałęzie, zadania zaplanowane, mapa miejsc (E5), „sprawdź w pierwszej godzinie” (TELEPORT_S1 §5) —
  plus **koszt teleportu**: ponowne zbudowanie kontekstu (0,2–0,46 mln tokenów na żywy kontekst, pomiar z CLAUDE.md), praca w toku,
  która przepadnie (agenci, bramki), i warunek „tylko między pakietami” (`docs/ops/PRZEPROWADZKA_GCP_2026-10-23.md` §5) jako predykat, nie zdanie.
- **Dla kogo:** nowa sesja (czyta kartę, nie prozę), on (widzi, że przeprowadzka nic nie zgubiła: lista „przeniesione / zostało w chmurze”).
- **Mechanizm vs przykład:** karta = węzeł z proweniencją i `supersedes` poprzedniej; ręczna `TELEPORT_S1.md` jest pierwszą instancją.
- **Mierzyć:** minuty do pierwszej produktywnej komendy po teleportach; liczba pytań nowej sesji do starej/do niego; co trzeba było dopisać ręcznie.
- **Koszt:** mały (skrypt czytający pliki, które już są).
- **Odwrót:** karta pisana ręcznie jak 23.10.

### E8 Równoległość jako funkcja okna, nie liczba
- **Co:** `allowed_parallel(t)` = funkcja z danych (linia − zużycie, koszt agenta wg roli, odstęp) zamiast presetu „3”; salwa startuje ze
  **staggerem** (R89 P4, preset 0) i wspólnym prefiksem bajt w bajt; strona pokazuje wykres zużycia okna z zaznaczonymi startami.
- **Dla kogo:** koordynator i skrypty; on (jednym słowem podnosi/obniża, jak „preset 4” dla S2 13.10).
- **Mechanizm vs przykład:** liczba agentów jest **wynikiem** funkcji-presetu z `pace_plan.json`; „3” i „stagger 0” to przykłady (R83).
- **Mierzyć:** % okna zużyty w 15 min po salwie (baza 24 % w 11 min, 23.10); okno niewykorzystane do resetu (druga strata z ledgeru).
- **Koszt:** mały (rozszerzenie `pace.py`).
- **Odwrót:** stała liczba w pliku.

### E9 Kanały z kosztem kanału i routingiem klasy wiadomości jako danymi
| Kanał (przykład) | Niesie (przykład) | Koszt w tokenach | Koszt jego czasu | Stan 23.10 (fakt datowany) |
|---|---|---|---|---|
| strona na telefonie | kolejka, przyciski, paragony, wskaźniki tempa, mapa miejsc | 0 (render ze spec/kart) | niski, w jego porze | działa (artefakt, `spec_inbox`) |
| czat sesji | dyskusja, linie statusu, pytania tak/nie | wysoki (tura koordynatora ≈ 0,3–0,6 mln kontekstu) | wysoki (czyta wszystko) | działa; styl: tylko linie statusu |
| własny web UI aplikacji | te same perspektywy z grafu | 0 | niski | serwer bez publicznego adresu z jego telefonu |
| powiadomienie push | „czeka na Ciebie: 1, drogi odwrót” | 0 | sekundy | do sprawdzenia (platforma) |
| głos → cytat dosłowny | jego słowa jako pozycje spec | ASR (tani) | zero czytania | brak (R89 O9) |
| GitHub (commity, karty, CI) | prawda o kodzie, bramki | 0 (minuty CI) | tylko gdy chce | działa |
Routing: klasa wiadomości (status / pytanie / advisory / decyzja z kosztem odwrotu / awaria) × pora × budżet uwagi (E2) → kanał i
forma, zapisane jako dane z odwrotem; każda wiadomość wysłana poza presetem ma w paragonie powód. **Mierzyć:** decyzje per kanał,
słowa na decyzję, czas do odpowiedzi. **Koszt:** mały. **Odwrót:** wszystko do czatu.

### E10 Rutyny jako przepływy z paragonem i wspólnym prefiksem
- **Co:** rutyna (jego przykład R90: „przy starcie nowego podwykonawcy całą filozofię mu zapodać do kontekstu”) = zapisany przepływ
  `{prefix (wspólny, bajt w bajt), own, role→model, estimate, place, measure_after}`; filozofia **już** jedzie z `CLAUDE.md` do
  każdego agenta (≈ 39 tys. tokenów prefiksu, 7,3 % wydatku — nasze-02), więc ekonomia rutyny to: prefiks pierwszy (trafienie
  cache), część własna na końcu, paragon biegu w karcie. Przykłady rutyn: nowy podwykonawca, kontrola przed użyciem, bramka,
  próbka limitów, teleport, rekonesans VM, „zgaś/obudź usługę” — lista otwarta, dopełniana tym, co już jest w `tools/workflows/`.
- **Dla kogo:** on (jeden przycisk z ceną), koordynator, S1-VM (rutyny administracyjne bez tokenów).
- **Mechanizm vs przykład:** rutyna to przepływ w danych (ten sam rodzaj, co przepływy jądra), nie skrypt specjalny.
- **Mierzyć:** `cache_creation` na bieg vs rozmiar prefiksu (R89 P4); koszt biegu rutyny w czasie (kalibracja cennika ról).
- **Koszt:** mały na start (katalog rutyn jako JSON + istniejące skrypty); średni, gdy przeniesione do jądra.
- **Odwrót:** briefy ad hoc jak dziś.

## §4 Czego nie robić i dlaczego

| Nie robić | Dlaczego (dowód) |
|---|---|
| Osobnego produktu „panel kosztów” obok grafu aplikacji | R89 §6: interfejs to perspektywa grafu + cienki renderer; druga baza danych = synchronizacja i drugi koszt |
| Twardych limitów („max 3 agentów”, „nigdy Fable do poprawek”, „VM zawsze podtrzymana”) | CLAUDE.md: „NIE WYMYŚLAJ OGRANICZEŃ”; jedyny próg potwierdzany to ×10; wszystko inne to preset + advisory (E3, E8) |
| Szacowania limitów z liczby tokenów, gdy jest telemetria — ani brania `rate_limit_event` za koszt agenta | ledger (zasada 4): mierzymy, nie szacujemy; nasze-11: zdarzenie liczy całe konto (S1 + S2 + jego czaty), więc rozdziela się tylko tempo sumy |
| Podtrzymywania VM albo uruchamiania usług z jego danymi po cichu | to jego pieniądze i jego dane (CLAUDE.md: pytania tylko o to, jako tak/nie z liczbą — E5) |
| Czytania przez koordynatora pełnych transkryptów i logów | nasze-16: czytacz pełnego logu = 1,5 mln świeżego zapisu; tura koordynatora czyta 377–587 tys. kontekstu (ledger 06:08) — do niego trafia rekord, nie przebieg |
| Odpytywania agentów z sesji głównej („jak idzie?”) | każda tura główna to kwadrat kontekstu (nasze-01); zdarzenia przychodzą jako karty/wiadomości, nie przez polling |
| Restartu agenta na korektę | nasze-08: ≈ 0,9 pkt za trzy restarty; korekta = addendum po karcie (CLAUDE.md 6c) |
| Miary „koszt na commit” jako metryki projektowej | nasze-14: 2,94–3,32 USD/commit miesza wielkości; metryką jest koszt **przyjętego** wyniku per `spec:<id>` (ledger „Modele”) |
| Drugiego katalogu buildu na tej samej maszynie, bramki równolegle z agentem w jednym worktree | nasze-17 i CLAUDE.md: restart przy ~95 % dysku; jeden build dir, ≥ 5 GB wolne — E5 pokazuje to jako pole miejsca, nie jako zdanie do zapamiętania |
| Liczb w prozie zamiast w danych (ledger, cenniki ról, plan uwagi) | 23.10: ledger czytany w zrywach i ręcznie przeliczany (błąd 20,6 % → 19,2 % z 16:20 21.10); dane z datą i źródłem czyta strona, `pace.py` i on |

## §5 Pytania do dwóch pozostałych kątów

**Do kąta właściciela (`wlasciciel.md`):**
1. Jaką jednostkę jego uwagi liczyć na paragonie (słowa, sekundy, ekrany telefonu, liczba kliknięć) — i czy koszt przy każdej pozycji
   jest dla niego informacją czy szumem; jeśli szumem, to gdzie jedyne miejsce, w którym chce go widzieć (suma dnia? tylko przy decyzji)?
2. Czy nocą (jego „2:12”, R62) kanał z paragonem to tylko push „czeka 1, drogi odwrót”, czy nic — i kto ustala porę: on jednym
   słowem, czy wnioskowanie z czasów jego wizyt (znacznik wizyty już jest w `localStorage`)?
3. Jego liczba z aplikacji („46 %”) jako próbka tempa: czy pole na stronie to dobre miejsce, czy raczej rozpoznawać ją w jego
   zdaniach (sekretarz: regex na „% tygodniowego”) i tylko potwierdzać?

**Do kąta koordynatora i rutyn:**
4. Czy definicja rutyny niesie w sobie `role→model`, szacunek i miejsce (E3/E10 zakładają tak), czy koordynator dobiera je przy
   starcie — i gdzie wtedy ląduje luz z R63 („poluzować troszkę”): w funkcji `start_gate` jako parametr, czy w osądzie koordynatora
   poza danymi?
5. Czy karta teleportu (E7), karta pakietu (`single-package.js` CARD) i rekord sekretarza mogą mieć jeden kształt `handover` z
   paragonem — żeby jedna rutyna „przekaż dalej” obsłużyła zmianę maszyny, zmianę agenta i zmianę zmiany (noc/dzień)?
6. Kto jest wykonawcą rutyn bez tokenów na VM (tętno, zgaś/obudź usługę, pomiar dysku, build na zlecenie — E5/E6): sekretarz jako
   zadanie zaplanowane, osobny proces `systemd` bez modelu, czy S1-VM w turze — i jak koordynator w chmurze dostaje wynik bez polling-u?

## §6 Po dyskusji (2026-10-23)

Przeczytane: `wlasciciel.md` (W1–W10 i §6) i `koordynator.md` (K1–K9 i §6). Niżej tylko to, co zmienia kąt ekonomii; reszta §1–§5
stoi. Jak wyżej: nazwy i liczby to przykłady/presety z `_history`, fakty datowane 23.10, jego słowa tylko dosłownie z R-id. Kontrola
Haiku (4 antywzorce) nadal przed użyciem.

### 6.1 Co biorę

| Od | Co | Dlaczego z kąta ekonomii i co to zmienia u mnie |
|---|---|---|
| K7 | Zapis biegu `loom.routine_run/1` jako jedno źródło; paragon E1 = pole `receipt[]` tego zapisu | Paragon bez domu byłby czwartym magazynem (synchronizacja = koszt). Zapis pisze skrypt biegu — 0 tokenów na ścieżce zapisu; dziś linie statusu pisze Fable (pomiar K: 37/60 commitów to księgowość). E1 zostaje kształtem rekordu; E4 (próbki okna) zostaje osobną księgą dnia dołączaną po czasie, nie przypisywaną do biegu. |
| K3/K4 | Pakiet kontekstu = lista odwołań z hashem; brief = rekord z częścią wspólną bajt w bajt | E10 bez hasha jest hasłem: trafienie cache (R89 P4) wymaga identyczności bajt w bajt, a hash pakietu w zapisie biegu to jedyny tani dowód, że prefiks był ten sam. Pomiar: `cache_creation` na bieg vs rozmiar prefiksu (baza: 7,3 % wydatku to prefiks, nasze-02). |
| K1 | Katalog narzędzi z dostępnością per maszyna, datowaną, z zamiennikiem | Węzeł `place` (E5) dostaje pole `tools[]` z katalogu — „waluta miejsca” to także to, czym miejsce płaci zamiast tokenów (cron/`systemd` zamiast tury). Fakt 23.10: nic z narzędzi sesji nie sprawdzone na VM (TELEPORT §5). |
| K5 | Warunki wstępne jako dane `{check, why, since}` | Pola miejsca w E5 (dysk ≥ 5 GB, jeden build dir) dostają ten kształt zamiast zdania w CLAUDE.md; powód i data = odwrót, gdy przyczyna zniknie (np. `-gz` na dev preset zmniejszy build dir). |
| K6 | Operator na tanim modelu | `start_gate` (E3) i `pace.check` to kroki bez osądu → biegną w operatorze, nie w turze Fable (tura ≈ 377–587 tys. kontekstu, ledger 06:08). Warunek: koszt pustego biegu operatora zmierzony (P1), bo każde odpalenie świeżej sesji płaci prefiks. |
| K 6.1 | Rutyna niesie ROLĘ; tabela rola→model osobno, datowana; luz R63 = parametr tabeli per model koordynatora; miejsce jako `where` z presetem | Odpowiedź na moje pyt. 4 — przyjmuję. Warunek ekonomii: model ROZWIĄZANY przy starcie jest zapisany w biegu i na paragonie (nie sama rola), bo cennik ról kalibruje się z tego, co faktycznie biegło; błąd z 11:23 to brak właśnie tego zapisu. |
| K 6.2 | „nie odpytywać agentów” → odpytywany jest ZAPIS, nie agent | Uściślenie do §4: zakazem jest tura koordynatora spędzona na „jak idzie”; czytanie rekordu (plik, 0 tokenów modelu) jest wiedzą za darmo. |
| W1/W7/W9 | Liczniki jako render zdarzeń; „linia do niego zmienia licznik” jako preset filtru, nie prawo | Wskaźniki tempa (E4) i mapa miejsc (E5) stają się WIERSZAMI jego ekranu („okno 5 h: 61 % · linia 58 %”, „miejsca: 3 żyją · 1 śpi · 2 nie postawiono”) — zero osobnego ekranu, zero tokenów (render). |
| W3 | Pytanie z `if_silent`, `cost_to_reverse`, `ask_after`, `criteria` | Jedyne pytanie E5 do niego (podtrzymanie VM na bezczynności) i jedyny przypadek „czeka na Ciebie” w E3 (jego pieniądze, ×10) mają ten kształt. Dopisuję z ekonomii: `ask_after` liczy się od PIERWSZEGO POKAZANIA (znacznik wizyty), nie od utworzenia, z twardym limitem (preset) — inaczej pytanie zadane o 2 w nocy rozstrzyga się milczeniem, zanim je zobaczył, a zapis nie wie, czy widział. |
| W 6.2 p.2 | Koszt przy GEŚCIE i jako suma dnia; paragon per pozycja o dotknięcie dalej (preset jego perspektywy) | Odpowiedź na moje pyt. 1 — przyjmuję. Paragon per węzeł zostaje jako DANE (E1 bez zmian); perspektywa decyduje, gdzie się pokazuje. Pomiar presetu: czy otwiera paragony. |
| W 6.2 p.6 | Między jego dotknięciem/cytatem a zapisem nie stoi model | Uogólniam na zasadę ekonomii: **ścieżki zapisu są bez tokenów** — jego gest → spec (`spec.py approve`), bieg → zapis (skrypt), próbka → ledger. Model dokłada warstwę odczytu osobno, w swoim czasie, z paragonem. |
| W5 | „Co pamiętasz o …?” zwraca gałąź, nie odpowiedź | Każde takie pytanie w czacie to dziś tura Fable; zapytanie do strony = 0 tokenów. Największa pojedyncza oszczędność po stronie JEGO pytań. |
| W8, W 6.3 #5 | „stop” z kosztem utopionym; „stop” adresuje MIEJSCA | Render z paragonu biegu (K7+E1), zero nowej logiki. Kanał do miejsca = pole węzła `place`: fakt 23.10 — platforma odmówiła `send_message` do sesji VM bez jego zgody, więc preset kanału „stop” na VM = flaga w repo czytana przez tętno (opóźnienie = interwał tętna, preset), do czasu, gdy rekonesans pokaże lepszy. |
| W 6.1 (E7) | `handover` ma czwartą instancję: „Od Twojego wyjścia” | Zmiana nocna przekazuje jemu; ten sam rekord, inny render. Oryginalne w dyskusji — do syntezy. |
| W 6.3 #1, #3 | Jedna skrzynka z klasą jako polem (lista otwarta); sekretarz = operator v1, jedno zadanie | Dwie skrzynki = dwa odpytywania; dwa zadania = dwa prefiksy na kadencję. Drugie dopiero, gdy pomiar pokaże, że jedno nie nadąża. |

### 6.2 Co odrzucam albo osłabiam

U siebie, po ich uwagach:
1. **E2 `attention_plan.json` jako drugi plik planu** — osłabiam, jak K: czas czytania = waluta na paragonie, liczona z tego, co WYSYŁAMY
   (słowa pokazane × preset tempa czytania, `reliability: low`, dopóki on nie poprawi); próg = jeden wiersz routingu E9 z `_history`.
   Osobny plik dopiero, gdy pomiar pokaże zależność od pory/klasy. Powód: baza niezmierzona (słów/dobę za 21–23.10 nikt nie policzył) —
   plik byłby zgadywaniem z własnym cyklem utrzymania. Licznik W1 „dziś do Ciebie: N słów” zostaje.
2. **E2/E9 jako filtr tego, co widzi** — odrzucam własne „→ kanał i forma” tam, gdzie da się je czytać jako niepokazanie. Budżet uwagi
   jest licznikiem i advisory DLA KOORDYNATORA; u niego wszystko widoczne, zwinięte wg routingu, z wierszem „N zwiniętych — pokaż”.
   Ukrycie kosztuje zaufanie, które nie ma odwrotu; zwinięcie kosztuje 0.
3. **E3 „każdy `agent()` MUSI nieść `model`”** — „musi” z mojego „Co” znika; zostaje to, co stało w „mechanizmie”: advisory z pokazanym
   modelem dziedziczonym. Twardy błąd = wymyślone ograniczenie (K ma rację).
4. **E5 preset „nie podtrzymywać VM do czasu jego tak/nie”** — cofam. R90: „nie wiem czy chodzący Code to blokuje, Zadbaj o te wszystkie
   rzeczy” — prosił, żeby biegnąca praca nie ginęła; polityka 30 min celuje w koszt bezczynności, nie w zabijanie biegu. Nowy preset:
   podtrzymanie NA CZAS BIEGU (agent, bramka, build), godziny na paragonie tego biegu; na bezczynności — nie; jego pytanie tak/nie
   (kształt W3) dotyczy wyłącznie bezczynności. Jedyny próg, który potwierdza on (CLAUDE.md): prognoza godzin VM/dobę ponad ×10 bazy
   zmierzonej w rekonesansie; do pomiaru bazy — advisory. Mój stary preset wymyślał ograniczenie (domyślnie tracić pracę).
5. **E9 pora wnioskowana z wizyt** — osłabiam, jak W: wnioskowanie PROPONUJE wiersz routingu (Tak/Nie, `if_silent: bez zmian`), nigdy nie
   przestawia go samo. Błędna „noc” ukrywa decyzję, której by chciał — koszt jego czasu większy niż jedno pytanie.

U nich:
6. **K7 „commit per bieg” jako koszt do rozstrzygania (pyt. 7 K; ryzyko W „mnoży commity i push”)** — osłabiam samo pytanie. Fakt 23.10
   (`.github/workflows/`): `loom.yml` i `python.yml` biegną tylko na push do `main` z filtrem ścieżek (`loom/**`, `engine/**`, …) —
   push zapisów biegu na gałąź dokumentów = 0 minut CI, 0 tokenów. Zostaje szum w `git log` (filtr `--grep`). N=1 jest tani; N to preset.
7. **W 6.3 #4 „zdarzenie od razu do bazy strony”** — osłabiam do kierunku: pisać tam, gdzie PISZĄCY dociera bez tokenów. Bieg na VM nie
   ma bazy strony (fakt 23.10), skrypt workflow ma plik; jego telefon ma bazę strony, nie repo. Stąd decyzja per kierunek (6.4 #1).
8. **K6 operator jako świeża sesja „na wzór sekretarza” bez pomiaru pustego biegu** — warunek, nie odrzucenie: każde odpalenie płaci
   prefiks (≈ 39 tys. tokenów zapisu cache, nasze-02); co godzinę to 24 prefiksy/dobę za samo czuwanie. Kadencja = preset związany
   z pomiarem pustego biegu (P1). `if_silent` po `ask_after` nie wymaga czuwania: termin znany z góry → zadanie jednorazowe na godzinę
   terminu (`run_once_at`), 0 pollingu (6.4 #2).
9. **K2 runner v1 (`tools/routine.py`)** — przyjmuję z granicą: renderuje i zapisuje bieg, nie ma własnego stanu poza zapisem (bez
   kolejki, retry, harmonogramu) — każda cecha runnera to cecha do przeniesienia do jądra drugi raz (P7); K sam wpisał to ryzyko.
10. **W10 (aplikacja jako konsola)** — jak K: opcja z kosztem, nie teraz (jedna maszyna buildu zajęta, 23.10); ale nie „absence forever” —
    zostaje w E9 jako kanał z datowanym kosztem „build + synchronizacja grafu”.
11. **W/K pomiar jego uwagi** — zgadzam się z obiema stronami: preset = tylko znacznik ostatniej wizyty (konieczny dla W9 i dla
    `ask_after` od pokazania), reszta wyłączona do jego słowa. Z ekonomii: to, co WYSYŁAMY, mierzymy po swojej stronie bez zgody i bez
    kosztu; to, co ON robi, to jego dane. Druga waluta liczona z naszej strony wystarcza na start.

### 6.3 Odpowiedzi na pytania do tego kąta

- **K4 (waluty biegu; próbki całego konta):** `receipt[]` w kształcie E1: tokeny z `usage_report` (zmierzone), estymata ze `start_gate`
  (źródło: cennik ról, data), czas czytania (słowa wysłane × preset), godziny miejsca, GB, minuty CI — lista otwarta. Próbka
  `rate_limit_event` ani jego „46 %” NIE przypisuje się do biegu (liczy całe konto, nasze-11); to wiersz księgi dnia (E4) łączony po
  czasie; rozdziela się tylko tempo sumy.
- **K5 (gdzie operator; keepalive):** podział po tokenach, jak K 6.3 #2 — bez tokenów (tętno, dysk, zgaś/obudź, build na zlecenie,
  podtrzymanie w czasie biegu) = `systemd`/cron na VM z wynikiem jako rekord w repo; kroki z modelem = zadanie zaplanowane w chmurze.
  Keepalive to rutyna VM (timer), nie ustawienie instancji: „30 min” jest jego ustawieniem (jego pieniądze), my dokładamy tylko sygnał
  aktywności w czasie biegu. CO liczy się jako aktywność (ssh? CPU? proces?) — fakt do zmierzenia w rekonesansie (`VM_START_PROMPT`
  zad. 1.2). Mierzy: zapis tętna + lista zgaśnięć pod pracą (E5).
- **K6 (baza „przed”):** ledger nie ma rozbicia na tury; `usage_report.py` liczy per agent, nie per krok. Zmierzone: 217 ruchów × 377 tys.
  (21.10 06:08) i ≈ 20 tur × 0,28 mln na pakiet (R89 §4.2) — dwa punkty, nie baza. Pomiar do P1/P3: tury koordynatora oznaczone klasą
  kroku (mechaniczny / osąd) w zapisie biegu — wtedy K6 ma liczbę.
- **K7 (commit per bieg):** 6.2 p.6 — 0 CI, 0 tokenów (fakt 23.10). Commit lokalny natychmiast (bieg niezacommitowany ginie z maszyną —
  restarty kontenera, nasze-17), push wg presetu.
- **W1 (koszt wrzutki):** dwa składniki — zapis cytatu 0 tokenów (zasada „ścieżki zapisu bez tokenów”), warstwa odczytu w biegu sekretarza,
  który i tak biegnie (koszt marginalny ≈ długość wrzutki × model). Eskalacja po cechach (R88: „żonglerka modelami w zależności od stopnia
  złożonościi nietypowości mojej korekty”) z progami jako presety i paragonem każdej eskalacji; P1 mierzy pusty vs z wrzutką. Preset do
  pomiaru: Haiku czyta, Sonnet skleja serię, Fable tylko gdy sekretarz zgłasza sprzeczność z filozofią.
- **W2 (jego czas jako waluta):** 6.2 p.1 i p.11 — liczymy to, co wysyłamy; z jego zachowania tylko znacznik ostatniej wizyty (preset).
- **W3 (kanał nocny):** strona-artefakt — 0 tokenów, działa 23.10; czat nocą kosztuje turę koordynatora i jego czytanie. Budzenie
  koordynatora: `watch_url` istnieje w sesji chmurowej (fakt 23.10, narzędzia sesji); czy strona potrafi wysłać POST — do sprawdzenia.
  Preset routingu: webhook → sekretarz (tani) → koordynator tylko dla klas pieniądze / jego dane / ×10 / drogi odwrót. Obudzenie Fable
  = tura, więc jest pozycją na paragonie klasy.
- **W4 (dwie sesje, VM 30 min, „stop” do obu):** mapa miejsc E5 jako jeden wiersz W1; „stop” adresuje miejsca; kanał do VM dziś = repo
  (flaga czytana przez tętno), bo `send_message` do sesji VM odmówiony bez jego zgody (23.10); podtrzymanie wg 6.2 p.4.
- **W5 (digest statycznie czy na żywo):** dwie warstwy, obie bez modelu — render statyczny z repo (0 tokenów, zawsze; opóźnienie = kadencja
  sekretarza/commit) + dopisanie na żywo z bazy strony, gdy dostępna (opóźnienie 0); gdy baza niedostępna, warstwa statyczna z „stan z
  HH:MM”. Nigdy licznik liczony przez model.

### 6.4 Konflikty do jednej decyzji

| # | Konflikt | Strony | Propozycja ekonomii (preset, odwracalna) |
|---|---|---|---|
| 1 | **Dom zdarzeń**: repo pierwotne + lustro (K 6.3 #1) vs baza strony od razu + repo zbiorczo (W 6.3 #4) | K7 / W1, W9 / E4 | **Kierunek decyduje o magazynie pierwotnym**: jego gesty → baza strony (telefon dociera tam za 0); nasze biegi i próbki → repo (każde miejsce pisze plik za 0; git = historia; teleport przenosi; 0 CI na gałęzi dokumentów). Sekretarz lustruje w obie strony w swojej kadencji; każdy licznik niesie „stan z HH:MM” i źródło. Odwrót: jedno miejsce, gdy VM dostanie dostęp do bazy strony (fakt do zmierzenia). |
| 2 | **Świeżość `if_silent` i liczników vs koszt czuwania**: W „na czas”, K/sekretarz co godzinę, każde odpalenie płaci prefiks | W3 / K6 / E | Terminy znane z góry → `if_silent` wykonuje zadanie jednorazowe zaplanowane na `ask_after` (`run_once_at`); kadencja sekretarza zostaje presetem dla reszty; odpalenia/dobę i tokeny pustego biegu to licznik na stronie. Odwrót: sama kadencja. |
| 3 | **Podtrzymanie VM**: po cofnięciu mojego presetu zostaje: CO jest „biegiem” (agent, bramka, build — a sekretarz? samo tętno?) i próg | E5 / K 6.3 #3 | Bieg = rekord `routine_run` w stanie `running` z `where: vm`; samo tętno ani sekretarz nie są biegiem (inaczej VM nigdy nie gaśnie — podtrzymanie na bezczynności tylnymi drzwiami). Baza godzin/dobę z rekonesansu; prognoza > ×10 bazy → jego pytanie (jedyny próg z CLAUDE.md), inaczej advisory. Odwrót: wyłączyć timer. |
| 4 | **Model w rutynie**: K „rola w rutynie, tabela osobno” vs E3 „licznik PRZED startem musi znać model” | K 6.3 #4 / E3 | Przyjmuję K z warunkiem do zapisania w syntezie: `start_gate` rozwiązuje rola→model→miejsce PRZED startem i zapisuje wynik w biegu, briefie i paragonie; `brief.render` bez rozwiązanego modelu = advisory z modelem dziedziczonym (6.2 p.3). Bez tego cennik ról nie ma z czego się kalibrować. |

### 6.5 Teza po dyskusji

Dwie waluty zostają — punkty konta i jego czas — ale ekonomia nie jest osobnym panelem ani osobnym planem: jest **polem `receipt[]` w
zapisie biegu (K7) i wierszami liczników na jego ekranie (W1)**. Trzy zasady do syntezy: (1) **ścieżki zapisu są bez tokenów** — jego
gest → spec, bieg → rekord, próbka → ledger; model dokłada tylko warstwę odczytu, w swoim czasie, z paragonem; (2) **licznik przed
startem** (`start_gate` w operatorze, nie w turze Fable) rozwiązuje rolę→model→miejsce i zapisuje, co faktycznie biegło; przekroczenie to
advisory, a jedyny próg potwierdzany przez niego to ×10 — także dla godzin VM; (3) **miejsce decyduje o walucie, kierunek o magazynie**:
praca bez tokenów idzie tam, gdzie jej nie liczy w tokenach (VM/CI; godziny na paragonie biegu; podtrzymanie na czas biegu, nie na
bezczynności), jego gesty lądują tam, gdzie dociera telefon, nasze zdarzenia tam, gdzie każde miejsce pisze za darmo, a sekretarz
lustruje. Jego czas liczymy po swojej stronie (słowa wysłane), nigdy jako filtr; koszt pokazuje się przy geście i jako suma dnia, paragon
jest o dotknięcie dalej. `attention_plan.json` znika na rzecz waluty + wiersza routingu; „nie podtrzymywać VM” znika na rzecz podtrzymania
biegu z paragonem. Każda liczba wyżej to pomiar z datą albo preset z `_history`.
