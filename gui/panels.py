"""
ChatADHD v0.4.8 - Fixed colors + Directory attachments in memory
"""
import os
import threading
import logging
import zipfile
import shutil
import json
from pathlib import Path
from datetime import datetime

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.scrollview import ScrollView
from kivy.uix.textinput import TextInput
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.uix.spinner import Spinner
from kivy.uix.button import Button
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
        'bg': (0.08, 0.08, 0.10, 1),
        'card': (0.16, 0.16, 0.20, 1),
        'input_bg': (0.12, 0.12, 0.15, 1),  # Ciemne tło dla inputów
        'text': (0.95, 0.95, 0.95, 1),
        'dim': (0.55, 0.55, 0.60, 1),
        'accent': (0.30, 0.55, 0.95, 1),
        'user': (0.18, 0.22, 0.32, 1),
        'ai': (0.14, 0.18, 0.14, 1),
        'ok': (0.20, 0.55, 0.30, 1),
        'err': (0.70, 0.25, 0.25, 1),
        'warn': (0.80, 0.55, 0.20, 1),
    },
    'amoled': {
        'bg': (0.0, 0.0, 0.0, 1),
        'card': (0.10, 0.10, 0.12, 1),
        'input_bg': (0.06, 0.06, 0.08, 1),
        'text': (1.0, 1.0, 1.0, 1),
        'dim': (0.50, 0.50, 0.55, 1),
        'accent': (0.35, 0.60, 1.0, 1),
        'user': (0.12, 0.16, 0.25, 1),
        'ai': (0.08, 0.12, 0.08, 1),
        'ok': (0.15, 0.60, 0.25, 1),
        'err': (0.80, 0.20, 0.20, 1),
        'warn': (0.90, 0.60, 0.15, 1),
    },
}
C = THEMES['dark'].copy()

def set_theme(name):
    global C
    if name in THEMES:
        C.clear()
        C.update(THEMES[name])

# === LOG ===
class LogBuffer:
    def __init__(self): self.lines = []
    def add(self, msg):
        self.lines.append(f"{datetime.now():%H:%M:%S} {msg}")
        self.lines = self.lines[-300:]
    def get(self): return "\n".join(self.lines)
LOGBUF = LogBuffer()

# === WIDGETS ===
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
    """TextInput with forced dark background"""
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
    def toggle(self): self._visible = not self._visible; self.width = self.panel_width if self._visible else 0
    def open(self): self._visible = True; self.width = self.panel_width
    def close(self): self._visible = False; self.width = 0
    def refresh(self): pass


def show_toast(msg, duration=2):
    toast = Label(text=msg, size_hint=(None, None), size=(dp(200), dp(36)),
                  pos_hint={'center_x': 0.5, 'y': 0.05}, color=C['text'])
    with toast.canvas.before:
        Color(*C['ok'])
        toast.bg = RoundedRectangle(pos=toast.pos, size=toast.size, radius=[dp(6)])
    toast.bind(pos=lambda *a: setattr(toast.bg, 'pos', toast.pos))
    Window.add_widget(toast)
    Clock.schedule_once(lambda dt: Window.remove_widget(toast), duration)


# === THEME POPUP ===
class ThemePopup(Popup):
    def __init__(self, on_change=None, **kw):
        self.on_change = on_change
        content = BoxLayout(orientation='vertical', padding=dp(8), spacing=dp(6))
        for name in THEMES.keys():
            btn = RBtn(text=name.upper(), size_hint_y=None, height=dp(48))
            btn.theme_name = name
            btn.bind(on_press=self._sel)
            content.add_widget(btn)
        content.add_widget(BoxLayout())
        content.add_widget(RBtn(text="Close", bg=C['card'], size_hint_y=None, height=dp(40),
                               on_press=lambda *a: self.dismiss()))
        super().__init__(title="Theme", content=content, size_hint=(0.7, 0.45), **kw)
    
    def _sel(self, btn):
        set_theme(btn.theme_name)
        if self.on_change: self.on_change()
        self.dismiss()
        show_toast(f"Theme: {btn.theme_name}")


# === FILE/DIR PICKER ===
class FilePickerPopup(Popup):
    def __init__(self, callback, title="Select", allow_dirs=False, **kw):
        self.callback = callback
        self.allow_dirs = allow_dirs
        content = BoxLayout(orientation='vertical', spacing=dp(4), padding=dp(4))
        
        qk = BoxLayout(size_hint_y=None, height=dp(32), spacing=dp(2))
        for txt, path in [("Home", "/storage/emulated/0"), ("DL", "/storage/emulated/0/Download")]:
            if os.path.exists(path):
                b = RBtn(text=txt, size_hint_x=None, width=dp(48), bg=C['card'], font_size=sp(9))
                b.path = path
                b.bind(on_release=lambda b: setattr(self.fc, 'path', b.path))
                qk.add_widget(b)
        content.add_widget(qk)
        
        self.plbl = Label(text="", size_hint_y=None, height=dp(18), font_size=sp(8), color=C['dim'])
        content.add_widget(self.plbl)
        
        start = "/storage/emulated/0/Download" if os.path.exists("/storage/emulated/0/Download") else str(Path.home())
        self.fc = FileChooserListView(path=start, dirselect=allow_dirs)
        self.fc.bind(path=lambda i, p: setattr(self.plbl, 'text', p[-40:]))
        content.add_widget(self.fc)
        
        self.slbl = Label(text="", size_hint_y=None, height=dp(20), font_size=sp(9), color=C['ok'])
        self.fc.bind(selection=lambda i, s: setattr(self.slbl, 'text', f"> {os.path.basename(s[0]) if s else ''}"))
        content.add_widget(self.slbl)
        
        btns = BoxLayout(size_hint_y=None, height=dp(38), spacing=dp(4))
        btns.add_widget(RBtn(text="Cancel", bg=C['card'], on_press=lambda *a: (self.dismiss(), self.callback(None))))
        if allow_dirs:
            btns.add_widget(RBtn(text="This Dir", bg=C['warn'], on_press=self._sel_dir))
        btns.add_widget(RBtn(text="Select", bg=C['accent'], on_press=self._sel))
        content.add_widget(btns)
        super().__init__(title=title, content=content, size_hint=(0.95, 0.85), **kw)
    
    def _sel(self, *a):
        s = self.fc.selection
        self.dismiss()
        if s:
            self.callback(s[0])
        else:
            self.callback(None)
    
    def _sel_dir(self, *a):
        self.dismiss()
        self.callback(self.fc.path)


# === MODEL SELECTOR ===
class ModelSelectorPopup(Popup):
    PROVIDERS = {
        'openai': {'name': 'OpenAI', 'color': (0.2, 0.65, 0.4, 1)},
        'anthropic': {'name': 'Anthropic', 'color': (0.85, 0.5, 0.3, 1)},
        'google': {'name': 'Google', 'color': (0.3, 0.5, 0.85, 1)},
        'meta-llama': {'name': 'Meta', 'color': (0.3, 0.4, 0.75, 1)},
        'mistralai': {'name': 'Mistral', 'color': (0.75, 0.4, 0.2, 1)},
        'deepseek': {'name': 'DeepSeek', 'color': (0.5, 0.3, 0.65, 1)},
    }
    
    def __init__(self, models, current_model, on_select=None, **kw):
        self.models, self.current_model, self.on_select = models, current_model, on_select
        self.current_provider = None
        
        content = BoxLayout(orientation='vertical', padding=dp(6), spacing=dp(4))
        
        sr = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(2))
        self.search = DarkInput(hint_text="Search...", multiline=False, font_size=sp(11))
        self.search.bind(text=lambda i, t: self._build(t.lower()))
        sr.add_widget(self.search)
        sr.add_widget(RBtn(text="X", size_hint_x=None, width=dp(36), bg=C['card'],
                          on_press=lambda *a: setattr(self.search, 'text', '')))
        content.add_widget(sr)
        
        self.tabs = BoxLayout(size_hint_y=None, height=dp(32), spacing=dp(2))
        ab = RBtn(text="All", bg=C['accent'], font_size=sp(8)); ab.provider = None
        ab.bind(on_press=self._sel_prov); self.tabs.add_widget(ab)
        for pid, info in self.PROVIDERS.items():
            b = RBtn(text=info['name'][:4], bg=C['card'], font_size=sp(7))
            b.provider = pid; b.bind(on_press=self._sel_prov); self.tabs.add_widget(b)
        content.add_widget(self.tabs)
        
        self.scroll = ScrollView()
        self.lst = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(2))
        self.lst.bind(minimum_height=self.lst.setter('height'))
        self.scroll.add_widget(self.lst)
        content.add_widget(self.scroll)
        
        content.add_widget(RBtn(text="Close", size_hint_y=None, height=dp(36), bg=C['card'],
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
            self.lst.add_widget(Label(text="No models - tap Ref", size_hint_y=None, height=dp(36), color=C['dim']))
            return
        
        grp = {}
        for mid, info in all_m.items():
            prov = mid.split('/')[0] if '/' in mid else 'other'
            grp.setdefault(prov, []).append((mid, info))
        
        if self.current_provider:
            grp = {k: v for k, v in grp.items() if k == self.current_provider}
        
        for prov, models in sorted(grp.items()):
            pi = self.PROVIDERS.get(prov, {'name': prov.title(), 'color': C['dim']})
            hdr = BoxLayout(size_hint_y=None, height=dp(24))
            with hdr.canvas.before:
                Color(*pi['color'])
                hdr.bg = Rectangle(pos=hdr.pos, size=hdr.size)
            hdr.bind(pos=lambda w, *a: setattr(w.bg, 'pos', w.pos), size=lambda w, *a: setattr(w.bg, 'size', w.size))
            hdr.add_widget(Label(text=f"{pi['name']} ({len(models)})", font_size=sp(9), color=C['text'], bold=True))
            self.lst.add_widget(hdr)
            
            for mid, info in sorted(models, key=lambda x: x[1].get('name', x[0])):
                name = info.get('name', mid.split('/')[-1])
                if filt and filt not in name.lower() and filt not in mid.lower(): continue
                
                cur = mid == self.current_model
                card = Card(size_hint_y=None, height=dp(44), bg=C['accent'] if cur else C['card'])
                card.add_widget(Label(text=name[:32]+("*" if cur else ""), font_size=sp(9), color=C['text']))
                card.model_id = mid
                card.bind(on_touch_down=lambda w, t, m=mid: self._sel(m) if w.collide_point(*t.pos) else None)
                self.lst.add_widget(card)
    
    def _sel(self, mid):
        if self.on_select: self.on_select(mid)
        self.dismiss()


# === LOG VIEWER ===
class LogViewer(Popup):
    def __init__(self, **kw):
        content = BoxLayout(orientation='vertical', padding=dp(6), spacing=dp(4))
        scroll = ScrollView()
        self.lbl = Label(text=LOGBUF.get(), font_size=sp(8), color=C['text'],
                        text_size=(Window.width-dp(24), None), halign='left', valign='top', size_hint_y=None)
        self.lbl.bind(texture_size=lambda *x: setattr(self.lbl, 'height', self.lbl.texture_size[1]))
        scroll.add_widget(self.lbl)
        content.add_widget(scroll)
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Refresh", bg=C['accent'], on_press=lambda *a: setattr(self.lbl, 'text', LOGBUF.get())))
        btns.add_widget(RBtn(text="Close", bg=C['card'], on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)
        super().__init__(title="Logs", content=content, size_hint=(0.95, 0.85), **kw)


# === SETTINGS ===
class SettingsPopup(Popup):
    def __init__(self, config, secrets, on_save=None, **kw):
        self.config, self.secrets, self.on_save = config, secrets, on_save
        content = BoxLayout(orientation='vertical', padding=dp(8), spacing=dp(4))
        
        content.add_widget(Label(text="API Key:", color=C['text'], size_hint_y=None, height=dp(18)))
        self.key = DarkInput(text=secrets.get("api_key", ""), hint_text="sk-or-v1-...", password=True,
                            multiline=False, size_hint_y=None, height=dp(36))
        content.add_widget(self.key)
        
        content.add_widget(Label(text="Base URL:", color=C['text'], size_hint_y=None, height=dp(18)))
        self.url = DarkInput(text=config.get("base_url", ""), multiline=False, size_hint_y=None, height=dp(36))
        content.add_widget(self.url)
        
        content.add_widget(RBtn(text="Change Theme...", bg=C['card'], size_hint_y=None, height=dp(36),
                               on_press=lambda *a: ThemePopup(self.on_save).open()))
        content.add_widget(BoxLayout())
        
        btns = BoxLayout(size_hint_y=None, height=dp(40), spacing=dp(4))
        btns.add_widget(RBtn(text="Save", bg=C['accent'], on_press=self._save))
        btns.add_widget(RBtn(text="Cancel", bg=C['card'], on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)
        super().__init__(title="Settings", content=content, size_hint=(0.9, 0.5), **kw)
    
    def _save(self, *a):
        if self.key.text.strip(): self.secrets.set("api_key", self.key.text.strip())
        self.config.set("base_url", self.url.text.strip())
        self.config.save()
        if self.on_save: self.on_save()
        self.dismiss()


# === MESSAGE BUBBLE ===
class MsgBubble(BoxLayout):
    def __init__(self, msg, models, on_exclude=None, on_include=None, **kw):
        super().__init__(orientation='vertical', size_hint_y=None, padding=dp(6), spacing=dp(2), **kw)
        self.msg = msg
        excluded = msg.get('status') == 'excluded'
        bg = C['user'] if msg['role'] == 'user' else C['ai']
        
        with self.canvas.before:
            Color(*bg, 0.5 if excluded else 1)
            self.rect = RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(6)])
        self.bind(pos=lambda *a: setattr(self.rect, 'pos', self.pos),
                  size=lambda *a: setattr(self.rect, 'size', self.size))
        
        hdr = BoxLayout(size_hint_y=None, height=dp(22))
        role = "You" if msg['role'] == 'user' else models.name(msg.get('model'))
        hdr.add_widget(Label(text=role, font_size=sp(9), color=C['dim'], halign='left', size_hint_x=0.5))
        hdr.add_widget(RBtn(text="Copy", font_size=sp(8), size_hint_x=0.2, bg=C['card'], on_press=self._copy))
        hdr.add_widget(RBtn(text="Show" if excluded else "Hide", font_size=sp(8), size_hint_x=0.3, bg=C['card'],
                           on_press=lambda *a: (on_include(msg['id']) if excluded else on_exclude(msg['id'])) if on_exclude and on_include else None))
        self.add_widget(hdr)
        
        text = msg['text']
        self.txt = DarkInput(text=text, readonly=True, font_size=sp(11), size_hint_y=None, multiline=True)
        if excluded:
            self.txt.foreground_color = (*C['text'][:3], 0.5)
        
        lines = text.count('\n') + 1
        cpl = max(1, int((Window.width - dp(50)) / dp(7)))
        wrapped = max(lines, len(text) // cpl + 1)
        h = min(dp(280), max(dp(28), wrapped * dp(15)))
        self.txt.height = h
        self.add_widget(self.txt)
        
        att = msg.get('attachments', [])
        if att:
            self.add_widget(Label(text=f"[{len(att)} file(s)]", font_size=sp(8), color=C['dim'], size_hint_y=None, height=dp(14)))
            h += dp(14)
        
        self.height = dp(22) + h + dp(10)
    
    def _copy(self, *a):
        try: Clipboard.copy(self.msg['text']); show_toast("Copied!")
        except: pass


# === STREAMING BUBBLE ===
class StreamingBubble(BoxLayout):
    def __init__(self, models, model_id, **kw):
        super().__init__(orientation='vertical', size_hint_y=None, height=dp(60), padding=dp(6), **kw)
        with self.canvas.before:
            Color(*C['ai'])
            self.rect = RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(6)])
        self.bind(pos=lambda *a: setattr(self.rect, 'pos', self.pos),
                  size=lambda *a: setattr(self.rect, 'size', self.size))
        
        hdr = BoxLayout(size_hint_y=None, height=dp(18))
        hdr.add_widget(Label(text=models.name(model_id), font_size=sp(8), color=C['dim']))
        self.st = Label(text="...", font_size=sp(8), color=C['warn'], size_hint_x=0.2)
        hdr.add_widget(self.st)
        self.add_widget(hdr)
        
        self.txt = DarkInput(text="", readonly=True, font_size=sp(11), size_hint_y=None, height=dp(36), multiline=True)
        self.add_widget(self.txt)
        self._text = ""
        self._reasoning = ""
        self.reason_lbl = None
    
    def append(self, chunk):
        self._text += chunk
        self.txt.text = self._text
        lines = self._text.count('\n') + 1
        cpl = max(1, int((Window.width - dp(50)) / dp(7)))
        wrapped = max(lines, len(self._text) // cpl + 1)
        h = min(dp(280), max(dp(36), wrapped * dp(15)))
        self.txt.height = h
        self.height = dp(18) + h + dp(10)
    
    def finish(self):
        self.st.text = "OK"
        self.st.color = C['ok']
    
    def append_reasoning(self, chunk):
        if not self.reason_lbl:
            self.reason_lbl = Label(text="💭 ", font_size=sp(8), color=C['dim'], 
                                   size_hint_y=None, height=dp(18))
            self.add_widget(self.reason_lbl, index=0)
        self._reasoning += chunk
        self.reason_lbl.text = "💭 " + self._reasoning[-60:]


# === MEMORY PANEL (with files/dirs as items) ===
class MemoryPanel(Panel):
    def __init__(self, memory, **kw):
        super().__init__(**kw)
        self.memory = memory
        self.panel_width = dp(280)
        
        hdr = BoxLayout(size_hint_y=None, height=dp(36))
        hdr.add_widget(Label(text="Memory", font_size=sp(12), color=C['text'], bold=True))
        hdr.add_widget(RBtn(text="X", size_hint_x=None, width=dp(36), bg=C['card'], on_press=lambda *a: self.close()))
        self.add_widget(hdr)
        
        # Add buttons
        add_row = BoxLayout(size_hint_y=None, height=dp(30), spacing=dp(2))
        add_row.add_widget(RBtn(text="+Text", bg=C['accent'], font_size=sp(9), on_press=lambda *a: self._add(None, 'text')))
        add_row.add_widget(RBtn(text="+Folder", bg=C['ok'], font_size=sp(9), on_press=lambda *a: self._add(None, 'folder')))
        add_row.add_widget(RBtn(text="+File", bg=C['warn'], font_size=sp(9), on_press=lambda *a: self._add_file(None)))
        add_row.add_widget(RBtn(text="+Dir", bg=C['card'], font_size=sp(9), on_press=lambda *a: self._add_dir(None)))
        self.add_widget(add_row)
        
        self.scroll = ScrollView()
        self.tree = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(1))
        self.tree.bind(minimum_height=self.tree.setter('height'))
        self.scroll.add_widget(self.tree)
        self.add_widget(self.scroll)
    
    def refresh(self):
        self.tree.clear_widgets()
        if not self.memory:
            self.tree.add_widget(Label(text="No memory", size_hint_y=None, height=dp(30), color=C['dim']))
            return
        self._build(None)
        if not self.tree.children:
            self.tree.add_widget(Label(text="Empty", size_hint_y=None, height=dp(30), color=C['dim']))
    
    def _build(self, parent_id):
        for n in self.memory.get_children(parent_id):
            row = BoxLayout(size_hint_y=None, height=dp(34), spacing=dp(2))
            
            # Toggle
            act = RBtn(text="+" if n.active else "-", size_hint_x=None, width=dp(28),
                      bg=C['ok'] if n.active else C['dim'], font_size=sp(10))
            act.node_id = n.id
            act.bind(on_press=self._toggle)
            row.add_widget(act)
            
            # Icon + indent
            indent = "  " * n.depth
            icons = {'folder': '[D]', 'file': '[F]', 'dir': '[/]', 'text': '[.]'}
            icon = icons.get(n.node_type, '[?]')
            
            txt = f"{indent}{icon} {n.content[:16]}"
            if len(n.content) > 16: txt += ".."
            
            btn = RBtn(text=txt, bg=C['card'], font_size=sp(9), halign='left')
            btn.node = n
            btn.bind(on_press=self._edit)
            row.add_widget(btn)
            
            # Delete
            del_btn = RBtn(text="x", size_hint_x=None, width=dp(24), bg=C['err'], font_size=sp(9))
            del_btn.node_id = n.id
            del_btn.bind(on_press=self._delete)
            row.add_widget(del_btn)
            
            self.tree.add_widget(row)
            self._build(n.id)
    
    def _toggle(self, btn):
        for n in self.memory._cache.values():
            if n.id == btn.node_id:
                self.memory.update_node(n.id, active=not n.active)
                break
        self.refresh()
    
    def _delete(self, btn):
        self.memory.delete_node(btn.node_id, True)
        self.refresh()
    
    def _edit(self, btn):
        n = btn.node
        content = BoxLayout(orientation='vertical', padding=dp(6), spacing=dp(4))
        
        # For file/dir types, show path
        if n.node_type in ('file', 'dir'):
            path = n.metadata.get('path', n.content)
            content.add_widget(Label(text=f"Path: {path[-40:]}", color=C['dim'], size_hint_y=None, height=dp(20), font_size=sp(9)))
            
            # Show file list for dirs
            if n.node_type == 'dir' and os.path.isdir(path):
                files = os.listdir(path)[:10]
                flist = "\n".join(files) + (f"\n...+{len(os.listdir(path))-10} more" if len(os.listdir(path)) > 10 else "")
                content.add_widget(Label(text=flist, color=C['text'], size_hint_y=None, 
                                        height=dp(min(150, 20+len(files)*14)), font_size=sp(9), halign='left'))
        
        content.add_widget(Label(text="Content/Name:", color=C['text'], size_hint_y=None, height=dp(16)))
        txt = DarkInput(text=n.content, multiline=True, size_hint_y=0.3)
        content.add_widget(txt)
        
        tr = BoxLayout(size_hint_y=None, height=dp(34))
        tr.add_widget(Label(text="Type:", color=C['text'], size_hint_x=0.25))
        ts = Spinner(text=n.node_type, values=['folder', 'text', 'file', 'dir'], size_hint_x=0.75, background_color=C['card'])
        tr.add_widget(ts)
        content.add_widget(tr)
        
        info = f"ID: {n.id[:8]}.. | Depth: {n.depth}"
        content.add_widget(Label(text=info, font_size=sp(8), color=C['dim'], size_hint_y=None, height=dp(16)))
        
        btns = BoxLayout(size_hint_y=None, height=dp(34), spacing=dp(3))
        btns.add_widget(RBtn(text="+Child", bg=C['ok'], font_size=sp(9), on_press=lambda *a: self._add(n.id, 'text', popup)))
        btns.add_widget(RBtn(text="Save", bg=C['accent'], font_size=sp(9), on_press=lambda *a: (
            self.memory.update_node(n.id, content=txt.text.strip(), node_type=ts.text), popup.dismiss(), self.refresh())))
        
        # ZIP for folders/dirs
        if n.node_type in ('folder', 'dir'):
            btns.add_widget(RBtn(text="ZIP", bg=C['warn'], font_size=sp(9), on_press=lambda *a: self._zip(n)))
        
        content.add_widget(btns)
        
        popup = Popup(title=f"Edit: {n.content[:16]}", content=content, size_hint=(0.92, 0.65))
        popup.open()
    
    def _add(self, parent_id, node_type, parent_popup=None):
        content = BoxLayout(orientation='vertical', padding=dp(6), spacing=dp(4))
        content.add_widget(Label(text="Content:", color=C['text'], size_hint_y=None, height=dp(16)))
        txt = DarkInput(hint_text="Enter...", multiline=True, size_hint_y=0.5)
        content.add_widget(txt)
        
        def create(*a):
            if txt.text.strip():
                self.memory.add_node(txt.text.strip(), parent_id, node_type)
                popup.dismiss()
                if parent_popup: parent_popup.dismiss()
                self.refresh()
        content.add_widget(RBtn(text="Create", bg=C['accent'], size_hint_y=None, height=dp(36), on_press=create))
        popup = Popup(title=f"Add {node_type}", content=content, size_hint=(0.9, 0.45))
        popup.open()
    
    def _add_file(self, parent_id):
        def on_file(path):
            if path and os.path.isfile(path):
                name = os.path.basename(path)
                node_id = self.memory.add_node(name, parent_id, 'file')
                self.memory.update_node(node_id, metadata={'path': path})
                self.refresh()
                show_toast(f"Added: {name}")
        FilePickerPopup(on_file, "Select File").open()
    
    def _add_dir(self, parent_id):
        def on_dir(path):
            if path and os.path.isdir(path):
                name = os.path.basename(path) or path
                node_id = self.memory.add_node(name, parent_id, 'dir')
                self.memory.update_node(node_id, metadata={'path': path})
                self.refresh()
                show_toast(f"Added dir: {name}")
        FilePickerPopup(on_dir, "Select Directory", allow_dirs=True).open()
    
    def _zip(self, node):
        try:
            dl = Path("/storage/emulated/0/Download")
            if not dl.exists(): dl = Path.home() / "Downloads"
            
            zip_name = f"{node.content[:20].replace(' ', '_').replace('/', '_')}.zip"
            zip_path = dl / zip_name
            
            with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
                if node.node_type == 'dir':
                    # Real directory
                    dir_path = node.metadata.get('path', '')
                    if os.path.isdir(dir_path):
                        for root, dirs, files in os.walk(dir_path):
                            for f in files:
                                fp = os.path.join(root, f)
                                zf.write(fp, os.path.relpath(fp, dir_path))
                else:
                    # Memory folder - add children
                    zf.writestr("README.txt", node.content)
                    def add_children(pid, prefix):
                        for ch in self.memory.get_children(pid):
                            nm = ch.content[:30].replace('/', '_')
                            if ch.node_type == 'file':
                                fp = ch.metadata.get('path', '')
                                if os.path.isfile(fp):
                                    zf.write(fp, f"{prefix}/{os.path.basename(fp)}")
                            elif ch.node_type == 'dir':
                                dp = ch.metadata.get('path', '')
                                if os.path.isdir(dp):
                                    for f in os.listdir(dp):
                                        zf.write(os.path.join(dp, f), f"{prefix}/{nm}/{f}")
                            else:
                                zf.writestr(f"{prefix}/{nm}.txt", ch.content)
                            add_children(ch.id, f"{prefix}/{nm}")
                    add_children(node.id, node.content[:20])
            
            show_toast(f"ZIP: {zip_name}")
        except Exception as e:
            show_toast(f"ZIP error: {e}")
            LOGBUF.add(f"ZIP error: {e}")


# === CONVERSATION PANEL ===
class ConvPanel(Panel):
    def __init__(self, engine, on_select=None, **kw):
        super().__init__(**kw)
        self.engine, self.on_select = engine, on_select
        hdr = BoxLayout(size_hint_y=None, height=dp(36))
        hdr.add_widget(Label(text="Chats", font_size=sp(12), color=C['text'], bold=True))
        hdr.add_widget(RBtn(text="X", size_hint_x=None, width=dp(36), bg=C['card'], on_press=lambda *a: self.close()))
        self.add_widget(hdr)
        self.scroll = ScrollView()
        self.lst = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(2))
        self.lst.bind(minimum_height=self.lst.setter('height'))
        self.scroll.add_widget(self.lst)
        self.add_widget(self.scroll)
    
    def refresh(self):
        self.lst.clear_widgets()
        if not self.engine.db: return
        for c in self.engine.db.list_convs():
            card = Card(size_hint_y=None, height=dp(44))
            card.add_widget(Label(text=c['title'][:20], font_size=sp(10), color=C['text'], halign='left', size_hint_y=None, height=dp(22)))
            card.add_widget(Label(text=c['updated'][:16], font_size=sp(8), color=C['dim'], size_hint_y=None, height=dp(14)))
            card.conv = c
            card.bind(on_touch_down=lambda w, t, cv=c: self._sel(cv) if w.collide_point(*t.pos) else None)
            self.lst.add_widget(card)
    
    def _sel(self, conv):
        self.engine.load_conv(conv['id'])
        if self.on_select: self.on_select()
        self.close()


# === API PREVIEW ===
class ApiPreviewPopup(Popup):
    def __init__(self, api_json, on_send=None, **kw):
        self.on_send = on_send
        content = BoxLayout(orientation='vertical', padding=dp(6), spacing=dp(4))
        content.add_widget(Label(text="API Preview", font_size=sp(10), color=C['accent'], size_hint_y=None, height=dp(18)))
        
        self.editor = DarkInput(text=api_json, font_size=sp(8), multiline=True)
        content.add_widget(self.editor)
        
        self.stats = Label(text="", font_size=sp(8), color=C['dim'], size_hint_y=None, height=dp(16))
        self._upd()
        self.editor.bind(text=lambda *a: self._upd())
        content.add_widget(self.stats)
        
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Copy", bg=C['card'], font_size=sp(9), on_press=lambda *a: (Clipboard.copy(self.editor.text), show_toast("Copied"))))
        btns.add_widget(RBtn(text="Cancel", bg=C['card'], font_size=sp(9), on_press=lambda *a: self.dismiss()))
        btns.add_widget(RBtn(text="Send", bg=C['accent'], font_size=sp(9), on_press=self._do))
        content.add_widget(btns)
        super().__init__(title="API", content=content, size_hint=(0.95, 0.85), **kw)
    
    def _upd(self):
        t = self.editor.text
        self.stats.text = f"~{len(t)//4:,} tok | {len(t):,} ch"
    
    def _do(self, *a):
        self.dismiss()
        if self.on_send: self.on_send(self.editor.text)


# === CHAT PANEL ===
class ChatPanel(BoxLayout):
    def __init__(self, engine, models, on_settings=None, **kw):
        super().__init__(orientation='vertical', **kw)
        self.engine, self.models, self.on_settings = engine, models, on_settings
        self.sending = False
        self.pending_files = []
        self.streaming_bubble = None
        self._long_press_ev = None
        # Feature toggles
        self.web_search = False
        self.deep_research = False
        self.reasoning_effort = None  # None=adaptive, low, medium, high, max
        self._build()
    
    def _build(self):
        top = BoxLayout(size_hint_y=None, height=dp(42), padding=dp(2), spacing=dp(2))
        
        self.model_btn = RBtn(text=self.models.name(self.engine.config.get('default_model'))[:12],
                             size_hint_x=0.36, bg=C['card'], font_size=sp(9))
        self.model_btn.bind(on_press=self._open_models)
        top.add_widget(self.model_btn)
        
        for txt, clr, fn in [("Ref", C['card'], self._refresh_models), ("New", C['accent'], self._new),
                             ("Cfg", C['card'], lambda *a: self.on_settings() if self.on_settings else None),
                             ("Log", C['warn'], lambda *a: LogViewer().open())]:
            top.add_widget(RBtn(text=txt, size_hint_x=0.16, bg=clr, font_size=sp(9), on_press=fn))
        self.add_widget(top)
        
        # Feature toggles row
        feat = BoxLayout(size_hint_y=None, height=dp(30), padding=dp(2), spacing=dp(2))
        self.web_btn = RBtn(text="Web", size_hint_x=0.25, bg=C['card'], font_size=sp(9))
        self.web_btn.bind(on_press=self._toggle_web)
        feat.add_widget(self.web_btn)
        self.research_btn = RBtn(text="Deep", size_hint_x=0.25, bg=C['card'], font_size=sp(9))
        self.research_btn.bind(on_press=self._toggle_research)
        feat.add_widget(self.research_btn)
        self.reason_btn = RBtn(text="Auto", size_hint_x=0.25, bg=C['card'], font_size=sp(9))
        self.reason_btn.bind(on_press=self._cycle_reasoning)
        feat.add_widget(self.reason_btn)
        self.think_lbl = Label(text="", size_hint_x=0.25, font_size=sp(9), color=C['dim'])
        feat.add_widget(self.think_lbl)
        self.add_widget(feat)
        
        self.scroll = ScrollView()
        self.msgs = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(4), padding=dp(4))
        self.msgs.bind(minimum_height=self.msgs.setter('height'))
        self.scroll.add_widget(self.msgs)
        self.add_widget(self.scroll)
        
        # Attachment bar (hidden by default)
        self.att_bar = BoxLayout(size_hint_y=None, height=0, padding=[dp(4), 0], spacing=dp(2))
        self.att_lbl = Label(text="", font_size=sp(8), color=C['ok'], halign='left')
        self.att_bar.add_widget(self.att_lbl)
        clear_btn = RBtn(text="X", size_hint_x=None, width=dp(28), bg=C['err'], font_size=sp(9))
        clear_btn.bind(on_press=self._clear_files)
        self.att_bar.add_widget(clear_btn)
        self.add_widget(self.att_bar)
        
        # Input
        inp = Card(size_hint_y=None, height=dp(76), orientation='vertical', spacing=dp(2))
        row = BoxLayout(spacing=dp(4))
        self.txt_in = DarkInput(hint_text="Message...", multiline=True, font_size=sp(11))
        row.add_widget(self.txt_in)
        
        self.send_btn = RBtn(text="Send", size_hint_x=None, width=dp(48), bg=C['accent'], font_size=sp(10))
        self.send_btn.bind(on_release=self._send)
        self.send_btn.bind(on_touch_down=self._on_send_td)
        self.send_btn.bind(on_touch_up=self._on_send_tu)
        row.add_widget(self.send_btn)
        inp.add_widget(row)
        
        inp.add_widget(RBtn(text="+ Attach", size_hint_y=None, height=dp(22), bg=C['card'], font_size=sp(9),
                           on_press=lambda *a: FilePickerPopup(self._on_file).open()))
        self.add_widget(inp)
        
        self.status = Label(text="Ready", size_hint_y=None, height=dp(16), font_size=sp(8), color=C['dim'])
        self.add_widget(self.status)
    
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
        self.reason_btn.text = labels[idx]
        self.reason_btn.background_color = C['accent'] if self.reasoning_effort else C['card']
        show_toast(f"Reasoning: {labels[idx]}")
    
    def _open_models(self, *a):
        def on_sel(mid):
            self.engine.config.set('default_model', mid)
            self.model_btn.text = self.models.name(mid)[:12]
        ModelSelectorPopup(self.models, self.engine.config.get('default_model'), on_sel).open()
    
    def _refresh_models(self, *a):
        self.status.text = "Loading..."
        def up():
            ok = self.models.update_from_api()
            Clock.schedule_once(lambda dt: self._models_done(ok))
        threading.Thread(target=up).start()
    
    def _models_done(self, ok):
        if ok:
            self.status.text = f"{len(self.models.all())} models"
            self.model_btn.text = self.models.name(self.engine.config.get('default_model'))[:12]
        else:
            self.status.text = "Failed"
    
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
            self.att_bar.height = dp(24)
        else:
            self.att_bar.height = 0
    
    def _clear_files(self, *a):
        self.pending_files.clear()
        self._upd_att()
    
    def _on_send_td(self, w, touch):
        if w.collide_point(*touch.pos):
            self._long_press_ev = Clock.schedule_once(lambda dt: self._show_api(), 0.5)
            touch.grab(w)
        return False
    
    def _on_send_tu(self, w, touch):
        if touch.grab_current == w:
            touch.ungrab(w)
            if self._long_press_ev:
                self._long_press_ev.cancel()
                self._long_press_ev = None
        return False
    
    def _build_api_json(self):
        text = self.txt_in.text.strip()
        if not text: return None
        msgs = []
        if self.engine.memory:
            mem = self.engine.memory.get_active_context()
            if mem: msgs.append({"role": "system", "content": f"Memory:\n{mem}"})
        if self.engine.conv and self.engine.db:
            for m in self.engine.db.get_msgs(self.engine.conv['id']):
                if m['status'] == 'active':
                    msgs.append({"role": m['role'], "content": m['text']})
        msgs.append({"role": "user", "content": text})
        req = {"model": self.engine.config.get('default_model'), "messages": msgs,
               "temperature": self.engine.config.get('temperature', 0.7),
               "max_tokens": self.engine.config.get('max_tokens', 4096), "stream": True}
        if self.pending_files: req["_attachments"] = self.pending_files.copy()
        return json.dumps(req, indent=2, ensure_ascii=False)
    
    def _show_api(self):
        api = self._build_api_json()
        if not api: show_toast("Type msg"); return
        ApiPreviewPopup(api, lambda e: self._send(None)).open()
    
    def _send(self, *a):
        if self.sending: return
        text = self.txt_in.text.strip()
        if not text: return
        if not self.engine.secrets.get("api_key"):
            if self.on_settings: self.on_settings()
            return
        
        self.sending = True
        self.txt_in.text = ""
        self.status.text = "Sending..."
        
        att = self.pending_files.copy()
        self.pending_files.clear()
        self._upd_att()
        
        model = self.engine.config.get('default_model')
        self.streaming_bubble = StreamingBubble(self.models, model)
        self.msgs.add_widget(self.streaming_bubble)
        Clock.schedule_once(lambda dt: setattr(self.scroll, 'scroll_y', 0), 0.1)
        
        def thread():
            try:
                def on_chunk(c): Clock.schedule_once(lambda dt: self._on_chunk(c))
                def on_reasoning(r): Clock.schedule_once(lambda dt: self._on_reasoning(r))
                self.engine.send(text, attachments=att, on_chunk=on_chunk, on_reasoning=on_reasoning,
                                web_search=self.web_search, deep_research=self.deep_research,
                                reasoning_effort=self.reasoning_effort)
                Clock.schedule_once(lambda dt: self._ok())
            except Exception as e:
                Clock.schedule_once(lambda dt: self._err(str(e)))
        threading.Thread(target=thread).start()
    
    def _on_reasoning(self, chunk):
        self.think_lbl.text = "💭"
        if self.streaming_bubble:
            self.streaming_bubble.append_reasoning(chunk)
    
    def _on_chunk(self, chunk):
        if self.streaming_bubble:
            self.streaming_bubble.append(chunk)
            Clock.schedule_once(lambda dt: setattr(self.scroll, 'scroll_y', 0), 0)
    
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
        self.status.text = f"Err: {err[:22]}"
        self.think_lbl.text = ""
        LOGBUF.add(f"Error: {err}")
    
    def refresh(self):
        self.msgs.clear_widgets()
        if not self.engine.conv or not self.engine.db: return
        for m in self.engine.db.get_msgs(self.engine.conv['id'], True):
            self.msgs.add_widget(MsgBubble(m, self.models,
                lambda mid: (self.engine.db.set_msg_status(mid, 'excluded'), self.refresh()),
                lambda mid: (self.engine.db.set_msg_status(mid, 'active'), self.refresh())))
        Clock.schedule_once(lambda dt: setattr(self.scroll, 'scroll_y', 0))
