"""
ChatADHD v0.07.01 - Conversation Panel

Sliding panel listing all conversations with import and clear options.
"""
import logging
from typing import Callable, Optional

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.uix.scrollview import ScrollView
from kivy.metrics import dp, sp

from gui.base import C, RBtn, Card, Panel, show_toast

log = logging.getLogger(__name__)


class ConvPanel(Panel):
    """Conversation list with import/clear."""

    def __init__(self, engine, on_select: Optional[Callable] = None, **kw):
        super().__init__(**kw)
        self.engine = engine
        self.on_select = on_select

        hdr = BoxLayout(size_hint_y=None, height=dp(34))
        hdr.add_widget(Label(text="Conversations", font_size=sp(11),
                             color=C["text"], bold=True))
        hdr.add_widget(RBtn(text="X", size_hint_x=None, width=dp(34),
                            bg=C["card"], on_press=lambda *_: self.close()))
        self.add_widget(hdr)

        # Import / Clear row
        action_row = BoxLayout(size_hint_y=None, height=dp(30),
                               spacing=dp(2), padding=dp(2))
        action_row.add_widget(RBtn(text="Import", bg=C["accent"], font_size=sp(9),
                                   on_press=self._open_import))
        action_row.add_widget(RBtn(text="Clear", bg=C["err"], font_size=sp(9),
                                   on_press=self._confirm_clear))
        self.add_widget(action_row)

        self.scroll = ScrollView()
        self.lst = BoxLayout(orientation="vertical", size_hint_y=None, spacing=dp(2))
        self.lst.bind(minimum_height=self.lst.setter("height"))
        self.scroll.add_widget(self.lst)
        self.add_widget(self.scroll)

    def refresh(self):
        self.lst.clear_widgets()
        if not self.engine.db:
            return
        for c in self.engine.db.list_convs():
            card = Card(size_hint_y=None, height=dp(40))
            card.add_widget(Label(text=c["title"][:22], font_size=sp(9),
                                  color=C["text"], halign="left"))
            card.conv = c
            card.bind(on_touch_down=lambda w, t, cv=c:
                      self._select(cv) if w.collide_point(*t.pos) else None)
            self.lst.add_widget(card)

    def _select(self, conv):
        self.engine.load_conv(conv["id"])
        self.close()
        if self.on_select:
            self.on_select()

    def _open_import(self, *_):
        # Lazy import to avoid circular dependency.
        from gui.import_panel import ImportConversationPopup
        ImportConversationPopup(
            self.engine.db, self.engine,
            on_import=self._on_import_done,
        ).open()

    def _on_import_done(self):
        self.refresh()
        if self.on_select:
            self.on_select()

    def _confirm_clear(self, *_):
        content = BoxLayout(orientation="vertical", padding=dp(10))
        content.add_widget(Label(text="Delete ALL conversations?", color=C["text"]))
        btns = BoxLayout(size_hint_y=None, height=dp(40), spacing=dp(4))

        def do_clear(*_):
            for conv in self.engine.db.list_convs():
                self.engine.db.delete_conv(conv["id"])
            self.refresh()
            popup.dismiss()
            show_toast("Cleared all conversations")

        btns.add_widget(RBtn(text="Cancel", bg=C["card"],
                             on_press=lambda *_: popup.dismiss()))
        btns.add_widget(RBtn(text="DELETE ALL", bg=C["err"], on_press=do_clear))
        content.add_widget(btns)
        popup = Popup(title="Confirm", content=content, size_hint=(0.8, 0.3))
        popup.open()
