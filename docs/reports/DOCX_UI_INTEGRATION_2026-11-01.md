# DOCX SourceParts — odbiór UI, 2026-11-01

Przyjęto zakres frontendowy na backendzie **49444c869a80a086683f5db383b1f34c9bb01a46**
(kod **7a1037ab83664d38ab0fccd892557fbf3d135652**). Własne typecheck, web build,
**4/4** grupy komponentu i **6/6** grup rzeczywistego browser/native przeszły.
**Implemented i tested zakresowo; NOT deployed.** Kod C++/reader/profil/ABI niezmieniony.

## Pochodzenie i zgodność

Task `chatadhd-docx-parts-ui-20261101`, baza autora
`f527ee5cc34825f38485419e4c11ef7c3ead6ad0`, snapshot `1793505592012562333`.
Audited delta SHA256 `6ad56402f480aa99bbadbea86f5127b646c1ff6302226942bf959b9f09f41047`.
Odczytano matching admin/integrity/team/integration receipts oraz kanon §0 rewizja 2
z absolutnych ścieżek. Świeże HEAD/status-z i 10/10 hashy przed/po zgodne.
Po przeglądzie pełnych plików wykonano jedno `git apply --check --whitespace=error-all`
i jedno apply, początkowe 10/10 hashy zgodne. Nie nakładano 17 plików backendoverlayu,
STATE autora, transportu, CHECKPOINT, logów ani metadanych Git. Worktree autora tylko odczytywano.

Terminalny backend potwierdzono rzeczywistymi zachowanymi wynikami DOCX 9/9,
G1 11/11 i HTTP 4/4 oraz ponownym odczytem sum 107/107 dowodów. To **historyczne**
wyniki poprzedniego przyrostu, nie nowe wykonanie. Rezerwacja poprzedniego właściciela
zakończona, runner uruchomił jedyny task UI po backendzie; brak obcych ciężkich procesów.
Nie było C++/CMake/CTest/rebuilda starego Release lub zmian w istniejących artefaktach.

## Własne wykonanie

- `node loom/web/node_modules/typescript/bin/tsc --project loom/web/tsconfig.json --noEmit --incremental false`: exit 0.
- W `loom/web`: `node e2e/docx-source-parts-component.mjs`: **4/4 PASS**.
  Prawdziwy React z atrapą transportu: odmowa materializacji, dwa hashe i duplikaty,
  nieprawidłowy odczyt usuwa podgląd, blokada powtórnego kliknięcia i spóźnionej odpowiedzi.
  Nie jest to wynik native.
- W `loom/web`: `node node_modules/vite/bin/vite.js build --outDir <nowy własny webdist>`:
  exit 0, **38,04 s**, 328 modułów. Zachowano ostrzeżenie o chunkach ponad 500 kB.
- `node --test loom/web/tests/text-sources.test.mjs`: **11/11 PASS**, 0 skipped,
  398,406 ms. Mała regresja wspólnego modułu selektora i źródeł UTF-8 po jego zmianie;
  nie ponowna bramka backendu G1. Autorskie 15 testów mapped-state nie są własnym wynikiem.
- `python3 .integration-docx-ui-20261101/run-browser.py browser-04`:
  **6/6 PASS**, rzeczywista App/ImportPanel/GraphView/ChatView, Chromium/Playwright,
  zaakceptowany Release server/lib/default DOCX reader i nowy webdist.

Próba browser potwierdziła upload oryginału z trwałym kluczem bez automatycznej
materializacji; jawne odczytanie DOCX; rozpoznane akapity, nagłówek, listę i tekst
tabeli wraz z pustymi blokami oraz ostrzeżeniami; idempotencję; różne ID i zakresy
dwóch identycznych akapitów; graf i wybór globalny konta bez utraty innych ID,
budżetu lub ustawień; rzeczywisty wybrany tekst i ID w systemowym kontekście
lokalnego providera i widocznej odpowiedzi. Pytanie nie zawierało źródła ani załącznika;
nieobecne były niewybrane wystąpienie, akapit i tabela.

Sprawdzono drugi ZIP z identyczną pochodną: ten sam source_key, osobne Source/occurrence
IDs, poprzednik będący pochodną Source, oba oryginały dostępne. Reload i restart
native zachowały selektor, ostrzeżenia, rozpoznane części i odczyty. Przyciski pobierania
wykonały rzeczywiste pobranie ZIP/UTF-8 i SHA256, porównane również przez harness.
Dwa żądania `new_conversation: true` utworzyły dwie różne rozmowy: ID z rzeczywistych
zdarzeń SSE zgadzały się z UI, odpowiedzi były trwale zapisane w historii.
Uszkodzony syntetyczny DOCX: **HTTP 400 / parse / docx_invalid_container**, komunikat UI,
brak fikcyjnej materializacji/podglądu/wyboru i brak zmiany poprzedniej konfiguracji.
Dokładnie **2 wywołania lokalnej atrapy**, brak realnego modelu lub publicznego API.

## Izolacja i dowody

Dane to syntetyczne fixtures `loom/tests/fixtures/docx_parts/{base,zip-change}.docx`
i `base.json`, przyjęte z backendem, oraz jawny tekst `synthetic-not-a-zip`.
Atrapa konstruuje odpowiedź wyłącznie z otrzymanego systemowego kontekstu;
wynik dowodzi transportu/wyboru, nie rozumienia przez model. Jeden syntetyczny
root gościa reprezentuje konto; nie powtarzano macierzy izolacji kont A/B.

Backend uruchomiono dwukrotnie przez `setpriv --no-new-privs --landlock-access fs`,
z jawnymi accounts-root/data-dir/static-dir, białą listą env i loopback. Dozwolone
były /usr, /dev/null, odczyt pakietu/webdist i własny nowy root RW; checkout niedostępny.
Użyto **domyślnego package:docx_basic**, bez nadpisania readera/interpretera z checkoutu.
Server SHA256 `48cd4b760270a420e0109b5176187802b9b850379c9bfb6032c0054b44e556d9`,
lib `2f582cf7594f95b1249e3ca8548cccc57864310268886fbd64a45f286cc0f928`,
reader `8a3ed0a585d17d48bc1b42794da7947e7b957eb2f68e632146f6771be55c5f4c`.

Dowody pozostają w ignorowanym `.integration-docx-ui-20261101/`: source/admission,
applied-once, light-results, web-binding, logi, screenshoty, browser-04 receipt,
command/result i jawne źródła harnessu. Konfiguracje/DB gościa nie są produktem ani
publicznym manifestem. Browser/provider/server po próbie zostały zakończone.

Zachowano nieudane próby: 01 **0/6** (brak oczekiwania na gotowość powłoki po reload),
02 **5/6** (harness zakładał conv_id w żądaniu tworzącym rozmowę), 03 **3/6**
(błędne pole content zamiast text w dodatkowej asercji historii). Poprawiono tylko
harness: oczekiwanie na UI i prawdziwą szufladę, powiązanie new_conversation → SSE ID
→ UI → trwała historia. Nie usunięto asercji odrębności rozmów ani nie zmieniono produktu.
Każda próba miała nowy katalog; źródła o sumach zgodnych z command receipt zachowano.

## Granice

Wybór jest **globalny dla konta / wszystkich rozmów**, z read/merge/write i bez CAS
tej samej tablicy. Lista źródeł/projekcji jest oknem o limicie. Podgląd native i pobranie
sprawdzają rzeczywiste bajty/hash; selector database-only nie jest integralnością
całego magazynu ani mechanizmem naprawy grafu. Współrzędne odnoszą się wyłącznie do
pochodnego UTF-8, nie ZIP/XML, stron, układu, AST lub pełnego modelu tabel.

Brak nowych wyników mobile/JNI, realnego providera/modelu, pełnej bramki repo,
P0/share/accounts/backup/rollback. Zastany komunikat o kluczu dostawcy i etykieta roli
`assistant` pozostają lukami UI poza tym zakresem. Logger ma wcześniej opisane
ograniczenia; produkcyjne backup/restore i zgoda na wdrożenie pozostają u administratora.
Stare pakiety, Debug/Release cache i delty zachowane. Nowy pakiet ma otrzymać wyłącznie
przetestowany web oraz hash-identyczne backend/helper payloady, z osobną provenance.
