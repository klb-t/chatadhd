"""
ChatADHD v0.07.01 - GUI Base Components

Shared widgets, theme engine, colour constants, and utility functions
used by every other GUI module.  Import from here, not from Kivy directly
for styled widgets.
"""
import logging
import re
from datetime import datetime

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.button import Button
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.uix.scrollview import ScrollView
from kivy.uix.textinput import TextInput
from kivy.core.window import Window
from kivy.graphics import Color, Rectangle, RoundedRectangle
from kivy.metrics import dp, sp
from kivy.clock import Clock

log = logging.getLogger(__name__)

# ═══════════════════════════════════════════════════════════════════
# THEMES
# ═══════════════════════════════════════════════════════════════════

THEMES: dict[str, dict[str, tuple]] = {
    "dark": {
        "bg":       (0.06, 0.06, 0.08, 1),
        "card":     (0.14, 0.14, 0.18, 1),
        "input_bg": (0.10, 0.10, 0.13, 1),
        "text":     (0.95, 0.95, 0.95, 1),
        "dim":      (0.55, 0.55, 0.60, 1),
        "accent":   (0.30, 0.55, 0.95, 1),
        "user":     (0.15, 0.22, 0.30, 1),
        "ai":       (0.12, 0.18, 0.24, 1),
        "ok":       (0.20, 0.55, 0.30, 1),
        "err":      (0.70, 0.25, 0.25, 1),
        "warn":     (0.80, 0.55, 0.15, 1),
        "artifact": (0.20, 0.25, 0.35, 1),
    },
    "amoled": {
        "bg":       (0.0,  0.0,  0.0,  1),
        "card":     (0.08, 0.08, 0.10, 1),
        "input_bg": (0.05, 0.05, 0.07, 1),
        "text":     (1.0,  1.0,  1.0,  1),
        "dim":      (0.50, 0.50, 0.55, 1),
        "accent":   (0.35, 0.60, 1.0,  1),
        "user":     (0.08, 0.12, 0.18, 1),
        "ai":       (0.05, 0.08, 0.12, 1),
        "ok":       (0.15, 0.60, 0.25, 1),
        "err":      (0.80, 0.20, 0.20, 1),
        "warn":     (0.90, 0.60, 0.10, 1),
        "artifact": (0.12, 0.15, 0.22, 1),
    },
}

# Active colour palette — mutated in place by ``set_theme()``.
C: dict[str, tuple] = dict(THEMES["dark"])


def set_theme(name: str) -> None:
    """Switch active colour palette.  Existing widgets keep old colours."""
    if name not in THEMES:
        log.warning("Unknown theme: %s", name)
        return
    C.clear()
    C.update(THEMES[name])


# ═══════════════════════════════════════════════════════════════════
# LOG BUFFER
# ═══════════════════════════════════════════════════════════════════

class LogBuffer:
    """Circular buffer for in-app log viewing."""

    def __init__(self, maxlen: int = 500) -> None:
        self._lines: list[str] = []
        self._maxlen = maxlen

    def add(self, msg: str) -> None:
        self._lines.append(f"{datetime.now():%H:%M:%S} {msg}")
        if len(self._lines) > self._maxlen:
            self._lines = self._lines[-self._maxlen:]

    def get(self) -> str:
        return "\n".join(self._lines)

    def clear(self) -> None:
        self._lines.clear()


LOGBUF = LogBuffer()

# Hook Python logging into LOGBUF.
class _BufHandler(logging.Handler):
    def emit(self, record: logging.LogRecord) -> None:
        LOGBUF.add(self.format(record))

_handler = _BufHandler()
_handler.setFormatter(logging.Formatter("%(name)s: %(message)s"))
logging.getLogger("chatadhd").addHandler(_handler)


# ═══════════════════════════════════════════════════════════════════
# STYLED WIDGETS
# ═══════════════════════════════════════════════════════════════════

class RBtn(Button):
    """Rounded button with themed colours."""

    def __init__(self, bg: tuple | None = None, **kw) -> None:
        super().__init__(**kw)
        self.background_color = bg or C["card"]
        self.background_normal = ""
        self.color = C["text"]
        if "font_size" not in kw:
            self.font_size = sp(10)


class Card(BoxLayout):
    """Box with rounded-rectangle background."""

    def __init__(self, bg: tuple | None = None, **kw) -> None:
        super().__init__(**kw)
        self.padding = dp(6)
        with self.canvas.before:
            Color(*(bg or C["card"]))
            self._rect = RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(6)])
        self.bind(
            pos=lambda *_: setattr(self._rect, "pos", self.pos),
            size=lambda *_: setattr(self._rect, "size", self.size),
        )


class DarkInput(TextInput):
    """Text input with dark theme defaults."""

    def __init__(self, **kw) -> None:
        kw.setdefault("background_color", C["input_bg"])
        kw.setdefault("foreground_color", C["text"])
        kw.setdefault("cursor_color", C["accent"])
        kw.setdefault("hint_text_color", C["dim"])
        super().__init__(**kw)


class Panel(BoxLayout):
    """Slide-out side panel (left or right)."""

    def __init__(self, **kw) -> None:
        super().__init__(orientation="vertical", **kw)
        self.panel_width = dp(260)
        self.size_hint_x = None
        self.width = 0
        self._visible = False

        with self.canvas.before:
            Color(*C["bg"])
            self._bg = Rectangle(pos=self.pos, size=self.size)
        self.bind(
            pos=lambda *_: setattr(self._bg, "pos", self.pos),
            size=lambda *_: setattr(self._bg, "size", self.size),
        )

    def toggle(self) -> None:
        self._visible = not self._visible
        self.width = self.panel_width if self._visible else 0

    def open(self) -> None:
        self._visible = True
        self.width = self.panel_width

    def close(self) -> None:
        self._visible = False
        self.width = 0

    def refresh(self) -> None:
        """Override in subclasses to rebuild content."""


# ═══════════════════════════════════════════════════════════════════
# UTILITIES
# ═══════════════════════════════════════════════════════════════════

def show_toast(msg: str, duration: float = 2.0) -> None:
    """Show a transient notification at the bottom of the screen."""
    toast = Label(
        text=msg,
        size_hint=(None, None),
        size=(dp(220), dp(36)),
        pos_hint={"center_x": 0.5, "y": 0.05},
        color=C["text"],
        font_size=sp(11),
    )
    with toast.canvas.before:
        Color(*C["ok"])
        toast._bg = RoundedRectangle(pos=toast.pos, size=toast.size, radius=[dp(8)])
    toast.bind(pos=lambda *_: setattr(toast._bg, "pos", toast.pos))
    Window.add_widget(toast)
    Clock.schedule_once(lambda dt: Window.remove_widget(toast), duration)


def detect_artifacts(text: str) -> list[dict]:
    """Detect code blocks and other artifacts in message text."""
    artifacts: list[dict] = []
    code_pattern = re.compile(r"```(\w*)\n(.*?)```", re.DOTALL)
    for m in code_pattern.finditer(text):
        artifacts.append({
            "type": "code",
            "lang": m.group(1) or "code",
            "content": m.group(2),
            "start": m.start(),
            "end": m.end(),
        })
    return artifacts
