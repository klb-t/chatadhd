# Wątek 5 — instrukcja importu archiwów i API adnotacji

Gałąź: `gpt/archive-import-2026-10-04`. Baza kodu:
`161cc22dfb84fe863389d6b90323bd44516a68dc`, z profilami aplikacji. Po rebase
na main `7282437b1c88933977f64b3468b9f42f7b400494` (dokumenty integratora).
[Krótki raport](archive-import-2026-10-04.md) zawiera wyniki i status odbioru.
Wszystkie dotychczasowe pomiary są offline i dotyczą danych syntetycznych;
nie wykonano płatnych wywołań modeli.

## Import u właściciela

Eksporty i katalog danych trzymaj poza publicznym repozytorium. Istniejący
katalog pozwala najpierw obejrzeć jednostki i wybrać locatory:

```bash
loom --data-dir /private/chatadhd-data catalog scan --source /private/takeout.zip
loom --data-dir /private/chatadhd-data catalog list --limit 20
loom --data-dir /private/chatadhd-data catalog show UNIT_ID
```

Wybór przez `catalog include/exclude/pin`, a następnie
`catalog import --mode selective`, pozostaje istniejącym przepływem katalogu.
Ten wątek nie zmienia jego jakości selekcji ani automatycznie nie instaluje
nowych hooków polityki zużycia w importerze katalogowym.

Pełny import świadomie wskazanego eksportu dostawcy:

```bash
loom --data-dir /private/chatadhd-data --json --quiet import /private/takeout.zip --audit
loom --data-dir /private/chatadhd-data --json --quiet import /private/conversations.json --export-mode on --audit
```

ZIP w trybie `auto` korzysta z bezstratnej ścieżki eksportów dostawców.
Dla samodzielnego JSON-a eksportu wybierz `--export-mode on`. Tryb `off`
zachowuje wcześniejsze spłaszczanie. Dla JSON/ZIP dostawców surowe bajty trafiają do zachowanej kopii
źródła; przy domyślnym zapisie provenance parser czyta niezmienny blob zamiast
ponownie otwierać zmieniający się oryginał. SQLite z aktywnym WAL stanowi
oddzielną granicę opisaną poniżej.

Tablice JSON i obiekty z polem `conversations` są czytane rozmowa po rozmowie.
Pola wrappera przed i po tablicy, indeksy źródłowe, kolejność wiadomości,
rozgałęzienia i nieznane pola pozostają zachowane. Małe pokwitowanie CLI
ogranicza wyłącznie zwracane metadane; pełne źródło i zapis w bazie pozostają.
`--full-result` przywraca pełne metadane w odpowiedzi.

## Przerwanie i wznowienie

Każda rozmowa, jej provenance i checkpoint zatwierdzają się atomowo. Dziennik
źródła zachowuje identyfikatory po przerwaniu/wyjątku/awarii oraz koordynuje
równoległe importy tego samego źródła. Rozpoznane pomocnicze rekordy dostawców
w ZIP-ach również mają checkpointy. Surowe zachowanie członka (`-2`) jest
oddzielne od zatwierdzonej interpretacji (`-1`): błąd interpretacji nie staje się
trafieniem cache. Naprawa starych błędnych markerów zachowuje źródło i ID.
Zależne rekordy czekają na pomyślny zapis rozmów/projektów; nowe encje mają
`export.source_id`, a certyfikacja powiązań członka ma `binding_version=1`.
Przy starszych niecertyfikowanych zapisach importer dopisuje brakujące krawędzie
wyłącznie przy jednoznacznym potwierdzeniu własności encji przez to źródło.
Stare osierocone encje bez oznaczenia źródła pozostają zachowane i jawnie
`partial/Conflict`; podobny rekord z innego eksportu nie jest bezpiecznym dowodem
ich tożsamości. Cache nadrzędnego ZIP-a także sprawdza rzeczywiste `parts[].source_id`, w tym współdzielone źródła; stare kontenery bez tych ID są ponawiane, aby je zapisać.

Ctrl-C zatrzymuje import między rekordami; CLI zwraca kod **130**, zachowując
zatwierdzoną pracę. Zakończenie `complete` zwraca **0**, `partial` — **4**;
pokwitowanie JSON pozostaje dostępne także przy `--audit`. CLI ma preset `resume=true`: powtórz polecenie albo dodaj
`--resume`. Biblioteka C++ zachowuje domyślne `ImportOptions.resume=false`,
aby nie zmieniać historycznej obsługi powtórzeń; jej wywołujący jawnie
włączają `resume=true`. Wymagane są te same
bajty źródła, tożsamość parsera i zgodny marker checkpointu. Parser przy
wznowieniu ponownie skanuje początek źródła i pomija już zatwierdzone rekordy;
checkpoint nie oznacza bezpośredniego skoku do bajtowego offsetu.

Tworzenie kopii snapshotu źródła nie jest obecnie anulowalne w połowie
kopiowania. Obsługa Ctrl-C między rekordami nie obejmuje tego etapu.
`--no-resume` jawnie wyłącza checkpointy. `--force` tworzy osobny import;
nie służy do odzyskiwania przerwanej pracy. Starsze niepełne importy bez nowych
checkpointów nie są automatycznie przejmowane. Resume dla spłaszczania,
heurystycznych/nieznanych formatów i dowolnych archiwów niedostawców pozostaje
ograniczone do ich wcześniejszego zachowania.

## Ustawienia i estymacja

Wartości są presetami wywołującego, bez ograniczenia do wcześniejszych wartości
maksymalnych. CLI przyjmuje dodatni rozmiar bufora i nieujemne pozostałe
wielkości, w zakresie reprezentacji użytych typów.

| CLI | Pole `ImportOptions` | Domyślnie / znaczenie |
|---|---|---|
| `--json-read-chunk-bytes N` | `json_read_chunk_bytes` | 65 536 bajtów; wymagane `N > 0` |
| `--json-max-depth N` | `json_max_depth` | 512; `0` wyłącza sufit preflight |
| `--json-inline-threshold-bytes N` | `json_inline_threshold_bytes` | 1 000 000 bajtów; próg projekcji JSON |
| `--generic-inference-max-bytes N` | `generic_inference_max_bytes` | 64 000 000 bajtów; `0` bez limitu tego kroku |
| `--resume` / `--no-resume` | `resume` | CLI: `true`; biblioteka C++: `false`, jawny opt-in |
| `--full-result` | `include_result_metadata` | CLI domyślnie pomija pełne metadane w wyniku; C++ zachowuje domyślnie `true` |
| `--export-mode auto\|off\|on` | `export_mode` | `auto` |

`ImportOptions` udostępnia też m.in. `record_provenance`, `cancel`, `progress`,
`expected_source_hash`, `expected_source_bytes` i hooki `preflight`/`completed`.
Istniejące C ABI i parsowanie opcji serwera nie otrzymały pełnego adaptera tych
nowych opcji w tym wątku.

`import --audit` liczy znormalizowany tekst wiadomości, wszystkie statusy/role
oraz oddzielny zakres projektowany. `--audit-scope all|active` wybiera zakres;
`--audit-chars-per-token-low` i `--audit-chars-per-token-high` ustawiają
charakterową estymację tokenów (presety 4 i 3 znaki/token), a
`--audit-output-ratio` przewidywany stosunek wyjścia do wejścia (preset 0,4).
Obie ceny `--audit-input-price`/`--audit-output-price` podaje właściciel w USD
za milion tokenów. Bez nich koszt modelu pozostaje nieznany. Audyt nie uruchamia
tokenizera ani modelu. JSON źródłowy, załączniki, OCR i narzut promptu są poza
estymacją tekstu. Koszt wywołań modelu podczas lokalnego importu wynosi zero;
kosztów lokalnego CPU/dysku nie zmierzono.

Oddzielny audyt istniejącej bazy, bez importowania:

```bash
python3 loom/tools/eval/archive_cost.py --db /private/chatadhd-data/chatadhd.db --scope all --json
```

Python obsługuje zakresy `all|active|legacy`, `--no-tools`, `--no-versions`,
`--no-unknown-status`, `--include-excluded`/`--no-include-excluded` i
`--include-deleted`/`--no-include-deleted`. Można ustawić
`--chars-per-token-low`, `--chars-per-token-high`, `--fraction`,
`--output-ratio`, `--prefix-tokens` i `--sqlite-cache-kib`.
`--input-price`/`--output-price` oraz opcjonalne `--prefix-price` są cenami
właściciela; `--pricing` jawnie wskazuje własną lub historyczną tabelę.
Nie pobieramy ani nie deklarujemy aktualnych cen. Cache i rabaty tabeli są
niezweryfikowanymi założeniami planistycznymi.

Audyt Python zachowuje stare aliasy wyników, które wyłączają usunięte wiersze;
nowi odbiorcy powinni używać `raw` i `projected`. Liczenie obejmuje Unicode i
NUL. SQLite zapisuje lokalnie w plikach tymczasowych identyfikatory, role/statusy
i liczniki; Python nie gromadzi tekstów ani listy obiektów wszystkich rozmów.
Pojedyncza wiadomość może zostać zmaterializowana przy liczeniu jej tekstu.

## Warunkowy adapter polityki wątku 2

Adapter CLI zaczyna korzystać z rzeczywistego nagłówka/implementacji wątku 2
po ich integracji. Korzysta ze wspólnego `<data-root>/usage-policy.sqlite`,
zapisuje preflight z hashem i szacunkiem bajtów źródła oraz zmierzone bajty
snapshotu. Przy skonfigurowanym wzroście zużycia (preset ×10) zatrzymuje się
przed zapisem wierszy źródła/rozmów. Potwierdzenie wiąże dokładne pokwitowanie,
identyfikator operacji i referencję autoryzacji właściciela. Zmiana tożsamości
lub wielkości pliku po dopuszczeniu przerywa import przed tymi zapisami.

Na gałęzi bez zależności wątku 2 odpowiedź ma
`usage_policy.status: unavailable`; nie oznacza to działającego strażnika.
Po integracji pauza wymagająca potwierdzenia zwraca **3** i
`import_started:false`. Użyj dokładnych wartości zwróconych przez CLI:

```bash
loom --data-dir /private/chatadhd-data --json import /private/takeout.zip \
  --usage-operation-id RETURNED_ID --usage-confirm-receipt RETURNED_RECEIPT \
  --usage-confirmation-ref OWNER_AUTHORIZATION_REFERENCE
```

`--usage-baseline-key` pozwala wybrać tożsamość bazy porównania. Adapter mierzy
bajty wejściowego źródła, nie ekspansję magazynu, CPU, pamięć ani wydatki
analizy/modelu. Sam hash dopuszczenia wymaga lokalnego odczytu strumieniowego.
Natywni wywołujący instalują hooki `ImportOptions`; domyślne ścieżki
C ABI/serwera/katalogu wymagają osobnego podłączenia.

Osobny probe rzeczywistego W2, przypiętego do `34cc920`, z adapterem W5 na
`f50d016` wykonał **3/3 przypadki, 47/47 asercji** i zero wywołań dostawców.
Sprawdził żądanie potwierdzenia przed wierszami, odrzucenie innego pokwitowania,
przyjęcie dokładnej decyzji i trwały zapis zmierzonego zakończenia. Zaobserwowany
wzrost wynosił 124,395×; nie jest to nowy test granicy dokładnie ×10. Pierwszy
wynik 3/3 i 44/44 zachowano oddzielnie. Źródła, rzeczywisty core, binarium,
potwierdzenia i usunięte odtwarzalne link-mapy mają powiązane hashe w
[dowodzie W2×W5](archive-import-2026-10-04-evidence/w2-integration/README.md).
Ten probe nie oznacza połączonego pełnego CTest ani adaptera serwera/UI.

## Adnotacje wiadomość → węzeł

Publiczne deklaracje C++: `loom/include/loom/db.h`. Obecny `node_id` odnosi się
do starszej tabeli `Database::nodes`; nie jest uchwytem kanonicznego bytu
GraphPacket ani rekordu `KnowledgeStore`. Przekazanie ich identyfikatora nie
zastępuje adaptera między tymi reprezentacjami.

| Metoda `Database` | Zachowanie |
|---|---|
| `create_message_annotation` | Wiąże istniejącą wiadomość/węzeł, zakres i pochodzenie; opcjonalnie sprawdza `expected_text_hash` |
| `get_message_annotation` | Odczytuje bieżącą rewizję i diagnostykę powiązań |
| `list_message_annotations` | Paginacja po `after_id`; limit jest presetem, `nullopt` oznacza bez limitu |
| `revise_message_annotation` | Dopisuje korektę/wycofanie z CAS `expected_revision` |
| `get_message_annotation_history` | Odczytuje zachowane rewizje |
| `get_message_annotation_source` | Odczytuje niezmienny snapshot wiadomości |

Zakres `[start_char, end_char)` oznacza punkty kodowe Unicode, a nie bajty ani
klastry grafemowe. Offsety bajtowe i cytat wyprowadzane są z zachowanego tekstu.
Pochodzenie `recorded|model|user` jest oddzielne od klas dowodu/pochodzenia
tez grafu. Snapshot obejmuje pełny natywny `Message`: tekst, metadane,
załączniki i identyfikatory wersji; nie zastępuje provenance bajtów archiwum.
Snapshoty są deduplikowane po wiadomości i hashu pełnego rekordu.

Edycja wiadomości nie przekierowuje istniejącej adnotacji na nowy tekst.
Usunięcie wiadomości lub węzła nie usuwa utrwalonego dowodu; stan brakujących
powiązań i dryfu jest diagnozowany. Korekty oraz wycofania pozostawiają historię.
Migracje są addytywne, forward-only:
`loom_message_annotation_schema_version=1`,
`loom_import_checkpoint_schema_version=1`; rdzeń v4 i ogólne Loom v1 pozostają.
Ten wątek nie dodaje C ABI ani UI dla adnotacji.

## Granice i dowody

Pamięć importera zależy od największej rozmowy/wartości JSON, metadanych
wrappera oraz list podsumowań/identyfikatorów rozmów. Nie jest stała względem
liczby rozmów. Pomocnicze pliki dostawców i ogólne struktury heurystyczne mogą
nadal wymagać DOM. Elementy ZIP materializują się na dysku; snapshoty i baza
znormalizowana wymagają przestrzeni magazynowej.

Nie wczytywano prywatnych wielogigabajtowych eksportów. Testy syntetyczne nie
ustalają jakości semantycznej ich interpretacji. Reference-only retention,
watch/eksport źródłowych serwisów, etapy analizy archiwum, jakość wyboru
katalogowego i okna zapytań KB pozostają innymi zakresami.

[Dowody audytu Python](archive-import-2026-10-04-evidence/audit/README.md)
obejmują zamrożoną publiczną bazę, przenośny benchmark, 11 istniejących testów
i 8 dodatkowych regresji. Dla 600 tys. wiadomości RSS spadł z 190 512 do
28 072 KiB (−85,3%); czas wzrósł z 2,155 do 6,483 s (3,0×). Pełniejszy
zakres surowych liczników nie jest porównaniem wyłącznie tej samej ilości pracy.

### Zmierzony import natywny — 64 MiB

Pomiary dotyczą dokładnej bazy `161cc22` i binarium W5 z `f50d016`, oba
`Release -O0 -DNDEBUG`, WERROR. Późniejsze poprawki parsera/retry nie są objęte
tymi wcześniejszymi binariami. Korpus: 64 rozmowy, 128 wiadomości, 7 404 znaki
tekstu i 512 KiB surowych metadanych na wiadomość; wariant wrapper ma
67 161 202 bajty, tablica 67 161 093.

| Pierwszy import | RSS przed / po (KiB) | Czas przed / po (s) |
|---|---:|---:|
| Wrapper | 155 844 / 25 556 | 109,927 / 49,847 |
| Tablica | 24 328 / 25 920 | 69,626 / 60,149 |

RSS wrappera spadł o **83,6%**, a tablicy wzrósł o **6,5%**: wynik negatywny
zachowano. Są to pojedyncze czasy przy współdzielonym obciążeniu, bez deklaracji
kontrolowanego rozkładu przepustowości. Wszystkie bazy mają poprawne integrity,
liczbę rozmów/wiadomości/znaków, hash oryginalnego bloba i komplet 128 surowych
payloadów; W5 dodatkowo weryfikuje 64 checkpointy i indeksy źródła/wiadomości.
Powtórzenia ukończonego źródła: wrapper 4,012 s/13 868 KiB, tablica
6,026 s/13 724 KiB. To trafienia cache, nie pomiar przerwanego resume.

Źródło wrappera: `f5819bf76e8586f7239456b157eb94edba9e79406a48239f5b644147538bdfb9`;
tablicy: `64801ef63d61e3261698c474121b7b3e730402ec0c64490c12b0b8e3e22fa983`.
Binarium bazy: `7ea6f01631cd81e0a853d5dd43a523713a49511a87148c29b5bda4d20eddf71e`;
W5: `0c8c54d1b9f29b7bb37fa917b86a78003511b07f757b41bd7a6e253643ab3742`.
Pełne [pokwitowania importu](archive-import-2026-10-04-evidence/import/) wiążą
parametry, hashe i zachowane stdout/stderr/czas. RSS obejmuje proces CLI,
snapshot, interpretację i wynik; nie obejmuje cache stron systemu plików.

Fixture **2 148 031 323 bajtów** został zaimportowany po wznowieniu rzeczywistego
niepełnego importu spowodowanego ENOSPC: **2047 rozmów, 4094 wiadomości,
243 420 znaków**, wszystkie wcześniejsze ID zachowane. RSS CLI **19 272 KiB**,
czas **168,599 s**, exit 0/complete; źródło, raw wiadomości, tekst, checkpointy,
spany źródła i relacje przechodzą weryfikację. Binarium pomiaru: `9a8b88b`;
nie jest to ponowny pomiar późniejszych poprawek checkpointów pomocniczych.
Plik ma duże odstępy whitespace; nie modeluje 2 GB tekstu wiadomości.
Tymczasowa kopia 2,15 GB korzystała z tmpfs poza RSS procesu, zachowany blob
pozostał na dysku. [Pełne pokwitowania i granice pomiaru](archive-import-2026-10-04-evidence/import/README.md#executed-215-gb-import-and-recovery)
zachowują też wynik ENOSPC i dokładną wersję uruchomionego weryfikatora.

### Stan odbioru natywnego

Pierwszy pełny CTest uruchomił 112 pozycji: **110 przeszło, 2 nie przeszły**
(`test_import_exports`, `test_import_source_materialization`), 359,99 s.
[Pierwszy nieudany log](archive-import-2026-10-04-evidence/native/ctest-first-failed.txt)
pozostaje bez zmian; były to regresje zgodności implementacji, nie ENOSPC.
Końcowy pełny przebieg na 1474b260: **112/112, 316,20 s**,
696 przypadków natywnych/25 341 asercji, 1276 Python/0 skips.
Jeden istniejący opcjonalny catalog_scale wykonał 0 przypadków; nie oznaczamy
tego korpusu jako wykonanego. Log/hashe/receipt oraz źródła przed/po są
w katalogu dowodów native.
Końcowe focused resume wykonało **23/23 przypadki i 696/696 asercji**;
z adnotacjami 10, audytem 3 i brakiem W2: 1 daje **37/37 przypadków, 876/876 asercji**.
Końcowe dokładne próby integratora: **2/2 scenariusze,31/31 kontroli**;
CLI complete/partial ±audit:4/4. Starsze przebiegi pozostają osobno.
[Przenośna weryfikacja natywna](archive-import-2026-10-04-evidence/native/README.md)
odrzuca zerowe nowe filtry i rejestruje rzeczywiste liczniki.
Bazowe 108/108 pochodzi
z osobno przypisanego publicznego dowodu W2, a nie powtórzenia przez W5.

Adnotacje wskazują obecne rekordy `Database::nodes`; identyfikatory GraphPacket lub kanonicznej KB wymagają adaptera albo materializacji w tej tabeli (poza tym wątkiem). Parser snapshotów korzysta z niezmienności istniejącego magazynu blobów; naprawa uszkodzonego magazynu pozostaje zadaniem warstwy BlobStore.


## SQLite + WAL: granica snapshotu i cache

Odczyt SQLite z aktywnym WAL zachowuje ścieżkę do oryginalnej bazy
wraz z jej WAL; regresja odczytu i zachowania nazwy należy do testów resume. Sam zachowany hash
pliku głównego SQLite nie wiąże wyniku zapytań do niezmiennego stanu całej bazy.
Nie obejmuje aktywnego WAL i może pozostać taki sam po późniejszych zapisach do
WAL. Ponowne użycie cache wyłącznie po tym hashu może więc pominąć zmieniony
stan bazy. Nie deklarujemy pełnego snapshotu SQLite na podstawie kopii samego
pliku głównego. Spójny backup SQLite oraz jawna provenance plików towarzyszących
pozostają zadaniem do wykonania, niezależnie od poprawnego odczytu bieżącego WAL.

## Przegląd przekazań innych wątków

Sprawdzono raporty głównego `docs/reports/` na wszystkich pobranych gałęziach
`origin/gpt/*-2026-10-04`. Stan poniżej jest przypięty do ich wskazanych tipów;
nie jest odbiorem ich przyszłych zmian. W7 nie miał raportu w tym katalogu,
a gałąź profili pozostawała na bazie `161cc22`. Odświeżony przegląd W11 na
`b88154c2caaeac676a4fbd19aefcc60eb546ff20` znalazł **52 grupy** w
[`data-in-code/thread-5.md`](https://github.com/klb-t/chatadhd/blob/b88154c2caaeac676a4fbd19aefcc60eb546ff20/docs/reports/data-in-code/thread-5.md),
SHA-256 `dc39d06126810c2c52c4fc358863021c6c7c02cc499f3b5a8d38053de32b1f3c`.
To inwentarz starej bazy `161cc22`, nie dowód migracji nowych zmian W5.

| Wątek / źródło | Znaczenie dla importu i adnotacji |
|---|---|
| W2 / `910a1d6` (historyczny probe: `34cc920`) | Rzeczywisty lifecycle `request/confirm/complete/cancel`; 19 grup kontraktu poza CTest. Nowy JSON dispatcher jest tylko w kernelu statycznym. Ustawienia top-level zastępują cały obiekt, a otwarta instancja polityki zachowuje własny snapshot. |
| W3 / `1c4c8a1` | Pierwszy selektor vectors/cache/type_goal oraz wspólne memory/graph/KB dostarczone; rozszerzony rejestr metod nadal otwarty. Potrzebny adapter fragmentów odpowiedzi grafowej do snapshotu wiadomości i adnotacji `origin=model`, ze sprawdzonymi spanami źródła. |
| W4 / `daa6d42` | `loom_packet` i packet-side `loom.method_graph/1` / `loom.method_run_trace/1` są opisane i sprawdzone natywnie; produkcyjny adapter W3 i wspólny uchwyt celu adnotacji nadal otwarte. |
| W9 / main `7282437` | W5 zwrócony za replay checkpointów pomocniczych; poprawki i nowe próby są na tej gałęzi. W4 ma replay fix oraz packet-side kontrakt; odbiór czeka na wspólny adapter/regresję W3/W4. Rezerwacja nie jest pozwoleniem na kolejny dispatch. Main przesunął się wyłącznie o dokumentację integratora. |
| W10 / `ccc8bbf` | Widoki źródeł zachowują oddzielenie od bieżącego tekstu; adaptery polityki HTTP, packet UI i trwałych zadań są nadal zależnościami. Nie istnieje gotowy adapter UI adnotacji W5. |
| W1 / `a042ab7` | Zmiany precyzji zachowują obserwacje źródłowe; nie są dowodem jakości interpretacji prywatnych archiwów. |
| W6 / `1fb25ae` | Wektory selekcji mają być związane ze źródłem/profilem; raport jest protokołem, nie ukończonym importerem ani nowym kontraktem adnotacji. |
| W11 / `3cd3f47` (inwentarz: `b88154c`) | 52 grupy W5; `RuntimeProfile`/nullable/nakładki. Nowe dane usage_policy/config/runtime_paths gotowe do adaptera W2; brak packów importu/audytu. |
| W8 / `0c36849` | Szkic README zachowuje wyniki na ich historycznej bazie; nowe możliwości W2–W5 opisuje się dopiero po odbiorze. |

Wspólny kontrakt W3/W4 musi wskazać reprezentację celu (GraphPacket/KB) i
wersjonowany uchwyt tożsamości, encje metod/wersji/przebiegów, hash promptu oraz
przepisu, parametry/preset i krawędź wyniku do konkretnego przebiegu/metody
(np. uzgodnione `produced_by`). Packet-side wzorzec jest już dostarczony w
[`METHOD_GRAPH.md`](https://github.com/klb-t/chatadhd/blob/daa6d428ded19ef33072ce8ab1699346789a8aa5/loom/src/packet/METHOD_GRAPH.md):
`loom.method_graph/1` oraz `loom.method_run_trace/1`, natywne Entity/Claim/Observation,
rzeczywiste krawędzie wynik → run / wersja metody / kompilator, canonical JSON dla
definicji i hash dokładnych bajtów promptu. Nie jest operacją dispatcher-a ani
ukończonym produkcyjnym adapterem W3/W5. W5 wymaga jeszcze wersjonowanego
uchwytu celu adnotacji oraz materializacji śladu importera w tej reprezentacji. Oceny metod muszą być twierdzeniami o tych bytach, z dowodami
i datą; metadane diagnostyczne nie zastępują takiego zapisu. W5 nie nadaje dowolnym identyfikatorom pakietu znaczenia starszego
`Database::nodes` ani nie przyjmuje automatycznie danych modelu do KB.

## Inwentarz W5 do W11: dane pozostające w kodzie

| Obszar | Stan obecny i granica przeniesienia |
|---|---|
| Presety importu | `ImportOptions`: rozmiar chunku, głębokość, progi inline/inference, stream threshold, resume i projekcja wyniku nadal mają domyślne wartości w C++. Są ustawieniami, nie przeniesionym kompletnie pakietem danych. |
| Schematy eksportów | Rozpoznawanie pól dostawców, mapowanie ról/bloków/gałęzi i ich statusów nadal jest w `src/import/export_*`. Nieznane pola pozostają zachowane; nie oznacza to, że interpreter schematów jest już konfigurowalny z danych. |
| Audyt tekstu | Presety znaków/token, stosunek wyjścia oraz klasy statusów i ról narzędziowych pozostają w kodzie natywnym/Python. Ceny i zakres są jawnie wybierane przez wywołującego. |
| Adnotacje | Typowane DTO i znaczenie zakresów/rewizji są w C++; rozszerzenia metadata pozostają otwarte. Adapter celu GraphPacket/KB, uchwyty metody i przepisu wymagają wspólnego kontraktu. |
| Provenance metody | Istnieją source/blob hash i tożsamość parsera/checkpointu. Metoda jako byt grafu, wersja/hash przepisu i parametrów oraz run/`produced_by` nie są dostarczone samym JSON-em diagnostycznym. |

W11 powinien przypisać te elementy do właściwych właścicieli i uzgodnić ich
wersjonowany opis w grafie. W5 nie posiada `loom/data`, schematu profili ani
ogólnego modelu metod. Migracja powinna zachować weryfikację spanów, hashy,
referencji, CAS i atomowości; te operacje nie są zamiennymi presetami domeny.


W11 proponuje dla wykrywania/dispatch/MIME/wrapperów pack `import-formats`, dla
ról/tekstu/OCR/HTML profile `text-chat`, `html`, `sqlite`, a dla pól dostawców
`openai-export`, `anthropic-export`, `generic-export`. Progi/głębokości trafiają
do presetu importu; FTS/snippety do danych wyszukiwania. DIC0515 obejmuje CLI,
DIC0682 audyt Python. Dostępny `RuntimeProfile` obsługuje builtins/definicje,
overrides/values, RFC6902 patch i nakładkę `<root>/profiles/<domain>.pack`, z
rewizją/schematem/hashem efektywnych danych; typy nullable mogą korzystać z unii, np. `["integer","null"]`. Domyślne dane są w
`loom/data/runtime/<domain>.pack`, generatorze `gen_runtime_profiles.py`.
W5 nie edytuje tych cudzych ścieżek: potrzebny jest pack/schemat od W11 oraz
porównanie identyczności domyślnych wyników przed podłączeniem. Nie tworzymy
drugiego loadera. W2 także opisuje swoje stare dane jako
`legacy_code_pending_pack_migration`; jego opaque refs w zdarzeniach nie są
same w sobie encjami/krawędziami metod w grafie.


W4 `reply_fragment` przyjmuje oryginalny base packet i kompilację oraz dokładnie
jeden adres `{local_id}` lub `{node_id}`; ponownie kompiluje zachowane bajty.
Adapter W3→W5 powinien użyć `[char_start, char_start+char_len)` w **tym samym**
snapshocie wyświetlonej wiadomości, z `origin=model`, pozostawiając osobno
strukturalne pochodzenie kompilatora/systemu. Byte spans nie są indeksami UTF-16.
Do9: wcześniejsza linia W5 z negatywami i historycznymi binariami jest dostępna
na `archive/2026-10-04/archive-import-before-main-refresh`; rebase zachował kod,
a wszystkie wyniki są przypięte do faktycznie zmierzonych bajtów.


Ostatni fetch: W11 `3cd3f47` dostarczył dane `usage_policy`, `config` i
`runtime_paths` (łącznie 19 packów), z golden wartościami oraz izolowanym
14/14 receipt autora. To dane do podłączenia przez W2, nie już aktywny
bootstrap/adapter na main. Domyślna polityka ma float `10.0`; shallow override
Config zachowuje usunięcia baseline, a native walidacja W2 jest nadal konieczna.
Profil importu/audytu pozostaje zadaniem W11. W4 `8e0e86b` po ostatnim rebase
zmienił dokumenty/dowody; odczytany wcześniej kontrakt metod `daa6d42` pozostał
identyczny. Te nowe cudze bramki nie są certyfikowane własnym CTest W5.


## Aktualizacja przekazań po szturchnięciu 18:51

W11 `6116664` ma już rzeczywiste CLI `profile list/inspect/validate/save`;
nie oznacza to istniejących packów importu/audytu ani wdrożenia ich przez W5.
W4 `14eccaf` dopisał parameter-set oraz combination i rzeczywiste krawędzie,
z dokładnymi efektywnymi definicjami. Wspólny producent/fixture W3 jest nadal
warunkiem odbioru3/4; adnotacje W5 nadal wskazują starsze `Database::nodes`.

Konkretne kolejne zadania W5 z W11:

- **DIC0515:** wspólna schema/capabilities/validation `ImportOptions`, nazwy
  `export-mode auto/off/on`; profil importu/audytu potrzebny od właściciela danych.
- **Stream wrapperów:** obecne `xport::Loader` / `load_json_file` w
  `loom/src/import/export_internal.h` strumieniuje tablicę i wrapper conversations,
  z callbackami, anulowaniem i jawnymi statystykami błędów. Projects/memories
  pozostają auxiliary DOM. Współdzielony interfejs i parametry kluczy/ścieżek
  wymagają kolejnego przyrostu w import, a wywołanie/fallback w archive — W11.
- **Pending messages:** `Database::get_unanalysed_msgs(int limit=100)` i
  `length(text)>=20` są odziedziczonym presetem do przeniesienia. Uzgodnić z W11
  parametr/pochodzenie aktywnego profilu worker; nie nadpisywać jego konsumenta
  `worker/**` ani kopiować autorytatywnych defaults do drugiego miejsca.

To otwarte dalsze przyrosty, nie blokery odtworzonej poprawki checkpointów.
Nowa sesja potwierdza gotowość wskazanego przyrostu import/adnotacje po pełnych
bramkach; nie deklaruje migracji wszystkich 52 grup inwentarza.
