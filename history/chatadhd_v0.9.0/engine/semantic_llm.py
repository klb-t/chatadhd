"""
ChatADHD v0.07.01 - LLM Semantic Analyzer

Uses a cheap/fast model (Haiku, Flash, Gemma, or local Ollama) to extract
rich semantic information from messages.  Falls back to regex (core/semantic.py)
if the LLM call fails or is disabled.

The LLM receives a structured prompt and returns JSON with:
  - entities (name, kind, relevance)
  - topics (label, confidence)
  - relations (subject, predicate, object)
  - summary (one-line)

Cost estimate: Haiku at ~$0.25/M input → analysing a 500-token message
costs ~$0.000125.  At 200 messages/day ≈ $0.025/day.
"""
import json
import logging
from typing import Any, Optional

import requests

from core.semantic import analyzer as regex_analyzer

log = logging.getLogger(__name__)

_ANALYSIS_PROMPT = """\
Analyse the following chat message. Return ONLY valid JSON (no markdown, no backticks).

{
  "entities": [{"name": "...", "kind": "person|org|place|code|file|concept|product|event", "relevance": 0.0-1.0}],
  "topics": [{"label": "...", "confidence": 0.0-1.0}],
  "relations": [{"subject": "...", "predicate": "mentions|depends_on|references|contradicts|implements|part_of", "object": "..."}],
  "summary": "one line summary",
  "sentiment": "positive|negative|neutral|mixed"
}

Rules:
- Extract ALL named entities (people, orgs, code modules, files, concepts).
- Topics should be domain labels (legal, finance, tech, ai, security, health, project, personal).
- Relations connect entities mentioned in the text.
- Be precise. Fewer high-confidence items > many low-confidence ones.
- If the message is trivial (greetings, "ok", etc.) return empty arrays.

Message:
"""


class SemanticLLM:
    """LLM-powered semantic analysis with regex fallback."""

    def __init__(self, config, secrets) -> None:
        self.config = config
        self.secrets = secrets
        self._consecutive_failures = 0
        self._disabled_by_errors = False

    @property
    def enabled(self) -> bool:
        return bool(
            not self._disabled_by_errors
            and self.config.get("semantic_analysis", True)
            and self.config.get("semantic_model")
            and self.secrets.get("api_key")
        )

    def analyse(self, text: str) -> dict[str, Any]:
        """
        Analyse text using LLM, falling back to regex on failure.
        Returns dict with: entities, topics, relations, summary, sentiment.
        """
        # Always run regex first (instant, free).
        regex_result = regex_analyzer.analyse(text)

        if not self.enabled or len(text) < 20:
            return self._convert_regex(regex_result)

        try:
            llm_result = self._call_llm(text)
            if llm_result:
                self._consecutive_failures = 0
                # Merge: LLM results + regex entities that LLM might have missed.
                return self._merge(llm_result, regex_result)
            else:
                self._consecutive_failures += 1
        except Exception:
            log.debug("LLM semantic analysis failed — using regex", exc_info=True)
            self._consecutive_failures += 1

        if self._consecutive_failures >= 5:
            self._disabled_by_errors = True
            log.warning("Semantic LLM disabled after %d consecutive failures. "
                        "Check model ID in Settings.", self._consecutive_failures)

        return self._convert_regex(regex_result)

    def _call_llm(self, text: str) -> Optional[dict]:
        """Call the semantic model and parse JSON response."""
        key = self.secrets.get("api_key")
        base = self.config.get("base_url", "").rstrip("/")
        model = self.config.get("semantic_model")

        if not all((key, base, model)):
            return None

        # Truncate very long messages to save cost.
        analysis_text = text[:3000]

        resp = requests.post(
            f"{base}/chat/completions",
            headers={
                "Authorization": f"Bearer {key}",
                "Content-Type": "application/json",
                "HTTP-Referer": "https://github.com/chatadhd",
                "X-Title": "ChatADHD-Semantic",
            },
            json={
                "model": model,
                "messages": [
                    {"role": "user", "content": _ANALYSIS_PROMPT + analysis_text},
                ],
                "temperature": 0.1,
                "max_tokens": 800,
            },
            timeout=30,
        )

        if resp.status_code != 200:
            try:
                err_body = resp.json()
                log.warning("Semantic LLM error %d: %s", resp.status_code,
                            str(err_body.get("error", err_body))[:120])
            except Exception:
                log.warning("Semantic LLM error %d", resp.status_code)
            return None

        raw = resp.json()["choices"][0]["message"]["content"]

        # Strip markdown fences if present.
        raw = raw.strip()
        if raw.startswith("```"):
            raw = raw.split("\n", 1)[-1]
        if raw.endswith("```"):
            raw = raw.rsplit("```", 1)[0]
        raw = raw.strip()

        try:
            return json.loads(raw)
        except json.JSONDecodeError:
            log.debug("Semantic LLM returned invalid JSON: %.100s", raw)
            return None

    @staticmethod
    def _convert_regex(regex_result: dict) -> dict:
        """Convert regex analyzer output to the unified format."""
        entities = [
            {"name": e.text, "kind": e.entity_type.value, "relevance": e.confidence}
            for e in regex_result.get("entities", [])
        ]
        topics = [
            {"label": t, "confidence": 0.5}
            for t in regex_result.get("topics", [])
        ]
        relations = [
            {"subject": r.subject, "predicate": r.predicate.value, "object": r.obj}
            for r in regex_result.get("relations", [])
        ]
        return {
            "entities": entities,
            "topics": topics,
            "relations": relations,
            "summary": "",
            "sentiment": "neutral",
            "source": "regex",
        }

    @staticmethod
    def _merge(llm: dict, regex_result: dict) -> dict:
        """Merge LLM and regex results (LLM takes priority)."""
        llm.setdefault("entities", [])
        llm.setdefault("topics", [])
        llm.setdefault("relations", [])
        llm["source"] = "llm"

        # Add regex-only entities that LLM missed (URLs, IPs, file paths).
        llm_names = {e.get("name", "").lower() for e in llm["entities"]}
        for ent in regex_result.get("entities", []):
            if ent.text.lower() not in llm_names:
                llm["entities"].append({
                    "name": ent.text,
                    "kind": ent.entity_type.value,
                    "relevance": ent.confidence * 0.8,
                })

        return llm
