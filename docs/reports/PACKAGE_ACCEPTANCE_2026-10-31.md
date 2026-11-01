# Pakiet i lokalna akceptacja przed wydaniem — 2026-10-31 / 2026-11-01

Wynik: **własny Release zbudowany i zakresowo przyjęty w stagingu**.
Odbiór 2026-11-01T00:01:49.917686+00:00. Bez wdrożenia; nie jest to pełna bramka wydania.

## Baza i zakres

Baza źródeł: `e47079cf9f44b0b90654d21770bcfb24c4fd34cc`, drzewo `loom`
`985e2a1131709e793825409032daf88e5ce3e3e1`. Przyjęte P0, M1/M2/DOCX,
pakietowanie, G1 i PNG pozostają przodkami. Źródła aplikacji w `loom/` nie zostały
zmienione. Frontend/source hooki osobnego G1GUI nie są odbierane w tym przyroście.

Nowy katalog `.integration-release-20261031/build`: CMake Release, domyślne
`-O3 -DNDEBUG`, tylko `loom-server` i `loom`, `-j2`. Testy CMake i CLI wyłączone;
nie kompilowano `loom_tests`. Zachowany wcześniejszy Debug ma `-O0 -g0`.
Oba buildy korzystają z systemowego SQLite. Nowy Release włącza OpenSSL
jawnie; starszy Debug miał AUTO z wykrytym OpenSSL. To lokalny wariant,
nie deklaracja wykonania pełnego oficjalnego presetu release.

## Wykonany wynik i hashe

- Release `-O3 -DNDEBUG`, targety `loom-server loom -j2`: **PASS**, wybrane targety zakończone kodem 0.
- CTest `-j1`: **łącznie trzy zakresy odebrane**. Pierwszy przebieg: 1/3 zestawy
  (backend **8/8**, zero skip); GNU tar zablokował dwa pozostałe. Po ustaleniu
  przyczyny powtórzono wyłącznie te dwa: **2/2 PASS** z BusyBox tar 1.37.
- Checkout i oryginalny pakiet: **3/3 jawne odmowy odczytu** wewnątrz Landlock.
  Relokowany pakiet i oba katalogi wersji pozostały niezmienione.
- Istniejący `verify-package.sh`: SHA archiwum, zależności ELF,
  health, rzeczywisty web HTML i auth **PASS**.
- Istniejący `install.sh`: przygotowanie `--no-switch`, wybór kandydata,
  powrót do własnego Debug **PASS**. Dwa ponowne starty potwierdziły PL,
  zachowaną rozmowę i identyczne oryginalne bajty. Sam instalator nie zmienił
  danych/konfiguracji. Na końcu lokalny `current` wskazuje `baseline-e470-debug`.

Pakiet lokalny: `.integration-release-20261031/chatadhd-server-linux-x64-e47079cf-release-20261031.tar.gz`.
Wersja `e47079cf-release-20261031`. To nowy rzeczywisty Release, nie przemianowany Debug.

| Element | SHA256 |
|---|---|
| Archiwum Release | `fa5c71fc6bedb5ad82e6b93297571b7ba6309f5e18b434849983bf8bf883d744` |
| Serwer Release | `29d918d1d5ae030950061f22eb82ee93d21af52a6c3b5333258b1f7ea57dcb52` |
| libloom Release | `5d6d43e1d9a8622b81184f4a9d353c967a9d22cefd3b70927d9be0250a177fa9` |
| MANIFEST.json Release | `a566fa9df3abfd05acb84964481b96bcae17f079a2da757b61f961f92c3f2bf0` |
| Zbiór dowodów accepted-evidence.json | `608da12092563f3f7250f11ad8aa2fa581d8e257006e198ac4a804f4a0dde263` |

Dokładne argv/cwd, wyniki i hashe są w `.integration-release-20261031/`
(`gate-commands.json`, `gate-result.json`, `acceptance/test-command.json`,
`acceptance/work/smoke-result.json`, `switch-dryrun-v2/result.json`, `retry-result.json`, `gates-tool-compat.log`).
Parserowy wynik autora nie zastąpił żadnego z tych własnych testów.

## Artefakt i pochodzenie

Pakiet zachowuje kontrakt istniejącego `deploy/install.sh --from-tarball`.
Komponent CMake `runtime` dostarcza serwer, bibliotekę współdzieloną,
`share/loom/docx_basic/reader.py` i manifest interpretera. Dołączony własny
webdist ma **16/16** SHA zgodnych z odebranym buildem
`daf4f48412924737f260a42499818ea6bbeba513`; do e47079cf zmienił się tylko test
e2e, nie kod produktu web. Nie ponawiano buildu web ani poprzednich GUI.

`MANIFEST.json` w artefakcie zawiera SHA i rozmiary 24 plików payloadu
(w tym dowiązania biblioteki), opcje CMake, pochodzenie web, SHA osadzonych
helperów OCR/URL oraz zasobów runtime/text, wersje narzędzi i hashe wybranych
zależności systemowych; nie jest pełnym SBOM.
Sam manifest ma osobny SHA; całe archiwum ma sąsiedni plik `.sha256`.
Nie zawiera danych użytkowników, konfiguracji instancji, DB, test support,
checkpointów ani administracyjnych receipts.

Środowisko budowy: GCC 15.2.0, CMake 4.2.3, glibc 2.43, system SQLite 3.46.1,
OpenSSL 3.5.5. DOCX/URL/OCR korzystają z systemowego Python 3.14.4;
interpreter **nie jest dołączony**. Reader DOCX ma SHA
`e5c1e8f327e1d78d6201ff1e02d572631bcf63faa4323e53c7e86f485c0d53bd`.
OCR wymaga Tesseract 5.5.0 i danych pol/eng; PDF — Poppler-GLib.
Nie potwierdzono przenośności na inną dystrybucję/wersję bibliotek.
Źródłowy tryb `install.sh --package` nadal zbiera tylko serwer/web; nie jest
odebraną ścieżką komponentu runtime. Ten konkretny tar zawiera lib/helper
z `cmake --install --component runtime`, a instalator przyjmuje go przez
`--from-tarball`. Nie uruchamiano dodatkowego buildu tego trybu.

## Nowa bramka

Wykonano osiem krótkich przypadków syntetycznego backendu dla nowego
artefaktu: PL i dialog/restart/błąd, provider/BYOK, dwie strony PL PDF oraz
ostrzeżenie o stronie bez tekstu, OCR i kontekst, DOCX default/reopen/original
oraz odmowa uszkodzonego ZIP, osobno libloom/DOCX, zaimplementowany URL/share
i odmowa prywatnego adresu. Wynik: **8/8 PASS**, na relokowanym pakiecie Release.

Runner `deploy/staging-smoke.py` relokuje pakiet i uruchamia test przy odmowie
odczytu oryginalnego checkoutu/pakietu przez Landlock. Pakiet jest tylko do
odczytu, a stan testów powstaje obok. Wynik wymaga ośmiu przypadków bez skip,
zgodności manifestu oraz niezmienności payloadu po wykonaniu.

Dane pochodzą z jawnych repozytoryjnych fixture: polski PDF tekstowy i PDF
z pustą stroną, syntetyczny PNG z oczekiwanym OCR, generowany minimalny OOXML,
własny eksport JSON ChatADHD i lokalna strona URL. Dostawcy to liczące żądania
atrapy na loopback. Testowy adapter DNS/connect URL blokuje inne cele i nie
wchodzi do pakietu. Własny eksport nie dowodzi adaptera zewnętrznego share;
HTTP fixture nie dowodzi TLS. Nie użyto prawdziwego modelu ani poświadczeń.

Istniejący `verify-package.sh` ma teraz własny pusty profil testowy, z wyłączonymi
gośćmi i bez ścieżki kluczy; curl nie czyta domyślnej konfiguracji użytkownika.
Sprawdzono health, web HTML i auth. Dodatkowy lokalny
dry-run wywołuje istniejący instalator z wyłączonymi deps/systemd/Caddy.

## Przełączenie, powrót i ograniczenia

Procedura: [deploy/STAGING_ACCEPTANCE.md](../../deploy/STAGING_ACCEPTANCE.md).
Nowy katalog wersji, najpierw `--no-switch`, następnie istniejący selektor
`--rollback WERSJA`, kontrola odczytu, powrót do zapamiętanego poprzednika.
Nie nadpisywać istniejącej wersji. Dane/konfiguracja pozostają poza artefaktem.
`ln -sfn` nie stanowi atomowej transakcji procesu i bazy.

Punktem odniesienia dry-runu jest własny przyjęty Debug tego samego SHA,
a nie nieznana wersja produkcyjna. Sam powrót symlinku nie odwraca migracji;
przed prawdziwym przełączeniem wymagane są oddzielna zgoda, identyfikacja
rzeczywistego poprzednika i spójny snapshot danych przy zatrzymanych zapisach.
Nie wykonano żadnego działania na działającej instancji produkcyjnej.

GUI/mobile, pełny repo gate, sanitizery, rzeczywisty model, G1GUI i zgodność
ze schematem produkcyjnego poprzednika nie należą do nowego smoke. Historyczne
wyniki GUI zachowują swoje SHA i zakres. §0 rewizja 2 obowiązuje; globalny
wyłącznik/hierarchia logów nie zostały udowodnione tym odbiorem. Role labels,
raw503 i szersza polonizacja pozostają wcześniejszymi lukami. Brak push/CI/deploy.

## Zgodność narzędzia rozpakowującego — rzeczywisty wynik

Pierwszy CTest nie był pełnym PASS: staging8 przeszedł, ale verify-package
i dry-run zatrzymały się przy ekstrakcji. Strace jednej własnej próbki
potwierdził `openat2(…, RESOLVE_BENEATH) = -1 ENOSYS` (errno38) w GNU tar1.35
dla podkatalogu `chatadhd-server-linux-x64-e47079cf-release-20261031/`.
Pierwszy częściowy staging i logi zachowano. Nie jest to błąd backendu ani
naprawiona właściwość GNU tar; ten domyślny wariant pozostaje tu niedziałający.

Dostępny `/usr/bin/busybox`1.37.0 obsłużył wymagane opcje na osobnej próbce.
Dwa zatrzymane zakresy wykonano w nowych katalogach przez **te same** skrypty
install/verify, z lokalnym `compat-bin/tar` delegującym do `busybox tar` i PATH
ustawionym tylko dla tych procesów. Nie zmieniono systemowego tar, sandboxa,
ACL ani archiwów. Użyto wyłącznie własnych archiwów o sprawdzonym SHA i
wewnętrznych dowiązaniach. Nie wyłączano kontroli ścieżek tar żadną opcją.

Wynik drugiego CTest: **2/2 PASS,20,48s**. Pierwszy staging8: **8/8 PASS**,
bez ponownego uruchomienia. Narzędzie ekstrakcji jest zatem jawnym warunkiem
lokalnego dry-runu. Nie deklarować udanego default-GNU-tar deploy ani
przenośności na niezweryfikowany host. To nie drugi deployer: wywołany został
istniejący `deploy/install.sh` z no-deps/no-systemd/no-caddy.

## Diagnostyka i dalszy krok

Build zakończył się kodem 0, ale log zawiera 19 diagnostyk `warning:`;
nie ukrywano ich zmianą flag. Pełny log zachowano i zahashowano.
Pierwsza próba zbierania Debug zatrzymała się przed zapisem stagingu,
ponieważ preflight porównywał także zmieniony test e2e. Po sprawdzeniu
diffu wyłączono z porównania wyłącznie testy; 16/16 bajtowych hashy webdist
pozostało warunkiem. Nie zmieniono źródeł aplikacji ani bramki funkcjonalnej.

Next: osobny odbiór G1GUI przez administratora i dopiero odrębna decyzja
wydaniowa po sprawdzeniu rzeczywistego środowiska/poprzednika. Nie uruchomiono
następnego cyklu. Historyczne Debug i delty zachowano.

## Wykonane polecenia

```sh
cmake -S loom -B .integration-release-20261031/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DLOOM_BUILD_TESTS=OFF -DLOOM_BUILD_CLI=OFF \
  -DLOOM_BUILD_SERVER=ON -DLOOM_SHARED=ON -DLOOM_WERROR=ON \
  -DLOOM_WITH_OPENSSL=ON -DLOOM_USE_SYSTEM_SQLITE=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=/usr/bin/env \
  -DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/env
cmake --build .integration-release-20261031/build --target loom-server loom -j2
python3 -B .integration-release-20261031/assemble.py \
  --build .integration-release-20261031/build --kind Release
cc -shared -fPIC -O2 -Wall -Wextra -Werror \
  loom/tests/url_share/network_fixture.c -ldl \
  -o .integration-release-20261031/network_fixture.so
python3 -B .integration-release-20261031/run_gates.py \
  .integration-release-20261031/release-package.json
python3 -B .integration-release-20261031/run_retry.py
```

Pierwszy runner wykonał `ctest --test-dir .integration-release-20261031/ctest
-j1 -V --output-on-failure` (backend PASS, dwa ENOSYS). Drugi użył
`ctest-tool-compat` i tylko dwóch wcześniej zatrzymanych testów. Zapisane
argv instalatora i symlinku są w `switch-dryrun-v2/commands.json`; nie
należy ponawiać gotowych operacji przez samo odtworzenie powyższego bloku.
