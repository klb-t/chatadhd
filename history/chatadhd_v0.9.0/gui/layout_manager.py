"""
gui/layout_manager.py
======================

Zarządzanie layoutami paneli — persistence, presety, apply/export.

Layout = kompletny snapshot PanelManager.
Preset = nazwany layout zapisany w data/presets/layouts/<name>.json.

Presety wbudowane w wersję (shipped_presets):
  - minimal         : tylko chat + lista czatów (domyślne po świeżej instalacji)
  - classic_0.07    : layout z 0.07.x (ciągłość dla istniejących userów)
  - aod_video       : tryb produkcji AoD (scene graph + preview + portfolio)
  - cisza_beta      : tryb ekranizacji Ciszy Beta
  - research        : chat + memory + graph_viz
  - dev             : wszystko włączone

Zgodność z kodeksem: presety = DANE, layout_manager = LOGIKA. Presety są
w YAML/JSON niezależnie od kodu, można je eksportować/importować, shareować.
"""

from __future__ import annotations

import json
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional

from gui.panel_system import (
    PanelManager,
    PanelMount,
    MountMode,
    DockPosition,
)

log = logging.getLogger(__name__)

LAYOUT_VERSION = 1


@dataclass
class Layout:
    """Pełny layout — nazwa, wersja, lista snapshotów paneli."""
    name: str
    panels: List[Dict[str, Any]]
    version: int = LAYOUT_VERSION
    description: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return {
            "layout_version": self.version,
            "layout_name": self.name,
            "description": self.description,
            "panels": self.panels,
        }

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "Layout":
        return cls(
            name=d.get("layout_name", "unnamed"),
            panels=d.get("panels", []),
            version=int(d.get("layout_version", LAYOUT_VERSION)),
            description=d.get("description", ""),
        )


class LayoutManager:
    """
    Zarządza layoutami. Trzy źródła layoutów:
      1. Shipped presets (wbudowane w kod, niedostępne do edycji).
      2. User presets (w data/presets/layouts/*.json).
      3. Ad-hoc snapshots (np. auto-save przed zamknięciem).

    Użycie:
        lm = LayoutManager(panel_manager, data_dir=Path("~/Documents/ChatADHD"))
        lm.apply_preset("minimal")
        lm.save_current_as("my_custom")
        lm.list_presets()  # ["minimal", "aod_video", ..., "my_custom"]
    """

    def __init__(self, panel_manager: PanelManager, data_dir: Path) -> None:
        self._panel_manager = panel_manager
        self._data_dir = Path(data_dir).expanduser()
        self._user_presets_dir = self._data_dir / "presets" / "layouts"
        self._user_presets_dir.mkdir(parents=True, exist_ok=True)
        self._auto_save_path = self._data_dir / "last_layout.json"
        # Przy pierwszym uruchomieniu dumpujemy shipped presets do data_dir.
        # Zgodnie z fundamentalnymi zasadami: presety to DANE, KOD zawiera
        # tylko defaults jako fallback seed. User może edytować, usuwać,
        # dodawać — zmiany nie giną przy upgrade'ach.
        self._seed_shipped_to_user_presets()

    def _seed_shipped_to_user_presets(self) -> None:
        """
        Dumpuje shipped presety do data_dir jeśli tam jeszcze nie ma.
        Nie nadpisuje jeśli user już ma własną wersję — zgodnie z KOD≠DANE.

        Użytkownik może usunąć plik z data_dir — przy następnym starcie
        zostanie odtworzony z shipped defaults. To jest "reset to factory".
        """
        seeded_marker = self._data_dir / ".layouts_seeded"
        if seeded_marker.exists():
            return
        for name, layout_dict in SHIPPED_PRESETS.items():
            dest = self._user_presets_dir / f"{name}.json"
            if dest.exists():
                continue  # user ma własną wersję, nie nadpisujemy
            try:
                dest.write_text(json.dumps(layout_dict, indent=2),
                                encoding="utf-8")
            except Exception:
                log.exception("Failed to seed preset %s", name)
        seeded_marker.write_text("ChatADHD: shipped layouts seeded.\n",
                                   encoding="utf-8")
        log.info("Seeded %d shipped layouts to %s",
                 len(SHIPPED_PRESETS), self._user_presets_dir)

    # ─── Apply / save ──────────────────────────────────────────

    def apply_layout(self, layout: Layout) -> None:
        """Czyści panel-manager i odtwarza panele z layoutu."""
        log.info("Applying layout '%s' with %d panels", layout.name, len(layout.panels))
        try:
            self._panel_manager.restore(layout.panels)
        except Exception:
            log.exception("apply_layout failed; falling back to minimal")
            self._apply_shipped("minimal")

    def apply_preset(self, preset_name: str) -> bool:
        """
        Aplikuje preset po nazwie.
        Szuka najpierw w user presets, potem w shipped.
        Zwraca True jeśli znaleziono i zaaplikowano.
        """
        # 1. User presets
        user_path = self._user_presets_dir / f"{preset_name}.json"
        if user_path.exists():
            layout = self._load_from_file(user_path)
            if layout is not None:
                self.apply_layout(layout)
                return True

        # 2. Shipped presets
        if preset_name in SHIPPED_PRESETS:
            return self._apply_shipped(preset_name)

        log.warning("Preset '%s' not found", preset_name)
        return False

    def _apply_shipped(self, preset_name: str) -> bool:
        layout_dict = SHIPPED_PRESETS.get(preset_name)
        if layout_dict is None:
            return False
        layout = Layout.from_dict(layout_dict)
        self.apply_layout(layout)
        return True

    def snapshot_current(self, name: str = "current", description: str = "") -> Layout:
        """Zrzut obecnego stanu jako Layout (bez zapisu)."""
        return Layout(
            name=name,
            description=description,
            panels=self._panel_manager.snapshot(),
        )

    def save_current_as(self, preset_name: str, description: str = "") -> Path:
        """Zapisuje obecny stan jako user preset."""
        layout = self.snapshot_current(preset_name, description)
        path = self._user_presets_dir / f"{preset_name}.json"
        path.write_text(json.dumps(layout.to_dict(), indent=2), encoding="utf-8")
        log.info("Saved layout preset '%s' to %s", preset_name, path)
        return path

    def auto_save(self) -> None:
        """Zapisuje obecny layout jako ostatni — do restore przy następnym starcie."""
        layout = self.snapshot_current("_auto", "Automatic snapshot at shutdown")
        self._auto_save_path.write_text(
            json.dumps(layout.to_dict(), indent=2), encoding="utf-8"
        )

    def load_auto_save(self) -> bool:
        """Wczytuje ostatni auto-save. True jeśli się udało."""
        if not self._auto_save_path.exists():
            return False
        layout = self._load_from_file(self._auto_save_path)
        if layout is None:
            return False
        self.apply_layout(layout)
        return True

    # ─── Listing / export ──────────────────────────────────────

    def list_presets(self) -> List[Dict[str, str]]:
        """Lista wszystkich dostępnych presetów — shipped + user."""
        out: List[Dict[str, str]] = []
        for name, layout_dict in SHIPPED_PRESETS.items():
            out.append({
                "name": name,
                "source": "shipped",
                "description": layout_dict.get("description", ""),
            })
        for path in sorted(self._user_presets_dir.glob("*.json")):
            layout = self._load_from_file(path)
            if layout is not None:
                out.append({
                    "name": layout.name,
                    "source": "user",
                    "description": layout.description,
                })
        return out

    def delete_user_preset(self, preset_name: str) -> bool:
        """Usuwa user preset. Shipped są nieusuwalne."""
        path = self._user_presets_dir / f"{preset_name}.json"
        if path.exists():
            path.unlink()
            return True
        return False

    def export_current(self, dest_path: Path, name: str = "exported") -> Path:
        """Eksportuje obecny layout do dowolnej ścieżki — do share."""
        layout = self.snapshot_current(name)
        dest_path = Path(dest_path)
        dest_path.write_text(json.dumps(layout.to_dict(), indent=2), encoding="utf-8")
        return dest_path

    def import_from_file(self, source_path: Path, as_preset_name: Optional[str] = None) -> bool:
        """Importuje layout z pliku jako user preset."""
        layout = self._load_from_file(Path(source_path))
        if layout is None:
            return False
        name = as_preset_name or layout.name
        dest = self._user_presets_dir / f"{name}.json"
        dest.write_text(json.dumps(layout.to_dict(), indent=2), encoding="utf-8")
        return True

    # ─── Helpers ───────────────────────────────────────────────

    def _load_from_file(self, path: Path) -> Optional[Layout]:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            return Layout.from_dict(data)
        except Exception:
            log.exception("Failed to load layout from %s", path)
            return None


# ─── Shipped presets ────────────────────────────────────────────
#
# To są DANE, nie LOGIKA. Można je eksportować, modyfikować, podmieniać.
# Definiowane jako dict żeby były w 100% deklaratywne.

SHIPPED_PRESETS: Dict[str, Dict[str, Any]] = {

    # ── minimal ─────────────────────────────────────────────
    "minimal": {
        "layout_version": 1,
        "layout_name": "minimal",
        "description": "Tylko główny czat i lista konwersacji. "
                       "Domyślny layout po świeżej instalacji. "
                       "Wszystko inne dostępne przez menu View.",
        "panels": [
            {
                "panel_id": "chat_main",
                "panel_type": "chat",
                "panel_state": {},
                "mount": {
                    "mount_mode": "DOCKED",
                    "dock_position": "center",
                    "size": [800, 600],
                    "collapsed": False,
                },
            },
            {
                "panel_id": "conv_list_main",
                "panel_type": "conversation_list",
                "panel_state": {},
                "mount": {
                    "mount_mode": "DOCKED",
                    "dock_position": "left",
                    "size": [240, 600],
                    "collapsed": False,
                },
            },
        ],
    },

    # ── classic_0.07 ────────────────────────────────────────
    "classic_0.07": {
        "layout_version": 1,
        "layout_name": "classic_0.07",
        "description": "Layout z wersji 0.07.x — dla ciągłości dla istniejących użytkowników.",
        "panels": [
            {"panel_id": "chat_main", "panel_type": "chat", "panel_state": {},
             "mount": {"mount_mode": "DOCKED", "dock_position": "center",
                       "size": [600, 600], "collapsed": False}},
            {"panel_id": "conv_list_main", "panel_type": "conversation_list",
             "panel_state": {},
             "mount": {"mount_mode": "DOCKED", "dock_position": "left",
                       "size": [240, 600], "collapsed": False}},
            {"panel_id": "memory_main", "panel_type": "memory_tree", "panel_state": {},
             "mount": {"mount_mode": "DOCKED", "dock_position": "right",
                       "size": [300, 600], "collapsed": False}},
            {"panel_id": "graph_viz_main", "panel_type": "graph_viz", "panel_state": {},
             "mount": {"mount_mode": "HIDDEN", "dock_position": "center",
                       "size": [600, 400], "collapsed": False}},
        ],
    },

    # ── aod_video ───────────────────────────────────────────
    "aod_video": {
        "layout_version": 1,
        "layout_name": "aod_video",
        "description": "Tryb produkcji AoD: chat w centrum jako brainstorm, "
                       "scene graph floating po lewej, video preview "
                       "floating-locked po prawej, portfolio panel docked na dole.",
        "panels": [
            {"panel_id": "chat_brainstorm", "panel_type": "chat", "panel_state": {},
             "mount": {"mount_mode": "DOCKED", "dock_position": "center",
                       "size": [600, 400], "collapsed": False}},
            {"panel_id": "scene_graph_aod", "panel_type": "scene_graph_editor",
             "panel_state": {"view_mode": "timeline"},
             "mount": {"mount_mode": "FLOATING", "dock_position": "center",
                       "position": [50, 100], "size": [800, 300], "collapsed": False,
                       "z_order": 1}},
            {"panel_id": "video_preview_aod", "panel_type": "video_preview",
             "panel_state": {},
             "mount": {"mount_mode": "FLOATING_LOCKED", "dock_position": "center",
                       "position": [1200, 200], "size": [640, 360], "collapsed": False,
                       "z_order": 2}},
            {"panel_id": "portfolio_aod", "panel_type": "portfolio", "panel_state": {},
             "mount": {"mount_mode": "DOCKED", "dock_position": "bottom",
                       "size": [1200, 180], "collapsed": False}},
            {"panel_id": "generation_queue", "panel_type": "generation_queue",
             "panel_state": {},
             "mount": {"mount_mode": "FLOATING", "dock_position": "center",
                       "position": [900, 500], "size": [360, 240], "collapsed": True,
                       "z_order": 3}},
        ],
    },

    # ── cisza_beta ──────────────────────────────────────────
    "cisza_beta": {
        "layout_version": 1,
        "layout_name": "cisza_beta",
        "description": "Tryb ekranizacji Ciszy Beta: chat + scene graph (linear) + "
                       "character studio + video preview.",
        "panels": [
            {"panel_id": "chat_cb", "panel_type": "chat", "panel_state": {},
             "mount": {"mount_mode": "DOCKED", "dock_position": "center",
                       "size": [500, 400], "collapsed": False}},
            {"panel_id": "scene_graph_cb", "panel_type": "scene_graph_editor",
             "panel_state": {"view_mode": "timeline", "graph_type": "linear"},
             "mount": {"mount_mode": "DOCKED", "dock_position": "bottom",
                       "size": [1200, 200], "collapsed": False}},
            {"panel_id": "portfolio_cb", "panel_type": "portfolio",
             "panel_state": {"category_filter": "characters"},
             "mount": {"mount_mode": "DOCKED", "dock_position": "left",
                       "size": [240, 600], "collapsed": False}},
            {"panel_id": "video_preview_cb", "panel_type": "video_preview",
             "panel_state": {},
             "mount": {"mount_mode": "FLOATING", "dock_position": "center",
                       "position": [1000, 100], "size": [640, 360], "collapsed": False}},
        ],
    },

    # ── research ────────────────────────────────────────────
    "research": {
        "layout_version": 1,
        "layout_name": "research",
        "description": "Chat + memory tree + graph visualization + conversation list.",
        "panels": [
            {"panel_id": "chat_main", "panel_type": "chat", "panel_state": {},
             "mount": {"mount_mode": "DOCKED", "dock_position": "center",
                       "size": [600, 600], "collapsed": False}},
            {"panel_id": "conv_list", "panel_type": "conversation_list", "panel_state": {},
             "mount": {"mount_mode": "DOCKED", "dock_position": "left",
                       "size": [220, 600], "collapsed": False}},
            {"panel_id": "memory", "panel_type": "memory_tree", "panel_state": {},
             "mount": {"mount_mode": "DOCKED", "dock_position": "right",
                       "size": [300, 400], "collapsed": False}},
            {"panel_id": "graph_viz", "panel_type": "graph_viz", "panel_state": {},
             "mount": {"mount_mode": "FLOATING", "dock_position": "center",
                       "position": [400, 100], "size": [700, 500], "collapsed": False}},
        ],
    },

    # ── dev ─────────────────────────────────────────────────
    "dev": {
        "layout_version": 1,
        "layout_name": "dev",
        "description": "Wszystkie panele włączone — dla testów i rozwoju.",
        "panels": [
            # TODO: dołożyć wszystkie typy po zarejestrowaniu w startup
        ],
    },
}
