"""
ChatADHD v0.07.01 - Chat Panel

Main chat interface: message list, input area, streaming display,
feature toggles (web search, deep research, reasoning), and
attachment management.
"""
import json
import logging
import os
import threading
from typing import Callable, Optional

from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.scrollview import ScrollView
from kivy.core.clipboard import Clipboard
from kivy.core.window import Window
from kivy.metrics import dp, sp
from kivy.clock import Clock

from gui.base import (C, RBtn, Card, DarkInput, LOGBUF, show_toast,
                      detect_artifacts)
from gui.dialogs import (FilePickerPopup, QuickAPIPanel, SettingsPopup,
                         LogViewer, ModelSelectorPopup)

log = logging.getLogger(__name__)


# ═══════════════════════════════════════════════════════════════════
# MESSAGE BUBBLE
# ═══════════════════════════════════════════════════════════════════

class MsgBubble(BoxLayout):
    """Message display with scrollable expand and inline artifact preview."""

    _COLLAPSE_THRESHOLD = 300  # chars
    _EXPAND_MAX_H = 500  # dp — max expanded height before scroll kicks in

    def __init__(self, msg: dict, models, on_exclude=None, on_include=None, **kw):
        super().__init__(orientation="vertical", size_hint_y=None,
                         padding=dp(4), spacing=dp(2), **kw)
        self.msg = msg
        self._expanded = False
        self._artifacts = detect_artifacts(msg["text"])

        is_user = msg["role"] == "user"
        is_excluded = msg.get("status") == "excluded"
        is_version = msg.get("status") == "version"

        bg_color = C["user"] if is_user else C["ai"]
        if is_excluded:
            bg_color = tuple(c * 0.5 for c in bg_color[:3]) + (0.6,)
        if is_version:
            bg_color = tuple(c * 0.4 for c in bg_color[:3]) + (0.4,)

        from kivy.graphics import Color as GColor, RoundedRectangle
        with self.canvas.before:
            GColor(*bg_color)
            self._rect = RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(6)])
        self.bind(
            pos=lambda *_: setattr(self._rect, "pos", self.pos),
            size=lambda *_: setattr(self._rect, "size", self.size),
        )

        # ── Header ────────────────────────────────────────────
        hdr = BoxLayout(size_hint_y=None, height=dp(20), spacing=dp(2))
        role_label = msg["role"].upper()[:4]
        hdr.add_widget(Label(text=role_label, font_size=sp(8), color=C["accent"],
                             size_hint_x=None, width=dp(30), bold=True))

        model_name = models.name(msg.get("model", "")) if msg.get("model") else ""
        hdr.add_widget(Label(text=model_name[:12], font_size=sp(7), color=C["dim"]))

        if msg.get("weight", 1.0) != 1.0:
            hdr.add_widget(Label(text=f"w:{msg['weight']:.1f}", font_size=sp(7),
                                 color=C["warn"], size_hint_x=None, width=dp(30)))

        if is_excluded and on_include:
            hdr.add_widget(RBtn(text="[+]", size_hint_x=None, width=dp(26),
                                bg=C["ok"], font_size=sp(7),
                                on_press=lambda *_: on_include(msg["id"])))
        elif not is_excluded and on_exclude:
            hdr.add_widget(RBtn(text="[-]", size_hint_x=None, width=dp(26),
                                bg=C["dim"], font_size=sp(7),
                                on_press=lambda *_: on_exclude(msg["id"])))

        hdr.add_widget(RBtn(text="Cp", size_hint_x=None, width=dp(26),
                            bg=C["card"], font_size=sp(7),
                            on_press=lambda *_: (Clipboard.copy(msg["text"]),
                                                 show_toast("Copied"))))
        self.add_widget(hdr)

        # ── Text in scrollable container ──────────────────────
        text = msg["text"]
        # Strip code blocks from display text — show them as artifacts below.
        display_text = text
        for art in reversed(self._artifacts):
            display_text = display_text[:art["start"]] + f'[{art["lang"]} code — see below]' + display_text[art["end"]:]

        is_long = len(display_text) > self._COLLAPSE_THRESHOLD
        self._full_display = display_text
        collapsed_text = display_text[:self._COLLAPSE_THRESHOLD] + "..." if is_long else display_text

        # The text widget lives inside a ScrollView for expanded mode.
        self.txt = DarkInput(text=collapsed_text, readonly=True, font_size=sp(10),
                             size_hint_y=None, multiline=True)

        self.txt_scroll = ScrollView(size_hint_y=None, do_scroll_x=False,
                                     bar_width=dp(4))
        self.txt_scroll.add_widget(self.txt)
        self.add_widget(self.txt_scroll)

        # ── Expand / Collapse ─────────────────────────────────
        if is_long:
            self.expand_btn = RBtn(text="Expand", size_hint_y=None, height=dp(22),
                                   bg=C["card"], font_size=sp(8),
                                   on_press=self._toggle_expand)
            self.add_widget(self.expand_btn)
        else:
            self.expand_btn = None

        # ── Artifact previews ─────────────────────────────────
        for art in self._artifacts[:5]:
            self.add_widget(ArtifactBar(art))

        # ── Reasoning metadata ────────────────────────────────
        reasoning = msg.get("metadata", {}).get("reasoning", "")
        if reasoning:
            self.add_widget(Label(
                text=f"[Reasoning: {len(reasoning)} chars]",
                font_size=sp(7), color=C["dim"],
                size_hint_y=None, height=dp(16),
            ))

        self._update_height()

    def _toggle_expand(self, *_):
        self._expanded = not self._expanded
        if self._expanded:
            self.txt.text = self._full_display
            self.expand_btn.text = "Collapse"
        else:
            self.txt.text = self._full_display[:self._COLLAPSE_THRESHOLD] + "..."
            self.expand_btn.text = "Expand"
        self._update_height()

    def _update_height(self):
        """Recalculate heights so ScrollView works properly."""
        lines = self.txt.text.count("\n") + 1
        cpl = max(1, int((Window.width - dp(50)) / dp(7)))
        wrapped = max(lines, len(self.txt.text) // cpl + 1)
        text_h = max(dp(28), wrapped * dp(14))

        if self._expanded:
            # Text widget gets full height, scroll container gets capped.
            self.txt.height = text_h
            self.txt_scroll.height = min(dp(self._EXPAND_MAX_H), text_h)
        else:
            # Collapsed: no scroll needed.
            capped = min(dp(200), text_h)
            self.txt.height = capped
            self.txt_scroll.height = capped

        extra = dp(24)  # header
        extra += self.txt_scroll.height
        if self.expand_btn:
            extra += dp(22)
        extra += dp(26) * len([c for c in self.children if isinstance(c, ArtifactBar)])
        extra += dp(16) if any(isinstance(c, Label) and "Reasoning" in (c.text or "")
                               for c in self.children) else 0
        extra += dp(8)
        self.height = extra


# ═══════════════════════════════════════════════════════════════════
# ARTIFACT BAR
# ═══════════════════════════════════════════════════════════════════

class ArtifactBar(BoxLayout):
    """Detected code artifact with preview, Copy and Save actions."""

    def __init__(self, artifact: dict, **kw):
        super().__init__(orientation="vertical", size_hint_y=None,
                         spacing=dp(1), **kw)
        self.artifact = artifact
        self._preview_shown = False
        self.height = dp(26)

        # ── Header bar ────────────────────────────────────────
        bar = BoxLayout(size_hint_y=None, height=dp(26), spacing=dp(2))

        from kivy.graphics import Color as GColor, RoundedRectangle
        with bar.canvas.before:
            GColor(*C["artifact"])
            bar._rect = RoundedRectangle(pos=bar.pos, size=bar.size, radius=[dp(4)])
        bar.bind(
            pos=lambda *_: setattr(bar._rect, "pos", bar.pos),
            size=lambda *_: setattr(bar._rect, "size", bar.size),
        )

        lang = artifact.get("lang", "code")
        lines = artifact["content"].count("\n") + 1
        bar.add_widget(RBtn(text=f"▶ {lang} ({lines}L)", bg=C["card"],
                            font_size=sp(8), size_hint_x=0.4,
                            on_press=self._toggle_preview))
        bar.add_widget(RBtn(text="Copy", bg=C["card"], font_size=sp(8),
                            size_hint_x=0.3,
                            on_press=lambda *_: (Clipboard.copy(artifact["content"]),
                                                 show_toast("Code copied"))))
        bar.add_widget(RBtn(text="->Mem", bg=C["ok"], font_size=sp(8),
                            size_hint_x=0.3,
                            on_press=lambda *_: show_toast("Save to notes: TODO")))
        self.add_widget(bar)
        self._bar = bar

        # ── Preview (initially hidden) ────────────────────────
        self._preview_box = None

    def _toggle_preview(self, *_):
        if self._preview_shown:
            if self._preview_box:
                self.remove_widget(self._preview_box)
                self._preview_box = None
            self.height = dp(26)
            self._preview_shown = False
        else:
            content = self.artifact["content"]
            preview_lines = min(20, content.count("\n") + 1)
            preview_h = max(dp(40), min(dp(250), preview_lines * dp(13)))

            txt = DarkInput(
                text=content, readonly=True, font_size=sp(8),
                size_hint_y=None, multiline=True,
            )
            txt.height = preview_h + dp(20)

            scroll = ScrollView(size_hint_y=None, height=preview_h,
                                do_scroll_x=True, bar_width=dp(3))
            scroll.add_widget(txt)

            self._preview_box = scroll
            self.add_widget(scroll)
            self.height = dp(26) + preview_h
            self._preview_shown = True


# ═══════════════════════════════════════════════════════════════════
# STREAMING BUBBLE
# ═══════════════════════════════════════════════════════════════════

class StreamingBubble(BoxLayout):
    """Live-updating display for streaming API responses."""

    def __init__(self, models, model_id: str, **kw):
        super().__init__(orientation="vertical", size_hint_y=None,
                         height=dp(60), padding=dp(4), spacing=dp(2), **kw)

        from kivy.graphics import Color as GColor, RoundedRectangle
        with self.canvas.before:
            GColor(*C["ai"])
            self._rect = RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(6)])
        self.bind(
            pos=lambda *_: setattr(self._rect, "pos", self.pos),
            size=lambda *_: setattr(self._rect, "size", self.size),
        )

        hdr = BoxLayout(size_hint_y=None, height=dp(16))
        hdr.add_widget(Label(text="ASST", font_size=sp(8), color=C["accent"],
                             size_hint_x=None, width=dp(30)))
        hdr.add_widget(Label(text=models.name(model_id), font_size=sp(8),
                             color=C["dim"]))
        self.status_lbl = Label(text="...", font_size=sp(8), color=C["warn"],
                                size_hint_x=0.2)
        hdr.add_widget(self.status_lbl)
        self.add_widget(hdr)

        self.txt = DarkInput(text="", readonly=True, font_size=sp(10),
                             size_hint_y=None, height=dp(28), multiline=True)
        self.add_widget(self.txt)

        self._text = ""
        self._reasoning = ""
        self._reason_lbl = None

    def append(self, chunk: str) -> None:
        self._text += chunk
        self.txt.text = self._text
        self._update_height()

    def append_reasoning(self, chunk: str) -> None:
        if not self._reason_lbl:
            self._reason_lbl = Label(text="[thinking] ", font_size=sp(8),
                                     color=C["dim"], size_hint_y=None, height=dp(18))
            self.add_widget(self._reason_lbl, index=0)
        self._reasoning += chunk
        self._reason_lbl.text = "[thinking] " + self._reasoning[-60:]

    def finish(self) -> None:
        self.status_lbl.text = "OK"
        self.status_lbl.color = C["ok"]

    def _update_height(self):
        lines = self._text.count("\n") + 1
        cpl = max(1, int((Window.width - dp(50)) / dp(7)))
        wrapped = max(lines, len(self._text) // cpl + 1)
        self.txt.height = min(dp(200), max(dp(28), wrapped * dp(14)))
        extra = dp(18) if self._reason_lbl else 0
        self.height = dp(16) + self.txt.height + dp(8) + extra


# ═══════════════════════════════════════════════════════════════════
# MAIN CHAT PANEL
# ═══════════════════════════════════════════════════════════════════

class ChatPanel(BoxLayout):
    """Central chat area: top bar, messages, input, feature toggles."""

    def __init__(self, engine, models, config, on_settings=None, **kw):
        super().__init__(orientation="vertical", **kw)
        self.engine = engine
        self.models = models
        self.config = config
        self.on_settings = on_settings

        self._sending = False
        self._pending_files: list[str] = []
        self._streaming_bubble: Optional[StreamingBubble] = None
        self._long_press_ev = None

        # Feature toggles
        self.web_search = False
        self.deep_research = False
        self.reasoning_effort: Optional[str] = None

        Window.softinput_mode = "below_target"
        self._build()

    def _build(self):
        # ── Top bar ────────────────────────────────────────────────
        top = BoxLayout(size_hint_y=None, height=dp(38), padding=dp(2), spacing=dp(2))

        self.model_btn = RBtn(
            text=self.models.name(self.config.get("default_model"))[:10],
            size_hint_x=0.25, bg=C["card"], font_size=sp(9),
        )
        self.model_btn.bind(on_press=self._open_models)
        top.add_widget(self.model_btn)

        buttons = [
            ("Cfg",  C["accent"], lambda *_: QuickAPIPanel(
                self.config, self.models, self._on_api_change).open()),
            ("Sync", C["ok"],     self._open_github_sync),
            ("New",  C["card"],   self._new_conv),
            ("Set",  C["card"],   lambda *_: self.on_settings() if self.on_settings else None),
            ("Log",  C["warn"],   lambda *_: LogViewer().open()),
        ]
        for txt, clr, fn in buttons:
            top.add_widget(RBtn(text=txt, size_hint_x=0.15, bg=clr,
                                font_size=sp(9), on_press=fn))
        self.add_widget(top)

        # ── Feature toggles ───────────────────────────────────────
        feat = BoxLayout(size_hint_y=None, height=dp(30), padding=dp(2), spacing=dp(2))

        self.web_btn = RBtn(text="Web", size_hint_x=0.25, bg=C["card"], font_size=sp(9))
        self.web_btn.bind(on_press=self._toggle_web)
        feat.add_widget(self.web_btn)

        self.research_btn = RBtn(text="Deep", size_hint_x=0.25, bg=C["card"], font_size=sp(9))
        self.research_btn.bind(on_press=self._toggle_research)
        feat.add_widget(self.research_btn)

        self.reason_btn = RBtn(text="Auto", size_hint_x=0.25, bg=C["card"], font_size=sp(9))
        self.reason_btn.bind(on_press=self._cycle_reasoning)
        feat.add_widget(self.reason_btn)

        self.think_lbl = Label(text="", size_hint_x=0.25, font_size=sp(9), color=C["dim"])
        feat.add_widget(self.think_lbl)
        self.add_widget(feat)

        # ── Messages scroll ───────────────────────────────────────
        self.scroll = ScrollView()
        self.msgs = BoxLayout(orientation="vertical", size_hint_y=None,
                              spacing=dp(4), padding=dp(4))
        self.msgs.bind(minimum_height=self.msgs.setter("height"))
        self.scroll.add_widget(self.msgs)
        self.add_widget(self.scroll)

        # ── Attachment bar ─────────────────────────────────────────
        self.att_bar = BoxLayout(size_hint_y=None, height=0,
                                 padding=[dp(4), 0], spacing=dp(2))
        self.att_lbl = Label(text="", font_size=sp(8), color=C["ok"], halign="left")
        self.att_bar.add_widget(self.att_lbl)
        self.att_bar.add_widget(RBtn(text="X", size_hint_x=None, width=dp(26),
                                     bg=C["err"], font_size=sp(9),
                                     on_press=self._clear_files))
        self.add_widget(self.att_bar)

        # ── Input area ─────────────────────────────────────────────
        inp = Card(size_hint_y=None, height=dp(72), orientation="vertical", spacing=dp(2))
        row = BoxLayout(spacing=dp(3))
        self.txt_in = DarkInput(hint_text="Message...", multiline=True, font_size=sp(11))
        row.add_widget(self.txt_in)

        self.send_btn = RBtn(text="Send", size_hint_x=None, width=dp(50),
                             bg=C["accent"], font_size=sp(10))
        self.send_btn.bind(on_release=self._send)
        self.send_btn.bind(on_touch_down=self._on_send_down)
        self.send_btn.bind(on_touch_up=self._on_send_up)
        row.add_widget(self.send_btn)
        inp.add_widget(row)

        bottom = BoxLayout(size_hint_y=None, height=dp(24), spacing=dp(3))
        bottom.add_widget(RBtn(text="Attach", bg=C["card"], font_size=sp(9),
                               on_press=lambda *_: FilePickerPopup(self._on_file).open()))
        bottom.add_widget(RBtn(text="Voice", size_hint_x=None, width=dp(50),
                               bg=C["card"], font_size=sp(9),
                               on_press=self._voice_input))
        inp.add_widget(bottom)
        self.add_widget(inp)

        # ── Status ─────────────────────────────────────────────────
        self.status = Label(text="Ready", size_hint_y=None, height=dp(16),
                            font_size=sp(8), color=C["dim"])
        self.add_widget(self.status)

    # ── Feature toggles ───────────────────────────────────────────

    def _toggle_web(self, *_):
        self.web_search = not self.web_search
        self.web_btn.background_color = C["accent"] if self.web_search else C["card"]
        if self.web_search and self.deep_research:
            self.deep_research = False
            self.research_btn.background_color = C["card"]

    def _toggle_research(self, *_):
        self.deep_research = not self.deep_research
        self.research_btn.background_color = C["warn"] if self.deep_research else C["card"]
        if self.deep_research:
            self.web_search = True
            self.web_btn.background_color = C["accent"]

    _REASONING_CYCLE = [None, "low", "medium", "high", "max"]
    _REASONING_LABELS = ["Auto", "Low", "Med", "High", "MAX"]

    def _cycle_reasoning(self, *_):
        idx = (self._REASONING_CYCLE.index(self.reasoning_effort)
               if self.reasoning_effort in self._REASONING_CYCLE else 0)
        idx = (idx + 1) % len(self._REASONING_CYCLE)
        self.reasoning_effort = self._REASONING_CYCLE[idx]
        self.reason_btn.text = self._REASONING_LABELS[idx]
        self.reason_btn.background_color = (C["accent"]
                                            if self.reasoning_effort else C["card"])

    # ── Model / API ────────────────────────────────────────────────

    def _open_models(self, *_):
        def on_sel(mid):
            self.config.set("default_model", mid)
            self.model_btn.text = self.models.name(mid)[:10]
        ModelSelectorPopup(self.models, self.config.get("default_model"), on_sel).open()

    def _on_api_change(self):
        self.model_btn.text = self.models.name(self.config.get("default_model"))[:10]

    def _refresh_models(self, *_):
        self.status.text = "Loading models..."
        def _bg():
            ok = self.models.update_from_api()
            Clock.schedule_once(lambda dt: self._models_done(ok))
        threading.Thread(target=_bg, daemon=True).start()

    def _models_done(self, ok: bool):
        if ok:
            self.status.text = f"{len(self.models.all())} models"
            self.model_btn.text = self.models.name(self.config.get("default_model"))[:10]
        else:
            self.status.text = "Failed to load models"

    # ── Conversation ───────────────────────────────────────────────

    def _new_conv(self, *_):
        self.engine.new_conv()
        self.refresh()
        self.status.text = "New chat"

    # ── Files ──────────────────────────────────────────────────────

    def _on_file(self, path):
        if path:
            self._pending_files.append(path)
            self._update_att_bar()

    def _clear_files(self, *_):
        self._pending_files.clear()
        self._update_att_bar()

    def _update_att_bar(self):
        if self._pending_files:
            names = [os.path.basename(f)[:10] for f in self._pending_files[:3]]
            self.att_lbl.text = f"[{len(self._pending_files)}] " + ", ".join(names)
            self.att_bar.height = dp(22)
        else:
            self.att_bar.height = 0

    # ── Long-press Send → API Editor ──────────────────────────────

    def _on_send_down(self, w, touch):
        if w.collide_point(*touch.pos):
            self._long_press_ev = Clock.schedule_once(
                lambda dt: self._open_api_editor(), 0.6)
            touch.grab(w)
        return False

    def _on_send_up(self, w, touch):
        if touch.grab_current == w:
            touch.ungrab(w)
            if self._long_press_ev:
                self._long_press_ev.cancel()
                self._long_press_ev = None
        return False

    def _open_api_editor(self):
        show_toast("API Editor: TODO")

    # ── Voice ──────────────────────────────────────────────────────

    def _voice_input(self, *_):
        try:
            from gui.voice_panel import VoiceInputPopup
            VoiceInputPopup(self.engine, self._on_voice_result).open()
        except Exception as e:
            log.exception("Voice input unavailable")
            show_toast(f"Voice error: {str(e)[:25]}")

    def _on_voice_result(self, text, alternatives=None):
        if text:
            current = self.txt_in.text
            if current and not current.endswith(" "):
                text = " " + text
            self.txt_in.text = current + text

    # ── GitHub sync ────────────────────────────────────────────────

    def _open_github_sync(self, *_):
        try:
            from gui.github_panel import GitHubSyncPopup
            GitHubSyncPopup(self.engine, self.config, self.engine.secrets).open()
        except Exception as e:
            log.exception("GitHub sync panel unavailable")
            show_toast(f"Sync error: {str(e)[:25]}")

    # ── Send message ───────────────────────────────────────────────

    def _send(self, *_):
        if self._sending:
            return

        text = self.txt_in.text.strip()
        if not text:
            return

        if not self.engine.secrets.get("api_key"):
            if self.on_settings:
                self.on_settings()
            return

        self._sending = True
        self.txt_in.text = ""
        self.status.text = "Sending..."
        self.think_lbl.text = ""

        attachments = self._pending_files.copy()
        self._pending_files.clear()
        self._update_att_bar()

        model = self.config.get("default_model")
        self._streaming_bubble = StreamingBubble(self.models, model)
        self.msgs.add_widget(self._streaming_bubble)
        Clock.schedule_once(lambda dt: setattr(self.scroll, "scroll_y", 0), 0.1)

        def on_chunk(c):
            Clock.schedule_once(lambda dt: self._on_chunk(c))

        def on_reasoning(r):
            Clock.schedule_once(lambda dt: self._on_reasoning(r))

        def _thread():
            try:
                self.engine.send(
                    text,
                    attachments=attachments,
                    on_chunk=on_chunk,
                    on_reasoning=on_reasoning,
                    web_search=self.web_search,
                    deep_research=self.deep_research,
                    reasoning_effort=self.reasoning_effort,
                )
                Clock.schedule_once(lambda dt: self._send_ok())
            except Exception as e:
                log.exception("Send failed")
                Clock.schedule_once(lambda dt: self._send_err(str(e)))

        threading.Thread(target=_thread, daemon=True).start()

    def _on_chunk(self, chunk: str):
        if self._streaming_bubble:
            self._streaming_bubble.append(chunk)
            Clock.schedule_once(lambda dt: setattr(self.scroll, "scroll_y", 0), 0)

    def _on_reasoning(self, chunk: str):
        self.think_lbl.text = "[thinking]"
        if self._streaming_bubble:
            self._streaming_bubble.append_reasoning(chunk)

    def _send_ok(self):
        self._sending = False
        if self._streaming_bubble:
            self._streaming_bubble.finish()
            self._streaming_bubble = None
        self.refresh()
        self.status.text = "Ready"
        self.think_lbl.text = ""

    def _send_err(self, err: str):
        self._sending = False
        if self._streaming_bubble:
            self.msgs.remove_widget(self._streaming_bubble)
            self._streaming_bubble = None
        self.status.text = f"Error: {err[:30]}"
        self.think_lbl.text = ""
        LOGBUF.add(f"Send error: {err}")

    # ── Refresh ────────────────────────────────────────────────────

    def refresh(self):
        self.msgs.clear_widgets()
        if not self.engine.conv or not self.engine.db:
            return
        for m in self.engine.db.get_msgs(self.engine.conv["id"], include_all=True):
            self.msgs.add_widget(MsgBubble(
                m, self.models,
                on_exclude=lambda mid: (self.engine.db.set_msg_status(mid, "excluded"),
                                        self.refresh()),
                on_include=lambda mid: (self.engine.db.set_msg_status(mid, "active"),
                                        self.refresh()),
            ))
        Clock.schedule_once(lambda dt: setattr(self.scroll, "scroll_y", 0))
