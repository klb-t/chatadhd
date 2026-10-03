"""
ChatADHD v0.07.03 - Notes Panel

Sliding panel for user notes, code snippets, and file references.
Backed by the hierarchical memory tree engine.
"""
import logging
import os
import zipfile
from pathlib import Path

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.popup import Popup
from kivy.uix.scrollview import ScrollView
from kivy.uix.spinner import Spinner
from kivy.metrics import dp, sp

from gui.base import C, RBtn, Card, DarkInput, Panel, LOGBUF, show_toast
from gui.dialogs import FilePickerPopup

log = logging.getLogger(__name__)

# Extensions to import when scanning a directory.
_IMPORTABLE_EXTS = {".py", ".md", ".txt", ".json", ".js", ".html", ".css",
                    ".ts", ".yaml", ".yml", ".toml", ".xml", ".sql"}
_MAX_DIR_FILES = 200
_MAX_FILE_CHARS = 10_000


class MemoryPanel(Panel):
    """Hierarchical memory tree with CRUD operations."""

    def __init__(self, memory, **kw):
        super().__init__(**kw)
        self.memory = memory
        self.panel_width = dp(280)

        # Header
        hdr = BoxLayout(size_hint_y=None, height=dp(34))
        hdr.add_widget(Label(text="Notes", font_size=sp(12),
                             color=C["text"], bold=True))
        hdr.add_widget(RBtn(text="X", size_hint_x=None, width=dp(34),
                            bg=C["card"], on_press=lambda *_: self.close()))
        self.add_widget(hdr)

        # Add buttons
        add_row = BoxLayout(size_hint_y=None, height=dp(28), spacing=dp(2))
        add_row.add_widget(RBtn(text="+Text", bg=C["accent"], font_size=sp(8),
                                on_press=lambda *_: self._add_text(None)))
        add_row.add_widget(RBtn(text="+Folder", bg=C["ok"], font_size=sp(8),
                                on_press=lambda *_: self._add_folder(None)))
        add_row.add_widget(RBtn(text="+File", bg=C["warn"], font_size=sp(8),
                                on_press=lambda *_: self._add_file(None)))
        add_row.add_widget(RBtn(text="+Dir", bg=C["card"], font_size=sp(8),
                                on_press=lambda *_: self._add_dir(None)))
        self.add_widget(add_row)

        # Tree scroll
        self.scroll = ScrollView()
        self.tree = BoxLayout(orientation="vertical", size_hint_y=None, spacing=dp(1))
        self.tree.bind(minimum_height=self.tree.setter("height"))
        self.scroll.add_widget(self.tree)
        self.add_widget(self.scroll)

    def refresh(self):
        self.tree.clear_widgets()
        if not self.memory:
            self.tree.add_widget(Label(text="No memory engine",
                                       size_hint_y=None, height=dp(28), color=C["dim"]))
            return
        self._build_tree(None)
        if not self.tree.children:
            self.tree.add_widget(Label(text="Empty — add items",
                                       size_hint_y=None, height=dp(28), color=C["dim"]))

    # ── Tree rendering ─────────────────────────────────────────────

    def _build_tree(self, parent_id):
        icons = {"folder": "[D]", "file": "[F]", "dir": "[DIR]", "text": " * "}
        for n in self.memory.get_children(parent_id):
            row = BoxLayout(size_hint_y=None, height=dp(28), spacing=dp(2))

            # Toggle active
            act = RBtn(
                text="+" if n.active else "-", size_hint_x=None, width=dp(24),
                bg=C["ok"] if n.active else C["dim"], font_size=sp(9),
            )
            act.node_id = n.id
            act.bind(on_press=self._toggle_active)
            row.add_widget(act)

            # Content label
            indent = "  " * n.depth
            icon = icons.get(n.node_type, " * ")
            btn = RBtn(text=f"{indent}{icon} {n.content[:18]}",
                       bg=C["card"], font_size=sp(8), halign="left")
            btn.node = n
            btn.bind(on_press=self._edit_node)
            row.add_widget(btn)

            # Delete
            del_btn = RBtn(text="x", size_hint_x=None, width=dp(22),
                           bg=C["err"], font_size=sp(8))
            del_btn.node_id = n.id
            del_btn.bind(on_press=self._delete_node)
            row.add_widget(del_btn)

            self.tree.add_widget(row)
            self._build_tree(n.id)

    # ── Actions ────────────────────────────────────────────────────

    def _toggle_active(self, btn):
        n = self.memory.get_node(btn.node_id)
        if n:
            self.memory.update_node(n.id, active=not n.active)
        self.refresh()

    def _delete_node(self, btn):
        self.memory.delete_node(btn.node_id, recursive=True)
        self.refresh()

    def _edit_node(self, btn):
        n = btn.node
        content = BoxLayout(orientation="vertical", padding=dp(6), spacing=dp(3))

        if n.node_type in ("file", "dir"):
            path = n.metadata.get("path", n.content)
            content.add_widget(Label(text=f"Path: {path[-35:]}", color=C["dim"],
                                     size_hint_y=None, height=dp(18), font_size=sp(8)))

        txt = DarkInput(text=n.content, multiline=True, size_hint_y=0.5)
        content.add_widget(txt)

        # Type spinner
        tr = BoxLayout(size_hint_y=None, height=dp(32))
        tr.add_widget(Label(text="Type:", color=C["text"], size_hint_x=0.25))
        ts = Spinner(text=n.node_type, values=["folder", "text", "file", "dir"],
                     size_hint_x=0.75, background_color=C["card"])
        tr.add_widget(ts)
        content.add_widget(tr)

        btns = BoxLayout(size_hint_y=None, height=dp(32), spacing=dp(3))
        btns.add_widget(RBtn(text="+Child", bg=C["ok"], font_size=sp(8),
                             on_press=lambda *_: self._add_text(n.id, popup)))
        btns.add_widget(RBtn(text="Save", bg=C["accent"], font_size=sp(8),
                             on_press=lambda *_: (
                                 self.memory.update_node(n.id,
                                                         content=txt.text.strip(),
                                                         node_type=ts.text),
                                 popup.dismiss(), self.refresh())))
        if n.node_type in ("folder", "dir"):
            btns.add_widget(RBtn(text="ZIP", bg=C["warn"], font_size=sp(8),
                                 on_press=lambda *_: self._export_zip(n)))
        content.add_widget(btns)

        popup = Popup(title=f"Edit: {n.content[:16]}", content=content,
                      size_hint=(0.92, 0.55))
        popup.open()

    # ── Add operations ─────────────────────────────────────────────

    def _add_text(self, parent_id, parent_popup=None):
        content = BoxLayout(orientation="vertical", padding=dp(6), spacing=dp(3))
        txt = DarkInput(hint_text="Content...", multiline=True, size_hint_y=0.6)
        content.add_widget(txt)

        def create(*_):
            if txt.text.strip():
                self.memory.add_node(txt.text.strip(), parent_id, "text")
                popup.dismiss()
                if parent_popup:
                    parent_popup.dismiss()
                self.refresh()

        content.add_widget(RBtn(text="Create", bg=C["accent"],
                                size_hint_y=None, height=dp(34), on_press=create))
        popup = Popup(title="Add text node", content=content, size_hint=(0.9, 0.4))
        popup.open()

    def _add_folder(self, parent_id):
        content = BoxLayout(orientation="vertical", padding=dp(6), spacing=dp(3))
        txt = DarkInput(hint_text="Folder name...", size_hint_y=None, height=dp(34))
        content.add_widget(txt)

        def create(*_):
            if txt.text.strip():
                self.memory.add_node(txt.text.strip(), parent_id, "folder")
                popup.dismiss()
                self.refresh()

        content.add_widget(RBtn(text="Create", bg=C["ok"],
                                size_hint_y=None, height=dp(34), on_press=create))
        popup = Popup(title="Add folder", content=content, size_hint=(0.9, 0.3))
        popup.open()

    def _add_file(self, parent_id):
        def on_file(path):
            if path and os.path.isfile(path):
                name = os.path.basename(path)
                self.memory.add_node(name, parent_id, "file",
                                     metadata={"path": path})
                self.refresh()
                show_toast(f"Added: {name}")
        FilePickerPopup(on_file, "Select File").open()

    def _add_dir(self, parent_id):
        """Import directory with full file contents into memory tree."""
        def on_dir(path):
            if not path or not os.path.isdir(path):
                return

            name = os.path.basename(path) or path
            root_id = self.memory.add_node(name, parent_id, "folder",
                                           metadata={"path": path})
            added = 0
            for root_dir, dirs, files in os.walk(path):
                dirs[:] = [d for d in dirs
                           if d not in ("__pycache__", ".git", "node_modules", ".venv")]
                for f in files:
                    if added >= _MAX_DIR_FILES:
                        show_toast(f"Limit reached ({_MAX_DIR_FILES} files)")
                        break
                    ext = os.path.splitext(f)[1].lower()
                    if ext not in _IMPORTABLE_EXTS:
                        continue
                    fpath = os.path.join(root_dir, f)
                    try:
                        file_content = Path(fpath).read_text(errors="replace")[:_MAX_FILE_CHARS]
                        self.memory.add_node(
                            f"[{f}]\n{file_content}", root_id, "file",
                            metadata={"path": fpath},
                        )
                        added += 1
                    except Exception as e:
                        LOGBUF.add(f"Skip {f}: {e}")

            self.refresh()
            show_toast(f"Added {added} files from {name}")

        FilePickerPopup(on_dir, "Select Directory", allow_dirs=True).open()

    # ── Export ─────────────────────────────────────────────────────

    def _export_zip(self, node):
        try:
            dl = Path("/storage/emulated/0/Download")
            if not dl.exists():
                dl = Path.home() / "Downloads"

            safe_name = node.content[:18].replace(" ", "_").replace("/", "_")
            zip_path = dl / f"{safe_name}.zip"

            with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
                if node.node_type == "dir":
                    dir_path = node.metadata.get("path", "")
                    if os.path.isdir(dir_path):
                        for root, _, files in os.walk(dir_path):
                            for f in files:
                                fp = os.path.join(root, f)
                                zf.write(fp, os.path.relpath(fp, dir_path))
                else:
                    zf.writestr("README.txt", node.content)
                    self._zip_children(zf, node.id, node.content[:16])

            show_toast(f"ZIP: {zip_path.name}")
        except Exception as e:
            log.exception("ZIP export failed")
            show_toast(f"ZIP error: {str(e)[:30]}")

    def _zip_children(self, zf, parent_id, prefix):
        for ch in self.memory.get_children(parent_id):
            name = ch.content[:28].replace("/", "_")
            if ch.node_type == "file":
                fp = ch.metadata.get("path", "")
                if os.path.isfile(fp):
                    zf.write(fp, f"{prefix}/{os.path.basename(fp)}")
            else:
                zf.writestr(f"{prefix}/{name}.txt", ch.content)
            self._zip_children(zf, ch.id, f"{prefix}/{name}")
