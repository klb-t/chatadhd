"""
engine/prompt_compiler.py
==========================

Deterministyczna kompilacja sceny do promptu dla modelu video.

Założenie (z rozmowy): JSON **nie idzie** do modelu video. Modele wideo są
trenowane na prozie i halucynują gdy widzą surowy JSON. Idzie:
  - deterministyczna proza zbudowana z JSON-a (ten sam JSON → ten sam prompt)
  - style_tokens jako lista tagów
  - negative_prompts
  - anchor images (portfolio_item_ids → paths)
  - seed (hash z scene.id)
  - provider_specific dla Veo/Sora/Wan/Seedance

Dla krzywych matematycznych (camera path, zoom schedule) używamy expr_eval
żeby dostać jakościowe opisy: "slow orbit, radius 2m growing to 5m over 4s"
zamiast surowego wyrażenia.

PromptCompiler jest pure-function — bez efektów ubocznych, bez LLM calls.
Deterministyczne wejście/wyjście. Tu może żyć cała logika tłumaczenia
struktura→proza, testowalna jednostkowo.
"""

from __future__ import annotations

import hashlib
import logging
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from engine.film_structure import (
    FilmStructure, Scene, Character, Location, Prop, Motif, Quote,
    resolve_localized,
)
from engine.expr_eval import evaluate_scalar_curve, evaluate_vec3_curve

log = logging.getLogger(__name__)


# ─── Output struct ──────────────────────────────────────────────

@dataclass
class CompiledPrompt:
    """
    Wynik kompilacji sceny.

    primary_text: prose prompt for the video model.
    style_tokens: comma-separable tags (lens, grade, medium).
    negative_prompt: joined negative (for models that support it).
    reference_images: list of (portfolio_item_id, path) for anchors.
    seed: deterministic integer seed derived from scene.id.
    provider_specific: dict keyed by provider slug for passthrough options.
    debug_notes: human-readable trace of what the compiler decided.
    """
    primary_text: str = ""
    style_tokens: List[str] = field(default_factory=list)
    negative_prompt: str = ""
    reference_images: List[Tuple[str, str]] = field(default_factory=list)
    seed: int = 0
    provider_specific: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    debug_notes: List[str] = field(default_factory=list)

    def to_openrouter_payload(self, model_id: str, *,
                                duration_s: Optional[float] = None,
                                resolution: str = "1080p",
                                aspect_ratio: str = "16:9") -> Dict[str, Any]:
        """
        Zamienia CompiledPrompt na payload /api/v1/videos zgodny z OR.
        Pipeline.submit_clip_async używa tego jako źródła request_payload.
        """
        payload: Dict[str, Any] = {
            "model": model_id,
            "prompt": self.primary_text,
        }
        if duration_s is not None:
            payload["duration"] = int(max(1, round(duration_s)))
        if resolution:
            payload["resolution"] = resolution
        if aspect_ratio:
            payload["aspect_ratio"] = aspect_ratio
        if self.seed:
            payload["seed"] = self.seed
        if self.provider_specific:
            payload["provider"] = {"options": dict(self.provider_specific)}
        # Reference images — format OR: data-URL albo https URL
        # Tu zostawiamy do wypełnienia przez caller'a (który ma dostęp do
        # filesystemu i może base64-encode'ować albo uploadować).
        # Kompilator sygnalizuje tylko ŻE mają być i JAKIE.
        return payload


# ─── Compiler ───────────────────────────────────────────────────

class PromptCompiler:
    """
    Compiler scene → CompiledPrompt.

    Usage:
        compiler = PromptCompiler(film, portfolio_resolver=lambda pid: path)
        compiled = compiler.compile(scene, language="pl")

    portfolio_resolver: callable(portfolio_item_id) → Path | None.
        Used to resolve anchor images. Optional; if None, references are left
        as (id, "") tuples and caller resolves.
    """

    def __init__(self, film: FilmStructure, *,
                 portfolio_resolver: Optional[Any] = None,
                 data_dir: Optional[Path] = None) -> None:
        self._film = film
        self._resolve = portfolio_resolver
        self._data_dir = Path(data_dir) if data_dir else None

    def compile(self, scene: Scene, *, language: str = "default",
                 include_cross_scene_context: bool = True) -> CompiledPrompt:
        notes: List[str] = []
        sections: List[str] = []

        # ── 1. Film-level manifesto (one-liner) ──────────────
        if self._film.style_manifesto:
            sections.append(self._film.style_manifesto.strip())
            notes.append("+film_manifesto")

        # ── 2. Scene description (language-aware) ────────────
        desc = resolve_localized(scene.language_description, language).strip()
        if not desc and scene.synopsis:
            desc = scene.synopsis.strip()
        if desc:
            sections.append(desc)
            notes.append("+scene_description")

        # ── 3. Characters in scene ───────────────────────────
        char_parts = []
        for subj in scene.subjects:
            if subj.character_id:
                ch = self._film.get_character(subj.character_id)
                if ch:
                    label = subj.label or ch.name
                    appearance = ch.appearance or ""
                    pos_desc = self._describe_subject_position(subj,
                                                                  scene.duration_seconds)
                    if appearance:
                        char_parts.append(
                            f"{label} ({appearance}){pos_desc}"
                        )
                    else:
                        char_parts.append(f"{label}{pos_desc}")
                    continue
            if subj.prop_id:
                prop = self._film.get_prop(subj.prop_id)
                if prop:
                    label = subj.label or prop.name
                    desc_text = resolve_localized(prop.description, language)
                    pos_desc = self._describe_subject_position(subj,
                                                                  scene.duration_seconds)
                    if desc_text:
                        char_parts.append(f"{label} ({desc_text}){pos_desc}")
                    else:
                        char_parts.append(f"{label}{pos_desc}")
                    continue
            if subj.label:
                pos_desc = self._describe_subject_position(subj,
                                                              scene.duration_seconds)
                char_parts.append(f"{subj.label}{pos_desc}")

        if char_parts:
            sections.append("Scene contains: " + "; ".join(char_parts) + ".")
            notes.append(f"+subjects({len(char_parts)})")

        # ── 4. Location / environment ────────────────────────
        if scene.environment.location_id:
            loc = self._film.get_location(scene.environment.location_id)
            if loc:
                loc_desc = f"Location: {loc.name}"
                if loc.description:
                    loc_desc += f", {loc.description}"
                if loc.lighting_baseline.time_of_day:
                    loc_desc += f" ({loc.lighting_baseline.time_of_day})"
                if loc.lighting_baseline.weather:
                    loc_desc += f", {loc.lighting_baseline.weather} weather"
                sections.append(loc_desc + ".")
                notes.append("+location")

        if scene.environment.weather and scene.environment.location_id == "":
            sections.append(f"Weather: {scene.environment.weather}.")
        if scene.environment.particulates:
            kind = scene.environment.particulates.get("kind")
            if kind and kind != "none":
                density = scene.environment.particulates.get("density", 0.5)
                density_word = ("heavy" if density > 0.7
                                 else "moderate" if density > 0.3 else "light")
                sections.append(f"{density_word.capitalize()} {kind} in the air.")
                notes.append(f"+particulates({kind})")

        # ── 5. Camera ────────────────────────────────────────
        cam_desc = self._describe_camera(scene)
        if cam_desc:
            sections.append(cam_desc)
            notes.append("+camera")

        # ── 6. Lighting ──────────────────────────────────────
        light_desc = self._describe_lighting(scene)
        if light_desc:
            sections.append(light_desc)
            notes.append("+lighting")

        # ── 7. Motifs active in scene ────────────────────────
        for mid in scene.active_motif_ids:
            motif = self._film.get_motif(mid)
            if motif:
                motif_desc = resolve_localized(motif.description, language)
                if motif_desc:
                    sections.append(
                        f"Motif '{motif.name}' ({motif.kind}): {motif_desc}."
                    )
                    notes.append(f"+motif({motif.name})")

        # ── 8. Citations (non-destructive mention) ───────────
        for qid in scene.cited_quote_ids:
            quote = self._film.get_quote(qid)
            if quote and quote.verified:
                # Only verified quotes flow to the prompt — LLM-proposed
                # unverified ones stay out to avoid citing nonexistent works.
                q_text = resolve_localized(quote.text, language)
                src = quote.source
                if src.author and src.title:
                    attribution = f"{src.author}, {src.title}"
                elif src.author:
                    attribution = src.author
                elif src.raw:
                    attribution = src.raw
                else:
                    attribution = "unattributed"
                relationship = (f" as {quote.relationship.replace('_', ' ')}"
                                 if quote.relationship else "")
                sections.append(
                    f"Reference{relationship}: \"{q_text[:140]}\" — {attribution}."
                )
                notes.append(f"+citation({qid[:8]})")
            elif quote and not quote.verified:
                notes.append(f"-unverified_citation({qid[:8]})")

        # ── 9. Cross-scene continuity (props that carry over) ──
        if include_cross_scene_context:
            for pid in scene.props_present:
                prop = self._film.get_prop(pid)
                if not prop:
                    continue
                # Find most recent prior transformation
                prior_state = None
                for t in prop.transformation_log:
                    if t.scene_id == scene.id:
                        break
                    prior_state = t
                if prior_state and prior_state.state_description:
                    state_desc = resolve_localized(
                        prior_state.state_description, language)
                    sections.append(
                        f"{prop.name} appears (previously: {state_desc})."
                    )
                    notes.append(f"+prop_continuity({prop.name})")

        # ── 10. Narration as onscreen / voiceover cue ───────
        # Lyrics/dialog DON'T go into video prompt (text artifacts risk).
        # Only onscreen_text at kind=sign_in_world may be worth mentioning.
        for ost in scene.narration.onscreen_text:
            if ost.kind == "sign_in_world":
                txt = resolve_localized(ost.text, language)
                if txt:
                    sections.append(f"Visible in frame: sign reads \"{txt}\".")
                    notes.append("+sign_in_world")

        # ── 11. Style tokens ─────────────────────────────────
        style_tokens = list(scene.style_tokens)
        if scene.camera.lens_qualitative:
            style_tokens.append(f"{scene.camera.lens_qualitative} lens")
        if scene.lighting.qualitative:
            style_tokens.append(scene.lighting.qualitative)

        # ── 12. Negative prompt ──────────────────────────────
        negs = list(scene.negative_prompts)
        # Standard negative additions (from experience with video models):
        # avoid text artifacts since we're not rendering text.
        if not scene.narration.onscreen_text:
            negs.extend(["text overlay", "subtitles", "watermark"])
        negative = ", ".join(negs)

        # ── 13. Anchors → references ─────────────────────────
        refs: List[Tuple[str, str]] = []
        for anchor_id in scene.anchors:
            path = ""
            if self._resolve:
                try:
                    p = self._resolve(anchor_id)
                    path = str(p) if p else ""
                except Exception:
                    log.exception("portfolio_resolver failed for %s", anchor_id)
            refs.append((anchor_id, path))

        # ── 14. Seed ─────────────────────────────────────────
        seed = _deterministic_seed(scene.id)

        primary = " ".join(s for s in sections if s).strip()

        compiled = CompiledPrompt(
            primary_text=primary,
            style_tokens=style_tokens,
            negative_prompt=negative,
            reference_images=refs,
            seed=seed,
            debug_notes=notes,
        )

        # Append style tokens to primary text for models that expect a single string.
        if style_tokens:
            compiled.primary_text = primary + " Style: " + ", ".join(style_tokens) + "."

        return compiled

    # ── Camera description ───────────────────────────────────

    def _describe_camera(self, scene: Scene) -> str:
        path = scene.camera.path or {}
        ptype = path.get("type")
        duration = scene.duration_seconds

        if not ptype:
            return ""

        if ptype == "static":
            pos = path.get("position", [0, 0, 0])
            look = path.get("look_at", [0, 0, 0])
            return (f"Camera: static shot from position "
                    f"{_vec_to_prose(pos)} looking toward {_vec_to_prose(look)}.")

        if ptype == "orbit":
            r_start = evaluate_scalar_curve(path.get("radius", 2.0), 0.0)
            r_end = evaluate_scalar_curve(path.get("radius", 2.0), duration)
            av_start = evaluate_scalar_curve(path.get("angular_velocity", 1.0),
                                                0.0)
            radius_str = (f"orbit at {r_start:.1f}m" if abs(r_start - r_end) < 0.1
                           else f"orbit from {r_start:.1f}m to {r_end:.1f}m")
            speed = ("slowly" if abs(av_start) < 0.5
                     else "steadily" if abs(av_start) < 1.5 else "quickly")
            return f"Camera: {speed} orbiting subject, {radius_str} over {duration:.1f}s."

        if ptype == "dolly":
            start = path.get("start", [0, 0, 0])
            end = path.get("end", [0, 0, 0])
            return (f"Camera: dolly from {_vec_to_prose(start)} "
                    f"to {_vec_to_prose(end)} over {duration:.1f}s.")

        if ptype == "bezier":
            cps = path.get("control_points", [])
            if len(cps) >= 2:
                return (f"Camera: curved path through {len(cps)} waypoints, "
                        f"smooth motion over {duration:.1f}s.")
            return ""

        if ptype == "explicit":
            # Sample start/mid/end to describe trajectory
            try:
                start_pos = evaluate_vec3_curve(path.get("position"), 0.0)
                end_pos = evaluate_vec3_curve(path.get("position"), duration)
                return (f"Camera: path from {_vec_to_prose(start_pos)} "
                        f"to {_vec_to_prose(end_pos)} over {duration:.1f}s.")
            except Exception:
                return "Camera: custom trajectory."

        return ""

    # ── Lighting description ─────────────────────────────────

    def _describe_lighting(self, scene: Scene) -> str:
        lit = scene.lighting
        parts = []
        if lit.qualitative:
            parts.append(lit.qualitative)
        if lit.time_of_day:
            parts.append(f"{lit.time_of_day} light")
        if lit.color_temperature_k is not None:
            k = evaluate_scalar_curve(lit.color_temperature_k, 0.0)
            if k < 3500:
                parts.append("warm golden tones")
            elif k > 6500:
                parts.append("cool bluish tones")
        if parts:
            return "Lighting: " + ", ".join(parts) + "."
        return ""

    # ── Subject position ─────────────────────────────────────

    def _describe_subject_position(self, subj, duration: float) -> str:
        """Qualitative position: 'left', 'center-right', 'foreground', etc."""
        if not subj.transform:
            return ""
        pos = subj.transform.get("position")
        if pos is None:
            return ""
        try:
            x, y, z = evaluate_vec3_curve(pos, 0.0)
        except Exception:
            return ""

        horiz = ""
        if x < -0.5:
            horiz = "left"
        elif x > 0.5:
            horiz = "right"
        else:
            horiz = "center"

        depth = ""
        if z < -0.5:
            depth = "foreground"
        elif z > 0.5:
            depth = "background"

        if horiz and depth:
            return f" ({horiz}, {depth})"
        if horiz:
            return f" ({horiz})"
        return ""


# ─── Helpers ────────────────────────────────────────────────────

def _vec_to_prose(v) -> str:
    """Convert [x, y, z] to short prose. 'position [2, 0, 1]' is noise for a
    video model; 'right-center' is more useful."""
    if not v or len(v) < 3:
        return "origin"
    x, y, z = float(v[0]), float(v[1]), float(v[2])
    parts = []
    if abs(x) < 0.3:
        parts.append("center")
    elif x > 0:
        parts.append("right")
    else:
        parts.append("left")
    if abs(z) > 0.3:
        parts.append("near" if z < 0 else "far")
    if abs(y) > 0.5:
        parts.append("high" if y > 0 else "low")
    return "-".join(parts) if parts else "origin"


def _deterministic_seed(scene_id: str) -> int:
    """Deterministic seed from scene ID. Same scene → same seed."""
    h = hashlib.sha256(scene_id.encode()).digest()
    # 32-bit unsigned range, typical for video models.
    return int.from_bytes(h[:4], "big")
