#!/usr/bin/env python3
"""ChatADHD v0.4.2-dev - Multi-model AI chat with graph memory"""
import os, sys
from pathlib import Path

# CRITICAL: Add script directory to path FIRST (for Pydroid)
APP_DIR = Path(__file__).parent.resolve()
sys.path.insert(0, str(APP_DIR))

os.environ['KIVY_NO_CONSOLELOG'] = '1'

import logging

__version__ = "0.4.2-dev"

logging.basicConfig(level=logging.DEBUG, format='%(asctime)s [%(levelname)s] %(name)s: %(message)s',
    handlers=[logging.FileHandler(APP_DIR / "chatadhd.log", encoding='utf-8'), logging.StreamHandler(sys.stdout)])
log = logging.getLogger('main')
log.info("=" * 40)
log.info(f"ChatADHD v{__version__} starting...")
log.info(f"APP_DIR: {APP_DIR}")
log.info(f"sys.path[0]: {sys.path[0]}")

from kivy.app import App
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.floatlayout import FloatLayout
from kivy.uix.label import Label
from kivy.core.window import Window
from kivy.graphics import Color, Rectangle
from kivy.metrics import dp, sp
from kivy.clock import Clock

try:
    from db import DB
    from config import Config, Secrets, Models
    from engine import ChatEngine
    from gui.colors import C
    from gui.widgets import RBtn
    from gui.panels import MemoryPanel, ConvPanel, ChatPanel, SettingsPopup, LOGBUF
    log.info("All imports OK")
except ImportError as e:
    log.critical(f"Import failed: {e}")
    log.critical(f"Files in APP_DIR: {list(APP_DIR.iterdir())}")
    class ErrApp(App):
        def build(self): 
            return Label(text=f"Import Error:\n{e}\n\nAPP_DIR: {APP_DIR}\n\nFiles: {[f.name for f in APP_DIR.iterdir()]}")
    ErrApp().run()
    sys.exit(1)

class BufHandler(logging.Handler):
    def emit(self, record): LOGBUF.add(self.format(record))
log.addHandler(BufHandler())

Window.softinput_mode = 'below_target'

class MainLayout(FloatLayout):
    def __init__(self, **kw):
        super().__init__(**kw)
        log.info("Building UI...")
        
        self.config = Config(APP_DIR)
        self.secrets = Secrets(APP_DIR)
        self.models = Models(APP_DIR, self.config, self.secrets)
        self.db = DB()
        self.engine = ChatEngine(self.config, self.secrets, self.db)
        
        with self.canvas.before:
            Color(*C['bg'])
            self.bg = Rectangle(pos=self.pos, size=self.size)
        self.bind(pos=lambda *a: setattr(self.bg, 'pos', self.pos), size=lambda *a: setattr(self.bg, 'size', self.size))
        
        main = BoxLayout(orientation='horizontal', pos_hint={'x': 0, 'y': 0}, size_hint=(1, 1))
        
        # Left - conversations
        self.conv_panel = ConvPanel(self.engine, self._conv_selected)
        main.add_widget(self.conv_panel)
        
        # Center
        center = BoxLayout(orientation='vertical')
        
        # Nav
        nav = BoxLayout(size_hint_y=None, height=dp(48), padding=dp(4), spacing=dp(4))
        with nav.canvas.before:
            Color(*C['card'])
            nav.bg = Rectangle(pos=nav.pos, size=nav.size)
        nav.bind(pos=lambda *a: setattr(nav.bg, 'pos', nav.pos), size=lambda *a: setattr(nav.bg, 'size', nav.size))
        
        chats_btn = RBtn(text="Chats", size_hint_x=0.25, bg=C['card'])
        chats_btn.bind(on_press=lambda *a: (self.conv_panel.toggle(), self.conv_panel.refresh()))
        nav.add_widget(chats_btn)
        
        self.title_label = Label(text="ChatADHD", font_size=sp(15), color=C['text'], bold=True, size_hint_x=0.5)
        nav.add_widget(self.title_label)
        
        mem_btn = RBtn(text="Memory", size_hint_x=0.25, bg=C['card'])
        mem_btn.bind(on_press=lambda *a: (self.mem_panel.toggle(), self.mem_panel.refresh()))
        nav.add_widget(mem_btn)
        
        center.add_widget(nav)
        
        # Chat
        self.chat_panel = ChatPanel(self.engine, self.models, self._show_settings)
        center.add_widget(self.chat_panel)
        main.add_widget(center)
        
        # Right - memory
        self.mem_panel = MemoryPanel(self.engine.memory)
        main.add_widget(self.mem_panel)
        
        self.add_widget(main)
        
        # Load conversation
        convs = self.db.list_convs(1)
        if convs:
            self.engine.load_conv(convs[0]['id'])
        else:
            self.engine.new_conv()
        self.chat_panel.refresh()
        
        if not self.secrets.get("api_key"):
            Clock.schedule_once(lambda dt: self._show_settings(), 0.5)
        
        log.info("UI ready")
    
    def _conv_selected(self):
        self.chat_panel.refresh()
        if self.engine.conv:
            self.title_label.text = self.engine.conv.get('title', 'Chat')[:18]
    
    def _show_settings(self, *a):
        SettingsPopup(self.config, self.secrets, self.chat_panel._refresh_models).open()

class ChatADHDApp(App):
    def build(self):
        log.info("Building app...")
        return MainLayout()
    def on_stop(self):
        log.info("Stopping")

if __name__ == "__main__":
    try:
        ChatADHDApp().run()
    except Exception as e:
        log.critical(f"Fatal: {e}")
        import traceback
        log.critical(traceback.format_exc())
        sys.exit(1)
