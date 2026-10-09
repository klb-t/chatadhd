# Zadanie B — DATA / GRAPH / ENGINE — 2026-10-09

## Aktualny punkt kontynuacji — sprawdzony produkt 6d1231bd, 2026-10-09

Ta sekcja zastępuje historyczne „następne kroki” niżej. Gałąź pozostaje
`gpt/data-graph-engine-2026-10-09`; main i cudze gałęzie bez zmian. Kontynuacja
zachowała checkpoint `387afe08587f179d47c013a2ea518ff4b68e36bc` i późniejsze prace.
Aktualny sprawdzony kod runtime: **6d1231bd9a20304564eb233f478d1ace33a5df31**.
Późniejszy `02714d5b` aktualizuje dokładny locator audytu R42 i raport, bez zmiany
tego runtime. Bazowe SHA i fetch są zachowane poniżej. Źródła/przyjęcia C/D/E,
autorstwo, zależności i bramki wskazuje [manifest integracji](data-graph-engine-2026-10-09-integration.json).
Integracja main pozostaje u Claude’a; ten checkpoint jest kandydatem do A/Claude’a,
nie deklaracją ukończenia całego zakresu właściciela.

P4a native `568c3f7e` + App `ed4bd2fc` jest wdrożone: rzeczywisty ChatView i zadanie
bez UI odczytują referencję przez istniejący Catalog/MethodRegistry i pełny mapper.
Są source/version/produced_by, copy/link message+parent+version-group parity,
jawne niedostępności, zachowane lokalne edycje oraz obsługa klawiatury/focus/tap.
Transient/none ma sprawdzony brak trwałej kopii payloadu w badanym fresh store;
nie usuwa wcześniejszego indeksu/snapshotu i nie obiecuje częściowego I/O ZIP-a.
Stare gettery pozostają storage-only. Lokalny odczyt nie uprawnia do model egress;
ChatView odmawia source-history send do czasu wdrożenia P4b.

Po P4a opublikowano wiring/scheduling `9b6618d7`, fixture generatora `48977dd7`,
przenośne wygenerowane chunki `bb1a943b`, test Clang/nodiscard `02371b01` oraz
dokładne porównanie limitu odpowiedzi `6d1231bd`. Generator zachowuje identyczne
bajty istniejących definicji/provenance, także duże liczby i nieznane pola;
nie wyłączono WERROR ani ostrzeżenia o ponadlimitowym literale.

Aktualna pełna lokalna macierz na czystym, zamrożonym `6d1231bd`:

| Bramka | Wynik i rzeczywisty zakres |
|---|---|
| dev/GCC13, r6 | **160/160 PASS**, guard PASS, 159 wykonanych + catalog_scale opt-in; 317,30 s |
| vendored/Clang19 | **160/160 PASS**, guard PASS, 159 wykonanych + catalog_scale opt-in; 341,65 s |
| ASan+UBSan/GCC13, r2 | **159/159 PASS**, guard PASS, 158 wykonanych + catalog_scale opt-in; 1287,51 s |
| Świeży web/E2E | **15 komend testowych PASS** + build i pretest receipt; `B-final-web-_8sxv4h7`, bez driftu source/native/dist/cache; screenshoty odtworzone |
| JNI host/JDK17 | **1/1 PASS**, 13 wewnętrznych kontroli, CTest 1,57 s; aktualna biblioteka dev |
| Generatory runtime/onboarding | oba **--check PASS**, bez ręcznej edycji wyników |

Dev i Clang: każdy 991 native / 36 947 asercji / 2038 Python / 0 pominięć Python.
ASan: 991 native / 36 946 asercji / 1905 Python / 0 pominięć. Jego shared=OFF nie
rejestruje `resource_graph.contract` (133 Python); tę bramkę D wykonały dev i Clang.
ASan używa osobno przypiętego zwykłego dev FFI companion — nie jest to pokrycie
instrumentowanej biblioteki współdzielonej. `catalog_scale` jest jawnie
opt-in/niewykonany, nie zaliczony jako wykonany test. CLI ASan działał sam przez
**242,83 s**, z niezmienionym timeout300 i wszystkimi sanitizerami. Ciężkie
buildy/bramki były szeregowe. Source/config/bin receipts powstały przed testami;
pełne logi/JUnit/guard i hashe wejść są zachowane. Liczb presetów nie sumujemy
jako unikalnego pokrycia. Zbiorczy odczyt: `.onboarding/B-matrix-6d1231bd-metrics-r2.json`.
Jego sekcja R42 zachowuje pierwsze inventory; bieżące liczby R42 poniżej pochodzą
z `.onboarding/R42-anchor-review/final-review.json` i nie nadpisują tej historii.

R42: commit `02714d5b` przesuwa jeden dokładny locator niezmienionego literalnego
identyfikatora kontraktu; scope **1/1 + 42 Python PASS**,14,13 s. Inventory r2 ma
**valid=false**: 68 564 kandydaty, 6 dokładnych wyjątków, 68 558 nierozliczonych,
153 pliki z blokadami lexerów, 6547 issues, 0 stale allowlist i 0 discovery errors.
To otwarty przegląd kandydatów, odrębny od poprawności runtime; nie każdy kandydat
jest udowodnionym naruszeniem. Nie dodano szerokiej allowlisty.

Historyczne negatywy zachowano: dev r5 159/160 REJECT, trzy kolejne błędy buildu
Clang (literal, nodiscard w asercji, konwersja limitu do double), starszy pełny
ASan 155/156 REJECT przez CLI timeout300,10 oraz wcześniejsze błędy harnessów web.
Ich późniejsze naprawy mają osobne scope/receipty. Izolowany historyczny CLI PASS
nie zastępuje nowego pełnego przebiegu. Release i TSan są oddzielnymi presetami,
niewykonanymi tutaj; JNI/APK nie stanowią testu fizycznego urządzenia.

Watchdog: WD-003 `263c8ab` / WD-001 `05e4f290` / WD-011 `bc2778e1` są opublikowane
z pełnymi bramkami624/630/636. Nowe A4-WD-001 `6be9aed` odrzuca nieobsługiwane
kontrole HTTP przed kolejką; A4-WD-003 `f2b6b89` odrzuca niejednoznaczne wejścia
starszego JH16 przed pominięciem missing, zgodnie z istniejącym MethodSpec.
Poprawne wyniki są zachowane, nowy executor1.1.1 ma jawne provenance. Po scopes20
i35 oraz niezależnych review pełna złożona bramka lint/build/test na `f2b6b89`:
**644/644 PASS**, 0 fail/skip/cancel, 65.637 s; receipty
`data-graph-watchdog-submission-full-*`, bez driftu. A4-WD-002 pozostaje otwarte.
Pełny log ma dokładną nazwę `data-graph-watchdog-submission-full.log`, bez
przyrostka `-tests`. Dokumentacyjny checkpoint Watchdoga `8772114` zapisuje ten
wynik; nie zmienia przetestowanego produktu `f2b6b89`.

AGEDS ma rozbieżność publikacji request pinning: właściciel wskazuje wykonany
przyrost, lecz jego SHA/handoff nie odnaleziono w sześciu dostępnych refs; lokalne
i opublikowane B to `3878b537`. Należy odzyskać ten konkretny artefakt, bez
ponownej implementacji na podstawie starego backlogu. Wykonanego ASR recipe ani
ratio.exclude nie powtarzamy. Dowód: `.onboarding/ageds-publication-check/STATUS.md`.

Setup cloud draft **revision8** zapisano i odczytano kontrolnie. Publikacja przez
użytkownika i test nowej instancji pozostają pending. Lokalne setup gates:
Keyboard974, LEM3, AGEDS411 bez fail/skip; AR build-only/APK4ABI. Podpisy APK
sprawdzone, źródła przed/po identyczne; brak testu urządzenia/żywej jakości ASR/LLM.
Zero płatnych modeli/CI, prywatne korpusy i sekrety nie są artefaktami publikacji.

Następny odblokowany przyrost **P4b.1**: jawny native/headless fragment źródła przez
wspólny ContextEngine na clean store, z rzeczywistym DefaultLayers/R40 i
metadata-only receipt; przygotowany kontrakt/probe nie oznacza jeszcze wdrożenia.
Kolejny P4b.2 musi połączyć ten sam MethodRegistry z trwałym śladem bez payloadu,
autoryzacją źródła i dokładnego odbiorcy oraz zwykłym ChatEngine. Otwarte pozostają
P4 ordinary-chat/egress, D generic discovery i external resource→E, P5 kompletny
workflow/ExperimentSpec/opt-in/adoption, osobne ścieżki CH-011 batch/legacy CAS
oraz nierozliczony inventory. Nie przedstawiamy lokalnego widoku jako ukończenia
całego scenariusza źródło→kontekst→rozmowa.

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

## Historyczny plan — nie traktować jako aktualnego backlogu

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

## B-DATA-DOMAINS-001 — fizyczny katalog nie aktywuje cudzych danych jako KB

Pełna integracja wykryła konflikt, którego oddzielne testy D/E nie obejmowały: loader KB czytał każdy JSON w loom/data, a dziesięć nowych dokumentów D/E nie należało do jego pack.json. Zachowano negatyw154/155. Odwracalna naprawa: oddzielny data_domains.pack rozlicza dokładne path/schema/raw SHA256/domain/role/validation_refs dla dziesięciu plików. Pack::load_dir sprawdza integralność i granice ścieżek, następnie wydziela te dokumenty przed walidacją KB. Nie dodaje demo packet/source ani permissive policy D do KB; pliki/hash/defaults embedded KB są identyczne. In-memory/from_documents i overlay zachowują wcześniejszy strict kontrakt.

To rozdzielenie fizycznego katalogu, nie allowlista R42 ani walidator semantyki D/E. Właściwe kontrakty nadal sprawdzają ich istniejące zestawy. Brak/zmiana/schema mismatch/unknown sibling/duplikat/overlap z KB/unsafe path powodują błąd; żadnego pomijania całych katalogów. Pack.json/pack_embedded.inc pozostają bez zmian. Aktualizacja źródła D/E wymaga świadomego odświeżenia jego dokładnego wpisu po odpowiednich bramkach.

Build7 wykrył wyłącznie niejawne porównanie std::string/Json w nowej asercji doctest; jawne get<string> zachowuje oczekiwanie, build8 PASS. Bieżące unit.test_kb_pack, unit.test_kb_data_domains i resource_graph.contract:3/3 PASS,48,07 s, guard3 executed/0unexecuted; receipty `data-graph-integration-data-domains-*`. 5 nowych przypadków/subcases dowodzi identycznej tożsamości KB i wszystkich negatywów powyżej. Następna operacja: nowy pełny dev na czystym produkcie, następnie oddzielny build/full ASan.


## B-RESOURCE-004 — prawdziwa retencja indeksu i wznowienie skanu

Dodatkowa kontrola integracyjna wykryła błąd none→sketch→none: pierwszy refresh zapisywał sketch, pozostawiając etykietę none i puste title/head/attachments; następna zmiana mogła fałszywie oznaczyć taki historycznie indeksowany store jako pozbawiony payloadu. Regresja przed zmianą: retention-before-r2,5 przypadków,4 PASS/1 FAIL (33 asercje). Osobna hipoteza o treści błędu parsera nie potwierdziła się: istniejący wrapper zwraca bezpieczne invalid JSON; canary przez rzeczywisty katalog/kolejkę/store był już PASS. Nie dopisano zbędnego wrappera ani nie nazwano tego naprawą wycieku. Pierwszy test z błędnym oczekiwaniem raw wyjątku pozostaje w historii logs.

Jawny kontrakt catalog.index_retention/1 wersjonuje wiedzę o historii, a nie parser źródła. Aktualny indeks ma poprawne content_index; świadome włączenie sketch odtwarza także pola wcześniej pominięte przez none. Sprawdzenie historii poprzedza shortcut identycznego fingerprint. Starsze rekordy none bez tego dowodu mają jawny konflikt unknown history; nie da się na podstawie obecnych pustych pól udowodnić braku payloadu w dawnym WAL. Nadal można jawnie włączyć sketch albo rozpocząć świeży catalog store; niczego nie czyścimy ani nie wybieramy za użytkownika. Prawidłowe defaults i source IDs pozostają, fingerprint powoduje jednorazowe odświeżenie. Brak migracji usuwającej dane.

Drugi defekt dotyczył wznowienia: częściowy JSON albo odmowa odczytu całego pliku/elementu ZIP pozostawiały done=true. Kolejny scan mógł pominąć nierozwiązany problem. Przed poprawką rzeczywisty C API probe dla plain text i ZIP potwierdził force→warning/done=1, a następnie dwa odtworzenia Runtime→warnings=[]/done=1; body niezmienione. Teraz niepełny lub odrzucony scan zapisuje done=false wraz z bieżącym input hash. Już poprawne jednostki i poprzedni snapshot zostają, ponowienie nadal zgłasza problem, a świadoma naprawa polityki pozwala wznowić pracę.

Dowody lokalne: resource-operator/retention-whole-before.json i ten sam retention_whole_probe.py; logs/data-graph-integration-retention-before-r2-* oraz retention-fixed-* (10/10, guard PASS przed dodaniem ostatniego non-JSON subcase). Końcowy scope i pełna macierz są osobnymi bramkami po ostatniej zmianie. R42: catalog.index_retention/1 jest wersjonowanym kontraktem technicznym, pola/stany to istniejąca serializacja/mechanizm, komunikaty są diagnostyką; brak nowych ukrytych wartości polityki i allowlist.

## WD-003 — kolejny opublikowany przyrost ekosystemu

Watchdog-JH16: produkt263c8ab, raport867f83b na tej samej nazwie gałęzi B; baza main58a0c93, kontynuacja61ae246. Dane możliwości providerów definiują walidowane parametry liczbowe/całkowite/bool, a rzeczywisty ProfileGenerator/AssistantService przekazuje je do adapterów. Efektywne warstwy provider/task/request/reservation mają hash i provenance, submitted-body hash oraz credential_ref w istniejącym ledgerze. Niesprawne wywołanie zachowuje przygotowaną konfigurację/rezerwację, nie udaje sukcesu. Brak nadpisywania model/messages/system/budget/routing przez parametry; sprawdzanie przed wysyłką i rezerwacją. Domyślny bezpośredni adapter bez overrides zachowuje wszystkie16 dotychczasowych body; naprawione przesyłanie task temperature0 opisano jako bugfix dotychczas gubionej polityki, nie nowy default.

Przed: bezpieczna reprodukcja rzeczywistego generatora potwierdziła utratę temperature/seed i provenance. Po: focused32/32, a następnie pełne test:all — lint, build, receipt source/config/dist przed testami,624/624 PASS/0skip,64,534 s wraz z Chromium. Real SQLite close/reopen, zmiana danych bez przebudowy algorytmu i canary sekretów; wyłącznie kontrolowane transporty. Logi/receipt data-graph-watchdog-params-full-*. WD-001/template i inne findingi A pozostają otwarte; nie powtarzano ratio.exclude ani AGEDS.

B-RESOURCE-004 końcowy build PASS; scope po ostatniej zmianie **11/11**,16,56 s, guard PASS:10 wykonanych + catalog_scale jawnie niewykonany. Transient suite ma7 przypadków z dodatkowymi subcases plain/ZIP, odmową, retry i reopen; obejmuje też wcześniejsze snapshot/copy/operator/index i mapper/import-presets. Receipt przed testem z hashem roboczych źródeł: data-graph-integration-retention-final-*. Kod zamrożony do nowego pełnego dev, potem serial ASan/Clang.

Pełny dev po B-RESOURCE-004 na czystym bb4dbf50: **156/156**,265,61 s, guard PASS:155 wykonanych + catalog_scale jawnie opt-in/niewykonany;968 native/36166 asercji,2022 Python/0skip. Receipt source/config/bin przed testem, prefix data-graph-integration-composed-r3-full-dev. Niezależny C API after sprawdził6/6 prób plain/ZIP z retry/reopen i identycznym zachowanym body. Historyczna blokada Android artifact503 nie odtwarza się w29/29 krótkich sprawdzeniach; to dostępność, nie PASS buildów. Następuje osobna naprawa potwierdzonych A2-BSEM-001 i CH-011, bez zaliczania tej bramki jako wyniku ich późniejszych zmian.


## A2-BSEM-001 / CH-011 — aktualnie potwierdzony następny pakiet

Niezależny C API probe na bb4dbf50, rzeczywisty Runtime/worker, zero HTTP: wadliwy graph_ingest powoduje log błędu, lecz processed=1/errors=0/pending=0 i trwałe done przy zerowym grafie; reopen nie wznawia pracy, jawne requeue po naprawie tworzy poprawny graf. Osobno runtime otwarty z nakładką A po reset{} lub usunięciu pliku nadal używa A zamiast builtin. Istniejący analyzer_profile_hash A pozostaje poprawnie zapisany — nie powielamy historycznego zarzutu o brak tego hasha. Receipt b-worker-triage-20261009/worker-capi.json zawiera rzeczywisty hash biblioteki/source, bezpieczne kontrolki i wynik po shutdown/flush. Pierwsza instrumentacja czytała nieodświeżone strony zewnętrznym SQLite podczas otwartego Runtime; zachowana jako instrumentation-first, nie zaliczona jako dowód.

A2 poprawka jest osobnym małym przyrostem przed failure-state. CH-011 wymaga także propagacji błędów SQL z ingest_common i atomowego związania grafu z ukończeniem; samo wywołanie checked wrappera nie wystarcza. Historyczne done nie będą automatycznie requeue, bo poprawna pusta analiza może mieć taki sam status. Trwały błąd/niepewne wykonanie musi pozostać odróżnialne od done i nie uruchamiać samoczynnie nowego płatnego dispatch po restarcie. Batch trwałość i legacy admission nie zostaną zadeklarowane jako wykonane bez osobnego dowodu.


## A2-BSEM-001 — reset profilu z zachowaniem jawnego wstrzyknięcia analizatora

Naprawa rozdziela techniczne wiązanie zależności, nie tworzy resolvera ustawień. Runtime jawnie wybiera AnalyzerBinding::RuntimeProfile: każdy live operation/drain bierze aktualny, sprawdzony RuntimeProfile również po reset{} albo usunięciu pliku. Publiczne konstruktory GraphEngine/worker domyślnie zachowują ConstructorDefault: własny analizator przy builtin i dotychczasową obsługę nonbuiltin overlay. Publiczne SemanticLLM::analyse(text) pozostaje związane z konstruktorem. Wstępna wersja ignorująca arg konstruktora nie została opublikowana; review wykrył, że odebrałaby wspieraną strategię, więc zachowano ją jawnie i dodano regresję. Nie wprowadzono fallbacku danych ani wnioskowania o bindingu z równości hash.

Actual C API before na bb4dbf50 potwierdził oba reset FAIL. Byte-identical runner po poprawce: reset i removal PASS, poprawne kontrolki/requeue PASS, HTTP0; nadal odrębny CH-011 FAIL daje całemu runnerowi exit1. Nie przedstawiamy tego exit jako pełnego PASS. Receipt a2-after-before-ch011/worker-capi.json SHA256dea9d7b75264738d2a2751ef0d505cc4bef18d5335b598240f6b6c02485b0811; przypięta biblioteka f4b6c38012237922ce301afb3cf88d6e642248c14e860d0708967778ec98d375. Native testy pokrywają reset/remove × regex/kontrolowany fallback, live+worker, bezpośredni custom constructor/overlay, historyczny wynik i reopen.

Końcowy build GCC13 WERROR PASS. Scope **11/11**,7,19 s, guard11 wykonanych/0niewykonanych PASS: Runtime/graph/worker/LLM oraz Python-native semantic/graph/profile-source sentinels; receipt data-graph-integration-A2-final-* przed testami z roboczymi hashami. Pierwszy scope8/8 zachowany jako historia poprzedniej wersji. Brak migracji store: dawne wyniki i hash A pozostają, nowe operacje po resecie mają bieżący builtin. R42: enum opisuje techniczne źródło zależności, nie nową wartość polityki produktu.


## CH-011-PY — wyjątek i wynik batch nie oznaczają sukcesu

Legacy Python worker zapisuje widoczny wyjątek, jawne batch non-success albo niepoprawną analizę identyfikowalnej wiadomości jako failed z append-only loom_semantic_failures / loom.semantic_failure/1. Zachowuje unknown provider fields i najnowsze metadata, wiąże lokalnie obserwowaną wiadomość/version/text hash, przyczynę i czas. Powtórzony wynik batch nie nadpisuje rozstrzygniętego rekordu. Zajęty klucz historii o niezgodnym typie blokuje dispatch bez utraty danych. Jeśli zapis błędu jest niemożliwy, worker pauzuje; nie wprowadzono automatycznej ponownej wysyłki. Istniejące jawne ustawienie pending pozwala ponowić, zachowując historię. Default regex, valid-empty i rzeczywisty SemanticLLM z poprawnym regex fallback nadal kończą się done.

Przed zmianą: nowa akceptacja7 testów/8 porażek asercji wraz z subtestami; poprawny default i empty już PASS. Po:9/9 nowych i8/8 wcześniejszych semantic_recovery,0 skips,0 rzeczywistej sieci. Root powtórzył oba zestawy po review: receipt plików/interpretera przed uruchomieniem, identyczne hashe po nim; root-before-receipt.json / root-after-results.json w logs/b-worker-python-20261009. R42: nowe napisy to stany mechanizmu, kontrakt dowodów/serializacja i diagnostyka; stare hardcoded polityki nie dostały wyjątków.

Brak migracji historycznych done/error ani automatycznego requeue. Ten przyrost nie rozwiązuje legacy GraphEngine połykającego błędy SQL, atomowości jego zapisu, durable executing ani CAS przy zmianie źródła. Test SQL jawnie pokazuje zachowane wcześniejsze graph writes przy failed completion. Wynik batch wiąże lokalny rekord przy odbiorze, nie udowadnia wcześniejszego przypięcia requestu. Natywny lifecycle jest oddzielnym, nadal niezatwierdzonym przyrostem.


## CH-011-NATIVE — rzeczywisty per-message lifecycle i status aplikacji

GraphEngine begin/complete/fail jest wspólną granicą istniejącego live i worker. Preflight waliduje graph/analyzer przed wysłaniem. Durable executing wiąże source/version/text hash/status, próbę, dokładny graph profile i pochodzenie; completion używa tego samego snapshotu danych. Zapis nodes/links/relation definitions, wyniku i done jest jedną transakcją. SQL errors propagują się; poprawny pusty wynik pozostaje sukcesem. Failed zachowuje wynik/dowody, a nieudany zapis failed pozostawia wcześniejsze executing, również po reopen. Nie ma automatycznej ponownej wysyłki; jawne pending zachowuje historię. worker.pack revision2 deklaruje jedyną wdrożoną strategię explicit_requeue, embedded generowany i dokładny proof odświeżony.

Review wykrył i domknął przed publikacją: status źródła pominięty w tożsamości oraz niespójny handle conv_id/schema/source. Worker claim sprawdza istniejący warunek active, response nie nadpisuje wykluczenia, edycji/nowej wersji, requeue ani nowszej próby. Jawny all-status reindex zachowuje wcześniejszy zakres. RelationRegistry przyjmuje kolejność locków DB→registry; cache nie przeżywa rollbacku ani błędu SQL z częściowymi efektami triggera. Sygnał nowego lifecycle następuje po durable commit i zwolnieniu locków.

Worker API pokazuje trwałe failed/executing i counts_known; brak/nieznany status nie jest zerem. Rzeczywisty App/SemanticStatus używa istniejącego shared UserProfileHost i resolvera prezentacji, wcześniejszych kluczy catalog, override/exclude i potwierdzonych snapshots. Brak nowych obowiązkowych kluczy wymagających migracji starych UI packs. Failed/unknown nie są zielone, executing jest neutralne; details obsługuje hover/focus/Enter/tap. Resume nie znaczy retry/adoption.

Before: niezależny C API na bb4/A2 wykazał wadliwy graph_ingest→done przy zerowym grafie; receipt b-worker-triage-20261009. After zakresowy: GCC13/WERROR build PASS;9/9 CTest,6,48 s, guard9 executed/0unexecuted PASS (graph11,relations4,runtime7,recipe10,semanticLLM11,worker18 native cases oraz Python17 i istniejący sentinel). Receipt source/config/bin przed startem, pełne logi/JUnit/patch: data-graph-integration-CH011-final-*. Nowe native testy obejmują błędy node/link/done, rollback/reopen, wykonanie przerwane błędem zapisu failure, explicit requeue, source/profile zmienione w trakcie kontrolowanego transportu, valid-empty, odmowę przed dispatch i nieznany status. UI focused12/12 PASS, final2 receipt przed testami; jest to prawdziwy komponent/host z publicznymi kontrolowanymi wire responses, nie test natywnego worker. Cała macierz nadal wymaga aktualnego przebiegu.

Nie zmieniono DDL ani historycznych done. Nie inferujemy awarii z pustego grafu. Otwarte: osobne native batch helpers/SemanticBatchAPI (wyniki i proces-local polling), legacy Python atomowość/CAS, bool-only consumers archiwum oraz event pure ingest w cudzym outer transaction. Nowy lifecycle odrzuca outer transaction przed dispatch; powyższe historyczne wejścia nie dostały pozornego PASS. To odwracalna decyzja implementacyjna, nie nowe wymaganie właściciela. Alternatywa automatycznego retry byłaby zmianą kosztów/egress i nie jest aktywowana.

CH-011 dodatkowo web tsc/Vite build PASS (data-graph-CH011-web-build.log). Punkt wznowienia przed długą macierzą zapisany; scope nie jest pełnym odbiorem.


## Aktualny checkpoint integracyjny po CH-011

Produkt373f0774, pełny dev157/157 w281,75 s; guard156 executed + catalog_scale opt-in/unexecuted,981 native/36585 asercji,2031 Python/0skip. Receipt przed testem, stdout/JUnit/guard: data-graph-integration-composed-r4-full-dev-*. Jedynymi roboczymi zmianami były dokument kontraktu i manifest integracji, przypięte w receipt; kod produktu odpowiada commitowi.

Niezależny C API CH-01124/24 PASS i2 invariants (niezmienna biblioteka/0HTTP), uporządkowane A2 reset→enqueue2/2 PASS. Biblioteka SHA256cf9f7fbb330638834fd1f11142c2a13c78fc138d9c70eb50cd249752d97b6700. Ocena ch011-after-assessment.json SHA2561187f6aa4cef3b92e8a8b398ac5e5bcc8c4578cacc52ddfdd2f340b2704508bd. R2 raw exit1 dotyczy dwóch dodatkowych wadliwie uporządkowanych A2 prób: operacja zaczęła się przed resetem pliku, więc prawidłowo użyła A. Osobny test tworzy pusty Runtime, kończy reset, dopiero potem enqueues przez real importer; oba PASS. Pierwsza próba miała niezamykanie Python SQLite przy reopen Runtime i błąd I/O; zachowana jako błąd instrumentacji. Żadna nie została zaliczona jako pełny PASS. Nie symulowano kill procesu; przerwanie zapisu failed pozostawia durable executing, co sprawdzono po rzeczywistym zamknięciu/odtworzeniu.

Android setup odblokowany bez zmian źródeł ani testów: Keyboard192f820f974/974,lint0 errors/148 warnings; LEM1755e1df3/3 + production marker scan; AGEDS3878b537411/411 (276Android,135desktop); ARd8af2dd0assembleDebug4ABI bez testów runtime/device. Każdy APK podpis zweryfikowany, before/after hashes identyczne. Logi/świeże XML/receipty/summary w .onboarding/logs/{keyboard,lem,ageds,ar}-gate-20261009*. Setup draft revision6 zapisany, requires_publish=true; nowa instancja nie została sprawdzona. To bramki istniejących repo, nie zamknięcie ExperimentConfig/migracji dowodów ani AR device.


## CH-RES-N001 / P4a — bieżący odczyt rozmów przez referencję

Nowy additive Runtime/C ABI ConversationView używa istniejącego Catalog::execute_resource, MethodRegistry i pełnego mappera; GET metadanych nie czyta źródła, jawny POST daje tylko local-read przez istniejącego hosta. Nie tworzy drugiego parsera/rejestru/resolvera. Identyfikacja wymaga journal + SourceRecord + dwóch właściwych provenance; nowy fingerprint placeholdera współdzieli dotychczasowy writer. Historyczne pewne linki nadal działają, edytowane/wykluczone/wersjonowane lub niejednoznaczne rekordy pozostają jako binding_unresolved. Dziecko lokalnego placeholdera zachowuje rzeczywisty anchor. Utrata obu rodzajów provenance jest nadal niepewną historią storage-only, bez inferowania uprawnienia do source read.

Pełny DTO mappera jest wyłącznie pod last_successful — granica transient TaskEngine usuwa go przed trwałym wynikiem. Snapshot schema loom.resource_snapshot/2 i projection revision3 odróżniają nowy pełny odczyt od starej projekcji grafowej. Nie ma migracji DDL ani przepisywania poprzednich wyników. Defaults snapshot/sketch pozostają; transient/none jest jawnym wariantem, nie obietnicą usunięcia historycznych bytes ani fragmentarycznego I/O ZIP. Native test porównuje message fields, parent i partition grup wersji pełnego copy i link dla OpenAI/Anthropic, także po reopen; sprawdza rzeczywisty TaskEngine, store/log/temp canary, odmowy i niedostępność źródła.

Pierwszy GCC13/WERROR build PASS. Scope1 **15/17, guard REJECT**: native ConversationView9 przypadków i poprzednie zachowania/ABI PASS; nowe fixture HTTP nie miało content_type, a test profilu iterował subobject tymczasowego Json. Nie zmieniono parsera ani asercji, by ominąć wynik. Scope2 **16/17, guard REJECT**,40,73 s: poprawiony rzeczywisty HTTP→C ABI→store/restart PASS; pozostał test zakładający całkowicie niezmienny profil po jawnej instalacji packa. Istniejący update_pack dodaje audit event tej samej reguły privacy. Doprecyzowana akceptacja zachowuje całą historyczną odpowiedź i prefix, wymaga dokładnie tego zdarzenia z before==after==poprzednia reguła, revision+1 i identyczności reszty profilu. Po rebuild4 nowy test **1/1 PASS**,3,12 s,guard PASS. Żaden historyczny FAIL nie jest zaliczany jako pełny PASS. Receipty przed testami, pełne logi/JUnit: data-graph-integration-P4-scope{1,2}-* i P4-presentation-final-*.

Przegląd web wykrył i poprawił exact-wire seam (duże integer/float w JSON.parse), zbyt liberalne typy DTO, niepodniesioną rewizję fixture i brak izolacji regex workera w E2E. Dokładny natywny JSON jest pamięciowym dowodem poza serializacją view; dekodowana reprezentacja JS służy do widoku, bez round-trip źródła/settings. Build web oraz headless **9/9** PASS; actual App nadal jest sprawdzany. Pierwszy App1/9 zachowany: harness wybierał export title, podczas gdy istniejący link miał poprawny katalogowy tytuł. Zmieniany jest selektor fixture oparty o rzeczywisty native GET, bez zmiany polityki produktu.

Pełny ASan wcześniejszego produktu373: **155/156, guard REJECT**,987,54 s, wyłącznie cli.smoke Timeout300,10. Pozostałe wykonane zestawy bez zgłoszeń sanitizerów. Ten sam CLI/binary/options w izolacji: **1/1 PASS**,234,21 s,guard PASS, timeout nadal300. Nowy CTest RUN_SERIAL dla wieloprocesowego CLI zapewnia mu wyłączne zasoby testowe; nie zmienia testu, timeoutu, sanitizerów ani SQLite. Jego skuteczność wymaga nowej pełnej bramki. Pełne ASan/Clang/web/JNI aktualnego P4 drzewa nie są jeszcze zaliczone.


P4a końcowy zakres po wszystkich poprawkach: **17/17 PASS**,36,86 s,guard17/0unexecuted;77 native/6294 asercji,153 Python/0skips. Receipty data-graph-integration-P4-final-* powstały przed uruchomieniem. Runtime9 nowych przypadków i rzeczywisty HTTP server1, wraz z wcześniejszym ABI/katalogiem/retencją/R40/D. Actual App run3 **9/9 PASS**,zero model sends/remote media, stabilne source/binary, retention scan native store/log/TMPDIR i browser settings: .onboarding/p4-app-20261009-3. Run2 zachował5/9 i błąd synchronizacji harnessu: aggregate unavailable występował już podczas resetu metadata/read_denied, przed końcem POST; końcowy test czeka na dokładny resource.status i zakończenie akcji. Nie zmieniono timeoutów ani statusów produktu. Nowe pełne bramki pozostają osobnym następnym krokiem.


P4a native opublikowany jako 568c3f7e. Warstwa App jest osobnym przyrostem nad nim: jeden envelope dla wiadomości/gałęzi, per-row native/reference capabilities, brak cichych readów przy renderowaniu, exact native wire poza serializacją, jawna odmowa source-history send. Katalog presentation.conversation_view/v1 jest osobnym optional R40 entry; user.pack6 i embedded/browser bootstrap są generowane. Stare profile nie są zmieniane przy otwarciu; explicit install_entries zachowuje istniejące wartości/duże liczby/float i exclusion, z udokumentowanym istniejącym audit event aktualizacji packa. Poprzednie dwa harnessy dostosowano do additive endpoint/dependency bez usuwania asercji. npm test:product-integration uruchamia istniejące7 zestawów i nowe2; cały aggregate będzie zweryfikowany przez aktualny fullweb, a jego nazwa nie stanowi jeszcze wyniku.


## Pełny web po P4a i naprawy integracyjne harnessów

Opublikowane native568c3f7e/Apped4bd2fc. Pierwszy pełny web B-final-web-a2ly2pfl: **13/15 test commands PASS,2 FAIL** (oddzielnie build/receipt PASS). Zachowano również niezależne replay outputs. Workspace miał niejednoznaczny selector dwóch bloków pre po dodaniu receipt perspektywy E. Jawny testId przy dotychczasowym bloku wybiera właściwe dowody; wcześniejsza asercja pozostaje, dodatkowo sprawdzane są requested_run/run.id. Resource-controls harness nie obsługiwał canonical .pack?raw z integracji E; lokalny esbuild loader text odtwarza istniejące zachowanie Vite, bez kopii parsera ani zmian modułu E.

Nowy pełny B-final-web-obhyrddi: **15/15 test commands PASS**, dodatkowo build i native pretest receipt PASS, exit0. Cały rzeczywisty aggregate9 suites wykonany, wraz z App/native oraz Interface2/native; brak deklarowania brakującej capability jako PASS. Receipty przed build/test, pełne logs/command JUnit, screenshots i source/native/config/dist drift checks zachowane. Workflow instaluje zadeklarowane zależności D i uruchamia ten aggregate. Source/data nie zmieniano podczas testów; wyłącznie system Chromium151 bez pobierania nowego browsera. 39 lokalnych testów guard policy PASS; nowy server.conversation_view ma precyzyjne unittest binding w evidence policy3.


## Generator: przenośne osadzanie identycznych danych i dokładny proof

Dev r5 na9b6618d7: **159/160, guard REJECT**,301,07 s. Jedyny FAIL compat.test_onboarding_pack_generation (20 unittestów/21 błędów subtests) wynikał z brakującego conversation_view.pack w izolowanej kopii źródeł. Commit48977dd7 uzupełnia input i kontrolę wszystkich trzech outputs, z dwoma nowymi testami wspólnego źródła native/web oraz braku dryfu bazowego UI/runtime/privacy przy zmianie etykiety. Nowy scoped1/1 guard PASS,22 unittest/0skip, źródła i artifacts niezmienione; .onboarding/onboarding-generator-compat-fix-20261009. Historycznego159/160 nie zaliczamy jako PASS.

Pierwszy rzeczywisty Clang19/WERROR build odrzucił archive111237 B: sąsiadujące literały C++ mimo wizualnego podziału łączą się w jeden. Odwracalna decyzja implementacyjna: generator emituje niezależne constexpr string_view arrays i span, a dotychczasowy RuntimeProfile składa je przed tym samym parse/walidacją. Nie wyłączamy warning ani WERROR. Wszystkie27 definicji i27 provenance zachowują dokładne bajty; brak migracji storage/packów/profili, nowych fallbacków lub zmian polityki. Alternatywa sąsiadujących literałów nie spełnia aktualnej bramki.

Minimalny probe przed zmianą zachowuje ten sam Clang error. Po zmianie kompilowane/wykonane probes GCC12/13/Clang19 porównują cały katalog i syntetyczne definition+provenance ponad64KiB, Unicode, escaped NUL, unknown,9007199254740993 i1.0. Scoped generator1/1 guard PASS,18 unittest/0skip; generator --check PASS. Dowody .onboarding/runtime-profile-chunk-fix-20261009. Strażnik czyta nowy zamknięty format referencji, nadal sprawdza exact trusted plan/generator/output/28input hashes; odrzuca unknown/duplicate/unreferenced chunk i zmienione dane. Proof revision4 odświeżony obliczeniem z generatora, allowlist/scopes/exclusions bez zmian. Scope strażnika1/1 guard PASS,42 unittest/0skip, niezależny review bez blokad. To nie zastępuje pełnego bieżącego runtime/Clang/ASan.

Końcowy zakres konsumenta po GCC13/WERROR rebuild: **5/5 PASS**,15,18 s,guard5 executed/0unexecuted,18 native/465 asercji i82 Python/0skip. Prefix data-graph-integration-chunk-runtime-final-* zawiera receipt przed wykonaniem, patch/hash bieżących źródeł, pełne stdout/JUnit. Cała złożona macierz nadal jest osobną bramką.


Clang build2 po bb1a943b przeszedł generator/kod produktu, następnie zatrzymał się na CHECK_THROWS_AS w test_import_resume.cpp: ignorowany [[nodiscard]] Result, WERROR. Minimalna poprawka jawnie odrzuca wynik przez (void) wewnątrz tej samej asercji; oczekiwany std::runtime_error i wszystkie kontrole storage/wznowienia są identyczne. GCC13 rebuild PASS; nowy scope1/1 guard PASS,23 native/696 asercji,1,42 s, receipt przed testem i pełne dowody w .onboarding/import-resume-nodiscard-fix-20261009. Historyczny build2FAIL zachowany; pełny Clang wymaga kolejnego build3.


## Dokładna granica rozmiaru odpowiedzi — portability serwera

Clang build3 na02371b01: FAIL67/69 przez analysis_ui::response_limit, konwersja uint64 max do double w porównaniu z size_t max. Nie stwierdzono błędnego przyjęcia wartości na tym64bit hoście; wszystkie uint64 mieszczą się w size_t. Naprawa mechanizmu używa całkowitoliczbowego porównania po osobnym sprawdzeniu znaku. Null nadal oznacza natywną granicę; float1.0, ułamki, bool, string i ujemne wartości pozostają odrzucone. Bez zmiany danych/defaultów/storage, migracji lub wyciszenia WERROR.

GCC13 rebuild PASS. Rzeczywisty server.chat_active_task:1/1 CTest,2/2 Python scenarios,guard PASS,3,16 s; prepare/inspect/admission HTTP bez dispatch i rezerwacji. Istniejący analysis-ui harness:5/5 groups PASS,28,204 s. Natywny bridge sprawdza faktyczne Prepared.max_response_bytes, w tym signed/unsigned ponad2^53 i UINT64max, nie tylko echo JSON; wszystkie wcześniejsze asercje zachowane. Host64, brak deklaracji wykonanego32bit. Receipt źródła/config/bin poprzedza testy, dodatkowy hash wygenerowanego executora poprzedza jego uruchomienie; źródła niezmienione. Dowody .onboarding/analysis-response-limit i wskazane tam scoped logs/JUnit.

Próby konfiguracji browsera zachowane: brak spodziewanego pobranego slotu i późniejsze EROFS w /var/tmp dały brak wykonania native/0z5groups, nie PASS produktu. Nowy lokalny mapping wskazuje istniejący Chromium, browser TMPDIR jest zapisywalnym katalogiem pod /tmp; nie zmienia to oddzielnego TMPDIR=/var/tmp wymaganego przez credential fixtures w pełnej natywnej macierzy. Nie pobierano browsera ani nie pomijano testów. Kolejny etap to Clangbuild4 i pełne bramki zamrożonego drzewa.

## B-R42-ANCHOR-001 — dokładny locator po przenośnym generatorze

Po ukończeniu zamrożonej macierzy produktu `6d1231bd` zaktualizowano wyłącznie
hash pliku i offsety jednego istniejącego wyjątku `"loom.runtime_profile/1"` w
`runtime_profile.cpp`; literal, kategoria kontraktu i uzasadnienie są identyczne.
Pack audytu ma revision 5. Nie rozszerzono allowlisty ani wyłączeń. To metadane
przeglądu, bez zmiany runtime, generatora lub binariów sprawdzonych na `6d1231bd`.

Aktualny scope `data-graph-integration-R42-anchor-final`: **1/1 PASS**, guard PASS,
**42 Python / 0 skips**, 14,13 s; receipt kodu/konfiguracji/binariów i patch przed
wykonaniem. Nowe inventory `data-graph-integration-final-r2-r42` pozostaje
**REJECT / valid=false**: 583 pliki, 455 produktowych, 128 wyłączonych test/fixture,
68 564 kandydaty, 6 dokładnych wyjątków, 68 558 nierozliczonych, 153 pliki z
blokadami skanowania i 6547 issues; **0 stale allowlist, 0 discovery errors**.
Jeden wygenerowany plik ma zweryfikowany plan/hashy. Nierozliczeni kandydaci i
braki lexerów nie są automatycznie udowodnionymi naruszeniami R42. Inventory
pozostaje oddzielne od bramek poprawności runtime.

Review, pre/post hashe i wyniki: `.onboarding/R42-anchor-review/final-review.json`;
pełny raport i manifest inventory zachowano poza repo. Wcześniejsze inventory z
jednym stale anchor zachowano. Następny krok: aktualny zbiorczy checkpoint pełnej
macierzy i sprawdzenie kolejnych pionowych przyrostów, bez masowej allowlisty.
