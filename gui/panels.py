"""
ChatADHD v0.4.5 - Fixed Kivy FileChooser
Nie używa plyer (nie działa w Pydroid)
Używa natywnego Kivy FileChooser z ulepszeniami
"""
import os
import threading
import logging
from pathlib import Path

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.scrollview import ScrollView
from kivy.uix.textinput import TextInput
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.uix.spinner import Spinner
from kivy.uix.checkbox import CheckBox
from kivy.uix.button import Button
from kivy.uix.filechooser import FileChooserListView
from kivy.core.window import Window
from kivy.metrics import dp, sp
from kivy.clock import Clock
from kivy.graphics import Color, Rectangle, RoundedRectangle

log = logging.getLogger('panels')

# === COLORS ===
C = {
    'bg': (0.11, 0.11, 0.14, 1),
    'card': (0.18, 0.18, 0.22, 1),
    'text': (0.92, 0.92, 0.92, 1),
    'dim': (0.55, 0.55, 0.58, 1),
    'accent': (0.35, 0.55, 0.95, 1),
    'user': (0.22, 0.28, 0.38, 1),
    'ai': (0.18, 0.22, 0.18, 1),
    'ok': (0.25, 0.55, 0.35, 1),
    'err': (0.65, 0.25, 0.25, 1),
    'warn': (0.75, 0.55, 0.25, 1),
}

# === LOG BUFFER ===
class LogBuffer:
    def __init__(self):
        self.lines = []
    
    def add(self, msg):
        from datetime import datetime
        self.lines.append(f"{datetime.now():%H:%M:%S} {msg}")
        self.lines = self.lines[-300:]
    
    def get(self):
        return "\n".join(self.lines)

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
        self.padding = dp(8)
        with self.canvas.before:
            Color(*(bg or C['card']))
            self.rect = RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(8)])
        self.bind(pos=lambda *a: setattr(self.rect, 'pos', self.pos),
                  size=lambda *a: setattr(self.rect, 'size', self.size))

class Panel(BoxLayout):
    """Sliding panel"""
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

# === FILE PICKER POPUP (Kivy native) ===
class FilePickerPopup(Popup):
    """
    Popup z Kivy FileChooser - działa w Pydroid!
    Szybki dostęp do popularnych folderów Android.
    """
    def __init__(self, callback, title="Wybierz plik", filters=None, **kw):
        self.callback = callback
        
        content = BoxLayout(orientation='vertical', spacing=dp(8), padding=dp(8))
        
        # === Quick access bar ===
        quick_bar = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        
        quick_paths = [
            ("📁", "/storage/emulated/0"),
            ("📥", "/storage/emulated/0/Download"),
            ("📄", "/storage/emulated/0/Documents"),
            ("🖼", "/storage/emulated/0/Pictures"),
            ("📷", "/storage/emulated/0/DCIM"),
        ]
        
        for icon, path in quick_paths:
            if os.path.exists(path):
                btn = RBtn(text=icon, size_hint_x=None, width=dp(44), bg=C['card'])
                btn.path = path
                btn.bind(on_release=self._quick_nav)
                quick_bar.add_widget(btn)
        
        content.add_widget(quick_bar)
        
        # === Current path label ===
        self.path_label = Label(
            text="", 
            size_hint_y=None, 
            height=dp(24),
            font_size=sp(11),
            color=C['dim'],
            halign='left'
        )
        self.path_label.bind(size=self.path_label.setter('text_size'))
        content.add_widget(self.path_label)
        
        # === File chooser ===
        start_path = "/storage/emulated/0/Download"
        if not os.path.exists(start_path):
            start_path = "/storage/emulated/0"
        if not os.path.exists(start_path):
            start_path = str(Path.home())
        
        self.file_chooser = FileChooserListView(
            path=start_path,
            filters=filters or ['*'],
            dirselect=False
        )
        self.file_chooser.bind(path=self._on_path)
        self.file_chooser.bind(selection=self._on_selection)
        content.add_widget(self.file_chooser)
        
        # === Selection info ===
        self.sel_label = Label(
            text="Wybierz plik",
            size_hint_y=None,
            height=dp(28),
            font_size=sp(12),
            color=C['ok']
        )
        content.add_widget(self.sel_label)
        
        # === Buttons ===
        btn_row = BoxLayout(size_hint_y=None, height=dp(48), spacing=dp(10))
        
        cancel_btn = RBtn(text="Anuluj", bg=C['card'])
        cancel_btn.bind(on_release=self._cancel)
        btn_row.add_widget(cancel_btn)
        
        select_btn = RBtn(text="Wybierz", bg=C['accent'])
        select_btn.bind(on_release=self._select)
        btn_row.add_widget(select_btn)
        
        content.add_widget(btn_row)
        
        super().__init__(
            title=title, 
            content=content, 
            size_hint=(0.95, 0.85),
            **kw
        )
        
        # Update path label
        self._on_path(self.file_chooser, self.file_chooser.path)
    
    def _quick_nav(self, btn):
        if hasattr(btn, 'path') and os.path.exists(btn.path):
            self.file_chooser.path = btn.path
    
    def _on_path(self, instance, path):
        # Skróć ścieżkę do wyświetlenia
        display = path.replace("/storage/emulated/0", "📱")
        self.path_label.text = display
    
    def _on_selection(self, instance, selection):
        if selection:
            fname = os.path.basename(selection[0])
            try:
                size = os.path.getsize(selection[0])
                size_str = self._format_size(size)
                self.sel_label.text = f"✓ {fname} ({size_str})"
            except:
                self.sel_label.text = f"✓ {fname}"
        else:
            self.sel_label.text = "Wybierz plik"
    
    def _format_size(self, size):
        for unit in ['B', 'KB', 'MB', 'GB']:
            if size < 1024:
                return f"{size:.0f}{unit}"
            size /= 1024
        return f"{size:.1f}TB"
    
    def _cancel(self, *a):
        self.dismiss()
        self.callback(None)
    
    def _select(self, *a):
        sel = self.file_chooser.selection
        self.dismiss()
        if sel and os.path.isfile(sel[0]):
            LOGBUF.add(f"File selected: {sel[0]}")
            self.callback(sel[0])
        else:
            self.callback(None)


# === LOG VIEWER ===
class LogViewer(Popup):
    def __init__(self, **kw):
        content = BoxLayout(orientation='vertical', padding=dp(8), spacing=dp(8))
        scroll = ScrollView()
        self.lbl = Label(
            text=LOGBUF.get(), 
            font_size=sp(9), 
            color=C['text'],
            text_size=(Window.width - dp(40), None), 
            halign='left', 
            valign='top', 
            size_hint_y=None
        )
        self.lbl.bind(texture_size=lambda *x: setattr(self.lbl, 'height', self.lbl.texture_size[1]))
        scroll.add_widget(self.lbl)
        content.add_widget(scroll)
        
        btns = BoxLayout(size_hint_y=None, height=dp(44), spacing=dp(8))
        rb = RBtn(text="Refresh", bg=C['accent'])
        rb.bind(on_press=lambda *a: setattr(self.lbl, 'text', LOGBUF.get()))
        btns.add_widget(rb)
        cb = RBtn(text="Close", bg=C['card'])
        cb.bind(on_press=lambda *a: self.dismiss())
        btns.add_widget(cb)
        content.add_widget(btns)
        
        super().__init__(title="Logs", content=content, size_hint=(0.95, 0.85), **kw)


# === SETTINGS POPUP ===
class SettingsPopup(Popup):
    def __init__(self, config, secrets, on_save=None, **kw):
        self.config, self.secrets, self.on_save = config, secrets, on_save
        content = BoxLayout(orientation='vertical', padding=dp(12), spacing=dp(8))
        
        content.add_widget(Label(text="API Key:", color=C['text'], size_hint_y=None, height=dp(24)))
        self.key_input = TextInput(
            text=secrets.get("api_key", ""), 
            hint_text="sk-or-v1-...", 
            multiline=False,
            password=True, 
            size_hint_y=None, 
            height=dp(44), 
            background_color=C['card'], 
            foreground_color=C['text']
        )
        content.add_widget(self.key_input)
        
        content.add_widget(Label(text="Base URL:", color=C['text'], size_hint_y=None, height=dp(24)))
        self.url_input = TextInput(
            text=config.get("base_url", ""), 
            multiline=False, 
            size_hint_y=None, 
            height=dp(44),
            background_color=C['card'], 
            foreground_color=C['text']
        )
        content.add_widget(self.url_input)
        
        content.add_widget(BoxLayout())  # Spacer
        
        btns = BoxLayout(size_hint_y=None, height=dp(48), spacing=dp(10))
        save_btn = RBtn(text="Save", bg=C['accent'])
        save_btn.bind(on_press=self._save)
        btns.add_widget(save_btn)
        cancel_btn = RBtn(text="Cancel", bg=C['card'])
        cancel_btn.bind(on_press=lambda *a: self.dismiss())
        btns.add_widget(cancel_btn)
        content.add_widget(btns)
        
        super().__init__(title="Settings", content=content, size_hint=(0.92, 0.55), **kw)
    
    def _save(self, *a):
        if self.key_input.text.strip():
            self.secrets.set("api_key", self.key_input.text.strip())
        self.config.set("base_url", self.url_input.text.strip())
        self.config.save()
        if self.on_save:
            self.on_save()
        self.dismiss()


# === MESSAGE BUBBLE ===
class MsgBubble(Card):
    def __init__(self, msg, models, on_exclude=None, on_include=None, **kw):
        bg = C['user'] if msg['role'] == 'user' else C['ai']
        super().__init__(orientation='vertical', size_hint_y=None, bg=bg, **kw)
        excluded = msg.get('status') == 'excluded'
        opacity = 0.5 if excluded else 1.0
        
        header = BoxLayout(size_hint_y=None, height=dp(24))
        role_name = "You" if msg['role'] == 'user' else models.name(msg.get('model'))
        header.add_widget(Label(
            text=role_name, 
            font_size=sp(11), 
            color=C['dim'], 
            halign='left', 
            size_hint_x=0.75, 
            opacity=opacity
        ))
        
        btn = RBtn(
            text="Show" if excluded else "Hide", 
            font_size=sp(10), 
            size_hint_x=0.25, 
            bg=C['card']
        )
        if on_exclude and on_include:
            btn.bind(on_press=lambda *a: (on_include(msg['id']) if excluded else on_exclude(msg['id'])))
        header.add_widget(btn)
        self.add_widget(header)
        
        # Content
        content = Label(
            text=msg['text'], 
            font_size=sp(13), 
            color=C['text'], 
            text_size=(Window.width - dp(60), None),
            halign='left', 
            valign='top', 
            size_hint_y=None, 
            opacity=opacity
        )
        content.bind(texture_size=lambda *x: setattr(content, 'height', content.texture_size[1] + dp(8)))
        self.add_widget(content)
        
        # Show attachments if any
        attachments = msg.get('attachments', [])
        if attachments:
            att_label = Label(
                text=f"📎 {len(attachments)} attachment(s)",
                font_size=sp(10),
                color=C['dim'],
                size_hint_y=None,
                height=dp(20)
            )
            self.add_widget(att_label)
        
        self.bind(minimum_height=self.setter('height'))


# === MEMORY PANEL ===
class MemoryPanel(Panel):
    def __init__(self, memory, **kw):
        super().__init__(**kw)
        self.memory = memory
        self.panel_width = dp(280)
        
        header = BoxLayout(size_hint_y=None, height=dp(44))
        header.add_widget(Label(text="Memory", font_size=sp(15), color=C['text'], bold=True))
        close_btn = RBtn(text="X", size_hint_x=None, width=dp(44), bg=C['card'])
        close_btn.bind(on_press=lambda *a: self.close())
        header.add_widget(close_btn)
        self.add_widget(header)
        
        self.scroll = ScrollView()
        self.tree_layout = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(2))
        self.tree_layout.bind(minimum_height=self.tree_layout.setter('height'))
        self.scroll.add_widget(self.tree_layout)
        self.add_widget(self.scroll)
    
    def refresh(self):
        self.tree_layout.clear_widgets()
        if not self.memory:
            return
        for node in self.memory.get_children(None):
            self.tree_layout.add_widget(
                Label(text=f"• {node.content[:40]}", size_hint_y=None, height=dp(30), color=C['text'])
            )


# === CONVERSATION PANEL ===
class ConvPanel(Panel):
    def __init__(self, engine, on_select=None, **kw):
        super().__init__(**kw)
        self.engine = engine
        self.on_select = on_select
        
        header = BoxLayout(size_hint_y=None, height=dp(44))
        header.add_widget(Label(text="Chats", font_size=sp(15), color=C['text'], bold=True))
        close_btn = RBtn(text="X", size_hint_x=None, width=dp(44), bg=C['card'])
        close_btn.bind(on_press=lambda *a: self.close())
        header.add_widget(close_btn)
        self.add_widget(header)
        
        self.scroll = ScrollView()
        self.conv_list = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(6))
        self.conv_list.bind(minimum_height=self.conv_list.setter('height'))
        self.scroll.add_widget(self.conv_list)
        self.add_widget(self.scroll)
    
    def refresh(self):
        self.conv_list.clear_widgets()
        if not self.engine.db:
            return
        for conv in self.engine.db.list_convs():
            card = Card(orientation='vertical', size_hint_y=None, height=dp(56))
            card.add_widget(Label(
                text=conv['title'][:24], 
                font_size=sp(12), 
                color=C['text'], 
                halign='left', 
                size_hint_y=None, 
                height=dp(28)
            ))
            card.add_widget(Label(
                text=conv['updated'][:16], 
                font_size=sp(10), 
                color=C['dim'], 
                size_hint_y=None, 
                height=dp(20)
            ))
            card.conv = conv
            card.bind(on_touch_down=lambda w, t, c=conv: self._select(c) if w.collide_point(*t.pos) else None)
            self.conv_list.add_widget(card)
    
    def _select(self, conv):
        self.engine.load_conv(conv['id'])
        if self.on_select:
            self.on_select()
        self.close()


# === CHAT PANEL ===
class ChatPanel(BoxLayout):
    def __init__(self, engine, models, on_settings=None, **kw):
        super().__init__(orientation='vertical', **kw)
        self.engine = engine
        self.models = models
        self.on_settings = on_settings
        self.sending = False
        self.pending_files = []
        self._build()
    
    def _build(self):
        # === Top bar ===
        top = BoxLayout(size_hint_y=None, height=dp(50), padding=dp(4), spacing=dp(4))
        
        names = [v.get('name', k) for k, v in self.models.all().items()] or ['(no models)']
        current = self.models.name(self.engine.config.get('default_model'))
        self.model_spinner = Spinner(
            text=current[:18], 
            values=names, 
            size_hint_x=0.42, 
            background_color=C['card']
        )
        self.model_spinner.bind(text=self._model_changed)
        top.add_widget(self.model_spinner)
        
        for txt, clr, fn in [
            ("Ref", C['card'], self._refresh_models), 
            ("New", C['accent'], self._new),
            ("Cfg", C['card'], lambda *a: self.on_settings() if self.on_settings else None),
            ("Log", C['warn'], lambda *a: LogViewer().open())
        ]:
            btn = RBtn(text=txt, size_hint_x=0.145, bg=clr, font_size=sp(11))
            btn.bind(on_press=fn)
            top.add_widget(btn)
        
        self.add_widget(top)
        
        # === Messages ===
        self.scroll = ScrollView()
        self.msg_layout = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(8), padding=dp(8))
        self.msg_layout.bind(minimum_height=self.msg_layout.setter('height'))
        self.scroll.add_widget(self.msg_layout)
        self.add_widget(self.scroll)
        
        # === Attachments bar ===
        self.att_bar = BoxLayout(size_hint_y=None, height=dp(36), padding=[dp(8), dp(2)], spacing=dp(4))
        self.att_label = Label(text="", size_hint_x=1, font_size=sp(10), color=C['ok'], halign='left')
        self.att_label.bind(size=self.att_label.setter('text_size'))
        self.att_bar.add_widget(self.att_label)
        
        clear_btn = RBtn(text="✕ Clear", size_hint_x=None, width=dp(70), bg=C['err'], font_size=sp(10))
        clear_btn.bind(on_press=self._clear_files)
        self.att_bar.add_widget(clear_btn)
        self.att_bar.opacity = 0
        self.att_bar.height = 0
        self.add_widget(self.att_bar)
        
        # === Input area ===
        inp_card = Card(size_hint_y=None, height=dp(100), orientation='vertical', spacing=dp(4))
        
        inp_row = BoxLayout(spacing=dp(8))
        self.text_input = TextInput(
            hint_text="Message...", 
            multiline=True, 
            background_color=C['card'], 
            foreground_color=C['text'], 
            font_size=sp(13)
        )
        inp_row.add_widget(self.text_input)
        
        send_btn = RBtn(text="Send", size_hint_x=None, width=dp(60), bg=C['accent'])
        send_btn.bind(on_press=self._send)
        inp_row.add_widget(send_btn)
        inp_card.add_widget(inp_row)
        
        # Attach button
        att_btn = RBtn(text="📎 Attach file", size_hint_y=None, height=dp(28), bg=C['card'], font_size=sp(11))
        att_btn.bind(on_press=self._attach)
        inp_card.add_widget(att_btn)
        
        self.add_widget(inp_card)
        
        # === Status ===
        self.status = Label(text="Ready", size_hint_y=None, height=dp(22), font_size=sp(10), color=C['dim'])
        self.add_widget(self.status)
    
    def _model_changed(self, spinner, txt):
        for mid, info in self.models.all().items():
            if info.get('name') == txt:
                self.engine.config.set('default_model', mid)
                break
    
    def _refresh_models(self, *a):
        self.status.text = "Loading models..."
        def up():
            ok = self.models.update_from_api()
            Clock.schedule_once(lambda dt: self._models_done(ok))
        threading.Thread(target=up).start()
    
    def _models_done(self, ok):
        if ok:
            self.model_spinner.values = [v.get('name', k) for k, v in self.models.all().items()]
            self.status.text = f"{len(self.models.all())} models"
        else:
            self.status.text = "Failed - check API key"
    
    def _new(self, *a):
        self.engine.new_conv()
        self.refresh()
        self.status.text = "New chat"
    
    def _attach(self, *a):
        """Open Kivy FileChooser popup"""
        def on_file_selected(path):
            if path:
                self.pending_files.append(path)
                self._update_att_bar()
                self.status.text = f"Attached: {os.path.basename(path)}"
                LOGBUF.add(f"File attached: {path}")
            else:
                self.status.text = "No file selected"
        
        FilePickerPopup(
            callback=on_file_selected,
            title="Wybierz plik do załączenia",
            filters=['*']
        ).open()
    
    def _update_att_bar(self):
        if self.pending_files:
            names = [os.path.basename(f) for f in self.pending_files]
            self.att_label.text = f"📎 {', '.join(names)}"
            self.att_bar.opacity = 1
            self.att_bar.height = dp(36)
        else:
            self.att_label.text = ""
            self.att_bar.opacity = 0
            self.att_bar.height = 0
    
    def _clear_files(self, *a):
        self.pending_files.clear()
        self._update_att_bar()
        self.status.text = "Attachments cleared"
    
    def _send(self, *a):
        if self.sending:
            return
        text = self.text_input.text.strip()
        if not text:
            return
        if not self.engine.secrets.get("api_key"):
            if self.on_settings:
                self.on_settings()
            return
        
        self.sending = True
        self.text_input.text = ""
        self.status.text = "Sending..."
        
        # Get attachments
        attachments = self.pending_files.copy()
        self.pending_files.clear()
        self._update_att_bar()
        
        def send_thread():
            try:
                self.engine.send(text, attachments=attachments)
                Clock.schedule_once(lambda dt: self._send_ok())
            except Exception as e:
                err = str(e)
                Clock.schedule_once(lambda dt: self._send_err(err))
        
        threading.Thread(target=send_thread).start()
    
    def _send_ok(self):
        self.sending = False
        self.refresh()
        self.status.text = "Ready"
    
    def _send_err(self, err):
        self.sending = False
        self.status.text = f"Error: {err[:30]}"
        LOGBUF.add(f"Send error: {err}")
    
    def refresh(self):
        self.msg_layout.clear_widgets()
        if not self.engine.conv or not self.engine.db:
            return
        for msg in self.engine.db.get_msgs(self.engine.conv['id'], True):
            self.msg_layout.add_widget(MsgBubble(
                msg, 
                self.models,
                lambda mid: (self.engine.db.set_msg_status(mid, 'excluded'), self.refresh()),
                lambda mid: (self.engine.db.set_msg_status(mid, 'active'), self.refresh())
            ))
        Clock.schedule_once(lambda dt: setattr(self.scroll, 'scroll_y', 0))
