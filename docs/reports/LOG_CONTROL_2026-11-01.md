# Wyłącznik loggera deweloperskiego — 2026-11-01

Baza: `6cfd9a40052859c4efd3333655def3a41a0aebc5`. Mała delta odebrana po
audycie administratora i własnej kontroli stabilności25/25 oraz plików8/8,
nałożona raz do głównego worktree. **Implemented/tested zakresowo, integrated
lokalnie; niewdrożona**. Patch źródłowy
`092c3d6f8ecbffc4e6d1a70f9ca34b9c6b88e77a19f88513d513799330622c0f`.
Przyjęty immutable pakiet `8b2c0e68-release-g1gui-20261101` nie zawiera tej zmiany
i nie został przebudowany ani przemianowany. Kanon §0, rewizja 2.

## Kontrakt

Przed uruchomieniem nowego, przyszłego binarium ustaw `LOOM_LOG_ENABLED=0`.
`loom::log` nie przyjmuje wtedy nowych rekordów do ringu, stderr ani dodatkowych
sinków. Dotyczy wszystkich poziomów, w tym Error. Nazwa i interpretacja są
kontraktem mechanizmu w kodzie, poza swobodnym profilem `util`:

- zmienna nieobecna lub dokładnie `1`: pozwala na logowanie, jak wcześniej;
- `0`, pusty lub inny podany tekst: wyłącza (fail-closed);
- wartość pobierana raz przy pierwszym użyciu bramki w procesie; ustawiać przed
  startem, bez równoległego modyfikowania środowiska;
- `set_enabled(false)` wyłącza również w trakcie pracy; `set_enabled(true)`
  ponownie włącza wyłącznie, gdy pozwala na to konfiguracja startowa;
- `set_level`, `set_stderr`, rejestracja callbacku i caller-owned profil nie
  omijają bramki. Dawny próg poziomu i `LOOM_LOG_STDERR=0` zachowują znaczenie.

Wyłączenie nie usuwa już zgromadzonego ringu. `recent` nadal go udostępnia;
`clear_recent` pozostaje osobną jawną operacją. Domyślne wyłączenie od startu
zapobiega jego napełnieniu przez logger. Nie cofa rekordów odebranych przez sink.

Atomowa bramka jest kontrolą dopuszczania, **nie drain barrier ani transakcją
obejmującą wszystkie sinki**. Rekord dopuszczony wcześniej może zakończyć zapis
lub callback po powrocie z wyłączenia. Aby wymagać całkowitej ciszy, zatrzymać
producentów i poczekać na ich zakończenie. Callbacki pozostają poza blokadami;
callback może wyłączyć logger bez deadlocku. Pozostaje istniejący kontrakt
thread-safe callbacków — wiele wątków może wywoływać jeden sink.

## Przejrzana ścieżka i granice

Wejścia: bezpośrednie `write`, profilowane `write_checked` oraz szablony
`debug/info/warn/error` w `log.h` (nie są to makra). Makra `LOOM_LOG_*` w C ABI
są stałymi poziomów. Publikacja w `src/util/log.cpp` jest wspólna dla ringu,
stderr i `add_sink`; C API `loom_set_log_sink` instaluje właśnie taki callback,
a JNI kieruje go do logcat. Faktycznie uruchomiono callback C++, nie JNI/Android.
Nie dodano symbolu C ABI. Nowe funkcje C++ pozostają zgodne ze starymi wejściami.

Nieobjęte ujścia, pozostawione bez zmian:

- `server/src/main.cpp`: argumenty/usage, odmowa niebezpiecznego bindu,
  niepowodzenie inicjalizacji, start/stop oraz komunikaty CLI kont;
- `server/src/app.cpp`: błędy otwarcia kont/guest, konfiguracji, budżetu/fallbacku
  i obsługi nieoczekiwanego wyjątku; handler nadal zwraca HTTP 500;
- `include/loom/phase_timing.h`: niezależny `PhaseTimer`, `fputs(stderr)`,
  sterowany `LOOM_PHASE_TIMING` (domyślnie wyłączony). Ta delta go nie obejmuje;
- bezpośrednie stdio helperów/interpreterów/bibliotek oraz konsola przeglądarki
  i niezależne logi platformowe. Nie przeprowadzono ich pełnego audytu.

`httplib::set_logger` w `app.cpp` kończy też pomiar i guest admission; nie
wyłączono tego callbacku, ochrony kont, HTTP błędów ani trwałych danych audytu.
Przegląd Accounts/auth nie wykazał osobnego security-audit sinka w `loom::log`.
Bramka obejmuje jednak diagnostykę błędów `loom::log::error`, m.in. wyjątki C API;
nie zmienia zwracanych błędów. Nie stanowi odbioru kompletności audytu bezpieczeństwa.

To **nie jest dowód braku prywatnych treści we wszystkich logach aplikacji**:
niezmienione bezpośrednie komunikaty mogą zawierać ścieżkę, identyfikator lub
tekst wyjątku. Logger nie jest sanitizerem. Nowa implementacja niczego nie
loguje, test używa tylko syntetycznych markerów. Pełna hierarchiczna
instrumentacja ekosystemu i redakcja pozostałych ujść nie zostały odebrane.

## Wykonane testy

Osobny obszar obiektów `.integration-log-control-20261101/build`.
Focused executable kompiluje rzeczywiste `log.cpp`, RuntimeProfile i potrzebne
utility z tej delty; bez starych obiektów, cudzych binariów lub stubów loggera.
Linker usuwa nieużywane sekcje współdzielonych jednostek (bez monolitu/servera).

```sh
cmake -S .integration-log-control-20261101/source/loom/tests/log_control \
  -B .integration-log-control-20261101/build
cmake --build .integration-log-control-20261101/build -j2
ctest --test-dir .integration-log-control-20261101/build -j1 -V
```

Własny build PASS. Dwa zakresy CTest odebrane osobno:

1. Native sink/startup: **6/6 procesów, 453 asercje**, CTest 1/1 PASS.
   Start unset/1/0/empty/typo oraz 1 + historyczny stderr=0.
   Dokładne canary przez write/write_checked i wszystkie cztery szablony do
   trzech ujść; po disable brak nowych zapisów; istniejący ring zachowany;
   próg i stderr switch nadal działają, runtime reenable działa, start-off
   nie daje się obejść setterami lub poprawnym profilem. Czterech producentów
   po disable, blokowany callback w toku i samowyłączenie callbacku.
2. Globalne symbole C++: **7/7** (2 nowe i 5 starych), CTest 1/1 PASS,
   `nm` na własnym executable. Nie jest to pełna bramka eksportów libloom C ABI.

Pierwszy przebieg nie przeszedł `reenable stderr`: harness używał pozycji
buforowanego `FILE` innej niż `stderr`, mimo współdzielonego deskryptora.
Zastąpiono pomiar przez `fstat/pread`, bez osłabienia asercji i bez zmiany loggera.
Zachowano `ctest-01.log`; finalny `ctest-02.log` i `ctest-symbols.log` zawierają PASS.
Wstępny probe symbolu `recent` nie uwzględniał `[abi:cxx11]`; właściwy test
sprawdza dokładny symbol, nie usunięto wymagania zachowania starego wejścia.

Nie wykonywano server/GUI/accounts/P0/Release/full-repo gate, TSAN ani Androida.
Brak providerów i poświadczeń. Test nie uruchamia serwerów. Stare pakiety i
wcześniejsze dowody pozostają niezmienione. Przed użyciem przy wydaniu potrzebny
był audyt i integracja tej delty (wykonane), pozostaje świadomy build nowego artefaktu oraz zakresowy
odbiór jego konfiguracji startowej; niniejszy test nie relabeluje starego pakietu.
