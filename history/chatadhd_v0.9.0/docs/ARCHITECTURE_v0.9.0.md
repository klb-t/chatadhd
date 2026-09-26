# ChatADHD v0.9.0 — architektura

**Status**: draft, pierwsze podejście
**Data**: 2026-04-17
**Motto**: wszystko można, nic nie trzeba

---

## Dwie ortogonalne zmiany

**A. Reforma systemu paneli (infrastruktura).** Dotychczas ChatADHD miał panele wbudowane na sztywno w `gui/` (chat_panel, conv_panel, memory_panel, graph_viz itd.). Każdy panel to klasa dziedzicząca po Kivy widget, podłączana w `main.py` do konkretnych slotów layoutu. Rozszerzanie = kolejny specjalny przypadek.

0.9.0 wprowadza **panel-system**: generyczną warstwę gdzie każdy panel jest:
- rejestrowalny z zewnątrz (plug-in pattern),
- niezależny w state od innych,
- osadzalny w czterech trybach (docked / floating / floating-locked / hidden),
- serializowalny do layoutu JSON (save/load/share).

**B. Video pipeline jako pierwszy obywatel nowego panel-systemu.** Nie jako osobna apka, tylko jako zestaw paneli + engine. Panele: `video_preview`, `scene_graph_editor`, `portfolio_panel`, `model_catalog_panel`, `generation_queue_panel`. Engine: `video_pipeline`, `scene_graph`, `models_video`.

A musi być zrobione przed B. A nie dotyka AI. B używa A.

---

## A. Panel-system

### Invariant

Każdy panel `P` spełnia:

1. `P` ma unikalne `panel_id: str` i `panel_type: str`.
2. `P` ma własny state serializowalny do JSON przez `to_dict()` / `from_dict()`.
3. `P` ma cztery możliwe tryby osadzenia (`MountMode`): `DOCKED`, `FLOATING`, `FLOATING_LOCKED`, `HIDDEN`.
4. `P` publikuje sygnały przez event bus (patrz `engine/events.py` który już jest w 0.07.x) — nigdy nie woła bezpośrednio innych paneli.
5. `P` może paść (exception w `update()`), a reszta systemu żyje.

### Cztery tryby

| Tryb | Pozycja | Rozmiar | Widoczność |
|---|---|---|---|
| DOCKED | Zarządzana przez dock-layout | Zarządzany | Tak |
| FLOATING | (x, y) w oknie głównym | (w, h) | Tak, zawsze on-top |
| FLOATING_LOCKED | (x, y) zamrożone | (w, h) zamrożone | Tak |
| HIDDEN | — | — | Nie (state żyje) |

Ortogonalnie: **collapsed** (zwinięty do headera) ↔ **expanded**. Dotyczy wszystkich trybów prócz HIDDEN.

### Layout JSON

```json
{
  "layout_version": 1,
  "layout_name": "AoD Video Production",
  "panels": [
    {
      "panel_id": "chat_main",
      "panel_type": "chat",
      "mount_mode": "DOCKED",
      "dock_position": "center",
      "collapsed": false,
      "panel_state": { "active_conversation_id": "..." }
    },
    {
      "panel_id": "video_preview_1",
      "panel_type": "video_preview",
      "mount_mode": "FLOATING_LOCKED",
      "position": [1200, 200],
      "size": [640, 360],
      "collapsed": false,
      "panel_state": { "current_clip_path": "..." }
    },
    {
      "panel_id": "scene_graph_editor_1",
      "panel_type": "scene_graph_editor",
      "mount_mode": "FLOATING",
      "position": [50, 50],
      "size": [800, 400],
      "collapsed": false,
      "panel_state": { "active_graph_id": "..." }
    }
  ]
}
```

### Presety layoutu (wbudowane w wersję 0.9.0)

- `minimal` — tylko chat + lista czatów (Twoja prośba „domyślnie wszystko wyłączone, zostaje główny czat").
- `classic_0.07` — layout z 0.07.x, dla ciągłości.
- `aod_video_production` — chat w centrum, scene_graph_editor floating po lewej, video_preview floating-locked po prawej, portfolio_panel docked na dole.
- `research` — chat + memory + graph_viz + conv_list (typowy tryb research).
- `dev` — wszystko włączone.

User może dodawać własne presety. Lądują w `data/presets/layouts/<name>.json`.

### Wpływ na istniejący kod

Istniejące panele (`chat_panel.py`, `memory_panel.py`, itd.) dostają **adapter** do `IPanel` interface. To nie jest rewrite — to wrap. Istniejąca logika panelu nietknięta, tylko wystawiona przez wspólny interfejs.

---

## B. Video pipeline

### Invariant

**Pipeline = graf zależności, nie liniowa sekwencja.** Linia czasu to projekcja grafu.

**Każda scena ma ≥1 anchor z portfolio.** Anchor to hard reference (konkretne zdjęcie), nie soft hint (tekstowy opis). Bez anchora nie ma spójności międzyujęciowej.

**Spójność jest policzalna.** Embedding wizualny → odległość klip ↔ anchor. Próg konfigurowany przez usera, domyślnie np. 0.3 (CLIP cosine distance).

**Każda faza zapisuje artefakty na dysku.** Można wrócić do fazy N bez regeneracji faz N+1, N+2, ... dopóki invarianty się nie zmieniły.

### Cztery fazy

**Faza 1: Struktura.** Graf scen + anchory + constraints + opisy. Tylko tekst (LLM). Tanie. Iterowalne.

**Faza 2: Klucze.** Dla każdej sceny: statyczny keyframe (image-gen). Związany z anchorem przez referencję. Odrzucasz i regenerujesz aż keyframes pod-set jest spójny. Średnio tanie. Iterowalne.

**Faza 3: Ruch.** Dla każdej sceny: image→video z keyframe jako pierwszego kadru. Drogie. Uruchamiane tylko na zatwierdzonych keyframes.

**Faza 4: Skład.** Compositor: łączy klipy, synchronizuje z audio, dodaje przejścia, ewentualne efekty (dla AoD: polirytmiczny zoom).

### Scene graph

```
SceneGraph:
  id: str
  name: str
  type: "linear" | "branching"
  nodes: List[Scene]
  edges: List[SceneEdge]  # dla branching; dla linear edges = pairs (i, i+1)

Scene:
  id: str
  description: str
  duration_seconds: float
  anchors: List[PortfolioItemId]  # ≥1
  characters: List[CharacterId]
  location: Optional[LocationId]
  style_tokens: List[str]
  audio_sync: Optional[AudioSyncSpec]  # np. polirytmiczny zoom dla AoD
  constraints: Dict[str, Any]
  artifacts:
    keyframe_path: Optional[str]  # faza 2
    clip_path: Optional[str]      # faza 3
    embedding_keyframe: Optional[np.ndarray]
    embedding_clip: Optional[np.ndarray]
    model_used: Optional[str]
    generation_cost_usd: Optional[float]
  state: "pending" | "keyframe_ready" | "clip_ready" | "failed" | "flagged"

SceneEdge:
  from_scene: SceneId
  to_scene: SceneId
  condition: Optional[str]  # dla branching: warunek widza (np. "preferred_pace=slow")
  transition: str  # "cut" | "dissolve" | "fade" | ...
```

### Katalog modeli wideo

Jak `engine/models.py` w 0.07.x — registry. Każdy model ma:

```
VideoModel:
  name: str
  provider: str  # "openrouter" | "replicate" | ...
  capabilities:
    input_modality: Set["text", "image", "image+text", "video"]
    output_duration_max_s: float
    max_resolution: Tuple[int, int]
    supports_character_consistency: bool
    supports_audio: bool
  cost_per_second_usd: float
  avg_generation_time_s: float
  quality_tier: int  # 1-5, subiektywne, aktualizowane na podstawie observed quality
```

Wybór modelu dla danego zadania: filtruj pod constraints → sortuj wg (quality, cost) → fallback chain.

### Pipeline API (szkielet)

```python
class VideoPipeline:
    def __init__(self, data_dir: Path, model_registry: ModelRegistry,
                 embedding_provider: IEmbeddingProvider):
        ...

    # Faza 1
    def create_scene_graph(self, prompt: str, graph_type: str = "linear",
                           portfolio: List[PortfolioItem] = None) -> SceneGraph: ...

    def edit_scene_graph(self, graph_id: str, edits: List[GraphEdit]) -> SceneGraph: ...

    # Faza 2
    def generate_keyframe(self, scene_id: str, model_hint: Optional[str] = None) -> Scene: ...

    def regenerate_keyframe(self, scene_id: str, feedback: str = "") -> Scene: ...

    # Faza 3
    def generate_clip(self, scene_id: str, model_hint: Optional[str] = None) -> Scene: ...

    # Faza 4
    def compose_film(self, graph_id: str, audio_path: Optional[str] = None,
                     output_path: Path = None, sync_spec: Optional[AudioSyncSpec] = None) -> Path: ...

    # Diagnostics
    def measure_coherence(self, graph_id: str) -> CoherenceReport: ...
    def get_cost_estimate(self, graph_id: str, phase: int) -> CostEstimate: ...
```

Każda metoda publikuje zdarzenia na event busie (`video.scene.keyframe_ready`, `video.scene.failed`, ...). Panele subskrybują.

### Fallback chain

Dla fazy 2 i 3: jeśli wybrany model zwróci artefakt o odległości `embedding(anchor, result) > threshold`, spróbuj kolejnego modelu z katalogu. Jeśli wyczerpane — flaguj scenę, pipeline idzie dalej z placeholderem. User widzi flag w `scene_graph_editor`, może wejść ręcznie.

### Persistence

- `data/video/graphs/<graph_id>.json` — metadata grafu.
- `data/video/keyframes/<scene_id>.png` — wygenerowane keyframes.
- `data/video/clips/<scene_id>.mp4` — wygenerowane klipy.
- `data/video/embeddings/<scene_id>.npy` — embeddingi dla coherence check.
- `data/video/renders/<graph_id>_<timestamp>.mp4` — finalne składy z fazy 4.

Spójne z istniejącą konwencją `~/Documents/ChatADHD/` jako data root.

---

## Use-cases — jedna architektura, różne konfiguracje

### AoD — „Krew jak smoła" teledysk

- `scene_graph.type = "linear"`, N scen.
- `portfolio` = mood-board AoD (jeden nosacz jako anchor dla wszystkich scen, plus dissolved-motifs jako style anchors).
- `audio_sync` = konkretny track AoD, zoom rytmiczny 0.5 Hz → 8 Hz w apogeum.
- Faza 4 compositor: aplikuje polirytmiczny zoom na beacie (nie płynny, dyskretny).
- Dokumentalny styl: `style_tokens = ["hyper-realistic wildlife documentary", "static camera", "national geographic"]`.
- Każda scena ma constraint: `camera = "static", subject = "proboscis monkey"`.

### AoD — drugi teledysk (z postaciami niewidzialnymi)

- Zostaje do ustalenia które to był utwór.
- Niewidzialność postaci jako style constraint? → zapewne potrzeba edge-case'a w pipeline.

### Cisza Beta — ekranizacja (pół-ręczna, test)

- `scene_graph.type = "linear"` na pierwszy rzut — jedna ścieżka narracyjna.
- Personalizacja = **poza scope** 0.9.0. To jest test pipeline'u na tekście-powieści.
- Portfolio = generowane obrazy postaci + lokacji (pre-faza: content brief → image-gen → portfolio).
- Pół-ręcznie: scene graph tworzony przez user (na podstawie spisu treści Ciszy Beta), pipeline generuje keyframes i clips.
- Compositor: brak synchronizacji z audio (no polirytmia), podkład narracji jako TTS albo nagrany głos.

### Uniwersalność

Te różnice to **konfiguracja**, nie osobny kod. Algorytm pipeline'u jest jeden. Graf, portfolio, audio_sync są parametrami.

**Bonus**: jeśli `IGenerator` jest abstrakcyjny (image-gen, video-gen, text-gen), metaksiążka może używać tego samego pipeline'u z backend-generator = text-gen. Scene graph → plot graph. Keyframes → kluczowe sceny w prozie. Clips → rozwinięte rozdziały. Compositor → składanie wersji książki pod preferencje.

To jest bardzo silna unifikacja, ale **poza scope 0.9.0**. Zaznaczam jako invariant architektury.

---

## Co NIE jest w 0.9.0

- Personalizacja filmu pod widza (branching scene graph + runtime selector). Silnik ma wsparcie, panel-editor nie.
- Compositor z synchronizacją audio-wideo na beat (faza 4 w MVP eksportuje listę klipów do zewnętrznego montażu).
- Metaksiążka backend.
- Graph/Score view dla scene_graph_editor (na start tylko timeline-linear).
- Wielomodelowa generacja parallel (na start szeregowa per scena).
- Własne modele on-device. Wszystko przez OR albo inne API.

Wszystko to jest ścieżką na 0.10.x+.

---

## Kolejność implementacji (iteracje wewnątrz 0.9.0)

1. **0.9.0-a1** — panel-system (IPanel, PanelRegistry, MountMode, LayoutManager). Adaptery dla istniejących paneli. Layout presets. **Bez video.** Test: istniejące panele działają po refaktorze + user może je przełączać między trybami.

2. **0.9.0-a2** — `video_preview` panel + `portfolio_panel` (read-only viewer portfolio). Panele istnieją w panel-systemie, pokazują placeholder content. **Bez pipeline.** Test: user może otworzyć video_preview jako floating-locked, załadować plik mp4, odtworzyć.

3. **0.9.0-a3** — `engine/models_video.py` + `engine/video_pipeline.py` (faza 2 — keyframes only) + `engine/scene_graph.py`. Integracja z jednym image-gen modelem. **Bez clips, bez compositor.** Test: z promptu powstaje scene graph, każda scena dostaje keyframe, spójność mierzona.

4. **0.9.0-a4** — `scene_graph_editor` panel (linear timeline). Integracja z pipeline. User tworzy scene graph w UI, uruchamia generation, widzi keyframes jako strip. Test: AoD track — użytkownik układa 8 scen, generuje keyframes, 7 z 8 spójne z portfolio.

5. **0.9.0-a5** — faza 3 (clips). Integracja z image→video model. Test: scene graph → clips → user ogląda clip-by-clip w video_preview.

6. **0.9.0-a6** — faza 4 minimal (export listy klipów + script ffmpeg concat). Brak polirytmicznego zoom. Test: pełny run „Krew jak smoła" — scene graph → keyframes → clips → concat mp4.

7. **0.9.0-b1+** — polirytmiczny zoom (AoD-specific filter w fazie 4), branching scene graph, i dalej.

Każda iteracja jest testowalnym deliverable. Poprzednia iteracja działa po wdrożeniu następnej.

---

## Otwarte pytania

1. **Kivy panel-system**. Kivy nie ma natywnych dockable/floating. Muszę zbudować. Koszt: ~kilka dni pracy na solidną implementację. Alternatywa: dialog-based (Popup) dla floating, nie pełne okna. Start od Popup-based, rozszerz do prawdziwego floating w iteracji.

2. **Embedding provider** dla coherence check. Lokalny CLIP (ciężki) vs. API (OpenAI embeddings, Cohere, Jina). Propozycja: abstract `IVisualEmbedding`, domyślnie API, lokalny CLIP jako opcja.

3. **Limit klipu**. OR modele typowo 3–10s. Długi film (45 min) = ~270–540 klipów. Koszt: przy $0.5/klip → $135-270 per film. To jest do budżetowania per-graph przed fazą 3.

4. **Drugi teledysk (niewidzialne postacie)**. Do jakiego utworu? Pytanie w tekście, nie w kodzie.

5. **Manifest AoD**. Nadal nie mam. Czy ma wpływ na architekturę (np. konkretne style_tokens defaults)?

---

## Fundamenty kodeksu — check

- [x] Rzeczywistość (modele, portfolio, sceny jako dane) oddzielona od decyzji (algorytm wyboru modelu).
- [x] Każdy panel wystawia invariant (IPanel) i jest queryable.
- [x] Fallback chain dla modeli z metadanymi.
- [x] Skalowanie: od 1 sceny (test) do setek (długometraż).
- [x] Multiplatformowość: Kivy cross-platform, pipeline czysto Python, artefakty na dysku.
- [x] User może wszystko skonfigurować (layout, modele, constraints) bez dostępu do kodu.
- [x] Constrainty (user wybór) oddzielne od optymalizacji (algorytm).
- [x] „Wszystko można, nic nie trzeba": domyślnie minimal layout + sensowne modele; user może dowolnie rozszerzać.
