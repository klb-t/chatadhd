# Wątek 5 — import archiwów i adnotacje, 2026-10-04

Gałąź: `gpt/archive-import-2026-10-04`; świeży rebase na main **30ad7d3**
(przyjęty W2 z danych i wymagania R39–R41), opublikowany jako **a343a8c**.
Aktualny zweryfikowany kod: **4e8c3dec29bb3aa1e343e09bee00b7768f0fc0f2**.
Późniejsze commity zmieniają tylko raporty/dowody. Historyczny kod pomiarów
wydajności: **1474b260582a80f006f99953baaad1b3253ca6e1**.
Zakres 5, dane syntetyczne/publiczne, offline: **zero płatnych wywołań**.

**Gotowy do odbioru przyrost W5 po szturchnięciu 21:06.**
Świeży fetch/rebase: main30ad7d3 bez zmian. Pełny build/WERROR i CTest
**115/115,290,39 s**, **706 przypadków/26 375 asercji native**,
**1295 Python/0 skips**. Nowe6 grup native: **47/47 i1910/1910**;
regularny audyt Python **19/19**. Web offline: **85 modułów**.
Niezależne replaye: **OCR MIME3/3,22/22 kontroli** oraz **checkpointy2/2,31/31**.
1203 pliki źródeł i9 binariów mają stabilne hashe przed/po pełnej bramce.
[Pełny aktualny dowód](archive-import-2026-10-04-evidence/native/owner-nudge-2106/mixed-native/README.md).
Istniejący opcjonalny `catalog_scale` wykonał0 przypadków; nie oznaczamy
tego benchmarku jako wykonanego. Brak migracji wszystkich52 grup do profili.
Na starym źródle03cd52e integrator wykazał MIME **1/3**: oba importy z provenance
czytały niezmienne bajty, ale traciły rozszerzenie obrazu. Poprawka zachowuje deklarowany format
osobno od blobu; media API ma już `ocr_bytes`, więc nie trzeba zmian w W11.
Naprawiono kontrolę wersji migracji wewnątrz `BEGIN IMMEDIATE`
i włączono 8 regresji audytu do istniejącego CTest discovery bez zmian CMake.
Pełna dawna linia zachowana pod
`archive/2026-10-04/archive-import-before-mixed-w2` (**03cd52e**).
Historyczne bramki112/112 poniżej dotyczą starego źródła i nie zastępują nowych.

Historycznie, po szturchnięciu właściciela 18:51: fetch, rebase (`up to date`), pełny
build/CTest **112/112 w 307,19 s**, **696/25 341** natywnych przypadków/asercji,
**1276 Python/0 skips**, nowe grupy **37/37 i 876/876**. Niezależny replay
**2/2 scenariusze, 31/31 kontroli**, web TypeScript/Vite: **85 modułów**.
[Nowe receipts/logi/hashe](archive-import-2026-10-04-evidence/native/owner-nudge-1851/receipt.json)
i [niezależny replay](archive-import-2026-10-04-evidence/native/probes/owner-nudge-1851/receipt.json).
Kod/header bytes 4314976 nadal odpowiadają zmierzonemu 1474b260; nowy pełny
build potwierdził brak pracy, a źródła/binaria mają niezmienione hashe.
Była to historyczna gotowość, potem cofnięta po negatywie OCR integratora.

## Poprawki po szturchnięciu 21:06

1. **OCR:** `import_file` i bezpośredni `import_screenshot` z provenance przekazują do
   istniejącego `ocr_bytes` bajty niezmiennego snapshotu i osobno deklarowane
   rozszerzenie źródła. SourceRecord zachowuje `mime` i
   `metadata.declared_image_format`; hash-only blob nadal zawiera identyczne
   bajty. Nie otwieramy ponownie oryginału. Format jest deklarowany nazwą,
   nie potwierdzony dekoderem/sygnaturą; dawne `jpg` wire semantics zachowane.
   Strict mock sprawdza pełne żądanie, MIME/base64, NUL/high bytes, cztery
   obsługiwane formaty i mixed-case, oba API, provenance, nadpisanie/usunięcie
   oryginału po snapshotcie oraz konflikt admission przed dispatch.
   Ukierunkowana próba z historycznym archive i zmienionymi obiektami:
   **przed 2/6; po 6/6 i 872/872 asercji**, bez live calls.
   To próba pomocnicza, nie pełny build po przyjęciu W2. Kod **bf8b620**.
2. **Migracja:** oba klucze wersji ponownie sprawdzane wewnątrz
   `BEGIN IMMEDIATE`, przed DDL tej migracji lub zapisem wersji.
   Deterministyczny writer zapisuje2 po preflight, przed uzyskaniem blokady:
   stary kod miał **6 błędów** (otwarcie/downgrade/DDL, oba klucze), poprawka
   zwraca `Unsupported` i zachowuje2, drugi klucz oraz rekordy.
   Ukierunkowana próba z nowym obiektem i historycznym archive:
   **11/11 przypadków, 189/189 asercji**; niezależny review bez uwag.
   Rdzeńv4 i oba additive schema1 bez zmiany; nie obniżamy nowszych wersji.
   Kod **f18af911**.
3. **Stałe regresje audytu:** 8 zachowanych przypadków przeniesiono z
   evidence do `loom/tools/eval/test_archive_cost.py`; stare11 bez osłabiania.
   Zwykły discovery i adapter istniejącego CTest globu wykonały **19/19**.
   `compat.test_archive_cost` ładuje tę samą suite, bez kopii testów ani zmian
   centralnego CMake. Kod **521ba0c**; grupa pełnego CTest także wykonała19/19.
4. **Głębokość auxiliary/repair (finding INDEX):** prywatny `load_json_doc`
   wymaga jawnego `max_depth`; wszystkie6 ścieżek przekazują
   `ImportOptions.json_max_depth`. Usunięto ukryty default512 z
   `parse_tolerant`. Nowe regresje obejmują preset8, zero/unlimited, payload
   o głębokości550, wznowienie oraz naprawę powiązania z zachowaniem ID.
   Pełny świeży build/WERROR i grupa natywna: **2/2,106/106 asercji**.
   Legacy JSON/JSONL i heurystyka generic finder nadal mają osobne mechanizmy;
   nie jest to claim kompletnej migracji wszystkich 52 grup.

[Publiczne dowody nowych prób](archive-import-2026-10-04-evidence/native/owner-nudge-2106/README.md)
zachowują pełne red/green, dokładne polecenia/hashe i osobny web build
(85 modułów, 79 niezmienionych źródeł). Ukierunkowane próby są uzupełnieniem
osobnej pełnej bramki na rzeczywistym świeżym buildzie.
Niezależne replaye na rzeczywistym świeżym rdzeniu W2+W5 **4e8c3de**
(core SHA256 `d4093c76c886d1b2b30b8bac4f9dc6eeeee5e994dce316dfc7ecc81ff56b2028`):
**OCR MIME3/3,22/22 kontroli**, bez zmiany oryginalnego probe;
**checkpointy2/2 scenariusze,31/31 kontroli**, zachowane ID/projekt/dokument/relacja
oraz jawny niekompletny wynik malformed. Źródła/biblioteki przed/po bez zmian.
[OCR replay](archive-import-2026-10-04-evidence/native/owner-nudge-2106/screenshot-replay/README.md),
[checkpoint replay](archive-import-2026-10-04-evidence/native/owner-nudge-2106/checkpoint-replay/README.md).
Świeży pełny build/WERROR zakończony. Pierwszy pełny CTest **113/115,393,26 s**:
oba błędy przed uruchomieniem serwera (`PermissionError`, mode0644). Samo
przywrócenie0755 ujawniło `Exec format error`: pierwsze64 bajty wygenerowanego
pliku zerowe, przyczyna uszkodzenia nieustalona. Ponowne linkowanie wyłącznie
serwera z niezmienionych obiektów/bibliotek dało poprawny ELF i **2/2**
ukierunkowanych testów. Core SHA i wszystkie źródła bez zmian; testów nie zmieniono.
[Pierwszy negatyw i odtworzenie](archive-import-2026-10-04-evidence/native/owner-nudge-2106/mixed-native-first-failure/README.md).
Nowy pełny przebieg zakończył się **115/115,290,39 s**. Niezależna agregacja
potwierdza wykonane mianowniki, źródła i9 binariów; nie łączymy zaliczonych
fragmentów pierwszego negatywu z drugim przebiegiem.
Nowy fetch po wszystkich4 poprawkach: main30ad7d3 bez zmian, rebase up to date.

## Wdrożone

JSON/ZIP dostawców przetwarza rozmowy strumieniowo, zachowując źródłowe bajty,
pola, kolejność i gałęzie. Rozmowa/provenance/checkpoint zatwierdzają się atomowo;
wznowienie zachowuje ID. CLI zwraca małe pokwitowanie, `--full-result` pełne
metadane; bufor/głębokość/progi są ustawieniami. `complete` → exit0,
`partial` → exit4, Ctrl-C → exit130, potwierdzenie W2 → exit3.

Naprawiono zwrot integratora: surowy zapis członka nie zastępuje poprawnej
interpretacji. Błędne stare markery i cache są ponawiane; zależne rekordy czekają
na zapis rozmów/projektów. Brakujące połączenia uzupełnia się tylko dla
jednoznacznie należących do źródła encji, z zachowaniem ID. Cache sprawdza
rzeczywisty DAG zagnieżdżonych/współdzielonych źródeł. Stare osierocone encje
bez dowodu własności pozostają zachowane i jawnie częściowe; nie podłącza się
podobnych rekordów z innych eksportów.

Adnotacje C++ łączą zakres punktów kodowych Unicode `[start,end)` z
`Database::nodes` i niezmiennym snapshotem pełnej wiadomości.
`recorded|model|user`, rewizje/CAS/wycofanie, hash i diagnostyka dryfu/braków.
Migracje forward-only: adnotacje1/checkpointy1, rdzeńv4 bez zmiany.
Audyt liczy wszystkie statusy/role oraz osobny zakres czytania; estymacja
znakowa tokenów, ceny właściciela lub jawnie nieznane. Python nie gromadzi
tekstów całego archiwum.

## Liczby przed i po

| Pomiar | Przed | Po |
|---|---:|---:|
| Python, 600 tys. wiadomości: RSS | 190 512 KiB | 28 072 KiB (−85,3%) |
| Czas audytu Python | 2,155 s | 6,483 s (3,0× dłużej) |
| Surowe wiersze | 480 tys., bez deleted | 600 tys., wszystkie statusy |
| Regresje Python | 11 istniejących, nowe8 tylko w evidence | 19/19 w zwykłej suite i pełnym CTest |
| Wrapper 64 MiB: RSS / czas | 155 844 KiB / 109,927 s | 25 556 KiB / 49,847 s |
| Tablica 64 MiB: RSS / czas | 24 328 KiB / 69,626 s | 25 920 KiB / 60,149 s |
| Pełny CTest | Historyczne W5:112/112, źródło1474b260 | Aktualne W2+W5:115/115,290,39 s; native706/26 375; Python1295/0skip |
| Nowe grupy natywne | Historyczne37/37,876/876 | Aktualne47/47,1910/1910 |
| OCR reproduktor integratora | MIME1/3 | MIME3/3,22/22 kontroli na actual mixed core |
| Wyścig wersji migracji | 6 błędów (oba klucze) | Unsupported; brak downgrade/DDL,11/11 i189/189 |
| Oryginalne scenariusze checkpointów integratora | Negatyw w archive | 2/2 scenariusze, 31/31 kontroli |
| Rzeczywisty historyczny W2×W5 | 3/3, 44 asercje | 3/3, 47/47 asercji |

64 MiB: 64 rozmowy/128 wiadomości, wszystkie raw payloady/hash/integrity;
wrapper RSS −83,6%, tablica **+6,5%** — negatyw zachowany. Pojedyncze czasy
na współdzielonym hoście, oba buildy `-O0 -DNDEBUG`; nie produkcyjny benchmark.
Pokwitowania wiążą dokładne historyczne binaria, nie późniejsze poprawki.

**Import 2 148 031 323 bajtów wykonano**: po ENOSPC przy 1477 rozmowach
wznowienie dało **2047 rozmów/4094 wiadomości/243 420 znaków**, wszystkie
wcześniejsze ID i źródło zachowane. RSS CLI **19 272 KiB**, czas **168,599 s**;
pełne raw/tekst/relacje/checkpointy/hash sprawdzone. Binarium `9a8b88b`, nie
późniejsza naprawa auxiliary. Plik ma duże odstępy whitespace; nie bada 2 GB
tekstu wiadomości. Tymczasowa kopia 2,15 GB była w tmpfs poza RSS procesu.
ENOSPC, przerwany przebieg i kompletne oryginalne wyjścia pozostają odtwarzalne.

Pierwszy pełny CTest: **110/112**, dwie regresje zgodności poprawiono bez zmian
istniejących testów. Kolejny przebieg przerwano po research timeout przy dużym
obciążeniu; oba logi zachowano. Końcowy wynik jest osobnym pełnym przebiegiem. Istniejący opt-in
`catalog_scale` wykonał 0 przypadków (nieużyty korpus); nowe grupy 37/37,
a Python pełnego przebiegu 1276 przypadków/0 skips.
[Dowody i hashe](archive-import-2026-10-04-evidence/native/README.md),
[pomiary importu](archive-import-2026-10-04-evidence/import/README.md),
[audyt Python](archive-import-2026-10-04-evidence/audit/README.md).

## Granice i niewykonane

Resume skanuje początek; kopiowanie snapshotu nie jest anulowalne w połowie.
C++ domyślnie resume wyłącza, CLI włącza. Pamięć zależy od największej
rozmowy/wartości/wrappera i list podsumowań; auxiliary mogą wymagać DOM.
SQLite+WAL czyta oryginał, ale hash pliku głównego nie wiąże WAL/live query;
spójny backup/cache sidecarów pozostaje. Nie badano prywatnych eksportów,
jakości OCR ani semantyki. Transport OCR i tożsamość bajtów sprawdzono offline
na strict mock; nie jest to ocena dekodowania wszystkich formatów przez model.
Brak adaptera adnotacji GraphPacket/KB, C ABI/UI.
Presety i interpretacja pól pozostają do migracji do packów/profili W11.
[Instrukcja właściciela, API i przekazania](archive-import-2026-10-04-runbook.md).

## Do wątku 2

Warunkowy adapter CLI korzysta z realnego W2, teraz przyjętego na main30ad7d3
z kanonicznym `usage_policy.pack`; brak zależności to `unavailable`.
Podłączyć `preflight/completed` w domyślnych adapterach C ABI/serwera/katalogu,
trwałe ustawienia i wspólny atomic dispatch. Historyczny probe `34cc920` nie
certyfikuje aktualnego startupu; obecna bramka W5 korzysta z przyjętego W2.
W11 powinien wyprowadzać usage defaults z jedynego autorytatywnego dokumentu2,
bez utrzymywania drugiego ręcznego presetu.

## Do wątku 3

Użyć zweryfikowanych `reply_fragment` W4: `[char_start,char_start+char_len)`
na tym samym snapshocie wiadomości, `origin=model`. Zachować osobno strukturalne
pochodzenie kompilatora; cel GraphPacket uzgodnić z4, nie przekazywać jego ID
jako starszego `Database::nodes`.

## Do wątku 4

Przyjąć opublikowany packet-side `loom.method_graph/1` / `loom.method_run_trace/1`
jako podstawę wspólnego adaptera i uzgodnić wersjonowany uchwyt celu adnotacji.
Importer/metoda/wersja/przebieg oraz wynik→run/wersja mają być rzeczywistymi
bytami/krawędziami grafu. Oceny — osobne twierdzenia z dowodami i datą;
sam JSON diagnostyczny W5 tego nie realizuje. Wspólna regresja3/4 nadal wymagana.

## Do wątku 9

**Odebrano aktualny INDEX/main30ad7d3 i nowy negatyw OCR6e4bf03.**
**Gotowy do odbioru ten przyrost:** OCR/migrację poprawiono, regresje audit są
zwykłymi testami; auxiliary/repair honorują caller depth. Pełny CTest115/115,
web85 oraz niezależne replaye3/22 i2/31 zielone na source4e8c3de/main30ad7d3.
Receipt w `native/owner-nudge-2106/mixed-native/receipt.json` przypina dokładne
źródła/core/binaria; current full log SHA256
`4df3d75e30a18fede43b0e78c7702b12c6f0f9b95781f8cc2ce0d63afd91fe34`.
Kod nie zmienił się po bramkach; tylko raporty/dowody. Pierwszy nieudany
mixed przebieg i naprawa wygenerowanego serwera zachowane osobno.
Rebase37 commitów czysty, każdy opublikowany;
oryginalny03cd52e zachowany w archiwum. INDEX/main/STATE pozostają własnością9.

Nowe próby odtwarzają oba oryginalne reproduktory bez osłabiania kontroli
(jedyna adaptacja: jawny `resume=true` po zachowaniu starego presetu C++).
W2 przed W5; ponowić fetch/rebase, fullCTest i build web przed fast-forward.
Main/STATE/README nie edytowano. Historyczna linia z negatywami zachowana na
`archive/2026-10-04/archive-import-before-main-refresh`.

## Do wątku 11

Odebrano **52 grupy** inwentarza W5 i API `RuntimeProfile`/nakładek/nullable.
Brakuje packów importu/audytu: dostarczyć `import-formats`, profile dostawców,
role/tekst i presety, bez drugiego loadera. W5 nie edytuje cudzych `loom/data/**`.
Potem podłączyć dane w naszym zakresie z porównaniem domyślnych wyników,
wiążąc efektywny hash wersji metody/przepisu/parametrów z grafem4.
Odebrano nowe 6116664: CLI profile API istnieje, lecz packów import/audytu brak.
DIC0515, wspólny streaming conversations/projects/memories z archive i
parametry get_unanalysed_msgs pozostają kolejnymi przyrostami w naszym zakresie;
konsument archive/worker oraz dane profilu należą do11. Konkretna granica
i obecny Loader/API są opisane w instrukcji.
