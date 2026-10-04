# Wątek 5 — import archiwów i adnotacje, 2026-10-04

Gałąź: `gpt/archive-import-2026-10-04`; świeży rebase na main **30ad7d3**
(przyjęty W2 z danych i wymagania R39–R41), opublikowany jako **a343a8c**.
Historyczny zmierzony kod: **1474b260582a80f006f99953baaad1b3253ca6e1**.
Zakres 5, dane syntetyczne/publiczne, offline: **zero płatnych wywołań**.

**Wstrzymany po nowym niezależnym blokerze OCR (szturchnięcie 21:06).**
Integrator wykazał MIME **1/3**: oba importy z provenance czytają niezmienne
bajty, ale tracą rozszerzenie obrazu. Poprawka zachowuje deklarowany format
osobno od blobu; media API ma już `ocr_bytes`, więc nie trzeba zmian w W11.
Równolegle naprawiamy kontrolę wersji migracji wewnątrz `BEGIN IMMEDIATE`
i włączamy 8 regresji audytu do istniejącego CTest discovery bez zmian CMake.
Pełna dawna linia zachowana pod
`archive/2026-10-04/archive-import-before-mixed-w2` (**03cd52e**).
Dotychczasowe bramki poniżej dotyczą starego źródła; nowych jeszcze nie wykonano.

Po szturchnięciu właściciela 18:51: świeży fetch, rebase (`up to date`), pełny
build/CTest **112/112 w 307,19 s**, **696/25 341** natywnych przypadków/asercji,
**1276 Python/0 skips**, nowe grupy **37/37 i 876/876**. Niezależny replay
**2/2 scenariusze, 31/31 kontroli**, web TypeScript/Vite: **85 modułów**.
[Nowe receipts/logi/hashe](archive-import-2026-10-04-evidence/native/owner-nudge-1851/receipt.json)
i [niezależny replay](archive-import-2026-10-04-evidence/native/probes/owner-nudge-1851/receipt.json).
Kod/header bytes 4314976 nadal odpowiadają zmierzonemu 1474b260; nowy pełny
build potwierdził brak pracy, a źródła/binaria mają niezmienione hashe.
To gotowość tego przyrostu, nie zakończenie 52 migracji danych/profili.

## Poprawki po szturchnięciu 21:06

1. **OCR:** `import_file` i bezpośredni `import_screenshot` przekazują do
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
   `BEGIN IMMEDIATE`, przed jakimkolwiek DDL lub zapisem wersji.
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
   centralnego CMake. Kod **521ba0c**; pełny CTest jeszcze przed nami.
4. **Głębokość auxiliary/repair (finding INDEX):** prywatny `load_json_doc`
   wymaga jawnego `max_depth`; wszystkie6 ścieżek przekazują
   `ImportOptions.json_max_depth`. Usunięto ukryty default512 z
   `parse_tolerant`. Nowe regresje obejmują preset8, zero/unlimited, payload
   o głębokości550, wznowienie oraz naprawę powiązania z zachowaniem ID.
   WERROR syntax zielony; native execution czeka na pełny build.
   Legacy JSON/JSONL i heurystyka generic finder nadal mają osobne mechanizmy;
   nie jest to claim kompletnej migracji wszystkich 52 grup.

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
| Regresje Python | 11 istniejących | 11/11 + 8/8 nowych |
| Wrapper 64 MiB: RSS / czas | 155 844 KiB / 109,927 s | 25 556 KiB / 49,847 s |
| Tablica 64 MiB: RSS / czas | 24 328 KiB / 69,626 s | 25 920 KiB / 60,149 s |
| Pełny CTest | Osobny dowód W2: 108/108 | 112/112, 316,20 s; natywne 696/25 341 asercji |
| Nowe grupy natywne | — | 37/37 przypadków, 876/876 asercji |
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
OCR ani jakości semantycznej. Brak adaptera adnotacji GraphPacket/KB, C ABI/UI.
Presety i interpretacja pól pozostają do migracji do packów/profili W11.
[Instrukcja właściciela, API i przekazania](archive-import-2026-10-04-runbook.md).

## Do wątku 2

Warunkowy adapter CLI korzysta z realnego W2; brak zależności to `unavailable`.
Podłączyć `preflight/completed` w domyślnych adapterach C ABI/serwera/katalogu,
trwałe ustawienia i wspólny atomic dispatch. Historyczny probe `34cc920` nie
certyfikuje nowych presetów/startupu `910a1d6`; W11 `3cd3f47` dostarczył
dane usage_policy/config/runtime_paths do ich podłączenia przez W2.

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
Gotowość starego przyrostu cofnięta do poprawienia OCR/migracji i nowych
bramek na przyjętym W2. Rebase37 commitów czysty, każdy opublikowany;
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
