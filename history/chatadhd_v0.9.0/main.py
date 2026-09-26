"""
ChatADHD v0.9.0 — Mobile-First AI Chat Client with Video Pipeline
=================================================================

Rozszerzenie v0.07.10 o:
  - Reforma UI: panel-system z DOCKED/FLOATING/FLOATING_LOCKED/HIDDEN
  - Video pipeline: 4-fazowy silnik z sync keyframes + async clips (OR Video API)
  - Uogólniona infrastruktura async-jobs (IAsyncJob → video dziś, batch/TTS/ASR jutro)

Run: ``python main.py``

Environment variables:
  CHATADHD_DATA       Override data directory path
  KIVY_LOG_LEVEL      Kivy log verbosity
  CHATADHD_LAYOUT     Override starting layout preset (default: auto-save → "minimal")
  CHATADHD_NO_VIDEO   Wyłącz inicjalizację video pipeline (dla szybkich testów chat-only)
"""

__version__ = "0.9.0"

import logging
import os
import sys

# ── Logging (before any other imports) ─────────────────────────────

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(levelname)-5s] %(name)s: %(message)s",
    datefmt="%H:%M:%S",
    handlers=[logging.StreamHandler(sys.stdout)],
)
log = logging.getLogger("chatadhd")
log.info("=" * 50)
log.info("ChatADHD v%s starting", __version__)

# ── Android GL backend ─────────────────────────────────────────────

if hasattr(sys, "getandroidapilevel"):
    os.environ.setdefault("KIVY_GL_BACKEND", "sdl2")

# ── Kivy imports (after env setup) ─────────────────────────────────

from kivy.app import App
from kivy.clock import Clock
from kivy.core.window import Window

# ── Legacy engine imports (z 0.07.10, bez zmian) ───────────────────

from engine.paths import resolve_data_dir
from engine.config import Config, Secrets
from engine.db import Database
from engine.chat_engine import ChatEngine
from engine.memory_engine import MemoryEngine
from engine.models import ModelRegistry
from engine.graph_engine import GraphEngine
from engine.semantic_llm import SemanticLLM
from engine.graph_memory import GraphMemorySelector
from engine.events import bus, GRAPH_CHANGED, IMPORT_DONE
from core.semantic import analyzer

# ── Legacy GUI imports ─────────────────────────────────────────────

from gui.base import LOGBUF, set_theme
from gui.chat_panel import ChatPanel
from gui.conv_panel import ConvPanel
from gui.memory_panel import MemoryPanel
from gui.dialogs import SettingsPopup
from gui.graph_viz import GraphExplorerPanel

# ── v0.9.0 panel system ────────────────────────────────────────────

from gui.panel_system import PanelRegistry, PanelManager
from gui.layout_manager import LayoutManager
from gui.dock_renderer import DockLayoutRenderer
from gui.legacy_adapter import register_legacy_panels
from gui.video_panel import register_video_preview
from gui.scene_graph_panel import register_scene_graph_editor

# ── v0.9.0 video pipeline (opcjonalne) ─────────────────────────────
# Import lazy — żeby CHATADHD_NO_VIDEO=1 mógł wyłączyć całość.


class ChatADHDApp(App):
    """Application entry point."""

    def build(self):
        self.title = f"ChatADHD v{__version__}"

        # ── Data directory (first, before anything logs) ──────────
        # resolve_data_dir sam loguje; dopóki nie mamy file handlera
        # to idzie tylko na konsolę, OK.
        self.data_dir = resolve_data_dir()

        # ── Logging setup (plik per uruchomienie + konsola) ───────
        # Fundamentalna zasada: każde wysokopoziomowe zdarzenie runtime
        # ma być śledzalne. Plik idzie do data_dir/logs/DDMMYYYYHHMMSS.log.
        # Robimy to PRZED live_debug — żeby debug server łapał już
        # sformatowane wpisy i żeby plik zawierał cały ślad startu.
        try:
            from engine.logging_setup import configure_logging
            log_file = configure_logging(self.data_dir)
            if log_file:
                log.info("Log file: %s", log_file)
        except Exception:
            log.exception("Could not configure logging (continuing with defaults)")

        # ── Live debug server (uruchamiany WCZEŚNIE — łapie cały start) ──
        # Otwórz w przeglądarce: http://localhost:8765
        # Wyłącz: CHATADHD_NO_DEBUG=1
        self._debug_server = None
        if os.environ.get("CHATADHD_NO_DEBUG") != "1":
            try:
                from engine.live_debug import start_debug_server
                self._debug_server = start_debug_server(
                    app=self,
                    port=int(os.environ.get("CHATADHD_DEBUG_PORT", "8765")),
                )
                if self._debug_server:
                    log.info("Live debug: http://127.0.0.1:%d/",
                              self._debug_server.port)
            except Exception:
                log.exception("Failed to start live debug server (continuing)")

        # ── Keyboard ───────────────────────────────────────────────
        Window.softinput_mode = "below_target"
        Window.bind(on_keyboard=self._on_keyboard)

        log.info("Data directory: %s", self.data_dir)

        # ── Engines (legacy, bez zmian w stosunku do 0.07.10) ──────
        self.cfg = Config(self.data_dir / "config.json")
        self.secrets = Secrets(self.data_dir / "secrets.json")
        self.db = Database(self.data_dir / "chatadhd.db")
        self.models = ModelRegistry(
            self.data_dir / "models.json", self.cfg, self.secrets)
        self.memory = MemoryEngine(
            self.data_dir / "memory.json", semantic_analyzer=analyzer)
        self.semantic_llm = SemanticLLM(self.cfg, self.secrets)
        self.graph_memory = GraphMemorySelector(self.db, self.cfg)
        self.engine = ChatEngine(
            self.cfg, self.secrets, self.db, self.memory, self.graph_memory)
        self.graph_engine = GraphEngine(self.db, self.semantic_llm)

        # Background semantic worker (legacy).
        from engine.semantic_worker import SemanticWorker, SEMANTIC_PROGRESS
        self.semantic_worker = SemanticWorker(
            self.db, self.semantic_llm, self.graph_engine,
            self.cfg, self.secrets,
        )
        self.semantic_worker.start()

        # Legacy event routing.
        bus.on(GRAPH_CHANGED, lambda _: Clock.schedule_once(
            lambda dt: self._auto_refresh_graph(), 0))
        bus.on(IMPORT_DONE, lambda data: Clock.schedule_once(
            lambda dt: self._on_import_done(data), 0))
        bus.on(SEMANTIC_PROGRESS, lambda data: Clock.schedule_once(
            lambda dt: self._on_semantic_progress(data), 0))

        # ── Theme ──────────────────────────────────────────────────
        set_theme(self.cfg.get("theme", "dark"))

        LOGBUF.add(f"ChatADHD v{__version__} started")
        LOGBUF.add(f"Data: {self.data_dir}")

        # ── Legacy panel instances (unchanged constructors) ────────
        self.chat_panel = ChatPanel(
            self.engine, self.models, self.cfg,
            on_settings=self._show_settings,
        )
        self.conv_panel = ConvPanel(
            self.engine, on_select=self._on_conv_select)
        self.memory_panel = MemoryPanel(self.memory)
        self.graph_panel = GraphExplorerPanel(self.engine, self.memory)

        # ── v0.9.0: Panel system ───────────────────────────────────
        self.panel_registry = PanelRegistry()
        self.panel_manager = PanelManager(self.panel_registry)

        # Rejestracja legacy paneli jako IPanel-compat via adapter.
        register_legacy_panels(self.panel_registry, self)

        # Rejestracja nowych paneli.
        register_video_preview(self.panel_registry)
        register_scene_graph_editor(self.panel_registry)

        # Layout manager — presety, persistence, auto-save.
        self.layout_manager = LayoutManager(self.panel_manager, self.data_dir)

        # ── v0.9.0: Video pipeline (opcjonalne) ────────────────────
        if os.environ.get("CHATADHD_NO_VIDEO") != "1":
            self._init_video_pipeline()
        else:
            log.info("Video pipeline disabled (CHATADHD_NO_VIDEO=1)")

        # ── Dock renderer — tworzy root widget aplikacji ──────────
        self.dock_renderer = DockLayoutRenderer(self.panel_manager)

        # Wczytaj layout: env override → auto-save → "minimal".
        layout_override = os.environ.get("CHATADHD_LAYOUT")
        if layout_override:
            self.layout_manager.apply_preset(layout_override)
        elif not self.layout_manager.load_auto_save():
            self.layout_manager.apply_preset("minimal")

        # Zbuduj hierarchię widget'ów z aktualnego stanu PanelManager.
        root = self.dock_renderer.build()

        # ── Deferred refresh + diagnostic ────────────────────────
        Clock.schedule_once(lambda dt: self._initial_load(), 0.5)
        Clock.schedule_once(lambda dt: self._post_build_diagnostic(root), 1.0)
        Clock.schedule_once(lambda dt: self._post_build_diagnostic(root), 3.0)

        # Auto-save layoutu przy zamykaniu apki.
        # App.on_stop jest wywoływane przez Kivy; patrz na_stop().
        return root

    def _post_build_diagnostic(self, root):
        """Loguje stan root widgetu po tym jak Kivy miało szansę go rozmieścić."""
        try:
            from kivy.core.window import Window
            log.info("DIAG: Window.size=%s", Window.size)
            log.info("DIAG: root type=%s size=%s pos=%s",
                      type(root).__name__, root.size, root.pos)
            log.info("DIAG: root children count=%d", len(root.children))
            for i, child in enumerate(root.children):
                log.info("DIAG:   [%d] %s size=%s pos=%s visible=%s",
                          i, type(child).__name__, child.size, child.pos,
                          getattr(child, "opacity", "?"))
                # Jeszcze głębiej dla dock_frame
                for j, gc in enumerate(getattr(child, "children", [])):
                    label = getattr(gc, "_slot_label", "")
                    log.info("DIAG:     [%d.%d] %s%s size=%s pos=%s "
                              "size_hint=%s children=%d",
                              i, j, type(gc).__name__,
                              f"({label})" if label else "",
                              gc.size, gc.pos,
                              (gc.size_hint_x, gc.size_hint_y),
                              len(getattr(gc, "children", [])))
        except Exception:
            log.exception("post_build_diagnostic failed")

    # ── v0.9.0: Video pipeline initialization ──────────────────────

    def _init_video_pipeline(self):
        """
        Inicjalizacja video pipeline. Tolerancyjna — każdy błąd
        degraduje feature, nie zabija apki.
        """
        try:
            from engine.models_video import VideoModelRegistry
            from engine.compositor import create_compositor
            from engine.embeddings import create_embedding_provider
            from engine.openrouter_generator import OpenRouterGenerator
            from engine.async_jobs import AsyncJobManager, AsyncJobStore
            from engine.openrouter_video import OpenRouterVideoProvider
            from engine.video_pipeline import VideoPipeline
        except ImportError as e:
            log.warning("Video pipeline modules missing: %s", e)
            self.video_pipeline = None
            return

        api_key = self.secrets.get("api_key")
        if not api_key:
            log.warning(
                "No OR API key in secrets.json; video pipeline inactive "
                "until user adds key in Settings.")
            # Zachowujemy strukture ale z generator'em który rzuci gdy ktoś wywoła.
            api_key = ""

        # Video model registry.
        video_models_file = self.data_dir / "video_models.json"
        self.video_registry = VideoModelRegistry(video_models_file)
        self.video_registry.load_or_seed_defaults()

        # Async sync z OR w tle — żeby nie blokować startu.
        if api_key:
            def _sync():
                try:
                    n = self.video_registry.sync_from_openrouter(api_key)
                    log.info("Video registry synced from OR: %d models", n)
                except Exception:
                    log.exception("OR video models sync failed (non-fatal)")
            import threading
            threading.Thread(target=_sync, daemon=True,
                             name="OR-video-sync").start()

        # Generator (sync image-gen dla keyframes).
        self.image_generator = OpenRouterGenerator(
            api_key=api_key,
            app_name="ChatADHD",
        )

        # Embedding provider (z fallback chain).
        self.embedding_provider = create_embedding_provider("auto")

        # Compositor (z fallback chain).
        self.compositor = create_compositor()
        log.info("Compositor selected: %s", self.compositor.backend_name)

        # Async job manager.
        jobs_db = self.data_dir / "async_jobs.db"
        jobs_artifacts = self.data_dir / "video" / "job_artifacts"
        self.job_store = AsyncJobStore(jobs_db)

        providers = {}
        if api_key:
            providers["openrouter:video"] = OpenRouterVideoProvider(
                api_key=api_key, app_name="ChatADHD",
            )

        self.job_manager = AsyncJobManager(
            store=self.job_store,
            providers=providers,
            artifacts_dir=jobs_artifacts,
            poll_interval_s=30.0,
            event_bus=bus,
        )
        self.job_manager.start()

        # Pipeline.
        self.video_pipeline = VideoPipeline(
            data_dir=self.data_dir,
            registry=self.video_registry,
            generator=self.image_generator,
            embedding_provider=self.embedding_provider,
            compositor=self.compositor,
            job_manager=self.job_manager,
        )

        # Event routing: job events → pipeline → scene updates.
        bus.on("job:completed",
               lambda d: self.video_pipeline.on_job_event("job:completed", d))
        bus.on("job:failed",
               lambda d: self.video_pipeline.on_job_event("job:failed", d))
        bus.on("job:cancelled",
               lambda d: self.video_pipeline.on_job_event("job:cancelled", d))

        log.info("Video pipeline ready: %d video models, compositor=%s, "
                 "embedding=%s, async providers=%d",
                 len(self.video_registry.list_all()),
                 self.compositor.backend_name,
                 type(self.embedding_provider).__name__,
                 len(providers))

    # ── Shutdown ──────────────────────────────────────────────────

    def on_stop(self):
        """Kivy App lifecycle hook — wywoływane przed zamknięciem."""
        try:
            self.layout_manager.auto_save()
            log.info("Layout auto-saved")
        except Exception:
            log.exception("Layout auto-save failed")

        if hasattr(self, "job_manager"):
            try:
                self.job_manager.stop(timeout=5.0)
                log.info("AsyncJobManager stopped")
            except Exception:
                log.exception("AsyncJobManager stop failed")

        if getattr(self, "_debug_server", None) is not None:
            try:
                self._debug_server.stop(timeout=2.0)
            except Exception:
                pass

    # ── Init ───────────────────────────────────────────────────────

    def _initial_load(self):
        self.conv_panel.refresh()
        self.memory_panel.refresh()
        if self.engine.conv:
            self.chat_panel.refresh()
        self.chat_panel._refresh_models()

    # ── Legacy event handlers (unchanged from 0.07.10) ────────────

    def _auto_refresh_graph(self):
        if hasattr(self, "graph_panel") and self.graph_panel._visible:
            self.graph_panel.refresh()

    def _on_import_done(self, data):
        if hasattr(self, "conv_panel"):
            self.conv_panel.refresh()
        count = data.get("count", 0)
        log.info("Import complete: %d conversations from %s",
                 count, data.get("source", "?"))
        if hasattr(self, "semantic_worker"):
            st = self.semantic_worker.status
            pending = st.get("pending", 0)
            if pending > 0:
                LOGBUF.add(f"Imported {count} msgs. "
                           f"Semantic analysis queued: {pending} pending")
            self.semantic_worker.wake()

    def _on_semantic_progress(self, data):
        pending = data.get("pending", 0)
        processed = data.get("processed", 0)
        mode = data.get("mode", "?")
        if pending > 0 and processed > 0:
            LOGBUF.add(f"Semantic [{mode}]: +{processed}, {pending} left")
        elif pending == 0:
            LOGBUF.add("Semantic analysis complete")
        self._auto_refresh_graph()

    def _on_conv_select(self):
        self.chat_panel.refresh()
        if self.graph_panel._visible:
            self.graph_panel.refresh()

    def _show_settings(self):
        SettingsPopup(
            self.cfg, self.secrets,
            models=self.models,
            on_save=self._on_settings_save,
        ).open()

    def _on_settings_save(self):
        self.chat_panel._refresh_models()
        # Jeśli user dodał OR key, re-init video pipeline.
        if (os.environ.get("CHATADHD_NO_VIDEO") != "1"
                and self.secrets.get("api_key")
                and not getattr(self, "video_pipeline", None)):
            log.info("API key added; initializing video pipeline now")
            self._init_video_pipeline()

    # ── Keyboard ───────────────────────────────────────────────────

    @staticmethod
    def _on_keyboard(window, key, *args):
        if key == 27:  # Back / ESC
            return True
        return False


if __name__ == "__main__":
    ChatADHDApp().run()
