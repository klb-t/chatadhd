"""
ChatADHD v0.07.01 - Chat Engine

Handles OpenRouter API communication with:
  - Streaming and non-streaming responses
  - Web search and deep research plugins
  - Reasoning / extended thinking tokens
  - Attachment processing (images, text files)
  - Semantic analysis of responses
  - Event emission for real-time graph building

All errors are logged and raised — no silent ``except: pass``.
"""
import base64
import json
import logging
from pathlib import Path
from typing import Any, Callable, Optional

import requests

from core.semantic import analyzer as semantic_analyzer
from engine.events import bus, MSG_CREATED
from engine.attachments import AttachmentStore

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

    def __init__(self, config, secrets, db, memory=None,
                 graph_memory=None) -> None:
        self.config = config
        self.secrets = secrets
        self.db = db
        self.memory = memory
        self.graph_memory = graph_memory  # GraphMemorySelector
        self.conv: Optional[dict] = None
        self.last_reasoning: Optional[str] = None
        self.attachment_store = AttachmentStore(self.config._path.parent)

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

    def _ensure_active_conv(self) -> dict:
        """Return a valid conversation, recreating it if a stale reference was deleted."""
        if self.conv:
            fresh = self.db.get_conv(self.conv["id"])
            if fresh is not None:
                self.conv = fresh
                return self.conv
            log.warning("Active conversation %s no longer exists; creating a new one", self.conv.get("id"))
            self.conv = None
        self.new_conv()
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
        conv = self._ensure_active_conv()

        # Persist user message.
        user_mid = self.db.create_msg(
            conv["id"], text, "user", attachments=attachments
        )
        bus.emit(MSG_CREATED, {
            "id": user_mid, "text": text,
            "conv_id": conv["id"], "role": "user",
        })

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

        asst_mid = self.db.create_msg(
            conv["id"], response_text, "assistant",
            model=model, metadata=metadata,
        )
        bus.emit(MSG_CREATED, {
            "id": asst_mid, "text": response_text,
            "conv_id": conv["id"], "role": "assistant",
        })

        # Auto-title from first exchange.
        msgs = self.db.get_msgs(conv["id"])
        if len(msgs) <= 2 and self.config.get("auto_title", True):
            title = text[:30] + ("..." if len(text) > 30 else "")
            self.db.update_conv(conv["id"], title=title)
            conv["title"] = title
            self.conv = conv

        self.conv = conv
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

        # Memory context (hierarchical tree).
        if self.memory:
            mem_ctx = self.memory.get_active_context()
            if mem_ctx:
                messages.append({
                    "role": "system",
                    "content": f"User's memory/context:\n{mem_ctx}",
                })

        # Graph memory (semantically related past messages).
        if self.graph_memory:
            try:
                conv_id = self.conv["id"] if self.conv else None
                graph_ctx = self.graph_memory.select_context(
                    current_text, current_conv_id=conv_id,
                )
                if graph_ctx:
                    messages.append({
                        "role": "system",
                        "content": graph_ctx,
                    })
            except Exception:
                log.debug("Graph memory selection failed", exc_info=True)

        # Conversation history. Exclude the just-persisted final user message
        # because we rebuild it below with attachment-aware multipart content.
        if self.conv:
            history = self.db.get_msgs(self.conv["id"])
            if history and history[-1]["role"] == "user" and history[-1]["text"] == current_text:
                last_attachments = history[-1].get("attachments") or []
                if list(last_attachments) == list(attachments or []):
                    history = history[:-1]
            for m in history:
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

        try:
            view = self.attachment_store.ingest_paths(attachments, query=text)
        except Exception:
            log.exception("Attachment pipeline failed; falling back to legacy attachment mode")
            view = {"manifest": "", "text_blocks": [], "image_paths": list(attachments)}

        prompt_intro = text.strip() or "Please analyze the attached files."
        manifest = view.get("manifest", "")
        blocks = view.get("text_blocks", [])
        parts: list[dict] = []
        lead = prompt_intro
        if manifest:
            lead += "\n\n" + manifest
        if blocks:
            lead += "\n\nRelevant extracted representations:\n" + "\n\n".join(blocks[:8])
        parts.append({"type": "text", "text": lead})

        for path_str in view.get("image_paths", []):
            p = Path(path_str)
            if not p.exists():
                continue
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

        return parts


    def build_request_spec(
        self,
        text: str,
        attachments: Optional[list[str]] = None,
        web_search: bool = False,
        deep_research: bool = False,
        reasoning_effort: Optional[str] = None,
    ) -> dict[str, Any]:
        messages = self._build_messages(text, attachments)
        model = self.config.get("default_model")
        payload = self._prepare_payload(messages, model, stream=True, web_search=web_search,
                                        deep_research=deep_research, reasoning_effort=reasoning_effort)
        masked_key = self.secrets.get("api_key") or ""
        if masked_key:
            masked_key = masked_key[:8] + "..."
        return {
            "method": "POST",
            "url": f"{self.config.get('base_url', '').rstrip('/')}/chat/completions",
            "headers": {
                "Authorization": f"Bearer {masked_key or '<API_KEY>'}",
                "Content-Type": "application/json",
                "HTTP-Referer": "https://github.com/chatadhd",
                "X-Title": "ChatADHD",
            },
            "json": payload,
        }

    def execute_request_spec(
        self,
        request_spec: dict[str, Any],
        on_chunk: Optional[Callable[[str], None]] = None,
        on_reasoning: Optional[Callable[[str], None]] = None,
    ) -> tuple[str, Optional[str]]:
        key = self.secrets.get("api_key")
        if not key:
            raise ValueError("No API key configured — open Settings")
        headers = dict(request_spec.get("headers") or {})
        auth = headers.get("Authorization", "")
        if "<API_KEY>" in auth or "..." in auth or not auth.strip():
            headers["Authorization"] = f"Bearer {key}"
        url = request_spec.get("url") or f"{self.config.get('base_url', '').rstrip('/')}/chat/completions"
        payload = request_spec.get("json")
        if not isinstance(payload, dict):
            raise ValueError("Request spec must contain json object")
        stream = bool(payload.get("stream", on_chunk is not None))
        resp = requests.post(url, headers=headers, json=payload, stream=stream, timeout=180)
        if resp.status_code != 200:
            body = resp.text[:500]
            log.error("API error %d: %s", resp.status_code, body)
            raise ValueError(f"API error {resp.status_code}: {body}")
        if stream:
            return self._read_stream(resp, on_chunk or (lambda *_: None), on_reasoning)
        return self._read_json(resp)

    def _prepare_payload(
        self,
        messages: list[dict],
        model: str,
        stream: bool,
        web_search: bool = False,
        deep_research: bool = False,
        reasoning_effort: Optional[str] = None,
    ) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "model": model,
            "messages": messages,
            "temperature": self.config.get("temperature", 0.7),
            "max_tokens": self.config.get("max_tokens", 4096),
            "stream": stream,
        }
        if web_search or deep_research:
            plugin: dict[str, Any] = {"id": "web"}
            if deep_research:
                plugin["max_results"] = 10
                payload["web_search_options"] = {"search_context_size": "high"}
            payload["plugins"] = [plugin]
        self._configure_reasoning(payload, model, reasoning_effort)
        payload["include_reasoning"] = True
        return payload

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
        payload = self._prepare_payload(messages, model, stream=stream, web_search=web_search,
                                        deep_research=deep_research, reasoning_effort=reasoning_effort)

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
