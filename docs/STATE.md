## 2026-11-01 — jawna reprojekcja i native XLS/video/bundle (lokalny odbiór zakresowy)

Kod **e27ce9ff0076c287963a588d942d68e7bebaca3f**, baza **eae7ece4**. Zachowane
starsze pakiety/readery/GUI. Istniejący pipeline otrzymał sprawdzanie pinned
projekcji i jawną reprojekcję oryginału do nowej wersji, bez przepisywania starych
Units/receipt lub cichej zmiany globalnego selektora. Stary runtime odmawia nowego
wybranego PDF; zimna kopia sprzed zmiany rzeczywiście odtworzona i uruchomiona
przywraca stary wybór/original, **z utratą zmian po kopii (RPO)**. Nie lossless rollback.

Native legacy XLS przez przypięty catdoc/charset/config i bounded host daje
PARTIAL output matrix, bez nazw arkuszy/formuł/adresów BIFF. Video: rzeczywiste
Source/Unit/Observation/genesis, osobny oryginał/receipt/PNG, PTS+ordinal,
`has_ocr=false`, read/reuse/SQL rollback. Bundle: rzeczywisty exporter/importer,
exact conversation.json, source pointer→target ID/parent/managed original hash,
12 slotów readback/reuse; jawny INCOMPLETE zamiast deklaracji pełnego restore.

Wspólny Release core/server -j2 i poprawki tylko dotkniętych TU; pięć grup native
wykonano etapami z odrębnymi hashami bibliotek. Własny **HTTP 1/1 PASS**:
XLS wybrany ID/tekst w rzeczywistym kontekście atrapy, video/bundle/restart/original.
Końcowe scoped empty/partial/parent/cache checks PASS. Małe DOCX/CSV sentinele
z powodu zmiany transportu; 126 C exports bez zmian. Nie podpisujemy wcześniejszych
faz jako ponownie wykonanych na finalnym SHA. Webdist identyczny z ed139231,
bez ponowienia browsera. Nowy immutable pakiet **0caab73321bc7d9a565d1ec6f69e48be9108af6c4e885b2f36dfda2b4f62c63e**:
108 payload hashes, verify-package BusyBox/start/health/auth/static i relokowany
XLS default helper PASS. GNU tar nie ponawiano. Stary pakiet ee2117e8…47e8 bez zmian.

**NOT RUN/next:** nowy GUI reprojection/XLS/video/bundle; spatial selection consumer
przed wpięciem extent validatora; ZIP tree migration; video toolchain reprojection;
pełna macierz statusów/wersji bundle. Real vision NOT RUN (brak zweryfikowanej
już autoryzowanej ścieżki/pokrycia), mobile/fullrepo/deploy NOT RUN. SQL rollback
nie usuwa orphan blobs; filesystem Landlock nie jest kernelowym network sandboxem.
Produkcja/backup/restore operatora nietknięte. Kanon §0 rewizja 2.
[Pełny raport i pochodzenie](reports/PROJECTION_MEDIA_BATCH_2026-11-01.md).

## 2026-11-01 — wspólny file→graph: XLSX/ODT/EPUB i hierarchia ZIP, odbiór zakresowy

Kod **ed1392310b87aca76be7571dbc90e447e7c35a71**, baza **0252198f**. Instalacyjny
closed descriptor + istniejący bounded host tworzą natywne Source/Unit/Observation/
genesis; oryginał/pochodny UTF-8/struktura JSON osobno. XLSX dwa sheets/ranges/cells,
ODT nested units, EPUB spine/chapters; ZIP folder→file→wewnętrzny PDF/arkusz przez
tę samą aplikacyjną materializację. Poprawiono rzeczywiście ujawnione luki PDF
(prefix heading bez pustej linii, odrębne page occurrences tego samego oryginału).

Własny wspólny Release core/server i web/typecheck PASS; 5 scoped native risk checks
PASS przed późniejszym PDF-only fix i poprawką pakietu ODT; browser **3/3** plus
brakujący wspólny context **1/1**: 7 wystąpień różnych formatów i oddzielne ID tej
samej treści standalone/archive w rzeczywistym request/odpowiedzi atrapy. Reopen,
oryginały, odmowy i SQL rollback sprawdzone zakresowo. 126 C exports bez zmian.
Końcowy relokowany pakiet: ZIP→PDF/XLSX/read PASS, 48 payloadów, 26 parent edges,
integrity/FK PASS; verify-package BusyBox PASS. Stary GNU tar ENOSYS nie jest PASS.

Artefakt **ee2117e8644bed34bc2b1772df017e001b6c713842df56edbc8fee8cddf947e8**;
stare pakiety zachowane. Obraz: nowy actual local OCR ImportPanel PASS;
**vision NOT RUN**, nowy region-selector GUI i pełna fala NIEODEBRANE. Brak
realnego modelu, mobile, fullrepo, migracji starych przypiętych projekcji/wyborów,
rollback nowych formatów i deploy. XLS native/video/bundle/extent pozostają
konkretnymi wewnętrznymi hookami, nie zewnętrznym brakiem zgody. Unknown nested
CHECKPOINT jest czytelny; historyczny root EACCES zamknięty.

Szczegóły, fazy dowodów i ograniczenia:
[FILE_GRAPH_BATCH_2026-11-01.md](reports/FILE_GRAPH_BATCH_2026-11-01.md).

## 2026-11-01 — wybrany OCR i projekcja PDF: backend odebrany zakresowo

Kod **36e945fd1e3c165252a6a6d93268be5c41b70455**, baza **572c9a0d**. Istniejący region OCR
trafia do rzeczywistego kontekstu przez resolver konta (`inferred/nullconfidence`),
bez nowego silnika/importera. PDF zachowuje oryginał i osobną projekcję UTF-8,
Source/Unit/Observation/genesis; read/context ponownie ekstrahuje oryginał przez
przypiętą instalację. Wspólny transport DOCX rozszerzony o wejście w anonimowym fd;
CSV/AST manifest i pięć payloadów pozostają identyczne. C ABI 126 eksportów bez zmian.

Własna wspólna bramka: początkowo **16/17 native PASS**, jeden realny PDF read FAIL
naprawiony (canonical JSON); **2/2 dotknięte native /32 asercje PASS** po fix.
**2/2 HTTP PASS**: wybrany tekst/ID w systemowym kontekście syntetycznego providera,
restart/original i odmowy; mały CSV/AST continuity sentinel PASS.
Release **f9767a0c20e628f942f8abda9b24daebab5fe9fb23666ae70324f132840754fb**, verify-package BusyBox PASS,
relokowany default PDF helper PASS (34 payloady), bez checkoutu.
Nie ponawiano starych parser/P0/GUI/backup matrix; stare błędy zachowane.

**NOT RUN**: nowy GUI/mobile, realny model, pełny gate, rollback nowych projekcji,
deploy. Pozostałe readery/plany **nieprzyjęte native**, bez nakładania ich shared
overlayów; unknown descriptor CHECKPOINT EACCES13. Cała fala nie jest ukończona.
[Raport, hashe, wyniki i next per moduł](reports/FLEET_OCR_PDF_INTEGRATION_2026-11-01.md).

## 2026-11-01 — HOSTREADERS: CSV i Python AST odebrane zakresowo

Kod **89cb74f7b144e6f21e39c93fac2882637d9c9063**, baza **4a82c0cb**.
Zaufany lokalny parser generuje receipt z oryginalnego bloba; klient nie może
podawać poświadczającego JSON, helpera, profilu ani interpretera. Rozszerzono
istniejący bounded subprocess DOCX. CSV row/cell i Python AST korzystają
z Source/Unit/Observation/genesis/provenance i reparse przy read/context.
Dawny bloker AST poniżej jest historyczny — zastąpiony działającym host hookiem.

Własne PASS: **9/9 native**, **2/2 HTTP/context/restart**, **8/8 odmów procesu**,
uszkodzenie oryginału, G1 **11/76 asercji** i DOCX **9/172**, default packaged
DOCX, trwały graf CSV i packaged CSV po relokacji. **126 eksportów C** bez zmian.
Nowy Release **42cf960c0f89d55be91ad5175ff0b0d082a5ca1216168c0fc5a9d88329f2f61d**;
verify-package PASS z dokumentowanym BusyBox (GNU tar ENOSYS zachowany).
Stare artefakty i 16 payloadów web zachowane. Brak GUI nowych formatów,
realnego modelu, deploy, pełnego gate oraz testu rollback nowych wystąpień.
Zmiana przypiętej instalacji/parsera/interpretera wymaga osobnej decyzji migracji;
nie deklarujemy zgodności starej metody z nowym pakietem.
[Raport i granice](reports/HOST_READERS_CSV_2026-11-01.md).

## 2026-11-01 — Python AST: parser przyjęty, native hook odłożony

Baza odbioru `e2f6873c7ca24b41957b36b0127439b926068f41`; task
`fleet-python-structure-20261101`. Przyjęto lokalny parser stdlib oraz testy,
bez zmiany runtime i przyjętego pakietu. Własna diagnostyka: 4/4 przypadki,
w tym 3 spójne fałszerstwa przyjmowane przez checker bajtów C++, a odrzucane
przez pinned reparse Python. Własny provided native harness: **FAIL** przy
materializacji (`python_declaration` niedostępne), dalsze graf/context/reopen
nieodebrane. Shared hook **nienałożony**: brak zaufanego host-side parsera,
client JSON nie może poświadczać AST. 19/19 stdlib i 5 syntax to wcześniejsze
wyniki autora, nie powtórzone bramki integratora. Stary pakiet b786b7e6… bez zmian.
Szczegóły, hashe i next: [raport](reports/FLEET_PYTHON_AST_INTEGRATION_2026-11-01.md).

## 2026-11-01 — DOCX derived coordinates: in-place niezgodne, restore sprawdzony

Baza runtime/UI **5a560710f422095510316b4c9fbdb71d313ff6ca** i pakiet
**b786b7e654e6660e6382cd7d8ac8434d9ef1517f2e0326ff9003a3abc372ac10** bez zmian.
Jedna ograniczona nowa bramka syntetyczna, **4/4 etapy wykonane**: poprzednik
`e77f698e` → cold backup → kandydat z derived DOCX → poprzednik na oddzielnej
kopii → restore sprzed zmiany i ponowny start poprzednika.
**In-place compatibility FAIL:** explicit read HTTP409 i czat SSE error
`text_source_occurrence_mismatch`, 0 wywołań providera. ID selektora w config
nie dowodzi obsługi kontekstu. Kandydat miał jeden prawdziwy request/odpowiedź
lokalnej atrapy z dokładnie wybranym PL fragmentem i ID, bez niewybranych części.
**Restore PASS:** konto/sesja, rozmowa/oryginał i 3 DB integrity/FK/schema/count/meta
zgodne; 4 zamrożone drzewa bez zmian. RPO kopii 04:45:37.070102 UTC: późniejsze
2 wiadomości, pochodna/graf i selector są utracone. Nie jest to rollback produkcji.
Zachowano wcześniejszą odmowę testu przez atime katalogów po liczniku rozmiaru;
naprawiono wyłącznie harness na O_NOATIME, bez osłabienia cold_snapshot.
Nowe dane <6,1 MB alokowane w pomiarach, limit128MiB; brak buildu, zmiany runtime,
pakowania, starych macierzy lub deploy. Własne procesy zakończone.
Next: administrator odbiera dowody i uzgadnia RPO/ścieżkę wydaniową; brak
akceptacji powrotu starego executable na nowe dane derived.
Lokalny commit zablokowany: jedyne git add exit128, EROFS dla index.lock tego
świeżego worktree. Trzy pliki/patch/dowody zachowane; HEAD nadal5a560710.
Bez obejścia metadanych i bez ponowienia testu po odblokowaniu stagingu.
[Raport](reports/DOCX_COORDINATE_ROLLBACK_2026-11-01.md).

## 2026-11-01 — DOCX source parts: UI odebrane zakresowo

Na backendzie **49444c869a80a086683f5db383b1f34c9bb01a46** przyjęto tylko
10 plików audytowanej delty UI (patch `6ad56402…`), bez backendoverlayu.
Własne typecheck i Vite38,04s PASS, React z atrapą transportu **4/4**,
regresja wspólnego stanu UTF-8 **11/11**, rzeczywisty browser/native **6/6**.
Upload → jawna materializacja DOCX → rozpoznane części/odrębne wystąpienia →
wybór globalny konta → rzeczywisty kontekst i odpowiedź lokalnej atrapy;
wersje, pobranie ZIP/UTF-8 z hashami, reload/nativeRestart, ostrzeżenia,
HTTP400 uszkodzonego DOCX bez zmiany selektora. Dwie odrębne trwałe rozmowy.
**Implemented/tested zakresowo, NOT deployed.** Brak nowej kompilacji C++;
server/lib/defaultreader z przyjętego kodu7a1037ab, Landlock z checkoutem niedostępnym.
Własne 3 nieudane próby harnessu zachowane i poprawione do faktycznego kontraktu,
bez osłabiania asercji lub zmian produktu. Backend/P0/share/compat/backup niepowtarzane.
Granice: derived UTF8, nie strony/XML/AST; selector konta bez CAS, lista ograniczona;
brak mobile/JNI/realnego modelu/pełnej bramki i wdrożenia. Stare artefakty zachowane.
Code **ccada5e5c2542252112fe40caa95d4b407048e80**. Nowy immutable Release
**ccada5e5-release-docx-ui-20261101**, SHA256
**b786b7e654e6660e6382cd7d8ac8434d9ef1517f2e0326ff9003a3abc372ac10**.
Archive25/25 i verify-package(start/health/static/auth/deps/checksum) PASS.
Backendpayloady hash-identyczne z przyjętym 7a1037ab; nowy web powiązany z wykonanym GUI.
Next: administrator odbiera kod/pakiet/dowody; backup/restore produkcji i decyzja
wdrożeniowa pozostają oddzielne. Ten przyrost zakończony.
[Raport odbioru](reports/DOCX_UI_INTEGRATION_2026-11-01.md).

## 2026-11-01 — DOCX source parts: backend odebrany, UI osobno

Baza **f527ee5cc34825f38485419e4c11ef7c3ead6ad0** i pakiet **d5e7531426acb8243d653956f916ed15fc0efee2bbedd59cfe148f231348f609** zachowane.
Audytowany source DOCX snapshot1793504748406534500, productpatch587538af…,
17/17hashy stabilne; jednoapply bez STATEautora/CHECKPOINT/transportu.
Jawna materializacja DOCX tworzy osobną pochodnąUTF8/Source/Unit/Observation,
provenance do oryginału i wystąpienia; nie współrzędne ZIP/XML/stron.

Własny Release-j2 i stagedreader PASS; native DOCX **9/9,172asercje**,
G1UTF8 **11/11,76asercji**, CTest-j1 **2/2**. HTTP **4/4**:2DOCX+2konkretneG1
(context/reopen i code/version), rzeczywisty selector/request syntetycznego
providera również po restarcie. HTTPdefault packagehelper, backendcheckout
niedostępny przezLandlock. Brak zmian audytowanego runtime; testy wzmocnione.
**Backend tested, UI NIE odebrane, NOT deployed.** SelectorDB-only nie dowodzi
hashybloba; cache nie naprawia grafu; DBrollback nie gwarantuje cleanupplików.
Poprzedni binaryselector nie rozumie nowychderivedcoordinates — brak nowego
rollbackPASS, administrator potrzebuje odrębnego sprawdzenia lub kopii sprzed zmiany.
Code **7a1037ab83664d38ab0fccd892557fbf3d135652**. Nowy immutable Release
**7a1037ab-release-docx-parts-20261101**, SHA256
**cbbb87ee7f8efd563407cfee41bff6a492dbe2598f970ccca935a4dfead019b6**.
Archive25/25 iverify-package(start/health/web/auth/deps/checksum)PASS; staged
server/lib/reader/runtime identyczne ztestem,16webpayloadów bez zmian. Pierwsze
zbyt wąskie oczekiwanie runtime.json poprawiono o rzeczywisty reader_sha256;
nieudany staging zachowany, bez zmianyproduktu/rebuilda.
Next: administrator odbiera backend/pakiet/manifest, osobno auditedUI. Brak
blokera backendu; bez powtórekP0/parser/share/accounts/backup, bez deploy/modelcalls/agentów.
[Raport](reports/DOCX_SOURCE_PARTS_INTEGRATION_2026-11-01.md).

## 2026-11-01 — external ChatGPT share: runtime/HTTP/GUI odebrane zakresowo

Kod produkcyjny **3bf01f7c29981d27554dd44dbd21e2d29f42029c**: własny przyrostowy
Release core/server/lib-j2 PASS, focused native **10/10,181/181 asercji**, CTest-j1.
Nowy własny Vite webdist PASS; HTTP/browser **6/6grup**: import dwóch struktur
zewnętrznego HTML, role/parents/kVersion/provenance/original, idempotencja,
wybrana gałąź w całym rzeczywistym żądaniu i odpowiedzi syntetycznego providera,
restart/reopen i odmowy SSRF/redirect/proxy.2wywołania atrapy, brak realnego API.
Dwie poprawki harnessu (records provenance, szuflada ChatShell) bez zmiany produktu
lub osłabienia asercji; nieudane próby zachowane. Brak publicznego provider/TLS,
wszystkich profili kontekstu, mobile i pełnego repo gate; NOT deployed.

LEM/light completed i cleanup potwierdzone świeżymi receipts przed buildem.
Stary Release cache/webdist/pakiet95bb540c… zachowane, operator tools/logger bez zmian.
Odbiór code/result **e77f698ef70a7d9359011d24c24edd68c71f2c49**. Nowy immutable
Release **e77f698e-release-share-20261101**, SHA256
**d5e7531426acb8243d653956f916ed15fc0efee2bbedd59cfe148f231348f609**.
Archive25/25(24payloady+manifest), bin/lib/web zgodne z testem; istniejący
verify-package PASS: checksum/dependencies/start/health/web/auth. Stary pakiet
niezmieniony. Typecheck/parsery/P0/accounts/backup niepowtarzane.
Next: audyt administratora i odrębne bramki produkcyjne/zgoda wdrożeniowa;
brak blokera zakresowego, wszystkie własne serwery zakończone.
[Wynik i ograniczenia](reports/EXTERNAL_SHARE_INTEGRATION_2026-11-01.md).

## 2026-11-01 — external share scalony lekko; native acceptance oczekuje

Przyjęta baza produktu nadal **97a5562bed5294602161eb0e7d2f2c4bc73e2a60**,
pakiet ccb5ef9d SHA95bb540c… bez zmian. Nowy commit to **implemented,
integrated, NOT native-accepted / NOT deployed**. Na początku slot miał LEM.
Końcowy odczyt03:13UTC: LEMcompleted, slot przekazany osobnemu nativeacceptance,
które czeka na zakończenie tej fazy. Nie uruchomiono C++/CMake/Vite/Gradle/pakowania.

Audytowany task chatadhd-external-share-import-20261101, snapshot1793501984686167540,
patch2f6146f7…:24/24hashy stabilne, JEDNO apply z wyłączeniem CHECKPOINT;
23/23produktowe pliki początkowo identyczne. Parser HTML ChatGPT Next.js/turbo,
parent/branch/locator/reference-only assets i UI PL/EN. Własny web typecheck
całego src PASS (tsc --noEmit, exit0), składnia nowego harnessu JS PASS.
53Python/4Node/2C++syntax/2generatory to wcześniejsze wyniki AUTORA, nie nowy PASS.

Native10 przypadków czeka; w istniejącym przypadku dodano pełniejszą kontrolę
DB rollback/reopen. Przygotowano focused target bez monolitu i6grup rzeczywistego
HTTP/browser/context/reopen z syntetycznym HTTPS/providerem. Wszystkie te bramki
mają **0 wykonań**; nowy kod testowy C/C++ także nieskompilowany. kVersion oraz
filtrowanieactive potwierdzone w kodzie, nie w nowym rzeczywistym żądaniu.
Next: runner wznawia ten sam UUID w przygotowanym nativeacceptance; sprawdzenie
świeżej rezerwacji, własnybuild-j2,
CTest-j1 i6grup GUI/HTTP. Nie deklarować publicznego share/TLS ani Claude adaptera.
[Raport lekkiej integracji](reports/EXTERNAL_SHARE_INTEGRATION_2026-11-01.md),
[przygotowane bramki](../loom/tests/url_share/NATIVE_ACCEPTANCE.md).

## 2026-11-01 — cold snapshot scalony; niepusty recovery WAL odebrany

Baza **77efca1647fb506d41a606a0fb25e82d10bdb74a**, audytowany patch68200288…
nałożony raz;48/48dowodów i6/6hashy źródeł zgodne. Nowa bramka WAL PASS:
własny proces SQLite zatwierdził zmianę wyłącznie w WAL12392B; mainDBhash bez
zmiany, main-only nie zawierał canary. Preflight→backup→restore niezmienionym
narzędziem, read-only odczyt zWAL ma1zatwierdzony rekord/integrityok/FK0.
Źródło4/4wpisy i zaakceptowane wejście bez zmian. Osobny test/source/kopie,
Landlock i envwhitelist; bez nowych generatorów kont ani odczytu credentials.
Poprzednie10przypadków/restorekont pozostają historycznym dowodem, nie powtarzane.

Narzędzie/testy/procedura operatora zintegrowane; bez zmian runtime/log/web,
bez builda lub nowego pakietu. Immutable Release SHA95bb540c… pozostaje przyjęty.
Dokumentacja poprawiona: st_dev wykrywa inne urządzenie, nie dowodzi wykrycia
same-device bind mount. Topologia/operatorfreeze, foreignUID/GID/ACL/MAC,
produkcyjne klucze/RPO/backup/restore nadal osobną bramką administratora.
Next: odbiór tego commita/manifestu; bez push/CI/deploy i samodzielnej kontynuacji.
[Wynik WAL, pełne SHA i polecenie](reports/BACKUP_TOOLS_WAL_2026-11-01.md).

## 2026-11-01 — historyczny etap restore przed odbiorem narzędzia

Baza/result głównego worktree **77efca1647fb506d41a606a0fb25e82d10bdb74a** bez zmian.
Nowe narzędzie/docs/testy wyłącznie w osobnym fresh source; nie zintegrowane.
Istniejący pakiet ccb5ef9d SHA95bb540c… uruchomiony z odtworzoną kopią rootu kont:
health200, accounts=true, anonymous401, stop0. Pakietowa lib odczytała3 rozmowy
oraz2 oryginały z pełnymi hashami;5/5DB integrity/FK/schema/count zgodne przed/po.
Oryginalna zamknięta fixture i backup02 niezmienione41/41 wpisów każde.
Narzędzie cold snapshot:10/10 praktycznych przypadków PASS. Pierwsza odmowa ACL
obcego UID oraz odmowa restore po zmianie atime przez harness zachowane; poprawa
harnessu, nowa kopia i rzeczywisty restore bez osłabienia asercji. Końcowy fix
no-follow dla INCOMPLETE uzasadnił nowy cel restore03 i tę samą zakresową bramkę.
Własny UID/GID/xattr/same-UID ACL odebrane, obce UID/ACL/MAC nadal bramką admina.
WAL fixture0B, rozmowy bez wiadomości; brak production RPO/restore/GUI/loginmatrix.
Bez rebuild/pakowania/deploy; stare backendy i dowody niepowtarzane.
Next: audyt patcha, następnie osobna decyzja integracyjna; produkcyjny backup/
restore i zgoda wdrożeniowa pozostają administratorowi/właścicielowi.
[Raport i ograniczenia](reports/BACKUP_RESTORE_2026-11-01.md),
[procedura operatora](../deploy/COLD_SNAPSHOT.md).

## 2026-11-01 — logger scalony, nowy Release odebrany zakresowo

Kod **`ccb5ef9d9a4879bc9634db7296a60ce4d35404ce`**, baza6cfd9a40 zachowana.
Audytowana delta loggera nałożona raz; pełny stary Release build skopiowany
bez reflink i porównany410/410 wpisów. Przyrostowy Release server/lib-j2 PASS,
bez CLI/loom_tests;6 ostrzeżeń niezmienionych obszarów zachowanych w logu.
Nowy pakiet `ccb5ef9d-release-log-control-20261101`, SHA256
`95bb540ca669dc6410af75aa135434800374957bea7a6cc7a5d2359cfd643481`.
24/24 payloady zgodne;16web+2DOCX/helper payloady niezmienione, nowe binaria.
Stary pakiet8b2c0e68 SHAe65ed96e... nadal identyczny, Debug/dowody zachowane.

Własny **CTest3/3 PASS -j1**: rzeczywista lib (7symboliC++,126eksportówC bez zmian),
2procesy C API (canary w stderr/ringu/callback przy1; wszystkie puste przy0;
błędy API nadal zwracane), isolated server health/static/auth200 i odmowy401/403,
istniejący verify-package na NOWYM archiwum. Landlock, whitelist env, syntetyczne
dane, bez modeli/API. Wszystkie własne serwery zakończone. Błąd skryptu
pakowania i przedwczesny preflight testów zachowane/naprawione bez powtórki
buildu, install ani osłabienia asercji; szczegóły w raporcie.

Integrated/built/tested zakresowo; **deployed NIE**. Nie powtarzano focused
6/6+453 ani starych accounts/single-runtime/G1GUI/P0/rollbacków. Brak pełnego
gate/GUI/mobile/TSAN dla nowego artefaktu. `LOOM_LOG_ENABLED=0` testowane tylko
lokalnie; directstdio/PhaseTimer/helpers/browser i pełna sanitizacja poza zakresem.
Runtime disable nie jest drain barrier, stary ring pozostaje. §0rewizja2.
Next: odbiór pakietu przez administratora, jego backup/restore i odrębna zgoda
wdrożeniowa; bez samodzielnego kolejnego cyklu/push/CI/deploy.
[Raport z pełnymi SHA, komendami i ograniczeniami](reports/LOG_CONTROL_RELEASE_2026-11-01.md).

## 2026-11-01 — wyłącznik loggera, etap scalenia przed buildem

Baza `6cfd9a40052859c4efd3333655def3a41a0aebc5`; odebrany patch
`092c3d6f8ecbffc4e6d1a70f9ca34b9c6b88e77a19f88513d513799330622c0f`.
Audyt administratora i własna stabilność25/25, nałożenie raz i hashe8/8 PASS.
Scalono logger do głównego worktree: `LOOM_LOG_ENABLED=0` blokuje dopisywanie
przez `loom::log` do stderr/ringu/callbacków; poziomy i historyczny stderr switch
zachowane. Ustawienie startowe poza profilem, runtime disable/reenable zgodnie
z nim; stare wpisy ringu pozostają, wyłączenie nie jest drain barrier.
Własny focused native build-j2 PASS, procesy6/6 (453 asercje), symbole C++7/7;
dwa zakresy CTest-j1 PASS. Początkowy błąd przechwytywania stderr w harnessie
zachowany i naprawiony bez osłabienia asercji. Dane wyłącznie syntetyczne.
Implemented/tested zakresowo; integrated lokalnie, deployed NIE. Bieżący immutable Release
niezmieniony i nie zawiera poprawki. Direct stdio serwera, PhaseTimer i inne
niezależne ujścia nieobjęte; brak pełnego PASS sanitizacji/logowania aplikacji.
§0 rewizja2. Next: nowy własny Release i zakresowy odbiór rzeczywistej lib/server;
starego pakietu nie relabelować, build/testy artefaktu jeszcze niewykonane.
[Kontrakt, dowody, ograniczenia](reports/LOG_CONTROL_2026-11-01.md).

## 2026-11-01 — G1 GUI przyjęte, nowy lokalny pakiet Release

Kod **`8b2c0e689b1ddf43c74519e68ed626e79d5a4b16`**, hook uploadu `d54030114b7d83d991b0f04f8f068b9399d24077`;
poprzedni Release `a033aad8538519ca78058436bb17da604453a293` i wszystkie backendy zachowane.
Świeży odbiór delty G1GUI19/19 zgodnych plików przed/po, bez dostępu do
chronionego archiwum. Upload TXT/MD/kodu zapisuje trwały source_key przed jawną
materializacją; SourcePartsPanel łączy oryginał, wystąpienia, GraphView i globalny
selector konta. **Implemented/tested zakresowo; deployed NIE.**

Własne: typecheck/webbuild i przyrostowy Release server+lib `-j2` PASS;
Node15/15, React/browser komponent3/3, nativeHTTP5/5, rzeczywiste G1GUI8/8,
PNG GUI2/2. ID i wybrany tekst w systemowym kontekście oraz widocznej odpowiedzi
syntetycznego dostawcy; wersje, oryginały/restart i zachowanie innych pól
context_execution sprawdzone. Trzy wcześniejsze nieudane przebiegi harnessu
zachowane i opisane; nie zmniejszano asercji. CTest `-j1`.

Pakiet `8b2c0e68-release-g1gui-20261101`, SHA256 `e65ed96eab67d119b20852e05d65b0bcc4fb8a9ce24b5978a92c7d1fa43ba1db`:
24payloady/16webhashy zgodne, source/tree wskazane w manifeście.
Staging **2/2 PASS** po GUI: istniejący verify/installer, przełączenie i powrót
do poprzedniego własnego Release e470, również odczyt nowego wystąpienia G1 po
powrocie. Bez zmian danych przez instalator i bez mutacji pakietów; procesy
zakończone. Tylko lokalny staging, nie rollback produkcji.

Wybór globalny dla konta/wszystkich rozmów, brak CAS; bounded list nie dowodzi
nieistnienia źródła. JNI/fizyczny Android, pełne AST/tabele/współrzędne PDF/DOCX/OCR,
realny model, pełny repo gate i deploy nieodebrane. Nadal GNUtar ENOSYS (w teście
własny wrapper BusyBox), źródłowy installer --package nie obejmuje pełnego runtime,
zależności systemowe niebundlowane. Starych pełnych zestawów nie powtarzano.
§0rewizja2; globalny wyłącznik logów i pełna polonizacja nieudowodnione.
[Raport, komendy, hashe i ograniczenia](reports/G1_GUI_ACCEPTANCE_2026-11-01.md).
Next: odrębna decyzja administratora; bez kolejnego cyklu, push/CI/deploy.

## 2026-11-01 — lokalny pakiet Release i wykonana akceptacja stagingu

Źródła produktu **`e47079cf9f44b0b90654d21770bcfb24c4fd34cc`** pozostają bez zmian;
G1/PNG i starsze przyjęte commity zachowane. Nowy własny Release `-O3 -DNDEBUG`,
`loom-server` + `libloom`, build `-j2` **PASS**; runtime component + własny
webdist 16/16 hashy, helper DOCX i manifest interpretera w osobnym archiwum.
Wersja `e47079cf-release-20261031`, SHA256 archiwum
`fa5c71fc6bedb5ad82e6b93297571b7ba6309f5e18b434849983bf8bf883d744`.

Nowy staging po relokacji, z checkoutem niedostępnym: **8/8 PASS**, zero skip
(PL/dialog/błąd/restart, provider/BYOK, PDF PL/warnings, OCR/kontekst,
DOCX default/original/reopen/odmowa, libDOCX, URL/share i odmowa prywatnego adresu).
CTest: pierwszy przebieg 1/3 (backend 8/8); GNU tar zwrócił ENOSYS.
Ponowiono tylko dwa zatrzymane zakresy z istniejącym BusyBox tar 1.37:
weryfikator archiwum i lokalny dry-run stage→switch→return **2/2 PASS**.
Łącznie odebrano wszystkie trzy zakresy, bez powtórki backendu. Dane i
konfiguracja poza pakietem; rozmowa, PL i oryginalny plik odczytane po obu
przełączeniach. Pakiety niezmienione. Baseline powrotu to własny Debug tego
samego SHA; nie jest to dowód migracji/rollbacku nieznanej produkcji.

Wyniki dotyczą syntetycznych plików i dostawców loopback. GUI/mobile,
pełny repo gate, sanitizery, rzeczywiste modele i wdrożenie **niewykonane**.
Release używa systemowego SQLite i tutejszych bibliotek (glibc 2.43);
Python/Tesseract/Poppler są zależnościami systemowymi, nie instalowano ich.
Nie deklarować przenośności oficjalnego pakietu Ubuntu ani adaptera share
zewnętrznego dostawcy. G1GUI pozostaje osobnym odbiorem; bez kolejnego cyklu.
[Raport, hashe i ograniczenia](reports/PACKAGE_ACCEPTANCE_2026-10-31.md).
Bez push/CI/deploy. §0 rewizja 2; globalny wyłącznik logów nadal nieudowodniony.

## 2026-10-31 — backend G1 i poprawka prezentacji PNG przyjęte

Przyjęty końcowy kod **`5d653f73e25033881242bad3f72a6a45e0473d97`**.
G1 odebrano osobno w **`42e5c5be77448bb1c228573672fcc164d05bf1da`**;
baza integratora6c46bd28 i wszystkie wcześniejsze P0/M1/M2/DOCX/package/GUI zachowane.
Bloker EACCES usunięty administracyjnie; integrator potwierdził20/20hashy,
HEAD/status/diff przed i po odczycie. 19/19 końcowych zmienionych źródeł zgodnych
z code_head. Oba generaty odtworzone na scalonych danych, default DOCX zachowany.

G1: jawna materializacja istniejącego bloba → Source/Unit → Segmenter/genesis →
odrębne wystąpienia, odczyt oryginału/zakresu oraz istniejący unified context.
UTF-8/TXT/Markdown/kod jako tekst, częściowy wynik z pochodzeniem; bez AST,
pełnych rozdziałów/tabel i adapterów współrzędnych PDF/OCR/DOCX.
Efektywny język G1 rozstrzygnięty bez zmiany historycznej tożsamości segmentera.
Cache projekcji to receipt idempotencji, nie dowód integralności/naprawy grafu.

Własny build-j2 PASS; native G1 **11/11**, regresje fragmentów14/genezy7/
flow8/unified14 PASS (CTest5/5,54przypadki). HTTP G1 **3/3**, dialog2/tekst3
PASS (CTest3/3,8przypadków); ABI **3/3**. Oryginały, wersje, reopen, powtórzenia,
wybrana treść w faktycznym żądaniu syntetycznego providera, sesje/CSRF/izolacja
sprawdzone. Nie ma rzeczywistego modelu ani pełnej bramki repo.

Osobny commit PNG: importtruth/legacyJSON **9/9**, rzeczywiste GUI **2/2**.
Komunikat to „Pliki źródłowe: 1”, tytuł `[Import] synthetic-pl.png`, bez
fałszywego ostrzeżenia JSON/ZIP. Historyczny patch4b2852db jest **już nałożony**,
nie stosować ponownie. Użyto zachowanego własnego webdist; nie powtarzano
package5/GUI7/component4 ani całych wcześniejszych backendowych bramek.

Implemented/tested: powyższy backend i PNG GUI. **GUI G1/upload→materializacja/
selection→context nadal next**; bez claimu całego przepływu. Szersze wymagania
grafu i control→binding→setting otwarte. §0rewizja2; globalna hierarchia/jeden
wyłącznik logów nieudowodnione. Deployed **NIE**; fizyczny mobile, live model,
Release/sanitizer/pełny repo gate i rollback niewykonane. Role labels/raw503
oraz pozostała polonizacja nadal otwarte. DOCX wymaga systemowego Pythona.
[Raport z SHA, poleceniami, dowodami i ograniczeniami](reports/G1_IMPORT_PRESENTATION_INTEGRATION_2026-10-31.md).
Następny cykl tylko przez administratora, bez push/CI/deploy.

## 2026-10-31 — odbiór G1 odłożony: EACCES w worktree autora

Zachowana przyjęta baza produktu **`3319312f22e1d7920a051a2496337fb6ee5e9554`**,
kod package/GUI `daf4f48412924737f260a42499818ea6bbeba513`. Sprawdzono20/20
poprzednich plików bez zmian. Nie powtarzano zaakceptowanych testów.

G1 `chatadhd-graph-source-parts-20261101`, snapshot1793485310488154795,
ma completed/stable audited receipt, lecz aktualny odczyt **UID/EUID990**
zwraca **EACCES(13) dla12plików produktu oraz .git autora**. Git status exit128.
Checkpoint i7plików handoff mają zgodne sumy przed/po; nie rekonstruowano
z patchy plików objętych odmową. Nie nałożono zmian G1, brak build/test PASS.
Native8/HTTP2 autora są wyłącznie przygotowane, niewykonane.

Odczytano wszystkie4absolutne dokumenty kontekstu, w tym §0rewizja2;
nie są brakujące. Next: przywrócony zwykły odczyt dla integratora i świeży
odbiór; sprawdzić fallback seg.language(), granice cache, autoryzację trasy,
źródła/wystąpienia/wersje i rzeczywisty kontekst. Generaty odtworzyć po scaleniu
.pack bez nadpisania defaultów DOCX. GUI/upload→materializacja pozostają osobno.
[Dokładne ścieżki, hashe, zakres odłożony i kontynuacja](reports/G1_ADMISSION_BLOCKER_2026-10-31.md).
Bez nowego cyklu, push/CI/deploy ani ingerencji w ACL/hosta.

## 2026-10-31 — domyślny pakiet DOCX i funkcjonalne GUI OCR/DOCX

Przyjęty kod **`daf4f48412924737f260a42499818ea6bbeba513`**;
pakiet `ed3b8cbc48e7f74a9c3803306fdebd32cef8ec7d`, baza `d0af04a2` zachowana.
17/17 zmienionych plików zgodnych z testowanymi bajtami. Poprzednie M1/M2/P0/DOCX
nie były ponownie nakładane ani poddane pełnym powtórkom backendowych bramek.

Domyślny reader/config DOCX jest teraz dostarczany przez komponent CMake runtime.
Własny relokowany staging z rzeczywiście niedostępnym checkoutem: **5/5 PASS**
(PL HTTP/kontekst/reopen/restart/original, brak helpera/interpretera,
niezaufana nazwa uploadu, shared-library C API). Własny przyrostowy build `-j2`
PASS. Python >=3.10 pozostaje jawną zależnością systemową; testowano 3.14.4,
bez instalacji hosta. Nie jest to pakiet z bundlowanym interpreterem.

Nowy własny web build PASS (22,83 s). Rzeczywiste GUI ImportPanel/ChatView
**7/7**: lokalny polski OCR → rzeczywisty kontekst/odpowiedź syntetyczna;
DOCX → tekst/warnings/oryginał/reopen/restart; odmowa i potwierdzenie sekretu
z dokładnym hashem. Nowy przycisk pobiera oryginał dopiero po sprawdzeniu SHA256.
Komponent pobrania **4/4**, końcowe ABI **3/3**, runtime `--check` PASS.
Dane i provider syntetyczne; istniejący własny Chromium, bez rzeczywistego modelu.

**Pełna akceptacja prezentacji GUI nadal otwarta:** ImportPanel błędnie opisuje
PNG jako JSON bez ZIP, a tytuł importu zawiera techniczną nazwę req_…png.
Jawny mały patch czterech wspólnych plików przekazano jako **nienałożony,
nietestowany native** do późniejszego odbioru z G1; wyłącznie `apply --check` PASS.
Nie zmieniono genesis/client_blobs. Zachowane role labels/raw503 i inne luki
polonizacji nie stają się PASS. Viewport 420×900 nie dowodzi fizycznego mobile.

Implemented/tested tylko powyższy zakres; deployed **NIE**, pełny repo gate,
realny model, Release/sanitizer i rollback wdrożenia niewykonane. DOCX częściowy,
warnings zachowane. §0 rewizja 2: integralność w kodzie, presety/teksty w danych;
jednego wyłącznika i hierarchii logów nadal nie dowiedziono.
[Raport, dowody, ograniczenia i patch do dalszego odbioru](reports/DOCX_PACKAGE_GUI_2026-10-31.md).
Następny cykl wyłącznie przez administratora; bez push/CI/deploy.

## 2026-10-31 — M1 oraz skonfigurowany podstawowy DOCX przyjęte zakresowo

Przyjęty i testowany kod **`4f6656bcfc515fff8a887a37ee7f1a932212b969`**.
M1 przyjęto wcześniej w tym przyroście w `d8692078ecd95ddb9835f69c00c65524c6d6a789`;
P0/M2/UI i wszystkie wcześniejsze commity zachowane. 49/49 plików kodu/testów
zgodnych z testowanymi bajtami. Baza publiczna pozostaje przodkiem.

Własny przyrostowy build `-j2` PASS. DOCX **40/40 parser, 7/7 HTTP PASS**:
rzeczywisty multipart, polska treść i ostrzeżenia w kontekście, idempotentne
źródło/hash, oryginał, dalsze tury/reopen/restart; odmowy uszkodzeń, ZIP/XML,
limitów, innych formatów i braku helpera. Dodatkowy test sekretu wyłącznie
w XML odmawia przed dostawcą i zapisem rozmowy, także po restarcie.
Dane syntetyczne, provider loopback; żadnego rzeczywistego modelu/API.

M1 po odblokowaniu odczytu PNG: lokalny OCR/parser **16/16**; na scalonym kodzie
native **17/17 (1183 asercje)** i ABI/HTTP **8/8 PASS**. Zgoda związana z hashem,
prepare/persist, wybrana historia przed routingiem i granice body_bytes sprawdzone.
Eksportowane ABI **3/3**, CTest `-j1` **6/6 zestawów / 23/23 przypadki, 0 skip**
(PDF5/dialog2/text3/locale2/BYOK9/CAPI2). Błędy własnej konfiguracji CWD testów
zachowano i poprawiono bez zmiany bramek; 11 ukończonych regresji nie powtarzano.

Implemented/tested: opisany backend, **nie pełne wydanie**. DOCX pozostaje
częściowy, z warnings; bez formatowania/paginacji, DOC/XLSX/PPTX/ODT ani OCR
osadzonych obrazów. **Domyślny helper `/usr/share/loom/docx_basic/reader.py`
nie jest dostarczony w instalacji**: testowano jawną własną konfigurację;
pakiet/helper/interpreter trzeba odebrać przed wdrożeniem. Bez instalacji tutaj.
Nowy GUI OCR/DOCX, fizyczny mobile, realny model, pełny repo gate i rollback
deploy niewykonane; deployed **NIE**. Poprzednie P0 GUI 9/9+8/8 i UI hash4/4
pozostają wcześniejszym dowodem. Anthropic vision/zewnętrzny share nadal brak.

§0 rewizja2: dane limitów oddzielone od ochrony integralności; inference jawne.
Globalna hierarchia/jeden wyłącznik logów nieudowodnione, bez refaktoru.
[Raport, SHA, dowody, ograniczenia i odwracalność](reports/M1_DOCX_INTEGRATION_2026-10-31.md).
Następny przyrost wyłącznie przez administratora; bez push/PR/publikacji/deploy.

## 2026-10-31 — P0 GUI potwierdzone, M2 przyjęte, core M1 odłożone

Przyjęty kod **`5972828c46ac8c8675d132ee8ad557382c5726b6`**: M2 URL `c756c9dd1c1cd33b948b5e95351173d34c459fae`
i własne przekazywanie `image_payload_sha256` przy potwierdzaniu w GUI.
P0 `cab9ca88` i wszystkie wcześniejsze commity pozostają przodkami.
26/26 commitowanych plików zgodnych ze sprawdzonymi scalonymi bajtami.

Własny przyrostowy build `-j2` PASS; M2 native **8/8 (107 asercji)**,
artefakt bez dostępu do checkoutu **1/1 (18 asercji)**, Python **33/33**,
pakietowanie **4/4**, żądania web **4/4**, HTTP odmowa oraz dodatni
import/restart **PASS**. Regresje CTest `-j1` **6/6 zestawów, 23/23 przypadki,
0 skip** (P0 12 + BYOK 9 + C API 2), ABI **3/3**. Nowy web build PASS;
GUI języka **9/9**, osoba PL **8/8**, potwierdzenie sekretu **4/4 grupy**.
Wcześniejsze GUI P0 odebrano z zachowanych logów; nowych przebiegów nie sumowano.

M2 używa osadzonego helpera, zachowuje źródłowe bajty i pochodzenie,
nie wywołuje modelu. Dodatni HTTP ma jawny testowy DNS/connect do loopback,
nie dowodzi publicznego TLS ani adaptera share zewnętrznego dostawcy.
Core M1 **nienałożone**: odczyt `synthetic-pl.png` autora zwraca EACCES(13).
Bez obchodzenia praw/archiwów; M1 native 11 + ABI/HTTP 7 i runtime generator
czekają na udostępnienie fixture i świeży odbiór. UI hash sprawdzony z atrapą
backendu nie stanowi native acceptance OCR/vision.

Implemented/tested: zakres powyżej. GUI accepted: scenariusze syntetyczne,
bez fizycznego mobile; raw503/role labels nadal luki produktu.
Deployed **NIE**, pełny repo gate / real model / zewnętrzny share **NIE**.
§0 rewizja 2: ochrona URL i integralność w kodzie, presety jako dane;
globalna hierarchia logów/jeden wyłącznik deploy i pełne części/graf poza dowodem.
[Raport, SHA, źródła i kontynuacja](reports/M1_M2_INTEGRATION_2026-10-31.md).
Następny przyrost uruchamia administrator; bez push/PR/publikacji/deploy.

## 2026-10-31 — własne P0 web/GUI odebrane po korekcie preflight

Na `cab9ca88d725c9d78cd8bceb8993443c2969624f`: web build **PASS**, rzeczywisty
`lang-page-native` **9/9**, `polish-person-native` **8/8 PASS**. Własny serwer
z wcześniej przyjętego code_head9f672641, zachowane hashe, dane syntetyczne,
Chromium i dostawca loopback. Po przerwie odebrano zapisane wyniki, bez ponownej
kompilacji/GUI/12HTTP+2 regresji. Poprzednie twierdzenie o braku wszystkich
zależności było błędem preflight: wymagane rozwiązywanie względem loom/web/package.json.

Zapis EN/PL/profilu, PDF2strony, dalsza tura/reopen/ostrzeżenie i istniejący graf
potwierdzone w GUI. Widoczny surowy HTTP503 i role user/assistant/v1 pozostają
lukami produktu, nie środowiska. Wąski viewport nie jest fizycznym telefonem.
GUI accepted zakresowo; pełne wydanie/model quality/deployed nadal NIE.
[Uzupełniony raport i granice dowodów](reports/P0_INTEGRATION_2026-10-31.md).
M1/M2 pozostają osobnym odbiorem w toku; nie są przyjęte na podstawie tych testów.

## 2026-10-31 — P0 locale/PDF przyjęty zakresowo, GUI nadal otwarte

Przyjęty kod i baza dla osobnego G1: **`9f672641e5eb223cf7bdc5561faca08dc679a753`**.
Zachowano `08c6d1d15e54e0be7f7d605fbc90d066e02ebccd` (macierz dostawców)
i wcześniejszy BYOK jako przodków. Worker base `1d53bd0` nie był poleceniem
resetu. Locale: `85bda3dd2f8b6ce6d35005c58b3911662f53534a`; PDF/dialog
z testami integratora: `9f672641`. Stabilne completed delty sprawdzono
przed/po względem świeżych receipts, checkpointów i hashy; archiwów root-only,
cudzych buildów i prywatnych notatek nie importowano.

Własny nowy WERROR build serwera/biblioteki `-j2` **PASS**. Końcowy CTest
`-j1` na wskazanym SHA: **4/4 zestawy, 12/12 przypadków, 0 skip** (120,85 s):
PDF **5/5**, dialog **2/2**, tekst UTF-8/TXT/Markdown/kod jako tekst **3/3**,
trwałość ustawienia języka i profilu przez HTTP **2/2**. Rzeczywiste żądania
loopback, dokładna treść, oryginalne bajty, następne tury, reopen/restart,
trwałe ostrzeżenia i kontrolowane błędy. Osobno wybrane regresje granicy
poświadczeń **2/2 PASS** (14,02 s) na identycznych scalonych bajtach;
nie powtarzano całego starego odbioru BYOK. Nie sumowano dwóch przebiegów CTest.

Locale odtwarza profil bez otwierania panelu i odświeża język powłoki po zapisie
karty; jawny wybór pozostaje nadrzędny. W receipt poprawiono wielokrotne
odnotowywanie jednej dyspozycji `wider`. Integrator dodał testy HTTP tekstu
i locale; brak nowej implementacji formatów biurowych, ABI lub migracji tabel.

**Implemented: przyjęte; tested: backend zakresowo; deployed: nie;
GUI/mobile accepted: nie.** Brak własnych zależności web/React/Vite/Playwright,
przeglądarki i obsługi TS w systemowym Node; instalacje poza zleceniem.
GUI 9/9 i osoba 8/8 w raporcie locale są wynikami autora, nie nową bramką.
Zachowane luki języka logowania/gościa/ostrzeżeń auto i prezentacji błędów.
Pełna ocena wydania 2026-11-01 09:00 PL wymaga osobnej próby GUI/mobile;
raport zawiera test akceptacyjny i odwracalność do pakietu na 12:00 PL.
Brak pełnej bramki repo/release, rzeczywistych modeli/API, OCR i DOCX/XLSX/PPTX.

Kanon §0 rewizja 2: rozdział danych/inwariantów zachowany; wspólny wyłącznik
ring/sinks/stderr oraz hierarchia logowania pozostają nieudowodnione, istniejące
logi załączników zawierają nazwę/ścieżkę. Bez niezwiązanego refaktoru.
G1/macierz/OCR/URL-share nie blokują tej sprawdzonej bazy; kolejne zadanie
uruchamia administrator. Bez publikacji, push/PR i wdrożenia.
[Raport odbioru, hashe, zakres i bramki](reports/P0_INTEGRATION_2026-10-31.md).

## 2026-10-31 — lekki odbiór macierzy dostawców

Na bazie `1d53bd0fcdd902cbac1809ab7cdfa96fd0c1fe19` odebrano dokumentację
z taska `chatadhd-free-providers-20261101`: [raport](reports/FREE_PROVIDER_FEATURE_MATRIX_2026-10-31.md)
i [macierz JSON](reports/FREE_PROVIDER_FEATURE_MATRIX_2026-10-31.json).
**238 = 17×14** wierszy; według klasyfikacji autora **17 potwierdzonych,
43 częściowe, 178 pozostałych**. Własny walidator dokumentacji: **627/627 PASS**,
42 lokalizatory kodu/testów, 130 lokalnych linków, 8 tabel; to nie test produktu
ani potwierdzenie wszystkich 65 źródeł. Próbka administratora obejmuje tylko
trzy warunki T4/G10/R1. Nie rozszerzono jej na pozostałe twierdzenia.

Minimalna korekta integratora: backlog i propozycje testów nie rozszerzają
celu 2026-11-01 09:00 PL (locale z wyborem języka, BYOK, HTTP z dwustronicowym
PDF/dalszą turą/reopen/ostrzeżeniami, dialog), nie przesuwają ograniczonego G1
ustalonego przez właściciela i nie nadają zgody kosztowej. Usunięto odsyłacz
Gemini API jako dowód warunków Vertex w JSON. Zachowano wszystkie 238 obserwacji,
liczniki i historyczne base_head/result_head badania. Kanon §0 rewizja 2.

Wyłącznie dokumentacja: **0 nowych buildów/testów produktu/API/wydatków**,
bez browsera/serwera i bez powtarzania odbioru BYOK. Status przyjętego kodu,
GUI i wdrożenia pozostaje jak poniżej. Kolejny odbiór P0 uruchamia administrator
w tej samej sesji po rzeczywistych deltach locale/PDF i świeżym audycie receipts.

## 2026-10-31 — Flow przyjęty po własnym buildzie i testach

Przyjęty kod: **`885e493945b125ee72e2fa293682be7d83ae76c5`** na publicznej bazie
`9533d68e6cf6e133e0fdcd5d5fcf7ee85654da08`. B1/B2/B3 rozwiązane w zakresie
odbioru. Własny WERROR build serwera/biblioteki `-j2` PASS; HTTP **9/9**,
C API **2/2**, native **23/23** (6567/6567 asercji), ABI **3/3 PASS**.
Dodatkowy test wykrył utratę klucza przy A → B → A; mała korekta integratora
przeszła ten sam test **1/1** oraz rozszerzone scenariusze. BYOK, również pusty,
zachowuje pierwszeństwo. Oryginalny negatyw i dowody autora zachowano oddzielnie.

Osobno przyjęto badania: Android `f7c5a922450be294c8c48c3c7d636dce42d876a0`
(**2/2** kontrole statyczne, bez implementacji HOME); Linux
`c75241edb176ef6f21104736ebcbd59c2bb485cf` (**8/8** kontraktów, 0 skip,
bez adaptera/D-Bus/GUI). Późniejsze raporty nie zmieniają przetestowanych bajtów Flow.

Stan: implemented/tested zakresowo; **bez deployed i GUI accepted**.
Nie wykonano pełnej bramki, webu, HTTP/PDF ani testów urządzeń. Otwarte:
równoległość i pozostałe nadpisania tras, wspólny wyłącznik oraz hierarchia
logów według §0 rew. 1 z 2026-10-31. Następny priorytet właściciela: polski
interfejs z wyborem języka, dalej rzeczywisty HTTP/PDF, szersze pliki i własne
grafy. Świeże taski/worktree uruchamia administrator; niczego nie publikowano.
[Pełny odbiór, hashe, testy i ograniczenia](reports/TEAM_INTEGRATION_2026-10-31.md).

## 2026-10-31 — wznowienie odbioru po korekcie kontraktu

Baza kodu `9533d68e6cf6e133e0fdcd5d5fcf7ee85654da08`. B1 rozwiązany:
`checkpoint_sha256` dotyczy archiwum `files.tar.gz`; osobne sumy dokumentu,
patcha i plików potwierdzono **3/3** względem aktualnego przekazania administratora
z 17:46 UTC. Poprzedni audyt i wyniki pozostają poniżej. B3 rozwiązany:
rzeczywisty staging dwóch plików przeszedł; B2 (testy Flow) pozostaje do wykonania.
Własny etap dokumentacyjny, 0 przyjętych delt produktu. Rezerwacja jednego
własnego buildu `-j2` potwierdzona w nowym zleceniu; bez publikacji i wdrożenia.
[Raport i skorygowany kontrakt](reports/TEAM_INTEGRATION_2026-10-31.md).

## 2026-10-31 — inwentaryzacja odbiorów integratora, przyrost 1

Baza i kod produktu: `9533d68e6cf6e133e0fdcd5d5fcf7ee85654da08`. Własny przyrost
jest dokumentacyjny: **0 przyjętych delt i commitów wykonawców**. Manifest z
17:30 UTC podaje `completed` dla Flow, Androida i Linuksa; ponowny odczyt
potwierdził stabilność 3/3 worktree. Odbiór odłożony: wskazane snapshoty zwracają
EACCES, a zakres pola `checkpoint_sha256` nie jest opisany i jego wartości
różnią się od sum lokalnych checkpointów. Flow dodatkowo wymaga własnego testu
integratora; wyniki autora nie są nową bramką.

Własne sprawdzenia: próba kontraktu Linuksa w kopii pod CWD **8/8 PASS, 0 skip**;
Android **2/2 kontroli statycznych PASS**, 41 plików / 0 dopasowań słownika API
launchera; zastosowalność patchy Flow/Linux **2/2 PASS** bez nakładania.
Bez buildu jądra, pełnej bramki, HTTP/GUI/urządzeń i wdrożenia w tym przyroście.
Android/Linux pozostają badaniami. [Raport odbioru, hashe i kolejność](reports/TEAM_INTEGRATION_2026-10-31.md).
Commit dokumentacyjny nie powstał: jawny `git add` zwrócił `Read-only file system`
przy własnym `index.lock` integratora; brak zmian w stagingu, pliki zapisane w worktree.
Wymagane przywrócenie zapisu metadanych przez administratora, bez obchodzenia dostępu.

## 2026-10-31 — odczyt PDF i kontynuacja rozmowy z plikami

Na gałęzi `codex/chatadhd-fixes-20261031` dodano lokalny odczyt warstwy tekstowej PDF na Linux (Poppler-GLib), sprawdzanie wydobytego tekstu pod kątem sekretów i odtwarzanie załączników w kolejnych turach zwykłego czatu. Ostrzeżenia o stronach bez tekstu/przycięciach są trwałe i dostępne w webie; opcjonalne pole `attachment_notes[].message` oraz notatki w `done`, bez migracji tabel. Bez OCR, DOCX ani dowodu kompletności wszystkich przepływów.

C++/web build PASS; regresje 30 CTest: 29 PASS i jeden zestaw ze starymi oczekiwaniami dwóch obiektów przycięcia, po jawnej aktualizacji o pole komunikatu jego powtórzenie PASS. Web 56/56, generatory PASS. Bez nowego pełnego przebiegu całego repo, HTTP/GUI, telefonu i wdrożenia. Baza przyrostu `b3263b05`; produkcja odczytana jako `2d39b07c`.

[Macierz funkcji, zakres i wyniki](reports/CHAT_DOCUMENT_FLOWS_2026-10-31.md).

## 2026-10-31 — sprawdzone poprawki jądra i interfejsu

Kod przygotowany do przeglądu na bazie pkg-webfix 910dca31. Naprawy dotyczą języków okien i pulpitu, genezy tekstu, częściowego odczytu metadanych, potwierdzania operacji kosza, diagnostyki oraz niejednoznacznych dostawców modeli gościa. C++/web zbudowane; 72/72 zestawy logiki webu zielone. W serii 219 C++ unit: 218 PASS i jeden nieaktualny wzorzec; po korekcie dwóch potwierdzonych wpisów jego osobne powtórzenie PASS. Nie jest to pełna bramka 281 ani dowód wdrożenia. Testy HTTP/GUI i otwarte funkcje wymagają dalszej pracy.

[Zakres, wyniki i ograniczenia](reports/NATIVE_WEB_FIXES_2026-10-31.md).

## Archiwalne checkpointy projektu

# STATE — ChatADHD / Loom

> B, 2026-10-16: gałąź `gpt/implicit-structures-2026-10-14` scala teraz
> main `a51a347b` (P28 WebXR, P29 i P30). P29 zachowuje nieznane odwołania
> jako węzły emergentne i zapisuje `same_as_candidate`; dopasowanie modelem
> pozostaje otwarte. Dev pierwszego scalenia `f628e50b` kompiluje się;
> bramki bieżącego złożenia jeszcze niewykonane. Następny przyrost: wspólne
> adnotacje P23 i nakładki rodzajów krawędzi P24, z dowodami i polityką użytkownika.
> [Raport B](reports/implicit-structures-2026-10-14.md).

## 2026-10-15 — S1+S2 (P19, P23–P28, R32–R34)

Szczegóły i decyzje: `docs/reports/PLAN_2026-10-13.md` (wpisy P19–P28), `docs/reports/INDEX.md`, architektura: `docs/architecture/STRAZNIK_I_SRODOWISKA_2026-10-14.md`, `PRZEGLAD_STRAZNIK_SRODOWISKA_2026-10-15.md`, `WIDOK_SILNIK_WIZUALIZACJI_2026-10-15.md`. Przebieg wątków: `docs/reports/LEDGER_2026-10-14.md`.

**Co działa teraz (słowami właściciela)**

- **Selektor zawsze wybiera z grafu.** Kontekst to wycinek grafu zależny od ustawień rozstrzyganych łańcuchem dziedziczenia (rozmowa → dowolne jednostki → użytkownik → instalacja); także surowa rozmowa jest wyborem z grafu. Wcześniej ustawienie dane tylko rozmowie było pomijane — poprawione.
- **Przepływ wejścia i wyjścia jako dane.** Jak wycinek idzie do modelu (renderer) i co się dzieje z odpowiedzią (węzeł wyjścia) to dane; domyślnie żądanie bajt w bajt jak dawniej. Odpowiedź w formacie grafu można wymusić: widzisz tekst, a jej węzły trafiają do grafu.
- **Osadzanie to odwrotność selektora.** Osadzenie tego, co selektor wybrał, nie zmienia grafu (test bajt w bajt); analizy są osobnym krokiem po osadzeniu.
- **Strażnik-stenograf i wspólna warstwa adnotacji.** Reprezentacja rozmowy w poziomach (bez szumu, bez dygresji, skompilowane polecenie) obok surowych wiadomości; cykl poprawka → hipoteza → zasada; selektor decyduje, czego użyć.
- **Model do pracy w tle nigdy nie jest dziedziczony z rozmowy.** Wybiera go Twoja polityka; „względem modelu rozmowy” tylko gdy sam to wybierzesz.
- **Perspektywy i rodzaje krawędzi są danymi.** Podstawowe w ROM, Twoje w nakładkach; nakładka może dodać, zmienić, ukryć, wyłączyć albo przemianować wpis ROM, a każdy wpis mówi, skąd pochodzi. Perspektywy obok siebie w panelu „Perspektywy”; „skąd to ustawienie” w ustawieniach rozmowy.
- **Silnik widoków.** Widok składa się z deklaracji w grafie (źródła z wagą i głębokością, miary, układy, kodowania, reguły); reguły sensu oceniają kodowania na danych i nigdy nie blokują. Preset: historia przed Tobą, gałęzie w stożku, ekosystem po bokach, przyszłość z tyłu. Strzałki z modyfikatorami, locki przełączają widoki; wszystko do przepisania z rozmowy.
- **Środowiska, agent, źródła narzędzi (S2 R32–R34, S1 P26).** „Co mogę zrobić na telefonie / w tym katalogu” z grafu; agent wywołuje akcje katalogu jako narzędzia, zatrzymuje się na propozycji i czeka na Twoje tak/nie; serwery MCP dodajesz sam, każde użycie pyta, dopóki nie zaufasz.
- **Duże rozmowy.** Wiadomości i graf ładują się stronami („wczytaj starsze”); rozmiary stron to ustawienia.

**Co otwarte**

- Odpowiedź w formacie grafu odrzuca dziś całą odpowiedź, gdy odwołuje się do węzła, którego nie wysłaliśmy — to za ostre; ma być nowym węzłem i kandydatem do dopasowania (P29).
- Szerszy wycinek grafu (szerokość, głębokość, perspektywa) jako dane przepływu; dopasowanie wektorami i modelem.
- Widok 3D i VR/AR (P28 krok 3, w toku); edytor przypisań klawiszy w interfejsie; MDS i czas jako ruch.
- Gałąź GPT `gpt/implicit-structures-2026-10-14`: przegląd i włączenie jako nakładka rodzajów krawędzi.
- Android: brak widoku propozycji agenta i stronicowania.
- Nadal brak próby z prawdziwym modelem i prawdziwym telefonem.

**Co musisz zrobić Ty** — bez zmian: ważny klucz OpenRouter w sekretach sieci środowiska, sekrety podpisu Androida na GitHubie, prawdziwy telefon.

## 2026-10-14 — S1+S2

Szczegóły techniczne i decyzje: `docs/reports/INDEX.md` (sekcje P9–P18 i S2/R*), wymagania właściciela: `docs/architecture/OWNER_REQUIREMENTS_2026-09-26.md` (R54, R55).

**Co działa teraz (słowami właściciela)**

- **Pierwsza wiadomość zawsze dostaje odpowiedź albo widoczny powód.** Nic już nie milknie: dostajesz odpowiedź, pytanie o zgodę, odmowę z powodem albo błąd z treścią, a Twój tekst zostaje w polu, gdy nic nie poszło. Wiadomość wysłana zaraz po „Nowa rozmowa” czeka na rozmowę i idzie dokładnie raz.
- **Pierwsze kroki działają od zera.** Przewodnik „pierwsze kroki” (`docs/PIERWSZE_KROKI.md`) i jego automatyczne sprawdzenie przechodzą na czystym Debianie na GitHubie: instalacja, konto, klucz, pierwsza rozmowa, polecenie z rozmowy.
- **Graf jest centrum.** Rozmowy, wiadomości, wyniki akcji, urządzenia, adaptery i wybory modeli są węzłami jednego grafu; widok wejściowy pokazuje prawdziwe liczniki, a nie atrapy.
- **Jedno „użyj modelu”.** Każde miejsce, które woła model (czat, rozpoznanie polecenia, streszczenie, tłumaczenie, analiza w tle, mowa, obraz), idzie tą samą drogą, z tym samym strażnikiem: wzrost zużycia o rząd wielkości (×10) zawsze pyta Ciebie, a koszt w pieniądzach jest pokazany albo uczciwie „nieznany”, nigdy zero.
- **Strażnik ×10 nie zatrzymuje drugiej wiadomości.** Dopóki aplikacja nie zna Twojego zwykłego zużycia, uznaje za zwykłą rozsądną wielkość wiadomości, więc po „cześć” następna zwykła wiadomość idzie bez pytania. Naprawdę duży skok nadal pyta. Próg jest ustawieniem, nie regułą.
- **Limity wydatków na dzień, tydzień i miesiąc.** Ustawiasz je zdaniem („ustaw limit 20 zł na miesiąc”) i sam wybierasz, co się dzieje po przekroczeniu: aplikacja pyta o zgodę, odmawia (z powodem) albo tylko mówi. Limit dotyczy każdego użycia modelu: zwykłej wiadomości, rozpoznania polecenia, analizy w tle i poleceń z rozmowy; plan pokazany w rozmowie mówi z góry, że przekroczy limit. Bez ustawionego limitu nic się nie zmienia.
- **Akcje na elementach.** O każdej rzeczy (rozmowa, wiadomość, węzeł, nagranie, ustawienie) aplikacja wie, co można z nią zrobić, ile to kosztuje i czy reguły każą Cię zapytać. Własne reguły zapisują się u Ciebie, nigdy w plikach dostarczanych z aplikacją.
- **Okienko rozmowy robi wszystko, co potrafią panele.** To, co robisz z paneli, powiesz też w rozmowie, po polsku albo po angielsku: szukanie rozmów, „co jest powiązane z …”, zapamiętywanie i zapominanie, zakładanie, zmiana nazwy i usuwanie rozmów, poprawianie wiadomości z zachowaniem starej wersji, import i eksport, podłączenie Dysku Google, S3 albo WebDAV, wybór modelu do konkretnej pracy, „ile wydałem w tym tygodniu”, limity, odświeżenie listy modeli, praca w tle, dziennik. Zdanie, które coś zmienia, aplikacja najpierw pokazuje i wykonuje po Twoim „tak”. „Co mogę tu zrobić?” daje listę po ludzku; „jak to zrobić: …” — wyjaśnienie bez wykonywania.
- **Archiwum, wiedza i katalog z rozmowy.** „Przeanalizuj ten folder”, „zbuduj wiedzę z tego folderu”, „przejrzyj ten katalog”: bez ścieżki przeglądarka otwiera wybór folderu, jego pliki idą do programu (nie do modelu), a analiza rusza w tle, lokalnie i za darmo, dopóki sam nie dopuścisz modelu. „Jak idzie analiza?” i „przerwij” też są zdaniami.
- **Katalog rośnie sam.** Gdy powiesz coś, czego aplikacja nie zna, proponuje nową akcję (po „tak” zapisaną u Ciebie), a powtarzane sekwencje proponuje zapamiętać jako jedną. Zdania, które tylko coś stwierdzają („Ocena tej wiadomości jest niska”), nie są już brane za polecenie.
- **Urządzenia robi klient.** Mikrofon, aparat, schowek, wybór pliku i wybór folderu obsługuje strona albo aplikacja na Androidzie; serwer tylko prosi i czeka na odpowiedź.
- **ROM.** Pamięć tylko do odczytu, montowana pierwsza: to, co aplikacja wie o sobie.
- **Porty.** Który model albo dostawca wykonuje którą pracę (streszczenia, tłumaczenia, rozpoznawanie mowy i tekstu) — wybór zapisuje się na właściwym poziomie.
- **Telefon w emulatorze Androida działa.** Na GitHubie aplikacja instaluje się w emulatorze, łączy z serwerem, loguje, odpowiada na wiadomość i wykonuje „streść tę rozmowę” po „tak” (przebieg `android-smoke` 38602445745 zielony). To emulator, nie prawdziwy telefon.
- **Bezpieczeństwo serwera z kontami** przejrzane i poprawione; limity serwera są ustawieniami. Są paczki serwera dla starszych systemów i dla ARM. Klucze i hasła nigdy nie przechodzą przez rozmowę: wpisujesz je w Ustawieniach, a rozmowa mówi, które pole otworzyć.
- **Pokrycie jest sprawdzane.** Lista wszystkiego, co aplikacja potrafi, jest zapisana jako dane razem z tym, jak to powiedzieć w rozmowie; nowa funkcja bez decyzji „da się z rozmowy / nie z rozmowy, bo …” zatrzymuje bramkę.

**Co otwarte**

- Z rozmowy nie da się jeszcze: oceny, wyboru i nadpisań poszczególnych pozycji katalogu, języka zapytań do wiedzy, rejestru metod i onboardingu — to rozbudowana konfiguracja, której mówiona postać nie jest jeszcze zaprojektowana. Logowanie i zmiana hasła są celowo poza rozmową.
- Gdy limit odmawia lub pyta przy analizie w tle, pytanie pojawia się w rozmowie (analiza zbiorcza i ponowienia); zwykła analiza wiadomość po wiadomości po prostu przechodzi wtedy na tryb lokalny, bez pytania.
- Prawdziwa próba z prawdziwym modelem nie była robiona: wszystkie sprawdzenia szły na atrapie serwera modeli.
- Wybór folderu działa w przeglądarce; powłoka Androida go nie ma (wybierasz plik albo wpisujesz ścieżkę). Powłoka Androida buduje się na GitHubie jako paczka APK i przeszła próbę w emulatorze, ale nie była uruchomiona na prawdziwym telefonie.

**Co musisz zrobić Ty**

1. Wstawić ważny klucz OpenRouter do sekretów sieci środowiska — bez niego nie da się sprawdzić niczego z prawdziwym modelem.
2. Dodać na GitHubie cztery sekrety z kluczem podpisu wydania (jak to zrobić: `docs/PIERWSZE_KROKI.md`) — bez nich paczka Androida jest podpisana tylko kluczem testowym i nie zainstaluje się jako aktualizacja własnej wersji.
3. Dać prawdziwy telefon na paczkę APK — emulator nie ma prawdziwego mikrofonu ani aparatu, więc bez telefonu nie wiem, czy mikrofon, aparat i wybór pliku działają w powłoce.

## Historyczny checkpoint 2026-10-15 — B: osobna gałąź rozpoznawania struktur

`gpt/implicit-structures-2026-10-14`, kod `298b429b`, test modelu packa
`f682fc89`, wcześniejsza poprawka transferu `79187377`. Main bez zmian;
integracja należy do Claude’a. Relacyjny wzorzec z danych rozpoznaje opisane,
nienazwane struktury; zapisuje kandydata, wiązania, dowody i alternatywy przez
istniejący pipeline/store. Odtworzenie i wznowienie sprawdzone, zero wywołań modeli.
Perspektywy kodu/idei/specyfikacji **tego samego projektu** pozostają osobną
integracją; konkurencyjne interpretacje nie są ich zamiennikiem.

Historyczny pełny dev/guard v3 PASS: 204/204 wpisów, 203 wykonane,
1354 native / 47921 asercji / 2077 Python, zero Python skips;
1 catalog_scale opt-in niewykonany. ASan/Clang/web pending w tym checkpoincie.
[Raport](reports/implicit-structures-2026-10-14.md) zawiera historię błędów,
dowody i punkt wznowienia. R42 inventory nadal valid=false; nie jest bramką PASS.


## 2026-10-12 — S1: wątki T7, T6, T1–T5 scalone, `main` = gałąź robocza

Źródło szczegółów: `docs/reports/wp-queue-2026-10-10.md` (stan, decyzje, otwarte) i `docs/reports/INDEX.md`.
Ostatnia pełna brama na czubku kodu (worktree T5, GCC Debug dev + serwer): **CTest 169/169, strażnik `verify_ctest.py`
PASS (168 wykonanych + 1 opt-in `catalog_scale`), skrypty `.github` OK, web build OK, 19/19 skryptów web**.
Niewykonane w tej sesji: Clang, ASan pełny (tylko testy krypto pod ASan), TSan, release, urządzenie Android, CI hostowane.

| Wątek | Co działa |
|---|---|
| T7 | Python: `memory.json`/`models.json` nie tracą pól ani rekordów i nie są nadpisywane po nieudanym odczycie; `github_sync` porównuje SHA bloba |
| T6 | Zwykły czat i wsadowy worker przez wspólny `ModelUsageGuard`; ×10 → stan `usage_pending` z potwierdzeniem w web/C ABI/HTTP/CLI; podgląd kosztu |
| P-1 | Czat obok panelu wiedzy na telefonie nie znika (ciało czatu miało 0 px) |
| T1 | Import: nierozpoznane → węzły z adnotacją i lokatorem; duplikaty raportowane; `duplicate_policy` jako preset/warstwa |
| T2 | Worker: `retry_mode` explicit/automatic (trwałe ustawienie), masowe ponowienie, `stalled`, naprawiony zator, historia prób bez kopii profilu, `context_order` legacy/stable_first |
| T3 | `loom.graph_entry/1`: obszary z osi, kalibracja z §3, liczniki z rejestrów albo `{known:false}`, zalążek zerowy z danych |
| T4 | `loom.economy_report/1` tylko do odczytu; TOCTOU w `access.py` zamknięte (fd-walk `O_NOFOLLOW` + tożsamość) |
| T5 | Inwentarz magazynów i kluczy (F1–F10), projekt obszaru „Magazyn i bezpieczeństwo”, preset `pq_hybrid` domyślny dla nowych zapisów (THREADS §6), `legacy` z powodem tam, gdzie brak prymitywu; F3/F4 naprawione |
| T9, T10/T14 | Dokumenty Fable: paradygmat „wszystko w grafie”, integracja ekosystemu + 20 briefów; 89 przypadków granicznych, ścieżki akceptacyjne z telefonu; decyzja D3 |

Najważniejsze otwarte: Android trzyma dane i `secrets.json` w współdzielonym `Documents` bez szyfrowania (F2, decyzja o lokalizacji danych);
tryb LLM per-wiadomość poza ponowieniami bez strażnika ×10; enumeratory interfejsów/metod/perspektyw dla widoku wejściowego;
ML-KEM w przeglądarce (koperta v3 w formularzu); akcje C ABI dla KEM/podpisów; tryb `auto` ekonomii.

## 2026-10-09 — zadanie B na gałęzi produktu

Gałąź `gpt/data-graph-engine-2026-10-09`, sprawdzony runtime **6d1231bd**;
`02714d5b` to późniejszy dokładny locator audytu R42, bez zmiany runtime. Main
niezmieniony, integracja należy do Claude’a. Ograniczone C/D/E zachowują źródła,
autorstwo i zależności w manifeście. P4a native `568c3f7e` / App `ed4bd2fc` udostępnia
jawny lokalny ConversationView przez wspólny Catalog/MethodRegistry/mapper,
copy/link message+parent+version-group parity, source/version/produced_by,
niedostępności i transient/none bez trwałego payloadu w badanym fresh store.
Lokalny odczyt nie uprawnia do wysyłania; stare gettery pozostają storage-only.

Aktualna pełna macierz na `6d1231bd`: **dev 160/160, Clang 160/160, ASan 159/159, guard PASS**.
Wykonano odpowiednio 159/159/158 wpisów; po jednym catalog_scale jest jawnie
opt-in/niewykonany. Dev/Clang: 991 native / 36947 asercji / 2038 Python; ASan: 991/36946/1905,
0 Python skips. D shared contract jest w dev/Clang, FFI companion ASan zwykły dev.
CLI ASan: 242,83 s przy timeout 300 s, RUN_SERIAL i niezmienionych sanitizerach.
Świeży web: 15 komend + build/receipt PASS, JNI 1/1 PASS, oba generatory --check PASS.
Wcześniejsze negatywy pozostają historią, nie były podmieniane na nowy PASS.
Release/TSan/device/live-quality niewykonane. R42 scope: 42 Python PASS; inventory
nadal valid=false, 68558 unclassified / 153 blocked, 0 stale, bez szerokiej allowlisty.

Watchdog WD-003/001/011 mają pełne 624/630/636 PASS. Po A4-WD001 `6be9aed` i
A4-WD003 `f2b6b89` aktualny złożony lint/build/test: **644/644 PASS**,
0 skip/fail; A4-WD002 provenance otwarte. AGEDS request pin: odzyskać źródłowy
SHA/handoff przy rozbieżności publikacji, nie implementować ponownie. Setup
draft revision 8 zapis/readback; publikacja użytkownika i nowa instancja pending.

Następnie P4b.1: checked source observations przez istniejący ContextEngine i
R40, wyłącznie przygotowanie w pamięci. P4b.2 source-context/egress/retention,
D generic discovery/external resource→E, P5 workflow/ExperimentSpec/adoption
i batch/legacy pozostają otwarte. [Raport B](reports/data-graph-engine-2026-10-09.md)
i [manifest](reports/data-graph-engine-2026-10-09-integration.json) podają dokładne
bramki, migracje i granice. Zero płatnych modeli/CI.


## 2026-10-05 wieczór — historyczna integracja Claude’a na main

Wątki GPT skończyły tokeny; właściciel: „przyjmij jego wyniki”. Na `main`
weszła cała gotowa i wstrzymana kolejka po rebase oraz trzy poprawki
integracyjne. Szczegóły, źródła autorów i otwarte prace:
[reports/INDEX.md](reports/INDEX.md).

- Przyjęte (w kolejności): 11 (dane → profile), 6 (mechanizm katalogu),
  5₂, 4₂, 6₂, 7 (wyniki badań etapów 1–5a), 3₂, 12₂, 8 (higiena/CI),
  1 (pełny caller semantyczny z naprawą ponowienia W2), 10₂ (UI metod).
- Bramka na czubku: CTest 146/146, 923 native / 32 739 asercji, 1876 Python, 0 pominięć; web PASS.
- Nie wykonano: ASan (8), niezależna powtórka Chromium E2E (10), sprawdzian
  wątku 7 na nowych danych i na prawdziwych eksportach właściciela.
- Płatnych wywołań w tej integracji: 0. Badania 7: 0,70 USD z 5 USD.

Updated 2026-10-04. **The owner authorized autonomous development and integration
of the application profile cycle described below.** Claude retains the broader
next-cycle handoff outside this owner-requested scope.
The owner additionally requested cheap-model analysis experiments in this
recovery session. [432 paired requests are prepared](research/analysis_optimization_2026-10-02/README.md),
with zero new model calls; restoring this programme's credential is the live
execution blocker. This temporary research task is distinct from a permanent
GPT integration/helper assignment.
**The recovered base is published on `main` in the existing public
`klb-t/chatadhd` repository, together with the verified profile increments.**
The first integration `ab581923` directly continues original main `b5f7eac`
with one parent; N3 is a later increment on that line. The 40 original
main-line commits retain their identities. Read
[the Claude handoff](HANDOFF_2026-10-02_TO_CLAUDE.md).

## Source and ownership

- Accepted source: recovered checkpoint `af3c81a5ddce70bdf4346a2c8c808ce6cc97b4d0`,
  containing night development and the compatible late-W1/test repairs.
- Original main is `b5f7eacdead11101e5c2a08e1864195c74034d45`. Selected
  content directly continues that original public history with one parent,
  retaining all 39 original paths. Verified complete backups also preserve
  every original branch. A separately prepared local metadata-corrected
  bundle is an optional derivative, not this hosted main's ancestry.
- [Archive and exact selection](archive/README.md) retain full source,
  first failures, unused experiments and original W3/W5 Git bundles.
  Completed hosted branches were renamed under `archive/`: 33 renames,
  35 archived branches in total, all original tips preserved. Four active or
  supporting branches and two isolated evaluation branches remain separate.
  No remote branch has been deleted or force-pushed by this recovery.
- The owner-requested profile cycle is complete. No GPT helper has an ongoing
  assignment from this recovery; Claude chooses any broader later helper's
  scope, branch, deadline and integration boundary.
- The owner explicitly keeps the existing repository public. Historical
  author/committer metadata still contains the contact finding in 26 commits;
  this integration does not rewrite or hide it. The selected source contains
  no confirmed private-content finding in the scoped audit. See
  [the privacy audit](PUBLICATION_PRIVACY_2026-10-02.md).
  No private conversation/export or credential is a publication input.
  No new paid provider calls were made.

## What is integrated

### Owner-requested application profile increment (2026-10-04)

The first increment directly continued public main
`421415f8b9a5c29e25fc4c5fda5b87ba8f5fb9cd` and was integrated as
`9d15d2dd0733274356f768816e207e26e13845e4` through
[PR8](https://github.com/klb-t/chatadhd/pull/8). The autonomous extension follows
that commit through [PR9](https://github.com/klb-t/chatadhd/pull/9).
Its measured implementation commit is
`95e5049382a6025ca0c591e9b6dba49f90af3946`; subsequent documentation commits
retain that implementation. Main advances without rewriting prior history.

[Application profile contract and handoff](APPLICATION_PROFILES.md) describe
versioned JSON profiles, simultaneous views, actual Loom adapters and declarative
workflow inputs/result bindings. Revision-1 examples remain intact; revision-2
workflows retain created conversation IDs. Separate source-backed profiles pin
LibreChat 0.8.8 and NextChat 2.16.1. These are partial mappings; original service
and arbitrary historical-version fidelity are not established.

Imported source blocks are inspectable separately from current edited text,
including excluded rows and saved versions. Exact valid UTF-8 profile source,
including BOM/whitespace, can be explicitly accepted through the existing native
GraphPacket store, read back with receipt/drift checks and restored after client
storage loss. Existing core/KB schema and C ABI are unchanged. Native object
comparison now ignores key order but preserves arrays and exact numeric values;
unauthorized POST responses close the connection to prevent reused-body failures.

Changing a view preserves explicit model/context controls. Saving a profile
creates a completed canonical knowledge run, so automatic latest-run queries
may select it; that write effect is disclosed in the UI and contract. Android
JNI storage, specialized media/artifact views, complete descendant navigation,
individually queryable capability links and durable server workflow recovery
remain explicit gaps. Local view/workflow persistence is not cross-device sync.

Fresh measured checks are recorded in
[the extended verification](verification/application-profiles-extended-2026-10-04/RESULTS.md):
108 CTest entries covered across 106 + 2 stages, native-store FFI 18/18,
Python contracts 209/209, runtime 19/19, adapters 9/9, native graph 11/11,
profile browser 15/15, native imported-source display 4/4 and existing browser
16/16. The earlier prototype and historical native measurements remain separate.
No paid provider calls or GitHub Actions runs were used.

### Previously integrated on public main

| Area | Current behavior and boundary |
|---|---|
| W1 / N1 — task acceptance | Explicit ActiveTaskSpec reaches real chat. Acceptance and its user row share the existing EventLog transaction. Exact locator/time ordering, durable source checks, exceptional/C ABI cancellation cleanup, large revision/timestamp tests, history ABA and scope identity fixes are included. The incompatible second W1 journal is archived. This is not automatic task inference from prose. |
| W2 / N2 — retrieval | Per-thesis scope/detail, claims/counters and graph/TF-IDF channels reach actual chat. Exact built-in TF-IDF corpus vectors are reused across plan queries. Plan provenance is caller-declared; verified W1→W2 plan authority is still missing. |
| W3 — recipes | `directed_refute_v3`, 48 prepared DEV requests and historical-response replay are retained. No fresh live model-quality measurement is claimed. |
| W4 / N3 — graph workflow | Python AnalysisPlan, GraphPacket transforms and durable coordination work. Explicitly selected entities/claims/observations now reach native KnowledgeStore atomically, with immutable full-packet receipts, owner judgement replay, CAS and checked readback/replay. Native CandidateGraph remains distinct; full reversible-history validation remains in Python. See the [store contract](NATIVE_GRAPH_PACKET_STORE.md). |
| W5 — workspace | Native views/references/couplings and browser-local perspective restoration work. Plan execution remains an explicit user choice. This is not cross-device perspective synchronization. |
| W6 / N4 — import | Source-array and traversal order stay distinct; parent bindings, actual-root pointers, wrapper fields and member indices are retained. ZIP raw member materialization and the large-whitespace sniff fix are included. Raw source remains preserved. |
| N5 / N6 | Historical resource peaks survive reservation release/crash boundaries. Latest completed native knowledge-run lookup no longer loses an older completion behind many unfinished runs. |

Existing chat context, provenance, semantic metadata preservation, confidence,
workspace contracts, ABI and import behavior remain subject to regression gates.
Python compatibility is no longer a product requirement, but existing sentinels
remain until a deliberate migration. N3 adds `loom_graph_packet_store` to the
C ABI and migrates KB schema 2→3 with a receipt table; core schema remains v4.
Existing KB records are preserved by the tested migration. The optional W3 source-record
fields `resolved_commit` and `git_tree_sha` also permit exact historical byte
verification in the separately prepared metadata-corrected bundle. The public
repository retains the original commit identities.

## Fresh verification

The latest profile-cycle measurements and exact source/binary hashes are in
[verification/application-profiles-extended-2026-10-04/RESULTS.md](verification/application-profiles-extended-2026-10-04/RESULTS.md).
The preceding recovery measurements remain pinned to their source in
[verification/current-2026-10-03/RESULTS.md](verification/current-2026-10-03/RESULTS.md):
108/108 CTest entries and 13/13 actual native-store FFI regressions. The initial
15 environment failures and their passing rerun are retained. Previous web
verification remains attached to its unchanged web source. Historical 77/77, 94/94 and 851-test
claims are not transferred to the recovered source.

## Research and money

[Research/cost audit](RESEARCH_AND_COSTS_2026-10-01.md) separates retrieval,
extraction, commitment policy, replay and live inference. BM25 52/60 evidence
ranking and Jev 45/48 supplied-candidate selection are different DEV tasks;
neither establishes free graph extraction quality or a general model ranking.
Free graph extraction retains its negative first results in the archive.

992 saved ledger rows deduplicate to 631 attempts. All 629 available response
hashes and 628 usage-cost values reconcile; three costs remain unknown.
The campaign arithmetic is **$0.133931438** including the opening balance.
A later saved key reading is **$1.23206716**, with **$1.098135722 unassigned**;
therefore $0.134 is not certified total account spend. These are historical,
not current balances. The authorized cheap/Jev programme has a non-resetting
**$2** limit; a new conversation does not reset it. No new frontier pilot is
authorized. Reconcile live identity, usage and outstanding reservations before
resuming paid work.
The recovered credential envelope expired on 2026-10-01. The required private
decryption/session artifacts are unavailable; its expiration is not bypassed.
The 432 requests remain unexecuted pending a valid authorized credential and
fresh account reconciliation.

## Open engineering and evidence boundaries

The complete [limits and wiring inventory](LIMITS_AND_WIRING_2026-10-01.md)
is a source audit, not a claim that all limits have been removed. Its line
references pin `33fb30a`; the recovered delta and final tests are described
in the handoff. Priorities for Claude to scope are:

1. Convert hard product/resource ceilings into explicit caller policy/presets;
   implement expected ×10 usage confirmation end to end. Preserve necessary
   representation and transaction invariants, and preserve unknown source data.
2. Connect model goal typing and a real vector provider to production callers;
   provide candidate-channel controls and capability reporting in the UI.
3. Connect checked task→retrieval provenance and scope any broader native
   GraphPacket history/transformation support. N3 acceptance/read/replay is
   delivered; it does not replace Python's complete history validator.
4. W1 callback exceptions after output still do not retain a partial assistant
   row; many distinct lineage sources can produce quadratic provenance payload.
5. Recipe efficacy, real full-export/Anthropic semantic quality and independent
   evaluation remain unproved. Do not use the sealed holdout as DEV.

`gpt/ecosystem-agent-research-2026-10-01` / draft PR7 is an isolated Python
prototype, not a production service or a launched cloud VM. The archived `wip/precision`
is an unverified historical experiment. Neither is silently promoted.
The original `semantic_sketch` and shared-lease `c26c525` bytes remain missing;
later replacements do not recover those originals. Full transcripts of all
36 historical child agents are unavailable. See the bounded
[recovery inventory](CONVERSATION_RECOVERY_2026-10-01.md).
The additional bounded search of 68 recent saved files on 2026-10-03 found no
missing originals or complete child-agent transcripts.

## Integrator9 — równoległy cykl1–11 (2026-10-04)

Baza źródła: `161cc22dfb84fe863389d6b90323bd44516a68dc`, z całym INTERFEJS/PR9.
Ten checkpoint dodaje wyłącznie stan i raporty integratora; **0 gałęzi kodu
przyjętych**. Wstrzymane implementacje nie są przodkami tego przyrostu.
Dotychczasowe wpisy i pliki profili pozostają zachowane.

Aktualny [indeks11 wątków](reports/INDEX.md) przypina sprawdzone commity,
raporty, konkretne błędy oraz przekazania „Do wątku N”. Inwentarz11:
695 grup w559 plikach,39496 kandydatów mechanicznych; to zakres migracji,
nie liczba wdrożonych zmian. Ponownie przydzielono1 prompty/metody w grafie,
a2 presety config/startup. Odbiór zadań przez autorów nie jest potwierdzony.

Najnowsze polecenie właściciela obowiązuje w tym cyklu: metody, wersje,
przepisy/prompty z hashem, parametry, presety i kombinacje są bytami w grafie;
wyniki mają krawędź do konkretnej wersji metody, a oceny są datowanymi
twierdzeniami z dowodami. **Wspólny kontrakt3/4 i regresja między ich API są
warunkiem odbioru przed scaleniem**. Domyślne metody/polityka pochodzą z danych
packa/profilu, z nakładką użytkownika; nie z ręcznych presetów C++.

W2 ma zielone niezależne bramki poprzedniego przyrostu (19/19 kontraktów,
108/108 CTest,659 native/1276 Python, web85 modułów), ale nadal nie spełnia
wymogu presetu jako danych. W4 iW5 po ostatnim fetch dostarczyły
poprawki wykazanych błędów replay/checkpointu; przegląd kodu je potwierdza,
ale niezależny rerun nowych regresji oraz bramki po rebase pozostają otwarte.
W4 opublikował packet-side format metod, którego W3 jeszcze nie produkuje.
W7 naprawił wykazany orphan-response błąd, lecz nie ukończył całego zakresu.
W6 sam wstrzymał checkpoint po nieprzejściu jakości; negatyw jest w archive.
W8 dev/ASan receipts są kompletne, vendored Clang wymaga poprawki w10.

Osobny build/web/fullCTest czystej linii161cc22 z tymi dokumentami:
**zielony:108/108 CTest,659 native/24465 assertions,1276 Python/0 skips;
web85 modułów**. [Oryginalne dowody](reports/integrator-main-verification-2026-10-04/README.md)
obejmują pełny przebieg351.36s oraz1276 rzeczywiście wykonanych przypadków Python.
[Raport9](reports/integrator-2026-10-04.md) rozdziela wyniki różnych źródeł.
Kolejność kodu pozostaje2→3/4/5→reszta. README czeka na odbiór zmian;
szkic8 nie jest dowodem ukończenia aplikacji.

Płatne badania tylko w7 w osobnym budżecie5€ i na kluczu z limitem5€;
integrator wykonał0 płatnych wywołań. Nie czytał ślepego korpusu ani
`eval/real-holdout-key`; starsze zapisy badań powyżej pozostają historią.

## Integrator9 — wznowienie i R39–R41 (2026-10-04, druga sesja)

Powyższy stan7282437 jest historyczny. Dokumentacyjny przyrost Claude62cfd3a
(R39–R41,48 linii) przyjęto po czystym rebase jako ba6eaf6, bez cofania INTERFEJS.
Pełny CTest108/108315.71s, guard659 native/24465 assertions/1276 Python0skip
i web85 zielone; [dowód](reports/integrator-requirements-verification-2026-10-04/README.md).

**Przyjęto pierwszy przyrost2: usage policy z autorytatywnych danych**. Autorytatywny usage_policy.pack i checked decoder zastępują
wszystkie6 ręcznych wartości C++; zgodne5 default paths i config overlay/restart.
Niezależny actual129-object build,25/25 kontraktów,5/5 generatora,4/4 source-edit
i bad-preset variants; 108/108265.33s;107 executed,659 native/24465 assertions,1276 Python0skip; web85.
[Dowód odbioru](reports/integrator-usage-acceptance-2026-10-04/README.md).
Stare próby i pierwszy nowy107/108 (lokalny CLI0644, hash bez zmian po0755)
zostały w archive; pełny nowy przebieg nie jest sumą pojedynczych retry.
Startup/config0/5, shared public export i dalsze profily nie są tym odbiorem.

3 ma grafowy rejestr/wykonanie/eksporter golden;4 ma jeden kontrakt i konsumenta.
Golden i natywny consumer PASS są w32e2381; raw producer1/97 i pełny121/1604 też zapisane; acknowledgment4
i mixed gates nadal wymagane. Autorzy mogą wykonać je offline
na obecnym2, bez czekania na wzajemny main. Pierwotny P1 replay4 niezależnie
przeszedł: drugie wykonanie false, ledger calls=1/unresolved;
[dowód](reports/integrator-packet-fix-2026-10-04/README.md).2/3/4 wypchnęły nowe refs w tej sesji;
nie stwierdzono przerwy≥2h; INDEX rozdziela committer time od obserwacji fetch.

5 checkpoint naprawiony i niezależnie sprawdzony (2 scenariusze/29 kontroli),
ale nowy actual mock replay wykazał format OCR utracony przy immutable blob:
MIME1/3, dwa błędne import paths. Negatyw wyłącznie w archive;5 nadalwstrzymany
do poprawki i mixed gates. Raport/runbook5 już istnieją.11 opublikował loader,
CLI profile i naprawił materialize; dawne blockers zastąpiono aktualnymi zadaniami.
8/10 Clang nadal niezielony.6 dalej tylko DEV, blind nie używany ponownie.

[INDEX](reports/INDEX.md) i [raport9](reports/integrator-2026-10-04.md) zawierają
aktualne przekazania R39–R41 i właściwe commity. Kolejność kodu2→3/4/5→reszta.
README czeka na odbiór funkcji; kod innych wątków nie jest dopisywany przez9.
Nowe płatne wywołania0;7 dokańcza offline i czeka wyłącznie z płatnym wykonaniem
na osobny klucz5€ przekazany przez właściciela.

## Integrator9 — przyrost3+4 (2026-10-05)

Powyższe wpisy pozostają historią. Przyjęto przyrost3+4, testowane źródło `a1be689dc026367783473133de3c25cf5ae60971` po rebase.
Pełny CTest110/110 (149.03s), guard663 native/24489asercji, 1303Python/0skip; webPASS.
Wspólny kontrakt METHOD_GRAPH i kanoniczny golden uzgodnione. Integrator: registry/wykonanie/ślad121/1604, canonical+freshnativeconsumerPASS, alias/nested/nested+alias3/3PASS, KBwindowPASS. 3 czystyrebase na30ad7d3; bez merytorycznych zmian. Historyczne negatywy4 pozostają na archive; domyślneprofile/legacywiring to dalszyprzyrost.
[Dowód9](reports/integrator-intake-2026-10-05/README.md); [bieżący INDEX](reports/INDEX.md). Cały INTERFEJS i wcześniejsze wpisy zachowane;0paidcalls.

## Integrator9 — przyrost 5 (2026-10-05)

Przyjęto po rebase; testowane źródło `0a81480bd1a70b394bac72e98f6b5e7cb49f5d0b`. Pełny CTest 117/117 (225.60s), 710 native / 26399 asercji, 1322 Python / 0 skips; build web PASS.

31wybranych źródeł bez zmian merytorycznych po rebase; OCR3/3 MIME i22/22kontrole, checkpoint2/2scenariusze i31/31kontrole na rzeczywistym kernelu. Forward-only adnotacje i audit przyjęte; runbook autora przypięty w INDEX. Pierwsze timeouty zachowane w archive, progi bez zmian.

[Dowody](reports/integrator-intake-2026-10-05/README.md) i [INDEX](reports/INDEX.md). Zachowano cały INTERFEJS i wcześniejsze wpisy; zero płatnych wywołań.

## Integrator9 — przyrost 1 (2026-10-05)

Przyjęto po rebase; testowane źródło `a95a3e1d9117af4b5ebfa3bc2a042c74efbd08f8`. Pełny CTest 117/117 (196.85s), 710 native / 26399 asercji, 1322 Python / 0 skips; build web PASS.

CZĘŚCIOWO:20źródeł identycznych z50e6bb9:precyzja leksykalna i privatePromptRegistry.43/43registry;11sekcji metryk synthetic równe i24/24integrity. Zmienione inputhash/runID wynikają dokładnie z wersji packa; nie deklarujemy całej karty byte-parity. semantic.cpp/semantic_usage.h wykluczone na3FAIL/8asercjach retry; pełny negatyw archive. Backend wiring/graph-method projection pozostają otwarte. Selfhost autora:18005→12863claims i37249→37249observations; brak nowego selfhost9.

[Dowody](reports/integrator-intake-2026-10-05/README.md) i [INDEX](reports/INDEX.md). Zachowano cały INTERFEJS i wcześniejsze wpisy; zero płatnych wywołań.



## Integrator9 — przyrost 12 (2026-10-05)

Przyjęto po rebase; testowane źródło `1e63bcc9350d126611b4931ddd21b3b7a01b2230`. Pełny CTest 121/121 (229.50s), 761 native / 29103 asercji, 1322 Python / 0 skips; build web PASS.

28źródeł autora3c0bc36 byte-identical po realnym rebase. Native onboarding4suites=51/2704 z pełnego JUnit; controller17/17, generatorcheckPASS. ActualW3registry bridge load/resolve/tamper/multiversionPASS;0providers/0method-execution, bez deklaracji podpiętego czatu. Profile/scenario/layers/store+UIcomponents przyjęte; HTTP/nawigacja/retencja dalsze10/3/11. Helperreporter Node22 jawnieTAP;pierwszy parsernegatyw w archive.

[Dowody](reports/integrator-intake-2026-10-05/README.md) i [INDEX](reports/INDEX.md). Zachowano cały INTERFEJS i wcześniejsze wpisy; zero płatnych wywołań.



## Integrator9 — przyrost 10 (2026-10-05)

Przyjęto po rebase; testowane źródło `e8fbb6d356247efe2943ff91cfea5a567f7eb3d3`. Pełny CTest 121/121 (214.03s), 761 native / 29103 asercji, 1322 Python / 0 skips; build web PASS.

Pierwszy przyrost67źródeł identyczny z rebasedprefixda50562 autora fa7538d; zachowano packetroute4 i28źródeł12. Wymagane fullCTest/webPASS. Autor pierwszego84fixture/20nativeUI/16E2EPASS; niezależny9 replay27/84 iBLOCKED przed geometry:Chromium socket() OperationNotPermitted.0/20nativeUI i0/16E2E w tej próbie9; nie deklarujemy ich powtórki. Pełny negatyw archive/integrator-runtime-negatives5b081f6, bez obchodzenia ochron/zmiany testów. Drugi10 nadalwstrzymany120/121 nafullcaller1.

[Dowody](reports/integrator-intake-2026-10-05/README.md) i [INDEX](reports/INDEX.md). Zachowano cały INTERFEJS i wcześniejsze wpisy; zero płatnych wywołań.



## Integrator9 — Claude R42 i budżet (2026-10-05)

Przyjęto dokumenty autora3cd5848 po rzeczywistym rebase,2/2pliki byte-identical. R42: sześć klas danych/polityki do profili; AGENTS wskazuje osobny budżet5€ tylko7.0paidcalls9. Właściciel zlecił rebase bez pełnego build, kod nie zmieniony. [Dowód](reports/integrator-intake-2026-10-05/claude-r42-proof.json).

## Integrator9 — bieżąca kolejka sesji 2026-10-05

## Stan kolejki integratora — bieżący checkpoint

**Przyjęte:** 2:first, 3+4:first, 5:first, 1:partial, 12:first, 10:first, Claude:docs. Każdy kodowy przyrost ma własny pełny CTest, guard rzeczywiście wykonanych przypadków i build web; dokumenty Claude są wyjątkiem zleconym przez właściciela.

**Czeka w kolejce, z commitem gotowym do odbioru** (ready autora nie zastępuje bramek9):

| Przyrost | Przypięty commit | Pozostały odbiór |
|---|---|---|
|11:first|`22873e047abe6b56808dd2fbc4fe41188ee0e7a8`|canonical usage projection; rebase/full clean build/gates9|
|6:first|`d9b29c502bafd3b4cb17f187e3e74f32cfff72ee`|wyłącznie mechanizm; neutralne31/45,0FP; DEV, bez blind|
|5:second|`f494931dbf24c7debac588361d1d0098275bd47a`|source156a820;24paths; własne mixed gates9|
|4:second|`097859af292bf2b28915e4684f9c085efabd66e2`|autor117/117/parity1536; własny rebase/gates9|
|6:second|`dab76dfc5c2b9ac0cb71c434146fd71bd33bab65`|dane alias/index; własny rebase/gates9|

**Wstrzymane / niegotowe:** pełny1 (3 przypadki/8 asercji retry/cache), drugi10 (120/121, ten sam błąd);8 `57ad9bb20b5e21b4445d03abb7bcd2e616f63f2d` (pełna macierz nieukończona);3second `6b54926950752c6890eb69ec85bf1c4643ec346a` i12second `374bab7` (WIP, bez gotowego własnego fullgate).1second nie zawiera nowego gotowego przyrostu. Nie cofamy przyjętego INTERFEJS.0 płatnych wywołań9.


Pełny1 i drugi10 wstrzymane na błędzie retry; szczegóły i Do wątku N w [INDEX](reports/INDEX.md). Historyczne wpisy zachowane.
