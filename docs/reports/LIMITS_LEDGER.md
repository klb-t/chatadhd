# Dziennik limitów — telemetria okna (od 2026-10-21)

Po co: GPT i właściciel ustalili, że limity mierzymy, a nie szacujemy z liczby tokenów
(zasada 4 z `docs/architecture/SLOWA_WLASCICIELA_2026-10-16.md`, Uzupełnienie 5).
Dla Claude'a odpowiednikiem „codex app-server” jest zdarzenie `rate_limit_event` sesji.

**Skąd liczby:** `list_events(session_id, kinds=["rate_limit_event"], limit=100)` w sesji S1.
Zdarzenie niesie `five_hour.utilization` i `seven_day.utilization` (0–1, krok 0,01),
`resetsAt` (sekundy epoki UTC) oraz `status` (`allowed` = limit nie zablokował).
To liczby od dostawcy, nie moje przeliczenie. **Ograniczenia:** (1) zużycie dotyczy
całego konta — S1, S2, agenci workflow i czaty właściciela razem, więc nie rozdzielę
kosztu na agenta, tylko tempo sumy; (2) rozdzielczość 1 punkt procentowy; (3) odczyt jest
dostępny dla sesji (narzędzie MCP), nie dla skryptów workflow — próbki dopisuję ręcznie
przy każdym powiadomieniu i przy każdej kontroli.

**Okno 5-godzinne:** reset 2026-10-21 04:50 UTC (06:50 w Polsce), początek 23:50 UTC.
**Tydzień:** reset 2026-10-27 08:00 UTC.

**Linia tempa** (preset, nie prawo): zużycie rośnie równo do 100 % w chwili resetu,
`u*(t) = (t − początek) / 5 h`. Powyżej linii — nie startuję nowych agentów, kończące się
dojeżdżają do checkpointu. Poniżej linii o więcej niż ~5 punktów — jeśli czeka gotowy
pakiet, startuję kolejnego. Dwie formy marnotrawstwa: przepalić okno i stać (praca w toku
bez commita przepada, kontekst trzeba cache'ować od nowa), albo zostawić je niewykorzystane.

| czas (UTC) | 5 h | 7 dni | agenci w toku | uwagi |
|---|---|---|---|---|
| 00:05 | 5 % | 1 % | 2 (W0 jednym długim agentem, poprawka logowania) + S1 | linia tempa: 4 % |
| 00:06 | 6 % | 2 % | 2 + S1 | |
| 00:10 | 8 % | 2 % | 3 (+ P39 weryfikacja) + S1 | linia: 7 % |
| 00:13 | 10 % | 3 % | 3 + S1 | |
| 00:14 | 11 % | 3 % | 3 + S1 | linia: 8 %; kontekst agentów: W0 276 tys., P39 226 tys., logowanie 107 tys. tokenów |
| 00:30 | 16 % | 5 % | 3 (W0 w pakietach od 00:18, poprawka logowania, P39 projekt) + S1 + naprawa X-Port | linia: 13 %; po zatrzymaniu długiego agenta W0 tempo 00:14→00:30: +5 pkt w 15,5 min ≈ 0,32 pkt/min, czyli dokładnie tyle, ile trzeba do końca okna |
| 01:28 | 36 % | 10 % | 3 (W0 pakiety P1–P2 gotowe, P3 w toku; poprawka logowania; P39 w sędziowaniu) + S1 | linia: 33 %; 00:30→01:28: +20 pkt w 58 min ≈ 0,34 pkt/min; tydzień +5 pkt (stosunek 5 h : 7 dni ≈ 4 : 1) |
| 02:00 | 48 % | 13 % | 3 (W0 P1–P4 gotowe, P5 w toku; poprawka logowania; P39 synteza) + S1 | linia: 43,7 %; 01:28→02:00: +12 pkt w 33 min ≈ 0,37 pkt/min (lekko nad linią); tydzień 13 % przy linii 10,7 % |
| 02:18 | 50 % | 13 % | 2 (W0 P5 w toku; poprawka logowania) + paczki X-Port (2 po kolei, zakończone) + S1 | linia: 49,3 %; 02:00→02:18: +2 pkt w 18 min ≈ 0,11 pkt/min — poniżej potrzebnych 0,33; wolne miejsce w oknie, brak gotowej paczki bez kompilacji jądra (dysk 7 GB, drugi build jądra się nie mieści) |
| 03:07 | 54 % | 14–15 % | 1–2 (W0 P7 w toku; poprawka logowania w przeglądzie) + S1 | linia 5 h: 65,7 % (jesteśmy 12 pkt pod); 02:18→03:07: +4 pkt w 49 min ≈ 0,08 pkt/min; tydzień: linia 11,4 %, jesteśmy 3–4 pkt nad |
| 06:08 | 12 % (nowe okno 04:50–09:50; linia 26 %) | **22 %** | 1 (W0 krok naprawczy r1:b3) + S1; wcześniej 2–3 | tydzień: linia 13,2 % (22 h z 168 h), jesteśmy 9 pkt NAD; 03:07→06:08: +7 pkt w 3 h ≈ 2,3 pkt/h ≈ 56 %/dobę (linia: 14,3 %/dobę); średnio od resetu tygodnia: 24 %/dobę |
| 11:06 | 0 % (nowe okno) | **29 %** | 0–1 (przerwa: bramka, restarty kontenera) | tydzień: linia 16,1 % (27 h), jesteśmy 13 pkt nad; 06:08→11:06: +7 pkt w 5 h ≈ 34 %/dobę (spadek z 56 %/dobę, bo większość czasu to była przerwa i bramka bez agentów) |
| 13:19 | 4 % (okno 10:10–15:10; linia 67 %) | **30 %** | 0 (bramka skończona, czekam na CI) + S1 | tydzień: linia 17,6 % (29,5 h), jesteśmy 12 pkt nad; 11:06→13:19: +1 pkt w 2 h, bo prawie nic nie biegło (bieg CI i naprawy testów to praca poza kontem) |
| 16:20 | 0 % (okno 15:30–20:30) | **30 %** | 0 (czekanie na CI 256, zielone) + S1 | tydzień: linia 19,2 % (32,3 h), jesteśmy ≈ 11 pkt nad (pierwotnie wpisałem 20,6 % — błąd rachunku, poprawione); 13:19→16:20: 0 pkt w 3 h (nic nie biegło poza CI) |

## DECYZJA WŁAŚCICIELA (2026-10-21 ≈ 17:50 UTC): plan tempa tygodnia zamiast A/B
Słowa: „ostatnio jak koło 95 % zacząłeś kończyć, to i tak wyjechałeś wszystko z agentami, które już chodziły; zaplanuj agentów na słabych
modelach z krótkimi zadaniami; pośrodku tygodnia celuj w 50 %, doba przed końcem w 90 %, żeby został zapas na drogę hamowania agentów i
czaty; ostatnia doba z namysłem te 10 %, żeby nic nie zostało ani nie zabrakło”.
Zapisane jako dane: `tools/pace_plan.json` (punkty: połowa tygodnia pt 23.10 20:00 UTC → 50 %; pn 26.10 08:00 UTC → 90 %; reset wt 27.10
08:00 UTC → 100 %), liczone przez `python3 tools/pace.py --used <seven_day %> [--inflight N] [--model haiku]`. Dwie reguły startu agenta:
(1) punkt kontrolny — `used + (w_toku + 1) × koszt ≤ cel następnego punktu − margines` (droga hamowania agentów już chodzących liczona
z góry, nie po fakcie); (2) tempo — `(w_toku + 1) × koszt ≤ potrzebne_tempo × 2 h`, żeby nie zużyć puli jednym zrywem.
Krótko: nie „przerywam przy 95 %”, tylko nie startuję, jeśli to, co już chodzi, plus nowy, przekroczyłoby punkt kontrolny.
**Koszt pakietu (preset 0,5 pkt tygodnia, GÓRNA granica):** pakiet R38 (agent 187 tys. tokenów, 66 wywołań, 29 min + bramka lokalna + moje
tury) przesunął okno 5 h o ≤ 1 pkt (0 → 1 %), a tydzień się nie zmienił w zaokrągleniu (30 %); 1 pkt okna 5 h ≈ 0,25 pkt tygodnia, więc
≤ 0,5 pkt (rozdzielczość 1 pkt!). **Do kalibracji:** przy każdym pakiecie zapisuję pomiar przed/po w tabeli niżej, osobno dla modeli.
**Próbka 21:34:** tydzień 32 % (+2 pkt w 4 h = 12 pkt/dobę przy planie ≈ 9,3); dodano regułę odstępu do `pace.py` (jeden pakiet na koszt/potrzebne_tempo godzin od startu poprzedniego, stan w `tools/pace_state.json`). Stan 17:33: zużyte 30 %, cel na teraz 20,0 % (+10 pkt), do punktu „połowa tygodnia” 50,5 h, potrzebne tempo +0,40 pkt/h ≈ +9,5 pkt/dobę.
**Modele:** pakiety mechaniczne (poprawki testów, dokumenty, etykiety, drobiazgi UX z kontrolą w CI) na `haiku`; praca w jądrze C++ i
logika na domyślnym modelu sesji; `fable` tylko etapy projektowe (P37-podobne). Kryterium wyboru to koszt **przyjętego** wyniku (pakiet +
poprawki po nieudanym podejściu), nie cena tokena — dlatego pomiary w tabeli.

| pakiet | model | okno 5 h przed→po | tydzień przed→po | przyjęty z 1. podejścia? | uwagi |
|---|---|---|---|---|---|
| R38 | domyślny (Sonnet) | 0→1 % | 30→30 % | tak | agent + bramka + moje tury razem |
| M1-D2 | domyślny (Sonnet) | 1→3 % | 30→30 % | tak (1 podejście) | agent 308 tys. tokenów, 111 wywołań (plan ~50), 40 min; w oknie także moje analizy zużycia (lokalne skrypty, bez tokenów) |
| K1a (2 agenci: przerwany restartem + kontynuacja) | domyślny (Sonnet) | — | 31→31 % | kontynuacja tak (85 wywołań, 209 tys. tokenów ≈ 12,5 mln rów.) | pierwszy agent stracony przy restarcie kontenera (jego commity i WIP uratowane) |
| M1-K1b | domyślny (Sonnet) | 2→2 % | 31→32 % | tak (167 wywołań — plan ~50; 397 tys. tokenów ≈ 24 mln rów.) | największy pakiet dnia; przekroczył plan ponad 3× |
| M1-W1a, W1b, DOC1, W1c (4 agenci jeden po drugim) | domyślny (Sonnet) | — | 32→34 % (00:15→05:26) | W1a, W1b, DOC1, W1c tak (W1b: 156 wywołań, 371 tys. tokenów) | razem z moimi turami, 3 poprawkami CI (klang `.inc`, asan `server.keepalive`, rejestr) i dwiema bramkami lokalnymi; ≈ +2 pkt na 4 pakiety ≈ 0,5 pkt/pakiet |
| M1-K1c1 (jedna liczba przed i po wysłaniu) | Sonnet (jawnie) | — | 34→? % (05:52→) | tak (1 podejście, bez poprawek po przeglądzie; 129 wywołań, 276 tys. tokenów, 32 min) | pierwszy pakiet z `model: sonnet` ustawionym jawnie; plan ~50 wywołań przekroczony ≈ 2,5× |
| M1-K1c2 (perspektywa sent_to_model w jądrze, część A + B) | Sonnet (jawnie) | 0→? % (nowe okno 07:08) | 34→34 % (07:09→07:48) | tak (A i B; 2 agenci — pierwszy zatrzymał się po A przy budżecie, drugi zrobił B; 154 wywołania, 420 tys. tokenów, 41 min) | plan ~50 wywołań przekroczony ≈ 3× |
| M1-K1c3a (nawigator + karta dla sent_to_model, jądro) | Sonnet (jawnie) | — | 34→34 % (08:38→08:59) | tak (75 wywołań, 207 tys. tokenów, 21 min) | mój przewidywany czerwony krok e2e nie wystąpił — stary krok nie odróżniał przyczyn |
| M1-K1c3b (web: Pokaż w grafie + e2e + raport) | Sonnet (jawnie) | — | 34→? % (09:46→10:22) | tak (120 wywołań, 304 tys. tokenów, 36 min) | znalazł prawdziwą przyczynę (zły adres `msg:m_`); bez zmian w jądrze |

## Na co poszły tokeny S1 dziś (2026-10-21 od 00:00 UTC do 17:50; `tools/usage_report.py`, liczby z pól `usage`, bez treści)
Tydzień: 3 % (00:14) → 30 % (17:40) = +27 pkt w ≈ 17,5 h na koncie (S1 + S2 + czaty właściciela). Zmierzone dla S1: **566 mln równoważników**
(wagi założone: odczyt cache 1, wejście 10, zapis 5 min 12,5, zapis 1 h 20, wyjście 50): agenci 538 mln (**95 %**, 81 agentów, 2270 ruchów), sesja
główna 28 mln od 13:00 (5 %). **Czego nie ma:** sesja główna sprzed 13:00 — zapis został przepisany przy kompresji kontekstu (pomiar z 06:08:
15 % zużycia S1); udział S2 i czatów — niewidoczny z tej sesji (S2 poproszony o ten sam raport). Gdyby S1 było całym zużyciem: ≈ 21 mln
równoważników na 1 pkt tygodnia (górna granica kosztu; prawdziwy jest niższy).
Rodzaj pracy (agenci): wdrożenie 35,4 %, poprawki po przeglądach i diagnoza 29,1 %, przeglądy 24,5 %, projekt P39 11,0 %.
Temat (% wydatku agentów): M1-W0 54 % (wdrożenie 21,4 + poprawki 19,8 + przeglądy 13,2), poprawka logowania 21 % (diagnoza+poprawka 9,4 + przegląd 11,3),
P39 11 %, pakiety pojedyncze (R38 itd.) 6,4 %, X-Port 3,7 %, zatrzymany długi agent ekranów W0 2,7 %, D2 1,1 % (w toku).
Model (w tokenach, nie w cenie): Sonnet 57,5 %, **Fable 42,5 %** — w tym wszystkie 13 pakietów poprawek po przeglądach W0 (≈ 106 mln,
19,6 % wydatku agentów) i 4 z 7 pakietów wdrożeniowych; cena tokena Fable jest wyższa (mnożnik nieznany), więc udział w limicie jest większy niż 42,5 %.
**Narzut, który nie jest pracą: 24 % wydatku agentów** — start agenta (zapis prefiksu ≈ 39 tys. tokenów × 81) 7,3 %, oraz ≈ 39 ruchów po
wygaśnięciu cache (kontekst ≈ 190 tys. tokenów przepisany od nowa po pauzie > 5 min, zwykle czekanie na build/ctest/e2e wewnątrz agenta) 17,1 %
(heurystyka: zapis ≥ 50 % kontekstu i > 30 tys.). Odczyt z cache stanowił 57 % równoważników, zapisy 39 %, wyjście 4 %.
**Udział S2 (jego `usage_report`, 17:49):** 48 mln równoważników (główna sesja Opus 5.5: 27,6 mln, 100 ruchów, śr. kontekst 182 tys.; 14 agentów Haiku:
20,5 mln, ≈ 1,5 mln na agenta, 5 ruchów po ≈ 54 tys. — każdy czyta cały log CI, więc to świeży zapis cache). Jego zapisy sprzed kompresji też są niepełne.
Razem S1 + S2 ≈ 614 mln na +27 pkt konta ⇒ **≈ 22 mln równoważników na 1 pkt tygodnia** (górna granica); S2 to ≈ 8 % tego, co widać. Koszt pakietu w `pace_plan.json`
podniesiony z 0,5 do 0,75 (agent 0,5 + koordynacja 0,25). Pomysły S2 na obniżenie kosztu, przyjęte: jeden agent na bieg zamiast na pytanie, krótsze ogony logów
(`tail_lines` 60, tylko blok REPORT), mniej tur głównej sesji na jedno zdarzenie.
Wnioski wprowadzone: reguła „żadne wywołanie narzędzia dłuższe niż ~4 min” (`single-package.js`, `CLAUDE.md` pkt 5), poprawki po przeglądach i
pakiety mechaniczne na tańszych modelach, jeden proporcjonalny przegląd poprawki błędu.

**Odczyt z 00:14:** od 00:10 do 00:14 przybyły 3 punkty w około 4 minuty (≈ 0,8 pkt/min),
od 00:05 średnio ≈ 0,6 pkt/min. Do reszty okna (276 min) zostaje 89 punktów, czyli stać nas
na ≈ 0,32 pkt/min. Przy tempie bez zmian okno skończyłoby się około 02:10–02:40 UTC, 2–2,5 h
przed resetem. Reakcja: zatrzymany długi agent W0 (276 tys. kontekstu, zero commitów po
13 minutach; jego robota zapisana jako commit WIP `4198dd88`), W0 puszczone od nowa jako
siedem małych pakietów. **Do sprawdzenia za 20–30 minut:** czy tempo spadło do ≈ 0,3–0,4.

Tydzień: 3 % po ~16 godzinach od resetu; przy rozdzielczości 1 punktu stosunek 5 h : 7 dni
wygląda na ≈ 3–4 : 1 (jedno pełne okno ≈ ćwierć tygodnia), **do potwierdzenia** kolejnymi
próbkami. Jeśli się utrzyma, wiążące na dłuższą metę jest okno tygodniowe, a nie 5-godzinne.

**Kalibracja z 01:28:** w ciągu godziny okno 5 h przybyło o 20 punktów, tydzień o 5, więc **jedno pełne
okno 5 h ≈ 25 % tygodnia** (stosunek 4 : 1, rozdzielczość 1 pkt, więc ±). Tydzień jest na linii tempa
(10 % po 17,5 h z 168 h = 10,4 %), okno 5 h też (36 % przy linii 33 %). Przy 0,34 pkt/min okno
wystarcza do ≈ 04:36 UTC, reset 04:50. **Wniosek:** trzy agenci naraz mieszczą się dokładnie w
budżecie; czwarty w tej chwili by go przekroczył, więc kolejna paczka (poprawki UX X-Port Android)
czeka na zakończenie jednego z obecnych.

**Wniosek z 03:07 — wiąże okno tygodniowe, nie 5-godzinne.** Okno 5 h ma 12 punktów luzu, ale tydzień
jest 3–4 punkty NAD swoją linią (15 % po 19 h z 168 h = 11,4 %). Skoro jedno pełne okno 5 h to
≈ 25 % tygodnia, dosypywanie agentów tylko po to, by „wypełnić” okno 5 h, zjadałoby tydzień, który
ma starczyć do 27.10 08:00 UTC. Reguła na teraz: nie startuję pracy spekulatywnej (np. projekt P37),
tylko to, co jest w kolejce i widoczne dla właściciela. Linia tygodniowa to ≈ 14,3 % na dobę.

**Stan z 06:08 — tydzień jest daleko nad linią.** Przy tempie z ostatnich 3 godzin tydzień (100 %) skończyłby się
ok. czw. 22.10 15:30 UTC, przy średnim tempie od resetu ok. 28 godzin później; reset dopiero wt. 27.10 08:00 UTC.
**Skąd zużycie (S1 od 00:00 UTC, zmierzone z zapisów):** sesja główna 217 ruchów, 82 mln odczytów z cache, średnio
377 tys. kontekstu na ruch (teraz 587 tys.); 51 agentów workflow 1613 ruchów, 227 mln odczytów i 14,8 mln zapisów
do cache. W „równoważnikach odczytu” (zapis ≈ 20 × odczyt, wagi z cen API — założenie) sesja główna ≈ 15 %,
agenci ≈ 85 % zużycia S1. Konto jest wspólne z S2 (od 12.10: S1 ≈ 1934 USD wg cen API, S2 ≈ 429 USD, sesja
„Prompt review i optymalizacja” ≈ 397 USD), więc nie rozdzielę udziału w oknie, tylko w tokenach.
**(Zamknięte 17:50: właściciel wybrał plan tempa, patrz niżej.)** Pytanie było: zostawić X % tygodnia na jego czaty i resztę spalić (preset 5 %),
czy trzymać linię tygodniową.
