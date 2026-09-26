"""
gui/legacy_adapter.py
======================

Adapter dla istniejących paneli v0.07.x do nowego panel-systemu 0.9.0.

Istniejące panele (ConvPanel, ChatPanel, MemoryPanel, GraphExplorerPanel,
ImportPanel, VoicePanel, GithubPanel) dziedziczą po BoxLayout i mają:
  - metodę toggle() / open() / close()
  - atrybut _visible (bool)
  - width / size_hint_x do zwijania (slide-out pattern)
  - metodę refresh() do re-renderowania

Adapter wystawia je przez IPanel bez modyfikacji ich kodu.
Zgodnie z kodeksem: "Separacja rzeczywistości od decyzji" — istniejące
panele to RZECZYWISTOŚĆ (zastane), adapter to DECYZJA (jak je zintegrować).

Żadnych dynamic_cast. Adapter używa znanych konwencji (toggle/_visible)
przez duck-typing, z fallbackiem gdy metoda niedostępna.
"""

from __future__ import annotations

import logging
from typing import Any, Callable, Dict, Optional

from gui.panel_system import IPanel, PanelMount, MountMode

log = logging.getLogger(__name__)


class LegacyPanelAdapter(IPanel):
    """
    Owija dowolny istniejący panel v0.07.x w IPanel.

    Użycie:
        conv_panel = ConvPanel(engine, on_select=...)  # istniejący panel
        adapter = LegacyPanelAdapter(
            panel_id="conv_list_main",
            panel_type="conversation_list",
            legacy_widget=conv_panel,
            title="Conversations",
        )
        panel_manager._instances[adapter.panel_id] = PanelInstance(adapter, mount)

    Adapter NIE modyfikuje legacy_widget. Wszystko co robi to:
      - wystawia widget przez get_widget()
      - deleguje refresh() gdy dostępne
      - reaguje na mount/unmount przez toggle/open/close

    State: adapter zapisuje tylko swój panel_id/type. Legacy widget trzyma
    własny state wewnętrznie (w ConvPanel._convs, etc.) — adapter nie próbuje
    tego serializować. Po restore apki legacy widget musi się sam odbudować
    z engine/db (co robi naturalnie przez refresh()).
    """

    def __init__(self, panel_id: str, panel_type: str,
                 legacy_widget: Any,
                 title: str = "",
                 custom_state_fns: Optional[Dict[str, Callable]] = None):
        self.panel_id = panel_id
        self.panel_type = panel_type
        self._widget = legacy_widget
        self._title = title or panel_type

        # Optional custom state getter/setter — jeśli konkretny panel ma
        # coś wartego serializacji (np. current scroll position), przekazujemy.
        fns = custom_state_fns or {}
        self._get_state_fn: Optional[Callable] = fns.get("get_state")
        self._set_state_fn: Optional[Callable] = fns.get("set_state")

    def get_widget(self) -> Any:
        return self._widget

    def get_title(self) -> str:
        # Jeśli legacy panel sam ma tytuł, preferujemy dynamiczny.
        if hasattr(self._widget, "get_title"):
            try:
                return str(self._widget.get_title())
            except Exception:
                pass
        return self._title

    def get_state(self) -> Dict[str, Any]:
        if self._get_state_fn is not None:
            try:
                return dict(self._get_state_fn(self._widget))
            except Exception:
                log.exception("custom get_state failed for %s", self.panel_id)
        return {}

    def set_state(self, state: Dict[str, Any]) -> None:
        if self._set_state_fn is not None:
            try:
                self._set_state_fn(self._widget, state)
            except Exception:
                log.exception("custom set_state failed for %s", self.panel_id)

    def on_mount(self, mount: PanelMount) -> None:
        """
        Przy mount: wywołujemy open() jeśli panel to obsługuje i mount != HIDDEN.
        Jeśli HIDDEN: close(). Jeśli metoda nie istnieje (np. ChatPanel który
        nie ma toggle), nic nie robimy — panel jest zawsze widoczny gdy
        osadzony przez dock-layout.
        """
        if mount.mount_mode == MountMode.HIDDEN:
            self._try("close")
            return
        self._try("open")
        # Po otwarciu odśwież — legacy pattern z main.py.
        self._try("refresh")

    def on_unmount(self) -> None:
        """Przy destroy: close() jeśli dostępne. Zatrzymuje watki, canvas itp."""
        self._try("close")

    def on_event(self, event_name: str, payload: Dict[str, Any]) -> None:
        """
        Legacy panele reagują na eventy bus'a bezpośrednio (subskrybują się
        w konstruktorze, np. ChatPanel słucha MSG_CREATED). Adapter NIE dubluje
        subskrypcji — to byłoby źródło podwójnych wywołań.

        Ale jeśli konkretny panel ma handle_event() (nowsza konwencja), deleguj.
        """
        if hasattr(self._widget, "on_event"):
            try:
                self._widget.on_event(event_name, payload)
            except Exception:
                log.exception("legacy on_event failed for %s", self.panel_id)

    def query(self, key: str) -> Any:
        """Deleguj do legacy widget jeśli ma query()."""
        if hasattr(self._widget, "query"):
            try:
                return self._widget.query(key)
            except Exception:
                return None
        # Znane klucze z legacy convention
        if key == "visible":
            return bool(getattr(self._widget, "_visible", True))
        return None

    # ─── Helpers ──────────────────────────────────────────

    def _try(self, method_name: str) -> None:
        """Wywołaj metodę jeśli istnieje. Nigdy nie rzuca."""
        fn = getattr(self._widget, method_name, None)
        if fn is None or not callable(fn):
            return
        try:
            fn()
        except Exception:
            log.exception("legacy %s.%s() failed", self.panel_id, method_name)


# ─── Registration helper for all legacy panels ──────────────────

def register_legacy_panels(registry, app) -> None:
    """
    Rejestruje wszystkie legacy panele z 0.07.x w PanelRegistry.

    Wywołać z main.py PO utworzeniu engines i panels w starym stylu, np:

        app.conv_panel = ConvPanel(app.engine, on_select=app._on_conv_select)
        app.memory_panel = MemoryPanel(app.memory)
        ...
        register_legacy_panels(panel_registry, app)

    Rejestrujemy factory które zwracają *już istniejące* instancje (singleton
    wzorzec). Konsekwencja: w MVP reformy UI legacy panele są singletons —
    nie można mieć dwóch ConvPanel. To jest świadome uproszczenie — pełna
    multi-instancja wymaga refaktoru samych legacy paneli.
    """
    from gui.panel_system import PanelRegistration

    def _make_factory(widget, panel_type: str, title: str):
        """Closure factory zwracający ten sam adapter za każdym razem (singleton)."""
        def factory(pid: str, state: Dict[str, Any]) -> IPanel:
            return LegacyPanelAdapter(
                panel_id=pid,
                panel_type=panel_type,
                legacy_widget=widget,
                title=title,
            )
        return factory

    legacy_panels = [
        # (attr_name_on_app, panel_type, display_name, category, default_mount)
        ("chat_panel", "chat", "Chat", "chat",
         PanelMount(mount_mode=MountMode.DOCKED, dock_position=None)),
        ("conv_panel", "conversation_list", "Conversations", "chat",
         PanelMount(mount_mode=MountMode.DOCKED)),
        ("memory_panel", "memory_tree", "Memory", "research",
         PanelMount(mount_mode=MountMode.DOCKED)),
        ("graph_panel", "graph_viz", "Graph Explorer", "research",
         PanelMount(mount_mode=MountMode.HIDDEN)),
    ]

    for attr, ptype, pname, cat, mount in legacy_panels:
        widget = getattr(app, attr, None)
        if widget is None:
            log.info("Legacy panel '%s' not found on app; skipping", attr)
            continue

        # Fix dock_position gdy None (DOCKED wymaga position).
        if mount.mount_mode == MountMode.DOCKED:
            from gui.panel_system import DockPosition
            if mount.dock_position is None:
                mount = PanelMount(
                    mount_mode=MountMode.DOCKED,
                    dock_position=DockPosition.CENTER,
                    size=mount.size,
                )

        registry.register(PanelRegistration(
            panel_type=ptype,
            display_name=pname,
            factory=_make_factory(widget, ptype, pname),
            default_mount=mount,
            category=cat,
            singleton=True,  # legacy = jedna instancja
        ))
        log.info("Registered legacy panel: %s → %s", attr, ptype)
