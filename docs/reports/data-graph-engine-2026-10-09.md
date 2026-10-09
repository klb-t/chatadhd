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

Próba odkrycia dodatkowych repo: `gh repo list klb-t --limit 100 --json name,defaultBranchRef` → GraphQL Forbidden. Dostępne repo nie są blokowane. Gałęzie A/C nie pojawiły się podczas fetchu; nie czekamy na ich publikację.

Przeczytane instrukcje AGENTS/CLAUDE, STATE, INDEX i kontrakty R39–R42. Integracja 2026-10-05 zawiera już rejestr metod, profile runtime i prezentację W12. Historycznych bramek nie zaliczamy jako nowych. Nie edytujemy loom/tools/structure. Nie wywołujemy modeli, płatnego CI ani nowych usług.

## Kolejny konkretny krok

Sprawdzić ochronę danych ograniczonych max_detail/max_sensitivity: brak metadanych nie może omijać limitu z grafowej polityki. Użyć istniejącego resolvera i OnboardingStore; zachować nieograniczone defaults. Baseline testów onboardingu uruchomiony; wynik przed zmianą do dopisania.

## Gotowe przyrosty

Brak — checkpoint nie jest gotowym przyrostem.

## B-POLICY-001 — klasyfikacja danych przy limitach prywatności

Decyzja implementacyjna (odwracalna): przy wskazaniu pola jego zapisane detail/sensitivity stanowią dolną granicę klasyfikacji sprawdzanej przy istniejącym limicie. Jawne wyższe wartości żądania też są sprawdzane. Alternatywa: ufać tylko klasyfikacji caller’a — odrzucona, bo pominięcie lub obniżenie wartości omijało limit. Bez pola i bez metadanych ograniczona kategoria daje InvalidArgument; nieograniczone defaults zachowują wynik. Nie zmieniono wartości polityki ani budżetu.

Pion: onboarding.privacy w packu/override → istniejąca walidacja → R40 resolution → privacy_decision / policy_decision → filtrowany model_request → SQLite reopen. Nowa regresja korzysta z rzeczywistej bazy i istniejącej projekcji grafu, bez transportu/modeli.

Wykonane: baseline 7/7 onboardingu (47,58 s), build GCC13 Debug WERROR shared CLI/server vendored SQLite, po zmianie 7/7 onboardingu (43,32 s). Logi: /workspace/.onboarding/logs/data-graph-{baseline,build,caps-tests}.log. Pełna bramka i web dopiero uruchamiane; nie deklarujemy ukończenia.

Punkt wznowienia: kod i regresja gotowe roboczo; przed commitem wykonać pełny CTest + guard i build web. ASan uruchomić osobno. Odkrycie repo także REST users/klb-t/repos → Forbidden.

Wykonano dodatkowo build web PASS oraz niezależny Chromium E2E 16/16 PASS (system Chromium 151, oryginalny npm run e2e; screenshoty zachowane poza Git). B-POLICY-001 nie zamyka CH-004: ordinary chat nadal wymaga osobnego podłączenia zgód. Audyt A pobrano jawnie z baa9e30c; fetch domyślny obejmował wyłącznie main. C: 6988085. Żadne źródła A/C nie zostały scalone.

Pełny CTest trwa. Pierwszy przebieg ma timeout research.contracts przy równoległej kompilacji ASan; build ASan przerwano, limit 60 s zachowano. Konfiguracja ASan najpierw nie znalazła dynamicznych bibliotek sanitizerów w lokalnym sysroot; podłączono istniejące systemowe ABI libasan.so.8 i libubsan.so.1 i ponowna konfiguracja PASS. Pełny build/test ASan nadal nieukończony. Historii/negatywów nie nadpisujemy.

Commit B-POLICY-001: `d92d7cc` (push PASS). Bramka zakresu 7/7 + web/E2E PASS; pełna macierz pozostaje otwarta, commit nie jest odbiorem integracyjnym całego stosu.

## CH-003-B — wspólny snapshot analizatora (roboczy checkpoint)

Audyt A rzeczywiście pobrany z `baa9e30c12a29ab7f14fc060a13676b1bd35b036`. Priorytet P1 zastępuje wcześniej planowaną etykietową migrację W12-DIC-0013. Runtime używa istniejącego create_from_data_dir. Live graph i worker rozwiązują istniejący profil semantic_analyzer raz na operację / drain batch; ten sam obiekt dostarcza reguły fallbackowi SemanticLLM. Hash overlay zapisujemy także przez istniejący mark_analysed (bez zmiany schematu storage). Legacy call analyse(text) zachowuje jawnie przekazany analizator konstruktora. Nie refaktoryzujemy badań C.

Decyzja implementacyjna: granica snapshotu = operacja live / cały drain; zmiana pliku działa przy kolejnej operacji. Alternatywy: freeze na całe życie Runtime (utrudnia edycję) albo reload dla każdego rekordu (miesza wersje w batchu). Builtin wynik pozostaje bez nowych pól; provenance hash dodawany tylko dla rzeczywiście zmienionego profilu. CH-003 nie obejmuje CH-002 (legacy prompt/3000), CH-005 (koszty transportu) ani CH-006 (admission relacji).

Pierwszy pełny CTest przerwany po części przebiegu; zachowany log i timeout contracts nie są bramką PASS. Przerwanie wrappera nie zatrzymało od razu potomnego CTest/Ninja; potwierdzono PID i wysłano SIGINT bezpośrednio do własnych procesów. ASan build zatrzymał się na 83/300. Przed kontynuacją: ukończyć build zmienionego runtime, regresję real runtime i pełny CTest bez konkurującej kompilacji. Fetch brakującego historycznego b118c80 zakończył się poprawnie; testy C mogą teraz mieć mniej braków historycznych. Nie zmieniono ich kodu.

CH-003-B: build GCC13 WERROR PASS; 9/9 zestawów native związanych z runtime/analyzatorem/workerem/grafem/storage PASS (2,40 s), 2/2 sentineli Python↔native semantic/db PASS (6,27 s). Nowa regresja rzeczywistego Runtime sprawdza dwie nakładki bez zmiany algorytmu, identyczny hash live/worker/fallback, snapshot całego batchu mimo aktualizacji packa przez callback rzeczywistego EventBus, blokadę błędnej nakładki bez dodatkowych mock requests, zachowanie pending i udane wznowienie po naprawie oraz reopen. Użyto wyłącznie ScriptedTransport; 0 rzeczywistych modeli.

CH-003 zamknięty w zidentyfikowanych natywnych ścieżkach real-time/worker/fallback. Publiczny legacy analyse(text) celowo używa analizatora konstruktora; runtime tworzy go ze sprawdzonego startup packa. Nie deklarujemy zmiany wszystkich odrębnych transportów batch ani komponentów Python. Pełna macierz odbioru całego stosu nadal wymaga nowego przebiegu po następnej migracji.

## W12-DIC-0013 — etykiety projekcji grafu (roboczy checkpoint)

Osiem etykiet/template’ów przeniesiono do istniejącego ui.pack (EN/PL), konsumowanego przez DefaultLayers.presentation w rzeczywistej project_graph. Pack user revision 3→4, entry prezentacji revision 2→3; builtin.inc i web generated/ui.json wyłącznie przez gen_onboarding_pack.py. Domyślne teksty pozostają takie same. Override locale/tekstu zmienia etykiety, nie method/version/parameter hashes ani bindingi. Suppression daje pustą etykietę, bez ukrytego English; brak wymaganego template daje jawny błąd, zachowując źródło do korekty. Starsza kompletna nakładka katalogu bez nowych kluczy wymaga jawnego uzupełnienia; nie jest nadpisywana fallbackiem.

Pierwsza regresja testowa zbyt szeroko sprawdzała attrs.layer, obejmując również default nodes. Negatyw zachowany; poprawiono adresowanie asercji do rodzaju warstwy z istniejącego vocabulary (bez zmiany oczekiwanych wartości). Przy podniesieniu pack revision dostosowano dwa testy pinujące poprzednie 3 do bieżącego 4, a ich upgrade nadal faktycznie podnosi revision do 5. Bramek po tej zmianie nie przenosimy z poprzedniego przyrostu.

W12-DIC-0013: generator --check PASS, aktualny build GCC13 WERROR PASS, 8/8 zestawów onboarding + generator PASS (48,45 s), build web PASS (2,43 s). Test natywnej projekcji udowadnia zachowanie angielskich etykiet, zmianę przez polski katalog/nakładkę, niezmienność method bindings/attrs oraz jawny błąd brakującego klucza i brak English po wykluczeniu. Dotychczasowe testy store potwierdzają retencję exclusion przez upgrade/restart i niezmienną politykę/model_request przy suppression. To zamyka dokładnie grupę 0013, nie cały CH-008.
