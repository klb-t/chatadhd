"""
ChatADHD v0.07.01 - Dialog Popups

Settings, QuickAPI, Theme, FilePicker, LogViewer, ModelSelector,
NodeEditor — all popups that overlay the main UI.
"""
import json
import logging
import os

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.uix.scrollview import ScrollView
from kivy.uix.slider import Slider
from kivy.uix.spinner import Spinner
from kivy.uix.filechooser import FileChooserListView
from kivy.core.clipboard import Clipboard
from kivy.metrics import dp, sp

from gui.base import C, RBtn, Card, DarkInput, LOGBUF, show_toast

log = logging.getLogger(__name__)


# ═══════════════════════════════════════════════════════════════════
# SETTINGS
# ═══════════════════════════════════════════════════════════════════

class SettingsPopup(Popup):
    """API keys and core configuration."""

    def __init__(self, config, secrets, on_save=None, **kw):
        self.config = config
        self.secrets = secrets
        self.on_save = on_save

        content = BoxLayout(orientation="vertical", padding=dp(6), spacing=dp(3))

        # API Key
        content.add_widget(Label(text="API Key:", color=C["text"],
                                 size_hint_y=None, height=dp(16), font_size=sp(9)))
        self.api_key = DarkInput(
            text=secrets.get("api_key", ""), password=True,
            size_hint_y=None, height=dp(32), font_size=sp(10),
        )
        content.add_widget(self.api_key)

        # Base URL
        content.add_widget(Label(text="Base URL:", color=C["text"],
                                 size_hint_y=None, height=dp(16), font_size=sp(9)))
        self.base_url = DarkInput(
            text=config.get("base_url", ""),
            size_hint_y=None, height=dp(32), font_size=sp(10),
        )
        content.add_widget(self.base_url)

        # GitHub Token
        content.add_widget(Label(text="GitHub Token:", color=C["text"],
                                 size_hint_y=None, height=dp(16), font_size=sp(9)))
        self.gh_token = DarkInput(
            text=secrets.get("github_token", ""), password=True,
            size_hint_y=None, height=dp(32), font_size=sp(10),
        )
        content.add_widget(self.gh_token)

        # Groq Key (for voice)
        content.add_widget(Label(text="Groq API Key (voice):", color=C["text"],
                                 size_hint_y=None, height=dp(16), font_size=sp(9)))
        self.groq_key = DarkInput(
            text=secrets.get("groq_api_key", ""), password=True,
            size_hint_y=None, height=dp(32), font_size=sp(10),
        )
        content.add_widget(self.groq_key)

        # System Prompt
        content.add_widget(Label(text="System Prompt:", color=C["text"],
                                 size_hint_y=None, height=dp(16), font_size=sp(9)))
        self.sys_prompt = DarkInput(
            text=config.get("system_prompt", ""),
            multiline=True, size_hint_y=0.25, font_size=sp(9),
        )
        content.add_widget(self.sys_prompt)

        # Semantic Model
        sem_row = BoxLayout(size_hint_y=None, height=dp(32), spacing=dp(4))
        sem_row.add_widget(Label(text="Semantic:", color=C["text"],
                                  size_hint_x=0.25, font_size=sp(9)))
        self.sem_model = DarkInput(
            text=config.get("semantic_model", "anthropic/claude-3.5-haiku"),
            font_size=sp(9),
        )
        sem_row.add_widget(self.sem_model)
        content.add_widget(sem_row)

        # Semantic analysis toggle
        sem_toggle = BoxLayout(size_hint_y=None, height=dp(28), spacing=dp(4))
        sem_toggle.add_widget(Label(text="LLM Analysis:", color=C["text"],
                                     size_hint_x=0.4, font_size=sp(9)))
        self._sem_enabled = config.get("semantic_analysis", True)
        self.sem_btn = RBtn(
            text="ON" if self._sem_enabled else "OFF",
            bg=C["ok"] if self._sem_enabled else C["card"],
            font_size=sp(9),
            on_press=self._toggle_semantic,
        )
        sem_toggle.add_widget(self.sem_btn)
        content.add_widget(sem_toggle)

        # Buttons
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Save", bg=C["accent"], on_press=self._save))
        btns.add_widget(RBtn(text="Cancel", bg=C["card"],
                             on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)

        super().__init__(title="Settings", content=content,
                         size_hint=(0.95, 0.7), **kw)

    def _toggle_semantic(self, *_):
        self._sem_enabled = not self._sem_enabled
        self.sem_btn.text = "ON" if self._sem_enabled else "OFF"
        self.sem_btn.background_color = C["ok"] if self._sem_enabled else C["card"]

    def _save(self, *_):
        self.secrets.set("api_key", self.api_key.text.strip())
        self.secrets.set("github_token", self.gh_token.text.strip())
        self.secrets.set("groq_api_key", self.groq_key.text.strip())
        self.secrets.save()

        self.config.set("base_url", self.base_url.text.strip())
        self.config.set("system_prompt", self.sys_prompt.text.strip())
        self.config.set("semantic_model", self.sem_model.text.strip())
        self.config.set("semantic_analysis", self._sem_enabled)
        self.config.save()

        show_toast("Settings saved")
        self.dismiss()
        if self.on_save:
            self.on_save()


# ═══════════════════════════════════════════════════════════════════
# QUICK API PANEL
# ═══════════════════════════════════════════════════════════════════

class QuickAPIPanel(Popup):
    """Fast access to model selection, presets, and temperature."""

    PRESETS = {
        "Creative":  {"temperature": 0.9, "max_tokens": 4096},
        "Balanced":  {"temperature": 0.7, "max_tokens": 4096},
        "Precise":   {"temperature": 0.3, "max_tokens": 4096},
        "Code":      {"temperature": 0.2, "max_tokens": 8192},
        "Long":      {"temperature": 0.7, "max_tokens": 16384},
    }

    FAVORITES = [
        "anthropic/claude-sonnet-4-20250514",
        "anthropic/claude-4.6-opus",
        "openai/gpt-5.2",
        "openai/gpt-5.3-codex",
        "google/gemini-2.5-pro",
        "deepseek/deepseek-r1",
    ]

    def __init__(self, config, models, on_change=None, **kw):
        self.config = config
        self.models = models
        self.on_change = on_change

        content = BoxLayout(orientation="vertical", padding=dp(8), spacing=dp(4))

        # Favourite models
        content.add_widget(Label(text="Quick Models:", color=C["text"],
                                 size_hint_y=None, height=dp(18), font_size=sp(10)))

        current = config.get("default_model", "")
        fav_grid = BoxLayout(size_hint_y=None, height=dp(70),
                             orientation="vertical", spacing=dp(2))
        row1, row2 = BoxLayout(spacing=dp(2)), BoxLayout(spacing=dp(2))
        for i, mid in enumerate(self.FAVORITES[:6]):
            short = mid.split("/")[-1][:10]
            is_current = mid == current
            btn = RBtn(text=short,
                       bg=C["accent"] if is_current else C["card"],
                       font_size=sp(8))
            btn.model_id = mid
            btn.bind(on_press=self._select_model)
            (row1 if i < 3 else row2).add_widget(btn)
        fav_grid.add_widget(row1)
        fav_grid.add_widget(row2)
        content.add_widget(fav_grid)

        # Presets
        content.add_widget(Label(text="Presets:", color=C["text"],
                                 size_hint_y=None, height=dp(18), font_size=sp(10)))
        preset_row = BoxLayout(size_hint_y=None, height=dp(32), spacing=dp(2))
        for name in self.PRESETS:
            btn = RBtn(text=name, bg=C["card"], font_size=sp(8))
            btn.preset_name = name
            btn.bind(on_press=self._apply_preset)
            preset_row.add_widget(btn)
        content.add_widget(preset_row)

        # Temperature slider
        temp_row = BoxLayout(size_hint_y=None, height=dp(34))
        temp_row.add_widget(Label(text="Temp:", color=C["text"],
                                  size_hint_x=0.2, font_size=sp(9)))
        self.temp_slider = Slider(min=0.0, max=1.5,
                                  value=config.get("temperature", 0.7),
                                  size_hint_x=0.6)
        temp_row.add_widget(self.temp_slider)
        self.temp_label = Label(
            text=f"{config.get('temperature', 0.7):.2f}",
            color=C["text"], size_hint_x=0.2, font_size=sp(9),
        )
        self.temp_slider.bind(value=self._on_temp)
        temp_row.add_widget(self.temp_label)
        content.add_widget(temp_row)

        # Max tokens slider
        tok_row = BoxLayout(size_hint_y=None, height=dp(34))
        tok_row.add_widget(Label(text="Tokens:", color=C["text"],
                                 size_hint_x=0.2, font_size=sp(9)))
        self.tok_slider = Slider(min=1000, max=32000,
                                 value=config.get("max_tokens", 4096),
                                 size_hint_x=0.6)
        tok_row.add_widget(self.tok_slider)
        self.tok_label = Label(
            text=f"{int(config.get('max_tokens', 4096)):,}",
            color=C["text"], size_hint_x=0.2, font_size=sp(9),
        )
        self.tok_slider.bind(value=self._on_tok)
        tok_row.add_widget(self.tok_label)
        content.add_widget(tok_row)

        # Buttons
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Apply", bg=C["accent"], on_press=self._apply))
        btns.add_widget(RBtn(text="Close", bg=C["card"],
                             on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)

        super().__init__(title="Quick API Settings", content=content,
                         size_hint=(0.95, 0.55), **kw)

    def _select_model(self, btn):
        self.config.set("default_model", btn.model_id)
        show_toast(f"Model: {btn.model_id.split('/')[-1]}")
        self._notify()

    def _apply_preset(self, btn):
        preset = self.PRESETS.get(btn.preset_name, {})
        for k, v in preset.items():
            self.config.set(k, v)
        self.temp_slider.value = preset.get("temperature", 0.7)
        self.tok_slider.value = preset.get("max_tokens", 4096)
        show_toast(f"Preset: {btn.preset_name}")
        self._notify()

    def _on_temp(self, _, val):
        self.temp_label.text = f"{val:.2f}"

    def _on_tok(self, _, val):
        self.tok_label.text = f"{int(val):,}"

    def _apply(self, *_):
        self.config.set("temperature", round(self.temp_slider.value, 2))
        self.config.set("max_tokens", int(self.tok_slider.value))
        self.config.save()
        show_toast("Settings applied")
        self._notify()
        self.dismiss()

    def _notify(self):
        if self.on_change:
            self.on_change()


# ═══════════════════════════════════════════════════════════════════
# THEME POPUP
# ═══════════════════════════════════════════════════════════════════

class ThemePopup(Popup):
    def __init__(self, config, on_change=None, **kw):
        self.config = config
        self.on_change = on_change
        content = BoxLayout(orientation="vertical", padding=dp(8), spacing=dp(4))
        for name in ("dark", "amoled"):
            btn = RBtn(text=name.capitalize(), bg=C["accent"], font_size=sp(11))
            btn.theme_name = name
            btn.bind(on_press=self._pick)
            content.add_widget(btn)
        super().__init__(title="Theme", content=content, size_hint=(0.6, 0.35), **kw)

    def _pick(self, btn):
        from gui.base import set_theme
        set_theme(btn.theme_name)
        self.config.set("theme", btn.theme_name)
        self.config.save()
        show_toast(f"Theme: {btn.theme_name}")
        self.dismiss()
        if self.on_change:
            self.on_change()


# ═══════════════════════════════════════════════════════════════════
# FILE PICKER
# ═══════════════════════════════════════════════════════════════════

class FilePickerPopup(Popup):
    """Kivy-based file browser (plyer doesn't work in Pydroid)."""

    def __init__(self, on_select, title="Select File", allow_dirs=False, **kw):
        self._on_select = on_select
        self._allow_dirs = allow_dirs

        content = BoxLayout(orientation="vertical", padding=dp(4), spacing=dp(2))

        start_path = "/storage/emulated/0" if os.path.exists("/storage/emulated/0") \
            else os.path.expanduser("~")

        self.chooser = FileChooserListView(
            path=start_path,
            dirselect=allow_dirs,
            filters=["*"] if allow_dirs else [],
        )
        content.add_widget(self.chooser)

        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Select", bg=C["accent"], on_press=self._select))
        btns.add_widget(RBtn(text="Cancel", bg=C["card"],
                             on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)

        super().__init__(title=title, content=content, size_hint=(0.95, 0.85), **kw)

    def _select(self, *_):
        sel = self.chooser.selection
        if sel:
            self._on_select(sel[0])
        elif self._allow_dirs:
            self._on_select(self.chooser.path)
        self.dismiss()


# ═══════════════════════════════════════════════════════════════════
# LOG VIEWER
# ═══════════════════════════════════════════════════════════════════

class LogViewer(Popup):
    def __init__(self, **kw):
        content = BoxLayout(orientation="vertical", padding=dp(4))
        txt = DarkInput(text=LOGBUF.get(), readonly=True, font_size=sp(8))
        content.add_widget(txt)
        btns = BoxLayout(size_hint_y=None, height=dp(34), spacing=dp(4))
        btns.add_widget(RBtn(text="Copy", bg=C["card"],
                             on_press=lambda *a: (Clipboard.copy(txt.text),
                                                  show_toast("Copied"))))
        btns.add_widget(RBtn(text="Clear", bg=C["err"],
                             on_press=lambda *a: (LOGBUF.clear(),
                                                  setattr(txt, "text", ""),
                                                  show_toast("Cleared"))))
        btns.add_widget(RBtn(text="Close", bg=C["card"],
                             on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)
        super().__init__(title="Log", content=content, size_hint=(0.95, 0.7), **kw)


# ═══════════════════════════════════════════════════════════════════
# MODEL SELECTOR
# ═══════════════════════════════════════════════════════════════════

class ModelSelectorPopup(Popup):
    """Full model picker grouped by provider."""

    def __init__(self, models, current_id, on_select, **kw):
        self._on_select = on_select
        content = BoxLayout(orientation="vertical", padding=dp(4))

        scroll = ScrollView()
        lst = BoxLayout(orientation="vertical", size_hint_y=None, spacing=dp(2))
        lst.bind(minimum_height=lst.setter("height"))

        grouped = models.grouped()
        for provider, provider_models in sorted(grouped.items()):
            lst.add_widget(Label(
                text=f"--- {provider} ---", color=C["dim"],
                size_hint_y=None, height=dp(22), font_size=sp(9),
            ))
            for m in provider_models:
                is_current = m["id"] == current_id
                btn = RBtn(
                    text=m["name"][:28],
                    bg=C["accent"] if is_current else C["card"],
                    size_hint_y=None, height=dp(30), font_size=sp(8),
                )
                btn.model_id = m["id"]
                btn.bind(on_press=self._pick)
                lst.add_widget(btn)

        scroll.add_widget(lst)
        content.add_widget(scroll)
        content.add_widget(RBtn(text="Close", bg=C["card"], size_hint_y=None,
                                height=dp(34), on_press=lambda *a: self.dismiss()))

        super().__init__(title="Select Model", content=content,
                         size_hint=(0.95, 0.8), **kw)

    def _pick(self, btn):
        self._on_select(btn.model_id)
        show_toast(f"Model: {btn.model_id.split('/')[-1]}")
        self.dismiss()


# ═══════════════════════════════════════════════════════════════════
# NODE EDITOR (for Graph)
# ═══════════════════════════════════════════════════════════════════

class NodeEditorPopup(Popup):
    """Edit properties of a graph node (weight, pin, content)."""

    def __init__(self, node, engine, memory, on_update=None, **kw):
        self.node = node
        self.engine = engine
        self.memory = memory
        self.on_update = on_update

        content = BoxLayout(orientation="vertical", padding=dp(8), spacing=dp(4))

        content.add_widget(Label(
            text=f"[{node.type}] {node.id[:10]}",
            font_size=sp(9), color=C["dim"],
            size_hint_y=None, height=dp(20),
        ))

        content.add_widget(Label(text="Content:", color=C["text"],
                                 size_hint_y=None, height=dp(16)))
        label_text = (node.data.get("text", node.full_label)
                      if hasattr(node, "full_label") else node.label)
        self.content_input = DarkInput(text=label_text, multiline=True, size_hint_y=0.4)
        content.add_widget(self.content_input)

        # Weight
        wr = BoxLayout(size_hint_y=None, height=dp(34))
        wr.add_widget(Label(text="Weight:", color=C["text"], size_hint_x=0.25))
        self.weight_slider = Slider(min=0.1, max=2.0, value=node.weight, size_hint_x=0.5)
        wr.add_widget(self.weight_slider)
        self.weight_label = Label(text=f"{node.weight:.1f}", color=C["text"], size_hint_x=0.25)
        self.weight_slider.bind(
            value=lambda _, v: setattr(self.weight_label, "text", f"{v:.1f}"))
        wr.add_widget(self.weight_label)
        content.add_widget(wr)

        # Pin
        pr = BoxLayout(size_hint_y=None, height=dp(30))
        pr.add_widget(Label(text="Pin:", color=C["text"], size_hint_x=0.4))
        self.pin_btn = RBtn(
            text="PINNED" if node.pinned else "FREE",
            bg=C["warn"] if node.pinned else C["card"],
            size_hint_x=0.6, on_press=self._toggle_pin,
        )
        pr.add_widget(self.pin_btn)
        content.add_widget(pr)

        # Buttons
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Save", bg=C["accent"], on_press=self._save))
        btns.add_widget(RBtn(text="Close", bg=C["card"],
                             on_press=lambda *a: self.dismiss()))
        content.add_widget(btns)

        super().__init__(title=f"Edit: {node.label[:18]}", content=content,
                         size_hint=(0.92, 0.55), **kw)

    def _toggle_pin(self, *_):
        self.node.pinned = not self.node.pinned
        self.pin_btn.text = "PINNED" if self.node.pinned else "FREE"
        self.pin_btn.background_color = C["warn"] if self.node.pinned else C["card"]

    def _save(self, *_):
        self.node.weight = self.weight_slider.value
        if self.node.type in ("user", "assistant") and self.engine:
            self.engine.db.update_msg(self.node.id, weight=self.node.weight)
        elif self.memory:
            self.memory.update_node(self.node.id, weight=self.node.weight)
        show_toast("Saved")
        self.dismiss()
        if self.on_update:
            self.on_update()
