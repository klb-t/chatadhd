# W11 — dane importu dla W5, 2026-10-05

Dodano trzy definicje `RuntimeProfile`: `import_formats`, `import`, `import_audit`. Są dostępne do inspekcji i nakładek po regeneracji wspólnego rejestru przez właściciela frameworka. Każda definicja i jej `value_schema` oznaczają zakres **UNWIRED**. Import, CLI importu, audyt natywny i narzędzie audytu Python nadal wykonują dotychczasowy kod; te profile nie zmieniają ich działania. To przygotowanie kontraktu danych, bez zamknięcia pozycji inwentarza należących do W5.

Źródłem jest dokładny kod W5 `4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2`; ostatni handoff W5 to `5f0abd20dd83e5c0e7f43e84c333d5e23a5e681f`. Zachowano 14 pełnych publicznych blobów źródłowych, 279 748 bajtów, z Git blob OID i SHA-256 w [source-manifest.json](evidence/import-profile-data/source-manifest.json). Słowniki pochodzą z rzeczywistych stałych tego kodu. Przyszłych aliasów, cen modeli ani domyślnego budżetu nie wymyślano.

| Przepis | Co obejmuje | Domyślne wartości / istotne rozróżnienie |
| --- | --- | --- |
| `import_formats` | Kolejność 9 reguł rozszerzeń, 16 rozszerzeń, sygnatury sniffingu, MIME, nazwy trybu eksportu, role oraz reguły tekstu/Markdown/OCR | JSON odrzuca `system`, SQLite mapuje go na `assistant`. Eksport OpenAI zachowuje brak roli jako `unknown`, Anthropic jako `assistant`. |
| `import` | 13 serializowalnych pól `ImportOptions` i osobna warstwa 2 różnic CLI | Biblioteka: `resume=false`, pełne metadane wyniku `true`; CLI: `resume=true`, pełne metadane `false`. Czytnik 65 536 B, głębokość 512 jako preset; głębokość 0 oraz inference 0 oznaczają bez ograniczenia. |
| `import_audit` | Wspólne założenia estymacji, natywny zakres, presety funkcji Python/CLI, cache SQLite oraz klasy statusów/role narzędzi | 4/3 znaków na szacowany token, współczynnik wyjścia 0,4, ceny `null`. Funkcja Python zachowuje prefix 8000, CLI prefix 0 i zakres `all`. |

W sumie są 165 wartości skalarnych i 32 tablice. Schematy korzystają tylko ze wspieranego podzbioru `RuntimeProfile`; obiekty przyjmują rozszerzenia, listy nie mają zamkniętej liczby elementów, a słowniki MIME/mapowania ról pozwalają dodawać i usuwać wpisy. Dla bufora dodatniość i reprezentowalność są istniejącym kontraktem czytnika W5. Granice `int64`/`size_t` opisują reprezentację natywną, bez dodatkowej kwoty. Podgląd niestandardowej nazwy parsera/trybu nie tworzy jej implementacji; W5 musi sprawdzić możliwości wykonawcze po rozwiązaniu danych.

Nakładka używa istniejącego formatu `loom.runtime_profile_overlay/1`, domeny zgodnej z nazwą pliku i `overrides`/`patch`; lokalizacja to `<data_dir>/profiles/<domain>.pack`. Przykładowo `/library/json_max_depth=0` usuwa limit głębokości z tego przepisu, a `/library/generic_inference_max_bytes=0` usuwa jego limit inferencji. W domenie `import` należy rozwiązać warstwy: `library` → opcjonalne `cli_overrides` dla wywołania CLI → jawne flagi/ustawienia konkretnego wywołania. Nakładki na te trzy domeny są obecnie wyłącznie danymi podglądu.

Nie serializowano funkcji callback, tokena anulowania ani hooków wykonawczych. Niezmienniki parsera, schematy wyjściowe audytu, wersje parserów, CAS, sprawdzanie przyjętych bajtów/hashu źródła, źródłowe zakresy znaków i atomowe checkpointy pozostają operacjami silnika.

## Dowód przed i po

Nowy przenośny [test_import_profile_data.py](../../../loom/tests/compat/test_import_profile_data.py) przeszedł **11/11**, bez błędów i pominięć; CMake wykryje go przez istniejący glob testów zgodności. Zapisano [pełny log](evidence/import-profile-data/portable-test.log), [walidację definicji](evidence/import-profile-data/definition-validation.log) i [receipt.json](evidence/import-profile-data/receipt.json). Walidacja generatora była tylko odczytem; ten podwątek nie regenerował pliku `.inc` ani nie uruchamiał buildu.

Przed: 0 nowych domen danych, 0 połączeń tych domen z importem. Po: 3 domeny danych, nadal 0 połączeń z importem. Kanoniczny JSON domyślnych wartości każdego profilu jest identyczny z wartościami odtworzonymi z przypiętych źródeł. SHA-256 domyślnych wartości wynoszą:

| Domena | SHA-256 przed = po |
| --- | --- |
| `import_formats` | `2e53c09b6c6ccbe7d16b171d4e54719b0b9c522080ca3d55ea713d2ac4ebce99` |
| `import` | `8c4529721bb5cecf1a9aab3b60ec2ea866d32c3b4da9ae25000361747632a480` |
| `import_audit` | `fe3422a84ebb7e28ff0a39cf5be55063074669bc679c98e0a9d411dd84543559` |

Uruchomiono rzeczywistą zachowaną funkcję Python `estimate()` na tym samym syntetycznym agregacie: domyślne argumenty funkcji oraz jawne argumenty z profilu dały identyczny pełny JSON, SHA-256 `8c6d2e6abd706a1f4fd9eec6b8e8480c99ec2176c5f731a6de1ae5b3aac1b32d`. Wynik to 11–15 szacowanych tokenów tekstu, prefix 16 000, zero wywołań modelu i brak ceny. Pełne dane wejściowe, oba wyniki i odmienny wariant prefixu 0 są zachowane w katalogu dowodu. To sprawdzian funkcji Python, nie natywnego przebiegu importu.

Testy obejmują dodawanie aliasów i otwartych pól, bufor 1 GiB bez arbitralnego małego sufitu, presety bez ograniczenia, round-trip danych, 7 błędów typów/wymaganych pól, 2 przypadki wymagające istniejącej walidacji semantycznej audytu oraz zmianę domyślnego `resume` w źródle. Każdy negatywny wariant daje się odtworzyć z zachowanego testu i kompletnych źródeł. Wszystko offline, bez wywołań dostawców i bez opłat.

Wspierany podzbiór schematu nie potrafi wyrazić ścisłej dodatniości, relacji `low >= high > 0` ani wspólnej obecności obu cen. Schemat pozostawia te przypadki istniejącym walidatorom natywnym/Python; test pokazuje, że przypadek przechodzący sam schemat nadal musi zostać odrzucony przez walidator semantyczny. Nie dodano zastępczego, wymyślonego minimum.

Powtórzenie dowodu danych bez pobierania gałęzi:

```sh
python loom/tests/compat/test_import_profile_data.py -v
```

Odtworzenie publicznych snapshotów: dla każdej pozycji manifestu zapisać dokładne bajty `git show 4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2:<path>` pod `snapshot`, porównać długość, SHA-256 i `git rev-parse 4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2:<path>`. Test domyślnie działa z zapisanych blobów i nie potrzebuje Git ani sieci.

## Czego nie wykonano i dlaczego

Nie podłączono `import/**`, CLI importu ani narzędzia kosztów: kod konsumujący należy do W5. Nie zastąpiono archiwalnego parsera skanerem W5: to osobny kontrakt kolejności, napraw UTF-8, zachowania przy przerwanych wrapperach i identyfikatorów. Nie zmieniono mapowania migracji DIC0515 ani częściowych pozycji archiwum na „migrated”. Nie uruchomiono nowych modeli, natywnego Runtime, pełnego buildu ani pełnego CTest w tym podwątku; wspólne bramki wykonuje koordynator po regeneracji i integracji.

## Do wątku N

**Do wątku 10:** Podgląd profili ma pokazywać `UNWIRED`; ich zmiana jeszcze nie zmienia importu. `x-setting`, `x-unit`, `x-consumer` i opisy schematów wskazują ścieżki edycji i wymagane walidatory. Nie przedstawiać nowych nazw/aliasów jako dostępnych parserów przed potwierdzeniem capability API przez W5.

**Do wątku 9:** Do integracji trzy nowe domeny, test, manifest, snapshoty i receipt. Wspólny właściciel generatora musi raz odświeżyć osadzone dane po swojej poprawce P0. Ta zmiana nie zamyka istniejących otwartych zadań W5 ani bramki pełnego CTest. Nowy test rejestruje istniejący glob `loom/CMakeLists.txt:209`; nie edytowano CMake.

**Do wątku 5:** Podłączyć powyższe domeny przez istniejący `RuntimeProfile` z jedną nakładką, bez drugiego loadera. Zachować oddzielne domyślne wartości biblioteki i CLI oraz dotychczasową precedencję jawnych opcji. DIC0515 pozostaje otwarte do faktycznego podłączenia `ImportOptions`, sprawdzenia trybu `auto/off/on` i raportu możliwości wykonawczych. Dla audytu zachować istniejące walidatory semantyczne oraz wymaganie pary cen; nie interpretować `null` jako zera ani bieżącej ceny modelu.

Przed aktywacją wykonać natywny sprawdzian domyślnych wyników, metadanych, source hash/CAS/checkpointów i anulowania oraz jawny zapis hashy efektywnych profili w pochodzeniu. Odróżnić dawny wynik od nowego aktywnego przepisu; zmiana istotnego profilu powinna unieważniać odpowiedni cache/import identity, przy identycznym domyślnym profilu zachować dotychczasowe identyfikatory i bajty.

W5 OCR API jest zgodne z W11: `ocr_bytes(image, format)` otrzymuje dokładne snapshot bytes oraz format z pierwotnej nazwy pliku. Nie normalizować `.jpg` do `.jpeg`; zachować `image/jpg` i oczekiwany request, mimo ogólnego wpisu MIME `screenshot=image/*`.

Skaner W5 (`export_internal.h:154`, `export_common.cpp:428`) strumieniuje top-array i wrapper `conversations`; `projects`/`memories` i współdzielenie z archiwum pozostają osobnym zadaniem. Przy przyszłym adapterze archiwalnym głębokość 0 zachowuje wcześniejszy brak ograniczenia, zamiast automatycznie przejmować preset 512 z ImportOptions. Potrzebne są regresje napraw UTF-8/BOM/surrogatów, przerwanych tablic/wrapperów, trailing bytes, kolejności callbacków i stabilnych `blob#index`/node IDs.
