# Odbiór zespołu ChatADHD — 2026-10-31, audyt i integracja

## Wynik odbioru 18:38 UTC

**Flow przyjęty lokalnie:** `885e493945b125ee72e2fa293682be7d83ae76c5`.
Baza publiczna pozostaje `9533d68e6cf6e133e0fdcd5d5fcf7ee85654da08`;
pierwszy własny commit audytu to `8b255ac9bfc9ff30ba9af0bc24d20e7dc516e8cb`.
B1, B2 i B3 rozwiązano w zakresie tego odbioru. Nie jest to pełna bramka wydania.

| Pakiet | Własny lokalny commit `[skip ci]` | Przyjęty zakres |
|---|---|---|
| Flow, `chatadhd-flows-20261031` | `885e493945b125ee72e2fa293682be7d83ae76c5` | Granica poświadczeń, testy i raport autora; dodatkowa korekta integratora opisana poniżej. |
| Android, `chatadhd-android-20261031` | `f7c5a922450be294c8c48c3c7d636dce42d876a0` | Sam [raport badawczy](ANDROID_LAUNCHER_RESEARCH_2026-10-31.md), bez implementacji HOME. |
| Linux, `chatadhd-linux-20261031` | `c75241edb176ef6f21104736ebcbd59c2bb485cf` | [Raport](LINUX_DESKTOP_RESEARCH_2026-10-31.md) i trzy pliki próby kontraktu, bez adaptera pulpitu. |

Stan źródeł ponownie sprawdzono przed i po odbiorze: końcowy odczyt
18:37:11 UTC, **3/3 PASS** (task/locator/archive SHA, HEAD, status `-z`,
patch, wszystkie pliki i lista nieśledzonych). Trzy taski nadal `completed`;
lokatory z 17:46 UTC i sumy z tabeli korekty B1 poniżej pozostają aktualne.
Archiwów root-only nie odczytywano. Zastosowano jawne patche po
`git apply --check`; nowe pliki porównano bajt w bajt z przejrzanymi kopiami.
Cudzego STATE i artefaktów administracyjnych nie importowano.

### Flow: niezależna weryfikacja i własna korekta

Odebrany patch kodu (bez cudzej zmiany STATE) ma SHA-256
`25d1b40651791d9ee15ceb37fc6d4abe28181fac2ca9aa5f678c1be28709c790`.
Pełny patch autora, nowe pliki i checkpoint są zidentyfikowane w historycznej
tabeli poniżej. Przed buildem wszystkie **9/9** przyjętych plików odpowiadały
snapshotowi wykonawcy. [Raport autora](CHAT_CREDENTIAL_BOUNDARY_2026-10-31.md)
zachowano bez zmian: opisuje jego wcześniejsze binaria i wyniki, nie nową bramkę.

Własny build serwera i biblioteki zakończył się **PASS**, GCC 15.2,
`LOOM_WERROR=ON`, Debug `-O0 -g0`, wyłącznie `-j2`. Nowy katalog integratora:
`.integration-flow-20261031-r3/build`; nie używano cudzych binariów ani
wspólnych build/dev. Przed buildem było 26 GB wolnego miejsca. Zewnętrzny
STOP VM przerwał pierwszą kompilację po 104 krokach; zachowane źródła i build
wznowiono inkrementalnie. Przerwanie nie zostało zaliczone jako błąd kodu ani PASS.

Pierwszy własny przebieg oryginalnej delty: HTTP **5/5**, C API **2/2**,
native **23/23** (6567/6567 asercji) i ABI **3/3 PASS**. Dodatkowy test
integratora wykazał jednak **FAIL**: po A → B bez BYOK → A następna odpowiedź
zwracała `No API key configured`. Odłączenie operatora od B działało, ale
powrót do A nie przywracał klucza w działającym kontekście. Ten negatyw zachowano.

Mała korekta w `App::context_for_guest` ponownie sprawdza aktualną pulę tylko
przy brakującym poświadczeniu i przywraca overlay wyłącznie dla zgodnego
identyfikatora dostawcy, dokładnego adresu i slotu klucza. Jawny BYOK, również
pusta wartość, nadal wyklucza operatora. Dodano cztery scenariusze: powrót do
adresu dla czatu, powrót dla katalogu modeli, powrót znacznika źródła oraz
pusty BYOK przed/po restarcie. Istniejących asercji nie osłabiono.

SHA-256 delty własnej względem snapshotu wykonawcy:
`981ee13cd555c8f3b8a4a1de067d97e1ab92283942d63678be62bbdecd5c1bd0`.
Zmienione dodatkowo pliki i końcowe sumy:

| Plik | SHA-256 po korekcie integratora |
|---|---|
| `loom/server/src/app.cpp` | `c68015749b038f813b77440a3b4355474df85e794c2b767270151859ae4fae0a` |
| `loom/server/tests/test_server_guest_credentials.py` | `5cd57e93c5b5bd24abc75c8bdbf22b94b2333017fbceea75e74dc17dfa4bb508` |

Pozostałe siedem plików Flow zachowuje sumy snapshotu. Po korekcie zbudowano
ponownie serwer; biblioteka ma identyczną sumę jak podczas native/ABI.
Testowane źródła są bajtami commita Flow wskazanego wyżej; późniejsze commity
badawcze i raportowe nie zmieniają tego kodu ani jego testów.

| Własna kontrola | Rzeczywisty wynik | Zakres |
|---|---|---|
| HTTP po korekcie | **9/9 PASS**, 51,351 s | Dwie atrapy loopback, dokładne nagłówki; zmiana adresu, BYOK, restart, katalog modeli i powroty. |
| C API po korekcie | **2/2 PASS**, 4,596 s | Własna biblioteka, lokalny HTTP, jawny zapis i pusta wartość, niezależny slot overlay. |
| Wpisy CTest | **2/2 PASS**, 56,96 s łącznie | `server.guest_credentials`, `capi.credential_override`; nie cały CTest. |
| Native config/C API | **23/23 PASS**, **6567/6567** asercji, 0 skip | Osobny runner z istniejących `main.cpp`, `test_config.cpp`, `test_capi.cpp`, połączony z własnym `libloom_core.a`. |
| ABI | **3/3 PASS** | Deklaracje sygnatur, eksportowane symbole, FFI smoke; brak nowych symboli ABI. |
| Dokładny dodatkowy scenariusz A → B → A | Przed korektą **1 FAIL**, po niej **1/1 PASS**, 5,759 s | Ten sam niezależny harness; negatyw nie został nadpisany. |
| Własny diff/staging | PASS | Jawne 9 plików Flow, bez checkpointu, logów i buildu. |

Sumy własnych artefaktów końcowych:

- serwer: `eaa4fc5d26d405fd82d40b610bf003cbc3d5858194a976984e03b2fe50e2b717`;
- `libloom.so`: `522c209fd2fc5eb2c9906abb9bd946aef3b7bbccc6386c732261bfd831922f91`;
- runner native: `f9be3e737498f20cb669c40be0e8861bd79016617bae82f5ff1917fac7417300`.

Odtworzenie podstawowej kompilacji i zakresu CTest we własnym nowym katalogu:

```sh
cmake -S loom -B .integration-flow-20261031-r3/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g0' \
  -DCMAKE_C_FLAGS_DEBUG='-O0 -g0' -DLOOM_BUILD_SERVER=ON \
  -DLOOM_SHARED=ON -DLOOM_BUILD_TESTS=ON -DLOOM_WERROR=ON \
  -DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/env \
  -DCMAKE_C_COMPILER_LAUNCHER=/usr/bin/env
cmake --build .integration-flow-20261031-r3/build --target loom-server loom -j2
ctest --test-dir .integration-flow-20261031-r3/build \
  -R '^(server.guest_credentials|capi.credential_override)$' --output-on-failure -V
```

Testy uruchamiano z oczyszczonym środowiskiem, lokalnymi atrapami i własnymi
ścieżkami binariów. Szczegółowe komendy runnera, logi obu przebiegów, negatyw,
sumy i checkpoint pozostają prywatnie w katalogu integratora; nie są częścią
produktu. Nie wywoływano prawdziwych modeli/API.

### Osobne odbiory badawcze

Android: własne **2/2 kontrole statyczne PASS** — manifest aplikacji ma
3 aktywności i 1 filtr MAIN+LAUNCHER; słownik 13 nazw w 41 plikach daje 0
trafień. Pierwszy pomocniczy odczyt wskazał pusty, błędny katalog `android`;
nie był testem. Właściwa kontrola używa `loom/android` i wymaga dokładnie
41 plików. Bez APK, JNI, emulatora, urządzenia i akceptacji HOME.
Patch nowego raportu: SHA-256
`b84d06bc8fc0fff0c0ac61dfcf657db8b6af2c39719c0e747db7657117eb2018`.

Linux: po zastosowaniu czterech plików we własnym CWD ponownie wykonano
`PYTHONDONTWRITEBYTECODE=1 python3 tools/research/linux_desktop/test_contract.py`:
**8/8 PASS**, 0 skip, 0,051 s. To walidator Desktop Entry, GIO i marshalling
GLib; bez połączenia D-Bus, wykonania Exec, adaptera produktu i GUI.
Patch nowych plików: SHA-256
`6bf9104a32093133128805ff012f32bc24e4fe140584c8e9832789ac6b3a165d`.

Punktowo sprawdzono źródła oficjalne: świadomy wybór roli w
[RoleManager](https://developer.android.com/reference/android/app/role/RoleManager),
filtr [HOME](https://developer.android.com/reference/androidx/core/role/RoleManagerCompat#ROLE_HOME),
granice profili w [LauncherApps](https://developer.android.com/reference/android/content/pm/LauncherApps#getProfiles()),
kontrakt [OpenURI](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.OpenURI.html),
cykl [Request](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Request.html)
i [Exec](https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html).
To ograniczona próbka zgodności raportów, nie pełna certyfikacja platform.

### Granice przyjęcia i następny krok

**Implemented:** poprawka granicy poświadczeń wraz z korektą powrotu.
**Tested:** wyłącznie zakresy i bajty wskazane wyżej.
**Deployed:** nie. **GUI accepted:** nie.
Nie wykonano pełnej bramki repozytorium, nowego buildu/testów webu, sanitizerów,
HTTP/PDF, OCR/DOCX, eksportu bajtów, graph_packet ani testów urządzeń.
Nie zamknięto audytu równoległych żądań i wszystkich nadpisań tras modeli.

Zasady §0 rewizja 1 z 2026-10-31 przyjęto z polecenia właściciela, bez dostępu
do jego prywatnego źródła. W sprawdzanym zakresie polityka puli pozostaje
w danych, kod egzekwuje granicę poświadczeń; brak migracji lub zmian bajtów
źródłowych. Nowe literały są kluczami kontraktu zgodnie z R42. Nie dodano
logowania wartości kluczy. Luka istniejącego logowania: cztery poziomy i nazwy
modułów mają globalny próg; `set_stderr` nie wyłącza ring/sinków, PhaseTimer
ma osobny przełącznik, a serwer używa też `cerr`. Brak potwierdzonego jednego
wyłącznika wszystkich logów deweloperskich przy deploy i hierarchicznych
progów. Nie wykonywano niezwiązanego refaktoru.

Dalsze P0: polski interfejs z jawnym wyborem języka, rzeczywisty HTTP/PDF,
szersze pliki i własne grafy. Macierz darmowych dostawców oraz przegląd
architektury pozostają osobnymi lekkimi zadaniami. Administrator może tworzyć
świeże worktree od przekazanego końcowego SHA; integrator nie uruchamiał tych
zadań ani następnego cyklu. Raporty Android/Linux nie blokują przyjętego Flow.
Wszystkie commity pozostają lokalne; bez push, PR, merge do main i wdrożenia.

## Wznowienie 17:59 UTC — korekta B1 i zapis audytu

Inwentaryzacja poprzedniego etapu pozostaje zachowana poniżej. **B1 rozwiązany**:
`checkpoint_sha256` jest aliasem sumy skompresowanego `files.tar.gz`, a suma
`CHECKPOINT.md` ma osobne pole. Porównanie różnych obiektów w poprzednim etapie
nie było dowodem niezgodności delty. Prywatne archiwa pozostają poza dostępem
integratora; przekazanie administratora umożliwia sprawdzenie plików bez ich odczytu.

Aktualne `team-receipts.json` (`codex-team-receipts/2`, 17:59:02 UTC) oraz
`integration-receipts.json` (`codex-team-integration-receipts/1`, 17:46:56 UTC)
wiążą task, bazę, locator checkpointu i sumę archiwum. Potwierdzono zgodność
bieżących HEAD, statusów, patchy i pełnych plików **3/3** z przekazaniem.
Status w receipt jest kodowany jako porcelain `-z` z pełną listą plików;
pierwsza własna próba porównała wariant z newline i została poprawiona po
sprawdzeniu bajtów. Zawartość delt nie zmieniła się od pierwszego audytu.

| Task | Aktualny identyfikator checkpointu | SHA-256 archiwum (dowód administratora) |
|---|---|---|
| `chatadhd-android-20261031` | `1793468762490422580` | `27b8006f968178006a6c5d9d2c471dc154467b6c7807c9302a8f45a1cd20f4ae` |
| `chatadhd-flows-20261031` | `1793468765048660599` | `f4ec9c1b124c90b8dc54cdb92c6530475acb710edadf3b52a332e7b3629dd40a` |
| `chatadhd-linux-20261031` | `1793468767805183216` | `144fc5fb7f317d9f02751f6ed78e496d4c80e4766c97e5dfa57a0d53215341e3` |

B3 rozwiązany: rzeczywisty staging dokładnie tego raportu i STATE przeszedł
po naprawie profilu integratora. B2 nadal wymaga własnego buildu/testów Flow. W tym
commicie dokumentacyjnym przyjętych delt produktu: **0**. Raporty Android/Linux
pozostają osobnymi odbiorami i nie blokują poprawnie sprawdzonego Flow.

Nowsze zlecenie zapewnia wyłączną rezerwację jednego ciężkiego buildu `-j2`
w nowym własnym katalogu. Limit procesu 3600 s od wznowienia; checkpoint
najpóźniej przed 50. minutą. Historyczny limit 25 minut i zależność od konwersji
VM zostały zastąpione tym zleceniem. Publikacja i wdrożenie pozostają poza zakresem.

Zasady nadrzędne: §0, rewizja 1 z 2026-10-31, wskazana przez właściciela.
Prywatnego źródła nie odczytywano ani nie importowano. W odbiorze obowiązują
istniejące R42 i model Loom: polityki w danych, inwarianty w kodzie, zachowanie
źródeł, oddzielne klasy dowodu; logi bez sekretów i prywatnych treści.
Sprawdzenie wielopoziomowego logowania i wyłącznika wdrożeniowego jest
ograniczone do audytu istniejących granic, bez niezwiązanego refaktoru.

## Historyczny wynik etapu 17:30–17:38 UTC

Ukończono inwentaryzację i testy zakresowe, przyjętych delt/commitów: 0.
Odbiór odłożono wtedy na B1/B2, a zapis commita zatrzymał B3. Poniższe
lokatory i oceny opisują ten wcześniejszy etap; aktualny kontrakt jest powyżej.

## Punkt bazowy i wznowienie

- Baza wskazana przez właściciela i początkowy HEAD integratora:
  `9533d68e6cf6e133e0fdcd5d5fcf7ee85654da08`; lokalny obiekt ma rodzica
  `b3263b05d9e10c433f1f2ff45ddc74d9e9c7a8a5` i tytuł
  `Read PDF text attachments and retain files across chat turns [skip ci]`.
- Gałąź: `team/chatadhd-integrator`; CWD:
  `/var/lib/codex-team/worktrees/chatadhd-integrator`.
  Po wznowieniu 17:30 UTC status był pusty, lokalnego `CHECKPOINT.md` nie było.
  Nie było zapisanych zmian do ponownego zastosowania.
- Odczytano AGENTS, STATE, wymagania właściciela, model pojęciowy, CLAUDE,
  README Loom, bieżące dwa wpisy QUEUE_S1 i całą macierz
  [przepływów dokumentów](CHAT_DOCUMENT_FLOWS_2026-10-31.md).
  Wyników historycznych PDF/web/native nie przeniesiono na obecny odbiór.
- `git worktree list --porcelain` potwierdza cztery worktree ChatADHD
  (integrator, android, flow, linux), każdy na tej bazie, oraz repozytorium bare.
  Nie odczytywano historii prywatnej gałęzi ani nie zmieniano cudzych referencji.
  Nie wykonywano fetch/push, merge, PR ani wdrożenia.

## Manifest i gotowość

Źródło tylko do odczytu: `/var/lib/codex-team/context/team-receipts.json`.
Rzeczywisty format to obiekt JSON z `at` i tablicą `tasks`, bez pola wersji
schematu i bez opisu znaczenia sumy checkpointu. Rekord zawiera `id`, `slot`,
`state`, `updated_at`, `error_class`, `reason`, `waiting_on`, `session_id`,
`attempt`, `failure_count`, `checkpoint`, `checkpoint_error`, `worktree`,
`base_head`, `checkpoint_sha256`. Manifest zawiera także integratora i zadanie
smoke; smoke ma inną bazę i nie jest kandydatem tej fali. Manifest nie podaje
rezerwacji pełnej bramki.

Data manifestu: `2026-10-31T17:30:20.821186+00:00`.

| Tor / task | Stan i aktualizacja UTC | Gałąź / worktree | Lokalny checkpoint: mtime UTC |
|---|---|---|---|
| Android / `chatadhd-android-20261031` | `completed`, 17:01:14 | `team/chatadhd-android`, `/var/lib/codex-team/worktrees/chatadhd-android` | `CHECKPOINT.md`, 17:01:07 |
| Flow / `chatadhd-flows-20261031` | `completed`, 17:15:01 | `team/chatadhd-flow`, `/var/lib/codex-team/worktrees/chatadhd-flow` | `CHECKPOINT.md`, 17:14:36 |
| Linux / `chatadhd-linux-20261031` | `completed`, 17:05:59 | `team/chatadhd-linux`, `/var/lib/codex-team/worktrees/chatadhd-linux` | `CHECKPOINT.md`, 17:05:37 |

Dla każdego z tych rekordów `base_head`, rzeczywisty HEAD oraz base/result
w lokalnym checkpoincie to pełny SHA `9533d68e6cf6e133e0fdcd5d5fcf7ee85654da08`.
Checkpointy jawnie przekazują niezacommitowane pliki; brak commita wykonawcy
nie jest blokerem. `completed` ustala zakończenie wykonania, nie poprawność zmian.

**B1 — niepotwierdzone powiązanie checkpointu z manifestem.** Bezpośredni
`stat` każdej z trzech ścieżek `checkpoint` z manifestu zwraca `EACCES`
(kod 13, Permission denied). Wstępne `Path.exists()` zwracało false; odczyt
błędu wykazał brak dostępu, więc nie stwierdzamy braku plików. Nie próbowano
obejść ochrony ani zaglądać do prywatnej kolejki. Hashe lokalnych `CHECKPOINT.md`
różnią się od pól `checkpoint_sha256`. Nie wiadomo, czy pole dotyczy tych
bajtów, czy opakowania administracyjnego; różnica nie dowodzi uszkodzenia.
Do odbioru potrzebne jest wyjaśnienie zakresu sumy albo czytelne przekazanie
snapshotu z sumami plików. Sama stabilność worktree nie rozstrzyga tej relacji.

| Task | `checkpoint_sha256` w manifeście | SHA-256 lokalnego `CHECKPOINT.md` |
|---|---|---|
| `chatadhd-android-20261031` | `de32cb5b52936a14e1dd4ca09a1f8b83010edb6f7e68ce49a3dfc89bb4b0f17b` | `dee1bc6aeead923e29aabe6050ea70b1554531538a34dcfffc0d9ad5db7989d5` |
| `chatadhd-flows-20261031` | `16ccd159ebc9d82c7866f5ea200f169335175a82bf2c54f655cefefed8c5aae9` | `6b97d58dd447cd69a77897aaf82bbc006e1c6eb83e9d8cb4a7afbb306880c33e` |
| `chatadhd-linux-20261031` | `b64c5938ceb095cc8812965ca3f572389d6b5b4ec980dd7fbecd541f67540552` | `cb4094d1a2859c37cfbfabe79fa58150f030360358aaac2060bdb12ea614d1b6` |

## Rzeczywisty zakres kandydatów i własność plików

| Tor | Zmienione śledzone pliki | Nowe pliki przejrzane w całości | Decyzja |
|---|---|---|---|
| Android | Brak | `docs/reports/ANDROID_LAUNCHER_RESEARCH_2026-10-31.md` | Odłożony raport: B1. P1/HomeActivity jest propozycją, brak implementacji. |
| Flow | `docs/STATE.md`, `loom/CMakeLists.txt`, `loom/include/loom/config.h`, `loom/include/loom/loom.h`, `loom/server/src/app.cpp`, `loom/server/src/guest_source.h`, `loom/tests/test_config.cpp` | `docs/reports/CHAT_CREDENTIAL_BOUNDARY_2026-10-31.md`, `loom/server/tests/test_capi_credential_override.py`, `loom/server/tests/test_server_guest_credentials.py` | Odłożona poprawka i raport: B1+B2. |
| Linux | `docs/STATE.md` | `docs/reports/LINUX_DESKTOP_RESEARCH_2026-10-31.md`, `tools/research/linux_desktop/README.md`, `org.example.ChatADHDProbe.desktop`, `test_contract.py` (ostatnie dwa w tym samym katalogu) | Odłożony pakiet badawczy: B1. L1/adapter produktu jest propozycją. |

Flow odłącza pożyczony overlay po własnym wyborze źródła, kontroluje odtworzony
runtime przed podaniem klucza puli i usuwa odpowiedni wpis overlay po jawnym
zapisie sekretu. `loom.h` zmienia komentarz istniejącej funkcji, bez nowego
symbolu; CMake dodaje dwa testy. Przeczytano pełny patch i oba nowe testy.
Szerszy audyt nadpisań tras i konkurencyjnych żądań pozostaje otwarty.

Rzeczywisty konflikt własności to **`docs/STATE.md` (Flow, Linux, integrator)**:
obie delty dopisują wpis przed tą samą pierwszą linią. Żadnej nie nałożono;
integrator zapisuje własny stan odbioru. Wpis Flow odwołuje się do prywatnego
checkpointu — przy przyszłym odbiorze stan publiczny powinien wskazywać
publiczny raport i własny commit integratora. Innych wspólnych ścieżek bieżących
delt nie wykryto. Przyszłe propozycje Android/Linux mogą współdzielić rejestry,
pakiety akcji/zdolności i pulpit webu; nie są jeszcze konfliktem kodu tej fali.

Poza pakietem produktu pozostają wszystkie `CHECKPOINT.md`, katalogi buildów,
prywatne logi i administracyjne kopie manifestu. Linux ma także
`docs/reports/linux-desktop-handoff-2026-10-31/{HEAD.diff,untracked.txt}`:
odczytano je; pierwszy powiela dokładnie patch STATE, drugi listę przekazania.
Nie są przeznaczone do commita produktu. Nie kopiowano plików `.git`.
W przejrzanych patchach i ośmiu nowych plikach kandydujących nie stwierdzono
prawdziwych kluczy ani danych produkcyjnych; testy Flow używają jawnych
fikcyjnych znaczników i loopback. Nie czytano magazynów sekretów, `*.env`,
`owner_init.json` ani holdoutów. To kontrola wymienionego zakresu, nie globalny
audyt wszystkich danych repozytorium.

## Identyfikacja odczytanej delty i stabilność

W cudzych worktree wykonywano wyłącznie odczyt: `git status --porcelain=v1`,
`git rev-parse HEAD`, `git ls-files --others --exclude-standard` oraz
`git diff --binary --no-ext-diff HEAD -- <jawna lista z tabeli wyżej>`.
Git działał z `GIT_OPTIONAL_LOCKS=0`. Dla Androida lista wskazuje nowy raport,
więc patch śledzonych plików jest pusty; nie oznacza to braku wyniku badania.

| Task | SHA-256 patcha śledzonych plików | Bajty |
|---|---|---|
| `chatadhd-android-20261031` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | 0 |
| `chatadhd-flows-20261031` | `4b6b2112d7bf3d73c41137266ddd5bca3a78838f760d90a5ed995e41ed901ed6` | 12291 |
| `chatadhd-linux-20261031` | `d58c1f3aacb5546fe625e7b8898ebdaa9a69921331431aad8f80a8d6c23a54e5` | 1608 |

Poniższe sumy identyfikują pełne odczytane pliki, w tym nowe pliki niewidoczne
w zwykłym diffie. Nazwa toru wskazuje worktree z tabeli gotowości.

| Tor | Plik | SHA-256 |
|---|---|---|
| android | `docs/reports/ANDROID_LAUNCHER_RESEARCH_2026-10-31.md` | `2ba3f7966bf9cd1f0340fcbc47f26c3a796e2c888435a78c11c9a6aed892478d` |
| flow | `docs/STATE.md` | `22ba8466b8847ddcd1ced448c90c64713134eff27b6283ea2992c07cdaa12815` |
| flow | `loom/CMakeLists.txt` | `83028d5fe6dbb1c6f0524dc3f63093888cf1b63bb7103a3418e3fc7c3d34e51e` |
| flow | `loom/include/loom/config.h` | `dc18e48907d575cdd8087eb7eb41bd743cbfc455b3ade217f10825d6e9b3adcf` |
| flow | `loom/include/loom/loom.h` | `9858413f17baa9f8608b52ada6f3f570e91e596c5b0331d0ffe31e9579466b08` |
| flow | `loom/server/src/app.cpp` | `624d0e88d6a08efbbcf60ab5c94ca710481dbd7db0a1122aada5709e8d75ec51` |
| flow | `loom/server/src/guest_source.h` | `83f0e6df75e888274fce610e934e9a59c6a75ee109d191296df8cdc794c462dd` |
| flow | `loom/tests/test_config.cpp` | `0821af70b52ee0df23946c009828f252ba1800c3c33885d7130b893212177046` |
| flow | `docs/reports/CHAT_CREDENTIAL_BOUNDARY_2026-10-31.md` | `7ac6c4035a1bfad223eb0fabcc3ae6fdda89e38f297b9044abf16dfbf10fb9e8` |
| flow | `loom/server/tests/test_capi_credential_override.py` | `5a728264ba01ca9dac475106684fa4d7ab91c0effd1fcdb146a7b4283ccf19b6` |
| flow | `loom/server/tests/test_server_guest_credentials.py` | `6c58826a1ac0351d4c794f7b22ce58a2ab4b0a31b50922d7f37cb6f14341265f` |
| linux | `docs/STATE.md` | `d7752d7a7c1c3914f13696bd9a605dfdb6213425189eb0701284cdb7269b34f4` |
| linux | `docs/reports/LINUX_DESKTOP_RESEARCH_2026-10-31.md` | `614ea4e0d94f356cbe5e67daa3e78e3a449a585957731b55eb29a97c7c1e5f01` |
| linux | `tools/research/linux_desktop/README.md` | `8eece1ff4ce45d09475f5b877ef37297076b12fe7db2c89afe7fa03fc8ed25bc` |
| linux | `tools/research/linux_desktop/org.example.ChatADHDProbe.desktop` | `66deb6a90c0d14f3dc88be3ea9170c0da0aee119dc97e5874e2a524c3fb4f5a2` |
| linux | `tools/research/linux_desktop/test_contract.py` | `936a91a6c5a40f309013e72d62d309870a3e850894e220b2f7d5d23c7e2e1cb1` |

Ponowny odczyt po przeglądzie: `2026-10-31T17:33:52.477509+00:00`. Dla **3/3** torów zgodne
pozostały rekord manifestu, HEAD, status, lista nieśledzonych plików, patch
i sumy wszystkich wymienionych plików oraz checkpointu. Manifest przed/po:
SHA-256 `44550fc52cbc6cba33df138b391f942c8fb6c863556bb3d8f1a9a68537cff71b`.
Brak wykrytej zmiany w trakcie odczytu. Kopie robocze i notatki audytu są
wyłącznie w ignorowanym `.integration-audit-20261031/` integratora.

## Weryfikacja i granice dowodu

| Tor / kontrola | Dowód wykonawcy | Własny wynik integratora |
|---|---|---|
| Android | Checkpoint opisuje parser XML: 3 aktywności, 1 filtr MAIN+LAUNCHER, brak HOME/DEFAULT/queries/aliasów/receiverów; słownik 13 API w 41 plikach, 0 dopasowań. | Powtórzono na bazie: **2/2 kontroli statycznych PASS** (XML i przeszukanie); 41 plików / 0 dopasowań. Brak APK/JNI/emulatora/telefonu. |
| Linux | Raport/checkpoint: 8/8 PASS, 0 skip; wcześniejsze błędne oczekiwanie odrzucenia wpisu bez Exec opisane jawnie. | Dokładna odczytana kopia skryptu i fixture we własnym CWD: **8/8 PASS, 0 skip**, 0,044 s. Walidator Desktop Entry, GIO i GLib, pakiet OpenURI tylko w pamięci. Bez D-Bus, uruchomienia Exec lub GUI. |
| Flow HTTP | Odczyt `fixed-http.log`: 5/5 przypadków, jeden wpis CTest PASS, 23,15 s. | **Niewykonany** niezależny HTTP; odczyt logu nie jest jego powtórzeniem. |
| Flow C API | `fixed-capi-verbose.log`: 2/2 przypadki, jeden wpis CTest PASS, 3,45 s. | **Niewykonany** niezależny runtime C API. |
| Flow native | `native-tests.log`: 23/23 przypadki, 6567/6567 asercji, 0 skip; mały runner test_config + test_capi. | **Niewykonany** niezależny native; nie jest to pełny loom_tests. |
| Flow ABI | `abi.log`: 3/3 PASS, eksporty/sygnatury/FFI. | **Niewykonany** niezależny test biblioteki. |
| Flow negatywy | `baseline-http-contract-corrected.log`: 5/5 FAIL, `baseline-capi.log`: 2/2 FAIL. Odczytano negatywy poświadczeń; wcześniejszy błąd 200/201 harnessu jest oddzielony. | Nie uruchamiano bazowych ani cudzych binariów. |
| Zastosowalność | — | `git apply --check` na jawnych patchach Flow i Linux: **2/2 PASS** przed zmianą własnego STATE; bez zastosowania. |
| Nowe pliki | — | **8/8** kontroli UTF-8/newline/białych znaków/hash PASS; **3/3** pliki Python parsowane przez AST. AST nie jest wykonaniem scenariuszy. |

Komenda własnej próby Linux (ze swojego CWD):

```sh
PYTHONDONTWRITEBYTECODE=1 python3 .integration-audit-20261031/linux/tools/research/linux_desktop/test_contract.py
```

Sumy odczytanych binariów Flow zgadzają się z raportem wykonawcy:
serwer `36bc0c4b3784844512624f6fb0d6707a018337e912071ea9624a3efcf6d96705`,
biblioteka `3f342ca6a9bd5dc9d74294adda9a0c8c7e1cb8f5f07b5f451e0334d010a334a7`.
Odczytano także log końcowej kompilacji serwera. Binariów nie uruchamiano
ani nie kopiowano. Logi pozostały u wykonawcy; lokalna notatka integratora
zawiera tylko sumy i rozmiary dowodów.

**B2 — brak niezależnego wyniku poprawki Flow.** Nie zbudowano i nie uruchomiono
tej delty w worktree integratora. Przyjęcie kodu wymaga własnych testów na
odebranych bajtach. Brak rezerwacji pełnej bramki; w tym przyroście wykonano
wyłącznie zakres powyżej, bez budowania jądra i korzystania ze wspólnych buildów.
Zewnętrzne źródła raportów badawczych przeczytano jako twierdzenia wykonawców;
nie powtarzano ich weryfikacji WWW w tym audycie.

Rozdzielenie stanów: Flow ma lokalną implementację i wyniki wykonawcy;
Android ma raport/projekt; Linux ma raport/próbę bibliotek. Żaden z nich
nie otrzymał tutaj statusu **integrated**, **deployed** ani **GUI accepted**.
PDF HTTP, skany/OCR, DOCX, graph_packet, eksport bajtów oraz D7–D10 i W11a-2
pozostają otwarte zgodnie z bieżącą kolejką.

## Kolejność i checklista odbioru

1. Wyjaśnić B1 poprzez publiczny dla integratora opis sumy lub czytelne
   przekazanie snapshotu. Nie zmieniać uprawnień i nie wymagać commita wykonawcy.
2. Flow ma pierwszeństwo merytoryczne ze względu na granicę poświadczeń.
   Przed przyjęciem przeczytać kontekst tras/overlay, sprawdzić miejsce i czas,
   zbudować wyłącznie we własnym nowym katalogu i powtórzyć HTTP 5, C API 2,
   native config/C API oraz ABI. Pełną bramkę wykonać dopiero z rezerwacją;
   stan zakresowych testów i pełnej bramki raportować oddzielnie.
3. Raport Androida oraz raport/próbę Linux można odbierać jako osobne małe
   pakiety po B1, bez zależności od pełnego buildu Flow. Przeczytać oficjalne
   kontrakty przy zatwierdzaniu wniosków; wykonać własne kontrole ich zakresu.
   P1 i L1 wymagają odrębnego zlecenia — nie startują z tego raportu.
4. Przed każdym odbiorem ponownie sprawdzić manifest/status/HEAD i hashe;
   różnica względem odczytanego snapshotu oznacza odłożenie. Użyć jawnej listy
   plików, obejrzeć nowe pliki, wykluczyć checkpointy/artefakty administracyjne.
5. W swoim CWD: `git apply --check` jawnego patcha, zastosowanie dopiero po
   pozytywnym odbiorze, kopia tylko obejrzanych nowych plików, własny diff,
   adekwatny rzeczywisty test. STATE redagować wspólnie z wynikiem integracji,
   zachowując wcześniejsze wpisy. Nie rozwiązywać konfliktu odrzuceniem zmian.
6. Mały lokalny commit `[skip ci]` z task/base/hash delty; jawne `git add`
   wyłącznie przyjętych plików. Zapisać result_head i następny krok w lokalnym
   CHECKPOINT. Bez push/PR/main/wdrożenia oraz bez automatycznego następnego cyklu.

Na koniec tego przyrostu zapisano własny raport i datowany wpis STATE.
B1 oraz B2 są punktami wznowienia; żadna cudza delta nie została zastosowana.

## Zapis przyrostu — bloker B3

Jedyna próba stagingu jawnej listy dwóch plików:

```sh
git add -- docs/reports/TEAM_INTEGRATION_2026-10-31.md docs/STATE.md
```

zakończyła się kodem 128: `Unable to create
'/var/lib/codex-team/repos/chatadhd.git/worktrees/chatadhd-integrator/index.lock':
Read-only file system`. Indeks pozostał bez zmian; commit nie powstał.
Nie ponawiano operacji, nie zmieniano praw, konfiguracji ani położenia indeksu
i nie próbowano obejść ograniczenia. Potrzebny jest zapis do własnych metadanych
Git integratora zapewniony przez administratora. Gotowy komunikat z `[skip ci]`,
task/base/hash znajduje się lokalnie w `.integration-audit-20261031/commit-message.txt`.
Przed późniejszym stagingiem trzeba odczytać CHECKPOINT, status i HEAD; dodawać
wyłącznie powyższe dwa pliki. CHECKPOINT i prywatne notatki pozostają poza commitem.
