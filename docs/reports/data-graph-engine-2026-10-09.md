# Zadanie B — DATA / GRAPH / ENGINE — 2026-10-09

## Aktualny punkt kontynuacji — integracja B, 2026-10-09

Stan tej sekcji zastępuje historyczne „następne kroki” niżej. Kontynuacja zaczęła się z czystego `387afe08587f179d47c013a2ea518ff4b68e36bc`, bez resetu ani zmiany main. Fresh fetch wszystkich siedmiu main: bazowe SHA w tabeli pozostają aktualne. Lokalna praca Chat/Watchdog/AGEDS zachowana. Źródła współpracy pobrane jawnie: A `43cc61e09475446c12040e53965ea83526a6cf73`, C `c525d0e942be95c8b3a79b670e621d50e0b31a1a`, D `6ed509a482d343288cd9ebd20553ccdcd0f3096b`, E `3eaac2953c3ee0d01d085e595d68edc091c284f2`.

Priorytet 0 zamknięty zakresowo: C `a6481d11a05158977a87dd55f3a4d6896a190dad` przyjęty przez cherry-pick -x jako `82800d56de22f846542961ca83286aac3f7172af`, autor zachowany. Handoff `4e46ae87` przeczytany, pozostałe badania C nieprzyjęte. Testy credential 10/10 PASS; research.structure 1273/1273, 0 skips, guard PASS. Pełny dev na tym SHA: CTest 146/146 PASS, 252,52 s; guard PASS: 145 wykonanych wejść, `unit.test_catalog_scale` jawnie opt-in/niewykonany; 928 native / 32953 asercje, 1878 Python / 0 skips. Receipt source/config/bin przed testem, pełne stdout/JUnit 10 MiB. Vendored SQLite, GCC13 Debug-g1 WERROR, bez konkurującego ASan. Pierwsza próba scoped receipt miała brakujący argument cache; log pozostał, scoped test powtórzono z poprawnym receipt przed startem. Markery Git i testy negatywne zachowane.

Aktualne opublikowane przyrosty: `d5b4236a` A3-DISC-001, `a4b26a55` A3-IMP-CH004/006 i część CH003, `4b8c0e2b` wspólny mapper D, `3dc0ebf2` rzeczywisty operator zasobu, niezależna retencja, CH-RES-N002 i filtr kontekstu receipt. D/E przyjęto przez cherry-pick -x w ograniczonym, przejrzanym stosie; źródła i zależności w manifeście. Scope 23/23 PASS, guard 22 wykonane + jeden jawnie opt-in/niewykonany catalog_scale. Szczegóły i migracje w końcowych sekcjach raportu.

Punkt wznowienia przed długą bramką: build6 jest aktualny, pełny dev/ASan jeszcze niewykonany na złożonym drzewie. E ma sprawdzone native R40 i headless, ale pierwszy rzeczywisty App E2E 16/17: brak widocznych węzłów w nowym panelu, trwa diagnoza; kod E integracji pozostaje roboczy. CH-RES-N001 ordinary link, P4 kontekst/zgoda/trace bez payloadu oraz P5 pełny workflow/ExperimentSpec nadal otwarte. R42 inventory nie jest runtime gate. Watchdog WD-003 jest kolejnym niezależnym potwierdzonym pakietem w pracy, bez powtarzania ratio.exclude.

Manifest integracji: [data-graph-engine-2026-10-09-integration.json](data-graph-engine-2026-10-09-integration.json). Receipty zawierają SHA/source/config/binaria przed testami oraz hash plików roboczych tam, gdzie gate dotyczył brudnego drzewa. Zero płatnych modeli i CI, main do odbioru Claude’a. Szkic setup odświeżony o zależności D i bezpieczny TMPDIR; zapis potwierdzony, publikacja konfiguracji nadal jest osobną czynnością użytkownika.


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

Następny przyrost B: EA-AGEDS-002 — versioned request w payload kolejki, deduplikacja po intent/hash i rzeczywisty worker czytający przypięte dane. Przed implementacją rozdzielić jawnie requested model/decoder od placement/device workera, aby nie zastąpić jego istniejących ustawień zasobów defaultem serwera. Zachować puste legacy payloads jako unknown. WD-003 pozostaje następną niezależną grupą: capability-validated params, chronione model/messages/output bound, pełne provenance bez sekretów. Pełny nowy guard ChatADHD wymaga również kontraktu C o TMPDIR; poprzednie negatywy zachowane.

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

## AGEDS — niezależny przyrost opublikowany

Produkt `157d88f`, dokumentacja/ledger f2e6d2c i 55b23a8, push PASS. Wersjonowana
lokalna receptura ASR ze źródła server/profiles/asr/default.json podłączona przez
walidację/istniejące Settings do rzeczywistego verified worker; pełny snapshot/hash
w istniejących run/result metadata i inertnym eksporcie grafu. Domyślne small/CPU/
int8/VAD/timings zachowane. Model overlay i opcje niezależne; brak model revision
pozostaje unknown. Błędne dane nie uruchamiają dekodera, źródło pozostaje, naprawa
i nowy run zachowują historię. Backend dekodera syntetyczny, nie pomiar jakości ASR.

Świeża baza w oddzielnym detached worktree: 429 testów + 588 subtestów PASS (9,46 s).
Finalny Python 432 + 588 PASS (9,81 s), Node 85 PASS, verifier PASS, scoped 19 PASS.
EA-AGEDS-001 zamknięty w zakresie istniejącego local adaptera; dalsze opcje/provider
unsupported jawnie. EA-AGEDS-002 (pin do queued job i recipe-aware dedup) otwarty,
EA-AGEDS-006 (całość konfiguracji w grafie) otwarty. Bez migracji żywego storage,
bez płatnych modeli/CI, bez zmian Android. Szczegóły w raporcie tego repo.

## ASan — punkt wznowienia przed pełnym testem

Build GCC13 WERROR ASan+UBSan ukończony PASS, log data-graph-asan-final-build.log.
Instrumentowane executables mają shared OFF i SQLite vendored (jawna lokalna
adaptacja presetu, którego standardowy SQLite jest systemowy). Sanitizery zachowane:
address,undefined; no recovery; detect_leaks=1, strict_string_checks=1.
FFI companion to identyczny kod produktu z istniejącego dev/libloom.so; jego cache
i SHA zapisane osobno jako asan-ffi-companion-reused-dev. Receipts obu buildów
zapisano przed tym testem. Full CTest/guard asan uruchomione skryptem
/workspace/.onboarding/run-data-graph-asan.sh, TMPDIR=/var/tmp, stdout 10 MiB.
Wynik jest jeszcze oczekiwany.

Opis styku C: [synthetic temporary root contract](../architecture/DATA_GRAPH_ENGINE_RESEARCH_TEMP_CONTRACT_2026-10-09.md). Publikacja nie oznacza doręczenia ani akceptacji przez C; nie edytowano jego kodu.

ASan pierwszy pełny przebieg znalazł heap-use-after-free w historycznym teście
loom/tests/test_onboarding.cpp:229 (blame 1e63bcc93, obecny na bazie). Pętla
odczytuje subobject tymczasowego session.snapshot(); ten sam wzorzec jest w :241.
Poprawka zachowuje kopię snapshotu do końca każdej pętli, bez zmiany asercji ani
produkcyjnego API. DefaultLayers.snapshot zwraca const reference i jego dwie
pętle nie mają tej wady. Source testu poprawiony, obecny full CTest nadal używa
oryginalnego instrumentowanego binary pinned w receipt; nie przebudowujemy go
podczas przebiegu. Po zakończeniu: build obu binary testów, scoped dev/ASan,
commit, nowy full ASan. Negatywny log i abort pozostają zachowane.

## B-SAN-001 — poprawiona własność snapshotu w teście

Dwa zakresy iteracji zachowują teraz kopie JSON do końca pętli. 0 usuniętych
asercji, 0 zmian API produktu. Build dev/ASan WERROR PASS; bieżący test
unit.test_onboarding PASS w dev (0,41 s) i ASan+UBSan (2,37 s), z oryginalnymi
flagami detect_leaks/strict_string_checks/no-recovery. Logi i JUnit
data-graph-snapshot-{dev,asan}; nie są pełnym CTest.

Pełny ASan przed poprawką testu: 144/146 (1053,60 s); heap-use-after-free w
unit.test_onboarding oraz 8 błędów C w research.structure. Guard REJECT: dwa
niezielone wejścia; liczniki zaakceptowanych wejść 908 native / 32588 asercji,
605 Python, 0 skips Python. Runtime, worker, fallback, storage oraz pozostałe
onboarding suites przeszły instrumentowane. FFI jest zwykłym companion dev i
ma osobny receipt, więc nie nazywamy jego wyników instrumentowanymi.

Nie deklarujemy pełnego guard PASS po osobnej poprawce. Nowy pełny guard
pozostaje otwarty; powtórka research.structure wymaga środowiskowej poprawki
fixtures C opisanej w kontrakcie. Test-only fix nie zmienia danych produktu,
więc wcześniejszy finalny E2E dotyczy tego samego produktu; nie nazywamy go
nowym przebiegiem po zmianie testu. Release/TSan/oddzielny vendored nadal unrun.

Odkrywanie repo: publiczna strona właściciela HTTP 200 pokazała sześć znanych
repo (bez dodatkowych); wszystkie siedem podanych repo dostępne. API także
bez uwierzytelnienia blokuje proxy CONNECT 403. Receipt
data-graph-owner-discovery.json; nie jest pełnym spisem prywatnych repo.

Aktualny backlog A ma 12 pozycji: CH-001–011-B oraz CH-015-B. Nie dodajemy
ponownie CH-012–014 sklasyfikowanych poza tym backlogiem. CH-003-B zamknięty
w opisanych ścieżkach native; W12-DIC-0013 zmniejsza CH-008-B. Pozostałe otwarte.

## Końcowy checkpoint publikacji

B-SAN-001 produkt/test commit `600cef90` push PASS. Current focused dev i ASan:
18/18 przypadków, 234/234 asercje; wartości oczekiwane zachowane, bez pominięcia
przypadków tej grupy (909 innych z tego binary nie wybrano przez filtr CTest).
AGEDS najnowszy 3878b53: dodatkowy production bootstrap test, finalny Python
433/433 + 588 subtests PASS. Watchdog najnowszy 61ae246 (produkt 666775a).
Remote branch SHA każdego z trzech repo potwierdzono przez ls-remote; drzewa
czyste, main bez zmian. Nie ma aktywnych procesów bramek ani podagentów B.

Archiwum lokalnych dowodów, 69 plików sukcesów/negatywów z manifestem/hashami:
/workspace/data-graph-engine-2026-10-09-evidence.zip (210513 bytes), SHA-256
5bdf7a5c0069fcdc6b0d839deda17fa8c32e79ffa1fa4a6dbfee1fc3bedef577.
Manifest osobno: /workspace/data-graph-engine-2026-10-09-manifest.json. Zawiera
piny kodu wykonanych testów, nie ten późniejszy commit raportu. Pełnego wyniku
PASS całego zadania B, Basic→Advanced workflow ani ExperimentSpec nie deklarujemy.

## Doprecyzowanie właściciela: zasoby zewnętrzne — punkt wznowienia

Nowy priorytet: pełne wnętrze referencji w grafie i przekrojowe discovery (R15/R20/R21). Przed zmianą wykonano aktualną bramkę istniejących komponentów: catalog, catalog_retention, import_exports, import_export_fidelity 4/4 PASS, 9,61 s. Wykorzystano istniejący locator ZIP i lossless export-3; nie pisano nowego importera kombinacji format/transport. Kontrakt zakresu: docs/architecture/EXTERNAL_RESOURCE_VERTICAL_2026-10-09.md.

Implementacja robocza: read_resource wspólne dla istniejącego catalogPreview i rzeczywiście zarejestrowanego zadania TaskEngine, transakcyjna projekcja do istniejących nodes/links, wersjonowane IDs i retencja ostatniej poprawnej projekcji. Syntetyczny ZIP testuje alternatywną gałąź, treści/statusy/raw i graf wobec rzeczywistego lossless importera, endpoint C ABI widoku, zadanie, brak źródła, reopen i wznowienie. Nie zmieniono prywatnych archiwów ani C/loom/tools/structure. Nie zamknięto całego wymaganego pierwszego przyrostu: profil/runtime, discovery, permission-aware context i nested/reference pozostają otwarte.

Pierwszy build testu ujawnił kolizję ImportOptions (catalog vs importer); poprawiono kwalifikację bez zmiany oczekiwań. Dev 6/6 PASS (16,08 s); ASan 6/6 PASS (68,51 s), lecz ten zakres uruchomiono przez --test-dir, więc nie deklarujemy explicit sanitizer environment z presetu. Po doprecyzowaniu coverage unknown-domain końcowe dev i ASan z właściwym presetem są w toku. Przed dłuższą operacją zapisano ten punkt wznowienia. Logi baseline/build/negatywy i testy: .onboarding/logs/data-graph-resource-*.

B-RESOURCE-001 bramki końcowe: build dev + CLI/server WERROR PASS; dev 6/6 PASS, 13,93 s; build ASan PASS; właściwy `ctest --preset asan` z detect_leaks/strict_string_checks/UBSan halt 6/6 PASS, 39,00 s (JUnit data-graph-resource-final-asan.xml). Bramka zakresowa, nie pełna macierz 146/146. UI korzysta z istniejącego inspector/All model fields i endpointu; nie dodano nowego katalogu etykiet w kodzie ani zmiany rendererowego parsera. Wcześniejszy web build wykonywał przejściowy, później usunięty blok renderera; nie liczymy go jako finalnego gate. Następny konkretny przyrost: external RuntimeProfile + runtime i jawna uncertain mapping.

B-RESOURCE-001 commit `ad6269a5`, push PASS (bez main). Drugi zakres: istniejący RuntimeProfile waliduje external overlay, effective fields są nodes/links, real analyzer konsumuje tę samą istniejącą referencję i hash. Symlink binding jest jawnie utworzony przez właściciela/test, nie przez discovery. Unknown pole zachowuje źródło i daje uncertain/error bez aktywacji lub publikacji niezinterpretowanych sekretów. Brak referencji blokuje nowy Runtime istniejącą regułą loadera; graf zachowuje ostatnią poprawną projekcję i osobno aktualizuje availability. Testy pokazują pause/resume zadania, repair/reopen i 0 HTTP na ScriptedTransport. Nie wygenerowano nowego silnika konfiguracji.

Negatywy budowania testu (błędna nazwa entity_type i niekwalifikowana asercja string/Json doctest) zachowano; poprawiono test bez zmiany oczekiwań. Końcowe dev 7/7 PASS (15,67 s), właściwy ASan 7/7 PASS (42,69 s), Chromium E2E 16/16 PASS z nową asercją keyboard→catalog→graph messages przez real server (receipt chat-browser-7evi1rqh), web build PASS (1,92 s), native CLI/server build PASS. To bramki zakresowe; pełna bieżąca macierz nadal wymaga odbioru. Następny krok przed nowym produktem: bieżący full dev z build receipt PRZED uruchomieniem, JUnit 10MiB i guard; znane błędy C/Git-temp nie są zwolnieniem ani PASS. Przed tą operacją zapisujemy trwały commit/checkpoint. Opis źródła/mapping/evidence/permission w method_graph, zagnieżdżony selector referencji, generyczne discovery i polityki cache/live/write-back pozostają otwarte; nie przedstawiamy ich jako przełączników działających.

Przed checkpointem doprecyzowano mapping_version dla odrzuconego, ale zadeklarowanego profilu: nadal wskazuje jego loom.runtime_profile_overlay/1, nie export-3. Dodano dokładną asercję. To drobna nowa zmiana po opisanych 7/7; bieżący katalog dev/ASan oraz pełny dev zostaną wykonane po rebuildzie, zamiast zaliczać wcześniejszy wynik jako identyczne drzewo. Pliki wygenerowane nie były zmieniane.

Dalsze domknięcie unknown-profile przed full gate: odrzucony overlay ma graf składni JSON (syntax_only), z węzłem /future_mapping/preserve i wersjonowanym value_ref. Wartości nieklasyfikowane pozostają w dokładnym źródle, a nie plaintext eksportu; parser nie udaje profilu executable. Regresja sprawdza dostęp do wartości przez ten sam verified read_unit/pointer oraz brak runtime:profile w niepewnym wyniku. Ta ostatnia zmiana wymaga nowych bramek; wcześniejszy katalog 1/1 dev 7,56s / ASan 37,13s był stanem sprzed syntax_only i nie zastępuje ich.

R42 review przed pełną bramką: rodzaje węzłów i predykaty projekcji są teraz w kanonicznym runtime/resource_projection.pack; embedding wyłącznie gen_runtime_profiles.py (generate + --check PASS). Ten sam istniejący resolver działa dla każdej konfiguracji. Snapshot zapisuje effective descriptor/hash, a IDs uwzględniają hash projekcji. Nowa regresja zmienia rodzaj wiadomości i parent relation przez dane, zachowuje raw conversation i analyzer hash, odrzuca pusty lub usunięty obowiązkowy klucz i zachowuje poprzedni graf. Dokładne R42 wyjątki są opisane w kontrakcie; nie dodano allowlisty, nie przenoszono ślepo nazw schematów/operacji/formatów standardowych. Wyniki syntax_only sprzed tej zmiany: dev 7/7 29,38s, ASan 7/7 45,77s, E2E16/16 (chat-browser-qw92enex); zachowane, ale nie są aktualną bramką nowego presetu. Nowe scope tests i full dev będą wykonane z nowego buildu.

B-RESOURCE-002 aktualny zakres po R42 vocabulary: pełny build dev GCC13 WERROR i scoped ASan build PASS; generator --check PASS; dev 7/7 PASS, 32,09s; ASan preset 7/7 PASS, 46,11s; Chromium E2E 16/16 PASS, receipt chat-browser-a1oalm5e. Wszystko lokalnie, 0 płatnych modeli/CI. Ten commit jest trwałym checkpointem zakresu przed pełnym dev/guard (nie odbiorem całego wymagania ownera). Następna operacja: run-resource-full-dev.sh, przypina Git/tree/binaria i rzeczywisty cache przed CTest; schema core/storage unchanged, nowe dane to istniejące nodes/links i jeden kanoniczny preset. Nie ma migracji destrukcyjnej ani kasowania wcześniejszych projekcji. W pełnej macierzy ASan po wcześniejszym teście UAF fix nie było jeszcze kolejnego 146-suite przebiegu; nowa ścieżka resource/profile ma aktualne scoped ASan, nie fałszywy full PASS.

## Końcowy odbiór zasobów i trwały checkpoint — 2026-10-09

Opublikowane nowe commity produktu: `ad6269a5` (shared resource graph reads) i `b20c0d8a` (external RuntimeProfile, unknown JSON tree/value_ref, data-defined projection vocabulary); push obu PASS. Runtime/profile/catalog/source tests są rzeczywiste, nie tylko mock graph. Databaza core i istniejące odtwarzanie bez zmiany schematu; cache projection używa istniejących nodes/links, nie zapisuje do źródeł. Link nie zachowuje kopii surowego ZIP-a; po odczycie zapisuje jednak pochodną projekcję, także tekst rozmowy. To ujawniony snapshot/cache, nie obietnica całkowitego braku lokalnego payloadu. Niezależne opcje cache/retention/live/write-back wymagają kolejnego przyrostu.

Aktualny pełny dev na `b20c0d8ac37934e1600c3b9216492fd524220941`, tree `54a1544cc29307e8c23fb02eb4172c988015ec7e`: 145/146, 265,49s. Receipt przed testami, stdout/JUnit 10MiB; rzeczywisty cache: vendored SQLite, GCC13 Debug-g1, WERROR (lokalne odchylenie od system-SQLite presetu jawnie zapisane). Guard REJECT: wyłącznie research.structure z 8 błędami HybridHandoffTests, twarde /tmp we fixture C objęte markerem Git. 1271 przypadków tego zestawu nie zaliczono do przechodzących. Zaakceptowane wykonania pozostałych wejść: 928 native / 32953 asercje, 605 Python / 0 skips. Opt-in catalog_scale nadal niewykonany. Nie edytowano C ani nie usunięto zabezpieczenia. To nowa rzeczywista bramka, a nie wynik historyczny.

ASan aktualnej ścieżki resource/profile 7/7 (46,11s), E2E aktualnego runtime/server 16/16 (chat-browser-a1oalm5e), generator --check PASS. Pełny 146-entry ASan po nowych zasobach, release, TSan i osobny preset vendored nie były wykonane; wcześniejszy full ASan i naprawa UAF pozostają oddzielnymi dowodami. Nie deklarujemy pełnej macierzy zielonej.

Rzeczywisty CLI R42 inventory checkoutu również wykonany (nie pomylony z testem narzędzia): początkowo REJECT, m.in. stale proof embeddingu. Dokładny trusted generator plan potwierdził nowe bytes; odświeżono tylko generated output SHA i kompletną listę wejść (26), także dwie już istniejące canonical sources chat_reasoning/context_goal_cues pominięte wcześniej. Revision product_literals 1→2, generator identity unchanged. Allowlist, scopes i exclusions identyczne. Compat test narzędzia po zmianie 1/1 PASS, 14,17s. To naprawa metadanych dowodu, nie zwolnienie źródła z guardu.

Końcowy rzeczywisty inventory nadal REJECT: 559 plików / 433 product, 66594 kandydatów, 6 allowed / 66588 unclassified, 140 blocked files z nieobsługiwanymi konstrukcjami lexerów/encoding, 0 stale allowlist, 1 verified generated file. Nie nazywamy wszystkich nierozliczonych kandydatów naruszeniami ani nie ukrywamy braków pokrycia. Zero source writes/network/generator-output writes w CLI. Pomocniczy import Pythona utworzył dwa pyc w scope src; przeniesiono własne artefakty do .onboarding/python-cache bez kasowania historii, powtórka końcowa nie zawiera tego szumu. Negatywy zostały zachowane. Logi/manifesty/receipt/JUnit: .onboarding/logs/data-graph-resource-*. Pełne candidate inventory pozostaje lokalnym artefaktem, nie nową szeroką allowlistą.

Otwarte pierwszego scenariusza całościowego: pełna graph interpretation wszystkich unknown values i credential_ref, ordinary-chat transparent reference hydration, permission-aware context/analysis, nested ZIP selector i transport remote, niezależne polityki retention/eager/cache/live/write-back. Wspólne discovery/adapter evidence/permission i produced_by muszą wejść przez istniejący MethodRegistry + loom.method_graph/1 / loom.method_run_trace/1, nie nowy silnik. Te same zadania Basic/Advanced/Expert, ExperimentSpec/opt-in trigger/queue/adoption i pozostałe ID A nadal mają wcześniejszy jawny backlog. Nowe commity zmniejszają R15/R20/R21, nie zamykają wszystkich wymagań ownera.

Następny konkretny krok: dodać data-defined descriptor operatora catalog.read_resource do istniejącego method registry, przygotować i związać jego rzeczywisty run/result z produced_by, źródłem/version/selector/mapping, walidacją oraz osobnym uprawnieniem; wykazać brak dispatch/egress dla niezatwierdzonego mapowania. Przed nową implementacją zachowany jest ten sprawdzony, opublikowany checkpoint. Integracja main pozostaje wyłącznie u Claude’a.

Metadane proof i końcowy raport: `362cb8b9`, push PASS; remote heads potwierdzone zgodne z lokalnymi dla chatadhd, Watchdog i AGEDS, wszystkie siedem drzew czyste. Paczka dowodów do pobrania: `data-graph-engine-2026-10-09-resources-evidence.zip` (208216 bytes, SHA256 dd360519586e6a09362701a869179d0d6db5cfc46c45e10f8df6d7a5c5c979c0), obok `data-graph-engine-2026-10-09-resources-manifest.json`. Manifest przypina head 362cb8b9 i test-source b20c0d8a, zawiera 71 pozycji (logi/receipt/JUnit/guard, kontrakt, raport i format-patch). Pełne candidate inventories nie są w ZIP-ie; pozostają lokalnie. Wcześniejsza paczka `data-graph-engine-2026-10-09-evidence.zip` nie została nadpisana. Push działał, więc bundle ratunkowy nie jest wymagany. Późniejszy commit wpisu o paczce nie zmienia kodu ani dowodów tego checkpointu.

## A3-DISC-001 — równoważne DTO i rzeczywisty pakiet C

Wspólny dokładny comparator z istniejącego GraphPacketStore przeniesiony bez poszerzania reguł do src/util/json_value.h i użyty przez MethodRegistry. Kolejność kluczy obiektów nie zmienia danych; kolejność tablic, nieznane pola, precyzja i dopuszczalne typy nadal podlegają walidacji. Nie ma usuwania unknown ani sortowania/zapisu źródłowych bytes. Immutable duplicate/source/manifest checks używają tych samych reguł. Oddzielny błąd konsumpcji kontraktu: bind_results zachowuje jawne availability i sprawdza input_sha256 wobec prepared trace; model-origin/response verification bez osłabienia.

A public native repro na pakiecie C z c525d0e9: przed 13 PASS / 4 FAIL / 1 BLOCKED; po 17 PASS / 0 FAIL / 1 BLOCKED. Naprawione DISC-NATIVE-07/09/10 i DISC-C2-01 (A3-DISC-001). DISC-PROVIDER-04 jest niezależnym, nadal niewiązanym konsumentem, nie PASS. Źródło artifactu i native archive przypięte w .onboarding/method-fix/{before-receipt,after-receipt}.json; zero transportów modeli.

Aktualna bramka zakresowa złożonego drzewa: unit.test_method_registry PASS; compat.test_graph_packet_store PASS (170,02 s). Przyjęcie tego przyrostu nie zalicza całego uruchomienia 18-entry: 17/18, guard REJECT przez regresję unknown-JSON na granicy importera ZIP, naprawianą osobno; pełne logi/dirty-worktree receipt zachowane jako data-graph-integration-audit-fixes-*. Nowe testy rejestru obejmują real accept→zamknięcie→reopen, precision/unknown/array negatives, brak dispatch przy unavailable, manifest ordering i typed outcome/input identity. Nie zmieniono storage ani historii zaakceptowanych danych.

## A3-IMP-CH003/004/006 — rzeczywiste konsumowanie formatu

Domknięto **A3-IMP-CH004** (próg rozmiaru nie zmienia rozpoznania obiektu JSON ani bezpośredniej tablicy wiadomości) i **A3-IMP-CH006** (HTML zachowuje kolejność źródłową zamiast grupowania po roli) w istniejących importerach Python i native. **A3-IMP-CH003 domknięto częściowo**: nieznane mapowanie ma jawny `UnrecognizedConversationStructure(status=unknown)` / `Errc::Unsupported`; rozpoznany pusty eksport nadal daje pusty wynik. Końcowy scalar tablicy nie znika podczas streamingu. Konsument generic ZIP zachowuje nieznany JSON przez istniejącą ścieżkę raw retention, raportując `unrecognized` / `unsupported`; rzeczywisty błąd parsowania/odczytu pozostaje `partial` i nie staje się cache-hit. Nadal otwarte: publiczne wyniki per-member w legacy Python ZIP oraz pełna bezstratność, referencje i selekcja kontekstu tego starszego importera. Obiekt JSON i pojedyncza rozmowa mogą wymagać przetworzenia całości w pamięci; nie deklarujemy dla nich stałego zużycia pamięci.

Jawna decyzja migracyjna: legacy parser **1→2**, bez zmiany schematu storage i bez zmiany provider `export-3`. Wcześniejsze źródła, rozmowy i provenance pozostają nienaruszone; kolejny import tworzy osobną interpretację v2, a dalsze importy v2 korzystają z jej deduplikacji. Wspólna wersja legacy oznacza, że ponowny import dowolnego źródła v1 może utworzyć nową rozmowę v2. Alternatywę pozostawienia v1 i wymagania `force` odrzucono, ponieważ nie rozróżniałaby zmienionych interpretacji. Testy obejmują realny Database/BlobStore, zamknięcie i odtworzenie, surowe bajty, historię v1 oraz przerwanie i ponowienie odczytu tablicy wiadomości; brak płatnych wywołań.

Dowody przed zmianą: niezależny runner A na `387afe08587f179d47c013a2ea518ff4b68e36bc` potwierdził trzy reprodukcje i trzy FAIL akceptacji (`logs/b-audit-repros-20261009/python-importers.json`). Osobny rzeczywisty `loom_compat_tool` potwierdził unknown i tablicę wiadomości >5 MB jako błędny sukces z zerem rozmów (`import-fix/native-json-observation.json`); reprodukcja końcowego scalara ma 5 000 005 bajtów (`import-fix/scalar-before.json`). Po zmianie Python **9/9 PASS, 2,079 s** (`import-fix/scalar-after.log`). Pierwszy złożony scoped gate: **17/18 PASS**, w tym native importer/stream, Python↔native compat i nowe regresje; zachowany FAIL `import_presets_binding` wykrył opisany adapter ZIP (`logs/data-graph-integration-audit-fixes-tests.log`). Build4 po jego poprawieniu PASS; bieżący `unit.test_import_presets_binding` PASS (0,47 s), receipt i JUnit `data-graph-integration-final-scope-*`. Cały scope 20/23, guard REJECT przez trzy niezależne oczekiwania integracyjne katalogu/operatora/presentation store. Nie przedstawiamy zakresowego PASS importera jako pełnej bramki drzewa.

## B-D-INTEGRATION-001 — wspólny mapper i wykonywalna bramka D

Przyjęto opublikowany stos D `1d3d1331` → `3b44a982` → `e485c8f7` → `6ed509a4` przez cherry-pick -x, odpowiednio `e34c4bf7`, `08edcb7c`, `8b8496d3`, `b402384e`. Publiczny kontrakt mappera z handoffu D wdrożono osobno po stronie produktu: `loom.export_mapping/1` jest cienką projekcją istniejących lossless parserów. Nie tworzy Runtime, DB ani plików; zachowuje raw, relacje i nieznane pola. Normalizowane timestamps zależne od zegara pominięto jawnie, źródłowe wartości pozostają raw. Dostępność assetów jest not_evaluated. To ten kontrakt konsumuje referencyjny katalog B. Nie włączono automatycznie transportów/discovery D ani jego permissive defaults jako zgód produktu.

CTest rejestruje `resource_graph.contract` w buildzie shared. Evidence policy revision 2 dodaje dokładne powiązanie z unittest; 0 cases/skips/brak dowodu nadal powodują REJECT. Testy polityki 29/29 PASS. Bieżący build4: publiczny mapper unit PASS (4 przypadki), D 133/133 PASS bez skips (12,32 s), receipt `data-graph-integration-final-scope-*`. Cały scope miał trzy niezależne FAIL, zachowane i naprawiane osobno. ASan shared=OFF nie zawiera tej bramki FFI; towarzyszący dev/libloom.so nie będzie nazywany instrumentowanym. Brak migracji storage/ABI, źródła D bez lokalnych poprawek.

## B-RESOURCE-003 / CH-RES-N002 — operator, retencja i poprawne copy

Rzeczywisty `catalog.read_resource` przechodzi przez MethodRegistry, method_graph/run_trace, GraphPacketStore i trwały TaskEngine. Descriptor `resource_method.pack` jest danymi; ślad wiąże source/hash/selector, source_index, parser/mapping, effective config, read scope, recognition/coverage, validation i produced_by. Brak executora jest unavailable; odmowa odczytu daje Auth przed dotknięciem źródła, dispatch i zmianami DB. Uprawnienie hosta do lokalnego odczytu nie jest zgodą na model egress. Snapshot i receipt są atomowe: trigger SQL przerywa zapis, nie zostawia nowej projekcji ani częściowego receipt, stare dane pozostają, retry działa.

Niezależny istniejący RuntimeProfile `resource_read` wybiera snapshot/transient oraz sketch/none. Defaults snapshot/sketch zachowane. Wariant transient+none na nowym store ma projekcję tylko w pamięci; TaskEngine zapisuje reference/receipt zamiast treści. Testy publicznych ZIP-ów obu providerów sprawdzają realne DB/WAL/blobs/logi i tworzenie plików tymczasowych, reopen, niedostępność/zmianę źródła, porównanie identycznej projekcji ze snapshot. Stare index/cache są zachowane; zmiana polityki nie jest obietnicą czyszczenia historii. Adapter może czytać cały kontener; unit_bytes jest zmierzone, container_io=not_instrumented. Receipt domyślnie ma dane `implicit_context_eligible=false`, wspólny filtr KnowledgeStore działa przed LIMIT i jest używany przez runtime/context/C API. Starsze runy bez pola zachowują dotychczasowe zachowanie; odrębna zgoda na udostępnienie modelowi nadal wymagana.

CH-RES-N002: rzeczywisty Catalog copy korzysta z pełnego istniejącego provider model/writer, zachowując rodziców, branch/status/groups/attachments/raw unknown. Test porównuje full importer, copy i resource projection, odtwarza store, sprawdza niedostępne źródło oraz rollback/retry. Source-array ordinal jest jawny i ściśle typowany; stare rekordy bez niego pozostają unit_relative. Projection descriptor revision 2 wersjonuje zmienioną interpretację, poprzednie wyniki/importowane kopie nie są nadpisywane. Scanner fingerprint wersjonuje odświeżenie ordinal. CH-RES-N001 ordinary link getter nadal zwraca placeholder; nie zamykamy całego P4.

Bramka aktualnego drzewa po build6: 23/23 wpisy, 63,05 s; guard PASS z 22 rzeczywiście wykonanymi i jednym catalog_scale jawnie opt-in/niewykonanym. Receipty przed testami obejmują brudne drzewo i hashe zmienionych plików, stdout/JUnit zachowane w `data-graph-integration-final-scope-r2-*`. Obejmuje nowe mapper/copy/transient/operator/context/perspective testy i wcześniejsze catalog/onboarding/import-presets. Wcześniejsze 20/23 zachowane: dokładnie poprawiono stare oczekiwanie transformu parsera, revision packa i kod wymuszonego SQLITE_CONSTRAINT_TRIGGER (Conflict wraz z pełnym komunikatem). Żadnych pominięć ani zmian timeoutów. To bramka zakresowa, pełne dev/ASan/E2E następują osobno. R42 proof runtime embeddingu odświeżony przez generator: 28 wejść, revision 3, bez zmian allowlist/scopes/exclusions.

## B-E-INTEGRATION-001 — perspektywy w istniejącej aplikacji

E `3eaac295` przyjęty wcześniej jako `4c190de7`; osobna integracja B podłącza go do App/KnowledgeWorkbench i istniejącego UserProfileHost. Canonical graph_perspective.pack jest produkcyjną definicją R40, domyślnie disabled; demo E nie zostaje workflowem produktu. Basic włącza gotową perspektywę, Advanced edytuje te same składniki przez NativeLayerClient/default layers. Zakres obejmuje realny KnowledgeStore, nie jeszcze zewnętrzne źródła Catalog ani pełny P5 workflow. Headless i renderer używają tego samego selektora/ref/version/permissions. Unsupported struktura/szczegółowość/grouping pozostają jawnie unavailable; brak pomiarów nie staje się rankingiem.

Pack user revision 5 wygenerowany przez gen_onboarding_pack.py. Stary profil pozostaje na revision 4 do jawnej instalacji. Addytywne OnboardingStore::install_entries wykonuje istniejący CAS natively; poprzednie pack/scenario nigdy nie wracają przez zaokrąglone liczby JavaScript. Testy zachowują dokładny integer 9007199254740993, typ 1.0, unknown metadata, override/disable/exclude po aktualizacji i reopen; stale CAS/kolizje odrzucane. Brak nowego resolvera, parsera UI albo C ABI.

Rzeczywisty App E2E wykrył współdzielony race: dwa panele anulowały sobie queued read sesji i przechodziły w reloadRequired. Konsument używa teraz odłączonej kopii ostatniego potwierdzonego native snapshot, bez dodatkowego odczytu/replay na notification. Niepewny zapis nadal blokuje zmiany i wymaga jawnego reload. Test wysyła prawdziwy zapis, gubi odpowiedź, sprawdza zero automatycznych prób i późniejsze odzyskanie. Naprawiono też wyłącznie błędne oczekiwania harnessu: pre z rzeczywistym preview zamiast dowolnego słowa resource, hover/tap realnego koła SVG zamiast pustego środka grupy, oczekiwanie acknowledged native checkbox zamiast optimistic UI. Timeouty i asercje zachowane; wcześniejsze negatywy mają osobne logi/DOM/screenshoty.

Bramki: native perspective/onboarding w scope23/23 oraz full dev (jego jedyny FAIL to oddzielny kb_pack). E headless18/18, native-layers11/11, adapters10/10 grup, real exporter compatibility PASS; produkt headless8/8, generator20/20 i --check, tsc/web PASS. Bieżący host Chromium21/21; rzeczywisty App Chromium17/17 (`data-graph-E-app-native-ack-final.log`, receipt `chat-browser-7llz48l0`), hover/focus/tap, identyczne headless/UI refs, reload/trwałe wykluczenie, model/context/privacy bez zmian i zero dodatkowych model dispatch. Transport odpowiedzi czatu jest kontrolowany lokalnie; to nie pomiar jakości LLM.

Cały pierwszy full dev:154/155,291,83 s, guard REJECT wyłącznie kb_pack po przyjęciu obcych plików danych D/E. Naprawa granicy domen jest kolejnym oddzielnym przyrostem; nie utożsamiamy powyższych scope PASS z pełnym odbiorem złożonego drzewa.
