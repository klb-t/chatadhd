# Lokalny pakiet i akceptacja stagingu

Artefakt zachowuje format `deploy/install.sh --from-tarball`: jeden katalog
`chatadhd-server-linux-x64-<wersja>/`, `bin/loom-server`, `web/`, `VERSION`
i `BUILD_INFO`. Komponent CMake `runtime` dodaje `lib/libloom.so*` oraz
`share/loom/docx_basic/{reader.py,runtime.json}`. `MANIFEST.json` wiąże pliki
z SHA źródeł, typem kompilacji, opcjami CMake i zależnościami systemowymi.
Dane kont, konfiguracja i bazy nie należą do artefaktu.

Po własnej kompilacji **tylko** `loom-server loom -j2` można zebrać komponent:

```sh
cmake --install "$OWN_BUILD" --prefix "$NEW_STAGE" --component runtime
```

Do nowego stagingu należy dodać własny, zweryfikowany `web/`, `VERSION`,
`BUILD_INFO` i manifest. Nie używać `install.sh --package` do zebrania tego
istniejącego buildu: ta opcja uruchamia osobną kompilację oraz instalację
zależności web, a obecny `make_package` zbiera wyłącznie serwer i web.
Nie dołącza automatycznie biblioteki i helpera DOCX z komponentu runtime.
Odebrana ścieżka to przygotowane kompletne archiwum i `--from-tarball`. Zachować domyślne flagi Release i rzeczywisty typ z CMakeCache;
Debug ma mieć jawne oznaczenie w nazwie archiwum i manifeście.

Helper DOCX korzysta z absolutnego interpretera zapisanego w `runtime.json`.
Python jest zależnością, nie dołączoną kopią interpretera. Helpery OCR i URL
są osadzone w kodzie natywnym; nadal wymagają systemowego Pythona. OCR wymaga
Tesseract i danych językowych, PDF — Poppler-GLib. `ldd` samego serwera nie
wykryje wszystkich tych zależności. Lokalny build z systemowym SQLite/GCC/glibc
nie jest dowodem przenośności oficjalnego pakietu Ubuntu ani zgodności ABI
na innym hoście.

## Krótki test nowego artefaktu

Z katalogu głównego własnego checkoutu, po ukończeniu kompilacji, z nowymi
katalogami pod tym checkoutem:

```sh
cc -shared -fPIC -O2 -Wall -Wextra -Werror \
  loom/tests/url_share/network_fixture.c -ldl -o "$OWN_NETWORK_FIXTURE"
python3 -B deploy/staging-smoke.py \
  --package "$OWN_STAGE" --work-dir "$NEW_TEST_DIR" \
  --network-fixture "$OWN_NETWORK_FIXTURE"
```

Runner sprawdza manifest, kopiuje pakiet do nowej lokalizacji, a następnie
odmawia dostępu do oryginalnego pakietu i checkoutu przez Landlock. Pakiet
relokowany jest tylko do odczytu; stan testów i konfiguracje powstają obok.
Osiem przypadków obejmuje PL/dialog/restart/błąd, granicę provider/BYOK,
PDF PL i ostrzeżenia, OCR i kontekst, domyślny DOCX i odmowę, bibliotekę DOCX,
zaimplementowany URL/share oraz odmowę adresu prywatnego. Odpowiedzi dostawców
i dokumenty są syntetyczne. Testowy adapter DNS/connect URL dopuszcza tylko
własny loopback i nie wchodzi do pakietu. Własny eksport JSON ChatADHD nie
dowodzi adaptera udostępnień zewnętrznego dostawcy; test HTTP nie dowodzi TLS.

`test-command.json`, `work/smoke-result.json` i `test-result.json` zachowują
wykonane polecenie, wyniki i kontrolę niezmienności pakietu. Brak Landlock,
brak zależności lub niezgodność manifestu oznaczają błąd, nie pominięty PASS.
Testy nie wykonują przeglądarki, prawdziwego modelu, migracji produkcyjnych,
pełnego repo gate ani wdrożenia. Nowy wynik dotyczy dokładnego hasha artefaktu.

## Istniejący kontrakt przełączenia

`install.sh` przechowuje wersje jako `<prefix>/<wersja>` i wybiera je symlinkiem
`current`; nie tworzymy innego deployera. Lokalny dry-run musi zawsze przekazać
własne `--prefix`, `--data-dir` oraz `--no-deps --no-build --no-systemd --no-caddy`.
Archiwum i sąsiedni `.sha256` muszą pochodzić z tego samego odbioru.

1. Zachować poprzedni cel `current` i pozostawić jego katalog. Kandydat musi
   mieć nową nazwę; instalator przy ponownej instalacji tej samej wersji
   nadpisuje jej `bin/web`, więc nie używać tego do zmiany przyjętej wersji.
2. Przygotować kandydata przez `--from-tarball PLIK --version NOWA --no-switch`.
   Sprawdzić manifest, zależności i brak zmiany danych/konfiguracji oraz `current`.
3. Wybrać już przygotowaną wersję istniejącym `--rollback NOWA` (ten sam
   selektor katalogu wersji). W dry-runie proces zatrzymuje/uruchamia wyłącznie
   harness. Instalator używa `ln -sfn`; nie deklarujemy atomowej transakcji
   przełączenia procesu, symlinku i bazy ani zerowego przestoju.
4. Sprawdzić health, treść web oraz odczyt utrwalonego stanu. W razie niepowodzenia
   `--rollback POPRZEDNIA` i ponowny start poprzedniego procesu, potem ten sam
   test odczytu. Zachować oba katalogi i dowody.

Dla przyszłego rzeczywistego przełączenia operator musi najpierw uzyskać
oddzielną zgodę, ustalić faktyczną działającą wersję, zależności i zgodność
schematu oraz zabezpieczyć spójny snapshot danych/konfiguracji przy zatrzymanych
zapisach. Sam powrót symlinku nie cofa migracji bazy. Istniejący kontrakt usługi
z `install.sh` określa start/restart; nie uruchomiono go na hoście podczas
lokalnego odbioru. Dry-run z własnym Debug jako poprzednikiem nie dowodzi
odwracalności nieznanej wersji produkcyjnej. Szczegółowy wynik, hashe i jawne
ograniczenia konkretnego przyrostu są w datowanym raporcie integratora.

## Narzędzie tar w tym sandboxie

Odbiór2026-10-31/11-01 wykazał `openat2(..., RESOLVE_BENEATH) = -1 ENOSYS`
w GNU tar1.35 przy ekstrakcji własnego archiwum. Nie zmieniano zabezpieczeń
ani narzędzi hosta. Dostępny BusyBox1.37.0 przeszedł próbkę oraz rzeczywiste
verify/install/rollback. Lokalny, ignorowany wrapper miał wyłącznie treść:

```sh
#!/bin/sh
exec /usr/bin/busybox tar "$@"
```

Jego katalog dodano do PATH **tylko testowanych procesów**. Instalator
otrzymał jawne `--version`, a archiwa i ścieżki/dowiązania były wcześniej
sprawdzone. Nie używać opcji zdejmujących ochronę ścieżek. Zgodność tar
na docelowym hoście trzeba sprawdzić przed przełączeniem; udany lokalny
wariant BusyBox nie jest dowodem działania domyślnego GNU tar.
