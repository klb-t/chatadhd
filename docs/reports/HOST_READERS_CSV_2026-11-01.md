# Zaufane host readery CSV i Python AST — odbiór 2026-11-01

Przyjęto zakresowy backend obu formatów oraz nowy pakiet Release. Host sam
uruchamia przypięty lokalny parser na oryginalnym blobie; client JSON nie jest
dowodem struktury. Dawny bloker AST z 4a82c0cb został rozwiązany kodem tego
przyrostu. Nie wykonano wdrożenia ani akceptacji nowego GUI.

## Pochodzenie i zakres

- Baza integratora: `4a82c0cbf543c14412b01e7491938384551b4c7a`.
- Przyjęty/testowany kod: `89cb74f7b144e6f21e39c93fac2882637d9c9063`.
- CSV: task `fleet-csv-structure-20261101`, donor
  `e2f6873c7ca24b41957b36b0127439b926068f41`, snapshot `1793513404816813087`.
  Audytowany product patch:
  `4278fbdd76438db3dc48cba1e411157e4fdba006caf85c0117bd9a7537450c1f`.
  Attestation archiwum:
  `f29bb53b82d833fe1d4b75fc180dc9fadbd0ab3a8e982fdb6528bbdbe376efa9`.
  Archiwum chronione nie było otwierane. Pełny review i zgodność 11/11 plików
  przed/po; HEAD/status/diff stabilne, 7 plików produktu nałożono RAZ po
  apply-check. Zachowano BOM/CRLF fixture. README uzupełniono własnym wynikiem.
- Przyjęty wcześniej parser AST zachowano. Nie nałożono starego niezaufanego
  integration.patch `1745986596b3593796d1627f2b2e341fc70ddf8f4580378c6cab081bbd5a87ed`.
  Własny host zastępuje ten proponowany transport; stary checker pozostaje
  niepodłączonym narzędziem diagnostycznym.
- Kanon §0 rewizja 2: dane profilu osobno od egzekwowanego w kodzie obowiązku
  ograniczonego procesu, weryfikacji oryginału i przypiętej metody. Nie rozszerzano
  loggera ani nie deklarowano pełnej instrumentacji/higieny logów ekosystemu.

Rozszerzono istniejący subprocess i resolver DOCX, bez drugiego stosu transportu.
Instalacja komponentu runtime dostarcza `structure_readers`, pięć plików payloadu
oraz manifest interpretera/hashów. Interpreter jest zależnością systemową.
Ścieżka/helper/interpreter/limity/opcje pochodzą z instalacji, nigdy z uploadu
lub konfiguracji konta. Requesty próbujące podać receipt, pack, reader, profil
lub opcje są odrzucane przed mutacją. Python działa przez argv bez shell,
`-I -B -S`, ograniczone stdin/stdout/CPU/pamięć/wall time, czyste env i cleanup.
Kod użytkownika nie jest wykonywany.

Source/Unit/Observation, genesis, provenance, wersja i wystąpienia korzystają
z istniejącego grafu. CSV raw quotes i bajty BOM/CRLF pozostają obserwowanym
oryginałem; decoded values i struktura AST są pochodne. Wiersz i jedyna komórka
tego wiersza mają osobne occurrence ID mimo identycznego zakresu. Odczyt i
rzeczywisty selected context reparsują dane przypiętym helperem. Sam DB selector
nie dowodzi integralności bloba. Usunięcie bindingu nie zamienia starego
wystąpienia w zaufany zwykły tekst. Nie dodano C ABI: 126 eksportów zachowane.

## Własne wykonane bramki

Wszystkie dane syntetyczne: PL fixture CSV z BOM/CRLF, cytowanymi polami,
ragged/missing columns, testowy Python zawierający niewykonywany `raise`,
istniejące syntetyczne DOCX/G1 oraz lokalna atrapa providera. Landlock/env
whitelist, własne katalogi i loopback; helper z pakietu, checkout readerów
niedostępny. Wyniki autora 15 CSV/98 differential records i 19 AST są historyczne,
nie stanowią poniższych własnych PASS i nie były ponawiane.

| Bramka | Rzeczywisty wynik |
|---|---|
| Native przez nową lib/C API | 9/9 grup PASS (run03): raw/derived, row/cell/range, idempotencja, odmowy przed mutacją, zagnieżdżenia, AST bez wykonania, receipt/binding tamper, reopen |
| Uszkodzenie oryginału na nowej kopii | PASS read/materialize refusal, liczniki DB bez zmian, oryginalna fixture nienaruszona |
| HTTP CSV i AST | 2/2 PASS: upload, materializacja, selector, rzeczywisty system context z ID i wyłącznie wybraną treścią, odpowiedź atrapy, restart/read/original |
| Granice subprocess/pakietu | 8/8 PASS: brak/podmiana helpera, brak interpretera, input/output/wall/CPU/exit |
| Wspólne sentinele CTest -j1 | DOCX 9 przypadków/172 asercje; G1 UTF8 11/76; oba testy PASS |
| Domyślny packaged DOCX | PASS 11 części i PL derived read, oryginalny hash zgodny |
| Trwały graf CSV, odczyt read-only DB | PASS Source, genesis/version, pełny receipt, 5 rows/11 cells/11 Observations, 16 provenance i 11 parent links |
| Zasób text pack | generator --check PASS |
| Nowy pakiet | checksum/path check, verify-package startup/health/auth/static PASS wariantem BusyBox; default CSV reader po relokacji PASS |

Dowody w `.integration-host-readers-20261101/`: `native-result-run03.json`,
`http-tests.log`, `limits-result.json`, `original-tamper-result.json`,
`sentinel-ctest.log`, `docx-default-result.json`, `persistence-result.json`,
`runtime-binding.json`, `package-helper-result.json`, `verify-package02.log`.
Pełne argv/env/izolacja w `*-command*.json`; manifest końcowy wiąże te pliki,
źródła, skrypty operacyjne i pakiet. Nie włączono prywatnych transportów do Git.

Polecenia kompilacji: `cmake --build .integration-release-20261031/build
--target loom loom-server -j2`. Reużyto własny Release cache bez pełnej kopii;
po wykryciu nieaktualnej lib przebudowano jeden TU source_parts i relinkowano.
Końcowa kontrola build: `ninja: no work to do`. Focused sentinele z
`loom/tests/docx_source_parts`, osobny katalog obiektów; `ctest --test-dir
.integration-host-readers-20261101/sentinel-build -j1 --output-on-failure -V`.
Bez monolitu/CLI/pełnej macierzy P0. Przed buildem >1 GiB, brak aktywnego heavy,
terminal poprzednik i bieżąca autoryzacja administratora; bez pobierania zależności.

Zachowane nieudane próby: run01 harness próbował zapisać immutable fixture
(EACCES); poprawiono test na atomic replacement tylko własnej nowej kopii.
Run02 używał stage sprzed późnej kontroli identity; nie uznano wyniku za odbiór
nowych źródeł. Run03 jest związany z finalnym kodem/lib. Pierwszy verify-package
z GNU tar zakończył ENOSYS (`Function not implemented`); opisany już w repo
wrapper BusyBox dał PASS po sprawdzeniu ścieżek i symlinków archiwum. To nie
jest dowód działania GNU tar na docelowym hoście. Żadnych osłabionych asercji.

## Nowy immutable pakiet

Ścieżka pod CWD:
`.integration-host-readers-20261101/chatadhd-server-linux-x64-89cb74f7-release-host-readers-20261101.tar.gz`.

| Składnik | SHA256 |
|---|---|
| Archiwum | `42cf960c0f89d55be91ad5175ff0b0d082a5ca1216168c0fc5a9d88329f2f61d` |
| MANIFEST.json | `f03f5712b2b9482414f04db428b4cfdcf7d780cd81e2509f62c8282225538837` |
| server | `daf0302694d1aba15df22fcb640874d72a24d4770d0c1640087e7869c35abfd3` |
| libloom | `b1e0e288364a27cf61b315b9036fadf2595b04a6cc7f56a1e237e07c2b995cdd` |

16 payloadów web niezmienionych, pochodzenie `ccada5e5c2542252112fe40caa95d4b407048e80`.
Manifest wiąże źródła/binarne/helpery/profil/interpreter/web i dowody.
Poprzedni pakiet `b786b7e654e6660e6382cd7d8ac8434d9ef1517f2e0326ff9003a3abc372ac10`
zachowany bez zmiany. W pakiecie nie ma danych kont, DB, sekretów ani prywatnej
konfiguracji. Nowego pakietu nie wdrożono. Dokumentacyjny commit końcowy nie
zmienia testowanego drzewa `loom`.

## Granice i next

Brak GUI CSV/AST, realnego modelu, Android/JNI/mobile, pełnej bramki repo i
aktualnego testu rollback danych z nowymi wystąpieniami. Stare testy kompatybilności
nie pokrywają tej zmiany. Nie deklarujemy in-place rollback; operator zachowuje
zgodny pakiet i spójną kopię zgodnie z odebranym cold_snapshot.

Preset CSV: średnik, cudzysłów, nagłówek, PL. Opcje zmienia się przez świadomy
pakiet/profil, bez sniffingu lub nowego UI per dokument. Zmiana przypiętego
interpretera/helpera odmawia replay starej metody; brak wielowersyjnego rejestru
parserów/migracji. Zachować matching runtime. Hash pliku interpretera nie jest
atestacją całej systemowej stdlib ani granicą przeciw złośliwej instalacji.
Transport nie dodaje sieciowego sandboxa. Koszt reparse per occurrence nie był
benchmarkowany. Cache nie audytuje/naprawia całego podgrafu; SQL rollback nie
obiecuje usunięcia wszystkich wcześniej zapisanych CAS blobów. Odmowy mogą
zawierać zaufaną ścieżkę instalacji i zachowują istniejące kody diagnostyczne DOCX.

Następny krok należy do oddzielnego, już zapisanego zadania odbioru pozostałej
floty po audycie administratora. Tego cyklu nie uruchomiono. Runtime CSV/AST
odebrany zakresowo; dalsze bramki wydaniowe/operatora pozostają jawne.
