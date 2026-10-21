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

**Odczyt z 00:14:** od 00:10 do 00:14 przybyły 3 punkty w około 4 minuty (≈ 0,8 pkt/min),
od 00:05 średnio ≈ 0,6 pkt/min. Do reszty okna (276 min) zostaje 89 punktów, czyli stać nas
na ≈ 0,32 pkt/min. Przy tempie bez zmian okno skończyłoby się około 02:10–02:40 UTC, 2–2,5 h
przed resetem. Reakcja: zatrzymany długi agent W0 (276 tys. kontekstu, zero commitów po
13 minutach; jego robota zapisana jako commit WIP `4198dd88`), W0 puszczone od nowa jako
siedem małych pakietów. **Do sprawdzenia za 20–30 minut:** czy tempo spadło do ≈ 0,3–0,4.

Tydzień: 3 % po ~16 godzinach od resetu; przy rozdzielczości 1 punktu stosunek 5 h : 7 dni
wygląda na ≈ 3–4 : 1 (jedno pełne okno ≈ ćwierć tygodnia), **do potwierdzenia** kolejnymi
próbkami. Jeśli się utrzyma, wiążące na dłuższą metę jest okno tygodniowe, a nie 5-godzinne.
