"""
engine/embeddings.py
=====================

Implementacje IVisualEmbedding.

Tier 1: CLIPEmbedding (sentence-transformers, real CLIP).
        Wymaga pip install sentence-transformers + torch.
        Generuje prawdziwe wizualne embeddingi — coherence check działa.

Tier 2: HashEmbedding (stdlib only).
        Deterministyczne ale zerowe znaczenie semantyczne.
        Zawsze działa, ale coherence=0 dla identycznych obrazów, losowe
        liczby dla różnych. Pipeline nie wywali się, ale threshold-based
        coherence check będzie meaningless.

Fallback chain: próbuj CLIP, gdy brak torch → Hash.
"""

from __future__ import annotations

import hashlib
import logging
import math
from pathlib import Path
from typing import List, Optional

from engine.video_pipeline import IVisualEmbedding

log = logging.getLogger(__name__)


class CLIPEmbedding(IVisualEmbedding):
    """CLIP przez sentence-transformers. Wymaga torch + sentence-transformers."""

    def __init__(self, model_name: str = "clip-ViT-B-32") -> None:
        from sentence_transformers import SentenceTransformer  # may raise
        self._model = SentenceTransformer(model_name)
        self._name = model_name

    def embed_image(self, path: Path) -> List[float]:
        from PIL import Image
        img = Image.open(str(path)).convert("RGB")
        emb = self._model.encode([img], convert_to_numpy=True)[0]
        return [float(x) for x in emb]

    def embed_video(self, path: Path) -> List[float]:
        """
        Naive: sample 3 frames (start/middle/end), avg CLIP embedding.
        Wymaga imageio albo cv2. Gdy brak — rzucamy, pipeline ma fallback.
        """
        try:
            import imageio.v3 as iio
            frames = list(iio.imiter(str(path)))
        except Exception as e:
            raise RuntimeError(f"Cannot read video frames from {path}: {e}")

        if not frames:
            raise RuntimeError(f"No frames in {path}")

        # Sample start, middle, end
        n = len(frames)
        indices = [0, n // 2, n - 1]
        from PIL import Image
        pil_frames = [Image.fromarray(frames[i]) for i in indices]
        embs = self._model.encode(pil_frames, convert_to_numpy=True)
        avg = embs.mean(axis=0)
        return [float(x) for x in avg]

    def distance(self, a: List[float], b: List[float]) -> float:
        # Cosine distance = 1 - cosine_similarity
        if not a or not b or len(a) != len(b):
            return 1.0
        dot = sum(x * y for x, y in zip(a, b))
        na = math.sqrt(sum(x * x for x in a))
        nb = math.sqrt(sum(y * y for y in b))
        if na == 0 or nb == 0:
            return 1.0
        sim = dot / (na * nb)
        # Clamp numerical noise.
        sim = max(-1.0, min(1.0, sim))
        return 1.0 - sim


class HashEmbedding(IVisualEmbedding):
    """
    Hash-based fallback. Zawsze działa, tylko stdlib.

    Embedding = 32-wymiarowy float vector z SHA-256 hash'u bytes pliku.
    Normalizujemy do [-1, 1]. Identyczne pliki → identyczny embedding
    (distance=0). Różne pliki → losowy ale deterministyczny embedding
    (distance ~ uniform 0-2).

    Coherence check z tym jest meaningless dla semantyki, ale nie
    wywala pipeline'u. Pipeline z HashEmbedding działa tak jak z
    `threshold=∞` (nic nie jest outlier).
    """

    def __init__(self) -> None:
        log.warning(
            "Using HashEmbedding fallback. Coherence checks will be noise. "
            "Install sentence-transformers + torch for real CLIP embeddings."
        )

    def embed_image(self, path: Path) -> List[float]:
        return self._hash_to_vector(Path(path).read_bytes())

    def embed_video(self, path: Path) -> List[float]:
        # Po prostu hash całego pliku. Różne klipy → różne embeddingi.
        return self._hash_to_vector(Path(path).read_bytes())

    def distance(self, a: List[float], b: List[float]) -> float:
        if not a or not b or len(a) != len(b):
            return 1.0
        dot = sum(x * y for x, y in zip(a, b))
        na = math.sqrt(sum(x * x for x in a))
        nb = math.sqrt(sum(y * y for y in b))
        if na == 0 or nb == 0:
            return 1.0
        sim = dot / (na * nb)
        sim = max(-1.0, min(1.0, sim))
        return 1.0 - sim

    @staticmethod
    def _hash_to_vector(data: bytes, dims: int = 32) -> List[float]:
        # SHA-256 → 32 bytes. Mapujemy każdy byte na float w [-1, 1].
        h = hashlib.sha256(data).digest()
        # Jeśli potrzeba więcej wymiarów, chainuje hashe.
        while len(h) < dims:
            h = h + hashlib.sha256(h).digest()
        return [(b / 127.5) - 1.0 for b in h[:dims]]


def create_embedding_provider(prefer: str = "auto") -> IVisualEmbedding:
    """
    Factory z fallback chain.

    prefer: "clip" | "hash" | "auto".
    "auto" = próbuj CLIP, przy błędzie fallback do Hash.
    """
    if prefer == "hash":
        return HashEmbedding()

    if prefer in ("auto", "clip"):
        try:
            return CLIPEmbedding()
        except Exception as e:
            if prefer == "clip":
                # User wymagał CLIP — nie degradujemy po cichu.
                raise
            log.info("CLIP unavailable (%s); falling back to HashEmbedding", e)
            return HashEmbedding()

    raise ValueError(f"Unknown embedding preference: {prefer}")
