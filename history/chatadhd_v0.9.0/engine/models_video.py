"""
engine/models_video.py
=======================

Rejestr modeli generujących obrazy i wideo.

Zgodność z kodeksem:
  - Rzeczywistość (capabilities, koszty) = DANE: modele są zarejestrowane.
  - Decyzja (wybór modelu pod zadanie) = LOGIKA: select_model().
  - Fallback z metadanymi: lista modeli w kolejności preferencji.
  - Wszystko queryable: model.query("cost_per_second") zwraca liczbę.
  - „Wszystko można, nic nie trzeba": user może nadpisać capabilities, dodać
    własne modele (np. lokalne), zmienić kolejność fallbacku.

Modele są DANYMI. Registry jest LOGIKĄ która nimi operuje.
Definiowane domyślne modele są w data/presets/video_models.json
— można edytować bez dotykania kodu.
"""

from __future__ import annotations

import json
import logging
from dataclasses import dataclass, field, asdict
from enum import Enum
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Set, Tuple

log = logging.getLogger(__name__)


# ─── Capabilities ───────────────────────────────────────────────

class ModelModality(str, Enum):
    TEXT_TO_IMAGE = "text_to_image"
    IMAGE_TO_IMAGE = "image_to_image"
    TEXT_TO_VIDEO = "text_to_video"
    IMAGE_TO_VIDEO = "image_to_video"
    VIDEO_TO_VIDEO = "video_to_video"
    TEXT_PLUS_IMAGE_TO_VIDEO = "text_plus_image_to_video"


@dataclass
class ModelCapabilities:
    """
    Deklaracja co model potrafi. Używane przez select_model() do filtrowania.

    supports_character_consistency: czy model ma mechanizm (np. reference image,
        charakter LoRA) do utrzymania tej samej postaci w wielu generacjach.
        Kluczowe dla AoD (ten sam nosacz w 8 scenach) i Ciszy Beta (ta sama
        postać w całym filmie).

    max_duration_s: limit pojedynczej generacji. 0 = N/A (image).
    """
    modalities: Set[ModelModality] = field(default_factory=set)
    max_resolution_w: int = 1024
    max_resolution_h: int = 1024
    max_duration_s: float = 0.0
    supports_character_consistency: bool = False
    supports_audio_output: bool = False
    supports_seed: bool = True

    def to_dict(self) -> Dict[str, Any]:
        d = {
            "modalities": [m.value for m in self.modalities],
            "max_resolution_w": self.max_resolution_w,
            "max_resolution_h": self.max_resolution_h,
            "max_duration_s": self.max_duration_s,
            "supports_character_consistency": self.supports_character_consistency,
            "supports_audio_output": self.supports_audio_output,
            "supports_seed": self.supports_seed,
        }
        return d

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "ModelCapabilities":
        return cls(
            modalities={ModelModality(m) for m in d.get("modalities", [])},
            max_resolution_w=int(d.get("max_resolution_w", 1024)),
            max_resolution_h=int(d.get("max_resolution_h", 1024)),
            max_duration_s=float(d.get("max_duration_s", 0.0)),
            supports_character_consistency=bool(d.get("supports_character_consistency", False)),
            supports_audio_output=bool(d.get("supports_audio_output", False)),
            supports_seed=bool(d.get("supports_seed", True)),
        )


# ─── Cost & quality ─────────────────────────────────────────────

@dataclass
class ModelPricing:
    """Rzeczywistość: ile kosztuje."""
    cost_per_image_usd: float = 0.0
    cost_per_second_video_usd: float = 0.0
    cost_per_megapixel_usd: float = 0.0  # dla modeli płacących za pixel*time
    currency: str = "USD"

    def estimate(self, n_images: int = 0, n_seconds_video: float = 0.0,
                 megapixels: float = 0.0) -> float:
        return (self.cost_per_image_usd * n_images
                + self.cost_per_second_video_usd * n_seconds_video
                + self.cost_per_megapixel_usd * megapixels)

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "ModelPricing":
        return cls(**d)


@dataclass
class ModelQualityProfile:
    """
    Subiektywna jakość modelu w różnych domenach (0-100).
    Aktualizowana po obserwacjach użytkownika (user feedback).

    Domeny celowo konkretne, nie generic 'quality' — bo model świetny w portretach
    może być słaby w pejzażach.
    """
    portraits: int = 50
    environments: int = 50
    abstract: int = 50
    documentary_realism: int = 50  # dla AoD nosaczy
    cinematic: int = 50
    sample_count: int = 0  # ile obserwacji przyczyniło się do tego profilu

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "ModelQualityProfile":
        return cls(**d)

    def for_domain(self, domain: str) -> int:
        return getattr(self, domain, 50)


# ─── Model definition ───────────────────────────────────────────

@dataclass
class VideoModel:
    """
    Pełna definicja modelu w registrze.

    Pola:
      model_id: unikalny identyfikator
      display_name: widoczne w UI
      provider: "openrouter" | "replicate" | "stability" | "fal" | "local"
      provider_model_name: ID w API dostawcy (np. "runway/gen-4-turbo")
      capabilities: co potrafi
      pricing: ile kosztuje
      quality: subiektywna jakość (aktualizowana)
      priority: użytkownicka preferencja (wyższy = preferowany)
      enabled: czy dostępny
      notes: wolny tekst
    """
    model_id: str
    display_name: str
    provider: str
    provider_model_name: str
    capabilities: ModelCapabilities
    pricing: ModelPricing
    quality: ModelQualityProfile = field(default_factory=ModelQualityProfile)
    priority: int = 50
    enabled: bool = True
    notes: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return {
            "model_id": self.model_id,
            "display_name": self.display_name,
            "provider": self.provider,
            "provider_model_name": self.provider_model_name,
            "capabilities": self.capabilities.to_dict(),
            "pricing": self.pricing.to_dict(),
            "quality": self.quality.to_dict(),
            "priority": self.priority,
            "enabled": self.enabled,
            "notes": self.notes,
        }

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "VideoModel":
        return cls(
            model_id=d["model_id"],
            display_name=d["display_name"],
            provider=d["provider"],
            provider_model_name=d["provider_model_name"],
            capabilities=ModelCapabilities.from_dict(d.get("capabilities", {})),
            pricing=ModelPricing.from_dict(d.get("pricing", {})),
            quality=ModelQualityProfile.from_dict(d.get("quality", {})),
            priority=int(d.get("priority", 50)),
            enabled=bool(d.get("enabled", True)),
            notes=d.get("notes", ""),
        )

    def query(self, key: str) -> Any:
        """Zapytywalny invariant — zgodność z kodeksem."""
        d = self.to_dict()
        # Flatten jedno-poziomowy dostęp
        if key in d:
            return d[key]
        if key in d["capabilities"]:
            return d["capabilities"][key]
        if key in d["pricing"]:
            return d["pricing"][key]
        if key in d["quality"]:
            return d["quality"][key]
        return None


# ─── Selection spec ─────────────────────────────────────────────

@dataclass
class ModelSelectionSpec:
    """
    Specyfikacja zadania — co ma robić model.
    Używana przez ModelRegistry.select() do filtrowania.
    """
    required_modality: ModelModality
    min_resolution_w: int = 512
    min_resolution_h: int = 512
    min_duration_s: float = 0.0
    need_character_consistency: bool = False
    need_audio_output: bool = False
    budget_usd_max: Optional[float] = None
    domain_hint: Optional[str] = None  # "portraits", "documentary_realism", ...
    preferred_provider: Optional[str] = None
    excluded_models: Set[str] = field(default_factory=set)


# ─── Registry ───────────────────────────────────────────────────

class VideoModelRegistry:
    """
    Rejestr modeli wideo/obrazu.

    CELOWO odrębny od engine/models.ModelRegistry (który zarządza OpenRouter
    chat-LLM-ami). Obie klasy mogą współistnieć — są to różne domeny.

    Modele ładowane z pliku presets/video_models.json przy starcie.
    User może dodawać/edytować przez UI — zmiany idą do tego samego pliku.

    select() zwraca uporządkowaną listę kandydatów (fallback chain).
    """

    def __init__(self, models_file: Optional[Path] = None) -> None:
        self._models: Dict[str, VideoModel] = {}
        self._models_file = models_file

    def register(self, model: VideoModel) -> None:
        if model.model_id in self._models:
            log.info("Overwriting model registration: %s", model.model_id)
        self._models[model.model_id] = model

    def unregister(self, model_id: str) -> None:
        self._models.pop(model_id, None)

    def get(self, model_id: str) -> Optional[VideoModel]:
        return self._models.get(model_id)

    def list_all(self, enabled_only: bool = True) -> List[VideoModel]:
        ms = list(self._models.values())
        if enabled_only:
            ms = [m for m in ms if m.enabled]
        return ms

    def select(self, spec: ModelSelectionSpec) -> List[VideoModel]:
        """
        Zwraca uporządkowaną listę kandydatów — fallback chain.

        Kryteria filtrowania:
          - ma wymaganą modalność
          - spełnia min_resolution / min_duration
          - consistency / audio jeśli wymagane
          - budżet
          - enabled
          - nie w excluded

        Kryteria sortowania (malejąco):
          1. preferred_provider match
          2. priority
          3. quality dla domain_hint
          4. -cost (tańsze wyżej przy remisie)
        """
        candidates: List[VideoModel] = []
        for m in self._models.values():
            if not m.enabled:
                continue
            if m.model_id in spec.excluded_models:
                continue
            if spec.required_modality not in m.capabilities.modalities:
                continue
            if (m.capabilities.max_resolution_w < spec.min_resolution_w
                    or m.capabilities.max_resolution_h < spec.min_resolution_h):
                continue
            if m.capabilities.max_duration_s < spec.min_duration_s:
                continue
            if spec.need_character_consistency and not m.capabilities.supports_character_consistency:
                continue
            if spec.need_audio_output and not m.capabilities.supports_audio_output:
                continue
            if spec.budget_usd_max is not None:
                # Estymuj koszt — prosta reguła: za 1 generację + duration
                est = m.pricing.estimate(
                    n_images=(1 if spec.required_modality
                              in (ModelModality.TEXT_TO_IMAGE, ModelModality.IMAGE_TO_IMAGE)
                              else 0),
                    n_seconds_video=max(spec.min_duration_s, 0.0),
                )
                if est > spec.budget_usd_max:
                    continue
            candidates.append(m)

        # Sortowanie — kluczem jest tuple, Python sortuje leksykograficznie.
        def sort_key(m: VideoModel) -> Tuple[int, int, int, float]:
            provider_match = 1 if (spec.preferred_provider
                                   and m.provider == spec.preferred_provider) else 0
            domain_quality = m.quality.for_domain(spec.domain_hint) if spec.domain_hint else 50
            # Negate cost so cheaper is "higher".
            # Koszt per image+sec jako proxy
            cost_proxy = (m.pricing.cost_per_image_usd
                          + m.pricing.cost_per_second_video_usd * max(spec.min_duration_s, 1.0))
            return (-provider_match, -m.priority, -domain_quality, cost_proxy)

        candidates.sort(key=sort_key)
        return candidates

    # ── Persistence ────────────────────────────────────────

    def save(self, path: Optional[Path] = None) -> Path:
        target = Path(path) if path else self._models_file
        if target is None:
            raise ValueError("No save path provided and no default models_file set")
        target.parent.mkdir(parents=True, exist_ok=True)
        data = {"models": [m.to_dict() for m in self._models.values()]}
        target.write_text(json.dumps(data, indent=2), encoding="utf-8")
        return target

    def load(self, path: Optional[Path] = None) -> bool:
        target = Path(path) if path else self._models_file
        if target is None or not target.exists():
            return False
        try:
            data = json.loads(target.read_text(encoding="utf-8"))
            self._models.clear()
            for md in data.get("models", []):
                self.register(VideoModel.from_dict(md))
            return True
        except Exception:
            log.exception("Failed to load model registry from %s", target)
            return False

    def load_or_seed_defaults(self, path: Optional[Path] = None) -> None:
        """Ładuje z pliku, albo seeduje wbudowanymi defaults jeśli pliku brak."""
        if not self.load(path):
            self._seed_defaults()
            if path or self._models_file:
                self.save(path)

    def sync_from_openrouter(self, api_key: str,
                              base_url: str = "https://openrouter.ai/api/v1",
                              *,
                              keep_user_overrides: bool = True) -> int:
        """
        Pobiera listę video models z /api/v1/videos/models i aktualizuje registry.

        Zgodnie z fundamentalnymi zasadami: rzeczywistość (capabilities, pricing)
        przychodzi z API, decyzja (priorytet, quality, enabled) zostaje u usera.

        keep_user_overrides: jeśli True, zachowujemy user-set priority/quality/
        enabled/notes per model_id. Fresh discovery tylko aktualizuje capabilities
        i pricing (które są "rzeczywistością").

        Zwraca liczbę znanych modeli po sync.
        """
        try:
            from engine.openrouter_video import fetch_video_models_from_openrouter
        except ImportError:
            log.warning("openrouter_video module not available; cannot sync")
            return len(self._models)

        remote = fetch_video_models_from_openrouter(api_key, base_url)
        if not remote:
            log.warning("sync_from_openrouter: empty response; keeping existing")
            return len(self._models)

        for m in remote:
            model_id = m.get("id")
            if not model_id:
                continue

            # Parse capabilities
            sizes = m.get("supported_sizes", []) or []
            max_w, max_h = _parse_max_size(sizes)

            # OR unified schema: one model supports many modalities.
            # Zgodnie z dokumentacją T2V i I2V (frame_images) oraz R2V
            # (input_references) są na tym samym endpointcie.
            modalities = {
                ModelModality.TEXT_TO_VIDEO,
                ModelModality.IMAGE_TO_VIDEO,
                ModelModality.TEXT_PLUS_IMAGE_TO_VIDEO,
            }

            # Pricing parsing
            skus = m.get("pricing_skus", {}) or {}
            cost_per_s = _extract_per_second_cost(skus)

            existing = self._models.get(model_id)
            if existing is not None and keep_user_overrides:
                # Aktualizujemy tylko rzeczywistość, user overrides zostają.
                existing.capabilities.modalities = modalities
                existing.capabilities.max_resolution_w = max_w
                existing.capabilities.max_resolution_h = max_h
                existing.capabilities.max_duration_s = max(
                    existing.capabilities.max_duration_s, 20.0
                )
                existing.pricing.cost_per_second_video_usd = cost_per_s
                existing.provider_model_name = model_id
                existing.display_name = m.get("name", existing.display_name)
                existing.notes = (m.get("description", "")[:500]
                                   if m.get("description") else existing.notes)
            else:
                self.register(VideoModel(
                    model_id=model_id,
                    display_name=m.get("name", model_id),
                    provider="openrouter",
                    provider_model_name=model_id,
                    capabilities=ModelCapabilities(
                        modalities=modalities,
                        max_resolution_w=max_w,
                        max_resolution_h=max_h,
                        max_duration_s=20.0,  # typowo; OR dokumentacja nie podaje max per model
                        supports_character_consistency=True,  # OR unified reference-to-video
                        supports_audio_output=bool(_model_has_audio(m)),
                    ),
                    pricing=ModelPricing(cost_per_second_video_usd=cost_per_s),
                    priority=50,
                    notes=m.get("description", "")[:500],
                ))

        if self._models_file:
            self.save()
        return len(self._models)

    def _seed_defaults(self) -> None:
        """
        Wbudowane domyślne modele — punkt startowy. User może edytować w data/presets.

        UWAGA: ceny i capabilities należy zweryfikować aktualną dokumentacją
        OpenRoutera i dostawców przed użyciem w produkcji. To są defaults do nadpisania.
        """
        # Text/image-to-video przez OpenRouter (po wprowadzeniu generacji wideo).
        # Placeholder — konkretne nazwy do uzupełnienia po sprawdzeniu aktualnej oferty OR.
        self.register(VideoModel(
            model_id="or_video_default_t2v",
            display_name="OpenRouter T2V (default)",
            provider="openrouter",
            provider_model_name="PLACEHOLDER/t2v",
            capabilities=ModelCapabilities(
                modalities={ModelModality.TEXT_TO_VIDEO},
                max_resolution_w=1280,
                max_resolution_h=720,
                max_duration_s=10.0,
                supports_character_consistency=False,
            ),
            pricing=ModelPricing(cost_per_second_video_usd=0.5),
            priority=50,
            notes="Placeholder — zaktualizować po weryfikacji aktualnej oferty OR.",
        ))
        self.register(VideoModel(
            model_id="or_video_default_i2v",
            display_name="OpenRouter I2V (default)",
            provider="openrouter",
            provider_model_name="PLACEHOLDER/i2v",
            capabilities=ModelCapabilities(
                modalities={ModelModality.IMAGE_TO_VIDEO},
                max_resolution_w=1280,
                max_resolution_h=720,
                max_duration_s=8.0,
                supports_character_consistency=True,
            ),
            pricing=ModelPricing(cost_per_second_video_usd=0.6),
            priority=60,
            notes="Preferowany dla zachowania spójności z keyframe.",
        ))
        # Image-gen — tańsze, do fazy 2.
        self.register(VideoModel(
            model_id="or_image_default",
            display_name="OpenRouter Image (default)",
            provider="openrouter",
            provider_model_name="PLACEHOLDER/image",
            capabilities=ModelCapabilities(
                modalities={ModelModality.TEXT_TO_IMAGE, ModelModality.IMAGE_TO_IMAGE},
                max_resolution_w=2048,
                max_resolution_h=2048,
            ),
            pricing=ModelPricing(cost_per_image_usd=0.04),
            priority=50,
        ))


# ─── Parsing helpers (for sync_from_openrouter) ─────────────────

def _parse_max_size(sizes: list) -> tuple:
    """Z listy ['1280x720', '1920x1080'] zwraca największe (w, h)."""
    max_w = 1024
    max_h = 1024
    for s in sizes:
        try:
            w, h = s.split("x")
            w_i, h_i = int(w), int(h)
            if w_i * h_i > max_w * max_h:
                max_w, max_h = w_i, h_i
        except Exception:
            continue
    return max_w, max_h


def _extract_per_second_cost(skus: dict) -> float:
    """
    Z pricing_skus wyciąga pierwszy koszt per-second.

    Przykład skus: {"per-video-second": "0.50", "per-video-second-1080p": "0.75"}
    Bierzemy najtańszy per-second (domyślna rozdzielczość).
    """
    candidates = []
    for key, val in (skus or {}).items():
        if "per-video-second" in key or "per_second" in key:
            try:
                candidates.append(float(val))
            except (TypeError, ValueError):
                continue
    if not candidates:
        return 0.0
    return min(candidates)


def _model_has_audio(m: dict) -> bool:
    """Heurystyka: model generuje audio gdy description wspomina audio/sound."""
    desc = (m.get("description") or "").lower()
    name = (m.get("name") or "").lower()
    return any(t in desc or t in name
               for t in ("audio", "sound", "dialogue", "lip-sync"))
