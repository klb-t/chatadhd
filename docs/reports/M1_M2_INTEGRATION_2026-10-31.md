# Odbiór P0 GUI, M2 URL oraz potwierdzania OCR — 2026-10-31

**Przyjęty kod: `5972828c46ac8c8675d132ee8ad557382c5726b6`.** M2 i poprawka potwierdzania
sekretu w UI są przyjęte zakresowo. Core M1 pozostaje nienałożone i zablokowane
na odczycie fixture; wynik nie oznacza ukończenia OCR/vision ani wydania.

M2: `c756c9dd1c1cd33b948b5e95351173d34c459fae`; UI: `5972828c46ac8c8675d132ee8ad557382c5726b6`.
26/26 plików w tych commitach ma dokładnie hashe bajtów użytych w kontrolach.
Testy wykonano przed commitami na scalonym drzewie; zgodność sprawdzono po
commitach przez `git show SHA:path`. Końcowy commit dokumentacji nie zmienia kodu.

Baza tego przyrostu: `cab9ca88d725c9d78cd8bceb8993443c2969624f`.
Wcześniejszy kod P0: `9f672641e5eb223cf7bdc5561faca08dc679a753`.
Zachowano wszystkie wcześniejsze commity, BYOK, locale/PDF, macierz i własny build.
Nie nałożono ponownie żadnej starej delty. Korekta błędnego preflight i odbiór
zachowanych wyników GUI: `e2f2cdf1a1930651908943efc700fa9eefbe32c0`.

## Źródła i granice odbioru

| Pakiet | Task / baza autora | Snapshot i integralność | Decyzja |
| --- | --- | --- | --- |
| M2 URL | `chatadhd-m2-url-share-20261101`, `08c6d1d15e54e0be7f7d605fbc90d066e02ebccd` | `1793479969908267581`; archive SHA256 `6e29663347592c7a72be40a04325a9c3c56331de850ba72ed59e265619a17d67` | Przyjęte zakresowo: `c756c9dd` + własne kontrole poniżej. |
| M1 OCR/vision | `chatadhd-m1-ocr-vision-20261101`, ta sama baza | `1793480221900527328`; archive SHA256 `181964a6fbc2b407b0860c31aa47101c098db19572ec72edb0778b3b7648e6a7` | Core nienałożone; konkretny bloker odczytu fixture. |

Archiwalne sumy pochodzą z zgodnych świeżych manifestów; prywatnych archiwów
nie odczytywano. `completed` oznaczało koniec wykonania, nie akceptację kodu.
Worktree autorów odczytywano wyłącznie read-only. HEAD, pełny status, lista
untracked, jawny tracked diff i hashe kontrolowano przed/po przeglądzie,
nałożeniu i przed commitem. Brak zmian źródłowego pakietu M2 w tych kontrolach.

M2: checkpoint dokumentu SHA256
`49520a4c6984b426236d550eab2ed732ac9f9ec83a1c2eda2c25d746c8206ea9`.
Jawny hook `M2_URL_SHARE_INTEGRATOR.patch` SHA256
`afe5627f8da82c83b0a0ffed40e7970132abb97592387ccd312f3243986b5829`
przeszedł `git apply --check`, potem zastosowanie we własnym CWD.
Przejrzano i skopiowano wyłącznie 19 nowych plików produktu/testów `loom/`;
manifest ich źródłowych hashy SHA256
`0b67fd28c319c806c07d4a8eae19a7f13693c7c9c289fd9c2117fa62cd2259d4`.
Przyjęta delta M2 (z własnymi kontrolami) SHA256
`a3de4d03e469e6448c267525d4acbbcb1eef8ad67b19bbe379394580d40bbe53`.
Osobna własna delta UI SHA256
`d055a3fe225e603f1c4c215f84767f7e1d80d3e4e5ee747390914b138c29fb8c`.
Tracked patch autora dotyczył STATE (SHA256
`12096a0a53e4bba0d7690decab0d760ad420cdb6f6b0259e608205925646534c`)
i nie został nałożony. Nie importowano checkpointów, handoffów, historii
prywatnych receiptów/logów, raportu z prywatnymi odsyłaczami ani danych administratora.

## Co zmienia M2 i własna poprawka UI

URL descriptor `.loomurl` przechodzi istniejącą trasą importu C API/HTTP.
Kanoniczny Python helper jest osadzony w artefakcie, zgodność bajtów i SHA
sprawdza generator. Produkcja nie szuka pliku przez `__FILE__`.
Publiczne URL są walidowane, każde przekierowanie ponownie sprawdzane;
DNS jest przypięty do sprawdzonego adresu, TLS weryfikuje hostname.
Prywatne adresy/credentials w URL i downgrade są odrzucane, proxy otrzymuje
jawny unsupported. Nie ma przełącznika HTTP/config wyłączającego ochronę.

Zapis zachowuje oryginalne bajty, kolejność/role/Unicode, załączniki,
pochodzenie i rewizję; powtórny import jest idempotentny. Nie uruchamia modelu
ani automatycznej analizy. Częściowy wynik i brak załącznika pozostają jawne.
Obsługiwany format rozmowy to własny JSON ChatADHD z `conversation/messages`.
**Nie odebrano adaptera share zewnętrznego dostawcy.**

Integrator poprawił dwa porównania `string == Json` w teście autora na jawne
`get<std::string>()`: pierwszy build wykrył błąd kompilacji, zachowano log,
znaczenie asercji pozostaje bez zmian. Dodał probe rzeczywistych praw
`0600/0700` oraz dodatni test HTTP/restart na osadzonym helperze.

Osobna mała poprawka ChatView przekazuje `image_payload_sha256` z otrzymanego
`secret_pending` do `secret_confirmation` wyłącznie po jawnym kliknięciu.
Zmiana snapshotu wymaga kolejnego potwierdzenia; legacy tekst nie otrzymuje
wymyślonego hasha, refusal nie może wysłać potwierdzenia.
To sprawdzony kontrakt UI z syntetycznym backendem, **nie odbiór core M1**.
Nie dodano pozornej obsługi Anthropic vision.

## Własne dowody i ich zakres

| Kontrola | Wynik | Faktyczny zakres |
| --- | --- | --- |
| Zachowany P0 web build | PASS, 26,64 s | TypeScript/Vite; wynik sprzed przerwy, odebrany bez ponowienia po quota. |
| Zachowane P0 GUI | 9/9 język, 8/8 osoba PL | Własny serwer, Chromium, dostawca loopback, rzeczywisty zapis EN/PL/profilu, PDF 2 strony/reopen/ostrzeżenia/błędy. |
| Przyrostowy M2 GCC/WERROR build | PASS | Własny zachowany build, serwer/libloom, `-j2`; nie cudze binaria i nie pełny repo gate. |
| Native M2 / CTest `-j1` | 8/8, 107/107 asercji | Zapis/reopen/role/bajty/hash rollback/idempotence/revision, brak modelu, policy/private URL, helper/watchdog/cleanup/prawa. |
| Artefakt bez dostępu do checkoutu | 1/1 wybrany, 18/18 asercji | Kopia własnej binarki, default helper, źródła niedostępne; pozostałe 7 przypadków celowo odfiltrowane, nie dodatkowe 8 testów. |
| Parser/transport Python | 33/33 | Syntetyczny parser, SSRF, DNS pinning, proxy, limity i jawne stany. |
| Pakietowanie Python | 4/4 | Zgodność osadzonego helpera, generator, prawa i pakiet; nie zastępuje native. |
| Żądania URL w web | 4/4 | Descriptor/mode, walidacja URL, brak dorabiania auth. |
| HTTP route | PASS | Auth401, private deny, idempotent receipt, zero żądań źródła/modelu. |
| Dodatni HTTP/restart | 1/1 | Domyślny helper → natywny zapis 5 wiadomości, 3 stany załączników, identyczne bajty/role/Unicode, restart, reimport, strona. |
| GUI sekretu | 4/4 grupy | Rzeczywisty ChatView/HTTP/Chromium: hashA/hashB, kolejne kliknięcie, legacy, refusal/dismiss. |
| Regresje P0/BYOK/C API | 6/6 zestawów, 23/23 przypadki, 0 skip | CTest `-j1`, 198,31 s; BYOK 9, dialog 2, tekst 3, locale 2, PDF 5, C API 2. Zmiana wspólnego importu uzasadniała regresję. |
| ABI | 3/3 | Oryginalne sygnatury, dokładny zestaw eksportów, ctypes na nowej własnej bibliotece. |
| Nowy web/GUI po zmianie źródeł | build PASS (23,47 s), lang 9/9, person 8/8 | Własny aktualny serwer; regresja po zmianie ImportPanel/ChatView, osobne logi. Liczby obu przebiegów nie są sumowane. |

`bwrap` odmówił tworzenia unprivileged namespace. Zachowano dokładny błąd;
nie zmieniano hosta ani uprawnień. Dostępny Landlock **dodatkowo ograniczył**
własne dziecko do systemowego Pythona/bibliotek, `/proc`, `/dev/null` i kopii artefaktu.
Przed uruchomieniem native `open` kanonicznego helpera i policy z checkoutu
rzeczywiście zwracał EACCES. Test przeszedł przy tym ograniczeniu.
To brak dostępu procesu do checkoutu, nie usunięcie repozytorium z hosta.

Dodatni HTTP używa jawnej, testowej biblioteki DNS/connect `LD_PRELOAD`
wyłącznie w swoim procesie serwera/dziecka. Nazwy `*.example.invalid`
trafiają do syntetycznego listenera loopback; nominalny adres publiczny nie
jest kontaktowany. Inne połączenia odrzuca fixture. Parser i produkcyjne
sprawdzanie URL pozostają aktywne; osobna trasa bez fixture odrzuca loopback.
Ten wynik nie dowodzi rzeczywistego publicznego TLS, usługi share ani modelu.
Dane PL/Unicode, eksport, załączniki, znaczniki i odpowiedzi są syntetyczne;
nie czytano prywatnych rozmów, sekretów, holdoutów ani produkcji.

Lokalizatory pełnych własnych logów/manifestów: ignorowane
`.integration-m1m2-20261031/` (m.in. `m2-native.log`,
`m2-artifact-landlock.log`, `m2-http-positive.log`, `gui-secret.log`,
`tested-source-files.json`). Dane audytowe pozostają lokalne.
Końcowy własny manifest dowodów, binariów i web dist SHA256
`717dd4cce606011aa86613038c9d9fa470b12b5a5bf60204751be082b5590059` (`accepted-evidence.json`).
Manifest 26 sprawdzonych źródeł SHA256
`89832c79fe11d03b9b233c4a5a5e295aa65732c3b093b067c4613d7a31f16b46`.
Własny serwer SHA256
`c6b602fb2f8c9720ac2d0e0757aec95c3333c6255968b2fdda3f5d19f0dcd2bc`,
biblioteka SHA256
`3503a42f58ac55d723b3ada7959ea43f66dc766467d7b51fc15c4e1092a5da67`.
Binarka native i jej odizolowana kopia mają tę samą SHA256
`a7528a44fe983bab0cfaf9bc482ec103e74237e4a7f4c020d68cf57836a22073`.
Zachowany manifest P0 GUI SHA256
`6a7a46318926c019240d4e271198c3b56f7ecee4fff96b5a8be1731ff8933523`.

## Odtworzenie zakresowych kontroli

Własny build: `.integration-p0-20261031/build`, `cmake --build … --target
loom-server loom -j2`. Native: kompilacja dwóch obiektów `tests/main.cpp` i
`tests/test_url_share_import.cpp` z tego CMake, link z własnym `libloom_core.a`,
`libloom_miniz.a`, `libloom_pqc.a`; własny zakresowy CTest `-j1`.
Nie budowano pełnego `loom_tests`. Dokładne komendy izolacji artefaktu są
w lokalnym `m2-artifact-landlock-command.json`.

Python: `loom/tests/url_share/test_worker.py` oraz `test_packaging.py`.
Web: `node --test loom/web/tests/url-import-request.test.mjs`,
`npm --prefix loom/web run build`; z CWD `loom/web`:
`node e2e/secret-confirmation.mjs`, `node e2e/lang-page-native.mjs`,
`node e2e/polish-person-native.mjs`. Wymagane własne `LOOM_SERVER_BIN`,
`TMPDIR`, cache Playwright; bez instalacji/pożyczania zależności.

HTTP: `loom/server/tests/test_server_url_share_import.py` i
`test_server_url_share_positive.py` z własnym `LOOM_SERVER_BIN`. Ten drugi
wymaga `LOOM_URL_NETWORK_FIXTURE`: własna biblioteka skompilowana poleceniem
`cc -shared -fPIC -Wall -Wextra -Werror loom/tests/url_share/network_fixture.c
-ldl -o <własny-katalog>/network_fixture.so`. Trasy testów, dowody i dane
tymczasowe pozostają we własnym CWD; runner usuwa ambient credentials/proxy
z otoczenia procesów testowych.

## Odłożone i wymagające dalszego odbioru

M1: odczyt
`chatadhd-m1-ocr-vision-20261101/loom/tests/fixtures/local_ocr/synthetic-pl.png`
we właściwym worktree zwrócił **EACCES(13)**. Metadata 0640 / UID 65534 / GID 65534,
28976 bajtów; oczekiwana SHA256 z manifestu
`05bb01e18f58fefb8fc984d14771d7c831268684ef6279089bdec0572d54bde1`
nie została przez integratora potwierdzona. Wiring M1 SHA256
`d06f26f5f6339a96142e4c88d8e79449c3719ab54893b8baaaaf3587bea4492d`
pozostaje nienałożone. Pozostałe dostępne bajty były zgodne z receipts,
ale to nie zastępuje odczytu i weryfikacji fixture. Nie zmieniano praw,
nie odtwarzano obrazu z alternatywnego patcha i nie czytano prywatnego archiwum.
Core M1 **nienałożone**: 11 native + 7 ABI/HTTP oraz regeneracja/runtime `--check`
M1 pozostają niewykonane. Gotowe źródłowo poprawki prepare/persist,
OCR historii przed routingiem i body_bytes nie są przez to naszym PASS.

Po udostępnieniu fixture potrzebny jest świeży audyt manifest/status/hash,
odbiór wyłącznie poprawionego snapshotu i własne testy na aktualnym SHA.
Nie nakładać drugi raz M2 ani P0. Kolejny przyrost uruchamia administrator.

W P0 nadal widoczne surowe 503 i etykiety ról user/assistant/v1; to luki produktu.
Na syntetycznym źródle bez auth nadal pojawia się komunikat o potrzebnym kluczu.
Wąski viewport Chromium nie jest fizycznym mobile. Brak własnych testów
rzeczywistego modelu/API, zewnętrznego share, deploy, pełnego repo gate,
Release/sanitizerów. OCR/M1, DOCX i G1 nie otrzymują akceptacji przez M2.

Kanon §0 rewizja 2: presety/etykiety są danymi, ochrona URL i integralności
bajtów inwariantami kodu; zachowane źródła i pochodzenie. Globalny hierarchiczny
system logowania z jednym wyłącznikiem deploy oraz pełne części/graf pozostają
nieudowodnione w tym zakresie; nie wykonano niezwiązanego refaktoru.
Brak publikacji, push, wdrożenia, instalacji lub zgody kosztowej.
