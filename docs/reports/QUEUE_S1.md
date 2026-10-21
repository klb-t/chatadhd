# Kolejka S1 — stan 2026-10-21 05:30 UTC

Po co ten plik: zasada 1 (trwałe checkpointy) — kto przejmie pracę po przerwie sesji, ma tu
wszystko, czego kontekst nie przeniesie. Aktualizowany przy każdym zakończeniu pakietu.

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
- **Poprawka przerywanego zrywania logowania** — gałąź `claude/fix-login-drop`, worktree
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
