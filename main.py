"""
ChatADHD v0.07.09 — Mobile-First AI Chat Client
================================================

Hierarchical memory · Branching conversations · Multi-model API ·
Streaming · Real-time knowledge graph · Zero-knowledge encryption ·
Voice input · GitHub sync · Universal import (ZIP, JSON, HTML, DB…)

Run: ``python main.py``

Environment variables:
  CHATADHD_DATA   Override data directory path
  KIVY_LOG_LEVEL  Kivy log verbosity (debug, info, warning, error)
"""
__version__ = "0.07.09"

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
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.metrics import dp, sp
from kivy.clock import Clock
from kivy.core.window import Window

# ── Engine imports ─────────────────────────────────────────────────

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

# ── GUI imports ────────────────────────────────────────────────────

from gui.base import C, LOGBUF, RBtn, set_theme
from gui.chat_panel import ChatPanel
from gui.conv_panel import ConvPanel
from gui.memory_panel import MemoryPanel
from gui.dialogs import SettingsPopup
from gui.graph_viz import GraphExplorerPanel


class ChatADHDApp(App):
    """Application entry point."""

    def build(self):
        self.title = f"ChatADHD v{__version__}"

        # ── Keyboard ───────────────────────────────────────────────
        Window.softinput_mode = "below_target"
        Window.bind(on_keyboard=self._on_keyboard)

        # ── Data directory ─────────────────────────────────────────
        data_dir = resolve_data_dir()
        log.info("Data directory: %s", data_dir)

        # ── Engines ────────────────────────────────────────────────
        self.cfg = Config(data_dir / "config.json")
        self.secrets = Secrets(data_dir / "secrets.json")
        self.db = Database(data_dir / "chatadhd.db")
        self.models = ModelRegistry(data_dir / "models.json", self.cfg, self.secrets)
        self.memory = MemoryEngine(data_dir / "memory.json",
                                   semantic_analyzer=analyzer)
        self.semantic_llm = SemanticLLM(self.cfg, self.secrets)
        self.graph_memory = GraphMemorySelector(self.db, self.cfg)
        self.engine = ChatEngine(self.cfg, self.secrets, self.db,
                                 self.memory, self.graph_memory)
        self.graph_engine = GraphEngine(self.db, self.semantic_llm)

        # Background semantic worker.
        from engine.semantic_worker import SemanticWorker
        self.semantic_worker = SemanticWorker(
            self.db, self.semantic_llm, self.graph_engine,
            self.cfg, self.secrets,
        )
        self.semantic_worker.start()

        # Auto-refresh graph panel when graph data changes.
        bus.on(GRAPH_CHANGED, lambda _: Clock.schedule_once(
            lambda dt: self._auto_refresh_graph(), 0))
        bus.on(IMPORT_DONE, lambda data: Clock.schedule_once(
            lambda dt: self._on_import_done(data), 0))

        # Semantic worker progress.
        from engine.semantic_worker import SEMANTIC_PROGRESS
        bus.on(SEMANTIC_PROGRESS, lambda data: Clock.schedule_once(
            lambda dt: self._on_semantic_progress(data), 0))

        # ── Theme ──────────────────────────────────────────────────
        set_theme(self.cfg.get("theme", "dark"))

        LOGBUF.add(f"ChatADHD v{__version__} started")
        LOGBUF.add(f"Data: {data_dir}")

        # ── Layout ─────────────────────────────────────────────────
        root = BoxLayout(orientation="horizontal")

        # Left panel: conversations
        self.conv_panel = ConvPanel(self.engine, on_select=self._on_conv_select)
        root.add_widget(self.conv_panel)

        # Centre: chat
        centre = BoxLayout(orientation="vertical")

        header = BoxLayout(size_hint_y=None, height=dp(38),
                           padding=dp(2), spacing=dp(2))
        header.add_widget(RBtn(
            text="[=]", size_hint_x=None, width=dp(36), bg=C["card"],
            on_press=lambda *_: self._toggle_conv(),
        ))
        self.title_label = Label(
            text=(self.engine.conv["title"][:40]
                  if self.engine.conv else "ChatADHD"),
            color=C["text"], font_size=sp(10), bold=True,
            shorten=True, shorten_from="right",
        )
        header.add_widget(self.title_label)
        header.add_widget(RBtn(
            text="Notes", size_hint_x=None, width=dp(50), bg=C["card"],
            font_size=sp(9), on_press=lambda *_: self._toggle_memory(),
        ))
        header.add_widget(RBtn(
            text="Graf", size_hint_x=None, width=dp(44), bg=C["accent"],
            font_size=sp(9), on_press=lambda *_: self._toggle_graph(),
        ))
        centre.add_widget(header)

        self.chat_panel = ChatPanel(
            self.engine, self.models, self.cfg,
            on_settings=self._show_settings,
        )
        centre.add_widget(self.chat_panel)
        root.add_widget(centre)

        # Right panel: memory
        self.memory_panel = MemoryPanel(self.memory)
        root.add_widget(self.memory_panel)

        # Graph (hidden by default)
        self.graph_panel = GraphExplorerPanel(self.engine, self.memory)
        root.add_widget(self.graph_panel)

        Clock.schedule_once(lambda dt: self._initial_load(), 0.5)
        return root

    # ── Init ───────────────────────────────────────────────────────

    def _initial_load(self):
        self.conv_panel.refresh()
        self.memory_panel.refresh()
        if self.engine.conv:
            self.chat_panel.refresh()
        self.chat_panel._refresh_models()

    # ── Navigation ─────────────────────────────────────────────────

    def _toggle_conv(self):
        self.conv_panel.toggle()
        self.conv_panel.refresh()

    def _toggle_memory(self):
        self.memory_panel.toggle()
        self.memory_panel.refresh()

    def _toggle_graph(self):
        self.graph_panel.toggle()
        if self.graph_panel._visible:
            self.graph_panel.refresh()

    def _on_conv_select(self):
        self.chat_panel.refresh()
        self.title_label.text = (
            self.engine.conv["title"][:40] if self.engine.conv else "New"
        )
        if self.graph_panel._visible:
            self.graph_panel.refresh()

    def _auto_refresh_graph(self):
        """Called from event bus when graph data changes."""
        if hasattr(self, 'graph_panel') and self.graph_panel._visible:
            self.graph_panel.refresh()

    def _on_import_done(self, data):
        """Called after bulk import finishes."""
        if hasattr(self, 'conv_panel'):
            self.conv_panel.refresh()
        count = data.get("count", 0)
        log.info("Import complete: %d conversations from %s",
                 count, data.get("source", "?"))
        # Show semantic queue status.
        if hasattr(self, 'semantic_worker'):
            st = self.semantic_worker.status
            pending = st.get("pending", 0)
            if pending > 0:
                LOGBUF.add(f"Imported {count} msgs. "
                           f"Semantic analysis queued: {pending} pending")
            self.semantic_worker.wake()

    def _on_semantic_progress(self, data):
        """Background semantic worker reports progress."""
        pending = data.get("pending", 0)
        processed = data.get("processed", 0)
        mode = data.get("mode", "?")
        if pending > 0 and processed > 0:
            LOGBUF.add(f"Semantic [{mode}]: +{processed}, {pending} left")
        elif pending == 0:
            LOGBUF.add("Semantic analysis complete")
        # Auto-refresh graph after enrichment.
        self._auto_refresh_graph()

    def _show_settings(self):
        SettingsPopup(
            self.cfg, self.secrets,
            models=self.models,
            on_save=self._on_settings_save,
        ).open()

    def _on_settings_save(self):
        self.chat_panel._refresh_models()

    # ── Keyboard ───────────────────────────────────────────────────

    @staticmethod
    def _on_keyboard(window, key, *args):
        if key == 27:  # Back / ESC
            return True
        return False


if __name__ == "__main__":
    ChatADHDApp().run()
