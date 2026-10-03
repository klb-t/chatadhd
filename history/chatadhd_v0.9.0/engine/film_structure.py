"""
engine/film_structure.py
=========================

Rich film structure: 3D-aware representation of a film with scenes, subjects,
cameras, lighting, narration, quotes, motifs, props, and language variants.

Maps 1:1 to data/schemas/film_structure.json. All dataclasses support:
  - to_dict() / from_dict() — JSON round-trip
  - clone() — deep copy via dict round-trip
  - ID conventions: fs_ (structure), sc_ (scene), ch_ (character), loc_ (location),
    pr_ (prop), mo_ (motif), q_ (quote), v_ (variation)

Not consumed by generation models directly. Feeds:
  - PromptCompiler → deterministic prose for Veo/Sora/Wan
  - StructureCritic (future) → JSON + multimodal for critic LLMs
  - SceneGraph (via conversion) → pipeline execution

Design principles:
  - Every struct has extra: Dict[str, Any] — open field per "wszystko można, nic nie trzeba"
  - Curves are dict-preserved (evaluated at render time via expr_eval)
  - Localized strings are Union[str, Dict[str, str]] — single or multilingual
  - Optional everywhere: empty film (just scenes) works, rich film (quotes + motifs + lyrics) works
"""

from __future__ import annotations

import json
import uuid
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, Dict, List, Optional, Union

# LocalizedString: plain str for single-language, dict for multilingual.
# PromptCompiler extracts via _resolve_localized(value, lang).
LocalizedString = Union[str, Dict[str, str]]


# ─── ID generation ──────────────────────────────────────────────

def _new_id(prefix: str) -> str:
    return f"{prefix}_{uuid.uuid4().hex[:12]}"


# ─── Artistic text ──────────────────────────────────────────────

@dataclass
class LyricsSegment:
    segment_id: str = field(default_factory=lambda: _new_id("lyr"))
    text: Optional[LocalizedString] = None
    role: str = "verse"  # verse/chorus/bridge/pre_chorus/intro/outro/spoken/refrain/ad_lib
    linked_scene_ids: List[str] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class ArtisticText:
    """Film-level artistic blocks: prologue, motto, epilogue, lyrics, etc."""
    prologue: Optional[LocalizedString] = None
    motto: Optional[LocalizedString] = None
    epilogue: Optional[LocalizedString] = None
    dedication: Optional[LocalizedString] = None
    tagline: Optional[LocalizedString] = None
    lyrics: List[LyricsSegment] = field(default_factory=list)
    extra: Dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        self.lyrics = [
            v if isinstance(v, LyricsSegment) else LyricsSegment(**v)
            for v in self.lyrics
        ]

    def to_dict(self) -> Dict[str, Any]:
        d: Dict[str, Any] = {}
        for attr in ("prologue", "motto", "epilogue", "dedication", "tagline"):
            v = getattr(self, attr)
            if v is not None:
                d[attr] = v
        if self.lyrics:
            d["lyrics"] = [s.to_dict() for s in self.lyrics]
        if self.extra:
            d["extra"] = dict(self.extra)
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "ArtisticText":
        d = d or {}
        return cls(
            prologue=d.get("prologue"),
            motto=d.get("motto"),
            epilogue=d.get("epilogue"),
            dedication=d.get("dedication"),
            tagline=d.get("tagline"),
            lyrics=[LyricsSegment(**ls) for ls in d.get("lyrics", []) or []],
            extra=dict(d.get("extra", {})),
        )


# ─── Quotes / Citations ─────────────────────────────────────────

@dataclass
class QuoteSource:
    author: str = ""
    title: str = ""
    year: Optional[Union[int, str]] = None
    medium: str = ""  # painting/poem/novel/film/music/text/sculpture/photography/essay/aphorism/scripture/other
    url: str = ""
    raw: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class Quote:
    id: str = field(default_factory=lambda: _new_id("q"))
    text: Optional[LocalizedString] = None
    source: QuoteSource = field(default_factory=QuoteSource)
    relationship: str = ""  # homage/inversion/direct_quote/paraphrase/conceptual_link/...
    visual_reference: str = ""  # portfolio_item_id
    license_note: str = ""
    verified: bool = False  # LLM can propose; only user can set True
    embedding: List[float] = field(default_factory=list)
    extra: Dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        if isinstance(self.source, dict):
            self.source = QuoteSource(**{
                k: self.source.get(k)
                for k in QuoteSource.__dataclass_fields__
                if k in self.source
            })

    def to_dict(self) -> Dict[str, Any]:
        d: Dict[str, Any] = {"id": self.id}
        if self.text is not None:
            d["text"] = self.text
        src = self.source.to_dict()
        if src:
            d["source"] = src
        for attr in ("relationship", "visual_reference", "license_note"):
            v = getattr(self, attr)
            if v:
                d[attr] = v
        d["verified"] = self.verified
        if self.embedding:
            d["embedding"] = list(self.embedding)
        if self.extra:
            d["extra"] = dict(self.extra)
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "Quote":
        src_d = d.get("source") or {}
        return cls(
            id=d.get("id") or _new_id("q"),
            text=d.get("text"),
            source=QuoteSource(**{k: src_d.get(k) for k in QuoteSource.__dataclass_fields__.keys() if k in src_d}),
            relationship=d.get("relationship", ""),
            visual_reference=d.get("visual_reference", ""),
            license_note=d.get("license_note", ""),
            verified=bool(d.get("verified", False)),
            embedding=list(d.get("embedding", []) or []),
            extra=dict(d.get("extra", {})),
        )


# ─── Props ──────────────────────────────────────────────────────

@dataclass
class PropTransformation:
    scene_id: str = ""
    state_description: Optional[LocalizedString] = None
    visual_delta: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class Prop:
    id: str = field(default_factory=lambda: _new_id("pr"))
    name: str = ""
    description: Optional[LocalizedString] = None
    portfolio_item_id: str = ""
    first_appearance_scene_id: str = ""
    narrative_arc: Optional[LocalizedString] = None
    transformation_log: List[PropTransformation] = field(default_factory=list)
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        d: Dict[str, Any] = {"id": self.id, "name": self.name}
        for attr in ("portfolio_item_id", "first_appearance_scene_id"):
            v = getattr(self, attr)
            if v:
                d[attr] = v
        if self.description is not None:
            d["description"] = self.description
        if self.narrative_arc is not None:
            d["narrative_arc"] = self.narrative_arc
        if self.transformation_log:
            d["transformation_log"] = [t.to_dict() for t in self.transformation_log]
        if self.extra:
            d["extra"] = dict(self.extra)
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "Prop":
        return cls(
            id=d.get("id") or _new_id("pr"),
            name=d.get("name", ""),
            description=d.get("description"),
            portfolio_item_id=d.get("portfolio_item_id", ""),
            first_appearance_scene_id=d.get("first_appearance_scene_id", ""),
            narrative_arc=d.get("narrative_arc"),
            transformation_log=[PropTransformation(**t)
                                 for t in d.get("transformation_log", []) or []],
            extra=dict(d.get("extra", {})),
        )


# ─── Motifs ─────────────────────────────────────────────────────

@dataclass
class MotifRecurrence:
    scene_ids: List[str] = field(default_factory=list)
    variation_pattern: str = ""
    rules: List[str] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class Motif:
    id: str = field(default_factory=lambda: _new_id("mo"))
    name: str = ""
    kind: str = "visual"  # visual/sonic/linguistic/narrative/structural/compound
    description: Optional[LocalizedString] = None
    recurrence_schema: MotifRecurrence = field(default_factory=MotifRecurrence)
    associated_citation_ids: List[str] = field(default_factory=list)
    associated_prop_ids: List[str] = field(default_factory=list)
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        d: Dict[str, Any] = {"id": self.id, "name": self.name, "kind": self.kind}
        if self.description is not None:
            d["description"] = self.description
        rec = self.recurrence_schema.to_dict()
        if rec:
            d["recurrence_schema"] = rec
        if self.associated_citation_ids:
            d["associated_citation_ids"] = list(self.associated_citation_ids)
        if self.associated_prop_ids:
            d["associated_prop_ids"] = list(self.associated_prop_ids)
        if self.extra:
            d["extra"] = dict(self.extra)
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "Motif":
        rec_d = d.get("recurrence_schema") or {}
        return cls(
            id=d.get("id") or _new_id("mo"),
            name=d.get("name", ""),
            kind=d.get("kind", "visual"),
            description=d.get("description"),
            recurrence_schema=MotifRecurrence(
                scene_ids=list(rec_d.get("scene_ids", [])),
                variation_pattern=rec_d.get("variation_pattern", ""),
                rules=list(rec_d.get("rules", [])),
            ),
            associated_citation_ids=list(d.get("associated_citation_ids", [])),
            associated_prop_ids=list(d.get("associated_prop_ids", [])),
            extra=dict(d.get("extra", {})),
        )


# ─── Narration ──────────────────────────────────────────────────

@dataclass
class DialogLine:
    text: Optional[LocalizedString] = None
    speaker_character_id: str = ""
    speaker_label: str = ""
    delivery: str = ""
    start_offset_s: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class OnscreenText:
    text: Optional[LocalizedString] = None
    kind: str = "subtitle"  # title/subtitle/insert/sign_in_world/chyron/credit
    position: str = "bottom"  # top/bottom/center/custom
    start_offset_s: float = 0.0
    duration_s: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class LyricsAlignment:
    text: Optional[LocalizedString] = None
    timestamp_s: Optional[float] = None
    beat_index: Optional[int] = None
    emphasis: str = "normal"  # normal/soft/accent/roared
    linked_lyrics_segment_id: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class Narration:
    voiceover: Optional[LocalizedString] = None
    dialog: List[DialogLine] = field(default_factory=list)
    onscreen_text: List[OnscreenText] = field(default_factory=list)
    lyrics_aligned: List[LyricsAlignment] = field(default_factory=list)
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        d: Dict[str, Any] = {}
        if self.voiceover is not None:
            d["voiceover"] = self.voiceover
        if self.dialog:
            d["dialog"] = [x.to_dict() for x in self.dialog]
        if self.onscreen_text:
            d["onscreen_text"] = [x.to_dict() for x in self.onscreen_text]
        if self.lyrics_aligned:
            d["lyrics_aligned"] = [x.to_dict() for x in self.lyrics_aligned]
        if self.extra:
            d["extra"] = dict(self.extra)
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "Narration":
        d = d or {}
        return cls(
            voiceover=d.get("voiceover"),
            dialog=[DialogLine(**x) for x in d.get("dialog", []) or []],
            onscreen_text=[OnscreenText(**x) for x in d.get("onscreen_text", []) or []],
            lyrics_aligned=[LyricsAlignment(**x) for x in d.get("lyrics_aligned", []) or []],
            extra=dict(d.get("extra", {})),
        )

    def is_empty(self) -> bool:
        return (self.voiceover is None
                and not self.dialog and not self.onscreen_text
                and not self.lyrics_aligned and not self.extra)


# ─── Characters / Locations ─────────────────────────────────────

@dataclass
class Character:
    id: str = field(default_factory=lambda: _new_id("ch"))
    name: str = ""
    role: str = ""
    portfolio_item_id: str = ""
    appearance: str = ""
    voice_profile: str = ""
    language_specific: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "Character":
        return cls(
            id=d.get("id") or _new_id("ch"),
            name=d.get("name", ""),
            role=d.get("role", ""),
            portfolio_item_id=d.get("portfolio_item_id", ""),
            appearance=d.get("appearance", ""),
            voice_profile=d.get("voice_profile", ""),
            language_specific=dict(d.get("language_specific", {})),
            extra=dict(d.get("extra", {})),
        )


@dataclass
class LightingBaseline:
    time_of_day: str = ""
    color_temperature_k: Optional[float] = None
    intensity: Optional[float] = None
    direction: List[float] = field(default_factory=list)
    weather: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class Location:
    id: str = field(default_factory=lambda: _new_id("loc"))
    name: str = ""
    description: str = ""
    portfolio_item_id: str = ""
    lighting_baseline: LightingBaseline = field(default_factory=LightingBaseline)
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        d: Dict[str, Any] = {"id": self.id, "name": self.name}
        if self.description:
            d["description"] = self.description
        if self.portfolio_item_id:
            d["portfolio_item_id"] = self.portfolio_item_id
        lb = self.lighting_baseline.to_dict()
        if lb:
            d["lighting_baseline"] = lb
        if self.extra:
            d["extra"] = dict(self.extra)
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "Location":
        lb_d = d.get("lighting_baseline") or {}
        return cls(
            id=d.get("id") or _new_id("loc"),
            name=d.get("name", ""),
            description=d.get("description", ""),
            portfolio_item_id=d.get("portfolio_item_id", ""),
            lighting_baseline=LightingBaseline(
                time_of_day=lb_d.get("time_of_day", ""),
                color_temperature_k=lb_d.get("color_temperature_k"),
                intensity=lb_d.get("intensity"),
                direction=list(lb_d.get("direction", [])),
                weather=lb_d.get("weather", ""),
            ),
            extra=dict(d.get("extra", {})),
        )


# ─── Subjects / Camera / Lighting / Environment (per scene) ────

@dataclass
class Subject:
    """Actor or object in a scene. Transform and material preserved as dict-curves."""
    id: str = ""
    character_id: str = ""
    prop_id: str = ""  # optional link to top-level Prop
    label: str = ""
    transform: Dict[str, Any] = field(default_factory=dict)  # vec3_curve / scalar_curve preserved raw
    material: Dict[str, Any] = field(default_factory=dict)
    behavior: Dict[str, Any] = field(default_factory=dict)
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class Camera:
    path: Dict[str, Any] = field(default_factory=dict)  # camera_path preserved raw
    fov_degrees: Any = None  # scalar_curve or number
    focus_distance: Any = None
    aperture: Any = None
    lens_qualitative: str = ""
    shake: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        d = {}
        if self.path:
            d["path"] = self.path
        for attr in ("fov_degrees", "focus_distance", "aperture"):
            v = getattr(self, attr)
            if v is not None:
                d[attr] = v
        if self.lens_qualitative:
            d["lens_qualitative"] = self.lens_qualitative
        if self.shake:
            d["shake"] = self.shake
        return d


@dataclass
class SceneLighting:
    primary_direction: List[float] = field(default_factory=list)
    color_temperature_k: Any = None
    intensity: Any = None
    time_of_day: str = ""
    ambient: Dict[str, Any] = field(default_factory=dict)
    qualitative: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class SceneEnvironment:
    location_id: str = ""
    weather: str = ""
    particulates: Dict[str, Any] = field(default_factory=dict)
    ambient_sound_cues: List[str] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


# ─── Scene ──────────────────────────────────────────────────────

@dataclass
class SceneAudioSync:
    audio_file: str = ""
    start_offset_s: float = 0.0
    beat_pattern: str = ""
    zoom_schedule: Any = None  # scalar_curve
    bpm: Optional[float] = None
    polyrhythm: List[int] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class SceneTransitions:
    in_from_previous: str = ""
    out_to_next: str = ""
    duration_s: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class SceneArtifacts:
    keyframe_path: str = ""
    clip_path: str = ""
    embedding_keyframe: List[float] = field(default_factory=list)
    embedding_clip: List[float] = field(default_factory=list)
    model_used_keyframe: str = ""
    model_used_clip: str = ""
    generation_cost_usd: float = 0.0
    attempts: int = 0

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class SceneVariation:
    variation_id: str = field(default_factory=lambda: _new_id("v"))
    label: str = ""
    style_overrides: Dict[str, Any] = field(default_factory=dict)
    camera_override: Optional[Camera] = None
    lighting_override: Optional[SceneLighting] = None
    artifact_path: str = ""
    embedding: List[float] = field(default_factory=list)
    user_rating: Optional[float] = None
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        d: Dict[str, Any] = {"variation_id": self.variation_id}
        if self.label:
            d["label"] = self.label
        if self.style_overrides:
            d["style_overrides"] = dict(self.style_overrides)
        if self.camera_override is not None:
            d["camera_override"] = self.camera_override.to_dict()
        if self.lighting_override is not None:
            d["lighting_override"] = self.lighting_override.to_dict()
        if self.artifact_path:
            d["artifact_path"] = self.artifact_path
        if self.embedding:
            d["embedding"] = list(self.embedding)
        if self.user_rating is not None:
            d["user_rating"] = self.user_rating
        if self.extra:
            d["extra"] = dict(self.extra)
        return d


@dataclass
class Scene:
    id: str = field(default_factory=lambda: _new_id("sc"))
    narrative_order: int = 0
    duration_seconds: float = 4.0
    language_description: str = ""
    synopsis: str = ""
    subjects: List[Subject] = field(default_factory=list)
    camera: Camera = field(default_factory=Camera)
    lighting: SceneLighting = field(default_factory=SceneLighting)
    environment: SceneEnvironment = field(default_factory=SceneEnvironment)
    style_tokens: List[str] = field(default_factory=list)
    negative_prompts: List[str] = field(default_factory=list)
    anchors: List[str] = field(default_factory=list)
    cited_quote_ids: List[str] = field(default_factory=list)
    active_motif_ids: List[str] = field(default_factory=list)
    props_present: List[str] = field(default_factory=list)
    narration: Narration = field(default_factory=Narration)
    audio_sync: SceneAudioSync = field(default_factory=SceneAudioSync)
    transitions: SceneTransitions = field(default_factory=SceneTransitions)
    variations: List[SceneVariation] = field(default_factory=list)
    selected_variation_id: str = ""
    artifacts: SceneArtifacts = field(default_factory=SceneArtifacts)
    state: str = "pending"
    notes: str = ""
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        d: Dict[str, Any] = {
            "id": self.id,
            "narrative_order": self.narrative_order,
            "duration_seconds": self.duration_seconds,
            "state": self.state,
        }
        for attr in ("language_description", "synopsis", "notes", "selected_variation_id"):
            v = getattr(self, attr)
            if v:
                d[attr] = v
        if self.subjects:
            d["subjects"] = [s.to_dict() for s in self.subjects]
        cam = self.camera.to_dict()
        if cam:
            d["camera"] = cam
        lit = self.lighting.to_dict()
        if lit:
            d["lighting"] = lit
        env = self.environment.to_dict()
        if env:
            d["environment"] = env
        for attr in ("style_tokens", "negative_prompts", "anchors",
                      "cited_quote_ids", "active_motif_ids", "props_present"):
            v = getattr(self, attr)
            if v:
                d[attr] = list(v)
        if not self.narration.is_empty():
            d["narration"] = self.narration.to_dict()
        audio = self.audio_sync.to_dict()
        if audio:
            d["audio_sync"] = audio
        tr = self.transitions.to_dict()
        if tr:
            d["transitions"] = tr
        if self.variations:
            d["variations"] = [v.to_dict() for v in self.variations]
        art = self.artifacts.to_dict()
        if art:
            d["artifacts"] = art
        if self.extra:
            d["extra"] = dict(self.extra)
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "Scene":
        cam_d = d.get("camera") or {}
        lit_d = d.get("lighting") or {}
        env_d = d.get("environment") or {}
        audio_d = d.get("audio_sync") or {}
        tr_d = d.get("transitions") or {}
        art_d = d.get("artifacts") or {}

        return cls(
            id=d.get("id") or _new_id("sc"),
            narrative_order=int(d.get("narrative_order", 0)),
            duration_seconds=float(d.get("duration_seconds", 4.0)),
            language_description=d.get("language_description", ""),
            synopsis=d.get("synopsis", ""),
            subjects=[Subject(**{k: s.get(k) for k in Subject.__dataclass_fields__
                                   if k in s})
                        for s in d.get("subjects", []) or []],
            camera=Camera(
                path=dict(cam_d.get("path", {})),
                fov_degrees=cam_d.get("fov_degrees"),
                focus_distance=cam_d.get("focus_distance"),
                aperture=cam_d.get("aperture"),
                lens_qualitative=cam_d.get("lens_qualitative", ""),
                shake=dict(cam_d.get("shake", {})),
            ),
            lighting=SceneLighting(
                primary_direction=list(lit_d.get("primary_direction", [])),
                color_temperature_k=lit_d.get("color_temperature_k"),
                intensity=lit_d.get("intensity"),
                time_of_day=lit_d.get("time_of_day", ""),
                ambient=dict(lit_d.get("ambient", {})),
                qualitative=lit_d.get("qualitative", ""),
            ),
            environment=SceneEnvironment(
                location_id=env_d.get("location_id", ""),
                weather=env_d.get("weather", ""),
                particulates=dict(env_d.get("particulates", {})),
                ambient_sound_cues=list(env_d.get("ambient_sound_cues", [])),
            ),
            style_tokens=list(d.get("style_tokens", [])),
            negative_prompts=list(d.get("negative_prompts", [])),
            anchors=list(d.get("anchors", [])),
            cited_quote_ids=list(d.get("cited_quote_ids", [])),
            active_motif_ids=list(d.get("active_motif_ids", [])),
            props_present=list(d.get("props_present", [])),
            narration=Narration.from_dict(d.get("narration", {})),
            audio_sync=SceneAudioSync(
                audio_file=audio_d.get("audio_file", ""),
                start_offset_s=float(audio_d.get("start_offset_s", 0.0)),
                beat_pattern=audio_d.get("beat_pattern", ""),
                zoom_schedule=audio_d.get("zoom_schedule"),
                bpm=audio_d.get("bpm"),
                polyrhythm=list(audio_d.get("polyrhythm", [])),
            ),
            transitions=SceneTransitions(
                in_from_previous=tr_d.get("in_from_previous", ""),
                out_to_next=tr_d.get("out_to_next", ""),
                duration_s=float(tr_d.get("duration_s", 0.0)),
            ),
            variations=[],  # TODO: variations from_dict when needed
            selected_variation_id=d.get("selected_variation_id", ""),
            artifacts=SceneArtifacts(
                keyframe_path=art_d.get("keyframe_path", ""),
                clip_path=art_d.get("clip_path", ""),
                embedding_keyframe=list(art_d.get("embedding_keyframe", [])),
                embedding_clip=list(art_d.get("embedding_clip", [])),
                model_used_keyframe=art_d.get("model_used_keyframe", ""),
                model_used_clip=art_d.get("model_used_clip", ""),
                generation_cost_usd=float(art_d.get("generation_cost_usd", 0.0)),
                attempts=int(art_d.get("attempts", 0)),
            ),
            state=d.get("state", "pending"),
            notes=d.get("notes", ""),
            extra=dict(d.get("extra", {})),
        )


# ─── LanguageVariant / MoodRef ──────────────────────────────────

@dataclass
class LanguageVariant:
    code: str = ""
    name: str = ""
    narrator_voice: str = ""
    localized_title: str = ""
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


@dataclass
class MoodRef:
    portfolio_item_id: str = ""
    weight: float = 1.0

    def to_dict(self) -> Dict[str, Any]:
        return _clean_dict(asdict(self))


# ─── FilmStructure (top level) ──────────────────────────────────

@dataclass
class FilmStructure:
    structure_id: str = field(default_factory=lambda: _new_id("fs"))
    title: str = ""
    synopsis: str = ""
    style_manifesto: str = ""
    target_duration_s: float = 0.0
    language_variants: List[LanguageVariant] = field(default_factory=list)
    portfolio_id: str = ""
    characters: List[Character] = field(default_factory=list)
    locations: List[Location] = field(default_factory=list)
    props: List[Prop] = field(default_factory=list)
    motifs: List[Motif] = field(default_factory=list)
    citations: List[Quote] = field(default_factory=list)
    artistic_text: ArtisticText = field(default_factory=ArtisticText)
    mood_board: List[MoodRef] = field(default_factory=list)
    scenes: List[Scene] = field(default_factory=list)
    extra: Dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        # Tolerate dict inputs in list fields — convert to proper dataclass.
        # This makes the constructor usable both with objects and with
        # raw JSON-parsed dicts, which is natural when LLM produces structures.
        self.language_variants = [
            v if isinstance(v, LanguageVariant) else LanguageVariant(**v)
            for v in self.language_variants
        ]
        self.characters = [
            v if isinstance(v, Character) else Character.from_dict(v)
            for v in self.characters
        ]
        self.locations = [
            v if isinstance(v, Location) else Location.from_dict(v)
            for v in self.locations
        ]
        self.props = [
            v if isinstance(v, Prop) else Prop.from_dict(v)
            for v in self.props
        ]
        self.motifs = [
            v if isinstance(v, Motif) else Motif.from_dict(v)
            for v in self.motifs
        ]
        self.citations = [
            v if isinstance(v, Quote) else Quote.from_dict(v)
            for v in self.citations
        ]
        self.mood_board = [
            v if isinstance(v, MoodRef) else MoodRef(**v)
            for v in self.mood_board
        ]
        self.scenes = [
            v if isinstance(v, Scene) else Scene.from_dict(v)
            for v in self.scenes
        ]
        if isinstance(self.artistic_text, dict):
            self.artistic_text = ArtisticText.from_dict(self.artistic_text)

    # ── Dict round-trip ──────────────────────────────────────

    def to_dict(self) -> Dict[str, Any]:
        d: Dict[str, Any] = {
            "structure_id": self.structure_id,
            "title": self.title,
        }
        for attr in ("synopsis", "style_manifesto", "portfolio_id"):
            v = getattr(self, attr)
            if v:
                d[attr] = v
        if self.target_duration_s:
            d["target_duration_s"] = self.target_duration_s
        if self.language_variants:
            d["language_variants"] = [x.to_dict() for x in self.language_variants]
        if self.characters:
            d["characters"] = [x.to_dict() for x in self.characters]
        if self.locations:
            d["locations"] = [x.to_dict() for x in self.locations]
        if self.props:
            d["props"] = [x.to_dict() for x in self.props]
        if self.motifs:
            d["motifs"] = [x.to_dict() for x in self.motifs]
        if self.citations:
            d["citations"] = [x.to_dict() for x in self.citations]
        at = self.artistic_text.to_dict()
        if at:
            d["artistic_text"] = at
        if self.mood_board:
            d["mood_board"] = [x.to_dict() for x in self.mood_board]
        d["scenes"] = [s.to_dict() for s in self.scenes]
        if self.extra:
            d["extra"] = dict(self.extra)
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "FilmStructure":
        return cls(
            structure_id=d.get("structure_id") or _new_id("fs"),
            title=d.get("title", ""),
            synopsis=d.get("synopsis", ""),
            style_manifesto=d.get("style_manifesto", ""),
            target_duration_s=float(d.get("target_duration_s", 0.0)),
            language_variants=[LanguageVariant(**lv)
                                for lv in d.get("language_variants", []) or []],
            portfolio_id=d.get("portfolio_id", ""),
            characters=[Character.from_dict(x)
                         for x in d.get("characters", []) or []],
            locations=[Location.from_dict(x)
                        for x in d.get("locations", []) or []],
            props=[Prop.from_dict(x) for x in d.get("props", []) or []],
            motifs=[Motif.from_dict(x) for x in d.get("motifs", []) or []],
            citations=[Quote.from_dict(x) for x in d.get("citations", []) or []],
            artistic_text=ArtisticText.from_dict(d.get("artistic_text", {})),
            mood_board=[MoodRef(**x) for x in d.get("mood_board", []) or []],
            scenes=[Scene.from_dict(x) for x in d.get("scenes", []) or []],
            extra=dict(d.get("extra", {})),
        )

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent, ensure_ascii=False)

    @classmethod
    def from_json(cls, s: str) -> "FilmStructure":
        return cls.from_dict(json.loads(s))

    # ── Persistence ──────────────────────────────────────────

    def save(self, directory: Path) -> Path:
        directory = Path(directory)
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / f"{self.structure_id}.json"
        path.write_text(self.to_json(), encoding="utf-8")
        return path

    @classmethod
    def load(cls, path: Path) -> "FilmStructure":
        return cls.from_json(Path(path).read_text(encoding="utf-8"))

    # ── Queries ──────────────────────────────────────────────

    def get_scene(self, scene_id: str) -> Optional[Scene]:
        for s in self.scenes:
            if s.id == scene_id:
                return s
        return None

    def get_character(self, char_id: str) -> Optional[Character]:
        for c in self.characters:
            if c.id == char_id:
                return c
        return None

    def get_location(self, loc_id: str) -> Optional[Location]:
        for loc in self.locations:
            if loc.id == loc_id:
                return loc
        return None

    def get_prop(self, prop_id: str) -> Optional[Prop]:
        for p in self.props:
            if p.id == prop_id:
                return p
        return None

    def get_motif(self, motif_id: str) -> Optional[Motif]:
        for m in self.motifs:
            if m.id == motif_id:
                return m
        return None

    def get_quote(self, quote_id: str) -> Optional[Quote]:
        for q in self.citations:
            if q.id == quote_id:
                return q
        return None


# ─── Localized-string resolver ──────────────────────────────────

def resolve_localized(value: Any, lang: str = "default",
                       fallback_order: Optional[List[str]] = None) -> str:
    """
    Extract plain string from LocalizedString.

    value: either a str (returned as-is) or dict {lang_code: str}.
    lang: desired language code.
    fallback_order: if desired lang missing, try these in order. Default:
        [lang, "default", "en", first available key].
    """
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    if not isinstance(value, dict):
        return str(value)

    order = fallback_order or [lang, "default", "en"]
    for code in order:
        if code in value and value[code]:
            return value[code]
    # Last resort: first non-empty value.
    for v in value.values():
        if v:
            return str(v)
    return ""


# ─── Helpers ────────────────────────────────────────────────────

def _clean_dict(d: Dict[str, Any]) -> Dict[str, Any]:
    """
    Remove empty/default values from dict before serialization.
    Keeps 0 and False (legitimate values), removes None/''/[]/{}.
    """
    out = {}
    for k, v in d.items():
        if v is None:
            continue
        if isinstance(v, (list, dict, str)) and not v:
            continue
        out[k] = v
    return out
