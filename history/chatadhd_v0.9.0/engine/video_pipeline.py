"""
engine/video_pipeline.py
=========================

Główny silnik generowania filmów. Cztery fazy:

  Faza 1 — Struktura     : scene graph + portfolio + constraints (text-only, tanie)
  Faza 2 — Klucze        : keyframes dla każdej sceny (image-gen)
  Faza 3 — Ruch          : clips z keyframes (image→video, drogie)
  Faza 4 — Skład         : compositor — concat, synchronizacja audio, przejścia

Każda faza jest niezależnie wywoływalna i re-runowalna. Artefakty na dysku.

Zgodność z kodeksem:
  - Problem → meta-rozwiązanie first: najpierw struktura, potem klucze, potem ruch.
    Każdy etap weryfikuje spójność przed uruchomieniem droższego następnego.
  - Fallback z metadanymi: gdy model padnie, próbuje następnego z listy.
    Wszystkie próby logowane (model, czas, koszt, wynik).
  - Invariant: każda scena ma ≥1 anchor z portfolio (hard constraint).
  - Event bus: wszystkie fazy publikują zdarzenia; panele subskrybują.
  - Separacja: IGenerator (abstrakcyjny) vs. konkretne wywołania API.
"""

from __future__ import annotations

import logging
import time
import uuid
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple

from engine.scene_graph import (
    SceneGraph, Scene, SceneState, Portfolio, PortfolioItem,
    AudioSyncSpec, GraphType, new_graph, new_scene,
)
from engine.models_video import (
    VideoModelRegistry, VideoModel, ModelSelectionSpec, ModelModality,
)
from engine.compositor import ICompositor, create_compositor, CompositionResult
from engine.async_jobs import (
    AsyncJobManager, AsyncJob, JobStatus,
    EVT_JOB_COMPLETED, EVT_JOB_FAILED,
)

log = logging.getLogger(__name__)


# ─── Event names (constants) ────────────────────────────────────
# Convention z engine/events.py: dwukropek jako separator.

EVT_GRAPH_CREATED = "video:graph:created"
EVT_GRAPH_UPDATED = "video:graph:updated"
EVT_SCENE_KEYFRAME_STARTED = "video:scene:keyframe:started"
EVT_SCENE_KEYFRAME_READY = "video:scene:keyframe:ready"
EVT_SCENE_KEYFRAME_FAILED = "video:scene:keyframe:failed"
EVT_SCENE_CLIP_STARTED = "video:scene:clip:started"
EVT_SCENE_CLIP_READY = "video:scene:clip:ready"
EVT_SCENE_CLIP_FAILED = "video:scene:clip:failed"
EVT_COMPOSE_STARTED = "video:compose:started"
EVT_COMPOSE_READY = "video:compose:ready"
EVT_COMPOSE_FAILED = "video:compose:failed"
EVT_COHERENCE_MEASURED = "video:coherence:measured"


# ─── Interfaces ─────────────────────────────────────────────────

class IGenerator(ABC):
    """
    Abstrakcyjny generator — wywołuje konkretny model.
    Implementacje: OpenRouterGenerator, ReplicateGenerator, LocalGenerator.

    Invariant: metoda zwraca (ścieżkę do artefaktu, koszt, czas) albo rzuca.
    Nigdy nie modyfikuje scene_graph bezpośrednio — to zadanie pipeline'u.
    """

    @abstractmethod
    def generate_image(self, prompt: str, model: VideoModel, *,
                       reference_images: Optional[List[Path]] = None,
                       output_path: Path,
                       width: int = 1024,
                       height: int = 1024,
                       seed: Optional[int] = None,
                       **extra: Any) -> "GenerationResult":
        ...

    @abstractmethod
    def generate_video(self, prompt: str, model: VideoModel, *,
                       keyframe: Optional[Path] = None,
                       output_path: Path,
                       duration_s: float = 4.0,
                       width: int = 1280,
                       height: int = 720,
                       seed: Optional[int] = None,
                       **extra: Any) -> "GenerationResult":
        ...


@dataclass
class GenerationResult:
    """Wynik pojedynczej generacji."""
    output_path: Path
    cost_usd: float
    duration_s: float  # czas generowania
    model_used: str
    raw_response: Dict[str, Any] = field(default_factory=dict)


class IVisualEmbedding(ABC):
    """Abstrakcja embedingu wizualnego do coherence check."""

    @abstractmethod
    def embed_image(self, path: Path) -> List[float]:
        ...

    @abstractmethod
    def embed_video(self, path: Path) -> List[float]:
        """Średnia embedding klatek albo dedykowany video-embedding."""
        ...

    @abstractmethod
    def distance(self, a: List[float], b: List[float]) -> float:
        """Cosine distance (0 = identyczne, 1 = ortogonalne)."""
        ...


# ─── Publisher adapter ──────────────────────────────────────────

class EventPublisher:
    """
    Adapter do event busa ChatADHD.

    Event bus w engine/events.py to singleton `bus` z metodą `emit(event, data)`.
    Stałe eventów są w tym samym module (MSG_CREATED, GRAPH_CHANGED, ...).
    Dodajemy nasze VIDEO_* eventy do tej samej przestrzeni.

    Jeśli events.py niedostępny (testy poza ChatADHD), log-only.
    """

    def __init__(self) -> None:
        self._bus = None
        try:
            from engine.events import bus  # type: ignore
            self._bus = bus
        except ImportError:
            log.info("engine.events not available; EventPublisher will log-only")

    def publish(self, event: str, payload: Dict[str, Any]) -> None:
        log.debug("Event: %s payload=%s", event, payload)
        if self._bus is not None:
            try:
                self._bus.emit(event, payload)
            except Exception:
                log.exception("Event bus emit failed for %s", event)


# ─── Pipeline ───────────────────────────────────────────────────

@dataclass
class CoherenceReport:
    """Raport spójności dla grafu."""
    graph_id: str
    per_scene_distances: Dict[str, float] = field(default_factory=dict)
    threshold: float = 0.3
    outliers: List[str] = field(default_factory=list)  # scene_ids które przekroczyły threshold
    overall_mean: float = 0.0


@dataclass
class CostEstimate:
    """Estymacja kosztów dla fazy grafu."""
    graph_id: str
    phase: int
    estimated_usd: float
    per_scene_estimate: Dict[str, float] = field(default_factory=dict)


class VideoPipeline:
    """
    Główny silnik. Koordynuje fazy, wybiera modele, mierzy spójność, publikuje zdarzenia.

    Użycie:
        pipeline = VideoPipeline(
            data_dir=Path("~/Documents/ChatADHD").expanduser(),
            registry=model_registry,
            generator=openrouter_generator,
            embedding_provider=clip_provider,
        )

        graph = pipeline.create_scene_graph(
            name="AoD - Krew jak smoła",
            scenes=[...],
            portfolio=aod_portfolio,
            audio_sync=krew_jak_smola_sync_spec,
        )

        for scene in graph.scenes:
            pipeline.generate_keyframe(graph, scene.scene_id)

        report = pipeline.measure_coherence(graph)
        if report.outliers:
            for sid in report.outliers:
                pipeline.regenerate_keyframe(graph, sid)

        for scene in graph.scenes:
            pipeline.generate_clip(graph, scene.scene_id)

        final_path = pipeline.compose_film(graph)
    """

    def __init__(self, data_dir: Optional[Path] = None,
                 *,
                 registry: VideoModelRegistry,
                 generator: IGenerator,
                 embedding_provider: IVisualEmbedding,
                 compositor: Optional[ICompositor] = None,
                 job_manager: Optional[AsyncJobManager] = None,
                 coherence_threshold: float = 0.3,
                 max_fallback_attempts: int = 3) -> None:
        # Integracja z KOD≠DANE: jeśli data_dir nie podany, użyj istniejącego
        # resolvera z engine/paths.py.
        if data_dir is None:
            try:
                from engine.paths import resolve_data_dir
                data_dir = resolve_data_dir()
            except ImportError:
                raise ValueError(
                    "No data_dir provided and engine.paths.resolve_data_dir "
                    "not available. Pass data_dir explicitly."
                )

        self._data_dir = Path(data_dir).expanduser()
        self._registry = registry
        self._generator = generator
        self._embed = embedding_provider
        self._compositor = compositor or create_compositor()
        # Async job manager dla długich operacji (OR video async API).
        # Opcjonalny — dla synchronicznych testów można pominąć.
        self._job_manager = job_manager
        self._coherence_threshold = coherence_threshold
        self._max_fallback = max_fallback_attempts
        self._publisher = EventPublisher()

        # Directory layout
        self._video_dir = self._data_dir / "video"
        self._graphs_dir = self._video_dir / "graphs"
        self._keyframes_dir = self._video_dir / "keyframes"
        self._clips_dir = self._video_dir / "clips"
        self._embeddings_dir = self._video_dir / "embeddings"
        self._renders_dir = self._video_dir / "renders"
        self._portfolios_dir = self._video_dir / "portfolios"

        for d in (self._graphs_dir, self._keyframes_dir, self._clips_dir,
                  self._embeddings_dir, self._renders_dir, self._portfolios_dir):
            d.mkdir(parents=True, exist_ok=True)

    # ─── Directory accessors ──────────────────────────────

    def graphs_dir(self) -> Path:
        return self._graphs_dir

    def portfolios_dir(self) -> Path:
        return self._portfolios_dir

    # ─── FAZA 1: Struktura ────────────────────────────────

    def create_scene_graph(self, name: str, *,
                            scenes: Optional[List[Scene]] = None,
                            portfolio: Optional[Portfolio] = None,
                            audio_sync: Optional[AudioSyncSpec] = None,
                            graph_type: GraphType = GraphType.LINEAR) -> SceneGraph:
        graph = new_graph(name, graph_type=graph_type,
                          portfolio_id=portfolio.portfolio_id if portfolio else None)
        if scenes:
            # Linearny — łączymy po kolei.
            prev_id: Optional[str] = None
            for sc in scenes:
                if prev_id is None:
                    graph.scenes.append(sc)
                else:
                    graph.add_scene(sc, after=prev_id)
                prev_id = sc.scene_id

        if audio_sync:
            graph.audio_sync = audio_sync

        # Persist.
        graph.save(self._graphs_dir)
        if portfolio:
            self._save_portfolio(portfolio)

        self._publisher.publish(EVT_GRAPH_CREATED, {
            "graph_id": graph.graph_id,
            "name": graph.name,
            "scenes_count": len(graph.scenes),
        })
        return graph

    def save_graph(self, graph: SceneGraph) -> None:
        graph.save(self._graphs_dir)
        self._publisher.publish(EVT_GRAPH_UPDATED, {"graph_id": graph.graph_id})

    def load_graph(self, graph_id: str) -> Optional[SceneGraph]:
        return SceneGraph.load(self._graphs_dir, graph_id)

    def _save_portfolio(self, portfolio: Portfolio) -> None:
        import json
        path = self._portfolios_dir / f"{portfolio.portfolio_id}.json"
        path.write_text(json.dumps(portfolio.to_dict(), indent=2), encoding="utf-8")

    def load_portfolio(self, portfolio_id: str) -> Optional[Portfolio]:
        import json
        path = self._portfolios_dir / f"{portfolio_id}.json"
        if not path.exists():
            return None
        try:
            return Portfolio.from_dict(json.loads(path.read_text(encoding="utf-8")))
        except Exception:
            log.exception("Failed to load portfolio %s", portfolio_id)
            return None

    # ─── FAZA 2: Klucze ───────────────────────────────────

    def generate_keyframe(self, graph: SceneGraph, scene_id: str, *,
                          model_hint: Optional[str] = None) -> Scene:
        scene = graph.get_scene(scene_id)
        if scene is None:
            raise ValueError(f"Scene {scene_id} not in graph {graph.graph_id}")

        errs = scene.validate()
        if errs:
            log.warning("Scene %s has validation errors: %s", scene_id, errs)
            scene.state = SceneState.FLAGGED
            return scene

        self._publisher.publish(EVT_SCENE_KEYFRAME_STARTED, {
            "graph_id": graph.graph_id, "scene_id": scene_id,
        })

        # Kompletacja prompta z anchorami
        portfolio = (self.load_portfolio(graph.portfolio_id)
                     if graph.portfolio_id else None)
        anchor_paths = self._resolve_anchor_paths(portfolio, scene.anchors)
        prompt = self._build_keyframe_prompt(scene, portfolio)

        # Wybór modelu — fallback chain
        spec = ModelSelectionSpec(
            required_modality=(ModelModality.IMAGE_TO_IMAGE
                               if anchor_paths
                               else ModelModality.TEXT_TO_IMAGE),
            need_character_consistency=len(scene.characters) > 0,
            domain_hint=scene.constraints.get("quality_domain"),
        )
        candidates = self._registry.select(spec)
        if model_hint:
            hinted = self._registry.get(model_hint)
            if hinted and hinted in candidates:
                candidates = [hinted] + [c for c in candidates if c.model_id != model_hint]

        if not candidates:
            scene.state = SceneState.FLAGGED
            self._publisher.publish(EVT_SCENE_KEYFRAME_FAILED, {
                "graph_id": graph.graph_id, "scene_id": scene_id,
                "reason": "no_model_matches_spec",
            })
            return scene

        # Output path
        output_path = self._keyframes_dir / f"{scene_id}.png"

        # Try fallback chain
        last_error: Optional[str] = None
        for model in candidates[: self._max_fallback]:
            try:
                scene.artifacts.attempts += 1
                result = self._generator.generate_image(
                    prompt=prompt,
                    model=model,
                    reference_images=anchor_paths,
                    output_path=output_path,
                    width=scene.constraints.get("width", 1280),
                    height=scene.constraints.get("height", 720),
                )
            except Exception as e:
                last_error = str(e)
                log.exception("Keyframe generation failed with model %s", model.model_id)
                continue

            # Coherence check vs. anchors
            emb = self._embed.embed_image(result.output_path)
            distances = [self._embed.distance(emb, self._get_anchor_embedding(portfolio, aid))
                         for aid in scene.anchors
                         if portfolio and self._get_anchor_embedding(portfolio, aid) is not None]
            min_dist = min(distances) if distances else 0.0

            if distances and min_dist > self._coherence_threshold:
                log.info("Keyframe coherence too low (%.3f > %.3f) for scene %s "
                         "with model %s; trying next.",
                         min_dist, self._coherence_threshold, scene_id, model.model_id)
                last_error = f"coherence_too_low: {min_dist:.3f}"
                continue

            # Success
            scene.artifacts.keyframe_path = str(result.output_path.relative_to(self._data_dir))
            scene.artifacts.embedding_keyframe = emb
            scene.artifacts.model_used_keyframe = model.model_id
            scene.artifacts.generation_cost_usd += result.cost_usd
            scene.artifacts.generation_time_s += result.duration_s
            scene.state = SceneState.KEYFRAME_READY
            graph.save(self._graphs_dir)
            self._publisher.publish(EVT_SCENE_KEYFRAME_READY, {
                "graph_id": graph.graph_id, "scene_id": scene_id,
                "model": model.model_id,
                "coherence_distance": min_dist,
                "keyframe_path": scene.artifacts.keyframe_path,
            })
            return scene

        # All failed
        scene.state = SceneState.KEYFRAME_FAILED
        graph.save(self._graphs_dir)
        self._publisher.publish(EVT_SCENE_KEYFRAME_FAILED, {
            "graph_id": graph.graph_id, "scene_id": scene_id,
            "reason": last_error or "unknown",
            "attempts": scene.artifacts.attempts,
        })
        return scene

    def regenerate_keyframe(self, graph: SceneGraph, scene_id: str,
                             feedback: str = "") -> Scene:
        scene = graph.get_scene(scene_id)
        if scene is None:
            raise ValueError(f"Scene {scene_id} not in graph {graph.graph_id}")
        # Zachowujemy feedback jako constraint — wpłynie na prompt next time.
        if feedback:
            existing = scene.constraints.get("regen_feedback", [])
            existing.append(feedback)
            scene.constraints["regen_feedback"] = existing
        scene.state = SceneState.PENDING
        return self.generate_keyframe(graph, scene_id)

    # ─── FAZA 3: Ruch ─────────────────────────────────────

    def generate_clip(self, graph: SceneGraph, scene_id: str, *,
                      model_hint: Optional[str] = None) -> Scene:
        scene = graph.get_scene(scene_id)
        if scene is None:
            raise ValueError(f"Scene {scene_id} not in graph {graph.graph_id}")

        if scene.state != SceneState.KEYFRAME_READY:
            log.warning("Scene %s not ready for clip generation (state=%s)",
                        scene_id, scene.state)
            scene.state = SceneState.FLAGGED
            return scene

        self._publisher.publish(EVT_SCENE_CLIP_STARTED, {
            "graph_id": graph.graph_id, "scene_id": scene_id,
        })

        keyframe_path = self._data_dir / scene.artifacts.keyframe_path
        prompt = self._build_clip_prompt(scene)

        spec = ModelSelectionSpec(
            required_modality=ModelModality.IMAGE_TO_VIDEO,
            min_duration_s=scene.duration_seconds,
            need_character_consistency=len(scene.characters) > 0,
        )
        candidates = self._registry.select(spec)
        if model_hint:
            hinted = self._registry.get(model_hint)
            if hinted and hinted in candidates:
                candidates = [hinted] + [c for c in candidates if c.model_id != model_hint]

        if not candidates:
            scene.state = SceneState.FLAGGED
            self._publisher.publish(EVT_SCENE_CLIP_FAILED, {
                "graph_id": graph.graph_id, "scene_id": scene_id,
                "reason": "no_model_matches_spec",
            })
            return scene

        output_path = self._clips_dir / f"{scene_id}.mp4"
        last_error: Optional[str] = None

        for model in candidates[: self._max_fallback]:
            try:
                scene.artifacts.attempts += 1
                result = self._generator.generate_video(
                    prompt=prompt,
                    model=model,
                    keyframe=keyframe_path,
                    output_path=output_path,
                    duration_s=scene.duration_seconds,
                )
            except Exception as e:
                last_error = str(e)
                log.exception("Clip generation failed with model %s", model.model_id)
                continue

            # Coherence check — embedding klipu vs. keyframe
            emb = self._embed.embed_video(result.output_path)
            if scene.artifacts.embedding_keyframe is not None:
                dist = self._embed.distance(emb, scene.artifacts.embedding_keyframe)
                if dist > self._coherence_threshold * 1.5:  # bardziej wyrozumiały próg dla klipów
                    log.info("Clip coherence low (%.3f) for scene %s; trying next model",
                             dist, scene_id)
                    last_error = f"coherence_too_low: {dist:.3f}"
                    continue

            scene.artifacts.clip_path = str(result.output_path.relative_to(self._data_dir))
            scene.artifacts.embedding_clip = emb
            scene.artifacts.model_used_clip = model.model_id
            scene.artifacts.generation_cost_usd += result.cost_usd
            scene.artifacts.generation_time_s += result.duration_s
            scene.state = SceneState.CLIP_READY
            graph.save(self._graphs_dir)
            self._publisher.publish(EVT_SCENE_CLIP_READY, {
                "graph_id": graph.graph_id, "scene_id": scene_id,
                "model": model.model_id,
                "clip_path": scene.artifacts.clip_path,
            })
            return scene

        scene.state = SceneState.CLIP_FAILED
        graph.save(self._graphs_dir)
        self._publisher.publish(EVT_SCENE_CLIP_FAILED, {
            "graph_id": graph.graph_id, "scene_id": scene_id,
            "reason": last_error or "unknown",
            "attempts": scene.artifacts.attempts,
        })
        return scene

    # ─── FAZA 3 (async): OR Video API async submit + poll ────

    def submit_clip_async(self, graph: SceneGraph, scene_id: str, *,
                          model_hint: Optional[str] = None) -> Optional[str]:
        """
        Async wariant generate_clip. Używa AsyncJobManager.

        Przepływ:
          1. Walidacja sceny (keyframe ready).
          2. Wybór modelu z fallback chain.
          3. Budowa request_payload zgodnego z OR /videos API.
          4. Submit przez job_manager.
          5. Job poll w tle; EVT_JOB_COMPLETED handler (on_job_event) aktualizuje
             scenę i publikuje EVT_SCENE_CLIP_READY.

        Zwraca job_id albo None jeśli nie udało się zsubmitować.

        Wymaga self._job_manager != None.
        """
        if self._job_manager is None:
            log.warning("submit_clip_async called but no job_manager; "
                        "falling back to synchronous generate_clip")
            self.generate_clip(graph, scene_id, model_hint=model_hint)
            return None

        scene = graph.get_scene(scene_id)
        if scene is None:
            raise ValueError(f"Scene {scene_id} not in graph {graph.graph_id}")

        if scene.state != SceneState.KEYFRAME_READY:
            log.warning("Scene %s not ready for clip generation (state=%s)",
                        scene_id, scene.state)
            scene.state = SceneState.FLAGGED
            return None

        # Model selection
        spec = ModelSelectionSpec(
            required_modality=ModelModality.IMAGE_TO_VIDEO,
            min_duration_s=scene.duration_seconds,
            need_character_consistency=len(scene.characters) > 0,
        )
        candidates = self._registry.select(spec)
        if model_hint:
            hinted = self._registry.get(model_hint)
            if hinted and hinted in candidates:
                candidates = [hinted] + [c for c in candidates
                                         if c.model_id != model_hint]
        if not candidates:
            scene.state = SceneState.FLAGGED
            return None

        model = candidates[0]

        # Build OR video API payload
        prompt = self._build_clip_prompt(scene)
        payload: Dict[str, Any] = {
            "model": model.provider_model_name or model.model_id,
            "prompt": prompt,
            "duration": int(max(1, round(scene.duration_seconds))),
        }

        # frame_images z keyframe jako first_frame (I2V mode).
        if scene.artifacts.keyframe_path:
            keyframe_full = str((self._data_dir
                                  / scene.artifacts.keyframe_path).absolute())
            # OR przyjmuje data-URL albo public URL. Dla lokalnych plików
            # powinniśmy upload-ować albo kodować base64. Na MVP:
            # zakładamy że keyframe jest dostępny przez generator (który
            # może go wrzucić na staging URL). Jeśli nie — payload zostaje
            # z samym promptem (T2V fallback).
            # TODO: decyzja user (base64 vs staging URL) w nastepnej iteracji.
            pass  # zostawiam jako TODO; pipeline działa T2V nawet bez keyframe

        # Submit
        job = self._job_manager.submit(
            kind="video_clip",
            provider="openrouter:video",
            request_payload=payload,
            context={
                "graph_id": graph.graph_id,
                "scene_id": scene_id,
                "model_id": model.model_id,
            },
            provider_model=model.model_id,
            max_poll_seconds=30 * 60,  # 30 min — typowe OR video max
        )

        if job.status == JobStatus.FAILED:
            scene.state = SceneState.CLIP_FAILED
            scene.artifacts.attempts += 1
            graph.save(self._graphs_dir)
            self._publisher.publish(EVT_SCENE_CLIP_FAILED, {
                "graph_id": graph.graph_id, "scene_id": scene_id,
                "reason": job.error,
            })
            return None

        # Job in flight. Pipeline nasłuchuje EVT_JOB_COMPLETED —
        # patrz on_job_event().
        self._publisher.publish(EVT_SCENE_CLIP_STARTED, {
            "graph_id": graph.graph_id, "scene_id": scene_id,
            "job_id": job.job_id,
        })
        return job.job_id

    def on_job_event(self, event_name: str, job_dict: Dict[str, Any]) -> None:
        """
        Handler eventów z AsyncJobManager. Wywołuje pipeline-side update
        scen na podstawie ukończonego job'a.

        Registration (w main.py):
            bus.on("job:completed", pipeline.on_job_event)
            bus.on("job:failed", pipeline.on_job_event)

        Używamy context z job'a (graph_id, scene_id) żeby znaleźć scenę.
        """
        ctx = job_dict.get("context") or {}
        graph_id = ctx.get("graph_id")
        scene_id = ctx.get("scene_id")
        job_kind = job_dict.get("job_kind")

        if job_kind != "video_clip" or not graph_id or not scene_id:
            return  # nie nasze

        graph = self.load_graph(graph_id)
        if graph is None:
            log.warning("on_job_event: graph %s not found", graph_id)
            return
        scene = graph.get_scene(scene_id)
        if scene is None:
            log.warning("on_job_event: scene %s not in graph %s",
                        scene_id, graph_id)
            return

        status = job_dict.get("status")
        if status == "completed":
            artifact = job_dict.get("local_artifact_path")
            if not artifact:
                log.warning("job completed but no local_artifact_path: %s",
                            job_dict.get("job_id"))
                return
            # Kopiuj / przenoś do właściwej lokalizacji w data/video/clips/
            target = self._clips_dir / f"{scene_id}.mp4"
            try:
                import shutil
                shutil.copy(artifact, target)
            except Exception:
                log.exception("Failed to copy artifact to clips dir")
                target = Path(artifact)

            # Embed dla coherence
            try:
                emb = self._embed.embed_video(target)
            except Exception:
                log.exception("Failed to embed clip; setting None")
                emb = None

            scene.artifacts.clip_path = str(target.relative_to(self._data_dir))
            scene.artifacts.embedding_clip = emb
            scene.artifacts.model_used_clip = ctx.get("model_id")
            scene.artifacts.generation_cost_usd += float(
                job_dict.get("cost_usd") or 0.0)
            scene.state = SceneState.CLIP_READY
            graph.save(self._graphs_dir)

            self._publisher.publish(EVT_SCENE_CLIP_READY, {
                "graph_id": graph_id,
                "scene_id": scene_id,
                "model": ctx.get("model_id"),
                "clip_path": scene.artifacts.clip_path,
                "job_id": job_dict.get("job_id"),
            })

        elif status in ("failed", "stale", "cancelled"):
            scene.state = SceneState.CLIP_FAILED
            scene.artifacts.attempts += 1
            graph.save(self._graphs_dir)
            self._publisher.publish(EVT_SCENE_CLIP_FAILED, {
                "graph_id": graph_id,
                "scene_id": scene_id,
                "reason": job_dict.get("error") or status,
                "job_id": job_dict.get("job_id"),
            })

    # ─── FAZA 4: Skład ────────────────────────────────────

    def compose_film(self, graph: SceneGraph, *,
                     audio_override: Optional[Path] = None,
                     output_path: Optional[Path] = None) -> Optional[Path]:
        """
        MVP: deleguje do ICompositora (imageio-ffmpeg → system → export-only).
        Polirytmiczny zoom (AoD): TODO w 0.9.0-b1 jako effect w compositorze.

        Zwraca ścieżkę do wynikowego mp4, albo ścieżkę do script'u (export-only),
        albo None gdy coś poszło nie tak.
        """
        self._publisher.publish(EVT_COMPOSE_STARTED, {"graph_id": graph.graph_id})

        # Zbierz klipy w kolejności
        ordered_scenes = self._topo_order(graph)
        missing = [s.scene_id for s in ordered_scenes
                   if s.state != SceneState.CLIP_READY]
        if missing:
            self._publisher.publish(EVT_COMPOSE_FAILED, {
                "graph_id": graph.graph_id,
                "reason": "clips_missing",
                "missing_scene_ids": missing,
            })
            return None

        clip_paths = [self._data_dir / s.artifacts.clip_path for s in ordered_scenes]

        if output_path is None:
            ts = int(time.time())
            output_path = self._renders_dir / f"{graph.graph_id}_{ts}.mp4"

        audio_path = audio_override
        if audio_path is None and graph.audio_sync:
            audio_path = self._data_dir / graph.audio_sync.audio_file

        result: CompositionResult = self._compositor.concat(
            clip_paths, output_path, audio=audio_path
        )

        if not result.success:
            self._publisher.publish(EVT_COMPOSE_FAILED, {
                "graph_id": graph.graph_id,
                "reason": result.error,
                "backend_used": result.backend_used,
                "diagnostics": result.diagnostics,
            })
            return None

        final_path = result.output_path or result.script_path
        self._publisher.publish(EVT_COMPOSE_READY, {
            "graph_id": graph.graph_id,
            "output_path": str(final_path),
            "backend_used": result.backend_used,
            "diagnostics": result.diagnostics,
        })
        return final_path

    # ─── Diagnostics ──────────────────────────────────────

    def measure_coherence(self, graph: SceneGraph) -> CoherenceReport:
        """Liczy spójność dla wszystkich scen z klipami/keyframes."""
        portfolio = (self.load_portfolio(graph.portfolio_id)
                     if graph.portfolio_id else None)
        report = CoherenceReport(graph_id=graph.graph_id,
                                  threshold=self._coherence_threshold)
        distances: List[float] = []
        for scene in graph.scenes:
            emb = scene.artifacts.embedding_clip or scene.artifacts.embedding_keyframe
            if emb is None:
                continue
            per_anchor = [
                self._embed.distance(emb, self._get_anchor_embedding(portfolio, aid))
                for aid in scene.anchors
                if portfolio and self._get_anchor_embedding(portfolio, aid) is not None
            ]
            if not per_anchor:
                continue
            min_d = min(per_anchor)
            report.per_scene_distances[scene.scene_id] = min_d
            distances.append(min_d)
            if min_d > self._coherence_threshold:
                report.outliers.append(scene.scene_id)

        report.overall_mean = sum(distances) / len(distances) if distances else 0.0
        self._publisher.publish(EVT_COHERENCE_MEASURED, {
            "graph_id": graph.graph_id,
            "mean": report.overall_mean,
            "outlier_count": len(report.outliers),
        })
        return report

    def get_cost_estimate(self, graph: SceneGraph, phase: int) -> CostEstimate:
        """Estymacja kosztów dla fazy (2 = keyframes, 3 = clips)."""
        total = 0.0
        per_scene: Dict[str, float] = {}

        for scene in graph.scenes:
            if phase == 2:
                spec = ModelSelectionSpec(required_modality=ModelModality.TEXT_TO_IMAGE)
                models = self._registry.select(spec)
                est = models[0].pricing.estimate(n_images=1) if models else 0.0
            elif phase == 3:
                spec = ModelSelectionSpec(
                    required_modality=ModelModality.IMAGE_TO_VIDEO,
                    min_duration_s=scene.duration_seconds,
                )
                models = self._registry.select(spec)
                est = (models[0].pricing.estimate(n_seconds_video=scene.duration_seconds)
                       if models else 0.0)
            else:
                est = 0.0
            per_scene[scene.scene_id] = est
            total += est

        return CostEstimate(graph_id=graph.graph_id, phase=phase,
                            estimated_usd=total, per_scene_estimate=per_scene)

    # ─── Helpers ──────────────────────────────────────────

    def _resolve_anchor_paths(self, portfolio: Optional[Portfolio],
                              anchor_ids: List[str]) -> List[Path]:
        if portfolio is None:
            return []
        paths: List[Path] = []
        for aid in anchor_ids:
            item = portfolio.get(aid)
            if item is None:
                continue
            paths.append(self._data_dir / item.file_path)
        return paths

    def _get_anchor_embedding(self, portfolio: Optional[Portfolio],
                              anchor_id: str) -> Optional[List[float]]:
        if portfolio is None:
            return None
        item = portfolio.get(anchor_id)
        if item is None:
            return None
        if item.embedding is not None:
            return item.embedding
        # Lazy-embed: policz raz, zapisz w portfolio.
        try:
            item.embedding = self._embed.embed_image(self._data_dir / item.file_path)
            self._save_portfolio(portfolio)
            return item.embedding
        except Exception:
            log.exception("Failed to embed anchor %s", anchor_id)
            return None

    def _build_keyframe_prompt(self, scene: Scene,
                                portfolio: Optional[Portfolio]) -> str:
        parts: List[str] = [scene.description]
        if scene.style_tokens:
            parts.append("Style: " + ", ".join(scene.style_tokens))
        if portfolio and scene.location:
            loc = portfolio.get(scene.location)
            if loc:
                parts.append(f"Location: {loc.description or loc.name}")
        if portfolio and scene.characters:
            char_desc = []
            for cid in scene.characters:
                c = portfolio.get(cid)
                if c:
                    char_desc.append(c.description or c.name)
            if char_desc:
                parts.append("Characters: " + "; ".join(char_desc))
        if scene.constraints.get("regen_feedback"):
            parts.append("Previous attempts had issues; now address: "
                         + " / ".join(scene.constraints["regen_feedback"]))
        return "\n".join(parts)

    def _build_clip_prompt(self, scene: Scene) -> str:
        parts: List[str] = [scene.description]
        motion = scene.constraints.get("motion_description")
        if motion:
            parts.append(f"Motion: {motion}")
        camera = scene.constraints.get("camera_description")
        if camera:
            parts.append(f"Camera: {camera}")
        return "\n".join(parts)

    def _topo_order(self, graph: SceneGraph) -> List[Scene]:
        """Dla linear: po prostu w kolejności edges. Dla branching: jedna z valid paths."""
        if not graph.edges:
            return list(graph.scenes)
        # Find entry (no in-edges)
        in_count: Dict[str, int] = {s.scene_id: 0 for s in graph.scenes}
        for e in graph.edges:
            in_count[e.to_scene] = in_count.get(e.to_scene, 0) + 1
        entry = next((sid for sid, c in in_count.items() if c == 0), None)
        if entry is None:
            return list(graph.scenes)

        out_edges: Dict[str, List[str]] = {}
        for e in graph.edges:
            out_edges.setdefault(e.from_scene, []).append(e.to_scene)

        order: List[str] = []
        visited = set()
        current = entry
        while current and current not in visited:
            visited.add(current)
            order.append(current)
            nexts = out_edges.get(current, [])
            current = nexts[0] if nexts else None

        return [graph.get_scene(sid) for sid in order if graph.get_scene(sid)]
