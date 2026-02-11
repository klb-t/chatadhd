"""
ChatADHD v0.06.02 - Major UI Overhaul
- Collapsible messages
- Artifact detection & auto-integration
- Quick API panel with presets
- Fixed keyboard handling
- Voice input (simplified)
"""
__version__ = "0.06.02"

import os
import sys

if hasattr(sys, 'getandroidapilevel'):
    os.environ.setdefault('KIVY_GL_BACKEND', 'sdl2')

from kivy.app import App
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.metrics import dp, sp
from kivy.clock import Clock
from kivy.core.window import Window

from engine.config import Config, Secrets
from engine.db import Database
from engine.chat_engine import ChatEngine
from engine.memory_engine import MemoryEngine
from engine.models import ModelRegistry
from gui.panels import (C, ChatPanel, ConvPanel, MemoryPanel, SettingsPopup,
                        LogViewer, LOGBUF, show_toast, RBtn, QuickAPIPanel)
from gui.graph_viz import GraphExplorerPanel


class ChatADHDApp(App):
    def build(self):
        self.title = f"ChatADHD v{__version__}"
        
        # Keyboard handling
        Window.softinput_mode = 'below_target'
        Window.bind(on_keyboard=self._on_keyboard)
        
        # Initialize engines
        data_dir = self._get_data_dir()
        self.config = Config(data_dir / "config.json")
        self.secrets = Secrets(data_dir / "secrets.json")
        self.db = Database(data_dir / "chatadhd.db")
        self.models = ModelRegistry(data_dir / "models.json", self.config, self.secrets)
        self.memory = MemoryEngine(data_dir / "memory.json")
        self.engine = ChatEngine(self.config, self.secrets, self.db, self.memory)
        
        LOGBUF.add(f"ChatADHD v{__version__} started")
        LOGBUF.add(f"Data: {data_dir}")
        
        # Root
        root = BoxLayout(orientation='horizontal')
        
        # Left - Conversations
        self.conv_panel = ConvPanel(self.engine, on_select=self._on_conv_select)
        root.add_widget(self.conv_panel)
        
        # Center
        center = BoxLayout(orientation='vertical')
        
        # Header
        header = BoxLayout(size_hint_y=None, height=dp(38), padding=dp(2), spacing=dp(2))
        header.add_widget(RBtn(text="☰", size_hint_x=None, width=dp(36), bg=C['card'],
                              on_press=lambda *a: self._toggle_conv()))
        
        self.title_label = Label(
            text=self.engine.conv['title'][:20] if self.engine.conv else "ChatADHD",
            color=C['text'], font_size=sp(11), bold=True)
        header.add_widget(self.title_label)
        
        header.add_widget(RBtn(text="Mem", size_hint_x=None, width=dp(44), bg=C['card'], font_size=sp(9),
                              on_press=lambda *a: self._toggle_memory()))
        header.add_widget(RBtn(text="Graf", size_hint_x=None, width=dp(44), bg=C['accent'], font_size=sp(9),
                              on_press=lambda *a: self._toggle_graph()))
        center.add_widget(header)
        
        # Chat
        self.chat_panel = ChatPanel(self.engine, self.models, self.config, 
                                   on_settings=self._show_settings)
        center.add_widget(self.chat_panel)
        
        root.add_widget(center)
        
        # Right - Memory
        self.memory_panel = MemoryPanel(self.memory)
        root.add_widget(self.memory_panel)
        
        # Graph (hidden)
        self.graph_panel = GraphExplorerPanel(self.engine, self.memory)
        root.add_widget(self.graph_panel)
        
        Clock.schedule_once(lambda dt: self._initial_load(), 0.5)
        
        return root
    
    def _get_data_dir(self):
        from pathlib import Path
        if hasattr(sys, 'getandroidapilevel'):
            candidates = [
                Path("/storage/emulated/0/Download/chatadhd_pydroid_v0.4.6"),
                Path("/storage/emulated/0/Download/chatadhd_data"),
                Path("/storage/emulated/0/Download/dev/chatadhd_pydroid_v0.4.6"),
            ]
            for c in candidates:
                if (c / "chatadhd.db").exists() or (c / "secrets.json").exists():
                    return c
            return Path("/storage/emulated/0/Download/chatadhd_data")
        return Path.home() / ".chatadhd"
    
    def _on_keyboard(self, window, key, *args):
        if key == 27:  # Back/ESC
            return True
        return False
    
    def _initial_load(self):
        self.conv_panel.refresh()
        self.memory_panel.refresh()
        if self.engine.conv:
            self.chat_panel.refresh()
        self.chat_panel._refresh_models()
    
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
        self.title_label.text = self.engine.conv['title'][:20] if self.engine.conv else "New"
        if self.graph_panel._visible:
            self.graph_panel.refresh()
    
    def _show_settings(self):
        SettingsPopup(self.config, self.secrets, on_save=self._on_settings_save).open()
    
    def _on_settings_save(self):
        self.chat_panel._refresh_models()


if __name__ == '__main__':
    ChatADHDApp().run()
