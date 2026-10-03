"""
gui/video_panel.py
===================

Video preview panel — pływające okno do oglądania wygenerowanych klipów.

Zgodne z IPanel: state serializowalny, on_event reaguje na eventy pipeline'u.

Subskrybowane eventy (nazwy z convention ChatADHD — dwukropki):
  - video:scene:clip:ready   → auto-załaduj klip jeśli follow ustawione
  - video:compose:ready      → załaduj finalny render

Domyślny mount: FLOATING, można ręcznie przełączyć na FLOATING_LOCKED.

Zgodne z fundamentalnymi zasadami:
  - Używa IVideoPlayer (fallback chain ffpyplayer/videoplayer/external)
  - Importy z gui.base dla stylu (C, RBtn, dp, sp) — spójność z resztą
  - Clock.schedule_once dla cross-thread UI updates (event bus jest
    synchroniczny, ale emitter może być w tle — worker, requests)
"""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Any, Dict, Optional

from gui.panel_system import IPanel, PanelMount, MountMode
from gui.video_player import IVideoPlayer, create_video_player

log = logging.getLogger(__name__)


class VideoPreviewPanel(IPanel):
    """
    Panel wideo-podglądu.

    State:
      current_clip_path: str | None
      follow_latest: bool
      follow_graph_id: str | None
      muted: bool
      loop: bool
      preferred_backend: str | None   — "kivy_video" | "kivy_videoplayer" | "external"
    """

    panel_type = "video_preview"

    def __init__(self, panel_id: str,
                 initial_state: Optional[Dict[str, Any]] = None,
                 player: Optional[IVideoPlayer] = None):
        self.panel_id = panel_id
        s = initial_state or {}
        self._current_clip_path: Optional[str] = s.get("current_clip_path")
        self._follow_latest: bool = s.get("follow_latest", True)
        self._follow_graph_id: Optional[str] = s.get("follow_graph_id")
        self._muted: bool = s.get("muted", False)
        self._loop: bool = s.get("loop", True)
        self._preferred_backend: Optional[str] = s.get("preferred_backend")

        self._widget = None
        self._player: IVideoPlayer = player or create_video_player(
            preferred_backend=self._preferred_backend,
        )

    # ── IPanel ────────────────────────────────────────────

    def get_widget(self) -> Any:
        if self._widget is None:
            self._widget = self._build_widget()
        return self._widget

    def get_title(self) -> str:
        if self._current_clip_path:
            return f"Video — {Path(self._current_clip_path).name}"
        return f"Video preview ({self._player.backend_name})"

    def get_state(self) -> Dict[str, Any]:
        return {
            "current_clip_path": self._current_clip_path,
            "follow_latest": self._follow_latest,
            "follow_graph_id": self._follow_graph_id,
            "muted": self._muted,
            "loop": self._loop,
            "preferred_backend": self._preferred_backend,
        }

    def set_state(self, state: Dict[str, Any]) -> None:
        self._current_clip_path = state.get("current_clip_path")
        self._follow_latest = state.get("follow_latest", True)
        self._follow_graph_id = state.get("follow_graph_id")
        self._muted = state.get("muted", False)
        self._loop = state.get("loop", True)
        new_pref = state.get("preferred_backend")
        if new_pref != self._preferred_backend:
            self._preferred_backend = new_pref
            self._player = create_video_player(preferred_backend=new_pref)
            if self._widget is not None:
                self._widget = self._build_widget()

        if self._widget is not None:
            self._schedule_refresh()

    def on_mount(self, mount: PanelMount) -> None:
        log.info("VideoPreviewPanel %s mounted as %s (backend: %s)",
                 self.panel_id, mount.mount_mode.value, self._player.backend_name)

    def on_event(self, event_name: str, payload: Dict[str, Any]) -> None:
        """
        Handler eventów bus'a. Bus w 0.07.x jest synchroniczny — jeśli emitter
        był w tle (semantic_worker, requests stream), jesteśmy w złym thread'ie.
        Clock.schedule_once dla UI updates.
        """
        if not self._follow_latest:
            return

        clip_path: Optional[str] = None
        graph_id = payload.get("graph_id") if payload else None

        if event_name == "video:scene:clip:ready":
            if self._follow_graph_id and graph_id != self._follow_graph_id:
                return
            clip_path = payload.get("clip_path") if payload else None
        elif event_name == "video:compose:ready":
            if self._follow_graph_id and graph_id != self._follow_graph_id:
                return
            clip_path = payload.get("output_path") if payload else None

        if clip_path:
            self._schedule_load(clip_path)

    def query(self, key: str) -> Any:
        if key == "current_clip_path":
            return self._current_clip_path
        if key == "backend":
            return self._player.backend_name
        if key == "backend_caps":
            return self._player.capabilities().__dict__
        return None

    # ── Public API ────────────────────────────────────────

    def load_clip(self, path: str) -> bool:
        self._current_clip_path = path
        if self._widget is not None:
            self._refresh_widget()
        success = self._player.load(path)
        log.info("VideoPreviewPanel %s loaded %s (%s)", self.panel_id, path,
                 "OK" if success else "FAILED")
        return success

    def toggle_follow(self) -> None:
        self._follow_latest = not self._follow_latest

    def toggle_mute(self) -> None:
        self._muted = not self._muted
        self._player.set_volume(0.0 if self._muted else 1.0)

    def toggle_loop(self) -> None:
        self._loop = not self._loop
        self._player.set_loop(self._loop)

    # ── Cross-thread scheduling ──────────────────────────

    def _schedule_load(self, path: str) -> None:
        try:
            from kivy.clock import Clock
            Clock.schedule_once(lambda dt: self.load_clip(path), 0)
        except ImportError:
            self.load_clip(path)

    def _schedule_refresh(self) -> None:
        try:
            from kivy.clock import Clock
            Clock.schedule_once(lambda dt: self._refresh_widget(), 0)
        except ImportError:
            self._refresh_widget()

    # ── Widget building (Kivy, z gui.base) ───────────────

    def _build_widget(self) -> Any:
        try:
            from gui.base import C, RBtn
            from kivy.metrics import dp, sp
            has_base = True
        except ImportError:
            from kivy.uix.button import Button as RBtn
            C = {"text": (0.95, 0.95, 0.95, 1),
                 "dim": (0.55, 0.55, 0.60, 1),
                 "card": (0.14, 0.14, 0.18, 1),
                 "accent": (0.30, 0.55, 0.95, 1)}
            dp = lambda x: x
            sp = lambda x: x
            has_base = False

        from kivy.uix.boxlayout import BoxLayout
        from kivy.uix.label import Label

        root = BoxLayout(orientation="vertical")

        # Header
        header = BoxLayout(orientation="horizontal", size_hint_y=None,
                            height=dp(32), padding=dp(2), spacing=dp(2))
        self._title_label = Label(
            text=self.get_title(),
            color=C["text"], font_size=sp(10),
            size_hint_x=0.55, shorten=True, shorten_from="right",
        )
        header.add_widget(self._title_label)

        btn_kwargs = {"font_size": sp(9)}
        if has_base:
            btn_kwargs["bg"] = C["card"]

        self._follow_btn = RBtn(
            text="Follow: ON" if self._follow_latest else "Follow: OFF",
            size_hint_x=0.15, **btn_kwargs,
        )
        self._follow_btn.bind(on_release=lambda *_: self._on_follow_toggle())
        header.add_widget(self._follow_btn)

        self._mute_btn = RBtn(
            text="Mute" if not self._muted else "Unmute",
            size_hint_x=0.15, **btn_kwargs,
        )
        self._mute_btn.bind(on_release=lambda *_: self._on_mute_toggle())
        header.add_widget(self._mute_btn)

        self._loop_btn = RBtn(
            text="Loop" if self._loop else "Once",
            size_hint_x=0.15, **btn_kwargs,
        )
        self._loop_btn.bind(on_release=lambda *_: self._on_loop_toggle())
        header.add_widget(self._loop_btn)

        root.add_widget(header)

        # Player widget z backendu
        player_widget = self._player.build_widget()
        if player_widget is not None:
            root.add_widget(player_widget)
        else:
            root.add_widget(Label(
                text=f"[Backend {self._player.backend_name} cannot embed]",
                color=C["dim"],
            ))

        if self._current_clip_path:
            self._player.load(self._current_clip_path)
            self._player.set_loop(self._loop)
            if self._muted:
                self._player.set_volume(0.0)

        return root

    def _refresh_widget(self) -> None:
        if self._widget is None:
            return
        self._title_label.text = self.get_title()
        self._follow_btn.text = "Follow: ON" if self._follow_latest else "Follow: OFF"
        self._mute_btn.text = "Mute" if not self._muted else "Unmute"
        self._loop_btn.text = "Loop" if self._loop else "Once"
        if self._current_clip_path:
            self._player.load(self._current_clip_path)

    # ── Event handlers ───────────────────────────────────

    def _on_follow_toggle(self) -> None:
        self.toggle_follow()
        self._refresh_widget()

    def _on_mute_toggle(self) -> None:
        self.toggle_mute()
        self._refresh_widget()

    def _on_loop_toggle(self) -> None:
        self.toggle_loop()
        self._refresh_widget()


# ─── Registration helper ────────────────────────────────────────

def register_video_preview(registry) -> None:
    from gui.panel_system import PanelRegistration, PanelMount, MountMode

    registry.register(PanelRegistration(
        panel_type="video_preview",
        display_name="Video Preview",
        factory=lambda pid, state: VideoPreviewPanel(pid, state),
        default_mount=PanelMount(
            mount_mode=MountMode.FLOATING,
            position=(1200.0, 200.0),
            size=(640.0, 360.0),
        ),
        category="video",
        singleton=False,
    ))
