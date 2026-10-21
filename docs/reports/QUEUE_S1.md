# Kolejka S1 — stan 2026-10-21 14:55 UTC

Po co ten plik: zasada 1 (trwałe checkpointy) — kto przejmie pracę po przerwie sesji, ma tu
wszystko, czego kontekst nie przeniesie. Aktualizowany przy każdym zakończeniu pakietu.

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
