"""
engine/openrouter_generator.py
===============================

Synchroniczny generator obrazów przez OpenRouter.

Dla video delegujemy do AsyncJobManager + OpenRouterVideoProvider (async).
Tu tylko obrazy (faza 2 keyframes) — te zazwyczaj wracają w kilka sekund.

Używa OR chat/completions z modelem image-capable (Gemini 2.5 Flash Image,
DALL-E, itp.). Zwraca GenerationResult z zapisanym plikiem PNG.
"""

from __future__ import annotations

import base64
import logging
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

from engine.video_pipeline import IGenerator, GenerationResult
from engine.models_video import VideoModel

log = logging.getLogger(__name__)


class OpenRouterGenerator(IGenerator):
    """
    Minimalny sync generator dla image-gen przez OR.

    generate_video() rzuca NotImplementedError — video idzie przez async path.
    Pipeline woła submit_clip_async() zamiast generate_video() dla fazy 3.
    """

    def __init__(self, api_key: str,
                 base_url: str = "https://openrouter.ai/api/v1",
                 http_referer: Optional[str] = None,
                 app_name: str = "ChatADHD") -> None:
        self._api_key = api_key
        self._base_url = base_url.rstrip("/")
        self._http_referer = http_referer
        self._app_name = app_name

    def _headers(self) -> Dict[str, str]:
        h = {
            "Authorization": f"Bearer {self._api_key}",
            "Content-Type": "application/json",
        }
        if self._http_referer:
            h["HTTP-Referer"] = self._http_referer
        if self._app_name:
            h["X-Title"] = self._app_name
        return h

    def generate_image(self, prompt: str, model: VideoModel, *,
                       reference_images: Optional[List[Path]] = None,
                       output_path: Path,
                       width: int = 1024,
                       height: int = 1024,
                       seed: Optional[int] = None,
                       **extra: Any) -> GenerationResult:
        """
        Image gen przez OR chat/completions z obrazem w response.

        OR accepts models like google/gemini-2.5-flash-image-preview
        which return image_url in message content.
        """
        import requests

        t0 = time.time()
        url = f"{self._base_url}/chat/completions"

        # Build multi-modal message: prompt + optional reference images.
        content: List[Dict[str, Any]] = [{"type": "text", "text": prompt}]
        for ref_path in (reference_images or []):
            if not ref_path.exists():
                log.warning("Reference image missing: %s", ref_path)
                continue
            b64 = base64.b64encode(ref_path.read_bytes()).decode("ascii")
            mime = _guess_mime(ref_path)
            content.append({
                "type": "image_url",
                "image_url": {"url": f"data:{mime};base64,{b64}"},
            })

        payload: Dict[str, Any] = {
            "model": model.provider_model_name or model.model_id,
            "messages": [{"role": "user", "content": content}],
            "modalities": ["image", "text"],
        }
        if seed is not None:
            payload["seed"] = seed

        resp = requests.post(url, headers=self._headers(),
                              json=payload, timeout=120)
        if resp.status_code != 200:
            raise RuntimeError(
                f"OR image gen HTTP {resp.status_code}: {resp.text[:300]}"
            )
        data = resp.json()

        # Find image in response.
        output_path = Path(output_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)

        img_written = False
        for choice in data.get("choices", []):
            msg = choice.get("message", {})
            # Najpierw sprawdzamy pole images (nowy format OR dla modalities).
            for img in msg.get("images", []) or []:
                url_field = img.get("image_url", {}).get("url", "")
                if url_field.startswith("data:"):
                    _, b64 = url_field.split(",", 1)
                    output_path.write_bytes(base64.b64decode(b64))
                    img_written = True
                    break
                elif url_field.startswith("http"):
                    # Download remote URL.
                    r = requests.get(url_field, timeout=60)
                    output_path.write_bytes(r.content)
                    img_written = True
                    break
            if img_written:
                break
            # Fallback: content może być lista bloków.
            content_blocks = msg.get("content")
            if isinstance(content_blocks, list):
                for block in content_blocks:
                    if (isinstance(block, dict)
                            and block.get("type") == "image_url"):
                        url_field = block.get("image_url", {}).get("url", "")
                        if url_field.startswith("data:"):
                            _, b64 = url_field.split(",", 1)
                            output_path.write_bytes(base64.b64decode(b64))
                            img_written = True
                            break
                if img_written:
                    break

        if not img_written:
            raise RuntimeError(
                f"OR response had no image; model={model.model_id}, "
                f"response keys={list(data.keys())}"
            )

        cost = float(data.get("usage", {}).get("cost", 0.0) or 0.0)
        return GenerationResult(
            output_path=output_path,
            cost_usd=cost,
            duration_s=time.time() - t0,
            model_used=model.model_id,
            raw_response={"id": data.get("id")},
        )

    def generate_video(self, prompt: str, model: VideoModel, *,
                       keyframe: Optional[Path] = None,
                       output_path: Path,
                       duration_s: float = 4.0,
                       width: int = 1280,
                       height: int = 720,
                       seed: Optional[int] = None,
                       **extra: Any) -> GenerationResult:
        """
        Video gen jest async — ten sync path nie jest wspierany.
        Pipeline używa submit_clip_async() + AsyncJobManager zamiast.
        """
        raise NotImplementedError(
            "Video generation via OR is async. "
            "Use VideoPipeline.submit_clip_async() + AsyncJobManager instead."
        )


def _guess_mime(path: Path) -> str:
    suffix = path.suffix.lower()
    return {
        ".png": "image/png",
        ".jpg": "image/jpeg",
        ".jpeg": "image/jpeg",
        ".webp": "image/webp",
        ".gif": "image/gif",
    }.get(suffix, "image/png")
