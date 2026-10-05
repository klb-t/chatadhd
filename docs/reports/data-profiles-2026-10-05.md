# Wątek 11 — dane w profilach, przyrost 2026-10-05

Kontynuacja gałęzi `gpt/data-profiles-2026-10-04`, po liniowym rebase na `main e4109df7e4af22b461def5f7d62e268d9b9a8825`. Nowy przyrost obejmuje CLI bez otwierania Runtime, jedno źródło usage, deskryptor pochodzenia, trzy domeny importu **UNWIRED**, paired dowód W12 i strażnik literałów R42. Kod tego przyrostu opublikowano w `2ac630a72e8ddc5ad126888f7e083a5b30235db8`, drzewo `409c056b046a3d563c9093f766d9393527fde5d8`. Odbiór przez wątek 9 pozostaje osobną czynnością; gałąź nie została przyjęta na main.

Pełny build po rebase i build web zakończyły się **exit 0**. Pełny bieżący **CTest 127/127 PASS**, 885,17 s. Oba niezmienione strażniki W8 na identycznych rzeczywistych wejściach potwierdzają **783 native przypadki / 26 985 asercji oraz 1371 Python przypadków, 0 skips**. Jedyny istniejący opt-in `unit.test_catalog_scale` pozostał jawnie `unexecuted` 0/0, bez zaliczania jako wykonanie. Oryginalny JUnit jest kompletny; nie stosowano odtwarzania wyjścia ani ponownych przebiegów do domknięcia liczb. [Końcowy receipt](data-in-code/evidence/continuation-2026-10-05/final-validation/receipt.json) wiąże testowane źródła, manifest, JUnit, pełny LastTest, statusy, politykę i logi. Wszystkie 735 hashów źródeł/danych/testów pozostały identyczne. Wszystkie wywołania były offline, na publicznych źródłach, syntetycznych danych i atrapach; koszt płatnych wywołań: **0**. Nie edytowano root `README.md`, `docs/STATE.md` ani implementacji cudzych silników. Nie czytano prywatnych archiwów ani `eval/real-holdout-key`.

Pierwszy build na `30ad7d3` świadomie zatrzymano exit 130 po pełnej bibliotece rdzenia i części obiektów testowych, przed rebase na przyjęte W3/W4. [Checkpoint](data-in-code/evidence/continuation-2026-10-05/build-checkpoint-before-rebase/receipt.json) zachowuje pełny log. Nie jest liczony jako PASS. [Mapa publikacji po rebase](data-in-code/evidence/continuation-2026-10-05/final-validation/published-rebase.json) wiąże wszystkie 21 ówczesnych commitów z identycznymi drzewami publikacji; kolejne dwa przyrosty to pin strażnika W8 i guard R42. Przedrebase wyniki pozostają na gałęziach `archive/2026-10-05/data-profiles-before-main-30ad7d3` oraz `archive/2026-10-05/data-profiles-before-main-e4109df`.

| Bramka tego przyrostu | Rzeczywisty wynik |
|---|---|
| Świeży main / liniowy rebase | `e4109df7e4af22b461def5f7d62e268d9b9a8825`, nadal aktualny po końcowym fetch |
| Pełny build native | PASS, GCC 13, vendored SQLite, WERROR, shared/CLI/server/tests ON; Debug `-g0` |
| Pełny CTest / dowód wykonanych przypadków | 127/127 PASS, obaj strażnicy PASS i identyczne obserwacje; 126 wykonanych wpisów + 1 jawny opt-in |
| Web | PASS: offline `npm ci`, `tsc` + Vite, 85 modułów |
| Oba generatory `--check` | PASS: 22 RuntimeProfile + kanoniczne usage |
| Default parity CLI na nowym pełnym rdzeniu | 19 wspólnych zgodnych, 3 dodatkowe; help/version identyczne, brak nowych plików danych |
| R42 — rzeczywisty audit produktu | **NEGATYWNY**, zachowany w całości; nie blokuje testu mechanizmu ani nie oznacza odbioru repo |
| Odbiór / zapis na main | Nie wykonano; wyłącznie wątek 9 |

## Inwentarz i rozliczenie zakresu

Przed pierwszą edycją ukończono inwentarz całego wymaganego repo, przypięty do `161cc22dfb84fe863389d6b90323bd44516a68dc`: **559 plików, 39 496 mechanicznych kandydatów, 695 ręcznie sklasyfikowanych grup**. [Raporty wątków](data-in-code/README.md) i [zbiorczy JSON](data-in-code/inventory.json) zachowują bazowe `plik:linia`, opis, właściciela, cel i mechanizm uogólnienia. Historyczny supplement wątku 8 pozostaje osobny. Nie przeliczono tych kotwic z powodu późniejszych gałęzi i dodatkowych packów.

| Miara | Zamrożony przyrost 2026-10-04 | Przyrost tego raportu |
|---|---:|---:|
| Wszystkie sklasyfikowane grupy | 695 | 695, bez nowego przeliczania |
| Grupy w zakresie 11 | 213 | 213, bez zmiany historycznej klasyfikacji |
| Migrated w opisanej ścieżce konsumenta | 187 | 187 |
| Partially migrated / otwarte okablowanie | 19 | 19 |
| Pozostałe pozycje zakresu 11 | 5 wire, 1 algorytm, 1 przekazana W1 | bez zmiany |
| Kanoniczne domeny RuntimeProfile | 19 | 22; trzy dodatkowe domeny importu są **UNWIRED** |
| Polecenia obsługi profili otwierające Runtime | 4/4 | 0/4 |
| Aktywne połączenia nowych domen importu z silnikiem W5 | 0 | 0 |

[Mapping 213 pozycji](data-in-code/migration-status.json) pozostaje źródłem dokładnych zadań i statusów. Dostępny deskryptor, zapis nakładki lub pozytywny test danych nie zamykają migracji cudzego konsumenta. Liczby 187/19 nie zwiększają się za przygotowanie trzech niepodłączonych packów.

Poprzedni [raport 2026-10-04](data-profiles-2026-10-04.md) zachowuje pełny opis migracji własnych subsystemów: semantic/graph/memory/selector, archive, materialize/knowledge, worker/media/GitHub, HTTP/model, regex/util i CLI. Domyślne dane, historyczne dekodery oraz odrębne origin/evidence nie zostały zastąpione nowymi priors. Creation normalizer nadal wymaga jawnego pochodzenia producenta, raportuje zastosowane priors osobno i oczekuje okablowania zewnętrznych producentów.

## Co zmieniono teraz

### CLI profili bez inicjalizacji aplikacji

`profile list / inspect / validate / save` rozwiązuje katalog danych bez otwierania Runtime, bazy, ledgeru, configu ani odzyskiwania zadań. `save` tworzy tylko katalog potrzebny do sprawdzonej nakładki i zapisuje ją atomowo. Pozostałe polecenia aplikacji zachowują Runtime. Kolejność discovery i aliasy pozostają zgodne; publiczny wspólny read-only discovery nadal wymaga właściciela W2/9. [Raport przyrostu CLI](data-in-code/implementation-cli-followup-2026-10-05.md) opisuje konkretne ścieżki i ograniczenia.

Zapis zachowuje jawny wybór równy presetowi, także scalar, tablicę i końcowy RFC6902 `replace`. Kolejny częściowy zapis zachowuje usunięcia słownika/tablicy i nie odtwarza starego usunięcia indeksu na nowej krótszej tablicy. Istniejący błędny plik nie jest naprawiany przez fallback. Intencja nie jest rekonstruowana z samej równości wartości: `is_builtin:true` może współistnieć z jawnym wyborem. Historycznie utraconej przez diff intencji nie da się odtworzyć. Atomowy plik nie zapewnia CAS równoległych edytorów ani trwałego identity exclusion W12.

`validate/save` sprawdza pełne efektywne usage przez natywny `validate_usage_policy_options`. To sprawdzian edytora, nie admission ani aktywacja pliku w W2. Aktywna polityka przyjętego W2 nadal pobiera `config.json/loom_usage_policy` ze swoją semantyką shallow replacement; profilowa nakładka nie staje się automatycznie tym ustawieniem.

### Jedno źródło usage i pełny deskryptor

`loom/data/runtime_sources.pack` opisuje ogólne projekcje `source` + RFC6901 `pointer/target` w szablon definicji. Czytelny runtime `usage_policy.pack` jest generowany wyłącznie z kanonicznego `loom/data/policy/usage_policy.pack` W2. Nie zmieniono kanonicznej polityki W2, opisu wrappera, rewizji ani schematu. **Wszystkie 19 dotychczasowych wpisów definicji embeddingu są byte-identyczne.** Hash usage definition+values pozostaje `e8b8d4e5b098213a421b88b1f11db949a8a6056838a2f8c9487303913da5a8bd`, a raw wrapper SHA-256 `85bea77d6f935c098ef3cb6de7ffc9fc9a07282e065bd9c10834f04b7d957335`.

Generator waliduje wszystkie wejścia i planuje wszystkie wyniki przed zapisem. Odrzuca malformed JSON, duplikaty, liczby niefinitywne, niepoprawny Unicode, ucieczki ścieżek/symlinków, hardlinkowe aliasy generowanych wejść, cykle, nieobecne/niepoprawne wskaźniki i nakładające się projekcje. `--check` sprawdza zarówno pochodny wrapper, jak i embedding. Atomowe zastąpienie jest pojedynczym zapisem pliku; nie deklaruje rollbacku po awarii filesystemu w środku końcowych zastąpień. Skończone szerokie tokeny całkowite normalizuje do double tak jak nlohmann JSON, bez dodatkowego sufitu kwot; ścisłe integer settings nadal waliduje ich schema.

`RuntimeProfile::inspection()` addytywnie daje `definition/defaults/values/value_schema/hash` oraz `source_provenance`: logiczną ścieżkę, wskaźnik, cel i SHA-256 oryginalnych bajtów. Pochodzenie źródeł jest poza dotychczasowym hashem definicji/wartości. BOM i końce linii zmieniają raw hash, choć nie muszą zmieniać przepisu. To źródła konstrukcji presetu, nie historia decyzji użytkownika. `from_definition` nie zna plików i pozostawia źródła puste. [Raport P0 i API](data-in-code/implementation-runtime-profile-sources-2026-10-05.md) zawiera pełny kontrakt i dowody.

### Trzy domeny danych importu dla W5

Dodano `import_formats`, `import`, `import_audit`: odpowiednio reguły formatów/rozszerzeń/MIME/ról/tekstu, defaults biblioteki i różnice CLI oraz założenia audytu natywnego/Python. Wszystkie definicje i schematy oznaczają **UNWIRED**. Import, CLI importu i audit nadal używają dotychczasowego kodu W5; podgląd/zmiana tych profili jeszcze nie zmienia działania tych silników.

[Raport danych importu](data-in-code/implementation-import-profile-data-2026-10-05.md) przypina defaults do W5 `4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2`, z handoff `5f0abd20dd83e5c0e7f43e84c333d5e23a5e681f`. Zachowano **14 pełnych publicznych źródeł, 279 748 B**, ich Git blob OID i SHA-256. Kanoniczne defaults trzech domen są identyczne z odtworzonymi źródłami. Rzeczywista zachowana funkcja Python `estimate()` na tym samym syntetycznym wejściu dała byte-identyczny JSON dla argumentów domyślnych i argumentów z profilu, SHA-256 `8c6d2e6abd706a1f4fd9eec6b8e8480c99ec2176c5f731a6de1ae5b3aac1b32d`. To sprawdzian funkcji Python, nie natywnego importu ani cache całego pipeline.

Defaults biblioteki i CLI pozostają rozdzielone. `null` ceny nie oznacza zera ani bieżącej ceny modelu. Obsługę capabilities nowych aliasów, walidację dodatniości/relacji parametrów/pary cen, source-byte/hash/CAS/checkpointów i anulowania musi zachować adapter W5. Profile nie zmieniają parserów ani historycznych formatów wyjściowych.

## R40/R41: co dowód potwierdza, a czego nie

[Kontrakt warstw i metod](data-in-code/layers-methods-contract-2026-10-05.md) korzysta z istniejącego W12 `DefaultLayers/runtime_profile_values`, przypiętego do `3c0bc36552ef9851f1174946cfb108549aae3228`. Nie powstała druga baza ani drugi resolver. Adapter rozwiązuje pointer→layer key, usuwa disabled/excluded/proposal/missing i sprawdza dokładne `with_values`. Usunięcie wymaganego pola daje jawny błąd; nie przywraca presetu. Trwałe tożsamości i wykluczenia po update/disappearance/reappearance należą do resolvera W12.

Rzeczywisty paired harness osobno kompiluje te same źródła W12 z zgodnymi modułami/nagłówkami W11 przed i po. Wynik zakresowy jest pozytywny: **19→22 domeny, 162→179 grup najwyższego poziomu, 1828→2148 asercji, 23 sprawdzone rekordy source provenance**, exit 0 po obu stronach; wszystkie 19 wspólnych definicji/defaults/hashy pozostały identyczne. [Definitive receipt](data-in-code/evidence/default-layers-runtime-mixed/actual-2026-10-05/receipt.json) zachowuje źródła, komendy, fixture i surowe wyniki.

Bindingi są syntetyczne i atomowe na poziomie grup, wyłącznie do tego dowodu. To nie jest produkcyjny rejestr stabilnych ID elementów tablic, pełny OnboardingStore/CAS/persistence ani potwierdzenie użycia warstw przez każdy silnik. Rejestrację definicji onboardingu z `profiles/user.pack:/runtime_definition` manifest opisuje na przyszłość; brakującego packa nie aktywuje. Przeniesienie efektywnych values przez `from_definition` nie przenosi źródeł plikowych: transport powinien zachować je z oryginalnego builtin lub zastosować jego `with_values`.

R41 pozostaje wspólnym kontraktem W3/W4 przyjętym na `e4109df`: metoda/wersja/run/result i dokładne consumed parameters. `selector.pack` W11 nie jest profilem całego method registry. Hash przepisu, seed deskryptora ani podgląd nie tworzą wykonania i nie zastępują rzeczywistych krawędzi result→run/version. Model judgement, datowana ocena konkretnej wersji/populacji, instrument measurement oraz creation prior pozostają osobnymi rzeczami; confidence prior nie jest zmierzoną skutecznością. UI musi rozróżniać źródła presetu, intencję warstwy użytkownika i pochodzenie rzeczywistego runu. `user_stated/model_inferred/form` nie zmieniają po cichu kanonicznych Origin/EvidenceClass; preset zgody nie jest zdarzeniem jej udzielenia.

## Weryfikacja bieżącego przyrostu i negatywy

| Sprawdzian zakresowy | Rzeczywisty wynik | Ograniczenie |
|---|---|---|
| Generator źródeł / isolated shadow inputs | 16/16 PASS | Bez pełnego rdzenia |
| Istniejące testy kontraktów Python | 209/209 PASS | Nie pełny CTest |
| Native RuntimeProfile i dane 22 domen | 11/11 przypadków, 312/312 asercji, 0 skipped | Nowe RuntimeProfile/fs/log/test/runner, pozostałe niezmienione utility dependencies; nie whole-core |
| Dane importu i zachowana funkcja Python | 11/11 PASS, 0 skipped; defaults/output zgodne | Brak natywnego adaptera W5 |
| Rzeczywisty CLI smoke oraz CLI-only parity | exit 0; 19/19 values/schema/hash zgodne, help/version byte-identical | Nowy CLI z kompletnym frozen before core/headers, przed nowym rdzeniem P0 |
| R40 paired native harness | before 1828 / after 2148 asercji, exit 0 | Syntetyczne bindingi, bez DB/CAS i produkcyjnych konsumentów |
| Generator `--check` właściwego drzewa | PASS dla 22 domen; kanoniczny generator usage także PASS | Nie aktywuje cudzych konsumentów |
| CLI z pełnym aktualnym rdzeniem | PASS: 19 wspólnych profili zgodnych, 3 nowe; pomoc/wersja identyczne; zero nowych plików | Porównanie values/schema/hash; addytywny deskryptor jest opisany osobno |

[CLI receipt](data-in-code/evidence/continuation-2026-10-05/cli-scoped-proof/receipt.json) zapisuje kompletne źródła i hashe. Stary CLI podczas sondy utworzył sentinel i dwie bazy; nowy nie utworzył żadnego pliku. Domyślna pomoc pozostała 4770 B i te same 17 flag. [Nowe porównanie whole-core](data-in-code/evidence/continuation-2026-10-05/cli-fullcore-after/comparison.json) oraz [pełny receipt i strumienie](data-in-code/evidence/continuation-2026-10-05/cli-fullcore-after/receipt.json) używają nowej całej biblioteki, bez mieszania układu ABI. Nie sumujemy nakładających się sond jako nowych przypadków CTest i nie przedstawiamy kontroli składni jako wykonanych testów produktu.

Negatywne wyniki zachowano bez luzowania testów:

- **ABI:** pierwszy izolowany probe łączył nowy RuntimeProfile z konsumentami fs/log, które rezerwowały jego poprzedni układ; SIGABRT/stack-smashing. To błędna izolacja sondy. Zachowano dokładny source/embed/header, komendy i raw log; po przebudowie rzeczywistych konsumentów nie miesza się tego ABI.
- **Niezgodny embedding:** następna sonda miała frozen 19 domen, a w trakcie pracy niezależnie pojawiły się 22 pliki danych: 10/11 przypadków i 220/221 asercji, jedyny błąd `import_formats NotFound`. Zgodny snapshot 22 przeszedł 11/11 i 312/312. Obie wersje pozostają w [dowodzie P0](data-in-code/evidence/runtime-profile-sources-2026-10-05/receipt.json) z odtworzeniem.
- **CLI `--version`:** harness użył nieobsługiwanej flagi zamiast istniejącego polecenia `version`; CLI exit 2, capture exit 1. Zachowano [replay i stdout/stderr](data-in-code/evidence/continuation-2026-10-05/cli-negative-unsupported-version/replay.json); poprawiono sondę, nie dodano produktu na podstawie błędnego założenia.
- **CLI compare path:** pierwszy nowy capture dostał ścieżkę katalogu zamiast pliku `receipt.json`; zakończył się `IsADirectoryError` po prawidłowym capture 22 profili. Zachowano pełny [negatyw, snapshot i replay](data-in-code/evidence/continuation-2026-10-05/cli-negative-compare-path/replay.json); poprawiono wyłącznie argument harnessu.
- **R42:** błędy syntetycznego run2, odzyskane dane historyczne i cały realny audit exit 1 pozostają w [osobnym raporcie](data-in-code/implementation-product-literal-guard-2026-10-05.md). Nie są zastępowane zielonymi fixtures.
- Historyczne negatywy build/permissions, null/schema/ABI i izolacji Runtime pozostają w poprzednich raportach oraz dowodach. Nie przepisano ich jako sukcesu tego przyrostu.

## Historyczna weryfikacja — wyłącznie kontekst

Zamrożony raport 2026-10-04 rozlicza własną ówczesną bazę `161cc22`, rebase na `7282437` i publikację `49e5e65`: **CTest 122/122**, 776 native / 26 758 asercji i 1276 Python oraz web. Te wyniki są historyczne i **nie są aktualnym PASS** przyrostu CLI/P0/import ani połączonego `e4109df`. Oryginalny skrócony JUnit, jego odrzucony guard i pochodne odtworzenie pełnych strumieni pozostają w [historycznym receipt](data-in-code/evidence/final-validation/receipt.json).

Historyczne sześć sond całych bibliotek zachowało 1 208 469 B zgodnych wyników; archiwum dodatkowo 8/8 eksportów 47 383 B, synteza 142 841 B i pomoc 4770 B. To wcześniejsze dowody dokładnej zgodności opisanych źródeł, bez przenoszenia statusu na nowy full-core. Tak samo wcześniejsze benchmarki mają ograniczony zakres: helper cold 356×, steady 6,624×, syntetyczny cały archive_run 2,833× i init+run+shutdown 5,520×. Nie ekstrapolujemy ich na prywatny import ani modele; nie są pomiarem dzisiejszego przyrostu.

## Nowe wymaganie R42

Ostatni fetch odczytał R42 w `3cd5848e7484d50d00f19f0855bc2cd228ad7f3b:docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md:483–515`: automatyczny strażnik literałów produktu, z sześcioma legalnymi kategoriami oraz jawnym uzasadnieniem każdego wyjątku. Ta sekcja nie jest jeszcze na odczytanym `main e4109df`. Przygotowanie mechanizmu i odtwarzalnego audytu jest osobnym przyrostem 11; dotychczasowy inwentarz nie jest takim guardem. Dane pozostające w cudzym zakresie wymagają migracji u właścicieli, bez zbiorczego wyjątku „legacy”. [Mechanizm, schemat i pełne dowody](data-in-code/implementation-product-literal-guard-2026-10-05.md) są gotowe. Dane obejmują rejestr backendów, zakresów i właścicieli oraz sześć dokładnie przejrzanych kotwic — po jednej na dozwoloną kategorię. Każda wiąże cały plik, zakres bajtów, literal i konkretne uzasadnienie. Nie ma zbiorczego wyjątku dla dawnego kodu ani luźniejszego fallbacku. To konserwatywny skan leksykalny, bez twierdzenia o AST/semantycznej kompletności; nieobsługiwane pliki pozostają jawnie BLOCKED.

**41 testów syntetycznych PASS**, niezależny przegląd mechanizmu PASS. Pierwszy rzeczywisty audit repo jest **NEGATYWNY: exit 1 / valid:false, 56 968 niezaklasyfikowanych literałów i 85 zablokowanych plików**. Wszystkie 56 974 rozpoznane kandydaty, 363 pliki w mianowniku i manifest zachowano w pełnym gzip/JSON; 390 hashów wejściowych przed/po jest identycznych. Zakres przypisany W11 ma 20 093 niezaklasyfikowane literały i 5 BLOCKED. To osobny licznik leksykalny: nie zmienia 695/213 grup ani nie dowodzi, że wszystkie te literały są zakazanymi danymi. Legalne kontrakty/standardy również wymagają indywidualnego przeglądu. **Odbiór R42 całego repo pozostaje zablokowany**; zielony CTest sprawdza mechanizm, nie znosi długu audytu.

## Czego nie wykonano i dlaczego

Nie podłączono trzech nowych domen do importu/audytu W5, wspólnego streamingu archiwum ani cudzych konstruktorów i C API. Nie aktywowano RuntimeProfile usage zamiast ustawienia W2. Nie zbudowano osobnego resolvera, DB, registry metod ani produkcyjnych bindings dla R40/R41. Nie zamknięto 19 pozycji częściowych dzięki samemu API; szczegółowe zadania pozostają w mappingu i raportach właścicieli. Granice wire/algorytmu nadal mają uzasadnienia per-ID.

Rebase, pełne buildy native/web, CTest i dwa zgodne sprawdziany wykonania zakończono pozytywnie. Nie przeprowadzono odbioru przez W9. Nie dokonano jeszcze indywidualnej klasyfikacji 56 968 literałów ani rozszerzenia rozpoznawania 85 BLOCKED: mechanizm zachowuje cały dług do pracy właścicieli, bez pozornej akceptacji. **Gałąź nie jest tym raportem przedstawiana jako przyjęta na main.**

## Do wątku N

- **Do wątku 1:** świeży raport W10 (`3807e31`) wskazuje mixed CTest 120/121 i nową tożsamość próby semantic retry jako zadanie 1/2. Bieżące 127/127 W11 na `e4109df` nie jest bramką tej mieszanej gałęzi. Zachowaj unresolved accounting, cache identity i anty-double-dispatch. Podłącz effective analyzer, hash producenta, checked model overloads i creation normalizer u swoich producentów; prompty/receptury jako wersje metod oraz preview pozostają twoim zakresem. Zachowaj origin/evidence i oryginalne obserwacje.
- **Do wątku 2:** kanoniczny usage pack pozostaje jedynym źródłem presetu. Zachowaj native validator i admission ×10; udostępnij wspólny read-only root discovery i wyjaśnialną aktywację config/path/usage, odróżniając seeded/persisted/explicit od równości wartości. Resolver warstw uzgadniaj z W12, bez kopii DB.
- **Do wątku 3/4:** W3-2 nowe deskryptory w `loom/data/runtime/*.pack` obejmie istniejący generator po przyjęciu; regeneracja nie aktywuje runtime settings. ACK dla W4-2: prywatny stan tekstowego `Normalizer` w `kb.h` mieści się w jego przepisie KB; `Normalizer::create` nie zamyka normalizatora tworzenia rekordów W11. Korzystaj z aktualnego registry/packet method-version/run/result oraz exact consumed parameters; binding profili nie może tworzyć fikcyjnego wykonania. Podłącz własnych konsumentów i creation API, zachowaj historical wire, datę/wersję/populację/source ocen i rozdzielenie measurement/judgement/prior.
- **Do wątku 5:** przyrost W5-2 (`b93b0c9`) wprowadza kanoniczne `data/presets/import.pack` i `import_audit.pack`; po przyjęciu powiązać je projekcjami wspólnego manifestu, bez kopiowania wartości. Obecne trzy profile są przypiętym historycznym deskryptorem **UNWIRED**, nie konkurencyjnym aktywnym presetem. Odbierz trzy domeny **UNWIRED** z przypiętych źródeł, podłącz je przez istniejący RuntimeProfile i natywne walidatory; wykonaj native default parity, source hash/CAS/checkpoint/cancel oraz provenance/cache identity. Zachowaj library→CLI→explicit precedence, OCR dokładnych snapshot bytes i oryginalnego formatu (`.jpg` nie staje się `.jpeg`). Streaming projects/memories oraz wspólny skaner archiwum pozostają osobnymi zadaniami z regresjami BOM/UTF-8/surrogatów/przerwań/order/IDs; nie przejmuj ukrycie presetu depth 512 tam, gdzie archiwum miało brak ograniczenia.
- **Do wątku 7:** model/token/cennik z datą, kalibracja i realne rozliczenie pozostają twoim zakresem; nowe wywołania płatne W11: 0. Nie nazywaj creation prior ani model judgement zmierzoną jakością.
- **Do wątku 8:** zachowaj pełne raw negatywy, niezmieniony guard rzeczywiście wykonanych przypadków i politykę opt-in; strażnik R42 ma poprawne testy mechanizmu, ale ścisły audit produktu celowo pozostaje czerwony. CI wymaga rozliczenia właścicieli, bez blanket allowlisty.
- **Do wątku 10:** użyj definition/defaults/effective schema/source provenance i natywnej walidacji; pokaż `UNWIRED`, błędy i reload. Jawny wybór równy presetowi, disable/exclusion/proposal oraz źródła presetu i prawdziwego runu mają osobne znaczenia. Suppressed wymagane pole nie może być cicho przywrócone.
- **Do wątku 12:** zgłoszenie duplikacji usage na `2eb65d4` jest historyczne: canonical-source `130807e71` został opublikowany po rebase jako `82d210874`, z tymi samymi bajtami manifestu/generatora/API. Pochodzenie i stare 19 definitions/defaults/hashes potwierdzają paired proof oraz nowy CLI. Aktywacja W2 nadal osobna. Użyj istniejącego DefaultLayers/runtime_profile_values i OnboardingStore; dane bindings/stabilnych ID, zwłaszcza elementów tablic, oraz persistence/CAS i realne konsumery wymagają oddzielnego połączenia. Source metadata nie są journalem użytkownika. Paired 19→22 jest sprawdzianem kontraktu, nie końcem wdrożenia wszystkich konsumentów.
- **Do wątku 9:** odebrać wyłącznie wybrany przyrost po świeżym fetch i swoich wymaganych bramkach; zachować liniową historię, pełne negatywy, 213/187/19 i **UNWIRED**. Nie przedstawiać pozytywnego testu strażnika jako odbioru R42. Przydzielić owner-debt nagłówków/Android/server i dalszą pracę po audycie; INDEX/STATE/root README aktualizować wyłącznie według faktycznie przyjętego wyniku.
