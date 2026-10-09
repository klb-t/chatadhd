# Zadanie B — DATA / GRAPH / ENGINE — 2026-10-09

## Baseline i checkpoint

Świeży `git fetch --prune origin` zakończył się poprawnie dla wszystkich siedmiu repo. Checkouty były czyste; HEAD = origin/main. Gałąź produktu: `gpt/data-graph-engine-2026-10-09`. Integracja main pozostaje u Claude’a.

| Repo | Bazowy SHA |
|---|---|
| klb-t/chatadhd | `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9` |
| klb-t/Watchdog-JH16 | `58a0c93bd0135e3715dcbc4d92fb80e61bd31215` |
| klb-t/Custom-Keyboard-Pro | `192f820f2d8c653768237e1bff3a1a6d950d69c0` |
| klb-t/AGEDS | `9c1d513bc19d177bd324d7506a21fbab98c2e268` |
| klb-t/LEM-Workbench | `1755e1dfaa69fcaac6fbdd12aa95ea1a593ec456` |
| klb-t/loom | `0b23fa64c1de955c947349feef7bb67c05763f97` |
| klb-t/ar-drafty | `d8af2dd022dacb2f068074158d83e566813592d6` |

Próba odkrycia dodatkowych repo: `gh repo list klb-t --limit 100 --json name,defaultBranchRef` → GraphQL Forbidden. Dostępne repo nie są blokowane. Domyślny fetch obejmował tylko main; gałęzie A/C pobrano następnie jawnie (piny poniżej).

Przeczytane instrukcje AGENTS/CLAUDE, STATE, INDEX i kontrakty R39–R42. Integracja 2026-10-05 zawiera już rejestr metod, profile runtime i prezentację W12. Historycznych bramek nie zaliczamy jako nowych. Nie edytujemy loom/tools/structure. Nie wywołujemy modeli, płatnego CI ani nowych usług.

## Kolejny konkretny krok

Dokończyć pełną bramkę dev z pełnym stdout i ASan dla drzewa 24c9fc3, zapisać wynik guard oraz blokady C. Następny przyrost produktu: WD-003 — walidowane parametry generacji i ich provenance, bez obchodzenia zarezerwowanego modelu, limitu kosztów ani prywatności. Rozpoznano ignorowanie request.params; implementacja jeszcze nie rozpoczęta.

## Gotowe przyrosty

Opublikowane przyrosty zakresowe: d92d7cc, ddcaeaf, 24c9fc3 w ChatADHD oraz 666775a w Watchdog-JH16. Szczegóły i wykonane bramki poniżej. Pełny odbiór integracyjny ChatADHD pozostaje otwarty; nie utożsamiamy commitów z PASS całej macierzy.

## B-POLICY-001 — klasyfikacja danych przy limitach prywatności

Decyzja implementacyjna (odwracalna): przy wskazaniu pola jego zapisane detail/sensitivity stanowią dolną granicę klasyfikacji sprawdzanej przy istniejącym limicie. Jawne wyższe wartości żądania też są sprawdzane. Alternatywa: ufać tylko klasyfikacji caller’a — odrzucona, bo pominięcie lub obniżenie wartości omijało limit. Bez pola i bez metadanych ograniczona kategoria daje InvalidArgument; nieograniczone defaults zachowują wynik. Nie zmieniono wartości polityki ani budżetu.

Pion: onboarding.privacy w packu/override → istniejąca walidacja → R40 resolution → privacy_decision / policy_decision → filtrowany model_request → SQLite reopen. Nowa regresja korzysta z rzeczywistej bazy i istniejącej projekcji grafu, bez transportu/modeli.

Wykonane: baseline 7/7 onboardingu (47,58 s), build GCC13 Debug WERROR shared CLI/server vendored SQLite, po zmianie 7/7 onboardingu (43,32 s). Logi: /workspace/.onboarding/logs/data-graph-{baseline,build,caps-tests}.log. Pełna bramka i web dopiero uruchamiane; nie deklarujemy ukończenia.

Punkt wznowienia: kod i regresja gotowe roboczo; przed commitem wykonać pełny CTest + guard i build web. ASan uruchomić osobno. Odkrycie repo także REST users/klb-t/repos → Forbidden.

Wykonano dodatkowo build web PASS oraz niezależny Chromium E2E 16/16 PASS (system Chromium 151, oryginalny npm run e2e; screenshoty zachowane poza Git). B-POLICY-001 nie zamyka CH-004: ordinary chat nadal wymaga osobnego podłączenia zgód. Audyt A pobrano jawnie z baa9e30c; fetch domyślny obejmował wyłącznie main. C: 6988085. Żadne źródła A/C nie zostały scalone.

Pełny CTest trwa. Pierwszy przebieg ma timeout research.contracts przy równoległej kompilacji ASan; build ASan przerwano, limit 60 s zachowano. Konfiguracja ASan najpierw nie znalazła dynamicznych bibliotek sanitizerów w lokalnym sysroot; podłączono istniejące systemowe ABI libasan.so.8 i libubsan.so.1 i ponowna konfiguracja PASS. Pełny build/test ASan nadal nieukończony. Historii/negatywów nie nadpisujemy.

Commit B-POLICY-001: `d92d7cc` (push PASS). Bramka zakresu 7/7 + web/E2E PASS; pełna macierz pozostaje otwarta, commit nie jest odbiorem integracyjnym całego stosu.

## CH-003-B — wspólny snapshot analizatora

Audyt A rzeczywiście pobrany z `baa9e30c12a29ab7f14fc060a13676b1bd35b036`. Priorytet P1 zastępuje wcześniej planowaną etykietową migrację W12-DIC-0013. Runtime używa istniejącego create_from_data_dir. Live graph i worker rozwiązują istniejący profil semantic_analyzer raz na operację / drain batch; ten sam obiekt dostarcza reguły fallbackowi SemanticLLM. Hash overlay zapisujemy także przez istniejący mark_analysed (bez zmiany schematu storage). Legacy call analyse(text) zachowuje jawnie przekazany analizator konstruktora. Nie refaktoryzujemy badań C.

Decyzja implementacyjna: granica snapshotu = operacja live / cały drain; zmiana pliku działa przy kolejnej operacji. Alternatywy: freeze na całe życie Runtime (utrudnia edycję) albo reload dla każdego rekordu (miesza wersje w batchu). Builtin wynik pozostaje bez nowych pól; provenance hash dodawany tylko dla rzeczywiście zmienionego profilu. CH-003 nie obejmuje CH-002 (legacy prompt/3000), CH-005 (koszty transportu) ani CH-006 (admission relacji).

Pierwszy pełny CTest przerwany po części przebiegu; zachowany log i timeout contracts nie są bramką PASS. Przerwanie wrappera nie zatrzymało od razu potomnego CTest/Ninja; potwierdzono PID i wysłano SIGINT bezpośrednio do własnych procesów. ASan build zatrzymał się na 83/300. Przed kontynuacją: ukończyć build zmienionego runtime, regresję real runtime i pełny CTest bez konkurującej kompilacji. Fetch brakującego historycznego b118c80 zakończył się poprawnie; testy C mogą teraz mieć mniej braków historycznych. Nie zmieniono ich kodu.

CH-003-B: build GCC13 WERROR PASS; 9/9 zestawów native związanych z runtime/analyzatorem/workerem/grafem/storage PASS (2,40 s), 2/2 sentineli Python↔native semantic/db PASS (6,27 s). Nowa regresja rzeczywistego Runtime sprawdza dwie nakładki bez zmiany algorytmu, identyczny hash live/worker/fallback, snapshot całego batchu mimo aktualizacji packa przez callback rzeczywistego EventBus, blokadę błędnej nakładki bez dodatkowych mock requests, zachowanie pending i udane wznowienie po naprawie oraz reopen. Użyto wyłącznie ScriptedTransport; 0 rzeczywistych modeli.

CH-003 zamknięty w zidentyfikowanych natywnych ścieżkach real-time/worker/fallback. Publiczny legacy analyse(text) celowo używa analizatora konstruktora; runtime tworzy go ze sprawdzonego startup packa. Nie deklarujemy zmiany wszystkich odrębnych transportów batch ani komponentów Python. Pełna macierz odbioru całego stosu nadal wymaga nowego przebiegu po następnej migracji.

## W12-DIC-0013 — etykiety projekcji grafu

Osiem etykiet/template’ów przeniesiono do istniejącego ui.pack (EN/PL), konsumowanego przez DefaultLayers.presentation w rzeczywistej project_graph. Pack user revision 3→4, entry prezentacji revision 2→3; builtin.inc i web generated/ui.json wyłącznie przez gen_onboarding_pack.py. Domyślne teksty pozostają takie same. Override locale/tekstu zmienia etykiety, nie method/version/parameter hashes ani bindingi. Suppression daje pustą etykietę, bez ukrytego English; brak wymaganego template daje jawny błąd, zachowując źródło do korekty. Starsza kompletna nakładka katalogu bez nowych kluczy wymaga jawnego uzupełnienia; nie jest nadpisywana fallbackiem.

Pierwsza regresja testowa zbyt szeroko sprawdzała attrs.layer, obejmując również default nodes. Negatyw zachowany; poprawiono adresowanie asercji do rodzaju warstwy z istniejącego vocabulary (bez zmiany oczekiwanych wartości). Przy podniesieniu pack revision dostosowano dwa testy pinujące poprzednie 3 do bieżącego 4, a ich upgrade nadal faktycznie podnosi revision do 5. Bramek po tej zmianie nie przenosimy z poprzedniego przyrostu.

W12-DIC-0013: generator --check PASS, aktualny build GCC13 WERROR PASS, 8/8 zestawów onboarding + generator PASS (48,45 s), build web PASS (2,43 s). Test natywnej projekcji udowadnia zachowanie angielskich etykiet, zmianę przez polski katalog/nakładkę, niezmienność method bindings/attrs oraz jawny błąd brakującego klucza i brak English po wykluczeniu. Dotychczasowe testy store potwierdzają retencję exclusion przez upgrade/restart i niezmienną politykę/model_request przy suppression. To zamyka dokładnie grupę 0013, nie cały CH-008.

Gotowe commity zakresowe opublikowane na gałęzi produktu: `d92d7cc` (privacy caps), `ddcaeaf` (CH-003 consumers), `24c9fc3` (W12-DIC-0013). Main pozostaje `9e20f99`. Aktualny pełny CTest używa drzewa 24c9fc3, nie konkurującego builda. Ostatnia migracja ma web build; E2E z pierwszego przyrostu nie jest dowodem finalnego drzewa, dlatego powtórka będzie osobna.

Odświeżony backlog A: CH-001 (bootstrap konfiguracji), CH-002 (legacy prompty i prefix), CH-004 (ordinary chat zgody), CH-005 (wszystkie transporty i ledger), CH-006 (admission legacy relations), CH-007–011 i CH-015 pozostają otwarte. CH-003 zamknięty tylko w opisanym natywnym zakresie. CH-008 ma częściowe zmniejszenie przez W12-DIC-0013; nie oznaczamy całego audytu jako rozwiązany. Pełny Basic→Advanced/Expert workflow oraz ExperimentSpec/trigger/queue/adoption nadal nie są domknięte przez te przyrosty.

Równoległy niezależny przyrost produktu (ten sam autor, inny zakres zapisu): Watchdog WD-002 / WD-B-002, baza 58a0c93, deklarowane ratio.exclude. Baseline lokalny 613/613. Report jest w tamtym repo pod tym samym relative path. Zamrożone JH16 i źródła C nie są zmieniane.

Pełny CTest finalnego kodu: 145/146, 278,08 s; tylko research.structure niezielone, 1271 przypadków / 154 błędy ochrony Git-temporary credentials. Historyczne obiekty przywrócone przez fetch. Guard odrzucił również niepełny stdout w JUnit: użyto domyślnego limitu CTest zamiast repozytoryjnych 10 MiB. Tego przebiegu nie zaliczamy jako guard PASS. Próbę /var/tmp przerwano po zauważeniu tego samego limitu. Nowa pełna bramka użyje dokładnie --test-output-size-passed/failed 10485760 zgodnie z .github/scripts/README.md, bez zmiany progów testów. Wszystkie wcześniejsze negatywy pozostają w logs.

Watchdog 666775a: ratio.exclude honoruje istniejącą politykę danych, zachowuje ID i ślad wykonania w artifact payload. Pełny lokalny test:all 616/616 PASS (67,9 s), baseline 613/613; demo JH16 pipeline_self_check PASS. Nie zmieniono zamrożonych wartości referencyjnych. Raport i otwarte granice wdrożenia są w repo Watchdog.

Receipt dev zapisano podczas ostatniego przebiegu (nie przed nim): data-graph-dev-build-receipt.json. Binaria nie były w tym czasie przebudowywane. Dokumentujemy ten błąd kolejności zamiast przedstawiać receipt jako wcześniejszy. Finalny E2E uruchomiony oddzielnie z zabezpieczeniem/restauracją tracked screenshotów.

## Aktualna bramka i punkt wznowienia — kod 24c9fc3

Pełny dev z TMPDIR=/var/tmp i stdout 10 MiB: 145/146, 249,35 s. research.structure: 1271 przypadków, 8 błędów wyłącznie test_credential_handoff_v2.HybridHandoffTests; ten zestaw jawnie tworzy katalogi pod /tmp objętym platformowym markerem Git. Nie usunięto markera, nie osłabiono ochrony i nie zmieniono plików C. Guard REJECT: jedna niesprawna pozycja; nie ma już zarzutu uciętego stdout. Zaakceptowane liczniki pozostałych wejść: 926 native / 32817 asercji, 605 Python; nie doliczamy 1271 research.structure do przechodzących przypadków. unit.test_catalog_scale pozostaje jawnym opt-in polityki repo. Nie nazywamy całej bramki PASS.

Finalny Chromium E2E: 16/16 PASS; screenshoty przywrócone do śledzonych oryginałów. Log data-graph-final-e2e.log, receipt chat-browser-38veoqd6.

Przed długą operacją ASan: dev ukończony, build ASan wznowiony osobno, GCC13 + ASan/UBSan + vendored SQLite + server ON + shared OFF zgodnie z presetem. Log data-graph-asan-final-build.log. Po buildzie zapisać receipt, manifest, uruchomić pełny CTest z limitem 10 MiB i guard asan. Pozostałe release/tsan/vendored nie są wykonane w tej sesji; dev używa vendored SQLite, co nie zastępuje oddzielnego presetu. Kosztowne bramki nigdy nie są przedstawiane jako historyczny PASS.
