"""
gui/video_player.py
====================

Abstrakcja odtwarzania wideo z fallback chain.

Zgodne z kodeksem:
  - "Wszystko można, nic nie trzeba": user może wymusić konkretny backend
    przez config, albo zostawić auto (default).
  - "Fallback z metadanymi": każdy backend ma capabilities + status; gdy
    jeden nie załaduje klipu, próbujemy następnego.
  - "Multiplatformowość": IVideoPlayer jest abstrakcją; implementacje
    desktop/android wybierane runtime.
  - "Hierarchia reprezentacji": od najprostszej (external launch) po
    najbogatszą (Video z pełną kontrolą).

Backendy:
  1. KivyVideoBackend      — kivy.uix.video.Video, wymaga ffpyplayer/gstreamer
  2. KivyVideoPlayerBackend — kivy.uix.videoplayer.VideoPlayer, lepsza tolerancja
  3. ExternalBackend       — otwiera klip w zewnętrznej apce (plyer/webbrowser)

Każdy backend zwraca widget do osadzenia w panelu.
"""

from __future__ import annotations

import logging
import platform
from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from typing import Any, List, Optional

log = logging.getLogger(__name__)


# ─── Capabilities ───────────────────────────────────────────────

@dataclass
class PlayerCapabilities:
    """Co backend potrafi. Użytkownik widzi w UI przy wyborze."""
    can_embed: bool                 # widget osadzalny, czy tylko launch
    can_loop: bool                   # zapętlenie
    can_seek: bool                   # przewijanie w UI
    can_control_volume: bool
    platforms: List[str]             # ["desktop", "android", ...]
    requires_deps: List[str]         # ["ffpyplayer"], []
    available: bool                  # aktualnie użyteczny na tej instalacji
    reason_unavailable: str = ""


# ─── Interface ──────────────────────────────────────────────────

class IVideoPlayer(ABC):
    """
    Abstrakcja odtwarzacza wideo.

    Invariant: build_widget() zwraca Kivy widget (lub None gdy backend
    jest tylko launcher). load(path) ustawia źródło, może być wywołane
    wielokrotnie na tym samym widget'cie.
    """

    backend_name: str = "abstract"

    @abstractmethod
    def capabilities(self) -> PlayerCapabilities:
        ...

    @abstractmethod
    def build_widget(self) -> Any:
        """Zwraca widget Kivy do osadzenia. Dla external może zwrócić placeholder."""
        ...

    @abstractmethod
    def load(self, path: str) -> bool:
        """Ładuje klip. True gdy sukces."""
        ...

    @abstractmethod
    def play(self) -> None: ...

    @abstractmethod
    def pause(self) -> None: ...

    @abstractmethod
    def stop(self) -> None: ...

    def set_loop(self, loop: bool) -> None:
        """Domyślnie no-op jeśli backend nie obsługuje."""
        pass

    def set_volume(self, volume: float) -> None:
        """0.0 - 1.0. Domyślnie no-op."""
        pass

    def query(self, key: str) -> Any:
        return None


# ─── KivyVideoBackend ───────────────────────────────────────────

class KivyVideoBackend(IVideoPlayer):
    """
    Używa kivy.uix.video.Video. Najbogatszy kontrola, ale wymaga ffpyplayer
    albo gstreamer. Na Pydroid3 ffpyplayer bywa problematyczny.
    """

    backend_name = "kivy_video"

    def __init__(self):
        self._widget = None
        self._current_path: Optional[str] = None
        self._loop = True
        self._available = self._detect_availability()

    def _detect_availability(self) -> bool:
        try:
            from kivy.uix.video import Video  # noqa: F401
            # Sprawdź czy ffpyplayer się ładuje — to jest prawdziwy test.
            try:
                import ffpyplayer  # noqa: F401
                return True
            except ImportError:
                # Bez ffpyplayer Video czasem działa przez gstreamer,
                # ale to rzadkie na Androidzie. Oznaczamy jako dostępne
                # ale z niższym priorytetem (sprawdzimy przy load).
                return True
        except ImportError:
            return False

    def capabilities(self) -> PlayerCapabilities:
        reason = ""
        if not self._available:
            reason = "kivy.uix.video not importable (missing ffpyplayer?)"
        return PlayerCapabilities(
            can_embed=True,
            can_loop=True,
            can_seek=False,  # Video samo nie ma UI kontroli — to robi panel
            can_control_volume=True,
            platforms=["desktop", "android"],
            requires_deps=["ffpyplayer"],
            available=self._available,
            reason_unavailable=reason,
        )

    def build_widget(self) -> Any:
        if not self._available:
            return None
        from kivy.uix.video import Video
        self._widget = Video(
            state="stop",
            options={"eos": "loop" if self._loop else "stop"},
            allow_stretch=True,
        )
        return self._widget

    def load(self, path: str) -> bool:
        if self._widget is None or not self._available:
            return False
        if not Path(path).exists():
            log.warning("KivyVideoBackend.load: file not found: %s", path)
            return False
        try:
            self._widget.source = path
            self._widget.state = "play"
            self._current_path = path
            return True
        except Exception:
            log.exception("KivyVideoBackend failed to load %s", path)
            return False

    def play(self) -> None:
        if self._widget is not None:
            self._widget.state = "play"

    def pause(self) -> None:
        if self._widget is not None:
            self._widget.state = "pause"

    def stop(self) -> None:
        if self._widget is not None:
            self._widget.state = "stop"

    def set_loop(self, loop: bool) -> None:
        self._loop = loop
        if self._widget is not None:
            self._widget.options["eos"] = "loop" if loop else "stop"

    def set_volume(self, volume: float) -> None:
        if self._widget is not None and hasattr(self._widget, "volume"):
            self._widget.volume = max(0.0, min(1.0, volume))


# ─── KivyVideoPlayerBackend ─────────────────────────────────────

class KivyVideoPlayerBackend(IVideoPlayer):
    """
    kivy.uix.videoplayer.VideoPlayer — wbudowane UI kontroli (play/pause/seek).
    Bardziej tolerancyjny na brakujące dep (pokazuje black frame zamiast crashu).
    """

    backend_name = "kivy_videoplayer"

    def __init__(self):
        self._widget = None
        self._current_path: Optional[str] = None
        self._loop = True
        self._available = self._detect_availability()

    def _detect_availability(self) -> bool:
        try:
            from kivy.uix.videoplayer import VideoPlayer  # noqa: F401
            return True
        except ImportError:
            return False

    def capabilities(self) -> PlayerCapabilities:
        return PlayerCapabilities(
            can_embed=True,
            can_loop=True,
            can_seek=True,
            can_control_volume=True,
            platforms=["desktop", "android"],
            requires_deps=["ffpyplayer"],  # też używa, ale degraduje łagodniej
            available=self._available,
            reason_unavailable=("" if self._available
                                else "kivy.uix.videoplayer not importable"),
        )

    def build_widget(self) -> Any:
        if not self._available:
            return None
        from kivy.uix.videoplayer import VideoPlayer
        self._widget = VideoPlayer(
            state="stop",
            options={"eos": "loop" if self._loop else "stop"},
            allow_stretch=True,
        )
        return self._widget

    def load(self, path: str) -> bool:
        if self._widget is None or not self._available:
            return False
        if not Path(path).exists():
            log.warning("KivyVideoPlayerBackend.load: file not found: %s", path)
            return False
        try:
            self._widget.source = path
            self._widget.state = "play"
            self._current_path = path
            return True
        except Exception:
            log.exception("KivyVideoPlayerBackend failed to load %s", path)
            return False

    def play(self) -> None:
        if self._widget is not None:
            self._widget.state = "play"

    def pause(self) -> None:
        if self._widget is not None:
            self._widget.state = "pause"

    def stop(self) -> None:
        if self._widget is not None:
            self._widget.state = "stop"

    def set_loop(self, loop: bool) -> None:
        self._loop = loop
        if self._widget is not None:
            self._widget.options["eos"] = "loop" if loop else "stop"

    def set_volume(self, volume: float) -> None:
        if self._widget is not None and hasattr(self._widget, "volume"):
            self._widget.volume = max(0.0, min(1.0, volume))


# ─── ExternalBackend ────────────────────────────────────────────

class ExternalBackend(IVideoPlayer):
    """
    Launch-only backend — otwiera klip w zewnętrznej apce systemowej.
    Na Androidzie: plyer.open + android intent. Na desktop: webbrowser.

    Widget to tylko placeholder-button. Działa ZAWSZE — ostatni fallback.
    Zgodne z kodeksem: "nic nie trzeba" — nawet gdy nic się nie załaduje,
    user ma opcję "otwórz zewnętrznie".
    """

    backend_name = "external"

    def __init__(self):
        self._widget = None
        self._current_path: Optional[str] = None

    def capabilities(self) -> PlayerCapabilities:
        return PlayerCapabilities(
            can_embed=False,  # widget to tylko button, nie prawdziwy player
            can_loop=False,
            can_seek=False,
            can_control_volume=False,
            platforms=["desktop", "android"],
            requires_deps=[],
            available=True,  # ZAWSZE dostępny
        )

    def build_widget(self) -> Any:
        from kivy.uix.boxlayout import BoxLayout
        from kivy.uix.label import Label

        try:
            from gui.base import C, RBtn
            has_base = True
        except ImportError:
            from kivy.uix.button import Button as RBtn  # type: ignore
            C = {"text": (0.95, 0.95, 0.95, 1), "dim": (0.55, 0.55, 0.60, 1)}
            has_base = False

        root = BoxLayout(orientation="vertical", padding=10, spacing=4)
        self._path_label = Label(
            text="(no clip loaded)",
            color=C["dim"],
        )
        root.add_widget(self._path_label)

        self._open_btn = RBtn(
            text="Open externally",
            size_hint_y=None,
            height=40,
        )
        self._open_btn.bind(on_release=lambda *_: self._open_external())
        root.add_widget(self._open_btn)

        self._widget = root
        return root

    def load(self, path: str) -> bool:
        if not Path(path).exists():
            return False
        self._current_path = path
        if self._widget is not None:
            self._path_label.text = f"Ready: {Path(path).name}\nPress to open"
        return True

    def _open_external(self) -> None:
        if self._current_path is None:
            return
        try:
            # Android: plyer
            if self._is_android():
                try:
                    from jnius import autoclass, cast
                    Intent = autoclass("android.content.Intent")
                    Uri = autoclass("android.net.Uri")
                    File = autoclass("java.io.File")
                    PythonActivity = autoclass("org.kivy.android.PythonActivity")

                    intent = Intent(Intent.ACTION_VIEW)
                    file = File(self._current_path)
                    uri = Uri.fromFile(file)
                    intent.setDataAndType(uri, "video/*")
                    intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    PythonActivity.mActivity.startActivity(intent)
                    return
                except Exception:
                    log.exception("Android intent failed; falling back to webbrowser")

            # Desktop / fallback
            import webbrowser
            webbrowser.open(f"file://{self._current_path}")
        except Exception:
            log.exception("Failed to open externally")

    def _is_android(self) -> bool:
        import sys
        return hasattr(sys, "getandroidapilevel")

    def play(self) -> None:
        # External: play = open.
        self._open_external()

    def pause(self) -> None:
        pass  # no-op

    def stop(self) -> None:
        pass  # no-op


# ─── Factory / auto-select ──────────────────────────────────────

def create_video_player(preferred_backend: Optional[str] = None,
                         fallback_chain: Optional[List[str]] = None) -> IVideoPlayer:
    """
    Tworzy player z fallback chain.

    preferred_backend: None = auto. "kivy_video" / "kivy_videoplayer" / "external".
    fallback_chain: kolejność prób. Default: [videoplayer, video, external].

    Zgodne z kodeksem: wybór jest DEDUKCYJNY z capabilities każdego backendu.
    """
    fallback_chain = fallback_chain or [
        "kivy_videoplayer",  # najbardziej tolerancyjny
        "kivy_video",         # bogatszy kontrola
        "external",           # ostatni ratunek, zawsze dostępny
    ]

    # Jeśli preferred i dostępny — użyj.
    if preferred_backend:
        backend = _instantiate(preferred_backend)
        if backend is not None and backend.capabilities().available:
            return backend
        log.info("Preferred backend '%s' unavailable, falling back",
                 preferred_backend)

    # Fallback chain.
    for name in fallback_chain:
        backend = _instantiate(name)
        if backend is not None and backend.capabilities().available:
            log.info("Video player backend selected: %s", name)
            return backend

    # External jest always-available, ale dla pewności:
    log.warning("No video backend available, using External as last resort")
    return ExternalBackend()


def _instantiate(name: str) -> Optional[IVideoPlayer]:
    if name == "kivy_video":
        return KivyVideoBackend()
    if name == "kivy_videoplayer":
        return KivyVideoPlayerBackend()
    if name == "external":
        return ExternalBackend()
    log.warning("Unknown video backend name: %s", name)
    return None


def list_available_backends() -> List[dict]:
    """Dla UI settings — user widzi co jest dostępne."""
    out = []
    for name in ("kivy_video", "kivy_videoplayer", "external"):
        inst = _instantiate(name)
        if inst is None:
            continue
        caps = inst.capabilities()
        out.append({
            "name": name,
            "available": caps.available,
            "reason": caps.reason_unavailable,
            "platforms": caps.platforms,
            "can_embed": caps.can_embed,
            "requires_deps": caps.requires_deps,
        })
    return out
