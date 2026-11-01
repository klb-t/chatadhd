# ZIP: niemutujący podgląd projekcji — 2026-11-01

Kod **3d022d3bb8dfe4fe00dd04c1fd3794edbce6092c**, baza **4324162959947e0c7fd7d41efb6c18e90a9ac776**.
TARGET_ENV_POLICY_APPLIED; kanon §0 rewizja 2. Bez publikacji i deploy.

## Zmiana i granice kontraktu

Istniejące `POST /api/client/blob/text` z `operation=projection_check` przyjmuje
teraz zapisany receipt ZIP. `projection_reproject` dla ZIP nadal zwraca
`archive_projection_upgrade_unsupported`, także przy poprawnym hash potwierdzenia.
Żadnego nowego endpointu, parsera, formatu, automatycznej materializacji ani
przepisywania globalnego selektora.

Preview używa istniejącego ograniczonego hosta `file_graph` i jego manifestu ZIP.
Weryfikuje przypiętą instalację, oryginalny blob, deterministyczne source/node/Unit
identities, raw member hash/size, locatory, Source/provenance i wymagane krawędzie
rodziców. Receipt musi odpowiadać rzeczywistemu drzewu. Projekcje dzieci są łączone
przez immutable source key/hash i source identity, **nie etykietę ani pozycję**.
Powiązane projekcje tekstowe przechodzą istniejący check/replay, zagnieżdżone ZIP
zachowują osobne drzewo. Bieżące granice depth/member/bytes pochodzą z zaufanego
profilu istniejącego czytnika; nie wprowadzono równoległego importera.

W odpowiedzi `loom.projection_upgrade/1` dodano pole `archive_preview` o schemacie
`loom.archive_projection_preview/1`: root, structure_nodes z parent/locator,
member source/key/hash/provenance, children z pełnym zapisanym receipt i osobnym
review. Root nie ma tekstowych occurrences: `current_replay=not_exercised`.
`manifest_verified=true` dotyczy sprawdzonego manifestu i wymaganych wiązań,
**nie naprawy lub integralności wszystkich możliwych krawędzi grafu**. Odmowy
replay dziecka są opisane w jego review, nie przemalowane na sukces roota.

Mapowanie jest jawnie `not_exercised`, candidates puste,
`archive_tree_migration_not_implemented`, `reproject_supported=false`.
Nieobsługiwany członek pozostaje `format_not_admitted/not_exercised`, z zachowanym
oryginałem; brak receipt, niespójność drzewa/hash/provenance lub zmieniony pin
instalacji kończy się odmową. Nie podpisujemy starej projekcji nową metodą.
Przekazanie selected_nodes należących do dziecka jako selektora root ZIP odmawia.

## Wykonany odbiór

Jedna mała syntetyczna próba HTTP:
`test_server_archive_projection_preview.ArchiveProjectionPreview` — **1/1 PASS**,
5.010 s. Rzeczywisty serwer/API, nowe konto/root, loopback, Landlock, whitelist env.
Nie wywołano modelu ani syntetycznego providera (0 żądań).

Fixture ZIP: `a/same.txt`, `b/same.txt` z identycznym polskim tekstem,
`a/nested.zip` z `głębiej/plik.txt` i `opaque.bin`. Test wykazał:

- archive→folder→file i osobne zagnieżdżone projekcje bez flatten;
- różne Source i node IDs identycznego tekstu, oryginalne hashe i raw readback;
- jawny unknown member NOT_EXERCISED, brak kandydatów migracji;
- odmowy reproject, obcego selected node i request manifest override;
- exact before/after hashe **pełnych wierszy nodes, links, loom_sources,
  loom_provenance**, inventory/hash blobów i konfiguracji: check i reopen bez zmian;
- celowo uszkodzony rodzic w zamkniętej nowej syntetycznej DB: odmowa po restarcie,
  bez naprawy/mutacji podczas check. To fixture korupcji, nie obejście w produkcie.

Pierwsza kompilacja wykryła dwa błędy typów nowego kodu (signed size oraz porównanie
string/JSON). Log zachowany; poprawiono tylko te porównania. Wznowiony przyrostowy
build **loom-server -j2 PASS** na istniejącym własnym Release cache; nie pełny build,
monolit ani nowa kopia cache. Wspólny nagłówek uzasadniał rekompilację zależnych TU.
Jedna próba HTTP wykonana po naprawie, bez seryjnych testów dawnych formatów.

## Wiązanie binarium i zasoby

Testowany server SHA256: **ada3e54e89b3a84064fd0774bf0f553e4c8426bae0498e06fe6c8d0f5bd71bab**.
Static core SHA256: `ecac7b08bab4f46f3eb10ef6d31184c53f5fc41740386b348d51cd2eeb9cbffc`.
86 plików helperów/profili builda przypięto w prywatnym runtime-binding.json;
helperów nie zmieniano. Wykonanie korzysta z packaged helpers pod własnym buildem,
a nie z importu parsera obok aplikacji.

**Nie przebudowano libloom.so**: nadal `7004d54bd106e281acddd5d352e8ab0e2ced0d710245f8a17c003ccb336e610a`.
Nie powstało nowe immutable archiwum. Paczki d490 i 5db9 pozostają bez zmian i
**nie zawierają tego preview**. Wynik jest kodem + rzeczywistym testem nowego
server executable, nie akceptacją nowego pełnego wydania ani JNI/C ABI shared lib.

Przed buildem 1,218,686,976 B wolne; oszacowany dodatkowy szczyt
128,316,640 B (core archive + 12 MiB), ponad wymaganym 1 GiB. Bez instalacji,
powiększania dysku/VM, kasowania starych danych, cache i archiwów.
Własne serwery/procesy testu zakończone, stare dowody zachowane.

## UI i next

UI nie zmieniano: SourceProjectionReview i projectionRequest nadal jawnie
blokują ZIP. Dotychczasowe odpowiedzi check dla nie-ZIP zachowują dokładnie osiem
pól; sprawdzono ich kształt na dziecięcej projekcji w nowej próbie. Nowy descriptor
archiwum jest dostępny przez API, **browser/nowy panel ZIP NOT RUN**.

Następny przyrost: jawny podgląd descriptoru w UI i osobno bezpieczny kontrakt
mapowania/migracji drzewa oraz wyborów. Nie wywodzić równoważności z nazw,
pozycji ani równego tekstu. Przed jakąkolwiek mutującą migracją zachować stare
receipts/Units, odmowy niezgodnych pinów, explicit selector decision i RPO.
Wydanie tego kodu wymaga świadomego nowego pakietu/bindingu; nie relabel 5db9.
NOT RUN: ZIP migration/reproject, UI ZIP, shared-library acceptance, pełne stare
CSV/AST/Face/P0/projection/backup matrices, realvision/API/models/CI, produkcja.
