"""
gui/panel_system.py
====================

Generyczny panel-system dla ChatADHD 0.9.0.

Invarianty (patrz docs/ARCHITECTURE_v0.9.0.md sekcja A):
  1. Każdy panel ma unikalne panel_id i panel_type.
  2. State serializowalny do JSON przez to_dict/from_dict.
  3. Cztery tryby osadzenia (MountMode).
  4. Komunikacja wyłącznie przez event bus.
  5. Crash jednego panelu nie zabija systemu.

Zasady z KODEKSU:
  - Separacja rzeczywistości (stan panelu) od logiki (panel-system decyduje o layout).
  - Invariant IPanel wystawia zapytywalność; każdy panel queryable co do swojego state.
  - Fallback z metadanymi: gdy panel padnie, state zachowany, widok = "crash-view".
  - Wszystko można (user konfiguruje layout), nic nie trzeba (domyślne presety działają).
"""

from __future__ import annotations

import json
import logging
import uuid
from abc import ABC, abstractmethod
from dataclasses import dataclass, field, asdict
from enum import Enum
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple, Type

log = logging.getLogger(__name__)


# ─── Mount modes ────────────────────────────────────────────────

class MountMode(str, Enum):
    """
    Cztery tryby osadzenia panelu.

    DOCKED           : w dock-layoucie głównego okna (pozycja zarządzana przez layout manager)
    FLOATING         : oderwane okno, ruchome, zmienialny rozmiar
    FLOATING_LOCKED  : oderwane okno, pozycja i rozmiar zamrożone
    HIDDEN           : niewidoczne, ale state żyje (instancja zachowana)
    """
    DOCKED = "DOCKED"
    FLOATING = "FLOATING"
    FLOATING_LOCKED = "FLOATING_LOCKED"
    HIDDEN = "HIDDEN"


class DockPosition(str, Enum):
    """Pozycja w dock-layoucie. Używane tylko gdy mount_mode = DOCKED."""
    CENTER = "center"
    LEFT = "left"
    RIGHT = "right"
    TOP = "top"
    BOTTOM = "bottom"


# ─── Panel state ────────────────────────────────────────────────

@dataclass
class PanelMount:
    """
    Stan osadzenia panelu w layoucie. Ortogonalny do panel_state.

    panel_state = co panel zawiera (konwersacja, klip, graf).
    PanelMount = gdzie/jak panel jest pokazany.
    """
    mount_mode: MountMode = MountMode.DOCKED
    dock_position: DockPosition = DockPosition.CENTER
    position: Tuple[float, float] = (0.0, 0.0)  # dla FLOATING* — px w oknie głównym
    size: Tuple[float, float] = (400.0, 300.0)   # dla FLOATING* — w, h
    collapsed: bool = False
    z_order: int = 0  # dla FLOATING — kolejność on-top

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        d["mount_mode"] = self.mount_mode.value
        d["dock_position"] = self.dock_position.value
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "PanelMount":
        return cls(
            mount_mode=MountMode(d.get("mount_mode", "DOCKED")),
            dock_position=DockPosition(d.get("dock_position", "center")),
            position=tuple(d.get("position", (0.0, 0.0))),
            size=tuple(d.get("size", (400.0, 300.0))),
            collapsed=bool(d.get("collapsed", False)),
            z_order=int(d.get("z_order", 0)),
        )


# ─── IPanel interface ───────────────────────────────────────────

class IPanel(ABC):
    """
    Interfejs każdego panelu w systemie.

    Żadne dynamic_cast ani isinstance w innych modułach — jeśli coś potrzebuje
    nowej metody, dodajemy ją tutaj. Zgodne z kodeksem Loom SDK.
    """

    # Metadata — nie zmieniają się po utworzeniu panelu.
    panel_id: str
    panel_type: str  # nazwa typu, np. "chat", "video_preview", "scene_graph_editor"

    @abstractmethod
    def get_widget(self) -> Any:
        """Zwraca Kivy widget panelu. Może być wołane wielokrotnie — zwraca ten sam obiekt."""
        ...

    @abstractmethod
    def get_title(self) -> str:
        """Tytuł wyświetlany w headerze panelu. Może być dynamiczny."""
        ...

    @abstractmethod
    def get_state(self) -> Dict[str, Any]:
        """
        Zwraca serializowalny stan panelu (bez mount info — to osobno).
        Musi być JSON-safe: tylko dict/list/str/int/float/bool/None.
        """
        ...

    @abstractmethod
    def set_state(self, state: Dict[str, Any]) -> None:
        """Odtwarza stan panelu z dict. Wywoływane przy load layoutu."""
        ...

    def on_mount(self, mount: PanelMount) -> None:
        """Hook wywoływany gdy panel zostaje zamontowany. Domyślnie no-op."""
        pass

    def on_unmount(self) -> None:
        """Hook wywoływany gdy panel zostaje odmontowany. Domyślnie no-op."""
        pass

    def on_event(self, event_name: str, payload: Dict[str, Any]) -> None:
        """
        Hook dla event busa. Panel subskrybuje zdarzenia przez PanelRegistry.
        Domyślnie no-op — panel który nie słucha nie potrzebuje reagować.
        """
        pass

    def query(self, key: str) -> Any:
        """
        Zapytywalność invariantu.
        Panel odpowiada na proste zapytania (np. "active_item_id", "item_count").
        Jeśli panel nie rozpoznaje klucza, zwraca None.
        """
        return None


# ─── Panel factory & registry ───────────────────────────────────

PanelFactory = Callable[[str, Dict[str, Any]], IPanel]
#                         ^id   ^state (opcjonalny initial state)


@dataclass
class PanelRegistration:
    """Rejestracja typu panelu w PanelRegistry. Factory tworzy instancje."""
    panel_type: str
    display_name: str  # nazwa w menu Add Panel
    factory: PanelFactory
    default_mount: PanelMount = field(default_factory=PanelMount)
    category: str = "general"  # grupowanie w UI: "chat", "video", "research", ...
    singleton: bool = False  # jeśli True, tylko jedna instancja naraz


class PanelRegistry:
    """
    Rejestr typów paneli. Każdy moduł (chat, video, memory, ...) rejestruje swoje
    typy przy starcie aplikacji. Panel-system nie wie nic o konkretnych typach.

    Użycie:
        registry = PanelRegistry()
        registry.register(PanelRegistration(
            panel_type="video_preview",
            display_name="Video Preview",
            factory=lambda pid, state: VideoPreviewPanel(pid, state),
            category="video",
        ))
    """

    def __init__(self) -> None:
        self._types: Dict[str, PanelRegistration] = {}

    def register(self, reg: PanelRegistration) -> None:
        if reg.panel_type in self._types:
            log.warning("Panel type '%s' already registered, overwriting.", reg.panel_type)
        self._types[reg.panel_type] = reg

    def unregister(self, panel_type: str) -> None:
        self._types.pop(panel_type, None)

    def list_types(self, category: Optional[str] = None) -> List[PanelRegistration]:
        regs = list(self._types.values())
        if category is not None:
            regs = [r for r in regs if r.category == category]
        return regs

    def get(self, panel_type: str) -> Optional[PanelRegistration]:
        return self._types.get(panel_type)

    def create(self, panel_type: str, panel_id: Optional[str] = None,
               initial_state: Optional[Dict[str, Any]] = None) -> IPanel:
        """
        Tworzy nową instancję panelu.
        Fallback: jeśli typ nieznany, raise — ale panel-manager ma wtedy
        obowiązek zastąpić go CrashPanel z metadanymi (patrz layout_manager.py).
        """
        reg = self._types.get(panel_type)
        if reg is None:
            raise UnknownPanelTypeError(f"Unknown panel type: {panel_type}")
        pid = panel_id or f"{panel_type}_{uuid.uuid4().hex[:8]}"
        state = initial_state or {}
        panel = reg.factory(pid, state)
        # Sanity check
        assert panel.panel_id == pid, f"Panel factory set wrong panel_id: {panel.panel_id} != {pid}"
        assert panel.panel_type == panel_type, f"Panel factory set wrong panel_type: {panel.panel_type} != {panel_type}"
        return panel


class UnknownPanelTypeError(Exception):
    """Rzucane gdy PanelRegistry.create dostaje nieznany panel_type."""
    pass


# ─── Crash-panel (fallback) ─────────────────────────────────────

class CrashPanel(IPanel):
    """
    Panel-placeholder zastępujący panel który padł lub nie mógł być utworzony.
    Zachowuje oryginalny panel_id i oryginalny state (żeby user mógł odzyskać
    dane po naprawie, lub wyeksportować state do buga).
    """
    panel_type = "_crash"

    def __init__(self, panel_id: str, original_type: str,
                 original_state: Dict[str, Any], error: str) -> None:
        self.panel_id = panel_id
        self._original_type = original_type
        self._original_state = original_state
        self._error = error
        self._widget = None

    def get_widget(self) -> Any:
        # Lazy import Kivy widget — panel-system sam w sobie nie zależy od Kivy.
        if self._widget is None:
            self._widget = self._build_widget()
        return self._widget

    def _build_widget(self) -> Any:
        # Lazy import — panel_system.py może być testowany bez Kivy.
        from kivy.uix.boxlayout import BoxLayout
        from kivy.uix.label import Label
        from kivy.uix.button import Button

        root = BoxLayout(orientation="vertical", padding=10, spacing=5)
        root.add_widget(Label(
            text=f"[b]Panel crashed[/b]\n"
                 f"Type: {self._original_type}\n"
                 f"ID: {self.panel_id}\n"
                 f"Error: {self._error}",
            markup=True,
        ))
        # Kopiuj state do schowka — dla raportu buga.
        btn_copy = Button(text="Copy state to clipboard", size_hint_y=None, height=36)
        btn_copy.bind(on_release=lambda *_: self._copy_state())
        root.add_widget(btn_copy)
        btn_restart = Button(text="Retry", size_hint_y=None, height=36)
        btn_restart.bind(on_release=lambda *_: self._retry())
        root.add_widget(btn_restart)
        return root

    def _copy_state(self) -> None:
        try:
            from kivy.core.clipboard import Clipboard
            Clipboard.copy(json.dumps({
                "panel_id": self.panel_id,
                "panel_type": self._original_type,
                "state": self._original_state,
                "error": self._error,
            }, indent=2))
        except Exception as e:
            log.exception("Failed to copy crash state to clipboard: %s", e)

    def _retry(self) -> None:
        # Wysyła event — panel-manager zdecyduje czy próbować ponownie utworzyć
        # oryginalny typ. Implementacja w layout_manager.py.
        log.info("CrashPanel %s retry requested", self.panel_id)
        try:
            from engine.events import bus
            bus.emit("panel:crash:retry", {
                "panel_id": self.panel_id,
                "panel_type": self._original_type,
                "state": self._original_state,
            })
        except ImportError:
            log.warning("engine.events not available; retry has no effect")

    def get_title(self) -> str:
        return f"[Crashed] {self._original_type}"

    def get_state(self) -> Dict[str, Any]:
        # Zachowujemy oryginalny state żeby nie został utracony przy zapisie layoutu.
        return {
            "_crashed": True,
            "_original_type": self._original_type,
            "_original_state": self._original_state,
            "_error": self._error,
        }

    def set_state(self, state: Dict[str, Any]) -> None:
        self._original_state = state.get("_original_state", {})
        self._error = state.get("_error", "unknown")


# ─── Panel-manager ──────────────────────────────────────────────

@dataclass
class PanelInstance:
    """Zarejestrowana instancja panelu + jej aktualny mount-state."""
    panel: IPanel
    mount: PanelMount

    def to_dict(self) -> Dict[str, Any]:
        return {
            "panel_id": self.panel.panel_id,
            "panel_type": self.panel.panel_type,
            "panel_state": self.panel.get_state(),
            "mount": self.mount.to_dict(),
        }


class PanelManager:
    """
    Zarządza *instancjami* paneli (PanelRegistry zarządza *typami*).

    Operacje:
      - create(panel_type) → nowa instancja
      - destroy(panel_id)
      - change_mount(panel_id, new_mount)
      - get(panel_id) → PanelInstance
      - list() → wszystkie aktywne
      - snapshot() → wszystko jako JSON-dict (dla LayoutManager)
      - restore(snapshot) → odtwarza z JSON-dict

    Wszystkie panele tutaj w jednym miejscu — pojedyncze źródło prawdy.
    """

    def __init__(self, registry: PanelRegistry) -> None:
        self._registry = registry
        self._instances: Dict[str, PanelInstance] = {}

    def create(self, panel_type: str, *,
               panel_id: Optional[str] = None,
               initial_state: Optional[Dict[str, Any]] = None,
               mount: Optional[PanelMount] = None) -> PanelInstance:
        log.info("PanelManager.create: type=%s id=%s mount=%s",
                  panel_type, panel_id,
                  mount.mount_mode.value if mount else "default")
        reg = self._registry.get(panel_type)
        if reg is None:
            log.error("PanelManager.create: unknown panel type: %s "
                       "(available: %s)",
                       panel_type, sorted(self._registry._registrations.keys()))
            raise UnknownPanelTypeError(panel_type)

        # Singleton guard
        if reg.singleton:
            existing = [pi for pi in self._instances.values()
                         if pi.panel.panel_type == panel_type]
            if existing:
                log.info("PanelManager.create: singleton hit, returning %s",
                          existing[0].panel.panel_id)
                return existing[0]

        # Create panel — z fallbackiem do CrashPanel gdy factory padnie.
        try:
            panel = self._registry.create(panel_type, panel_id=panel_id,
                                          initial_state=initial_state)
            log.info("PanelManager.create: panel built OK, id=%s",
                      panel.panel_id)
        except Exception as e:
            log.exception("PanelManager.create: factory raised for type=%s "
                           "— building CrashPanel fallback", panel_type)
            pid = panel_id or f"{panel_type}_crash_{uuid.uuid4().hex[:8]}"
            panel = CrashPanel(pid, panel_type, initial_state or {}, str(e))

        actual_mount = mount or PanelMount(**asdict(reg.default_mount))
        instance = PanelInstance(panel=panel, mount=actual_mount)
        self._instances[panel.panel_id] = instance

        try:
            panel.on_mount(actual_mount)
        except Exception:
            log.exception("PanelManager.create: panel.on_mount failed for %s",
                           panel.panel_id)

        return instance

    def destroy(self, panel_id: str) -> None:
        log.info("PanelManager.destroy: %s", panel_id)
        inst = self._instances.pop(panel_id, None)
        if inst is None:
            log.warning("PanelManager.destroy: not found: %s", panel_id)
            return
        try:
            inst.panel.on_unmount()
        except Exception:
            log.exception("Panel %s on_unmount failed", panel_id)

    def change_mount(self, panel_id: str, new_mount: PanelMount) -> None:
        inst = self._instances.get(panel_id)
        if inst is None:
            log.warning("change_mount: panel %s not found", panel_id)
            return
        log.info("PanelManager.change_mount: %s %s → %s",
                  panel_id,
                  inst.mount.mount_mode.value,
                  new_mount.mount_mode.value)
        inst.mount = new_mount
        try:
            inst.panel.on_mount(new_mount)
        except Exception:
            log.exception("Panel %s on_mount (re-mount) failed", panel_id)

    def set_mode(self, panel_id: str, mode: MountMode) -> None:
        inst = self._instances.get(panel_id)
        if inst is None:
            log.warning("set_mode: panel %s not found", panel_id)
            return
        log.info("PanelManager.set_mode: %s → %s", panel_id, mode.value)
        new_mount = PanelMount(
            mount_mode=mode,
            dock_position=inst.mount.dock_position,
            position=inst.mount.position,
            size=inst.mount.size,
            collapsed=inst.mount.collapsed,
            z_order=inst.mount.z_order,
        )
        self.change_mount(panel_id, new_mount)

    def toggle_collapsed(self, panel_id: str) -> None:
        inst = self._instances.get(panel_id)
        if inst is None:
            log.warning("toggle_collapsed: panel %s not found", panel_id)
            return
        inst.mount.collapsed = not inst.mount.collapsed
        log.info("PanelManager.toggle_collapsed: %s → %s",
                  panel_id, inst.mount.collapsed)

    def get(self, panel_id: str) -> Optional[PanelInstance]:
        return self._instances.get(panel_id)

    def list_all(self) -> List[PanelInstance]:
        return list(self._instances.values())

    def list_by_mode(self, mode: MountMode) -> List[PanelInstance]:
        return [pi for pi in self._instances.values() if pi.mount.mount_mode == mode]

    def snapshot(self) -> List[Dict[str, Any]]:
        """Zrzut wszystkich paneli jako lista dictów — do zapisu layoutu."""
        out: List[Dict[str, Any]] = []
        for inst in self._instances.values():
            try:
                out.append(inst.to_dict())
            except Exception:
                log.exception("Panel %s snapshot failed; skipping", inst.panel.panel_id)
        return out

    def restore(self, snapshot: List[Dict[str, Any]]) -> None:
        """
        Odtwarza panele z snapshotu. Niszczy obecne panele.
        Panele z nieznanym typem dostają CrashPanel zamiast oryginału — dane zachowane.
        """
        # Destroy current
        for pid in list(self._instances.keys()):
            self.destroy(pid)

        for entry in snapshot:
            panel_id = entry.get("panel_id")
            panel_type = entry.get("panel_type")
            panel_state = entry.get("panel_state", {})
            mount_dict = entry.get("mount", {})
            mount = PanelMount.from_dict(mount_dict)

            # Special case: entry z _crashed=True oznacza że w poprzedniej sesji
            # ten panel też padł. Zachowujemy CrashPanel.
            if panel_state.get("_crashed"):
                panel = CrashPanel(
                    panel_id,
                    panel_state.get("_original_type", "unknown"),
                    panel_state.get("_original_state", {}),
                    panel_state.get("_error", "from-previous-session"),
                )
                self._instances[panel_id] = PanelInstance(panel=panel, mount=mount)
                continue

            try:
                self.create(panel_type, panel_id=panel_id,
                            initial_state=panel_state, mount=mount)
            except Exception as e:
                log.exception("Failed to restore panel %s (%s)", panel_id, panel_type)
                # Dodaj CrashPanel ręcznie, create() już to robi dla failure w factory,
                # ale tu chwytamy failure w create() samo (np. UnknownPanelType).
                panel = CrashPanel(panel_id or "unknown", panel_type or "unknown",
                                   panel_state, str(e))
                self._instances[panel.panel_id] = PanelInstance(panel=panel, mount=mount)
