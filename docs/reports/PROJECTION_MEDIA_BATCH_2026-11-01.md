# Projekcje, legacy XLS, video i bundle — odbiór zakresowy 2026-11-01

Baza: `eae7ece48791a7170c6e595b23eaf868e00945a9`.
Kod: `e27ce9ff0076c287963a588d942d68e7bebaca3f`.
Runtime tree: `688466985dd9440563b0ec51eb30457d9312117a`.
Jeden integrator, lokalny przyrost; bez wdrożenia. Kanon §0 rewizja 2 z 2026-10-31.
Stare pakiety i dowody pozostają; wcześniejszych P0/GUI/autor-suite nie powtarzano.

## Zmiana

Istniejąca akcja `client.blob_text` obsługuje `projection_check` oraz jawne
`projection_reproject`. Sprawdza zapisane source/tool/profile/receipt/blob bindings,
reparsuje zachowany oryginał bieżącym przypiętym readerem i tworzy osobną wersję.
Stare Units, receipt i przypisania nie są przepisywane. Wynik daje propozycje
mapowania dokładnego tekstu/współrzędnych/rodzaju; różnice wymagają decyzji.
Nie zmienia globalnego wyboru konta. Aktywacja nowych ID jest osobnym jawnym
wywołaniem istniejącego selektora. Puste projekcje mają `not_exercised` zamiast
pozornego PASS replay. Bieżące domyślne reguły nie wykonują starych custom tools.
ZIP tree migration i `previous_source_id` są jawnie unsupported, bez flatten/replacement.

Legacy XLS używa istniejącego host transportu, prawdziwego `/usr/bin/xls2csv`,
przypiętych pakietowych charsetów, prywatnego `.catdocrc`, czystego środowiska,
stałego argv, limitów i filesystem Landlock. Native Source/Unit/Observation/genesis
reprezentują wyłącznie **PARTIAL output matrix**. Nazwy arkuszy, formuły i oryginalne
adresy BIFF nie są udowodnione; nie są podstawiane wymyślone współrzędne.

Video używa odebranego helpera/SourcePlan przez ten sam bounded host. Oryginał,
receipt i PNG są osobnymi zarządzanymi Sources. Zachowane są ordinal i racjonalny
PTS/timebase; tekst obserwacji dotyczy metadanych/hasha, nie znaczenia obrazu.
`PreparedImage.has_ocr=false`; brak routingu przez screenshot/OCR. Native zapis
korzysta z rzeczywistych hashy BlobStore, transakcji SQL, Provenance i genesis;
reuse/read reparsują host receipt oraz sprawdzają zapisane bindings.
Cache nie jest mechanizmem naprawy ani dowodem integralności całego podgrafu.

Bundle eksportuje rzeczywisty JSON rozmowy i zarządzane oryginały. Istniejący
ZIP importer interpretuje dokładnie `conversation.json`, pozostałe pliki zachowuje
jako opaque, `modeOff`, z provenance. Źródłowy JSON pointer jest zapisany przed
normalizacją i wiązany z rzeczywistym ID docelowym — bez założenia pozycji ID.
Sprawdzane są tekst/rola/parent, sloty i odczyt hasha oryginału. Nieobsługiwana lub
niejednoznaczna normalizacja odmawia. Brak załącznika domyślnie odmawia; jawny
`allow_incomplete` daje `INCOMPLETE`. To nie pełny restore bazy, statusów, grup wersji
ani całej zaszyfrowanej konfiguracji; źródłowe metadata pozostają w archiwum.
Pin archiwum daje integralność względem podanego hasha, nie niezależną autentyczność.

Proces hosta ma prywatny workspace i grupę procesów sprzątaną przez watchdog.
Catdoc/codec mają filesystem confinement; nie deklarujemy kernelowego odcięcia
sieci ani przypięcia wszystkich bibliotek współdzielonych i OS. SQL rollback nie
usuwa osieroconych bajtów content-addressed blobs. 126 eksportów C pozostało bez
zmian; rozszerzenie C++ `ImportOptions` wymaga przebudowanych konsumentów C++.
Kontrakt/polecenia: `loom/tests/projection_media/README.md`.

## Wykonane dowody

Wszystkie dane nowe, syntetyczne; modele wyłącznie lokalna atrapa HTTP.
Native/API korzystały z własnego staged Release, przy niedostępnym checkoutcie
helperów. Nie korzystano z cudzych binariów ani instalacji produkcji.

| Zakres | Rzeczywisty wynik i granica |
|---|---|
| PDF upgrade | PASS: stare pinned replay odmawia; integralność starej migawki, odmowa stale confirmation/foreign selection/receipt override/tamper, osobna wersja i zachowany stary receipt, jawny wybór, odczyt nowej wersji |
| XLS + video | PASS: XLS `A/2` z faktycznego catdoc; cztery native frame occurrences, źródła/hash/read, idempotencja, odmowy unchecked receipt i podmienionych PNG; SQL failure trigger bez nowych rekordów |
| Bundle | PASS: dwie wiadomości i 12 slotów oryginału JSON; JSON załącznika nie staje się kolejną rozmową; pin refusal, rzeczywisty SQL attachment failure, source pointer→ID, readback/reuse |
| Wspólny reopen | PASS: nowy PDF/XLS/video, bundle attachments, integrity/FK; małe DOCX/CSV sentinele z powodu zmiany subprocess transportu |
| HTTP | **1/1 PASS**: upload XLS, globalny wybór komórki, jej rzeczywisty ID/tekst w systemowym kontekście atrapy i brak ID drugiej komórki; video/read, bundle, forged refusal, restart i oryginał |
| Końcowe granice | PASS: pusta projekcja `not_exercised`; brak assetu wymaga explicit partial i pozostaje `INCOMPLETE`; podmieniony parent bundle / cache receipt video odmawiają bez cichej naprawy |
| Pakiet | verify-package start/health/auth/static PASS (istniejący BusyBox tar); 108 payloadów zgodnych; relokowany XLS default helper/read PASS, checkout helpers i pierwotny stage niedostępne |

Pięć grup native wykonano etapami. Biblioteka `b7329f37…` pokryła PDF i XLS/video;
`c41b3e36…` poprawkę bundle, wspólny reopen i HTTP. Później sprawdzono wyłącznie
dotknięte granice pustej projekcji oraz parent/cache; końcowa biblioteka ma hash
`7004d54b…`. **Nie przypisujemy wszystkich wcześniejszych wywołań końcowemu SHA jako
ponowionych testów.** Pełne hashe/fazy są w `runtime-binding.json` i logach.

Zachowane FAIL: compile errors nowych TU; harness próbował zapisać własny blob 0444
(naprawione tylko syntetyczne tamper setup); catdoc config z cudzysłowami odmawiał
mapy (poprawiony przypięty generator); bundle reuse zwiększał liczbę zapisów
(naprawione cache z pełnym readback); newline nie był pustym dokumentem
(zastąpiony faktycznie pustymi bajtami). Asercji nie osłabiano. Po awarii powtarzano
wyłącznie dotknięty zakres, a nie zakończoną migrację/readery/P0.

## Odwracalność

Stary własny pakiet `36e945fd` utworzył minimalny syntetyczny PDF/wybór, został
zamknięty i skopiowany istniejącym `cold_snapshot.py`. Kandydat pracował wyłącznie
na odtworzonej kopii. Następnie **na osobnej kopii stanu kandydata** poprzednik:

- odmówił nowego wybranego occurrence (brak zgodności in-place, nie PASS kontekstu);
- odczytał zachowaną starą projekcję, bez przepisania nowych ID na starą metodę.

Odtworzenie kopii sprzed zmiany do kolejnego nowego rootu i uruchomienie poprzednika
przywróciło stary wybór i oryginał; nowe occurrence było nieobecne.
**RPO: wszystkie zmiany po tej kopii są utracone przy restore.** Test nie jest
produkcyjnym backupem, zgodą na deploy, dowodem wszystkich wersji parserów ani
obsługą nieznanych kluczy/operator ACL. Oryginalna zamknięta fixture pozostaje bez
zmian. Produkcyjna kopia/restore i okno operacyjne należą do administratora;
obowiązuje istniejący `ADMIN_BACKUP_ROLLBACK_PLAN.md`/`install.sh`, bez nowego deployera.

## Artefakt i pochodzenie

`chatadhd-server-linux-x64-e27ce9ff-release-projection-media-20261101.tar.gz`

- SHA256: `0caab73321bc7d9a565d1ec6f69e48be9108af6c4e885b2f36dfda2b4f62c63e`.
- Manifest: `ddd3c4a40b90c09082d622fdae472117d9d8387394792076f611cb6b7880a345`.
- Server: `cc0f65f19cfaf609706064ae068f09073016aba257ce80509d1ae5431eb8ac90`.
- Lib: `7004d54bd106e281acddd5d352e8ab0e2ced0d710245f8a17c003ccb336e610a`.
- Webdist niezmieniony, hash-po-hash z przyjętego `ed139231`; bez nowego browser claim.
- Stary pakiet `ee2117e8…47e8` zachowany i ponownie sprawdzony hashem.

Intake: donor HEAD/status/manifest/hashes read-only przed/po, product raz, overlays
wspólnego runtime nie nadpisywały przyjętych readerów. Baza donorów
`e2f6873c7ca24b41957b36b0127439b926068f41`.

| Task | Przyjęta delta |
|---|---|
| fleet-video-helper-20261101 | `30978008148ec953515c28dc94447db4c5dbb6973425ae37c9e58b9ba42d381d`, 7 plików; własny host confinement |
| fleet-video-source-plan-20261101 | `1b016107394338578c04aaca81b56a4be0c59cb4e24657934b9aebdf1c39648a`, 6 nowych plików, bez ponowienia prerequisite |
| fleet-export-assets-bundle-20261101 | product `86fda0c217e8430827d7dbba4e29f0d6b91e0b002bae4ffcbb00f391d9c3c5a8` (4 obejrzane pliki); hooks `24c44bb08ff3522fb5172799edf168b62b7059bd85080b7500fa758a7ef55677`, świadomie rozszerzone |
| fleet-selection-extent-20261101 | `a944f72dc44cfd1e1283afa84aed38e4df8d09bb4eff2d223ee40d9a3bf8cf16` **nie nałożono**: ten adapter zwraca frame/read, nie ma jeszcze spatial selection consumer; samo zapisanie bounds nie byłoby odbiorem |

Dowody lokalne: `.integration-projection-media-batch-20261101/` — `intake.json`,
`applied.json`, `runtime-binding.json`, `release-package.json`, `manifest.json`,
`final-result.json`, logi komend/buildów/HTTP/native/cold snapshots. Prywatne rooty,
CP, receipt administratora i logi nie są częścią commita ani pakietu.

## Pozostały zakres

- Implemented/tested zakres backend powyżej. **NOT RUN:** nowe GUI migration/XLS/video/bundle,
  mobile, pełny repo gate/monolit/CLI/CTest suites, realny provider/model, deploy.
- Real vision **NOT RUN**: brak zweryfikowanej już autoryzowanej, skonfigurowanej
  ścieżki produktu i pokrycia kosztów; OCR nie stanowi vision PASS.
- Selection extent: następny konkretny krok to konsument wyboru zakresu klatki
  z trusted Source/frame, bounds/representation/method, osobno od hops/detail;
  dopiero wtedy dołączyć zachowany validator. Bez claimu voxel/3D.
- Video: brak selektora klatki do rzeczywistego payloadu obrazu/UI oraz migracji
  zapisanych projekcji video między toolchainami. Bundle: nieobsługiwane typy
  wiadomości/wersjonowanie wymagają jawnego rozszerzenia istniejącego importera.
- ZIP tree reprojection pozostaje osobnym jawnym zakresem; nie ma cichej zamiany.

Przed wspólnym buildem wolne 1 268 969 472 B, brak innego heavy procesu.
Jedna kompilacja `core/server -j2`, później tylko dotknięte TU po rzeczywistych
błędach/review; żadnej pełnej kopii cache. Intake/implementacja zaczęły się około
08:05 UTC, pierwszy build 08:19; dalsze kompilacje/testy są rozdzielone logami.
Główna praca to native persistence/pinning/migration, nie research formatu;
180/600 s nie były timerem ani SLA. Nie usuwano dawnych artefaktów ani nie
zmieniano VM. Po tym przyroście nie uruchomiono następnego cyklu.
