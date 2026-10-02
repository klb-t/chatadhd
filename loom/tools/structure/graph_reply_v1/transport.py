"""Consume a Chat Completions response without silently fixing model output.

Sending/accounting uses the existing openrouter_runner, not a new paid path.
This adapter accepts saved exact HTTP bytes or a mocked response for tests.
"""
from __future__ import annotations

import base64
import hashlib
import json
import math


def _pairs(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("graph_reply_completion_duplicate_key")
        value[key] = item
    return value


def _bad_number(value):
    raise ValueError("graph_reply_completion_nonfinite")


def _float(value):
    number = float(value)
    if not math.isfinite(number):
        raise ValueError("graph_reply_completion_nonfinite")
    return number


class CompletionError(ValueError):
    """Failed/refused/truncated completions retain the original first bytes."""

    def __init__(self, code, raw):
        super().__init__(code)
        self.capture = {"http_response_sha256": hashlib.sha256(raw).hexdigest(),
                        "http_response_base64": base64.b64encode(raw).decode("ascii"),
                        "http_response_bytes": len(raw)}


def consume_completion(packet, raw, *, request_id, turn_id, requested_model,
                       recipe_sha256=None, parent_turn_id=None, known_at=None,
                       http_status=200):
    """Compile the completed assistant graph; never parse hidden reasoning.

    Unknown HTTP fields survive in the captured bytes. A refusal, tool call,
    incomplete finish, multiple choice or content absence has no graph side
    effect. Neither fallback retries nor a provider call happens here.
    """
    from .reply import GraphReplyError, compile_reply
    if not isinstance(raw, bytes):
        raise TypeError("graph_reply_completion_bytes_required")
    try:
        if http_status != 200:
            raise ValueError("graph_reply_completion_http_error")
        envelope = json.loads(raw.decode("utf-8"), object_pairs_hook=_pairs,
                              parse_constant=_bad_number, parse_float=_float)
        if not isinstance(envelope, dict) or envelope.get("error"):
            raise ValueError("graph_reply_completion_error_envelope")
        choices = envelope.get("choices")
        if not isinstance(choices, list) or len(choices) != 1 or not isinstance(choices[0], dict):
            raise ValueError("graph_reply_completion_one_choice_required")
        choice = choices[0]
        message = choice.get("message")
        if not isinstance(message, dict) or message.get("role") != "assistant":
            raise ValueError("graph_reply_completion_assistant_required")
        if message.get("refusal"):
            raise ValueError("graph_reply_completion_refused")
        if choice.get("finish_reason") != "stop":
            raise ValueError("graph_reply_completion_incomplete")
        if message.get("tool_calls") or message.get("function_call"):
            raise ValueError("graph_reply_completion_tool_call_instead_of_graph")
        content = message.get("content")
        if not isinstance(content, str):
            raise ValueError("graph_reply_completion_text_content_required")
        returned_model = envelope.get("model")
        if returned_model is not None and (not isinstance(returned_model, str) or not returned_model):
            raise ValueError("graph_reply_completion_model_invalid")
        if not isinstance(requested_model, str) or not requested_model:
            raise ValueError("graph_reply_requested_model_required")
        compilation = compile_reply(packet, content.encode("utf-8"), request_id=request_id,
                                    turn_id=turn_id, model=returned_model or requested_model,
                                    recipe_sha256=recipe_sha256, parent_turn_id=parent_turn_id,
                                    known_at=known_at)
        return {"schema": "loom.graph_reply_completion/1", "compilation": compilation,
                "http_response_sha256": hashlib.sha256(raw).hexdigest(),
                "http_response_base64": base64.b64encode(raw).decode("ascii"),
                "http_response_bytes": len(raw), "requested_model": requested_model,
                "reported_model": returned_model, "model_identity_status": "provider-reported-unverified",
                "reported_provider": envelope.get("provider"),
                "reported_usage": envelope.get("usage"), "automatic_retry": False,
                "provider_schema_enforcement_measured": False}
    except (ValueError, UnicodeError, TypeError, KeyError, RecursionError) as exc:
        # Include graph-codec parse errors in the unchanged HTTP capture. The
        # returned envelope contains no canonical store write in either path.
        if isinstance(exc, RecursionError):
            code = "graph_reply_completion_parser_capacity_exceeded"
        elif isinstance(exc, GraphReplyError):
            code = str(exc)
        elif isinstance(exc, (json.JSONDecodeError, UnicodeError)):
            code = "graph_reply_completion_invalid_json_or_utf8"
        else:
            code = str(exc) if str(exc).startswith("graph_reply_") else "graph_reply_completion_invalid"
        raise CompletionError(code, raw) from None
