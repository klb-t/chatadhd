# External share: zakresowa bramka integratora

**Odbiór 2026-11-01:** native10/10 (181/181 asercji), browser6/6. Wyniki i korekty
harnessu: [raport](../../../docs/reports/EXTERNAL_SHARE_INTEGRATION_2026-11-01.md).
Polecenia poniżej opisują odtworzenie w nowym katalogu, nie nakaz ponowienia.

Stan historyczny przy przygotowaniu: kod scalony, **native/HTTP/browser/context/reopen niewykonane**.
Baza ostatniego przyjętego produktu: `97a5562bed5294602161eb0e7d2f2c4bc73e2a60`.
Nowe źródła nie zmieniają tożsamości przyjętego pakietu. Autor wykonał parser i
syntax, integrator tylko własny web typecheck i `node --check` harnessu.

Nie uruchamiać poniższych poleceń w lekkiej fazie. Runner wznawia tę samą sesję;
przed buildem ponownie sprawdzić `/var/lib/codex-team/context/heavy-build-reservation.json`
i stan aktywnych zadań. Samo wygaśnięcie czasu nie jest dowodem zwolnienia slotu.
Nie używać cudzych binariów ani starego serwera jako dowodu nowego kodu.

## 1. Własne aktualne core/server/web i mały target native

Zachować poprzednie artefakty/cache/dowody. Zweryfikować źródło i konfigurację
własnego istniejącego builda; przebudować przyrostowo wyłącznie `loom_core`
i `loom-server` z `-j2`. Ta przygotowana konfiguracja focused targetu zakłada
`LOOM_USE_SYSTEM_SQLITE=ON` (tak działa własny Release); przy innej konfiguracji
najpierw świadomie dopasować linkowanie, nie podmieniać SQLite przypadkowo.
Nie budować CLI ani monolitycznego `loom_tests`.

```sh
# TYLKO PO ŚWIEŻEJ REZERWACJI, jawnie ustawione własne ścieżki:
cmake --build "$CORE_BUILD" --target loom_core loom-server -j2
cmake -S loom/tests/url_share/native_acceptance -B "$NEW_FOCUSED_BUILD" \
  -DLOOM_SOURCE="$REPO/loom" -DLOOM_CORE_BUILD="$CORE_BUILD"
cmake --build "$NEW_FOCUSED_BUILD" -j2
ctest --test-dir "$NEW_FOCUSED_BUILD" -j1 --output-on-failure -V
# Jeżeli tsc już odebrany, wykonać tylko Vite do nowego katalogu:
(cd loom/web && node node_modules/vite/bin/vite.js build --outDir "$NEW_WEBDIST")
```

Focused target kompiluje tylko `tests/main.cpp` i `test_url_share_import.cpp`
oraz testową bibliotekę mapowania sieci, linkuje świeże własne archiwa core/miniz/pqc.
Suite `url_share_import` ma **10 przypadków**. Dwa nowe dotyczą rzeczywistego
zapisu drzewa/rodziców/źródła/locatorów/reference/idempotencji/reopen oraz odmowy
błędnych/duplikowanych parent pointers. Integrator dodał porównanie liczby
rekordów messages/sources/provenance przed/po rollbacku i ponownie po reopen.
To rollback DB, nie obietnica usunięcia bajtów z content-addressed BlobStore.
Nowe asercje otrzymały wynik kompilacji i wykonania w odbiorze 2026-11-01.

## 2. Rzeczywisty ImportPanel/HTTP i ChatView ze sprawdzonym kontekstem

`loom/web/e2e/external-share-native.mjs` przygotowuje **6 grup**. Łączy:

1. UI upload deskryptora → `/api/import` → osadzony domyślny worker → rzeczywisty
   fetch HTTPS do lokalnego źródła → native store; role, tekst, parent, `version`,
   reference-only assets, dokładny oryginał i `embedded_data` provenance.
2. Idempotencję przez ten sam UI/HTTP, bez duplikacji rekordów i model calls.
3. ChatView → rzeczywisty POST do syntetycznego providera: aktywna treść i kolejność
   user/assistant, brak off-path tekstu w **całym** odebranym body. Pytanie nie
   zawiera importowanej treści, nie ma załącznika zastępującego historię. Odpowiedź
   atrapy pochodzi wyłącznie z odebranych wiadomości, a UI musi ją pokazać.
4. Zamknięcie własnego serwera, reopen i reload przeglądarki: wiadomości, rodzice,
   wersje, provenance, oryginał, powtórny kontekst i idempotencja.
5. Drugi format HTML z niezależnym oryginałem i taką samą semantyką gałęzi.
6. Odmowy HTTP/UI: 403/login, nierozpoznana struktura, redirect do private IP,
   downgrade HTTPS→HTTP, bezpośredni private IP i jawny proxy. Zero pobrań
   referencji załączników oraz brak Cookie/Authorization w żądaniach źródła.

Fixture `external_share_network_fixture.c` jest wyłącznie testowym LD_PRELOAD
we własnym procesie serwera: nazwę ChatGPT mapuje na nominalny publiczny adres,
a połączenie na loopback HTTPS. Nie jest opcją produktu ani argumentem uploadu.
Pozostałe zewnętrzne połączenia są odrzucane; drugi dopuszczony loopback port to
syntetyczny provider. Produkcyjny DNS/public-IP/pinning/redirect/proxy kod
pozostaje niezmieniony. Ten test **nie dowodzi publicznego DNS/TLS lub prawdziwego
share z internetu**. Wytwarza nowy lokalny certyfikat testowy dla tej sesji;
klucz pozostaje w nowym prywatnym katalogu i nie trafia do manifestu produktu.
Nie wyłącza weryfikacji TLS — domyślny worker otrzymuje `SSL_CERT_FILE`.

Harness wymaga osobnego świeżego katalogu dowodów, Landlock backendu i whitelisty
env. Ogranicza także requesty przeglądarki do własnego originu. Nie uruchamia
instalacji ani builda. Na koniec zamyka własny browser/server/TLS/provider.

Po udanym buildzie utworzyć prywatny JSON receipt z rzeczywistymi wartościami:
`server_sha256`, `web_index_sha256`, `worker_sha256`, `runtime_tree` (git HEAD:loom),
`network_fixture_sha256` i `network_fixture_source_sha256`. Receipt musi wynikać
z zapisanych logów/source binding buildu, nie tylko dowolnego przepisania hashy
starego pliku. Harness sprawdza hashe przed startem.

```sh
# Nie wykonano w fazie lekkiej:
cd loom/web
LOOM_EXTERNAL_SHARE_NATIVE=1 \
LOOM_SERVER_BIN="$CORE_BUILD/server/loom-server" \
LOOM_SHARE_DIST="$REPO/loom/web/dist" \
LOOM_SHARE_NETWORK_FIXTURE="$NEW_FOCUSED_BUILD/libexternal_share_network_fixture.so" \
LOOM_SHARE_BUILD_RECEIPT="$NEW_EVIDENCE/build-receipt.json" \
LOOM_SHARE_EVIDENCE="$NEW_EVIDENCE/browser-new" \
node e2e/external-share-native.mjs
```

Nie obniżać asercji przy błędzie UI/history/worker; rozdzielić błąd harnessu,
produktu i konkretne odmowy środowiska. Brak grupy lub brak źródłowego dowodu
binariów oznacza brak odbioru. Pełnych historycznych P0/backup/accounts/Release
nie powtarzać. Nowy pakiet dopiero po osobnej decyzji po runtime admission.
