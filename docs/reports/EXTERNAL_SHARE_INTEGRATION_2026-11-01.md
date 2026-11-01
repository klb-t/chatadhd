# External share — odebrany runtime, HTTP i GUI

2026-11-01. **Implemented, integrated i tested zakresowo; nie deployed.**
Źródła produkcyjne z commita `3bf01f7c29981d27554dd44dbd21e2d29f42029c`
przeszły własną bramkę native oraz rzeczywisty HTTP/browser/context/reopen.
Dwie konieczne korekty harnessu nie zmieniły produktu ani warunków odbioru.
Nowy pakiet jest osobnym, następnym krokiem tego przyrostu; wcześniejszy immutable
pakiet ccb5ef9d SHA256
`95bb540ca669dc6410af75aa135434800374957bea7a6cc7a5d2359cfd643481`
pozostaje zachowany. Wyniki nie są zgodą na wdrożenie.

## Pochodzenie i rezerwacja

Źródłowa baza `97a5562bed5294602161eb0e7d2f2c4bc73e2a60`; task
`chatadhd-external-share-import-20261101`, completed snapshot
`1793501984686167540`, archive attestation
`615a76066109470c8b185196ed6e1c9f11228bcc9bab89232dbb32b8548f65e6`.
Worktree autora: `/var/lib/codex-team/worktrees/chatadhd-external-share-97a5562-20261101`.
W zakończonej fazie lekkiej sprawdzono stabilne24/24hashy/status/diff/HEAD,
przeczytano nowe pliki i zastosowano RAZ patch
`2f6146f73880933a93e64119790ee6f4823d0f2498ca9ea8fbe25aea196bbfa3`
z wyłączeniem CHECKPOINT.md. Prywatnego archiwum i chronionych treści nie czytano;
transport nie trafił do produktu. W tym przyroście nie ponawiano apply.

Świeże receipts03:17:40UTC potwierdziły zakończenie lekkiej integracji oraz LEM.
Runner przydzielił slot `chatadhd-external-share-native-acceptance-20261101` po
cleanup LEM; lista pozostałych Java pusta. Nie oparto zgody na expires_at.
Bezpośredni LEM CHECKPOINT zwrócił EACCES13; odczytano matching końcowe receipts
administratora i integrity, bez obchodzenia ACL. Nie uruchamiano innych agentów.

Kanon §0 rewizja2: źródłowe bajty i wyprowadzenia z pochodzeniem osobno;
inwarianty transportu/rodziców/transakcji pozostają w kodzie. Próg/etykiety nie
obchodzą SSRF. Istniejący logger i operator tools pozostają bez zmian.

## Wykonane własne bramki

| Bramka | Rzeczywisty wynik |
|---|---|
| Release przyrostowy core/server/lib | exit0, pojedynczy zmieniony TU url_share_import.cpp, linkowanie własnych artefaktów |
| Focused native `url_share_import`, CTest-j1 | **10/10 przypadków,181/181 asercji**, CTest1/1,6.68s |
| Nowy webdist, własny Vite | exit0; osobny katalog, wcześniejszy dist zachowany |
| ImportPanel/ChatView + HTTP + native + domyślny embedded worker | **6/6 grup**; wykonanie browser-03 exit0 |

Przed buildem porównano źródła i cache Release/systemSQLite, testsOFF/cliOFF.
Zachowano kompletną kopię starego builda:410wpisów/312281300bajtów logicznych,
bez polegania na reflink. Kopia nie była uruchamiana jako przeniesiony cache.
Kompilowano w oryginalnej ścieżce z-j2; focused target linkował ten sam core.
Nie budowano CLI ani monolitycznego loom_tests.

Polecenia wykonane z własnego repo:

```sh
cmake --build .integration-release-20261031/build --target loom_core loom-server loom -j2
cmake -S loom/tests/url_share/native_acceptance -B "$NEW/native-build" \
  -DLOOM_SOURCE="$REPO/loom" -DLOOM_CORE_BUILD="$REPO/.integration-release-20261031/build"
cmake --build "$NEW/native-build" -j2
ctest --test-dir "$NEW/native-build" -j1 --output-on-failure -V
# cwd loom/web; typecheck już odebrany w fazie lekkiej:
node node_modules/vite/bin/vite.js build --outDir "$NEW/webdist"
# zmienne bin/dist/networkfixture/buildreceipt/evidence według NATIVE_ACCEPTANCE.md:
node e2e/external-share-native.mjs
```

Native potwierdził zapis rodziców/active versus kVersion, source/provenance,
idempotencję i reopen. W odmowach unresolved/duplicate parent porównano liczby
messages/sources/provenance przed błędem, po nim i po reopen. Jest to rollback
transakcji DB, bez obietnicy usunięcia bajtów z content-addressed BlobStore.

Sześć grup browser-03 wykazało:

1. Rzeczywisty UI/HTTP import HTML turbo-stream v2: dokładny tekst PL,4wiadomości
   user/assistant/assistant/tool, drzewo rodziców, off-path kVersion,2references,
   source i locator `embedded_data` dla każdej wiadomości oraz hash oryginału.
2. Ponowny import przez UI/HTTP zachował source/conversation/rows bez duplikacji.
3. ChatView wysłał do syntetycznego providera aktywny tekst we właściwych rolach
   i kolejności. **Całe body nie zawierało off-path tekstu**. Pytanie nie
   zawierało fixture ani zastępczego załącznika; odpowiedź pochodziła wyłącznie
   z odebranej historii i została pokazana przez UI. ID rozmowy sprawdzone.
4. Po zamknięciu i ponownym starcie własnego serwera oraz reload przeglądarki
   zachowano wiadomości/rodziców/version/provenance/oryginał/idempotencję;
   ponowne żądanie i odpowiedź zachowały wybraną gałąź.
5. Drugi rzeczywisty format strukturalny HTML Next.js zachował odrębny oryginał
   i te same role/tekst/semantykę gałęzi.
6. Odmowy403/unknown/private-IP/private-redirect/HTTPS-downgrade/proxy mają
   oczekiwane reason i brak rozmów. UI wskazuje import pliku. Żadne reference
   assety nie zostały pobrane, źródło nie otrzymało Cookie/Authorization.

Łącznie2POST do własnego syntetycznego providera i8GET do własnego źródłaTLS.
Brak błędów przeglądarki. Każdy run kończył własne procesy/listenery przez finally.

## Korekty testu i granice

Browser-01 zatrzymał się na błędnym oczekiwaniu tablicy z provenance. Istniejący
kontrakt C API/HTTP zwraca subject_id/records/sources; poprawiono odczyt,
zachowano kontrole i dodano subject_id oraz obecność source. Browser-02 wykonał
2/6grup, następnie oczekiwał listy warsztatu po przejściu do ChatShell. Poprawiono
nawigację przez istniejącą szufladę i sprawdzono wybrane ID rozmowy. Oba nieudane
wyniki i wersje testu zachowane; nie liczą się jako PASS pełnej bramki.
Żadnej zmiany produktu, obniżenia asercji lub ponownego native/webbuilda dla tych
korekt nie wykonano.

Fixture HTML jest syntetyczna; pochodzenie struktury opisuje
[PROVENANCE](../../loom/tests/fixtures/url_share/PROVENANCE.md). Testowy DNS/dial
preload mapuje nominalny ChatGPT HTTPS do własnegoTLSloopback, odmawia innych
połączeń poza portem atrapy. Produkcyjny resolver/publicIP/pinning/redirect/proxy
pozostał niezmieniony. Weryfikacja TLS działa z nowym syntetycznym CA, bez
wyłączenia peer verification. Backend ma Landlock, env whitelist, jawne rooty;
browser dopuszcza tylko własny origin. Nie użyto publicznego URL, prawdziwego
modelu/API, prywatnych danych ani produkcji.

To **nie** dowód pobrania aktualnego publicznego ChatGPT share, wszystkich
prywatnych formatów HTML, adaptera Claude, aktywnego pobierania załączników,
wszystkich konfiguracji unified/include-inactive, mobile/JNI ani pełnego repo gate.
Profil testu: plain/raw, unified=false, include_knowledge=false. Brak nowego ABI
lub migracji schematu. Zachowano historyczne P0/G1GUI/accounts/single-runtime/
backup/logger wyniki bez powtarzania. Typecheck i parsery nie były powtarzane.
Ograniczenia loggera (directstdio/helpers/browser poza przełącznikiem, brak
pełnego sanitization) oraz produkcyjne backup/restore/RPO/ACL pozostają jawne.

Dowody lokalne: `.integration-external-share-acceptance-20261101`, polecenia,
logi, build-receipt, native-results, browser-01/02/03 i ich receipts. Syntetyczne
klucze TLS/DB/config nie trafiają do commita ani manifestu dowodów.
[Harness i polecenia](../../loom/tests/url_share/NATIVE_ACCEPTANCE.md).
