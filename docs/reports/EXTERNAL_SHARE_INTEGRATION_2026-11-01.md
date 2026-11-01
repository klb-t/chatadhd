# External share — wykonana lekka integracja, runtime nieodebrany

2026-11-01. Baza i ostatni przyjęty produkt:
`97a5562bed5294602161eb0e7d2f2c4bc73e2a60`.
**Nowy commit oznacza implemented/integrated, nie native-accepted ani deployed.**
Przyjęty immutable pakiet ccb5ef9d o SHA256
`95bb540ca669dc6410af75aa135434800374957bea7a6cc7a5d2359cfd643481`
pozostał bez zmian. Nie przebudowywano runtime, Release ani webdist.

## Odbiór źródła

Task `chatadhd-external-share-import-20261101` ma completed i matching audyt
administratora; snapshot `1793501984686167540`, archive SHA256
`615a76066109470c8b185196ed6e1c9f11228bcc9bab89232dbb32b8548f65e6`.
Prywatnego archiwum nie odczytywano. Worktree autora:
`/var/lib/codex-team/worktrees/chatadhd-external-share-97a5562-20261101`,
HEAD/base identyczny z bazą integratora. Manifest zawiera 24 pliki.

Świeże hashe 24/24, status-z i jawny tracked diff sprawdzono przed i po odczycie;
wykluczono chronione ścieżki exports/holdout, nie czytano ich treści. Przeczytano
checkpoint, pełne nowe pliki i diff. Osadzone bajty worker.inc są identyczne
z canonical worker.py. Completed i lekkie testy autora nie są akceptacją produktu.

Patch SHA256 `2f6146f73880933a93e64119790ee6f4823d0f2498ca9ea8fbe25aea196bbfa3`:
`git apply --check --exclude=CHECKPOINT.md`, następnie **jedno apply** z tym samym
wyłączeniem. 23/23 produktowe hashe zgodne bezpośrednio po apply. CHECKPOINT,
handoff i administracyjne receipts nie trafiły do produktu. Dotychczasowe
operator tools/logger/artefakty i historia commitów zachowane.

## Review semantyki i granice

Parser dotyczy zewnętrznych struktur ChatGPT share HTML: Next.js i ograniczony
JSON subset turbo-stream v2. Nie jest importem własnego JSON pod nową nazwą.
Czyta inertne dane, nie wykonuje JS, nie pobiera ukrytych endpointów ani modeli.
Dowody formatu i syntetyczne pochodzenie dwóch fixtures zapisano w
[PROVENANCE](../../loom/tests/fixtures/url_share/PROVENANCE.md). Nie pobrano
rzeczywistej rozmowy z internetu; prywatny format HTML może się zmienić.

Przeczytany kod C++ zachowuje parent pointers i source/blob/provenance, lokator
wskazuje `embedded_data`, a nie udaje JSON pointera do HTML. Wiadomość off-path
otrzymuje **msg_status::kVersion**. Domyślne `Database::get_msgs` zwraca active,
a ChatEngine ponownie filtruje active. To dowód źródłowy, **nie wykonany dowód
braku off-path tekstu w żądaniu providera**. Przyszła bramka sprawdza całe body
syntetycznego providera, kolejność/role i odpowiedź pochodzącą z odebranej historii.
Profil tej próby jest jawnie plain/raw z historią; nie dowodzi wszystkich
możliwych konfiguracji unified retrieval/include-inactive.

Zapis jest objęty istniejącą `sql::Txn`; unresolved/duplicate parent zwraca błąd
przed commit. Integrator rozszerzył przygotowany test o count messages/sources/
provenance przed/po błędzie i po reopen. Nie twierdzimy, że rollback DB usuwa
bajty z filesystemowego content-addressed BlobStore. Test nie został uruchomiony.
Załączniki dostawcy pozostają odniesieniami, również prywatne URL/asset_pointer;
nie są pobierane i nie stają się multimodalnym payloadem.

Własny lekki audyt AST potwierdził dokładną zgodność 7 definicji z bazą:
normalize_url, public_address, remaining, PublicTransport, Fetcher, validate_policy,
PageText. Zachowano resolver/public-IP, numeric pin, proxy refusal i redirect
policy. To nie nowy runtime PASS SSRF/TLS. Nie dodano drugiego stosu HTTP,
publicznego ABI ani migracji schematu. UI PL/EN opisuje reference-only, partial,
odmowę i fallback do importu pliku.

## Wykonane kontrole i przygotowane bramki

- **Własny typecheck PASS**: `node loom/web/node_modules/typescript/bin/tsc
  --project loom/web/tsconfig.json --noEmit --incremental false`, exit 0,
  pusty log; rzeczywiste własne zależności, bez Vite/builda/instalacji/emisji.
- **Własna kontrola składni harnessu JS PASS**: `node --check
  loom/web/e2e/external-share-native.mjs`. Nie uruchamia przeglądarki/serwera.
- Autor: 53 Python, 4 Node, 2 syntax C++, 2 generator checks — zachowane dowody
  historyczne, nie powtórzone i nie przypisane integratorowi.
- Native: 10 przypadków przygotowanych, **0 wykonanych**; nowe asercje rollbacku
  nie mają nawet nowego wyniku C++ syntax. Standalone focused target kompiluje
  dwa testowe TU i fixture, linkuje własny świeży core; nie buduje loom_tests.
- HTTP/browser: **6 grup przygotowanych, 0 wykonanych**. Rzeczywisty ImportPanel,
  domyślny osadzony worker, HTTPS loopback, parent/provenance/reference/original,
  idempotencja, wybrana gałąź w kontekście i odpowiedzi, restart/reopen, drugi
  format, odmowy/private redirect/TLS downgrade/proxy. Kod fixture C nieskompilowany.

[Instrukcja i dokładne polecenia](../../loom/tests/url_share/NATIVE_ACCEPTANCE.md).
Harness wymaga hash binding nowego binarium/web/worker/fixture oraz tree źródeł;
receipt musi powstać po rzeczywistym buildzie. Samo zapisanie hashy starego
artefaktu nie dowodzi nowego kodu. Testowy DNS/dial preload
jest tylko fixture własnego procesu; rzeczywiste TLS weryfikuje nowy lokalny
certyfikat, nie wyłącza peer verification. Nie jest to publiczny provider TLS.
Landlock backendu, env whitelist i ograniczenie originu przeglądarki są przygotowane,
ale jeszcze nieprzetestowane z nowym harness’em. Nie można oznaczyć ich jako PASS.

## Zasoby i następny krok

Na starcie LEM był running i posiadał heavy slot. Jednorazowy końcowy odczyt
**03:13:23 UTC**: LEM completed, brak pozostałych Java według administratora;
slot przypisano `chatadhd-external-share-native-acceptance-20261101` po zakończeniu
tej lekkiej fazy. Acceptance jest pending i czeka na ten task. Nie ma już
aktualnego blokera LEM; pozostaje brak wykonania nowej bramki i jej odrębny start.
Nie wykonano pollingu ani samodzielnego przejścia do fazy ciężkiej.

Runner wznawia ten sam UUID: ponowna kontrola rezerwacji, własny przyrostowy
core/server -j2, focused native10/CTest-j1, własny webbuild i6grup HTTP/GUI.
Nie powtarzać P0/compat/accounts/backupów ani lekkich parser tests bez powodu.
Nie relabelować dotychczasowego pakietu. Kod/harnessy mogą wymagać poprawki po
pierwszym wykonaniu; nie wolno osłabić asercji by uzyskać PASS.

Brak wdrożenia, modeli/płatnych API, danych produkcji, nowych agentów, instalacji,
zmian hosta i publikacji. Ten przyrost kończy się lokalnym commitem/checkpointem.
