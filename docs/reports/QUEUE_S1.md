# Kolejka S1 — stan 2026-10-22 17:05 UTC

Po co ten plik: zasada 1 (trwałe checkpointy) — kto przejmie pracę po przerwie sesji, ma tu
wszystko, czego kontekst nie przeniesie. Aktualizowany przy każdym zakończeniu pakietu.

## APK v2 DOSTARCZONY właścicielowi (17:02 UTC): `package.yml` run 39815413284 = success (16:50, 14 min), sha `f07fc52a` (CI 271 zielony; smoke S2 39801974892 42/0/0 ×3)
Artefakty: `chatadhd-android-apk-TEST-KEY` (17,8 MB, sha256 27818dd7…), `chatadhd-server-linux-x64/-arm64`, `first-steps-screenshots`, `chatadhd-0.10.00-ci.24.f07fc52`. Raport `M1_CHECKPOINT_1.md` (identyczny z commitem APK) wysłany jako plik.
## K2b-1 GOTOWE (16:58 UTC, `aa45db5b` na `claude/m1-w0` na wierzchu `5b0e04ff`): akcje `profile.interview` / `profile.show` / `profile.set` + `profile.confirm`, tryb formularza — bramka lokalna `k2b1` w toku (start 17:00)
Agent Sonnet (`wf_c9a232c9-64a`, 102 wywołania, 312 tys. tokenów, 41 min). Logika w `loom/src/actions/profile_ops.{h,cpp}`, dodatkowa akcja `profile.confirm`; odpowiedzi po polsku (start → pytanie o poziom z trzema podglądami; odpowiedź z uwagą „nie pytaj o zdrowie” → „Tak zrozumiałem …” bez zapisu aż do potwierdzenia; review jako propozycja; never; status; show z `asked_at_level`; set z bramką potwierdzenia, odmowa dla „nigdy”). Bez modelu i bez sieci (test pilnuje). Łańcuch rejestru zrobiony (interfaces.pack, rom.pack, pin sha).
Do dopracowania (moja krytyka odpowiedzi agenta, do W2/K2b-2): widoczne zdania zawierają wewnętrzne nazwy („Minimalnie (minimal)”, „ustawieniu profile_detail_level”, „erase=true”) — to żargon na ekranie rozmowy; schować w danych/„Szczegóły techniczne”. K2b-2: gałąź `metadata.flow` w `ChatEngine::send`, ścieżka modelu (privacy_decision + koszt), odświeżanie starych scenariuszy, `by_category`. W2: ekran „Ty”, karta „Tak zrozumiałem”, wywiad w oknie rozmowy.
FF `main` ← `aa45db5b` po zielonej bramce `k2b1` I po końcu CI 272 (kontrola 18:13).

## CI 271 (`f07fc52a`) = SUCCESS (16:19, 71 min); APK v2 w budowie: `package.yml` run 39815413284 na `f07fc52a` (start 16:36, kontrola 16:59); `main` = `5b0e04ff` (K2a + fix, bramka `k2a2` zielona) od 16:36, CI 272 (id 39815457361), kontrola ≈ 18:11
APK v2 = commit z zielonym CI + smoke S2 42/0/0 ×3 profile (FLAKE-1 potwierdzone). Właściciel dostanie: link do biegu + `M1_CHECKPOINT_1.md` + uczciwą listę (nowe: polskie powody, jedna liczba z bazą, „Pokaż w grafie” działa, naprawione losowe „Ustawienia się nie otwierają”; brak: cena, szacunek pustej wiadomości w arkuszu, załączniki, ekrany wywiadu „Ty”, przeciąganie W3).

## FLAKE-1 POTWIERDZONE NA EMULATORZE (S2, 16:00): android-smoke 39801974892 na `f07fc52a` = 42/0/0 na pixel_5 1.0, pixel_6 1.0, pixel_6 1.3; 0× „second attempt after”, 0× „settings did not open” (poprzednie 2 biegi na starym kodzie: losowa porażka na 2 z 3 profili)
`main` = `f07fc52a` (FLAKE-1 + K1c-4 + sterownik S2; FF zrobił S2 o 15:08 i anulował CI 270); CI 271 (id 39801969743) w toku od 15:08, kontrola 16:35. S2: „f07fc52a gotowe na APK v2, gdy loom.yml zielony”.
**K2a + K2a-fix: bramka `k2a2` ZIELONA (16:17, `5b0e04ff` = gałąź scalona z `main`)**; K2a-fix (`wf_0a950ca9-11e`, 40 wywołań, 123 tys. tokenów, 21 min): przyczyna czerwonej bramki `k2a` to 4 NOWE pola scenariusza z K2a bez etykiet w `slot_labels` (poprawiam własne wyjaśnienie: nie „starsze pola” — scenariusz miał wcześniej 9 pól);
rzut party:self zawsze wymieniał wszystkie pola (spec: sloty ze stanami, także „nieznane”), więc zachowanie bez zmian, dodane 4 etykiety pl/en + 2 testy-strażniki. Uwaga do K2b: slot powyżej wybranego poziomu pokazuje się jako „nieznane”, choć nigdy nie będzie zadany.
**K2b-1 wystartowało 16:17** (`wf_c9a232c9-64a`, Sonnet): akcje `profile.interview` / `profile.show` / `profile.set` nad istniejącym `OnboardingStore`, tylko tryb formularza (bez modelu), uwagi jako propozycje do potwierdzenia, `asked_at_level` w `profile.show`.
FF `main` ← `5b0e04ff` (K2a + fix) po końcu CI 271 (i po `git fetch` — S2 pcha na main). Potem APK v2: z commitu z zielonym CI; zdecyduję, czy z `f07fc52a` (FLAKE-1 potwierdzone na emulatorze) czy z kolejnego.

## `main` = `1ee7a6c8` (FLAKE-1) od 15:06; CI 269 (`b1b34f17`) SUCCESS (14:39, 124 min — asan wolny runner); CI 270 (id 39801749697) w toku, kontrola 16:35. Bramka `k2a` CZERWONA na jednym kroku (to samo audytem tekstu z K1c-4 — dobrze, że istnieje) → K2a-fix
Bramka `k2a` (`67c235d3`): jedyna porażka `e2e/m1-navigator-native.mjs` „no screen shown so far has developer text”: na ekranie „Ty” (party:self) cztery wiersze pokazują surowe id pól: `interview.detail`, `health.needs`, `relations.people`, `routine.rhythm`
(+ skrócone etykiety na mapie). Przyczyna: `slot_labels` w `graph_sources.pack` (klasa `party_self`) mają etykiety dla 9 z 13 pól `scenario.pack`; brakuje czterech (w tym nowego `interview.detail` z K2a). **K2a-fix** (`wf_0a950ca9-11e`, Sonnet, start 15:07): etykiety pl/en dla czterech pól + test-strażnik (każde pole scenariusza ma etykietę pl i en; sterowany danymi) + wyjaśnienie, czemu trzy starsze pola nie były wcześniej zgłaszane (czy K2a zmieniło to, co rzutuje party:self) + regeneracja profili + e2e navigator 2×.
S2 poinformowany: smoke na `1ee7a6c8` (potwierdzenie FLAKE-1 na emulatorze). APK v2 po zielonym CI 270 i wyniku S2. FF K2a (+fix) po zielonej bramce `k2a2` i po końcu CI 270.

## K2a GOTOWE (14:15 UTC, `67c235d3` na `claude/m1-w0`, na wierzchu FLAKE-1 `1ee7a6c8`): jądro wywiadu „Ty” — pytania PL, poziomy, uwagi — bramka lokalna `k2a` w toku (start 14:16)
Agent Sonnet (`wf_a8c61ce6-7fe`, 125 wywołań, 292 tys. tokenów, 26 min). (1) pytania i tytuły sekcji `{en,pl}` (brak pl = błąd testu; zostaje jawny `fallback:true` przy brakującym języku), (2) pierwsze pytanie „ile pytań możesz usłyszeć” + `detail_levels` minimalnie/zwykle/szczegółowo (przykłady z listy właściciela; progi 1/1, 2/2, bez limitu), podglądy
z liczbami liczonymi z prawdziwego profilu (test sprawdza zgodność), `plan()` z powodami pominięcia (declined/never/known/detail_above_level/…), (3) `remark_rules` + `apply_remark` — wyłącznie PROPOZYCJE („Tak zrozumiałem”), zapis tylko z `apply:true`; „nie pytaj o zdrowie” zamyka też pole uwagi/ADHD (decyzja agenta: ADHD to zdrowie; „nie pytaj o ADHD” zamyka tylko to pole),
(4) generator waliduje strukturę, `recipe_hash` teraz wyliczany (stary był nieaktualnym aliasem). Testy: 16 przypadków wywiadu (1033 asercji). Nic w web, nic w chat_engine/ops (to K2b).
Do oceny właściciela później (W2): polskie zdanie pierwszego pytania („Mało (minimalnie), zwykle albo tyle, na ile pozwalają Twoje zasady prywatności (szczegółowo)”) jest nieco ciężkie — do szlifu przy ekranie wywiadu.
**Plan FF:** jedna paczka `67c235d3` (FLAKE-1 + K2a) na `main` po zielonej bramce `k2a` I po końcu CI 269 (job `asan` jeszcze trwa po 95 min; dev/libcxx/vendored zielone) → CI 270 → smoke S2 na tym buildzie → APK v2.

## Bramka `flake1` ZIELONA (13:48, `1ee7a6c8`, drzewo czyste); K2a wystartowało 13:49 (`wf_a8c61ce6-7fe`, Sonnet): jądro wywiadu „Ty” — pytania PL, poziomy szczegółowości (minimalny/normalny/szczegółowy jako przykłady), uwagi „nie pytaj o …”
CI 269 (`b1b34f17`) jeszcze w toku (start 12:35, kontrola 13:57). FF `main` ← `1ee7a6c8` (FLAKE-1) po końcu CI 269; potem CI 270, smoke S2 na tym buildzie i APK v2. K2a nie rusza gałęzi chat_engine/ops (to K2b).

## FLAKE-1 GOTOWE (12:55 UTC, `1ee7a6c8` na `claude/m1-w0`): przyczyna losowego „Ustawienia się nie otwierają” POTWIERDZONA i naprawiona — bramka lokalna `flake1` w toku (start 12:56)
Agent Sonnet (`wf_155e0bcd-15a`, 39 wywołań, 170 tys. tokenów, 19 min). Hipoteza S1 z analizy statycznej potwierdzona: stary `LayerStack` pamiętał JEDNO oczekiwanie na własne `history.back()` i uznawał popstate za „własny” tylko przez 1000 ms; spóźniony popstate (obciążony WebView/emulator) po otwarciu nowej warstwy był czytany jak „Wstecz”
użytkownika i zamykał świeże menu/panel. **Poprawka (`0837aa2f`):** pamięta KAŻDE własne cofnięcie (`inflight`, `top`), klasyfikuje popstate po głębokości (własny, jeśli `1 ≤ top−depth ≤ inflight`), a nie po wieku; `backTimeoutMs` (1000) tylko mówi, kiedy stos przestaje czekać; nowa opcja `forgetBackAfterMs` (preset: nigdy).
Dotyczy wszystkich warstw (menu, szuflada, arkusze, panele, karty, ścieżka nawigatora, grupa ustawień). Dowody: test jednostkowy (6 nowych przypadków, 5 czerwonych na starym kodzie); e2e `m1-history-native.mjs` (30 rund w kształcie sterownika S2 z podkładką opóźniającą `history.back` 2000 ms): na starym kodzie czerwony w rundzie 2
(„grupa zamknęła się sama 99 ms po otwarciu”), na nowym zielony 2× po 30 rund. Nie sprawdzone: emulator (to S2), przeglądarki inne niż Chromium. Jedna niezreprodukowana awaria `m1-navigator-native` („fetch failed: other side closed”) — rerun zielony.
FF `main` ← `1ee7a6c8` po zielonej bramce `flake1` I po końcu CI 269 (kontrola 13:57). Potem smoke S2 na tym buildzie i APK v2.

## CI 268 (`d0ea24cb`) = SUCCESS (12:17); S2 smoke 39768779032 na `4a454783` (= d0ea24cb + sterownik): „Pokaż w grafie” DZIAŁA na 3 profilach emulatora; 1 losowa porażka „Ustawienia nie otwierają się” na 2 profilach → FLAKE-1
`main` = `b1b34f17` (K1c-4, bramka `k1c4` zielona 12:35) od 12:35, CI 269 (id 39779614521) w toku, kontrola ≈ 13:55. S2: wynik 42/0/0 (pixel_6 1.0), 41/1/0 (pixel_5 1.0, pixel_6 1.3); drugi raz losowy krok „panel Ustawienia nie zostaje otwarty”.
**Hipoteza S1 (analiza statyczna `shell/history.ts`):** `LayerStack` uznaje popstate za „własny” tylko przez `backTimeoutMs` = 1000 ms i pamięta JEDNO własne `history.back()`; sterownik robi do 8× `__loomBack()` pod rząd, a na obciążonym emulatorze popstate może przyjść po > 1 s, gdy następna warstwa (menu, panel) jest już otwarta —
spóźniony popstate czytany jak „Wstecz” użytkownika zamyka świeżą warstwę. To założenie szybkości w aplikacji, nie w sterowniku. **FLAKE-1** (`wf_155e0bcd-15a`, start 12:36): test jednostkowy z hostem dostarczającym popstate późno (na starym kodzie czerwony) → poprawka bez założenia czasu → pętla e2e 30× z podkładką opóźniającą `history.back`.
S2 poinformowany; jego następny smoke ma iść na buildzie z poprawką. APK v2 po: zielony CI + smoke S2 na poprawce.

## K1c-4 GOTOWE (11:45 UTC, `b1b34f17` na `claude/m1-w0`): powody po polsku z paczki, zdanie „Brak śladu” bez kluczy — bramka lokalna `k1c4` w toku (start 11:46)
Agent Sonnet (`wf_38d8c445-682`, 113 wywołań, 266 tys. tokenów, 35 min). Zapis (angielskie „why” przy wysyłce) bez zmian jako DANE; ekrany pokazują słowa z paczki wybrane po stałym kluczu (`why.<ref>` / `why.<source_kind>`), a gdy paczka nie ma słowa — zdanie jądra. Funkcja `whyWords`/`rowWhy` wspólna dla arkusza, wierszy nawigatora i karty;
test jednostkowy czyta źródło jądra, więc nowy powód bez słowa wywala test. Zdanie pod „Brak śladu” zwykłe, surowe nazwy ustawień tylko w zwiniętym „Szczegóły techniczne”. Nowy audyt tekstu (`m1-audit.mjs`) w trzech e2e: 0 znalezisk; znalezione ręcznie: surowy typ węzła pamięci „text”, „graph packet”, adres w nagłówku karty podczas ładowania — poprawione.
Zostaje: etykieta wiersza „graph packet” (jądro), angielskie zdanie w samym zapisie (decyzja), audyt nie łapie etykiet krótszych niż 3 słowa, brak emulatora.
Plan: po zielonej bramce i po końcu CI 268 (≈ 12:20-12:30, kontrola 12:37) FF `main` ← `b1b34f17`; smoke S2 na nim; potem APK v2 z commitu z zielonym CI. Brief K2a (jądro wywiadu: pytania PL, poziomy szczegółowości, uwagi „nie pytaj o…”) gotowy w scratchpadzie (`k2a_brief.json`), start ≥ 12:17 po bramce.

## `main` = `d0ea24cb` (K1c-3a + K1c-3b, bramka `k1c3b` ZIELONA 11:10) od 11:11; CI 267 (`568afce9`) = SUCCESS (10:24, 66 min); CI 268 (id 39768676601) w toku, kontrola 12:37; K1c-4 wystartowało 11:11 (`wf_38d8c445-682`)
Tydzień 35 % (10:24; linia planu 27,3 % o 05:51). K1c-4 (tylko słowa z paczki i web): zdania „dlaczego” po polsku zamiast angielskich zapisanych przy wysyłce; zdanie pod „Brak śladu” bez kluczy ustawień; przegląd innych angielskich resztek na ekranach „Co wysłaliśmy”.
S2 poinformowany: smoke na `d0ea24cb` kiedy mu pasuje (nowe testidy i krok Androida w `M1_W0_TESTIDS.md`). APK v2 dla właściciela: z commitu z zielonym CI po smoke S2 — najpewniej z gałęzi po K1c-4.
Dalej po K1c-4: K2a (jądro „Ty”/wywiad: pytania {en,pl}, poziom szczegółowości, `remark_rules` — `docs/architecture/M1_USTAWIENIA_JAKO_GRAF_2026-10-16.md` „M1-K2”, do rozcięcia na K2a/K2b/K2c).

## K1c-3b GOTOWE (10:22 UTC, `d0ea24cb` na `claude/m1-w0`): „Pokaż w grafie” DZIAŁA w przeglądarce — bramka lokalna `k1c3b` w toku (start 10:23)
Agent Sonnet (`wf_873d4c52-07d`, 120 wywołań, 304 tys. tokenów, 36 min). **Przyczyna wyłączonego przycisku (zmierzona na prawdziwym serwerze):** web pytał o adres `msg:m_<hex>`, a adres wiadomości w grafie to `msg:<hex>` (graph_sources.pack: prefiks `m_` → `msg:`);
serwer odpowiadał 404 „no node at …”. Żadnego kodu jądra/serwera nie zmieniono. Stary krok e2e tego nie odróżniał (sprawdzał tylko, że odpowiedź jest niepusta) — zastąpiony prawdziwym przepływem: przycisk włączony → nawigator na żądaniu (`sent_to_model`) → wiersze wysłane/pominięte/widok zapisu →
karta węzła pochodnego → wstecz; negatywy: „Brak śladu” (przycisk wyłączony z powodem) i sztuczny 404. Przycisk włącza się tylko gdy graf mówi `receipt.present`. Raport `M1_CHECKPOINT_1.md` odświeżony (zdania z K1c-1, nowa sekcja 9, 10 zrzutów).
Pozostałe uczciwe braki: angielskie „dlaczego” przy pozycjach (zapisane przy wysyłce) widać na polskim ekranie; zdanie pod „Brak śladu” nazywa klucze ustawień; ścieżki unified/knowledge/graph_packet nadal zbiorcze w paragonie; brak kroku w przeglądarce dla `handed_from`; brak emulatora. → K1c-4 (słowa z paczki).
FF `main` (K1c-3a + K1c-3b, `d0ea24cb`) po zielonej bramce `k1c3b` I po końcu CI 267 (kontrola 10:57). Potem S2 smoke na nowym `main` i APK v2.

## Bramka `k1c3a` ZIELONA (09:45, `07988cce`) — ALE zaskoczenie: stary krok e2e „Pokaż w grafie wyłączony” nadal PRZESZEDŁ, więc w prawdziwej przeglądarce przycisk dalej jest wyłączony
Mój przewidywany czerwony krok nie wystąpił: krok sprawdza tylko, że `data-kernel-answer` nie jest pusty, więc nie odróżnia przyczyn (brak `onShowInGraph` w tej uprzęży = „no_navigator”? odmowa na ścieżce HTTP `POST /api/graph/navigate`? inna odpowiedź dla `limit:1/hops:1`?).
Test jądra (w procesie) przechodzi, a przeglądarka nie widzi działającego przycisku — nie uznaję „Pokaż w grafie” za działające, dopóki e2e nie pokaże włączonego przycisku i listy wysłanych elementów.
**K1c-3b wystartowało 09:46** (`wf_873d4c52-07d`, Sonnet): pierwszy krok = wydrukować prawdziwą odpowiedź i naprawić właściwą warstwę (web przede wszystkim; minimalna poprawka jądra/HTTP dozwolona z testem), potem e2e z prawdziwym przepływem, renderowanie pól K1c-3a w nawigatorze i karcie, odświeżenie raportu.

## CI 266 (`efb31fcf`) = SUCCESS (09:07, 89 min); `main` = `568afce9` (K1c-2, bramka `k1c2` zielona) od 09:18, CI 267 (id 39754419991) w toku, kontrola ≈ 10:57
S2 dostał „266 zielony, main 568afce9”. Zadanie #26 (K1c-1) zamknięte. Zawartość `main` teraz: W1a/W1b/W1c, K1c-1 (jedna liczba), K1c-2 (jądro „wysłane do modelu” z paragonu i pozycje per element).
Na `claude/m1-w0` (jeszcze nie na `main`): K1c-3a (`07988cce`, bramka `k1c3a` w toku, spodziewany czerwony krok e2e). Dalej: K1c-3b (web + e2e + raport) ≥ 09:44.

## K1c-3a GOTOWE (08:59 UTC, `07988cce` na `claude/m1-w0`): jądro rysuje „wysłane do modelu” także w nawigatorze i na karcie — bramka `k1c3a` w toku (start 09:00), PRZEWIDYWANIE: czerwony krok e2e
Agent Sonnet (`wf_37698f0d-16b`, 75 wywołań, 207 tys. tokenów, 21 min): `graph.navigate` z perspektywą `sent_to_model` i fokusem `msg:` rysuje linki paragonu (waga = tokeny, pominięte jako `left_out_of_model`, węzły pochodne `sent:<id>:<ref>`),
`graph.card` zna adresy `sent:`, `perspectives.show` deklaruje `msg_id`/`label_chars`. Nic nie zapisuje do `nodes`/`links`. Bez nowej trasy API. Testy jądra 48/48.
**Przewidywana czerwień bramki:** `e2e/m1-context-sent-native.mjs` (~linia 143) nadal twierdzi, że „Pokaż w grafie” jest wyłączony — a teraz jądro odpowiada i przycisk włącza się sam. To nie błąd jądra; krok zastępuje K1c-3b (web + e2e + odświeżenie raportu), który startuje ≥ 09:44 (odstęp pace).
Plan FF: po końcu CI 266 (≈ 09:10) → `main` ← `568afce9` (K1c-2, bramka `k1c2` zielona); K1c-3a + K1c-3b razem po zielonej bramce `k1c3b`.

## Bramka `k1c2` ZIELONA (08:37, `568afce9`, drzewo czyste); S2: android-smoke 39741874779 na `efb31fcf` = success 41/0/0 na 3 profilach (W1c + K1c-1 na emulatorze); K1c-3a wystartowało 08:38 (`wf_37698f0d-16b`)
S2 widzi na emulatorze: „Klasa: nieznana” z rozwiniętymi szczegółami, szacunek z nazwaną podstawą („dla pustej wiadomości”), zero „0×”; Agent R38 bez zmian. Następny smoke S2 dopiero po K1c-3b (przycisk „Pokaż w grafie”).
K1c-3a (jądro): `graph.navigate` i `graph.card` dla perspektywy `sent_to_model` + deklaracja parametrów `perspectives.show`. Potem K1c-3b (web). FF `main` (K1c-2 + K1c-3a razem) po CI 266 (≈ 09:10) i po bramce `k1c3a`.

## APK Bramki 1 DOSTARCZONY właścicielowi (08:20 UTC): `package.yml` run 39741779475 = success (07:54, 17 min), sha `51684e78` (CI 264 zielony)
Artefakty: `chatadhd-android-apk-TEST-KEY` (17,7 MB, sha256 0f5cda56…, podpis kluczem testowym — brak sekretów wydania), `chatadhd-server-linux-x64`/`-arm64`, `first-steps-screenshots`, `chatadhd-0.10.00-ci.23.51684e7`.
Właściciel dostał: link do biegu + `M1_CHECKPOINT_1.md` (plik) + uczciwą listę: w APK jeszcze rozbieżność szacunku (1,1×) i strażnika (1,8×) — poprawiona dopiero na `main` (K1c-1); „Pokaż w grafie” wyłączony (jądro gotowe w K1c-2, web w K1c-3b); emulator S2 sprawdził W1b, W1c i K1c-1 w toku (smoke na `efb31fcf`).
Wersja v2 (APK po K1c-3b + raport odświeżony o słowa K1c-1 i działający przycisk) po zielonym CI i smoke S2.

## K1c-2 GOTOWE (07:48 UTC, `e0f7c670`; po scaleniu z `main` `568afce9` na `claude/m1-w0`): perspektywa „wysłane do modelu” działa w jądrze — bramka lokalna `k1c2` w toku (start 07:50)
2 agenci Sonnet (`wf_d8dbcfc0-f3f`, 154 wywołań, 420 tys. tokenów, 41 min): A) źródło `receipt` w `perspectives.cpp` + `projection_receipt.cpp`: zapisany paragon narysowany jako graf (wiadomość → pozycje, waga = tokeny, pominięte jako druga relacja `left_out_of_model`;
elementy prawdziwe to adresy `msg:`/`memory:`, zbiorcze to węzły pochodne `sent:<id>:<ref>`; nic nie jest zapisywane do `nodes`/`links`); paragon bez śladu = pusty graf + „brak śladu”, nie błąd. B) paragon wymienia pamięć i graf PER ELEMENT (id węzła pamięci, id wiadomości, `via`, głębokość), udziały tokenów sumują się dokładnie do sumy;
tekst żądania bajt w bajt ten sam (`select_context_detailed` / `get_active_context_detailed` tylko dodatkowo); nad `context_receipt_max_bytes` wraca do zbiorczych pozycji (`elements_collapsed`). Treść węzłów NIE trafia do paragonu (tylko typ/rola). Brak nowej trasy API.
**Luki (do K1c-3a, jądro):** `graph.navigate` z perspektywą `sent_to_model` i fokusem `msg:` nie rysuje jeszcze linków paragonu (działa `perspectives.show`); `graph.card` nie zna adresów `sent:`; akcja `perspectives.show` nie deklaruje `msg_id`/`label_chars` w katalogu. Potem K1c-3b (web: przycisk „Pokaż w grafie” + e2e).
FF `main` dopiero po zielonej bramce `k1c2` I po końcu CI 266 (≈ 09:10; przed FF merge `origin/main`).

## CI 264 (`51684e78`) = SUCCESS (07:20, 91 min, wszystkie 4 joby) → APK w budowie; `main` = `efb31fcf` (K1c-1 + `drive.mjs` od S2, FF S2 o 07:38; bieg CI 265 na `b15180c8` anulowany), CI 266 (id 39741867510) w toku
`package.yml` run 39741779475 (ref main → sha `51684e78`, arm64-v8a Release + serwer + first_steps) wystartował 07:37 — APK dla właściciela z commitu z zielonym CI (zawiera W1a/W1b/W1c); kontrola 08:18.
CI 266 (loom.yml, `efb31fcf`, id 39741867510) kontrola 09:18. S2 puścił swój jedyny android-smoke na `efb31fcf` (wynik ≈ 08:30, linie „Kontekst” w nowych słowach W1c). S2 dostał „main zielony” i opis różnicy 51684e78→b15180c8. K1c-2 (jądro, `wf_d8dbcfc0-f3f`) w toku od 07:09; jego commity nie idą na main przed końcem CI 265.
Uczciwie dla właściciela: emulator S2 sprawdził wersję W1b, a W1c (słowa) czeka na jego smoke.

## K1c-1 GOTOWE (06:25 UTC, `b15180c8` na `claude/m1-w0`, NIE na `main`): jedna liczba przed i po wysłaniu — bramka lokalna `k1c1` ZIELONA (07:08, drzewo czyste); K1c-2 (kernel: perspektywa `sent_to_model` z paragonu, `wf_d8dbcfc0-f3f`) wystartowało 07:09
Agent Sonnet (`wf_237e996f-ad3`, 129 wywołań, 276 tys. tokenów, 32 min). **Przyczyna (zmierzona):** szacunek po „Szerzej” był liczony z PUSTYM tekstem (`context.modify` → `context_guard(conv, "", …)`),
a graf dobiera się po tekście pytania, więc „szerzej” nie miało czego poszerzać: szacunek 1394 B = 0,87× bazy (tyle co bez „szerzej”), a to samo zapytanie z tekstem wysyłki 1960 B = 1,23×, strażnik po wysłaniu 1961 B = 1,23×.
Baza w obrębie jednego biegu ta sama (ten sam klucz `chat.send|endpoint|model`); porusza ją dopiero wysyłka (+1 próbka), a do 5. próbki to wartość startowa 16000 B (stąd wczesne „0,07×”). Pole to samo (`guard.ratio`), różniły się tylko słowa i brak nazwy bazy.
**Decyzja:** `context.modify` przyjmuje opcjonalne `pending_text` i `pending_options` (bez nowej trasy API — rejestr bez zmian), odpowiedź ma `estimate_for` (pusta wiadomość / wpisany tekst); jedno zdanie dla szacunku i strażnika z nazwaną bazą
(„2× zwykłego zapytania do tego modelu (średnia z ostatnich zapytań do tego modelu (liczba zapytań: 7))”); okno pisania przekazuje wpisany tekst i opcje wysyłki. Cofnięcie i przyczyna: `docs/reports/INDEX.md` „S1 / M1-K1c1”. Reguła ×10, złoty plik i schemat paragonu bez zmian.
**Niedomagania (uczciwie):** załączniki nie liczą się do szacunku („bez załączników” w słowach); arkusz pod odpowiedzią nie zna pola wpisywania, więc jego szacunek jest „dla pustej wiadomości” (powiedziane słowami); zagnieżdżone nawiasy w nazwie bazy (zmiana tylko w paczce słów, jeśli właściciel nie lubi).
Agent nie uruchomił bramki ani CI. **FF `main` dopiero po zielonej bramce `k1c1` I po końcu biegu CI 264** (push wywołałby nowy bieg i anulował 264 — reguła „nie anulować trwającego CI”).
Dalej (odstęp ≥ 07:04): K1c-2 — węzły `sent_to_model` z paragonu (odblokowanie „Pokaż w grafie”), potem K1c-3 (cena w paragonie, odczyt nośnika tury).

## CI 263 (`3b827313`) = failure TYLKO w `asan (g++)`, jeden test `server.keepalive` (05:26) → poprawka `51684e78` na `claude/m1-w0` (na W1c `1cf82c52`); FF `main` po końcu bramki `w1c`
Job `vendored (clang++)` PRZESZEDŁ (poprawka `.inc` z jawnymi długościami potwierdzona na prawdziwym CI), dev/tsan zielone.
Przyczyna (czytnik haiku): `rejected()` w `loom/server/tests/test_server_keepalive.py` dawało procesowi 10 s na start i odmowę złej wartości `--keep-alive-timeout-s`; pod ASAN na obciążonym runnerze to za mało
(`TimeoutExpired` → „kept running”). Serwer odmawia w `main()` PRZED otwarciem czegokolwiek (`main.cpp:218`), więc to wolny start procesu, nie błąd logiki. Klasa: test zakłada szybkość (kolejny raz).
Poprawka: limity działające tylko przy porażce: `rejected` 10→120 s, `Server.stop` 15→120 s, połączenie 10→60 s; zamierzone uśpienia keep-alive bez zmian. Lokalnie test przechodzi (`server keep-alive timeout: ok`).
**W1c GOTOWE** (`1cf82c52`, Sonnet): stosunki „nieznane — brak bazy / dużo mniej niż zwykle / od 0,1 jedna cyfra”, krótkie „Klasa: nieznana” + „Szczegóły techniczne” zwinięte, nagłówki „Kolejność/Przepływ” słowami z paczki, zdanie o sumowaniu kroków; 7 zrzutów odświeżonych. Bramka lokalna `w1c` w toku (start 05:07).
Po bramce: push `main` (FF; `origin/main` jest przodkiem) → nowy bieg loom.yml (≈ 120 min) → S2 dostaje „main ma poprawkę” (nie scala na czerwony main) → po zielonym CI + smoke S2: APK + raport właścicielowi.

## S2: android-smoke na W1b (`3b827313` + 2 kroki „Kontekst”) = SUCCESS (run 39717352777): pixel_5 1,0 41/0/0, pixel_6 1,3 41/0/0, pixel_6 1,0 40/1/0
Jedna usterka: Bramka 0 krok 5 „Wstecz” (po ⋮ › Ustawienia panel nie otworzył się; S2 czyta jako dotknięcie w trakcie animacji menu, nie błąd aplikacji; przeszedł na 2 profilach i we wszystkich wcześniejszych biegach).
Uwagi S2 o tekstach (przyjęte): (a) „0× bazy” / „ok. 0× zwykłego” wprowadza w błąd (mały stosunek zaokrąglony do 0 albo brak bazy) → W1c: „nieznane — brak bazy” / „dużo mniej niż zwykle” / od 0,1 jedna cyfra;
(b) kroki z arkusza i z menu DODAJĄ się (szerzej ×2) — zamierzone, dopisane zdanie w pomocy; (c) zdanie klasy modelu za długie i deweloperskie → krótkie „Klasa: nieznana” + pełne zdanie jądra dosłownie w zwiniętym „Szczegóły techniczne”; plus angielskie nazwy „Kolejność: stability / Przepływ: plain” → słowa z paczki.
**W1c** (`wf_1ad64f13-37d`, Sonnet) wystartowało 04:42; koszt pakietu w `pace_plan.json` skalibrowany z 0,75 do 0,5 pkt (pomiar: +3 pkt na ≈ 7 pakietów, 17:33→04:40; tydzień 33 %).
Plan: CI `3b827313` (kontrola 05:22) → jeśli zielony: `package.yml` (APK z W1b + raport `M1_CHECKPOINT_1.md`) do właściciela, z uczciwą listą trzech kosmetycznych usterek; potem wersja poprawiona po W1c (gate → main → S2 smoke → APK v2). FF `drive.mjs` S2 po zielonym CI.

## CI 262 (`d103d5d8`) = failure TYLKO w jobie `vendored (clang++)` (dev/asan/tsan przeszły) → poprawka na `main` = `3b827313` (W1b + raport DOC1 + poprawka), nowy bieg w toku
Przyczyna (agent haiku `wf_5bb3f9f1-ef1`, potwierdzona lokalnie clang 18): `builtin.inc` (pakiet onboardingu, ok. 420 KB po słowach z W1a) — clang liczy każdy znak surowego literału w constexpr strlen i przy ok. 1 mln kroków
na całą tablicę przerywa („constexpr variable kOnboardingPack must be initialized by a constant expression”). gcc ma wyższy limit, więc bramka lokalna i job gcc tego nie widziały; **build Android (clang) trafiłby w to samo**.
Poprawka: generator emituje `std::string_view(R"…", N)` z jawną długością (nic nie jest liczone, pakiet może rosnąć); clang 18 `-fsyntax-only` przechodzi dla wszystkich 9 źródeł z `.inc`; ctest onboarding/pack/runtime_profile 15/15.
Nowe narzędzie: `tools/clang_inc_check.py` (+ krok w `tools/gate.sh`) — kontrola, którą ta klasa błędu wymagała zamiast dwóch godzin CI.
Bieg CI dla `3b827313` (≈ 120 min, do ok. 05:20; kontrola poniżej). S2 dostał „W1b na main” i prośbę o merge `origin/main` przed jego jednym biegiem android-smoke.

## DOC1 GOTOWE (03:05 UTC, `2858800d` na `claude/m1-w0`): `docs/reports/M1_CHECKPOINT_1.md` + 13 zrzutów w `docs/reports/M1_CHECKPOINT_1/` (390×844, każdy < 90 KB)
Raport Bramki 1 (8 sekcji + „Czego jeszcze NIE ma” + „Co sprawdzają testy”); zrzuty z prawdziwej aplikacji i atrapy modelu (2 e2e: 7 + 10 kroków PASS; 4 dodatkowe ujęcia z kopii skryptu w scratchpadzie, bez zmiany asercji).
Cytaty właściciela dosłownie (dowód grepem): 2026-10-16 (CM27, „Kontekst selektor ma działać inteligentnie według ustawień… modyfikatory względem domyślnych, wiesz, szersze albo głębszy kontekst na żądanie…”) i 2026-10-14
(tryb kompatybilności: „jedynie standardowy system prompt i konwersację całą niemodyfikowaną”). Uczciwa uwaga agenta: w dokumentach NIE ma dosłownego zdania właściciela „co wysłaliśmy” — nie podszywam go pod cytat.
**Znalezisko do wyjaśnienia (K1c):** szacunek przed wysłaniem po „Szerzej” = ok. 1,1× zwykłego, a strażnik po wysłaniu pokazuje 1,8× bazy dla tego samego zapytania — dwie liczby o różnych podstawach
(bajty wejścia vs baza z ledgera?) mówią co innego; właściciel zobaczy obie. Do ujednolicenia albo opisania w słowach. Inne uczciwe braki w raporcie: nagłówki „Kolejność/Przepływ” z angielskimi nazwami (stability/legacy/plain),
angielskie „dlaczego” jądra, brak zrzutów listy „Zmień profil” w arkuszu i paska w trybie pełnym/ukrytym.
Dalej: FF `main` z `2858800d` po końcu CI 262 (`d103d5d8`, https://github.com/klb-t/chatadhd/actions/runs/39705119656, start 01:20, kontrola 03:18); potem „W1b na main” do S2 → jeden bieg android-smoke → APK + raport.
Następny pakiet (reguła odstępu, najwcześniej ok. 04:40): **K1c** — luki jądra z W1a/DOC1: węzły `sent_to_model` (przycisk „Pokaż w grafie”), cena w paragonie, odczyt nośnika tury (pasek zna też inne okno), rozbieżność 1,1× vs 1,8×.

## W1b GOTOWE (02:15 UTC, `9b376773` na `claude/m1-w0`) — bramka lokalna `w1b` w toku; FF `main` po końcu biegu CI dla `d103d5d8` (≈ 03:20)
W1b (`wf_63253a37-69f`, 156 wywołań, 371 tys. tokenów): podmenu `+` › Kontekst (chipy Szerzej/Głębiej/Węziej/Płycej, „Profil selektora” z listą i statusami preset/kalibracja/dawny, „Przepływ: tekst ↔ graf”,
„Podgląd wysyłki”; przełączniki przemianowane na „Profil i pamięć (z węzła Ty)” i „Twierdzenia z analizy”, nic nie usunięte), pasek kontekstu (`composer_context_strip`), chip modelu z krótką nazwą i arkuszem
pochodzenia (`model-provenance`: poziom, klasa, cena albo „nieznany — brak ceny”), podgląd z profilem w panelu Kontekst; nowa trasa API: żadna (rejestr bez zmian). Testy: 16 skryptów jednostkowych, e2e
`m1-context-menu-native.mjs` 4× z rzędu, `m1-context-sent-native.mjs` 3× (raz padł podczas regeneracji packów pod trwającym przebiegiem — nie odtworzone). **Nie uruchomione:** pytanie ×10 w przeglądarce
(brak punktu odniesienia w e2e), nowe linie w `ContextSlider` tylko jednostkowo, `npm run e2e` (`run.mjs`), Android. Pasek zna tylko zmiany z TEGO okna (jądro nie ma odczytu nośnika).
S2 czeka na moje słowo „W1b na main”, żeby puścić jeden bieg android-smoke z krokiem „Kontekst” (`50c54661` na `claude/s2-parallel`).
**Bramka lokalna `w1b` ZIELONA** (`9b376773`, 02:29). **DOC1** (raport `M1_CHECKPOINT_1.md` + zrzuty, `wf_4175950c-5e3`, Sonnet, tylko dokumenty) wystartował 02:50 UTC. FF `main` z `9b376773` po końcu biegu CI dla `d103d5d8` (≈ 03:20) → wtedy „W1b na main” do S2 → jeden bieg android-smoke → `package.yml` (APK + raport) dla właściciela.

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
