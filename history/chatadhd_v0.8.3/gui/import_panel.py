"""
ChatADHD v0.07.03 - Import Conversation Popup

Threaded import with live progress display.
"""
import logging
import os
import tempfile
import threading
import mimetypes
from urllib.parse import urlparse

import requests
import re

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

        # URL import
        url_row = BoxLayout(size_hint_y=None, height=dp(30), spacing=dp(4))
        url_row.add_widget(Label(text="Link:", color=C["text"],
                             size_hint_x=0.2, font_size=sp(9)))
        self.url_input = DarkInput(hint_text="https://...",
                                   size_hint_y=None, height=dp(30), multiline=False)
        url_row.add_widget(self.url_input)
        content.add_widget(url_row)

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
        self.link_btn = RBtn(text="Import Link", bg=C["card"],
                             on_press=self._import_link)
        btns.add_widget(self.link_btn)
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

    def _import_link(self, *_):
        if self._importing:
            show_toast("Import in progress...")
            return
        raw = (self.url_input.text or "").strip()
        if not raw:
            self.status.text = "Paste a link or file path first"
            return
        if os.path.exists(raw):
            self._on_file(raw)
            return
        if raw.startswith("/") and not os.path.exists(raw):
            self.status.text = "Path does not exist"
            return
        if not re.match(r'^https?://', raw, re.I):
            self.status.text = "Paste a full http(s) link or local file path"
            return
        self._importing = True
        self.pick_btn.disabled = True
        self.link_btn.disabled = True
        self.progress.value = 0
        self.status.text = f"Downloading: {raw[:60]}"
        t = threading.Thread(target=self._download_and_import_thread, args=(raw,), daemon=True)
        t.start()

    def _download_and_import_thread(self, url):
        temp_path = None
        try:
            parsed = urlparse(url)
            name = os.path.basename(parsed.path) or "imported_link"
            headers = {
                "User-Agent": "Mozilla/5.0 (Android 15; Mobile) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0 Mobile Safari/537.36",
                "Accept": "text/html,application/xhtml+xml,application/json,text/plain,*/*",
                "Accept-Language": "en-US,en;q=0.9",
                "Cache-Control": "no-cache",
                "Pragma": "no-cache",
            }
            attempts = [url]
            # Best-effort normalization for public share links that may redirect oddly.
            if 'claude.ai' in (parsed.netloc or '').lower() and '/chat/' in parsed.path:
                attempts.append(url.replace('/chat/', '/share/', 1))
            last_err = None
            for attempt in attempts:
                try:
                    with requests.get(attempt, stream=True, timeout=60, allow_redirects=True, headers=headers) as resp:
                        if resp.status_code in (401, 403):
                            raise PermissionError(f"{resp.status_code} forbidden: this link likely needs browser login or blocks scripted downloads")
                        resp.raise_for_status()
                        content_type = (resp.headers.get('content-type') or '').split(';', 1)[0].strip().lower()
                        total = int(resp.headers.get("content-length", "0") or 0)
                        guessed = mimetypes.guess_extension(content_type) or ""
                        suffix = os.path.splitext(name)[1]
                        if not suffix:
                            suffix = guessed or ('.html' if content_type.startswith('text/html') else '.tmp')
                        fd, temp_path = tempfile.mkstemp(prefix="chatadhd_import_", suffix=suffix)
                        os.close(fd)
                        done = 0
                        with open(temp_path, "wb") as f:
                            for chunk in resp.iter_content(chunk_size=65536):
                                if not chunk:
                                    continue
                                f.write(chunk)
                                done += len(chunk)
                                pct = int(done * 100 / total) if total > 0 else 0
                                Clock.schedule_once(lambda dt, p=pct, d=done: self._tick_download(p, d), 0)
                    Clock.schedule_once(lambda dt, p=temp_path: self._on_file(p), 0)
                    return
                except Exception as exc:
                    last_err = exc
                    if temp_path and os.path.exists(temp_path):
                        try:
                            os.remove(temp_path)
                        except OSError:
                            pass
                        temp_path = None
            raise last_err or RuntimeError("download failed")
        except Exception as exc:
            if temp_path and os.path.exists(temp_path):
                try:
                    os.remove(temp_path)
                except OSError:
                    pass
            msg = str(exc)
            low = url.lower()
            if ('claude.ai/chat/' in low or 'chatgpt.com/c/' in low or 'chat.openai.com/c/' in low) and ('forbidden' in msg.lower() or 'login' in msg.lower()):
                msg = 'Private live chat links are not directly importable without browser session cookies. Use a public share link or save/export the page to HTML/MHT first.'
            elif 'claude.ai/share/' in low and ('forbidden' in msg.lower() or 'login' in msg.lower()):
                msg = 'Claude share link blocked scripted download. Open it in the browser and save as HTML/MHT, then import the saved file.'
            log.exception("Download import failed")
            Clock.schedule_once(lambda dt, m=msg[:180]: self._error(m), 0)

    def _tick_download(self, pct, done):
        self.progress.value = pct
        kb = done / 1024.0
        self.status.text = f"Downloading... {pct}% ({kb:.0f} KB)"

    def _on_file(self, path):
        if not path or not os.path.isfile(path):
            self.status.text = "No file selected"
            return

        self._importing = True
        self.pick_btn.text = "Importing..."
        self.pick_btn.disabled = True
        self.link_btn.disabled = True
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

            def patched_import(msgs, title_arg=None):
                result = original_import_msg_list(msgs, title_arg)
                count[0] += 1
                Clock.schedule_once(lambda dt: self._tick(
                    count[0], f"Conv {count[0]}: {len(msgs)} msgs"
                ), 0)
                return result

            importer._import_message_list = patched_import

            results = importer.import_file(path, title)
            final = len(results) if isinstance(results, list) else 1
            Clock.schedule_once(lambda dt: self._finish(final), 0)

        except Exception:
            import traceback
            err_msg = traceback.format_exc().splitlines()[-1][:60]
            log.exception("Import failed")
            Clock.schedule_once(lambda dt, m=err_msg: self._error(m), 0)

    def _tick(self, n, msg):
        self.status.text = f"[{n}] {msg}"
        self.progress.value = (n * 5) % 100

    def _finish(self, count):
        self._importing = False
        self.pick_btn.text = "Pick File"
        self.pick_btn.disabled = False
        self.link_btn.disabled = False
        self.progress.value = 100
        self.status.text = f"Done: {count} conversation(s)"
        show_toast(f"Imported {count} conversation(s)")
        if self.on_import:
            self.on_import()

    def _error(self, msg):
        self._importing = False
        self.pick_btn.text = "Pick File"
        self.pick_btn.disabled = False
        self.link_btn.disabled = False
        self.progress.value = 0
        self.status.text = f"Error: {msg}"
        LOGBUF.add(f"Import error: {msg}")
