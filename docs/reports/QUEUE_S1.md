# Kolejka S1 — stan 2026-10-22 01:25 UTC

Po co ten plik: zasada 1 (trwałe checkpointy) — kto przejmie pracę po przerwie sesji, ma tu
wszystko, czego kontekst nie przeniesie. Aktualizowany przy każdym zakończeniu pakietu.

## CI 261 = SUCCESS (01:15 UTC, 119 min): `main` `59de144c` (K1a + K1b + poprawki asan) ZIELONY; `main` = `d103d5d8` (W1a + rejestr interfejsów) od 01:21, nowy bieg w toku
Pierwszy w pełni zielony `main` po K1 (asan przeszedł: `test_context_receipt`, `chat_active_task_*`). Push `d103d5d8` (bramka lokalna `w1a2` zielona) wyzwolił nowy bieg (≈ 120 min, do ok. 03:20; kontrola 03:18).
**W1b** (`wf_63253a37-69f`, start 00:53) w toku w `m1d0` — jego commity idą na `claude/m1-w0`; FF `main` po jego zakończeniu i bramce, nie wcześniej niż po końcu biegu `d103d5d8`.

## W1a: bramka `w1a2` ZIELONA (`d103d5d8`, 00:40); **W1b wystartowało 00:53 UTC** (`wf_63253a37-69f`, Sonnet, push na `claude/m1-w0`)
Po pierwszej bramce `w1a` (RED: `unit.test_graph_entry` 109 vs 111 tras web) zregenerowałem rejestr interfejsów (`gen_graph_interfaces.py` → `gen_runtime_profiles.py` → pin `graph/interfaces.pack` w `data_domains.pack`) —
ten łańcuch jest w briefie W1b. **FF `main` z commita `d103d5d8` (nie z roboczego stanu W1b) dopiero po końcu CI 261** (start 23:16, koniec ok. 01:10; kontrola 01:08).
Tydzień 32 % (00:52), cel 23,0 % — jesteśmy 9 pkt nad.

## W1a GOTOWE (23:50 UTC, `1a3233f2` na `claude/m1-w0`) — bramka lokalna `w1a` w toku (po merge `59de144c`); FF `main` po zielonym CI 261
Arkusz „Co wyświetliśmy/Co wysłaliśmy” (`wf_e2dce102-ea6`, 123 wywołania, 318 tys. tokenów): API `context.receipt`/`context.modify` w `loom-api/http/jni`, `ContextReceipt.tsx` + stan + słowa w
`conversation_view.pack` (`presentation.context_receipt` rev 2, status `partial`), linia „Kontekst: profil · pozycji N · ~T tok. — zobacz” pod odpowiedzią (ustawienie `receipt_link`: visible/menu_only), wpis „Co wysłaliśmy”
w menu wiadomości, surowy JSON śladu tylko w trybie eksperckim; testy: `test:context-receipt` 11/11, e2e `m1-context-sent-native.mjs` 8 kroków dwa razy z rzędu, sąsiedzi zielone. Zrzut tekstu z e2e (profil Zgodność):
„Instrukcja 19 tok. · Rozmowa 103 tok. (8 pozycji) · Pytanie 9 tok. · Puste sekcje: Pamięć, Graf, Projekt · prefiks stały: nie — ten porządek nie ma sekcji profilu · strażnik ×10: 1,4× bazy — bez pytania · koszt nieznany — brak ceny”.
**Luki jądra odkryte przez W1a (do pakietu K-następnego, nie ukryte):** (1) „Pokaż w grafie” — przycisk WIDOCZNY, ale WYŁĄCZONY z powodem słowami: `graph.navigate focus=msg:… perspectives=[sent_to_model]` zwraca `not_found`
(perspektywa i relacja `sent_to_model` istnieją w `graph_views.pack`, ale nic nie produkuje węzłów/krawędzi z paragonu); (2) paragon nie niesie ceny → „koszt nieznany — brak ceny”; (3) sekcja Graf ma jedną zagregowaną
pozycję (`graph_memory`), więc „liczba pozycji rośnie przy szerzej” nie jest mierzalna — e2e mierzy `tokens_est` sekcji (spec M1-W1 do poprawienia); (4) angielskie `why` jądra widać tylko dla pozycji bez słów w packu.
Zostaje **W1b**: podmenu kontekstu w `+` (chipy szerzej/głębiej…, lista profili, flow_text_graph, podgląd wysyłki), ContextStrip, chip modelu z pochodzeniem (`model-provenance`), retrieval-plan/ContextSlider.

## CI 260 (`9f02ef7b`) = failure (asan, 3 testy K1a) → naprawa na `main` = `59de144c` (K1b + poprawki), CI **261** w toku (start 23:16, koniec ok. 01:10)
Szczegóły odczytał tani agent (haiku, `wf_4542b027-6a2`): (1) `unit.test_context_receipt` — ASAN heap-use-after-free w teście `test_context_receipt.cpp:226`: `for (… : f.send(legacy)["messages"])`
= pułapka z CLAUDE.md (pętla po `operator[]` wyniku zwróconego przez wartość; mój skan dodanych linii K1a/K1b znalazł tylko tę jedną); (2) `unit.test_chat_active_task_concurrency` — `REQUIRE(a.ready())`
nie spełnione: `kWait` 5 s za krótki pod ASAN przy cięższej ścieżce `send` (K1a: paragon zawsze włączony); (3) `server.chat_active_task` — Timeout 90 s (64 s na dev). Poprawki: nazwana zmienna w teście,
`kWait` 120 s w obu testach `chat_active_task_*` (`wait_for` wraca natychmiast po wyniku — długi termin kosztuje tylko przy błędzie), TIMEOUT 600. **Ryzyko do zmierzenia (P-perf):** paragon „zawsze” spowalnia każdy `send`
— nie zmierzone; jeśli bieg 261 przejdzie tylko dzięki dłuższym terminom, zmierzyć koszt paragonu (sha256 + dodatkowy SELECT) na Release.
Pobranie artefaktu CI (`productionresultssa3.blob.core.windows.net`) jest zablokowane polityką sieci — logi tylko przez `get_job_logs` (tani agent czyta, ja dostaję tekst nieudanych testów).
**W1a** (`wf_e2dce102-ea6`, od 22:56) w toku w `/home/user/wt/m1d0` (push na `claude/m1-w0`); jego bramka lokalna i FF `main` po zielonym 261.

## K1b GOTOWE (22:00 UTC, `a4d45b12`; po merge main `f9f83a29`) — bramka lokalna `k1b` ZIELONA; FF `main` po końcu CI 260
K1b (`wf_b0b95e77-a0d`, 167 wywołań, 397 tys. tokenów — 3× plan): Binding.turn, nośnik tury (`TurnCarrier`, tylko następne żądanie, wartości wracają przy nieudanej wysyłce), ops `context.modify` /
`context.receipt` (POST `/api/context/modify`, `/api/context/receipt`), router (całe zdanie „szerzej/głębiej…”, „na stałe w tej rozmowie” = reguła rozmowy), strażnik ×10 w `ChatEngine::context_guard`
(zamiast `estimate.cpp` — świadome odchylenie od briefu), prawdziwe `receipt.guard`; testy (c)(d)(f)(g) zielone, 92/92 celowanych, złoty plik bajtów bez zmian.
Kształt dla W1: odpowiedź = dokument uruchomienia (`status` completed|awaiting_usage|rejected|failed; `result.data`), pytanie ×10 przez `POST /api/actions/confirm {run_id, approved, receipt_id, ref}`;
paragon `loom.context_receipt/1` (order, profile, modifiers, guard, sections[…], dropped[…], sha256, truncated). Bieg CI **260** (`9f02ef7b`, start 21:18) trwa — asan kończy ok. 23:10; **FF `main` z commita `f9f83a29`
(nie z roboczego stanu W1)** dopiero po nim.
**W1** (web, dzielę na dwa pakiety: W1a = arkusz „Co wysłaliśmy” + linia pod odpowiedzią + ⋯ › Co wysłaliśmy + API + e2e `m1-context-sent-native.mjs`; W1b = podmenu kontekstu w `+`, ContextStrip, chip modelu
z pochodzeniem, podgląd z profilem) **W1a wystartowało 22:56 UTC** (`wf_e2dce102-ea6`, Sonnet, push na `claude/m1-w0`; start zapisany w `tools/pace_state.json`).

## MAIN = `8e05e38f` (K1a, 21:00 UTC) — bramka `k1a` zielona; CI 259 w toku; K1b w toku
**Aktualizacja 21:18:** S2 zrobił FF `main` → `9f02ef7b` (K1a + jego merge: `android-smoke.yml` serwer na 24.04, `drive.mjs` sprawdza R38), co anulowało bieg 259 (miał 18 min). Docelowy bieg: **260** https://github.com/klb-t/chatadhd/actions/runs/39677626877 (start 21:18, do ok. 22:40). **Nie pushować kodu w `loom/**` na `main` do jego końca.** K1b przed FF: `git merge origin/main` do `claude/m1-w0`.
**K1b** (`wf_b0b95e77-a0d`, Sonnet, start 21:00): Binding.turn, nośnik tury + `context.modify`, `context.receipt`, routing „szerzej/głębiej” w `route.cpp`, ×10 w `estimate.cpp`, statusy w packach — push na
`claude/m1-w0`, nie na `main`. Dalej M1: W1 (web: arkusz „Co wysłaliśmy”, + › Kontekst), K2, W2, W3, A1a (S2). Tempo: tydzień 31 %, cel 22 % (+9), do punktu piątkowego 47 h → ok. 9,7 pkt/dobę.

## K1a GOTOWE (20:20 UTC, `8e05e38f` na `claude/m1-w0`) — bramka lokalna `k1a` w toku; R38 potwierdzone na emulatorze
K1a (kontynuacja `wf_1672d15c-6ce`, 85 wywołań, 209 tys. tokenów): klasy profile/rules w `build_messages`, porządek `stability`, deterministyczny prefiks profilu, paragon
`loom.context_receipt/1` w `metadata.context_trace.receipt`, selektor czyta profile; złote bajty `legacy` bez zmian (diff pliku względem `5854133a` = 0 bajtów); 78/78 testów celowanych.
Znalezione i naprawione po restarcie: test ABA historii (zmiana zapytania SQL), 3 ustawienia czytane przez `chat.send` nie zadeklarowane, opis `profile_detail_level` twierdził za dużo,
test pól ustawień nie znał stanu `partial`, test różnicowy Python↔Loom (Loom pisze paragon, Python nie → w teście paragon wyłączony). **Zostaje dla K1b:** testy (c) złoty test dwóch żądań z nośnikiem tury,
(d) „szerzej” jako zdanie → `context.modify`, (f) ×10 nad modyfikatorami (strażnik dziś `unknown_reason: not_computed_before_m1_k1b`), (g) `context.receipt` dla starych rekordów; Binding.turn w `scope_resolver`,
routing w `route.cpp`, `ops.cpp`, `estimate.cpp`, `actions.pack`. Uwaga: „Co wysłaliśmy” slim trace nie niesie `messages`, więc dzisiejsza sieć (`recordedContext`) go ignoruje — W1 to uwzględni.
**S2 potwierdził R38 na emulatorze** (android-smoke 39657173259, 3 profile, 39/0/0): odpowiedź agenta raz, słowami, bez JSON-a; serwer na 24.04 bez PPA działa.
FF `main` dopiero po końcu CI 258 (ok. 20:50) i zielonej bramce `k1a`.

**CI 258 = SUCCESS (20:39 UTC, 82 min)** — `main` = `ac50b60e` zielony (W0 + R38 + D2 + poprawka `server.security`). S2 uprzedzony: main wolny.

## CI 257 (R38) padł na `server.security` w asan; naprawione; `main` = `ac50b60e` (D2 + poprawka), CI 258 w toku (19:17 UTC)
`assert 429 in codes` — osiem razy 403 w kroku 12 testu (limit prób rejestracji). Przyczyna (odtworzona lokalnie): opóźnienie po darmowych próbach w presecie to 1 s,
potem ×2 — próby rozdzielone o > 2 s (żądanie Argon2id pod sanitizerem na obciążonym runnerze) nigdy nie dostają 429; produkt jest w porządku
(wolny serwer sam hamuje), test zależał od szybkości. Poprawka: krok 12 ustawia 120 s bazowego opóźnienia (`throttle` w profilu), przechodzi także z odstępem 2,2 s.
To trzecia klasa „test zależny od czasu” dziś (szczypanie, m1-settings, server.security) — przy następnej awarii CI najpierw pytać „czy test zależy od szybkości”.
Bieg **258**: https://github.com/klb-t/chatadhd/actions/runs/39660316270 (≈ 80–100 min, do ok. 20:50 UTC). Bramka lokalna `d2` była zielona (23 min).
**K1a** (kontekst: klasy profile/rules w `build_messages`, prefiks deterministyczny, paragon `loom.context_receipt/1`, selektor czyta profile) w toku w `/home/user/wt/m1d0`
(`wf_0202dff5-7ed`, Sonnet) — **przerwane restartem kontenera 19:45**; praca zachowana w gitcie (`5854133a` złote bajty, `98daf8b1` klasy profile/rules + paragon, `bc41a5e6` WIP, wszystko na `claude/m1-w0`); kontynuacja `wf_1672d15c-6ce` (nowy agent z briefem „co zrobione, czego szukać”, 19:52). K1b (Binding.turn, `context.modify`, `context.receipt`, ×10) po K1a. Tydzień 31 %.

## M1-D2 GOTOWE (18:10 UTC, `76c8f441` na `claude/m1-w0`) — bramka lokalna `d2` w toku
Dane kontekstu i grafu według specyfikacji (3 commity): `chat_context.pack` (klasy profile/rules, porządek `stability` jako preset, 8 `selector_profiles`),
`graph_views.pack` (rodzaj widoku, 5 perspektyw, wiązanie `touch.drag.hold` = planned), `action_rules.pack` (`context.*` planned), frazy
szerzej/węziej/głębiej/płycej + słowa „Co wysłaliśmy”, `user.pack` `profile.subgraphs`. Jedna zmiana C++ (3 linie w `chat_engine.cpp`: klasa bez części jest pusta,
inaczej przy `stability` graf poszedłby trzy razy). Karta pakietu: decyzje i cofnięcie w `INDEX.md` („M1-D2”). **Uwaga dla bramki/e2e:** dodano 5 perspektyw — testy
e2e, które je liczą, mogą się wysypać (agent ich nie uruchamiał). **Zmiana zachowania do K1:** domyślna kolejność kontekstu to `stability`; przy pustych profile/rules jest
równa dotychczasowej `stable_first` (graf na końcu) — decyzja 5 właściciela ze specyfikacji; cofnięcie: ustawienie `chat_context_order=legacy`.
Zostawione: `data/validation/product_literals.pack` (15 wejść było nieaktualnych już przed D2, nie jest w żadnej bramce — świadomie nie ruszane, połowiczna poprawka byłaby gorsza).
Po bramce: FF `main` dopiero gdy CI 257 się skończy (≈ 18:40). Dalej: **M1-K1** (jądro kontekstu: prefiks profilu deterministyczny, selektor czyta `selector_profiles`,
`context.modify`/`context.receipt`, kontrakt `loom.context_receipt/1`, ×10) w `/home/user/wt/m1d0` po bramce, jako 2 pakiety (K1a prefiks+selektor, K1b modyfikatory+rachunek);
start nie wcześniej niż ok. 19:15 (`pace.py`).

## PLAN TEMPA (decyzja właściciela 17:50 UTC) — zastępuje pytanie A/B
Połowa tygodnia (pt 23.10 20:00 UTC) → 50 %, pn 26.10 08:00 UTC → 90 %, reset wt 27.10 08:00 UTC → 100 %; agenci krótcy, na słabszych
modelach tam, gdzie kryterium odbioru jest mechaniczne. Narzędzie: `python3 tools/pace.py --used <%> --inflight <N> --model <m>`; dane:
`tools/pace_plan.json`; pomiary kosztu pakietu: tabela w `LIMITS_LEDGER.md`. **Przed każdym startem agenta uruchomić `pace.py`.**
Stan 17:33: 30 % przy celu 20 % → potrzebne tempo ≈ 9,5 pkt/dobę, czyli ok. jeden krótki pakiet na 1–1,5 h, nie kilka naraz.

## R38 NA `main` (17:18 UTC, `fb604c17`) — bramka lokalna GREEN, bieg CI 257 w toku
Pakiet R38 (wynik `agent.run` słowami, odpowiedź po „Akceptuj” w rozmowie, e2e bez brudzenia drzewa): decyzje i cofnięcie w `INDEX.md`
(„S1 / R38”). Po pełnej bramce `git status` czyste (hygiene działa). Bieg CI **257**
(https://github.com/klb-t/chatadhd/actions/runs/39642577946) ≈ 80 min, koniec ok. 18:40 UTC; **nie pushować kodu w `loom/**` na `main`
do jego końca** (S2 uprzedzony). Zadanie z kolejki „R38” zamknięte. Dalej w kolejności: M1-D2 → K1 (worktree `/home/user/wt/m1d0`
ma build dev i node_modules — wystarcza), X-Port pakiet E (UX drobiazgi), P-perf (niski priorytet), #24 `memory.attach_folder`.

## CI na `main` ZIELONY: bieg 256 (`6fa81839`) = success o 16:08 UTC (78 min) — to jest stan W0 + poprawki + sterownik smoke
Pierwszy zielony bieg po W0 (249 był przed W0; 250 padł na `data_domains`/szczypaniu/keepalive, 251 na wyścigu w `m1-settings`).
W 256 doszły do głosu także ~10 testów e2e po `m1-settings` — przeszły. `main` wolny: wolno pushować kod (każdy push uruchamia nowy
bieg ≈ 80 min i anuluje poprzedni). Próbka limitu 16:20: 5 h 0 % (okno 15:30–20:30), tydzień 30 % — bez zmiany od 13:19, bo od
godzin biegło tylko CI (poza kontem). Tydzień: linia 19,2 % (32,3 h), jesteśmy ≈ 11 pkt nad (wcześniej błędnie 20,6 %).

## CI 250 padł na dwóch testach (nie na kodzie produktu) — naprawa na `main` (13:31 UTC, `f2d519dd`)
Przebieg 250 (`7ff9231c`): (a) `dev (g++)` `test:m1-navigator`, krok mapy: szczypanie dwoma palcami dało zoom 1.000 zamiast 2.000 —
test czytał `data-zoom` zaraz po syntetycznym zdarzeniu, a React renderuje kamerę w następnej turze (przebieg 249 przeszedł ten sam test,
więc to wyścig, nie błąd mapy; lokalnie nie dało się go odtworzyć nawet przy 2× obciążeniu CPU, więc to **założenie, nie dowód**);
(b) `asan (g++)` `server.keepalive`: limit 120 s (na szybkiej maszynie 26 s). Poprawka: test czeka na kamerę (do 5 s), `TIMEOUT 600`
i 60 s na start serwera. Przebieg CI **251** (`f2d519dd`, https://github.com/klb-t/chatadhd/actions/runs/39608733048) w toku, ≈ 60–100 min
(250 trwał 98 min). **Nie pushować kodu na `main`, dopóki trwa.** Jeśli `test:m1-navigator` padnie znowu na zoomie: sprawdzić efekt
`setCam(CAMERA_HOME)` przy zmianie `result.focus.address` (`NavigatorMap.tsx:56`) — drugie możliwe wyjaśnienie.

## MAIN = `6fa81839` (14:50 UTC, FF przez S2): zawiera poprawkę `2752f372` + sterownik smoke + gate0 `nav-actions-note`
Bieg CI `loom.yml` na `main`: **256** (https://github.com/klb-t/chatadhd/actions/runs/39620297084), obok niego 254 (PR #48 S2, ten sam
SHA). Mój kontrolny bieg 255 na gałęzi anulowałem jako duplikat. **Nie pushować kodu na `main` do końca 256** (spodziewane ok. 16:30 UTC,
job `dev` z końcówką e2e ok. 15:35–15:50). Pierwszy bieg po W0, w którym dojdą do głosu ~10 testów e2e po `m1-settings`.
**Drobiazg do wejścia przy najbliższym pushu kodu (propozycja S2, słuszna):** e2e w bramce przepisuje wersjonowany
`loom/web/tests/shell/fixtures/graph-card-temperature.json` (zmienia się tylko `scope_id`: świeże `c_…` w każdym biegu) i
`e2e/screenshots/*.png` — drzewo po bramce jest brudne. Poprawka: maskować `scope_id` przy zapisie fixture albo zapisywać go tylko za
flagą środowiskową; zrzuty pisać do `M1_SHOTS_DIR`/katalogu poza drzewem, gdy ustawiony.

## CI 251 (main `f2d519dd`) = failure, jedna przyczyna: kolejny wyścig w e2e (14:50 UTC)
Zepsuty tylko job `dev (g++)`, krok `test:m1-settings`, „Modele › Role": `data-role-open` było `null` zamiast `'1'`
(`m1-settings-native.mjs:213`). Przyczyna (znaleziona przez S2 na CI jego PR #48, potwierdzona w kodzie): `RolesTable` zgłasza otwarcie
karty z `useEffect` (`RoleCard.tsx:57`), a `SettingsHuman.tsx:145` ustawia atrybut klatkę później — test czytał go raz, zaraz po karcie.
Poprawka: `waitFor` na `[data-role-open="1"]` — commit `2752f372` na `claude/m1-w0` (2× zielony lokalnie), **jeszcze nie na `main`**:
S2 dołącza go do swojej gałęzi (PR #48) i jego FF `main` ma być jedynym pushem, żeby nie anulować kolejnego biegu. Poprawka
szczypania z `f2d519dd` zadziałała (m1-navigator przeszedł), `asan`/`server.keepalive` bez zarzutu. Wzór: pierwsze pojawienie się testu
na wolnym runnerze ujawnia jednorazowe odczyty atrybutów ustawianych z efektów React — przy następnej awarii e2e najpierw sprawdzać to.

**Kontrolny bieg na gałęzi (14:45 UTC):** `loom.yml` przez `workflow_dispatch` na `claude/m1-w0` (`2752f372`), przebieg **255**
(https://github.com/klb-t/chatadhd/actions/runs/39619628946) — inna grupa współbieżności niż `main`, więc nie anuluje i nie jest
anulowany przez FF S2. Po co: w 251 job `dev` zatrzymał się na `test:m1-settings`, więc ~10 następnych testów e2e (chat-first …
product-integration, interface-2) na CI jeszcze **nie biegło** po zmianach W0 — 255 pokaże je, zanim S2 puści `main`. Wynik joba `dev`
spodziewany ok. 15:30–15:45 UTC, całość ok. 16:30. Sprawdzać tylko `get_workflow_run`; porażkę czytać `get_job_logs failed_only`.

## Wynik S2 z 13:59 UTC: Bramka 0 na emulatorze ZIELONA (android-smoke 39605980228, `ed380fbb` = main f85a8ddc + sterownik)
Trzy profile (pixel_5 1,0 / pixel_6 1,0 / pixel_6 1,3): 39 działa, 0 nie, 0 pominięte. Krok 1 (⋮ → Graf → ROM): pierwsza odpowiedź
nawigatora w ≈ 0,3 s od dotknięcia, `nav-chip=13`; wcześniejszy timeout 30 s na dwóch profilach **nie powtórzył się** (ekran z tamtej
awarii przepadł, bo bieg został zastąpiony) — S2 zgaduje jednorazową niegotowość. Cztery stare kroki (model, zapis ustawienia, wiedza,
stronicowanie) działają; to były zmiany sterownika po W0, nie błędy aplikacji. Wniosek dla P-perf: **niski priorytet, nie blokuje
bramki 0**; zostają moje pomiary 1,1–1,9 s na `graph.navigate` w Debug (brak bufora źródeł projekcji). S2 puszcza teraz własną bramkę
+ draft PR + FF `main`. **Uwaga:** jego pliki `loom/android/smoke/*.mjs` pasują do `paths: loom/**` w `loom.yml`, więc FF `main`
zanim 251 się skończy anuluje 251 — poprosiłem S2, żeby poczekał.

## Wyniki S2 z emulatora (13:10 UTC, android-smoke 39598360743, pixel_5/pixel_6/pixel_6+font 1,3)
Kroki 2–8 Bramki 0 działają na wszystkich trzech profilach (menu, ustawienia, karta roli, Wstecz, dolny pasek, menu perspektyw,
karta ROM bez łańcucha). **Krok 1 (⋮ → Graf → ROM) pada na pixel_5 i pixel_6 przy czcionce 1,0: timeout 30 s na `nav-rows`** (przy 1,3
przechodzi) — czekamy na widoczny ekran z nieudanego kroku (S2: 79ea5299). Moje pomiary: `graph.navigate` ≈ 1,1–1,9 s na szybkiej maszynie
w Debug, **każde wywołanie**, także ciepłe (brak buforowania źródeł projekcji); na emulatorze x86_64 może być wielokrotnie wolniej.
**Do zrobienia (pakiet P-perf, po decyzji z limitem tygodniowym):** profil `graph.navigate` (które źródła są czytane przy każdym
wywołaniu: pakiety danych, sumy kontrolne spisu domen?), bufor źródeł projekcji, pomiar na Release; w teście S2 pierwszy czas odpowiedzi
w raporcie. Uwaga UX: nazwa „Self-model" występuje w korzeniu (obszar) i w ROM (pozycja katalogu) — do rozstrzygnięcia z właścicielem.

## WYDANIE M1-W0 GOTOWE (12:15 UTC): `package.yml` przebieg 39593331749 = success (Android + serwer + first_steps)
APK (klucz testowy): artefakt `chatadhd-android-apk-TEST-KEY` 12342808211; pełny zip `chatadhd-0.10.00-ci.22.7ff9231` 12342868186;
serwer linux x64/arm64; zrzuty z przejścia kroków pierwszych. Właściciel dostaje link i raport `M1_CHECKPOINT_0.md`.
CI `loom.yml` na `main` (przebieg 250, `7ff9231c`) w toku — nie pushować na `main` do końca. S2 puścił własny android-smoke
z gałęzi `claude/s2-parallel` (matryca pixel_6 / pixel_5 / pixel_6+font 1,3) — raport z dosłownymi tekstami oczekiwany.

## M1-W0 NA `main` (11:40 UTC, `7ff9231c`) — GATE GREEN
Bramka lokalna (ctest + guard + web + wszystkie test:* + e2e) zielona, W0 + poprawka logowania + `main` wchodzą jednym FF.
S2 poinformowany (kroki Bramki 0 z `M1_W0_TESTIDS.md`, 5 kroków). `package.yml` na `main` (przebieg 39593331749: Android
+ serwer + first_steps) w toku — po nim link do APK dla właściciela. CI `loom.yml` na `main` dla `7ff9231c` w toku:
**nie pushować na `main` do jego końca.**
Lekcja: zadania w tle odpalone przez `nohup` giną, gdy kontener zasypia między turami (restarty ≈ 09:38, 10:15, 11:06);
długie zadania uruchamiać jako `run_in_background` narzędzia Bash (śledzone, budzą sesję po zakończeniu).

## (zamknięte) bramka W0 — drugie podejście
Pierwsze podejście (09:31) zabił kolejny restart kontenera (≈ 09:38 i ponownie ≈ 10:15); zdążyło pokazać prawdziwy błąd:
W0 (poprawka r2:b4) zmieniło `graph/scopes.pack` bez zaktualizowania sumy w `data_domains.pack` — `unit.test_kb_pack` padał.
Naprawione commitem w `claude/m1-w0`. Drugie podejście `gate.sh` wystartowało 10:18, odpalone przez `setsid`.
(Dalej jak niżej.)

## (stare) bramka W0 w toku
M1-W0 skończone (60 agentów, dwie rundy recenzji, 53 commity, 31 znalezisk w rundzie 1, naprawy partiami) i scalone
z `claude/fix-login-drop` oraz `main` w `/home/user/wt/m1d0` (head po scaleniu wypchnięty na `claude/m1-w0`).
`gate.sh /home/user/wt/m1d0 m1w0` uruchomiony ok. 09:31 (log `scratchpad/wp/gate-m1w0.out`); wynik sprawdza zaplanowane
zadanie. Po zielonej: FF `main`, S2 → kroki Bramki 0 z `M1_W0_TESTIDS.md`, `package.yml` na `main`, link do APK dla właściciela.
Otwarte po rundzie 2 (nieblokujące): pusty nagłówek „Skąd jest wartość" na kartach, które nie są ustawieniem;
zrzut „Ta rozmowa w grafie" na pustej rozmowie. CI `main` zielone (przebieg 249).

## Restart kontenera (≈ 05:20 UTC) — co się stało
Dysk przetrwał, procesy nie. Zatrzymane workflowy: W0 (był w naprawach po recenzjach, partia r1:b1) i poprawka
logowania (krok naprawczy po recenzji). **26 commitów W0 i 5 commitów poprawki logowania leżało tylko lokalnie**
— wypchnięte ręcznie zaraz po restarcie (`claude/m1-w0`, `claude/fix-login-drop`), niedokończone edycje zapisane jako
commity WIP. Oba workflowy wznowione z dziennika (`resumeFromRunId`: pakiety i recenzje z pamięci podręcznej,
tylko krok naprawczy leci od nowa i dostał notkę o WIP). To jest konkretny dowód za hakiem checkpointów (pytanie
do właściciela nadal otwarte).

## W toku (3 agenci naraz — tyle mieści budżet okna, `LIMITS_LEDGER.md`)
- **M1-W0, pakiety** — gałąź `claude/m1-w0`, worktree `/home/user/wt/m1d0`, skrypt
  `tools/workflows/m1-w0-packages.js`. P1 (menu, Wstecz), P2 (lista nawigatora), P3 (karta węzła),
  P4 (mapa) gotowe; P5 (ustawienia po ludzku) w toku; dalej P6 (karta roli), P7 (e2e, zrzuty,
  raport, testid-y dla S2), potem recenzje po kolei. **Po zakończeniu:** scalić `main`, bramka
  `gate.sh /home/user/wt/m1d0 m1w0` (≈ 40 min), przywrócić `loom/web/e2e/screenshots`, FF `main`,
  powiedzieć S2, że `docs/reports/M1_W0_TESTIDS.md` ma blok `gate0`, zbudować wydanie
  (`package.yml`), dać właścicielowi link do APK + `docs/reports/M1_CHECKPOINT_0.md`.
- **Poprawka zrywania logowania — GOTOWA po przeglądzie** (`claude/fix-login-drop`, head `76d3bbaa`, wypchnięta; 9 commitów):
  przyczyna w teście (zamrożona pętla zdarzeń `spawnSync` zostawia w puli gniazdo, które serwer zamyka po 5 s),
  serwer dostał ustawienie `--keep-alive-timeout-s`, `createAccount()`/`runChild()` w harnessie, test `stale-connection`.
  Review znalazł też, że agent W0 zabijał `pgrep -x loom-server` na całej maszynie (reguła dopisana do CLAUDE.md).
  **Do zrobienia:** gdy skończy się krok naprawczy W0 (żeby bramka nie ścigała się z jego serwerami):
  `gate.sh /home/user/wt/extract fixlogin` (≈ 40 min), przywrócić `loom/web/e2e/screenshots`, merge `main`
  (INDEX.md będzie miał konflikt na końcu pliku — zostawić obie sekcje), FF `main` **dopiero po zakończeniu
  przebiegu CI 249**, żeby go nie anulować.
- ~~Poprawka przerywanego zrywania logowania~~ (stary wpis) — gałąź `claude/fix-login-drop`, worktree
  `/home/user/wt/extract`, 2 commity; po zakończeniu bramka i FF `main` **przed** bramką W0
  (inaczej bramki bywają losowo czerwone).
## Zrobione od ostatniej wersji
- **CI na `main`:** przebieg PR nr 247 padł tylko na `asan` (`unit.test_graph_projection`, heap-use-after-free:
  pętla po `.at()` tymczasowego obiektu, błąd w teście K0, nie w jądrze; diagnoza S2 z logu). Poprawka na `main`
  (`f76e66f3`). **Wiążący jest przebieg 248 (push na `main`)**, asan trwa ok. 80 min (start 04:23); sprawdzenie
  zaplanowane ok. 05:45. Reguła dopisana do CLAUDE.md (sekcja Loom).
- **X-Port Android, paczka D** (head `e150c3aa`, build 39543074478 = success, artefakt `xport-apk` 12320695343):
  przepisy i edycje widzą zaznaczenie, oryginalne nazwy w `preserve/`, katalogi rozmów bez ułamków sekund,
  poprawione grupowanie radio i gramatyka; S2 powtarza test. Znalezisko do decyzji właściciela: nazwy plików
  `text/0001-<hash>.md` zamiast tytułów (profil domyślny; propozycja: placeholder `{conversation.name}`).
- **CI na `main` czerwone od 17.10 (presety bez libloom) — naprawione** (`9a743b4f`, wersja S1 została na
  `main` po merge S2, `f3c4adb5`). Przebiegi push 245/246 anulowane przez nowsze; wiążący jest przebieg PR
  nr 247 (S2) na `f3c4adb5` — wynik sprawdzić; jeśli czerwony, czytać logi.
- **Poprawka logowania:** przyczyna była w teście, nie w serwerze ani magazynie kont (3 commity na
  `claude/fix-login-drop`); trwa przegląd. Po nim bramka i FF `main`.
- **S2 wprowadził na `main`:** R38 na Androidzie, smoke X-Port na emulatorze, kroki Bramki 0 jako dane
  z `docs/reports/M1_W0_TESTIDS.md` (plik pojawi się na `main` ze scaleniem W0).
- **X-Port Android, paczka C** (gałąź `claude/xport-android`, head `fbe55b85`, build `xport-apk.yml` przebieg
  39538587463 = success, artefakt `xport-apk` 12318204849). **Przyczyna błędu „zip bez katalogów":** pakiet roboczy
  powstawał z zaznaczenia przy pierwszym użyciu (np. Podgląd przepisu przed zaznaczeniem = pusty pakiet), późniejsze
  zaznaczenie do niego nie trafiało, eksport ciął pusty pakiet; poprawka `_align_package`. Zdanie po polsku
  „co zniknie / co zostanie" przy przepisach (mierzone, nie zgadywane), przepisy jako widoczna lista, komunikat
  po eksporcie z użytymi profilami i liczbą katalogów rozmów. S2 ma powtórzyć dokładnie kolejność, która dała błąd.
  **Znane luki:** zaznaczenie częściowe (część wiadomości rozmowy) eksportuje całą rozmowę, jeśli była w pakiecie;
  Ponów i zwykła edycja seryjna nie mają jeszcze zdania po polsku; lista kluczy w zdaniu ograniczona presetem 6.

## X-Port Android: scalony na `main` (05:15 UTC)
- `claude/xport-android` wszedł na `main` po: emulator S2 18/18 (przebieg 39543420567), budowanie samego APK X-Port
  (39543074478), `package.yml` Android z modułem `:xport` (39545189617, success), 290 testów silnika. CI `loom.yml` na
  `main` po tym pushu: sprawdzić (zaplanowane jest sprawdzenie przebiegu 248 ok. 05:44, ten nowy przebieg dojdzie).
- **Paczka E (drobiazgi UX od S2, do zrobienia po scaleniu):** podwójna interpunkcja „…, raw_pointer, ….” w zdaniach;
  wewnętrzne nazwy pól (raw_pointer, created_utc…) zastąpić polskimi etykietami z danych; dopisać „N wiadomości
  bez zmian"; nazwy `text/0001-<hash>.md` → z tytułem (placeholder `{conversation.name}`, DECYZJA WŁAŚCICIELA:
  zmienia układ profilu domyślnego); zaznaczenie częściowe (część wiadomości rozmowy) nadal eksportuje całą rozmowę.

## Czeka na wolny worktree z buildem jądra (dysk 7 GB: drugi build jądra się nie mieści)
Kolejność po zakończeniu poprawki logowania i jej bramce: worktree `/home/user/wt/extract` (build 2,1 GB)
przestawić na nowy `main` i użyć go kolejno do: R38, potem M1-D2 → M1-K1.
1. **R38 — dwie uwagi S2** (pliki S1): (1) wiadomość wyniku `agent.run` niesie cały JSON pętli
   w treści obok bloku Akceptuj/Odrzuć — `body_of` ma pokazać tekst/propozycje; (2) po „Akceptuj”
   końcowa odpowiedź agenta (tekst wznowionego przebiegu) nigdzie nie trafia do rozmowy. Poprawka
   po stronie web już u S2 (`50239c94` na `claude/s2-parallel`).
2. Zadanie #24 `memory.attach_folder` (po retargetowaniu R39 na graf), #19 P37 „jeden silnik”,
   M1: D2, K1, W1, K2, W2, W3, A1 (S2), T1, DOC; etap replikacji dostawców po M1.

## Czeka na właściciela (tak/nie)
- Hak automatycznego wypychania gałęzi po commicie: `tools/install_checkpoint_hook.sh` (nieaktywny).
- Bezpośrednie połączenie z Google/Lyria.
- Publikacja GitHub Release.
- P39 §9: (1) wgląd w Twoje eksporty lokalnie dla E-A1 i T-R2 (do repozytorium tylko cytaty do Twojej
  akceptacji i liczby zbiorcze) — tak/nie; (2) trzecia sesja S3 dla torów D/E — tak/nie; (3) plik
  `face_embedding_research_master_prompt_2026-10-19.md` do `docs/architecture/`; (4) model estymatora
  `turn_policy_estimator_model`, jeśli chcesz trzecią politykę (koszt pokazany przed wywołaniem).

## Odnośniki
Zasady kontroli marnotrawstwa: `docs/reports/INDEX.md` („S1 — kontrola marnotrawstwa”), dziennik
okna: `docs/reports/LIMITS_LEDGER.md`, skrypty: `tools/workflows/`, spec M1:
`docs/architecture/M1_USTAWIENIA_JAKO_GRAF_2026-10-16.md`.
