"""
ChatADHD v0.07.00 - Voice Input Popup

Records audio via subprocess, sends to ASR provider (Groq/Google),
returns transcription to the caller.
"""
import logging

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.metrics import dp, sp

from gui.base import C, RBtn, show_toast

log = logging.getLogger(__name__)


class VoiceInputPopup(Popup):
    """Voice recording and transcription."""

    def __init__(self, engine, on_result=None, **kw):
        self.engine = engine
        self.on_result = on_result

        content = BoxLayout(orientation="vertical", padding=dp(8), spacing=dp(4))
        content.add_widget(Label(text="Voice Input", color=C["text"],
                                 font_size=sp(12), size_hint_y=None, height=dp(30)))

        self.status = Label(text="Press Record to start", color=C["dim"],
                            font_size=sp(10))
        content.add_widget(self.status)

        btns = BoxLayout(size_hint_y=None, height=dp(40), spacing=dp(4))
        btns.add_widget(RBtn(text="Record", bg=C["err"], font_size=sp(11),
                             on_press=self._record))
        btns.add_widget(RBtn(text="Close", bg=C["card"], font_size=sp(11),
                             on_press=lambda *_: self.dismiss()))
        content.add_widget(btns)

        super().__init__(title="Voice Input", content=content,
                         size_hint=(0.8, 0.4), **kw)

    def _record(self, *_):
        # Placeholder — full implementation uses subprocess + providers.py
        self.status.text = "Recording... (not yet implemented)"
        show_toast("Voice recording: requires Groq API key in Settings")
