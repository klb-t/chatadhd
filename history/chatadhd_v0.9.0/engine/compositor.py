"""
engine/compositor.py
=====================

Abstrakcja compositora — łączenie klipów w jeden plik wideo.

Zgodne z kodeksem:
  - Fallback chain: imageio-ffmpeg → system ffmpeg → export listy (JSON)
  - Każda implementacja wystawia capabilities()
  - Wybór DEDUKCYJNY z availability — nie arbitralny
  - Użytkownik może wymusić konkretny backend w config

Backendy:
  1. ImageIOFFmpegCompositor — imageio-ffmpeg (bundle'owany ffmpeg), działa na
     desktop i Androidzie (z pythonforandroid recipe).
  2. SystemFFmpegCompositor  — wywołuje ffmpeg z PATH, dla desktop userów
     którzy mają własny.
  3. ExportOnlyCompositor    — fallback: nic nie renderuje, tylko zapisuje
     listę klipów do JSON + batch script (sh/bat), user odpala ręcznie.
"""

from __future__ import annotations

import json
import logging
import shutil
import subprocess
import tempfile
from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from typing import Any, List, Optional

log = logging.getLogger(__name__)


@dataclass
class CompositorCapabilities:
    """Co backend potrafi."""
    can_concat: bool
    can_add_audio: bool
    can_apply_effects: bool  # dla polirytmicznego zoomu (0.9.0-b1+)
    platforms: List[str]
    available: bool
    reason_unavailable: str = ""


class ICompositor(ABC):
    """
    Abstrakcja compositora.

    Wszystkie backendy zwracają (success, output_or_script_path, diagnostics).
    Brak wyjątków — diagnostyka jako dict.
    """

    backend_name: str = "abstract"

    @abstractmethod
    def capabilities(self) -> CompositorCapabilities:
        ...

    @abstractmethod
    def concat(self, clip_paths: List[Path], output: Path,
               audio: Optional[Path] = None) -> "CompositionResult":
        ...


@dataclass
class CompositionResult:
    """Wynik operacji compose."""
    success: bool
    output_path: Optional[Path] = None          # wynikowy plik (dla backendów renderujących)
    script_path: Optional[Path] = None          # skrypt do odpalenia (dla export-only)
    backend_used: str = ""
    diagnostics: dict = None
    error: str = ""

    def __post_init__(self):
        if self.diagnostics is None:
            self.diagnostics = {}


# ─── ImageIOFFmpegCompositor ────────────────────────────────────

class ImageIOFFmpegCompositor(ICompositor):
    """
    imageio-ffmpeg — bundle'owany binary ffmpeg. Działa na desktop i Androidzie
    (jeśli pythonforandroid zbuduje recipe, albo user ręcznie zapewni).

    Nie wymaga ffmpeg w PATH — ściąga własny.
    """

    backend_name = "imageio_ffmpeg"

    def __init__(self):
        self._ffmpeg_path = self._detect_ffmpeg()

    def _detect_ffmpeg(self) -> Optional[str]:
        try:
            import imageio_ffmpeg
            path = imageio_ffmpeg.get_ffmpeg_exe()
            if Path(path).exists():
                return path
            return None
        except ImportError:
            return None
        except Exception:
            log.exception("imageio-ffmpeg detection failed")
            return None

    def capabilities(self) -> CompositorCapabilities:
        avail = self._ffmpeg_path is not None
        return CompositorCapabilities(
            can_concat=True,
            can_add_audio=True,
            can_apply_effects=True,
            platforms=["desktop", "android"],
            available=avail,
            reason_unavailable=("" if avail
                                else "imageio-ffmpeg not installed or binary missing"),
        )

    def concat(self, clip_paths: List[Path], output: Path,
               audio: Optional[Path] = None) -> CompositionResult:
        if self._ffmpeg_path is None:
            return CompositionResult(
                success=False,
                backend_used=self.backend_name,
                error="imageio-ffmpeg not available",
            )
        return _run_ffmpeg_concat(self._ffmpeg_path, clip_paths, output,
                                   audio, self.backend_name)


# ─── SystemFFmpegCompositor ─────────────────────────────────────

class SystemFFmpegCompositor(ICompositor):
    """Wywołuje ffmpeg z PATH. Dla userów którzy mają własny."""

    backend_name = "system_ffmpeg"

    def __init__(self):
        self._ffmpeg_path = shutil.which("ffmpeg")

    def capabilities(self) -> CompositorCapabilities:
        avail = self._ffmpeg_path is not None
        return CompositorCapabilities(
            can_concat=True,
            can_add_audio=True,
            can_apply_effects=True,
            platforms=["desktop"],  # na Androidzie rzadko w PATH
            available=avail,
            reason_unavailable=("" if avail else "ffmpeg not found in PATH"),
        )

    def concat(self, clip_paths: List[Path], output: Path,
               audio: Optional[Path] = None) -> CompositionResult:
        if self._ffmpeg_path is None:
            return CompositionResult(
                success=False,
                backend_used=self.backend_name,
                error="ffmpeg not in PATH",
            )
        return _run_ffmpeg_concat(self._ffmpeg_path, clip_paths, output,
                                   audio, self.backend_name)


# ─── ExportOnlyCompositor ───────────────────────────────────────

class ExportOnlyCompositor(ICompositor):
    """
    Ostatni fallback: nie renderuje. Zapisuje listę klipów jako concat-list.txt
    + shell/batch script który user odpala ręcznie po zainstalowaniu ffmpeg.

    Zgodne z kodeksem: "nic nie trzeba" — nawet bez ffmpeg user dostaje
    deliverable (skrypt + lista). Nie jest zmuszany do instalacji deps.
    """

    backend_name = "export_only"

    def capabilities(self) -> CompositorCapabilities:
        return CompositorCapabilities(
            can_concat=False,        # sam nie robi, ale generuje instrukcje
            can_add_audio=False,
            can_apply_effects=False,
            platforms=["desktop", "android"],
            available=True,           # ZAWSZE dostępny
        )

    def concat(self, clip_paths: List[Path], output: Path,
               audio: Optional[Path] = None) -> CompositionResult:
        # Zapisz concat list i script obok planowanego output.
        output = Path(output)
        output.parent.mkdir(parents=True, exist_ok=True)

        list_path = output.with_suffix(".list.txt")
        script_path = output.with_suffix(".sh")
        json_path = output.with_suffix(".manifest.json")

        # concat-demuxer list
        list_path.write_text(
            "\n".join(f"file '{cp.absolute()}'" for cp in clip_paths) + "\n",
            encoding="utf-8"
        )

        # shell script
        script_lines = ["#!/bin/sh", "set -e",
                         "# Run this after installing ffmpeg.",
                         "# Generated by ChatADHD ExportOnlyCompositor."]
        if audio is not None:
            script_lines.append(
                f"ffmpeg -y -f concat -safe 0 -i '{list_path.absolute()}' "
                f"-i '{audio.absolute()}' -c:v copy -c:a aac -shortest "
                f"'{output.absolute()}'"
            )
        else:
            script_lines.append(
                f"ffmpeg -y -f concat -safe 0 -i '{list_path.absolute()}' "
                f"-c:v copy '{output.absolute()}'"
            )
        script_path.write_text("\n".join(script_lines) + "\n", encoding="utf-8")
        try:
            script_path.chmod(0o755)
        except Exception:
            pass  # na Androidzie chmod może nie zadziałać

        # JSON manifest — dla skryptów które mają własny pipeline
        manifest = {
            "backend": self.backend_name,
            "clips": [str(cp.absolute()) for cp in clip_paths],
            "audio": str(audio.absolute()) if audio else None,
            "planned_output": str(output.absolute()),
            "instructions": "Install ffmpeg, then run the accompanying .sh script.",
        }
        json_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

        return CompositionResult(
            success=True,
            output_path=None,
            script_path=script_path,
            backend_used=self.backend_name,
            diagnostics={
                "list_file": str(list_path),
                "manifest": str(json_path),
                "script": str(script_path),
                "message": ("No ffmpeg available. Generated concat list + script. "
                            "Install ffmpeg and run the .sh file to produce the output."),
            },
        )


# ─── Shared ffmpeg invocation ──────────────────────────────────

def _run_ffmpeg_concat(ffmpeg_path: str, clip_paths: List[Path],
                       output: Path, audio: Optional[Path],
                       backend_name: str) -> CompositionResult:
    """Wspólna implementacja concat dla backendów używających ffmpeg."""
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False,
                                       encoding="utf-8") as f:
        for cp in clip_paths:
            f.write(f"file '{cp.absolute()}'\n")
        list_file = f.name

    cmd = [ffmpeg_path, "-y", "-f", "concat", "-safe", "0", "-i", list_file]
    if audio is not None:
        cmd += ["-i", str(audio.absolute()), "-c:v", "copy",
                "-c:a", "aac", "-shortest"]
    else:
        cmd += ["-c:v", "copy"]
    cmd += [str(output.absolute())]

    log.info("ffmpeg (%s): %s", backend_name, " ".join(cmd))

    try:
        result = subprocess.run(cmd, check=False, capture_output=True, text=True)
        if result.returncode != 0:
            return CompositionResult(
                success=False,
                backend_used=backend_name,
                error=f"ffmpeg exit {result.returncode}: {result.stderr[:500]}",
                diagnostics={"stdout": result.stdout[:500],
                              "stderr": result.stderr[:500]},
            )
        return CompositionResult(
            success=True,
            output_path=output,
            backend_used=backend_name,
            diagnostics={"list_file": list_file},
        )
    except Exception as e:
        log.exception("ffmpeg invocation failed")
        return CompositionResult(
            success=False,
            backend_used=backend_name,
            error=str(e),
        )
    finally:
        try:
            Path(list_file).unlink()
        except Exception:
            pass


# ─── Factory / auto-select ──────────────────────────────────────

def create_compositor(preferred_backend: Optional[str] = None,
                       fallback_chain: Optional[List[str]] = None) -> ICompositor:
    """
    Wybór compositora.

    Default fallback chain: imageio-ffmpeg → system ffmpeg → export-only.
    User może wymusić konkretny przez config.
    """
    fallback_chain = fallback_chain or [
        "imageio_ffmpeg",
        "system_ffmpeg",
        "export_only",
    ]

    if preferred_backend:
        comp = _instantiate_compositor(preferred_backend)
        if comp is not None and comp.capabilities().available:
            return comp
        log.info("Preferred compositor '%s' unavailable, falling back",
                 preferred_backend)

    for name in fallback_chain:
        comp = _instantiate_compositor(name)
        if comp is not None and comp.capabilities().available:
            log.info("Compositor selected: %s", name)
            return comp

    return ExportOnlyCompositor()


def _instantiate_compositor(name: str) -> Optional[ICompositor]:
    if name == "imageio_ffmpeg":
        return ImageIOFFmpegCompositor()
    if name == "system_ffmpeg":
        return SystemFFmpegCompositor()
    if name == "export_only":
        return ExportOnlyCompositor()
    return None


def list_available_compositors() -> List[dict]:
    out = []
    for name in ("imageio_ffmpeg", "system_ffmpeg", "export_only"):
        inst = _instantiate_compositor(name)
        if inst is None:
            continue
        caps = inst.capabilities()
        out.append({
            "name": name,
            "available": caps.available,
            "reason": caps.reason_unavailable,
            "platforms": caps.platforms,
            "can_add_audio": caps.can_add_audio,
            "can_apply_effects": caps.can_apply_effects,
        })
    return out
