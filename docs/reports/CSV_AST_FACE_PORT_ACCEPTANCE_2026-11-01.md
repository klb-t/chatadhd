# CSV/AST picker i izolowany Face — odbiór 2026-11-01

TARGET_ENV_POLICY_APPLIED. Baza `ba8a2330e3f0f9c18fbb0a712a33f73655093b79` zachowana.
Kod **a56a427c5f6701243a339e70519db118e441a74d**, drzewo loom `25212baed1896c50a543f2052957b74b235564a4`.
To następny kandydat lokalny, bez publikacji i wdrożenia przez integratora.

## Przyjęty zakres

Katalog wykrywa CSV i Python AST; zwykły kod pozostaje jawnym wyborem.
Sześcioliniowy hook opisów dodano do bieżącego SourcePartsPanel, zachowując
SourceProjectionReview. Brak nowego parsera, endpointu i zmiany C++/ABI.
Obecność formatu w pickerze nie oznacza dostępności helpera. SyntaxError i brak
helpera kończą się odmową, bez automatycznego fallbacku i sukcesu materializacji.

Osobno przyjęto 17 plików izolowanego `loom/tools/face_preview_port`: skończony
JSONL helper, przypięty renderer/controller, dane, syntetyczne fixtures i sanity.
Capability nadal `unavailable`. Host-system-site, instalacja zależności, host C++,
ChatView hook i render produktu **NIE przyjęte**. Helper nie jest dodany do
runtime pakietu. README tylko doprecyzowano, usuwając odsyłacze do nieprzyjętych
proposals. Nie kopiowano root reference, checkpointu, manifestu transportowego,
patchy, propozycji STATE ani danych administratora do produktu.

## Własne wykonanie

Jedna wspólna komenda:
`npm --prefix loom/web run build -- --outDir ../../.integration-csv-ast-face-port-20261101/web-dist`
— strict TypeScript i Vite PASS; Vite 19.84 s, zachowane ostrzeżenie dużych chunków.

`python3 .integration-csv-ast-face-port-20261101/run-browser.py` uruchomił
`node e2e/csv-ast-picker-native.mjs` w `loom/web`: **3/3 grupy PASS**, zero
błędów strony i zero żądań chat/model/provider. Rzeczywisty Chromium, App,
SourcePartsPanel, HTTP i istniejący pakietowy native runtime; żadnych atrap API.

- Picker CSV/Python → upload → source/parts → odczyt → globalny wybór.
  CSV PL/BOM/CRLF i quoted multiline cell zachowuje surowy cytat oraz physical
  lines [2,3]. AST deklaracji ma klasę `derived`; kod z `raise` nie jest wykonywany.
  Oryginały sprawdzone SHA256. Panel projection review pozostaje obecny.
- Rzeczywisty SyntaxError: odmowa, brak nowej projekcji i zmiany wybranego źródła,
  jedno żądanie bez fallbacku. Nowy jawny upload jako `code` pozwala czytać ten
  sam niepoprawny składniowo kod jako tekst.
- Native restart: exact read i globalny selektor zachowane, oryginały zgodne.
  Osobny wariant tych samych binariów bez packaged helpera: rzeczywista odmowa
  `docx_reader_unavailable`, format nadal `python_ast`, brak mutacji projekcji.

Wszystkie dane syntetyczne, nowe własne rooty, Landlock, loopback i whitelist env.
Wszystkie własne procesy zakończone. Nie powtarzano autorskich 4 picker ani
1 render/18 odmów Face, dawnych benchmarków, P0, OCR, projekcji, bundle ani macierzy
kompatybilności. Te wyniki pozostają historycznymi dowodami autorów, nie nowym PASS.
Nowy helper Face nie był tu wykonywany ani podłączany do hosta.

## Dokładny kandydat

- Archiwum: `chatadhd-server-linux-x64-a56a427c-release-csv-ast-ui-20261101.tar.gz`.
- SHA256 archiwum: `5db9ac62b9e7d1bcceb0e53bfc995a6e443da63d985141a28e62e58e6db0173c`.
- SHA256 MANIFEST: `7f6436d6cd9060d888f34a6e793dbd2534b26bb19179264cb561c1b7e997e7d4`.
- Runtime code: `e27ce9ff0076c287963a588d942d68e7bebaca3f`.
- libloom: `7004d54bd106e281acddd5d352e8ab0e2ced0d710245f8a17c003ccb336e610a`.
- server: `cc0f65f19cfaf609706064ae068f09073016aba257ce80509d1ae5431eb8ac90`.
- web index: `c3c2d4439c1b43c2377e4956d97edbf497ce079ba87dc61c95e292586396950f`.

106/106 payloadów zweryfikowano; 90 runtime/helperów identycznych z poprzednim
pakietem d490. Nowy web powiązany z przetestowanym buildem; stare archiwum pozostało
niezmienione. `assemble.py` nie kopiował całego cache. `verify-artifact.py` wykonał
istniejący verify-package przez BusyBox: actual extract/start/health/static/auth
HTTP200 PASS. GNU tar nie uruchamiano ponownie i nie przypisuje mu się PASS.
Preflight zostawiał ponad 1 GiB po szacowanej ekstrakcji oraz buforze 8 MiB.

Mandatory projection gate pozostaje historycznie wykonany na identycznym lib7004
oraz przypiętych helperach/profilach, przez hash
`76d069479c0f61ec39da44a63bb0ad2a0d0189ee0451bab41c35c53714ffc0d3`.
Nie jest nowym testem tej partii: explicit selection, `not_exercised`, odmowa
poprzednika i cold restore z utratą wszystkich post-copy writes (RPO) zachowane.

## Pochodzenie i następny krok

Donorzy `chatadhd-csv-ast-upload-ui-20261101` i
`ecosystem-face-local-render-port-20261101`, baza `2c789e58`.
Dwa świeże odczyty HEAD/status/diff/plików zgadzają się z audytami; strict
apply-check 3/3. Katalog patch `1d136f3377752d2e3b2dfc64743a4c3c672ace28920e75874644cb7dd7b553de`,
hook `492907724b7d99282cfe16ce391831934e0bf7f12ed580ccac98ed21d8900e46`,
Face podzbiór `0f7943d48e0e94f805ca37f5defa45d70b7d3e08323e908f39ae45270f32af87`.
Zastosowano raz; wszystkie 21 plików kodu/testów mają własny hashmanifest.

Administrator odbiera dokładny result SHA i pakiet, następnie publikuje/aktywuje
w swoim zakresie. Produkcja nie zmienia się od completed. Dalsza integracja Face
wymaga zaufanej instalacji NumPy/renderer/controller, bounded runnera,
pinów konfiguracji/zależności, cleanup i jawnego opt-in klienta; capability nie
może wynikać z samego posiadania helpera. Brak CAS globalnego selektora i bounded
listy pozostają wcześniejszymi granicami. Realvision, inferencja, fizyczny mobile,
inne OS/CI, pełny repo gate i deployment NOT RUN. Kanon §0 rewizja 2 zachowany.
