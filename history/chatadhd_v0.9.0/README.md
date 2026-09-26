# ChatADHD v0.9.0 — drugie podejście

**Status**: spec + scaffolding + docs-informed decisions + sanity-tested core.
**Data**: 2026-04-17
**Od**: v0.07.10 (ostatnia stabilna w repo `chatadhd-main`).

---

## Co się zmieniło od pierwszego podejścia

Po przeczytaniu dokumentacji OpenRouter Video API (launch 2026-04-15/16), dokumentacji Kivy FloatLayout i istniejącego kodu 0.07.10, podjąłem decyzje i uogólniłem całość zgodnie z fundamentalnymi zasadami.

### Kluczowe ustalenia z dokumentacji OR Video

1. **API jest async**. Submit zwraca `id + polling_url + status=pending`. Poll ~30s aż `completed`. Oddzielny endpoint download. To **nie pasuje** do synchronicznego `IGenerator.generate_video()` jak go zaprojektowałem.
2. **Unified schema endpoint**. Jeden `POST /videos` obsługuje T2V, I2V (`frame_images`), R2V (`input_references`). OR routuje. Nie trzeba dzielić modeli na T2V vs I2V.
3. **Capabilities dynamiczne**. `/api/v1/videos/models` zwraca żywe `supported_resolutions`, `supported_aspect_ratios`, `pricing_skus`. Hardkodowanie modeli było błędem — sync z API.
4. **Modele live (16.04.2026)**: Veo 3.1 (od $0.40/s, do 140s przez scene extension), Sora 2 Pro, Seedance 2.0/1.5, Wan 2.7/2.6.
5. **ZDR niedostępny** dla video gen.

### Uogólnienie: nie tylko video jest async

Anthropic Batches API (już w `engine/batch_api.py`) też jest submit+poll+download. Przyszłe TTS/ASR offload też będą. Zrobiłem **uogólnioną abstrakcję `IAsyncJob`** dla każdej długiej operacji.

### Decyzje

**Dwa tory: sync vs async.** IGenerator zostaje dla sync (keyframes image-gen, <10s). Dla async (video gen, 30s-kilka min) osobna ścieżka: `submit_clip_async()` → AsyncJobManager → event handler → `on_job_event()` aktualizuje scenę. Każdy tor dla swojej charakterystyki.

**VideoModelRegistry sync_from_openrouter.** Pobiera `/videos/models` i aktualizuje registry. Zachowuje user overrides (priority, enabled, notes) przy refreshu — tylko rzeczywistość (capabilities, pricing) idzie z API. Placeholdery usunąłem.

**Kivy FloatLayout jako root dla dock_renderer.** Natywny Kivy sposób na floating cross-platform. Nie `WindowBase` (rzadko na Androidzie), nie `Popup` (modalne). Drag przez `on_touch_down/move/up`.

**Video playback fallback chain** zgodnie z `IVideoPlayer`: VideoPlayer → Video → External. Auto-detect w `_detect_availability()`.

**ffmpeg compositor fallback chain**: `imageio-ffmpeg` → system ffmpeg → export-only (.sh + manifest). `imageio-ffmpeg` jest **soft dep**, nie wymagany.

**Presety layoutu** w `data_dir/presets/layouts/`. Shipped to seed (sentinel `.layouts_seeded`). User edytuje — zmiany nie giną przy upgrade'ach.

**ID prefixes**: `g_`, `sc_`, `pf_`, `pi_`, `j_`. 12-char hex. Kompatybilne z `c_`/`m_`/`n_`/`l_`/`vg_` z 0.07.x.

**Event names**: dwukropki zgodnie z `engine/events.py`. Nowe: `video:scene:*`, `video:compose:*`, `job:*`. Wszystko przez `bus.emit()`.

---

## Struktura plików

```
chatadhd_v0.9.0/
├── docs/ARCHITECTURE_v0.9.0.md
├── core/                             # (puste, z 0.07.10)
├── engine/
│   ├── scene_graph.py                # SceneGraph, Scene, Portfolio
│   ├── models_video.py               # VideoModelRegistry + sync_from_openrouter
│   ├── video_pipeline.py             # 4 fazy, sync + async paths
│   ├── compositor.py                 # ICompositor fallback chain
│   ├── async_jobs.py                 # NOWE: uogólniona async abstrakcja
│   └── openrouter_video.py           # NOWE: OR Video provider + discovery
└── gui/
    ├── panel_system.py               # IPanel, PanelManager, CrashPanel
    ├── layout_manager.py             # 6 shipped presets seed'owanych do data_dir
    ├── legacy_adapter.py             # dla 0.07.x paneli
    ├── dock_renderer.py              # NOWE: FloatLayout + drag + mode-toggle
    ├── video_player.py               # IVideoPlayer fallback chain
    ├── video_panel.py                # VideoPreviewPanel
    └── scene_graph_panel.py          # SceneGraphEditorPanel (timeline-linear)
```

13 plików kodu + spec + README. Wszystkie parsują. Sanity-tests przechodzą.

---

## Dwa tory generacji

```
                ┌────────────────────────┐
                │  User / SceneGraph     │
                └───────────┬────────────┘
                            │
          ┌─────────────────┴─────────────────┐
          │                                    │
  [FAZA 2: Keyframes]                [FAZA 3: Clips]
  synchroniczne                      asynchroniczne
          │                                    │
  IGenerator.generate_image()        AsyncJobManager.submit()
          │                                    │
  [3-10s]                            OpenRouterVideoProvider
          │                          submit→poll(30s)→fetch
          │                                    │
          │                          bus.emit("job:completed")
          │                                    │
          │                          pipeline.on_job_event()
          │                                    │
          ▼                                    ▼
  scene.state=KEYFRAME_READY         scene.state=CLIP_READY
  bus.emit("video:scene:              bus.emit("video:scene:
           keyframe:ready")                    clip:ready")
          │                                    │
          └─────────────┬──────────────────────┘
                        │
            UI panele słuchają → refresh
```

Uogólnienie: każda przyszła długa operacja (ASR long-form, TTS batch, image upscaling async) → nowy `IAsyncJobProvider`, rejestracja w AsyncJobManager, event handler.

---

## Status kompletności

**Wszystkie 42 moduły importują się czysto razem** (42 moduły = 28 legacy z 0.07.10 + 14 nowych 0.9.0). `main.py` v0.9.0 integruje wszystko. Uruchomienie:

```bash
cd chatadhd_v0.9.0
pip install kivy requests
python main.py
```

Pierwsze uruchomienie: wczytuje preset `minimal` (tylko chat + lista konwersacji). User włącza dodatkowe panele z menu (video preview, scene graph editor, itd.). Layout zapisuje się automatycznie przy zamykaniu, przywraca przy następnym starcie.

Video pipeline aktywuje się jeśli `secrets.json` ma klucz API OR. Bez klucza reszta apki działa normalnie (chat, memory, graph, import).

**Kompletna lista plików** (46 plików .py razem z `__init__.py`):

Legacy z 0.07.10 (27):
- core: crypto, selector, semantic
- engine: paths, config, db, events, chat_engine, memory_engine, models, graph_engine, graph_memory, semantic_llm, semantic_worker, batch_api, importer, github_sync, providers
- gui: base, chat_panel, conv_panel, memory_panel, dialogs, import_panel, voice_panel, github_panel, graph_viz

Nowe 0.9.0 (14):
- engine: scene_graph, models_video, video_pipeline, compositor, async_jobs, openrouter_video, openrouter_generator, embeddings
- gui: panel_system, layout_manager, legacy_adapter, dock_renderer, video_player, video_panel, scene_graph_panel

Plus: main.py (zrefaktorowany), docs/ARCHITECTURE_v0.9.0.md, README.md (ten plik).

---

## Kolejność implementacji

**a1** — dock renderer + panel-system na legacy paneli. Main.py refactor. Default preset `minimal`.

**a2** — video_preview jako pierwszy floating obywatel. File-picker, ręczne ładowanie mp4.

**a3** — async-jobs + OR video provider. `sync_from_openrouter()` cache 24h. Model catalog panel.

**a4** — scene_graph_editor (timeline) + faza 2 (keyframes). Portfolio panel. Test: "Krew jak smoła" 8 scen z nosaczem.

**a5** — faza 3 async. Generation queue panel. Test: 8 jobs → po kilku min wszystkie klipy.

**a6** — compose. ICompositor → mp4 albo .sh. Test: full run 32s wideo.

**b1+** — polirytmiczny zoom (ffmpeg filter), branching graph, graph view, score view, retry inteligentny.

---

## Pydroid3 kompatybilność

Fallback chains wszędzie zabezpieczają:
- `ffpyplayer` missing → VideoPlayer → External (Android intent).
- `ffmpeg` missing → imageio-ffmpeg (opt) → export-only script.
- `sentence-transformers` missing → hash embedding.

Nic nowego w requirements.txt ponad Kivy+requests. Opcjonalne deps jako `extras`.

---

## Otwarte decyzje (możesz się sprzeciwić)

1. **Dwa-torowa sync/async** — alternatywa: wszystko async. Uprości kod, doda ~100ms latency na każdej generacji. Zostawiam dwa tory.
2. **OR jedyny provider w a3** — Replicate ma też video, mogę dodać `ReplicateVideoProvider` analogicznie. Decyzja: po OR działa, dodajemy Replicate w 0.9.1.
3. **Osobny `async_jobs.db`** zamiast tabeli w `chatadhd.db` — żeby nie kolidować z `_SCHEMA_VERSION`. Alternatywa: dodać tabelę z migracją v4→v5.
4. **`supports_character_consistency=True`** zawsze dla OR video modeli — bo unified schema. Ale jakość różna. Można dodać ranking 1-5 per model.

Jeśli nic nie budzi wątpliwości → a1 gotowy do wdrożenia.
