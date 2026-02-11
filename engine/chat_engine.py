"""
ChatADHD v0.07.00 - Chat Engine

Handles OpenRouter API communication with:
  - Streaming and non-streaming responses
  - Web search and deep research plugins
  - Reasoning / extended thinking tokens
  - Attachment processing (images, text files)
  - Semantic analysis of responses

All errors are logged and raised — no silent ``except: pass``.
"""
import base64
import json
import logging
from pathlib import Path
from typing import Any, Callable, Optional

import requests

from core.semantic import analyzer as semantic_analyzer

log = logging.getLogger(__name__)

# Image MIME types supported by the multimodal API.
_IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".gif", ".webp"}

# Text file extensions we'll inline in the prompt.
_TEXT_EXTS = {
    ".txt", ".py", ".md", ".json", ".csv", ".xml",
    ".html", ".css", ".js", ".ts", ".yaml", ".yml",
    ".toml", ".ini", ".cfg", ".sh", ".bat", ".rs",
    ".go", ".java", ".c", ".cpp", ".h", ".sql",
}

# Maximum characters to read from a text attachment.
_MAX_FILE_CHARS = 15_000


class ChatEngine:
    """Orchestrates conversation flow: context → API call → storage."""

    def __init__(self, config, secrets, db, memory=None) -> None:
        self.config = config
        self.secrets = secrets
        self.db = db
        self.memory = memory
        self.conv: Optional[dict] = None
        self.last_reasoning: Optional[str] = None

        # Resume last conversation.
        convs = self.db.list_convs(limit=1)
        if convs:
            self.conv = convs[0]

    # ── Conversation management ────────────────────────────────────

    def new_conv(self, title: str = "New Chat") -> dict:
        self.conv = self.db.create_conv(title)
        return self.conv

    def load_conv(self, conv_id: str) -> Optional[dict]:
        self.conv = self.db.get_conv(conv_id)
        return self.conv

    # ── Send message ───────────────────────────────────────────────

    def send(
        self,
        text: str,
        attachments: Optional[list[str]] = None,
        on_chunk: Optional[Callable[[str], None]] = None,
        on_reasoning: Optional[Callable[[str], None]] = None,
        web_search: bool = False,
        deep_research: bool = False,
        reasoning_effort: Optional[str] = None,
    ) -> str:
        """
        Send a user message and return the assistant's response text.

        Raises ``ValueError`` on API errors (no silent failures).
        """
        if not self.conv:
            self.new_conv()

        # Persist user message.
        self.db.create_msg(
            self.conv["id"], text, "user", attachments=attachments
        )

        # Build API payload.
        messages = self._build_messages(text, attachments)
        model = self.config.get("default_model")

        # Call API.
        response_text, reasoning = self._call_api(
            messages, model, on_chunk, on_reasoning,
            web_search, deep_research, reasoning_effort,
        )
        self.last_reasoning = reasoning

        # Persist assistant message.
        metadata: dict[str, Any] = {}
        if reasoning:
            metadata["reasoning"] = reasoning[:2000]

        # Run semantic analysis on the response (non-blocking, best-effort).
        try:
            topics = semantic_analyzer.extract_topics(response_text, threshold=2)
            if topics:
                metadata["topics"] = topics
        except Exception:
            log.debug("Semantic analysis failed", exc_info=True)

        self.db.create_msg(
            self.conv["id"], response_text, "assistant",
            model=model, metadata=metadata,
        )

        # Auto-title from first exchange.
        msgs = self.db.get_msgs(self.conv["id"])
        if len(msgs) <= 2 and self.config.get("auto_title", True):
            title = text[:30] + ("..." if len(text) > 30 else "")
            self.db.update_conv(self.conv["id"], title=title)
            self.conv["title"] = title

        return response_text

    # ── Message building ───────────────────────────────────────────

    def _build_messages(
        self, current_text: str, attachments: Optional[list[str]] = None
    ) -> list[dict[str, Any]]:
        messages: list[dict] = []

        # System prompt.
        sys_prompt = self.config.get("system_prompt", "")
        if sys_prompt:
            messages.append({"role": "system", "content": sys_prompt})

        # Memory context.
        if self.memory:
            mem_ctx = self.memory.get_active_context()
            if mem_ctx:
                messages.append({
                    "role": "system",
                    "content": f"User's memory/context:\n{mem_ctx}",
                })

        # Conversation history.
        if self.conv:
            for m in self.db.get_msgs(self.conv["id"]):
                if m["status"] != "active":
                    continue
                content = m["text"]
                weight = m.get("weight", 1.0)
                if weight > 1.5:
                    content = f"[IMPORTANT] {content}"
                elif weight < 0.5:
                    content = f"[low priority] {content}"
                messages.append({"role": m["role"], "content": content})

        # Current user message (with attachments if any).
        content = self._build_content(current_text, attachments)
        messages.append({"role": "user", "content": content})

        return messages

    def _build_content(
        self, text: str, attachments: Optional[list[str]]
    ) -> Any:
        """Return plain string or multipart content array."""
        if not attachments:
            return text

        parts: list[dict] = [{"type": "text", "text": text}]

        for path_str in attachments:
            p = Path(path_str)
            if not p.exists():
                log.warning("Attachment not found: %s", p)
                continue

            if p.suffix.lower() in _IMAGE_EXTS:
                try:
                    data = base64.b64encode(p.read_bytes()).decode()
                    mime = f"image/{p.suffix[1:].lower()}"
                    if mime == "image/jpg":
                        mime = "image/jpeg"
                    parts.append({
                        "type": "image_url",
                        "image_url": {"url": f"data:{mime};base64,{data}"},
                    })
                except Exception:
                    log.exception("Failed to encode image: %s", p)

            elif p.suffix.lower() in _TEXT_EXTS:
                try:
                    file_text = p.read_text(errors="replace")[:_MAX_FILE_CHARS]
                    parts.append({
                        "type": "text",
                        "text": f"\n--- FILE: {p.name} ---\n{file_text}\n--- END FILE ---",
                    })
                except Exception:
                    log.exception("Failed to read text file: %s", p)

        return parts

    # ── API call ───────────────────────────────────────────────────

    def _call_api(
        self,
        messages: list[dict],
        model: str,
        on_chunk: Optional[Callable] = None,
        on_reasoning: Optional[Callable] = None,
        web_search: bool = False,
        deep_research: bool = False,
        reasoning_effort: Optional[str] = None,
    ) -> tuple[str, Optional[str]]:
        key = self.secrets.get("api_key")
        if not key:
            raise ValueError("No API key configured — open Settings")

        base = self.config.get("base_url", "").rstrip("/")
        url = f"{base}/chat/completions"

        headers = {
            "Authorization": f"Bearer {key}",
            "Content-Type": "application/json",
            "HTTP-Referer": "https://github.com/chatadhd",
            "X-Title": "ChatADHD",
        }

        stream = on_chunk is not None
        payload: dict[str, Any] = {
            "model": model,
            "messages": messages,
            "temperature": self.config.get("temperature", 0.7),
            "max_tokens": self.config.get("max_tokens", 4096),
            "stream": stream,
        }

        # Web search / deep research plugins.
        if web_search or deep_research:
            plugin: dict[str, Any] = {"id": "web"}
            if deep_research:
                plugin["max_results"] = 10
                payload["web_search_options"] = {"search_context_size": "high"}
            payload["plugins"] = [plugin]

        # Reasoning / extended thinking.
        self._configure_reasoning(payload, model, reasoning_effort)
        payload["include_reasoning"] = True

        log.info(
            "API request: model=%s, msgs=%d, stream=%s, web=%s",
            model, len(messages), stream, web_search,
        )

        resp = requests.post(url, headers=headers, json=payload,
                             stream=stream, timeout=180)
        if resp.status_code != 200:
            body = resp.text[:500]
            log.error("API error %d: %s", resp.status_code, body)
            raise ValueError(f"API error {resp.status_code}: {body}")

        if stream:
            return self._read_stream(resp, on_chunk, on_reasoning)
        else:
            return self._read_json(resp)

    @staticmethod
    def _configure_reasoning(
        payload: dict, model: str, effort: Optional[str]
    ) -> None:
        """Add reasoning config to payload if the model supports it."""
        thinking_indicators = ("opus", "sonnet", "o1", "o3", "r1", "thinking", "deepseek")
        is_thinking = any(x in model.lower() for x in thinking_indicators)
        if not is_thinking:
            return

        reasoning_cfg: dict[str, Any] = {"enabled": True}
        is_claude_new = any(v in model for v in ("4.6", "4-6", "4.5", "4-5"))

        if is_claude_new:
            if effort == "max":
                payload["verbosity"] = "max"
            elif effort and effort != "adaptive":
                budget = {"low": 5000, "medium": 15000, "high": 30000}
                reasoning_cfg["max_tokens"] = budget.get(effort, 15000)
        else:
            if effort:
                reasoning_cfg["effort"] = effort

        payload["reasoning"] = reasoning_cfg

    # ── Stream / JSON readers ──────────────────────────────────────

    @staticmethod
    def _read_stream(
        resp, on_chunk: Callable, on_reasoning: Optional[Callable]
    ) -> tuple[str, Optional[str]]:
        full_text = ""
        reasoning_text = ""

        for raw_line in resp.iter_lines():
            if not raw_line:
                continue
            line = raw_line.decode("utf-8", errors="replace")
            if not line.startswith("data: "):
                continue
            data_str = line[6:]
            if data_str.strip() == "[DONE]":
                break
            try:
                data = json.loads(data_str)
                delta = data.get("choices", [{}])[0].get("delta", {})

                chunk = delta.get("content", "")
                if chunk:
                    full_text += chunk
                    on_chunk(chunk)

                reasoning = delta.get("reasoning", "")
                if reasoning:
                    reasoning_text += reasoning
                    if on_reasoning:
                        on_reasoning(reasoning)
            except (json.JSONDecodeError, IndexError, KeyError):
                log.debug("Skipped malformed SSE chunk: %s", data_str[:100])

        return full_text, reasoning_text or None

    @staticmethod
    def _read_json(resp) -> tuple[str, Optional[str]]:
        data = resp.json()
        choice = data["choices"][0]
        text = choice["message"]["content"]
        reasoning = choice["message"].get("reasoning", "")
        return text, reasoning or None
