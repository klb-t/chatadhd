# Wspólny file → graph/context/UI — 2026-11-01

Przyjęcie **zakresowe**, lokalne; nie wdrożono. Baza
`0252198f358f247eed074e1f75c36bcfbfca3871`, kod
`ed1392310b87aca76be7571dbc90e447e7c35a71`, tree `loom`
`cc28e2f388abcbfb499bec932e850cd11a231cd1`.
Poprzedni artefakt `f9767a0c20e628f942f8abda9b24daebab5fe9fb23666ae70324f132840754fb`
i starsze delty/cache pozostają. Nie ponowiono OCR/PDF/CSV/AST intake.
Kanon §0 rewizja 2; obowiązują batch-test-economy i batch-file-graph-acceptance.

## Zmiana i granice kontraktu

Wspólny instalacyjny descriptor/spec/provider/mapping korzysta z odebranego
`unknown_structure.contract`, istniejącego `chat::read_local_json` i natywnego
`source_parts`. Runtime tworzy SourceRecord/Unit/Observation/genesis/provenance
oraz identyfikatory. Upload nie wybiera helpera, interpretera, profilu ani
zaufanego receiptu. Przypięte payloady/interpreter, ograniczone stdin/stdout,
CPU/pamięć/wall i czyste środowisko pozostają egzekwowane. Nie uruchamiamy kodu,
formuł ani makr pliku. Kod nie pobiera specyfikacji ani narzędzi z sieci.

XLSX zachowuje workbook → sheet → range → cell; zakres jest pochodnym obszarem
zajętych wierszy, **nie deklaracją tabeli Excela**. Cached values formuł nie są
obserwacjami. ODT zachowuje 14 zagnieżdżonych bloków małej próbki i nakładające się
zakresy; EPUB kolejność spine, rozdziały i bloki. Raw ZIP, pochodny UTF-8 i JSON
struktury są osobnymi Sources/blobami. Współrzędne tekstu nie wskazują bajtów ZIP.
Odczyt/wybór ponawia przypięty parser i sprawdza zapisane powiązania.

ZIP przez to samo wejście SourcePartsPanel zachowuje archive → folder → file →
wewnętrzną strukturę obsługiwanego pliku. Nie rozpakowuje ścieżek do filesystemu.
Wspólny licznik ogranicza rekurencję/rozmiar/liczbę elementów; wykorzystano istniejący
bounded ZIP validator i transakcje/savepointy. Odmowa elementu wycofuje zapisy SQL;
**nie obiecujemy fizycznego rollbacku osieroconych blobów**. Nieobsługiwany element
pozostaje oryginałem ze statusem partial. To nie import rozmowy ani restore bundle.

## Wykonany odbiór aplikacji

Wszystkie pliki są syntetyczne. Ten sam mały PDF i arkusz mają identyczne bajty
wewnątrz ZIP z podfolderem. Fixture generator i manifest są zachowane; brak
produkcji, realnego modelu i API. Browser to własny Chromium/Playwright, prawdziwe
ImportPanel/SourcePartsPanel/GraphView/ChatView oraz natywny serwer z instalacyjnymi
helperami. Serwer działał z Landlock, whitelistą env i wyłącznie loopback.

| Przykład | Rzeczywisty graf/treść | Kontekst | UI i ograniczenia |
|---|---|---|---|
| PDF | 2 strony, 2 nagłówki/rozdziały z prefiksu, akapity, original/provenance | wybrany fragment i ID w systemowym request i odpowiedzi atrapy | upload/materialize/struktura/read/reopen; nagłówki i akapity **inferred**, bez layoutu |
| Arkusz 2 sheets | odrębne sheets/ranges/cells, parent edges, formuła unsupported | wybrana komórka; brak `INNY_ZAKRES`; odrębne ID równej treści | SourcePartsPanel + GraphView, globalny selektor konta |
| ZIP folder/files | rodzice folderu i pliku, member Source i tekst UTF-8 | fragment elementu w wspólnym request/odpowiedzi | nawigacja do części, oryginał/hash; partial dla nieobsługiwanych formatów |
| ZIP z tym samym PDF/XLSX | hierarchia i obie wewnętrzne struktury razem, nie flatten | osobne ID wystąpień oryginału i elementu ZIP | otwarcie elementu, wspólny reopen/idempotencja; parent edges także sprawdzone w DB po relokacji |
| Obraz PL + prostokąt/koło/strzałka | **nowy PASS lokalnego OCR w imporcie i zapisanej rozmowie** | nowego wyboru regionu z tego obrazu nie badano; wcześniejszy backend OCR→context pozostaje dowodem historycznym | rzeczywisty ImportPanel; **real vision NOT RUN** — brak zweryfikowanej skonfigurowanej ścieżki produktu i pokrycia kosztowego; atrapa nie jest vision |

ODT i EPUB także przeszły rzeczywisty import/rozpoznane części/read/reopen oraz
wybór do wspólnego request. W ODT wybrano rozpoznany blok tabeli zawierający Żółw,
nie deklarujemy w tej próbie wyłącznie jednej komórki. Wspólny request zawierał
**7 odrębnych wystąpień**, w tym dwa ID identycznego `Żółw WYBRANY`. Pytanie nie
zawierało treści źródeł; odpowiedź atrapy pochodziła z rzeczywistego systemowego
kontekstu. To dowód transportu/wyboru, nie jakości rozumowania modelu.

Selektor pozostaje globalny dla konta; zachowano inne seed IDs i pola konfiguracji.
Brak CAS tej samej tablicy, ograniczone okno listy Sources i brak pełnej polonizacji
całej aplikacji pozostają ograniczeniami. Oryginały sprawdzono przez rzeczywisty
HTTP raw i hash; nie przypisujemy nowego PASS każdemu przyciskowi download/mobile.

## Dowody i ekonomia testów

W `.integration-file-graph-batch-20261101`:

- `build-fix2.log`: wspólny Release core/server `-j2` PASS. Początkowe błędy
  kompilacji zachowane. `build-pdf-occurrence.log`: później wyłącznie dotknięty
  adapter PDF i linkowanie po rzeczywistej awarii.
- `web-build.log`: własny `npm --prefix loom/web run build -- --outDir ...`:
  typecheck PASS, Vite PASS (23.54 s), ostrzeżenie rozmiaru chunków zachowane.
- `native.log`: **5 wspólnych kontroli ryzyka PASS**: idempotencja i forged receipt
  przed mutacją; odmowy uszkodzonego oryginału/receiptu/extraction/derived;
  odmowa zmienionego lokatora; rollback SQL z błędnym elementem ZIP; wspólny reopen
  XLSX + sentinele UTF-8/CSV/DOCX. Ten etap poprzedzał późniejszą poprawkę wyłącznie
  PDF i dodanie zależności pakietowej ODT; nie udajemy jego ponownego wykonania na
  końcowym binarium. Pozostały kod tych kontroli nie zmienił się.
- `browser-odt-package.log`, `browser-odt-package/observations.json`: **3/3 grupy**
  nowej wspólnej próby PASS. `browser-combined-context.*`: dodatkowy brakujący
  wspólny request różnych formatów **1/1 PASS**, bez ponawiania całej bramki.
- `abi.json`: 126 eksportów C niezmienionych. Generator `gen_text_pack.py --check`
  i `git diff --cached --check` PASS. Brak monolitu/CLI/fullrepo/CTest suites.
- `verify-package.log`: istniejący verify-package, **BusyBox tar**, start/health/
  static/auth PASS. GNU tar nie uruchamiano ponownie, historycznego ENOSYS nie
  przemianowano na sukces.
- `package-helper.log`: końcowy relokowany artefakt, default ZIP→PDF/XLSX i odczyt
  komórki PASS, przy niedostępnych helperach checkoutu/początkowego stagingu.
  48 payloadów zweryfikowanych hashami. `package-graph-result.json`: **26 trwałych
  relacji rodziców**, archive/folder/file/internal structure, integrity OK i 0 FK.

Pierwsze próby browser zachowano: brak nagłówków przy pojedynczych newline PDF;
kolizja page occurrence tego samego raw PDF pod innym source key; brak
`odt_basic.adapter` wymaganej przez pakietowe `__init__`. Naprawiono regułę
prefix-line, source identity wystąpienia i listę payloadów. Nie osłabiono asercji.
Po browserze usunięto wyłącznie nadmiarowy pusty EOF w XLSX `container.py`
(rzeczywista linia patcha 736); końcowy relokowany probe używa już tej normalizacji.
Wcześniejszy zapis o normalizacji `import_hook.py` był no-op, skorygowany w
`applied.json`. Dokładne manifesty etapów nie są utożsamiane.

Nie ponawiano historycznych 22/24/32/31 testów autorów ani P0/share/accounts/
backup/GUI matrix. Przygotowanie fixture, integracja/debugging i testy są odrębnymi
fazami; wskazówki 180/600 s nie stały się timeoutem zadania ani runtime SLA.

## Pochodzenie

Read-only HEAD/status/hash przed i po odczycie: cztery delty stabilne, nałożone raz.
Nested unknown CHECKPOINT był czytelny; wcześniejszy root EACCES nie jest blokerem.

| Task | Base | SHA256 product patch |
|---|---|---|
| fleet-unknown-structure-20261101 | 4a82c0cbf543c14412b01e7491938384551b4c7a | aab3d841da1edf551e7513bfbf13bf1e5f2a1337f88e7e5e95255d5cb6f95680 |
| fleet-xlsx-reader-20261101 | e2f6873c7ca24b41957b36b0127439b926068f41 | e43243b6673a4000f449e2495df919dd6c084d82d2b03dfbbdc8706c8ea9b9e6 |
| fleet-odt-reader-20261101 | e2f6873c7ca24b41957b36b0127439b926068f41 | 53407512629be083873df8c368aa3a4635c8603b0609ae737f368baa5e160867 |
| fleet-epub-reader-20261101 | e2f6873c7ca24b41957b36b0127439b926068f41 | 351e26839174b61b6bb4502becac56bb046036c091179f41527b1e17114184d8 |

EPUB tylko reader/preset/fixtures/tests/README; pięć przestarzałych native hook
proposals wykluczono. Cudze shared overlays/STATE/CHECKPOINT/logi nie trafiły do
produktu. `intake.json`, `applied.json` zawierają locatory/archive hashes i wyłączenia.

## Artefakt i next

Nowy lokalny Release:
`.integration-file-graph-batch-20261101/chatadhd-server-linux-x64-ed139231-release-file-graph-20261101.tar.gz`

- archive SHA256: `ee2117e8644bed34bc2b1772df017e001b6c713842df56edbc8fee8cddf947e8`
- manifest SHA256: `d3812660ac6830fb190e064bc2bc4cc30fbf0d14e26c1f2252bbe62f565868a0`
- server SHA256: `1a14008d7479fb7c2d051c4dd42f832e07d0ab78c72a72d535c43f7bf7c7a970`
- lib SHA256: `f435740a8c84e546c5b4196e256773a346a0720d44cacdebd2573ae8ad9f85bb`

Nie jest to całościowy PASS fali ani zgoda na wydanie. Pinned helper/profile/
interpreter changes odmawiają odczytu starego receiptu; zmieniona projekcja PDF
wymaga jawnej nowej materializacji. **Migracja/reprojekcja starych wyborów i rollback
nowych formatów nie zostały odebrane**. ZIP revision/previous_source_id nie jest
w tej wersji obsługiwany przez materializację (jawna odmowa, bez nadpisania).

Następne wewnętrzne kroki istniejącej kolejki: legacy XLS wymaga przypiętego
catdoc/charset/config/environment w bounded host (obecny descriptor nie jest
native XLS PASS); video wymaga rzeczywistego blob transaction/frame binding z
`has_ocr=false`; bundle wymaga message-pointer→targetID i readback/partial restore,
a nie zwykłej hierarchii ZIP; extent wymaga faktycznego natywnego konsumenta
zaufanych source/frame. Zachowane ich delty nie zostały nałożone w tej partii.
Nowy region-selector GUI obrazu i real vision pozostają jawnie nieodebrane.
To praca integracyjna/produktowa, nie brak dodatkowej zgody na istniejący kod.
Bez kolejnego cyklu, nowych agentów, publikacji i deploy.
