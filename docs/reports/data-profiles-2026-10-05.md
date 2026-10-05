# Wątek 11 — dane w profilach, przyrost 2026-10-05 — DRAFT

Kontynuacja gałęzi `gpt/data-profiles-2026-10-04`. Ten raport opisuje nowy przyrost CLI, wspólne źródło presetu usage, odczytowy deskryptor oraz dane importu i sprawdzony kontrakt W12. **Końcowy odbiór na aktualnym main pozostaje PENDING.** Nie edytowano `README.md`, `docs/STATE.md` ani implementacji cudzych silników. Wszystkie sprawdziany były offline, na publicznych źródłach, syntetycznych danych i atrapach; koszt płatnych wywołań: **0**. Nie czytano prywatnych archiwów ani `eval/real-holdout-key`.

Aktualny odczytany main to `e4109df`, po przyjęciu W3/W4. Pierwszy pełny build bieżącego przyrostu rozpoczęto jeszcze na bazie `30ad7d3`; celowo zatrzymano go po pełnej bibliotece rdzenia (krok 139) i części obiektów testowych (148/258), exit 130, aby wznowić kompilację na przyjętej bazie. [Checkpoint](data-in-code/evidence/continuation-2026-10-05/build-checkpoint-before-rebase/receipt.json) zachowuje pełny log i identyfikację źródeł. To nie jest wynik buildu ani bramki po rebase na `e4109df`. Opublikowano CLI `9a89e37`, canonical-source `130807e71d659f20c8ad5e946a88ec448220612b` oraz dane importu `26c1a4062137f74f84f3c8e82c47fdec7ca1c6c9`. Końcowe hashe i receipts musi dopisać koordynator po rzeczywistych bramkach.

| Bramka końcowa | Stan tego draftu | Do uzupełnienia przez koordynatora |
|---|---|---|
| Świeży main i końcowy rebase | PENDING; odczytany main `e4109df` | `FINAL_BASE_MAIN_SHA` |
| Końcowy hash gałęzi i publikacji | PENDING | `FINAL_BRANCH_SHA` / `FINAL_PUBLISH_SHA` |
| Pełny build właściwego drzewa | PENDING | `FINAL_BUILD_RECEIPT` |
| Pełny CTest i dowód rzeczywiście wykonanych przypadków | PENDING | `FINAL_CTEST_RECEIPT` |
| Build web: tsc + Vite | PENDING | `FINAL_WEB_RECEIPT` |
| Odbiór przez wątek 9 / hash main po odbiorze | PENDING | `FINAL_MAIN_SHA` |

<!-- FINAL_BASE_MAIN_SHA: PENDING -->
<!-- FINAL_BRANCH_SHA: PENDING -->
<!-- FINAL_PUBLISH_SHA: PENDING -->
<!-- FINAL_BUILD_RECEIPT: PENDING -->
<!-- FINAL_CTEST_RECEIPT: PENDING -->
<!-- FINAL_WEB_RECEIPT: PENDING -->
<!-- FINAL_MAIN_SHA: PENDING -->

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
| Generator `--check` po regeneracji | PASS dla 19, następnie 22 domen | Ostateczna bramka właściwego drzewa nadal PENDING |

[CLI receipt](data-in-code/evidence/continuation-2026-10-05/cli-scoped-proof/receipt.json) zapisuje kompletne źródła i hashe. Stary CLI podczas sondy utworzył sentinel i dwie bazy; nowy nie utworzył żadnego pliku. Domyślna pomoc pozostała 4770 B i te same 17 flag. Nie sumujemy nakładających się sond jako nowych przypadków CTest i nie przedstawiamy kontroli składni jako wykonanych testów produktu.

Negatywne wyniki zachowano bez luzowania testów:

- **ABI:** pierwszy izolowany probe łączył nowy RuntimeProfile z konsumentami fs/log, które rezerwowały jego poprzedni układ; SIGABRT/stack-smashing. To błędna izolacja sondy. Zachowano dokładny source/embed/header, komendy i raw log; po przebudowie rzeczywistych konsumentów nie miesza się tego ABI.
- **Niezgodny embedding:** następna sonda miała frozen 19 domen, a w trakcie pracy niezależnie pojawiły się 22 pliki danych: 10/11 przypadków i 220/221 asercji, jedyny błąd `import_formats NotFound`. Zgodny snapshot 22 przeszedł 11/11 i 312/312. Obie wersje pozostają w [dowodzie P0](data-in-code/evidence/runtime-profile-sources-2026-10-05/receipt.json) z odtworzeniem.
- **CLI `--version`:** harness użył nieobsługiwanej flagi zamiast istniejącego polecenia `version`; CLI exit 2, capture exit 1. Zachowano [replay i stdout/stderr](data-in-code/evidence/continuation-2026-10-05/cli-negative-unsupported-version/replay.json); poprawiono sondę, nie dodano produktu na podstawie błędnego założenia.
- Historyczne negatywy build/permissions, null/schema/ABI i izolacji Runtime pozostają w poprzednich raportach oraz dowodach. Nie przepisano ich jako sukcesu tego przyrostu.

## Historyczna weryfikacja — wyłącznie kontekst

Zamrożony raport 2026-10-04 rozlicza własną ówczesną bazę `161cc22`, rebase na `7282437` i publikację `49e5e65`: **CTest 122/122**, 776 native / 26 758 asercji i 1276 Python oraz web. Te wyniki są historyczne i **nie są aktualnym PASS** przyrostu CLI/P0/import ani połączonego `e4109df`. Oryginalny skrócony JUnit, jego odrzucony guard i pochodne odtworzenie pełnych strumieni pozostają w [historycznym receipt](data-in-code/evidence/final-validation/receipt.json).

Historyczne sześć sond całych bibliotek zachowało 1 208 469 B zgodnych wyników; archiwum dodatkowo 8/8 eksportów 47 383 B, synteza 142 841 B i pomoc 4770 B. To wcześniejsze dowody dokładnej zgodności opisanych źródeł, bez przenoszenia statusu na nowy full-core. Tak samo wcześniejsze benchmarki mają ograniczony zakres: helper cold 356×, steady 6,624×, syntetyczny cały archive_run 2,833× i init+run+shutdown 5,520×. Nie ekstrapolujemy ich na prywatny import ani modele; nie są pomiarem dzisiejszego przyrostu.

## Nowe wymaganie R42

Ostatni fetch odczytał R42 w `3cd5848e7484d50d00f19f0855bc2cd228ad7f3b:docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md:483–515`: automatyczny strażnik literałów produktu, z sześcioma legalnymi kategoriami oraz jawnym uzasadnieniem każdego wyjątku. Ta sekcja nie jest jeszcze na odczytanym `main e4109df`. Przygotowanie mechanizmu i odtwarzalnego audytu jest osobnym przyrostem 11; dotychczasowy inwentarz nie jest takim guardem. Dane pozostające w cudzym zakresie wymagają migracji u właścicieli, bez zbiorczego wyjątku „legacy”. Wynik audytu i testów zostanie dopisany po wykonaniu.

## Czego nie wykonano i dlaczego

Nie podłączono trzech nowych domen do importu/audytu W5, wspólnego streamingu archiwum ani cudzych konstruktorów i C API. Nie aktywowano RuntimeProfile usage zamiast ustawienia W2. Nie zbudowano osobnego resolvera, DB, registry metod ani produkcyjnych bindings dla R40/R41. Nie zamknięto 19 pozycji częściowych dzięki samemu API; szczegółowe zadania pozostają w mappingu i raportach właścicieli. Granice wire/algorytmu nadal mają uzasadnienia per-ID.

Nie zakończono jeszcze finalnego rebase na aktualny main, pełnego buildu/CTest/web i odbioru przez W9. Nowe scoped pozytywne wyniki pozwalają ocenić konkretny przyrost, ale nie zastępują tych bramek. **Gałąź nie jest tym raportem przedstawiana jako przyjęta na main.**

## Do wątku N

- **Do wątku 1:** podłącz effective analyzer, hash producenta, checked model overloads i creation normalizer u swoich producentów; prompty/receptury jako wersje metod oraz preview pozostają twoim zakresem. Zachowaj origin/evidence i oryginalne obserwacje.
- **Do wątku 2:** kanoniczny usage pack pozostaje jedynym źródłem presetu. Zachowaj native validator i admission ×10; udostępnij wspólny read-only root discovery i wyjaśnialną aktywację config/path/usage, odróżniając seeded/persisted/explicit od równości wartości. Resolver warstw uzgadniaj z W12, bez kopii DB.
- **Do wątku 3/4:** korzystaj z aktualnego registry/packet method-version/run/result oraz exact consumed parameters; binding profili nie może tworzyć fikcyjnego wykonania. Podłącz własnych konsumentów i creation API, zachowaj historical wire, datę/wersję/populację/source ocen i rozdzielenie measurement/judgement/prior.
- **Do wątku 5:** odbierz trzy domeny **UNWIRED** z przypiętych źródeł, podłącz je przez istniejący RuntimeProfile i natywne walidatory; wykonaj native default parity, source hash/CAS/checkpoint/cancel oraz provenance/cache identity. Zachowaj library→CLI→explicit precedence, OCR dokładnych snapshot bytes i oryginalnego formatu (`.jpg` nie staje się `.jpeg`). Streaming projects/memories oraz wspólny skaner archiwum pozostają osobnymi zadaniami z regresjami BOM/UTF-8/surrogatów/przerwań/order/IDs; nie przejmuj ukrycie presetu depth 512 tam, gdzie archiwum miało brak ograniczenia.
- **Do wątku 7:** model/token/cennik z datą, kalibracja i realne rozliczenie pozostają twoim zakresem; nowe wywołania płatne W11: 0. Nie nazywaj creation prior ani model judgement zmierzoną jakością.
- **Do wątku 8:** zachowaj historyczne raw negatywy i niezmieniony guard rzeczywiście wykonanych przypadków; nowy końcowy CTest wymaga nowego source-bound receipt, nie odziedziczonego 122/122.
- **Do wątku 10:** użyj definition/defaults/effective schema/source provenance i natywnej walidacji; pokaż `UNWIRED`, błędy i reload. Jawny wybór równy presetowi, disable/exclusion/proposal oraz źródła presetu i prawdziwego runu mają osobne znaczenia. Suppressed wymagane pole nie może być cicho przywrócone.
- **Do wątku 12:** użyj istniejącego DefaultLayers/runtime_profile_values i OnboardingStore; dane bindings/stabilnych ID, zwłaszcza elementów tablic, oraz persistence/CAS i realne konsumery wymagają oddzielnego połączenia. Source metadata nie są journalem użytkownika. Paired 19→22 jest sprawdzianem kontraktu, nie końcem wdrożenia wszystkich konsumentów.
- **Do wątku 9:** po aktualnym fetch wykonać końcowy rebase, pełny build/CTest/web i default parity właściwego całego drzewa; dopisać wszystkie `FINAL_*` i source-bound receipts powyżej. Zachować negatywy, 213/187/19 i status **UNWIRED**; aktualizować INDEX/STATE/README dopiero według faktycznie przyjętego wyniku.
