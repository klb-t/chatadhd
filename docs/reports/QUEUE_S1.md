# Kolejka S1 — stan 2026-10-21 12:15 UTC

Po co ten plik: zasada 1 (trwałe checkpointy) — kto przejmie pracę po przerwie sesji, ma tu
wszystko, czego kontekst nie przeniesie. Aktualizowany przy każdym zakończeniu pakietu.

## PILNE, następny wolny agent (Sonnet): gość nie może po cichu trafiać na Vertex (właściciel potwierdził, 30.10)
Stan z 30.10: na produkcji nie ma `/var/lib/chatadhd/.config/chatadhd/keys.env`, więc gość dostaje `vertex_gemini`
(`best_in_table`). `guests.model_sources` w `server_accounts.pack` (`[byok,pool,free,local]`) nie ma czytelnika: serwer tylko
wysyła go w `/api/auth/me`. Wyborem steruje `free_model_choice` w `free_providers.pack` (`tier_order [free, paid_credits]`).
Doraźna nakładka dla koordynatora: `free_providers.pack` z tabelą bez Vertexu.
Zakres pakietu:
1. Preset `model_sources` → `[byok, free, local]`. Pula tylko przy świadomym wypróbowaniu płatnych funkcji (R104), z czytelnikiem
   w kodzie: kolejność ma sterować wyborem.
2. Poziom `paid_credits` nie wybiera się automatycznie, tylko po zgodzie gościa (ustawienie z presetem).
3. Test: gość bez własnego klucza nie trafia na pulę, gdy działa darmowy dostawca.
4. Test: bez darmowych kluczy jest jawny stan i komunikat z drogą do własnego konta (R108), bez Vertexu.
5. Aktualizacja `BEZ_LOGOWANIA_R103` §4.1.
Budżetu 1 €/dzień na testy użytkowników właściciel nie ustalił. Preset `vertex_gemini.budget.credits.per_day = 1.0` już istnieje;
nie dopisywać nowego.
Decyzje koordynatora (30.10, później):
(a) Kwota 1 € zostaje na razie jako bezpiecznik. Pakiet ma sprawić, że `paid_credits` nigdy nie wybiera się samo.
(b) Notę wpisu Vertex „presets from the owner's spending rule (R103/R104)” poprawić na datowany odczyt. Właściciel nie ustalił
budżetu na gości, więc nie opisywać tego jako jego decyzji.
(c) Ten pakiet idzie jako następny, przed kolejnym projektowym.
(d) Nakładka `free_providers` na produkcji na razie nie jest stosowana: właściciel ma wyłącznik gości.
(e) Kluczy nie szukać po dysku.
(f) R111 (właściciel, 30.10; spec `a815fafc`): pula próbna tylko dla osób z kontem. Warunek tego pakietu: gość bez konta nigdy
nie dostaje puli, także po zgodzie (test). Zależność: dziś `registration: closed`, więc do otwarcia rejestracji pula jest dla nowych
osób niedostępna. Powiadomienie właściciela o zużyciu i ruchu (progi w danych) zaprojektować razem z R109, nie w tym pakiecie.
(g) R112 (właściciel, 30.10, uściślenie R111): gość bez konta ma tylko źródła bezkosztowe, także nasze darmowe klucze. Osoba z darmowym
kontem dostaje dodatkowo kilka groszy z płatnych źródeł lub kredytów, wszyscy razem do ≈ 1 € dziennie. 1 € to preset i skala
właściciela. W tym pakiecie: rozdzielić w danych pulę bezkosztową i pulę płatną. Notę przy `vertex_gemini.budget` zastąpić cytatem R112,
zamiast „datowanego odczytu” z punktu (b). Grosze na osobę to wartość otwarta z presetem. Test: gość nie dostaje puli płatnej
w żadnym stanie.
(h) R113 (właściciel, 30.10): ≈ 1 € dziennie dla kont to wartość startowa. Zgoda z góry na podniesienie przy pierwszej potrzebie;
nową kwotę zgłosić właścicielowi. W nocie presetu napisać, że to wartość startowa. Gość dalej bez płatnych źródeł.

## Po poprawce źródła gościa (Sonnet): język strony i niderlandzki (R114, właściciel 30.10; spec `62f9f028`)
Zgłoszenie: telefon ma język polski, a strona pokazuje angielski. Do tego wersja niderlandzka (nl) dla szerszego grona testerów.
Tropy z kodu (30.10, `2d39b07c`), do sprawdzenia na prawdziwej stronie, bez zgadywania:
(a) Serwer ustawia gościowi `ui_language` raz, przy starcie sesji, z pierwszego języka `Accept-Language` (`server/src/app.cpp` ≈ 2392).
Potem `config.ui_language` wygrywa z `navigator.languages` (`web/src/App.tsx:437`, `shell/language.ts`). Jeśli przeglądarka wysyła
`en` jako pierwszy (Chrome na Androidzie ma własną listę języków), gość zostaje na zawsze po angielsku. Preset pola to „auto”.
Rozważyć: start gościa zapisuje „auto” albo całą listę z wagami, a wybór idzie przez `shellLanguage` z wszystkimi tagami.
(b) Kopia gościa przywracana z przeglądarki (B1b) może nieść stare `ui_language`.
(c) `AuthGate.tsx:22` i `GuestLayer.tsx:194` biorą tylko `navigator.language`, bez listy.
(d) Teksty wpisane na stałe, na przykład półka „Conversations”: znaleźć wszystkie i przenieść do katalogów.
Zakres: naprawić wybór, z testem e2e dla Chromium z `locale` pl, en i nl oraz z różnymi `Accept-Language`. nl jako same dane,
lista języków otwarta: test, że nowy język w danych działa bez zmiany kodu. Tłumaczenie maszynowe na Sonnecie, w karcie oznaczone
„maszynowe, nieczytane przez człowieka”.

## Luka do zaprojektowania (właściciel, 30.10; przekazał koordynator): zapytania do modeli z urządzenia osoby
Słowa właściciela: „apka webowa operuje na urządzeniu klienckim i z niego wysyła zapytania”.
Obserwacja z 30.10 na `2d39b07c`: w `loom/web/src` nie ma żadnego adresu dostawcy ani `fetch` na adres zewnętrzny, więc
każde zapytanie do modelu idzie przez serwer i naszą pulę.
Projekt na Fable: osoba trzyma własny klucz na urządzeniu, a przeglądarka łączy się wprost z dostawcą, tam gdzie dostawca na to pozwala.
Do tego logowanie OAuth do dostawcy (przykład: OpenRouter). Pula serwera zostaje jako start. Dostawcy to przykłady: mechanizm ma
być ogólny. Co dostawca dopuszcza (zapytania z przeglądarki, OAuth, przepływ PKCE) zapisujemy jako datowane obserwacje
z oceną źródła, nie jako ograniczenia. Wybór trasy dla każdego dostawcy i modelu to ustawienie z presetem.
Do sprawdzenia w projekcie: gdzie leży klucz w przeglądarce (OPFS/IndexedDB, szyfrowanie, PH29), paragon RI-2 i potwierdzenie ×10
bez serwera, pierwsze wysłanie do nowego dostawcy. Nie przerywać bieżących agentów; ruszy w następnej partii.
Wymaganie R108 (właściciel, 30.10, dosłownie): „trzeba na początku użytkownika zachęcać zdecydowanie do utworzenia darmowego konta
poprzez jedno kliknięcie żeby nie korzystać z naszych limitów, bo jesteśmy na etapie Bootstrap”. Zapisane w spec na gałęzi dokumentów
(`37fa465f`). Odczyt koordynatora, do projektu: chodzi o darmowe konto u dostawcy modelu, podłączane jednym kliknięciem przez OAuth.
Zachęta jest wyraźna, ale aplikacja działa dalej bez konta. Moment i siła zachęty to preset w danych, z „nie pytaj więcej”. Komunikat
uczciwie mówi, że z kontem osoba ma własny limit, a jej zapytania idą wprost do tego dostawcy.
W tym samym pakiecie R109 (właściciel, 30.10, „na przykład”; spec `9c4b326d`): stopnie zużycia wspólnej puli przez jedną osobę.
Przykład właściciela: 10 % informacja, 25 % czerwone ostrzeżenie, 50 % odcięcie, a przy większej liczbie użytkowników niższe progi.
Progi to otwarty zbiór w danych, każdy z własną akcją. Zależność od liczby użytkowników to formuła-preset. Odcięcie dotyczy tylko puli
i prowadzi do konta jednym kliknięciem (R108). Rozszerzyć istniejące `guests.protection` (`server_accounts.pack`) i
`server/src/guest_guard.h` oraz `/api/pool/summary`. Dziś są tam tylko twarde odmowy z progami bezwzględnymi, na sesję, adres
i dzień puli, bez udziału osoby w puli.

## Luka (koordynator, 30.10): strona nie zapisuje, dlaczego się przeładowuje
Po poprawce pętli przeładowań gościa (`hotfix-reload`, `e94ba029`) nadal nie widać, co wywołało przeładowanie.
Do zrobienia: zapis zdarzeń klienta (przeładowanie i jego powód, przywrócenie kopii, błąd zapisu magazynu) do przeglądania
przy debugowaniu. Rozmiar, zakres, poziom i to, czy zdarzenia trafiają do serwera, to ustawienia z presetami. Wyłączenie zapisu też
jest ustawieniem. Wysłanie na serwer tylko wtedy, gdy osoba je włączy. Do następnej partii.
Proces partii (koordynator, 30.10): scalenie → niezależny przegląd poprawności na Fable (sam diff) → poprawki na Sonnecie → pełna bramka → sha do koordynatora.

## Partia nocna 29/30.10 na `w2b2` — BRAMKA ZIELONA (30.10, `e81c00c1`)
Higiena testów (`ec84aacd`), wtyczka OPFS (PH29 P1) i wolny start gościa scalone w `w2b2`. Pełna bramka na `e81c00c1` jest zielona:
ctest 276/276, web 121/121. Po bramce doszedł tylko dopisek w dokumentacji (`48b581df`); gałąź jest wypchnięta na `origin/w2b2`.
Pierwsze podejście bramki było czerwone tylko przez środowisko: po restarcie maszyny zabrakło venv w PATH. Bramkę uruchamiaj z
`PATH=$HOME/.venvs/loom311/bin:$PATH`. Następny krok, decyzja koordynatora: scalić `w2b2` dalej (`claude/m1-w1` jest 16 commitów w tyle).
Czeka na koordynatora: wiadomość od sesji „ekosystem” — karta PRZEKAZANIE_ASTRA odświeżona (`bee0131`); R107 (budzik i usypianie
maszyny) niewdrożony; E4.8d — tłumaczenia stron WatchDoga; PY312-1 w AGEDS przyjęte.

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
