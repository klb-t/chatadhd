# ChatADHD - PEŁNY RAPORT HISTORYCZNY
## Analiza wszystkich sesji rozwojowych (2026-01-21 → 2026-02-08)

---

# SPIS TREŚCI

1. [Geneza projektu](#1-geneza-projektu)
2. [Wersje chronologicznie](#2-wersje-chronologicznie)
3. [Funkcje - co zażądane vs co zaimplementowane](#3-funkcje)
4. [Aktualna struktura kodu](#4-aktualna-struktura)
5. [Potencjalnie zgubione funkcje](#5-potencjalnie-zgubione)

---

# 1. GENEZA PROJEKTU

## Sesja 1: 2026-01-21/22 - OpenRouter API Setup
**Transkrypt:** `2026-01-23-00-57-58-openrouter-api-setup-memory-system.txt`

### Kontekst początkowy:
- User pytał o **router do wielu API** (OpenRouter)
- Analiza kosztów dostępu do 6+ modeli przy minimalnym nakładzie (~30 zł)
- Porównanie: OpenAI, Anthropic, Google Gemini, Mistral, xAI Grok, DeepSeek

### Pierwsze założenia architektury pamięci:
```
User: "Opcje mają być takie: 
- Drzewo rozmów – wariantowanie, edycja nie kasuje tylko tworzy gałąź
- A no i oczywiście pamięć – wspomnienia na życzenie i automatyczne"
```

### Kluczowe decyzje:
- OpenRouter jako główny provider (jeden klucz API → wiele modeli)
- Hierarchiczna pamięć (nie płaska lista)
- Branching conversations (edycja = nowa wersja)

---

## Sesja 2: 2026-01-23 - Pierwszy kod (openrouter_gui.py)
**Transkrypt:** `2026-01-23-09-51-10-openrouter-client-refactor-external-config.txt`

### Request:
```
"System prompt edytowalny z kilkoma presetami. 
Wspomnienia, opis użytkownika i takie rzeczy jak zawsze są – przesyłane zgodnie ze standardami. 
Najważniejsze: pydroid tylko 1 plik. Od razu gui proszę"
```

### Zaimplementowane:
- **openrouter_gui.py** - jeden plik z Kivy GUI
- System prompt presets (Default, Epistemic, Coding, Legal, Research, Custom)
- Edytor system promptu
- Pamięć zgodna ze standardami OpenAI/Anthropic

### Zmiana architektury (user poprawił):
```
"Chodziło mi o jeden plik z kodem, ale konfiguracja i wszystkie dane 
jak listy modeli – w zewnętrznych plikach, dane o aktualnej dostępności 
aktualizowane automatycznie na bieżąco"
```

### Nowa struktura:
```
openrouter_client/
├── main.py              # Kod aplikacji (jeden plik)
├── config.json          # Konfiguracja użytkownika
├── models.json          # Lista modeli (auto-update z API)
├── presets.json         # Presety system promptów
```

---

## Sesja 3: 2026-01-23 - Nazwa "ChatADHD" + Hierarchiczna pamięć
**Transkrypt:** `2026-01-23-21-39-03-chatadhd-v2-hierarchical-memory-ui-refactor.txt`

### Geneza nazwy:
```
User: "Genialne 😂 ChatADHD – bo:
- 1000 rozmów porozrzucanych
- 7 spraw naraz
- Branching myśli w każdą stronę"
```

### Request - hierarchiczna pamięć:
```
"Hierarchiczna pamięć – nie płaska lista, tylko struktura:
   Projekty/
   ├── WatchDog/
   │   ├── opis, status, linki
   │   └── powiązane rozmowy
   ├── Legal Flow/
   └── Instytucje/
       └── Claims/
           └── Evidence[]"
```

### Zaimplementowane w v2:
- Sliding panels (Memory, Conversations)
- Hierarchia: Projects → notatki[], linki[]
- Claims → evidence[], timeline[]
- Material Design kolory

### Problem - crash bez komunikatu:
```
"Wywala się po sekundzie od uruchomienia bez komunikatu"
```

### Dodane:
- Pełny logging do pliku `chatadhd.log`
- Verbose output do konsoli
- Timestamps wszystkich operacji

---

## Sesja 4: 2026-01-24 - ChatADHD v3 - Universal Tree
**Transkrypt:** `2026-01-24-14-43-00-chatadhd-v3-universal-tree-memory-architecture.txt`

### Request - uniwersalna struktura:
```
"Uniwersalna struktura pamięci:
Memory Item
├── text: string
├── children: [Memory Item, ...]  → rekurencyjne zagnieżdżenie
├── attachments: [file refs...]
└── metadata: {active, tags, weight}"
```

### Zaimplementowane w v3:
- **Rekurencyjna struktura** - dowolna głębokość zagnieżdżenia
- **Attachments** - pliki jako odnośniki
- **Active flag** - czy dołączać do kontekstu
- Czysty deployment (bez danych użytkownika w paczce)

### Request - standalone deployment:
```
"W paczce ZIP nie było w ogóle:
- config.json (lub był z pustymi wartościami)
- data/client.db
- chatadhd.log
- żadnych plików z danymi użytkownika

Aplikacja ma tworzyć te pliki przy pierwszym uruchomieniu"
```

### Struktura v3:
```
chatadhd/
├── main.py           # Cała aplikacja
├── _presets.json     # Template (z _, nie nadpisuje)
├── requirements.txt
└── README.md

Tworzone przy starcie:
~/Documents/ChatADHD/
├── config.json
├── secrets.json
├── data/client.db
```

---

## Sesja 5: 2026-01-27 - ButtonBehavior Fix (v3.0.1)
**Transkrypt:** `2026-01-27-10-12-30-chatadhd-v3-buttonbehavior-import-fix.txt`

### Błąd:
```
NameError: name 'ButtonBehavior' is not defined
```

### Przyczyna:
- Import `ButtonBehavior` był za późno w kodzie
- Klasa `RBtn` próbowała go użyć wcześniej

### Fix:
- Wszystkie importy Kivy przeniesione na początek pliku

---

## Sesja 6: 2026-01-29 - v0.4.0-dev Modular Refactor + Git
**Transkrypt:** `2026-01-29-03-09-28-chatadhd-v040-modular-refactor-git-setup.txt`

### Request:
```
"Dowiaduję się, że można jednak w pydroid dzielić projekt na pliki, 
tylko trzeba ręcznie strukturą katalogów zarządzać, prawda to? 
Jeśli tak, to jedziemy modułowo!

I od razu mi napisz (jestem zielony w git): 
Jak skonfigurować repo i pushnąć z Androida?"
```

### Integracja kodu od GPT:
```
User pokazał kod wygenerowany przez GPT:
- MemoryEngine z grafem (linki między węzłami!)
- SelectorEngine - semantic search z fallbackami (sentence-transformers → TF-IDF → keywords)
- IterativePromptBuffer - agregacja etapów promptu
- FileParser - OCR i transkrypcja
```

### Zaimplementowane w v0.4.0-dev:
```
chatadhd_v0.4.0/
├── main.py              # GUI entry
├── cli.py               # CLI do testów
├── db.py                # SQLite
├── config.py            # Config/Secrets/Models
├── engine/
│   ├── chat_engine.py   # API calls
│   ├── memory_engine.py # Graf pamięci
│   └── selector.py      # Semantic search
└── gui/
    └── panels.py        # Kivy panels (363 linie)
```

### GUI (zaimplementowane od podstaw, nie GPT stub):
- `ConvPanel` - lista konwersacji (sliding panel)
- `ChatPanel` - główny chat z wiadomościami, input, model spinner
- `MemoryPanel` - drzewo pamięci z checkboxami
- `SettingsPopup` - API key, URL, temperatura

---

## Sesja 7: 2026-01-30 - v0.4.1/v0.4.2 GUI Fixes
**Transkrypt:** `2026-01-30-18-36-24-chatadhd-v042-gui-fixes-flat-zip.txt`

### Błędy:
1. `ModuleNotFoundError: No module named 'version'` - Pydroid nie widzi modułów
2. Ikony emoji (↻, ⚙) nie renderują się (pokazują □)
3. Przyciski nachodzą na siebie
4. File picker potrzebny natywny Android

### Fixy w v0.4.1-dev:
- **Bez emoji** - zamiast ↻ ⚙ □ teraz: `R`, `Cfg`, `X`, `[D]`, `[F]`
- Lepszy layout - przyciski nie nachodzą
- File picker - próbuje plyer (natywny Android), fallback do Kivy

### v0.4.2-dev - FLAT ZIP:
```
Rozpakowanie daje:
[twój_folder]/
├── main.py       ← uruchamiasz to
├── db.py
├── config.py
├── engine/
└── gui/

NIE będzie dodatkowego chatadhd_v0.4.2/ w środku
```

### Request - pamięć = wszystko:
```
"Zintegrować archiwum rozmów i projektów z pamięcią.
Tak w sumie bardziej naturalnie: wszystkie dane = memory.
W skład wchodzą: wszystkie drzewa rozmów, wszystkie załączniki, wszystko.
Do tego jest nakładka porządkująca."
```

---

## Sesja 8: 2026-02-02 - v0.4.3/v0.4.4 Architektura KOD≠DANE
**Transkrypt:** `2026-02-02-03-20-01-chatadhd-v043-v044-architecture-redesign.txt`

### Problem - hardcoded paths:
```
User: "Ale to co ty tam ścieżki hardcodujesz? 
Dlaczego nie działa w dowolnym katalogu byleby struktura była?"
```

### Nowa zasada KOD ≠ DANE:
```
KOD (dowolna lokalizacja, wersjonowane):
/Download/chatadhd_v0.4.2/
/Download/chatadhd_v0.5.0/
/wherever/you/want/

DANE (stała lokalizacja, NIE wersjonowane):
Android: /storage/emulated/0/Documents/ChatADHD/
├── config.json
├── secrets.json
├── models.json
└── data/client.db
```

### v0.4.3 - paths.py:
- Nowy moduł `paths.py` zarządza ścieżkami
- Próbuje po kolei: `/Documents/ChatADHD`, `/Download/ChatADHD`

### v0.4.4 - Native file picker:
- Próba użycia plyer.filechooser dla natywnego Android pickera

### Request - architektura konkurencyjna:
```
"Jak chcę zrobić konkurencję dla Ciebie i chata GPT, to podstawowe założenia:
- Domyślnie konta użytkownika
- Dane szyfrowane na serwerze bez możliwości odtworzenia przez operatora
- Ale również lokalne, edytowalne, z pełną kontrolą"
```

### Architektura ChatADHD v1.0 (plan):
- Zero-knowledge encryption (AES-256-GCM)
- Client-side key derivation
- Opcjonalny sync z serwerem (tylko ciphertext)
- Pełna kontrola lokalna

---

## Sesja 9: 2026-02-04 - v1.0 Reproducible Builds
**Transkrypt:** `2026-02-04-10-45-58-chatadhd-v1-reproducible-builds-transparency.txt`

### Zaimplementowane w v1.0:
```
chatadhd_v1.0/
├── core/
│   ├── crypto.py       # Zero-knowledge encryption (AES-256-GCM)
│   ├── db.py           # Unified DB (conversations, items, graph)
│   └── sync.py         # E2E encrypted sync
├── engine/
│   ├── chat_engine.py  # Multi-provider abstraction
│   ├── memory_engine.py
│   └── selector.py
└── gui/
    └── panels.py
```

### Dyskusja - reproducible builds:
```
User: "Ale czy w kodzie jest dowód że te skrypty pytona są wywoływane?
Bo myślę co można by logować, co byłoby deterministyczne i wystarczające
do zapewnienia transparentności"
```

### Pomysły na transparency:
- Logowanie wywołań API (bez danych wrażliwych)
- Hash kodu wykonywalnego
- Remote attestation (trudne)

---

## Sesja 10: 2026-02-04 - v1.0-zk Zero-Knowledge + Licensing
**Transkrypt:** `2026-02-04-11-30-17-chatadhd-v1-zero-knowledge-licensing.txt`

### Request - izolacja danych:
```
"W architekturze cały czas musi istnieć izolacja jawnych danych użytkownika i serwera.
Po stronie serwera tylko zaszyfrowane informacje bez możliwości odszyfrowania,
wywołania API z urządzenia użytkownika"
```

### Zaimplementowane w v1.0-zk:
- Izolacja: co widzi klient vs co widzi serwer
- Direct LLM calls (bez proxy przez serwer)
- Provider abstraction (własne klucze API)

### Request - providers + open source:
```
"Jak dodamy providerów, np. bezpośrednie klucze korporacyjne 
czy na dowolnej umowie od dostawców modeli,
Open Source framework - audytowalny bezpieczny hiperfunkcjonalny multikompatybilny chatbot"
```

### Request - offline + future proof:
```
"Żeby była opcja offline + Future proof, np. dla telefonów z multi GPU -
download i self-host dowolnego dostępnego modelu na telefon"
```

### Licencja - dual licensing:
```
User: "Jest taka licencja, nie? Że daje kod otwarty, ale zaznaczam że 
wykorzystanie komercyjne wiąże się z koniecznością posiadania odrębnej licencji?"
```

### Zaimplementowane:
- **LICENSE.md** - trzystopniowa licencja:
  1. Fair Use - darmowe do 1000 użytkowników
  2. Commercial - płatna licencja
  3. Partnership - custom dla enterprise
- **ARCHITECTURE.md** - dokumentacja zero-knowledge
- **CONTRIBUTORS.md** - szablon dla kontrybutorów

---

## Sesja 11: 2026-02-04 - v0.4.5 Pydroid FilePicker Fix
**Transkrypt:** `2026-02-04-12-15-27-chatadhd-v045-pydroid-filepicker-fix.txt`

### Problem:
- v0.4.3 - nie załącza wybranego pliku
- v0.4.4 - nie otwiera file pickera

### Decyzja:
- Revert do Kivy FileChooser (plyer nie działa w Pydroid)
- Zaimplementowany własny file picker z nawigacją

---

## Sesja 12: 2026-02-04 - v0.4.5 Memory Panel Fix
**Transkrypt:** `2026-02-04-20-25-46-chatadhd-v045-memory-panel-fix.txt`

### Problem:
- MemoryPanel otwiera się ale jest pusty
- `TypeError: MemoryNode.__init__() got an unexpected keyword argument 'embedding'`

### Fix:
- `MemoryPanel.refresh()` - teraz wywołuje `_build_tree(None)` rekurencyjnie
- Dodane `embedding: Any = None` w `MemoryNode` dataclass
- Filtrowanie tylko znanych pól przy ładowaniu z DB

---

## Sesja 13: 2026-02-04/05 - v0.4.6 Streaming + API Preview
**Transkrypt:** `2026-02-04-23-48-32-chatadhd-v046-streaming-long-messages-api-preview.txt`

### Request:
```
"Napraw wyświetlanie długich wiadomości,
Uwzględnij wyświetlanie na bieżąco w trakcie pisania"
```

### Problem - uszkodzona baza:
```
Odkryto uszkodzone rekordy w bazie - fragmenty UUID i metadanych SQLite wmieszane w tekst.
Widać fragmenty kodu który AI wygenerował ale się nie wyświetlił.
```

### Znalezione w uszkodzonej bazie (kod który miał być zaimplementowany):
- **ArtifactViewer** - pływająca ramka na fixy (zwijana, z Download/Copy/Integrate)
- **ModelSelectorPopup** - popup wyboru modelu z grupowaniem wg dostawcy

### Zaimplementowane:
- **panels_v2.py** - `MsgBubble` z `TextInput(readonly=True)` zamiast `Label`
- **panels_v3.py** - Fix dla streamingu
- **panels_v4.py** - API Preview (przytrzymaj Send 0.5s)
- **db_v2.py** - WAL mode dla ochrony przed crashami
- **api_client_v2.py** - streaming support
- **chat_engine_v2.py** - streaming callback

---

## Sesja 14: 2026-02-06 - v0.4.7/v0.4.8 Colors + Themes + Memory Fixes
**Transkrypt:** `2026-02-06-11-21-15-chatadhd-v047-panels-colors-themes-memory-fixes.txt`

### Request:
```
"Dodaj ustawienia kolorów bo zrobiłeś jasnoszare na jaśniejszym szarym.
Popraw widok hierarchii pamięci i ikonki zamiast których są krzaczki"
```

### Zaimplementowane w panels_v5.py:
- ASCII ikony (`[+]`, `|- `, `ON/OFF`) zamiast emoji
- ArtifactViewer (zwijana ramka)
- ModelSelectorPopup z dostawcami
- Ceny/rozmiary modeli
- Załączniki do wpisów pamięci
- Widok folderów jako ZIP
- ThemePopup (dark/light/amoled)

### Błąd:
```
W main.py jest wywołanie self.chat_panel._refresh_models() 
ale w panels_v5.py metoda nazywa się _ref_models
```

### Fix w panels_v6.py:
- Metoda przemianowana na `_refresh_models`

---

## Sesja 15: 2026-02-06 - v0.4.8 Color Fix + Directory Attachments
**Transkrypt:** `2026-02-06-12-48-04-chatadhd-v048-color-fix-directory-attachments.txt`

### Problem:
```
"Teraz jest białe na białym niezależnie co nie ustawisz.
Dodaj w pamięci opcję załączenia katalogów z zawartością."
```

### Zaimplementowane w panels_v7.py:
- Klasa `DarkInput` z wymuszonym ciemnym tłem
- Wszystkie `TextInput` zastąpione na `DarkInput`
- Directory attachments - możliwość załączania całych katalogów
- Unified item display dla wszystkich typów memory items

---

## Sesja 16: 2026-02-06 - v0.5.0 Graph Explorer + Versioning
**Transkrypt:** `2026-02-06-17-15-47-chatadhd-v050-graph-explorer-versioning-data-migration.txt`

### Request:
```
"Myślę, że czas na wersję 0.5.0
Zbierz aktualny kod, uwzględnij to o czym rozmawiałem z Gemini"
```

### Rozmowa z Gemini (wklejona):
```
"Tworzysz coś, co nazywam 'Prywatnym Systemem Operacyjnym Wiedzy'.
W architekturze v0.4.8, którą opisujesz..."
```

### Zaimplementowane w v0.5.0:
- **GraphExplorer** - force-directed visualization grafu wiedzy/rozmowy
- **Message Versioning** - edycja tworzy nową wersję, można przywrócić dowolną
- **Weighted nodes** - wagi węzłów pamięci
- **Node editor** - edycja metadanych węzła

### Problem - nie czyta poprzednich danych:
```
"Nie czyta ustawień ani historii z poprzednich wersji"
```

### Fix:
- Auto-migracja danych z poprzednich wersji
- Szuka w: `chatadhd_pydroid_v0.4.6`, `v0.4.5`, `dev/...`, `chatadhd_data`

---

## Sesja 17: 2026-02-06 - v0.5.0 Full Auto + OpenRouter Features
**Transkrypt:** `2026-02-06-19-02-06-chatadhd-v050-full-auto-openrouter-features.txt`

### Request:
```
"Poproszę wszystko full auto i popraw widok grafu bo tylko dwie kropki widać.
Wprowadź też wszystkie bajery jak Deep research, Zbadaj głęboko,
wszystko co się da z dokumentacji OpenRouter"
```

### Dokumentacja OpenRouter (wklejona):
- Claude 4.6 Opus - Adaptive thinking + max verbosity
- Web Search - `:online` suffix lub plugin
- Reasoning tokens - extended thinking display
- Deep Research

### Zaimplementowane:
- **Web Search toggle** - `plugins: [{id: 'web'}]`
- **Deep Research toggle** - extended search
- **Reasoning tokens display** - pokazuje tokeny myślenia
- **Większe węzły grafu** - czytelne etykiety
- **Auto-import kodu do pamięci**

---

## Sesja 18: 2026-02-06 - v0.5.1/v0.5.2 Keyboard + Voice
**Transkrypt:** `2026-02-08-02-28-01-chatadhd-v0-06-00-ui-overhaul-artifacts-presets.txt`

### Request v0.5.2:
```
"Popraw: aktualizacja dostępnego obszaru do wyświetlania po wysunięciu klawiatury – 
teraz klawiatura zasłania wpisywaną wiadomość. Dodaj też wprowadzanie głosowe"
```

### Zaimplementowane:
- **Keyboard fix** - `Window.softinput_mode = 'below_target'`
- **Voice Input** - przycisk mikrofonu, Android Speech Recognition

### Błędy zgłoszone:
- Błąd po kliknięciu elementu grafu
- Błąd voice: "JVM exception occurr"
- Artefakty wyświetlają się jako zwykły tekst

---

## Sesja 19: 2026-02-06 - v0.06.00 UI Overhaul
**Transkrypt:** `2026-02-08-02-28-01-chatadhd-v0-06-00-ui-overhaul-artifacts-presets.txt` (cd.)

### Request:
```
Napraw obsługę artefaktów, bo wyświetla jako zwykły tekst, 
a ma wpływającej ramce z przyciskami.
Zwijalne wiadomości.
Quick API panel.
Katalog do pamięci - pełna treść plików.
```

### Zmiana wersjonowania:
```
x.yy.zz - teraz sortowanie działa:
v0.05.01
v0.05.02
v0.06.00  ← current
...
Alfabetycznie = numerycznie ✓
```

### Zaimplementowane w v0.06.00:
- **MsgBubble** - collapsible messages (PREVIEW_LINES=4)
- **ArtifactBar** - detects code blocks, Copy, →Mem buttons
- **QuickAPIPanel** - 6 favorite models, 5 presets, temperature/tokens sliders
- **NodeEditorPopup** - edit node content, weight, pin
- **Directory import** - full file contents (up to 10,000 chars)

---

## Sesja 20: 2026-02-06 - v0.06.01 Import + Providers
**Transkrypt:** `2026-02-08-02-44-47-chatadhd-v0-06-02-api-editor-github-sync.txt`

### Request:
```
"Dodaj proszę importowanie rozmowy (np. z Tobą) na podstawie: 
db, json, html, mht, scrolled screenshot, itd."
```

### Request - no placeholders:
```
"Żadnych placeholderów. Implementuję pełną abstrakcję providerów z OCR i ASR"
```

### Zaimplementowane:

**engine/importer.py (693 linie):**
- `import_sqlite()` - Claude.ai, ChatGPT databases
- `import_json()` - API logs, exports
- `import_html()` - browser saves
- `import_mht()` - single-file archives (MIME multipart)
- `import_markdown()` - ## Human/## Assistant format
- `import_text()` - Human:/Assistant: patterns
- `import_screenshot()` - uses OCR providers

**engine/providers.py (467 linii):**
- `GroqASR` - whisper-large-v3-turbo
- `GoogleSpeechASR` - maxAlternatives=5, confidence scores
- `OCRSpaceProvider` - engine 2, base64 upload
- `ProviderManager` - unified interface, fallback chain

**gui/panels.py additions:**
- `ImportConversationPopup` - file browser, format detection
- `VoiceInputPopup` - audio recording
- `VoiceAlternativesPopup` - ASR alternatives selection

---

## Sesja 21: 2026-02-06 - v0.06.02 API Editor + GitHub Sync
**Transkrypt:** `2026-02-08-02-44-47-chatadhd-v0-06-02-api-editor-github-sync.txt` (cd.)

### Request:
```
"1. Edytor zapytania API musi mieć przede wszystkim możliwość edycji 
   promptu sys, wyciągu z pamięci, listy wiadomości - ogólnie wszystkiego
2. Wprowadź automatyczną synchronizację z GitHub"
```

### Zaimplementowane:

**engine/github_sync.py (411 linii):**
- `GitHubFile` dataclass (path, sha, size, status)
- `SyncConfig` dataclass (repo, branch, direction, patterns)
- `GitHubSync` class:
  - `list_remote_files()` - recursive directory listing
  - `pull_file()` / `push_file()` - single file operations
  - `sync()` - bidirectional with conflict detection
- `GitHubSyncManager` - config persistence, multiple repos

**gui/panels.py additions:**
- `APIRequestEditor` (430 linii):
  - **System tab**: editable system prompt, templates
  - **Memory tab**: full memory context, reload button
  - **Msgs tab**: message include/exclude toggle, weight sliders (0.1-2.0)
  - **Params tab**: model, temperature, max_tokens, top_p, penalties
  - Token count estimate, Copy JSON, Send
- `GitHubSyncPopup`:
  - Config selector (multi-repo)
  - Direction: bidirectional/pull_only/push_only
  - File list with checkboxes
  - Status icons: ✓ synced, ~ modified, +L new local, +R new remote

---

## Sesja 22: 2026-02-08 - v0.06.03 Bug Fixes
**Bieżąca sesja**

### Request:
```
"Graf - żeby nie wychodził poza kanwę i żeby znikał po zamknięciu.
Wyświetlanie wiadomości: Coś się popieprzyło. Lewy dolny róg – ciągle białe napisy migają.
Edytor wywołania API po długim wciśnięciu przycisku miałeś zaimplementować - Przenieś go tam z górnego paska"
```

### Zaimplementowane:
- **graph_viz.py** - Stencil clipping, canvas.clear() on stop/close
- **panels.py** - API Editor moved to long-press Send (0.6s)
- **Top bar** - removed 📝, added Log button back

---

# 3. FUNKCJE - CO ZAŻĄDANE VS CO ZAIMPLEMENTOWANE

## ✅ ZAIMPLEMENTOWANE (potwierdzone w kodzie v0.06.03):

| Funkcja | Sesja | Wersja | Lokalizacja w kodzie |
|---------|-------|--------|---------------------|
| Hierarchiczna pamięć | 3 | v2 | memory_engine.py |
| Rekurencyjna struktura (universal tree) | 4 | v3 | MemoryNode dataclass |
| Branching conversations | 1,3 | v2+ | db.py (parent_id) |
| Sliding panels | 3 | v2 | panels.py (Panel class) |
| System prompt presets | 2 | v1 | QuickAPIPanel |
| Model selector z dostawcami | 13 | v0.4.6 | ModelSelectorPopup |
| File picker (Kivy) | 11 | v0.4.5 | FilePickerPopup |
| Streaming responses | 13 | v0.4.6 | chat_engine.py |
| API Preview (long-press) | 13 | v0.4.6 | ChatPanel |
| WAL mode (DB protection) | 13 | v0.4.6 | db.py |
| Themes (dark/light/amoled) | 14 | v0.4.7 | ThemePopup |
| ASCII icons (no emoji) | 14 | v0.4.7 | panels.py |
| Directory attachments | 15 | v0.4.8 | MemoryPanel |
| Graph Explorer | 16 | v0.5.0 | graph_viz.py |
| Message versioning | 16 | v0.5.0 | db.py |
| Weighted nodes | 16 | v0.5.0 | MemoryNode.weight |
| Web Search toggle | 17 | v0.5.0 | ChatPanel |
| Deep Research toggle | 17 | v0.5.0 | ChatPanel |
| Reasoning tokens | 17 | v0.5.0 | StreamingBubble |
| Keyboard fix | 18 | v0.5.2 | main.py |
| Collapsible messages | 19 | v0.06.00 | MsgBubble |
| ArtifactBar (code detection) | 19 | v0.06.00 | ArtifactBar |
| QuickAPIPanel | 19 | v0.06.00 | QuickAPIPanel |
| Directory import (full content) | 19 | v0.06.00 | MemoryPanel._add_dir |
| Conversation import (7 formats) | 20 | v0.06.01 | importer.py |
| OCR providers (ocr.space) | 20 | v0.06.01 | providers.py |
| ASR providers (Groq, Google) | 20 | v0.06.01 | providers.py |
| VoiceInputPopup | 20 | v0.06.01 | panels.py |
| Full API Editor | 21 | v0.06.02 | APIRequestEditor |
| GitHub Sync | 21 | v0.06.02 | github_sync.py |
| Graph clipping | 22 | v0.06.03 | graph_viz.py |

## ⚠️ CZĘŚCIOWO ZAIMPLEMENTOWANE / DO WERYFIKACJI:

| Funkcja | Sesja | Problem |
|---------|-------|---------|
| Voice input (Android) | 18 | "JVM exception" - może nie działać na wszystkich urządzeniach |
| Zero-knowledge encryption | 9,10 | Kod v1.0-zk istnieje ale nie jest zintegrowany z główną linią |
| Semantic search (TF-IDF) | 6 | Kod od GPT zintegrowany, ale wymaga sentence-transformers |
| Native file picker (plyer) | 7,11 | Fallback do Kivy - plyer nie działa w Pydroid |

## ❌ ZAŻĄDANE ALE NIE ZAIMPLEMENTOWANE:

| Funkcja | Sesja | Status |
|---------|-------|--------|
| Automatyczne wspomnienia z ciekawszych wydarzeń | 1 | Nigdy nie zaimplementowane |
| Mobile inference (self-host model na telefon) | 10 | Tylko plan/architektura |
| Remote attestation / reproducible builds | 9 | Tylko dyskusja |
| Export konwersacji (odwrotność importu) | - | Brak |
| Auto-sync GitHub (timer/on-change) | 21 | Tylko manual sync |

---

# 4. AKTUALNA STRUKTURA KODU (v0.06.03)

```
chatadhd_v0.06.03/           (5738 linii)
├── main.py                  (152)   - App entry, keyboard handling
├── engine/
│   ├── __init__.py          (0)
│   ├── chat_engine.py       (272)   - API calls, streaming, web_search, deep_research
│   ├── config.py            (62)    - JSON config management
│   ├── db.py                (274)   - SQLite wrapper, WAL mode
│   ├── github_sync.py       (411)   - GitHub API, bidirectional sync
│   ├── importer.py          (693)   - Import 7 formats (DB, JSON, HTML, MHT, MD, TXT, Screenshot)
│   ├── memory_engine.py     (166)   - Hierarchical memory with graph
│   ├── models.py            (65)    - Model list management, auto-refresh
│   └── providers.py         (467)   - OCR/ASR abstraction (Groq, Google, ocr.space)
└── gui/
    ├── __init__.py          (1)
    ├── graph_viz.py         (424)   - Graph visualization with clipping
    └── panels.py            (2751)  - All UI components (22 classes)
```

## Klasy w panels.py:

| Klasa | Linie | Funkcja |
|-------|-------|---------|
| LogBuffer | 64-72 | Circular log buffer |
| RBtn | 73-79 | Styled button |
| Card | 80-89 | Styled box |
| DarkInput | 90-97 | Dark text input |
| Panel | 98-167 | Slide-out base |
| NodeEditorPopup | 168-235 | Edit graph nodes |
| QuickAPIPanel | 236-373 | Quick settings (6 models, 5 presets) |
| SettingsPopup | 374-457 | API keys (4), config |
| ThemePopup | 458-480 | Dark/AMOLED |
| FilePickerPopup | 481-519 | File browser |
| LogViewer | 520-541 | Debug logs |
| ModelSelectorPopup | 542-612 | Model picker with providers |
| MsgBubble | 613-729 | Collapsible message |
| ArtifactBar | 730-776 | Code artifact (Copy, →Mem) |
| StreamingBubble | 777-826 | Streaming display |
| MemoryPanel | 827-1051 | Memory tree |
| ConvPanel | 1052-1089 | Conversation list |
| ChatPanel | 1090-1575 | Main chat UI |
| ImportConversationPopup | 1576-1708 | Import dialog |
| VoiceInputPopup | 1710-1948 | Voice recording |
| VoiceAlternativesPopup | 1950-2001 | ASR alternatives |
| APIRequestEditor | 2003-2432 | Full API editor (4 tabs) |
| GitHubSyncPopup | 2434-2680+ | GitHub sync UI |

---

# 5. POTENCJALNIE ZGUBIONE FUNKCJE

## Z v1.0-zk (osobna gałąź - NIE ZINTEGROWANE):

### core/semantic.py (8422 bajtów) - Semantic Analyzer:
```python
class SemanticAnalyzer:
    """
    Multi-level semantic extraction:
    1. Pattern-based NER (fast, local, no dependencies)
    2. LLM-based extraction (optional, more accurate)
    3. Embedding-based similarity (for linking)
    """
    
    def extract_entities_pattern(self, text) -> List[ExtractedEntity]:
        # Regex patterns for: email, url, date, time, money, phone, hashtag, mention
        
    def extract_relations(self, text) -> List[ExtractedRelation]:
        # Subject-predicate-object extraction
```

**Status:** NIE MA W v0.06.03! Cały semantic analysis pipeline jest zgubiony.

### core/crypto.py (4340 bajtów) - Zero-Knowledge Encryption:
```python
class CryptoEngine:
    """
    AES-256-GCM encryption
    PBKDF2 key derivation
    Server NEVER has access to plaintext or keys
    """
    
    def encrypt(self, plaintext: str, password: str) -> EncryptedBlob
    def decrypt(self, blob: EncryptedBlob, password: str) -> str
```

**Status:** NIE MA W v0.06.03! Zero-knowledge encryption nie jest zintegrowane.

### core/graph.py (9393 bajtów) - Advanced Graph:
```python
# Bardziej zaawansowany graf niż w graph_viz.py
# Prawdopodobnie zawiera dodatkowe funkcje linkowania
```

**Status:** Częściowo - graph_viz.py ma tylko wizualizację, nie pełną logikę grafu.

---

## Z sesji 6 (kod od GPT - nie wiem czy w pełni zintegrowany):

```python
# SelectorEngine - semantic search z fallbackami
# sentence-transformers → TF-IDF → keywords

# IterativePromptBuffer - agregacja etapów promptu

# FileParser - OCR i transkrypcja (teraz w providers.py/importer.py)
```

**Status:** Częściowo - selector.py istniał w v0.4.0-dev, ale nie widzę go w v0.06.03.
Semantic search może być zgubiony!

## Z sesji 9-10 (v1.0-zk):

```python
# core/crypto.py - Zero-knowledge encryption (AES-256-GCM)
# core/sync.py - E2E encrypted sync
```

**Status:** Osobna gałąź (v1.0-zk). Nie zintegrowane z główną linią pydroid.

## Z sesji 13 (znalezione w uszkodzonej bazie):

```python
# ArtifactViewer - pływająca ramka na fixy (zwijana, z Download/Copy/Integrate)
```

**Status:** Zaimplementowane jako ArtifactBar (uproszczona wersja) w v0.06.00.

## Z sesji 1 (nigdy nie zaimplementowane):

```
"wspomnienia na życzenie i automatyczne z ciekawszych wydarzeń"
```

**Status:** Brak automatycznego tworzenia wspomnień.

---

# PODSUMOWANIE

## Ewolucja projektu:

```
2026-01-21  Pomysł na router API
    ↓
2026-01-23  openrouter_gui.py (jeden plik)
    ↓
2026-01-23  ChatADHD v2 (hierarchiczna pamięć)
    ↓
2026-01-24  v3 (universal tree, standalone)
    ↓
2026-01-27  v3.0.1 (ButtonBehavior fix)
    ↓
2026-01-29  v0.4.0-dev (modular, git)
    ↓
2026-01-30  v0.4.1/v0.4.2 (GUI fixes, flat ZIP)
    ↓
2026-02-02  v0.4.3/v0.4.4 (KOD≠DANE architecture)
    ↓
2026-02-04  v1.0/v1.0-zk (zero-knowledge - osobna gałąź)
    ↓
2026-02-04  v0.4.5 (file picker fix)
    ↓
2026-02-04  v0.4.6 (streaming, API preview)
    ↓
2026-02-06  v0.4.7/v0.4.8 (colors, themes, directory attachments)
    ↓
2026-02-06  v0.5.0 (graph, versioning, OpenRouter features)
    ↓
2026-02-06  v0.5.1/v0.5.2 (keyboard, voice)
    ↓
2026-02-06  v0.06.00 (collapsible messages, artifacts, QuickAPI)
    ↓
2026-02-06  v0.06.01 (import, providers)
    ↓
2026-02-06  v0.06.02 (API editor, GitHub sync)
    ↓
2026-02-08  v0.06.03 (bug fixes)
```

## Do sprawdzenia:
1. Czy semantic search (selector.py) jest w kodzie?
2. Czy v1.0-zk warto zintegrować z główną linią?
3. Voice input - czy działa na Androidzie?

---

*Raport wygenerowany z analizy 22 sesji rozwojowych*
*Data: 2026-02-08*
