# DOCX w istniejącym panelu źródeł — 2026-11-01

Raport autora z chwili przekazania. Aktualny wynik integratora znajduje się w
[odbiorze UI](DOCX_UI_INTEGRATION_2026-11-01.md). Poniższe liczby i braki
opisują wyłącznie wykonanie autora, nie końcowy stan produktu.

Status autora: **implemented; mapped-state tested; NOT integrated / NOT native-accepted /
NOT real-browser-accepted / NOT deployed**. Jeden przyrost, UID 992, bez sudo.
Baza odczytana z HEAD: `f527ee5cc34825f38485419e4c11ef7c3ead6ad0`.
Integrator: `01a192f5-2814-7e52-bf0c-32f3428151ed`. Bez commita/push/CI.
Deadline właściciela pozostaje `2026-11-01T17:21:19.260820Z`.

## Produkt

Istniejący `SourcePartsPanel` przyjmuje DOCX jako dokument. Upload zapisuje
oryginał i trwały `source_key`; dopiero przycisk „Odczytaj tekst i utwórz
wystąpienia” wywołuje istniejące `clientBlobText` z `format: docx`. Transport,
autoryzacja, backend oraz reader pozostają bez zmian.

Mapowanie rozróżnia oryginalny ZIP (`derivation.original_sha256`) i pochodny
UTF-8 (`source_sha256` / `Source.blob_hash`). Osobno opisane przyciski istniejącego
`AttachmentOriginal` pobierają i sprawdzają właściwe hashe obu plików. ZIP nie
jest dekodowany jako tekst. Materializacja już odczytanej wersji nadal wysyła
hash oryginalnego DOCX i jej pierwotny poprzednik, a odpowiedź jest wiązana po
zwróconym Source ID, kluczu, oryginale i poprzedniku. Sam wspólny hash tekstu
nie scala dokumentów ani wersji.

Panel pokazuje tekst rozpoznanych akapitów, nagłówków, punktów list i tabel,
puste części, częściowy charakter odczytu oraz tłumaczone ostrzeżenia czytnika.
Nie obiecuje pełnych tabel/AST/stron, XML offsets ani OCR. Identyczne akapity
pozostają osobnymi pozycjami i occurrence IDs. Cytaty/zakresy obserwacji i zakres
wystąpienia odnoszą się do pochodnego UTF-8. Podgląd i dopuszczenie do wyboru
wymagają istniejącego `clientBlobTextRead`; frontend dodatkowo sprawdza odpowiedź.

Wersję DOCX można utworzyć wskazując poprzednią **pochodną** Source DOCX;
`src_docx_<hash>` nie występuje jako poprzednik w tym wyborze. Oryginalny upload
jest ukrywany dopiero po odczycie odpowiadającej mu pochodnej wersji; różne
receipts czytnika tego samego oryginału nadal pozostają osobnymi wersjami.
Historia/projekcje po otwarciu/odświeżeniu pochodzą z istniejących odczytów serwera.
Lista jest ograniczona zadanym limitem; brak na niej nie dowodzi braku w grafie.

Occurrence IDs trafiają do istniejącego `context_execution.unified.legacy_seed_ids`.
Panel zachowuje inne ID, budżety, metody i ustawienia; zakres relacji jest jawnie
edytowalny. Wybór jest **globalny dla konta i wszystkich rozmów**, bez CAS.
Polski opis ujawnia możliwość nadpisania zmian z innej karty. Sam wybór nie jest
dowodem wysłania treści; silnik nadal stosuje konfigurację i budżety.

Błędy nie dodają fikcyjnego źródła ani potwierdzonego wyboru. Nie ma optymistycznego
czyszczenia zapisanych ID. Nieudany ponowny odczyt usuwa możliwość użycia starego
podglądu. Synchroniczna blokada chroni powtarzalny klik; tokeny/generacje odrzucają
spóźnione odczyty po zmianie źródła/projekcji/wystąpienia i zamknięciu panelu.
Spóźniony odczyt konfiguracji po zamknięciu nie rozpoczyna zapisu selektora.
Rozpoczętego zapisu serwera nie można anulować tym mechanizmem; błąd późniejszego
readback nie oznacza rollbacku. Odświeżenie odczytuje stan autorytatywny.

## Wykonane sprawdzenia

- `node --test loom/web/tests/docx-sources.test.mjs`: **15/15 PASS**, 0 skipped,
  Node `v22.22.1`. Nowe testy mapped-state, syntetyczne descriptor/transport;
  `base.json` backendu jest tylko odczytaną referencją tekstu/zakresów. Czytnika,
  natywnego kodu ani starego zestawu DOCX nie uruchamiano.
- `node --check`: moduł stanu, nowy test Node i oba nowe harnessy — **4/4 PASS**.
- `git diff --check -- loom/web`: PASS. Sprawdzenie jawnej delty i zakresu patcha
  jest zapisane w osobnym manifeście.
- Hashe otrzymanego overlayu: **17/17 niezmienione** względem pomiaru przed
  pierwszą edycją. To wynik lokalny, nie ponowny audyt/odbiór backendu.
- Stary test danych `text-sources.test.mjs` dostał wyłącznie aktualizację
  nieaktualnego oczekiwania „DOCX unsupported” oraz struktury słownika.
  Całego starego G1 ani P0/40 DOCX/7 HTTP/10 projekcji nie powtarzano.

## Otwarte bramki i ograniczenia

Korekta integratora: oba pliki istnieją pod absolutnym `/var/lib/codex-team/context/`
i zostały odczytane. Autor sprawdzał błędną ścieżkę względną. Historyczna relacja:

W kopii **brak** `context/docx-parts-admin-receipt.json` oraz
`context/philosophy-section-0.md` (nie istniał katalog `context`). Nie tworzono
zastępczego „kanonu” ani receipt administratora. Przeczytano dostępną instrukcję
`loom/src/text/DOCX_SOURCE_PARTS.md`, AGENTS/STATE i dokumenty architektury.
Manifest zawiera pełne 17 hashy before/after, ale ich zgodność z brakującym
receipt administratora pozostaje do sprawdzenia integratorowi.

Brak lokalnych `loom/web/node_modules`, `typescript`, `react`, `esbuild`,
`playwright` oraz polecenia `tsc`. **Typecheck TS/TSX: niewykonany**;
**React/browser: 0/4 grup**; **native/realbrowser/context: 0/6 grup**.
Sprawdzenie składni JS nie kompiluje TSX i nie dowodzi działania UI, odczytu
natywnego, pobierania, kontekstu ani odpowiedzi. Nie pobierano zależności;
nie uruchamiano Vite/build/CMake/Gradle/linkera/serwera/przeglądarki/API/modeli.
Nie używano kolejnych agentów, usług, VM, poświadczeń, produkcji ani holdoutów.

`docs/STATE.md`, ledger i stare logi nie są modyfikowane: zakres zadania dopuszcza
wyłącznie frontend/testy/ten raport oraz osobny manifest/CHECKPOINT. Stary pakiet
`d5e7531426acb8243d653956f916ed15fc0efee2bbedd59cfe148f231348f609` nie był
odczytywany ani zmieniany. Ten przyrost nie nadaje mu nowych właściwości.

## Przygotowane próby dla integratora — niewykonane

Najpierw uzupełnić brakujące dwa pliki context i porównać otrzymane hashe.
Integrator przyjmuje backend oddzielnie; patch UI **nie zawiera żadnego z 17
plików overlayu**. Następnie typecheck na własnych gotowych zależnościach.

`loom/web/e2e/docx-source-parts-component.mjs`: cztery nowe grupy prawdziwego
komponentu React ze sztucznym transportem: odmowa materializacji, dwa hashe/
ostrzeżenia/duplikaty, odmowa podglądu, podwójny klik i spóźniona odpowiedź po
reopen. Te wyniki, nawet po wykonaniu, nie są native/context acceptance.

`loom/web/e2e/docx-source-parts-native.mjs`: sześć nowych grup rzeczywistej App,
panelu importu i GraphView; tworzy wyłącznie własny tymczasowy root gościa oraz
lokalnego dostawcę bez klucza. Wymaga własnych świeżych binariów/webdist
z przyjętym backendem i tą deltą oraz lokalnego Pythona i gotowej przeglądarki.

```bash
node loom/web/e2e/docx-source-parts-component.mjs
LOOM_DOCX_UI_NATIVE=1 \
LOOM_SERVER_BIN="$PWD/<wlasny-build>/loom-server" \
LOOM_DOCX_UI_DIST="$PWD/<wlasny-webdist>" \
LOOM_DOCX_UI_EVIDENCE="$PWD/<nowy-katalog-dowodow>" \
LOOM_DOCX_PYTHON=/usr/bin/python3 \
node loom/web/e2e/docx-source-parts-native.mjs
```

To instrukcje przyszłego odbioru, nie wykonane polecenia. Native harness zapisuje
pełne żądania lokalnej atrapy, wire z czatu/materializacji, asercje oraz screenshoty;
nie wolno uznać samego screenshota za PASS. Weryfikuje upload bez materializacji
→ jawny DOCX → odrębne wystąpienia i dwa pliki/hashowane downloady → GraphView
→ globalny selektor z zachowaniem konfiguracji → rzeczywisty system context i
widoczną odpowiedź wyprowadzoną tylko z otrzymanego żądania → wersję innego ZIP
z tym samym tekstem i pochodnym poprzednikiem → reload/native restart → ponowne
walidacje oryginału/UTF-8 oraz żądanie z innej rozmowy. Stały mianownik 6, świeży
katalog dowodów i guard nie pozwalają zamienić niepełnego przebiegu w PASS.

Do odbioru rzeczywistych odmów po stronie runtime integrator może użyć tych
samych kroków UI na własnej kopii z niedostępnym readerem/brakującym oryginałem;
nowy harness komponentu sprawdza odmowy sztucznie, a dostarczone wcześniej
bramki backendu pozostają osobnym odbiorem. Nie powtarzać ich jako wyników autora UI.

## Artefakty przekazania

Delta UI SHA256 `6ad56402f480aa99bbadbea86f5127b646c1ff6302226942bf959b9f09f41047`,
task `chatadhd-docx-parts-ui-20261101`, baza `f527ee5cc34825f38485419e4c11ef7c3ead6ad0`.
Transport, manifest autora i prywatny CHECKPOINT pozostają poza produktem.
Własne wyniki integratora, dostosowanie harnessu do API i ograniczenia:
[DOCX_UI_INTEGRATION_2026-11-01.md](DOCX_UI_INTEGRATION_2026-11-01.md).
