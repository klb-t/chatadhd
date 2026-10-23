# Kolejka S1 — stan 2026-10-23 04:33 UTC

Po co ten plik: zasada 1 (trwałe checkpointy) — kto przejmie pracę po przerwie sesji, ma tu
wszystko, czego kontekst nie przeniesie. Aktualizowany przy każdym zakończeniu pakietu.

## Reguła właściciela R58 (22.10 ≈ 23:40 UTC; wcześniejsze znaczniki „23.10 00:00/00:20” w tym pliku to pomyłka zegara, prawdziwa godzina to 23:3x–23:4x): przykłady są przykładami, silnik obsługuje każdą konfigurację, mała użyteczność = niewiążąca podpowiedź „czy celowe? nie pytać następnym razem”
Powód: w tabeli planu napisałem „kolor = rodzaj, przezroczystość = pewność, głębia = czas, jak mówiłeś” — to były moje konkretne przypisania podane jak wymaganie i fałszywie przypisane jemu (jego własne słowa: „to jest tylko jedna z konfiguracji, a zakodować trzeba silnik”). Zapisane: `CLAUDE.md` (sekcja „Examples are examples…”), `OWNER_REQUIREMENTS` R58 (cytat dosłowny), `tools/workflows/single-package.js` (reguła w każdym briefie). Zadanie G3 (#30) ma teraz warunki wstępne: (a) skala: punkt odniesienia + odległość ze znakiem, kierunek zależny od drugiej miary, kilka kodowań jednej miary (dziś `view_engine.cpp` ~595–622: tylko `range`/`domain`/`type` i statyczne `invert`); (b) podpowiedź niewiążąca + zapamiętane „celowe, nie pytaj” + wykrycie wyniku zdegenerowanego (nic nie widać). Silnik już nigdy nie odrzuca kodowania (`promoted`/`allowed` z powodami), brakuje pytania i pamięci odpowiedzi.

## S2 smoke 39896257680 na 09ef26d7 (= main 270a065f + sterownik), 04:33 UTC: 43/1/0 ×3 — OBA kroki wywiadu DZIAŁAJĄ na emulatorze (uwaga przy pytaniu → Tak; po pominięciu wszystkiego → „It is already set that way”; UI po polsku: linia „Rozmowa bez modelu — tylko formularz…”, chipy „Dalej Podsumowanie Ile zostało”, „pomiń”/„podsumowanie” rozumiane; odpowiedzi po angielsku = K2b-5). Jedyna porażka: krok agenta sterownika wysłał „poproś agenta…” do otwartej rozmowy wywiadu (formularz bez routera) — poprawka sterownika a6516c5c, nowy bieg po czyste 44/0/0 (id za ~50 min). loom.yml 276 na 270a065f w toku o 04:31 (moja kontrola 04:50).

## G1b GOTOWE (04:10 UTC, `fe0d3850` na `claude/m1-w1`): panel importu pokazuje zdanie jądra + tabelę per rodzaj (znaleziono/zapisano/powód; „Nie było w archiwum: …”; JSON za „Szczegóły techniczne”); chipy załączników dla każdej roli ze stanem („tylko nazwa, bez pliku”); blok „Źródło importu” po polsku; kolizje etykiet usunięte („Mapa sił tej rozmowy (stary widok)”, „Co aplikacja o Tobie wie (stary widok)”) + strażnik duplikatów etykiet (test własnościowy 300 rund); „Co tu widzę?” (help.1-3, dla starej mapy osobne help_old) na nawigatorze; po zdaniach w czacie nawigator się OTWIERA (korzeń / Dane i korpusy); e2e `m1-import-truth-native` 2× zielone, 0 żądań do modelu, 0 znalezisk audytu
Agent Sonnet `wf_481d2bd2-8ec`: 146 wywołań, 357 tys. tokenów, 34 min. Nie zrobione: podgląd/otwieranie załącznika; część etykiet wewnątrz bloku źródła po angielsku (MediaBlock, Artifact); klucze `navigator_help_open`/`graph_view_help_open` zapisane przez PUT /api/config (bez deklaracji w settings_fields.pack — do dopisania w G1c/K); `presentation.test.mjs` test 11 czerwony już w bazie d8a58b34 (powód niezależny — sprawdzi bramka). **Bramka `g1`** na m1d1 (G1a + G1b) start 04:10, limit 2 h. Po zielonej: merge do main (razem z K2c-2 lub osobno), APK v3 z tego stanu zamiast 270a065f, jeśli zdąży przed smoke S2 — decyzja po bramce.

## S2 smoke na `main` 270a065f (run 39891458742, 03:40 UTC): 42/2/0 ×3 — obie porażki w sterowniku, aplikacja poprawna: uwaga przy czekającym pytaniu → propozycja → Tak (K2b-4 działa); druga uwaga bez pytania → „This is how I understood it: … It is already set that way.” (K2b-3 działa; brak propozycji, bo reguła już ustawiona — uczciwie); B: tytuł rozmowy po angielsku „Getting to know each other” nie w regexie sterownika. Poprawki sterownika 09ef26d7; S2 powtarza bieg. APK v3 z 270a065f po zielonym loom.yml (kontrola 04:50).

## G1a GOTOWE (03:0x UTC, `d8a58b34` na `claude/m1-w1`): „pokaż katalog główny grafu” → korzeń (25 obszarów z licznikami); „pokaż import w grafie” → „Dane i korpusy — rozmowy: N, wiadomości: N… Ten obszar pokazuje dziś tylko liczniki: jego zawartość nie jest jeszcze rozpisana na osobne węzły”; nieznana nazwa → lista znanych nazw i aliasów („mówisz też: …”); aliasy obszarów pl/en (test: żadne dwa obszary nie dzielą słowa); import mówi prawdę: `truth` per rodzaj (znaleziono/zapisano/powód z danych) + zdanie pl/en, np. „Załączniki: znaleziono 3, zapisano 0 — według obserwacji z 2026-10-22 eksport Claude.ai nie zawiera plików załączników – zapisano tylko ich nazwy i opisy”; goły .json: „nie policzono… wczytaj całe archiwum .zip”
Agent Sonnet `wf_b8d99db1-791`: 173 wywołania (3,5× planu — pierwszy build w m1d1 + ctest 1043 s), 359 tys. tokenów, 110 min. Nie wzięte: „pokaż graf” (= graph.overview, test pilnuje), alias „pamięć” (drzewo pamięci nie jest w korzeniu — G2). **G1b START (03:35 UTC, `wf_?`, Sonnet, m1d1):** web — podsumowanie i tabela `truth` w panelu importu, chipy załączników dla każdej roli ze stanem, blok „Źródło importu” po polsku, kolizja nazwy przycisku Warsztatu + strażnik duplikatów etykiet, teksty pomocy `help.1-3` jako „Co tu widzę?”, e2e `m1-import-truth-native`. Równolegle K2c-2 w m1d0.

## Bramka `k2b4` ZIELONA (reszta webu 18/18 o 02:45) → `main` = `270a065f` (FF 02:46 UTC): K2c-1 + K2b-3 + K2b-4 + fixture + sterownik S2 (fb32e0f4). FF anuluje CI 275 (tylko drive.mjs) — liczy się bieg na 270a065f (id do ustalenia, kontrola ≈ 04:50). S2 poinformowany: smoke na 270a065f, potem APK v3. Następne po FF: K2c-2 (podgraf party:self) w `m1d0` po merge main; G1b (web: raport importu, kolizja nazwy „Ta rozmowa w grafie”, teksty pomocy) po G1a; W0 okien po K2c-2.

## Bramka `k2b4` (02:39 UTC): jądro ZIELONE — ctest 258/258 rc=0, strażnik 0, skrypty 0, build webu 0; web-testy przerwane po 60 min MOIM limitem tła (kill w środku `m1-history` — „browser has been closed” to artefakt zabicia, nie błąd); 41 skryptów webu zielonych przed przerwaniem; pozostałe 18 (od `test:m1-history`) biegną teraz osobno (`rest-*.log` w `wp/gate-k2b4`, limit 2 h). Brak osieroconych serwerów. Lekcja w `tools/gate.sh`: bramka trwa 40–90 min, start z limitem 2 h.

## G4-fix GOTOWE (02:07 UTC, `9e748032`): 17 uwag kontroli zastosowanych (profil stylu per okno jako opcja `profile_layout_fields`; ekran „Połączenie” = rodzaj okna `planned` z obserwacją z datą, nie `not_window`; D18–D20 dopisane; liczba e2e z #warsztat zweryfikowana: 32). Wpis INDEX `S1 / G4`. Dokument gotowy jako brief dla W0–W2 (po zielonej bramce i FF main).

## G4 GOTOWE (01:58 UTC, `ddd79869`): `OKNA_SYSTEM_2026-10-23.md` — okno = instancja widoku + węzeł grafu, tryby maximized/docked/tabbed/floating/minimized, drzewo podziałów z kartami + warstwa pływająca, rejestr rodzajów okien jako dane (28), układy `loom.window_layout/2` (czat/siatka/warsztat), JEDEN mechanizm (standardowy czat = układ z jednym zmaksymalizowanym oknem + lista rozmów zadokowana i zwinięta), telefon = stos okien + przełącznik wpleciony w LayerStack (Wstecz), pływanie/dokowanie dostępne wszędzie z podpowiedziami R58, sprzężenia parametrów shared/independent/linked jako dane, operacje okien jako akcje z zdaniami, 14 testów własnościowych, 17 decyzji z odwrotem, 11 pakietów (W2 = pierwszy dotykalny: przełącznik „Okna” z ⋮, „otwórz graf obok”)
Agent Fable `wf_30f94975-604`: 22 wywołania, 187 tys. tokenów, 14 min (koszt do pomiaru). 18 cytatów właściciela sprawdzonych skryptem jako dosłowne. Decyzja D9 (moja, bez pytania): „Warsztat” = układ makiety, dzisiejsza siatka = „siatka” (#warsztat zmienia znaczenie w W4; odwrót = zamiana etykiet). Kontrola Haiku przed użyciem w toku. Poprawka: `single-package.js` nazywa w stopce model AGENTA (Fable/Sonnet/Haiku wg `model`), bo G4 podpisał się jako Sonnet.

## 01:44 UTC: drugi worktree i dwa starty — G4 (Fable: projekt systemu okien `OKNA_SYSTEM_2026-10-23.md`, `wf_30f94975-604`) i G1a (Sonnet, jądro+dane: słowa grafu „katalog główny”/„import w grafie”, aliasy obszarów, uczciwy raport importu z załącznikami; worktree `/home/user/wt/m1d1`, gałąź `claude/m1-w1` od 270a065f; `wf_b8d99db1-791`)
Dysk: usunięte 4 stare worktree (extract, fixk1, p39, xport — czyste, w main; r35 zostaje: 2 niescalone commity), ccache 7,3 → 4,7 GB; wolne 4,2 → 9,5 GB → `m1d1` z własnym buildem, więc pakiety jądra nie czekają na bramki w `m1d0` (reguła „jeden worktree, jeden piszący”). Słowa właściciela z 22.10 dopisane przez `spec.py add`: R64 (okna), R65 (ekrany grafu po imporcie), R66 (plan, warsztat); parafrazy R64 i R56 jako `proposed`. Brief G4 cytuje `spec:` i każe czytać słowa przez `spec.py show` (brak wklejania). Po G4: kontrola Haiku przed użyciem.

## K2b-4 GOTOWE (01:37 UTC, `bb548a02`): uwagi i słowa sterujące czytane w każdym języku paczki (preset `chat_ops profile.match_languages = all | conversation | lista`, nadpisanie przez overlay), wyświetlanie w języku rozmowy, negatory w języku wzorca, zdanie „Zapisałem… potwierdzisz razem z całą sekcją” uczciwe; nowy `test_profile_words` (macierz słów × języków, 3. język syntetyczny)
Agent Sonnet `wf_a578c761-63d`: 126 wywołań (≈4× planu — czekał na bramkę i ciął koszt testów), 285 tys. tokenów, 82 min. Testy czerwone na starym kodzie (880 asercji), 61/61 zielone na nowym. Worktree: fixture webu `graph-card-temperature.json` (odświeżone przez e2e po K2c-1) zatwierdzone `56d19946`; merge `origin/main` (fb32e0f4 = sterownik S2) `270a065f`; wypchnięte na `claude/m1-w0`. **Bramka `k2b4`** (jedna, na całym drzewie K2c-1 + K2b-3 + K2b-4 + main; nikt nie edytuje worktree) start 01:37. Po zielonej: FF `main` = `270a065f`, sha do S2, CI, potem smoke S2 → APK v3.

## R57-fix GOTOWE (00:37 UTC, `6a792580`): 34 znaleziska zastosowane, nowy §5.1 „Własny eksport Loom jako format do czytania przez innych” (D5-5…D5-8), zakaz wycieków przeetykietowany jako moja decyzja prawna, wszystkie decyzje z odwrotem; wpis INDEX `S1 / R57-fix`. Agent Sonnet 52 wywołania, 205 tys. tokenów, 8 min. Dokumenty = szkice do przeglądu właściciela (`understanding` w spec: do zapisania jako `proposed`).

## SPEC1 GOTOWE (00:34 UTC, `02663a18`): słowa właściciela jako dane — 487 węzłów, 1090 cytatów sprawdzonych jako dosłowne (plik:linia), ID `s_`+12 hex w `docs/spec/ids.json`, stare kody jako aliasy; `tools/spec.py build/check/show/list/cite/add/propose/approve/correct`, 28 testów
Agent Sonnet `wf_bbe83fe2-7b5`: 53 wywołania, 274 tys. tokenów, 16 min. Liczby zweryfikowane (moje 403/382/24/3 → 403 SLOWA + 10 bez kodu; LUKI 383 pozycji = 392 kodów (MM01–MM10 jako jedna pozycja); sieroty DIP37, VO01, AV01, MM01–10). Statusy (obserwacja z datą 16.10): częściowo 210, jest 151, brak 16 (nagłówek LUKI mówi 12 — nieaktualny), nieaktualne 5, sprzeczne 1. Nowe `cite brief.md [--full]`: sprawdza `spec:<id>` w briefie i wstawia dosłowne słowa. Luki (nie zgubione, w `check --gaps`): 117 linii prozy w 43 fragmentach (preambuły tematów, „Pominięte/Odrzucone”, „Krytyk kompletności” LUKI:17–43); parser nie rozstrzyga, czy cytat jest właściciela czy asystenta (flaga `secondary`); 20 sekcji OWNER bez cytatu (parafrazy w `restatement`); wszystkie 487 `understanding=none` (R61 — parafrazy piszę ja, zatwierdza on). Do zrobienia u mnie: briefy przez `spec:<id>` + `cite`; `spec.py check` do `tools/gate.sh`; przegląd transkryptów po 16.10 (R57–R63 już są w OWNER).
Niezatwierdzony plik roboczy: `REPLIKACJA_…md` zmodyfikowany przez trwającego agenta R57-fix (`wf_139e25a6-a23`) — nie dotykam.

## Bramka `k2c1b3`: ctest CZERWONY tylko na `unit.test_runtime_profile` (hash osadzonego `chat_ops` ≠ plik) — ARTEFAKT POMIARU: agent K2b-4 edytował `chat_ops.pack` w tym samym worktree podczas ctest (jego commit `98657b91` ma już zregenerowany `.inc`, drzewo czyste). Nie wina K2c-1/K2b-3. Decyzja: bramki nie powtarzam na 2bed65b1; po zakończeniu K2b-4 JEDNA bramka na końcowym drzewie (K2c-1 + K2b-3 + K2b-4), a reguła „jeden worktree, jeden piszący” w `CLAUDE.md`. Bieżąca bramka dobiega końca (web) — wynik webu informacyjny.

## CI 274 (`01dd2319`) = SUCCESS (00:28, 123 min, wszystkie 4 joby — poprawka użycia po zwolnieniu DOWIEDZIONA w asan); `main` = `01dd2319` ZIELONY. Bramka `k2c1b3`: ctest rc=8 (co najmniej jeden test czerwony — czytam), web w toku. S2 poinformowany (może FF sterownika).

## Kontrola filozofii projektów R57 (Haiku, 14 wywołań): 34 znaleziska — IMP 7 blokujących + 14 słownych, REP 1 blokujące + 12 słownych → poprawka `R57-fix` (Sonnet, start 00:28)
Blokujące: „repack na życzenie (jak chciał właściciel)” nad parafrazą (W9 to jego dosłowne zdanie o pracy WYŁĄCZNIE na ZIP-ie — ma być preset dostępności, nie degradacja); załączniki Claude.ai „tylko index_only” z jednej próbki (usunięte tryby zamiast „nie do zastosowania dziś”); „NOT POSSIBLE” na niezweryfikowanym negatywie; wymyślony próg „1–2 milczą”; zakaz czytania wycieków przypisany UI14 (to moja decyzja prawna). Brak: własny eksport Loom jako format do czytania przez innych (R60) — dopisywany. Wniosek: kontrola przed użyciem wchodzi do `CLAUDE.md` jako stała reguła (znaleziska Haiku są twierdzeniami do weryfikacji; poprawki na Sonnecie jako dodatek).

## R57 PROJEKTY GOTOWE (00:22 UTC): `IMPORT_JAKO_PLIKI_W_GRAFIE_2026-10-23.md` (93a4e750, 851 linii, pakiety P1–P10) i `REPLIKACJA_PRZEPLYWOW_DOSTAWCOW_2026-10-23.md` (d90f8fdc, 482 linii, pakiety R1–R13) — szkice do poprawek właściciela
Werdykt o klonach (twierdzenie Gemini): częściowo prawda — L1–L2 (wygląd, funkcje) tak; L3–L5 (składanie promptu, pętla narzędzi, dokładność) dla Claude nie; Codex CLI i Gemini CLI to otwarte oryginały (Apache-2.0), nie klony. Sprawdzone w kodzie: 10 profili aplikacji dzieli JEDEN renderer i różni się 6 tokenami koloru + szerokościami (skórka, nie układ); profile selektora like_claude/… mają status „kalibracja” (nietestowane). Oryginalne pomysły agentów: dziedziczenie w osi kontener/treść + kompozycja między osiami przez fasety; odcisk układu eksportu jako koniunkcja deskryptorów z wiarygodnością z rejestru, wynik zawsze lista kandydatów; pamięć podręczna = wiersze `messages` + dziennik łat RFC 6902 nad nietkniętym źródłem; zanik wiarygodności wyprowadzony z obserwowanych odstępów zmian układu; klasy prawne jako tagi pochodzenia; bliźniak chat.html jako druga forma tej samej treści (`expresses`). Niezweryfikowane (A–D milczą): reguła podziału na pliki, tożsamość jsonData w chat.html z shardami, semantyka assetsJson, gałęzie eksportu Claude — do pomiaru lokalnego przez właściciela (lista w dokumencie). miniz: surowe kopiowanie wpisów bez rekompresji potwierdzone w źródle (`mz_zip_writer_add_from_zip_reader`). Koszt w `LIMITS_LEDGER`. Kontrola filozofii (4 antywzorce + R60) przez Haiku w toku; poprawki jako dodatek.

## SPEC1 START (00:18 UTC, Sonnet): wymagania jako dane — ID nieprzejrzyste `s_`+12 hex przydzielone raz (`docs/spec/ids.json`), stare kody (UI12, R56…) jako aliasy, `understanding` (proposed/approved/corrected), status z datą
Właściciel: „brzmi dobrze” + pytanie o ID (odpowiedź: tak, numer bez treści) + „podkręć plan”, „działaj śmiało”, R63 (lepszy model = luźniejsza procedura). W toku 3 agenci: D1 Fable (import jako pliki), K2b-4, SPEC1. Plan na kilkadziesiąt godzin bez właściciela: po bramce `k2c1b3` + CI 274 → FF main → S2 smoke → APK v3; potem G1 (komendy grafu, uczciwy import) → K2c-2 → W2b → G2 (po projekcie D1) → G3 (R58: skala + podpowiedź, potem pasek „Widok”) → G4 (wytyczne + okna, Fable). Parafrazy każdego pakietu jako `proposed` w spec do jego przeglądu.

## K2b-4 START (00:15 UTC, `wf_a578c761-63d`, Sonnet): uwagi i słowa sterujące rozumiane w każdym języku paczki
S2 (run 39870977153, 42/1/0 ×3): przy czekającym pytaniu „nie pytaj o zdrowie” zapisane jako ODPOWIEDŹ. Przyczyna sprawdzona w kodzie (inna niż K2b-3): `apply_remark` dopasowuje tylko `languages(locale)`, `control_op` tylko `e.lang`; język rozmowy ustalony na starcie z języka UI, a emulator ma UI po angielsku (locale urządzenia en, brak `ui_language`), sterownik pisze po polsku. Decyzja: dopasowanie we wszystkich językach paczki (preset konfigurowalny), wyświetlanie w języku rozmowy; zdanie „Czeka na Twoje potwierdzenie” uczciwe (potwierdzenie przy końcu sekcji). Start mimo „CZEKAJ” tempa: błąd blokuje smoke S2 i wywiad na telefonie z UI innym niż język pisania; 0,5 pkt mieści się w 4 pkt do punktu 50 % o 20:00. Agent czeka z buildem na koniec bramki `k2c1b3` (ten sam build/dev). Odpowiedź do S2 wysłana (przyczyna, sha po naprawie, wariant smoke z ui_language=pl).

## 00:12 UTC: koordynator = Fable (właściciel, `/model`); styl wątku = tylko statusy; tydzień 46 %
Właściciel: „nie gadaj tyle…”, tylko: co zaczynam / kończę / buduję / co nie wyszło / co agenci wymyślili oryginalnego; dyskusja tylko gdy on ją otwiera; parafraza R61 ≤ 5 linii. Zapisane w `CLAUDE.md`. Pace: `--used 46`. W toku: bramka `k2c1b3`, CI 274 asan (kontrola 00:31), Fable D1 (import jako pliki). Czeka: zatwierdzenie SPEC1 (jedna linia pytania).

## R61 (00:05 UTC): droga praca startuje po zatwierdzonej parafrazie; przed pytaniem analiza rozumienia względem filozofii
Zapisane: `CLAUDE.md` (sekcja „How the owner works” — zastępuje mój 15-minutowy timer; sekcja „Decisions” — „Before a question”), `OWNER_REQUIREMENTS` R61 (cytat dosłowny). Preset „droga praca”: etap Fable, workflow wieloagentowe, pakiet większy niż drobna poprawka; bez zatwierdzenia: odczyty, bramki, czytniki logów, zapisywanie jego słów, drobne poprawki. SPEC1 dostał pole `understanding` (parafraza + stan proposed/approved/corrected; `spec.py approve/correct`). Wycofane moje pytania do właściciela o rodzaj eksportu i ekran (wybór jednego przypadku z wielu — projekt ma pokryć wszystkie).
Stan: dwa projekty Fable (`design:replication` skończony lub kończący się, `design:import-as-files` ruszył ≈ 00:0x) wystartowały na wyraźne polecenie właściciela PRZED regułą i bez zatwierdzonej parafrazy — nie przerywam; dokumenty = szkice do jego poprawek. CI 274 wciąż w toku o 00:04 (98 min). Bramka `k2c1b3` w toku. Czeka na zatwierdzenie parafrazy: SPEC1 (potem G1).

## K2b-3 GOTOWE (00:03 UTC, `2bed65b1` na `claude/m1-w0`, wypchnięte): „nie pytaj o zdrowie” rozumiane także gdy żadne pytanie nie czeka; bramka `k2c1b3` (K2c-1 + K2b-3) w toku od 00:01
Agent Sonnet `wf_0fe6ccf8-90c`: 63 wywołania, 164 tys. tokenów, 27 min. Test wcześniej czerwony na starym kodzie (3 przypadki), po poprawce zielone; odpowiedź na uwagę bez pytania: „Tak zrozumiałem: • Nie będę pytać o zdrowie…”; na zwykły tekst: „Teraz nie czekam na Twoją odpowiedź i niczego nie zapisałem. Możesz napisać „nie pytaj o ...”, „pytaj więcej” albo „podsumowanie”.” Nie zrobione: dziwne sformułowanie „podsumowanie” gdy nic nie czeka (w INDEX), brak ponownego smoke S2. Po zielonej bramce: merge `origin/main` (dziś = `01dd2319`, w gałęzi), FF `main` po końcu CI 274, potem powiedzieć S2 sha.

## Sposób pracy właściciela (2026-10-22 23:56 UTC): pisze, gdy ma czas, każdą myśl na bieżąco, potem pracuje nad następną — oczekiwać DŁUGICH SERII
Zapisane w `CLAUDE.md` (sekcja „How the owner works…”, cytat dosłowny). Wnioski (moje decyzje, presety, do odwrócenia): każda wiadomość zapisywana trwale od razu; drogie etapy (Fable, wieloagentowe workflow) nie startują na pierwszej wiadomości serii, chyba że to wyraźne polecenie — preset: ok. 15 min ciszy; działającego agenta nie restartuję (dodatek po zakończeniu); odpowiedzi w serii krótkie, pełna odpowiedź z listą zmian na koniec serii; kolejka musi nieść cały stan.

## Właściciel (≈ 00:00): filozofia ma uodparniać pracę na drobne zmiany i korekty; wielokrotnie prosił o pełną specyfikację; zaczął sesję zakładając, że nie musi sprawdzać
Stan faktyczny (policzone): pełny zbiór jego uwag ISTNIEJE — `SLOWA_WLASCICIELA_2026-10-16.md` (403 identyfikatory, cytaty dosłowne), `LUKI_2026-10-16.md` (382 identyfikatory, status: 210 częściowo, 151 jest, 16 brak, 5 nieaktualne, 1 sprzeczne), `OWNER_REQUIREMENTS` R1–R60 — ale to ARCHIWUM prozą w trzech plikach, nie dane; briefy piszę ręcznie, więc korekta = restart. Plan: SPEC1 (zadanie #33, `spec1_brief.json`) — deterministyczny parser → `docs/spec/spec.json` (id, cytaty dosłowne ze źródłem, status z datą jako obserwacja, nic nie odrzucone) + `tools/spec.py add/check/show`; potem briefy cytują id, korekty wchodzą przez `spec.py add`. Start gdy zwolni się slot tempa (w toku: K2b-3, badanie/projekty Fable).
Moje trzy odstępstwa od filozofii tego dnia (do pilnowania, bo „z grubsza się kieruje” nie wystarcza): przypisania podane jak wymagania (R58), opcja zdegradowana przez „nie widzę odbiorcy” (R58/R60), restarty zamiast odpornego procesu (waste 6). Zabezpieczenie mechaniczne do dodania: tania kontrola Haiku dokumentów projektowych i briefów pod kątem tych trzech wzorców, zanim zostaną użyte.

## Marnotrawstwo restartów (pytanie właściciela 23:5x) — pomiar i nowa zasada
Pomiar z zapisów agentów (wiadomości liczone raz, równoważniki wg wag `usage_report.py`): zmarnowane ≈ 19 mln równoważników ≈ 0,9 pkt (górna granica; ≈ 2 pakiety), ok. 19 minut pracy agentów: bieg 1 (zatrzymany po 4 min) 8,0 mln; bieg 2 badacz eksportów 5,5 mln + projekt replikacji Fable 1,2 mln; plus 4,4 mln za powtórzone badanie klonów, bo wznowienie z `resumeFromRunId` odtwarza tylko niezmieniony PREFIKS wywołań, a ja zmieniłem pierwsze wywołanie (badacza eksportów), więc drugie (klony) poszło od nowa mimo gotowego wyniku. Wcześniejsze „pół godziny pracy” w odpowiedzi dla właściciela było zawyżone (zgadnięte, nie zmierzone).
Przyczyny po mojej stronie: (1) cały łańcuch badanie → sceptyk → projekt w jednym workflow, więc brak punktu kontrolnego w czasie, gdy właściciel wysyłał korekty; (2) reguły wklejane do briefów zamiast odsyłać do `CLAUDE.md`, które każdy agent dostaje na starcie; (3) edycja pierwszego wywołania psuje cache; (4) restart bez porównania kosztu poniesionego z kosztem dodatku. Kanału do aktualizacji działającego agenta workflow nie ma (sprawdzone dopiero teraz: `ListAgents` – brak osiągalnych agentów). Zasada 6 dopisana do „Waste control” w `CLAUDE.md`.

## R60 (22.10 ≈ 23:49): brak odbiorcy dziś ≠ brak na zawsze; w ekosystemie są inni odbiorcy; budujemy nowy paradygmat — fakty o stanie świata to obserwacje z datą, nie ograniczenia
Zapisane: `CLAUDE.md` (punkt „Absence today is not absence forever”), `OWNER_REQUIREMENTS` R60. Błąd po mojej stronie: zdanie „o ile wiem, ani ChatGPT, ani Claude nie wczytują własnego eksportu” było niezweryfikowanym twierdzeniem o teraźniejszości użytym jako filtr projektowy. Trwających agentów Fable NIE restartuję (to czwarty restart; brief już mówi, że brak odbiorcy to tylko wejście do podpowiedzi); po zakończeniu D1 sprawdzam, czy dokument nie opiera się na „nie ma odbiorcy dziś” i czy rozważa własny eksport Loom jako coś, co inni mogą czytać; jeśli nie — krótki dodatek.

## R59 (22.10 ≈ 23:40): brief też jest listą przykładów; przykłady konieczne, niewystarczające — wystarcza tylko wyczerpanie możliwości; trzy etapy zapełniania list; wiarygodność źródeł z sieci aktualizowana testami i czasem
Zapisane: `CLAUDE.md` (4 punkty w sekcji „Examples are examples…”), `OWNER_REQUIREMENTS` R59 (cytat dosłowny), `tools/workflows/single-package.js` (agent czyta wybory z briefu jako przykłady silnika; akceptacja = przykłady przechodzą + przestrzeń możliwości pokryta, reszta jako luka). Fable (obu) dostał R59 w briefie: agent nieobsługiwanych formatów w czasie działania (z ograniczeniem prywatności: zapytania z deskryptorów strukturalnych, nigdy z treści; zgoda + koszt; preset zapytaj/auto/nigdy), rejestr wiarygodności źródeł (rozszerzenie `calibration.json`, Beta(α,β) + czas), lista zapełniana w 3 etapach. Badacz eksportów zapisuje też listę źródeł (domena, autor, data, poziom, wynik testu) jako ziarno rejestru. Workflow: `wf_c2686404-9f8` wznowiony z cache (wynik badania klonów zachowany), nowy numer zadania `whnpq1rkt`.
Obserwacja: właściciel pisze kolejne korekty co kilka minut i każda zmienia briefy trwających agentów — zatrzymałem i wznowiłem workflow trzy razy (≈ 10 min pracy dwóch agentów Sonnet straconych). Następnym razem: przed startem agentów projektowych poczekać na zebranie korekt albo wstawić do briefu punkt „reguły właściciela z zapisów” zamiast kopiować tekst.

## Druga korekta właściciela do R58 (22.10 ≈ 23:43): „ty proponujesz jako Export na życzenie” — zdegradowałem opcję (zachowanie formatu dostawcy przy edycji) do jednorazowego eksportu, bo „nie widzę odbiorcy”; to dokładnie to, czego R58 zakazuje (użyteczność wolno tylko zasygnalizować, nigdy nie zawęża opcji)
Naprawa: briefy obu agentów Fable przepisane (reguła R58 w nagłówku wszystkich agentów; punkt (e) „zapis z zachowaniem formatu jako pełnoprawna konfiguracja; brak znanego odbiorcy = niewiążąca podpowiedź, nie powód degradacji”; tabela łatwe/trudne = nakład i ryzyko, nie pozwolenie; „niemożliwe” tylko technicznie). Wzorzec do pilnowania u mnie: filtr „czy to się komuś przyda” użyty jako kryterium zakresu. Rozróżnienie do wyjaśnienia właścicielowi: konfiguracja jest wolna, nieodwracalna AKCJA na jego danych nadal pyta o zgodę.

## Pytania właściciela 22.10 ≈ 23:35: import jako pliki w grafie / pamięć podręczna / klony przepływów dostawców → badanie + dwa projekty Fable (`wf_c2686404-9f8`, start 23:44; pierwszy bieg `wf_9f67e0ac-2dd` zatrzymany po 3 min i uruchomiony od nowa z poprawionymi briefami)
Zapisane jako R57 w `OWNER_REQUIREMENTS` (cytat dosłowny; w zapisach NIE było wymagania „nie kopiujemy, linkujemy, klasa plik>ZIP>eksport>dostawca>wersja, węzeł pamięć podręczna” — przeszukane SLOWA i OWNER_REQUIREMENTS; powiązane już istniejące: UI14, RE31, X-Port/P38). Badanie źródeł z wagami A–D (format eksportów OpenAI/Anthropic/Google, w tym czy chat.html to druga kopia i reguła podziału na pliki), sceptyk, potem Fable: `IMPORT_JAKO_PLIKI_W_GRAFIE_2026-10-23.md` i `REPLIKACJA_PRZEPLYWOW_DOSTAWCOW_2026-10-23.md` (klony na GitHubie zweryfikowane na żywo — twierdzenie Gemini traktowane jako niezweryfikowane).
K2b-3 działa równolegle (`wf_0fe6ccf8-90c`, start 23:32). UWAGA: ten workflow wystartował PRZED terminem z planu tempa (najwcześniej 01:34 UTC) — na wprost wyrażoną prośbę właściciela; koszt Fable nieznany → po zakończeniu wiersz w `LIMITS_LEDGER.md`.
Odpowiedź dla właściciela na plan (G1–G4, tabela etapów) wysłana 23:5x; po wynikach badania zaktualizuję plan (G1/G2 zależą od projektu importu jako plików).

## K2c-1 GOTOWE (23:23 UTC, `222fc2b5` na `claude/m1-w0`, wypchnięte): jednostki (`units.define/list/forget`, `conversation.set_unit/clear_unit`, bramkowane) + pochodzenie notatek pamięci (4 ścieżki zapisu oznaczone; stare notatki: „pochodzenie nieznane (zapisane przed M1)”)
Agent Sonnet `wf_255ba3e4-de1`: 154 wywołania (≈3× planu), 335 tys. tokenów, 56 min. Znane luki: pytanie przy `units.forget` nie zawiera liczby rozmów (jest po „tak” i w `units.list`); zdanie swobodne nie poda rodzaju i id (router uczciwie „niedostępne”); „guardian/import” są w słowniku, ale nic jeszcze tak nie zapisuje.
**Do zrobienia przed bramką:** K2b-3 (zadanie #27, brief `k2b3_brief.json`) — S2 znalazł na emulatorze (android-smoke 39865097050, 42/1/0 ×3), że „nie pytaj o zdrowie” nie jest rozumiane, gdy żadne pytanie nie czeka (`op_answer` szuka pytania przed odczytem uwagi). Start czeka na slot (plan tempa: 3 agenci w toku). Potem jedna bramka na K2c-1 + K2b-3, merge `origin/main` jeśli ruszył, FF `main` po zielonej bramce I końcu CI 274.
**Uwagi właściciela 22.10 ≈ 23:00 (pierwsze od dawna):** „czyli onboarding głównie?” (tak — to moja kolejność), import nie pokazuje załączników w którejś rozmowie, „pokaż katalog główny grafu” / „pokaż import w grafie” nie działają w czacie, widok grafu = ruszająca się pajęczyna (3 węzły po imporcie tysięcy rozmów; koło z kwotami), brak 3D / kanałów alfa / wielowymiarowości, teksty nic nie mówią, pytanie o zebrane wytyczne i o plan (warsztat, perspektywy, kiedy funkcje, kiedy interfejs). Audyt tylko-do-odczytu: `wf_24f6dc8d-88c` (3 czytniki Sonnet: wytyczne / ekrany grafu / import + komendy). Odpowiedź dla właściciela po audycie.

## Bramka `w2a3` ZIELONA (22:25, `01dd2319`); `main` = `01dd2319` od 22:26 (W2a + W2a-fix + poprawka użycia po zwolnieniu); CI 274 (id 39864923873) w toku od 22:26, kontrola 00:09 (limit asan 150 min → najpóźniej 00:56); K2c-1 wystartowało 22:27 (`wf_255ba3e4-de1`, Sonnet: jednostki + pochodzenie notatek pamięci)
CI 273 zakończone wcześniej (FAILURE), więc FF nic nie anulował. S2 poinformowany (main = 01dd2319, smoke wywiadu mile widziany, testidy w `M1_W0_TESTIDS.md`). Następnie: gdy K2c-1 skończy → `git fetch origin main`, ewentualne scalenie, bramka, FF `main` dopiero po zielonej bramce I po końcu CI 274. Potem K2c-2 (podgraf `party:self`) i W2b (ekran „Ty”). APK v3 po zielonym CI 274 + smoke S2.

## CI 273 (`ae6d152f`) = FAILURE (21:08, tylko `asan`): `unit.test_profile_ops` — ASan `heap-use-after-free` w `op_answer` (`profile_ops.cpp:440`) → naprawione `01dd2319`; bramka `w2a3` (cała gałąź: W2a + W2a-fix + poprawka) w toku od 21:40
Przyczyna (log czytany przez agenta haiku `wf_f0812d19-4e1`, 16 wywołań, 99 tys. tokenów): `const Json* f = field_of(l, slot)` wskazywał w `l.state`; `write()` robi `l.state = *r` (linia 88) i zwalnia stare dane; `json::get_string(*f, "section")` po zapisie czytał zwolnioną pamięć. Błąd w kodzie produkcyjnym K2b-1, niewidoczny dla bramki bez ASan.
Poprawka: `field_of` zwraca `std::optional<Json>` (kopię), 9 miejsc wywołania; funkcje tylko-do-odczytu nie zapisują przy trzymanej referencji. `test_profile_ops` 122 s, `test_profile_chat`, `test_onboarding_interview` zielone lokalnie (bez ASan; dowód klasy tylko z CI `asan`). Reguła dopisana do `CLAUDE.md` („druga pułapka tylko dla ASan: wskaźnik do wartości, którą późniejszy zapis podmienia”).
Zatrzymałem bramkę `w2a2` w połowie (ctest), żeby nie sprawdzać stanu, o którym wiem, że jest zły; jedna bramka `w2a3` obejmuje wszystko. Zabite po numerach dwa osierocone serwery e2e sprzed 15 h (z wcześniejszych pakietów).
`main` = `ae6d152f` jest CZERWONY w CI (asan) do czasu FF poprawki — nic na niego nie dokładam, dopóki `w2a3` nie będzie zielona. APK v2 (z `f07fc52a`) nie zawiera tego błędu.

## W2a-fix GOTOWE (21:28 UTC, `9383d783`): bramka `w2a2` w toku (start 21:29)
Agent Sonnet (`wf_38b54457-b3c`, 76 wywołań, 151 tys. tokenów, 27 min). (1) golden `settings_fields.json` zregenerowany przełącznikiem repo (`LOOM_UPDATE_GOLDEN=1`): zmienia się wyłącznie `onboarding_card` (planned → implemented, powód i „dziś” po polsku i angielsku). (2) Zbędne `GET /api/conversations/<id>` USUNIĘTE (nie dopisane do atrap): rozmowa wywiadu rozpoznawana z `metadata.interview` na już wczytanych wiadomościach (`isInterviewMessages`); otwarcie zwykłej rozmowy: 8 → 7 żądań /api;
asercje `unexpected` bez zmian. (3) Dowody: ctest 49/49, 11 skryptów webowych dotykających czatu zielonych, `m1-profile-native` 2× (0 żądań do modelu, 0 znalezisk audytu), 4 sąsiednie m1 zielone.
Uwagi agenta: `e2e/semantic-controls.mjs` pada (GET /api/auth/me, /api/events/stream) — NIE jest w bramce ani w CI, pada tak samo na `5c15b2de`, nie od W2a; do osobnego porządku. `test:product-integration` wymaga świeżego `ONBOARDING_HOST_EVIDENCE_DIR` albo usunięcia pliku wyników w `/home/user/wt/onboarding-host-verification-2026-10-05/` (artefakt stanu maszyny; w bramkach dotąd przechodził).
FF `main` (W2a + fix) po zielonej bramce `w2a2` I po końcu CI 273 (kontrola 21:31).

## Bramka `w2a` CZERWONA (21:01, `5c15b2de`): 3 przyczyny, wszystkie skutki W2a → W2a-fix (`wf_38b54457-b3c`, Sonnet, start 21:02)
(1) `unit.test_action_rules`: złoty plik `settings_fields.json` nie zgadza się, bo W2a dał ustawieniu `onboarding_card` czytnik (planned → implemented) — zmiana zamierzona, golden do zaktualizowania (nie `chat_request_legacy.json`). (2) `test:chat-send-gate` i (3) `test:interface-2`: ich atrapy backendu flagują NIEOCZEKIWANE żądania — W2a robi `GET /api/conversations/<id>` przy każdym otwarciu rozmowy (odczyt `metadata.flow` dla linii informacyjnej wywiadu). To realny koszt (dodatkowe żądanie na każdą rozmowę dla jednego rodzaju rozmowy), nie tylko luka atrapy.
Decyzja (moja, dla agenta): wyprowadzić „to rozmowa wywiadu” z danych, które aplikacja już ma (`metadata.interview` na wiadomościach asystenta / wpisy listy rozmów), bez dodatkowego GET; nie osłabiać asercji `unexpected`.
Nauka: bramka złapała to, czego e2e W2a nie widziało (agent nie uruchomił sąsiednich testów z atrapami backendu) → w briefach dla paczek webowych dodaję pełną listę `test:*` dotykających ChatView.
CI 273 (`ae6d152f`) wciąż w toku o 21:02 (101 min), kontrola 21:30.

## W2a GOTOWE (20:11 UTC, `5c15b2de` na `claude/m1-w0`): wywiad „Poznajmy się” w aplikacji — bramka lokalna `w2a` w toku (start 20:13)
Agent Sonnet (`wf_18afae68-0f2`, 182 wywołania — ok. 2,6× planu, każdy bieg e2e ≈ 2,5 min; 350 tys. tokenów, 51 min). Karta „Poznajmy się” w pustym czacie (Zacznij / Później / Nigdy nie pytaj, „bez modelu, nic nie wychodzi poza to urządzenie”), 3 karty poziomów z podglądami z jądra (e2e porównuje znak po znaku), chipy Pomiń/Nie teraz/Nigdy/Podsumowanie/Ile zostało,
„Tak zrozumiałem” z przyciskami Tak/Nie (ActionProposalBlock w trybie `plain`, bo pokazywał surowe pola jądra), linia „Rozmowa bez modelu — tylko formularz” + „Pytań do zadania: N”. Typowania `/api/profile/*` w trzech transportach + łańcuch rejestru. e2e `m1-profile-native.mjs` 2× zielony, audyt tekstu 0 znalezisk, atrapa modelu dostała 0 żądań. Raport `M1_CHECKPOINT_2.md` + 8 zrzutów.
Uwagi agenta (do K2c/W2b): jądro samo nie cofa `onboarding_card=later` na `show` przy starcie (robi to web przy pierwszym widoku strony); porzucony wywiad zostawia kartę ukrytą (profil już istnieje) — powrót tylko przez „Poznaj mnie” z menu Ty (W2b); po „Tak” tytuł wyniku jest ogólny („Gotowe: Potwierdź, co zrozumiałem.”). Nie sprawdzone: emulator; poziom „Minimalnie” nie jest tapnięty w e2e (używa „Szczegółowo”).
FF `main` (W2a) po zielonej bramce `w2a` I po końcu CI 273 (kontrola 21:02). Potem smoke S2 i APK v3 (z wywiadem).

## Bramka `k2b2` ZIELONA (19:21, `ae6d152f`); `main` = `ae6d152f` (K2b-1 + K2b-2) od 19:21, CI 273 (id 39839307922), kontrola 21:01; W2a wystartowało 19:21 (`wf_18afae68-0f2`, Sonnet)
W2a: karta „Poznajmy się” w pustym czacie, karty poziomów z podglądem, chipy Pomiń/Nigdy/Podsumowanie/Ile zostało, „Tak zrozumiałem” przez `ActionProposalBlock`, linia „rozmowa bez modelu”, typowania `/api/profile/*` + łańcuch rejestru, e2e `m1-profile-native.mjs` (0 żądań do atrapy modelu), raport `M1_CHECKPOINT_2.md`.

## CI 272 (`5b0e04ff`, K2a) = SUCCESS (18:36, 120 min — asan wolny runner); `main` = `5b0e04ff`; bramka `k2b2` w web e2e (start 18:32); brief W2a gotowy (`w2a_brief.json`)
W2a (web): karta „Poznajmy się” w pustym czacie (`onboarding_card`: show/later/never), karty poziomów z podglądem (o co zapytam / co zapiszę / co zobaczy model), chipy Pomiń/Nigdy/Podsumowanie/Ile zostało, „Tak zrozumiałem” przez istniejący `ActionProposalBlock`, linia „rozmowa bez modelu”, typowania API `/api/profile/*` + łańcuch rejestru, e2e `m1-profile-native.mjs` (0 żądań do atrapy modelu), raport `M1_CHECKPOINT_2.md`.
Start po wyniku bramki `k2b2` (agent czeka na `GATE GREEN/RED` w logu). Potem: W2b (ekran „Ty”: podgrafy, Co pamiętam, Popraw/Zapomnij, zainteresowania do potwierdzenia) i K2c (jądro: `units.define`, `conversation.set_unit`, `metadata.provenance`, rzut party:self z podgrafami, `asked_at_level` w rzucie).

## K2b-2 GOTOWE (18:31 UTC, `ae6d152f` na `claude/m1-w0`, na wierzchu K2b-1 `aa45db5b`): wywiad „Ty” w oknie rozmowy, tryb formularza — bramka lokalna `k2b2` w toku (start 18:32)
Agent Sonnet (`wf_9de01309-e76`, 123 wywołania, 310 tys. tokenów, 42 min). KROK 0 (czerwona bramka `k2b1`) naprawiony DANYMI: `chat_coverage.pack` ma `chat_outcome` (pending|executed|unavailable|continuation) + powód pl/en; test czyta dane, wyjątek `import.file` przeszedł do danych; `profile.confirm` = kontynuacja; `profile.set` ma prawdziwe zdanie „zapisz o mnie work.projects=klub szachowy” (wzorce `heuristic.arguments` mogą nazwać jedną akcję).
Wywiad w rozmowie: `profile.interview start` bez `conv_id` otwiera rozmowę „Poznajmy się” (`metadata.flow = profile.interview`); gałąź jest w `chat_route::handle` (tam, gdzie wiadomości i tak stają się akcjami/propozycjami), NIE w `ChatEngine::send` (tam tylko odmowa z powodem jako zabezpieczenie) — działa także przy `intent_routing_method=off`, bez żadnego żądania do modelu (test liczy żądania transportu = 0).
Słowa sterujące (nigdy / nie teraz / pomiń / podsumowanie / ile zostało / dalej) są DANYMI; uwaga typu „nie pytaj o zdrowie” → „Tak zrozumiałem…” + zwykła propozycja `profile.confirm`; po „tak” albo „nie” kolejne pytanie. Oczekujący slot nie jest nigdzie zapisany osobno — wyliczany z `plan()` w każdej turze (jedyny stan = magazyn profilu).
Poziomy per kategoria podłączone; stary magazyn bez `detail_levels`/`remark_rules` dostaje propozycję jawnego odświeżenia (`update_pack`, odpowiedzi zostają). Zdania na ekranie bez nazw wewnętrznych („Minimalnie (minimal)” → „Minimalnie”, „ustawieniu profile_detail_level” → „ustawieniach”, „erase=true” → „Potwierdź, żeby usunąć”), test danych pl/en pilnuje snake_case/id/true/false/„=”.
Odpowiedzi użytkownika w rozmowie wywiadu są zapisywane jako wiadomości prywatne (bez analizy semantycznej/grafu). Zostaje: W2 (ekran „Ty”, karta „Tak zrozumiałem” w UI, kontynuacja przez ActionProposalBlock), ścieżka modelu (privacy_decision + koszt).
FF `main` (K2b-1 + K2b-2 `ae6d152f`) po zielonej bramce `k2b2` I po końcu CI 272 (jeszcze w toku o 18:13, kontrola 18:34).

## Bramka `k2b1` CZERWONA (17:47, `aa45db5b`): jeden test `unit.test_chat_ops` (12 asercji) — K2b-2 startuje od naprawy (KROK 0), `wf_9de01309-e76` start 17:49
Przyczyna: test pokrycia czatu wysyła zdania przykładowe KAŻDEJ akcji z `chat_coverage.pack` i oczekuje „wykonane od razu” albo propozycji `pending` z `receipt_id`/`run_id`; dla nowych `profile.set` i `profile.confirm` router odpowiada `unavailable` (zdanie nie niesie wymaganych parametrów; `profile.confirm` to kontynuacja z propozycji wywiadu, nie coś, co się mówi).
Wyjątek `import.file` był zakodowany na sztywno w teście. **Decyzja dla agenta:** oczekiwanie „co zdanie samo potrafi” staje się DANĄ wpisu pokrycia (`chat_outcome`: pending|executed|unavailable|continuation + powód pl/en), test ją czyta, wyjątek `import.file` przechodzi do danych; `profile.confirm` = kontynuacja; `profile.set` — zdanie prawdziwe, jeśli router uczciwie wypełni slot i wartość, inaczej `unavailable` z powodem (bez wymyślania parsera).
Nauka: regex akceptacji K2b-1 nie zawierał `chat` — dodany (`chat|route|coverage|actions|profile|onboarding`). Reszta 255/256 testów jądra i cały web zielone; po KROKU 0 K2b-1 powinno być gate-zielone.
Plan K2b-2: (1) KROK 0 naprawa; (2) start wywiadu w rozmowie (`metadata.flow = profile.interview`), gałąź w `ChatEngine::send` (bez modelu), propozycje jak w `agent.run`; (3) `profile_detail_by_category` + jawne odświeżenie starego scenariusza; (4) zdania na ekranie bez nazw wewnętrznych („(minimal)”, `profile_detail_level`, `erase=true`) + test danych pl/en. FF `main` (K2b-1 + K2b-2) po zielonej bramce i po końcu CI 272 (kontrola 18:13).

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
