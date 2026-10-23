# R89 — tor „nasze”: nasze własne pomiary, reguły i możliwości platformy

- **Tor:** nasze (tylko odczyt repozytorium, BEZ sieci — tak brzmiał brief tego toru)
- **Data:** 2026-10-23 (odczyt repo ≈ 10:20–10:40 UTC, gałąź `claude/chataddhd-cpp-loom-core-IRGRN`, HEAD `6258e22d`)
- **Autor:** agent badawczy R89 (model Fable 5.1 — stopka commita nazywa model agenta, zgodnie z `tools/workflows/single-package.js:18`;
  brief toru podawał stopkę „Sonnet 5.5”, ale to nie jest model, na którym ten agent biegł; dwa tory siostrzane podpisały się wg briefu,
  jeden wg prawdziwego modelu — do ujednolicenia w dzienniku kosztów)
- **Zasady:** R58/R83/R84 (przykłady to przykłady, liczba na liście nie jest listą), R59 (każde ustalenie ma źródło, datę, typ,
  wiarygodność), R60 (fakt o teraźniejszości to datowana obserwacja). Każda liczba poniżej jest przeczytana z pliku pod podanym
  `plik:linia`; przy każdym ustaleniu stoi, czy to **zmierzone** (liczby od dostawcy albo z zapisów), czy **założone** (model, wagi,
  heurystyka).

## Jak szukałem

- Źródła wskazane w briefie, czytane fragmentami (`sed -n`, `grep -n`), żaden plik w całości poza `LIMITS_LEDGER.md` (145 linii),
  `tools/usage_report.py` (145), `tools/pace.py` (85), `single-package.js` (41), `docs/spec/README.md` (139), `TELEPORT_S1.md` (62):
  `docs/reports/LIMITS_LEDGER.md`, `docs/reports/INDEX.md` (grep: waste/marnotraw, measured/zmierz/pomiar, cache, usage_report,
  tokens/tokenów, mln rów), `docs/reports/QUEUE_S1.md` (grep: mln, pkt, tokenów, wywoła, restart, koszt),
  `docs/reports/EKONOMIA_TOKENOW_2026-10-16.md`, `tools/pace_plan.json`, `tools/pace_state.json`, `tools/workflows/README.md`,
  `tools/workflows/briefs/README.md`, `tools/workflows/research-synthesis.js`, `tools/spec_html.py` (grep), `docs/ops/TELEPORT_S1.md`,
  `docs/ops/ZADANIA_ZAPLANOWANE_2026-10-23.md`, `loom/tools/eval/pricing_2026-09-25.json`, `CLAUDE.md` (sekcje „Working with
  subagents” i „Waste control” — numery linii z grep), słowa właściciela `python3 -I tools/spec.py show R61 R62 R63 R73 R88 R89`.
- Nie czytałem: `docs/exports`, `eval/real-holdout-key`, żadnego worktree w `/home/user/wt/*`, żadnego transkryptu JSONL (brief: tylko
  repo). Nie uruchamiałem `usage_report.py` ani `pace.py` (wymagają katalogu transkryptów / aktualnego procentu — nie mam ich),
  jedynie `git log` dla liczby commitów.
- Typ źródła: wszystkie poniżej to `our_own_measurement` (nasze pomiary zapisane w repo), z wyjątkiem ustaleń o możliwościach
  platformy (`other`, oznaczone: co z tego sprawdzono w użyciu, a co jest tylko zapisanym zamiarem) i słów właściciela (`other`).
- Wiarygodność: `high` = liczba od dostawcy (`rate_limit_event`, API sesji) albo policzona z zapisów i opisana metodą;
  `medium` = liczba z zapisów, ale przez ZAŁOŻONE wagi (`usage_report.py`) albo z rozdzielczością 1 pkt; `low` = model/hipoteza.
- Skala: **kaliber** = small / medium / large / scientific / experimental (rozmiar rozwiązania); **wysiłek** = small / medium / large (dla nas).

---

## nasze-01 — Koszt agenta rośnie z kwadratem jego długości; 98 % odczytów workflow siedziało w pięciu długich agentach

- **Źródło:** `docs/reports/INDEX.md:1707-1714` (sekcja „S1 — kontrola marnotrawstwa”, 2026-10-21); reguła w `CLAUDE.md:187-189`.
- **Typ / wiarygodność:** our_own_measurement / **high** — liczby z zapisów agentów workflow K0 (pola `usage`), metoda opisana.
- **Twierdzenie (zmierzone):** agent K0 `impl` miał 501 ruchów i średnio 365 tys. tokenów kontekstu (183 mln odczytów z cache);
  `fix:r1` 549 ruchów i 214 mln; każdy z trzech recenzentów 28–38 mln; 30 agentów „refute” (3–18 ruchów) razem ≈ 9 mln.
  **98 % odczytów całego workflow siedziało w pięciu długich agentach, nie w liczbie agentów.** Kontekst rósł liniowo 1,2–1,5 tys.
  tokenów na ruch (agent W0 w pierwszym podejściu ≈ 4 tys./ruch, 276 tys. po 54 ruchach i zero commitów).
- **Mechanizm:** każdy ruch czyta cały dotychczasowy kontekst, więc suma odczytów ∝ (liczba ruchów)²; dźwignią kosztu jest
  DŁUGOŚĆ agenta, nie ich liczba. Stąd pakiety ~50 wywołań + commit + karta wznowienia; następny agent czyta kartę i `git log`, nie
  cudzy kontekst.
- **Dotyczy:** workflow agents, coordinator session (sam koordynator też rośnie: 377 tys. → 587 tys. kontekstu na ruch, nasze-04).
- **Kaliber:** medium. **Wysiłek:** small (już wdrożone jako preset „~50 wywołań”).
- **Uwagi:** model restartów (`INDEX.md:1715-1720`) — przy 319 ruchach restart co N z kartą 25 tys. daje spadek równoważników
  odczytu o 35 % (N = 150), 45 % (100), ok. 50 % (25–75), optimum blisko 50 — to **MODEL, nie pomiar** (założenia: wzrost kontekstu
  jak zmierzony, zapis 1 h = 20 × odczyt, restart nie traci jakości). Na pełnym przebiegu pakietowym niepotwierdzony liczbowo.

## nasze-02 — 24 % wydatku agentów to nie praca: 7,3 % prefiks startu, 17,1 % konteksty przepisane po pauzie > 5 min

- **Źródło:** `docs/reports/LIMITS_LEDGER.md:90-92` (S1, 2026-10-21 00:00–17:50 UTC); reguła w `CLAUDE.md:197-199`.
- **Typ / wiarygodność:** our_own_measurement / **medium** — zapisy agentów (81 agentów), ale w „równoważnikach” z ZAŁOŻONYMI wagami
  i z heurystyką rozpoznania wygaśnięcia cache (zapis ≥ 50 % kontekstu i > 30 tys. tokenów).
- **Twierdzenie (zmierzone z założeniami):** start agenta = zapis prefiksu ≈ 39 tys. tokenów × 81 agentów = 7,3 % wydatku agentów;
  ≈ 39 ruchów po wygaśnięciu cache (kontekst ≈ 190 tys. przepisany od nowa po pauzie > 5 min, zwykle czekanie na build/ctest/e2e
  wewnątrz agenta) = 17,1 %. Razem **24 %**. Wcześniejszy pomiar prefiksu (K0, `INDEX.md:1708-1709`): ≈ 60 tys. tokenów (≈ 40,5 tys.
  z cache + ≈ 19,5 tys. nowych) — dwa pomiary, dwie liczby, różne populacje agentów.
- **Mechanizm:** cache promptu wygasa po 5 min bez ruchu → następny ruch płaci zapis całego kontekstu (waga 12,5–20× odczytu).
  Reguła wprowadzona: **żadne wywołanie narzędzia dłuższe niż ~4 min** (build/test/CI w tle + krótkie odpytywanie), żeby kolejny ruch
  modelu nastąpił w ciągu 5 minut (`single-package.js:23`, `CLAUDE.md:199`).
- **Dotyczy:** workflow agents, coordinator session (koordynator czekający na bramkę 50 min też przepisuje kontekst), the collaboration
  interface (sekretarz i tanie sesje: krótkie, bez długich pauz).
- **Kaliber:** small. **Wysiłek:** small (reguła w briefie); pomiar skutku — do zrobienia.
- **Uwagi:** udział po wprowadzeniu reguły NIE został jeszcze zmierzony ponownie (brak wpisu w ledgerze po 21.10 z tym rozbiciem).
  Waga zapisu 1 h vs 5 min (20 vs 12,5) to stosunek z cennika API, nie wiedza o liczeniu limitu abonamentu (`usage_report.py:9-11`).

## nasze-03 — Odczyt z cache to 57 % równoważników S1 (zapisy 39 %, wyjście 4 %) i 95–97 % WOLUMENU tokenów każdej sesji

- **Źródło:** `docs/reports/LIMITS_LEDGER.md:92` (21.10, równoważniki); `docs/reports/EKONOMIA_TOKENOW_2026-10-16.md:22` (API sesji,
  stan 2026-10-16 16:25 UTC, liczby surowe: S1 3 298 114 597 cache read vs 12 668 627 wyjścia, tabela w liniach 9-19).
- **Typ / wiarygodność:** our_own_measurement / **high** dla wolumenu (liczniki API sesji `list_sessions.usage`), **medium** dla
  udziału w równoważnikach (wagi założone).
- **Twierdzenie (zmierzone):** w wolumenie tokenów cache read = 95–97 % w każdej sesji roboczej; w równoważnikach kosztu
  (21.10, S1) odczyt 57 %, zapisy 39 %, wyjście 4 %. Każdy obrót pętli głównej S1 czytał ~350 tys. tokenów kontekstu z cache
  (`EKONOMIA:22`), 21.10 średnio 377 tys., chwilowo 587 tys. (`LIMITS_LEDGER.md:127-128`).
- **Mechanizm:** to strumień PONOWNEGO CZYTANIA kontekstu wyczerpuje okna limitów, nie tekst wyjściowy modelu; oszczędzanie tokenów
  wyjścia (krótsze odpowiedzi) jest u nas drugorzędne wobec rozmiaru i trwałości kontekstu.
- **Dotyczy:** coordinator session, workflow agents, the collaboration interface (każda „tania rozmowa” też niesie kontekst — liczy
  się, ile go ma).
- **Kaliber:** medium. **Wysiłek:** — (diagnoza, nie rozwiązanie).
- **Uwagi:** tokeny wyjścia kosztują właściciela CZAS (R62, nasze-29) — to osobna waluta, nie limit.

## nasze-04 — Agenci = 95 % zużycia S1 (538 z 566 mln równoważników), sesja główna 5 %; wcześniej 85/15 i 70/30

- **Źródło:** `docs/reports/LIMITS_LEDGER.md:80-82` (21.10 do 17:50: 81 agentów, 2270 ruchów), `:127-130` (21.10 do 06:08: 217 ruchów
  głównej sesji, 82 mln odczytów; 51 agentów, 1613 ruchów, 227 mln odczytów, 14,8 mln zapisów → ≈ 15 % / 85 %);
  `EKONOMIA_TOKENOW_2026-10-16.md:24` (12–16.10: pętla główna ~30 %), `:39` (17.10: 15,4 %).
- **Typ / wiarygodność:** our_own_measurement / **high** (liczniki z zapisów; proporcje stabilne w trzech pomiarach).
- **Twierdzenie (zmierzone):** udział pętli głównej koordynatora spada z ~30 % (12–16.10) przez 15,4 % (17.10) do 5 % (21.10 po 13:00),
  reszta to agenci i workflow. Uwaga: zapis sesji głównej sprzed kompresji kontekstu jest tracony (`LIMITS_LEDGER.md:82-83`), więc 5 %
  to dolna granica udziału koordynatora.
- **Mechanizm:** optymalizacja tempa = optymalizacja agentów (kontekst, długość, liczba równoległych od zera), nie oszczędzanie na
  rozmowie koordynatora z właścicielem.
- **Dotyczy:** workflow agents, coordinator session.
- **Kaliber:** medium. **Wysiłek:** —.
- **Uwagi:** w pieniądzach wg cennika API od 12.10: S1 ≈ 1934 USD, S2 ≈ 429 USD, sesja „Prompt review” ≈ 397 USD
  (`LIMITS_LEDGER.md:130-131`, stan 21.10 06:08) — cennik API ≠ limit abonamentu.

## nasze-05 — Kalibracja limitów: jedno pełne okno 5 h ≈ 25 % tygodnia; ≈ 22 mln równoważników na 1 pkt tygodnia (górna granica)

- **Źródło:** `docs/reports/LIMITS_LEDGER.md:33` (01:28: +20 pkt okna / +5 pkt tygodnia w 58 min), `:112-113` (wniosek 4 : 1),
  `:51-52` (R38: 1 pkt okna 5 h ≈ 0,25 pkt tygodnia), `:95` (S1 566 + S2 48 = 614 mln rów. na +27 pkt konta ⇒ ≈ 22 mln/pkt),
  `tools/pace_plan.json` (`cost_per_package._note`).
- **Typ / wiarygodność:** our_own_measurement / **medium** — procenty od dostawcy (`rate_limit_event`), ale rozdzielczość 1 pkt i
  konto wspólne (S1 + S2 + czaty właściciela), więc to GÓRNA granica kosztu na punkt.
- **Twierdzenie (zmierzone):** stosunek 5 h : 7 dni ≈ 4 : 1 (± z zaokrągleń); 1 pkt tygodnia ≈ 22 mln równoważników (wagi z
  `usage_report.py`); linia tygodnia bez planu ≈ 14,3 %/dobę; 21.10 tempo chwilowe sięgało 56 %/dobę (`:37`).
- **Mechanizm:** przelicznik między zmierzonym wydatkiem agenta a punktami limitu — pozwala planować pakiety w punktach, a nie
  „na oko”; rozstrzyga, że na dłuższą metę wiąże okno TYGODNIOWE (`:119-123`), więc „wypełnianie” okna 5 h zjada tydzień.
- **Dotyczy:** coordinator session (planowanie), workflow agents.
- **Kaliber:** small. **Wysiłek:** small (w użyciu).
- **Uwagi:** 23.10 07:50 zdarzenie pokazało też „nadwyżkę” 61 % wyłączoną (`out_of_credits`) i reset tygodnia 2026-10-27 00:00 UTC,
  inny niż wcześniej notowane 08:00 (`:142` vs `:17`) — dwie daty resetu w ledgerze, do wyjaśnienia przy następnej próbce.

## nasze-06 — Koszt pakietu w punktach tygodnia: Sonnet ≈ 0,43–0,5, projekt Fable ≈ 1,2–1,6 (preset 1,5), czytacz logu Haiku ≈ 0,07

- **Źródło:** `tools/pace_plan.json` (`cost_per_package`: default 0,5, fable 1,5, `haiku: null`, `_note` z kalibracją 22.10 04:40:
  +3 pkt na ≈ 7 pakietów ⇒ ≈ 0,43 pkt/pakiet; `_fable_note`: 35,2 i 25,1 mln rów.); `docs/reports/LIMITS_LEDGER.md:140`
  (R57: 93,8 mln ≈ 4,3 pkt, badacze Sonnet 3,8–7,2 mln każdy), `:51-53` (R38 ≤ 0,5 pkt), `:96` (pakiet = agent 0,5 + koordynacja 0,25).
- **Typ / wiarygodność:** our_own_measurement / **medium** (różnica procentów przed/po przy rozdzielczości 1 pkt; wagi Fable
  „z cen katalogowych”: odczyt 1,25, zapis 62,5, wyjście 250 względem odczytu cache Sonnet).
- **Twierdzenie (zmierzone):** pakiet ~50–170 wywołań na Sonnecie ≈ 0,43–0,5 pkt tygodnia (preset 0,5, wcześniej 0,75); etap
  projektowy Fable ≈ 1,2–1,6 pkt (preset 1,5); agent Haiku czytający log (5 ruchów po ≈ 54 tys.) ≈ 1,5 mln ≈ 0,07 pkt. Stosunek
  Fable : Sonnet : Haiku-log ≈ 1,5 : 0,5 : 0,07 (≈ 20× między najtańszym a najdroższym rodzajem pakietu).
- **Mechanizm:** jednostka planowania (`pace.py`), wybór modelu wg KOSZTU PRZYJĘTEGO WYNIKU, nie ceny tokena
  (`LIMITS_LEDGER.md:55-57`).
- **Dotyczy:** coordinator session, workflow agents, the collaboration interface (sekretarz Sonnet/Haiku ma znany rząd kosztu).
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi (luki):** pakiet 50 ruchów na Haiku **niezmierzony** (`"haiku": null`); mnożnik Fable w limicie abonamentu nieznany
  (`:89`); „koszt koordynacji 0,25 pkt” to ≈ 20 tur po ≈ 0,28 mln — oszacowanie z jednego pakietu (R38).

## nasze-07 — Pakiety przekraczają plan ~50 wywołań 2–5×; najtańszy pakiet dnia miał brief z hipotezą i planem odtworzenia

- **Źródło:** `docs/reports/LIMITS_LEDGER.md:62-77` (tabela pakietów 21–22.10), `docs/reports/QUEUE_S1.md:64` (W2b-1/K2c-2:
  264 wywołania, 566 tys. tokenów, 126 min, dwóch agentów — 5× planu), `:19` (K3 badanie: 104 wywołania, 326 tys., 16 min — 2×, bo
  weryfikował numery linii), `:76` (G1a: 173 wywołania — 3,5× planu, pierwszy build + ctest 1043 s), `:91` (126 wywołań ≈ 4× —
  czekał na bramkę).
- **Typ / wiarygodność:** our_own_measurement / **high** (liczby wywołań i tokenów z kart agentów i zapisów).
- **Twierdzenie (zmierzone):** M1-K1b 167 wywołań / 397 tys. tokenów (3×), K1c2 154 / 420 tys. (3×), W2a 182 / 350 tys. / 51 min
  (2,6×, każdy bieg e2e ≈ 2,5 min), W2b-1 264 / 566 tys. (5×). Najkrótszy: **M1-FLAKE1 39 wywołań, 170 tys., 19 min** — „brief
  z hipotezą i planem odtworzenia (test czerwony najpierw)”; K2a 125 wywołań w 1 podejściu dzięki „briefowi z istniejącym stanem
  (profile.cpp, scenario.pack) i listą testów” (`:72`); K2a-fix 40 wywołań / 123 tys. (`:73`).
- **Mechanizm:** jakość briefu (stan istniejący, lista testów, hipoteza) skraca agenta i daje przyjęcie z 1. podejścia (wszystkie
  wiersze tabeli: „tak”); agent, który sam buduje/czeka na e2e, rośnie wielokrotnie (czekanie = ruchy + przepisanie cache, nasze-02).
- **Dotyczy:** workflow agents, coordinator session (pisanie briefów), spec tooling (`spec.py cite --full` wkleja dosłowne słowa
  do briefu).
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** „~50 wywołań” to preset, nie reguła (`INDEX.md:1738-1741`); W2a i W2b-1 zostały po pomiarze oznaczone jako „do rozcięcia
  na dwa” — skutek nie zmierzony.

## nasze-08 — Restarty jednego workflow zmarnowały ≈ 19 mln równoważników ≈ 0,9 pkt (≈ 2 pakiety); `resumeFromRunId` odtwarza tylko niezmieniony prefiks

- **Źródło:** `docs/reports/QUEUE_S1.md:132-134` (pomiar 22.10 23:5x), `CLAUDE.md:203-212` (waste control 6).
- **Typ / wiarygodność:** our_own_measurement / **medium** (zapisy agentów, wagi `usage_report.py`; autor nazywa to górną granicą).
- **Twierdzenie (zmierzone):** bieg 1 (zatrzymany po 4 min) 8,0 mln; bieg 2: badacz eksportów 5,5 mln + projekt replikacji Fable
  1,2 mln; plus 4,4 mln za POWTÓRZONE badanie klonów, bo wznowienie z `resumeFromRunId` odtwarza tylko niezmieniony PREFIKS wywołań
  `agent()`, a zmienione było pierwsze wywołanie. Razem ≈ 19 mln ≈ 0,9 pkt, ≈ 19 min pracy agentów. Wcześniejsze „pół godziny”
  w odpowiedzi dla właściciela było zgadnięte, nie zmierzone (`:133`).
- **Mechanizm:** działającego agenta workflow nie da się zaktualizować (sprawdzone: `ListAgents` — brak osiągalnych agentów,
  `:134`). Reguły: badanie i projekt jako OSOBNE workflow (punkt kontrolny między nimi); reguły właściciela w `CLAUDE.md`, do którego
  brief odsyła zamiast wklejać; korekta = krótki dodatek po zakończeniu agenta; wyniki etapów do plików i jako `args` następnego
  workflow; przed stopem porównać koszt poniesiony z kosztem dodatku.
- **Dotyczy:** coordinator session, workflow agents, the collaboration interface (korekta właściciela w trakcie = dopisek do spec,
  nie restart).
- **Kaliber:** small. **Wysiłek:** small (zasady już w `CLAUDE.md`).
- **Uwagi:** to pomiar jednego zdarzenia (R57); częstość restartów po zmianie zasad nie jest liczona.

## nasze-09 — Zryw równoległy: 5 agentów naraz = 24 % okna 5 h w 11 minut (23.10) i wyczerpane okno w 8 minut (10.10)

- **Źródło:** `docs/reports/LIMITS_LEDGER.md:145` (23.10 10:11: okno 5 h 24 % po 11 min nowego okna; „5 równoległych agentów Sonnet =
  zryw jak 10.10”); `CLAUDE.md:164-168` (10.10: 5 agentów, 8 min, 57,5M cache-read i 1,34M cache-write, stall 4,5 h; re-caching
  0,2–0,46M tokenów na agenta i sesję główną; agent workflow nie da się wznowić po limicie).
- **Typ / wiarygodność:** our_own_measurement / **high** (procent od dostawcy; tokeny z zapisów 10.10).
- **Twierdzenie (zmierzone):** równoległy start N agentów to N × prefiks + N × rosnący kontekst w tej samej minucie; 10.10 pięć agentów
  zużyło okno w 8 min; 23.10 pięć torów R89 wzięło 24 % okna w 11 min (przy tygodniu 57 %, już NAD punktem planu 56 %/20:00).
- **Mechanizm:** równoległość bez stałego limitu, ale sterowana linią tempa i zmierzonym kosztem agenta (`INDEX.md:1744-1749`);
  hipoteza „agenci jednego workflow z tym samym prefiksem czytają go z cache, salwa płaci zapis N razy” — **do zmierzenia**.
- **Dotyczy:** coordinator session (decyzja o starcie), workflow agents (`parallel()` w `research-synthesis.js:61`).
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** zryw 23.10 był świadomy (właściciel prosił o kilku agentów, R89) i wymusił odłożenie projektu K3 Fable do końca torów
  (`:145`) — koszt alternatywny zrywu to opóźnienie innej pracy, nie tylko punkty.

## nasze-10 — Plan tempa jako dane: punkty kontrolne tygodnia + trzy reguły startu (punkt, tempo z 2 h wyprzedzenia, odstęp)

- **Źródło:** `tools/pace.py:7-9` (reguły 1–2), `:58-65` (reguła 3 — odstęp), `:66-80` (werdykty MOŻNA / CZEKAJ / HAMUJ);
  `tools/pace_plan.json` (waypoints 56 % 23.10 20:00, 80 % 25.10 08:00, 92 % 26.10 08:00, 100 % 27.10 08:00; `margin` 1,
  `lookahead_h` 2; `_history`); słowa właściciela `docs/reports/LIMITS_LEDGER.md:43-45` (21.10 ≈ 17:50 UTC); `tools/pace_state.json`
  (32 starty 21–23.10, `_note_2026-10-23`).
- **Typ / wiarygodność:** our_own_measurement (narzędzie + zapis startów) / **high** co do tego, jak liczy; skuteczność — patrz uwagi.
- **Twierdzenie (zmierzone, co robi narzędzie):** `used + (w_toku + 1) × koszt ≤ cel − margines` (droga hamowania liczona z góry),
  `(w_toku + 1) × koszt ≤ potrzebne_tempo × 2 h`, odstęp `koszt / potrzebne_tempo` godzin od poprzedniego startu; `--start` zapisuje
  start do `pace_state.json`. 21–23.10 zapisano 32 starty, średnio co ≈ 1,6 h.
- **Mechanizm:** zamiast „przerywam przy 95 %” — nie startuję, jeśli to, co chodzi, plus nowy, przekroczy punkt; plan jest
  presetem w JSON (zmiana jednym wpisem, poprzednie wartości w `_history`).
- **Dotyczy:** coordinator session; the collaboration interface (ten sam plan mógłby być widoczny właścicielowi jako dane, z
  „podkręć” jako edycją punktu).
- **Kaliber:** small. **Wysiłek:** small (istnieje).
- **Uwagi (zmierzona sztywność):** `pace.py` odmawiał zapisu startu przy werdykcie CZEKAJ, gdy właściciel kazał startować
  („działaj śmiało”, R62) — 4 starty dopisane ręcznie (`pace_state.json:_note_2026-10-23`); 23.10 00:2x plan podniesiono z 50/90/100
  na 56/80/92/100 na jego słowo. Narzędzie nie zna R63 („lepszy model może poluzować”) — to decyzja koordynatora poza skryptem.

## nasze-11 — Telemetria dostawcy istnieje: `rate_limit_event` niesie zużycie 5 h i 7 dni, ale tylko dla sesji, nie dla skryptów, i dla całego konta

- **Źródło:** `docs/reports/LIMITS_LEDGER.md:7-14` (co niesie zdarzenie i trzy ograniczenia), `CLAUDE.md:194-196`;
  zasada 4 właściciela i GPT: `docs/architecture/SLOWA_WLASCICIELA_2026-10-16.md:81` („zamiast szacowania ich wyłącznie na podstawie
  liczby tokenów”).
- **Typ / wiarygodność:** other (możliwość platformy) — **zmierzone w użyciu** 21–23.10 (próbki w ledgerze) / **high**.
- **Twierdzenie (zmierzone):** `list_events(session_id, kinds=["rate_limit_event"], limit=100)` daje `five_hour.utilization` i
  `seven_day.utilization` (0–1, krok 0,01), `resetsAt` (epoka UTC), `status` (`allowed`). Ograniczenia: zużycie całego konta
  (S1 + S2 + agenci + czaty właściciela), rozdzielczość 1 pkt, odczyt z sesji (MCP), nie ze skryptu — próbki dopisywane ręcznie.
- **Mechanizm:** mierzymy, nie szacujemy; próbka przy każdym powiadomieniu → tabela tempa → werdykt startu.
- **Dotyczy:** coordinator session; the collaboration interface (żywy wskaźnik tygodnia dla właściciela zamiast pytania w czacie —
  23.10 00:12 to ON podał „dopiero 46 %”, `LIMITS_LEDGER.md:136`).
- **Kaliber:** small. **Wysiłek:** small (ręcznie) / medium (automatyczny zapis próbek przez zadanie zaplanowane — niezrobione).
- **Uwagi:** na VM po przeprowadzce zamiennikiem jest „procent z aplikacji Claude, wpis ręczny” (`TELEPORT_S1.md:56`).

## nasze-12 — `usage_report.py`: co dokładnie mierzymy i czego nie widzimy

- **Źródło:** `tools/usage_report.py:2-11` (opis), `:16` (wagi: wejście 10, zapis 5 min 12,5, zapis 1 h 20, wyjście 50, odczyt 1),
  `:28-46` (tylko wiadomości `assistant` z polem `usage`, liczone raz po `message.id`, zachowana wersja z największym `output_tokens`),
  `:86-106` (grupowanie: sesja główna `<id>.jsonl`, agenci `<id>/subagents/**/agent-*.jsonl`, workflow po `wf_*.json`),
  `:131-140` (podział wg modelu i godzinami); `docs/reports/LIMITS_LEDGER.md:82-83` (czego nie ma).
- **Typ / wiarygodność:** our_own_measurement / **high** co do tego, co liczy; **medium** co do wag (założone, „RÓWNOWAŻNIKI do porównań,
  nie punkty limitu”).
- **Twierdzenie:** raport liczy tylko pola `usage` (bez treści), rozdziela sesję główną / agentów / workflow / model / godzinę.
  Nie widzi: innych sesji konta (S2, czaty właściciela — uruchamia się osobno u nich), sesji głównej sprzed kompresji kontekstu
  (zapis przepisany: 21.10 pomiar z 06:08 pokazywał 15 % zużycia S1, raport z 17:50 widział główną tylko od 13:00).
- **Mechanizm:** umożliwia przypisanie kosztu do pakietu/modelu/godziny i porównania „kto zużył ile względem innych”; podstawa
  nasze-02, -04, -06, -08.
- **Dotyczy:** coordinator session, workflow agents, spec tooling (koszt na pozycję spec — jeszcze nie liczony).
- **Kaliber:** small. **Wysiłek:** small (istnieje); medium: zapis próbek do pliku przed kompresją, żeby nie tracić głównej sesji.
- **Uwagi:** wagi Fable odrębne (`pace_plan.json:_fable_note`), raport ich nie stosuje (jedna waga na typ tokena, niezależnie od modelu
  — `usage_report.py:64`), więc udział Fable w „równoważnikach” jest niedoszacowany względem limitu (`LIMITS_LEDGER.md:89`).

## nasze-13 — `output_tokens` w transkryptach agentów to DOLNA granica: 15 521 z 17 651 odpowiedzi bez `stop_reason`

- **Źródło:** `docs/reports/INDEX.md:1295` (pokrycie transkryptów względem API sesji: odczyt cache 99,2 %, zapis 99,97 %, wejście 44 %,
  wyjście 16,8 %), `:1318` (17 651 odpowiedzi; 15 521 z licznikiem z początku strumienia = 172 506 tokenów; transkrypt główny 0 z
  1 595; kalibracja `chars/4 (en)` na 869 turach: mediana −56,5 %), `docs/reports/EKONOMIA_TOKENOW_2026-10-16.md:40`.
- **Typ / wiarygodność:** our_own_measurement / **high** (porównanie dwóch źródeł: transkrypt vs API sesji, X-Port `ekonomia --zbiorczo`,
  2026-10-17).
- **Twierdzenie (zmierzone):** w zapisach podagentów i agentów workflow licznik wyjścia jest zapisywany na STARCIE strumienia, więc
  suma wyjścia z transkryptów = 16,8 % wartości z API sesji; wejście 44 %. Odczyt i zapis cache są pełne (99,2 / 99,97 %).
- **Mechanizm:** każde narzędzie ekonomii tokenów (nasze `usage_report.py`, X-Port `token_economics`) musi oznaczać wyjście agentów jako
  „dolna granica” i brać wyjście z API sesji; kalibracja szacunku znaki→tokeny tylko na turach z licznikiem końcowym.
- **Dotyczy:** spec tooling / the app kernel (X-Port `loom/tools/extract`, profil `token_economics`), coordinator session.
- **Kaliber:** small. **Wysiłek:** small (już w X-Port: kwalifikator `output_lower_bound`, `INDEX.md:1307`).
- **Uwagi:** ponieważ wyjście to 4 % równoważników (nasze-03), błąd mało zmienia sumę kosztu — ale psuje każdą miarę „ile agent napisał”.

## nasze-14 — Koszt na commit: 2,94 USD (S1) i 2,96 USD (S2) do 16.10; 3,32 USD/commit 17.10 — i dlaczego to zła miara dla projektowania

- **Źródło:** `docs/reports/EKONOMIA_TOKENOW_2026-10-16.md:23` (1 102,73 USD / 375 commitów = 2,94; 407,97 / 138 = 2,96), `:38`
  (1 532,69 USD, 461 commitów ze stopką, 453 na main → 3,32; 3,38 na main), `docs/reports/INDEX.md:1295`; kryterium wyboru modelu
  `docs/reports/LIMITS_LEDGER.md:56-57`.
- **Typ / wiarygodność:** our_own_measurement / **high** (API sesji + `git log --all --format=%b | grep Claude-Session`).
- **Twierdzenie (zmierzone):** koszt wg cennika API na commit niemal identyczny w S1 (panele, workflow) i S2 (pojedynczy agenci);
  wzrost 2,94 → 3,32 w dniu paneli i przeglądów (dokumenty, nie commity). Autor: „koszt na commit nie jest dobrą miarą dla pracy
  projektowej; lepsza będzie miara na pozycję pracy (brief → main)”.
- **Mechanizm:** jednostka „koszt przyjętego wyniku” (pakiet + poprawki po nieudanym podejściu) zamiast ceny tokena; tabela pakietów
  w ledgerze ma kolumnę „przyjęty z 1. podejścia?” (`LIMITS_LEDGER.md:59`).
- **Dotyczy:** coordinator session, the collaboration interface (pozycja spec jako jednostka rozliczenia: koszt do `approved`).
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** cennik API ≠ limit abonamentu; ceny Anthropic w repo mają `cache_write: null` (koszt zapisu cache „nieznany”,
  `INDEX.md:1309`, `loom/tools/eval/pricing_2026-09-25.json`).

## nasze-15 — Podział modeli 21.10: Sonnet 57,5 % / Fable 42,5 % tokenów, w tym WSZYSTKIE 13 pakietów poprawek po przeglądach na Fable (≈ 106 mln, 19,6 %)

- **Źródło:** `docs/reports/LIMITS_LEDGER.md:88-89`, wnioski `:98-99`; ceny: `loom/tools/eval/pricing_2026-09-25.json` (USD/1M:
  Fable 10 / 50 / cache_read 0,25; Opus 4 / 20 / 0,20; Sonnet 2 / 10 / 0,20; Haiku 1 / 5 / 0,10; `batch_discount` 0,5; stan 2026-09-25).
- **Typ / wiarygodność:** our_own_measurement / **high** dla udziału w tokenach; ceny = datowane dane z repo (R60), nie zweryfikowane
  dziś.
- **Twierdzenie (zmierzone):** poprawki mechaniczne po przeglądach szły na najdroższym modelu; przy cenie katalogowej Fable/Sonnet
  = 5× (wejście i wyjście) udział Fable w LIMICIE jest większy niż 42,5 % (mnożnik abonamentu nieznany).
- **Mechanizm:** reguła od 21.10: poprawki po przeglądach, pakiety mechaniczne i czytanie logów na Haiku/Sonnet; Fable tylko etapy
  projektowe; jeden proporcjonalny przegląd poprawki błędu (`CLAUDE.md:199-201`, `tools/workflows/README.md:20-22`).
- **Dotyczy:** workflow agents, coordinator session, the collaboration interface (R88: „żonglerka modelami w zależności od stopnia
  złożoności i nietypowości mojej korekty”).
- **Kaliber:** small. **Wysiłek:** small (parametr `model` w `single-package.js:15`).
- **Uwagi:** 22–23.10 wszystkie pakiety w tabeli ledgera biegły na Sonnecie jawnie (`:66-77`); udział modeli po zmianie NIE został
  jeszcze podsumowany raportem.

## nasze-16 — Tani czytacz logów (Haiku) kosztuje głównie ŚWIEŻYM zapisem: 14 agentów, 5 ruchów po ≈ 54 tys., ≈ 1,5 mln każdy

- **Źródło:** `docs/reports/LIMITS_LEDGER.md:93-97` (raport S2 z 21.10 17:49: główna sesja Opus 5.5 27,6 mln / 100 ruchów / śr. kontekst
  182 tys.; 14 agentów Haiku 20,5 mln).
- **Typ / wiarygodność:** our_own_measurement / **medium** (`usage_report` S2, wagi założone).
- **Twierdzenie (zmierzone):** każdy agent Haiku czytał cały log CI, więc jego 5 ruchów to niemal wyłącznie zapis cache; 14 takich
  agentów ≈ 20,5 mln ≈ 43 % widocznego zużycia S2. Przyjęte pomysły S2: jeden agent na BIEG zamiast na pytanie, krótsze ogony
  (`tail_lines` 60, tylko blok REPORT), mniej tur głównej sesji na jedno zdarzenie.
- **Mechanizm:** tani model nie jest tani, jeśli każdy dostaje duży świeży wsad; opłaca się: zmniejszyć wejście (wycinek logu),
  łączyć pytania w jednego agenta, nie budzić sesji głównej na każde zdarzenie.
- **Dotyczy:** workflow agents, the collaboration interface (sekretarz czytający skrzynkę: tanie wyjście przy pustej skrzynce —
  `QUEUE_S1.md:11`).
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** efekt po zmianie nie zmierzony w ledgerze.

## nasze-17 — Restarty kontenera przez DYSK: dwa 23.10 (04:55, 07:19) straciły dwie bramki i agenta w fazie e2e; zadania `nohup` giną między turami

- **Źródło:** `docs/reports/QUEUE_S1.md:61` (04:55: dwie bramki równoległe stracone, dysk 93 %, build 6,4 GB), `:44` (07:19: agent W2b-1 sam
  z jednym buildem stracony; dysk 95 % = build 6,4 + 4,4 GB + ccache 5 GB; „przyczyna obu = DYSK, nie równoległość”), `:511`
  (zadania w tle przez `nohup` giną, gdy kontener zasypia między turami; restarty ≈ 09:38, 10:15, 11:06 innego dnia);
  reguły w `CLAUDE.md` (sekcja Loom: jeden katalog buildu, ≥ 5 GB wolne; `du -sh` pierwszą komendą po restarcie).
- **Typ / wiarygodność:** our_own_measurement / **high** (rozmiary `du`, czasy, skutki opisane na miejscu).
- **Twierdzenie (zmierzone):** katalog buildu dev 4,4–6,4 GB (CMakeFiles 3,0 GB, `libloom_core.a` 1,6 GB, binaria 1,9 GB), ccache
  ~5 GB; przy ~95 % zajętości kontener restartuje i zabija każde zadanie w tle. Stracone: 2 bramki (po 40–90 min) + faza e2e agenta;
  kod nie przepadł (commity wypchnięte wcześniej); zadania zaplanowane przeżywają restart (`:61`).
- **Mechanizm:** strata = powtórzenie bramki (czas) + ponowne cache'owanie kontekstu agenta (tokeny, nasze-02) + ewentualnie nowy
  agent kontynuacji (prefiks + odczyt kart). Zasada „jeden katalog buildu, ≥ 5 GB wolne, bramki jedna po drugiej”.
- **Dotyczy:** workflow agents, coordinator session; po przeprowadzce na VM (OPS1) dysk 200 GB jako preset właśnie z tych pomiarów
  (`QUEUE_S1.md:37`).
- **Kaliber:** small. **Wysiłek:** small (reguła) / medium (`-gz`/`-g1` w preset dev — otwarte, `CLAUDE.md`).
- **Uwagi:** to koszt czasu i powtórzeń, nie bezpośrednio tokenów; w ledgerze nie ma osobnej liczby „ile punktów kosztował restart”.

## nasze-18 — Największe zlewy kosztu ostatnich 48 h (21.10 ≈ 10:00 → 23.10 10:11 UTC) z liczbami

- **Źródło:** `docs/reports/LIMITS_LEDGER.md:39-40` (21.10 13:19→16:20: 0 pkt, tylko CI), `:62-77` (tabela pakietów 21–22.10),
  `:136` (23.10 00:12: tydzień 46 %), `:140` (R57 93,8 mln ≈ 4,3 pkt), `:142` (07:50: 52 %; od 00:12 +6 pkt w 7,6 h), `:145` (10:11:
  57 %; +5 pkt w 2,3 h; okno 5 h 24 % po 11 min); `docs/reports/QUEUE_S1.md:64` (W2b-1/K2c-2), `:19` (K3), `:85` (G4 Fable), `:132`
  (restarty), `tools/pace_plan.json` (`_note`: 22.10 04:40 tydzień 33 %); `git log --since 2026-10-21T10:00Z`: 206 commitów na tej
  gałęzi, 367 na wszystkich osiągalnych (policzone 23.10 10:3x).
- **Typ / wiarygodność:** our_own_measurement / **medium** (procenty od dostawcy, 1 pkt rozdzielczości; równoważniki z wag).
- **Twierdzenie (zmierzone):** tydzień **30 % (21.10 17:33) → 57 % (23.10 10:11) = +27 pkt w ≈ 41 h** (≈ 16 pkt/dobę przy planie
  ≈ 9–10). Przyrosty: 21.10 17:33 → 22.10 04:40: +3 pkt (≈ 7 pakietów: D2, K1a×2, K1b, W1a, W1b, DOC1); 22.10 04:40 → 21:28:
  33 → 38 % (+5 pkt: K1c1–4, FLAKE1, K2a(+fix), K2b1–2, W2a(+fix) — 12 pakietów Sonnet); 22.10 21:28 → 23.10 00:12: **38 → 46 %
  (+8 pkt w ≈ 2,7 h)** — w tym **R57 (badanie + sceptyk + 2 projekty Fable, 3 restarty) 93,8 mln rów. ≈ 4,3 pkt**, z czego same
  projekty Fable 35,2 + 25,1 mln (≈ 1,6 + 1,1 pkt), restarty ≈ 19 mln (≈ 0,9 pkt), oraz K2b-3/K2b-4; 23.10 00:12 → 07:50: +6 pkt
  (K2c-2-fix, G1b, W2b-1 ×2, OPS1, SPEC1, G4 Fable, R57-fix, trzy bramki); 07:50 → 10:11: **+5 pkt w 2,3 h** (K3 badanie 0,8 pkt;
  R88 v1 + strona spec z kontekstu koordynatora; R89 — 5 torów równolegle, 24 % okna 5 h w 11 min).
  Największe pojedyncze agenty w tokenach: W2b-1/K2c-2 566 tys. (264 wywołania, 126 min, 2 agenci), K1c2 420 tys., K1b 397 tys.,
  W1b 371 tys., W2a 350 tys., K3 badanie 326 tys., K2b1 312 tys., K2b2 310 tys.; Fable: G4 187 tys. w 22 wywołaniach.
- **Mechanizm (co z tego wynika):** trzy rodzaje zlewów — (a) długie pakiety jądra z buildem/e2e w środku (3–5× planu), (b) łańcuch
  badanie→projekt Fable w jednym workflow z restartami, (c) zryw równoległy. Każdy ma już regułę; żaden nie ma jeszcze pomiaru
  „po”.
- **Dotyczy:** coordinator session, workflow agents.
- **Kaliber:** medium. **Wysiłek:** —.
- **Uwagi:** udział S2 i czatów właściciela w tych punktach niewidoczny (konto wspólne); 206 commitów / 27 pkt ≈ 7,6 commita na
  punkt — liczba poglądowa (commity bardzo nierównej wagi, wiele to wpisy w kolejce i spec).

## nasze-19 — Platforma: strona-artefakt ze wspólną bazą jako skrzynka decyzji właściciela (R88 v1)

- **Źródło:** `docs/spec/README.md:130-139` (artefakt https://claude.ai/artifact/MfbrGaubViGGhsp2Z4ZEm7, kolekcja `spec_inbox`:
  `node, alias, kind = approve|correct|remark, text, at, state = new → applied|escalated`), `tools/spec_html.py:274-281, 329`
  (`window.claude.use('db')`, `window.claude.use('user')`, `db.collection('spec_inbox')`), `docs/reports/QUEUE_S1.md:11` (09:36 UTC:
  capabilities db+user; „próbka zapis→odczyt→usunięcie sprawdzona”), `docs/reports/INDEX.md` sekcja „S1 / R88-v1” (nie sprawdzone:
  pierwszy bieg sekretarza, przyciski na telefonie).
- **Typ / wiarygodność:** other (możliwość platformy) / **high** co do istnienia (strona opublikowana, zapis/odczyt sprawdzony);
  oszczędność tokenów — **założona, nie zmierzona**.
- **Twierdzenie:** strona generowana z `spec.json` ma przy każdej pozycji „Zatwierdzam parafrazę” / „Popraw swoimi słowami” /
  „Uwaga”; klik zapisuje wpis w bazie strony, bez udziału koordynatora; czat zostaje do dyskusji. Odwrót: publikacja bez
  `capabilities`, zadanie wyłączone.
- **Mechanizm:** decyzja właściciela trafia do DANYCH zamiast do kontekstu drogiego koordynatora (który i tak każdy ruch czyta cały,
  nasze-03); zapis dosłowny jego słów (R59) bez parafrazy po drodze.
- **Dotyczy:** the collaboration interface, spec tooling, the owner on a phone.
- **Kaliber:** small (v1). **Wysiłek:** small (zrobione) / medium (pomiar: ile pozycji przeszło przez skrzynkę vs czat).
- **Uwagi:** właściciel: „nie bierz tego schematu dosłownie” (R88) — v1 idei; żadna pozycja jeszcze nie przeszła tą drogą (strona
  opublikowana 09:36, właściciel śpi od 08:08, `QUEUE_S1.md:25`).

## nasze-20 — Platforma: zadanie zaplanowane spawnujące ŚWIEŻĄ sesję na wybranym modelu („Sekretarz spec”, Sonnet, co godzinę)

- **Źródło:** `docs/ops/ZADANIA_ZAPLANOWANE_2026-10-23.md:9-24` („Przegląd gałęzi GPT” cron `6 */6 * * *` przypięty do sesji
  koordynatora od 14.10; „Sekretarz spec (R88)” cron `23 * * * *`, ŚWIEŻA sesja na Sonnecie, od 23.10 09:35,
  `trig_01VyHbcN2o8DQhwKq6D9iNaU`), `docs/reports/QUEUE_S1.md:11` („tani wyjście przy pustej skrzynce”; „ostrzeżenie: zadanie nie przenosi
  konektorów MCP; te narzędzia są wbudowane, nie MCP”), `docs/ops/TELEPORT_S1.md:53` (zamiennik na VM: `cron` + `claude -p`).
- **Typ / wiarygodność:** other (możliwość platformy) / **high** co do istnienia zadania (id zapisane); **niezweryfikowane**: pierwszy
  bieg i dostępność narzędzi `ArtifactData`/`Artifact`/`send_message` w świeżej sesji (`QUEUE_S1.md:11`).
- **Twierdzenie:** rutyna (czytanie skrzynki, `spec.py approve/correct/add`, odświeżenie strony, commit+push, eskalacja
  nietypowych wiadomością do S1) biegnie w sesji BEZ nagromadzonego kontekstu — koszt ≈ prefiks + zadanie, na tańszym modelu;
  „Przegląd gałęzi GPT” od 16.10 każdy bieg kończył „bez zmian” (`:20`).
- **Mechanizm:** oddzielenie pracy cyklicznej od kontekstu koordynatora (który czyta ~400–600 tys. na ruch) — zamiast budzić go co
  godzinę, budzi się tani sekretarz; eskalacja tylko wyjątków.
- **Dotyczy:** the collaboration interface, spec tooling, coordinator session.
- **Kaliber:** small. **Wysiłek:** small (istnieje) / medium (pomiar kosztu biegu sekretarza; zapis próbek `rate_limit_event` tym samym
  mechanizmem).
- **Uwagi:** zadania przypięte do sesji nie jadą na nową maszynę (TELEPORT) — treści skopiowane do repo właśnie dlatego; „Przegląd
  GPT” biegnie co 6 h od 14.10 mimo braku zmian od 16.10 — koszt tych pustych biegów nie jest w ledgerze.

## nasze-21 — Platforma: dwie sesje (S1, S2) z własnymi kontekstami, wiadomości między sesjami i osobny raport zużycia każdej

- **Źródło:** `docs/ops/TELEPORT_S1.md:33-34` (S2 `session_01Armc9PkDiTDzjFBgXAMCjR`, kontakt `send_message`), `:54` (zamiennik:
  komentarz w QUEUE + linia do właściciela), `docs/reports/LIMITS_LEDGER.md:93-95` (S2 własny `usage_report` 17:49: 48 mln; S1 + S2 ≈
  614 mln; S2 ≈ 8 % widocznego), `docs/reports/EKONOMIA_TOKENOW_2026-10-16.md:23` (S2 407,97 USD, 138 commitów do 16.10), `QUEUE_S1.md:13`
  (S2 smoke na emulatorze jako dowód dla pakietów S1).
- **Typ / wiarygodność:** other (możliwość platformy) / **high** (w użyciu od 13.10; liczby z API sesji).
- **Twierdzenie (zmierzone):** podział pracy między sesje daje dwa niezależne konteksty (S2: 100 ruchów po śr. 182 tys. vs S1
  377–587 tys.); S2 robi smoke na emulatorze i czyta logi CI (Haiku), S1 koordynuje pakiety; wymiana przez `send_message`
  (sha, wyniki) kosztuje jedną turę po każdej stronie.
- **Mechanizm:** mniejszy kontekst na sesję = tańszy ruch; prośba „ten sam raport” do S2 dała sumę konta bez czytania jej zapisów
  przez S1.
- **Dotyczy:** coordinator session, the collaboration interface (osobna tania sesja rozmowy z właścicielem to ten sam wzorzec:
  R89 „Sonnet do konwersacji”).
- **Kaliber:** small. **Wysiłek:** small (istnieje).
- **Uwagi:** każda wiadomość między sesjami budzi turę drogiego odbiorcy; S2 zaproponował „mniej tur głównej sesji na jedno
  zdarzenie” (`LIMITS_LEDGER.md:97`) — liczba takich tur nie jest mierzona.

## nasze-22 — Platforma: narzędzie `Workflow` + skrypty jako dane pakietu (brief w `args`, karta wznowienia, model per etap, `parallel()`)

- **Źródło:** `tools/workflows/single-package.js:3` (opis), `:8-15` (args: wt, branch, remote_branch, ci_workflow, id, scope, accept,
  rules_extra, model), `:24` (DISCIPLINE: ~50 wywołań, karta `done=false` + co zostało), `:25-30` (schemat karty: done, commits, ci,
  summary, decisions, not_done), `:33-40` (2 podejścia: agent kontynuacji czyta kartę i `git log`); `tools/workflows/research-synthesis.js:61`
  (`parallel()` dla torów), `:68-78` (etap oceny na modelu sesji, synteza `model: 'fable'`); `tools/workflows/README.md:7-9, 12-15, 17-22`;
  `tools/workflows/briefs/README.md:3-5` (briefy skopiowane do repo, bo scratchpad ginie przy zmianie maszyny).
- **Typ / wiarygodność:** other (możliwość platformy) / **high** (w użyciu: ≥ 15 pakietów w tabeli ledgera 21–22.10 tym skryptem);
  README mówił 21.10 „wzorzec niepotwierdzony na pełnym przebiegu” (`:14-15`) — od tego czasu potwierdzony tabelą.
- **Twierdzenie:** pakiet = JSON z briefem; skrypt dopisuje reguły (odsyłacz do `CLAUDE.md`, reguła cache 4 min, dyscyplina 50
  wywołań, stopka z modelem AGENTA) i wymaga karty; po `done=false` drugi agent kończy tylko `not_done`; `model` per pakiet/etap.
- **Mechanizm:** krótkie agenty z twardym wyjściem (karta) zamiast jednego długiego; model dobierany do etapu; brief trwały w repo
  (`spec:<id>` zamiast wklejania — nasze-23).
- **Dotyczy:** workflow agents, coordinator session, the collaboration interface (pakiety jako widoczne dane: brief, karta, koszt).
- **Kaliber:** medium. **Wysiłek:** small (istnieje).
- **Uwagi:** 21 plików w `tools/workflows` ma na sztywno `/home/user` (OPS2, `INDEX.md` sekcja OPS1); na VM zamiennikiem jest `Agent`
  z tym samym briefem (`TELEPORT_S1.md:52`) — nie sprawdzone.

## nasze-23 — Spec jako dane: 487 pozycji, 1 090 cytatów dosłownych sprawdzanych `plik:linia`; brief cytuje `spec:<id>` zamiast wklejać

- **Źródło:** `docs/reports/QUEUE_S1.md:95-96` (SPEC1 gotowe 23.10 00:34: 487 węzłów, 1 090 cytatów; agent 53 wywołania, 274 tys.
  tokenów, 16 min), `docs/spec/README.md:72-81` (`spec:<numer>` w briefie, `spec.py show`, `cite --full`), `:57-70` (stany rozumienia
  none/proposed/approved/corrected; `approve` tylko na słowo właściciela), `docs/reports/INDEX.md` sekcja „S1 / SPEC1” (28 testów;
  „briefy nadal pisane ręcznie — koordynator ma zacząć cytować”).
- **Typ / wiarygodność:** our_own_measurement / **high** dla liczb (policzone przez `check`); oszczędność tokenów — **założona**.
- **Twierdzenie:** słowa właściciela są danymi z numerem; korekta = nowa/zmieniona pozycja, nie restart (`README.md:40`); brief G4
  cytował 18 blockquote'ów sprawdzonych skryptem jako dosłowne (`QUEUE_S1.md:85`); agent czyta pozycję poleceniem zamiast dostawać
  wklejkę.
- **Mechanizm:** to samo, co waste control 6b (reguły w `CLAUDE.md`, nie w briefie): krótszy brief = krótszy prefiks każdego agenta
  (nasze-02: 39–60 tys. na start); jedno źródło prawdy dla wielu agentów (czytane z cache, gdy ten sam plik).
- **Dotyczy:** spec tooling, workflow agents, the collaboration interface (R73/R88: żywe drzewo spec jest jego widokiem).
- **Kaliber:** medium. **Wysiłek:** small (istnieje); medium: `check` w `tools/gate.sh`, parafrazy dla pozycji (wszystkie `none` poza
  R73/R88/R89 `proposed`).
- **Uwagi:** 117 wierszy prozy w 43 fragmentach poza pozycjami (`README.md:116-120`); zbiór sięga 16.10 + rejestr R do R89.

## nasze-24 — Tania kontrola przed użyciem: Haiku (14 wywołań) znalazła 34 odstępstwa w dwóch projektach Fable; poprawka Sonnet 52 wywołania / 205 tys. / 8 min

- **Źródło:** `docs/reports/QUEUE_S1.md:103` (kontrola Haiku: 34 znaleziska, 8 blokujących), `:93` (R57-fix: Sonnet 52 wywołania,
  205 tys. tokenów, 8 min), `:85` (G4: Fable 22 wywołania / 187 tys. / 14 min + Haiku 17 uwag, 2 blokujące + Sonnet 32 zamiany),
  `CLAUDE.md` sekcja „Check before use” (4 antywzorce + pytanie R60); koszt czerwonej bramki: `docs/reports/LIMITS_LEDGER.md:73, 75, 77`
  („50 min + 1 pakiet”).
- **Typ / wiarygodność:** our_own_measurement / **high** (liczby z kart agentów).
- **Twierdzenie (zmierzone):** przegląd dokumentu projektowego najtańszym modelem (14–17 wywołań) wyłapał odstępstwa od filozofii, które
  najdroższy model (Fable) popełnił; poprawka na Sonnecie; nic nie zostało odrzucone przez agenta poprawiającego (`INDEX.md` R57-fix).
  Koszt czerwonej bramki wykrytej PO pakiecie: ≈ 50 min + 1 pakiet poprawkowy (3 przypadki 22.10).
- **Mechanizm:** tani sędzia przed drogim użyciem (kontrola → poprawka → dopiero pakiety); audyt tekstu w e2e jako stała asercja
  (`LIMITS_LEDGER.md:70, 73`) łapie to, czego testy jądra nie łapią.
- **Dotyczy:** workflow agents, coordinator session, the collaboration interface (sekretarz jako kontroler „czy to zostało faktycznie
  ustalone”).
- **Kaliber:** small. **Wysiłek:** small (procedura w `CLAUDE.md`).
- **Uwagi:** fałszywe trafienia kontroli Haiku nie są liczone („znaleziska to twierdzenia do sprawdzenia”); porównania „bez kontroli”
  nie ma.

## nasze-25 — Co sesja niesie, a co niesie git: teleport koordynatora w ≈ 15 min czytania (założone), narzędzia, które nie jadą

- **Źródło:** `docs/ops/TELEPORT_S1.md:14-18` (kolejność czytania, „≈ 15 min”), `:49-57` (tabela: Workflow, zadania zaplanowane,
  `send_message`, GitHub MCP, `list_events` — i zamienniki na VM; „Nic z tego nie było sprawdzone na VM”), `:59-62` (odwrót: git = prawda,
  przełączenie tylko między pakietami), `tools/workflows/briefs/README.md:3-5`, `docs/reports/QUEUE_S1.md:3-4` (kolejka = „wszystko, czego
  kontekst nie przeniesie”).
- **Typ / wiarygodność:** other (możliwość platformy) + our_own_measurement dla wymienionych strat / **medium** — lista z doświadczenia
  21–23.10; czas 15 min i przeniesienie bez strat — **założone**, nie zmierzone (TELEPORT jeszcze nie wykonany).
- **Twierdzenie:** stan koordynatora celowo trzymany poza kontekstem: `QUEUE_S1.md` (log od najnowszego), `INDEX.md` (decyzje
  z odwrotem), `spec.json` (słowa właściciela), karty i briefy w repo, `LIMITS_LEDGER.md` (tempo), `ZADANIA_ZAPLANOWANE` (treści
  zadań). Koszt nowej sesji = prefiks + te odczyty, zamiast ponownego cache'owania 400–600 tys. kontekstu przy każdej przerwie.
- **Mechanizm:** „sesja nigdy nie jest jedynym zapisem” (waste control 1) — utrata kontekstu (restart, kompresja, przeprowadzka)
  kosztuje tylko ponowny odczyt kart, nie powtórzenie pracy.
- **Dotyczy:** coordinator session, the collaboration interface (te same pliki są naturalnymi „wspólnymi dokumentami” R89).
- **Kaliber:** medium. **Wysiłek:** small (w toku).
- **Uwagi:** kompresja kontekstu sesji głównej gubi też zapisy `usage` (nasze-12) — stan przenosi git, ale pomiar kosztu sprzed
  kompresji przepada.

## nasze-26 — Jądro aplikacji: prompt routera skrócony z ≈ 2 580 do ≈ 55–440 tokenów przez wyszukanie katalogu akcji przed klasyfikacją

- **Źródło:** `docs/reports/INDEX.md:1041-1043` (P32, 2026-10-16: prompt klasyfikatora ok. 2,9 tys. tokenów przy ~160 liniach katalogu;
  część użytkownika 10 050 znaków (161 akcji) → ok. 210–1 700 znaków (4–8 akcji); ~2 580 → ~55–440 tokenów, szacunek znaki/3,9;
  system prompt z przykładami ~1 750 znaków bez zmian), `:1044` (odwrót: `model.catalogue_retrieval.top_k` = 0 → cały katalog).
- **Typ / wiarygodność:** our_own_measurement / **medium** (długości znaków zmierzone, tokeny oszacowane z dzielnika 3,9).
- **Twierdzenie (zmierzone w znakach):** 6–50× krótsza część zmienna promptu przy każdym wywołaniu routera; część stała (system +
  przykłady) niezmieniona, więc łączna oszczędność na wywołanie ≈ 2 100–2 500 tokenów.
- **Mechanizm:** wyszukiwanie leksykalne (top-k) zawęża katalog akcji przed wywołaniem modelu; dodatkowo P34: komendy regułowe czytane
  PRZED modelem i model pomijany, gdy reguła pasuje (preset) — zero tokenów na trafione komendy.
- **Dotyczy:** the app kernel (router `intent_routing.pack`); wzorzec przenośny na interfejs współpracy (tanie dopasowanie reguł
  przed drogim modelem).
- **Kaliber:** small. **Wysiłek:** small (zrobione).
- **Uwagi:** „selektor kończy na wyniku 0, więc katalog bywa krótszy niż K” — jakość (trafność) na prawdziwym modelu testuje S2
  (`:1045`), oszczędność w tokenach na prawdziwym tokenizerze nie zmierzona.

## nasze-27 — Jądro aplikacji: koszt mierzenia — każde wołanie przez bramkę dopisuje ≈ 9,5 KB do księgi zużycia (175 ms debug); zwijanie 4,5 MB → 127 KB

- **Źródło:** `docs/reports/INDEX.md:328-330` (zmierzone: 200 wołań = 1,9 MB i 35 s w buildzie debug; `semantic.analyse`/`batch.chat`
  jedno wołanie na wiadomość ⇒ ≈ 1 GB księgi na 100 tys. wiadomości), `:355-357` (122 operacje = 4,5 MB z WAL, po zwinięciu 127 KB;
  ≈ 37 KB/operację w teście, bo WAL dubluje strony; preset `usage_compaction` jako dane).
- **Typ / wiarygodność:** our_own_measurement / **high** (pomiar w teście; wartości dla Release nie mierzone).
- **Twierdzenie (zmierzone):** księga zużycia (`usage-policy.sqlite`) ma własny koszt miejsca i czasu; dla czatu nieistotny, dla wołań
  masowych znaczący; zwijanie jako profil danych z nakładką użytkownika.
- **Mechanizm:** telemetria zużycia sama musi być tania i konfigurowalna (częstość zwijania jako preset); analogicznie nasz
  `LIMITS_LEDGER.md` rośnie ręcznie (145 linii po 3 dniach).
- **Dotyczy:** the app kernel; coordinator session (dziennik próbek jako dane, nie proza — do rozważenia w interfejsie współpracy).
- **Kaliber:** small. **Wysiłek:** small.
- **Uwagi:** liczby z buildu debug (-O0); Release nie mierzony (`INDEX.md:1699`, `QUEUE_S1.md:377-378` — paragon „zawsze” spowalnia
  `send`? nie zmierzone).

## nasze-28 — Jądro aplikacji: szacunek kosztu PRZED wysyłką i paragon PO wysyłce pokazują tokeny osobie (bez ceny tam, gdzie jej nie ma)

- **Źródło:** `docs/reports/INDEX.md:551` (`action_estimate`: znaki wejścia, pasmo 4–3 znaków/token, współczynnik wyjścia wg
  `content_op` np. streszczenie 5–40 %, podłoga 32 tokenów, `max_tokens` użytkownika, cena z `ModelRegistry::get_pricing`; „nieznane
  zostaje nieznane”), `docs/reports/QUEUE_S1.md:364-369` (arkusz „Co wyświetliśmy / Co wysłaliśmy”: „Instrukcja 19 tok. · Rozmowa 103 tok.
  (8 pozycji) · Pytanie 9 tok. · Puste sekcje: Pamięć, Graf, Projekt · prefiks stały: nie · strażnik ×10: 1,4× bazy · koszt nieznany
  — brak ceny”), `:304` (zmierzona przyczyna błędu: szacunek po „Szerzej” liczony z PUSTYM tekstem — naprawione K1c1),
  `LIMITS_LEDGER.md:67-69` (perspektywa `sent_to_model`: paragon jako graf, waga = tokeny, pominięte jako `left_out_of_model`).
- **Typ / wiarygodność:** our_own_measurement / **high** dla istnienia i kształtu (testy e2e porównują tekst); dokładność szacunku —
  kalibracja, nie pomiar.
- **Twierdzenie:** użytkownik widzi przed wysyłką przedział kosztu, a po wysyłce paragon per element (id węzła pamięci, id wiadomości,
  `via`, głębokość), udziały sumują się dokładnie; strażnik ×10 porównuje z bazą; paragon nie niesie ceny → „koszt nieznany”.
- **Mechanizm:** „nic ukrytego”: tokeny jako widoczna waluta każdej akcji; to samo, czego brakuje w naszej własnej pracy (koszt
  pakietu widoczny dopiero w ledgerze po fakcie, nie w chwili decyzji).
- **Dotyczy:** the app kernel, the collaboration interface (ta sama zasada: koszt pakietu/decyzji pokazany przy pozycji spec).
- **Kaliber:** medium. **Wysiłek:** small (jądro ma to); medium (przeniesienie na interfejs współpracy).
- **Uwagi:** cena zapisu cache w danych = `null` (nieznana), więc pełny koszt rozmowy z cache nie jest liczony (`INDEX.md:1309`).

## nasze-29 — Słowa właściciela (nie pomiar): tokeny wyjścia kosztują JEGO czas; tani interfejs konwersacyjny; sekretarz; „to nie jest specyfikacja”

- **Źródło:** `python3 -I tools/spec.py show R62 R88 R89` → `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md:741` (R62, 23.10 ≈ 00:20:
  „co się dzieje z twoimi wyjściowymi tokenami które tutaj trafiają: musisz je powtarzać droższym modelem bo jednak mój czas który bym
  zmarnował na przewijanie i szukanie jest więcej wart… działaj śmiało”), `:880` (R88, 09:4x: „Zamiast gadać i wymieniać się długimi
  tekstami pracowali po prostu na jednej specyfikacji w ustalonym formacie, interfejs konwersacyjny na jakimś sonecie, albo… żonglerka
  modelami w zależności od stopnia złożoności i nietypowości mojej korekty, sekretarz ustala co zostało faktycznie ustalone… nie bierz
  tego schematu dosłownie”), `:885-886` (R89, 09:5x: „popatrzeć jak ludzie oszczędzają tokeny… rozwiązania różnego kalibru małe duże
  naukowe eksperymentalne, do tego relewatność… to nie jest specyfikacja bo ty lepiej znasz swój flow… masz wolną rękę”); R63 `:746`
  („jak zmieniam model na lepszy to domyślnie możesz poluzować troszkę wymagania”).
- **Typ / wiarygodność:** other (cytaty dosłowne właściciela, sprawdzone `spec.py check` jako dosłowny fragment pliku) / **high**.
- **Twierdzenie:** dwie waluty — tokeny (limit) i jego czas czytania (droższy); format wspólnej pracy = jedna specyfikacja w ustalonym
  formacie; role i modele to PRZYKŁADY („nie bierz dosłownie”, „wolna ręka”); reguły procedur napisane dla Sonneta może poluzować
  silniejszy koordynator (R63), filozofia zostaje.
- **Mechanizm:** wyznacza, co w ogóle liczyć jako „oszczędność” w tym pakiecie: mniej jego czytania + mniej punktów limitu + mniej
  jego decyzji „za niego”.
- **Dotyczy:** the collaboration interface, coordinator session, the owner on a phone.
- **Kaliber:** —. **Wysiłek:** —.
- **Uwagi:** rozumienie R88 i R89 jest w stanie `proposed` (nie zatwierdzone); R61–R63 `none`.

## nasze-30 — Rodzaj pracy 21.10: wdrożenie 35 %, poprawki po przeglądach + diagnoza 29 %, przeglądy 25 %, projekt 11 % — pętla przeglądów to 54 % wydatku agentów

- **Źródło:** `docs/reports/LIMITS_LEDGER.md:85-87` (rodzaj pracy; temat: M1-W0 54 % = wdrożenie 21,4 + poprawki 19,8 + przeglądy 13,2;
  poprawka logowania 21 % = diagnoza + poprawka 9,4 + przegląd 11,3; P39 11 %; pakiety pojedyncze 6,4 %; X-Port 3,7 %; zatrzymany
  długi agent W0 2,7 %), `:98-99` (wniosek: „jeden proporcjonalny przegląd poprawki błędu”).
- **Typ / wiarygodność:** our_own_measurement / **medium** (równoważniki z wag; klasyfikacja po etykietach agentów).
- **Twierdzenie (zmierzone):** przeglądy (24,5 %) + poprawki po nich i diagnoza (29,1 %) = 53,6 % wydatku agentów jednego dnia; dla
  poprawki logowania przegląd (11,3 %) kosztował WIĘCEJ niż sama diagnoza i poprawka (9,4 %).
- **Mechanizm:** po długości agenta drugą dźwignią jest pętla przegląd→poprawka: jeden recenzent proporcjonalny do zmiany, poprawki
  na tańszym modelu, naprawy w paczkach po 5 znalezisk (`INDEX.md:1740`), kontrola Haiku PRZED użyciem (nasze-24) zamiast trzech
  recenzentów po.
- **Dotyczy:** workflow agents, coordinator session.
- **Kaliber:** small. **Wysiłek:** small (reguły wprowadzone 21.10).
- **Uwagi:** udział po zmianie nie został ponownie policzony; jakość (ile błędów przepuszcza jeden recenzent) nie jest mierzona.

---

## Czego nie znalazłem (luki w naszych pomiarach — każda to kandydat na mały pakiet pomiarowy)

1. **Pomiar „po”** dla żadnej z wprowadzonych reguł: udział prefiksu i przepisanych kontekstów po regule 4 minut (nasze-02), udział
   przeglądów po „jednym recenzencie” (nasze-30), udział Fable po przeniesieniu poprawek na Sonnet (nasze-15), koszt czytaczy logów po
   zmianach S2 (nasze-16). Ledger ma tylko stan sprzed 21.10 17:50.
2. **Koszt pakietu na Haiku** (50 ruchów): `"haiku": null` w `pace_plan.json`; zmierzony tylko czytacz logu (5 ruchów).
3. **Mnożnik Fable w limicie abonamentu** — nieznany; wagi „z cen katalogowych” (`_fable_note`), `usage_report.py` ich nie stosuje.
4. **Koszt zrywu równoległego vs sekwencji** dla tego samego zbioru agentów (hipoteza o wspólnym prefiksie z cache, `INDEX.md:1747-1748`)
   — nie zmierzono; R89 (5 torów naraz, 24 % okna w 11 min) to okazja: porównać sumę `usage` torów z kosztem, jaki miałyby po kolei.
5. **Koszt koordynatora na pakiet** (≈ 20 tur × 0,28 mln = 0,25 pkt) — z jednego pakietu (R38); liczba tur koordynatora na zdarzenie
   (powiadomienie agenta, bramka, CI) nie jest liczona, a kontekst koordynatora rośnie (377 → 587 tys. na ruch 21.10).
6. **Koszt sekretarza spec** (nasze-20) i **koszt pustych biegów** „Przegląd gałęzi GPT” co 6 h od 16.10 — nie w ledgerze.
7. **Ile decyzji właściciela przeszło skrzynką (nasze-19) zamiast czatem** — zero danych (strona od 09:36, właściciel śpi).
8. **Koszt restartów kontenera w punktach** (nasze-17) — opisane skutki, nie policzone tokeny powtórzeń.
9. **Dwie daty resetu tygodnia** w ledgerze (27.10 08:00 vs 00:00 UTC) — do wyjaśnienia następną próbką `rate_limit_event`.
10. **Transkrypty JSONL** sesji nie były czytane (brief: tylko repo) — wszystkie liczby „z zapisów” to liczby już zapisane w
    ledgerze/kolejce przez koordynatora, nie moje przeliczenie; nie uruchamiałem `usage_report.py`.
11. **Koszt na pozycję spec** (brief → `approved`/main) jako jednostka zamiast commitu — postulat z `EKONOMIA:38`, niezrealizowany.
12. Nie ma w repo liczby tur ani tokenów wyjścia, które właściciel PRZECZYTAŁ (R62: jego czas jako waluta) — żadnej miary tej drugiej
    waluty.

## Gdzie się zatrzymałem

Po przeczytaniu wszystkich źródeł z briefu i ich odsyłaczy (ledger, INDEX w zakresach z grep, QUEUE w liniach z grep, EKONOMIA,
pace, usage_report, workflow, spec, teleport, zadania zaplanowane, ceny, słowa właściciela). Ostatnie trzy przeczytane fragmenty
(`INDEX.md:549-553`, `QUEUE_S1.md:364-369`, `research-synthesis.js:55-80`) tylko potwierdzały wzorce już zapisane (koszt widoczny
osobie; model per etap). Nie czytałem starszych raportów S2 (`S2_R*.md`) ani `docs/STATE.md` — mogą zawierać dalsze pomiary
jądra (P19 wydajność, R26), ale brief tego toru wskazywał na ekonomię tokenów naszej PRACY, nie aplikacji; trzy ustalenia
o jądrze (nasze-26..28) są tu jako wzorce przenośne na interfejs współpracy, nie jako komplet.
