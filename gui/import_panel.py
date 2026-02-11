"""
ChatADHD v0.07.03 - Import Conversation Popup

Threaded import with live progress display.
"""
import logging
import os
import threading

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.uix.progressbar import ProgressBar
from kivy.metrics import dp, sp
from kivy.clock import Clock

from gui.base import C, RBtn, DarkInput, LOGBUF, show_toast
from gui.dialogs import FilePickerPopup

log = logging.getLogger(__name__)


class ImportConversationPopup(Popup):
    """Import conversations with live progress."""

    def __init__(self, db, engine, on_import=None, **kw):
        self.db = db
        self.engine = engine
        self.on_import = on_import
        self._importing = False

        content = BoxLayout(orientation="vertical", padding=dp(8), spacing=dp(4))
        content.add_widget(Label(
            text="Import conversations",
            color=C["text"], font_size=sp(11), bold=True,
            size_hint_y=None, height=dp(24),
        ))

        fmt_text = "ZIP, JSON, JSONL, HTML, DB, MD, TXT"
        content.add_widget(Label(
            text=fmt_text, color=C["dim"], font_size=sp(8),
            size_hint_y=None, height=dp(18),
        ))

        # Title override
        row = BoxLayout(size_hint_y=None, height=dp(30), spacing=dp(4))
        row.add_widget(Label(text="Title:", color=C["text"],
                             size_hint_x=0.2, font_size=sp(9)))
        self.title_input = DarkInput(hint_text="Auto-detect",
                                     size_hint_y=None, height=dp(30))
        row.add_widget(self.title_input)
        content.add_widget(row)

        # Progress bar
        self.progress = ProgressBar(max=100, value=0,
                                     size_hint_y=None, height=dp(8))
        content.add_widget(self.progress)

        # Status
        self.status = Label(text="Ready", color=C["dim"], font_size=sp(9),
                            size_hint_y=None, height=dp(40),
                            halign="left", valign="top")
        self.status.bind(size=self.status.setter("text_size"))
        content.add_widget(self.status)

        # Buttons
        btns = BoxLayout(size_hint_y=None, height=dp(36), spacing=dp(4))
        self.pick_btn = RBtn(text="Pick File", bg=C["accent"],
                             on_press=self._pick)
        btns.add_widget(self.pick_btn)
        btns.add_widget(RBtn(text="Close", bg=C["card"],
                             on_press=lambda *_: self.dismiss()))
        content.add_widget(btns)

        super().__init__(title="Import", content=content,
                         size_hint=(0.95, 0.5), **kw)

    def _pick(self, *_):
        if self._importing:
            show_toast("Import in progress...")
            return
        FilePickerPopup(self._on_file, "Select file to import").open()

    def _on_file(self, path):
        if not path or not os.path.isfile(path):
            self.status.text = "No file selected"
            return

        self._importing = True
        self.pick_btn.text = "Importing..."
        self.pick_btn.disabled = True
        self.progress.value = 0
        self.status.text = f"Starting: {os.path.basename(path)}"

        t = threading.Thread(target=self._import_thread, args=(path,), daemon=True)
        t.start()

    def _import_thread(self, path):
        """Background import with UI updates via Clock."""
        count = [0]
        try:
            from engine.importer import ConversationImporter
            importer = ConversationImporter(self.db, self.engine)
            title = self.title_input.text.strip() or None

            # Hook into importer to get progress callbacks.
            original_import_msg_list = importer._import_message_list

            def patched_import(msgs, conv_id, source="import"):
                result = original_import_msg_list(msgs, conv_id, source)
                count[0] += 1
                Clock.schedule_once(lambda dt: self._tick(
                    count[0], f"Conv {count[0]}: {len(msgs)} msgs"
                ), 0)
                return result

            importer._import_message_list = patched_import

            results = importer.import_file(path, title)
            final = len(results) if isinstance(results, list) else 1
            Clock.schedule_once(lambda dt: self._finish(final), 0)

        except Exception as e:
            log.exception("Import failed")
            Clock.schedule_once(lambda dt: self._error(str(e)[:60]), 0)

    def _tick(self, n, msg):
        self.status.text = f"[{n}] {msg}"
        self.progress.value = (n * 5) % 100

    def _finish(self, count):
        self._importing = False
        self.pick_btn.text = "Pick File"
        self.pick_btn.disabled = False
        self.progress.value = 100
        self.status.text = f"Done: {count} conversation(s)"
        show_toast(f"Imported {count} conversation(s)")
        if self.on_import:
            self.on_import()

    def _error(self, msg):
        self._importing = False
        self.pick_btn.text = "Pick File"
        self.pick_btn.disabled = False
        self.progress.value = 0
        self.status.text = f"Error: {msg}"
        LOGBUF.add(f"Import error: {msg}")
