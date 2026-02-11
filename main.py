"""
ChatADHD v0.5.0 - Non-Linear Context Editor
Main application entry point

Features:
- Graph Explorer with force-directed layout
- Message versioning (edit creates new version)
- Weighted nodes for context priority
- Memory with file/directory attachments
- Streaming responses
- Dark/AMOLED themes
"""
__version__ = "0.5.0"

import os
import sys

# Pydroid3 compatibility
if hasattr(sys, 'getandroidapilevel'):
    os.environ.setdefault('KIVY_GL_BACKEND', 'sdl2')

from kivy.app import App
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.metrics import dp
from kivy.clock import Clock

from engine.config import Config, Secrets
from engine.db import Database
from engine.chat_engine import ChatEngine
from engine.memory_engine import MemoryEngine
from engine.models import ModelRegistry
from gui.panels import (C, ChatPanel, ConvPanel, MemoryPanel, SettingsPopup, 
                        LogViewer, LOGBUF, show_toast, RBtn)
from gui.graph_viz import GraphExplorerPanel


class ChatADHDApp(App):
    def build(self):
        self.title = f"ChatADHD v{__version__}"
        
        # Initialize engines
        data_dir = self._get_data_dir()
        self.config = Config(data_dir / "config.json")
        self.secrets = Secrets(data_dir / "secrets.json")
        self.db = Database(data_dir / "chatadhd.db")
        self.models = ModelRegistry(data_dir / "models.json", self.config, self.secrets)
        self.memory = MemoryEngine(data_dir / "memory.json")
        self.engine = ChatEngine(self.config, self.secrets, self.db, self.memory)
        
        LOGBUF.add(f"ChatADHD v{__version__} started")
        LOGBUF.add(f"Data dir: {data_dir}")
        
        # Root layout
        root = BoxLayout(orientation='horizontal')
        
        # Left panel - Conversations
        self.conv_panel = ConvPanel(self.engine, on_select=self._on_conv_select)
        root.add_widget(self.conv_panel)
        
        # Center
        center = BoxLayout(orientation='vertical')
        
        # Header
        header = BoxLayout(size_hint_y=None, height=dp(36), padding=dp(2), spacing=dp(2))
        header.add_widget(RBtn(text="Chats", size_hint_x=0.18, bg=C['card'], font_size=sp(9),
                              on_press=lambda *a: self._toggle_conv()))
        
        self.title_label = Label(text=self.engine.conv['title'][:22] if self.engine.conv else "ChatADHD",
                                color=C['text'], font_size=sp(11), bold=True)
        header.add_widget(self.title_label)
        
        header.add_widget(RBtn(text="Mem", size_hint_x=0.15, bg=C['card'], font_size=sp(9),
                              on_press=lambda *a: self._toggle_memory()))
        header.add_widget(RBtn(text="Graph", size_hint_x=0.18, bg=C['accent'], font_size=sp(9),
                              on_press=lambda *a: self._toggle_graph()))
        center.add_widget(header)
        
        # Chat panel
        self.chat_panel = ChatPanel(self.engine, self.models, on_settings=self._show_settings)
        center.add_widget(self.chat_panel)
        
        root.add_widget(center)
        
        # Right panel - Memory
        self.memory_panel = MemoryPanel(self.memory)
        root.add_widget(self.memory_panel)
        
        # Graph panel (starts hidden)
        self.graph_panel = GraphExplorerPanel(self.engine, self.memory)
        root.add_widget(self.graph_panel)
        
        # Initial refresh
        Clock.schedule_once(lambda dt: self._initial_load(), 0.5)
        
        return root
    
    def _get_data_dir(self):
        from pathlib import Path
        if hasattr(sys, 'getandroidapilevel'):
            base = Path("/storage/emulated/0/Download/chatadhd_data")
        else:
            base = Path.home() / ".chatadhd"
        base.mkdir(parents=True, exist_ok=True)
        return base
    
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
        self.title_label.text = self.engine.conv['title'][:22] if self.engine.conv else "New"
        if self.graph_panel._visible:
            self.graph_panel.refresh()
    
    def _show_settings(self):
        SettingsPopup(self.config, self.secrets, on_save=self._on_settings_save).open()
    
    def _on_settings_save(self):
        self.chat_panel._refresh_models()


# Import sp for font sizes
from kivy.metrics import sp

if __name__ == '__main__':
    ChatADHDApp().run()
