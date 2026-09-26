"""
engine/scene_graph.py
======================

Modele danych dla struktury filmu: graf scen, portfolio, spec synchronizacji audio.

Invariant: scene graph to graf zależności, nie liniowa sekwencja.
Linear film = graf bez rozgałęzień (edges tworzą ścieżkę 1→2→3→...).
Branching film (Cisza Beta personalizacja) = graf z warunkami na edgach.

Każda scena ma ≥1 anchor z portfolio. Bez anchora nie ma hard-constraintu
na spójność międzyujęciową.

Persistence: data/video/graphs/<graph_id>.json + artefakty w data/video/{keyframes,clips,embeddings}/.
"""

from __future__ import annotations

import json
import logging
import uuid
from dataclasses import dataclass, field, asdict
from enum import Enum
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

log = logging.getLogger(__name__)


# ─── Portfolio ──────────────────────────────────────────────────

class PortfolioItemRole(str, Enum):
    """Rola pozycji w portfolio. Wpływa na to jak scena może użyć anchora."""
    CHARACTER = "character"     # postać — musi zachować spójność w całym filmie
    LOCATION = "location"       # lokacja — może być re-used w wielu scenach
    PROP = "prop"               # rekwizyt — lokalne wsparcie
    MOOD_BOARD = "mood_board"   # ogólny styl, kolory, tekstura
    STYLE_REF = "style_ref"     # konkretne odniesienie stylistyczne


@dataclass
class PortfolioItem:
    """Jedna pozycja w portfolio — statyczne zdjęcie/obraz z metadanymi."""
    item_id: str
    name: str
    role: PortfolioItemRole
    file_path: str  # względem data_dir
    tags: List[str] = field(default_factory=list)
    description: str = ""
    embedding: Optional[List[float]] = None  # wizualny embedding dla coherence

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        d["role"] = self.role.value
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "PortfolioItem":
        return cls(
            item_id=d["item_id"],
            name=d["name"],
            role=PortfolioItemRole(d["role"]),
            file_path=d["file_path"],
            tags=list(d.get("tags", [])),
            description=d.get("description", ""),
            embedding=d.get("embedding"),
        )


@dataclass
class Portfolio:
    """Kolekcja portfolio items, przypisana do grafu."""
    portfolio_id: str
    name: str
    items: List[PortfolioItem] = field(default_factory=list)

    def get(self, item_id: str) -> Optional[PortfolioItem]:
        return next((it for it in self.items if it.item_id == item_id), None)

    def by_role(self, role: PortfolioItemRole) -> List[PortfolioItem]:
        return [it for it in self.items if it.role == role]

    def to_dict(self) -> Dict[str, Any]:
        return {
            "portfolio_id": self.portfolio_id,
            "name": self.name,
            "items": [it.to_dict() for it in self.items],
        }

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "Portfolio":
        return cls(
            portfolio_id=d["portfolio_id"],
            name=d.get("name", ""),
            items=[PortfolioItem.from_dict(x) for x in d.get("items", [])],
        )


# ─── Audio sync spec ────────────────────────────────────────────

@dataclass
class AudioSyncSpec:
    """
    Specyfikacja synchronizacji wideo z audio.

    Dla AoD „Krew jak smoła":
        audio_file = "album/krew_jak_smola.wav"
        zoom_schedule = [(0.0, 1.0), (8.5, 16.0), (12.3, 256.0), ...]  # (t_sec, zoom_factor)
        zoom_discrete = True   # cięcie zamiast płynnego zoom
        zoom_beat_align = True # kotwiczenie zmian na beacie

    Dla liniowego filmu bez polirytm: AudioSyncSpec(audio_file=..., zoom_schedule=[]).
    """
    audio_file: str
    zoom_schedule: List[Tuple[float, float]] = field(default_factory=list)
    zoom_discrete: bool = True
    zoom_beat_align: bool = False
    bpm: Optional[float] = None
    polyrhythm: Optional[List[int]] = None  # np. [17, 13] dla 17/8 vs 13/8

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "AudioSyncSpec":
        return cls(
            audio_file=d["audio_file"],
            zoom_schedule=[tuple(x) for x in d.get("zoom_schedule", [])],
            zoom_discrete=d.get("zoom_discrete", True),
            zoom_beat_align=d.get("zoom_beat_align", False),
            bpm=d.get("bpm"),
            polyrhythm=d.get("polyrhythm"),
        )


# ─── Scene ──────────────────────────────────────────────────────

class SceneState(str, Enum):
    PENDING = "pending"
    KEYFRAME_READY = "keyframe_ready"
    KEYFRAME_FAILED = "keyframe_failed"
    CLIP_READY = "clip_ready"
    CLIP_FAILED = "clip_failed"
    FLAGGED = "flagged"  # wymaga ręcznej interwencji


@dataclass
class SceneArtifacts:
    """Wszystkie artefakty wygenerowane dla sceny. Ścieżki względne do data_dir."""
    keyframe_path: Optional[str] = None
    clip_path: Optional[str] = None
    embedding_keyframe: Optional[List[float]] = None
    embedding_clip: Optional[List[float]] = None
    model_used_keyframe: Optional[str] = None
    model_used_clip: Optional[str] = None
    generation_cost_usd: float = 0.0
    generation_time_s: float = 0.0
    attempts: int = 0

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "SceneArtifacts":
        return cls(**d)


@dataclass
class Scene:
    """Jedna scena w filmie."""
    scene_id: str
    description: str
    duration_seconds: float
    anchors: List[str] = field(default_factory=list)  # portfolio item_ids; ≥1 wymagane
    characters: List[str] = field(default_factory=list)  # item_ids z role=CHARACTER
    location: Optional[str] = None  # item_id z role=LOCATION
    style_tokens: List[str] = field(default_factory=list)
    constraints: Dict[str, Any] = field(default_factory=dict)
    artifacts: SceneArtifacts = field(default_factory=SceneArtifacts)
    state: SceneState = SceneState.PENDING
    notes: str = ""  # user-editable comment

    def to_dict(self) -> Dict[str, Any]:
        d = {
            "scene_id": self.scene_id,
            "description": self.description,
            "duration_seconds": self.duration_seconds,
            "anchors": self.anchors,
            "characters": self.characters,
            "location": self.location,
            "style_tokens": self.style_tokens,
            "constraints": self.constraints,
            "artifacts": self.artifacts.to_dict(),
            "state": self.state.value,
            "notes": self.notes,
        }
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "Scene":
        return cls(
            scene_id=d["scene_id"],
            description=d["description"],
            duration_seconds=float(d["duration_seconds"]),
            anchors=list(d.get("anchors", [])),
            characters=list(d.get("characters", [])),
            location=d.get("location"),
            style_tokens=list(d.get("style_tokens", [])),
            constraints=dict(d.get("constraints", {})),
            artifacts=SceneArtifacts.from_dict(d.get("artifacts", {})),
            state=SceneState(d.get("state", "pending")),
            notes=d.get("notes", ""),
        )

    def validate(self) -> List[str]:
        """Zwraca listę błędów walidacji. Pusta lista = OK."""
        errs: List[str] = []
        if not self.anchors:
            errs.append(f"Scene {self.scene_id}: no anchors (≥1 required)")
        if self.duration_seconds <= 0:
            errs.append(f"Scene {self.scene_id}: duration must be > 0")
        if not self.description.strip():
            errs.append(f"Scene {self.scene_id}: empty description")
        return errs


# ─── Scene edges ────────────────────────────────────────────────

class TransitionType(str, Enum):
    CUT = "cut"
    DISSOLVE = "dissolve"
    FADE = "fade"
    MATCH_CUT = "match_cut"
    HARD_ZOOM = "hard_zoom"


@dataclass
class SceneEdge:
    """Krawędź w scene graphu."""
    from_scene: str
    to_scene: str
    transition: TransitionType = TransitionType.CUT
    condition: Optional[str] = None  # dla branching: wyrażenie ewaluowane w runtime
    transition_duration_s: float = 0.0  # 0 dla cut, >0 dla dissolve/fade

    def to_dict(self) -> Dict[str, Any]:
        d = asdict(self)
        d["transition"] = self.transition.value
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "SceneEdge":
        return cls(
            from_scene=d["from_scene"],
            to_scene=d["to_scene"],
            transition=TransitionType(d.get("transition", "cut")),
            condition=d.get("condition"),
            transition_duration_s=float(d.get("transition_duration_s", 0.0)),
        )


# ─── Scene graph ────────────────────────────────────────────────

class GraphType(str, Enum):
    LINEAR = "linear"
    BRANCHING = "branching"


@dataclass
class SceneGraph:
    """Scene graph — pełna struktura filmu."""
    graph_id: str
    name: str
    graph_type: GraphType = GraphType.LINEAR
    scenes: List[Scene] = field(default_factory=list)
    edges: List[SceneEdge] = field(default_factory=list)
    portfolio_id: Optional[str] = None
    audio_sync: Optional[AudioSyncSpec] = None
    metadata: Dict[str, Any] = field(default_factory=dict)

    # ── Queries ────────────────────────────────────────────

    def get_scene(self, scene_id: str) -> Optional[Scene]:
        return next((s for s in self.scenes if s.scene_id == scene_id), None)

    def total_duration(self) -> float:
        """Dla linear: suma. Dla branching: max (najdłuższa ścieżka)."""
        if self.graph_type == GraphType.LINEAR:
            return sum(s.duration_seconds for s in self.scenes)
        # Branching: longest path. Proste BFS — zakładamy acykliczność.
        return self._longest_path_duration()

    def _longest_path_duration(self) -> float:
        scenes_by_id = {s.scene_id: s for s in self.scenes}
        out_edges: Dict[str, List[str]] = {s.scene_id: [] for s in self.scenes}
        for e in self.edges:
            out_edges.setdefault(e.from_scene, []).append(e.to_scene)
        # Topological order via DFS
        memo: Dict[str, float] = {}

        def dfs(node_id: str) -> float:
            if node_id in memo:
                return memo[node_id]
            scene = scenes_by_id.get(node_id)
            base = scene.duration_seconds if scene else 0.0
            children = out_edges.get(node_id, [])
            if not children:
                memo[node_id] = base
                return base
            memo[node_id] = base + max(dfs(c) for c in children)
            return memo[node_id]

        if not self.scenes:
            return 0.0
        # Sceny bez in-edges = entry points.
        in_count = {s.scene_id: 0 for s in self.scenes}
        for e in self.edges:
            in_count[e.to_scene] = in_count.get(e.to_scene, 0) + 1
        entries = [sid for sid, c in in_count.items() if c == 0]
        if not entries:
            # Cyclic or empty — fallback.
            return sum(s.duration_seconds for s in self.scenes)
        return max(dfs(e) for e in entries)

    def validate(self) -> List[str]:
        """Pełna walidacja grafu. Zwraca listę błędów."""
        errs: List[str] = []
        scene_ids = {s.scene_id for s in self.scenes}

        # Walidacja scen
        for s in self.scenes:
            errs.extend(s.validate())

        # Walidacja edges
        for e in self.edges:
            if e.from_scene not in scene_ids:
                errs.append(f"Edge references unknown from_scene: {e.from_scene}")
            if e.to_scene not in scene_ids:
                errs.append(f"Edge references unknown to_scene: {e.to_scene}")

        # Dla linear: edges muszą tworzyć jedną ścieżkę
        if self.graph_type == GraphType.LINEAR and self.edges:
            # Każda scena powinna mieć ≤1 out-edge i ≤1 in-edge
            out_count: Dict[str, int] = {}
            in_count: Dict[str, int] = {}
            for e in self.edges:
                out_count[e.from_scene] = out_count.get(e.from_scene, 0) + 1
                in_count[e.to_scene] = in_count.get(e.to_scene, 0) + 1
            for sid, c in out_count.items():
                if c > 1:
                    errs.append(f"Linear graph: scene {sid} has {c} out-edges (max 1)")
            for sid, c in in_count.items():
                if c > 1:
                    errs.append(f"Linear graph: scene {sid} has {c} in-edges (max 1)")

        return errs

    # ── Mutations ──────────────────────────────────────────

    def add_scene(self, scene: Scene, after: Optional[str] = None) -> None:
        """
        Dodaje scenę. Dla linear: jeśli `after` podane, wstawia po tej scenie
        i aktualizuje edges. Dla branching: samo dodanie, edges trzeba dodać osobno.
        """
        self.scenes.append(scene)
        if self.graph_type == GraphType.LINEAR and after is not None:
            # Rozłączamy istniejącą edge after→X i wstawiamy after→new→X
            old_edge = next((e for e in self.edges if e.from_scene == after), None)
            if old_edge is not None:
                self.edges.append(SceneEdge(
                    from_scene=scene.scene_id,
                    to_scene=old_edge.to_scene,
                    transition=old_edge.transition,
                ))
                old_edge.to_scene = scene.scene_id
            else:
                # after jest ostatnią sceną — dodajemy tylko nową edge
                self.edges.append(SceneEdge(
                    from_scene=after,
                    to_scene=scene.scene_id,
                ))

    def remove_scene(self, scene_id: str) -> bool:
        """Usuwa scenę i wszystkie powiązane edges. Dla linear: łączy sąsiadów."""
        scene = self.get_scene(scene_id)
        if scene is None:
            return False

        # Dla linear: znajdź in-edge i out-edge, połącz.
        if self.graph_type == GraphType.LINEAR:
            in_edge = next((e for e in self.edges if e.to_scene == scene_id), None)
            out_edge = next((e for e in self.edges if e.from_scene == scene_id), None)
            if in_edge and out_edge:
                # Reconnect
                in_edge.to_scene = out_edge.to_scene
                self.edges.remove(out_edge)
            elif in_edge:
                self.edges.remove(in_edge)
            elif out_edge:
                self.edges.remove(out_edge)
        else:
            # Branching: usuń wszystkie edges dotyczące tej sceny
            self.edges = [e for e in self.edges
                          if e.from_scene != scene_id and e.to_scene != scene_id]

        self.scenes.remove(scene)
        return True

    # ── Serialization ──────────────────────────────────────

    def to_dict(self) -> Dict[str, Any]:
        return {
            "graph_id": self.graph_id,
            "name": self.name,
            "graph_type": self.graph_type.value,
            "scenes": [s.to_dict() for s in self.scenes],
            "edges": [e.to_dict() for e in self.edges],
            "portfolio_id": self.portfolio_id,
            "audio_sync": self.audio_sync.to_dict() if self.audio_sync else None,
            "metadata": self.metadata,
        }

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "SceneGraph":
        return cls(
            graph_id=d["graph_id"],
            name=d.get("name", ""),
            graph_type=GraphType(d.get("graph_type", "linear")),
            scenes=[Scene.from_dict(x) for x in d.get("scenes", [])],
            edges=[SceneEdge.from_dict(x) for x in d.get("edges", [])],
            portfolio_id=d.get("portfolio_id"),
            audio_sync=AudioSyncSpec.from_dict(d["audio_sync"])
                       if d.get("audio_sync") else None,
            metadata=dict(d.get("metadata", {})),
        )

    def save(self, graphs_dir: Path) -> Path:
        path = Path(graphs_dir) / f"{self.graph_id}.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.to_dict(), indent=2), encoding="utf-8")
        return path

    @classmethod
    def load(cls, graphs_dir: Path, graph_id: str) -> Optional["SceneGraph"]:
        path = Path(graphs_dir) / f"{graph_id}.json"
        if not path.exists():
            return None
        try:
            return cls.from_dict(json.loads(path.read_text(encoding="utf-8")))
        except Exception:
            log.exception("Failed to load scene graph from %s", path)
            return None


# ─── Factory helpers ────────────────────────────────────────────

def new_graph(name: str, graph_type: GraphType = GraphType.LINEAR,
              portfolio_id: Optional[str] = None) -> SceneGraph:
    """Tworzy pusty scene graph. ID convention: g_ + 12-char hex (jak c_, m_, n_)."""
    return SceneGraph(
        graph_id=f"g_{uuid.uuid4().hex[:12]}",
        name=name,
        graph_type=graph_type,
        portfolio_id=portfolio_id,
    )


def new_scene(description: str, duration_s: float = 4.0,
              anchors: Optional[List[str]] = None) -> Scene:
    """Tworzy nową scenę. ID: sc_ + 12-char hex."""
    return Scene(
        scene_id=f"sc_{uuid.uuid4().hex[:12]}",
        description=description,
        duration_seconds=duration_s,
        anchors=list(anchors or []),
    )


def new_portfolio(name: str) -> "Portfolio":
    """Tworzy nowe portfolio. ID: pf_ + 12-char hex."""
    return Portfolio(
        portfolio_id=f"pf_{uuid.uuid4().hex[:12]}",
        name=name,
    )


def new_portfolio_item(name: str, role: PortfolioItemRole, file_path: str,
                        description: str = "") -> PortfolioItem:
    """Tworzy portfolio item. ID: pi_ + 12-char hex."""
    return PortfolioItem(
        item_id=f"pi_{uuid.uuid4().hex[:12]}",
        name=name,
        role=role,
        file_path=file_path,
        description=description,
    )
