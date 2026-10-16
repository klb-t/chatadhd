# Ekstrakt z archiwów rozmów — narzędzie P38 (2026-10-16)

Podrzędny wobec R1–R56 (`OWNER_REQUIREMENTS_2026-09-26.md`), `GRAF_HUB_KOD_SILNIK_2026-10-13.md`
i `LOOM_CONCEPTUAL_MODEL.md`. Buduje na tym, co już jest: importer bezstratny
(`loom/src/import`, nagłówek `loom/include/loom/importer.h`, opis formatów
`docs/exports/OPENAI_ANTHROPIC_EXPORT_FORMATS.md`), potok archiwum (`loom/src/archive`,
`loom/include/loom/archive.h`, przechodzenie drzew w `ingest_parse.cpp`), importer Pythona
(`engine/importer.py`), selektor trójpoziomowy (`core/selector.py`), tablica platform jako dane
(`loom/include/loom/platforms.h`, `data/presets/import.pack`), przewodnik
`docs/OWNER_REAL_EXPORT_GUIDE.md` i generatory fixture'ów (`loom/tools/gen_export_fixtures.py`,
`gen_import_fixtures.py`). Nic z tego nie jest tu powielane; narzędzie dokłada to, czego brakuje
(wybór, wycięcie, pakiet, raport) i nowe źródła (Gemini, Grok, sesje Claude Code).

Druga redakcja (2026-10-16, po południu) dokłada to, o co właściciel dopytał, i to, co z filozofii
wynika: **jeden silnik i trzy twarze** (wiersz poleceń, komander w terminalu, lokalne API),
**przepisy jako dane**, **globalną oś czasu**, **komander eksportów** (X-Port Commander)
z zaznaczaniem, filtrowaniem i edycją seryjną, **różnicę** „co nowego od ostatniego eksportu”,
**pakiet przekazania** do importera aplikacji i decyzję o prywatności (DIP26) w §2. Nic
z pierwszej redakcji nie zostało usunięte; §8 ma teraz 36 wymagań (dawne 1–28 są w nich
zawarte, dawne rozszerzenia 29–56 mają numery 37–64).

## 0. Słowa właściciela (2026-10-16, wiernie)

> „w ogóle tak sobie myślę jak będzie najtaniej, żeby pełny solidny ekstrakt z archiwów od
> otwartych i ludzkich, z Googla zresztą też i gr oka, bo z nimi też czasem gadałem (btw. nie wiesz
> czy, a jeśli tak to z jaką dokładnością w archiwach są przebiegi agentowe?) na temat ChataADHD
> i jeszcze kilka innych małych niedokończonych wcześniejszych projektów, które z założenia miały
> być zintegrowane w czata (agent, analizator archiwów - hahaha, rekurencja wchodzi ;D)? jeszcze
> takie ekstrakty dotyczące innych tematów będę potrzebował, więc dobrze to zrobić jako osobny
> skrypcik nawet opusowi To chyba można zlecić jako - nie wiem, sam Zadecyduj czy agent czy inna
> sesja czy jak. tylko mu dobrze określ Według mojej filozofii czyli pełna funkcjonalność
> i konfigurowalność przy minimalnej wymaganej konfiguracji, Key Wordy regeksy wektory
> wykorzystanie modeli według różnych Flow wyszukiwanie zaznaczanie według maski według filtra
> słownie opisanego... wycinanie zachowujące format oryginalnego archiwum łącznie z odwołaniami
> do załączników które tylko wykorzystywane będą w paczce... albo z dowolną przebudową (chociażby
> profil: tylko tytuły rozmów załączniki rozgałęzienia rozumowania narzędzia, wyciągnięte
> z zagnieżdżenia i uproszczone Do prostego rozmowa->wiadomość), timestampy...”

> „jeśli z Filozofii i z przeglądu uwag rozszerzysz tę listę 7-10 krotnie, to będzie funkcjonalne”

**2026-10-16, ok. 15:40 UTC** (pierwszy akapit dotyczy M1 i jest odpowiedziany w planie M1, nie
tutaj; drugi dotyczy tego narzędzia):

> „Cieszę się że wyłapałeś parę rzeczy których teraz nie wspominałem, A były istotne. to rodzi
> solidne nadzieje na to, że m1 mnie ucieszy. kiedy się go można spodziewać?
> I jak tam analizator archiwów? bo potrzebuję go do innych rzeczy również żeby w ogóle nie
> mówiłem ci domyśliłeś się że się przyda na przykład sortowanie globalne wiadomości po
> timestampach, eksplorator eksportów (X-Port Commander ;) umożliwiający poza podstawowymi
> operacjami na drzewach i innego rodzaju strukturach zaawansowane zaznaczanie filtrowanie
> edycja seryjna wartości, nazw, struktury.....?”

Obowiązuje też stała zasada właściciela (CLAUDE.md, „Lists are examples”, 2026-10-16): każda
lista, którą podaje, jest ziarnem, nie zakresem — wypełnić ją tym, co już jest w aplikacji
i tym, co z filozofii wynika, i **powiedzieć, co dodano** (§0.1).

Przykłady właściciela są **przypadkami** mechanizmów poniżej, nie ich zakresem (R53). „Otwarci
i ludzcy” = eksporty OpenAI/ChatGPT i Anthropic/Claude; „Google” = Gemini (Takeout);
„Grok” = xAI (grok.com i archiwum X). Rekurencja jest zamierzona: narzędzie jest pierwszym
zastosowaniem analizatora archiwów, który sam ma z tych archiwów zostać odtworzony (R2).

### 0.1 Co dodano poza listami właściciela i dlaczego

Listy właściciela: (12:01) słowa kluczowe, regexy, wektory, modele według przepływów,
wyszukiwanie, zaznaczanie maską i filtrem opisanym słownie, wycinanie w formacie źródła
z załącznikami tylko użytymi, dowolna przebudowa (profil), timestampy; (15:40) sortowanie
globalne po timestampach, eksplorator eksportów z operacjami na drzewach i innych strukturach,
zaawansowane zaznaczanie, filtrowanie, edycja seryjna wartości, nazw i struktury. Wszystko to
jest w §8. Poza tym dodano (numery = wiersze §8):

| Dodane | Dlaczego |
|---|---|
| **Inwentarz** jako osobny raport i „czego format nie zawiera” (4) | bez pomiaru każdy szacunek kosztu i każda odpowiedź o przebiegach agentowych byłaby zgadywaniem (R5) |
| wiele archiwów naraz, wersje tego samego konta, duplikaty, **różnica „co nowego od ostatniego eksportu”** (2) | właściciel eksportuje cyklicznie; bez tego drugi eksport = drugi pełny przebieg i druga opłata |
| strumieniowanie GB, wznawianie (3) | eksporty ChatGPT bywają wielogigabajtowe; przerwany przebieg nie może zaczynać od zera |
| załączniki: brakujące flagowane, przeglądarka, tekst z OCR/ASR opcjonalnie (5) | „tylko wykorzystywane w paczce” wymaga wiedzy, które są, a których brak |
| oś czasu: luki, serie, kubełki, strefa z zachowaniem oryginału, **kontynuacje między usługami** (6–7) | sortowanie globalne ma sens dopiero, gdy widać, co z niego wynika; rozmowa zaczęta w ChatGPT i dokończona w Claude to jeden wątek właściciela |
| ziarna, sąsiedztwo, pewność i źródło decyzji per poziom, kanał-cień, próba na sucho, `wyjasnij` (12–13, 16) | wybór ma być sprawdzalny, nie „model tak powiedział” (R25, R27) |
| odkrywanie „innych małych projektów” klastrami (14) | właściciel nie pamięta wszystkich; narzędzie ma je zaproponować |
| uczenie się z poprawek, zakładki, etykiety, zapisane widoki (17) | żeby płacić — pieniędzmi i uwagą — raz |
| **jeden silnik, trzy twarze** i **przepisy jako dane**; komander nagrywa przepis (33) | ten sam przebieg zdaniem, plikiem, ekranem, API, a później z czatu aplikacji; klikanie też jest odtwarzalne |
| edycja seryjna z **podglądem różnic, cofaniem, dziennikiem**, tylko na kopii; **autozapis** (21, 24) | edycja bez cofania i dziennika niszczyłaby jedyne źródło (R19, R44); TUI nie może zgubić godziny pracy |
| **jeden język zaznaczania** dla CLI/TUI/API i wzorce strukturalne („≥ 2 wywołania narzędzi”, „ślepa gałąź”) (19) | „zaawansowane zaznaczanie” musi być wyrażalne, żeby było zapisywalne i powtarzalne |
| klawisze, układ, kolumny, teksty jako dane pl/en (22) | R42: zdania i klawisze w danych, nie w kodzie |
| redakcja z raportem, budżet rozmiaru, mapa prywatności (28, 46, 41) | pakiety będą trafiać do modeli i do innych ludzi |
| manifest, odtwarzalność, determinizm, fixture'y syntetyczne, strażnik ścieżek (30, 34) | wynik ma być dowodem, nie wrażeniem; prywatność właściciela (DIP26) |
| **pakiet przekazania** do importera aplikacji (29) | cel ekstraktu to m.in. zasilić ChatADHD/Loom — rekurencja z §0 |
| port jądra, akcja czatu, obserwacja katalogu (33, 39) | ta sama operacja ma być dostępna z czatu, nie tylko z terminala |
| własne czytniki, operacje edycji, hooki na funkcje użytkownika (35) | rozszerzalność danymi, bez zmiany rdzenia |
| raport po polsku w słowach właściciela (36) | bo on go czyta |

## 1. W jednym zdaniu

Osobny skrypt bez budowania — `python3 loom/tools/extract …` — to **jeden silnik o trzech
twarzach** (wiersz poleceń, komander w terminalu, lokalne API), który czyta archiwa i **nigdy ich nie
zmienia**, najpierw **mierzy**, co w nich jest (inwentarz), układa wszystko na **jednej osi czasu**
niezależnie od źródła, **wybiera kaskadą** (maski → słowa i regexy → wektory → tani model → mocny
model; poziomy z modelem tylko po jawnym włączeniu, z kosztem pokazanym przed uruchomieniem) albo
ręcznie w komanderze, pozwala **edytować seryjnie** kopię (wartości, nazwy, strukturę — z podglądem
różnic i cofaniem), **wycina pakiet** w formacie źródłowym 1:1 albo według profilu przebudowy,
kopiuje do pakietu **tylko użyte załączniki**, zapisuje **manifest i przepis**, z których pakiet da się
odtworzyć, **pakiet przekazania** gotowy do importu w aplikacji jednym krokiem, i **raport po polsku**.
Wszystko, co użytkownik zrobi, jest **przepisem** (danymi): ten sam przepis działa z CLI, z komandera
i przez API. Minimalna konfiguracja: wskazać archiwum i nazwać temat. Wszystko inne to presety
w plikach danych, które użytkownik nadpisuje własnymi.

### 1.1 „Jak będzie najtaniej” — odpowiedź

Najtańsza droga to kaskada, w której pieniądze wydaje się dopiero na to, czego darmowe poziomy nie
rozstrzygnęły:

1. `inwentarz` — zero kosztu, zero sieci. Daje liczby: ile rozmów, wiadomości, znaków, tokenów
   (szacunek lokalny), jakie rodzaje bloków, narzędzia, gałęzie, załączniki, okresy. Bez tego każdy
   szacunek kosztu byłby zgadywaniem.
2. `szukaj --na-sucho` z samymi słowami/regexami/maskami i wektorami (poziomy 0–2, lokalnie).
   Wynik: trzy pasma — *wchodzi*, *nie wchodzi*, *niejednoznaczne* — z licznikami i powodami.
   Na archiwum o jednym wyraźnym temacie (ChatADHD, Loom, nazwy plików repo) pasmo
   niejednoznaczne jest zwykle małe; raport mówi, ile dokładnie.
3. Tani model tylko na paśmie niejednoznacznym (preset: okno 6 wiadomości wokół trafienia, nie
   cała rozmowa). Przed uruchomieniem: liczba wywołań × tokeny × cena z `models.json` =
   szacunek; „nieznany”, gdy ceny brak — nigdy zero.
4. Mocny model tylko na tym, co tani model zostawił jako niejednoznaczne (preset: wyłączony).
5. Przegląd listy trafień ręcznie (`zaznacz`) — decyzje zapisują się i obowiązują w następnych
   przebiegach, więc płaci się raz.
6. `wytnij` — zero kosztu.

Ekstrakt „pełny i solidny” bierze się nie z drogiego modelu, lecz z **sumy metod** (R25, R27):
leksykalny i wektorowy kanał pracują równolegle, a różnica między nimi (co znalazły słowa, a nie
znalazły wektory, i odwrotnie) jest osobną listą do przejrzenia. Model służy do rozstrzygania,
nie do szukania.

### 1.2 „Czy i z jaką dokładnością w archiwach są przebiegi agentowe?” — odpowiedź

Krótko: **częściowo, różnie u każdego dostawcy, a dokładność trzeba zmierzyć na własnym
eksporcie** — stąd inwentarz liczy je osobno (wiadomości narzędzi, bloki `tool_use`/`tool_result`,
bloki rozumowania i czy są pełne, czy streszczone). Z opisów formatów i syntetycznych fixture'ów
(§7) wynika:

| Źródło | Wywołania narzędzi | Wyniki narzędzi | Rozumowanie | Czego brak |
|---|---|---|---|---|
| ChatGPT (eksport danych) | tak — wiadomości `author.role: tool` (nazwa narzędzia w `author.name`), `recipient`, `content_type: code` | częściowo — `execution_output`, `tether_browsing_display`, `tether_quote`; strony www tylko jako cytaty/skróty | **tylko streszczenia** (`thoughts`, `reasoning_recap`), nie pełny łańcuch | pełne rozumowanie, prompt systemowy, pełne treści przeglądanych stron, stan Agent mode/Operator poza tym, co zapisano jako wiadomości |
| Claude.ai (eksport danych) | tak — bloki `tool_use` w `chat_messages[].content` | tak — bloki `tool_result` | bloki `thinking` — obecność i długość zmienna (bywają streszczone/puste); inwentarz liczy znaki rozumowania względem znaków tekstu | sesje Claude Code w chmurze (claude.ai/code) i Cowork — **nie ma ich w eksporcie** |
| Claude Code lokalnie (`~/.claude/projects/*/*.jsonl`) | **pełne** — `tool_use` z wejściem, `toolUseResult` | pełne | `thinking` pełne, gdy nie zredagowane | sesje, których transkryptu nie ma na dysku; wiersze wewnętrzne (`queue-operation`, `attachment`, …) trafiają do kubełka „nieznane/wewnętrzne”, nie giną |
| Gemini (Takeout „My Activity / Gemini Apps”) | nie | nie | nie | brak identyfikatorów rozmów (rekonstrukcja po czasie, oznaczona jako wywnioskowana), brak gałęzi; Deep Research tylko jako gotowy tekst, jeśli zapisano go w Dokumentach |
| Grok (grok.com, archiwum X) | prawdopodobnie nie (do zmierzenia) | nie | nie | kształt do zmierzenia na pierwszym prawdziwym pliku; opis formatu to plik danych, więc poprawka = plik, nie kod |

Inwentarz mówi o konkretnym eksporcie: „rozmów z wiadomościami narzędzi: 37; bloków rozumowania:
412, w tym pełnych (≥ 200 znaków): 9; eksport nie zawiera: …”. Tego nie zgadujemy — mierzymy.

## 2. Granice

- **Nie zmienia źródeł.** Czyta tylko. Wszystko, co pisze, idzie do katalogu pakietu.
- **Zero sieci domyślnie.** Żaden bajt nie wychodzi, dopóki użytkownik nie włączy kroku z modelem
  (`--model` albo `flows.model.enabled: true` w zadaniu). Domyślna odpowiedź na pytanie
  o uruchomienie kroku płatnego to „nie”.
- **Model tylko przez znaną drogę.** Przez `loom-server` (`/api/chat`, podgląd kosztu
  `/api/chat/usage-preview`), gdy jest skonfigurowany, albo przez jawnie podaną konfigurację
  dostawcy (OpenAI-zgodne `chat/completions`, klucz z `secrets.json` aplikacji — czytany dopiero po
  włączeniu modelu). Każde wywołanie zapisane w dzienniku z kosztem.
- **Prywatność (DIP26, właściciel 2026-10-12).** Prawdziwe eksporty właściciela są dozwolone
  i **preferowane** w lokalnych przebiegach odbiorczych tam, gdzie jest wybór; korpusy syntetyczne
  pozostają kontrolą. Stąd: testy i fixture'y w repo są wyłącznie syntetyczne
  (`loom/tools/gen_export_fixtures.py`, `gen_import_fixtures.py`, nowy `gen_extract_fixtures.py`);
  `docs/exports`, `eval/real-holdout-key` ani żaden prawdziwy eksport nigdy nie trafia do repo,
  testów, dzienników, raportów ani transkryptów agentów; agent może uruchomić narzędzie na
  prawdziwych eksportach **tylko po to, by podać liczby zbiorcze** (nigdy treści, tytułów, nazw
  plików, identyfikatorów) — służy do tego tryb `--zbiorczo`, w którym raport, dziennik i wyjście
  `--json` zawierają wyłącznie liczby i nazwy formatów; wysłanie danych właściciela do nowego
  dostawcy pozostaje pytaniem tak/nie do właściciela. Strażnik w testach sprawdza ścieżki,
  a `--zbiorczo` jest testowany na fixture'ach (żaden tytuł ani fragment tekstu nie przechodzi).
- **Nic nie znika po cichu.** Nieznany rodzaj bloku, członek archiwum, brakujący załącznik,
  nieczytelny plik — każde ma wpis w inwentarzu, raporcie i manifeście, z przykładem.
- **Żadnych wymyślonych limitów.** Każda liczba, która ogranicza (rozmiar okna, liczba kandydatów,
  próg), jest presetem w danych; `0` = bez cięcia.
- **Bez budowania.** Python 3.11+ i biblioteka standardowa; `sentence-transformers`/`scikit-learn`
  opcjonalnie jak w `core/selector.py`; `libloom.so` opcjonalnie jako drugi czytnik do porównań.
- **Archiwum to dane, nie kod.** Narzędzie nie wykonuje nic z archiwum, nie zmienia katalogu
  roboczego na katalog archiwum, nie importuje modułów spoza repo; w automatyce uruchamiać
  z `python3 -I`.
- **Jeden silnik.** CLI, komander i API wołają te same operacje (§3.11); żadna twarz nie ma
  logiki, której nie ma w silniku; co da się zrobić na ekranie, da się zapisać jako przepis
  i uruchomić bez ekranu — i odwrotnie.
- **Edycja tylko na kopii.** Edycja seryjna działa na drzewie pakietu, nigdy na migawce źródeł;
  każda operacja ma podgląd różnic, wpis w dzienniku operacji i odwrotność (cofnięcie); nic nie
  znika bez wpisu (§3.15).

## 3. Architektura

### 3.1 Potok

Każdy etap jest funkcją z jawnymi wejściami i wyjściami; wynik etapu ma klucz pamięci podręcznej
= sha256(parametry etapu + hashe wejść). Przebieg przerwany zostawia `state.json` (etap, pozycja)
i wznawia się od tego miejsca. Bez modelu każdy artefakt jest czystą funkcją bajtów wejścia (jak
w `loom/archive.h`: bez losowych id, bez zegara ściennego w treści, stabilne porządki).

| Etap | Wejście | Wyjście | Uwagi |
|---|---|---|---|
| `scan` | ścieżki (pliki, katalogi, zip) | lista kontenerów i członków z hashami, rozmiarami, datami; format rozpoznany **z treści** | zip czytany strumieniowo (`ZipFile.open`), bez rozpakowywania na dysk; zip w zipie rekurencyjnie |
| `read` | członek + opis formatu (§5.1) | strumień **jednostek** modelu wspólnego (§3.2) z lokalizatorem i surowym obiektem | czytniki (§3.4); duże tablice JSON strumieniowo; JSONL wiersz po wierszu |
| `merge` | jednostki z wielu archiwów | jednostki z tożsamością, wersjami, duplikatami | tożsamość: (usługa, id zewnętrzne) → hash treści; najnowsza wersja aktywna, starsze w `versions[]` |
| `inventory` | jednostki | `inventory.json` + sekcja raportu | mierzy, nie zgaduje (§5.7) |
| `select` | jednostki + kryteria + kaskada + decyzje ręczne | decyzja per ziarno z pewnością, poziomem i powodem (`explain.jsonl`) | §6 |
| `expand` | decyzje + preset sąsiedztwa | decyzje rozszerzone (okno, gałąź, rozmowa, rozmowy powiązane) | §6.5 |
| `cut` | jednostki wybrane + profil | drzewo pakietu w pamięci/na dysku | `preserve` 1:1 albo profile przebudowy (§5.5) |
| `write` | drzewo pakietu | pliki, załączniki, `manifest.json`, `RAPORT.md`, `log.jsonl` | nazwy i kodowanie przewidywalne (§5.8) |
| `timeline` | jednostki po `merge` | `timeline.jsonl`, kubełki, luki, serie, kontynuacje między usługami | jedna oś czasu dla wszystkich źródeł; klucz sortowania i rozstrzyganie remisów jako presety (§3.13) |
| `diff` | jednostki z dwóch zbiorów (stary/nowy eksport lub manifest) | `diff.json`: nowe, zmienione, usunięte, bez zmian; na poziomie rozmów i wiadomości | „co nowego od ostatniego eksportu” (§3.16); może zasilić `select` (`mode: only_new`) |
| `edit` | drzewo pakietu + kroki edycji z przepisu | drzewo po edycji, `oplog.jsonl`, podgląd różnic | tylko na kopii; każda operacja odwracalna (§3.15) |
| `handoff` | drzewo pakietu | `handoff/` — katalog importowalny jedną operacją przez `engine.importer` i `loom import` | §3.17 |

Polecenia CLI (§4) składają etapy: `inwentarz` = scan→read→merge→inventory;
`szukaj` = … →select→expand (bez `write`, chyba że `--zapisz-decyzje`); `wytnij` = całość;
`os-czasu` = …→timeline; `roznica` = scan→read→merge×2→diff. Etapy są **operacjami silnika**
(§3.11): `komander` wykonuje je interaktywnie i dopisuje każdy krok do przepisu; `przepis uruchom`
wykonuje kroki przepisu po kolei; `serwuj` udostępnia te same operacje przez HTTP.

### 3.2 Model wspólny (jednostki)

Jedna reprezentacja pośrednia dla wszystkich źródeł, nadzbiór tego, co dziś przechodzi przez
`walk_tree` w `ingest_parse.cpp` i zgodna polami z `loom.export.v1` (metadane importera
bezstratnego), żeby port do jądra (§3.10) był przepisaniem, nie przeprojektowaniem. Surowy obiekt
źródłowy jest zawsze dostępny obok (`raw_pointer`, a na żądanie `raw`), więc profil `preserve`
nigdy nie zależy od interpretacji.

```json
{"schema": "loom.extract.unit/1", "kind": "conversation",
 "uid": "sha256:3f9c…", "ext_id": "68a1…", "title": "Pamięć hierarchiczna w ChatADHD",
 "created": "2025-10-09T08:53:20Z", "updated": "2025-10-09T09:40:02Z",
 "account": {"service": "openai", "product": "chatgpt", "id": "user-…", "label": null},
 "source": {"archive": "chatgpt_2026-10-01.zip", "sha256": "…", "member": "conversations.json",
            "format": "openai.chatgpt.export", "locator": {"json_pointer": "/17"}},
 "tree": {"roots": ["n0"], "current_leaf": "n41", "forks": [{"node": "n12", "alternatives": 2}]},
 "messages": [
   {"id": "n13", "parent": "n12", "role": "assistant", "author": {"name": null},
    "created": "2025-10-09T08:55:01Z", "branch": "0/1", "on_current_path": true,
    "blocks": [
      {"kind": "thinking", "text": "…", "complete": false, "native": "reasoning_recap"},
      {"kind": "text", "text": "…"},
      {"kind": "tool_use", "name": "python", "input": {"code": "…"}, "native": "code"},
      {"kind": "attachment", "ref": "file-abc", "name": "schemat.png", "mime": "image/png",
       "resolved": "user-…/file-abc-schemat.png", "status": "resolved"},
      {"kind": "unknown", "native": "future_widget", "example": {"content_type": "future_widget"}}
    ],
    "raw_pointer": "/17/mapping/n13"}],
 "attachments": [{"ref": "file-abc", "status": "resolved", "member": "user-…/file-abc-schemat.png",
                  "size": 3412, "sha256": "…"}],
 "unknown": [{"where": "/17/metadata/x_future", "example": {"…": "…"}}],
 "versions": [{"source": {"archive": "chatgpt_2026-06-12.zip", "…": "…"}, "updated": "…"}],
 "stats": {"messages": 42, "chars": 58120, "tokens_est": 14500, "tools": {"python": 3},
           "thinking_chars": 1200, "branches": 2, "attachments": 1, "lang": {"pl": 0.9, "en": 0.1}}}
```

Rodzaje bloków (`data/block_kinds.json`, lista otwarta): `text`, `thinking` (z `complete` i
`redacted`), `tool_use`, `tool_result`, `code`, `execution_output`, `attachment` (obraz, dźwięk,
wideo, dokument — `mime` mówi który), `citation`, `search_result`, `context` (instrukcje użytkownika
i modelu), `system`, `error`, `unknown` (z przykładem). Rodzaje jednostek: `conversation`,
`project`, `memory`, `account`, `feedback`, `shared_link`, `artifact`, `member` (plik archiwum bez
interpretacji), `session` (Claude Code), `unknown`.

### 3.3 Moduły i katalogi

```
loom/tools/extract/
  __main__.py            # python3 loom/tools/extract …  (katalog z __main__.py; dodaje korzeń repo do sys.path)
  cli.py                 # czasowniki, zdanie, plik zadania, kody wyjścia
  i18n.py                # mała tablica pl/en (data/i18n/*.json), wybór języka
  paths.py               # katalog danych aplikacji przez engine/paths.py (nakładki użytkownika)
  model.py               # dataclasses modelu wspólnego, hashowanie, porządki
  sources/               # scan: kontenery (plik, katalog, zip, zip-w-zipie, sqlite), rozpoznanie z treści
  readers/
    base.py              # kontrakt czytnika; kubełek „nieznane”
    tree_walk.py         # bezstratne przejście drzew (mapping / chat_messages) — semantyka walk_tree z ingest_parse.cpp
    chatgpt.py claude.py # eksporty dostawców (szardy, członkowie, załączniki, encje nie-rozmowy)
    gemini.py grok.py    # Takeout (HTML + JSON), grok.com i archiwum X
    claude_code.py       # ~/.claude/projects/*.jsonl (sesje, sidechainy, podsumowania, kompaktowanie)
    loom_graph.py        # nasz pakiet grafu loom.graph_packet/1 i chatadhd.db (metadata.export.raw)
    textlike.py          # html/mht/markdown/text/jsonl/sqlite PRZEZ engine.importer (podklasa przechwytująca)
    loom_backend.py      # opcjonalnie: libloom.so (loom_import_file_ex) jako drugi czytnik do porównań
  merge.py               # tożsamość, duplikaty, wersje
  inventory.py           # pomiary (§5.7)
  criteria.py            # ładowanie/łączenie kryteriów, maski
  lexical.py             # słowa z fleksją PL i składaniem znaków, regexy, „dlaczego pasuje”
  vectors.py             # core.selector.SelectorEngine + pamięć podręczna osadzeń
  flows.py               # przepływy z modelem (§5.3), trasa, szacunek, strażnik ×10, dziennik
  cascade.py             # kaskada poziomów (§6)
  cluster.py             # odkrywanie tematów (§6.6)
  expand.py              # sąsiedztwo
  decisions.py           # decyzje ręczne, uczenie się z poprawek
  cut/preserve.py        # format źródłowy 1:1
  cut/profiles.py        # profile przebudowy jako dane
  outputs/               # json, jsonl, markdown, csv, graph_packet, zip
  redact.py              # maskowanie z raportem
  timestamps.py          # normalizacja, strefa, przesunięcie, anonimizacja
  manifest.py report.py cost.py cache.py sentence.py
  engine.py              # jeden silnik: stan (migawka jednostek + drzewo pakietu + zaznaczenie), rejestr operacji, zdarzenia postępu
  ops.py                 # operacje jako funkcje (nazwa, parametry, stan) → wynik; każda = krok przepisu; rejestr z data/ops.json
  recipe.py              # przepis: ładowanie, schemat, nagrywanie, odtwarzanie, normalizacja (determinizm)
  select_expr.py         # język zaznaczania — jedna gramatyka dla CLI/TUI/API, wzorce strukturalne, czas (§6.9–6.11)
  timeline.py            # oś czasu globalna: klucze, remisy, kubełki, luki, serie, kontynuacje (§3.13)
  diff.py                # różnica między eksportami / manifestami (§3.16)
  edits/
    base.py              # kontrakt operacji edycji: preview → różnica, apply, invert; rejestr z data/edits/*.json
    values.py keys.py structure.py attachments.py roles.py time.py   # operacje wbudowane (§5.12)
  oplog.py               # dziennik operacji, cofnij/ponów, migawki zakresu
  handoff.py             # pakiet przekazania do importera aplikacji (§3.17)
  commander/
    state.py             # CommanderState: panele, kursor, zaznaczenie, tryby, filtr żywy — bez curses, sterowany listą klawiszy
    actions.py           # akcje (id → operacja silnika lub zmiana stanu) z data/commander/actions.json
    keymap.py            # klawisze → akcje z data/commander/keymap.json (profile fkeys, vim), kolizje, pomoc
    view_curses.py       # renderer curses — jedyny moduł importujący curses, ładowany leniwie
    driver.py            # sterownik bezgłowy: drive(keys) → stan; testy i odtwarzanie nagrań
    autosave.py          # autozapis przepisu i stanu (atomowo), wznowienie po awarii
  serve.py               # http.server na pętli zwrotnej: JSON API operacji, SSE postępu, token na uruchomienie (§3.18)
  data/                  # presety (§5); użytkownik nadpisuje w <katalog danych>/extract/
    formats/*.json  profiles/*.json  flows/*.json  cascades/*.json  presets/defaults.json
    block_kinds.json  lexicon/pl_inflection.json  lexicon/stop_pl.json  lexicon/stop_en.json
    redaction/*.json  grammar/pl.json  grammar/en.json  i18n/pl.json  i18n/en.json  prices.json
    ops.json  edits/*.json  recipes/*.json  select_fields.json  grammar/select.json
    commander/keymap.json  commander/actions.json  commander/layout.json  commander/colors.json
    timeline/presets.json  handoff/profile.json
  tests/test_*.py        # unittest, fixture'y syntetyczne (§9)
loom/tools/gen_extract_fixtures.py   # nowe fixture'y: Gemini, Grok, Claude Code, graf, katalog mieszany, duplikaty
```

### 3.4 Czytniki — zasada „nie powielaj parserów”

- **Formaty tekstowe** (HTML, MHT, Markdown, tekst, JSONL z rolami, SQLite): narzędzie używa
  **istniejących** metod `engine.importer.ConversationImporter` przez podklasę, która zastępuje
  jedyną metodę zapisu `_import_message_list(messages, title)` przechwyceniem do modelu wspólnego
  (wszystkie te handlery tam zbiegają; baza nie jest potrzebna, `engine.events` nie wymaga Kivy).
  Dzięki temu heurystyki ról, dzielenie Markdown i parser HTML są jedne.
- **Drzewa dostawców** (ChatGPT `mapping`, Claude `chat_messages`): `_import_chatgpt_mapping`
  w Pythonie spłaszcza drzewo do listy i odrzuca wszystko poza `user`/`assistant` i tekstem — czyli
  dokładnie to, o co prosi właściciel (gałęzie, rozumowanie, narzędzia, załączniki). Dlatego
  `readers/tree_walk.py` implementuje **bezstratne** przejście z semantyką `walk_tree`
  z `ingest_parse.cpp` (porządek dokumentu, bieżąca ścieżka z `current_node` /
  `current_leaf_message_uuid`, rozwidlenia z alternatywami, liniowy łańcuch gdy brak rodziców)
  i jest sprawdzane wobec **tej samej wyroczni** co importer C++: `EXPECTED.json`
  z `gen_export_fixtures.py` (liczby rozmów, wiadomości, gałęzi, rozwidleń, załączników). Parytet
  pilnuje test, nie zduplikowany kod.
- **Nowe źródła** (Gemini, Grok, Claude Code, nasz graf): nowe czytniki; ich pola natywne mapuje
  plik `formats/*.json`, kod czytnika zna tylko strukturę (drzewo / lista / wiersze / wpisy
  aktywności).
- **`libloom.so`** (gdy jest, `LOOM_LIBRARY`): drugi czytnik `loom_import_file_ex` do tymczasowego
  katalogu danych; test porównuje liczniki obu czytników, a użytkownik może nim czytać
  (`reader_backend: loom`). Nie jest wymagany.

Kontrakt czytnika: `iter_units(member, fmt, ctx) -> Iterator[Unit]`; każdy czytnik oddaje też
`unknown` (co widział, a nie umiał przypisać) i `absent` (czego formatu z definicji nie ma —
z `formats/*.json`, pole `known_absent`).

### 3.5 Dane i nakładki

Pierwszeństwo (najbardziej konkretne wygrywa, jak w `PortProfile`/R47): plik zadania → opcje CLI →
nakładka użytkownika `<katalog danych>/extract/<rodzaj>/*.json` (katalog danych aplikacji przez
`engine.paths.resolve_data_dir`, więc `CHATADHD_DATA` działa) → presety dostarczane
`loom/tools/extract/data/`. Każdy wynik wyboru ustawienia ma pole `source` („skąd to ustawienie”)
w manifeście. Nakładka może dodać, zmienić albo wyłączyć wpis presetu (`"disabled": true`), nigdy
nie modyfikuje pliku dostarczanego. Brak lub uszkodzony plik danych = jawny błąd z nazwą pliku,
nie ukryty domyślny literał w kodzie (R42).

### 3.6 Wznawianie i pamięć podręczna

`--cache KATALOG` (preset: `<pakiet>/cache/`). Wpisy: wyniki `scan` per sha archiwum, jednostki
per członek, osadzenia per (poziom selektora, model, sha tekstu), odpowiedzi modeli per
(przepływ, model, sha wejścia). Powtórny przebieg z tymi samymi wejściami nie liczy nic drugi raz
i **nie woła modelu** — odpowiedzi odtwarza z pamięci; gdy jej brak, mówi to i pyta. `odtwórz
manifest.json` buduje pakiet z manifestu i pamięci podręcznej.

### 3.7 Strumieniowanie plików wielogigabajtowych

Zip przez `ZipFile.open` (bez `extractall`), tablice JSON przez dekoder przyrostowy
(`json.JSONDecoder.raw_decode` nad buforem — ten sam pomysł co `_iter_json_elements`
w `engine/importer.py`), JSONL wiersz po wierszu, HTML Takeout dzielony na wpisy strumieniowo
(parser `html.parser` zdarzeniowy). Pamięć rośnie z jedną rozmową, nie z archiwum; wyjątek:
`preserve` dla formatu bez szardów (jeden `conversations.json`) pisze wynik strumieniowo
element po elemencie. Równoległość: `workers` (preset `auto` = liczba rdzeni, `1` = sekwencyjnie,
porządek wyjścia zawsze deterministyczny).

### 3.8 Model — jedna droga, koszt zawsze widoczny

- Trasa (`flows.model.route`): `loom_server` (URL z `--serwer`/`LOOM_SERVER_URL`; podgląd kosztu
  z `/api/chat/usage-preview`, wysyłka przez `/api/chat` — wtedy obowiązuje strażnik i polityka
  użycia aplikacji) albo `provider` (jawna konfiguracja: `base_url`, nazwa sekretu, model;
  OpenAI-zgodne `chat/completions`, bajty żądania jak adapter `openai_chat`). Żadnej trzeciej
  drogi w kodzie.
- Szacunek **przed** wysyłką: liczba wywołań × tokeny (lokalny szacunek 4 znaki/token dla EN,
  3,2 dla PL — stałe w `presets/defaults.json`, inwentarz pokazuje je jawnie) × cena
  z `models.json` katalogu danych (pole `pricing`, jak `engine/models.py`) lub `data/prices.json`;
  brak ceny → „nieznany”, nigdy zero.
- Strażnik ×10 (próg w danych): gdy szacunek przekracza 10× ostatni zaakceptowany koszt przebiegu
  z `<katalog danych>/extract/usage.jsonl`, pytanie przed wysyłką; w trybie bez terminala — odmowa
  z powodem, nigdy ciche wysłanie. Limity kwotowe na przebieg i na dzień jako ustawienia (`0` =
  brak).
- Dziennik `log.jsonl`: każde wywołanie z modelem, trasą, tokenami, kosztem, hashami wejścia
  i wyjścia (nigdy sekretów); suma w manifeście i raporcie.
- Model dla tła nigdy nie jest dziedziczony z czatu (zasada z 2026-10-15): przepływ nazywa klasę
  (`cheap`, `strong`) albo konkretny model; klasę rozwiązuje `presets/defaults.json` → nakładka.

### 3.9 Język

Teksty dla użytkownika (CLI, raport, komunikaty) z `data/i18n/pl.json` i `en.json`; klucze
zdarzeń w kodzie, zdania w danych (R42). Wybór: `--jezyk`, `LOOM_EXTRACT_LANG`, `ui_language`
z `config.json` aplikacji (gdy katalog danych znany), preset `pl`. Brak klucza w tablicy = klucz
wypisany jawnie, nie pusty tekst.

### 3.10 Port jądra (później)

Jądro woła narzędzie jako port `archive_extract` (rola w `data/runtime/ports.pack`), początkowo
przez proces (`python3 loom/tools/extract --zadanie - --json`), zadanie i manifest są już JSON.
Krok następny: etap `extract` w potoku `loom/src/archive` czytający te same pliki danych
(`formats`, `profiles`, `flows`, `cascades`) — dlatego są one danymi, nie kodem Pythona. Akcja
czatu („wyciągnij z archiwum X wszystko o Y”) = wpis katalogu akcji wskazujący ten port, z tym
samym szacunkiem i strażnikiem co inne akcje.

### 3.11 Jeden silnik, trzy twarze

Silnik (`engine.py`) trzyma **stan**: migawkę jednostek (po `scan→read→merge`, niezmienną),
drzewo pakietu (kopia, edytowalna), zaznaczenie (zbiór identyfikatorów ziaren z powodami), filtry
widoków i dziennik operacji. **Operacja** = funkcja `(nazwa, parametry, stan) → (nowy stan, wynik,
zdarzenia)`; rejestr operacji jest plikiem danych (`data/ops.json`: nazwa, schemat parametrów, czy
zmienia pakiet, czy wymaga modelu, czy jest odwracalna, czy jest nagrywana do przepisu). Każda
operacja wykonana przez którąkolwiek twarz jest **krokiem przepisu** (§3.12). Długie operacje biegną
w wątku roboczym i zgłaszają postęp zdarzeniami (`progress`, `done`, `error`, `question` — np.
pytanie o koszt); twarze tylko je pokazują i odpowiadają.

| Twarz | Wejście | Jak woła silnik | Co zapisuje |
|---|---|---|---|
| CLI (`cli.py`) | czasownik i opcje, zdanie, plik zadania, plik przepisu | jedna lub więcej operacji po kolei | `recipe.json` równoważny poleceniu, manifest, raport |
| Komander (`commander/`) | klawisze (z `keymap.json`) | akcja → operacja; wynik odświeża panele | każdy krok zmieniający stan do przepisu (nagrywanie), autozapis |
| Serwowanie (`serve.py`) | HTTP JSON na `127.0.0.1` | `POST /api/op` → ta sama operacja | przepis sesji; dziennik żądań |

Zasada: żadna twarz nie ma własnej logiki wyboru, edycji ani zapisu. Test parytetu (§9) uruchamia
ten sam przepis trzema drogami i porównuje `reproducible_hash`. Później czwarta twarz — akcja czatu
aplikacji przez port (§3.10) — to tylko kolejne wejście do tego samego rejestru.

### 3.12 Przepis — wszystko, co robi użytkownik, jest danymi

Przepis (`loom.extract.recipe/1`, §5.10) = źródła + ustawienia + lista kroków + decyzje ręczne +
widoki, zakładki, etykiety. Krok = `{id, op, params, by}` (`by`: `cli` | `tui` | `api` | `sentence`
| `recipe`), opcjonalnie `at` (poza haszem). Silnik **normalizuje** przepis przed wykonaniem (porządek
kluczy, parametry z presetów dopisane jawnie z polem `source`, ścieżki względem katalogu przepisu),
więc dwa przebiegi tego samego przepisu nad tymi samymi źródłami dają ten sam pakiet
(`reproducible_hash`). Komander **nagrywa**: każda akcja zmieniająca stan dopisuje krok; akcje tylko
nawigacyjne (kursor, przewijanie, zwijanie) nie. Nagranie da się odtworzyć bez ekranu (`przepis
uruchom`) i z ekranem (`komander --odtworz` — krok po kroku, do nauki i do testów). Kroki z modelem
odtwarzają się z pamięci podręcznej; gdy jej brak — zatrzymanie z pytaniem (zdarzenie `question`),
nigdy ciche wywołanie. Plik zadania (§4.3) jest przepisem bez kroków ręcznych; `odtworz manifest.json`
wykonuje przepis z manifestu. Przepis może wołać funkcje użytkownika (`op: user.call`, moduł
z nakładki) — ładowane tylko po jawnym wskazaniu, raport mówi, że kod jest użytkownika.

### 3.13 Oś czasu globalna

Każda jednostka dostaje `t` = chwila UTC z `created` (preset) obliczona z czasu natywnego: epoch
(ChatGPT), ISO ze strefą (Claude, Claude Code, Gemini), ISO bez strefy + strefa z presetu
`timeline.assume_zone` (oznaczone `inferred`), mtime pliku jako ostateczność (oznaczone). Oryginał
zostaje (`created_src`), obok `created_utc` i `created_local` dla strefy z ustawienia `zone` (preset
`source` = w wyjściach bez przeliczania; oś zawsze liczona w UTC). Klucze sortowania to presety
(`timeline/presets.json`): `created`, `updated`, `first_message`, `last_message`, `activity` (każda
wiadomość osobno — jedna oś wiadomości ze wszystkich rozmów i usług). Remisy rozstrzyga stały
porządek `(t, usługa, sha archiwum, lokalizator)` — stabilny między przebiegami i niezależny od
`workers`, zapisany w manifeście. Wyjścia: `timeline.jsonl` (wiersze z `t`, rodzaj, uid, usługa,
konto, tytuł lub rola, długość, lokalizator), kubełki dzień/tydzień/miesiąc (`buckets`), **luki**
(okresy bez aktywności dłuższe niż preset; `0` = nie raportuj), **serie** (kubełki z aktywnością
> k × mediana; k preset), **kontynuacje między usługami**: rozmowa z usługi B zaczynająca się
w oknie czasu po ostatniej wiadomości rozmowy z usługi A i dzieląca z nią słowa kluczowe, nazwy
plików lub identyfikatory (kontrast leksykalny jak w `klastry`) — wymieniona jako kandydat
`continuation` z pewnością, oznaczona `inferred`, nigdy scalana automatycznie; w komanderze
i w profilu `timeline` widoczna obok siebie. Opcje: `shift` (stałe przesunięcie), `anonymise`
(zachowane odstępy, wyzerowana epoka — oś względna), `granularity` (zaokrąglenie), `window`
(`--od`/`--do`). Edycja seryjna `edit.sort` porządkuje pakiet tym samym kluczem (§5.12).

### 3.14 Komander (X-Port Commander)

Dwa panele (lewy/prawy, `Tab`), każdy jest **widokiem** nad korzeniem: `sources` (archiwa →
rozmowy → gałęzie → wiadomości → bloki → załączniki), `package` (drzewo pakietu), `recipe` (kroki),
`timeline` (lista płaska), `attachments` (po typie, rozmiarze, rozmowie), `clusters`, `inventory`,
`diff`, `selection`. Widok ma tryb (drzewo albo lista płaska po wybranej ścieżce — np. wszystkie
wiadomości z wszystkich archiwów), kolumny per rodzaj węzła z `layout.json`, filtr żywy, sortowanie,
zaznaczenie z licznikiem i powodem per pozycja. Operacje podstawowe (klawisze z `keymap.json`; profile
`fkeys` i `vim` oba dostarczone, §5.11): otwórz/zwiń, podgląd (F3: JSON czytelny / Markdown / surowy
bajt w bajt, przełączane), kopiuj do pakietu (F5), przenieś w pakiecie (F6), nowa grupa/katalog (F7),
usuń z pakietu (F8), zmień nazwę, porównaj dwa węzły (różnica pól i tekstu), zaznacz/odznacz/odwróć
(`Ins`, `*`, `+`/`-` z wyrażeniem §6.9), „więcej takich” (podobieństwo wektorowe lub leksykalne,
próg preset), zakładka, etykieta, zapisany widok, szukaj w obu panelach (`/`), idź do (id, data),
okruszki, wiersz stanu (liczniki widoku, rozmiar zaznaczenia, szacowany rozmiar pakietu, szacowany
koszt kroków płatnych w przepisie, ostatnia operacja), menu akcji (F2), pomoc generowana
z aktualnego keymapu (F1), edycja seryjna (F4 na zaznaczeniu → wybór operacji → parametry →
podgląd różnic → zastosuj / odrzuć; `Ctrl-Z`/`Ctrl-Y` cofnij/ponów). Długa praca biegnie w wątku
roboczym z paskiem postępu; ekran nigdy nie czeka; `Esc` przerywa operację z zachowaniem stanu.
**Autozapis**: po każdym kroku `komander.autosave.json` (przepis + stan widoków; zapis atomowy
`.tmp` → `replace`), start po awarii proponuje wznowienie. `CommanderState` nie zna curses: lista
klawiszy → stan jest testowalna bezgłowo (§9); `view_curses.py` tylko rysuje i jest ładowany
leniwie, więc brak `curses` (Windows bez `windows-curses`) daje czytelny komunikat, a CLI i API
działają. Teksty z i18n; kolory i znaki ramek z `colors.json` (w tym profil bez kolorów i ASCII dla
terminali 16-kolorowych i czytników ekranu).

### 3.15 Edycja seryjna: podgląd, zastosowanie na kopii, cofnięcie

Operacja edycji (`data/edits/*.json` + moduł, §5.12) ma trzy funkcje: `preview(drzewo, zakres,
parametry) → różnica`, `apply`, `invert` (gdy odwrotność jest obliczalna; inaczej migawka zakresu
sprzed operacji, zapisana w `cache/`). Zakres = bieżące zaznaczenie albo wyrażenie języka
zaznaczania (§6.9). Różnica = lista zmian jako JSON Patch (RFC 6902) plus linie czytelne („12
wiadomości: rola `tool` → `assistant`”; szablony w pliku operacji). Zastosowanie dopisuje wpis do
`oplog.jsonl` (operacja, zakres, hash drzewa przed i po, odwrotność lub odwołanie do migawki) i krok
do przepisu; `cofnij` zdejmuje ostatni wpis i stosuje odwrotność, `ponow` odwrotnie; manifest wymienia
wszystkie edycje w `edits[]`, raport w sekcji „co zmieniono”. Edycja nigdy nie dotyka migawki źródeł —
tylko drzewa pakietu. Podgląd na próbce (K pierwszych dopasowań, K preset, `0` = wszystkie) przed
pełnym zastosowaniem; `--tak` pomija pytanie, nie wpis w dzienniku. Własna operacja = moduł użytkownika
zarejestrowany wpisem danych (`module: "user:nazwa"`), ładowany tylko po jawnym wskazaniu.

### 3.16 Różnica: co nowego od ostatniego eksportu

`roznica STARE NOWE` (archiwa, katalogi, pakiety albo manifesty) porównuje zbiory jednostek po
tożsamości (usługa, `ext_id`) i hashu treści: **nowe** (brak w starym), **zmienione** (ten sam
`ext_id`, inny hash — z różnicą na poziomie wiadomości: dodane, zmienione, usunięte, zmieniony tytuł,
nowa gałąź, nowy załącznik), **usunięte** (w starym, nie w nowym — wymienione, nigdy pomijane;
eksporty nie zawierają rozmów skasowanych po stronie usługi), **bez zmian**. Wynik: `diff.json`
(§5.14), sekcja raportu „zmiany między eksportami”, widok `diff` w komanderze (obok siebie, z różnicą
tekstu wiadomości). Krok przepisu `diff` z `mode: only_new | changed | removed | all` zasila
`select`, więc **przebieg przyrostowy** = ten sam przepis z `--od-manifestu poprzedni/manifest.json`
(manifest pamięta hashe jednostek). Dwa eksporty tego samego konta w jednym przebiegu robią to samo
przez `merge` (`versions[]`); `roznica` działa też na dwóch pakietach.

### 3.17 Pakiet przekazania do importera aplikacji

`handoff/` w pakiecie = katalog, który aplikacja importuje **jedną operacją** (`loom import
<pakiet>/handoff`, panel importu na Androidzie, `ConversationImporter.import_file` w Pythonie —
oba importery rozumieją już kształt rozmowa → wiadomości z `role`/`content`/`created` i metadanymi):
`handoff.json` (`loom.extract.handoff/1`: lista plików, liczniki, hash, profil, pochodzenie = hash
manifestu), `conversations/*.json` (rozmowa → wiadomości; `metadata.export` z proweniencją:
`schema: "loom.export.v1"` jak w `loom/src/import/export_internal.h`, lokalizator źródła, bloki
natywne, status gałęzi, wersje), `attachments/` z odwołaniami względnymi, i — gdy wybrano —
`graph/` (`loom.graph_packet/1`). Zasada: importer aplikacji **nie uczy się nowego formatu**; pakiet
przekazania używa pól, które importery już rozumieją, a wszystko ponad to idzie w `metadata` i jest
dostępne po imporcie (`messages.metadata.export.raw`). Test obiegu (§9): `wytnij … --przekaz` →
import przez `engine.importer` i przez `libloom` (gdy jest) → liczniki równe liczbom z manifestu.

### 3.18 Serwowanie (API lokalne)

`serwuj [--port N] [--adres 127.0.0.1] [--pakiet KAT]` uruchamia `http.server.ThreadingHTTPServer`
tylko na pętli zwrotnej (inny adres = jawne ustawienie z ostrzeżeniem w dzienniku). Token losowy
na uruchomienie, wypisany na stderr i zapisany w `<pakiet>/serve.token` (prawa 600), wymagany
w nagłówku `X-Extract-Token`. Punkty: `GET /api/ops` (rejestr z `ops.json`), `POST /api/op`
(`{op, params}` → wynik albo `{job}` dla operacji długich), `GET /api/jobs/{id}`, `GET /api/events`
(SSE postępu i pytań — jak `/api/events` serwera Loom), `POST /api/answer` (odpowiedź na pytanie,
np. zgoda na koszt), `GET /api/view?root=&path=&filter=&sort=` (dokładnie to, co widzi panel
komandera), `GET /api/recipe`, `POST /api/recipe/run`, `GET /api/package/manifest`,
`GET /api/i18n`. Odpowiedzi to te same obiekty, które komander rysuje — stąd webowy Warsztat
(`loom/web`, układ `Warsztat` w `shell/data/layouts.json`) będzie mógł pokazać ten sam komander bez
powtarzania logiki; do tego czasu `serwuj` służy testom, skryptom i portowi jądra. Serwowanie nie
zmienia reguły z §2: zero sieci wychodzącej; kroki z modelem nadal wymagają jawnego włączenia
w żądaniu i przechodzą przez szacunek i strażnika.

## 4. CLI

### 4.1 Czasowniki (polskie, z angielskimi aliasami)

| Polecenie | Alias | Co robi |
|---|---|---|
| `inwentarz ŹRÓDŁA… [--pakiet KAT]` | `inventory` | scan→read→merge→inventory; pisze `inventory.json` i raport inwentarza |
| `szukaj ŹRÓDŁA… --temat "…" [--kryteria plik] [--na-sucho]` | `search`, `--dry-run` | kaskada bez pisania pakietu; lista trafień z pasmami, licznikami, kosztem przyszłych kroków |
| `wytnij ŹRÓDŁA… --temat "…" [--profil …]… [--pakiet KAT]` | `cut` | pełny potok; pakiet + manifest + raport |
| `wyciągnij …` | `extract` | synonim `wytnij` (słowo właściciela) |
| `zaznacz --pakiet KAT [--lista] [+id…] [-id…] [--powód "…"]` | `mark` | decyzje ręczne na liście trafień; zapis do `decisions.json` (nakładka użytkownika, stosowane w następnych przebiegach) |
| `klastry ŹRÓDŁA… [--wybierz id…]` | `clusters` | odkrywanie tematów; lista kandydatów do potwierdzenia; wybór = kryterium `clusters` |
| `opisz "filtr słownie" [--model]` | `describe` | zamiana opisu słownego na kryteria; bez `--model` — gramatyka regułowa; pokazuje kryteria, zapisuje po potwierdzeniu |
| `profil list\|show\|new\|check` | `profile` | profile przebudowy |
| `przeplyw list\|show\|check` | `flow` | przepływy i kaskady |
| `koszt ŹRÓDŁA… --temat "…" --model` | `cost` | tylko szacunek kroków płatnych |
| `odtworz manifest.json [--pakiet KAT]` | `reproduce` | pakiet z manifestu i pamięci podręcznej; bajt w bajt, gdy źródła niezmienione |
| `wyjasnij --pakiet KAT ID` | `explain` | pełna ścieżka decyzji dla rozmowy/wiadomości: poziomy, wyniki, powody, kto zdecydował |
| `formaty [PLIK]` | `formats` | znane formaty; dla pliku: co rozpoznano, z czego, co ten format zawiera i czego nie |
| `komander [ŹRÓDŁA…] [--pakiet KAT] [--przepis PLIK] [--odtworz] [--klawisze fkeys\|vim]` | `commander`, `xc` | komander dwupanelowy w terminalu (§3.14); nagrywa przepis; `--odtworz` odgrywa przepis krok po kroku |
| `serwuj [--port N] [--adres A] [--pakiet KAT]` | `serve` | lokalne API JSON tych samych operacji (§3.18) |
| `przepis uruchom\|pokaz\|sprawdz\|nagraj\|normalizuj PLIK` | `recipe` | przepisy jako dane (§5.10); `nagraj` = komander z nagrywaniem do wskazanego pliku; `--plan` pokazuje kroki z szacunkiem czasu i kosztu bez wykonania |
| `os-czasu ŹRÓDŁA… [--klucz created\|updated\|first_message\|last_message\|activity] [--kubelek dzien\|tydzien\|miesiac] [--od D1 --do D2] [--strefa Z]` | `timeline` | jedna oś czasu dla wszystkich źródeł; luki, serie, kontynuacje; `timeline.jsonl` (§3.13) |
| `roznica STARE NOWE [--tylko nowe\|zmienione\|usuniete]` | `diff` | co nowego od ostatniego eksportu (§3.16); działa na archiwach, pakietach i manifestach |
| `edytuj --pakiet KAT --operacja NAZWA [--zakres WYRAŻENIE] [--param k=v]… [--podglad]` | `edit` | edycja seryjna z CLI: podgląd różnic, zastosowanie na kopii, wpis do przepisu (§3.15) |
| `cofnij\|ponow --pakiet KAT [N]` | `undo`, `redo` | dziennik operacji |
| `zaznacz --wyraz "WYRAŻENIE" [--dodaj\|--odejmij\|--przetnij]` | `mark --expr` | zaznaczanie językiem wyrażeń (§6.9) — ten sam język w komanderze i API |
| `zalaczniki ŹRÓDŁA… [--typ MIME] [--rozmiar >N] [--rozmowa ID] [--brakujace]` | `attachments` | przeglądarka załączników: odwołania, rozwiązane, brakujące, po typie/rozmiarze/rozmowie |
| `przekaz --pakiet KAT [--z-grafem]` | `handoff` | pakiet przekazania do importera aplikacji (§3.17) |
| `statystyki --pakiet KAT [--rozmowa ID]` | `stats` | statystyki per rozmowa i łącznie, oś tematu; streszczenia z `--model` |
| `filtr zapisz\|list\|usun NAZWA [WYRAŻENIE]` | `filter` | zapisane filtry (komander, `szukaj`, API) |
| `sprawdz --pakiet KAT` | `check` | walidacja pakietu: schematy, odwołania do załączników, hashe, importowalność `handoff/` |

Wspólne opcje: `--zadanie PLIK|-` (JSON lub TOML — `tomllib` jest w bibliotece standardowej 3.11),
`--jezyk pl|en`, `--json` (wyjście maszynowe na stdout, komunikaty na stderr), `--cache KAT`,
`--dane KAT` (katalog danych aplikacji), `--model` (włącza poziomy z modelem; bez tego poziomy
płatne są pomijane i raport mówi, ile zostało niejednoznaczne), `--tak` (akceptuj szacunek bez
pytania — tylko gdy limit kwotowy ustawiony), `--workers N`, `--loguj poziom`, `--przepis PLIK`
(wykonaj kroki przepisu; z czasownikiem — przed nim), `--plan` (pokaż kroki, szacunek czasu
i kosztu, nie wykonuj), `--zbiorczo` (tylko liczby w raporcie, dzienniku i `--json` — tryb dla
przebiegów agentowych na prawdziwych eksportach, §2).

Kody wyjścia: `0` ok; `2` błąd argumentów; `3` nic nie rozpoznano (raport mówi, co widziano);
`4` przerwano, stan zapisany (wznów tym samym poleceniem); `5` krok płatny odmówiony (koszt,
strażnik, limit); `6` źródło nieczytelne (lista w raporcie); nigdy `0` z cichym pominięciem.

### 4.2 Zdanie

`python3 loom/tools/extract "wyciągnij z archiwum ~/eksporty/chatgpt.zip wszystko o ChatADHD w
profilu płaskim"` — pierwszy argument bez znanego czasownika CLI jest zdaniem. Gramatyka
regułowa jako dane (`data/grammar/pl.json`: wzory „wyciągnij/wytnij/pokaż/policz …
z archiwum/z archiwów X … wszystko o/na temat T … w profilu P … od D1 do D2 … bez narzędzi …
tylko z załącznikami …”). Zdanie jest tłumaczone na zadanie (JSON), **pokazane użytkownikowi**
przed wykonaniem (`--na-sucho` pokazuje tylko tłumaczenie), i zapisane w manifeście. Czego
gramatyka nie zrozumiała, wypisuje jako resztę i — tylko z `--model` — proponuje interpretację
modelem (`opisz`). Reguły gramatyki dopisuje użytkownik w nakładce.

### 4.3 Plik zadania

```json
{"schema": "loom.extract.task/1",
 "sources": ["~/eksporty/chatgpt_2026-10-01.zip", "~/eksporty/claude/", "~/.claude/projects/"],
 "criteria": "chatadhd",
 "profiles": ["preserve", "flat", "structure"],
 "package": "~/ekstrakty/chatadhd-2026-10-16",
 "flows": {"model": {"enabled": false}},
 "language": "pl"}
```

Każde pole ma preset; `criteria` to nazwa pliku z `criteria/` albo obiekt inline; `"topic": "…"`
jest skrótem na kryteria z jednym słowem kluczowym i jednym zapytaniem wektorowym. Plik zadania
jest przepisem (§5.10) bez kroków ręcznych: `"recipe": "nazwa"` albo `"steps": [...]` inline
zastępuje `criteria`/`profiles` krokami jawnymi; silnik i tak zapisuje postać znormalizowaną.

### 4.4 Scenariusz właściciela, krok po kroku

```sh
E="python3 loom/tools/extract"
$E inwentarz ~/eksporty/*.zip ~/.claude/projects/ --pakiet ~/ekstrakty/chatadhd
$E klastry ~/eksporty/*.zip --pakiet ~/ekstrakty/chatadhd           # kandydaci: „agent”, „analizator archiwów”, …
$E szukaj ~/eksporty/*.zip ~/.claude/projects/ --temat "ChatADHD" --kryteria chatadhd --na-sucho
$E zaznacz --pakiet ~/ekstrakty/chatadhd --lista                     # przegląd pasma niejednoznacznego
$E zaznacz --pakiet ~/ekstrakty/chatadhd +c_18 -c_77 --powód "c_77 to leki, nie aplikacja"
$E koszt ~/eksporty/*.zip --temat ChatADHD --model                   # ile zapłacę za tani model na reszcie
$E wytnij ~/eksporty/*.zip ~/.claude/projects/ --kryteria chatadhd --profil preserve --profil flat --profil structure --pakiet ~/ekstrakty/chatadhd
$E os-czasu ~/eksporty/*.zip ~/.claude/projects/ --kubelek tydzien --pakiet ~/ekstrakty/chatadhd   # jedna oś; luki, serie, kontynuacje ChatGPT→Claude
$E komander ~/eksporty/*.zip --pakiet ~/ekstrakty/chatadhd        # przegląd, zaznaczanie, edycja seryjna; nagrywa recipe.json
$E przepis uruchom ~/ekstrakty/chatadhd/recipe.json                 # to samo bez ekranu, ten sam reproducible_hash
$E roznica ~/eksporty/chatgpt_2026-06-12.zip ~/eksporty/chatgpt_2026-10-01.zip   # co doszło między eksportami
$E przekaz --pakiet ~/ekstrakty/chatadhd                            # handoff/ do importu w aplikacji jednym krokiem
```

### 4.5 Zdanie, plik, ekran, API — ten sam przebieg

```sh
# 1. zdanie
$E "wyciągnij z archiwum ~/eksporty/chatgpt.zip wszystko o ChatADHD w profilu płaskim"
# 2. plik przepisu (lub zadania) — ten sam, który zdanie wygenerowało i zapisało w manifeście
$E przepis uruchom chatadhd-flat.json
# 3. komander: F2 → „Zaznacz wyrażeniem” → title~/chat\s*adhd/i OR ~"aplikacja czatu z pamięcią"
#    → F5 (kopiuj do pakietu) → F4 „spłaszcz do ścieżki bieżącej” → podgląd → zastosuj → F10 zapis
#    (nagrany przepis = jak w 2.; `przepis normalizuj` pokazuje, że są równoważne)
# 4. API (serwuj uruchomiony)
curl -s -H "X-Extract-Token: $T" -d '{"op":"recipe.run","params":{"recipe":"chatadhd-flat.json"}}' \
  http://127.0.0.1:8765/api/op
```

Cztery drogi, jeden `reproducible_hash` (test parytetu, §9). Później piąta: akcja czatu aplikacji
przez port (§3.10).

## 5. Formaty danych

Wszystkie pliki JSON (UTF-8, `ensure_ascii=False`, klucze w porządku zapisu), każdy z polem
`schema` z wersją. Zmiana niezgodna = nowa wersja schematu i czytnik starej.

### 5.1 Opis formatu źródła — `formats/*.json`

Odpowiednik tablicy platform jądra (`platforms.h`, sekcja `platforms` w `import.pack`), ale
pełniejszy: mówi też, jak mapować bloki i czego format **nie zawiera**.

```json
{"schema": "loom.extract.format/1", "id": "openai.chatgpt.export",
 "vendor": "openai", "product": "chatgpt", "label": {"pl": "Eksport danych ChatGPT", "en": "ChatGPT data export"},
 "containers": ["zip", "json"],
 "sniff": {"any_of": [{"zip_member": "conversations.json"}, {"zip_member_glob": "conversations-*.json"},
                      {"json_object_keys_all": ["mapping", "title"]},
                      {"json_array_first_keys_all": ["mapping"]}]},
 "reader": "chatgpt",
 "shards": "conversations-NNN.json", "wrappers": ["conversations"],
 "conversation": {"ext_id": ["conversation_id", "id"], "title": "title",
                  "created": {"path": "create_time", "type": "epoch"}, "updated": {"path": "update_time", "type": "epoch"},
                  "tree": "mapping", "current": "current_node"},
 "message": {"role": "author.role", "author_name": "author.name", "recipient": "recipient",
             "created": {"path": "create_time", "type": "epoch"}, "content": "content"},
 "roles": {"user": "user", "assistant": "assistant", "system": "system", "tool": "tool"},
 "blocks": {"text": "text", "multimodal_text": "<parts>", "thoughts": "thinking:summary",
            "reasoning_recap": "thinking:summary", "code": "tool_use", "execution_output": "tool_result",
            "tether_browsing_display": "tool_result", "tether_quote": "citation", "sonic_webpage": "citation",
            "image_asset_pointer": "attachment", "audio_asset_pointer": "attachment",
            "real_time_user_audio_video_asset_pointer": "attachment", "audio_transcription": "text",
            "user_editable_context": "context", "model_editable_context": "context", "system_error": "error",
            "*": "unknown"},
 "attachments": {"pointer_prefix": "file-", "pointer_fields": ["asset_pointer", "metadata.attachments[].id"],
                 "layouts": ["{id}-{name}", "dalle-generations/{id}-{name}", "user-*/{id}-{name}", "{id}.dat"]},
 "members": {"user.json": "account", "message_feedback.json": "feedback", "shared_conversations.json": "shared_link",
             "model_comparisons.json": "member", "chat.html": "member", "textdocs/**": "artifact", "*": "unknown"},
 "preserve": {"unit": "conversation_object", "keep_members": ["user.json"], "attachment_root": "."},
 "known_absent": {"pl": ["pełne rozumowanie — tylko streszczenia thoughts/reasoning_recap", "prompt systemowy",
                         "pełne treści przeglądanych stron"],
                  "en": ["…"]}}
```

Rozpoznanie zawsze **z treści** (sygnatura zip, pierwszy nie-biały znak JSON, klucze pierwszego
obiektu, `<html` + znacznik produktu); rozszerzenie jest tylko podpowiedzią kolejności prób. Nowy
format = nowy plik + ewentualnie czytnik, gdy struktura jest nowa (jak w `platforms.h`).

### 5.2 Kryteria — `criteria/*.json`

```json
{"schema": "loom.extract.criteria/1", "id": "chatadhd", "language": "pl",
 "description": "ChatADHD/Loom i wcześniejsze małe projekty do integracji (agent, analizator archiwów)",
 "keywords": [
   {"term": "ChatADHD", "inflect": "forms", "forms": ["ChatADHD", "Chat ADHD", "czata", "czacie", "czatem"], "fold": true, "weight": 2.0},
   {"term": "Loom", "boundary": true, "weight": 1.5},
   {"term": "analizator archiwów", "inflect": "rules", "weight": 1.0},
   "importer", "graf wiedzy", "pamięć hierarchiczna", "OpenRouter", "Kivy"],
 "regex": [{"pattern": "(?i)chat\\s*adhd", "weight": 2.0}, {"pattern": "\\bloom/(src|include|tools)/", "weight": 1.5}],
 "exclude": {"keywords": [{"term": "ADHD", "when_without": ["chat", "aplikacja", "app"]}], "regex": []},
 "masks": {"date": {"from": "2024-06-01", "to": null}, "services": [], "accounts": [], "roles": [],
           "tools": [], "title_regex": null, "with_attachments": null, "with_branches": null,
           "agentic_only": false, "languages": []},
 "vectors": [{"query": "aplikacja czatu z wieloma modelami, pamięcią hierarchiczną i grafem wiedzy",
              "threshold": 0.35, "candidates": 0, "grain": "window"}],
 "clusters": [],
 "grain": "conversation", "expand": "conversation",
 "cascade": "default",
 "flows": {"model": {"enabled": false, "cheap": "cheap_classify", "strong": "strong_judge"}},
 "weights": {"lexical": 1.0, "vectors": 1.0, "cheap": 1.5, "strong": 3.0}}
```

- `inflect`: `off` | `rules` (lekki stemer PL z `lexicon/pl_inflection.json`: zdejmowanie
  końcówek, składanie znaków diakrytycznych ą→a itd.) | `forms` (formy podane jawnie; `rules`
  dodatkowo, gdy `"also_rules": true`). W „dlaczego pasuje” widać dopasowaną formę.
- `boundary`: dopasowanie na granicy słowa; `fold`: bez rozróżniania wielkości i znaków.
- `candidates: 0` = bez cięcia liczby kandydatów; `threshold` to preset, nie reguła.
- `exclude` nigdy nie usuwa po cichu: wykluczone pozycje są w `explain.jsonl` z powodem
  i w raporcie jako liczba.

### 5.3 Przepływy — `flows/*.json`

Każdy poziom kaskady wykonuje jeden przepływ. Przepływy lokalne (`masks`, `lexical`, `vectors`)
i z modelem (`cheap_classify`, `strong_judge`, `summarize`, `describe_criteria`) mają ten sam
kształt; użytkownik dopisuje własne.

```json
{"schema": "loom.extract.flow/1", "id": "cheap_classify", "kind": "model", "class": "cheap",
 "label": {"pl": "Tani model: czy to o temacie?", "en": "Cheap model: on topic?"},
 "input": {"grain": "window", "window": 6, "max_chars": 0, "fields": ["title", "role", "text"],
           "include_blocks": ["text"], "strip": ["citations"]},
 "prompt": {"system": {"pl": "Oceniasz, czy fragment rozmowy dotyczy tematu: {topic}. Odpowiedz JSON …"},
            "user": {"pl": "Tytuł: {title}\n{window}\nKryteria: {criteria_summary}"}},
 "output": {"schema": "verdict", "values": ["include", "exclude", "ambiguous"], "confidence": true, "reason": true},
 "model": {"route": "inherit", "class": "cheap", "parameters": {"temperature": 0, "max_tokens": 200}},
 "cache": true, "parallel": 4}
```

Przepływy z modelem w presetach mają `enabled: false` w kaskadzie; `--model` włącza je bez edycji
plików. `summarize` (streszczenia rozmów do raportu) i `describe_criteria` (opis słowny →
kryteria) są osobnymi przepływami, też wyłączonymi domyślnie.

### 5.4 Kaskady — `cascades/*.json`

```json
{"schema": "loom.extract.cascade/1", "id": "default",
 "levels": [
   {"id": "L0", "flow": "masks",          "on": "all",       "decides": ["exclude"]},
   {"id": "L1", "flow": "lexical",        "on": "passed",    "decides": ["include", "exclude", "ambiguous"]},
   {"id": "L2", "flow": "vectors",        "on": "passed",    "decides": ["include", "exclude", "ambiguous"]},
   {"id": "L3", "flow": "cheap_classify", "on": "ambiguous", "enabled": false},
   {"id": "L4", "flow": "strong_judge",   "on": "ambiguous", "enabled": false}],
 "combine": "weighted_max", "bands": {"include": 0.75, "exclude": 0.25},
 "manual": "wins", "unresolved": "report", "shadow": {"lexical_vs_vectors": true}}
```

`on`: `all` | `passed` (nie wykluczone wyżej) | `ambiguous` | `included` (weryfikacja);
`unresolved`: `report` (zostaje w paśmie, raport pyta) | `include` | `exclude` (presety do wyboru,
nigdy ukryte); `shadow` włącza listę rozbieżności kanałów (R27). Inne kaskady w presetach:
`recall_first` (wszystko, co trafiło w którymkolwiek kanale, wchodzi; model tylko weryfikuje
wykluczenia), `precision_first`, `lexical_only`, `vectors_only`.

### 5.5 Profile przebudowy — `profiles/*.json`

| Profil | Co zawiera |
|---|---|
| `preserve` | format źródłowy 1:1: te same pliki (`conversations.json` lub szardy, `user.json`…), mniej rozmów, surowe obiekty bajt w bajt (bez reserializacji, kopiowane z lokalizatora), załączniki w tym samym układzie katalogów; dla Claude Code — te same wiersze JSONL; importuje się z powrotem do aplikacji źródłowej (tam, gdzie ona to umie) i do nas |
| `flat` | rozmowa → wiadomości po wybranej ścieżce, tylko tekst, role, czasy; nota o wyciętych gałęziach |
| `structure` | tytuły, daty, konta, drzewo gałęzi, lista narzędzi, załączniki, liczniki — bez treści |
| `reasoning` | wyłącznie bloki rozumowania i ślady narzędzi (wywołanie + wynik) z odwołaniem do wiadomości |
| `text` | Markdown czytelny dla człowieka (jeden plik na rozmowę lub jeden wspólny), nagłówki z czasem i rolą, załączniki jako linki względne |
| `graph` | nasz pakiet `loom.graph_packet/1`: węzły rozmowa/wiadomość/załącznik/temat/narzędzie, krawędzie `reply_to`, `part_of`, `mentions`, `tagged_with`, `derived_from`; proweniencja każdego węzła = lokalizator źródła |
| `table` | CSV wiadomości (rozmowa, id, rodzic, rola, czas, gałąź, rodzaje bloków, tekst) |
| `timeline` | lista chronologiczna wiadomości lub rozmów ze **wszystkich** źródeł (klucz i strefa z presetu, §3.13), z usługą, kontem, lokalizatorem; luki i serie jako wiersze `gap`/`burst`; kontynuacje między usługami jako `continuation` (`inferred`); JSONL + Markdown „dziennik” |
| `cards` | karty rozmów: tytuł, okres, uczestnicy, narzędzia, streszczenie i otwarte pytania (streszczenie tylko z `--model`; bez niego pola puste z notą) |
| własny | z pól i reguł jak niżej |

```json
{"schema": "loom.extract.profile/1", "id": "flat", "label": {"pl": "Płaski: rozmowa → wiadomości"},
 "layout": "conversation>message",
 "branches": {"keep": "current", "alternatives": "note", "note_text": {"pl": "wycięto {n} gałęzi w {k} miejscach"}},
 "fields": {"conversation": ["uid", "title", "created", "service", "account.label"],
            "message": ["id", "role", "created", "text"]},
 "blocks": {"text": "text", "thinking": "omit", "tool_use": "omit", "tool_result": "omit",
            "attachment": "reference", "citation": "omit", "unknown": "note"},
 "timestamps": {"zone": "source", "format": "iso8601", "shift": null, "anonymise": null},
 "redaction": "none",
 "outputs": [{"kind": "jsonl", "path": "flat/messages.jsonl"}, {"kind": "markdown", "path": "flat/{conversation.uid}.md"}],
 "sort": "source_order"}
```

`branches.keep`: `tree` | `current` | `newest` | `longest` | `marked` (z `decisions.json`) |
`all_paths` (każda ścieżka osobno). Bloki: `keep` | `omit` | `reference` | `summary` (tylko
z `--model`) | `note`. Każdy element profilu jest wartością, nie kodem; `profil check` sprawdza
plik względem schematu.

### 5.6 Decyzje ręczne — `decisions.json` (nakładka użytkownika)

```json
{"schema": "loom.extract.decisions/1", "criteria": "chatadhd",
 "entries": [
   {"unit": "sha256:3f9c…", "grain": "conversation", "decision": "include", "reason": "to o aplikacji", "at": "2026-10-16T10:12:00Z",
    "was": {"level": "L2", "decision": "ambiguous", "score": 0.52}},
   {"unit": "sha256:91ab…", "grain": "conversation", "decision": "exclude", "reason": "leki, nie aplikacja",
    "learn": {"exclude_keyword": {"term": "ADHD", "when_without": ["chat", "aplikacja"]}}}]}
```

Tożsamość jednostki to hash treści, więc decyzja przeżywa nowy eksport tej samej rozmowy. Pole
`learn` to **propozycja** reguły wywnioskowanej z poprawki (pokazana, zapisana po potwierdzeniu
do nakładki kryteriów); dwie zgodne poprawki tego samego rodzaju → propozycja z wyższym
priorytetem (cykl wystąpienie → hipoteza → zasada, jak u strażnika). Nic nie staje się regułą bez
potwierdzenia.

### 5.7 Inwentarz — `inventory.json`

Zmierzone, per archiwum i łącznie: formaty i członkowie (znane / nieznane z przykładem), konta,
rozmowy, wiadomości, role, **rodzaje bloków natywne → wspólne** (liczby i znaki), narzędzia
(nazwy, liczby wywołań), bloki rozumowania (liczba, znaki, ile „pełnych” wg progu z presetu),
gałęzie i rozwidlenia, załączniki (odwołania, rozwiązane, brakujące, typy MIME, rozmiar), zakres
dat i histogram miesięczny, języki (detekcja n-gramowa z danych, udziały), luki (okresy bez
rozmów dłuższe niż preset), duplikaty i wersje między archiwami, szacunek tokenów, **`absent`:
czego format nie zawiera** (z `known_absent` formatu + obserwacja: „0 bloków rozumowania
w 1 240 wiadomościach asystenta”). Każda liczba ma pole `how` (skąd policzona).

### 5.8 Pakiet i manifest

```
<pakiet>/
  manifest.json      inventory.json     RAPORT.md        criteria.json     decisions.json
  recipe.json        oplog.jsonl        explain.jsonl    log.jsonl        state.json (tylko gdy przerwano)
  timeline.jsonl     timeline.json      diff.json        views.json       glossary.json
  komander.autosave.json (tylko komander; usuwany po czystym zamknięciu)   serve.token (tylko serwuj)
  handoff/           handoff.json  conversations/*.json  attachments/  graph/ (opcjonalnie)
  preserve/<nazwa archiwum>/…           flat/…  structure/…  reasoning/…  text/…  graph/…  table/…
  attachments/<nazwa archiwum>/…        (tylko użyte; układ jak w źródle; brakujące wymienione, nie podmienione)
  cache/
```

```json
{"schema": "loom.extract.manifest/1",
 "tool": {"loom_version": "0.10.00", "extract_schema": "1", "python": "3.11.9", "selector_tier": 2},
 "run": {"id": "sha256 wejść i zadania", "started": "…", "finished": "…", "command": ["…"], "sentence": "…"},
 "sources": [{"path": "…", "sha256": "…", "size": 0, "mtime": "…", "format": "openai.chatgpt.export", "units": 0,
              "members_unknown": ["…"]}],
 "task": {"…": "…"}, "criteria": {"…": "…"}, "settings_sources": {"cascade": "preset", "profiles[0]": "task", "…": "…"},
 "cascade": {"id": "default", "levels": [{"id": "L1", "evaluated": 0, "include": 0, "exclude": 0, "ambiguous": 0, "seconds": 0.0}],
             "shadow": {"lexical_only": 0, "vectors_only": 0}},
 "decisions": {"applied": 0, "file_sha256": "…"},
 "selection": {"conversations": 0, "messages": 0, "branches_kept": 0, "branches_cut": 0, "windows": 0},
 "expand": {"preset": "conversation", "added_messages": 0, "linked_conversations": 0},
 "attachments": {"referenced": 0, "copied": 0, "bytes": 0, "missing": [{"ref": "file-…", "conversation": "…"}]},
 "redaction": {"profile": "none", "entries": 0},
 "model_calls": [{"flow": "cheap_classify", "model": "…", "route": "provider", "calls": 0, "tokens_in": 0, "tokens_out": 0,
                  "cost": {"amount": 0.0, "currency": "USD", "known": true}, "cache_hits": 0}],
 "cost_total": {"amount": 0.0, "currency": "USD", "known": true},
 "outputs": [{"path": "preserve/chatgpt_2026-10-01/conversations.json", "sha256": "…", "profile": "preserve", "bytes": 0}],
 "recipe": {"sha256": "…", "steps": 13, "by": {"cli": 10, "tui": 3, "api": 0}},
 "edits": [{"step": "s8", "op": "edit.set", "scope": "message[role=tool]", "count": 12, "before": "sha256:…", "after": "sha256:…", "inverted_by": "invert"}],
 "timeline": {"key": "created", "ties": ["t", "service", "archive", "locator"], "zone": "source", "gaps": 3, "bursts": 2, "continuations": 1},
 "diff": {"against": "manifest sha256:…", "new": 0, "changed": 0, "removed": 0, "unchanged": 0},
 "handoff": {"path": "handoff/", "conversations": 0, "messages": 0, "attachments": 0, "sha256": "…"},
 "faces": ["cli", "tui"],
 "reproducible_hash": "sha256 wszystkich outputs[].sha256 w porządku ścieżek"}
```

`odtworz` porównuje `reproducible_hash`; `run.started/finished` nie wchodzą do hasza.

### 5.9 Raport — `RAPORT.md`

Po polsku, w słowach właściciela, sekcje: **co znaleziono** (rozmowy i wiadomości per źródło, per
klaster tematu, oś czasu tematu), **czego nie dało się odczytać** (członkowie nieznani, pliki
nieczytelne, załączniki brakujące — każdy z nazwą), **czego formaty nie zawierają** (z inwentarza),
**co zamaskowano** (liczby per rodzaj; nigdy wartości), **ile to kosztowało** (per przepływ,
łącznie; „0 — bez modelu” gdy tak było), **co pozostało niejednoznaczne** i jak to rozstrzygnąć
(`zaznacz`, `--model`), **jak odtworzyć** (`odtworz manifest.json`). Wersja `--json` raportu =
manifest + inwentarz.

### 5.10 Przepis — `recipes/*.json`

```json
{"schema": "loom.extract.recipe/1", "id": "chatadhd-flat",
 "label": {"pl": "ChatADHD: płaski ekstrakt z ChatGPT, Claude i Claude Code"},
 "sources": ["~/eksporty/chatgpt_2026-10-01.zip", "~/eksporty/claude_2026-10-03.zip", "~/.claude/projects/"],
 "settings": {"language": "pl", "cache": "auto", "workers": "auto", "flows": {"model": {"enabled": false}}},
 "steps": [
  {"id": "s1",  "op": "inventory"},
  {"id": "s2",  "op": "select.criteria",   "params": {"criteria": "chatadhd", "cascade": "default"}},
  {"id": "s3",  "op": "select.expr",       "params": {"expr": "service:openai AND created:2025-06..2025-12 AND NOT role:tool", "mode": "add"}},
  {"id": "s4",  "op": "select.similar",    "params": {"to": ["sha256:3f9c…"], "threshold": 0.5, "mode": "add"}, "by": "tui"},
  {"id": "s5",  "op": "select.structural", "params": {"pattern": "count(tool_use) >= 2", "mode": "add"}},
  {"id": "s6",  "op": "expand",            "params": {"preset": "conversation"}},
  {"id": "s7",  "op": "package.copy",      "params": {"from": "selection", "into": "/ChatADHD"}, "by": "tui"},
  {"id": "s8",  "op": "edit.set",          "params": {"where": "message[role=tool]", "field": "role", "value": "assistant"}, "by": "tui"},
  {"id": "s9",  "op": "edit.flatten",      "params": {"where": "conversation", "path": "current", "layout": "conversation>message"}},
  {"id": "s10", "op": "edit.sort",         "params": {"by": "created", "stable": true}},
  {"id": "s11", "op": "cut",               "params": {"profiles": ["preserve", "flat", "timeline"]}},
  {"id": "s12", "op": "handoff"},
  {"id": "s13", "op": "write",             "params": {"package": "~/ekstrakty/chatadhd-2026-10-16"}}],
 "manual": {"include": [{"unit": "sha256:…", "reason": "to o aplikacji"}], "exclude": []},
 "views": [{"id": "v1", "pane": "left", "root": "sources", "mode": "tree", "filter": "has:attachments", "sort": "created"}],
 "bookmarks": [{"unit": "sha256:…", "note": "pierwszy opis pamięci hierarchicznej"}],
 "tags": {"sha256:…": ["agent"], "sha256:…": ["analizator"]}}
```

- `op` to nazwa z `data/ops.json`; `params` wg schematu operacji; brakujące parametry uzupełnia
  normalizacja z presetów (z polem `source`); `by` mówi, która twarz dopisała krok.
- `mode` operacji `select.*`: `replace` | `add` | `subtract` | `intersect` (preset `add`).
- Krok z modelem (`op: flow.run`, `params.flow`) ma `enabled` dziedziczone z `settings.flows`;
  odtwarzanie bierze odpowiedzi z pamięci podręcznej po hashu wejścia.
- Przepis minimalny (to, co produkuje zdanie z §4.2): `sources`, `steps: [select.criteria, cut, write]`.
- Przepis przyrostowy: pierwszy krok `{"op": "diff", "params": {"against": "poprzedni/manifest.json", "mode": "only_new"}}`.
- `przepis sprawdz` waliduje schemat, nazwy operacji i parametry; `przepis normalizuj` wypisuje
  postać kanoniczną (ta trafia do manifestu i do hasza).

### 5.11 Komander jako dane — `commander/keymap.json`, `actions.json`, `layout.json`

```json
{"schema": "loom.extract.keymap/1",
 "profiles": {
  "fkeys": {"inherits": null, "bindings": {
    "F1": "help", "F2": "menu", "F3": "view", "F4": "edit", "F5": "copy", "F6": "move", "F7": "mkdir", "F8": "remove",
    "F9": "menu", "F10": "quit", "Tab": "pane.switch", "Enter": "open", "Backspace": "up", "Home": "top", "End": "bottom",
    "Insert": "select.toggle", "*": "select.invert", "+": "select.expr", "-": "deselect.expr", "Space": "select.toggle",
    "/": "search", "C-f": "filter.live", "C-g": "goto", "C-z": "undo", "C-y": "redo", "C-s": "recipe.save",
    "b": "bookmark", "t": "tag", "m": "select.similar", "d": "diff.nodes", "r": "rename", "v": "view.cycle",
    "Esc": "cancel"}},
  "vim": {"inherits": "fkeys", "bindings": {
    "j": "down", "k": "up", "h": "up", "l": "open", "g g": "top", "G": "bottom", "C-d": "page.down", "C-u": "page.up",
    "v": "select.toggle", "y": "copy", "p": "paste", "d d": "remove", "u": "undo", "C-r": "redo", ":": "command", "n": "search.next"}}},
 "default": "fkeys"}
```

`actions.json`: lista `{id, label: {pl, en}, kind: "op" | "state", op?, params?, records: bool,
needs_selection?: bool}` — `records` mówi, czy akcja trafia do przepisu (nawigacja: `false`).
`keymap.py` sprawdza, że każdy klawisz wskazuje istniejącą akcję, że w profilu nie ma kolizji
(sekwencje jak `g g` są rozwiązywane prefiksowo) i generuje ekran pomocy (F1) z profilu.
`layout.json`: kolumny per rodzaj węzła (`conversation`: tytuł, usługa, data, wiadomości, gałęzie,
załączniki, pasmo, wynik; `message`: rola, czas, bloki, znaki, początek tekstu; `block`: rodzaj,
nazwa, rozmiar; `attachment`: nazwa, MIME, rozmiar, stan), szerokości (`auto`/procent), pola
obliczane jako wyrażenia (`words = len(text.split())`), elementy wiersza stanu i ich kolejność.
`colors.json`: profile `dark`, `light`, `mono` (bez kolorów, ASCII). Nakładka użytkownika w
`<katalog danych>/extract/commander/` nadpisuje dowolny z tych plików (R47: najbardziej konkretne
wygrywa); własny profil klawiszy = plik z `inherits`.

### 5.12 Operacje edycji — `edits/*.json` + moduł

| Operacja | Co robi | Odwrotność |
|---|---|---|
| `set` | `pole = wartość` w zakresie | poprzednie wartości (`invert`) |
| `replace_regex` | regex z grupami na polu tekstowym | migawka zakresu |
| `map_table` | wartość → wartość z tabeli (np. nazwy modeli, nazwy narzędzi) | tabela odwrotna, gdy jednoznaczna; inaczej migawka |
| `rename_key` | zmiana nazwy klucza/pola w zakresie (np. `text` → `content`) | odwrotna nazwa |
| `delete_field` / `add_field` | usunięcie / dodanie pola (ze stałą lub wyrażeniem) | dodanie / usunięcie |
| `flatten` | drzewo → lista po ścieżce (`current`, `newest`, `longest`, `marked`, `all_paths`) do układu `conversation>message`, z notą o wyciętych gałęziach | migawka |
| `nest` | lista → drzewo po polu rodzica lub po regule (np. co N wiadomości, po dacie) | migawka |
| `split_at_date` | podział rozmowy datą (lub listą dat) na kilka | `merge_conversations` |
| `merge_conversations` | scalenie rozmów (np. kontynuacja ChatGPT→Claude) z zachowaniem źródła każdej wiadomości | `split` po zapisanych granicach |
| `reorder_by_time` / `sort` | porządek po kluczu czasu z §3.13, remisy stabilne | poprzedni porządek |
| `drop_blocks` / `keep_blocks` | usuń lub zostaw rodzaje bloków (np. bez `thinking`) | migawka |
| `move_attachments` | przeniesienie załączników do innego układu katalogów z aktualizacją odwołań | odwrotne przeniesienie |
| `relabel_roles` | mapa ról (np. `tool` → `assistant`, `human` → `user`) | mapa odwrotna |
| `shift_time` / `anonymise_time` | przesunięcie lub oś względna (§3.13) | odwrotne przesunięcie / migawka |
| `redact` | profil redakcji z §8 (28) jako operacja edycji z raportem | migawka (zaszyfrowana hasłem użytkownika, gdy włączone; inaczej brak — redakcja nieodwracalna jest jawnie oznaczona) |
| `user:<moduł>` | funkcja użytkownika `(drzewo, zakres, parametry) → zmiany` | migawka |

```json
{"schema": "loom.extract.edit/1", "id": "replace_regex", "module": "edits.values:ReplaceRegex",
 "label": {"pl": "Zamień regexem w polu", "en": "Regex replace in a field"},
 "scope": ["conversation", "message", "block", "attachment"],
 "params": {"where": {"type": "expr"}, "field": {"type": "path"}, "pattern": {"type": "regex"},
            "replacement": {"type": "string"}, "flags": {"type": "string", "preset": ""}},
 "invertible": "snapshot",
 "preview_lines": {"pl": "{count} × {field}: „{before}” → „{after}”"}}
```

### 5.13 Oś czasu — `timeline.json` + `timeline.jsonl`

`timeline.json`: `{"schema": "loom.extract.timeline/1", "key": "created", "zone": "source",
"ties": [...], "range": {"from": "…", "to": "…"}, "buckets": {"unit": "week", "rows": [{"start": "…",
"conversations": 0, "messages": 0, "by_service": {...}}]}, "gaps": [{"from": "…", "to": "…",
"days": 41}], "bursts": [{"bucket": "…", "messages": 0, "ratio_to_median": 4.2}], "continuations":
[{"from": "sha256:…", "to": "sha256:…", "gap_minutes": 12, "shared_terms": ["…"], "confidence": 0.7,
"inferred": true}]}`. `timeline.jsonl`: jeden wiersz na wiadomość (lub rozmowę przy kluczach
rozmowy): `{"t": "…Z", "t_src": "…", "kind": "message", "uid": "…", "conversation": "…", "service":
"…", "account": "…", "role": "…", "chars": 0, "locator": {...}}`, w porządku osi — plik jest już
posortowany, więc inne narzędzia czytają go strumieniowo.

### 5.14 Różnica — `diff.json`

`{"schema": "loom.extract.diff/1", "old": {"path": "…", "sha256": "…", "units": 0}, "new": {...},
"new_units": [{"uid": "…", "ext_id": "…", "title": "…"}], "changed": [{"ext_id": "…", "old_uid":
"…", "new_uid": "…", "messages": {"added": 0, "changed": 0, "removed": 0}, "title_changed": false,
"new_branches": 0, "new_attachments": 0}], "removed": [...], "unchanged": 0}` — w trybie
`--zbiorczo` listy są zastąpione licznikami.

### 5.15 Pakiet przekazania — `handoff/handoff.json`

`{"schema": "loom.extract.handoff/1", "from_manifest": "sha256:…", "profile": "handoff",
"files": [{"path": "conversations/0001.json", "sha256": "…", "conversations": 1, "messages": 42}],
"attachments": {"copied": 0, "missing": 0}, "graph": "graph/packet.json" | null, "import_hint":
{"loom": "loom import <ten katalog>", "python": "ConversationImporter.import_file(<ten katalog>)"}}`.
Kształt `conversations/*.json`: `{"title": "…", "created": "…", "messages": [{"role": "user",
"content": "…", "created": "…", "metadata": {"export": {"schema": "loom.export.v1", "provider": "…",
"locator": {...}, "blocks": [...], "branch": {...}, "status": "active"}}}], "metadata": {"export":
{...}, "extract": {"recipe": "sha256:…"}}}` — wyłącznie pola, które `engine.importer`
(`_import_conversation_obj` / `_import_message_list`) i importer C++ już rozumieją; reszta
w `metadata`.

## 6. Kaskada — semantyka

### 6.1 Ziarna

`conversation` | `branch` (ścieżka od korzenia do liścia) | `window` (N kolejnych wiadomości na
jednej ścieżce; preset N = 6, `0` = cała ścieżka) | `message`. Kryteria działają na ziarnie
z `grain`; wynik ziarna niższego propaguje w górę (`max` z wagą pokrycia: rozmowa, w której 1 okno
z 40 pasuje, dostaje wynik okna i pokrycie 0,025 — oba są widoczne i oba mogą być progami).

### 6.2 Wynik, pewność, źródło decyzji

Każdy poziom daje dla ziarna `{score ∈ [0,1], decision, confidence, reasons[]}`; `reasons` to
dopasowane słowa (z formą i pozycją), regexy, najbliższe zapytanie wektorowe z podobieństwem,
albo odpowiedź modelu (z jej `reason`). Łączenie (`combine`): `weighted_max` (preset),
`weighted_mean`, `vote`, `first_decisive`. Decyzja końcowa ma `source` = poziom lub `manual`,
i trafia do `explain.jsonl`. Pasma `bands` dzielą wynik na include/ambiguous/exclude; model
dostaje tylko pasmo środkowe (`on: ambiguous`).

### 6.3 Poziomy lokalne

- **L0 maski** — dokładne filtry: daty (po `created`, z tolerancją strefy), usługi i konta, role,
  nazwy narzędzi, tytuł regexem, obecność załączników/gałęzi, `agentic_only` (≥ 1 blok
  `tool_use`/`tool_result` lub rola `tool`), języki. Maska **wyklucza** albo przepuszcza; nie
  włącza (chyba że kaskada `masks_include`).
- **L1 leksykalny** — słowa (fleksja, składanie), regexy, wykluczenia; wynik = nasycona suma wag
  dopasowań znormalizowana długością okna; `boundary`, `fold` jak w kryteriach. Wyjaśnienie
  pokazuje do K pierwszych dopasowań (K preset, `0` = wszystkie).
- **L2 wektory** — `core.selector.SelectorEngine` na oknach (indeks per przebieg, osadzenia
  w pamięci podręcznej); poziom selektora (embeddings / TF-IDF / słowa) zapisany w manifeście
  i raporcie — wynik TF-IDF nie udaje osadzeń. Zapytań może być wiele (`vectors[]`), wynik =
  max podobieństwa; `threshold` i `candidates` to presety.

### 6.4 Poziomy z modelem

Przepływ dostaje okno (lub ziarno z `input.grain`) i kryteria w skrócie, oddaje `verdict`
z pewnością i powodem; odpowiedzi poza schematem = `ambiguous` z powodem „odpowiedź
niezgodna” (nigdy nie zgadujemy). Tani model (`class: cheap`) na całym paśmie, mocny
(`class: strong`) na tym, co zostało. Przebieg bez `--model` zostawia pasmo w raporcie z liczbą
i szacunkiem, ile kosztowałoby jego rozstrzygnięcie.

### 6.5 Sąsiedztwo (`expand`)

Presety: `none` | `window:N` (N przed i po) | `branch` | `conversation` (preset — R18: cała
rozmowa na temat) | `conversation+linked` (plus rozmowy, do których prowadzą odwołania: linki
współdzielone, ten sam projekt Claude, artefakty, pliki o tych samych hashach) |
`time_window:P` (wiadomości z innych rozmów w oknie czasu P). Opcja `strip_offtopic` dla rozmów
wielowątkowych: tylko okna z wynikiem powyżej pasma + ich sąsiedztwo, z notą, co wycięto.

### 6.6 Odkrywanie „innych małych projektów” (`klastry`)

Bez modelu: nazwy własne (tokeny wielką literą poza początkiem zdania, identyfikatory kodu,
nazwy plików i ścieżek, repozytoria), rzeczowniki powtarzalne (kontrast TF-IDF rozmowa vs
korpus, jak etap `expand` w `archive.h`), współwystępowanie w oknach → deterministyczne
grupowanie (propagacja etykiet z ustalonym porządkiem; `libloom` Louvain, gdy biblioteka jest).
Wyjście: lista kandydatów `{id, top_terms, sample_titles, conversations, period}` do
potwierdzenia; `--wybierz` zamienia klaster w kryterium (`clusters: [id]`, terminy jako słowa
kluczowe z wagami). Z `--model`: nazwy i opisy klastrów przepływem `summarize`.

### 6.7 Uczenie się z poprawek

`zaznacz` zapisuje decyzję i jej kontekst („było L2 ambiguous 0,52”). Narzędzie proponuje
(nigdy nie wprowadza) zmiany kryteriów: nowe formy słów z zaznaczonych rozmów, wykluczenia
z odznaczonych, korektę progów (gdy ≥ M poprawek w jednym kierunku; M preset). Propozycje
w raporcie i w `decisions.json#proposals`; `--przyjmij-propozycje` zapisuje je do nakładki.

### 6.8 Kanał-cień (R27)

Raport zawsze zawiera: co znalazł tylko kanał leksykalny, co tylko wektorowy, co model odrzucił
mimo trafienia leksykalnego. Te listy są materiałem do `zaznacz`, nie wyrokiem.

### 6.9 Język zaznaczania (jeden dla CLI, komandera i API)

Gramatyka jako dane (`data/grammar/select.json`), pola i ich typy per rodzaj węzła
w `data/select_fields.json`. Składnia:

| Forma | Znaczenie |
|---|---|
| `pole:wartość` | równość (tekst: zawiera, ze składaniem znaków, gdy `fold`) |
| `pole~/regex/flagi` | regex na dowolnym polu (także na kluczach surowego obiektu: `raw.author.name~/…/`) |
| `pole:a..b` | zakres (daty, liczby); `..b`, `a..` otwarte |
| `has:x` | obecność: `attachments`, `branches`, `thinking`, `tools`, `citations`, `unknown` |
| `is:x` | cecha: `agentic`, `sidechain`, `compacted`, `inferred`, `duplicate`, `continuation`, `deadend`, `current_path` |
| `count(rodzaj) OP n` | liczba bloków/wiadomości/gałęzi: `count(tool_use) >= 2`, `count(messages) < 4` |
| `len OP n`, `words OP n`, `depth OP n`, `forks OP n` | długości i kształt drzewa |
| `~"tekst"` | podobieństwo wektorowe do tekstu (próg z presetu lub `~"tekst"@0.5`) |
| `like:UID` | „więcej takich” — podobieństwo do jednostki |
| `near:UID±N` | N wiadomości wokół jednostki (na tej samej ścieżce) |
| `around:DATA±N[d\|h]`, `week-of:"X"`, `gap:>Nd` | czas (§6.11) |
| `cluster:id`, `band:ambiguous`, `tag:x`, `label:x`, `source:nazwa`, `account:x`, `service:x`, `lang:pl`, `model:x`, `tool:x`, `role:x`, `kind:x` | pola wyboru z §8 (15) |
| `AND`, `OR`, `NOT`, `( )` | składanie; domyślny spójnik między termami: `AND` (preset) |

Aliasy polskie w gramatyce (`tytuł:`, `data:`, `rola:`, `narzędzie:`, `usługa:`, `konto:`, `ma:`,
`jest:`, `I`, `LUB`, `NIE`). Każde dopasowanie zwraca `reason` z listą termów, które zadecydowały,
i wartościami, które pasowały (`title~/chat\s*adhd/i → „ChatADHD: pamięć”`). Wyrażenie jest też
zakresem operacji edycji (`where`) i filtrem widoku; `filtr zapisz` nadaje mu nazwę.

### 6.10 Wzorce strukturalne

Wbudowane (`data/select_fields.json#patterns`, lista otwarta — wzorzec to wyrażenie z §6.9
z nazwą): `agentic` = `count(tool_use) >= 1 OR role:tool`; `heavy_tools` = `count(tool_use) >= 2`;
`deadend` = gałąź kończąca się wiadomością użytkownika albo blokiem `error`; `edited_branch` =
`forks > 0`; `long_thinking` = `count(thinking) >= 1 AND thinking_chars >= 200`; `no_text` =
`has:thinking AND NOT has:text`; `missing_attachments` = `attachments.missing > 0`; `multi_turn` =
`count(messages) >= 10`; `cross_service` = `is:continuation`. Użytkownik dopisuje własne w nakładce;
komander pokazuje je w menu „Zaznacz wzorcem” z licznikami.

### 6.11 Zaznaczanie czasem

`created:2025-06..2025-07` (okno), `around:2025-06-14±3d` („wokół tej daty ± N”), `week-of:"pamięć
hierarchiczna"` (kubełek tygodnia, w którym termin miał najwyższy wynik leksykalny — obliczany
z `timeline.json`; przy remisie wszystkie tygodnie, raport mówi które), `gap:>30d` (jednostki
bezpośrednio po luce), `burst:` (jednostki w seriach), `updated:..2024-12`. Czas porównywany w UTC
z `t` z §3.13, więc wynik nie zależy od strefy maszyny.

## 7. Co zawiera, a czego nie zawiera każde źródło

Stan wiedzy: ChatGPT i Claude z opisu formatów (`importer.h`, `export_internal.h`,
`gen_export_fixtures.py`) sprawdzonego na syntetycznych fixture'ach; Claude Code zmierzone na
transkrypcie lokalnym (rodzaje wierszy i bloków); Gemini i Grok z wiedzy ogólnej — **hipotezy do
potwierdzenia inwentarzem na pierwszym prawdziwym pliku**; ich opisy są plikami danych.

| Źródło | Kontener | Jednostki | Drzewo / gałęzie | Bloki | Załączniki | Czasy | Czego nie ma |
|---|---|---|---|---|---|---|---|
| **ChatGPT** eksport danych | zip: `conversations.json` (tablica lub `{"conversations":[…]}`, od 2026 szardy `conversations-NNN.json`), `user.json`, `message_feedback.json`, `shared_conversations.json`, `model_comparisons.json`, `chat.html`, `textdocs/`, katalogi grupowe/sora; pliki `file-*` w korzeniu, `dalle-generations/`, `user-*/`, `.dat` | rozmowa z `mapping` (węzły id→parent/children/message), `current_node`; konto; opinie; linki współdzielone; canvas | pełne drzewo; edycje = rodzeństwo; bieżąca ścieżka znana | `text`, `multimodal_text` (parts), `code` + `execution_output`, `tether_*` (przeglądanie), `thoughts`/`reasoning_recap` (streszczenia), `image/audio_asset_pointer`, `audio_transcription`, `user/model_editable_context`, `system_error`; role `user/assistant/system/tool` z `author.name`; `recipient` | wskaźniki `file-…` w `asset_pointer` i `metadata.attachments`; pliki w kilku układach; nie każdy rozwiązany (DALL·E, wygasłe) | epoch (s, ułamkowe) per wiadomość i rozmowa | pełne rozumowanie; prompt systemowy; treści stron www; projekty jako struktura (tylko metadane rozmowy); niektóre załączniki |
| **Claude.ai** eksport danych | zip: `conversations.json` (tablica rozmów), `projects.json`, `memories.json`, `users.json`; załączniki/pliki per wiadomość (`attachments`, `files`, `files_v2`), artefakty | rozmowa `chat_messages[]` z `uuid`, `parent_message_uuid`, `current_leaf_message_uuid`; projekt z dokumentami; pamięci; konto | drzewo przez `parent_message_uuid` (brak rodziców = liniowo); bieżący liść znany | `text`, `thinking`, `tool_use`, `tool_result`, `image`, `document`, `voice_note`, nieznane przyszłe; `sender: human/assistant` | wymienione per wiadomość; treść wyciągnięta bywa w `extracted_content`; binaria nie zawsze w zipie | ISO 8601 per wiadomość i rozmowa | sesje Claude Code w chmurze, Cowork; pełność `thinking` zmienna; ciała wyników wyszukiwania częściowe |
| **Claude Code** lokalnie | katalog `~/.claude/projects/<projekt>/<sesja>.jsonl` (+ `summary` w osobnych wierszach, pliki `.jsonl` podagentów) | sesja = rozmowa; wiersze `user`/`assistant`/`system`/`summary`; `isSidechain` = podagent; `compact` = streszczenie kontekstu | łańcuch `uuid`/`parentUuid` (rozgałęzienia przy wznowieniach), `logicalParentUuid` po kompaktowaniu | `text`, `thinking` (pełne lub zredagowane), `tool_use` (pełne wejście), `tool_result` + `toolUseResult`, `image`; `cwd`, `gitBranch`, `version` per wiersz | ścieżki plików w wejściach narzędzi (odwołania do dysku, nie kopie — `attachments.copy_tool_paths` preset `off`) | ISO 8601 per wiersz; `cost-state`/`modelUsage` — koszt sesji | sesje bez transkryptu na dysku; wiersze wewnętrzne (`queue-operation`, `attachment`, `mode`, `atis-latch`, …) → kubełek „wewnętrzne” z przykładem |
| **Gemini** Takeout | zip Takeout: `Takeout/My Activity/Gemini Apps/MyActivity.html` lub `.json`; pliki załączników obok | wpisy aktywności: `title` („Prompted …”), `time`, `products`, `subtitles`/`details`; w HTML prompt i odpowiedź jako tekst | **brak**; rozmowy rekonstruowane po odstępie czasu (preset 30 min; wynik oznaczony `inferred`) | tekst promptu i odpowiedzi; obrazy jako pliki | pliki obok z nazwą we wpisie (do zmierzenia) | ISO 8601 per wpis | id rozmów, gałęzie, rozumowanie, narzędzia; Gems; AI Studio (osobny JSON w Drive — osobny plik formatu, gdy pojawi się przykład) |
| **Grok** | grok.com: zip/JSON z rozmowami i odpowiedziami (`conversation{id,title,create_time}`, `responses[]` z `sender`, `message`, `create_time` — hipoteza); archiwum X: `data/grok-chat-item.js` | rozmowa, wiadomość | prawdopodobnie liniowo | tekst; może odnośniki do wyszukiwania | do zmierzenia | do zmierzenia | rozumowanie, DeepSearch jako ślad; wszystko do potwierdzenia |
| **nasz graf / baza** | `loom.graph_packet/1` (JSON) i `chatadhd.db` (`messages.metadata.export.raw` = pełny obiekt źródłowy, `status` active/excluded/version, `version_group_id`) | rozmowa, wiadomość, węzły, krawędzie | pełne (z `parent_id`, wersje) | bloki z `metadata.export.blocks` | `messages.attachments` (ścieżki blob) | ISO 8601 | — (źródło najpełniejsze; `preserve` odtwarza oryginalny format z `raw`) |
| **Markdown / tekst / HTML / MHT / JSONL ról / SQLite** | plik | rozmowa z ról heurystycznych (`engine.importer`) | brak | `text` | brak (linki w tekście jako `citation`) | tylko gdy w treści (regex dat jako dane) | wszystko poza tekstem; role to heurystyka — oznaczone `inferred` |
| **katalog mieszany** | katalog z dowolnymi plikami | każdy plik rozpoznany z treści osobno; nieznane → `member` | — | — | — | mtime pliku jako ostateczność, oznaczone | — |
| **zrzut ekranu** (PNG/JPG) | plik | rozmowa z OCR przez `engine.importer.import_screenshot` → `_ocr_image` (dostawca z `engine.providers`, gdy skonfigurowany; inaczej `member` z notą „OCR niedostępny”) | brak | `text` (OCR, `inferred`) | sam plik jako załącznik | mtime, oznaczone | wszystko poza tekstem; OCR zdalny = sieć → wymaga jawnego włączenia jak model (§2) |

Czytnik zaznacza każdą wywnioskowaną wartość (`inferred: true`, R5), a `formaty PLIK` wypisuje
tę tabelę dla konkretnego pliku z liczbami.

Sprawdzono 2026-10-16 (celowane grepy, bez czytania całych plików): `engine/importer.py` ma handlery
`import_zip/jsonl/sqlite/json/html/mht/screenshot/markdown/text`, `_import_chatgpt_mapping` idzie
tylko „aktywną ścieżką” i spłaszcza (stąd własne przejście bezstratne), `_import_claude_export`
czyta `chat_messages`, `_import_claude_db`/`_import_chatgpt_db` obsługują SQLite (drugi jest
szkieletem — inwentarz zgłosi `member`), `_iter_json_elements` strumieniuje tablice;
`loom/src/archive/ingest_parse.cpp` ma `walk_tree` z `current_node` / `current_leaf_message_uuid`,
rozwidlenia z alternatywami i `forks` w metadanych; `docs/OWNER_REAL_EXPORT_GUIDE.md` potwierdza
zip ChatGPT i Claude, JSON z `--export-mode on` i liczniki audytu (rozmowy, wiadomości, gałęzie,
rozwidlenia, `leaves_preserved` vs `json_leaves`, wskaźniki załączników rozwiązane/nierozwiązane,
członkowie nieznani/nieodwołani, `partial`, naprawy) — inwentarz narzędzia podaje te same
liczniki pod tymi samymi nazwami, żeby dało się je porównać z audytem `loom import`.

## 8. Wymagania 1–36 → mechanizm, preset, odwrócenie

Lista rozszerzona przez S1 z list właściciela i filozofii (§0.1); każde wymaganie to preset, nie
reguła; minimalna konfiguracja = „wskaż archiwum i nazwij temat”.

| # | Wymaganie | Mechanizm | Preset | Odwrócenie |
|---|---|---|---|---|
| **Źródła** | | | | |
| 1 | formaty dostawców + nasz graf + Markdown/tekst + katalog mieszany; format rozpoznany z treści, nie z rozszerzenia | `formats/*.json`, `sources/`, czytniki §3.4 | wszystkie formaty włączone; rozszerzenie tylko jako kolejność prób | `disabled: true` w nakładce formatu; `--format ID` wymusza |
| 2 | wiele archiwów naraz, także kilka eksportów tego samego konta z różnych dat: scalanie po tożsamości, duplikaty i wersje, najnowsza z historią; **„co nowego od ostatniego eksportu”** | `merge.py` (usługa, `ext_id` → hash), `versions[]`; `diff.py`, `roznica`, `--od-manifestu` (§3.16) | najnowsza aktywna, starsze w `versions[]`; usunięte wymienione | `merge.strategy: keep_all`; `diff.mode: all` |
| 3 | strumieniowe czytanie plików GB; przebiegi wznawialne | §3.6–3.7, `state.json` | cache w pakiecie, `workers: auto` | `--cache off`, `--workers 1` |
| 4 | **inwentarz** jako osobny raport: rodzaje bloków, narzędzia, gałęzie, załączniki, zakres dat, języki, luki, czego format nie zawiera — zmierzone, nigdy zgadywane | `inventory.py`, `inwentarz`, `known_absent` + obserwacja (§5.7); te same nazwy liczników co audyt `loom import` | zawsze pisany; `how` przy każdej liczbie | `inventory: false` |
| 5 | załączniki: odwołania zachowane; kopiowane tylko użyte; tekst wyciągnięty opcjonalnie (istniejące OCR/ASR); brakujące flagowane, nigdy pomijane po cichu; przeglądarka (typ, rozmiar, rozmowa) | `cut/*`, `attachments.mode`, `zalaczniki`, widok `attachments` (§3.14) | `copy_used`; tekst `reference`; brakujące w raporcie i manifeście | `copy_all` / `reference_only`; `extract_text: true` |
| **Czas** | | | | |
| 6 | **globalny porządek po czasie** przez wszystkie archiwa i usługi: jedna oś wiadomości/rozmów; klucze jako presety (created, updated, pierwsza, ostatnia); remisy stabilne; rozmowa zaczęta w ChatGPT i kontynuowana w Claude obok siebie; luki i serie; kubełki dzień/tydzień/miesiąc; jedna strefa z zachowaniem oryginału; przesunięcie/anonimizacja | `timeline.py`, `os-czasu`, profil `timeline`, widok `timeline`, `edit.sort` (§3.13, §5.13) | klucz `created`; remis `(t, usługa, archiwum, lokalizator)`; kubełek `tydzień`; luka > 30 dni; seria > 3× mediana; `zone: source`; kontynuacja: okno 24 h + ≥ 3 wspólne terminy | `--klucz`, `timeline/presets.json` (`gaps: 0` = nie raportuj), `zone`, `shift`, `anonymise`, `continuations: off` |
| 7 | wybór oknem czasu, „wokół tej daty ± N”, „tydzień, w którym mówiono o X” | wyrażenia `created:a..b`, `around:D±N`, `week-of:"X"` (§6.11) | — | — |
| **Wybór** | | | | |
| 8 | słowa z polską fleksją i składaniem znaków, regexy, maski (tytuł/data/rola/narzędzie/usługa/konto), listy include/exclude, składanie AND/OR/NOT z grupami | `lexical.py`, `criteria.py`, język wyrażeń (§6.9) | `inflect: rules`, `fold: true`, domyślny spójnik AND | per słowo `inflect: off`; `fold: false` |
| 9 | wektory tym samym selektorem trójpoziomowym co aplikacja; próg i liczba kandydatów jako presety; „dlaczego pasuje” | `vectors.py` → `core.selector` | próg 0,35; kandydaci 0 (= bez cięcia); poziom selektora w manifeście | `vectors: []`, `vectors.engine` |
| 10 | filtr opisany słownie → model przepisuje na kryteria (słowa, regexy, zapytania wektorowe), pokazuje, użytkownik poprawia; kryteria zapisane jako dane | `opisz`, przepływ `describe_criteria`; bez modelu gramatyka regułowa | model wyłączony; `grammar/pl.json` | `--model` |
| 11 | kaskada przepływów według klasy modelu: tani na szerokim paśmie, mocny tylko na niejednoznacznych; każdy przepływ = dane; użytkownik dodaje własne | `cascades/`, `flows/` (§5.3–5.4) | L3 tani na `ambiguous`, L4 wyłączony | inna kaskada; `enabled` w nakładce |
| 12 | ziarno: cała rozmowa, gałąź, okno wiadomości, wiadomość, blok wewnątrz wiadomości (narzędzie, rozumowanie, załącznik); wynik z pewnością i źródłem decyzji per poziom | §6.1–6.2, `explain.jsonl`, `wyjasnij` | `conversation` | `grain` |
| 13 | sąsiedztwo: N przed/po, cała gałąź, cała rozmowa — per preset | `expand.py` (§6.5) | `conversation` | `expand: none` / `window:N` |
| 14 | odkrywanie „innych małych projektów”: klastry (nazwy własne, rzeczowniki powtarzalne, pliki) → kandydaci do potwierdzenia → wybór klastrem | `klastry`, `cluster.py` (§6.6), widok `clusters` | bez modelu | `--model` dla nazw |
| 15 | wybór po koncie/usłudze, tylko agentowe, tylko z załącznikami, tylko z gałęziami, po języku, po modelu, który odpowiadał, po długości | maski L0 + pola `service`, `account`, `is:agentic`, `has:attachments`, `has:branches`, `lang`, `model`, `len` | puste = wszystko | — |
| 16 | próba na sucho: co weszłoby, liczniki i koszt — bez pisania | `szukaj --na-sucho`, `koszt`, `--plan` | — | — |
| 17 | zaznaczanie ręczne w komanderze: zaznacz/odznacz/odwróć/„więcej takich”; zapisane jako dane i stosowane w następnym przebiegu (uczenie z poprawek); zakładki, etykiety, zapisane widoki | `zaznacz`, `decisions.json`, `select.similar`, `bookmarks`/`tags`/`views` w przepisie (§5.10), §6.7 | `manual: wins` | `manual: advisory` |
| **Komander (X-Port Commander)** | | | | |
| 18 | dwa panele, każdy drzewo lub lista płaska dowolnej struktury (archiwa → rozmowy → gałęzie → wiadomości → bloki → załączniki; pakiet; przepis); otwórz, podgląd F3 (JSON/Markdown/surowy), kopiuj F5, przenieś F6, grupa F7, usuń z pakietu F8, zmień nazwę, porównaj dwa węzły | `commander/` (§3.14), `keymap.json` | profil `fkeys` | profil `vim` lub własny (`inherits`) |
| 19 | **zaawansowane zaznaczanie**: wyrażeniem (ten sam język co CLI), regexem na dowolnym polu, wzorcem strukturalnym („≥ 2 wywołania narzędzi”, „ślepa gałąź”), podobieństwem, czasem; liczniki i powód per pozycja | `select_expr.py`, operacje `select.*` (§6.9–6.11) | wzorce wbudowane z `select_fields.json` | nakładka |
| 20 | **filtrowanie** widocznej listy na żywo (przyrostowe), zapisane filtry | `filter.live` w `CommanderState`, `filtr zapisz` | po tytule/tekście/roli; odświeżenie po każdym znaku (`debounce_ms: 0`) | `filter.fields`, `debounce_ms` |
| 21 | **edycja seryjna**: wartości (pole = wartość, regex, mapa), nazwy kluczy/pól, struktura (spłaszcz drzewo → lista po ścieżce, zagnieźdź, podziel rozmowę datą, scal rozmowy, uporządkuj po czasie, usuń/zostaw rodzaje bloków, przenieś załączniki, przemianuj role) — każda edycja krokiem przepisu z podglądem różnic, na kopii, cofalna, w manifeście | `edits/`, `oplog.py`, `edytuj`, `cofnij`/`ponow` (§3.15, §5.12) | podgląd zawsze; próbka K = 20 | `--tak` (bez pytania, podgląd zostaje w dzienniku); `preview_sample: 0` |
| 22 | klawisze i układ jako dane (keymap, kolumny per rodzaj węzła); teksty pl/en w tablicy | `data/commander/*.json`, `i18n` (§5.11) | `fkeys` + `vim` dostarczone; kolumny z `layout.json` | nakładka użytkownika |
| 23 | szukaj w obu panelach; „idź do” po id/dacie; okruszki; wiersz stanu z licznikami, rozmiarem zaznaczenia, szacowanym rozmiarem pakietu i kosztem modelu | `CommanderState` (§3.14) | elementy wiersza stanu z `layout.json` | kolejność i zestaw w nakładce |
| 24 | TUI nigdy nie blokuje się na długiej pracy (wątek + postęp); nigdy nie gubi przepisu po awarii (autozapis) | wątek roboczy, kolejka zdarzeń, `autosave.py` | autozapis po każdym kroku, zapis atomowy | `autosave.every: N` kroków / `off` |
| **Wycinanie i przebudowa** | | | | |
| 25 | format źródłowy 1:1 (ten sam JSON/zip, mniej rozmów) z poprawnymi odwołaniami do załączników w pakiecie; importuje się z powrotem do aplikacji źródłowej lub do nas | profil `preserve` (bajty z lokalizatora, §5.5) | profil domyślny | inne profile |
| 26 | profile przebudowy jako dane: `flat`, `structure`, `reasoning`, `text`, `graph`, `timeline`, własne z pól i reguł | `profiles/*.json` (§5.5) | `preserve` + `text` | `profiles: [...]` |
| 27 | gałęzie: drzewo albo spłaszczenie do ścieżki (najnowsza, najdłuższa, zaznaczona) z notą, co wycięto | `branches.keep`, `edit.flatten` | `tree` w `preserve`, `current` w `flat` | per profil / per krok |
| 28 | redakcja: usuń lub zamaskuj dane osobowe, klucze, adresy — maskami i słownikiem; raport, co zamaskowano; nic nie znika bez wpisu | `redact.py`, `redaction/*.json`, `edit.redact` | `none` | profile `personal`, `keys`, własny słownik |
| 29 | wyjścia: JSON/JSONL, Markdown, CSV wiadomości, nasz pakiet grafu, zip z załącznikami; nazwy i kodowanie przewidywalne; **przekazanie do importera aplikacji jednym krokiem** | `outputs/`, `handoff.py`, `przekaz` (§3.17, §5.15) | wg profilu; `handoff/` przy `--przekaz` | `outputs[]`; `handoff: always \| never` |
| 30 | manifest pakietu: źródła (ścieżka, hash, data), kryteria, przepis, decyzje, statystyki, koszt modelu, wersja narzędzia; pakiet odtwarzalny z manifestu | `manifest.py`, `odtworz` (§5.8) | zawsze | — |
| 31 | statystyki i streszczenia per rozmowa i łącznie (wiadomości, słowa, narzędzia, języki, okresy), oś czasu tematu, streszczenia modelem opcjonalnie | `inventory.py`, `report.py`, `statystyki`, profil `cards`, przepływ `summarize` | bez modelu | `--model` |
| **Działanie** | | | | |
| 32 | zero sieci domyślnie; model tylko po jawnym włączeniu, z szacunkiem kosztu, strażnikiem ×10 i limitami; dziennik każdego wywołania | §3.8, `log.jsonl` | wyłączone; strażnik 10×; limity 0 (= brak) | `--model`; nakładka |
| 33 | **zdanie, plik i ekran**: ten sam przebieg z CLI („wyciągnij z archiwum X wszystko o ChatADHD w profilu płaskim”), z pliku zadania/przepisu, z komandera i przez API; później akcja czatu i port | §3.11–3.12, §4.5, §3.10 | — | — |
| 34 | determinizm i testy: te same wejścia + przepis → ten sam pakiet; syntetyczne archiwa każdego formatu z przypadkami brzegowymi (gałęzie, narzędzia, brakujące załączniki, duplikaty, mieszane strefy, dwa eksporty jednego konta); prawdziwe eksporty właściciela nigdy w testach ani repo | §9, `reproducible_hash`, strażnik ścieżek, `--zbiorczo` | — | — |
| 35 | rozszerzalność: nowy format, profil, przepływ, operacja edycji, keymap = pliki danych (dla parsera lub edycji dodatkowo jeden moduł Pythona zarejestrowany wpisem) pod `loom/tools/extract/data/`, bez zmiany rdzenia; nieznane rodzaje bloków do kubełka `unknown` z przykładem; hooki na funkcje użytkownika z przepisu | §3.5, `reader: "user:x"`, `edits/*.json#module`, `op: user.call` (§3.12) | kod użytkownika ładowany tylko po jawnym wskazaniu; raport mówi, że jest użytkownika | — |
| 36 | raport po polsku, w słowach właściciela: co znaleziono, czego nie dało się odczytać, co zamaskowano, ile kosztowało, co zmieniono | `RAPORT.md`, i18n (§5.9) | `pl` | `--jezyk en` |

### 8.1 Rozszerzenia z filozofii (37–64; dawne 29–56)

Wiersze oznaczone *(⊂ §8: n)* są już zawarte w wymaganiu n powyżej i zostają tu jako
uszczegółowienie mechanizmu.

| # | Rozszerzenie | Mechanizm / preset |
|---|---|---|
| 37 | **Wiele tematów w jednym przebiegu** — jeden `scan`/`read`, wiele kryteriów → wiele pakietów | `tasks: [...]` w pliku zadania; wspólna pamięć podręczna |
| 38 | **Ekstrakt przyrostowy** — tylko to, co nowe od ostatniego manifestu (nowy eksport tego samego konta) *(⊂ §8: 2)* | `--od-manifestu plik` → różnica jednostek po hashach |
| 39 | **Obserwacja katalogu** (R20) — nowe pliki eksportu w katalogu → propozycja przebiegu | `obserwuj KAT` (pętla, bez sieci); później zadanie cykliczne aplikacji |
| 40 | **Proweniencja w każdym wyjściu** — każda wiadomość w `flat`/`text`/`graph` ma lokalizator źródła | pole `source` / stopka Markdown; wyłączalne |
| 41 | **Mapa prywatności przebiegu** (R19) — co opuściło maszynę (które okna, do którego dostawcy), co zostało | sekcja raportu „co wyszło na zewnątrz”; tryb `paranoid` = brak `--model` i jawne ostrzeżenie o OCR/ASR zdalnym |
| 42 | **Próbka do sprawdzenia** — losowa (z ziarnem w manifeście) lista K rozmów z każdego pasma do ręcznej weryfikacji jakości | `--probka K`; wyniki weryfikacji wracają jako `decisions` |
| 43 | **Słownik tematu** — terminy nauczone z zaznaczonych rozmów (nazwy, pliki, identyfikatory) jako plik do ponownego użycia | `glossary.json` w pakiecie; `--przyjmij-slownik` dopisuje formy do kryteriów |
| 44 | **Scalanie tożsamości kont** — to samo konto w kilku eksportach (`user.json`, `users.json`) → jedna etykieta; dane konta redagowane osobno | `accounts.json` w nakładce; maska `accounts` |
| 45 | **Cytaty i wyniki wyszukiwania jako bloki** — zachowane w `preserve`, opcjonalnie w profilach | blok `citation`/`search_result`; `blocks.citation: keep` |
| 46 | **Budżet rozmiaru pakietu** — docelowy rozmiar (bajty, tokeny) z polityką: najpierw tnij załączniki, potem rozumowanie, nigdy tekst | `budget.bytes`, `budget.tokens` (`0` = bez cięcia); raport mówi, co odpadło |
| 47 | **Kodowanie i normalizacja** — NFC/NFD, końce linii, BOM w CSV, cudzysłowy | ustawienia `encoding.*` (preset: NFC, `\n`, bez BOM) |
| 48 | **Szybki rzut oka** — `--probkuj N` czyta tylko N pierwszych rozmów każdego archiwum, by zobaczyć kształt | wynik oznaczony `partial: true` |
| 49 | **Test obiegu zamkniętego** — pakiet `preserve` importuje się przez `engine.importer`/`libloom` i daje te same liczniki *(⊂ §8: 29, §9)* | test w §9; `wytnij --sprawdz-import` |
| 50 | **Wyjaśnij decyzję** — pełna ścieżka dla jednej rozmowy *(⊂ §8: `wyjasnij`, 12)* | `wyjasnij` |
| 51 | **Karty rozmów** — streszczenie, decyzje, otwarte pytania per rozmowa (modelem) | przepływ `summarize`, profil `cards`; wyłączony |
| 52 | **Własny czytnik użytkownika** — plik formatu + moduł Pythona w nakładce (`<dane>/extract/readers/x.py`) ładowany tylko po jawnym wskazaniu w pliku formatu *(⊂ §8: 35)* | `reader: "user:x"`; raport mówi, że czytnik jest użytkownika |
| 53 | **Równoległość i postęp** — `workers`, pasek postępu z ETA na stderr, `--cicho` | preset `auto` |
| 54 | **Format dat w Markdown** wg lokalizacji | `text.date_format` (preset ISO) |
| 55 | **Oś czasu tematu** — miesiące × liczba rozmów/wiadomości per klaster; `timeline.json` + tabela w raporcie *(⊂ §8: 6, 31)* | zawsze przy `klastry`/`wytnij` |
| 56 | **Wersje rozmów między eksportami** — co doszło/zmieniło się między datami eksportu *(⊂ §8: 2)* | `versions[]`, sekcja raportu „zmiany między eksportami” |
| 57 | **Duplikaty w obrębie archiwum** (R50) — te same treści pod różnymi id (link współdzielony + rozmowa) | flagowane, nigdy usuwane; `dedup.within_archive: flag` |
| 58 | **Rozmowy powiązane** — projekt Claude, wspólny plik, link współdzielony, kontynuacja („jak w poprzedniej rozmowie…” — tylko regex z danych, oznaczone `inferred`) | `expand: conversation+linked` |
| 59 | **Polityka nieznanego** (R44) — kubełek `unknown` z przykładem i liczbą per rodzaj; `preserve` kopiuje je bajt w bajt *(⊂ §8: 35)* | zawsze |
| 60 | **Zgodność z importerem aplikacji** — model wspólny ↔ `loom.export.v1` (nazwy pól: `blocks`, `attachments`, `pointers`, `citations`, `status`) *(⊂ §8: 29)* | test parytetu nazw pól |
| 61 | **Przebieg wsadowy** — `--zadania KAT` wykonuje każdy plik zadania po kolei, jeden raport zbiorczy | — |
| 62 | **Wyjście do aplikacji** — pakiet jako katalog gotowy do `loom import` / panelu importu Androida (R35) *(⊂ §8: 29)* | profil `preserve` + `graph` |
| 63 | **Osobne pakiety per źródło lub jeden wspólny** | `package.split: by_source|single` (preset `single`) |
| 64 | **Etykiety ręczne** — `zaznacz --etykieta "agent"` nadaje jednostce etykiety; profile i maski mogą po nich filtrować *(⊂ §8: 17)* | `labels` w `decisions.json` |

### 8.2 Rozszerzenia z filozofii komandera i osi czasu (65–80; nowe w tej redakcji)

| # | Rozszerzenie | Mechanizm / preset |
|---|---|---|
| 65 | **Własne profile klawiszy** z dziedziczeniem, sprawdzanie kolizji, pomoc generowana z keymapu | `keymap.json#profiles.*.inherits`; `keymap.py`; F1 |
| 66 | **Tryb bez kolorów / ASCII / czytnik ekranu** | `colors.json#mono`; `--kolory mono` |
| 67 | **Nagranie sesji jako samouczek** — `komander --odtworz` odgrywa przepis krok po kroku z opisem akcji | `driver.py` + `actions.json#label` |
| 68 | **Porównanie dwóch pakietów** (lub pakietu z archiwum) tym samym `roznica` | `diff.py` nad drzewami pakietów |
| 69 | **Makra** — nazwane fragmenty przepisu wywoływane jednym klawiszem | `recipes/macros/*.json`; akcja `macro.run` |
| 70 | **Walidacja pakietu** — schematy, odwołania, hashe, importowalność `handoff/` | `sprawdz --pakiet` |
| 71 | **Zaznaczenie jako plik** — eksport listy uid do innych narzędzi i import listy jako zaznaczenie | `selection.jsonl`; `zaznacz --z-pliku` |
| 72 | **Tryb `--zbiorczo`** — liczby bez treści (DIP26) dla przebiegów agentowych na prawdziwych eksportach | §2; testowany na fixture'ach |
| 73 | **Statystyki wersji** — ile dopisano między eksportami tej samej rozmowy | `versions[]` + `diff` w raporcie |
| 74 | **Plan przebiegu** — kroki przepisu z szacunkiem czasu i kosztu przed wykonaniem | `--plan` |
| 75 | **Pola obliczane w widokach** (słowa, liczba narzędzi, udział rozumowania) jako wyrażenia w układzie | `layout.json#computed` |
| 76 | **Podgląd edycji na próbce** przed pełnym zastosowaniem | `preview_sample` (preset 20, `0` = całość) |
| 77 | **„Dlaczego nie”** — wyjaśnienie, czemu jednostka nie weszła (maska, filtr, pasmo, decyzja ręczna) | `wyjasnij --nie ID`; w komanderze `?` na pozycji |
| 78 | **Zaznaczenie jako wejście innego przepisu** | `sources: [{"selection": "…/selection.jsonl"}]` |
| 79 | **Schowek komandera** — kopiuj/wklej węzły i kroki między widokami i sesjami | akcje `copy`/`paste` na `clipboard.json` w pakiecie |
| 80 | **API widoku = panel** — `GET /api/view` zwraca dokładnie to, co rysuje komander, więc Warsztat webowy podmienia tylko renderer | §3.18 |

## 9. Testy i bramka

- `loom/tools/extract/tests/test_*.py` (unittest, bez sieci — strażnik gniazd w `setUp` blokuje
  każde połączenie poza pętlą zwrotną): rozpoznanie z treści (każdy format, zła końcówka, zip
  w zipie), bezstratne przejście drzew wobec `tests/fixtures/exports/EXPECTED.json` (ta sama
  wyrocznia co `test_import_exports.cpp`), przechwycenie `engine.importer` dla formatów tekstowych
  wobec `tests/fixtures/import/`, nowe fixture'y z `gen_extract_fixtures.py` (Gemini HTML + JSON,
  Grok dwa kształty, Claude Code z narzędziami/sidechainem/kompaktowaniem/nieznanymi wierszami,
  pakiet grafu, katalog mieszany, **dwa eksporty tego samego konta z różnych dat** — z rozmową nową,
  zmienioną, usuniętą i bez zmian, **kontynuacja ChatGPT→Claude** w oknie czasu, **mieszane strefy
  i epoch/ISO**, brakujący załącznik, duplikat), inwentarz (liczby = wyrocznia), kaskada (pasma,
  wyjaśnienia, decyzje ręczne wygrywają, cień), `preserve` bajt w bajt dla wybranych rozmów + obieg
  zamknięty przez `engine.importer`, profile, redakcja z raportem, czasy, manifest i `odtworz`
  (dwa przebiegi → identyczny `reproducible_hash`), zdanie → zadanie, i18n (każdy klucz w pl i en,
  także teksty komandera), model: atrapa trasy `provider` (lokalny serwer HTTP w teście) —
  szacunek przed wysyłką, odmowa bez `--model`, strażnik ×10, dziennik, pamięć podręczna bez drugiego
  wywołania.
- **Przepisy i determinizm** (`test_recipe.py`): schemat, normalizacja (dwa zapisy tego samego
  przepisu → jedna postać kanoniczna), ten sam przepis dwa razy → ten sam `reproducible_hash`,
  niezależnie od `workers`; kroki z modelem odtwarzają się z pamięci podręcznej, a bez niej
  zatrzymują z pytaniem (zdarzenie `question`), nigdy nie wołają sieci.
- **Parytet trzech twarzy** (`test_faces_parity.py`): ten sam przepis przez `cli.main([...])`,
  przez sterownik bezgłowy komandera (lista klawiszy z `keymap.json`, oba profile) i przez `serve`
  (serwer w wątku, `127.0.0.1`, token) → trzy identyczne `reproducible_hash` i identyczny
  znormalizowany przepis.
- **Komander bezgłowo** (`test_commander_state.py`): `CommanderState` sterowany listami klawiszy bez
  importu `curses`: nawigacja, przełączanie paneli, zaznaczanie (toggle/invert/wyrażenie/„więcej
  takich”), kopiuj/przenieś/grupa/usuń, zmiana nazwy, porównanie węzłów, filtr żywy (wynik po każdym
  znaku), szukaj, idź do, zakładki, etykiety, zapisane widoki, cofnij/ponów, nagrywanie (sekwencja
  klawiszy → oczekiwany przepis; akcje nawigacyjne nie nagrywane), autozapis po każdym kroku i
  wznowienie z `komander.autosave.json`, przerwanie długiej operacji (`Esc`) z zachowaniem stanu,
  pytanie o koszt kroku płatnego → odpowiedź „nie” jako preset. Sekwencje klawiszy w testach
  pochodzą z `keymap.json`, więc zmiana keymapu nie psuje testów logiki.
- **Komander z curses** (`test_commander_curses.py`): tylko gdy `curses` da się zaimportować
  i jest pseudoterminal (`os.openpty`, `TERM=xterm-256color`); inaczej `skipTest("curses lub pty
  niedostępne: <powód>")` — powód widoczny w wyjściu ctest. Test uruchamia renderer w podprocesie
  na fixture'ach, wysyła kilka klawiszy przez pty, sprawdza brak wyjątku, zapis autozapisu i że
  ekran pomocy zawiera każdą akcję z profilu.
- **Keymap i układ jako dane** (`test_keymap.py`): każdy klawisz → istniejąca akcja; brak kolizji
  w profilu (także prefiksy sekwencji); każda akcja ma etykietę pl i en; `layout.json` wymienia
  kolumny tylko dla istniejących pól; nakładka użytkownika z `inherits` działa.
- **Edycja seryjna** (`test_edits.py`): dla każdej operacji wbudowanej `preview` = różnica po
  `apply`; `apply` + `invert` = identyczność (hash drzewa); migawka, gdy odwrotność nieobliczalna;
  `oplog.jsonl` i cofnij/ponów; edycja nigdy nie zmienia migawki źródeł (hash przed/po); manifest
  `edits[]` i sekcja raportu; `redact` nieodwracalna oznaczona jawnie.
- **Oś czasu** (`test_timeline.py`): mieszane strefy, epoch i ISO, brak strefy → `inferred`;
  stabilność remisów między przebiegami i przy `workers` 1 vs 4; kubełki; luki i serie wg presetów
  (`0` = brak); kontynuacja ChatGPT→Claude z fixture'a wykryta z pewnością, nie scalona;
  `edit.sort` daje ten sam porządek co `timeline.jsonl`.
- **Różnica** (`test_diff.py`): dwa eksporty jednego konta → liczby nowe/zmienione/usunięte/bez
  zmian = wyrocznia z generatora; `mode: only_new` w przepisie; różnica dwóch pakietów;
  `--od-manifestu`.
- **Pakiet przekazania** (`test_handoff.py`): `handoff/` importuje się przez `engine.importer`
  (baza w pamięci) i przez `libloom` (gdy `LOOM_LIBRARY`; inaczej SKIP z powodem) → liczniki
  rozmów/wiadomości/załączników równe manifestowi; `metadata.export.locator` wskazuje źródło.
- **Serwowanie** (`test_serve.py`): serwer w wątku na porcie 0; brak tokenu → 401; `GET /api/ops`
  = `ops.json`; `POST /api/op` dla operacji krótkiej i długiej (`job` + SSE `progress`/`done`);
  `GET /api/view` = to, co widzi `CommanderState`; adres inny niż lokalny → ostrzeżenie
  w dzienniku; strażnik gniazd potwierdza brak połączeń wychodzących.
- **Język zaznaczania** (`test_select_expr.py`): gramatyka (każda forma z §6.9), aliasy polskie,
  wzorce strukturalne z fixture'ów, czas (§6.11), `reason` per dopasowanie, błędy składni
  z pozycją.
- **Prywatność** (`test_privacy_guard.py`): żaden test ani fixture nie zawiera ścieżek
  `docs/exports`, `eval/real-holdout-key` ani `~/.claude/projects` z prawdziwej maszyny; tryb
  `--zbiorczo` na fixture'ach — wyjście nie zawiera żadnego tytułu, fragmentu tekstu ani nazwy
  pliku z fixture'a (sprawdzane listą „zakazanych” ciągów z generatora).
- Rejestracja w `loom/CMakeLists.txt` obok `research.*`:
  `add_test(NAME tools.extract COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${LOOM_ROOT}/tools/extract/tests -p "test_*.py")`
  z `ENVIRONMENT "${_loom_python_env};LOOM_LIBRARY=$<TARGET_FILE:loom>;TERM=xterm-256color"`
  i `TIMEOUT 600` (test curses i serwowania potrzebują zapasu); testy zależne od `libloom`
  i `curses` pomijają się z widocznym powodem, nigdy nie zawodzą z braku zależności opcjonalnej.
- Nigdy: `docs/exports`, `eval/real-holdout-key`, prawdziwe eksporty. Strażnik testowy sprawdza,
  że żaden test ani fixture nie zawiera tych ścieżek.

## 10. Decyzje i jak je odwrócić

| Decyzja | Dlaczego | Odwrócenie |
|---|---|---|
| Osobny pakiet Pythona `loom/tools/extract`, uruchamiany `python3 loom/tools/extract` (katalog z `__main__.py`), bez budowania | słowa właściciela „osobny skrypcik”; działa na każdej maszynie z Pythonem 3.11; jądro woła go jako port | etap `extract` w `loom/src/archive` czytający te same pliki danych; model wspólny jest zgodny z `loom.export.v1` |
| Formaty tekstowe przez podklasę `engine.importer.ConversationImporter` przechwytującą `_import_message_list`; drzewa dostawców własnym bezstratnym przejściem sprawdzanym wyrocznią `EXPECTED.json` | Python spłaszcza drzewa i gubi gałęzie/narzędzia/załączniki — dokładnie to, co ma wyjść w ekstrakcie; parytet pilnuje test, nie kopia kodu | `reader_backend: loom` (`libloom.so`) jako czytnik główny |
| Opis formatu jako dane (`formats/*.json`) z mapą bloków i `known_absent`; rozpoznanie z treści | R42, R21; Gemini/Grok to hipotezy — poprawka ma być plikiem | — (dane) |
| Pliki zadań JSON lub TOML (`tomllib`), bez YAML | biblioteka standardowa | czytnik YAML, gdy `PyYAML` obecny (opcjonalnie) |
| Kaskada domyślna: L0–L2 lokalnie, L3/L4 obecne lecz wyłączone; `--model` włącza | zero sieci domyślnie; koszt widoczny | inna kaskada lub `enabled: true` w nakładce |
| Ziarno `conversation`, sąsiedztwo `conversation` | R18: całe rozmowy na temat | `grain`, `expand` |
| Pakiet domyślny = `preserve` + `text` + inwentarz + manifest + raport | minimalna konfiguracja daje coś użytecznego i odwracalnego | `profiles` |
| Załączniki: tylko użyte, układ jak w źródle, brakujące wymienione | słowa właściciela | `copy_all`, `reference_only` |
| Czasy: zachowane jak w źródle + pole znormalizowane ISO 8601; bez przeliczania strefy domyślnie | nic nie zmieniamy bez prośby | `zone`, `shift`, `anonymise` |
| Redakcja wyłączona domyślnie | pakiet jest dla właściciela | profile redakcji |
| Model: `loom_server`, gdy podany, inaczej jawny `provider`; klucz z `secrets.json` czytany dopiero po `--model`; szacunek przed wysyłką; ×10 względem historii przebiegów | jedna droga, koszt zawsze widoczny | próg ×10 i limity w nakładce (`0` = brak) |
| Fleksja PL lekkim stemerem regułowym + formy jawne, bez bibliotek | stdlib; formy widoczne w „dlaczego” | lematyzator opcjonalny, gdy obecny |
| Wektory przez `core.selector` z pamięcią podręczną; poziom zapisany | ten sam selektor co aplikacja | `vectors.engine` |
| Tożsamość jednostki = hash treści; decyzje ręczne przeżywają nowy eksport | uczenie się z poprawek | `decisions.key: ext_id` |
| Odkrywanie klastrów deterministyczne, bez modelu | koszt zero; model tylko nazywa | `--model` |
| Sesje Claude Code jako źródło; wiersze wewnętrzne do kubełka | pytanie właściciela o przebiegi agentowe | `formats/claude_code.json` `disabled` |
| ctest `tools.extract`, TIMEOUT 600 | jak `research.*`; testy komandera (pty) i serwowania potrzebują zapasu | TIMEOUT w `CMakeLists.txt` |
| i18n pl/en w `data/i18n`, preset `pl` | R42 | `--jezyk` |
| Testy wyłącznie na fixture'ach syntetycznych; strażnik ścieżek | prywatność | — |
| Osobny pakiet Pythona `loom/tools/extract/` (S1, 2026-10-16): `python3 -m`/katalog z `__main__.py`, bez budowania, stdlib najpierw, `sentence-transformers`/`scikit-learn` opcjonalnie jak w `core/selector.py`, degradacja z widoczną notą | słowa właściciela „osobny skrypcik”; działa wszędzie, gdzie jest Python 3.11 | port w `loom/src/archive` czytający te same pliki danych |
| **Adaptery, nigdy zduplikowane parsery** (S1): `engine/importer.py` (mapping, chat_messages, zip, jsonl, html, mht, markdown, text) przez podklasę; model jednostek zgodny z `loom/src/archive`; nowe czytniki tylko dla Gemini Takeout, Grok, sesji Claude Code | jedna heurystyka ról, jeden parser HTML; parytet pilnuje test | `reader_backend: loom` |
| **Jeden silnik, trzy twarze** (S1): CLI, komander TUI (curses z biblioteki standardowej, dwa panele jak Norton/Total Commander, klawisze F i vim jako dane), serwowanie (`http.server` na localhost, JSON API tych samych operacji) — żadna twarz bez logiki własnej | ten sam przebieg zdaniem, plikiem, ekranem, API; Warsztat webowy później podmienia tylko renderer | `faces` w `ops.json`: twarz można wyłączyć (`disabled`) bez zmiany silnika; inny renderer TUI (np. `prompt_toolkit`, gdy obecny) = nowy `view_*.py` |
| **Wszystko, co robi użytkownik, jest przepisem** (S1): JSON z krokami (zaznaczanie, filtry, edycje, wycięcie/profil, wyjście); ten sam przepis z CLI, TUI i API; TUI nagrywa; przepis odtwarzalny z manifestu | determinizm, odtwarzalność, nauka z nagrań | `records: false` per akcja w `actions.json` (nie nagrywaj) |
| Komander: `CommanderState` bez curses + `view_curses.py` ładowany leniwie; sterownik bezgłowy do testów; autozapis atomowy po każdym kroku | testowalność w CI bez terminala; brak `curses` nie blokuje CLI/API; nie gubić pracy | `autosave.every`, `autosave: off` |
| Edycja seryjna tylko na kopii (drzewo pakietu): podgląd jako JSON Patch + linie czytelne, `oplog.jsonl`, `invert` albo migawka, cofnij/ponów, wpis w manifeście | źródła nietykalne; nic nie znika bez wpisu | `invertible: none` dla operacji jawnie nieodwracalnych (oznaczone w raporcie) |
| Jeden język zaznaczania (gramatyka jako dane) dla CLI, komandera, API, zakresów edycji i filtrów | jedno wyrażenie = jedno znaczenie wszędzie; zapisywalne | gramatyka w nakładce; aliasy językowe w danych |
| Oś czasu liczona w UTC z zachowaniem oryginału; remisy `(t, usługa, archiwum, lokalizator)`; kontynuacje między usługami tylko jako kandydaci `inferred` | determinizm; nic nie scala się bez decyzji | `timeline/presets.json`; `continuations: off` |
| Różnica po tożsamości (usługa, `ext_id`) + hash treści; usunięte wymienione, nigdy pomijane | R44; eksporty gubią skasowane rozmowy | `diff.identity: content_hash` (bez `ext_id`) |
| Pakiet przekazania używa pól, które importery już rozumieją (`loom.export.v1` w `metadata.export`); importer się nie zmienia | import jednym krokiem bez nowego formatu po stronie aplikacji | profil `handoff/profile.json` |
| Serwowanie tylko na pętli zwrotnej z tokenem na uruchomienie; brak sieci wychodzącej | §2; API służy testom, skryptom, portowi i Warsztatowi | `--adres` jawnie (z ostrzeżeniem) |
| **Prywatność DIP26** (właściciel 2026-10-12): prawdziwe eksporty preferowane w lokalnych przebiegach odbiorczych; syntetyczne jako kontrola; w repo/testach/dziennikach/raportach/transkryptach tylko syntetyczne; agent na prawdziwych danych tylko w `--zbiorczo`; nowy dostawca = pytanie tak/nie | słowa właściciela; jego dane | — (decyzja właściciela) |
| Keymap, akcje, układ, kolory, teksty komandera jako dane z nakładką użytkownika; profile `fkeys` i `vim` dostarczone | R42, R47 | — (dane) |
| **Plaster A, 2026-10-16 (S1, wykonane):** krok przepisu jest samoopisowy — `params` zapisane w całości z `params_source` per parametr (`step` \| `preset:<ścieżka>` \| `op_default`); `normalise` szanuje zapisane pochodzenie; odtworzenie z `recipe.json` pakietu daje ten sam `reproducible_hash` (test) | bez tego odtworzenie przepisu z pakietu oznaczało presety jako `step` i hash się różnił | `params_source` można pominąć w nakładce `ops.json` (`record_sources: false`) — wtedy odtworzenie bierze bieżące presety |
| Plaster A: odwołania do załączników z części treści ChatGPT (`image_asset_pointer` itp.) są rejestrowane na jednostce (rozwiązane albo brakujące), więc licznik `attachment` jest ≥ wyroczni C++ (13 vs 7 na `openai_2026_sharded.zip`); `branch` (liście) może przekraczać wyrocznię, gdy mapping ma liście bez wiadomości (11 vs 9) — test parytetu sprawdza równość rozmów, wiadomości, rozwidleń i ścieżki bieżącej, a te dwa liczniki jako `>=` | słowa właściciela: nic nie znika po cichu; brakujące załączniki wymienione | `attachments.from_parts: false` w pliku formatu; port jądra może przyjąć tę samą regułę |
| Plaster A: tryb `--zbiorczo` (agent na prawdziwych eksportach): archiwa numerowane `archive-N`, w przepisie `sources` = `archive-N:sha256:<hash treści>`, bez ścieżek, nazw członków, tytułów, tekstów, lokalizatorów, powodów dopasowań, terminów kontynuacji i klastrów (tylko liczby); test strażnika sprawdza wyjście na fixture'ach listą ciągów zakazanych | DIP26 | `--zbiorczo` jest opcją; tryb pełny zostaje presetem |
| Plaster A: `write` zapisuje to, co plaster produkuje (`inventory.json`, `timeline.json/.jsonl`, `diff.json`, `selection.json`, `explain.jsonl`, `clusters.json`, `decisions.json`, `recipe.json`, `units.jsonl`, `RAPORT.md`, `manifest.json`); `reproducible_hash` = sha256 par `ścieżka:sha256` wszystkich wyjść poza manifestem; `run.started/finished` poza haszem | determinizm sprawdzalny już przed plastrem B (`preserve`/profile dopiszą wyjścia do tej samej listy) | — |
| Plaster A: `roznica` wobec pakietu czyta jego migawkę `units.jsonl` (schemat `loom.extract.unit/1`), wobec archiwów — scan→read→merge z tymi samymi ustawieniami; tożsamość `ext_id` (preset) lub `content_hash` | przebieg przyrostowy bez ponownego czytania starego eksportu | `write.units_snapshot: false` (wtedy różnica tylko wobec archiwów) |
| Plaster A: poziomy z modelem w kaskadzie są **liczone** (ziarna, znaki, szacunek tokenów), nigdy wołane; `--model` tylko włącza zliczanie; strażnik gniazd w testach blokuje każde połączenie poza pętlą zwrotną | zero sieci w plastrze A; wywołania, trasa, szacunek ceny i ×10 przychodzą w plastrze B | — |
| Plaster A: strefa dla stempli bez strefy (`timeline.assume_zone`) przyjmuje też nazwy IANA przez `zoneinfo` (biblioteka standardowa); brak bazy stref = jawny błąd z nazwą | właściciel ma stemple lokalne bez strefy | preset `UTC` |
| Plaster A: poziom wektorowy bez indeksu jest **pominięty z powodem** (`skipped`), nie oceniony na 0 — brak indeksu nie wyklucza | wykluczenie z braku danych byłoby decyzją za użytkownika | — |
| Plaster A: granica końcowa zakresu dat (`created:..2026-05`, `--do`) jest wyłączna (pierwsza chwila po okresie), początkowa włączna | jednoznaczne okna miesięczne/dzienne | — |
| Plaster A: fixture'y nowych formatów z `loom/tools/gen_extract_fixtures.py` (Claude Code z narzędziami/sidechainem/kompaktowaniem/wierszami nieznanymi i niepoprawnymi, Gemini JSON+HTML, Grok dwa kształty, pakiet grafu, `loom.export`, katalog mieszany z mieszanymi stemplami, dwa eksporty jednego konta, duplikat, kontynuacja ChatGPT→Claude); `EXPECTED.json` liczony z danych generatora, nie przez czytniki; ctest `tools.extract` (75 testów, `python3 -I -m unittest`) | §9; prywatność | — |
| **Plaster B, 2026-10-16 (S1, wykonane):** `preserve` = bajty źródła — skaner rozpiętości tablic JSON bajt w bajt (`jsonspan.py`, strumieniowo, kawałkami; obsługa opakowania `{"conversations": [...]}` i przedrostka JS archiwum X); separatory źródła; zaznaczenie wszystkiego = plik identyczny bajt w bajt (test); kontener tego samego rodzaju (zip → zip z metadanymi członków źródła, katalog → katalog, plik → plik) | „cięcie z zachowaniem formatu”; determinizm bez reserializacji | profil `preserve` jest jednym z `cut.profiles` (preset `["preserve", "text"]`) — pominięcie go w zadaniu |
| Plaster B: odwołania do załączników w `preserve` zostają ważne, bo użyte załączniki leżą w układzie źródła (te same nazwy członków); w profilach przebudowy i w `handoff/` odwołania są **przepisane** na ścieżki względne w pakiecie (`attachments/<archiwum>/<członek>`); kopiowane tylko użyte, brakujące wymienione w manifeście i raporcie; załączniki z metadanych wiadomości (bez bloku treści) też są pokazywane | słowa właściciela; nic nie znika po cichu | `--zalaczniki copy_all` / `reference_only` (`cut.attachments`) |
| Plaster B: członkowie nie-rozmowy w `preserve` wg tabeli `preserve` formatu: `keep_members` kopiowane, `filter_members` (np. `message_feedback.json`, `shared_conversations.json`, `textdocs/*` po `conversation_id`) zawężane do zachowanych rozmów, reszta (np. `chat.html` ze wszystkimi rozmowami) pominięta **z wpisem** (nazwa + powód) | pakiet „o temacie” nie może przemycać innych rozmów; nic bez wpisu | profil `preserve` z `other_members: copy` w nakładce albo glob w `keep_members` pliku formatu |
| Plaster B: ziarno `preserve` = cała rozmowa (format źródła nie zna części rozmowy); rozmowy zaznaczone częściowo i członkowie bez granic rozmów (JSONL sesji, HTML, Markdown) kopiowane w całości i policzone w manifeście (`partial_whole`, `indivisible`) | 1:1 znaczy 1:1 | cięcie na poziomie wiadomości: profile przebudowy (`flat`, `text`, …) |
| Plaster B: redakcja (`data/redaction/*.json`: `none` preset, `personal`, `secrets`, `dictionary`; maski regexami z kontrolą Luhna/PESEL, słownik z fleksją i składaniem znaków, ta sama wartość → ta sama maska) obejmuje wszystkie wyjścia pakietu (także `units.jsonl`, oś czasu, raport) i `preserve` (elementy reserializowane, manifest: `byte_exact: false`); wpis per wystąpienie bez wartości (`redaction.jsonl`), raport z liczbami; załączniki binarne kopiowane bez redakcji z notą | prywatność bez cichych strat | `--redakcja none` (preset); `attachments: omit` w profilu redakcji |
| Plaster B: profile przebudowy jako dane (`data/profiles/*.json`, rodzaje `restructure`/`timeline`/`cards`/`preserve`; bloki `keep|omit|reference|note|summary`; gałęzie `tree|current|newest|longest|marked|all_paths` z notą o wyciętych; pola; wyjścia `json|jsonl|csv|markdown|markdown_index|markdown_journal|markdown_cards|graph|zip`); własny profil = plik albo obiekt w kroku; nazwy plików `NNNN-uid8` w porządku pakietu (kopiowanie do pakietu układa chronologicznie; preset `cut.order: package`, więc `edit.sort` i przeniesienia zmieniają porządek wyjść), bez tytułów w nazwach | R42; nazwy stabilne i bez treści (bezpieczne w trybie zbiorczym) | `cut.order: created|updated|title|source_order`; szablony `path` w profilu z nakładki |
| Plaster B: czasy w wyjściach przez `TimePolicy` (strefa `source` = ISO ze źródła bez przeliczania, epoch → ISO UTC; `shift`, `anonymise: package|conversation` = oś od 1970-01-01 z zachowanymi odstępami, `granularity`, `format: iso8601|epoch|date|source`) | nic nie zmieniamy bez prośby | opcje `cut` / `--strefa-wyjscia`, `--przesun-czas`, `--anonimizuj-czas` |
| Plaster B: drzewo pakietu = kopia zaznaczenia (`package.copy`, jawnie albo niejawnie przy `cut`); 17 operacji edycji seryjnej jako dane (`data/edits/*.json`) z podglądem = różnica `apply` (JSON Patch + `old`), migawka do cofnięcia w pamięci (głęboka kopia), `oplog.jsonl` bez migawek, `redact` nieodwracalne jawnie; `undo` zdejmuje krok z przepisu; CLI `edytuj` wstawia krok edycji przed `cut` i odtwarza przepis pakietu, `cofnij`/`ponow` przez `redo.json` | źródła nietykalne; przepis = stan | — (operacje są danymi; `invertible` w pliku operacji) |
| Plaster B: `recipe.json` pakietu i przepis wbudowany w manifest **bez kroku `write`** (przepis opisuje treść, nie miejsce); `odtworz manifest.json` buduje pakiet obok i porównuje `reproducible_hash` i każde wyjście; `SOURCE_DATE_EPOCH` ustala czasy przebiegu → te same wejścia + przepis = identyczne bajty pakietu (test); zapis usuwa pliki poprzedniego przebiegu wymienione w starym manifeście | determinizm sprawdzalny bajtowo; pakiet = dokładnie jego manifest | bez `SOURCE_DATE_EPOCH` czasy z zegara (i tak poza haszem) |
| Plaster B: `handoff/` = `conversations/<klucz>.json` (pola importerów + `metadata.export` `loom.export.v1` z blokami natywnymi, lokalizatorem, gałęzią), `attachments/`, opcjonalnie `graph/`; preset gałęzi `current` (importer Pythona spłaszcza listę wiadomości); `handoff.zip` **tylko z rozmowami** (importer Pythona importuje każdy plik zipa, obraz stałby się „rozmową” z OCR); liczniki `conversations_with_messages` i `messages_with_text` = to, co zobaczy importer Pythona (test i `sprawdz`) | import jednym krokiem bez nowego formatu | `handoff.branches: tree`, `handoff.zip: false`, `--z-grafem` |
| Plaster B: pakiet grafu `loom.graph_packet/1` z węzłami w `entities` i krawędziami jako `claims` (`subject`/`predicate`/`object`: `part_of`, `reply_to`, `mentions`, `uses_tool`, `tagged_with`, `derived_from`) — czytnik `loom.graph_packet` czyta go z powrotem | jeden format grafu w narzędziu | — |
| Plaster B: model tylko po `--model`; trasa `provider` (OpenAI-zgodne `chat/completions`, klucz z `LOOM_EXTRACT_API_KEY` albo `secrets.json` czytany dopiero po włączeniu); `loom_server` opisany, odmowa z powodem; szacunek przed wysyłką (wywołania × tokeny, wyjście = `max_tokens` jako górna granica); zgoda `--tak` albo „tak” na pytanie (domyślnie nie); strażnik ×10 wobec ostatniego zaakceptowanego przebiegu z `usage.jsonl` wymaga `--zgoda-x10`; limity `0` = brak; nieznana cena przy ustawionym limicie = odmowa; `prices.json` dostarczany **pusty** (cen nie zgadujemy; `models.json` aplikacji: cena za token × 1e6); pamięć podręczna per (przepływ, model, hash wejścia); dziennik bez tekstu i kluczy; kod wyjścia 5 | jedna droga, koszt zawsze widoczny | ustawienia `flows.model.*` w nakładce/zadaniu; `--tylko-z-pamieci` |
| Plaster B: kaskada z modelem w dwóch przebiegach: poziomy lokalne → szacunek dokładnie na paśmie niejednoznacznym → zgoda → L3/L4 (werdykt modelu rozstrzyga ziarno, na którym biegł; źródło decyzji `L3`/`L4`); bez zgody nic nie wychodzi | koszt tylko za to, czego lokalnie nie rozstrzygnięto | kaskada bez L3/L4 albo `enabled: false` |
| Plaster B: zdanie → zadanie gramatyką jako danymi (`data/grammar/pl.json`, `en.json`; wybór gramatyki z lepszym dopasowaniem); niezrozumiane słowa wypisane jako reszta, nigdy zgadywane; temat = id kryteriów, jeśli istnieje, inaczej termin; „bez narzędzi/rozumowania” = edycja `drop_blocks`, „tylko z …” = wyrażenie (przecięcie); zadanie pokazane przed wykonaniem i zapisane w manifeście (`run.sentence`); pliki zadań JSON lub TOML | słowa właściciela „zdanie, plik, ekran” | reguły gramatyki w nakładce |
| Plaster B: kody wyjścia `3` nic nie rozpoznano, `4` przerwano, `5` krok płatny odmówiony, `6` źródło nieczytelne (zmiana z `1` z plastra A); tryb `--zbiorczo`: listy wyjść jako liczby (ścieżki niosą nazwy załączników), zdanie pokazuje liczbę źródeł | §4.1; DIP26 | — |
| Plaster B, znalezisko: `engine/importer.py` nie czyta postaci `{"conversations": [...]}` eksportu ChatGPT z drzewami `mapping` (ani źródła, ani cięcia) — `_import_conversation_obj` wymaga `messages`; obieg przez importer Pythona sprawdzany na postaci tablicy (`conversations-001.json`); poprawka importera to osobna decyzja (nie zmieniano go tutaj) | adaptery, nie zmiany importera w tym zadaniu | dopisać w importerze gałąź `mapping` dla elementów opakowania |

## 11. Przyrosty

Trzy plastry; każdy kończy się zieloną bramką (`ctest tools.extract` + strażnik prywatności)
i daje właścicielowi coś, co działa na jego eksportach lokalnie (w `--zbiorczo`, gdy uruchamia
agent). Dawne P38.1–P38.5 mieszczą się w nich w tej kolejności.

1. **Plaster A — rdzeń silnika** (dawne P38.1 + P38.2): `engine.py`, `ops.py`, `recipe.py`
   (schemat, normalizacja, determinizm), `formats/`, czytniki (ChatGPT, Claude, Claude Code,
   tekstowe przez `engine.importer`, nasz graf; Gemini i Grok jako pliki formatów z hipotezą),
   `merge`, `inventory`, **`timeline`** (klucze, remisy, kubełki, luki, serie, kontynuacje),
   **`diff`**, kryteria, L0–L2, **język zaznaczania** (§6.9–6.11), `expand`, decyzje ręczne
   i uczenie się, `explain`/`wyjasnij`, klastry bez modelu, i18n, `gen_extract_fixtures.py`,
   testy A, ctest. Bramka: liczby inwentarza = wyrocznia; parytet przejścia drzew z `EXPECTED.json`;
   oś czasu i przepis deterministyczne. Właściciel dostaje: `inwentarz`, `os-czasu`, `roznica`,
   `szukaj --na-sucho` — odpowiedź liczbami na pytanie o przebiegi agentowe i jedną oś czasu
   wszystkich archiwów.
2. **Plaster B — wycięcie, profile, manifest, raport, CLI** (dawne P38.3 + P38.4): profil
   `preserve` bajt w bajt, profile `flat`/`structure`/`reasoning`/`text`/`graph`/`table`/
   `timeline`/`cards`, **operacje edycji** z podglądem, `oplog`, cofnij/ponów (z CLI: `edytuj`),
   czasy, redakcja, `outputs/`, manifest i `odtworz`, **`handoff`** + obieg zamknięty przez
   `engine.importer` i `libloom`, `RAPORT.md`, zdanie i plik zadania, wszystkie czasowniki CLI,
   przepływy z modelem (trasa, szacunek, strażnik ×10, dziennik, pamięć podręczna), `opisz`,
   nazwy klastrów, `summarize`, `sprawdz`. Bramka: `reproducible_hash` z dwóch przebiegów równy;
   `preserve` reimportuje się z tymi samymi licznikami; model przez atrapę — ani jednego
   wywołania bez `--model`. Właściciel dostaje: `wytnij` w profilach, pakiet do importu
   w aplikacji, raport po polsku.
3. **Plaster C — komander i serwowanie** (dawne P38.5 rozszerzone): `CommanderState`,
   `keymap.json`/`actions.json`/`layout.json`/`colors.json`, `view_curses.py`, sterownik bezgłowy,
   nagrywanie i `--odtworz`, autozapis, widoki (`sources`, `package`, `recipe`, `timeline`,
   `attachments`, `clusters`, `inventory`, `diff`, `selection`), edycja seryjna z F4, `serwuj`
   (API, SSE, token), test parytetu trzech twarzy, rozszerzenia 65–80. Potem: Gemini i Grok na
   pierwszym prawdziwym pliku (właściciel uruchamia `formaty PLIK` i `inwentarz`; poprawki
   w plikach formatów), port jądra `archive_extract`, akcja czatu, `obserwuj`. Bramka: test
   parytetu zielony; `test_commander_curses` zielony tam, gdzie jest pty, pominięty z powodem
   w CI. Właściciel dostaje: X-Port Commander.

**Stan 2026-10-16 (S1):** plaster A wykonany na gałęzi `claude/p38-archive-extract` — `loom/tools/extract/`
(silnik + rejestr operacji `data/ops.json`, przepisy, czytniki wszystkich formatów z §3.4 plus Gemini/Grok jako
hipotezy, `merge`, `inventory`, `timeline`, `diff`, kryteria, L0–L2, język zaznaczania, `expand`, decyzje ręczne
z propozycjami, klastry, i18n pl/en, CLI z czasownikami `inwentarz`, `os-czasu`, `roznica`, `szukaj`, `wyjasnij`,
`zaznacz`, `klastry`, `przepis`, `formaty`, `operacje`; `write` pakietu plastra A z manifestem i `RAPORT.md`);
bramka: `ctest -R tools.extract` zielona lokalnie. Z plastra A **nie** weszły jeszcze: `zdanie` (§4.2) i plik zadania
TOML jako osobny czasownik (przepis TOML działa), `wyjasnij` jako osobny plik `explain`-only, `odtworz manifest.json`
(odtwarzanie idzie przez `przepis uruchom recipe.json` pakietu), pamięć podręczna osadzeń i `state.json` wznawiania
(§3.6), `workers` równoległe (§3.7 — porządek wyjścia i tak deterministyczny), `libloom` jako drugi czytnik.

**Stan 2026-10-16 (S1), plaster B:** wykonany na tej samej gałęzi — `jsonspan.py`, `cut/` (`preserve`, `views`, `profiles`,
`common`), `outputs.py`, `package.py`, `edits.py` (+ `data/edits/*.json`, 17 operacji), `handoff.py`, `stats.py`, `redact.py`
(+ `data/redaction/*.json`), `manifest.py` (manifest, `odtworz`, `sprawdz`), `flows.py` (+ `data/prices.json`), `sentence.py`
(+ `data/grammar/pl.json`, `en.json`), `ops_cut.py`, `cli_cut.py`, profile `data/profiles/*.json` (`preserve`, `flat`,
`structure`, `reasoning`, `text`, `graph`, `table`, `timeline`, `cards`, `bundle`); czasowniki `wytnij`/`wyciagnij`, `koszt`,
`odtworz`, `przekaz`, `statystyki`, `sprawdz`, `zalaczniki`, `profil`, `przeplyw`, `opisz`, `edytuj`, `cofnij`, `ponow`,
`filtr`, tryb zdania, `--zadanie` (JSON/TOML), `--plan`; ctest `tools.extract` 114 testów (+39), w tym determinizm bajtowy
pakietu, obieg `preserve` → czytnik narzędzia i `engine/importer.py`, atrapa dostawcy modelu na pętli zwrotnej. Dodane ponad
listę (zasada „listy są przykładami”): profil `bundle` (zip Markdown + JSONL + załączniki), `table` z CSV rozmów, karty z
pytaniami otwartymi, `package.mark` (liść dla `branches.keep: marked`), `package.show`, zapisane filtry, `filtr`/`--filtr`,
licznik „co zobaczy importer Pythona” w przekazaniu, usuwanie plików poprzedniego przebiegu, redakcja plików bocznych.
Doszły też: nazwy klastrów modelem (przepływ `name_clusters`, `klastry --nazwij`), tekst załączników tekstowych w wyjściach
(`--tekst-zalacznikow`), obieg `handoff.zip` przez `libloom` (`loom_import_file_ex`, ctest podaje `LOOM_LIBRARY`).
Z plastra B **nie** weszły: trasa `loom_server`, OCR/ASR załączników-obrazów i dźwięku, edycje `move_attachments`
i `user:<moduł>`, `state.json` wznawiania (powtórzenie polecenia jest wznowieniem: odpowiedzi modelu z pamięci podręcznej,
kroki lokalne liczone od nowa), `workers`; komander i `serwuj` to plaster C.

Otwarte (do zmierzenia, nie do zgadywania): rzeczywisty kształt eksportów Gemini i Grok;
pełność `thinking` w bieżących eksportach Claude; czy aplikacje źródłowe importują własny format
z powrotem (ChatGPT i Claude nie oferują dziś importu — `preserve` służy wtedy nam i innym
narzędziom); dokładność szacunku tokenów dla polskiego (stała do kalibracji z `models.json`);
kiedy Warsztat webowy przejmie komander (wymaga tylko renderera nad `GET /api/view`, decyzja
po plastrze C); termin pierwszego przebiegu `--zbiorczo` na prawdziwych eksportach właściciela
(po plastrze A, lokalnie, tylko liczby).
