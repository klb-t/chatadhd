#!/usr/bin/env python3
"""
ChatADHD v0.4.5

Fixed Kivy FileChooser (bez plyer - nie działa w Pydroid)
"""
import os
import sys
from pathlib import Path

# === PATH SETUP ===
CODE_DIR = Path(__file__).parent.resolve()
sys.path.insert(0, str(CODE_DIR))

# === DATA DIRECTORY ===
def get_data_dir():
    """Persistent data directory - nie zależy od wersji kodu"""
    if 'ANDROID_ROOT' in os.environ:
        # Android - Documents folder
        base = Path('/storage/emulated/0/Documents/ChatADHD')
    else:
        # Desktop
        base = Path.home() / '.config' / 'chatadhd'
    
    base.mkdir(parents=True, exist_ok=True)
    return base

DATA_DIR = get_data_dir()

# === KIVY CONFIG ===
os.environ['KIVY_NO_CONSOLELOG'] = '1'

# === LOGGING ===
import logging
__version__ = "0.4.5"

log_file = DATA_DIR / 'logs' / f'kivy_{__version__}.log'
log_file.parent.mkdir(parents=True, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(name)s: %(message)s',
    handlers=[
        logging.StreamHandler(),
        logging.FileHandler(str(log_file), mode='a')
    ]
)
log = logging.getLogger('main')
log.info(f"ChatADHD v{__version__}")
log.info(f"CODE_DIR: {CODE_DIR}")
log.info(f"DATA_DIR: {DATA_DIR}")

# === KIVY IMPORTS ===
from kivy.app import App
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.floatlayout import FloatLayout
from kivy.uix.label import Label
from kivy.core.window import Window
from kivy.graphics import Color, Rectangle
from kivy.metrics import dp, sp
from kivy.clock import Clock

# === APP IMPORTS ===
try:
    from db import DB
    from config import Config, Secrets, Models
    from engine import ChatEngine
    from gui import (
        C, LOGBUF, RBtn, 
        MemoryPanel, ConvPanel, ChatPanel, SettingsPopup
    )
    log.info("All imports OK")
except ImportError as e:
    log.critical(f"Import failed: {e}")
    class ErrApp(App):
        def build(self):
            return Label(text=f"Import Error:\n{e}\n\nCODE_DIR: {CODE_DIR}")
    ErrApp().run()
    sys.exit(1)

# === LOG HANDLER ===
class BufHandler(logging.Handler):
    def emit(self, record):
        LOGBUF.add(self.format(record))

logging.getLogger().addHandler(BufHandler())

# === WINDOW CONFIG ===
Window.softinput_mode = 'below_target'

# === MAIN LAYOUT ===
class MainLayout(FloatLayout):
    def __init__(self, **kw):
        super().__init__(**kw)
        log.info("Building UI...")
        
        # Initialize components
        self.config = Config(DATA_DIR)
        self.secrets = Secrets(DATA_DIR)
        self.models = Models(DATA_DIR, self.config, self.secrets)
        self.db = DB(DATA_DIR)
        self.engine = ChatEngine(self.config, self.secrets, self.db)
        
        # Background
        with self.canvas.before:
            Color(*C['bg'])
            self.bg = Rectangle(pos=self.pos, size=self.size)
        self.bind(
            pos=lambda *a: setattr(self.bg, 'pos', self.pos),
            size=lambda *a: setattr(self.bg, 'size', self.size)
        )
        
        # Main layout
        main = BoxLayout(orientation='horizontal', pos_hint={'x': 0, 'y': 0}, size_hint=(1, 1))
        
        # Left panel - conversations
        self.conv_panel = ConvPanel(self.engine, self._conv_selected)
        main.add_widget(self.conv_panel)
        
        # Center - chat
        center = BoxLayout(orientation='vertical')
        
        # Nav bar
        nav = BoxLayout(size_hint_y=None, height=dp(48), padding=dp(4), spacing=dp(4))
        with nav.canvas.before:
            Color(*C['card'])
            nav.bg = Rectangle(pos=nav.pos, size=nav.size)
        nav.bind(
            pos=lambda *a: setattr(nav.bg, 'pos', nav.pos),
            size=lambda *a: setattr(nav.bg, 'size', nav.size)
        )
        
        chats_btn = RBtn(text="Chats", size_hint_x=0.25, bg=C['card'])
        chats_btn.bind(on_press=lambda *a: (self.conv_panel.toggle(), self.conv_panel.refresh()))
        nav.add_widget(chats_btn)
        
        self.title_label = Label(text="ChatADHD", font_size=sp(15), color=C['text'], bold=True, size_hint_x=0.5)
        nav.add_widget(self.title_label)
        
        mem_btn = RBtn(text="Memory", size_hint_x=0.25, bg=C['card'])
        mem_btn.bind(on_press=lambda *a: (self.mem_panel.toggle(), self.mem_panel.refresh()))
        nav.add_widget(mem_btn)
        
        center.add_widget(nav)
        
        # Chat panel
        self.chat_panel = ChatPanel(self.engine, self.models, self._show_settings)
        center.add_widget(self.chat_panel)
        
        main.add_widget(center)
        
        # Right panel - memory
        self.mem_panel = MemoryPanel(self.engine.memory)
        main.add_widget(self.mem_panel)
        
        self.add_widget(main)
        
        # Load last conversation or create new
        convs = self.db.list_convs(1)
        if convs:
            self.engine.load_conv(convs[0]['id'])
        else:
            self.engine.new_conv()
        self.chat_panel.refresh()
        
        # Show settings if no API key
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
        return MainLayout()


if __name__ == "__main__":
    ChatADHDApp().run()
