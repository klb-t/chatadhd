# Analiza odzyskanych wersji 0.8.3 i 0.9.0 — co jest do wykorzystania

Data: 2026-09-26. Metoda: trzech niezależnych czytelników przeczytało oba
snapshoty w całości (`history/`), uruchomiło kod w izolowanym katalogu
(Python 3.11, zmockowane HTTP, Kivy w venv + xvfb) i porównało go plikowo z
repo (0.7.x) oraz z Loomem. Tam, gdzie piszę „zweryfikowane”, był uruchomiony
test; „przeczytane” oznacza tylko lekturę kodu.

Decyzja właściciela (2026-09-26): **nie scalamy starych gałęzi — budujemy od
nowa w Loomie, z testami.** Dlatego werdykty poniżej mówią, co przenosimy jako
*idee, kontrakty i dane*, a nie jako kod.

## TL;DR

- **Pipeline wideo z 0.9.0 nie działa end-to-end w domyślnej konfiguracji**
  (zweryfikowane): bramka spójności z hash-embeddingiem odrzuca każdą klatkę
  kluczową; faza 3 nie wysyła klatki kluczowej do modelu (czyli to T2V, nie
  I2V); UI nie jest podłączone do silnika. Przy pominiętej bramce i
  zmockowanym OpenRouterze przechodzi fazy 1–4.
- **Najcenniejsze w 0.9.0 to pomysły i dane**, nie kod: czterofazowa
  rafinacja „tanio → drogo” z bramką i kosztorysem przed każdym etapem;
  `film_structure.json` (653 linie) jako schemat IR filmu; `prompt_compiler`
  (deterministyczne renderowanie IR → prompt z trace'em); panele i układy jako
  dane (`panel_system`, presety layoutów); zasada „każde zdarzenie śledzalne”.
- **Najcenniejsze w 0.8.3 to `engine/attachments.py`**: zasób → reprezentacje
  (z pewnością i ostrzeżeniami) → widok do promptu dobrany do zapytania.
  Loom nie ma nic podobnego. Do tego: podgląd/edycja żądania API jako
  wykonywalny artefakt, poprawne MHT (quoted-printable), import z URL.
- **Linia wersji jest inna niż nazwy**: 0.8.3 odgałęzia się od **0.7.9**, a
  0.9.0 od **0.7.10 sprzed poprawki abspath** (zweryfikowane per plik względem
  gita). To dobry, prawdziwy przypadek testowy dla self-discovery.
- **Bezpieczeństwo (zweryfikowane)**: klucz OpenRouter wysyłany do obcych
  hostów (URL-e pobierania), wstrzykiwanie poleceń w eksportowanym skrypcie
  `.sh` (compositor), DoS w `expr_eval` (nieograniczone potęgowanie/stringi),
  debug-serwer bez kontroli nagłówka Host (DNS rebinding). Żadne z tych nie
  przechodzi do Looma — przepisujemy z poprawnymi kontraktami.
- **Wiele funkcji z raportu historycznego „oscyluje”**: pojawia się, znika,
  wraca (edytor API: podgląd v0.4.6 → 4 zakładki v0.06.02 → stub „TODO” w
  0.7.10 → surowy JSON w 0.8.3 → stub w 0.9.0). Status funkcji musi być
  per gałąź i wersja, z relacją „utracone ponownie”.

## Linia wersji

```
v2/v3 (2026-01-23/24) → 0.4.0 … 0.4.8 → 0.5.x → 0.06.00 … 0.06.03 (2026-02-08)
  → 0.7.0 … 0.7.9 ─┬─→ 0.7.10 (1e3fa2b) ─┬─→ ddcab9e (abspath, 2026-03-06) → repo main
                   │                      └─→ 0.9.0 (2026-04-17/18: wideo, panele)
                   └─→ 0.8.x / „0.8.3” (2026-03-17/21: załączniki, edytor żądań)
repo main (0.7.10) → Loom (C++20, 2026-09) — port 1:1 + provenance/zadania/archiwum
```

## Moduły: dojrzałość i werdykt

| Moduł | Co robi | Dojrzałość | Werdykt | Gdzie w Loomie |
|---|---|---|---|---|
| 0.9 `video_pipeline.py` | orkiestracja 4 faz (struktura → klatki → klipy → montaż) | zepsuty | idea | zadania `video.*` na TaskEngine + BlobStore; bramka jako polityka |
| 0.9 `scene_graph.py` | graf scen, portfolio, przejścia z warunkami | działa | idea | IR artefaktu „film” (wyparty przez film_structure) |
| 0.9 `data/schemas/film_structure.json` | schemat IR filmu (postaci, lokacje, sceny, motywy, krzywe) | działa | **zasób danych** | paradygmat „film” w bibliotece paradygmatów |
| 0.9 `prompt_compiler.py` | IR → tekst promptu, sekcje, trace | działa | **przepisać** | renderer ContextSet (sekcje jako dane) |
| 0.9 `expr_eval.py` | wyrażenia dla krzywych parametrów | działa, niebezpieczny | **przepisać** | mały parser Pratta tylko na liczbach, limity |
| 0.9 `async_jobs.py` | submit → poll → download | działa | idea | TaskEngine już to ma (checkpointy, ponowienia) |
| 0.9 `openrouter_video.py` | adapter OR Video | niezweryfikowane | przepisać | dostawca w ProviderRegistry; auth tylko do bazowego hosta |
| 0.9 `models_video.py` | rejestr modeli wideo + sync z API | działa | idea | manifesty możliwości ze źródłem i pewnością (API/użytkownik/heurystyka) |
| 0.9 `compositor.py` | ffmpeg → mp4 albo eksport `.sh` | działa, wstrzyknięcie | przepisać | adapter: argv + timeout, bez powłoki |
| 0.9 `embeddings.py` | hash-embedding zamiast CLIP | zepsuty | odrzucić | brak możliwości ⇒ „niezweryfikowane”, nie fałszywa metryka |
| 0.9 `panel_system.py`, presety layoutów | panele i układ jako dane, placeholder awarii | działa | **kontrakt + dane** | UI-IR (MEGA MASTER §4.11): web + Android |
| 0.9 `dock_renderer.py`, `legacy_adapter.py` | Kivy: dokowanie, owijanie starych paneli | zepsuty | odrzucić / idea | tylko idea regionów dokowania |
| 0.9 `live_debug.py`, `logging_setup.py`, `events.py` | serwer debug, log per uruchomienie, śledzenie emitów | działa | idea | Loom ma bufor logów + SSE; brakuje sinka do pliku |
| 0.8 `attachments.py` | zasób → reprezentacje → widok do promptu | działa | **przepisać (priorytet)** | ekstraktory jako dostawcy możliwości; reprezentacje = artefakty pochodne |
| 0.8 `chat_engine.py` (request spec) | budowa/edycja/wykonanie żądania | działa | przepisać | żądanie jako edytowalny, hashowany artefakt zadania |
| 0.8 `importer.py` (MHT, sniffing) | poprawne MHT, lepsze wykrywanie | działa | przepisać | Loom ma naiwne MHT z 0.7.10 — do poprawy |
| 0.8 `import_panel.py` (URL) | import z linku | niezweryfikowane | przepisać | adapter źródła „URL” z provenance |
| 0.8 `models.py` | uproszczony rejestr modeli | regresja | odrzucić | nadpisywał `models.json` bez cen |

## Status funkcji z raportu historycznego (skrót)

| Funkcja | Stan dziś |
|---|---|
| Czat wielomodelowy, strumieniowanie, gałęzie/wersje, pamięć hierarchiczna, import 7+ formatów, OCR/ASR, WAL, graf | zaimplementowane (repo i Loom) |
| Presety system promptu (Default/Epistemic/Coding/Legal/Research) | **utracone wszędzie** |
| Pełny edytor żądania API (4 zakładki) | **utracony**; 0.8.3 ma surowy JSON |
| Plik logu | utracony w repo; jest tylko w 0.9.0 |
| Automatyczna migracja ze starych katalogów danych | utracona |
| `IterativePromptBuffer` | utracony (najbliżej: ContextSet w Loomie) |
| Motyw jasny | utracony (są dark/AMOLED) |
| Ikony ASCII zamiast emoji (Pydroid) | **regresja** — emoji wróciły w repo, 0.8.3 i 0.9.0 |
| Szyfrowanie zero-knowledge | biblioteka jest (repo, Loom), nieużywana w aplikacji |
| Synchronizacja E2E, modele na telefonie, powtarzalne buildy | niezaimplementowane |
| ArtifactViewer | uproszczony do ArtifactBar |
| Selektor modeli z cenami | jest w 0.7.10 i 0.9.0, brak w 0.8.3, w Loomie tylko API |

## Co to znaczy dla silnika self-discovery

1. **Prawdziwa prawda wzorcowa.** Linia wersji (0.8.3 ← 0.7.9, 0.9.0 ← 0.7.10
   sprzed abspath) i tabela statusów powyżej to test, który silnik musi
   zdać sam: odtworzyć punkty rozgałęzień per plik (najbliższa rewizja po
   diffie + głosowanie) i statusy funkcji zgodne z raportem właściciela.
2. **Oscylacja statusu** jako wzorzec pierwszej klasy: `lost_again`,
   `restored`, `superseded` per gałąź i wersja.
3. **Niezmienniki właściciela odzyskane z raportu** (np. „bez emoji na
   Pydroidzie”, „bez placeholderów”, „wersje sortowalne x.yy.zz”, „jeden
   płaski zip”) — do sprawdzania nowego kodu jak testy.
4. **Poziomy dowodu już istnieją w 0.9.0**: `Quote.verified` („LLM może
   zaproponować, tylko użytkownik weryfikuje”) i kompilator promptu, który
   wpuszcza tylko zweryfikowane cytaty — uogólniamy to na cały system.
5. **Rafinacja etapowa jako paradygmat międzydomenowy**: struktura (tanio) →
   klucze → rozwinięcie (drogo) → kompozycja, z bramką i kosztorysem — ten sam
   szkielet dla filmu, muzyki, książki i kodu.
6. **Uczciwa degradacja**: brak możliwości ⇒ niższy poziom dowodu
   („niezweryfikowane”), nigdy fałszywa metryka.
