"""
gui/dock_renderer.py
=====================

Główny widget renderujący layout paneli w Kivy.

Struktura:
    FloatLayout (root — relative positioning)
    ├── BoxLayout "dock_frame"           — sztywny layout dla DOCKED
    │   ├── BoxLayout "dock_left"
    │   ├── BoxLayout "dock_center"
    │   │   ├── BoxLayout "dock_top"
    │   │   ├── <chat / other center content>
    │   │   └── BoxLayout "dock_bottom"
    │   └── BoxLayout "dock_right"
    └── <floating_frames>                 — panele FLOATING/FLOATING_LOCKED
        (każdy z własnym headerem, drag, resize)

Pos_hint/size_hint używane wszędzie — layout respektuje zmiany rozmiaru okna,
działa cross-platform (desktop resizable + Android orientation change).

Fundamentalne zasady:
  - Separacja: DockRenderer nie wie CO jest w panelu (chat, video, cokolwiek),
    tylko GDZIE i JAK osadzić.
  - Fallback: panel który nie zwróci widgetu (CrashPanel, ExternalBackend)
    dostaje placeholder-message zamiast wybuchu.
  - "Wszystko można": user przeciąga floating, klika header żeby zwinąć,
    klika dock/undock żeby zmienić tryb.
  - Cross-platform: FloatLayout + pos_hint działa tak samo na desktop i
    Androidzie. Drag przez on_touch_down/move/up — domyślne Kivy touch-events.

Nie zależne od żadnego konkretnego typu panelu. Zależy tylko od IPanel.
"""

from __future__ import annotations

import logging
from typing import Any, Callable, Dict, Optional

from gui.panel_system import (
    IPanel, PanelMount, MountMode, DockPosition,
    PanelInstance, PanelManager,
)

log = logging.getLogger(__name__)


class DockLayoutRenderer:
    """
    Renderuje PanelManager jako widget hierarchii Kivy.

    Użycie:
        renderer = DockLayoutRenderer(panel_manager)
        root_widget = renderer.build()
        # osadź w App.build() jako root

        # Po każdej zmianie layoutu (mount, create, destroy):
        renderer.rebuild()

    Uwaga: `rebuild()` odłącza stare widgety i buduje od nowa. Legacy
    panele zachowują swój state (BoxLayout dziedziczą), ale jeśli mają
    wewnątrz canvas z bindami, mogą wymagać refresh() po rebuild.
    Adapter wywołuje refresh() w on_mount — więc to działa automatycznie.
    """

    def __init__(self, panel_manager: PanelManager) -> None:
        self._manager = panel_manager
        self._root: Any = None
        self._dock_slots: Dict[DockPosition, Any] = {}
        self._floating_container: Any = None
        # Map panel_id → widget frame (FloatingFrame lub DockedFrame)
        self._frames: Dict[str, Any] = {}

    # ─── Public ──────────────────────────────────────────

    def build(self) -> Any:
        """Tworzy hierarchię widgetów. Zwraca root."""
        log.info("DockLayoutRenderer.build() starting")
        self._root = self._build_root()
        log.info("DockLayoutRenderer: root FloatLayout created")
        self.rebuild()
        return self._root

    def rebuild(self) -> None:
        """Odnawia zawartość zgodnie z aktualnym PanelManager."""
        if self._root is None:
            self.build()
            return

        all_instances = list(self._manager.list_all())
        log.info("DockLayoutRenderer.rebuild(): %d panel instances to mount",
                 len(all_instances))

        # Clear
        for slot in self._dock_slots.values():
            slot.clear_widgets()
        if self._floating_container is not None:
            # Nie czyścimy root FloatLayout całego (byłoby zniszczeniem dock_frame),
            # tylko floating frames pamiętane w self._frames.
            for pid, frame in list(self._frames.items()):
                try:
                    inst = self._manager.get(pid)
                    # Jeśli panel był floating, odepnij go z root (dock_frame
                    # czyściliśmy wyżej przez clear_widgets slotów).
                    if (inst and inst.mount
                            and inst.mount.mount_mode != MountMode.DOCKED
                            and frame.parent is not None):
                        frame.parent.remove_widget(frame)
                except Exception:
                    log.exception("Failed to unmount old frame for %s", pid)
        self._frames.clear()

        # Re-populate
        mounted_count = 0
        for inst in all_instances:
            try:
                self._mount_instance(inst)
                mounted_count += 1
            except Exception:
                log.exception("Failed to mount %s", inst.panel.panel_id)

        log.info("DockLayoutRenderer.rebuild(): mounted %d/%d panels",
                 mounted_count, len(all_instances))

        # Safety net: jeśli nic się nie zmountowało, pokaż placeholder.
        # Zapobiega czarnemu ekranowi i informuje usera co się dzieje.
        if mounted_count == 0 and len(all_instances) == 0:
            log.warning("No panels to mount — showing empty-state placeholder")
            self._show_empty_placeholder()
        elif mounted_count == 0:
            log.error("Panels exist but none mounted — showing error placeholder")
            self._show_error_placeholder(len(all_instances))

    def _show_empty_placeholder(self) -> None:
        from kivy.uix.label import Label
        from kivy.uix.boxlayout import BoxLayout
        center = self._dock_slots.get(DockPosition.CENTER)
        if center is None:
            return
        center.clear_widgets()
        placeholder = Label(
            text=("[ ChatADHD ]\n\nNo panels to display.\n"
                  "Check live debug at http://localhost:8765/"),
            halign="center", valign="middle",
            color=(0.6, 0.6, 0.65, 1),
        )
        placeholder.bind(size=placeholder.setter("text_size"))
        center.add_widget(placeholder)

    def _show_error_placeholder(self, panel_count: int) -> None:
        from kivy.uix.label import Label
        center = self._dock_slots.get(DockPosition.CENTER)
        if center is None:
            return
        center.clear_widgets()
        placeholder = Label(
            text=(f"[ ChatADHD — Mount Error ]\n\n"
                  f"{panel_count} panels failed to mount.\n"
                  f"Check live debug at http://localhost:8765/\n"
                  f"for tracebacks."),
            halign="center", valign="middle",
            color=(1.0, 0.5, 0.4, 1),
        )
        placeholder.bind(size=placeholder.setter("text_size"))
        center.add_widget(placeholder)

    def on_panel_mounted(self, panel_id: str) -> None:
        """Hook do zawołania po PanelManager.create/change_mount/set_mode."""
        inst = self._manager.get(panel_id)
        if inst is None:
            return
        # Remove old frame jeśli istnieje.
        old = self._frames.pop(panel_id, None)
        if old is not None:
            try:
                parent = old.parent
                if parent is not None:
                    parent.remove_widget(old)
            except Exception:
                log.exception("Failed to remove old frame for %s", panel_id)
        self._mount_instance(inst)

    def on_panel_destroyed(self, panel_id: str) -> None:
        frame = self._frames.pop(panel_id, None)
        if frame is not None and frame.parent is not None:
            frame.parent.remove_widget(frame)

    # ─── Build internals ─────────────────────────────────

    def _build_root(self) -> Any:
        from kivy.uix.floatlayout import FloatLayout
        from kivy.uix.boxlayout import BoxLayout
        from kivy.graphics import Color, Rectangle

        root = FloatLayout()

        # Diagnostic: colorful background on root so we know SOMETHING paints.
        # Zostanie nakryte przez dock_frame i panele, ale gdy wszystko zawodzi
        # user widzi czerwone tło zamiast czarne.
        with root.canvas.before:
            Color(0.25, 0.05, 0.05, 1)  # ciemna czerwień
            root._debug_bg = Rectangle(pos=(0, 0), size=(10000, 10000))
        log.info("_build_root: FloatLayout created with debug red background")

        # Dock frame (base layer, covers 100% of FloatLayout)
        dock_frame = BoxLayout(
            orientation="horizontal",
            size_hint=(1, 1),
            pos_hint={"x": 0, "y": 0},
        )
        # Debug bg on dock_frame so we see it's taking Window area
        with dock_frame.canvas.before:
            Color(0.05, 0.05, 0.1, 1)  # ciemny granatowy = dock_frame żyje
            dock_frame._debug_bg = Rectangle(pos=dock_frame.pos, size=dock_frame.size)
        dock_frame.bind(
            pos=lambda inst, v: setattr(inst._debug_bg, "pos", v),
            size=lambda inst, v: setattr(inst._debug_bg, "size", v),
        )

        # Left
        self._dock_slots[DockPosition.LEFT] = self._make_slot(
            orientation="vertical", size_hint_x=None, width=0,
            debug_color=(0.1, 0.15, 0.1, 1), label="LEFT",
        )
        dock_frame.add_widget(self._dock_slots[DockPosition.LEFT])

        # Center (vertical split: top / center / bottom)
        center_col = BoxLayout(orientation="vertical", size_hint_x=1)
        self._dock_slots[DockPosition.TOP] = self._make_slot(
            orientation="horizontal", size_hint_y=None, height=0,
            debug_color=(0.15, 0.10, 0.15, 1), label="TOP",
        )
        self._dock_slots[DockPosition.CENTER] = self._make_slot(
            orientation="vertical", size_hint_y=1,
            debug_color=(0.08, 0.08, 0.14, 1), label="CENTER",
        )
        self._dock_slots[DockPosition.BOTTOM] = self._make_slot(
            orientation="horizontal", size_hint_y=None, height=0,
            debug_color=(0.15, 0.10, 0.15, 1), label="BOTTOM",
        )
        center_col.add_widget(self._dock_slots[DockPosition.TOP])
        center_col.add_widget(self._dock_slots[DockPosition.CENTER])
        center_col.add_widget(self._dock_slots[DockPosition.BOTTOM])
        dock_frame.add_widget(center_col)

        # Right
        self._dock_slots[DockPosition.RIGHT] = self._make_slot(
            orientation="vertical", size_hint_x=None, width=0,
            debug_color=(0.1, 0.15, 0.1, 1), label="RIGHT",
        )
        dock_frame.add_widget(self._dock_slots[DockPosition.RIGHT])

        root.add_widget(dock_frame)

        # Floating container — same FloatLayout; frames dodawane bezpośrednio.
        self._floating_container = root

        return root

    def _make_slot(self, *, debug_color, label, **kwargs) -> Any:
        """Tworzy slot BoxLayout z diagnostycznym tłem."""
        from kivy.uix.boxlayout import BoxLayout
        from kivy.graphics import Color, Rectangle

        slot = BoxLayout(**kwargs)
        slot._slot_label = label  # dla debugowania
        with slot.canvas.before:
            Color(*debug_color)
            slot._debug_bg = Rectangle(pos=slot.pos, size=slot.size)
        slot.bind(
            pos=lambda inst, v: setattr(inst._debug_bg, "pos", v),
            size=lambda inst, v: setattr(inst._debug_bg, "size", v),
        )
        return slot

    def _mount_instance(self, inst: PanelInstance) -> None:
        mount = inst.mount
        panel = inst.panel

        log.info("_mount_instance: panel_id=%s type=%s mode=%s dock_pos=%s size=%s",
                 panel.panel_id,
                 getattr(panel, "panel_type", "?"),
                 mount.mount_mode.value,
                 mount.dock_position.value if mount.dock_position else None,
                 mount.size)

        if mount.mount_mode == MountMode.HIDDEN:
            log.info("  → HIDDEN, skipping render")
            return

        try:
            frame = self._build_frame(panel, mount)
            self._frames[panel.panel_id] = frame
            log.info("  → frame built OK: %s", type(frame).__name__)
        except Exception:
            log.exception("  → FAILED to build frame for %s", panel.panel_id)
            return

        if mount.mount_mode == MountMode.DOCKED:
            slot = self._dock_slots.get(mount.dock_position)
            if slot is None:
                log.warning("  → unknown dock position %s; defaulting to CENTER",
                            mount.dock_position)
                slot = self._dock_slots[DockPosition.CENTER]
            # Rozszerz slot jeśli trzeba.
            self._ensure_slot_size(slot, mount)
            slot.add_widget(frame)
            log.info("  → added to dock slot, slot now has %d children",
                     len(slot.children))
        else:
            # FLOATING / FLOATING_LOCKED — dodajemy do root FloatLayout.
            self._floating_container.add_widget(frame)
            log.info("  → added to floating container")

    def _ensure_slot_size(self, slot: Any, mount: PanelMount) -> None:
        """Dostosowuje size_hint/size slotu żeby panel miał miejsce."""
        from kivy.uix.boxlayout import BoxLayout

        if not isinstance(slot, BoxLayout):
            return

        w, h = mount.size
        if slot.orientation == "vertical":
            # slot-y jako column (left/right/center-col)
            if slot.width < w:
                slot.width = w
        else:
            # slot-y jako row (top/bottom)
            if slot.height < h:
                slot.height = h

    # ─── Frame builders ─────────────────────────────────

    def _build_frame(self, panel: IPanel, mount: PanelMount) -> Any:
        if mount.mount_mode == MountMode.DOCKED:
            return self._build_docked_frame(panel, mount)
        return self._build_floating_frame(panel, mount)

    def _build_docked_frame(self, panel: IPanel, mount: PanelMount) -> Any:
        """Docked: prosty container z headerem."""
        from kivy.uix.boxlayout import BoxLayout

        try:
            from gui.base import C
            from kivy.metrics import dp, sp
        except ImportError:
            C = {"card": (0.14, 0.14, 0.18, 1), "text": (0.95, 0.95, 0.95, 1)}
            dp = lambda x: x
            sp = lambda x: x

        frame = BoxLayout(orientation="vertical")
        header = self._build_header(panel, mount, collapsible=True,
                                      movable=False)
        frame.add_widget(header)

        if not mount.collapsed:
            widget = self._get_panel_widget_safe(panel)
            frame.add_widget(widget)

        return frame

    def _build_floating_frame(self, panel: IPanel, mount: PanelMount) -> Any:
        """
        Floating frame: FloatLayout-child z pos_hint/size_hint.
        Touch-draggable (unless FLOATING_LOCKED).
        """
        return FloatingPanelFrame(
            panel=panel,
            mount=mount,
            manager=self._manager,
            renderer=self,
            get_widget_fn=self._get_panel_widget_safe,
        )

    def _build_header(self, panel: IPanel, mount: PanelMount, *,
                       collapsible: bool, movable: bool) -> Any:
        """Wspólny header dla docked i floating."""
        from kivy.uix.boxlayout import BoxLayout
        from kivy.uix.label import Label

        try:
            from gui.base import C, RBtn
            from kivy.metrics import dp, sp
            has_base = True
        except ImportError:
            from kivy.uix.button import Button as RBtn
            C = {"card": (0.14, 0.14, 0.18, 1),
                 "text": (0.95, 0.95, 0.95, 1),
                 "accent": (0.30, 0.55, 0.95, 1)}
            dp = lambda x: x
            sp = lambda x: x
            has_base = False

        header = BoxLayout(
            orientation="horizontal",
            size_hint_y=None,
            height=dp(28),
            spacing=dp(2),
            padding=dp(2),
        )

        # Title
        title = Label(
            text=panel.get_title(),
            color=C["text"], font_size=sp(10),
            size_hint_x=1, shorten=True, shorten_from="right",
        )
        header.add_widget(title)

        # Collapse
        if collapsible:
            btn_args = {"font_size": sp(9), "size_hint_x": None,
                        "width": dp(28)}
            if has_base:
                btn_args["bg"] = C["card"]
            collapse_btn = RBtn(
                text=("▸" if mount.collapsed else "▾"),
                **btn_args,
            )

            def _on_collapse(*_):
                self._manager.toggle_collapsed(panel.panel_id)
                self.on_panel_mounted(panel.panel_id)
            collapse_btn.bind(on_release=_on_collapse)
            header.add_widget(collapse_btn)

        # Mount-mode toggle
        btn_args2 = {"font_size": sp(9), "size_hint_x": None, "width": dp(44)}
        if has_base:
            btn_args2["bg"] = C["card"]
        mode_btn = RBtn(text=_mode_label(mount.mount_mode), **btn_args2)

        def _on_mode_toggle(*_):
            next_mode = _next_mount_mode(mount.mount_mode)
            self._manager.set_mode(panel.panel_id, next_mode)
            self.on_panel_mounted(panel.panel_id)
        mode_btn.bind(on_release=_on_mode_toggle)
        header.add_widget(mode_btn)

        # Hide (→ HIDDEN)
        btn_args3 = {"font_size": sp(9), "size_hint_x": None, "width": dp(28)}
        if has_base:
            btn_args3["bg"] = C["card"]
        hide_btn = RBtn(text="×", **btn_args3)

        def _on_hide(*_):
            self._manager.set_mode(panel.panel_id, MountMode.HIDDEN)
            self.on_panel_mounted(panel.panel_id)
        hide_btn.bind(on_release=_on_hide)
        header.add_widget(hide_btn)

        return header

    def _get_panel_widget_safe(self, panel: IPanel) -> Any:
        """Pobiera widget panelu z fallbackiem na placeholder gdy None."""
        from kivy.uix.label import Label

        try:
            w = panel.get_widget()
        except Exception:
            log.exception("Panel %s get_widget failed", panel.panel_id)
            w = None

        if w is None:
            try:
                from gui.base import C
            except ImportError:
                C = {"dim": (0.55, 0.55, 0.60, 1)}
            return Label(
                text=f"[Panel {panel.panel_type} has no widget]",
                color=C["dim"],
            )
        return w


# ─── FloatingPanelFrame ─────────────────────────────────────────

class FloatingPanelFrame:
    """
    Wrapper dla pływającego okna w Kivy. Używa FloatLayout-child
    pozycjonowanego przez pos/size (absolute).

    Drag: gdy nie FLOATING_LOCKED, header łapie touch events i przesuwa
    cały frame.

    Resize: wbudowany resize-handle w prawym-dolnym rogu (chyba że locked).
    """

    def __new__(cls, panel: IPanel, mount: PanelMount,
                manager: PanelManager, renderer: DockLayoutRenderer,
                get_widget_fn):
        """
        Hack: __new__ zamiast __init__ — zwraca gotowy Kivy BoxLayout
        (FloatLayout-child), a nie instancję FloatingPanelFrame. Wrapper
        jest logiką konstrukcji, widget jest Kivy widget.
        """
        from kivy.uix.boxlayout import BoxLayout
        from kivy.graphics import Color, Line, Rectangle

        try:
            from gui.base import C
            from kivy.metrics import dp
        except ImportError:
            C = {"bg": (0.06, 0.06, 0.08, 1),
                 "card": (0.14, 0.14, 0.18, 1),
                 "accent": (0.30, 0.55, 0.95, 1)}
            dp = lambda x: x

        # Root frame jest BoxLayout z size_hint=(None, None), pos/size
        # ustawione bezwzględnie. FloatLayout akceptuje to bez problemu.
        frame = BoxLayout(
            orientation="vertical",
            size_hint=(None, None),
            size=mount.size,
            pos=(mount.position[0], mount.position[1]),
        )

        # Background + border (żeby floating odróżniało się od tła apki).
        with frame.canvas.before:
            Color(*C["bg"])
            frame._bg_rect = Rectangle(pos=frame.pos, size=frame.size)
            Color(*C["accent"])
            frame._border = Line(rectangle=(frame.x, frame.y,
                                              frame.width, frame.height),
                                   width=1.2)
        frame.bind(
            pos=lambda inst, v: (
                setattr(frame._bg_rect, "pos", inst.pos),
                frame._border.__setattr__("rectangle",
                    (inst.x, inst.y, inst.width, inst.height))
            ),
            size=lambda inst, v: (
                setattr(frame._bg_rect, "size", inst.size),
                frame._border.__setattr__("rectangle",
                    (inst.x, inst.y, inst.width, inst.height))
            ),
        )

        # Header (clickable dla drag jeśli nie locked)
        header = renderer._build_header(panel, mount, collapsible=True,
                                          movable=(mount.mount_mode
                                                    != MountMode.FLOATING_LOCKED))

        # Drag handler przypięty do headera
        if mount.mount_mode == MountMode.FLOATING:
            _attach_drag(header, frame, manager, panel.panel_id)

        frame.add_widget(header)

        # Content
        if not mount.collapsed:
            content = get_widget_fn(panel)
            # Odepnij z poprzedniego parent jeśli trzeba (legacy panels
            # mogą być przypięte).
            if hasattr(content, "parent") and content.parent is not None:
                try:
                    content.parent.remove_widget(content)
                except Exception:
                    pass
            frame.add_widget(content)

        return frame


# ─── Drag helper ────────────────────────────────────────────────

def _attach_drag(header: Any, frame: Any, manager: PanelManager,
                  panel_id: str) -> None:
    """Prosty drag: łap on_touch_down w headerze, przesuwaj frame."""

    state = {"dragging": False, "dx": 0, "dy": 0}

    original_down = header.on_touch_down
    original_move = header.on_touch_move
    original_up = header.on_touch_up

    def _on_down(touch):
        if header.collide_point(*touch.pos):
            state["dragging"] = True
            state["dx"] = frame.x - touch.x
            state["dy"] = frame.y - touch.y
            touch.grab(header)
            return True
        return original_down(touch) if original_down else False

    def _on_move(touch):
        if touch.grab_current is header and state["dragging"]:
            frame.x = touch.x + state["dx"]
            frame.y = touch.y + state["dy"]
            return True
        return original_move(touch) if original_move else False

    def _on_up(touch):
        if touch.grab_current is header:
            touch.ungrab(header)
            if state["dragging"]:
                state["dragging"] = False
                # Persist nowej pozycji do mountu.
                inst = manager.get(panel_id)
                if inst is not None:
                    inst.mount.position = (float(frame.x), float(frame.y))
                return True
        return original_up(touch) if original_up else False

    header.on_touch_down = _on_down
    header.on_touch_move = _on_move
    header.on_touch_up = _on_up


# ─── Label helpers ──────────────────────────────────────────────

def _mode_label(mode: MountMode) -> str:
    return {
        MountMode.DOCKED: "dock",
        MountMode.FLOATING: "float",
        MountMode.FLOATING_LOCKED: "lock",
        MountMode.HIDDEN: "hide",
    }.get(mode, "?")


def _next_mount_mode(mode: MountMode) -> MountMode:
    """Cykl: DOCKED → FLOATING → FLOATING_LOCKED → DOCKED."""
    cycle = [MountMode.DOCKED, MountMode.FLOATING, MountMode.FLOATING_LOCKED]
    try:
        idx = cycle.index(mode)
    except ValueError:
        return MountMode.DOCKED
    return cycle[(idx + 1) % len(cycle)]
