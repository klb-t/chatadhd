# Odbiór G1 GUI i aktualizacja pakietu — 2026-11-01

Przyjęty kod: **`8b2c0e689b1ddf43c74519e68ed626e79d5a4b16`**. Hook uploadu: `d54030114b7d83d991b0f04f8f068b9399d24077`.
Baza integratora `a033aad8538519ca78058436bb17da604453a293` zachowana jako przodek wraz z P0, M1/M2,
DOCX, G1 i poprzednim Release. Wynik: **implemented/tested zakresowo**;
**deployed: nie**. Własny Chromium, backend Release i syntetyczny dostawca HTTP.
Nie wykonano wywołań rzeczywistego modelu/API.

## Pochodzenie i zakres delty

- Task `chatadhd-g1-gui-20261101`, źródłowa baza `e47079cf9f44b0b90654d21770bcfb24c4fd34cc`.
- Worktree autora: `/var/lib/codex-team/worktrees/chatadhd-g1-gui-e47079-20261101`.
- Completed snapshot `1793488871812749883`; archive attestation
  `54861a1896d379fd09f96903b652d3c76dcfe774680deeb2631b170459a5f4ab`.
  Archiwum chronione nie było odczytywane. Completed nie zastępowało testów.
- Frontend patch `0d307b041e7b07f66088943498969281be4102dfb0041fc9ad4c5c3c200f8c05`;
  osobny hook `2fdef446581c690922ecaf8c00b5073cc012ee68cef1222c2c53ab7f19a7f620`.
- Własny świeży odczyt przed/po: **19/19** zgodnych plików, niezmienione manifesty,
  HEAD, status i tracked diff. Status SHA256
  `4a1428c884ca5060f5579b3a914f3cac73d7eba26830a81c4398bc6aaa3f37d2`;
  diff `d56390a1b62b34d48cb35c100a8d1aea00d327ff1dc26fe0785a15414b74a177`.
  Pominięto chronione `docs/exports/**` i `eval/real-holdout-key/**` zgodnie z
  audytem administratora; nie czytano ich treści. Autor wyłącznie read-only.
- Oba jawne patche przeszły `git apply --check` i zostały nałożone po jednym razie.
  Bez transportu, CHECKPOINT, logów, administracyjnych plików czy buildu w commicie.
  Importowany kod nie zawiera rzeczywistych sekretów ani danych użytkownika.

Hook opt-in zapisuje SourceRecord klucza i hasha już podczas uploadu. Stare
uploady bez `source_key` zachowują kontrakt. Materializacja jest oddzielną,
jawną akcją. SourcePartsPanel w ImportPanel, ChatView i GraphView używa istniejących
akcji i grafu, odczytuje oryginał oraz konkretne wystąpienia. Dane etykiet i
presetów formatów są w JSON; walidacje tożsamości, UTF-8 i selektora są w kodzie.
Nie zmieniono schematu DB ani publicznego ABI. Generowane runtime/text zasoby
oraz helpery pozostały zgodne ze starym pakietem; nie nadpisano ich deltą autora.

## Własne wykonane bramki

| Zakres | Wynik i granica |
| --- | --- |
| `npm --prefix loom/web run build` | PASS: `tsc -b` z konfiguracją repo oraz Vite23.86s, 328 modułów; ostrzeżenie chunk>500kB zachowane |
| Node: `text-sources.test.mjs`, `url-import-request.test.mjs` | **15/15**, 11 logiki G1 +4 URL; to nie jest native acceptance |
| React/Chromium `source-parts-component.mjs` | **3/3**: błędny zakres UTF-8, odmowa native bez zapisu selektora, nieprawidłowy limit odczytu nie czyści wyboru |
| Przyrostowy Release server+lib `-j2` | PASS, tylko zmienione client_blobs/app i linkowanie; zachowany własny Release/Debug |
| HTTP `test_server_upload_source` | **2/2**: klucz przed materializacją, hash, idempotencja, odrębność źródeł, legacy/restart/original; odmowy401/403/400 |
| HTTP `test_server_graph_source_parts` | **3/3** regresje rzeczywistego kontekstu, wersji/original/reopen i izolacji sesji |
| G1 GUI `g1-source-parts-native.mjs` | **8/8**, ostatni CTest19.59s, rzeczywiste App/ImportPanel/GraphView/ChatView |
| PNG GUI `ocr-docx-native.mjs`, cases1,2 | **2/2**: lokalny polski OCR w ImportPanel bez wywołania modelu; treść OCR w rzeczywistym żądaniu i odpowiedzi atrapy |
| Staged archive + istniejący instalator | **2/2**, CTest15.67s: web/health/auth oraz stage→switch→return |

Wszystkie CTest uruchamiano `-j1`. Nie budowano `loom_tests` ani niepowiązanych
targetów. Nie powtarzano pełnych historycznych P0/M1/M2/DOCX/Release8 z powodu
wznowienia. Nowy hook uzasadniał HTTP5 i regresję PNG2; zmiana webu uzasadniała
nowy typecheck/build/GUI. Brak nowej bramki pełnego repo, sanitizerów i ABI;
ABI nie rozszerzono.

GUI dowodzi: upload MD z trwałym kluczem przed materializacją/reload; jawne
utworzenie grafu z odrębnymi ID i zakresami identycznych fragmentów; idempotentne
ponowienie; podgraf wybranego wystąpienia w GraphView; dokładne ID i tekst w
SYSTEM context odebranym przez atrapę, bez źródła w pytaniu i bez attachments;
ID i tekst w nowej widocznej odpowiedzi, pochodzące wyłącznie z tego kontekstu;
reopen i rzeczywisty restart backendu; stare/nowe wersje kodu i oryginały;
odrębne źródła identycznych TXT; PNG bez fikcyjnych części G1. Test porównał
całe `context_execution` przed/po, dopuszczając tylko zmianę selektora:
inne seedIDs, budget/profile i pozostałe pola zachowane.

Pierwsze przebiegi harnessu GUI były **nieudane**: 0/8 — wyścig po reloadzie i
próba kliknięcia ukrytego mobilnego toggle; 2/8 — pomylone `id` z `data-testid`
suwaka; 5/8 — nowa asercja odpowiedzi znalazła poprzednią odpowiedź zamiast
oczekiwać bieżącej. Logi/screenshoty pozostały. Poprawiono oczekiwania i locator
harnessu, nie osłabiono asercji ani nie zmieniono produktu dla uzyskania PASS.
Ostatni przebieg ma8/8, brak page errors, dodatkowo ID odpowiedzi i zachowanie
konfiguracji. HTTP5/PNG2 nie uruchamiano ponownie po zmianach samego harnessu.

Dane: syntetyczne polskie TXT/MD/Python zapisane jawnie w harnessach; PNG
`loom/tests/fixtures/local_ocr/synthetic-pl.png`, SHA256
`05bb01e18f58fefb8fc984d14771d7c831268684ef6279089bdec0572d54bde1`.
Wyłącznie własne świeże konta i dostawcy loopback; żadnych realnych poświadczeń,
produkcji ani holdoutów. G1 viewport1280×900, PNG420×900 w Chromium. To nie jest
odbiór fizycznego telefonu, Android/JNI czy innych przeglądarek.

## Nowy pakiet i odwracalność

Wersja `8b2c0e68-release-g1gui-20261101`; lokalne archiwum
`.integration-g1-gui-20261101/chatadhd-server-linux-x64-8b2c0e68-release-g1gui-20261101.tar.gz`.

| Artefakt | SHA256 |
| --- | --- |
| Archiwum | `e65ed96eab67d119b20852e05d65b0bcc4fb8a9ce24b5978a92c7d1fa43ba1db` |
| Manifest | `f63958480873b572c99b3fd6382a30448cd2406002db3fc63c81833efe46a361` |
| Server | `ed3a6e153414d084626f651d46eec69a5cea5ad640d7b060bbcde30eba483044` |
| libloom | `13139d05db718cbf9471e0ed00df0ea673bc4a5afe603fd2b18656d57d6ad55c` |
| Zbiorczy evidence | `993a376daa780190caf4a1c2088c6a402c80f927fea7d66033e6912da2ab2ea1` |

Manifest wskazuje code_head powyżej, runtime tree `d78643f661fd242158f597b6cecd9afcc64cddab`,
wersje narzędzi, systemowe zależności/interpreter oraz pochodzenie helperów/webu.
Sprawdzono **24/24** payloady, **16/16** plików nowego webdist i **17/17** źródeł
wobec commita; hash serwera z receipt GUI jest zgodny z pakietem. Czytnik DOCX,
manifest interpretera, embedded helpery i zasoby nie zmieniły się wobec
poprzednio odebranego Release. Dane i konfiguracja są poza immutable pakietem.

Wykorzystano istniejące `deploy/verify-package.sh` i `deploy/install.sh`, bez
nowego deployera: własny prefix/data, `--no-deps --no-build --no-systemd --no-caddy`,
`--from-tarball`, kandydat z `--no-switch`, następnie istniejący selektor
`--rollback <wersja>` do przełączenia i powrotu. Testy odbyły się po GUI.
Bazą powrotu był poprzedni **własny Release e470**, archiwum
`fa5c71fc6bedb5ad82e6b93297571b7ba6309f5e18b434849983bf8bf883d744`.
Nowa wersja utworzyła upload/source i wystąpienie G1; to samo wystąpienie oraz
oryginalne bajty, PL i rozmowę odczytano także po powrocie do starego Release.
Instalator nie zmienił danych/konfiguracji; oba payloady niezmienione po testach.
Finalny lokalny `current`: `baseline-e470-release`. Serwery zakończone.
Nie testowano nieznanej wersji produkcyjnej ani jej migracji.

Stary bloker GNUtar1.35/openat2 ENOSYS pozostaje: zastosowano wcześniej sprawdzony
lokalny wrapper na dostępny BusyBox tar1.37, tylko PATH procesów testu. Bez
instalacji i zmiany hosta. Nadal nie odebrano źródłowego `install.sh --package`,
który nie zbiera pełnego runtime; odebrany jest konkretny kompletny tar z
komponentu CMake runtime. Zależności systemowe Python/Tesseract/Poppler/glibc/
SQLite nie są bundlowane; brak deklaracji przenośności oficjalnego Ubuntu builda.
[Kontrakt stagingu](../../deploy/STAGING_ACCEPTANCE.md) i
[poprzedni odbiór](PACKAGE_ACCEPTANCE_2026-10-31.md) pozostają ważne w swoim zakresie.

## Komendy i granice

```sh
npm --prefix loom/web run build
node --test loom/web/tests/text-sources.test.mjs loom/web/tests/url-import-request.test.mjs
# cwd loom/web:
node e2e/source-parts-component.mjs
# cwd repo:
cmake --build .integration-release-20261031/build --target loom-server loom -j2
ctest --test-dir .integration-g1-gui-20261101/ctest -j1 -V --output-on-failure
# Po opisanych poprawkach harnessu, wyłącznie G1 GUI:
ctest --test-dir .integration-g1-gui-20261101/ctest-gui-retry3 -j1 -V --output-on-failure
python3 -B .integration-g1-gui-20261101/assemble.py --build .integration-release-20261031/build --kind Release
python3 -B .integration-g1-gui-20261101/run_staging.py
```

Dokładne argv/cwd i hashe dowodów są w prywatnym `.integration-g1-gui-20261101`:
`test-commands.json`, poszczególne CTestTestfile, `code-result.json`,
`accepted-evidence.json`, `switch-dryrun/commands.json`, `switch-dryrun/result.json`.
Te pliki i checkpoint nie są częścią produktu.

Selector jest **globalny dla konta i wszystkich rozmów**. Nie jest request-only
ani conversation-only; UI jawnie to opisuje. Istniejące API nie ma CAS dla tej
samej tablicy seedIDs; współbieżny zapis może utracić zmianę. JNI fallback
read/merge/write nie jest odebrany. Listy są ograniczonym odczytem; brak pozycji
nie dowodzi jej nieistnienia. Frontend waliduje kontrakt i długość UTF-8;
rzeczywisty oryginał/hash/wycinek sprawdza native `blob_text_read`.
G1 dotyczy tekstu UTF-8: bez pełnego AST/tabel i współrzędnych PDF/DOCX/OCR.
Cache projekcji nie stanowi dowodu integralności całego podgrafu.

Kanon §0 rewizja2: dane osobno od inwariantów, oryginały zachowane, części
połączone z pochodzeniem. Brak dowodu pełnej wieloskalowości/hipergrafu lub
jednego globalnego wyłącznika logowania. Testy zawierają wyłącznie syntetyczne
treści. Pozostają wcześniejsze luki polonizacji i raw role/error labels;
ten przyrost nie dowodzi kompletności całego interfejsu.

Next: decyzja wydaniowa administratora na podstawie tego SHA/pakietu oraz
rzeczywistej wersji poprzedniej i kopii danych. Bez samodzielnego następnego
cyklu. Brak blokera przyjętego lokalnego zakresu; zewnętrzne/deploy/mobile/pełne
bramki pozostają niewykonane. Nie publikowano, nie wdrażano, bez push/CI/API/agentów.
