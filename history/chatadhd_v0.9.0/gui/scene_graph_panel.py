"""
gui/scene_graph_panel.py
=========================

Edytor scene graphu. W 0.9.0 MVP: widok timeline-linear.
Graph view i score view są TODO na 0.9.0-b1+.

Panel trzyma REFERENCJĘ do grafu, nie kopię — mutacje idą bezpośrednio do
SceneGraph, panel publikuje eventy po zapisie.

State:
  active_graph_id       : str | None
  view_mode             : "timeline" | "graph" | "score"
  selected_scene_id     : str | None

Panel subskrybuje zdarzenia pipeline'u i odświeża strip thumbnails
gdy keyframes/clips są gotowe.
"""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

from gui.panel_system import IPanel, PanelMount

log = logging.getLogger(__name__)


class SceneGraphEditorPanel(IPanel):
    """
    Edytor struktury filmu.

    Integracja z pipeline przez callbacki (ustawiane w main.py przez DI):
      - on_generate_keyframe(graph_id, scene_id)
      - on_generate_clip(graph_id, scene_id)
      - on_compose(graph_id)
      - on_load_graph(graph_id) → SceneGraph
      - on_save_graph(graph)

    To nie jest zależność twarda — panel nie wie o VideoPipeline bezpośrednio.
    Zgodne z kodeksem: dependency injection, brak dynamic_cast.
    """

    panel_type = "scene_graph_editor"

    def __init__(self, panel_id: str, initial_state: Optional[Dict[str, Any]] = None):
        self.panel_id = panel_id
        s = initial_state or {}
        self._active_graph_id: Optional[str] = s.get("active_graph_id")
        self._view_mode: str = s.get("view_mode", "timeline")
        self._selected_scene_id: Optional[str] = s.get("selected_scene_id")

        # DI — callbacks ustawiane po konstrukcji w startup.
        self.on_generate_keyframe: Optional[Callable[[str, str], None]] = None
        self.on_generate_clip: Optional[Callable[[str, str], None]] = None
        self.on_compose: Optional[Callable[[str], None]] = None
        self.on_load_graph: Optional[Callable[[str], Any]] = None  # → SceneGraph
        self.on_save_graph: Optional[Callable[[Any], None]] = None

        # Widget i cache grafu (lazy load).
        self._widget = None
        self._graph_cache: Any = None  # SceneGraph

    # ── IPanel ────────────────────────────────────────────

    def get_widget(self) -> Any:
        if self._widget is None:
            self._widget = self._build_widget()
            if self._active_graph_id:
                self._load_active_graph()
        return self._widget

    def get_title(self) -> str:
        if self._graph_cache is not None:
            return f"Scene Graph — {self._graph_cache.name}"
        if self._active_graph_id:
            return f"Scene Graph — {self._active_graph_id}"
        return "Scene Graph Editor"

    def get_state(self) -> Dict[str, Any]:
        return {
            "active_graph_id": self._active_graph_id,
            "view_mode": self._view_mode,
            "selected_scene_id": self._selected_scene_id,
        }

    def set_state(self, state: Dict[str, Any]) -> None:
        self._active_graph_id = state.get("active_graph_id")
        self._view_mode = state.get("view_mode", "timeline")
        self._selected_scene_id = state.get("selected_scene_id")
        if self._widget is not None:
            self._refresh()

    def on_event(self, event_name: str, payload: Dict[str, Any]) -> None:
        """
        Handler z bus'a. Event bus synchroniczny + emitter może być w tle
        (pipeline worker) → UI updates przez Clock.schedule_once.
        """
        if self._active_graph_id != payload.get("graph_id"):
            return
        if event_name in (
            "video:scene:keyframe:ready",
            "video:scene:keyframe:failed",
            "video:scene:clip:ready",
            "video:scene:clip:failed",
        ):
            # Reload z dysku — pipeline mógł zmodyfikować graf.
            self._schedule_reload()

    def _schedule_reload(self) -> None:
        try:
            from kivy.clock import Clock
            Clock.schedule_once(lambda dt: self._load_active_graph(), 0)
        except ImportError:
            self._load_active_graph()

    def query(self, key: str) -> Any:
        if key == "active_graph_id":
            return self._active_graph_id
        if key == "selected_scene_id":
            return self._selected_scene_id
        if key == "scene_count":
            return len(self._graph_cache.scenes) if self._graph_cache else 0
        return None

    # ── Public API ────────────────────────────────────────

    def load_graph(self, graph_id: str) -> None:
        self._active_graph_id = graph_id
        self._selected_scene_id = None
        self._load_active_graph()

    def _load_active_graph(self) -> None:
        if self.on_load_graph is None or self._active_graph_id is None:
            return
        try:
            self._graph_cache = self.on_load_graph(self._active_graph_id)
        except Exception:
            log.exception("Failed to load graph %s", self._active_graph_id)
            self._graph_cache = None
        if self._widget is not None:
            self._refresh()

    # ── Widget building (Kivy) ────────────────────────────

    def _build_widget(self) -> Any:
        from kivy.uix.boxlayout import BoxLayout
        from kivy.uix.scrollview import ScrollView
        from kivy.uix.label import Label
        from kivy.uix.button import Button

        root = BoxLayout(orientation="vertical")

        # Toolbar.
        toolbar = BoxLayout(orientation="horizontal", size_hint_y=None, height=36, spacing=4)
        self._title_label = Label(text=self.get_title(), size_hint_x=0.4)
        toolbar.add_widget(self._title_label)

        btn_gen_all_kf = Button(text="Gen all keyframes", size_hint_x=0.15)
        btn_gen_all_kf.bind(on_release=lambda *_: self._act_generate_all_keyframes())
        toolbar.add_widget(btn_gen_all_kf)

        btn_gen_all_clips = Button(text="Gen all clips", size_hint_x=0.15)
        btn_gen_all_clips.bind(on_release=lambda *_: self._act_generate_all_clips())
        toolbar.add_widget(btn_gen_all_clips)

        btn_compose = Button(text="Compose", size_hint_x=0.12)
        btn_compose.bind(on_release=lambda *_: self._act_compose())
        toolbar.add_widget(btn_compose)

        btn_add_scene = Button(text="+ Scene", size_hint_x=0.1)
        btn_add_scene.bind(on_release=lambda *_: self._act_add_scene())
        toolbar.add_widget(btn_add_scene)

        # Placeholder na view switcher (timeline / graph / score).
        # TODO: prawdziwy spinner — na razie label.
        toolbar.add_widget(Label(text=f"View: {self._view_mode}", size_hint_x=0.08))

        root.add_widget(toolbar)

        # Timeline strip.
        self._strip_scroll = ScrollView(size_hint=(1, 1))
        self._strip_container = BoxLayout(orientation="horizontal", size_hint_x=None,
                                           spacing=6, padding=6)
        self._strip_container.bind(
            minimum_width=lambda inst, val: setattr(self._strip_container, "width", val)
        )
        self._strip_scroll.add_widget(self._strip_container)
        root.add_widget(self._strip_scroll)

        return root

    def _refresh(self) -> None:
        if self._widget is None:
            return
        self._title_label.text = self.get_title()
        self._strip_container.clear_widgets()
        if self._graph_cache is None:
            return
        for scene in self._graph_cache.scenes:
            self._strip_container.add_widget(self._build_scene_tile(scene))

    def _build_scene_tile(self, scene) -> Any:
        """Mały kafelek reprezentujący scenę w stripie."""
        from kivy.uix.boxlayout import BoxLayout
        from kivy.uix.image import Image
        from kivy.uix.label import Label
        from kivy.uix.button import Button

        tile = BoxLayout(orientation="vertical", size_hint=(None, 1), width=180,
                          padding=4, spacing=2)

        # Thumbnail — keyframe jeśli jest, inaczej placeholder.
        if scene.artifacts.keyframe_path:
            tile.add_widget(Image(
                source=scene.artifacts.keyframe_path,
                size_hint_y=0.6,
                allow_stretch=True,
            ))
        else:
            tile.add_widget(Label(
                text="[no keyframe]",
                size_hint_y=0.6,
            ))

        # Metadata.
        tile.add_widget(Label(
            text=f"{scene.scene_id[:10]}\n"
                 f"{scene.duration_seconds:.1f}s · {scene.state.value}",
            size_hint_y=0.2,
        ))

        # Akcje.
        actions = BoxLayout(orientation="horizontal", size_hint_y=0.2, spacing=2)
        btn_kf = Button(text="KF")
        btn_kf.bind(on_release=lambda *_, sid=scene.scene_id: self._act_generate_keyframe(sid))
        actions.add_widget(btn_kf)

        btn_clip = Button(text="Clip")
        btn_clip.bind(on_release=lambda *_, sid=scene.scene_id: self._act_generate_clip(sid))
        actions.add_widget(btn_clip)

        tile.add_widget(actions)
        return tile

    # ── Actions ──────────────────────────────────────────

    def _act_generate_keyframe(self, scene_id: str) -> None:
        if self.on_generate_keyframe and self._active_graph_id:
            self.on_generate_keyframe(self._active_graph_id, scene_id)

    def _act_generate_clip(self, scene_id: str) -> None:
        if self.on_generate_clip and self._active_graph_id:
            self.on_generate_clip(self._active_graph_id, scene_id)

    def _act_generate_all_keyframes(self) -> None:
        if self._graph_cache is None or self.on_generate_keyframe is None:
            return
        for scene in self._graph_cache.scenes:
            self.on_generate_keyframe(self._active_graph_id, scene.scene_id)

    def _act_generate_all_clips(self) -> None:
        if self._graph_cache is None or self.on_generate_clip is None:
            return
        for scene in self._graph_cache.scenes:
            self.on_generate_clip(self._active_graph_id, scene.scene_id)

    def _act_compose(self) -> None:
        if self.on_compose and self._active_graph_id:
            self.on_compose(self._active_graph_id)

    def _act_add_scene(self) -> None:
        # TODO: dialog nowej sceny. Placeholder — po prostu event.
        log.info("Add scene requested for graph %s", self._active_graph_id)


# ─── Registration helper ────────────────────────────────────────

def register_scene_graph_editor(registry) -> None:
    from gui.panel_system import PanelRegistration, PanelMount, MountMode

    registry.register(PanelRegistration(
        panel_type="scene_graph_editor",
        display_name="Scene Graph Editor",
        factory=lambda pid, state: SceneGraphEditorPanel(pid, state),
        default_mount=PanelMount(
            mount_mode=MountMode.FLOATING,
            position=(50.0, 100.0),
            size=(800.0, 300.0),
        ),
        category="video",
        singleton=False,  # user może mieć wiele edytorów otwartych, np. AoD + Cisza Beta.
    ))
