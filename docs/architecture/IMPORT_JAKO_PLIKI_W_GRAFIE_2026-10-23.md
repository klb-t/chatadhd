# Import jako pliki w grafie: rodowód klas, pamięć podręczna, kontynuacja i modyfikacja rozmów z eksportu (2026-10-23)

Projekt D1, część A (etap Fable). Podrzędny wobec R1–R59 (`OWNER_REQUIREMENTS_2026-09-26.md`,
`SLOWA_WLASCICIELA_2026-10-16.md`), `GRAF_HUB_KOD_SILNIK_2026-10-13.md` (graf jako hub),
`WIDOK_SILNIK_WIZUALIZACJI_2026-10-15.md` i `EKSTRAKT_ARCHIWOW_2026-10-16.md` (X-Port, P38).
Nie projektuje tego, co już jest (X-Port inwentaryzuje, wycina z zachowaniem formatu, przebudowuje
i edytuje seryjnie eksporty ChatGPT, Claude, Claude Code, Gemini, Grok, tekst); mówi, co to pokrywa
i gdzie ten projekt się różni (§1.3). Zadanie R57 w kolejce.

Legenda wiarygodności źródeł (R59 p. 4, używana w całym tekście): **[A]** oficjalna dokumentacja
dostawcy, **[B]** utrzymywane parsery/importery i źródła fachowe, **[C]** wyprowadzone z danych
(kod i syntetyczne fixture'y w repo), **[D]** najniższy poziom (pojedyncze notatki, nieznani autorzy).
„Niezweryfikowane” znaczy: nikt tego nie sprawdził na prawdziwym eksporcie; gdzie da się to zrobić
lokalnie, podaję jak. Żadne twierdzenie bez oznaczenia poziomu nie jest faktem o cudzym formacie:
jest decyzją projektową albo faktem o naszym kodzie (ścieżka i linie).

Zgodnie z R59 p. 1–2: każda lista, wartość, nazwa i kolejność w tym dokumencie jest **przykładem**
tego, co silnik ma obsłużyć (jedna instancja, co najwyżej preset), a dla każdego mechanizmu
podaję jego **pełną przestrzeń możliwości**, sposób jej wyliczania (testy generatywne, nie ręczne
przypadki) i to, co **pozostaje niepokryte**.

---

## 0. Streszczenie dla właściciela (zwykłym językiem)

1. **Co już mamy.** Importer w `loom/src/import` kopiuje wszystko do tabeli `messages`, załączniki
   ChatGPT do magazynu blobów, a ZIP rozpakowuje do katalogu tymczasowego, który po imporcie znika
   (`importer_zip.cpp:1,36`). Nie tworzy **żadnego** węzła grafu: rozmowa z importu nie jest węzłem,
   plik eksportu nie jest węzłem, `chat.html` jest pomijany z adnotacją „duplikat renderingu”
   (`export_openai.cpp:989-993`). To jest wprost sprzeczne z Pana zapisem z 2026-09-27/28
   („może być poprzez linkowanie do plików zipa […] ale może być kopiowane, jakkolwiek sobie
   użytkownik nie wymyśli”) i z zasadą „graf jako hub”.
2. **Co projektuję.** Każdy import staje się węzłem klasy `file` z nakładkami (fasetami):
   archiwum ZIP → eksport → eksport danego dostawcy → konkretny układ (wersja) tego eksportu. Węższa
   faseta dziedziczy metody i łącza szerszej. Dodanie nowego dostawcy albo nowej wersji = nowy plik
   danych, bez kodu (chyba że struktura jest zupełnie nowego rodzaju — wtedy jeden czytnik).
3. **Trzy tryby importu** (i ich złożenia): kopia (dziś), **link w miejscu** po jednorazowym
   rozpakowaniu (preset tego projektu, **decyzja projektu** na podstawie odczytu R57: „nie kopiujemy,
   rozpakowujemy” — to preset, nie reguła), odczyt wprost z ZIP-a bez rozpakowania. Preset globalny i per
   dostawca; zmiana rozmową („importuj bez kopiowania”) i z menu. Nic nie znika: tryb kopii zostaje;
   odwrót: zmiana presetu na inny tryb, bez zmiany kodu.
4. **Pamięć podręczna** to węzeł główny, pod którym widać, które elementy eksportu są teraz
   zmaterializowane (otwarte, edytowane). Jej trwałą postacią jest **dziennik łat** (JSON Patch) nad
   nietkniętym źródłem — dzięki temu zwolnienie pamięci podręcznej nie traci edycji.
5. **Kontynuacja i modyfikacja rozmów z ZIP-a**: wszystkie warianty zostają jako konfiguracja
   (§5, §7): kontynuacja w Loom z łączem `derived_from`, edycja jako nakładka, zapis zwrotny do
   rozpakowanego drzewa z zachowaniem formatu (także bliźniaka HTML), przepakowanie do ZIP na
   życzenie, a zapis w formacie dostawcy jako pełnoprawna konfiguracja. Mechanizm jest łatwy;
   trudne są **niewiadome formatu** (reguła podziału na pliki, tożsamość `chat.html` z JSON-em,
   znaczenie kluczy `assetsJson`) — i to są rzeczy, które dziś rozstrzyga dopiero pomiar na prawdziwym
   eksporcie (Pana albo każdego innego); w [A]–[D] nie znalazłem publicznego pomiaru (obserwacja
   z 2026-10-23, niezweryfikowane — ślad w §1.5 i §13).
6. **Czy ZIP jest za drogi?** Częściowo ma Pan rację. Odczyt jednego wpisu jest tani (ZIP ma spis
   i każdy wpis jest skompresowany osobno). Drogie jest przepisywanie całego archiwum z
   rekompresją — ale biblioteka, którą już mamy (miniz 11.0.2), umie **kopiować nietknięte wpisy bez
   rekompresji** i zapisywać bez kompresji. Prawdziwym kosztem jest `chat.html` (druga kopia
   wszystkich rozmów w jednym pliku, w jednym zgłoszonym eksporcie ~600 MB [B, wnioskowanie]), nie
   sam ZIP. Liczby trzeba zmierzyć, nie zakładać (§6). Koszt jest **ostrzeżeniem bez blokady**
   (reguła ×10 tam, gdzie dotyczy), nie powodem usunięcia opcji: praca wprost na ZIP-ie zostaje
   dostępna (patrz W9 w §1.1).
7. **Ważna poprawka do odczytu wypowiedzi** (§13): `chat.html` **nie jest podzielony** jak JSON — to
   jedna tablica w jednym pliku [B, 7 niezależnych parserów]; reguły podziału JSON: brak w [A]/[B]
   (obserwacja z 2026-10-23, niezweryfikowane); „nie kopiujemy, rozpakowujemy” to jednak kopia
   (rozpakowana); a dla ChatGPT istnieje prawdopodobnie **konsument własnego eksportu** (oficjalny
   artykuł o przenoszeniu rozmów między kontami, widziany tylko w streszczeniu wyszukiwarki
   [A, niepobrany]) — jeśli to się potwierdzi, zapis w formacie dostawcy może być bardziej użyteczny,
   niż zakładano (wniosek warunkowy, poziom [A] niepobrany). Opcja zostaje bez względu na to, czy
   konsument istnieje dziś (R60).
8. **Pytanie o klony** (czy reprodukują przepływy ChatGPT/Codex itd.) należy do części B tego
   zadania; tu tylko zaznaczam błąd w pytaniu (§13 p. 5).

---

## 1. Wymaganie jako twierdzenia, co już zapisane, co sprzeczne z importerem

### 1.1 Odczyt koordynatora (parafrazy; cytat dosłowny w R57), W1–W11, z wypowiedzi 2026-10-23

Poniższe punkty to **odczyt koordynatora** wypowiedzi właściciela, nie jego słowa; dosłowny tekst jest
w `OWNER_REQUIREMENTS_2026-09-26.md`, sekcja R57. Tam, gdzie punkt zawiera cudzysłów, jest on dosłowny.

- **W1** Ustalenie trybu importu ma być możliwe **rozmową i w menu**.
- **W2** Import **nie kopiuje**: ZIP jest rozpakowany, w grafie **linkowany** i **opisany
  metadanymi**.
- **W3** Wyświetlanie w interfejsie wzorowanym na aplikacji dostawcy danego eksportu.
- **W4** Zaimportowana rzecz to **plik** z takimi samymi metodami w grafie i standardowymi łączami
  jak każdy inny plik; do tego to, co charakterystyczne dla **archiwum ZIP**; węziej dla
  **eksportu**; węziej dla **eksportu danego dostawcy w danej wersji** (rodowód klas).
- **W5** Do przemyślenia: **kontynuacja lub modyfikacja** rozmów, które są w ZIP-ie; czy wszystkie
  możliwości są łatwe.
- **W6** Przykład: **zachowanie oryginalnego formatu archiwum** przy edycji samych danych.
- **W7** Przykład: OpenAI dzieli rozmowy na kilka plików JSON i (chyba) powtarza je w jednym
  `.html`; zachować **zasady podziału** i **drugą kopię w HTML**, jeśli to to samo.
- **W8** Potwierdzenie **w sieci**: najlepiej dokumentacja, źródła fachowe; dopiero z braku
  wiarygodnych — określenie z danych i źródeł mniej wiarygodnych, „wszystko ważone z głową”.
- **W9** Praca **wyłącznie na ZIP-ie**. Dosłownie (R57): „praca wyłącznie na zippie to za dużo
  obliczeń, kompresji, rekompresji będzie ciągnąć, to tylko na wyraźne życzenie użytkownika”.
  Mechanizm (odczyt koordynatora, **decyzja projektu**): to **preset dostępności** — domyślnie
  wyłączony, o jedno ustawienie od włączenia (rozmową albo z menu); koszt jest **nieblokującym
  ostrzeżeniem** (z regułą ×10 tam, gdzie dotyczy); opcja nigdy nie jest usuwana ani zdegradowana
  z powodu kosztu. Odwrót: zmiana presetu.
- **W10** Standardowo: **węzeł główny typu pamięć podręczna**, gdzie są aktualnie przetwarzane
  i edytowane elementy eksportu.
- **W11** Klony aplikacji na GitHubie: czy reprodukują oryginalne przepływy (Claude, Codex…);
  jeśli nie, na ile my możemy je zreplikować. (Część B.)

### 1.2 Co już jest zapisane (cytaty)

- **UI14** (`SLOWA_WLASCICIELA_2026-10-16.md:1486-1491`): profile interfejsu inspirowane dostawcami
  **jako dane**, także wersje archiwalne; odwzorowanie, nie klonowanie. Realizacja częściowa:
  `docs/APPLICATION_PROFILES.md`, `loom/web/src/profiles/data/*.json`
  (`loom.application_profile/1`, m.in. `chatgpt-inspired-r2.json`, `claude-inspired-r2.json`,
  `gemini-inspired-r2.json`, `librechat-0.8.8.json`, `nextchat-2.16.1.json`).
- **DIP13** (tamże `:1715-1716`): „czy w grafie będziemy powielać importy, czy tylko linkować do
  danych w plikach? […] nie podejmujemy decyzji zostawiamy użytkownikowi […] łącznie z odwzorowaniem
  w grafie wszystkich danych i meta danych i pilnowaniem cyklicznym czy pliki nowe nie doszły”.
- **DIP14** (tamże `:1718-1724`): eksporty Anthropic i OpenAI zinterpretowane w całości, załączniki
  powiązane, struktura sprawdzana w sieci, a gdy jej nie znajdzie — zgadywana po nazwach
  i powiązaniach; „może być poprzez linkowanie do plików zipa które interfejs obsłuży, żeby nie
  kopiować, ale może być kopiowane”.
- **RE31** (tamże `:2174`): replikacja funkcjonalności aplikacji dostawców, testy porównawcze jako
  kryterium.
- **EKSTRAKT (P38)**: X-Port ma opis formatu jako dane (`formats/*.json` z `sniff`, `shards`,
  `wrappers`, `attachments.layouts`, `members`, `preserve`, `known_absent`), profil `preserve`
  (format 1:1 z mniejszą liczbą rozmów, kopiowanie wpisów ZIP z metadanymi źródła, `cut/preserve.py`),
  edycje seryjne z `oplog.jsonl` i odwrotnością (§3.15), różnicę między eksportami (§3.16),
  pakiet przekazania do importera aplikacji (§3.17).

### 1.3 Co pokrywa X-Port, a gdzie ten projekt się różni

| Rzecz | X-Port (jest) | Ten projekt (dokłada) |
|---|---|---|
| Rozpoznanie formatu | `sniff` z treści, jeden plik na format; **brak poziomu wersji/układu** (pola `version`/`era`/`layout` nie istnieją w `formats/*.json` — sprawdzone) | odcisk **dostawca + układ (wersja)** jako dane, z proweniencją i wiarygodnością (§2.3, §12) |
| Wycięcie 1:1 | `preserve`: kopia wybranych rozmów z własnych bajtów źródła, ten sam kontener | **edycja w miejscu** rozpakowanego drzewa i przepakowanie z kopią surową nietkniętych wpisów (§5c–d) |
| Edycja | seryjna, na drzewie **pakietu** (nigdy na migawce źródła), `oplog.jsonl` | ta sama semantyka łat (RFC 6902) jako **trwała postać pamięci podręcznej** nad źródłem (§4) |
| Graf | pakiet `graph/` na życzenie | **każdy import** jest węzłami grafu, także w trybie kopii (§3.4) |
| Interfejs | CLI, komander, API lokalne | widok w profilu dostawcy w aplikacji (§8) |
| Odbiorca | pakiet `handoff/` dla importera aplikacji | importer **nie uczy się nowego formatu** — zasada z §3.17 zostaje; dokłada tryb `link` |

### 1.4 Co w dzisiejszym importerze jest sprzeczne z W1–W10 (fakty o naszym kodzie, [C])

1. **Kopiuje wszystko** do `messages` (`importer_core.cpp`, lejek `import_message_list()`), a bajty
   źródła do magazynu blobów (`importer_core.cpp:240`, `blobs_->put_file`). Tryb linku nie istnieje.
2. **ZIP → katalog tymczasowy, usuwany po imporcie** (`importer_zip.cpp:1`, `fsutil::TempDir td` :36).
   Link w miejscu wymaga trwałego drzewa rozpakowania.
3. **Nie pisze węzłów grafu**: brak klasy dla pliku eksportu, wpisu archiwum i rozmowy jako węzła
   (audyt 2026-10-22; w `classes.pack` są klasy `file` :954 i `archive_item` :944, ale import ich
   nie tworzy).
4. **`chat.html` pomijany bezwarunkowo** z adnotacją „duplicate rendering of conversations.json”
   (`export_openai.cpp:989-993`, test `test_import_exports.cpp:477`). Tożsamość z JSON jest
   **niezweryfikowana** (§1.5). Gdyby HTML zawierał rozmowy nieobecne w JSON, nie zostałyby
   zaimportowane bez ostrzeżenia.
5. **Claude.ai**: załączniki tylko jako metadane (`export_anthropic.cpp:305-329`, `resolved=false`),
   bez ostrzeżenia dla osoby; to **dzisiejsze ograniczenie eksportu** (obserwacja z 2026-10-23: brak
   bajtów w znanych próbkach, [B] + [A] dla Team/Enterprise; inna wersja eksportu może je nieść) —
   nie importera, ale powinno być powiedziane.
6. **Wynik importu**: angielska linia + surowy JSON (audyt). W2 mówi o opisie metadanymi w grafie;
   **język opisu po polsku** to **decyzja projektu / preset** (etykiety dla właściciela po polsku,
   język jest daną i da się go zmienić), nie treść W2/W3. Odwrót: zmiana presetu języka opisu.
7. **Predykat shardów** `is_conversation_file` (`export_internal.h:326-328`) akceptuje
   `conversations-.json` (zero cyfr; `> 18` zamiast `> 19`) i ścieżki zagnieżdżone po nazwie
   bazowej; convoviz dopasowuje tylko katalog główny [B]. Drobne, nie grozi utratą danych.
8. **Fixture `chat.html`** nie ma kształtu prawdziwego pliku: `var jsonData = {obiekt}` zamiast
   tablicy, bez `assetsJson`, bez JS podglądu, 1 z 7 rozmów (`gen_export_fixtures.py:~467`);
   fixture `openai_legacy` ma 39-bajtowy zaślepkę. Testy nigdy nie ćwiczą prawdziwego kształtu.

### 1.5 Stan wiedzy o formatach, w skrócie (pełne badanie: materiał D1-research + werdykty sceptyka)

| Twierdzenie | Poziom | Pewność | Jak sprawdzić lokalnie (nic nie wychodzi z maszyny) |
|---|---|---|---|
| OpenAI nie publikuje specyfikacji plików eksportu | [A] milczy; [B] convoviz: „no official contract” | średnia | — |
| `chat.html` = HTML z osadzonymi danymi: `var jsonData = [...]`, potem `var assetsJson = {...}`, potem JS podglądu; tytuł „ChatGPT Data Export” | [B] 7 niezależnych parserów | wysoka | `python3 -I`: `i = html.index('var jsonData = ')+15; obj, _ = json.JSONDecoder().raw_decode(html[i:])`; typ i długość |
| `jsonData` ≡ `conversations*.json` (semantycznie) | [D] jedna notatka 2025-06; [B] SteveLeve odkodowuje encje HTML w `jsonData` (wskazówka na brak tożsamości bajtowej) | niska | zbiory id z HTML i z shardów; `json.dumps(sort_keys=True)` per id; raportować tylko liczby |
| Podział na `conversations-NNN.json` (000, 001…), `conversations.json` wtedy nieobecny | [A] (streszczenie, niepobrane) + [B] convoviz, portable-ai-memory | wysoka | `unzip -l` |
| **Reguła podziału** (liczba rozmów / rozmiar / porządek) | brak w [A]–[D] | — | per shard: liczba rozmów, bajty, min/max `create_time`, rozłączność id |
| Sharded eksport nadal zawiera `chat.html`; jeden eksport: shardy ~650 MB i `chat.html` ~599 MB | [B] README CRT-GroundCheck-SSE (wnioskowanie z rozmiarów, oznaczone „when populated”); datamoat czyta `assetsJson` z 2026 | średnia | `unzip -l` |
| `export_manifest.json`, `conversation_asset_file_names.json`, `library_files.json`, `user_settings.json`, `file_<id>.dat` w eksportach 2026 | [B] Nexus, ChatGPT-Wrapped, datamoat; **żadne źródło OpenAI**; dwa narzędzia same **generują** plik o tej nazwie (możliwa pętla naśladowcza) | średnia/niska | `unzip -l`; porównać `export_files` z listą wpisów |
| Wariant „Privacy portal”: ZIP w ZIP, media `.dat` bez rozszerzenia | [D]/[B−] jedno źródło (temnoon) | niska | `unzip -l` zewnętrznego; `file` na `.dat` |
| Drzewo wiadomości OpenAI: `mapping`, korzeń `parent=null`, gałęzie = wiele `children`, `current_node` | [B] convoviz + oa-chat; [C] `export_openai.cpp:451-540` | wysoka | symetria parent/children, jeden korzeń |
| Claude.ai: `conversations.json`, `users.json`, `projects.json`, opc. `memories.json`, **`design_chats/<uuid>.json`** (nowe; **niezweryfikowane** — Aneks B p. 5), bez HTML, bez bajtów załączników (obserwacja z datą, nie ograniczenie) | [B] osteele, lordjabez; **[B] jedno pierwszorzędne sprawdzenie prawdziwego eksportu 2026-04-22 (waggle-os)**; [A] „doesn't include the contents of files you uploaded” (tylko Team/Enterprise) | średnia | `unzip -l`; liczba `attachments` z/bez `extracted_content` |
| Gałęzie w ZIP-ie Claude (`parent_message_uuid`, `current_leaf_message_uuid`) | [D] „eksport liniowy”; schematy: pola opcjonalne | niska | liczyć wiadomości, których rodzic ≠ poprzednik |
| Gemini: nie produkt „Gemini” w Takeout, lecz My Activity → Gemini Apps (`MyActivity.html`/`.json`), płaski dziennik bez id rozmowy | [B] 4 parsery; [A] generyczny schemat My Activity (streszczenie) | średnia | klucze wpisów, liczba z/bez `safeHtmlItem` |
| **Konsument eksportu ChatGPT**: oficjalny artykuł „Transfer exported conversations between ChatGPT accounts” (9106926): po rozpakowaniu szukać `conversations.json`, większe eksporty mają numerowane pliki, za duże wgrywać osobno | [A] **tylko streszczenie wyszukiwarki, strona niepobierana (DNS)** | średnia | przeczytać artykuł w przeglądarce |
| Claude.ai / Gemini: brak znanego importu własnego eksportu | tylko brak dowodu (negatyw niezweryfikowany) | — | — |

Wiersze tej tabeli to **obserwacje z datą 2026-10-23** (okno ważności: do następnego pomiaru albo
nowej wersji eksportu; poziomy w kolumnie 2), nie ograniczenia: żadna z nich nie zawęża trybów
ani opcji tego projektu, a zmiana stanu rzeczy (np. nowa wersja eksportu z bajtami załączników,
z nowym katalogiem, z innym podziałem) ma się obejść **bez zmiany kodu** — nowy wiersz danych
w profilu dostawcy.

---

## 2. Rodowód klas: plik → archiwum → eksport → eksport dostawcy → układ (wersja) — jako DANE

### 2.1 Dziedziczenie czy kompozycja — decyzja i powód

Łańcuch właściciela (W4) jest **jednym przypadkiem**, w którym dwie niezależne osie pokrywają się:

- oś **kontenera**: `file` → `archive` → `zip_archive` (albo `directory`, `json_file`, `html_file`,
  `sqlite_file`, `zip_in_zip`…),
- oś **treści**: `export` → `export.<vendor>` → `export.<vendor>.<product>` →
  `export.<vendor>.<product>/<layout>`.

Przypadki, w których się **nie** pokrywają, istnieją wg obserwacji z 2026-10-23 (poziomy w §1.5; wiersz [D]
nie jest dowodem, a decyzja niżej nie zależy od ich liczby — kompozycja osi jest ogólna): goły `conversations.json` bez ZIP-a
(eksport, nie archiwum); eksport rozpakowany do katalogu; wariant „Privacy portal” ZIP w ZIP [D];
`MyActivity.html` Gemini (HTML, nie JSON, w ZIP-ie Takeout obok innych produktów); `design_chats/`
jako katalog w ZIP-ie Claude [B, niezweryfikowane — Aneks B p. 5]. Pojedyncze dziedziczenie `file > archive > export` złamałoby się
na każdym z nich.

**Decyzja D2-1:** dziedziczenie **wewnątrz każdej osi** (drzewo klas z `parent`, cechy `traits`
dziedziczone po `parent` — dokładnie tak, jak już działa `data/graph/classes.pack`: „classes declare
`traits` (inherited along `parent`; a class id is also a trait of its own)”), a **kompozycja między
osiami**: węzeł ma klasę kontenera (`class`) i listę **faset** (`facets[]`), z których każda jest
klasą z innej osi. Akcje pasują po cechach (`requires` w `data/runtime/actions.pack`), więc akcja
„przepakuj ZIP” wymaga cechy `zip_archive`, „zapisz shard” — `export.openai.chatgpt`, a „otwórz
w widoku dostawcy” — `export` + istnienie profilu dla `vendor/product`. Węższa faseta dziedziczy
metody szerszej **bez** wpisywania ich ponownie, a jedna rzecz może być naraz ZIP-em, eksportem
i plikiem — co jest zgodne z fragmentem R57 (dosłownie: „jak każdy inny plik kropka do tego
charakterystyczne dla archiwum ZIP, jeszcze węższe: dla eksportu, jeszcze węższe: eksport danego
dostawcy dana wersja”; odczyt koordynatora: to rodowód klas), tylko bez wymuszania, by eksport
zawsze leżał pod archiwum.

Odwrót: gdyby kompozycja okazała się zbędna (wszystkie realne eksporty pod ZIP-em), fasety
spłaszcza się do jednego drzewa jedną migracją danych (`facets[0]` → `parent`), kod akcji nie zmienia
się, bo pasuje po cechach.

### 2.2 Klasy (przykłady, lista otwarta) — nowe wpisy w `data/graph/classes.pack`

Oś kontenera (rozszerza istniejące `file` :954 i `archive_item` :944):

| Klasa | `parent` | Cechy dodane | Metody/łącza (akcje po cechach) |
|---|---|---|---|
| `file` (jest) | — | `has_file`, `has_hash` | otwórz, pokaż metadane, policz hash, `stored_as` blob, `derived_from`, `part_of` katalogu |
| `archive` | `file` | `has_members`, `container` | lista wpisów, rozpakuj (gdzie: preset), odczyt wpisu, różnica z innym archiwum |
| `zip_archive` | `archive` | `zip` | surowa kopia wpisu bez rekompresji, dopisanie wpisu, zip64, poziom kompresji, CRC |
| `zip_in_zip` | `zip_archive` | `nested` | rozwiń zagnieżdżenie, zachowaj zagnieżdżenie przy przepakowaniu |
| `directory_tree` | `archive` | `on_disk` | to samo, co archiwum, bez kompresji (rozpakowane drzewo **jest** tym węzłem) |
| `archive_member` | `archive_item` (jest) | `has_locator` | odczytaj bajty, hash, mtime z ZIP, metoda kompresji, „kto mnie używa” |
| `json_file`, `html_file`, `sqlite_file`, `jsonl_file`, `binary_file` | `file` | odpowiednio | parsuj, waliduj, podgląd |

Oś treści (nowe drzewo):

| Klasa | `parent` | Cechy | Metody/łącza |
|---|---|---|---|
| `export` | — | `export`, `has_provenance` | odcisk (fingerprint), raport „co jest / czego z definicji brak” (`known_absent`), widok w profilu dostawcy, zapis zwrotny w formacie (§5), różnica między eksportami (X-Port §3.16) |
| `export.openai` | `export` | `vendor:openai` | konto (`user.json`), łącza udostępnień, feedback |
| `export.openai.chatgpt` | `export.openai` | `tree:mapping` | shardy, bliźniak HTML, `assetsJson`, `textdocs`, obrazy `user-*/`, `dalle-generations/` |
| `export.openai.chatgpt/single-2023` | `export.openai.chatgpt` | `layout` | 5 plików głównych [B] |
| `export.openai.chatgpt/single-2025` | j.w. | `layout` | + `shared_conversations.json`, `textdocs/`, `user-{id}/`, `sediment://` [B, średnia] |
| `export.openai.chatgpt/sharded-2026` | j.w. | `layout` | `conversations-NNN.json`, `chat.html`, manifest? `.dat`? [B/D] |
| `export.openai.chatgpt/privacy-portal-2026` | j.w. | `layout` | ZIP w ZIP, `.dat` [D] |
| `export.anthropic.claude/2024-text`, `/2026-full` | `export.anthropic.claude` | `layout` | `memories.json`, `design_chats/` [B, niezweryfikowane — Aneks B p. 5] |
| `export.google.gemini/takeout-myactivity` | `export.google.gemini` | `layout`, `activity_log` | grupowanie wpisów w rozmowy (hipoteza, [B] parsery grupują po dniu/URL) |
| `export.xai.grok`, `export.anthropic.claude_code`, `export.loom` | `export` | … | z `formats/*.json` X-Port |

Nazwy układów (`single-2023` itd.) są **etykietami przykładowymi**; właściwy identyfikator układu
to hash jego odcisku (§2.3), etykieta jest polem `label` do zmiany przez osobę.

Treść i forma (§6 `GRAF_HUB`, klasy `content`/`form` w `classes.pack`): rozmowa z eksportu jest
**treścią** (`conversation`), a jej wystąpienia to **formy**: obiekt w shardzie JSON i obiekt
w `jsonData` w `chat.html`. Łącze `expresses` (`classes.pack:569`) z wagą = wierność. Pytanie
„czy HTML to to samo” jest wtedy pytaniem o **wagę** tego łącza, mierzoną lokalnie (§1.5), a nie
założeniem w kodzie. To jedna z różnic wobec dzisiejszego `Skip{"offline_viewer_skipped"}`.

### 2.3 Odcisk dostawcy i układu (sniffing/fingerprinting) jako dane

Istnieje: `sniff.any_of` w `loom/tools/extract/data/formats/*.json` (`zip_member`,
`zip_member_glob`, `zip_member_json`, `json_object_keys_all`, `json_array_first_keys_all`,
`json_wrapper_first…`) i `match`/`shape`/`shard_prefix` w `data/presets/import.pack` (4 platformy,
np. `{"id":"chatgpt","walker":"mapping","shard_prefix":"conversations-","match":[["mapping"]]}`).
Brakuje poziomu układu. Projekt: **słownik deskryptorów** (zamknięty w kodzie, otwarty w danych)
i **układ = koniunkcja deskryptorów** z proweniencją każdego z nich.

Słownik deskryptorów (pełna przestrzeń tego, co odcisk może badać; każdy deskryptor ma `source`
i `claim_id` do rejestru §12):

```
member_present(regex)          member_absent(regex)          member_count(regex, min, max)
root_folder(regex)             nesting(zip_in_zip | flat)    json_top(member, array|object|scalar)
json_keys(member, path, all|any|none, [keys])                json_value_re(member, path, regex)
js_marker(member, literal)     html_title(member, regex)      size_class(member, bucket_id)
ext_set([exts])                encoding(member, utf8|utf16|…) mtime_span(min_days, max_days)
```

Progi `size_class` i `mtime_span` są wartościami w danych (presety), nie w kodzie; żadnych
stałych liczbowych w słowniku.

Profil układu (plik `data/formats/<vendor>/<product>/layouts/<id>.json`, schemat
`loom.export_layout/1`) — przykład dla `sharded-2026`:

```json
{"schema": "loom.export_layout/1", "id": "sha256:…", "label": {"pl": "ChatGPT, podział na pliki (2026)", "en": "ChatGPT sharded (2026)"},
 "class": "export.openai.chatgpt", "observed": {"from": "2026-02", "until": null, "basis": ["claim:convoviz-changelog-2026-02-23"]},
 "descriptors": [
   {"d": "member_present", "arg": "^conversations-\\d{3}\\.json$", "claim": "claim:shards-NNN", "weight": "ledger"},
   {"d": "member_absent",  "arg": "^conversations\\.json$",        "claim": "claim:shards-NNN"},
   {"d": "member_present", "arg": "^chat\\.html$", "claim": "claim:chat-html-ships-in-sharded", "optional": true},
   {"d": "js_marker", "member": "chat.html", "arg": "var jsonData = [", "claim": "claim:chat-html-jsondata", "optional": true},
   {"d": "member_present", "arg": "^export_manifest\\.json$", "claim": "claim:manifest-2026", "optional": true, "note": "no OpenAI source; mimic loop possible"}
 ],
 "distinguishes_from": ["single-2025", "privacy-portal-2026"],
 "unknowns": ["split rule", "jsonData identity", "assetsJson key semantics"],
 "reader": "chatgpt", "format": "openai_chatgpt.json"}
```

Dopasowanie: dla każdego układu liczymy wynik jak w `assess.cpp` (noisy-OR po deskryptorach,
`r_source` = średnia Beta wiarygodności twierdzenia stojącego za deskryptorem, z `calibration.json`
rozszerzonego w §12). Wynik: **lista kandydatów z wynikiem**, nigdy pojedyncza odpowiedź bez
alternatyw; poniżej progu (preset, w danych) → „nieznany układ znanego dostawcy” albo „nieznany
dostawca” → agent z §11.

Pełna przestrzeń możliwości odcisku: wszystkie koniunkcje skończonego słownika nad skończonym
zbiorem nazw wpisów i kluczy. Wyliczanie: **test generatywny** — z każdego profilu układu generator
(`gen_export_fixtures.py`, rozszerzony o układy) buduje syntetyczny eksport, a potem **mutacje**
(usuń wpis, dodaj nieznany wpis, zmień nazwę sharda, zmień opakowanie `array`↔`{"conversations":…}`,
przenieś pod katalog z datą, zagnieźdź w ZIP, zamień klucz) i sprawdza własność: *odcisk nigdy
nie zwraca pewnego, błędnego układu* (albo właściwy, albo „nieznany” z kandydatami). Niepokryte:
realne układy nieobecne w danych (na to §11) i układy nierozróżnialne w słowniku (wtedy wynik
remisowy jest raportowany, nie rozstrzygany).

### 2.4 Jak dodać nowego dostawcę lub wersję bez kodu

1. Nowy plik `formats/<id>.json` (X-Port, już tak działa) **albo** nowy plik układu w istniejącym
   formacie — oba to dane. 2. Klasa w `classes.pack` (lub w nakładce użytkownika `graph_entry`):
   `parent`, `traits`, `label.pl/en`. 3. Profil interfejsu w `web/src/profiles/data/` (opcjonalnie).
   4. Kod jest **dziś** potrzebny wtedy, gdy struktura jest nowego rodzaju (znane rodzaje na
   2026-10-23: drzewo `mapping`, lista `chat_messages`, dziennik aktywności, wiersze SQLite, JSONL
   sesji; to obserwacja z datą, nie lista zamknięta) — wtedy jeden czytnik z kontraktem `iter_units`. Agent z §11 produkuje punkty 1–2 jako **propozycję danych**.

---

## 3. Tryby importu: kopia / link w miejscu / odczyt przez ZIP

### 3.1 Przestrzeń możliwości

Tryb nie jest jedną wartością. Jest **iloczynem**: *element* × *co z nim zrobić* × *gdzie*.

- Elementy (osie klas z §2): kontener; wpisy nierozmowne (`user.json`, `message_feedback.json`,
  `projects.json`, `memories.json`…); dane rozmów; załączniki/aktywa; bliźniaki form (`chat.html`).
- Co zrobić: `copy` (bajty do blobów, wiersze do `messages`), `link` (węzeł + lokalizator, odczyt
  z drzewa/ZIP na żądanie), `both`, `index_only` (tylko metadane, bez treści), `skip_recorded`
  (pominięty, ale zapisany — nic nie znika bez wpisu, zasada z `cut/preserve.py`).
- Gdzie kontener: `unpack:<data>/imports/<source_id>/tree/` | `unpack:beside_source` |
  `unpack:<user path>` | `none` (odczyt przez ZIP, W9) | `keep_zip_blob` (dziś: `put_file` całego
  ZIP-a).

Trzy nazwane tryby (presety tego projektu; odczyt W1/W2/W9 z R57) są **punktami** tej przestrzeni:

| Preset | kontener | wpisy | rozmowy | załączniki | `chat.html` |
|---|---|---|---|---|---|
| `copy` (dzisiejszy) | `keep_zip_blob` | copy | copy → `messages` | copy → blob | skip_recorded |
| `link` (W2, **domyślny globalny — preset**) | `unpack:<data>/imports/…` | link | link (materializacja na żądanie, §4) | link | link + poradnik „porównaj/importuj też HTML” |
| `read_through_zip` (W9, **preset dostępności: domyślnie wyłączony** — R57: „to tylko na wyraźne życzenie użytkownika”; zawsze dostępny, koszt = ostrzeżenie) | `none` | link(zip) | link(zip) | link(zip) | link(zip) |
| `index_only` (dodane: „co mam w eksportach”, bez treści) | dowolny | index_only | index_only | index_only | index_only |
| `both` (dodane: kopia **i** link, DIP13 „kopia, link, oba”) | unpack | both | both | both | link |

Per dostawca/układ presety różnią się tam, gdzie format **dziś** to wymusza, ale **pełny zestaw
trybów obowiązuje dla każdego rodzaju elementu**, także załączników. Gdy eksport nie niesie
bajtów (obserwacja z datą 2026-10-23: eksport Claude.ai, 1 próbka [B]; Gemini: dziennik bez
załączników [B]), tryby `copy` i `link` są dla niego **„nie do zastosowania w tym eksporcie”**
(obserwacja z datą, 1 próbka [B]) i silnik wydaje **nieblokujące ostrzeżenie** („ten eksport nie
ma bajtów załączników; zostają metadane”, z „nie pytaj następnym razem”) — nie usuwa trybów.
Preset załączników dla takiego układu to `index_only` plus opcja `fetch_remote_assets` (pobranie
z `preview_url`/`thumbnail_url` — to wywołanie sieciowe do serwera Anthropic z Pana sesji; zasada
projektu: wywołania sieciowe tylko gdy osoba włączy, z szacunkiem kosztu; preset `ask`). Przyszła
wersja eksportu z bajtami nie wymaga zmiany kodu: wiersz danych w profilu układu i `copy`/`link`
stają się stosowalne. Odwrót: preset załączników zmienia się jednym ustawieniem.

Pełna przestrzeń = wszystkie przypisania; wyliczanie: test generatywny bierze losowe przypisanie
i syntetyczny eksport, importuje i sprawdza własności niezależne od trybu (liczniki rozmów,
wiadomości, gałęzi, załączników równe `EXPECTED.json`; każdy element ma węzeł; `skip_recorded` ma
wpis). Niepokryte: zachowanie na wadliwych ZIP-ach poza fixture'ami (na to istniejące testy
zip-slip i `opaque` w `importer_zip.cpp:64-70`).

### 3.2 Zmiana trybu rozmową i z menu (W1)

- **Rozmową**: intencje w `data/runtime/intent_routing.pack` (dane, pl+en), np. „importuj bez
  kopiowania”, „podlinkuj ten eksport”, „nie kopiuj, tylko opisz”, „pracuj na zipie”, „import by
  link”, „index only”. Słowa są **przykładami** w packu; właściciel dopisuje swoje (M1: słownik
  z jego słów). Rozpoznana intencja → akcja `import.set_mode` z zakresem (ten import / ten dostawca
  / globalnie) i bramką potwierdzenia z `action_rules.pack`, gdy skutek jest nieodwracalny
  (np. usunięcie już skopiowanych wierszy przy przejściu `copy → link` — nigdy automatycznie).
- **Z menu**: ustawienia jako graf (M1): węzeł `import` → `tryb` z polami `has_default/default/edit`
  (`GRAF_HUB` §1: Loom zwraca, czy pole ma wartość domyślną i jaka jest zasada edycji).
- **Zakres**: `scopes.pack` (istnieje) — globalny, dostawca, układ, pojedynczy import, pojedyncza
  rozmowa. Węższy zakres wygrywa; każda wartość pamięta, skąd pochodzi (`source: preset | user |
  conversation:<id>`).

### 3.3 Co znaczy „opisujemy metadanymi” — węzły opisu

Dla każdego importu (w **każdym** trybie, także `copy`): węzeł `file`/`zip_archive` (nazwa, rozmiar,
sha256, mtime, miejsce na dysku, hash blobu gdy jest), węzły `archive_member` (nazwa, rozmiar
nieskompresowany i skompresowany, metoda, CRC32, mtime z ZIP, sha256 liczone przy rozpakowaniu lub
leniwie), węzeł `export` z fasetami (dostawca, układ, wynik odcisku z kandydatami, `known_absent`
z `formats/*.json`, `unknowns` z profilu układu), węzły `conversation` (tytuł, czasy, liczba
wiadomości/gałęzi, lokalizator `{source_id, member, json_pointer}`), węzły `attachment`
(wskaźnik, rozwiązany plik lub `unresolved` z powodem), węzły `account`/`project`/`memory`
(z `user.json`, `projects.json`, `memories.json`). Łącza: `part_of` (wpis → archiwum, rozmowa →
wpis), `expresses` (forma → treść), `mentions`/`attached_to` (wiadomość → załącznik),
`derived_from` (import → źródło), `stored_as` (→ blob). Wszystko to **istniejące** relacje
`classes.pack` :569-625 i `action_results.pack`; nowe są tylko klasy z §2.2.

Podsumowanie importu po polsku (W2, audyt): zdanie generowane z szablonu w packu (pl+en):
„Eksport ChatGPT (układ: podział na pliki, 2026 — dopasowanie 0,91; alternatywa: pojedynczy plik
2025 — 0,12). 13 plików rozmów, 1 240 rozmów, 48 211 wiadomości, 312 gałęzi, 3 801 załączników
(3 790 odnalezionych, 11 nie — lista). `chat.html` podlinkowany, nie porównany — porównać teraz?
Nieznane: reguła podziału; 2 pliki nieznane (`export_manifest.json`, `user_settings.json`) —
zapytać agenta formatów?” Liczby są przykładem.

### 3.4 Graf jako hub — co ten projekt naprawia przy okazji

Audyt: „import writes NO graph nodes”. Tryb `link` wymaga węzłów (bez nich nie ma do czego
linkować), więc **węzły powstają dla każdego trybu** — zadanie G2 („graf pokazuje JEGO dane”)
dostaje realizację w pakiecie P1 (§10).

---

## 4. Węzeł „pamięć podręczna” (working cache)

### 4.1 Co to jest i czym nie jest

Pamięć podręczna to **stan** elementów eksportu („zmaterializowane, edytowane”), pokazany jako
**węzeł główny** (W10; obszar w `data/graph/entry.pack`, id `working_cache`, etykieta pl „Pamięć
podręczna”, en „Working cache”; kolejność i widoczność w nakładce `graph_entry`, jak inne obszary).
Nie jest osobnym magazynem danych: jej **trwałą postacią** jest para *(wiersze w `messages` +
dziennik łat nad źródłem)*. Dzięki temu zwolnienie elementu z pamięci podręcznej **nie traci
edycji** — one są w dzienniku łat (§5b), a wiersze da się odtworzyć ze źródła i łat.

### 4.2 Co tam żyje

Węzły `cache_entry` z łączem `caches` → element (rozmowa, wpis, załącznik, wiadomość) i stanem:
`materialized` (wiersze istnieją; odczyt z `messages`), `dirty` (są łaty niezapisane do źródła),
`written_back` (łaty zastosowane do drzewa/ZIP w trybie §5c–d), `evicted` (wiersze usunięte,
łaty zostają), `pinned` (nie zwalniać). Także elementy **nie-rozmowne** (edytowany `projects.json`,
plik załącznika otwarty do podmiany) — pamięć podręczna nie jest tylko dla rozmów.

### 4.3 Cykl życia

1. **Otwarcie** rozmowy z węzła eksportu → materializacja: czytnik (`iter_units`) z lokalizatora →
   wiersze `messages` z `metadata.export.locator`, `conv.metadata.import_mode="link"`, rekord
   proweniencji `pv_` z `transform="import.link@1"` (`provenance.h:86-93`). Preset: materializacja
   **przy otwarciu** (leniwa); alternatywy: `on_import` (wszystko od razu = de facto `both`),
   `on_search_hit`, `manual`.
2. **Edycja / kontynuacja** → łata do `<data>/imports/<source_id>/overlay/<conv_id>.patch.jsonl`
   (RFC 6902, jak `oplog.jsonl` X-Port §3.15: operacja, zakres, hash przed/po, odwrotność albo
   migawka) **i** aktualizacja wierszy. Źródło (drzewo rozpakowane / ZIP) **nietknięte**, dopóki
   osoba nie wybierze zapisu zwrotnego.
3. **Zapis zwrotny** → akcja z §5c/§5d; po sukcesie stan `written_back`, łaty pozostają jako
   historia (odwrót = odwrotność łat).
4. **Zwolnienie** → preset `manual` (domyślnie **nic nie jest zwalniane samo**); alternatywy jako
   dane: `on_written_back`, `lru(size=<wartość osoby>)`, `lru(age=<wartość osoby>)`. **Żadnej
   liczby w kodzie**; jeśli osoba nie ustawi progu, progu nie ma. Poradnik nieblokujący, gdy
   osoba ustawiła próg i został przekroczony, z „nie pytaj więcej”.

### 4.4 Relacja do tabeli `messages`

Zmaterializowana rozmowa **jest zwykłą rozmową** dla `chat_panel`, listy rozmów, wyszukiwarki
i pamięci — nic w interfejsie się nie zmienia. Różnica: `conversations.source="import:<id>"`,
`metadata.import_mode`, lokalizator w każdej wiadomości, i to, że wiersze **wolno usunąć**
(zwolnić), bo da się je odtworzyć. Decyzja: **bez drugiej tabeli**; `messages` jest magazynem
pamięci podręcznej. Odwrót: tabela `cache_entries` z tymi samymi kolumnami — migracja kopiująca.

### 4.5 Proweniencja i epistemika

Każdy `cache_entry` wie: z jakiego źródła (`src_`), jakiego układu (id odcisku + wynik), jakim
czytnikiem (`parser`, `parser_version` z `SourceRecord`), z jakimi łatami. Tekst wiadomości
z eksportu ma status epistemiczny `observed` (klasa dowodu z `calibration.json` → `isotonic`),
a wiadomości dopisane w Loom — `derived`. Odpowiedź na „skąd to wiesz” jest łączem, nie zdaniem.

---

## 5. Macierz kontynuacji i modyfikacji rozmów z eksportu

Każdy wiersz: co znaczy; nakład w **pakietach** (pakiet ≈ 50 wywołań narzędzi agenta; liczba jest
**osądem**, czas liczy koordynator z mierzonego tempa); ryzyka; jak testować (obieg zamknięty na
syntetycznych fixture'ach + własności generatywne). Wszystkie opcje są **konfiguracją**; „na
życzenie” to preset dostępności, nie usunięcie.

| | Opcja | Co znaczy | Pakiety | Ryzyka | Test |
|---|---|---|---|---|---|
| **a** | **Kontynuacja w Loom** z `derived_from` do wiadomości źródłowej | Nowa gałąź/rozmowa w Loom; pierwsza nowa wiadomość ma `derived_from` → wiadomość z eksportu (lokalizator), rozmowa ma `continues` → rozmowa źródłowa; model dostaje historię z eksportu jako kontekst | 1 | żadne dla źródła (nietknięte); ryzyko tylko w kontekście: czego eksport **nie ma** (`known_absent`: pełne rozumowanie, prompt systemowy, bajty plików Claude) — poradnik przy pierwszej kontynuacji | fixture → kontynuuj → łącza istnieją, źródło hash-identyczne |
| **b** | **Edycja jako nakładka/łata** nad nietkniętym źródłem | §4.3 p. 2; widok = źródło + łaty; cofnięcie = odwrotność | 1–2 | konflikt, gdy źródło się zmieni pod spodem (nowy eksport) — wykrywany hashem (`before`), rozwiązywany jak X-Port `diff`: nowy, zmieniony, usunięty | własność: `apply(invert(apply(p)))` = identyczność; losowe sekwencje łat |
| **c** | **Zapis zwrotny do rozpakowanego drzewa z zachowaniem formatu** | Łaty → obiekt rozmowy w **tym samym shardzie**, re-serializacja deterministyczna (porządek kluczy i spacje jak w źródle — wykryte z próbki, nie założone), `update_time`, `current_node`, symetria `parent/children`; rekordy w `shared_conversations.json`, `message_feedback.json`, `textdocs/*.json` z `conversation_id`; aktywa po wskaźniku; `user.json` nietknięty. **Bliźniak HTML**: opcja `html_twin: skip | rewrite | advise` — przy `rewrite` ta sama rozmowa w `jsonData` (jedna tablica, ucieczka `</script>`, U+2028/2029, zachowane znaczniki `var jsonData = ` / `var assetsJson`, wpisy `assetsJson` dla dodanych/usuniętych wskaźników). **Reguła podziału**: nieznana [A–D], więc zachowujemy **obserwowaną przynależność** (rozmowa zostaje w swoim shardzie); nowa rozmowa: preset `last_shard` \| `new_shard` \| `by_observed_count` (gdy pomiar lokalny wykaże stałą liczbę) \| `by_observed_bytes` | shard: 2; HTML: 2–3; rekordy towarzyszące: 1 | HTML: plik setek MB, ekstrakcja po nawiasach, brak schematu; semantyka kluczy `assetsJson` sporna (id wiadomości vs wskaźnik) → obie hipotezy jako dane, wybór po lokalnym teście; JSON: byte-identity nietkniętych rozmów trzeba **pilnować testem**, nie zakładać | **obieg**: fixture → import link → edycja → zapis → ponowny import obiema ścieżkami (loom, `engine/importer.py`) → liczniki = `EXPECTED.json`; nietknięte rozmowy bajt w bajt; tknięte równe po `json.dumps(sort_keys=True)`; HTML: zbiór id `jsonData` = zbiór id shardów. **Naprawa fixture `chat.html`** (tablica + `assetsJson` + zaślepka JS) jest warunkiem — dziś test nie ćwiczy prawdziwego kształtu |
| **d** | **Przepakowanie do ZIP** — **preset dostępności: domyślnie wyłączony, o jedno ustawienie od włączenia** (R57: „to tylko na wyraźne życzenie użytkownika”); opcja zawsze dostępna, koszt = nieblokujące ostrzeżenie | Nowy ZIP: nietknięte wpisy **kopiowane surowo** (`mz_zip_writer_add_from_zip_reader`, `miniz.h:1384`: „fully clones the source file's compressed data (no recompression)”), tknięte deflate albo store (`MZ_NO_COMPRESSION`, :270), **deterministyczny porządek** = porządek źródła (`MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY` :1069), zip64 gdy trzeba (`mz_zip_is_zip64` :1253, `MZ_ZIP_FLAG_WRITE_ZIP64` :1072); alternatywa `append` (`mz_zip_writer_init_from_reader` :1347 — dopisanie wpisu, stary zostaje jako martwe bajty) | 1–2 | rozmiar dysku ×2 w czasie przepakowania; `append` rośnie bez kompaktowania; mtime/atrybuty wpisów — kopiowane przez miniz razem z nagłówkiem [C, komentarz w nagłówku, zachowanie na realnym ZIP-ie **niezweryfikowane**] | hash każdego nietkniętego wpisu po rozpakowaniu równy; CRC z centralnego katalogu równe; `unzip -t`; ponowny import |
| **e** | **Zapis w formacie dostawcy jako pełnoprawna konfiguracja** (nie „eksport na życzenie”) | c+d dostępne jako stały tryb pracy: każda edycja/kontynuacja od razu trafia do drzewa (i opcjonalnie ZIP-a) w formacie dostawcy; Loom jest wtedy edytorem eksportu | 1 (nad c, d) | **R58**: użyteczność nie jest powodem do degradacji. Konsument: ChatGPT — **prawdopodobnie istnieje** (artykuł 9106926 o przenoszeniu rozmów między kontami [A, tylko streszczenie, niepobrany]; jakie pola wymaga — nieznane); Claude.ai, Gemini — **nie znaleziono** (obserwacja z 2026-10-23, [A]–[D]; negatyw niezweryfikowany). Poradnik nieblokujący „nie znamy odbiorcy tego formatu poza ChatGPT (niepotwierdzone) — tak miało być?” z „nie pytaj więcej” | jak c+d, w pętli: N losowych edycji → N zapisów → import końcowy = stan końcowy pamięci podręcznej |
| **f** (dodane) | **Lustrzana gałąź** | a + c: kontynuacja w Loom **i** zapis jako nowa gałąź drzewa dostawcy (OpenAI: `mapping` ma gałęzie natywnie [B]; Claude: gałęzie w ZIP-ie **niezweryfikowane** [D] → zapis jako nowa rozmowa z `parent_message_uuid`, jeśli lokalny pomiar wykaże, że eksport je nosi) | 1 (nad a, c) | jak c | jak c, plus: liczba gałęzi +1 |
| **g** (dodane) | **Scalenie między dostawcami** | Kontynuacja rozmowy ChatGPT w Claude (X-Port `merge_conversations` z zachowaniem źródła każdej wiadomości, §5.12) | 1 (jest w X-Port; dołączyć do akcji) | utrata pól nieprzekładalnych — raport | obieg przez `handoff/` |
| **h** (dodane) | **Kontynuacja u dostawcy przez API** | Historia z eksportu jako kontekst wywołania modelu u tego dostawcy (OpenRouter/API) — to **nie** zapis do jego aplikacji | 1 | koszt tokenów (szacunek przed, rzeczywiste po; reguła ×10) | istniejące testy `use_model` |
| **i** | **Zapis do aplikacji dostawcy po stronie serwera** (edycja istniejącej rozmowy na jego koncie) | — | — | **niedostępne, dopóki droga nie jest znana** dla Claude.ai i Gemini: *brak znanej drogi* (obserwacja z 2026-10-23, [A]–[D] bez punktu wejścia; negatyw niezweryfikowany) — opcja zostaje w tabeli i w silniku, nieblokujące ostrzeżenie z „nie pytaj więcej”; nowa droga (np. API dostawcy) = nowy wiersz danych, bez zmiany kodu. Dla ChatGPT jedyna znana droga to **import całego zestawu** do konta (9106926, [A] streszczenie), nie edycja per wiadomość | — |

Pełna przestrzeń opcji = {cel: Loom, drzewo, ZIP, inny dostawca, API dostawcy, aplikacja
dostawcy} × {operacja: dopisz, zmień, usuń, rozgałęź, scal} × {zakres: wiadomość, rozmowa, zbiór}.
Tabela wylicza punkty wynikające z odczytu R57 i te, które wynikają z iloczynu; test generatywny
losuje trójkę i sprawdza, że akcja albo istnieje, albo jest jawnie oznaczona jako „niedostępne,
dopóki droga nie jest znana” (albo — tylko przy niemożliwości technicznej — NOT POSSIBLE) z powodem,
datą i poziomem źródła. Niepokryte: dostawcy nieobecni w danych.

### 5.1 Własny eksport Loom jako format do czytania przez innych

Reguła „brak dziś nie znaczy brak na zawsze” (R60, `CLAUDE.md`): nie wiadomo, kto i kiedy będzie czytał
format, który produkujemy, więc **własny eksport Loom (klasa `export.loom`, pakiet `handoff/` z X-Port)
jest projektowany jako czytelny, opisany i wersjonowany format**, nie jako wewnętrzny zrzut. To
**decyzje projektu** (nie słowa właściciela), każda z odwrotem:

1. **D5-5 Identyfikator schematu i wersji** w manifeście (dziś w X-Port `loom.extract.handoff/1`;
   nazwa jest przykładem), a w obrębie wersji znaczenie pól się nie zmienia. Odwrót: nowa wersja
   obok starej (czytnik zna obie), nigdy nadpisanie.
2. **D5-6 Własny odcisk układu** (§2.3) i ten sam rodowód klas: `export` → `export.loom` → układ/wersja —
   tą samą mechaniką rozpoznajemy cudze i własne eksporty, więc obcy czytnik dostaje to samo
   „skąd to wiesz” co my. Odwrót: odcisk zostaje danymi, można go pominąć w manifeście presetem.
3. **D5-7 Manifest czytelny bez Loom**: lista plików, liczniki, hashe, proweniencja każdego elementu,
   `known_absent`. Odwrót: pola opcjonalne, okrojenie jednym presetem (z ostrzeżeniem, nie blokadą).
4. **D5-8 Opis formatu** (pola, wersje, syntetyczne przykłady) generowany z tych samych danych
   (`formats/*.json`) i wersjonowany z kodem; wysłanie go na zewnątrz tylko gdy osoba zdecyduje
   (zero wywołań sieciowych). Odwrót: opis zostaje lokalnym plikiem.
5. Wiersz rejestru §12 dla własnego formatu startuje z poziomem [C] (nasz kod); „nikt poza nami go dziś
   nie czyta” to obserwacja z datą 2026-10-23, nie argument za mniej czytelnym formatem.

---

## 6. Koszt obliczeniowy ZIP — czy obawa W9 jest słuszna

Fakty o formacie i o miniz 11.0.2 ([C], `third_party/miniz/miniz.h`):

1. **Odczyt spisu** = odczyt centralnego katalogu na końcu pliku: koszt ~ liczba wpisów, nie
   rozmiar archiwum. Dla eksportu z kilkunastoma tysiącami plików to kilobajty–megabajty.
2. **Odczyt jednego wpisu** = seek + inflate **tylko tego wpisu** (każdy wpis ma własny strumień
   deflate); strumieniowo przez `mz_zip_reader_extract_to_callback` (:1275) lub
   `mz_zip_reader_extract_iter_new` (:1279). **Tani**. Praca „wyłącznie na ZIP-ie” w trybie odczytu
   nie jest droga; droższe od drzewa na dysku tylko o inflate.
3. **Zmiana jednego wpisu**: ZIP **nie ma** aktualizacji w miejscu. Dwie drogi: (i) nowe archiwum
   z surową kopią nietkniętych wpisów (`mz_zip_writer_add_from_zip_reader`) — **I/O = rozmiar
   archiwum, CPU = deflate tylko zmienionych wpisów**; (ii) dopisanie
   (`mz_zip_writer_init_from_reader`) — I/O = zmieniony wpis + nowy katalog, archiwum rośnie
   o martwe bajty, trzeba kiedyś skompaktować (i).
4. **Kompresja zmienionych wpisów**: preset poziomu z `MZ_NO_COMPRESSION` (CPU ≈ 0, większy plik)
   do `MZ_BEST_COMPRESSION`; domyślny preset = ten, który miało źródło (odczytany z nagłówka
   wpisu).
5. **Rozpakowanie całości** (tryb `link`) = inflate wszystkiego raz; potem praca na drzewie bez
   kosztu ZIP. Koszt dysku: rozmiar nieskompresowany (JSON kompresuje się dobrze, więc drzewo może
   być kilkukrotnie większe od ZIP-a — **to też jest „kopia”**, §13 p. 3).
6. **Prawdziwy ciężar**: bliźniak `chat.html`. Jeden zgłoszony eksport: shardy ~650 MB **i**
   `chat.html` ~599 MB [B, wnioskowanie z README, oznaczone „when populated”]. Zachowanie
   bliźniaka przy zmianie jednej rozmowy = przepisanie jednej tablicy w jednym pliku ≈ rozmiar
   `jsonData`. Nie da się tego zmniejszyć bez porzucenia tożsamości bajtowej HTML (opcja
   `html_twin: skip` z poradnikiem).
7. **Na telefonie** wszystko powyżej jest wolniejsze, prawdopodobnie o rząd; rząd jest **do zmierzenia
   w P5** (§10), dziś nie jest zmierzony ani założony.

Werdykt: obawa jest **słuszna dla przepakowania z rekompresją całości** i dla bliźniaka HTML,
**niesłuszna dla odczytu** i dla zmiany pojedynczego wpisu z kopią surową. Dlatego presety:
`read_through_zip` dostępny bez ostrzeżeń dla odczytu; `repack` jako **preset dostępności**
(R57, dosłownie: „to tylko na wyraźne życzenie użytkownika”; domyślnie wyłączony, o jedno
ustawienie od włączenia, **opcja zawsze dostępna** — koszt jest nieblokującym ostrzeżeniem, nie
powodem usunięcia) z wyświetlonym szacunkiem (I/O = rozmiar archiwum, CPU = suma zmienionych wpisów,
HTML = rozmiar `jsonData`) **przed** wykonaniem i pomiarem **po**; reguła ×10 dotyczy pomiaru.
Pakiet pomiarowy (§10 P5) mierzy na syntetycznym eksporcie o rozmiarze zbliżonym do Pana
prawdziwego (rozmiary Pan podaje, dane są syntetyczne).

---

## 7. Tabela EASY / MEDIUM / HARD / NIEDOSTĘPNE (nakład i ryzyko, nie pozwolenie)

Każda opcja pozostaje konfiguracją; „domyślnie wył.” to preset dostępności; poradnik nieblokujący
tam, gdzie użyteczność wątpliwa (R58). „NOT POSSIBLE” jest zarezerwowane dla niemożliwości
**technicznej** (np. kod po stronie serwera dostawcy, którego nie mamy i nie możemy uruchomić);
w tej tabeli nie występuje żaden taki wiersz. „Niedostępne, dopóki droga nie jest znana” to
obserwacja z datą i poziomem źródła (R60), nie zakaz.

| Opcja | Nakład | Ryzyko | Preset | Poradnik |
|---|---|---|---|---|
| Węzły grafu dla każdego importu, podsumowanie po polsku | EASY | niskie | wł. | — |
| Odcisk dostawca+układ jako dane, kandydaci z wynikiem | MEDIUM | średnie (jakość danych) | wł. | gdy remis/nieznany |
| Tryb `link` (trwałe drzewo, lokalizatory, materializacja na żądanie) | MEDIUM | niskie | **wł. globalnie** | — |
| Tryb `read_through_zip` (odczyt) | EASY–MEDIUM | niskie | dostępny | — |
| Tryb `copy` (dzisiejszy) | jest | — | dostępny | — |
| `both`, `index_only` | EASY | niskie | dostępne | — |
| Pamięć podręczna + dziennik łat + zwalnianie ręczne | MEDIUM | niskie | wł. | przy progu ustawionym przez osobę |
| (a) kontynuacja w Loom z `derived_from` | EASY | niskie | wł. | przy pierwszym razie: `known_absent` |
| (b) nakładka/łaty | MEDIUM | niskie | wł. | — |
| (c) zapis zwrotny do shardu JSON z zachowaniem przynależności | MEDIUM | średnie (determinizm serializacji) | dostępny, pyta przez bramkę (zmienia dane osoby) | — |
| (c) bliźniak HTML `rewrite` | MEDIUM–HARD | wysokie (setki MB, brak schematu, sporna semantyka `assetsJson`) | dostępny; preset `advise` | „HTML nie porównany z JSON; przepisać, pominąć, porównać?” |
| (c) rekordy towarzyszące (`shared_conversations`, `feedback`, `textdocs`) | EASY–MEDIUM | średnie (listy pól z [B]) | wł. przy (c) | — |
| (d) przepakowanie z kopią surową, zip64, store/deflate | MEDIUM | średnie (dysk ×2) | **domyślnie wył. — preset dostępności** (R57: „to tylko na wyraźne życzenie użytkownika”), zawsze dostępny | szacunek kosztu przed (ostrzeżenie, nie blokada) |
| (d) `append` zamiast przepakowania | EASY | średnie (wzrost pliku) | dostępny | „plik urósł o X, skompaktować?” |
| (e) zapis w formacie dostawcy jako stały tryb | EASY nad c+d | jak c+d | dostępny | „znany odbiorca: ChatGPT (niepotwierdzone); inni nieznani — tak miało być?” |
| (f) lustrzana gałąź | EASY nad a+c | jak c; Claude: gałęzie niezweryfikowane | dostępny | przy Claude: „eksport może nie nosić gałęzi” |
| (g) scalenie między dostawcami | EASY (jest w X-Port) | utrata pól — raport | dostępny | — |
| (h) kontynuacja u dostawcy przez API | EASY | koszt tokenów | dostępny | szacunek kosztu |
| Pobranie aktywów Claude z `preview_url` | EASY | prywatność (wywołanie do serwera Anthropic) | preset `ask` | koszt i adresat |
| Użycie `assetsJson` do rozwiązywania wskaźników | EASY | sporna semantyka | drugi resolver po wzorcu nazw; który zadziałał — zapisane | — |
| (i) edycja rozmowy w aplikacji dostawcy po stronie serwera | **niedostępne, dopóki droga nie jest znana** (Claude.ai, Gemini: brak znanej drogi — obserwacja z 2026-10-23, [A]–[D]; negatyw niezweryfikowany) | — | dostępne jako opcja w silniku, bez wykonania, gdy brak drogi | ostrzeżenie „brak znanej drogi” z „nie pytaj więcej”; ChatGPT: tylko import całości do konta [A, streszczenie] |
| Agent nieobsługiwanych formatów (§11) | HARD | koszt, prywatność, jakość | preset `ask` | zawsze przed wyjściem w sieć |
| Rejestr wiarygodności (§12) | MEDIUM | — | wł. (bez wywołań) | — |

---

## 8. Wyświetlanie w interfejsie dostawcy przez profile aplikacji

**Jest** ([C], `docs/APPLICATION_PROFILES.md`, `loom/web/src/profiles/`): kontrakt
`loom.application_profile/1` (tożsamość aplikacji, `target.version` może być `null` — „unknown is
explicit”, `evidence.sources/gaps`, `presentation` z tokenami i rendererem `chat`, `composer`,
`actions` mapowane na operacje Loom, `workflows` jako automaty skończone); profile
`chatgpt-inspired(-r2)`, `claude-inspired(-r2)`, `gemini-inspired(-r2)`, `loom-default(-r2)`,
`librechat-0.8.8`, `nextchat-2.16.1`. Walidacja: brak wymaganego adaptera blokuje aktywację,
braki opcjonalne widoczne. To jest wycinek **web**.

**Luki** wobec W3:

1. Brak renderera **przeglądarki eksportu**: lista wpisów, boczny panel w kształcie aplikacji
   dostawcy zasilany **z eksportu** (ChatGPT: rozmowy, udostępnione, canvas z `textdocs/`; Claude:
   projekty z `projects.json`, wspomnienia z `memories.json`, `design_chats/` (niezweryfikowane — Aneks B p. 5); Gemini: dni/wątki
   z dziennika). Nowy renderer `export_browser` + dane w profilu (`sidebar.sources` = klasy
   z §2.2). 2 pakiety.
2. Brak **przypisania profilu wg źródła importu** (UI18): reguła w danych
   `profile_by_source: {"export.openai.chatgpt": "chatgpt-inspired", …}` z zakresami jak §3.2;
   osoba zmienia per rozmowa. 1 pakiet.
3. **Wersje archiwalne** (UI14): `target.version` jest `null` wszędzie; profil układu (§2.3) niesie
   `observed.from/until`, więc profil interfejsu może deklarować, do jakiego **okresu** eksportu
   pasuje; bez dowodu — `evidence.status: "inspired"`, jak dziś. Zbieranie dowodów: osoba może
   nagrać zachowanie oryginału (RE31). Nakład zależny od dowodów, nie od kodu.
4. **Wierność jest ograniczona przez eksport**, nie przez profil (obserwacja z 2026-10-23, [B]): dziś Claude bez bajtów plików,
   Gemini bez id rozmów i często bez odpowiedzi [B]; profil ma pokazywać **puste miejsce
   z powodem** (`known_absent`), nie udawać.
5. Android: profile są dziś w `loom/web`; ścieżka na telefon przez istniejący serwer/webview
   (M1) — potwierdzić w planie M1, tu **niezweryfikowane**.

---

## 9. Migracja z importera kopiującego

Zasada: dane już zaimportowane **nie giną i nie są przepisywane po cichu**; zgodność z Pythonem
nie jest wymagana (2026-09-29), ale `schema_version "4"` zostaje sentinelem — tu nie trzeba go
ruszać (nowe rzeczy to węzły, pliki i `metadata`).

1. **Istniejące importy** mają rekordy `SourceRecord` (`provenance.h:69-80`: `kind="zip_member"`,
   `uri`, `blob_hash`, `format`, `parser`, `parser_version`) i rekordy `pv_` per rozmowa/wiadomość.
   Pakiet migracji **tworzy węzły grafu z tych rekordów** (plik, wpisy, rozmowy, łącza) i oznacza
   `import_mode="copy"` — bez dotykania wierszy.
2. **Podniesienie do `link`** (na życzenie, przez bramkę): jeśli blob całego ZIP-a jest w magazynie
   (`put_file` zewnętrznego archiwum, `importer_core.cpp:240`), rozpakować go do drzewa, policzyć
   lokalizatory (dopasowanie po `ext_id` rozmowy i id wiadomości z `metadata.export`), dopisać
   do wierszy. Wiersze **zostają** (stan `materialized`); ich usunięcie to osobna akcja
   nieodwracalna z bramką.
3. **Ponowny import tego samego eksportu** w trybie `link`: deduplikacja po `blob_hash`
   (`find_sources_by_hash`, `note_source_duplicate` już są) → zamiast drugiej kopii: dowiązanie
   istniejących rozmów do nowego węzła źródła.
4. Fixture i `EXPECTED.json`: te same liczniki w obu trybach — test migracji = import `copy` →
   migracja → liczniki równe, hashe wierszy równe.

---

## 10. Pakiety wykonawcze w kolejności (akceptacja; liczby pakietów = osąd)

| # | Pakiet | Zawartość | Akceptacja | Właściciel dotyka na telefonie |
|---|---|---|---|---|
| P1 | **Import jako węzły** | klasy z §2.2 w `classes.pack`; każdy import (także `copy`) tworzy węzły pliku/wpisów/rozmów/załączników z łączami; podsumowanie po polsku z szablonu pl+en; obszar wejścia „Importy” | `EXPECTED.json` liczniki = liczby węzłów; podsumowanie nie zawiera surowego JSON; test na 6 fixture'ach | **tak**: po imporcie widzi eksport jako węzeł z rozmowami i metadanymi (G2) |
| P2 | **Odcisk układu** | słownik deskryptorów, profile układów OpenAI ×4, Anthropic ×2, Gemini ×1 (z poziomami źródeł), kandydaci z wynikiem; naprawa fixture `chat.html` (tablica, `assetsJson`, zaślepka JS) i predykatu shardów | test generatywny mutacji: nigdy pewny błędny układ; `chat.html` fixture parsowalny jak u 7 parserów [B] | tak: w podsumowaniu widzi układ i alternatywy |
| P3 | **Tryb `link`** | trwałe drzewo `imports/<src>/tree`, lokalizatory, materializacja przy otwarciu, presety per dostawca, zmiana z menu i rozmową (intencje pl+en), `read_through_zip` dla odczytu | import `link` → otwarcie rozmowy → wiersze z lokalizatorami; liczniki równe `copy`; żaden element bez wpisu | **tak**: otwiera rozmowę z eksportu bez kopiowania całości |
| P4 | **Pamięć podręczna + łaty + (a), (b)** | węzeł `working_cache`, `cache_entry`, dziennik łat, kontynuacja z `derived_from`, zwalnianie ręczne | własność `apply∘invert = id`; zwolnienie i odtworzenie daje te same wiersze | tak: widzi, co ma „w pamięci podręcznej”, kontynuuje rozmowę z ChatGPT w Loom |
| P5 | **Zapis zwrotny (c) do shardu + pomiar kosztów** | deterministyczna re-serializacja, przynależność do shardu, rekordy towarzyszące, `html_twin: advise`; benchmark odczyt/zmiana/przepakowanie na syntetycznym eksporcie o rozmiarach podanych przez właściciela | obieg: edycja → zapis → import obiema ścieżkami → liczniki; nietknięte bajt w bajt | tak: edytuje tytuł rozmowy i widzi zmianę w drzewie eksportu |
| P6 | **Bliźniak HTML `rewrite` + (d) przepakowanie + (e)** | ekstrakcja po nawiasach, ucieczki, `assetsJson` dwie hipotezy; miniz surowa kopia, zip64, store/deflate, porządek źródła; poradniki R58 | zbiór id HTML = shardów; hashe nietkniętych wpisów ZIP równe; `unzip -t` | tak: „zapisz jako ZIP” z szacunkiem kosztu |
| P7 | **Widok w profilu dostawcy** | renderer `export_browser`, `profile_by_source`, puste miejsca z powodem | profil Claude pokazuje projekty/wspomnienia z fixture; brak bajtów plików pokazany, nie ukryty | **tak**: eksport Claude „jak w apce Claude” |
| P8 | **Migracja** (§9) | węzły z `SourceRecord`, podniesienie do `link` przez bramkę, deduplikacja | import `copy` → migracja → liczniki i hashe równe | tak: stare importy pojawiają się w grafie |
| P9 | **Rejestr wiarygodności** (§12) | rozszerzenie `calibration.json`, węzły źródeł/twierdzeń, pętla testów lokalnych, widok i nakładka | lokalny test twierdzenia zmienia Beta źródła; decay/wersja wg reguły | tak: przy układzie widzi „skąd to wiemy i jak pewnie” |
| P10 | **Agent nieobsługiwanych formatów** (§11) | wyzwalacz, sanitizer deskryptorów, kroki, hipoteza jako dane, testy lokalne, adopcja po potwierdzeniu | syntetyczny „nowy układ” → agent (z atrapą sieci) → profil → test lokalny → potwierdzenie → import działa | tak: nieznany eksport → pytanie i propozycja |

Kolejność jest presetem koordynatora; P1–P3 dają pierwsze rzeczy do dotknięcia.

---

## 11. Agent nieobsługiwanych formatów w czasie działania (R59 p. 3c)

**Wyzwalacz:** odcisk (§2.3) nie daje kandydata powyżej progu **albo** znany dostawca, ale nieznane
wpisy/klucze (np. nowy plik główny) **albo** osoba prosi wprost („sprawdź w sieci, co to za
format”).

**Preset uruchomienia:** `ask | auto | never` — domyślnie `ask` (zasada projektu: wywołania modelu
i sieci tylko gdy osoba włączy, z szacunkiem kosztu: liczba zapytań, tokeny, które adresy).

**Prywatność — co wolno wysłać:** wyłącznie **deskryptory strukturalne po sanityzacji**: nazwy
wpisów z zastąpionymi identyfikatorami (`user-<id>/file_<hex>-<uuid>.png` → literalnie z
placeholderami; nazwy plików użytkownika `file-<id>-<original_name>` → **bez** `<original_name>`,
bo to dane osoby), nazwy kluczy JSON (nie wartości), rozszerzenia, ciągi wersji/schematów
(`"schema": "…"`), znaczniki (`var jsonData`), tytuł HTML, liczności w klasach rzędu wielkości.
**Nigdy**: treść wiadomości, tytuły rozmów, nazwy własne z wartości, adresy e-mail, id kont.
Sanitizer jest kodem z testem własności: wyjście nie zawiera żadnej wartości z JSON-a (tylko
klucze), żadnego ciągu dopasowanego do wzorców id/e-mail/URL osoby. Osoba **widzi zapytanie przed
wysłaniem** (preset `ask`) i może je zredagować.

**Kroki (jak w W8 i R59 p. 4):**

1. **Dokumentacja oficjalna** (domeny dostawcy: `help.openai.com`, `support.claude.com`,
   `support.google.com`, `developers.google.com`) — zapytania z deskryptorów; wynik: strony jako
   **dane** (adres, data pobrania, fragmenty z deskryptorami). Uwaga z badania: te domeny bywały
   **niedostępne** z naszego środowiska (DNS/proxy) — agent zapisuje „niepobrane, tylko
   streszczenie” jako poziom A−.
2. **Źródła fachowe i utrzymywane parsery** (repozytoria z testami, changelogi z datami).
3. **Źródła słabsze** (notatki, fora) — **preset kaskady** (przykład, w danych, do zmiany): sięgamy
   po nie, gdy kroki 1–2 milczą; sam próg „milczą” (ile i jakich wyników) jest presetem tego
   projektu, nie słowami właściciela (W8 mówi: „dopiero z braku wiarygodnych”; odwrót: zmiana presetu
   albo ręczne włączenie kroku 3 wprost); zawsze oznaczone [D].
4. **Hipoteza jako DANE**: profil układu (§2.3) z `descriptors[]`, każdy z `claim` i `source`;
   plus hipotezy czytnika (mapowanie pól do `formats/*.json`: `conversation.ext_id`, `tree`,
   `message.role`…) — kilka konkurencyjnych, jeśli źródła się różnią (np. dwie semantyki
   `assetsJson`).
5. **Testy lokalne każdej hipotezy na plikach osoby** (nic nie wychodzi): czy deskryptory pasują;
   czy czytnik z mapowaniem daje spójne drzewo (symetria parent/children, jeden korzeń, czasy
   monotoniczne, rozłączność id między shardami, zbiór id HTML = JSON, wskaźniki aktywów
   rozwiązane); liczniki. Wynik per hipoteza: `passed/failed/inconclusive` z liczbami.
6. **Raport** po polsku: które hipotezy przeszły, co pozostaje nieznane, które źródła miały rację.
7. **Adopcja tylko po potwierdzeniu** osoby: profil układu trafia do nakładki danych
   (`<data>/formats/…`), z `provenance` (źródła, daty, kto potwierdził) i `reliability`
   (z §12). Nic nie zmienia się w packach programu („ROM”); propozycja do repo to osobny krok
   (PR z danymi), nigdy automatyczny.
8. **Zwrotnie** wyniki testów zasilają rejestr §12 (źródła, które przeszły test, zyskują).

Pełna przestrzeń: {dostawca znany/nieznany} × {kontener} × {struktura: drzewo/lista/dziennik/
wiersze/nowa} × {dostępność sieci: tak/nie/ograniczona}. Test: syntetyczne „nowe układy”
z generatora (mutacje) + atrapa sieci zwracająca przygotowane strony o różnej jakości → agent musi
dojść do poprawnego profilu albo uczciwie powiedzieć „nie wiem” — nigdy do pewnego błędu.
Niepokryte: realna jakość wyszukiwania (tylko na żywo, z zapisem do rejestru).

---

## 12. Rejestr wiarygodności źródeł (R59 p. 5) dla wiedzy o formatach eksportu

**Precedens, rozszerzany, nie wymyślany ponownie:** `loom/data/policy/calibration.json`
(`loom.kb.calibration/1`): `reliability.<extractor|rule>: {alpha, beta}` (Beta), `raw = noisy-OR
po jednostkach 1 − ∏(1 − r_source·q)`, izotoniczna mapa per klasa dowodu (`observed`, `derived`,
`inferred`, `extrapolated`), `min_labels`, bramki `ece`; użycie w `src/resolve/assess.cpp:85-114`
(średnia Beta z `alpha/(alpha+beta)`, `reliability_key`).

**Rozszerzenie (dane, ten sam plik lub `calibration.d/export_formats.json` ładowany tak samo):**

```json
{"reliability": {
   "source.domain:support.claude.com":   {"alpha": 9, "beta": 1, "tier": "A", "note": "fetched 2026-10-22"},
   "source.domain:help.openai.com":      {"alpha": 5, "beta": 1, "tier": "A", "note": "search summaries only"},
   "source.repo:mohamed-chs/convoviz":   {"alpha": 8, "beta": 2, "tier": "B"},
   "source.repo:temnoon/openai_export_parser": {"alpha": 3, "beta": 3, "tier": "D"},
   "author:mohamed-chs":                 {"alpha": 8, "beta": 2},
   "claim:chat-html-jsondata":           {"alpha": 7, "beta": 1, "asserted_by": ["source.repo:tka85/…", "…×7"]},
   "claim:shards-NNN":                   {"alpha": 8, "beta": 1},
   "claim:jsondata-equals-json":         {"alpha": 1, "beta": 1, "asserted_by": ["source.repo:SmuklyLew/jazn_latka"]},
   "claim:split-rule":                   {"alpha": 1, "beta": 1, "status": "unknown"}
 },
 "validity": {
   "claim:shards-NNN": {"provider": "openai.chatgpt", "from": "2026-02", "until": null},
   "claim:single-conversations-json": {"provider": "openai.chatgpt", "from": "2023-04", "until": "2026-02"}
 },
 "decay": {"rule": "half_life_from_observed_changes", "min_changes": 3, "fallback": "none"},
 "update": {"test_pass": {"alpha": 1}, "test_fail": {"beta": 1}, "assert_only": {"alpha": 0, "beta": 0},
            "propagate_to_author": true, "propagate_to_domain": 0.5}}
```

Liczby `alpha/beta` powyżej to **priory startowe** (przykład; „hand-set until the dev ground truth
exists”, jak w precedensie). Reguły:

- **Aktualizacja:** lokalny test twierdzenia (§11 p. 5, §1.5 kolumna „jak sprawdzić”) przeszedł →
  `alpha += 1` dla twierdzenia **i** dla każdego źródła, które je zgłosiło (każde z nich to
  twierdziło; propagacja do autora 1, do domeny ułamkiem — wartości w danych); nie przeszedł →
  `beta += 1`. **Samo twierdzenie bez testu nie zmienia nic** (jak w `assess.cpp`: „asserting nor
  confirming a claim supplies a calibration measurement”). Zgoda 7 źródeł to 7 rekordów
  `asserted_by`, nie 7 pomiarów.
- **Czas:** każde twierdzenie o układzie ma `validity.from/until` (per dostawca i układ); test na
  eksporcie z datą poza oknem **nie karze** źródła (format się zmienił), tylko zawęża okno. Zanik
  (decay) ku priorowi: półokres wyprowadzony z **obserwowanej** mediany odstępów między zmianami
  układu danego dostawcy, liczony dopiero od `min_changes` zmian (preset 3; do zmiany); wcześniej
  **brak zaniku** — żadnej wymyślonej stałej czasu. Gdy pomiarów jest dość (`min_labels`, istnieje:
  30), wiarygodność kluczuje się po `(źródło, dostawca, układ)`.
- **Miejsce w grafie:** węzły `source` (domena/repozytorium/autor; obszar epistemiki/proweniencji),
  `claim` (twierdzenie), `layout` (profil układu). Łącza: `source asserts claim`, `claim supports
  layout.descriptor`, `test_run confirms|refutes claim` (`port_run`/`action_run`, istnieją),
  `claim valid_during (from, until)`. Wiarygodność = własność węzła, odczytywana przez odcisk (§2.3)
  jako `r_source`.
- **Osoba widzi i nadpisuje:** przy układzie „skąd to wiemy” → lista twierdzeń z poziomem,
  pewnością, datą, źródłami, wynikiem jej własnych testów; może ustawić wiarygodność ręcznie
  (nakładka `calibration` w katalogu danych, jak „editable in the overlay” w packach), oznaczyć
  źródło jako zaufane/niezaufane, dodać własne źródło. Każda ręczna zmiana jest wpisem z autorem
  i datą, odwracalnym.
- **Co jest niepokryte:** jakość samego testu (test błędny obciąża źródło niesłusznie) — dlatego
  testy też są węzłami z proweniencją i można je unieważnić, cofając ich wpływ (odwrotność:
  `alpha −= 1`).

---

## 13. Rozbieżności między odczytem wypowiedzi a stanem wiedzy (uprzejmie i konkretnie)

Nagłówki punktów w cudzysłowie są **dosłownymi fragmentami R57** (ASR, bez poprawek), chyba że punkt
mówi „odczyt koordynatora” — wtedy to parafraza, nie słowa właściciela. Fakty o świecie w tej
sekcji są **obserwacjami z datą 2026-10-23** z poziomem źródła, nie ograniczeniami.

1. **„to zachowanie zasad podziału, jeśli dobrze mówię że to samo, to też druga kopia w html”** (dosł. R57). Dwie rzeczy nie
   trzymają się: (i) `chat.html` **nie jest podzielony** — to jedna tablica `jsonData` w jednym
   pliku [B, 7 parserów], więc „ten sam podział w HTML” nie istnieje i nie ma czego zachowywać;
   (ii) **reguły podziału JSON: brak w [A]–[D]** (niezweryfikowane; żadne źródło A–D nie podaje liczby
   rozmów ani rozmiaru na plik; oficjalny tekst mówi tylko „dla większych eksportów”). Można zachować
   **obserwowaną przynależność** rozmów do plików, nie regułę. Regułę da się **zmierzyć** na
   prawdziwym eksporcie (§1.5) — Pana albo każdego innego posiadacza takiego eksportu; dziś żadnego
   publicznego pomiaru nie znalazłem (obserwacja z datą, niezweryfikowane).
2. **„praca wyłącznie na zippie to za dużo obliczeń, kompresji, rekompresji będzie ciągnąć”** (dosł. R57). Słuszne dla rekompresji całości i dla
   bliźniaka HTML; **niesłuszne dla odczytu** (spis + inflate jednego wpisu) i dla zmiany jednego
   wpisu z surową kopią pozostałych (§6). Obawa bez pomiaru jest założeniem; projekt dokłada
   pomiar i pokazuje szacunek przed wykonaniem. Zgodnie z W9 to preset dostępności (domyślnie
   wyłączony), a koszt — ostrzeżenie; opcja nie znika.
3. **„Nie kopiujemy, rozpakowujemy.”** Rozpakowanie **jest** kopią — nieskompresowaną, zwykle
   większą od ZIP-a. To, co tryb `link` naprawdę oszczędza, to **drugą** kopię (wiersze w bazie
   i bloby) i jej niespójność ze źródłem. Uczciwe zdanie: „jedna kopia (drzewo) zamiast dwóch;
   zero kopii tylko przy odczycie przez ZIP, za cenę CPU”.
4. **„wyświetlamy w interfejsie wzorującym się na apce dostawców danego exportu”** (dosł. R57). Aplikacja dostawcy pokazuje **żywe konto**, eksport
   pokazuje **to, co dostawca zechciał wyeksportować** (dziś, wg [B]): Claude bez bajtów plików [B]/[A dla
   Team/Enterprise], Gemini bez id rozmów i często bez odpowiedzi [B], ChatGPT bez pełnego
   rozumowania. Widok może być wierny **danym**, nie aplikacji; profil musi pokazywać braki
   z powodem (`known_absent`), inaczej odwzorowanie będzie kłamać.
5. **Odczyt koordynatora: „czy klony na GitHubie reprodukują oryginalne przepływy”** (R57 pyta o
   „work flows” Claude, Codexa i in.; część B) — tu tylko o pytaniu: klon
   **interfejsu** (LibreChat, NextChat — już są jako profile) nie jest klonem **przepływu**
   (prompty systemowe, narzędzia, pamięć, tryby agentowe). Pytanie trzeba rozdzielić na trzy:
   interfejs, przepływ, zachowanie modelu; nasze profile już rozróżniają `evidence.status`
   („inspired”) od zweryfikowanej wersji. Odpowiedź „tak/nie” na jedno pytanie będzie fałszywa.
6. **„potwierdzone w sieci najlepiej w dokumentacji, fachowych źródłach”** (dosł. R57). Dla OpenAI
   dokumentacja **milczy** o formacie [A] (obserwacja z 2026-10-23, część stron niepobrana — DNS);
   dla Claude oficjalna strona nie wymienia **żadnego** pliku [A, pobrane]. Hierarchia z W8 prowadzi
   więc do poziomu B plus **pomiar lokalny** — i to jest dobra wiadomość:
   pomiar na własnym eksporcie jest jedynym źródłem poziomu „pewne” dla pytań z §1.5. Przy okazji
   ważna poprawka: dla ChatGPT istnieje prawdopodobnie **oficjalny konsument** własnego eksportu
   (przenoszenie rozmów między kontami, artykuł 9106926 — tylko streszczenie, do przeczytania
   w przeglądarce) — jeśli się potwierdzi, przemawia to za opcją (e); opcja (e) zostaje niezależnie
   od tego (R60).
7. **„węzeł główny typu pamięć podręczna, gdzie byłyby aktualnie przetwarzane i edytowane elementy z eksportu”** (dosł. R57). Jeśli pamięć podręczna
   byłaby **jedynym** miejscem edycji, to jej zwolnienie traciłoby edycje, a zapis zwrotny
   stawałby się przymusem. Dlatego trwałą postacią edycji jest dziennik łat nad źródłem (§4),
   a pamięć podręczna jest **widokiem** stanu — węzeł główny zostaje, ale nie jako magazyn.
8. **„czy wszystkie możliwości są łatwe w realizacji”** (dosł. R57). Pytanie zakłada binarność. Mechanizmy są łatwe
   do średnich; **trudne są niewiadome formatu** (tożsamość HTML, semantyka `assetsJson`, reguła
   podziału, gałęzie w eksporcie Claude), których nie rozwiąże kod, tylko pomiar na danych
   i rejestr §12. Tabela §7 odpowiada nakładem i ryzykiem, nie „tak/nie”.
9. **„chyba te same w jednym .html”** (dosł. R57). Intuicja trafna co do **istnienia** drugiej kopii [B];
   **tożsamość** niezweryfikowana — jeden parser odkodowuje encje HTML w `jsonData`, co sugeruje,
   że kopia może nie być bajtowo identyczna. Do pomiaru lokalnego (skrypt w §1.5), wynik tylko
   jako liczby.
10. **Ryzyko pętli naśladowczej w źródłach B.** Dwa narzędzia **generują** `export_manifest.json`
    o tym samym kształcie, który inne „obserwują” w eksportach; bez źródła OpenAI nie wiadomo,
    czy obserwują eksporty, czy wytwory narzędzi. Rejestr §12 ma na to pole `asserted_by`
    i obniża wagę zgody, gdy źródła nie są niezależne — Pańskie „ważone z głową” potrzebuje tego
    wprost.

---

## 14. Decyzje i odwrót

| # | Decyzja | Dlaczego | Odwrót |
|---|---|---|---|
| D1-1 | Język opisu metadanych w grafie: polski jako **preset** (etykiety dla właściciela po polsku) | decyzja projektu, nie treść W2/W3 (§1.4 p. 6) | zmiana presetu języka opisu (dane, bez kodu) |
| D2-1 | Dziedziczenie wewnątrz osi (kontener, treść), kompozycja między osiami przez `facets[]`; akcje pasują po cechach | realne eksporty nie zawsze są ZIP-ami (obserwacja z 2026-10-23, §1.5); akcje już pasują po cechach (`actions.pack`) | spłaszczyć fasety do `parent` jedną migracją danych |
| D2-2 | Identyfikator układu = hash odcisku; etykieta osobno | nazwy „2026” są osądem; hash jest stabilny | zmienić `id` na slug, etykiety zostają |
| D2-3 | Bliźniak HTML modelowany jako druga **forma** tej samej treści (`expresses`, waga = wierność) | odpowiada §6 GRAF_HUB; tożsamość staje się pomiarem, nie założeniem | zwinąć do flagi `html_twin_identical` na węźle eksportu |
| D3-1 | Preset globalny `link` (W2, odczyt R57); `copy`, `read_through_zip`, `both`, `index_only` dostępne; `repack` i `read_through_zip` = **preset dostępności** (domyślnie wył., R57 dosł.: „to tylko na wyraźne życzenie użytkownika”), opcja zawsze dostępna, koszt = ostrzeżenie | R57; R58 (preset, nie reguła; koszt nie usuwa opcji) | zmienić preset w `import.pack`/nakładce — jedno pole |
| D3-2 | Węzły grafu powstają dla **każdego** trybu, także `copy` | graf jako hub; G2 | nie ma powodu cofać; można wyłączyć obszar w `entry.pack` |
| D3-3 | `chat.html` w trybie `link`: podlinkowany + poradnik „porównaj/importuj też”; `copy` zachowuje dzisiejsze pominięcie z wpisem | R58 (preset, nie usunięcie); niezweryfikowana tożsamość | zmienić preset `html_twin` |
| D3-4 | Pobieranie aktywów Claude z `preview_url`: opcja z presetem `ask` | wywołanie sieciowe do serwera dostawcy | preset `never`/`auto` w danych |
| D4-1 | Pamięć podręczna = węzeł-widok; trwałość = `messages` + dziennik łat (RFC 6902); bez drugiej tabeli | nie tracić edycji przy zwolnieniu; X-Port ma już tę semantykę | tabela `cache_entries` z migracją kopiującą |
| D4-2 | Zwalnianie: preset `manual`, żadnych progów w kodzie | R25/R58: nie wymyślać liczb | presety `lru(...)` z wartościami osoby |
| D5-1 | Zapis zwrotny zachowuje **obserwowaną przynależność** do shardu; nowa rozmowa wg presetu `last_shard` | reguła podziału nieznana [A–D] | gdy pomiar da regułę: preset `by_observed_count/bytes` |
| D5-2 | Dwie semantyki `assetsJson` jako konkurencyjne hipotezy w danych, wybór po lokalnym teście | źródła B się różnią | usunąć przegraną po pomiarach |
| D5-3 | Przepakowanie: surowa kopia nietkniętych wpisów, porządek źródła, poziom kompresji ze źródła, zip64 automatycznie | miniz to umie (`miniz.h:1384,1069,1253`) | presety w akcji `repack` |
| D5-4 | (e) jako pełnoprawny tryb z poradnikiem o odbiorcach, nie „eksport na życzenie” | R58, R60; dla ChatGPT odbiorca prawdopodobnie istnieje [A, streszczenie]; dla innych brak znanego (obserwacja z datą) | osoba ustawia preset (e) jednym polem w `import.pack` (np. na „tylko na żądanie”); projekt sam go nie degraduje (R58) |
| D5-5..8 | Własny eksport Loom (`export.loom`) jako czytelny, opisany, wersjonowany format: schemat+wersja, własny odcisk i rodowód klas, manifest, opis z danych (§5.1) | R60: nie wiadomo, kto będzie czytał; ten sam mechanizm co dla cudzych eksportów | nowa wersja schematu obok starej; okrojenie manifestu presetem z ostrzeżeniem; opis zostaje lokalny |
| D9-1 | Migracja tworzy węzły z `SourceRecord`, nie rusza wierszy; podniesienie do `link` przez bramkę | „nic po cichu” | usunąć węzły migracji (są oznaczone `derived_from: migration`) |
| D11-2 | Kaskada źródeł: sięgnięcie po słabsze [D], gdy kroki 1–2 milczą, to **preset** (§11 p. 3), nie reguła właściciela | W8: „dopiero z braku wiarygodnych”; próg „milczą” jest naszym osądem | zmiana presetu albo ręczne włączenie kroku 3 wprost |
| D11-1 | Agent formatów: preset `ask`; sanitizer deskryptorów z testem własności; adopcja do nakładki danych, nie do packów | prywatność; ROM nietykalny | preset `auto`/`never`; włączenie propozycji PR z danymi |
| D12-1 | Rejestr = rozszerzenie `calibration.json` (klucze `source.*`, `author:*`, `claim:*`, `validity`, `decay`, `update`), ta sama matematyka Beta/noisy-OR | „extend, do not reinvent” | osobny plik `calibration.d/*.json` ładowany tym samym kodem |
| D12-2 | Zanik wyprowadzony z obserwowanych zmian układu (od `min_changes`), wcześniej brak | żadnej wymyślonej stałej czasu | ustawić `decay.rule: fixed_half_life` z wartością osoby |
| D12-3 | Samo twierdzenie źródła nie zmienia wiarygodności; zmienia ją tylko test | precedens `assess.cpp` | `update.assert_only.alpha > 0` w danych |
| D10-1 | Kolejność pakietów P1→P10; P1–P3 dają rzeczy do dotknięcia | właściciel sprawdza na telefonie | koordynator przestawia kolejność (preset koordynatora, bez zmiany kodu) |

Pytania do właściciela (tylko tak/nie, zgodnie z zasadą): **żadnych** na tym etapie. Pieniądze
poza budżetem nie występują (agent §11 ma preset `ask` z szacunkiem), nowy dostawca prywatnych
danych nie występuje (jedyne wywołanie do dostawcy — `preview_url` Claude — ma preset `ask`),
kierunek nie sprzeciwia się wymaganiom.

---

## Aneks A. Co sprawdzono w repo (ścieżki i linie; [C])

- `loom/src/import/importer_zip.cpp:1,36` — ZIP do `fsutil::TempDir`, usuwany po imporcie.
- `loom/src/import/importer_core.cpp:203-245` — blob całego źródła (`put_file`), proweniencja.
- `loom/src/import/export_openai.cpp:989-993` — `chat.html` → `Skip{"offline_viewer_skipped"}`.
- `loom/src/import/export_anthropic.cpp:305-329` — załączniki jako metadane, `resolved=false`.
- `loom/src/import/export_internal.h:326-328` — `is_conversation_file` (`> 18`).
- `loom/include/loom/provenance.h:69-80, 86-93` — `SourceRecord`, rekord `pv_`.
- `loom/data/graph/classes.pack` — 94 klasy; `file` :954, `archive_item` :944; relacje `expresses`
  :569, `derived_from` :589, `part_of` :625; cechy dziedziczone po `parent`; grupy m.in.
  `irreversible`, `private_data`, `off_device`.
- `loom/data/runtime/actions.pack` — akcje pasują po `requires` (cechy), flaga `irreversible`.
- `loom/data/presets/import.pack` — `platforms`: 4 wpisy (`chatgpt` z `shard_prefix`).
- `loom/data/policy/calibration.json`, `loom/src/resolve/assess.cpp:85-114` — Beta, noisy-OR.
- `loom/third_party/miniz/miniz.h` — `MZ_VERSION "11.0.2"`; `mz_zip_writer_add_from_zip_reader`
  :1384; `mz_zip_writer_init_from_reader` :1347; `MZ_NO_COMPRESSION` :270; `mz_zip_is_zip64`
  :1253; `MZ_ZIP_FLAG_WRITE_ZIP64` :1072; `MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY` :1069;
  `extract_to_callback` :1275, `extract_iter_new` :1279; API zapisu **włączone** (wyłączałoby je
  tylko `MINIZ_NO_DEFLATE_APIS`, :158-160). Zachowanie na realnych archiwach **niezweryfikowane**
  (brak realnego ZIP-a w testach; fixture'y syntetyczne).
- `loom/tools/extract/data/formats/*.json` — 14 formatów; `sniff.any_of`; **brak** pól wersji/układu.
- `loom/tools/extract/cut/preserve.py` — kopia 1:1 z własnych bajtów źródła, wpisy ZIP z metadanymi.
- `loom/web/src/profiles/data/*.json`, `docs/APPLICATION_PROFILES.md` — `loom.application_profile/1`.
- `docs/architecture/SLOWA_WLASCICIELA_2026-10-16.md:1486-1491, 1715-1724, 2174`.

Nie czytano: `docs/exports/*`, `eval/real-holdout-key` (prywatne). Do sieci nie wyszło nic
z tej sesji (badanie formatów pochodzi z osobnego pakietu, z własną notą o prywatności).

## Aneks B. Lista „niezweryfikowane” (do pomiaru lokalnego przez właściciela, nic nie wychodzi)

1. Tożsamość `jsonData` ↔ `conversations*.json` (zbiory id; kanoniczny JSON per id).
2. Reguła podziału na shardy (liczba/bajty/porządek; rozłączność id).
3. Semantyka kluczy `assetsJson` (wskaźniki vs id wiadomości).
4. Obecność i nazwa manifestu (`export_manifest.json`), `user_settings.json`,
   `conversation_asset_file_names.json`, `library_files.json`, `.dat` w Pana eksporcie.
5. Czy eksport Claude.ai nosi gałęzie (`parent_message_uuid` ≠ poprzednik); czy ma `design_chats/`.
6. Czy `memories.json` jest u wszystkich kont.
7. Treść artykułów `help.openai.com` 7260999, 20001279, 9106926 (niepobrane; tylko streszczenia).
8. Zachowanie miniz na realnym archiwum (mtime, atrybuty, zip64) — po pakiecie P6.
9. Ścieżka profili interfejsu na Android (plan M1).
