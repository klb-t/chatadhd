# DOCX source parts — odebrany backend

2026-11-01. **Implemented/integrated/tested zakresowo, nie deployed.**
Baza: `f527ee5cc34825f38485419e4c11ef7c3ead6ad0`. Przyjęty wcześniej pakiet
`e77f698e-release-share-20261101`, SHA256
`d5e7531426acb8243d653956f916ed15fc0efee2bbedd59cfe148f231348f609`,
pozostaje zachowany. Nowy pakiet backendu powstaje osobno po wykonanym odbiorze;
nie przypisujemy mu testów UI.

## Źródło i zakres integracji

Task `chatadhd-docx-source-parts-20261101`, worktree
`/var/lib/codex-team/worktrees/chatadhd-docx-parts-f527ee5-20261101`, base jak wyżej.
Matching audited receipt/integrity: completed snapshot `1793504748406534500`,
archive attestation `a1c9c4f97e1cdada567723eab6455327e8d3772bb89697aed41563625f0200f5`.
Prywatnego archiwum nie czytano. Product patch
`587538af89214015306d742777beeaa7eefe3e9694a1aa479b87bbdb18ed360c` zastosowano
**raz**, po apply-check. Własne17/17hashy, status-z, diffHEAD i HEAD przed/po
zgodne z audytem; wszystkie nowe pliki i zawartość syntetycznych ZIP przeczytano.
STATE autora, checkpoint, manifesty, patch i logi nie trafiły do produktu.
UI osobnego autora nie było czytane ani integrowane.

Android gate otrzymał completed04:08:03UTC; runner uruchomił ten task04:08:06
bez oczekujących zależności. Warunek rezerwacji: terminal właściciela i cleanup,
nie samo expires_at. Odczyt listy procesów nie wykazał ciężkich workerów.
Bezpośredni checkpoint Androida zwrócił EACCES13; nie obchodzono ACL. Przyjęto
świeży terminalny receipt i przejście runnera zgodne z release_condition.

## Zachowanie i granice danych

Jawne `client.blob_text`, format DOCX, uruchamia istniejący bounded reader.
Sam upload nie materializuje grafu. Oryginalny ZIP pozostaje osobnym blobem i
Source. Rozpoznany tekst ma własny blob UTF-8, Source/Unit/Observation, metodę,
provenance oraz istniejące genesis i wystąpienia. Dwa różne ZIP dające ten sam
tekst nie zlewają Source ani occurrence IDs. Identyczne akapity zachowują osobne
zakresy i wystąpienia; wspólny fingerprint nie jest tożsamością źródła.

`derived_utf8_bytes` dotyczy wyłącznie pochodnej. Lokator body-child readera nie
jest byte offsetem ZIP/XML, stroną ani pełnym XPath. Zachowano partial/warnings,
pełną ekstrakcję, skrypt i limity jako zapisane pochodzenie. Nie wykonano nowego
parsera, schematu SQL ani publicznego C ABI. Kanon §0 rewizja2: dane i
wyprowadzenia oddzielone, inwarianty hash/zakres/tożsamość egzekwuje kod.

Selector jest **database-only**. Kontroluje descriptor i metadane, nie odczytuje
plików BlobStore podczas każdego żądania. Hashy na dysku dowodzą jawne read/
materialize. Cache projekcji nie naprawia grafu. Rollback transakcji DB nie
obiecuje usunięcia content-addressed plików. Stare binarium nie rozumie nowej
współrzędnej pochodnej; historyczna zgodność rollbacku nie dowodzi obsługi tych
nowych wystąpień. Powrót z takimi danymi wymaga odrębnego sprawdzenia lub
odtworzenia wcześniejszej spójnej kopii przez administratora.

## Własne wykonane testy

| Bramka | Wynik |
|---|---|
| Przyrostowy Release core/server/lib, -j2 | PASS, bez CLI/monolitycznego loom_tests |
| DOCX source parts native | **9/9 przypadków,172/172 asercje**,6.93s |
| Regresja wspólnego G1 UTF-8 | **11/11 przypadków,76/76 asercji**,4.28s |
| CTest-j1 | **2/2**,11.23s; filtry obu suites wykonane |
| Nowy DOCX HTTP | **2/2 scenariusze** |
| G1 HTTP context/reopen oraz code/version | **2/2 scenariusze** |
| Łączny HTTP | **4/4**,10.040s, bez skip/failure |
| Regeneracja textpack i --check | PASS, hash równy audytowanej delcie |

Zachowano kompletną kopię poprzedniego Release cache z porównaniem hashy i
sprawdzeniem miejsca; kopia nie służyła jako przeniesiony CMake cache. Build
pozostał we własnym canonical katalogu Release/systemSQLite. Focused runner
linkuje wyłącznie własny nowy core, trzy testowe TU, bez monolitu. Nowy reader
staged razem z server/lib. Native używa jawnego staged readera; HTTP używa
**domyślnego package:docx_basic** i manifestu interpretera.

Native wykazał zgodność projekcji i zakresów, odmowy zmienionego hash/wersji/
koordynat, realny upload bez automaterializacji,11osobnych wystąpień (duplikaty,
puste, multiline), Source/Unit/Observation/provenance i original. Idempotencja,
wersja ZIP i tekstu, zmiana metody, genesis initial events, finer word projection,
reopen, błędny poprzednik/reader/limity oraz uszkodzone descriptors/brak blobów
mają wykonane asercje. ScriptedTransport odebrał wyłącznie wybrany akapit.

HTTP wykonał multipart→jawną materializację→graf→wybór occurrence przez config→
rzeczywisty POST do loopback providera. Systemowy kontekst zawiera wybrany
akapit i jego occurrence ID; nie zawiera niewybranego powtórzonego akapitu ani
tabeli. Pytanie nie zawiera fixture, attachments są puste. Po restarcie sprawdzono
read/idempotencję/oryginał/pochodną, zachowany selector w config oraz ponowny
POST z tym samym wybranym tekstem. Drugi scenariusz zachował osobne Source przy
identycznym tekście różnych ZIP i odmówił błędnego poprzednika. Odpowiedź
providera jest syntetycznym markerem; nie twierdzimy, że testuje rozumienie DOCX.

Nowe własne rozszerzenia testów: opcjonalny staged reader w native; domyślny
pakiet w HTTP zamiast skryptu z checkoutu; zapisany selector i ponowny kontekst
po restarcie. Focused CMake/README. Nie zmieniano audytowanego kodu produktu.
Autor miał10Python/4fixtures/4AST/1generator/4syntaxTU — historyczne, nie nasze
runtime wyniki. Nie powtarzano parserów,40DOCX, pełnychP0/share/compat/backupów.
Regresja G1 jest uzasadniona zmianą wspólnych source_parts/textpack/context.

## Izolacja i dowody

Backend HTTP działał pod Landlock: /usr, nowy runtime-stage i wyłącznie nowy
syntetyczny rootRW, jawne data-dir/accounts-root/static-dir/env whitelist,
loopback. Checkout był dla procesu serwera niedostępny. Zewnętrzny runner miał
read-only dostęp do testów/fixtures oraz własny katalog wyników. Brak publicznych
URL/API/modeli/produkcji. Harness zamknął wszystkie własne serwery i providery.

Dowody: `.integration-docx-parts-20261101`: admission, audyt/hash/apply-once,
preserved-build, build/native/http logs/results, staged helper binding i
syntetyczne request transcripts. [Focused polecenia](../../loom/tests/docx_source_parts/README.md).
Testy HTTP uruchomione tylko dla DocxSourceParts i dwóch wskazanych metod
GraphSourceParts; dokładne argv/Landlock/env w http-command.json.

Brak odbioru UI/selector-panel/browser/mobile/JNI; autor UI jest osobnym zadaniem.
Brak stron, pełnego layoutu/tabel-cell grafu, AST, OCR/PDF koordynat, realnego
modelu, wszystkich środowisk interpretera lub pełnego repo gate. Logowanie i
operator tools pozostają bez zmian i z wcześniejszymi ograniczeniami. Brak
zgody wdrożeniowej; produkcyjna kopia/restore/ACL/RPO należą do administratora.

Next tego przyrostu: lokalny commit wyniku, świadomy nowy artefakt i jego
verify-package; późniejszy UI wymaga osobnego audytu i wykonania. Nie uruchamiać
kolejnego cyklu samodzielnie.
