"""GUI panels and popups"""
import threading
from pathlib import Path
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.scrollview import ScrollView
from kivy.uix.textinput import TextInput
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.uix.spinner import Spinner
from kivy.uix.checkbox import CheckBox
from kivy.core.window import Window
from kivy.metrics import dp, sp
from kivy.clock import Clock
from .colors import C
from .widgets import RBtn, Card, Panel

class LogBuffer:
    def __init__(self, mx=500):
        self.lines = []
        self.mx = mx
    def add(self, msg):
        from datetime import datetime
        self.lines.append(f"{datetime.now():%H:%M:%S} {msg}")
        self.lines = self.lines[-self.mx:]
    def get(self): return "\n".join(self.lines)
    def clear(self): self.lines = []

LOGBUF = LogBuffer()

class LogViewer(Popup):
    def __init__(self, **kw):
        content = BoxLayout(orientation='vertical', padding=dp(8), spacing=dp(8))
        scroll = ScrollView()
        self.log_label = Label(text=LOGBUF.get(), font_size=sp(10), color=C['text'],
                               text_size=(Window.width - dp(40), None), halign='left', valign='top', size_hint_y=None)
        self.log_label.bind(texture_size=lambda *x: setattr(self.log_label, 'height', self.log_label.texture_size[1]))
        scroll.add_widget(self.log_label)
        content.add_widget(scroll)
        btns = BoxLayout(size_hint_y=None, height=dp(40), spacing=dp(8))
        for txt, clr, fn in [("Refresh", C['accent'], lambda *a: setattr(self.log_label, 'text', LOGBUF.get())), ("Close", C['card'], lambda *a: self.dismiss())]:
            b = RBtn(text=txt, bg=clr); b.bind(on_press=fn); btns.add_widget(b)
        content.add_widget(btns)
        super().__init__(title="Log", content=content, size_hint=(0.95, 0.85), **kw)

class SettingsPopup(Popup):
    def __init__(self, config, secrets, on_save=None, **kw):
        self.config, self.secrets, self.on_save = config, secrets, on_save
        content = BoxLayout(orientation='vertical', padding=dp(12), spacing=dp(10))
        content.add_widget(Label(text="API Key:", color=C['text'], size_hint_y=None, height=dp(25)))
        self.key_input = TextInput(text=secrets.get("api_key", ""), hint_text="sk-or-v1-...", multiline=False, password=True, size_hint_y=None, height=dp(40), background_color=C['card'], foreground_color=C['text'])
        content.add_widget(self.key_input)
        content.add_widget(Label(text="Base URL:", color=C['text'], size_hint_y=None, height=dp(25)))
        self.url_input = TextInput(text=config.get("base_url", ""), multiline=False, size_hint_y=None, height=dp(40), background_color=C['card'], foreground_color=C['text'])
        content.add_widget(self.url_input)
        for label, attr, key in [("Temperature:", "temp_input", "temperature"), ("Max Tokens:", "tok_input", "max_tokens")]:
            row = BoxLayout(size_hint_y=None, height=dp(40))
            row.add_widget(Label(text=label, color=C['text'], size_hint_x=0.4))
            inp = TextInput(text=str(config.get(key, "")), multiline=False, size_hint_x=0.6, background_color=C['card'], foreground_color=C['text'])
            setattr(self, attr, inp); row.add_widget(inp); content.add_widget(row)
        btns = BoxLayout(size_hint_y=None, height=dp(45), spacing=dp(10))
        save_btn = RBtn(text="Save", bg=C['accent']); save_btn.bind(on_press=self._save); btns.add_widget(save_btn)
        cancel_btn = RBtn(text="Cancel", bg=C['card']); cancel_btn.bind(on_press=lambda *a: self.dismiss()); btns.add_widget(cancel_btn)
        content.add_widget(btns)
        super().__init__(title="Settings", content=content, size_hint=(0.9, 0.6), **kw)
    
    def _save(self, *a):
        if self.key_input.text.strip(): self.secrets.set("api_key", self.key_input.text.strip())
        self.config.set("base_url", self.url_input.text.strip())
        try: self.config.set("temperature", float(self.temp_input.text))
        except: pass
        try: self.config.set("max_tokens", int(self.tok_input.text))
        except: pass
        self.config.save()
        if self.on_save: self.on_save()
        self.dismiss()

class MsgBubble(Card):
    def __init__(self, msg, models, on_exclude=None, on_include=None, **kw):
        bg = C['user'] if msg['role'] == 'user' else C['ai']
        super().__init__(orientation='vertical', size_hint_y=None, bg=bg, **kw)
        header = BoxLayout(size_hint_y=None, height=dp(22))
        role_name = "You" if msg['role'] == 'user' else models.name(msg.get('model'))
        excluded = msg.get('status') == 'excluded'
        opacity = 0.5 if excluded else 1.0
        header.add_widget(Label(text=role_name, font_size=sp(11), color=C['dim'], halign='left', size_hint_x=0.7, opacity=opacity))
        btn_text = "Show" if excluded else "Hide"
        btn = RBtn(text=btn_text, font_size=sp(10), size_hint_x=0.3, bg=C['card'])
        if on_exclude and on_include:
            btn.bind(on_press=lambda *a: (on_include(msg['id']) if excluded else on_exclude(msg['id'])))
        header.add_widget(btn)
        self.add_widget(header)
        content = Label(text=msg['text'], font_size=sp(13), color=C['text'], text_size=(Window.width - dp(50), None), halign='left', valign='top', size_hint_y=None, opacity=opacity)
        content.bind(texture_size=lambda *x: setattr(content, 'height', content.texture_size[1] + dp(6)))
        self.add_widget(content)
        self.bind(minimum_height=self.setter('height'))

class MemoryNodeWidget(BoxLayout):
    def __init__(self, node, memory, on_refresh=None, **kw):
        super().__init__(orientation='horizontal', size_hint_y=None, height=dp(36), **kw)
        self.node, self.memory, self.on_refresh = node, memory, on_refresh
        if node.depth > 0:
            self.add_widget(Label(size_hint_x=None, width=dp(16) * node.depth))
        cb = CheckBox(active=node.active, size_hint_x=None, width=dp(30))
        cb.bind(active=self._toggle)
        self.add_widget(cb)
        icons = {'folder': '📁', 'file': '📎', 'audio': '🎵', 'image': '🖼️', 'text': '•'}
        self.add_widget(Label(text=icons.get(node.node_type, '•'), size_hint_x=None, width=dp(24), font_size=sp(14), color=C['text']))
        text_btn = RBtn(text=node.content[:35] + ('...' if len(node.content) > 35 else ''), bg=C['card'], font_size=sp(12), halign='left')
        text_btn.bind(on_press=self._edit)
        self.add_widget(text_btn)
        children = memory.get_children(node.id)
        if children or node.links:
            info = []
            if children: info.append(f"{len(children)}↓")
            if node.links: info.append(f"{len(node.links)}→")
            self.add_widget(Label(text=" ".join(info), size_hint_x=None, width=dp(50), font_size=sp(10), color=C['dim']))
    
    def _toggle(self, cb, value):
        self.memory.update_node(self.node.id, active=value)
    
    def _edit(self, *a):
        content = BoxLayout(orientation='vertical', padding=dp(12), spacing=dp(8))
        text_input = TextInput(text=self.node.content, multiline=True, size_hint_y=0.3, background_color=C['card'], foreground_color=C['text'])
        content.add_widget(text_input)
        type_row = BoxLayout(size_hint_y=None, height=dp(40))
        type_row.add_widget(Label(text="Type:", color=C['text'], size_hint_x=0.3))
        type_spin = Spinner(text=self.node.node_type, values=['folder', 'text', 'file', 'audio', 'image'], size_hint_x=0.7, background_color=C['card'])
        type_row.add_widget(type_spin)
        content.add_widget(type_row)
        if self.node.links:
            content.add_widget(Label(text=f"Links: {', '.join(list(self.node.links)[:5])}", font_size=sp(10), color=C['dim'], size_hint_y=None, height=dp(25)))
        btns = BoxLayout(size_hint_y=None, height=dp(40), spacing=dp(8))
        add_btn = RBtn(text="+Child", bg=C['ok'], font_size=sp(11))
        add_btn.bind(on_press=lambda *a: self._add_child(popup))
        btns.add_widget(add_btn)
        def save(*a):
            self.memory.update_node(self.node.id, content=text_input.text, node_type=type_spin.text)
            popup.dismiss()
            if self.on_refresh: self.on_refresh()
        save_btn = RBtn(text="Save", bg=C['accent']); save_btn.bind(on_press=save); btns.add_widget(save_btn)
        def delete(*a):
            self.memory.delete_node(self.node.id, True)
            popup.dismiss()
            if self.on_refresh: self.on_refresh()
        del_btn = RBtn(text="Delete", bg=C['err']); del_btn.bind(on_press=delete); btns.add_widget(del_btn)
        content.add_widget(btns)
        popup = Popup(title=f"Edit: {self.node.content[:25]}", content=content, size_hint=(0.95, 0.6))
        popup.open()
    
    def _add_child(self, parent_popup):
        content = BoxLayout(orientation='vertical', padding=dp(12), spacing=dp(8))
        text_input = TextInput(hint_text="New item...", multiline=True, size_hint_y=0.4, background_color=C['card'], foreground_color=C['text'])
        content.add_widget(text_input)
        type_spin = Spinner(text='text', values=['folder', 'text', 'file'], size_hint_y=None, height=dp(40), background_color=C['card'])
        content.add_widget(type_spin)
        def create(*a):
            if text_input.text.strip():
                self.memory.add_node(text_input.text.strip(), self.node.id, type_spin.text)
                popup.dismiss(); parent_popup.dismiss()
                if self.on_refresh: self.on_refresh()
        create_btn = RBtn(text="Create", bg=C['accent'], size_hint_y=None, height=dp(40)); create_btn.bind(on_press=create)
        content.add_widget(create_btn)
        popup = Popup(title="Add Child", content=content, size_hint=(0.9, 0.45))
        popup.open()

class MemoryPanel(Panel):
    def __init__(self, memory, **kw):
        super().__init__(**kw)
        self.memory = memory
        self.panel_width = dp(320)
        self._build()
    
    def _build(self):
        header = BoxLayout(size_hint_y=None, height=dp(40))
        header.add_widget(Label(text="Memory", font_size=sp(16), color=C['text'], bold=True))
        close_btn = RBtn(text="✕", size_hint_x=None, width=dp(40), bg=C['card'])
        close_btn.bind(on_press=lambda *a: self.close())
        header.add_widget(close_btn)
        self.add_widget(header)
        add_btn = RBtn(text="+ Add Root", size_hint_y=None, height=dp(36), bg=C['accent'])
        add_btn.bind(on_press=self._add_root)
        self.add_widget(add_btn)
        self.scroll = ScrollView()
        self.tree_layout = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(4))
        self.tree_layout.bind(minimum_height=self.tree_layout.setter('height'))
        self.scroll.add_widget(self.tree_layout)
        self.add_widget(self.scroll)
    
    def refresh(self):
        self.tree_layout.clear_widgets()
        self._build_tree(None)
    
    def _build_tree(self, parent_id):
        for node in self.memory.get_children(parent_id):
            self.tree_layout.add_widget(MemoryNodeWidget(node, self.memory, self.refresh))
            self._build_tree(node.id)
    
    def _add_root(self, *a):
        content = BoxLayout(orientation='vertical', padding=dp(12), spacing=dp(8))
        text_input = TextInput(hint_text="Item text...", multiline=True, size_hint_y=0.4, background_color=C['card'], foreground_color=C['text'])
        content.add_widget(text_input)
        type_spin = Spinner(text='folder', values=['folder', 'text', 'file'], size_hint_y=None, height=dp(40), background_color=C['card'])
        content.add_widget(type_spin)
        def create(*a):
            if text_input.text.strip():
                self.memory.add_node(text_input.text.strip(), None, type_spin.text)
                popup.dismiss(); self.refresh()
        create_btn = RBtn(text="Create", bg=C['accent'], size_hint_y=None, height=dp(40)); create_btn.bind(on_press=create)
        content.add_widget(create_btn)
        popup = Popup(title="Add Root Item", content=content, size_hint=(0.9, 0.45))
        popup.open()

class ConvPanel(Panel):
    def __init__(self, engine, on_select=None, **kw):
        super().__init__(**kw)
        self.engine, self.on_select = engine, on_select
        self._build()
    
    def _build(self):
        header = BoxLayout(size_hint_y=None, height=dp(40))
        header.add_widget(Label(text="Chats", font_size=sp(16), color=C['text'], bold=True))
        close_btn = RBtn(text="✕", size_hint_x=None, width=dp(40), bg=C['card'])
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
        if not self.engine.db: return
        for conv in self.engine.db.list_convs():
            card = Card(orientation='vertical', size_hint_y=None, height=dp(55))
            card.add_widget(Label(text=conv['title'][:28], font_size=sp(12), color=C['text'], halign='left', size_hint_y=None, height=dp(25)))
            card.add_widget(Label(text=conv['updated'][:16], font_size=sp(10), color=C['dim'], size_hint_y=None, height=dp(20)))
            card.bind(on_touch_down=lambda w, t, c=conv: self._select(c) if w.collide_point(*t.pos) else None)
            self.conv_list.add_widget(card)
    
    def _select(self, conv):
        self.engine.load_conv(conv['id'])
        if self.on_select: self.on_select()
        self.close()

class ChatPanel(BoxLayout):
    def __init__(self, engine, models, on_settings=None, **kw):
        super().__init__(orientation='vertical', **kw)
        self.engine, self.models, self.on_settings = engine, models, on_settings
        self.sending = False
        self.pending_atts = []
        self._build()
    
    def _build(self):
        top = BoxLayout(size_hint_y=None, height=dp(46), padding=dp(4), spacing=dp(4))
        all_models = self.models.all()
        names = [v.get('name', k) for k, v in all_models.items()] or ['(no models)']
        default_model = self.engine.config.get('default_model') if hasattr(self.engine.config, 'get') else ''
        self.model_spinner = Spinner(text=self.models.name(default_model), values=names, size_hint_x=0.4, background_color=C['card'])
        self.model_spinner.bind(text=self._model_changed)
        top.add_widget(self.model_spinner)
        for txt, clr, fn in [("⟳", C['card'], self._refresh_models), ("New", C['accent'], self._new), ("⚙", C['card'], lambda *a: self.on_settings() if self.on_settings else None), ("Log", C['warn'], lambda *a: LogViewer().open())]:
            btn = RBtn(text=txt, size_hint_x=0.15, bg=clr); btn.bind(on_press=fn); top.add_widget(btn)
        self.add_widget(top)
        self.scroll = ScrollView()
        self.msg_layout = BoxLayout(orientation='vertical', size_hint_y=None, spacing=dp(8), padding=dp(8))
        self.msg_layout.bind(minimum_height=self.msg_layout.setter('height'))
        self.scroll.add_widget(self.msg_layout)
        self.add_widget(self.scroll)
        inp_card = Card(size_hint_y=None, height=dp(95), orientation='horizontal', spacing=dp(8))
        inp_left = BoxLayout(orientation='vertical', size_hint_x=0.82)
        self.text_input = TextInput(hint_text="Message...", multiline=True, background_color=C['card'], foreground_color=C['text'], font_size=sp(13))
        inp_left.add_widget(self.text_input)
        att_btn = RBtn(text="📎 Attach", size_hint_y=None, height=dp(26), bg=C['card'], font_size=sp(10))
        att_btn.bind(on_press=self._attach)
        inp_left.add_widget(att_btn)
        inp_card.add_widget(inp_left)
        send_btn = RBtn(text="Send", size_hint_x=0.18, bg=C['accent']); send_btn.bind(on_press=self._send)
        inp_card.add_widget(send_btn)
        self.add_widget(inp_card)
        self.status = Label(text="Ready", size_hint_y=None, height=dp(20), font_size=sp(10), color=C['dim'])
        self.add_widget(self.status)
    
    def _model_changed(self, sp, txt):
        for mid, info in self.models.all().items():
            if info.get('name') == txt:
                if hasattr(self.engine.config, 'set'): self.engine.config.set('default_model', mid)
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
        else: self.status.text = "Failed - check API key"
    
    def _new(self, *a):
        self.engine.new_conv()
        self.refresh()
        self.status.text = "New chat"
    
    def _attach(self, *a):
        try:
            from plyer import filechooser
            filechooser.open_file(on_selection=lambda s: (self.pending_atts.extend(s), setattr(self.status, 'text', f"{len(self.pending_atts)} attached")), multiple=True)
        except:
            from kivy.uix.filechooser import FileChooserListView
            content = BoxLayout(orientation='vertical', padding=dp(8))
            fc = FileChooserListView(path=str(Path.home()))
            content.add_widget(fc)
            btns = BoxLayout(size_hint_y=None, height=dp(40), spacing=dp(8))
            def sel(*a):
                if fc.selection: self.pending_atts.extend(fc.selection); self.status.text = f"{len(self.pending_atts)} attached"
                popup.dismiss()
            btns.add_widget(RBtn(text="Attach", bg=C['accent'], on_press=sel))
            btns.add_widget(RBtn(text="Cancel", bg=C['card'], on_press=lambda *a: popup.dismiss()))
            content.add_widget(btns)
            popup = Popup(title="Select File", content=content, size_hint=(0.95, 0.8))
            popup.open()
    
    def _send(self, *a):
        if self.sending: return
        text = self.text_input.text.strip()
        if not text: return
        secrets = self.engine.secrets
        if hasattr(secrets, 'get') and not secrets.get("api_key"):
            if self.on_settings: self.on_settings()
            return
        self.sending = True
        self.text_input.text = ""
        self.status.text = "Sending..."
        def send_t():
            try:
                self.engine.send(text)
                Clock.schedule_once(lambda dt: self._send_ok())
            except Exception as e:
                Clock.schedule_once(lambda dt: self._send_err(str(e)))
        threading.Thread(target=send_t).start()
    
    def _send_ok(self):
        self.sending = False
        self.refresh()
        self.status.text = "Ready"
    
    def _send_err(self, e):
        self.sending = False
        self.status.text = f"Error: {e[:35]}"
    
    def refresh(self):
        self.msg_layout.clear_widgets()
        if not self.engine.conv or not self.engine.db: return
        for msg in self.engine.db.get_msgs(self.engine.conv['id'], True):
            self.msg_layout.add_widget(MsgBubble(msg, self.models,
                lambda mid: (self.engine.db.set_msg_status(mid, 'excluded'), self.refresh()),
                lambda mid: (self.engine.db.set_msg_status(mid, 'active'), self.refresh())))
        Clock.schedule_once(lambda dt: setattr(self.scroll, 'scroll_y', 0))
