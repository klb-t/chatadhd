"""
ChatADHD v0.06.01 - Complete GUI
- Collapsible messages with expand
- Artifact detection & floating panel
- Quick API panel with presets
- Node editor for graph
- Voice input (simple subprocess)
"""
import os
import threading
import logging
import zipfile
import json
import re
import subprocess
from pathlib import Path
from datetime import datetime

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.scrollview import ScrollView
from kivy.uix.textinput import TextInput
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.uix.spinner import Spinner
from kivy.uix.button import Button
from kivy.uix.slider import Slider
from kivy.uix.filechooser import FileChooserListView
from kivy.core.window import Window
from kivy.metrics import dp, sp
from kivy.clock import Clock
from kivy.graphics import Color, Rectangle, RoundedRectangle
from kivy.core.clipboard import Clipboard

log = logging.getLogger('panels')

# === THEMES ===
THEMES = {
    'dark': {
        'bg': (0.06, 0.06, 0.08, 1), 'card': (0.14, 0.14, 0.18, 1),
        'input_bg': (0.10, 0.10, 0.13, 1), 'text': (0.95, 0.95, 0.95, 1),
        'dim': (0.55, 0.55, 0.60, 1), 'accent': (0.30, 0.55, 0.95, 1),
        'user': (0.15, 0.22, 0.30, 1), 'ai': (0.12, 0.18, 0.24, 1),
        'ok': (0.20, 0.55, 0.30, 1), 'err': (0.70, 0.25, 0.25, 1),
        'warn': (0.80, 0.55, 0.15, 1), 'artifact': (0.20, 0.25, 0.35, 1),
    },
    'amoled': {
        'bg': (0.0, 0.0, 0.0, 1), 'card': (0.08, 0.08, 0.10, 1),
        'input_bg': (0.05, 0.05, 0.07, 1), 'text': (1.0, 1.0, 1.0, 1),
        'dim': (0.50, 0.50, 0.55, 1), 'accent': (0.35, 0.60, 1.0, 1),
        'user': (0.08, 0.12, 0.18, 1), 'ai': (0.05, 0.08, 0.12, 1),
        'ok': (0.15, 0.60, 0.25, 1), 'err': (0.80, 0.20, 0.20, 1),
        'warn': (0.90, 0.60, 0.10, 1), 'artifact': (0.12, 0.15, 0.22, 1),
    },
}
C = THEMES['dark'].copy()

def set_theme(name):
    global C
    if name in THEMES:
        C.clear()
        C.update(THEMES[name])

# === LOG BUFFER ===
class LogBuffer:
    def __init__(self): self.lines = []
    def add(self, msg): 
        self.lines.append(f"{datetime.now():%H:%M:%S} {msg}")
        self.lines = self.lines[-500:]
    def get(self): return "\n".join(self.lines)
LOGBUF = LogBuffer()

# === BASIC WIDGETS ===
class RBtn(Button):
    def __init__(self, bg=None, **kw):
        super().__init__(**kw)
        self.background_color = bg or C['card']
        self.background_normal = ''
        self.color = C['text']

class Card(BoxLayout):
    def __init__(self, bg=None, **kw):
        super().__init__(**kw)
        self.padding = dp(6)
        with self.canvas.before:
            Color(*(bg or C['card']))
            self.rect = RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(6)])
        self.bind(pos=lambda *a: setattr(self.rect, 'pos', self.pos),
                  size=lambda *a: setattr(self.rect, 'size', self.size))

class DarkInput(TextInput):
    def __init__(self, **kw):
        kw.setdefault('background_color', C['input_bg'])
        kw.setdefault('foreground_color', C['text'])
        kw.setdefault('cursor_color', C['accent'])
        kw.setdefault('hint_text_color', C['dim'])
        super().__init__(**kw)

class Panel(BoxLayout):
    def __init__(self, **kw):
        super().__init__(orientation='vertical', **kw)
        self.panel_width = dp(260)
        self.size_hint_x = None
        self.width = 0
        self._visible = False
        with self.canvas.before:
            Color(*C['bg'])
            self.bg = Rectangle(pos=self.pos, size=self.size)
        self.bind(pos=lambda *a: setattr(self.bg, 'pos', self.pos),
                  size=lambda *a: setattr(self.bg, 'size', self.size))
    
    def toggle(self):
        self._visible = not self._visible
        self.width = self.panel_width if self._visible else 0
    
    def open(self):
        self._visible = True
        self.width = self.panel_width
    
    def close(self):
        self._visible = False
        self.width = 0
    
    def refresh(self):
        pass

def show_toast(msg, duration=2):
    toast = Label(text=msg, size_hint=(None, None), size=(dp(220), dp(36)),
                  pos_hint={'center_x': 0.5, 'y': 0.05}, color=C['text'], font_size=sp(11))
    with toast.canvas.before:
        Color(*C['ok'])
        toast.bg = RoundedRectangle(pos=toast.pos, size=toast.size, radius=[dp(8)])
    toast.bind(pos=lambda *a: setattr(toast.bg, 'pos', toast.pos))
    Window.add_widget(toast)
    Clock.schedule_once(lambda dt: Window.remove_widget(toast), duration)

# === ARTIFACT DETECTION ===
def detect_artifacts(text):
    """Detect code blocks and other artifacts in text."""
    artifacts = []
    
    # Code blocks with ```
    code_pattern = r'```(\w*)\n(.*?)```'
    for match in re.finditer(code_pattern, text, re.DOTALL):
        lang = match.group(1) or 'code'
        code = match.group(2)
        artifacts.append({
            'type': 'code',
            'lang': lang,
            'content': code,
            'start': match.start(),
            'end': match.end(),
        })
    
    # Python files
    if '# ===' in text or 'def ' in text and 'class ' in text:
        if len(text) > 500 and text.count('\n') > 20:
            artifacts.append({
                'type': 'code',
                'lang': 'python',
                'content': text,
                'start': 0,
                'end': len(text),
            })
    
    return artifacts

# === NODE EDITOR POPUP (for Graph) ===
class NodeEditorPopup(Popup):
    def __init__(self, node, engine, memory, on_update=None, **kwargs):
        self.node = node
        self.engine = engine
        self.memory = memory
        self.on_update = on_update
        
        content = BoxLayout(orientation='vertical', padding=dp(8), spacing=dp(4))
        
        # Header
        content.add_widget(Label(text=f"[{node.type}] {node.id[:10]}", 
                                font_size=sp(9), color=C['dim'], 
                                size_hint_y=None, height=dp(20)))
        
        # Content
        content.add_widget(Label(text="Content:", color=C['text'], 
                                size_hint_y=None, height=dp(16)))
        self.content_input = DarkInput(
            text=node.data.get('text', node.full_label) if hasattr(node, 'full_label') else node.label,
            multiline=True, size_hint_y=0.4)
        content.add_widget(self.content_input)
        
        # Weight
        wr = BoxLayout(size_hint_y=None, height=dp(34))
        wr.add_widget(Label(text="Weight:", color=C['text'], size_hint_x=0.25))
        self.weight_slider = Slider(min=0.1, max=2.0, value=node.weight, size_hint_x=0.5)
        wr.add_widget(self.weight_slider)
        self.weight_label = Label(text=f"{node.weight:.1f}", color=C['text'], size_hint_x=0.25)
        self.weight_slider.bind(value=lambda s, v: setattr(self.weight_label, 'text', f"{v:.1f}"))
        wr.add_widget(self.weight_label)
        content.add_widget(wr)
        
        # Pin
        pr = BoxLayout(size_hint_y=None, height=dp(30))
        pr.add_widget(Label(text="Pin:", color=C['text'], size_hint_x=0.4))
        self.pin_btn = RBtn(text="PINNED" if node.pinned else "FREE",
                           bg=C['warn'] if node.pinned else C['card'],
                           size_hint_x=0.6, on_press=self._toggle_pin)
        pr.add_widget(self.pin_btn)
        content.add_widget(pr)
        
        # Buttons
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Save", bg=C['accent'], font_size=sp(10), on_press=self._save))
        btns.add_widget(RBtn(text="Close", bg=C['card'], font_size=sp(10), 
                            on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)
        
        super().__init__(title=f"Edit: {node.label[:18]}", content=content, 
                        size_hint=(0.92, 0.55), **kwargs)
    
    def _toggle_pin(self, *a):
        self.node.pinned = not self.node.pinned
        self.pin_btn.text = "PINNED" if self.node.pinned else "FREE"
        self.pin_btn.background_color = C['warn'] if self.node.pinned else C['card']
    
    def _save(self, *a):
        self.node.weight = self.weight_slider.value
        if self.node.type in ('user', 'assistant') and self.engine:
            self.engine.db.update_msg(self.node.id, weight=self.node.weight)
        elif self.memory:
            self.memory.update_node(self.node.id, weight=self.node.weight)
        show_toast("Saved")
        self.dismiss()
        if self.on_update:
            self.on_update()

# === QUICK API PANEL ===
class QuickAPIPanel(Popup):
    """Fast access to API parameters, presets, favorite models."""
    
    PRESETS = {
        'Creative': {'temperature': 0.9, 'max_tokens': 4096},
        'Balanced': {'temperature': 0.7, 'max_tokens': 4096},
        'Precise': {'temperature': 0.3, 'max_tokens': 4096},
        'Code': {'temperature': 0.2, 'max_tokens': 8192},
        'Long': {'temperature': 0.7, 'max_tokens': 16384},
    }
    
    FAVORITES = [
        'anthropic/claude-sonnet-4-20250514',
        'anthropic/claude-4.6-opus',
        'openai/gpt-5.2',
        'openai/gpt-5.3-codex',
        'google/gemini-2.5-pro',
        'deepseek/deepseek-r1',
    ]
    
    def __init__(self, config, models, on_change=None, **kw):
        self.config = config
        self.models = models
        self.on_change = on_change
        
        content = BoxLayout(orientation='vertical', padding=dp(8), spacing=dp(4))
        
        # Favorite models
        content.add_widget(Label(text="Quick Models:", color=C['text'], 
                                size_hint_y=None, height=dp(18), font_size=sp(10)))
        
        fav_grid = BoxLayout(size_hint_y=None, height=dp(70), orientation='vertical', spacing=dp(2))
        row1 = BoxLayout(spacing=dp(2))
        row2 = BoxLayout(spacing=dp(2))
        
        current = config.get('default_model', '')
        for i, mid in enumerate(self.FAVORITES[:6]):
            short = mid.split('/')[-1][:10]
            is_current = mid == current
            btn = RBtn(text=short, bg=C['accent'] if is_current else C['card'], font_size=sp(8))
            btn.model_id = mid
            btn.bind(on_press=self._select_model)
            if i < 3:
                row1.add_widget(btn)
            else:
                row2.add_widget(btn)
        
        fav_grid.add_widget(row1)
        fav_grid.add_widget(row2)
        content.add_widget(fav_grid)
        
        # Presets
        content.add_widget(Label(text="Presets:", color=C['text'], 
                                size_hint_y=None, height=dp(18), font_size=sp(10)))
        
        preset_row = BoxLayout(size_hint_y=None, height=dp(32), spacing=dp(2))
        for name in self.PRESETS.keys():
            btn = RBtn(text=name, bg=C['card'], font_size=sp(8))
            btn.preset_name = name
            btn.bind(on_press=self._apply_preset)
            preset_row.add_widget(btn)
        content.add_widget(preset_row)
        
        # Temperature
        temp_row = BoxLayout(size_hint_y=None, height=dp(34))
        temp_row.add_widget(Label(text="Temp:", color=C['text'], size_hint_x=0.2, font_size=sp(9)))
        self.temp_slider = Slider(min=0.0, max=1.5, value=config.get('temperature', 0.7), size_hint_x=0.6)
        temp_row.add_widget(self.temp_slider)
        self.temp_label = Label(text=f"{config.get('temperature', 0.7):.2f}", 
                               color=C['text'], size_hint_x=0.2, font_size=sp(9))
        self.temp_slider.bind(value=self._on_temp)
        temp_row.add_widget(self.temp_label)
        content.add_widget(temp_row)
        
        # Max tokens
        tok_row = BoxLayout(size_hint_y=None, height=dp(34))
        tok_row.add_widget(Label(text="Tokens:", color=C['text'], size_hint_x=0.2, font_size=sp(9)))
        self.tok_slider = Slider(min=1000, max=32000, value=config.get('max_tokens', 4096), size_hint_x=0.6)
        tok_row.add_widget(self.tok_slider)
        self.tok_label = Label(text=f"{int(config.get('max_tokens', 4096)):,}", 
                              color=C['text'], size_hint_x=0.2, font_size=sp(9))
        self.tok_slider.bind(value=self._on_tok)
        tok_row.add_widget(self.tok_label)
        content.add_widget(tok_row)
        
        # Current stats
        self.stats = Label(text="", color=C['dim'], size_hint_y=None, height=dp(20), font_size=sp(8))
        self._update_stats()
        content.add_widget(self.stats)
        
        # Buttons
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Apply", bg=C['accent'], on_press=self._apply))
        btns.add_widget(RBtn(text="Close", bg=C['card'], on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)
        
        super().__init__(title="Quick API Settings", content=content, size_hint=(0.95, 0.55), **kw)
    
    def _select_model(self, btn):
        self.config.set('default_model', btn.model_id)
        show_toast(f"Model: {btn.model_id.split('/')[-1]}")
        self._notify()
    
    def _apply_preset(self, btn):
        preset = self.PRESETS.get(btn.preset_name, {})
        for k, v in preset.items():
            self.config.set(k, v)
        self.temp_slider.value = preset.get('temperature', 0.7)
        self.tok_slider.value = preset.get('max_tokens', 4096)
        show_toast(f"Preset: {btn.preset_name}")
        self._notify()
    
    def _on_temp(self, slider, val):
        self.temp_label.text = f"{val:.2f}"
        self._update_stats()
    
    def _on_tok(self, slider, val):
        self.tok_label.text = f"{int(val):,}"
        self._update_stats()
    
    def _update_stats(self):
        model = self.config.get('default_model', 'none')
        self.stats.text = f"Model: {model.split('/')[-1][:20]}"
    
    def _apply(self, *a):
        self.config.set('temperature', round(self.temp_slider.value, 2))
        self.config.set('max_tokens', int(self.tok_slider.value))
        self.config.save()
        show_toast("Settings applied")
        self._notify()
        self.dismiss()
    
    def _notify(self):
        if self.on_change:
            self.on_change()


# === SETTINGS POPUP ===
class SettingsPopup(Popup):
    def __init__(self, config, secrets, on_save=None, **kw):
        self.config = config
        self.secrets = secrets
        self.on_save = on_save
        
        content = BoxLayout(orientation='vertical', padding=dp(8), spacing=dp(4))
        
        content.add_widget(Label(text="API Key:", color=C['text'], 
                                size_hint_y=None, height=dp(16)))
        self.key = DarkInput(text=secrets.get("api_key", ""), 
                            hint_text="sk-or-v1-...",
                            password=True, multiline=False, 
                            size_hint_y=None, height=dp(36))
        content.add_widget(self.key)
        
        content.add_widget(Label(text="Base URL:", color=C['text'], 
                                size_hint_y=None, height=dp(16)))
        self.url = DarkInput(text=config.get("base_url", "https://openrouter.ai/api/v1"), 
                            multiline=False, size_hint_y=None, height=dp(36))
        content.add_widget(self.url)
        
        content.add_widget(RBtn(text="Theme: Dark/AMOLED", bg=C['card'], 
                               size_hint_y=None, height=dp(34),
                               on_press=lambda *a: ThemePopup(self.on_save).open()))
        
        content.add_widget(BoxLayout())  # Spacer
        
        btns = BoxLayout(size_hint_y=None, height=dp(38), spacing=dp(4))
        btns.add_widget(RBtn(text="Save", bg=C['accent'], on_press=self._save))
        btns.add_widget(RBtn(text="Cancel", bg=C['card'], on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)
        
        super().__init__(title="Settings", content=content, size_hint=(0.9, 0.5), **kw)
    
    def _save(self, *a):
        if self.key.text.strip():
            self.secrets.set("api_key", self.key.text.strip())
        self.config.set("base_url", self.url.text.strip())
        self.config.save()
        if self.on_save:
            self.on_save()
        self.dismiss()


class ThemePopup(Popup):
    def __init__(self, on_change=None, **kw):
        self.on_change = on_change
        content = BoxLayout(orientation='vertical', padding=dp(8), spacing=dp(6))
        for name in THEMES.keys():
            btn = RBtn(text=name.upper(), size_hint_y=None, height=dp(44))
            btn.theme_name = name
            btn.bind(on_press=self._sel)
            content.add_widget(btn)
        content.add_widget(BoxLayout())
        content.add_widget(RBtn(text="Close", bg=C['card'], size_hint_y=None, height=dp(38),
                               on_press=lambda *a: self.dismiss()))
        super().__init__(title="Theme", content=content, size_hint=(0.7, 0.4), **kw)
    
    def _sel(self, btn):
        set_theme(btn.theme_name)
        show_toast(f"Theme: {btn.theme_name}")
        self.dismiss()
        if self.on_change:
            self.on_change()


# === FILE PICKER ===
class FilePickerPopup(Popup):
    def __init__(self, callback, title="Select", allow_dirs=False, **kw):
        self.callback = callback
        self.allow_dirs = allow_dirs
        
        content = BoxLayout(orientation='vertical', spacing=dp(3), padding=dp(3))
        
        # Quick nav
        qk = BoxLayout(size_hint_y=None, height=dp(28), spacing=dp(2))
        for txt, p in [("Home", "/storage/emulated/0"), ("DL", "/storage/emulated/0/Download")]:
            if os.path.exists(p):
                b = RBtn(text=txt, size_hint_x=None, width=dp(44), bg=C['card'], font_size=sp(9))
                b.path = p
                b.bind(on_release=lambda b: setattr(self.fc, 'path', b.path))
                qk.add_widget(b)
        content.add_widget(qk)
        
        start = "/storage/emulated/0/Download" if os.path.exists("/storage/emulated/0/Download") else str(Path.home())
        self.fc = FileChooserListView(path=start, dirselect=allow_dirs)
        content.add_widget(self.fc)
        
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Cancel", bg=C['card'], 
                            on_press=lambda *a: (self.dismiss(), self.callback(None))))
        if allow_dirs:
            btns.add_widget(RBtn(text="This Dir", bg=C['warn'], 
                                on_press=lambda *a: (self.dismiss(), self.callback(self.fc.path))))
        btns.add_widget(RBtn(text="Select", bg=C['accent'], on_press=self._sel))
        content.add_widget(btns)
        
        super().__init__(title=title, content=content, size_hint=(0.95, 0.85), **kw)
    
    def _sel(self, *a):
        s = self.fc.selection
        self.dismiss()
        self.callback(s[0] if s else None)


# === LOG VIEWER ===
class LogViewer(Popup):
    def __init__(self, **kw):
        content = BoxLayout(orientation='vertical', padding=dp(6), spacing=dp(4))
        
        scroll = ScrollView()
        self.lbl = Label(text=LOGBUF.get(), font_size=sp(8), color=C['text'],
                        text_size=(Window.width-dp(30), None), halign='left', valign='top',
                        size_hint_y=None)
        self.lbl.bind(texture_size=lambda *x: setattr(self.lbl, 'height', self.lbl.texture_size[1]))
        scroll.add_widget(self.lbl)
        content.add_widget(scroll)
        
        btns = BoxLayout(size_hint_y=None, height=dp(34), spacing=dp(4))
        btns.add_widget(RBtn(text="Refresh", bg=C['accent'], 
                            on_press=lambda *a: setattr(self.lbl, 'text', LOGBUF.get())))
        btns.add_widget(RBtn(text="Close", bg=C['card'], on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)
        
        super().__init__(title="Logs", content=content, size_hint=(0.95, 0.85), **kw)


# === MODEL SELECTOR ===
class ModelSelectorPopup(Popup):
    PROVIDERS = {
        'openai': ('OpenAI', (0.2, 0.7, 0.45, 1)),
        'anthropic': ('Anthropic', (0.9, 0.55, 0.3, 1)),
        'google': ('Google', (0.3, 0.55, 0.9, 1)),
        'meta-llama': ('Meta', (0.3, 0.4, 0.8, 1)),
        'mistralai': ('Mistral', (0.8, 0.45, 0.25, 1)),
        'deepseek': ('DeepSeek', (0.55, 0.35, 0.75, 1)),
    }
    
    def __init__(self, models, current_model, on_select=None, **kw):
        self.models = models
        self.current_model = current_model
        self.on_select = on_select
        self.current_provider = None
        
        content = BoxLayout(orientation='vertical', padding=dp(5), spacing=dp(3))
        
        # Search
        sr = BoxLayout(size_hint_y=None, height=dp(34), spacing=dp(2))
        self.search = DarkInput(hint_text="Search...", multiline=False, font_size=sp(10))
        self.search.bind(text=lambda i, t: self._build(t.lower()))
        sr.add_widget(self.search)
        sr.add_widget(RBtn(text="X", size_hint_x=None, width=dp(34), bg=C['card'],
                          on_press=lambda *a: setattr(self.search, 'text', '')))
        content.add_widget(sr)
        
        # Provider tabs
        self.tabs = BoxLayout(size_hint_y=None, height=dp(30), spacing=dp(2))
        ab = RBtn(text="All", bg=C['accent'], font_size=sp(8))
        ab.provider = None
        ab.bind(on_press=self._sel_prov)
        self.tabs.add_widget(ab)
        for pid, (nm, col) in self.PROVIDERS.items():
            b = RBtn(text=nm[:4], bg=C['card'], font_size=sp(7))
            b.provider = pid
            b.bind(on_press=self._sel_prov)
            self.tabs.add_widget(b)
        content.add_widget(self.tabs)
        
        # Model list
        self.scroll = ScrollView()
        self.lst = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(2))
        self.lst.bind(minimum_height=self.lst.setter('height'))
        self.scroll.add_widget(self.lst)
        content.add_widget(self.scroll)
        
        content.add_widget(RBtn(text="Close", size_hint_y=None, height=dp(34), bg=C['card'],
                               on_press=lambda *a: self.dismiss()))
        
        super().__init__(title="Models", content=content, size_hint=(0.95, 0.85), **kw)
        self._build()
    
    def _sel_prov(self, btn):
        self.current_provider = btn.provider
        for c in self.tabs.children:
            if hasattr(c, 'provider'):
                c.background_color = C['accent'] if c.provider == self.current_provider else C['card']
        self._build()
    
    def _build(self, filt=''):
        self.lst.clear_widgets()
        all_m = self.models.all()
        
        if not all_m:
            self.lst.add_widget(Label(text="No models - tap Ref", size_hint_y=None, 
                                     height=dp(34), color=C['dim']))
            return
        
        grp = {}
        for mid, info in all_m.items():
            prov = mid.split('/')[0] if '/' in mid else 'other'
            grp.setdefault(prov, []).append((mid, info))
        
        if self.current_provider:
            grp = {k: v for k, v in grp.items() if k == self.current_provider}
        
        for prov, models in sorted(grp.items()):
            nm, col = self.PROVIDERS.get(prov, (prov.title(), C['dim']))
            
            hdr = BoxLayout(size_hint_y=None, height=dp(22))
            with hdr.canvas.before:
                Color(*col)
                hdr.bg = Rectangle(pos=hdr.pos, size=hdr.size)
            hdr.bind(pos=lambda w, *a: setattr(w.bg, 'pos', w.pos),
                    size=lambda w, *a: setattr(w.bg, 'size', w.size))
            hdr.add_widget(Label(text=f"{nm} ({len(models)})", font_size=sp(9), 
                                color=C['text'], bold=True))
            self.lst.add_widget(hdr)
            
            for mid, info in sorted(models, key=lambda x: x[1].get('name', x[0])):
                name = info.get('name', mid.split('/')[-1])
                if filt and filt not in name.lower() and filt not in mid.lower():
                    continue
                
                cur = mid == self.current_model
                card = Card(size_hint_y=None, height=dp(38), bg=C['accent'] if cur else C['card'])
                card.add_widget(Label(text=name[:28] + ("*" if cur else ""), 
                                     font_size=sp(9), color=C['text']))
                card.model_id = mid
                card.bind(on_touch_down=lambda w, t, m=mid: self._sel(m) if w.collide_point(*t.pos) else None)
                self.lst.add_widget(card)
    
    def _sel(self, mid):
        if self.on_select:
            self.on_select(mid)
        self.dismiss()


# === COLLAPSIBLE MESSAGE BUBBLE ===
class MsgBubble(BoxLayout):
    """Message with collapse/expand functionality."""
    
    PREVIEW_LINES = 4
    
    def __init__(self, msg, models, on_exclude=None, on_include=None, **kw):
        super().__init__(orientation='vertical', size_hint_y=None, padding=dp(5), spacing=dp(2), **kw)
        
        self.msg = msg
        self.models = models
        self.on_exclude = on_exclude
        self.on_include = on_include
        self.expanded = False
        
        excluded = msg.get('status') == 'excluded'
        is_user = msg['role'] == 'user'
        bg = C['user'] if is_user else C['ai']
        
        with self.canvas.before:
            Color(*bg, 0.5 if excluded else 1)
            self.rect = RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(6)])
        self.bind(pos=lambda *a: setattr(self.rect, 'pos', self.pos),
                  size=lambda *a: setattr(self.rect, 'size', self.size))
        
        # Header
        hdr = BoxLayout(size_hint_y=None, height=dp(20))
        role = "You" if is_user else models.name(msg.get('model'))
        hdr.add_widget(Label(text=role[:20], font_size=sp(8), color=C['dim'], halign='left', size_hint_x=0.5))
        
        hdr.add_widget(RBtn(text="Copy", font_size=sp(7), size_hint_x=0.2, bg=C['card'],
                           on_press=lambda *a: (Clipboard.copy(msg['text']), show_toast("Copied"))))
        
        toggle_text = "Show" if excluded else "Hide"
        hdr.add_widget(RBtn(text=toggle_text, font_size=sp(7), size_hint_x=0.3, bg=C['card'],
                           on_press=self._toggle_exclude))
        self.add_widget(hdr)
        
        # Content - check for artifacts
        text = msg['text']
        artifacts = detect_artifacts(text) if not is_user else []
        
        # Preview/full text
        lines = text.split('\n')
        if len(lines) > self.PREVIEW_LINES:
            preview = '\n'.join(lines[:self.PREVIEW_LINES]) + '\n...'
            self.full_text = text
            self.preview_text = preview
            display_text = preview
            self.can_expand = True
        else:
            display_text = text
            self.can_expand = False
        
        self.txt = DarkInput(text=display_text, readonly=True, font_size=sp(10), 
                            size_hint_y=None, multiline=True)
        if excluded:
            self.txt.foreground_color = (*C['text'][:3], 0.5)
        self._calc_height(display_text)
        self.add_widget(self.txt)
        
        # Expand button
        if self.can_expand:
            self.expand_btn = RBtn(text="▼ więcej", size_hint_y=None, height=dp(22),
                                   bg=C['card'], font_size=sp(8), on_press=self._toggle_expand)
            self.add_widget(self.expand_btn)
        
        # Artifacts
        if artifacts:
            for art in artifacts[:2]:  # Max 2 artifacts
                self.add_widget(ArtifactBar(art, msg))
        
        # Attachments
        att = msg.get('attachments', [])
        if att:
            self.add_widget(Label(text=f"📎 {len(att)} file(s)", font_size=sp(8), 
                                 color=C['dim'], size_hint_y=None, height=dp(14)))
        
        self._update_total_height()
    
    def _calc_height(self, text):
        lines = text.count('\n') + 1
        cpl = max(1, int((Window.width - dp(50)) / dp(7)))
        wrapped = max(lines, len(text) // cpl + 1)
        self.txt.height = min(dp(200), max(dp(28), wrapped * dp(14)))
    
    def _update_total_height(self):
        h = dp(20) + self.txt.height + dp(8)  # header + text + padding
        if self.can_expand:
            h += dp(22)
        if hasattr(self, 'art_bars'):
            h += len(self.art_bars) * dp(36)
        if self.msg.get('attachments'):
            h += dp(14)
        self.height = h
    
    def _toggle_expand(self, *a):
        self.expanded = not self.expanded
        if self.expanded:
            self.txt.text = self.full_text
            self.expand_btn.text = "▲ mniej"
        else:
            self.txt.text = self.preview_text
            self.expand_btn.text = "▼ więcej"
        self._calc_height(self.txt.text)
        self._update_total_height()
    
    def _toggle_exclude(self, *a):
        excluded = self.msg.get('status') == 'excluded'
        if excluded:
            if self.on_include:
                self.on_include(self.msg['id'])
        else:
            if self.on_exclude:
                self.on_exclude(self.msg['id'])


# === ARTIFACT BAR ===
class ArtifactBar(BoxLayout):
    """Floating bar for detected artifacts (code, etc)."""
    
    def __init__(self, artifact, msg, **kw):
        super().__init__(size_hint_y=None, height=dp(34), spacing=dp(3), padding=dp(3), **kw)
        
        self.artifact = artifact
        self.msg = msg
        
        with self.canvas.before:
            Color(*C['artifact'])
            self.rect = RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(4)])
        self.bind(pos=lambda *a: setattr(self.rect, 'pos', self.pos),
                  size=lambda *a: setattr(self.rect, 'size', self.size))
        
        # Icon + type
        icon = "📄" if artifact['type'] == 'code' else "📋"
        lang = artifact.get('lang', 'text')[:8]
        self.add_widget(Label(text=f"{icon} {lang}", font_size=sp(9), color=C['text'], size_hint_x=0.3))
        
        # Size info
        size = len(artifact['content'])
        self.add_widget(Label(text=f"{size:,}ch", font_size=sp(8), color=C['dim'], size_hint_x=0.2))
        
        # Actions
        self.add_widget(RBtn(text="Copy", bg=C['card'], font_size=sp(8), size_hint_x=0.25,
                            on_press=lambda *a: (Clipboard.copy(artifact['content']), show_toast("Copied"))))
        self.add_widget(RBtn(text="→Mem", bg=C['accent'], font_size=sp(8), size_hint_x=0.25,
                            on_press=self._to_memory))
    
    def _to_memory(self, *a):
        """Auto-integrate artifact to memory."""
        from kivy.app import App
        app = App.get_running_app()
        if app and app.memory:
            content = self.artifact['content']
            lang = self.artifact.get('lang', 'code')
            
            # Create memory node
            title = f"[{lang}] {datetime.now():%Y-%m-%d %H:%M}"
            app.memory.add_node(f"{title}\n{content[:500]}...", None, 'text')
            show_toast("Added to Memory")
        else:
            show_toast("Memory not available")


# === STREAMING BUBBLE ===
class StreamingBubble(BoxLayout):
    def __init__(self, models, model_id, **kw):
        super().__init__(orientation='vertical', size_hint_y=None, height=dp(50), padding=dp(5), **kw)
        
        with self.canvas.before:
            Color(*C['ai'])
            self.rect = RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(6)])
        self.bind(pos=lambda *a: setattr(self.rect, 'pos', self.pos),
                  size=lambda *a: setattr(self.rect, 'size', self.size))
        
        hdr = BoxLayout(size_hint_y=None, height=dp(16))
        hdr.add_widget(Label(text=models.name(model_id), font_size=sp(8), color=C['dim']))
        self.st = Label(text="...", font_size=sp(8), color=C['warn'], size_hint_x=0.2)
        hdr.add_widget(self.st)
        self.add_widget(hdr)
        
        self.txt = DarkInput(text="", readonly=True, font_size=sp(10), 
                            size_hint_y=None, height=dp(28), multiline=True)
        self.add_widget(self.txt)
        
        self._text = ""
        self._reasoning = ""
        self.reason_lbl = None
    
    def append(self, chunk):
        self._text += chunk
        self.txt.text = self._text
        self._update_height()
    
    def append_reasoning(self, chunk):
        if not self.reason_lbl:
            self.reason_lbl = Label(text="💭 ", font_size=sp(8), color=C['dim'],
                                   size_hint_y=None, height=dp(18))
            self.add_widget(self.reason_lbl, index=0)
        self._reasoning += chunk
        self.reason_lbl.text = "💭 " + self._reasoning[-60:]
    
    def _update_height(self):
        lines = self._text.count('\n') + 1
        cpl = max(1, int((Window.width - dp(50)) / dp(7)))
        wrapped = max(lines, len(self._text) // cpl + 1)
        self.txt.height = min(dp(200), max(dp(28), wrapped * dp(14)))
        self.height = dp(16) + self.txt.height + dp(8) + (dp(18) if self.reason_lbl else 0)
    
    def finish(self):
        self.st.text = "OK"
        self.st.color = C['ok']


# === MEMORY PANEL ===
class MemoryPanel(Panel):
    def __init__(self, memory, **kw):
        super().__init__(**kw)
        self.memory = memory
        self.panel_width = dp(280)
        
        hdr = BoxLayout(size_hint_y=None, height=dp(34))
        hdr.add_widget(Label(text="Memory", font_size=sp(12), color=C['text'], bold=True))
        hdr.add_widget(RBtn(text="X", size_hint_x=None, width=dp(34), bg=C['card'],
                           on_press=lambda *a: self.close()))
        self.add_widget(hdr)
        
        # Add buttons
        add_row = BoxLayout(size_hint_y=None, height=dp(28), spacing=dp(2))
        add_row.add_widget(RBtn(text="+Text", bg=C['accent'], font_size=sp(8),
                               on_press=lambda *a: self._add(None, 'text')))
        add_row.add_widget(RBtn(text="+Folder", bg=C['ok'], font_size=sp(8),
                               on_press=lambda *a: self._add(None, 'folder')))
        add_row.add_widget(RBtn(text="+File", bg=C['warn'], font_size=sp(8),
                               on_press=lambda *a: self._add_file(None)))
        add_row.add_widget(RBtn(text="+Dir", bg=C['card'], font_size=sp(8),
                               on_press=lambda *a: self._add_dir(None)))
        self.add_widget(add_row)
        
        # Tree
        self.scroll = ScrollView()
        self.tree = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(1))
        self.tree.bind(minimum_height=self.tree.setter('height'))
        self.scroll.add_widget(self.tree)
        self.add_widget(self.scroll)
    
    def refresh(self):
        self.tree.clear_widgets()
        if not self.memory:
            self.tree.add_widget(Label(text="No memory", size_hint_y=None, height=dp(28), color=C['dim']))
            return
        self._build(None)
        if not self.tree.children:
            self.tree.add_widget(Label(text="Empty - add items", size_hint_y=None, height=dp(28), color=C['dim']))
    
    def _build(self, parent_id):
        for n in self.memory.get_children(parent_id):
            row = BoxLayout(size_hint_y=None, height=dp(28), spacing=dp(2))
            
            # Toggle active
            act = RBtn(text="+" if n.active else "-", size_hint_x=None, width=dp(24),
                      bg=C['ok'] if n.active else C['dim'], font_size=sp(9))
            act.node_id = n.id
            act.bind(on_press=self._toggle)
            row.add_widget(act)
            
            # Content
            indent = "  " * n.depth
            icons = {'folder': '📁', 'file': '📄', 'dir': '📂', 'text': '📝'}
            icon = icons.get(n.node_type, '•')
            txt = f"{indent}{icon} {n.content[:16]}"
            
            btn = RBtn(text=txt, bg=C['card'], font_size=sp(8), halign='left')
            btn.node = n
            btn.bind(on_press=self._edit)
            row.add_widget(btn)
            
            # Delete
            del_btn = RBtn(text="x", size_hint_x=None, width=dp(22), bg=C['err'], font_size=sp(8))
            del_btn.node_id = n.id
            del_btn.bind(on_press=self._delete)
            row.add_widget(del_btn)
            
            self.tree.add_widget(row)
            self._build(n.id)
    
    def _toggle(self, btn):
        n = self.memory.get_node(btn.node_id)
        if n:
            self.memory.update_node(n.id, active=not n.active)
        self.refresh()
    
    def _delete(self, btn):
        self.memory.delete_node(btn.node_id, recursive=True)
        self.refresh()
    
    def _edit(self, btn):
        n = btn.node
        
        content = BoxLayout(orientation='vertical', padding=dp(6), spacing=dp(3))
        
        if n.node_type in ('file', 'dir'):
            path = n.metadata.get('path', n.content)
            content.add_widget(Label(text=f"Path: {path[-35:]}", color=C['dim'],
                                    size_hint_y=None, height=dp(18), font_size=sp(8)))
        
        txt = DarkInput(text=n.content, multiline=True, size_hint_y=0.5)
        content.add_widget(txt)
        
        # Type spinner
        tr = BoxLayout(size_hint_y=None, height=dp(32))
        tr.add_widget(Label(text="Type:", color=C['text'], size_hint_x=0.25))
        ts = Spinner(text=n.node_type, values=['folder', 'text', 'file', 'dir'],
                    size_hint_x=0.75, background_color=C['card'])
        tr.add_widget(ts)
        content.add_widget(tr)
        
        # Buttons
        btns = BoxLayout(size_hint_y=None, height=dp(32), spacing=dp(3))
        btns.add_widget(RBtn(text="+Child", bg=C['ok'], font_size=sp(8),
                            on_press=lambda *a: self._add(n.id, 'text', popup)))
        btns.add_widget(RBtn(text="Save", bg=C['accent'], font_size=sp(8),
                            on_press=lambda *a: (self.memory.update_node(n.id, content=txt.text.strip(),
                                                node_type=ts.text), popup.dismiss(), self.refresh())))
        if n.node_type in ('folder', 'dir'):
            btns.add_widget(RBtn(text="ZIP", bg=C['warn'], font_size=sp(8),
                                on_press=lambda *a: self._zip(n)))
        content.add_widget(btns)
        
        popup = Popup(title=f"Edit: {n.content[:16]}", content=content, size_hint=(0.92, 0.55))
        popup.open()
    
    def _add(self, parent_id, node_type, parent_popup=None):
        content = BoxLayout(orientation='vertical', padding=dp(6), spacing=dp(3))
        txt = DarkInput(hint_text="Content...", multiline=True, size_hint_y=0.6)
        content.add_widget(txt)
        
        def create(*a):
            if txt.text.strip():
                self.memory.add_node(txt.text.strip(), parent_id, node_type)
                popup.dismiss()
                if parent_popup:
                    parent_popup.dismiss()
                self.refresh()
        
        content.add_widget(RBtn(text="Create", bg=C['accent'], size_hint_y=None, height=dp(34),
                               on_press=create))
        popup = Popup(title=f"Add {node_type}", content=content, size_hint=(0.9, 0.4))
        popup.open()
    
    def _add_file(self, parent_id):
        def on_file(path):
            if path and os.path.isfile(path):
                name = os.path.basename(path)
                nid = self.memory.add_node(name, parent_id, 'file', metadata={'path': path})
                self.refresh()
                show_toast(f"Added: {name}")
        FilePickerPopup(on_file, "Select File").open()
    
    def _add_dir(self, parent_id):
        """Add directory with full file contents to memory."""
        def on_dir(path):
            if path and os.path.isdir(path):
                # Count files first
                file_count = 0
                total_size = 0
                for root, dirs, files in os.walk(path):
                    for f in files:
                        if f.endswith(('.py', '.md', '.txt', '.json', '.js', '.html', '.css')):
                            file_count += 1
                            total_size += os.path.getsize(os.path.join(root, f))
                
                # Warn if large
                if file_count > 50 or total_size > 500000:
                    show_toast(f"⚠️ {file_count} files, {total_size//1000}KB - may be large!")
                
                # Create root folder
                name = os.path.basename(path) or path
                root_id = self.memory.add_node(name, parent_id, 'folder', metadata={'path': path})
                
                # Add all files with content
                added = 0
                for root_dir, dirs, files in os.walk(path):
                    dirs[:] = [d for d in dirs if d not in ('__pycache__', '.git', 'node_modules')]
                    
                    for f in files:
                        if f.endswith(('.py', '.md', '.txt', '.json', '.js', '.html', '.css')):
                            fpath = os.path.join(root_dir, f)
                            try:
                                content = Path(fpath).read_text(errors='replace')[:10000]
                                self.memory.add_node(f"[{f}]\n{content}", root_id, 'file',
                                                    metadata={'path': fpath})
                                added += 1
                            except Exception as e:
                                LOGBUF.add(f"Skip {f}: {e}")
                
                self.refresh()
                show_toast(f"Added {added} files from {name}")
        
        FilePickerPopup(on_dir, "Select Directory", allow_dirs=True).open()
    
    def _zip(self, node):
        try:
            dl = Path("/storage/emulated/0/Download")
            if not dl.exists():
                dl = Path.home() / "Downloads"
            
            zip_name = f"{node.content[:18].replace(' ', '_').replace('/', '_')}.zip"
            zip_path = dl / zip_name
            
            with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
                if node.node_type == 'dir':
                    dir_path = node.metadata.get('path', '')
                    if os.path.isdir(dir_path):
                        for root, dirs, files in os.walk(dir_path):
                            for f in files:
                                fp = os.path.join(root, f)
                                zf.write(fp, os.path.relpath(fp, dir_path))
                else:
                    zf.writestr("README.txt", node.content)
                    
                    def add_children(pid, prefix):
                        for ch in self.memory.get_children(pid):
                            nm = ch.content[:28].replace('/', '_')
                            if ch.node_type == 'file':
                                fp = ch.metadata.get('path', '')
                                if os.path.isfile(fp):
                                    zf.write(fp, f"{prefix}/{os.path.basename(fp)}")
                            else:
                                zf.writestr(f"{prefix}/{nm}.txt", ch.content)
                            add_children(ch.id, f"{prefix}/{nm}")
                    
                    add_children(node.id, node.content[:16])
            
            show_toast(f"ZIP: {zip_name}")
        except Exception as e:
            show_toast(f"ZIP error: {str(e)[:20]}")


# === CONVERSATION PANEL ===
class ConvPanel(Panel):
    def __init__(self, engine, on_select=None, **kw):
        super().__init__(**kw)
        self.engine = engine
        self.on_select = on_select
        
        hdr = BoxLayout(size_hint_y=None, height=dp(34))
        hdr.add_widget(Label(text="Conversations", font_size=sp(11), color=C['text'], bold=True))
        hdr.add_widget(RBtn(text="X", size_hint_x=None, width=dp(34), bg=C['card'],
                           on_press=lambda *a: self.close()))
        self.add_widget(hdr)
        
        # Import button
        import_row = BoxLayout(size_hint_y=None, height=dp(30), spacing=dp(2), padding=dp(2))
        import_row.add_widget(RBtn(text="📥 Import", bg=C['accent'], font_size=sp(9),
                                  on_press=self._open_import))
        import_row.add_widget(RBtn(text="🗑️ Clear", bg=C['err'], font_size=sp(9),
                                  on_press=self._confirm_clear))
        self.add_widget(import_row)
        
        self.scroll = ScrollView()
        self.lst = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(2))
        self.lst.bind(minimum_height=self.lst.setter('height'))
        self.scroll.add_widget(self.lst)
        self.add_widget(self.scroll)
    
    def _open_import(self, *a):
        ImportConversationPopup(self.engine.db, self.engine, on_import=self._on_import_done).open()
    
    def _on_import_done(self):
        self.refresh()
        if self.on_select:
            self.on_select()
    
    def _confirm_clear(self, *a):
        content = BoxLayout(orientation='vertical', padding=dp(10))
        content.add_widget(Label(text="Delete ALL conversations?", color=C['text']))
        btns = BoxLayout(size_hint_y=None, height=dp(40), spacing=dp(4))
        
        def do_clear(*a):
            # Delete all conversations
            for conv in self.engine.db.list_convs():
                self.engine.db.delete_conv(conv['id'])
            self.refresh()
            popup.dismiss()
            show_toast("Cleared all conversations")
        
        btns.add_widget(RBtn(text="Cancel", bg=C['card'], on_press=lambda *a: popup.dismiss()))
        btns.add_widget(RBtn(text="DELETE ALL", bg=C['err'], on_press=do_clear))
        content.add_widget(btns)
        popup = Popup(title="Confirm", content=content, size_hint=(0.8, 0.3))
        popup.open()
    
    def refresh(self):
        self.lst.clear_widgets()
        if not self.engine.db:
            return
        
        for c in self.engine.db.list_convs():
            card = Card(size_hint_y=None, height=dp(40))
            card.add_widget(Label(text=c['title'][:22], font_size=sp(9), color=C['text'], halign='left'))
            card.conv = c
            card.bind(on_touch_down=lambda w, t, cv=c: self._sel(cv) if w.collide_point(*t.pos) else None)
            self.lst.add_widget(card)
    
    def _sel(self, conv):
        self.engine.load_conv(conv['id'])
        self.close()
        if self.on_select:
            self.on_select()


# === MAIN CHAT PANEL ===
class ChatPanel(BoxLayout):
    def __init__(self, engine, models, config, on_settings=None, **kw):
        super().__init__(orientation='vertical', **kw)
        
        self.engine = engine
        self.models = models
        self.config = config
        self.on_settings = on_settings
        
        self.sending = False
        self.pending_files = []
        self.streaming_bubble = None
        self._long_press_ev = None
        
        # Features
        self.web_search = False
        self.deep_research = False
        self.reasoning_effort = None
        
        # Keyboard
        Window.softinput_mode = 'below_target'
        
        self._build()
    
    def _build(self):
        # Top bar
        top = BoxLayout(size_hint_y=None, height=dp(38), padding=dp(2), spacing=dp(2))
        
        self.model_btn = RBtn(text=self.models.name(self.config.get('default_model'))[:10],
                             size_hint_x=0.28, bg=C['card'], font_size=sp(9))
        self.model_btn.bind(on_press=self._open_models)
        top.add_widget(self.model_btn)
        
        for txt, clr, fn in [
            ("⚙️", C['accent'], lambda *a: QuickAPIPanel(self.config, self.models, self._on_api_change).open()),
            ("Ref", C['card'], self._refresh_models),
            ("New", C['ok'], self._new),
            ("Cfg", C['card'], lambda *a: self.on_settings() if self.on_settings else None),
            ("Log", C['warn'], lambda *a: LogViewer().open()),
        ]:
            top.add_widget(RBtn(text=txt, size_hint_x=0.14, bg=clr, font_size=sp(9), on_press=fn))
        self.add_widget(top)
        
        # Feature toggles
        feat = BoxLayout(size_hint_y=None, height=dp(30), padding=dp(2), spacing=dp(2))
        
        self.web_btn = RBtn(text="🌐Web", size_hint_x=0.25, bg=C['card'], font_size=sp(9))
        self.web_btn.bind(on_press=self._toggle_web)
        feat.add_widget(self.web_btn)
        
        self.research_btn = RBtn(text="🔬Deep", size_hint_x=0.25, bg=C['card'], font_size=sp(9))
        self.research_btn.bind(on_press=self._toggle_research)
        feat.add_widget(self.research_btn)
        
        self.reason_btn = RBtn(text="🧠Auto", size_hint_x=0.25, bg=C['card'], font_size=sp(9))
        self.reason_btn.bind(on_press=self._cycle_reasoning)
        feat.add_widget(self.reason_btn)
        
        self.think_lbl = Label(text="", size_hint_x=0.25, font_size=sp(9), color=C['dim'])
        feat.add_widget(self.think_lbl)
        
        self.add_widget(feat)
        
        # Messages
        self.scroll = ScrollView()
        self.msgs = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(4), padding=dp(4))
        self.msgs.bind(minimum_height=self.msgs.setter('height'))
        self.scroll.add_widget(self.msgs)
        self.add_widget(self.scroll)
        
        # Attachment bar
        self.att_bar = BoxLayout(size_hint_y=None, height=0, padding=[dp(4), 0], spacing=dp(2))
        self.att_lbl = Label(text="", font_size=sp(8), color=C['ok'], halign='left')
        self.att_bar.add_widget(self.att_lbl)
        clear_btn = RBtn(text="X", size_hint_x=None, width=dp(26), bg=C['err'], font_size=sp(9))
        clear_btn.bind(on_press=self._clear_files)
        self.att_bar.add_widget(clear_btn)
        self.add_widget(self.att_bar)
        
        # Input area
        inp = Card(size_hint_y=None, height=dp(72), orientation='vertical', spacing=dp(2))
        
        row = BoxLayout(spacing=dp(3))
        self.txt_in = DarkInput(hint_text="Message...", multiline=True, font_size=sp(11))
        row.add_widget(self.txt_in)
        
        self.send_btn = RBtn(text="Send", size_hint_x=None, width=dp(50), bg=C['accent'], font_size=sp(10))
        self.send_btn.bind(on_release=self._send)
        self.send_btn.bind(on_touch_down=self._on_send_td)
        self.send_btn.bind(on_touch_up=self._on_send_tu)
        row.add_widget(self.send_btn)
        inp.add_widget(row)
        
        # Bottom row
        bottom = BoxLayout(size_hint_y=None, height=dp(24), spacing=dp(3))
        bottom.add_widget(RBtn(text="📎 Attach", bg=C['card'], font_size=sp(9),
                              on_press=lambda *a: FilePickerPopup(self._on_file).open()))
        bottom.add_widget(RBtn(text="🎤", size_hint_x=None, width=dp(40), bg=C['card'], font_size=sp(11),
                              on_press=self._voice_input))
        inp.add_widget(bottom)
        
        self.add_widget(inp)
        
        # Status
        self.status = Label(text="Ready", size_hint_y=None, height=dp(16), font_size=sp(8), color=C['dim'])
        self.add_widget(self.status)
    
    def _on_api_change(self):
        self.model_btn.text = self.models.name(self.config.get('default_model'))[:10]
    
    def _toggle_web(self, *a):
        self.web_search = not self.web_search
        self.web_btn.background_color = C['accent'] if self.web_search else C['card']
        if self.web_search and self.deep_research:
            self.deep_research = False
            self.research_btn.background_color = C['card']
        show_toast("Web: " + ("ON" if self.web_search else "OFF"))
    
    def _toggle_research(self, *a):
        self.deep_research = not self.deep_research
        self.research_btn.background_color = C['warn'] if self.deep_research else C['card']
        if self.deep_research:
            self.web_search = True
            self.web_btn.background_color = C['accent']
        show_toast("Deep Research: " + ("ON" if self.deep_research else "OFF"))
    
    def _cycle_reasoning(self, *a):
        cycle = [None, 'low', 'medium', 'high', 'max']
        labels = ['Auto', 'Low', 'Med', 'High', 'MAX']
        idx = cycle.index(self.reasoning_effort) if self.reasoning_effort in cycle else 0
        idx = (idx + 1) % len(cycle)
        self.reasoning_effort = cycle[idx]
        self.reason_btn.text = f"🧠{labels[idx]}"
        self.reason_btn.background_color = C['accent'] if self.reasoning_effort else C['card']
        show_toast(f"Reasoning: {labels[idx]}")
    
    def _voice_input(self, *a):
        """Simple voice input - opens system voice input if available."""
        show_toast("Voice: Use system keyboard mic")
        # On Android, the keyboard mic button works
        # Full speech recognition requires android.speech which is complex
    
    def _open_models(self, *a):
        def on_sel(mid):
            self.config.set('default_model', mid)
            self.model_btn.text = self.models.name(mid)[:10]
        ModelSelectorPopup(self.models, self.config.get('default_model'), on_sel).open()
    
    def _refresh_models(self, *a):
        self.status.text = "Loading models..."
        
        def up():
            ok = self.models.update_from_api()
            Clock.schedule_once(lambda dt: self._models_done(ok))
        
        threading.Thread(target=up).start()
    
    def _models_done(self, ok):
        if ok:
            self.status.text = f"{len(self.models.all())} models"
            self.model_btn.text = self.models.name(self.config.get('default_model'))[:10]
        else:
            self.status.text = "Failed to load models"
    
    def _new(self, *a):
        self.engine.new_conv()
        self.refresh()
        self.status.text = "New chat"
    
    def _on_file(self, path):
        if path:
            self.pending_files.append(path)
            self._upd_att()
    
    def _upd_att(self):
        if self.pending_files:
            names = [os.path.basename(f)[:10] for f in self.pending_files[:3]]
            self.att_lbl.text = f"[{len(self.pending_files)}] " + ", ".join(names)
            self.att_bar.height = dp(22)
        else:
            self.att_bar.height = 0
    
    def _clear_files(self, *a):
        self.pending_files.clear()
        self._upd_att()
    
    def _on_send_td(self, w, touch):
        if w.collide_point(*touch.pos):
            self._long_press_ev = Clock.schedule_once(lambda dt: self._show_api_preview(), 0.6)
            touch.grab(w)
        return False
    
    def _on_send_tu(self, w, touch):
        if touch.grab_current == w:
            touch.ungrab(w)
            if self._long_press_ev:
                self._long_press_ev.cancel()
                self._long_press_ev = None
        return False
    
    def _show_api_preview(self):
        """Show API request preview."""
        text = self.txt_in.text.strip()
        if not text:
            show_toast("Type message first")
            return
        
        # Build preview
        msgs = []
        if self.engine.memory:
            mem = self.engine.memory.get_active_context()
            if mem:
                msgs.append({"role": "system", "content": f"Memory:\n{mem[:500]}..."})
        
        if self.engine.conv and self.engine.db:
            for m in self.engine.db.get_msgs(self.engine.conv['id']):
                if m['status'] == 'active':
                    msgs.append({"role": m['role'], "content": m['text'][:200] + "..."})
        
        msgs.append({"role": "user", "content": text})
        
        req = {
            "model": self.config.get('default_model'),
            "messages": f"{len(msgs)} messages",
            "temperature": self.config.get('temperature', 0.7),
            "max_tokens": self.config.get('max_tokens', 4096),
            "web_search": self.web_search,
            "deep_research": self.deep_research,
            "reasoning": self.reasoning_effort,
        }
        
        # Token estimate
        total_chars = sum(len(m['content']) for m in msgs)
        est_tokens = total_chars // 4
        
        info = json.dumps(req, indent=2)
        info += f"\n\n~{est_tokens:,} tokens estimated"
        
        # Show popup
        content = BoxLayout(orientation='vertical', padding=dp(8))
        content.add_widget(DarkInput(text=info, readonly=True, font_size=sp(9)))
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Copy", bg=C['card'], on_press=lambda *a: (Clipboard.copy(info), show_toast("Copied"))))
        btns.add_widget(RBtn(text="Close", bg=C['card'], on_press=lambda *a: popup.dismiss()))
        content.add_widget(btns)
        popup = Popup(title="API Preview", content=content, size_hint=(0.95, 0.6))
        popup.open()
    
    def _send(self, *a):
        if self.sending:
            return
        
        text = self.txt_in.text.strip()
        if not text:
            return
        
        if not self.engine.secrets.get("api_key"):
            if self.on_settings:
                self.on_settings()
            return
        
        self.sending = True
        self.txt_in.text = ""
        self.status.text = "Sending..."
        self.think_lbl.text = ""
        
        att = self.pending_files.copy()
        self.pending_files.clear()
        self._upd_att()
        
        model = self.config.get('default_model')
        self.streaming_bubble = StreamingBubble(self.models, model)
        self.msgs.add_widget(self.streaming_bubble)
        Clock.schedule_once(lambda dt: setattr(self.scroll, 'scroll_y', 0), 0.1)
        
        def on_chunk(c):
            Clock.schedule_once(lambda dt: self._on_chunk(c))
        
        def on_reasoning(r):
            Clock.schedule_once(lambda dt: self._on_reasoning(r))
        
        def thread():
            try:
                self.engine.send(
                    text,
                    attachments=att,
                    on_chunk=on_chunk,
                    on_reasoning=on_reasoning,
                    web_search=self.web_search,
                    deep_research=self.deep_research,
                    reasoning_effort=self.reasoning_effort
                )
                Clock.schedule_once(lambda dt: self._ok())
            except Exception as e:
                Clock.schedule_once(lambda dt: self._err(str(e)))
        
        threading.Thread(target=thread).start()
    
    def _on_chunk(self, chunk):
        if self.streaming_bubble:
            self.streaming_bubble.append(chunk)
            Clock.schedule_once(lambda dt: setattr(self.scroll, 'scroll_y', 0), 0)
    
    def _on_reasoning(self, chunk):
        self.think_lbl.text = "💭"
        if self.streaming_bubble:
            self.streaming_bubble.append_reasoning(chunk)
    
    def _ok(self):
        self.sending = False
        if self.streaming_bubble:
            self.streaming_bubble.finish()
            self.streaming_bubble = None
        self.refresh()
        self.status.text = "Ready"
        self.think_lbl.text = ""
    
    def _err(self, err):
        self.sending = False
        if self.streaming_bubble:
            self.msgs.remove_widget(self.streaming_bubble)
            self.streaming_bubble = None
        self.status.text = f"Error: {err[:25]}"
        self.think_lbl.text = ""
        LOGBUF.add(f"Error: {err}")
    
    def refresh(self):
        self.msgs.clear_widgets()
        if not self.engine.conv or not self.engine.db:
            return
        
        for m in self.engine.db.get_msgs(self.engine.conv['id'], include_all=True):
            self.msgs.add_widget(MsgBubble(
                m, self.models,
                on_exclude=lambda mid: (self.engine.db.set_msg_status(mid, 'excluded'), self.refresh()),
                on_include=lambda mid: (self.engine.db.set_msg_status(mid, 'active'), self.refresh())
            ))
        
        Clock.schedule_once(lambda dt: setattr(self.scroll, 'scroll_y', 0))


# === IMPORT CONVERSATION POPUP ===
class ImportConversationPopup(Popup):
    """Import conversations from various formats."""
    
    SUPPORTED = {
        '.db': 'SQLite Database',
        '.json': 'JSON (API logs)',
        '.html': 'HTML Page',
        '.htm': 'HTML Page',
        '.mht': 'MHT Archive',
        '.mhtml': 'MHT Archive',
        '.md': 'Markdown',
        '.txt': 'Plain Text',
        '.png': 'Screenshot',
        '.jpg': 'Screenshot',
    }
    
    def __init__(self, db, engine, on_import=None, **kw):
        self.db = db
        self.engine = engine
        self.on_import = on_import
        self.selected_file = None
        
        content = BoxLayout(orientation='vertical', padding=dp(8), spacing=dp(4))
        
        # Info
        content.add_widget(Label(
            text="Import conversation from:",
            color=C['text'], size_hint_y=None, height=dp(20), font_size=sp(11)
        ))
        
        # Format list
        formats_text = "• DB (Claude.ai, ChatGPT)\n• JSON (API logs)\n• HTML/MHT (web saves)\n• Markdown, Text\n• Screenshot (placeholder)"
        content.add_widget(Label(
            text=formats_text, color=C['dim'], size_hint_y=None, 
            height=dp(80), font_size=sp(9), halign='left'
        ))
        
        # Selected file
        self.file_label = Label(
            text="No file selected", color=C['dim'],
            size_hint_y=None, height=dp(24), font_size=sp(9)
        )
        content.add_widget(self.file_label)
        
        # Title override
        title_row = BoxLayout(size_hint_y=None, height=dp(34))
        title_row.add_widget(Label(text="Title:", color=C['text'], size_hint_x=0.2, font_size=sp(10)))
        self.title_input = DarkInput(hint_text="Auto-detect", multiline=False, size_hint_x=0.8)
        title_row.add_widget(self.title_input)
        content.add_widget(title_row)
        
        # Buttons
        btns = BoxLayout(size_hint_y=None, height=dp(38), spacing=dp(4))
        btns.add_widget(RBtn(text="Browse...", bg=C['accent'], on_press=self._browse))
        btns.add_widget(RBtn(text="Import", bg=C['ok'], on_press=self._import))
        btns.add_widget(RBtn(text="Cancel", bg=C['card'], on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)
        
        # Status
        self.status = Label(text="", color=C['dim'], size_hint_y=None, height=dp(20), font_size=sp(9))
        content.add_widget(self.status)
        
        super().__init__(title="Import Conversation", content=content, size_hint=(0.95, 0.55), **kw)
    
    def _browse(self, *a):
        def on_file(path):
            if path:
                self.selected_file = path
                name = os.path.basename(path)
                ext = os.path.splitext(path)[1].lower()
                fmt = self.SUPPORTED.get(ext, 'Unknown')
                self.file_label.text = f"{name} ({fmt})"
                self.file_label.color = C['text']
        
        FilePickerPopup(on_file, "Select file to import").open()
    
    def _import(self, *a):
        if not self.selected_file:
            self.status.text = "Select a file first!"
            self.status.color = C['err']
            return
        
        if not os.path.exists(self.selected_file):
            self.status.text = "File not found!"
            self.status.color = C['err']
            return
        
        self.status.text = "Importing..."
        self.status.color = C['warn']
        
        # Run in thread
        def do_import():
            try:
                from engine.importer import ConversationImporter
                
                importer = ConversationImporter(self.db, self.engine)
                title = self.title_input.text.strip() or None
                
                results = importer.import_file(self.selected_file, title)
                
                if results:
                    if isinstance(results, list):
                        count = len(results)
                        msg = f"Imported {count} conversation(s)"
                    else:
                        msg = "Imported 1 conversation"
                    
                    Clock.schedule_once(lambda dt: self._success(msg))
                else:
                    Clock.schedule_once(lambda dt: self._error("No conversations found"))
                    
            except Exception as e:
                Clock.schedule_once(lambda dt: self._error(str(e)))
        
        threading.Thread(target=do_import).start()
    
    def _success(self, msg):
        self.status.text = msg
        self.status.color = C['ok']
        show_toast(msg)
        
        if self.on_import:
            self.on_import()
        
        # Close after short delay
        Clock.schedule_once(lambda dt: self.dismiss(), 1.5)
    
    def _error(self, msg):
        self.status.text = f"Error: {msg[:30]}"
        self.status.color = C['err']
        LOGBUF.add(f"Import error: {msg}")
