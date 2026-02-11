"""
ChatADHD v0.07.01 - Import Conversation Popup

Wraps engine.importer.ConversationImporter in a file-picker UI.
"""
import logging
import os

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.metrics import dp, sp

from gui.base import C, RBtn, DarkInput, LOGBUF, show_toast
from gui.dialogs import FilePickerPopup

log = logging.getLogger(__name__)

_SUPPORTED = {
    ".db": "SQLite Database",
    ".json": "JSON (OpenAI / Anthropic)",
    ".html": "HTML export",
    ".htm": "HTML export",
    ".mht": "MHT archive",
    ".mhtml": "MHT archive",
    ".md": "Markdown",
    ".txt": "Plain text",
    ".png": "Screenshot (OCR)",
    ".jpg": "Screenshot (OCR)",
}


class ImportConversationPopup(Popup):
    """Import conversations from various file formats."""

    def __init__(self, db, engine, on_import=None, **kw):
        self.db = db
        self.engine = engine
        self.on_import = on_import

        content = BoxLayout(orientation="vertical", padding=dp(8), spacing=dp(4))
        content.add_widget(Label(
            text="Import conversations from file",
            color=C["text"], font_size=sp(10), size_hint_y=None, height=dp(24),
        ))

        # Supported formats
        fmt_text = "Supported: " + ", ".join(sorted(set(_SUPPORTED.values())))
        content.add_widget(Label(
            text=fmt_text, color=C["dim"], font_size=sp(8),
            size_hint_y=None, height=dp(20),
        ))

        # Title override
        content.add_widget(Label(text="Title (optional):", color=C["text"],
                                 size_hint_y=None, height=dp(16), font_size=sp(9)))
        self.title_input = DarkInput(hint_text="Auto-detect",
                                     size_hint_y=None, height=dp(30))
        content.add_widget(self.title_input)

        # Status
        self.status = Label(text="", color=C["dim"], font_size=sp(9),
                            size_hint_y=None, height=dp(20))
        content.add_widget(self.status)

        # Buttons
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        btns.add_widget(RBtn(text="Pick File", bg=C["accent"],
                             on_press=self._pick))
        btns.add_widget(RBtn(text="Close", bg=C["card"],
                             on_press=lambda *_: self.dismiss()))
        content.add_widget(btns)

        super().__init__(title="Import Conversation", content=content,
                         size_hint=(0.95, 0.55), **kw)

    def _pick(self, *_):
        FilePickerPopup(self._on_file, "Select file to import").open()

    def _on_file(self, path):
        if not path or not os.path.isfile(path):
            self.status.text = "No file selected"
            return

        self.status.text = f"Importing {os.path.basename(path)}..."

        try:
            from engine.importer import ConversationImporter
            importer = ConversationImporter(self.db, self.engine)
            title = self.title_input.text.strip() or None
            results = importer.import_file(path, title)

            count = len(results) if isinstance(results, list) else 1
            self.status.text = f"Imported {count} conversation(s)"
            show_toast(f"Imported {count} conversation(s)")

            if self.on_import:
                self.on_import()

        except Exception as e:
            log.exception("Import failed")
            self.status.text = f"Error: {str(e)[:40]}"
            LOGBUF.add(f"Import error: {e}")
