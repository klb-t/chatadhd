# Odbiór M1 i podstawowego DOCX — 2026-10-31

Baza przyrostu: `1cf490dcd55587c00e33c37e7290b813562f758c`.
M1 przyjęte w `d8692078ecd95ddb9835f69c00c65524c6d6a789`.
DOCX i testowany scalony kod przyjęte w `4f6656bcfc515fff8a887a37ee7f1a932212b969`. P0, M2 i poprawka GUI zgody
pozostają przodkami. Nie nakładano ponownie ani nie powtarzano bramek M2.
Własny build Debug/WERROR, `-O0 -g0`, `-j2`; CTest `-j1`.

## Pochodzenie i integralność

| Pakiet | Task / base HEAD | Snapshot | SHA256 archiwum z attestation |
|---|---|---|---|
| M1 | `chatadhd-m1-ocr-vision-20261101` / `08c6d1d15e54e0be7f7d605fbc90d066e02ebccd` | `1793480221900527328` | `181964a6fbc2b407b0860c31aa47101c098db19572ec72edb0778b3b7648e6a7` |
| DOCX | `chatadhd-m3-docx-20261101` / `cab9ca88d725c9d78cd8bceb8993443c2969624f` | `1793481775588530739` | `2c6913be51dfd14acd9c8ae8e6c227e14ebcc67f91355c131aacba57e33cddd2` |

Worktree źródłowe: `/var/lib/codex-team/worktrees/<task>`; checkpoint w każdym
`CHECKPOINT.md`. Zadania completed, ale odbiór oparto na własnym odczycie i testach.
Archiwa pozostały root-only i nie były odczytywane. Manifesty, HEAD, status
(`--porcelain=v1 -z --untracked-files=all`), jawny diff i pełne nowe pliki
porównano przed/po odczycie oraz przed przyjęciem: M1 37/37, DOCX 22/22 zgodne.
Nie kopiowano checkpointów, handoffów, prywatnych logów ani metadanych Git.

M1 PNG rzeczywiście odczytany i obejrzany: 28976 B, SHA256
`05bb01e18f58fefb8fc984d14771d7c831268684ef6279089bdec0572d54bde1`.
Wcześniejszy EACCES usunięto administracyjnym ACL; integrator nie zmieniał praw.
DOCX początkowo miał nieczytelną bazę i pozornie zmieniony status. Odbiór odłożono;
po udostępnieniu odczytu sprawdzono wszystkie 20 bazowych plików, pusty diff,
status i 22 nowe pliki. Zachowano także negatywny preflight.

| Materiał | SHA256 |
|---|---|
| M1 dokument checkpoint | `74e63bc4bbfa99594800c007df684113cdc1b37dd018ed71e2d3509bdc98f28b` |
| M1 wiring | `d06f26f5f6339a96142e4c88d8e79449c3719ab54893b8baaaaf3587bea4492d` |
| M1 przyjęta delta | `0818d45a4698ef74599551c077a71e525d54bbe219fae60a79df040090cbad89` |
| DOCX dokument checkpoint | `ad324af434934480c56a062076042821ad39a11b797bfccf8de61629d99491bc` |
| DOCX oryginalne hooki | `d96b4ce625a9fff07c31ec03769b40fdab978c89b58c5885f5c9e7c0403822c7` |
| DOCX przyjęta delta | `e3a5ad57f65b99c9aea11404da44ef86ce1cb0fe6ac6d3eb3bcb4cbf2134df5f` |
| DOCX wybrane hooki bez starego generated inc | `f4b3e208aff8781a52168511c33f3134ad3213439db4f2abc4d015b46f715f59` |

M1: 21 nowych plików, 13 hooków i własna regeneracja runtime; 35 plików
przyjętego kodu/testów. DOCX: 11 nowych plików, 4 hooki i regeneracja; 16 plików.
Każdy patch poprzedzono `git apply --check`. Po M1 54 wcześniejsze profile
zachowane i dodany `image_input`; po DOCX zmienia się wyłącznie `chat_context`.
Jego poprzednie wartości i kolejność readerów, w tym PDF, pozostają identyczne.
Oba generatory `--check` PASS. Nie nadpisano wspólnych zasobów starą całością.

## Własne wyniki M1

| Kontrola na d8692078 | Wynik |
|---|---|
| Przyrostowy server/libloom build `-j2` | PASS |
| Lokalny OCR i parser | 16/16 PASS, 0 skip |
| Native M1 11 + legacy screenshot 6 | 17/17, 1183/1183 asercji PASS |
| Publiczne C ABI + provider HTTP loopback | 8/8 PASS, 33,393 s |
| Eksportowane ABI | 3/3 PASS |
| Regresje CTest `-j1` | 6/6 zestawów, 23/23 przypadki, 0 skip, 170,79 s |

Regresje: PDF 5, dialog 2, UTF-8/TXT/Markdown/kod jako tekst 3, locale 2,
granica poświadczeń 9, C API override 2. Nowe przebiegi uzasadnia zmiana
wspólnego chat_engine/use_model. Nie sumowano kolejnych prób jako nowych przypadków.

Rzeczywisty lokalny Tesseract odczytał polski fixture i 13 regionów. Testy wiążą
oryginalne bajty, SHA, źródło, wynik OCR, lokalizatory regionów i reopen.
Prepare nie utrwala artefaktów; odmowa/preview/hold nie wysyłają HTTP ani nie
zapisują nowego OCR. Wybrana historia jest skanowana przed routingiem.
Dodatkowy test integratora sprawdza dodatnią zgodę z dokładnym
`image_payload_sha256`, jeden dispatch i persist po wysłaniu, a po zmianie
pytania ponowny hold. Zwykłe `approved:true` nie omija bramki nowego OCR.
Kontrola rzeczywistych `body_bytes` obejmuje adaptery; nazwa modelu nie nadaje
vision. Anthropic vision pozostaje jawnie nieobsługiwane.

Negatywne przebiegi zachowano. Test autora próbował użyć niewyeksportowanej
funkcji `loom_usage_policy_json`; test używa teraz publicznego
`loom_invoke(chat, request, preview:true)`. Native testy nadal obejmują preview
zużycia. Poprawiono też sprawdzanie tekstu wewnątrz części wiadomości zamiast
`str(list)` oraz rozdzielono relacje obrazu od dwóch prawidłowych relacji
wiadomości po udanym dialogu. Pełne kontrole braku zapisów przy odmowie
pozostały niezmienione. Nie zmieniono C ABI ani kodu produktu dla tych poprawek.

## DOCX — zakres i własna bramka

Parser: **40/40 PASS**, 0 skip, 0,538 s. Przyrostowy adapter C++/server/libloom
build **PASS**. Własny HTTP przez natywny serwer/adapter: **7/7 PASS**, 0 skip,
111,369 s (CTest 111,88 s). M1 native po scaleniu: **17/17, 1183/1183 PASS**,
16,84 s. M1 ABI/HTTP po scaleniu: **8/8 PASS**, 33,244 s; eksportowane
ABI **3/3 PASS**. Regresje scalonego kodu: **6/6 zestawów, 23/23 przypadki,
0 skip PASS**: 9 poświadczeń + 2 C API, następnie 12 P0 w czterech zestawach
(133,16 s). Są to końcowe wyniki na tych samych 49 plikach źródłowych i tych
samych binariach, po opisanych niżej błędach katalogu dowodów; nie suma prób.

HTTP sprawdził multipart upload, dwukrotny identyczny hash, pobranie dokładnych
bajtów, ekstrakcję PL i faktyczny kontekst dostawcy, źródło `src_docx_<hash>`,
dalsze tury, reopen/restart, niezmienny oryginał oraz trwałe partial/warnings
w obu wiadomościach. Dodatni przypadek ma 3 żądania; w każdym własny odczyt
transcriptu potwierdza polski tekst i polskie ostrzeżenie o formatowaniu.
Zewnętrzna relacja daje warning i tylko 1 żądanie chat, bez pobrania celu.

Odmowy: pusty/uszkodzony/ucięty kontener, brak tekstu, szyfrowany Office,
traversal, XXE w pomocniczym XML, makra i pozostałe rozszerzenia, limit
dekompresji/stdout oraz brak interpretera/helpera. Wszystkie te przypadki:
0 żądań dostawcy, bez częściowej tury/źródła, oryginał zachowany po restarcie.
Dodatkowy test integratora umieszcza syntetyczny znacznik wyłącznie w XML:
`secrets_in_chat=never` odmawia przed dostawcą i zapisem rozmowy także po
restarcie. To nie jest test jakości modelu.

Pierwszy własny start CTest miał 7 błędów setup: katalog dowodów był poza CWD
harnessu. Zmieniono tylko `WORKING_DIRECTORY` prywatnej rejestracji CTest;
nie kod ani asercje produktu. Negatywny log zachowano, a zakończonego native
nie powtarzano. Dopiero następny przebieg dał powyższy PASS.
Ten sam błąd prywatnego katalogu dowodów zatrzymał setup czterech zestawów P0
uruchamianych przez CTest z build CWD. Poświadczenia 9/9 i C API 2/2 już wtedy
przeszły; poprawiono ścieżkę wyłącznie dla pozostałych zestawów. Nie zmieniono
asercji i nie powtórzono 11 zakończonych przypadków.

Parser ZIP/OOXML zachowuje Unicode PL, kolejność prostych akapitów, nagłówków,
list i tabel. SHA dotyczy oryginalnych bajtów. Każdy sukces jest `partial`;
formatowanie, paginacja, właściwe etykiety list i pozostałe pominięcia mają
ostrzeżenia. Brak wyliczonych stron DOCX. Upload nie wywołuje modelu.

Kontrola ZIP obejmuje rzeczywiste wielkości dekompresji/CRC/koniec strumienia,
liczbę wpisów, traversal/aliasy/symlinki, także ignorowane części. XML używa
Expat, odrzuca DTD/XXE także w UTF-16 i kontroluje wspólne limity. Relacje
zewnętrzne nie są otwierane. Szyfrowanie, makra/DOCM, ZIP64 oraz inne kontenery
pozostają odmowami. Nie ma deklaracji DOC/XLSX/PPTX/ODT przez samą nazwę pliku.

Presety limitów są danymi (`0` wyłącza daną granicę); brak sieci/modelu w parserze
jest inwariantem. Adapter uruchamia zaufany interpreter/helper bez shella,
z minimalnym środowiskiem, limitem stdout i timeoutem; nie jest to sandbox OS.
Kod jest warunkowy dla Linux/glibc ≥ 2.34. Brak nowego ABI ani migracji tabel.
Źródło DOCX jest idempotentne i związane z blobem; nie dodano grafu per blok.

## Granice odbioru i dalszy krok

DOCX testowany z jawną konfiguracją helpera z własnego checkoutu i istniejącym
interpreterem. `/usr/bin/python3` istnieje; domyślny helper
`/usr/share/loom/docx_basic/reader.py` **nie jest dostarczony w tej instalacji**.
To luka pakietowania/gotowości domyślnej, nie dowód działającego pakietu wdrożeniowego.
Nie wykonano instalacji. Należy dostarczyć helper i potwierdzić konfigurację
w docelowym artefakcie przed akceptacją wydania.
Wyłączenie readera `.docx` w profilu nie wymaga kasowania blobów ani źródeł;
zmiana nie migruje schematu. Osobny lokalny commit pozwala wycofać hooki.
Procedury rollbacku wdrożenia nie wykonywano ani nie deklaruje się jej PASS.

Dane: syntetyczny PNG autora, jawne małe ZIP/XML generowane w testach,
atrapy dostawców loopback. OCR jest rzeczywisty lokalnie; odpowiedź modelu
jest syntetyczna. Testy nie dowodzą jakości OCR dla dowolnego dokumentu,
zgodności pełnego OOXML, bezpieczeństwa przez fuzzing ani realnego modelu/API.
M1 lokalny worker ma timeout procesu Tesseracta; odrębny watchdog całego
procesu Pythona nie jest dowiedziony. Brak zdalnego fallbacku.

GUI: zachowane P0 lang 9/9, person 8/8 i poprzedni test przekazywania hasha 4/4;
nie powtarzano bez zmiany web. Nie są odbiorem nowego pełnego GUI OCR/DOCX.
Fizyczny telefon, nowy GUI flow, Release/sanitizery i pełny repo gate niewykonane.
Raw503/role labels pozostają znanymi lukami. M2 zewnętrzny provider-share i
publiczny TLS nie wynikają z tego odbioru. Implemented/tested tylko w opisanym
zakresie; deployed **NIE**.

Kanon §0 rewizja 2: dane/presety oddzielone od inwariantów integralności,
brak inference jako observation. Globalna hierarchia logów i jeden wyłącznik
wdrożeniowy pozostają nieudowodnione; bez niezwiązanego refaktoru. Wąski przegląd
ścieżek/diffów/znanych wzorców sekretów nie jest pełnym audytem bezpieczeństwa.
Bez agentów/modeli/API, prywatnych danych, host changes, push/PR/publikacji/deploy.
Następny przyrost uruchamia administrator; deadline nieodnowiony.

## Powiązanie źródeł, binariów i wyników

49/49 plików kodu/testów M1+DOCX odpowiada dokładnie `git show` testowanego
commita `4f6656bcfc515fff8a887a37ee7f1a932212b969`. Manifest źródeł SHA256
`aa140facf4fc9527e343546a58cc7114f41f313968fc3add2fb66e8ba9b1b399`.
Baza publiczna, P0 i M2 pozostają przodkami; bez importu innej historii.

Własne dowody pozostają lokalnie w `.integration-m1-docx-20261031/`,
build w `.integration-p0-20261031/build`; nie są commitowane do produktu.
Manifest `accepted-evidence.json` SHA256
`53376789584868d73f7bd101905170e82c2a62b03686605d34c9e15f5fa88f78`
wiąże logi, siedem syntetycznych transcriptów, source manifests i CMakeCache.
Manifest wcześniejszego odbioru M1 SHA256
`6b76b5f58e16de3b76a9e3e2b5cbe9038d9356ab9ab6e11142f07654ac6a2526`.

| Własny artefakt po DOCX | SHA256 |
|---|---|
| loom-server | `e918637435f81009c7178dee844c07b1a10fcde2ec42ff1bd10d1e520e3bfccf` |
| libloom.so | `b9bf72f00ff8d202cb7db9ca0354b8071fecd0984c57d14655ce87c3329fa884` |

Własne buildy, CTest, serwery i harnessy zakończone. Końcowy odczyt nazw
procesów tego UID nie wykazał loom-server/ninja/cc1plus/ctest/node/chromium.
Nie zatrzymywano cudzych procesów. Przyrost lokalny zakończony; kolejne prace
wymagają uruchomienia przez administratora.
