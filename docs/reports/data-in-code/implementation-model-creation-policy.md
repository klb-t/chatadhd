# Wątek 11 — presety tworzenia rekordów modeli

Dodano nowe, sprawdzane API tworzenia rekordów oraz 16 domyślnych wartości w `model.pack`. Testy wykonały **6/6 przypadków, 99/99 asercji**, bez błędów i pominięć. W siedmiu rodzajach rekordów z builtin presetem wynik po odczycie jest identyczny z wynikiem zastanego dekodera. DIC-0055–0059 pozostają **częściowe**, ponieważ istniejące zewnętrzne konstruktory i ścieżki zapisu wymagają podłączenia tego API.

## API i dane

Nowy nagłówek `loom/model_profile.h` udostępnia:

- `normalize_creation(kind, record)` — jawne tworzenie z builtin presetem;
- `normalize_creation(kind, record, profile)` — tworzenie z konkretnym aktywnym profilem, ponownie sprawdzanym przez builtin schemat;
- `creation_capabilities()` — rejestr siedmiu zaufanych dekoderów i strukturalnych relacji zagnieżdżenia;
- `CreationRecord {record, applied_defaults, profile_hash}` — rekord oraz osobne metadane wskazujące dokładnie dodane wartości przez JSON Pointer i hash efektywnego profilu.

| Inwentarz | `model.pack#/defaults/creation_defaults` | Builtin preset |
|---|---|---|
| DIC-0055 | `principle` | confidence0.5, validation_status=candidate |
| DIC-0056 | `operator` | confidence0.5, validation_status=candidate |
| DIC-0057 | `morphism` | confidence0.5, validation_status=candidate, mode=presence |
| DIC-0058 | `slot`, `domain_kind`, `domain_relation` | weight1/required=false; card=one dla slotu, many dla rodzaju/relacji |
| DIC-0059 | `project_kind` | subject_kind=project, validation_status=candidate |

Rejestr w `model_pack.cpp:33` zawiera możliwości istniejących dekoderów; wartości polityki pochodzą wyłącznie z danych. `normalize_creation_record` (`:56`) stosuje presety do brakujących pól. Jawne wartości wywołania wygrywają. Jawne null pola polityki jest błędem, aby nie przejść przypadkiem do historycznej gałęzi missing-field. Project-kind normalizuje zagnieżdżone domain-kinds, a te sloty i relacje tym samym profilem. Zastane dekodery sprawdzają wynik przed zwróceniem rekordu.

Profil nie zawiera defaultów origin, tożsamości, typowania, evidence ani provenance. Principle/operator/morphism/project-kind wymagają jawnego origin producenta. Zapisane dane źródłowe i pozostałe jawne pola producenta są kopiowane bez zmiany; nie są zastępowane wynikiem serializacji dekodera. Hash i receipt są osobne od rekordu. Confidence dodane przez preset jest **priorem tworzenia**, a nie pomiarem kalibracji, dowodem ani wynikiem sprawdzenia. `applied_defaults["/confidence"]` występuje wyłącznie przy takim dodaniu; jawne confidence producenta pozostaje bez zmiany.

Domyślne pola DTO i funkcje `from_json` nie zostały zmienione. `model_general.cpp` jest bajtowo identyczny z dostępnym checkoutem baseline; SHA256 `03df74670f2afc1bddccbf004ab2c1f003fc61a4b3904e6d6f17742f3e8cf5d6`. Odczyt historycznego rekordu bez origin nadal używa dawnego fall-backu, nawet po utworzeniu innej instancji profilu. Dodatnie wagi i confidence w jednostkowym przedziale są zastanym kontraktem dekodera. Schemat dopuszcza każdą reprezentowalną dodatnią wagę double, bez dodatkowego progu produktu.

## Dowody

`test_model_creation_profile.cpp` sprawdza: siedem porównań z historycznymi dekoderami, nested presety, jawne nadpisanie pojedynczego wywołania, zachowanie źródeł/checks/producer-provenance/origin/confidence, brak ewaluacji tekstu producenta, wymagane origin/tożsamość, jawne null/NaN i błędne wagi/enum, odrzucenie obcego oraz sztucznie łagodnego schematu, niezależność historycznego odczytu. Minimalna dodatnia waga double jest akceptowana.

Rzeczywisty izolowany build używa bieżących `model_pack.cpp` i `runtime_profile.cpp` ze świeżym embeddingiem oraz niezmienionych zależności pierwszego pełnego buildu. Trzy jednostki przeszły oryginalne flagi z `-Werror`, link i wykonanie zakończyły się kodem0. Nie tworzy Runtime ani transportu. Dowód nie zastępuje pełnego `ctest` ani połączenia z istniejącymi producentami.

Pełne logi, polecenia kompilacji/linkowania, SHA256 źródeł i delta pięciu ID znajdują się w `evidence/model-creation-policy/`. Na końcowym runnerze: `loom_tests --test-suite=model.creation_profile`. Niezależny przegląd read-only nie znalazł błędu zgodności lub pochodzenia. Nie wykonywano sieci ani wywołań płatnych.

Po dodaniu creation-defaults powtórzono także wcześniejszy probe publicznych legacy API net/model z nowym obiektem model_pack i świeżym profilem. Nadal **264 606 bajtów** identycznych ze starym wynikiem, SHA256 `6eb6f00d3c398870a15f350a918d7558eb20d68e31b64af82ca745d250b0b7a4`; pełny nowy wynik i receipt mają prefiks `legacy-`. Baseline i wcześniejszy szczegółowy opis są w dowodach knowledge-net-model. To dodatkowo sprawdza, że dołożenie presetów nie zmieniło dawnych anchorów, rang, historii i domyślnych requestów.

## Czego nie zrobiono i dlaczego

Nie przepisano historycznych czytników ani istniejących konstruktorów w cudzych zakresach: profil tworzenia musi zostać rozwiązany przed zapisaniem nowego rekordu. Nie należy zastosować zmiennego presetu przy odczycie starych danych. Stąd status pięciu pozycji jest częściowy, mimo gotowego, sprawdzonego API i danych. Nie zmieniono `model.h`, aby zachować dotychczasowy kontrakt i ograniczyć przebudowę.

## Do wątku N

- **Do wątku 1:** nowe principle/operator/morphism rozwiązać przez `normalize_creation` przed zapisaniem; podać prawdziwe origin i evidence; zachować companion receipt priors/hash. Do odczytu archiwalnego nadal `from_json`.
- **Do wątku 4:** wejście nowych project-kinds/domain-kinds/slots/relations może użyć tego samego API; obecne dekodery i schema recordu zostają kontraktem wire. Hash profilu i applied-defaults utrwalać jako pochodzenie tworzenia, nie measured confidence.
- **Do wątku 9:** podłączyć aktywny `RuntimeProfile("model")` w pozostałych producentach/API; pięć ID pozostaje częściowych do tej integracji. Przeprowadzić pełny ctest z nowym zestawem6/99 i świeżym embeddingiem. Nie zmieniać historycznego odczytu pod wpływem aktywnego profilu.
- **Do wątku 10:** ekran ekspercki używa `creation_capabilities()` i schematu `/creation_defaults`; origin/evidence/identity/type wymagają danych producenta i nie są template presets. Podgląd powinien pokazać `applied_defaults` oraz hash przy tworzeniu i wyraźnie nazwać confidence z presetu priorem, nie pomiarem.
