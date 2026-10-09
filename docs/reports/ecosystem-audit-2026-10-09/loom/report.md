# Audyt loom — 2026-10-09

Baza: `main@0b23fa64c1de955c947349feef7bb67c05763f97`. Repo publiczne. Zachowany baseline SQLite v4 i EventBus. Wykryte zachowania nie mogą być automatycznie przeniesione na aktywny kernel ChatADHD.

## Zakres i relacje

Sklasyfikowano **21/21 śledzonych plików**. Semantycznie prześledzono **9/9 plików kodu produktu**. Rozkład: build_ci_metadata: 3, documentation_license: 6, product_code: 9, tests_fixtures: 2, vendor_placeholder: 1. Manifest z hashem i liczbą linii w `coverage.json`. Nie obejmuje to pełnej historii git ani wizualnego audytu rastrowych zasobów.

README main w 0b23fa64 jawnie zachowuje wcześniejszy standalone compatibility baseline i kieruje bieżący kernel/workbench/reports do klb-t/chatadhd. Repo nie jest oznaczone archived przez GitHub, lecz rola kodu jest historyczna. To relacja lineage/aktualny następca, nie tożsamość repozytoriów.

docs/PORT_AUDIT.md opisuje wcześniejszy port i nieudawane API. Wcześniejsze obserwacje nie są dowodem bieżącego zachowania ChatADHD; findingi w tym raporcie potwierdzono tylko w SHA standalone.

ECOSYSTEM.md to brainstorm potencjalnego współużycia; żaden build dependency na te produkty nie występuje.

Nie ma AGENTS.md, CLAUDE.md ani centralnego STATE/INDEX w tym drzewie. Właściwe lokalne reguły i stan wymieniono w coverage.json. **Wszystkie wymienione tam pobrane gałęzie mają zero commitów nieobecnych w main**; nie utworzono backlogu ze starej kolejki.

## Ustalenia

| ID | Klasyfikacja | Priorytet | Ustalenie |
|---|---|---|---|
| LOOM-001 | naruszenie | P1 | Próg 20 znaków blokuje analizę krótkich wiadomości; licznik ma inny predykat |
| LOOM-002 | naruszenie | P1 | Polityki kolizji i tożsamości cicho zachowują albo nadpisują dane |
| LOOM-003 | naruszenie | P2 | Batch pomija wiadomości bez tekstu, nawet z załącznikiem |
| LOOM-004 | naruszenie | P2 | Domyślne dane i polityki SQLite są literałami, choć większość nie jest twardym limitem |
| LOOM-005 | naruszenie | P1 | Otwarcie bazy bezwarunkowo oznacza ją jako wersję 4 |
| LOOM-006 | niepewne | P3 | Graf to prymitywy przechowania, nie podłączony model całej aplikacji |
| LOOM-007 | dopuszczalny mechanizm | P3 | Słownictwo mechanizmu i formaty zgodności mieszczą się w wyjątkach R42 |

### LOOM-001 — Próg 20 znaków blokuje analizę krótkich wiadomości; licznik ma inny predykat

Klasyfikacja: **naruszenie**. Lokalizacja: `src/db_messages.cpp:156–162`.

**Dowód:** get_unanalysed_messages ma SQL semantic_status=pending AND status=active AND length(text)>=20; count_pending_semantic liczy wszystkie pending. API udostępnia limit liczby rekordów, ale nie próg długości lub predykat kwalifikacji.

**Zachowanie / wpływ:** Dla trzech pending (krótkiej aktywnej, długiej aktywnej, długiej excluded) licznik zwraca 3, selektor 1. Krótka aktywna wiadomość nie może być analizowana tą metodą nawet przy zwiększeniu limit. Ukryta polityka wykluczenia i licznik sugerujący pracę, której konsument nie może pobrać.

**Wymaganie:** R42, R41, Zadanie A: filtrowanie/braki, docs/PORT_AUDIT.md:77. **Interpretacja audytora:** Problem historyczny potwierdzony w aktualnym standalone main, nie skopiowany jako niezweryfikowany backlog. Nie oznacza, że występuje w aktywnym chatadhd/loom; tam wymagany odrębny test.

**Alternatywy i stan:** Długość >=20 + aktywne — jedyna polityka selektora; Wszystkie pending — istnieje wyłącznie w liczniku; Konfigurowalna kwalifikacja z powodem wykluczenia — brak.

**Dane/graf → konsument:** Semantic queue policy/method parameters z jednolitym eligible predicate i osobnymi licznikami total/eligible → Database.get_unanalysed_messages, Database.count_pending_semantic, zewnętrzny klient C++.

**Rekomendacja:** Opisać stary predykat jako legacy policy; w aktywnym silniku sprawdzić i ujednolicić policzalne eligible jobs.

**Test akceptacyjny:** Dla długości 0/1/19/20 i statusów active/excluded/version count eligible==selectable; polityka progu=1 dopuszcza krótkie aktywne; total pending może pozostać osobnym jawnie opisanym licznikiem.

**Ryzyko migracji:** Zachować threshold=20 jako profil legacy; zmiana domyślnej kolejki może zwiększyć wolumen i koszt przyszłego konsumenta.. **Zależności:** Weryfikacja odpowiednika w aktywnym chatadhd/loom; Wersjonowany profil kolejki.

**Weryfikacja:** Native audit reproducer PASS: count_pending_semantic=3, get_unanalysed_messages=1; wcześniej SQL probe dawał ten sam wynik. Pełne źródła biblioteki skompilowano jawnie z SQLite vendor opisanym w verification.json.

### LOOM-002 — Polityki kolizji i tożsamości cicho zachowują albo nadpisują dane

Klasyfikacja: **naruszenie**. Lokalizacja: `src/db_graph.cpp:7–20`; `src/db_graph.cpp:49–51`.

**Dowód:** create_node wykonuje INSERT OR IGNORE i zwraca ID bez informacji o zignorowaniu; find_node(label,kind) LIMIT 1 nie ma rozróżniania źródeł; create_link wyszukuje (src,dst,link_type) i aktualizuje weight,metadata.

**Zachowanie / wpływ:** Wstawienie tego samego node_id z nową treścią zachowuje starą treść i sygnalizuje sukces. Dwa dowody relacji tego samego typu między tymi samymi węzłami kończą w jednym rekordzie z metadanymi drugiego. get_or_create utożsamia label+kind. Utrata nowego payloadu albo poprzedniego provenance; API nie daje jawnego wyboru reject/keep/merge/version ani sygnału kolizji.

**Wymaganie:** R15, R35, R42, Zadanie A: deduplikacja i automatyczne przyjmowanie inferencji, docs/DECISION_POLICY.md:5-17. **Interpretacja audytora:** To zachowanie zapisane w portowanym kontrakcie, nie dowód, że label+kind ustala ontologię właściciela. Zwykły upsert może być zamierzonym mechanizmem, lecz wybrana polityka konfliktu i tożsamości nie jest parametryzowana ani audytowalna.

**Alternatywy i stan:** Legacy keep-first node / replace-link metadata — aktywne; Reject collision z details — brak; Versioned evidence / multiedge — brak; Merge z provenance i rozstrzygnięciem — brak.

**Dane/graf → konsument:** Identity/dedup/conflict policy oraz relation assertion/evidence nodes lub równoważna bogata reprezentacja grafowa → Database.create_node, Database.find_node, Database.get_or_create_node, Database.create_link.

**Rekomendacja:** Zweryfikować aktywny kernel testami konfliktów; utrzymać legacy jako nazwany tryb i dodać wynik operacji created/unchanged/conflict/updated.

**Test akceptacyjny:** Dwie deklaracje tej samej relacji z odrębnymi źródłami zachowują oba źródła albo jawnie odrzucają konflikt; kolizja node_id nie zwraca nieodróżnialnego sukcesu; label+kind nie scala bez wybranej polityki.

**Ryzyko migracji:** Nie zmieniać po cichu kompatybilności starego ABI/v4; oferować jawny adapter legacy i raport utraty wcześniejszych metadanych.. **Zależności:** Aktywny odpowiednik grafu; Kontrakt tożsamości i evidence.

**Weryfikacja:** Native audit reproducer PASS: oba create_node zwracają ten sam ID/sukces, zawartość pozostaje stara; drugi create_link nadpisuje metadata pierwszego. Dodatkowo probe rzeczywistych SQL PASS.

### LOOM-003 — Batch pomija wiadomości bez tekstu, nawet z załącznikiem

Klasyfikacja: **naruszenie**. Lokalizacja: `src/db_messages.cpp:56–91`; `src/db_messages.cpp:7–14`.

**Dowód:** Pętla batch_create_messages ma if(m.text.empty()) continue przed zapisem attachments_json/metadata_json. Pojedyncze create_message odrzuca pusty text jawnym invalid_argument.

**Zachowanie / wpływ:** Wiadomość reprezentowana samym załącznikiem nie trafia do DB w batchu; metoda zwraca tylko liczbę dodanych, bez listy pominiętych ID/powodów. Pojedyncza ścieżka zgłasza błąd, wsadowa przechodzi dalej. Dane wejściowe można utracić bez wystarczającego śladu; silent skip jest odmienny od jawnego błędu i ogranicza reprezentowalność wejścia.

**Wymaganie:** R14, R18, R15, Zadanie A: traktowanie braków, docs/DECISION_POLICY.md:5-17. **Interpretacja audytora:** W samym standalone repo nie ma wykonanej implementacji importera; to potwierdzona semantyka prymitywu batch, nie dowód o utracie konkretnego archiwum użytkownika.

**Alternatywy i stan:** Batch skip empty text — aktywne; Reject invalid entry with reason — tylko pojedynczy insert; Payload z attachment bez tekstu — nieobsługiwany; Quarantine/report skipped items — brak.

**Dane/graf → konsument:** Ingestion policy + typed message payload + import outcome/loss trace → Database.batch_create_messages, Database.create_message, przyszły lub zewnętrzny importer.

**Rekomendacja:** Zachować możliwość starej walidacji jako profil; nie używać prymitywu legacy jako dowodu bezstratnego importu.

**Test akceptacyjny:** W batchu wiadomość attachment-only zostaje zachowana lub jawnie odrzucona z pozycją i powodem; return report rozlicza każde wejście. Single i batch mają tę samą jawną politykę.

**Ryzyko migracji:** Zmiana starych liczników i interfejsu wyniku; nie rekonstruować odrzuconych dawniej załączników bez źródła.. **Zależności:** Kontrakt message payload w aktywnym kernelu; Adapter legacy.

**Weryfikacja:** Native audit reproducer PASS: attachment-only batch zwraca sukces i inserted=0; pojedynczy pusty insert zwraca błąd. Nie używano rzeczywistych archiwów użytkownika.

### LOOM-004 — Domyślne dane i polityki SQLite są literałami, choć większość nie jest twardym limitem

Klasyfikacja: **naruszenie**. Lokalizacja: `include/loom/db.h:89–142`; `src/db.cpp:27–32`; `include/loom/db.h:17–70`.

**Dowód:** New Chat, entity, related, weight=1.0, limity 50/100/200 i batch 1000 są domyślnymi argumentami/struct values. open() narzuca busy_timeout=30000, WAL, synchronous=NORMAL. Nie ma konfiguracji danych produktu.

**Zachowanie / wpływ:** Wywołujący może nadpisać wiele argumentów (limit, typ, weight), ale nie politykę otwarcia bazy. Zmiana domyślnych wartości dla wszystkich klientów wymaga rekompilacji. Brak wspólnego pochodzenia i dziedziczenia domyślnych. Mylne byłoby nazwać limit=200 maksymalną pojemnością grafu: API przyjmuje większą wartość.

**Wymaganie:** R42, R40, Zadanie A: nie utożsamiać domyślnego z ograniczeniem. **Interpretacja audytora:** R42 pozwala na słownictwo SQL/klucze i wartości stanu mechanizmu; nie pozwala na tytuł UI, domyślne typy domenowe, timeout lub politykę trwałości tylko dlatego, że występują w SQL. Stary schema v4 wymaga celowej migracji, więc adapter kompatybilności zachowuje swój kontrakt jawnie.

**Alternatywy i stan:** Domyślne argumenty i jawne override call-site — aktywne częściowo; Warstwowy profil defaultów — brak; SQLite system/bundled — jawne opcje CMake, bundled bez źródła daje błąd.

**Dane/graf → konsument:** Legacy defaults preset + storage durability/timeout profile; resolve value z provenance warstwy → Database API, Database.open, init_schema_unlocked/migrate_unlocked.

**Rekomendacja:** Sprawdzić odpowiadające ścieżki aktywnego silnika; opisać legacy defaults w danych, zachowując opcjonalne argumenty jako mechanizm override.

**Test akceptacyjny:** Zmień limit i tytuł przez preset, bez rekompilacji; jawny override wygrywa; zapytanie limit=1000 zwraca >200 węzłów; zmiana timeoutu/trwałości przechodzi walidację i zapisuje effective profile.

**Ryzyko migracji:** Bez zmiany historycznych wyników domyślnych; ważna granica nowego profilu vs kompatybilność istniejącej bazy v4.. **Zależności:** Aktywny resolver defaultów; Adapter storage policy.

**Weryfikacja:** Native audit reproducer PASS: domyślnie list_nodes zwraca 200, jawny limit=300 zwraca 201 z 201 węzłów. Argument limit jest domyślną wartością, nie pojemnością grafu. Polityki storage prześledzone statycznie.

### LOOM-005 — Otwarcie bazy bezwarunkowo oznacza ją jako wersję 4

Klasyfikacja: **naruszenie**. Lokalizacja: `src/db.cpp:153–193`; `src/db.cpp:40–46`.

**Dowód:** migrate_unlocked dodaje brakujące kolumny/indeksy i kończy set_meta_unlocked(schema_version,4); nie odczytuje wcześniejszej wersji. open wywołuje je przy każdym otwarciu.

**Zachowanie / wpływ:** Baza ze znacznikiem schematu nowszego niż 4, ale kompatybilnymi tabelami, jest ponownie oznaczana jako 4 bez porównania wersji. Brak jawnej decyzji reject/adapter/migrate. Utrata informacji o wersji i ryzyko otwierania danych nowszego kernela przez zachowany baseline; ważne wobec odrębnych repo o nazwie Loom.

**Wymaganie:** Zadanie A: zachowanie przy wyjątkach/brakach, R15, docs/DECISION_POLICY.md:45-47. **Interpretacja audytora:** Overwrite potwierdzono natywnym wykonaniem na kontrolowanej scratch bazie z markerem 999. Nie otwierano bazy rzeczywistej nowej wersji ChatADHD; nie przypisujemy automatycznie niezgodności jej aktualnemu formatowi.

**Alternatywy i stan:** Repair columns + force schema_version=4 — aktywne; Reject future version — brak; Explicit known version adapter — brak.

**Dane/graf → konsument:** Schema migration plan z from/to version i migration result evidence → Database.open, Database.migrate_unlocked, Database.schema_version.

**Rekomendacja:** Oznaczyć standalone jako nieprzeznaczony do otwierania przyszłych formatów i dodać guard w razie wznowienia/wykorzystania komponentu.

**Test akceptacyjny:** Fixture zawierający schema_version=999: open odmawia bez modyfikacji znacznika i danych; zaakceptowane legacy wersje migrują z zachowaniem zawartości i jawnego journalu.

**Ryzyko migracji:** Nie otwierać realnych baz użytkownika testowo; użyć fixture i kontrolowanego snapshotu.. **Zależności:** Inwentaryzacja formatów aktywnego ChatADHD; Zgodność lub jawny brak zgodności.

**Weryfikacja:** Native audit reproducer PASS: scratch DB oznaczoną schema_version=999 biblioteka otwiera z sukcesem i zmienia znacznik na 4. Nie otwierano rzeczywistej bazy użytkownika ani bieżącej bazy ChatADHD.

### LOOM-006 — Graf to prymitywy przechowania, nie podłączony model całej aplikacji

Klasyfikacja: **niepewne**. Lokalizacja: `include/loom/loom.h:44–48`; `CMakeLists.txt:11–16`; `docs/PORT_AUDIT.md:86–97`.

**Dowód:** loom_get_config/loom_set_config/loom_get_models są deklaracjami C ABI; źródła budują tylko db.cpp/db_messages.cpp/db_graph.cpp/event_bus.cpp. docs/PORT_AUDIT.md wprost informuje, że symbole poza zaimplementowanymi modułami nie są udawane.

**Zachowanie / wpływ:** Node.kind, link_type, metadata_json pozwalają zapisać dowolne etykiety/JSON. Nie ma wykonujących się profili, metod, modeli, promptów, eksperymentów, context buildera, UI ani warstw defaultów. Nie można zaliczyć deklaracji API lub dowolnego JSON jako pokrycia R15/R40/R41. Nie ma też podstaw oceniać zachowania niewdrożonego UI.

**Wymaganie:** R15, R40, R41, Zadanie A: rzeczywiste podłączenie, README: standalone compatibility baseline. **Interpretacja audytora:** Niepewna kwalifikacja jako naruszenie produktu: to jawnie zachowany poprzedni baseline, nie aktualna aplikacja. Potwierdzony zakres możliwości i ograniczenie; brak implementacji jest ujawniony. Backlog aktywnego kernela wymaga odrębnej analizy.

**Alternatywy i stan:** Generyczne node/link metadata CRUD — zaimplementowane; Pełny C ABI i application graph — deklarowany, niewdrożony tutaj; Aktywny kernel w chatadhd/loom — wskazany w README, odrębny zakres audytu.

**Dane/graf → konsument:** Mapa zdolności baseline→aktywna implementacja; nie nowa architektura obok R15/R40/R41 → C++ Database API, C ABI bez definicji poza deklaracją.

**Rekomendacja:** Zachować repo jako oznaczony baseline, a naprawy wymagające aplikacji kierować do aktywnego kernela po porównaniu kodu.

**Test akceptacyjny:** Każdy deklarowany capability w raporcie ma test linkowania/wykonania i realnego konsumenta albo jawny status unsupported; nie wystarcza nagłówek.

**Ryzyko migracji:** Fałszywe utożsamienie repo grozi backportowaniem starej semantyki do rozwiniętego kernela.. **Zależności:** Odrębny audyt chatadhd/loom.

**Weryfikacja:** Sprawdzono całe 21-pliki drzewo/9 plików produktu i deklaracje C ABI. db_compat_test i event_bus_test PASS przy bezpośredniej kompilacji; niewdrożonego C ABI/UI nie uznano za działające.

### LOOM-007 — Słownictwo mechanizmu i formaty zgodności mieszczą się w wyjątkach R42

Klasyfikacja: **dopuszczalny mechanizm**. Lokalizacja: `include/loom/event_bus.h:14–22`; `src/db_messages.cpp:151–153`; `src/db_internal.h:130–153`; `CMakeLists.txt:25–34`.

**Dowód:** Nazwy message:created/node:created to zdarzenia mechanizmu; status active/version/deleted/pending/done steruje cyklem życia; SQL identyfikatory odpowiadają kontraktowi SQLite v4; format ISO UTC i JSON odpowiada serializacji. bundled SQLite bez źródła kończy konfigurację błędem.

**Zachowanie / wpływ:** EventBus obsługuje dowolną nazwę zdarzenia, snapshot handlers i re-entrant callback; nie wymusza katalogu domenowego. create_node/link przyjmują dowolny kind/link_type, więc domyślne entity/related nie stanowią zamkniętej ontologii. Nie należy generować pozornych naruszeń z każdego literału ani usuwać jawnych błędów bootstrapu.

**Wymaganie:** R42 wyjątki 1,2,3,4,6, Zadanie A: dokładny test R42. **Interpretacja audytora:** Kategorie dopuszczalne: 1 klucze/wersje kontraktu, 2 gramatyka SQL/JSON, 3 lifecycle/event operation names, 4 format daty i identyfikatora wymagany zgodnością, 6 developer diagnostics. Wyjątek 5 nie został przywołany: nie znaleziono tu potrzeby uzasadniania nim polityki danych. Defaulty entity/related/New Chat nie objęte tym findingiem (LOOM-004).

**Alternatywy i stan:** Dowolne nazwy event/kind/relation jako parametry — aktywnie reprezentowalne; Zamknięty enum typów dziedzinowych — nie narzucony; System/bundled SQLite — jawnie wybrane przy build, bez cichego substytutu.

**Dane/graf → konsument:** Machine contract pozostaje w kodzie; user-facing labels ewentualnego UI w danych → EventBus.subscribe/emit, Database.create_node/create_link, db_internal.now_iso8601_utc, CMake SQLite selection.

**Rekomendacja:** Zachować mechanizm i opisać wyjątki per lokalizacja; utrzymać rozdział od polityk LOOM-004.

**Test akceptacyjny:** Własna nazwa event oraz nowy typ node/relation przechodzą API; słownictwo kontraktu ma allowlist category+reason; biblioteka nie ogranicza typu do wartości domyślnej.

**Ryzyko migracji:** Automatyczna zamiana kontraktów może złamać ABI/schema, dlatego nie stosować ślepego usuwania literałów.. **Zależności:** Brak; zachować jako wyjątki z uzasadnieniem w skanerze.

**Weryfikacja:** event_bus_test i db_compat_test skompilowane lokalnym c++ -std=c++20: PASS. Typy node/link pozostają dowolnymi stringami; native audit reproducer potwierdza override limitu. Nie wykonano CMake/CTest ani sanitizerów.

## Granice i wznowienie

- Początkowa kompilacja z systemowym SQLite była zablokowana brakiem sqlite3.h. Znaleziono istniejący vendor w chatadhd@9e20f99; bez modyfikacji repo i bez pobierania zależności skompilowano pełne źródła. db_compat_test, event_bus_test i 6 natywnych audit checks PASS. CMake/CTest/sanitizery nieuruchomione.
- EventBus skompilowany oddzielnie c++ -std=c++20 i event_bus_test: OK.
- SQL probe uruchomiony Python sqlite3: 5 sprawdzeń, w tym 3 zachowania rzeczywistych SQL z kodu i 2 kontrole przepływu źródła. To nie pełny test C++/integracji.
- Historia zachowana w git; przegląd bieżącego drzewa i ancestry gałęzi nie jest audytem wszystkich historycznych rewizji.
- Brak UI/model requestów w implementacji tego repo. Obserwacje dotyczące brakującego pełnego grafu są sklasyfikowane niepewne z uwagi na jawną rolę zachowanego baseline.

Przed naprawami porównać odpowiedniki LOOM-001…005 w bieżącym chatadhd/loom. Baseline pozostawić zachowany; ewentualny guard wersji lub adapter legacy tylko jako jawna decyzja maintenera. Brak nieprzyjętych przyrostów na dostępnych gałęziach tego repo.

Wymagania właściciela z zadania A stosujemy jako aktualne kryteria. Cytat właściciela jest null w rekordach: treści kodu/README nie są cytatami jego intencji. Interpretacje i rekomendacje są oddzielone od obserwacji. Nie edytowano produktu, schematów ani STATE/INDEX. Przegląd B/C jest zadaniem końcowej integracji audytu w chatadhd, nie twierdzeniem o ukończeniu w tej części.

Skan automatyczny: 9 plików, 1068 linii, 802 kandydatów; patrz `scan/summary.json`. To sygnały do weryfikacji, nie liczba naruszeń. 

Końcowa weryfikacja natywna: `native-probe-result.json` i `verification.json`; odtworzenie `python checks/run_native.py --repo <loom-checkout> --sqlite-dir <sqlite-amalgamation-dir>`. Skrypt nie pobiera zależności, pracuje na tymczasowych bazach. PASS oznacza reprodukcję zachowania starego baseline, nie zgodność z testami akceptacji przyszłych napraw.
