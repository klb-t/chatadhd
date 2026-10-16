"""AGEDS read connector (stub) — anchored quotes from the Evidence Workbench.

ChatADHD talks to AGEDS only through its versioned read API
(``/api/read/v1``, bearer token). The model's job is to parse the user's
question into a typed *intent*; AGEDS executes it and returns **anchored
items** (artifact id, transcript version id, selector, exact quote,
``quote_sha256``). This module never synthesizes prose from evidence and
refuses any result that is not ``mode: anchored`` with ``prose: null``.
Interpretive prose is a separate, labelled contract validated by AGEDS.

Off by default. Settings are read with ``config.get(key, preset)`` and
``secrets.get('ageds_read_token')``; nothing is added to ``engine.config.DEFAULTS``
(the Python↔C++ compat invariant keeps ``config.json`` unchanged), so a config
file without these keys is identical to today's. The connector descriptor
mirrors ``loom/data/runtime/tool_sources.pack`` (a source = kind, transport,
url, a *reference* to a vault key, effects) so a future Loom ToolSource adapter
can read the same data.

No network call happens at import or construction; the first request is the
first contact, and only after the user enabled the connector.
"""
from __future__ import annotations

import hashlib
import json
import logging
import re
from dataclasses import dataclass, field
from typing import Any, Callable, Optional
from urllib.parse import urlencode

log = logging.getLogger(__name__)

CONNECTOR_ID = "ageds"
CONNECTOR_KIND = "ageds_read"
CONTRACT_SCHEMA = "ageds.read/v1"
INTENT_RESULT_SCHEMA = "ageds.intent-result/v1"
INTERPRETIVE_SCHEMA = "ageds.interpretive-answer/v1"
CITATION_SCHEMA = "ageds.citation-evidence/v1"
SECRET_KEY = "ageds_read_token"
# Presets, not rules (owner): every value is a user setting; read with config.get(key, preset).
PRESETS: dict[str, Any] = {
    "ageds_enabled": False,
    "ageds_endpoint": "",
    "ageds_timeout_s": 30,
    "ageds_client_id": "chatadhd",
    "ageds_search_limit": 50,
}
INTENT_KINDS = ("search", "timeline_window", "artifact", "citation", "quote")

Transport = Callable[[str, str, dict, Optional[bytes], float], tuple[int, dict, bytes]]
_TOKEN_RE = re.compile(r"ageds_[0-9a-f]+\.[A-Za-z0-9_\-]+")


class AgedsError(Exception):
    """Base error; message never contains the token."""


class AgedsDisabled(AgedsError):
    """Connector is off or incompletely configured; no request was made."""


class AgedsAuthError(AgedsError):
    """401/403 from AGEDS; ``code`` is the stable server code."""

    def __init__(self, status: int, code: str, message: str):
        super().__init__(f"AGEDS refused the token ({status} {code}): {message}")
        self.status, self.code = status, code


class AgedsHttpError(AgedsError):
    def __init__(self, status: int, code: str, message: str):
        super().__init__(f"AGEDS error {status} {code}: {message}")
        self.status, self.code = status, code


class AgedsContractError(AgedsError):
    """The response does not satisfy the contract this client relies on."""


def redact(text: object, *secrets: Optional[str]) -> str:
    """Strip the token (and any token-shaped string) from text shown to the user or logs."""
    out = str(text)
    for secret in secrets:
        if secret:
            out = out.replace(secret, "***")
    return _TOKEN_RE.sub("ageds_***", out)


def connector_descriptor(config, secrets=None) -> dict:
    """Tool-source-shaped description of this connector (data, as in tool_sources.pack).

    ``access.secret`` is the *name* of the vault/secrets key, never the bytes.
    Effects follow the shipped preset vocabulary: reading only, but the question
    text leaves the device to the AGEDS host, which holds private evidence.
    """
    endpoint = str(config.get("ageds_endpoint", PRESETS["ageds_endpoint"]) or "").strip()
    has_token = bool(secrets is not None and secrets.get(SECRET_KEY, ""))
    return {
        "id": CONNECTOR_ID,
        "kind": CONNECTOR_KIND,
        "label": {"en": "AGEDS evidence workbench (read)", "pl": "AGEDS — warsztat dowodowy (odczyt)"},
        "transport": "http",
        "url": endpoint,
        "contract": CONTRACT_SCHEMA,
        "access": {"secret": SECRET_KEY, "configured": has_token},
        "enabled": bool(config.get("ageds_enabled", PRESETS["ageds_enabled"])),
        "effects": {
            "reads_only": True, "writes": False, "sends_off_device": True, "costs_money": False,
            "irreversible": False, "touches_private_data": True, "effects_unknown": False,
            "note": "the parsed intent (not the conversation) is sent to the AGEDS host; proposals require the propose scope",
        },
        "tools": {
            "ageds.intent": {"description": "execute a typed read intent; returns anchored quotes only", "effects": {"reads_only": True}},
            "ageds.citation_packet": {"description": "fetch a pinned citation packet for verification", "effects": {"reads_only": True}},
            "ageds.propose_annotation": {"description": "submit an annotation proposal (human accepts in AGEDS)", "effects": {"writes": True, "reads_only": False}},
        },
        "graph": {"node": f"connector:{CONNECTOR_ID}", "class": "service_environment"},
    }


def from_settings(config, secrets, *, transport: Optional[Transport] = None) -> "AgedsReadClient":
    """Build a client from the user's settings; raise ``AgedsDisabled`` when off."""
    if not config.get("ageds_enabled", PRESETS["ageds_enabled"]):
        raise AgedsDisabled("AGEDS connector is off (setting ageds_enabled)")
    endpoint = str(config.get("ageds_endpoint", PRESETS["ageds_endpoint"]) or "").strip()
    if not endpoint:
        raise AgedsDisabled("AGEDS endpoint is not set (setting ageds_endpoint)")
    token = secrets.get(SECRET_KEY, "") or ""
    if not token:
        raise AgedsDisabled(f"AGEDS token is not set (secret {SECRET_KEY})")
    return AgedsReadClient(endpoint, token, transport=transport,
                           timeout=float(config.get("ageds_timeout_s", PRESETS["ageds_timeout_s"])),
                           client_id=str(config.get("ageds_client_id", PRESETS["ageds_client_id"])))


def _requests_transport(method: str, url: str, headers: dict, body: Optional[bytes], timeout: float) -> tuple[int, dict, bytes]:
    import requests  # lazy: the GUI must import this module without touching the network stack
    response = requests.request(method, url, headers=headers, data=body, timeout=timeout)
    return response.status_code, dict(response.headers), response.content


@dataclass
class AnchoredResult:
    """Items from an anchored intent result; ``prose`` is structurally absent."""
    intent: dict
    items: list[dict]
    has_more: Optional[bool]
    complete: Optional[bool]
    truncated: bool
    raw: dict = field(repr=False)

    def item_ids(self) -> list[str]:
        return [item["item_id"] for item in self.items if "item_id" in item]

    def quotes(self) -> list[dict]:
        return [item for item in self.items if item.get("quote_text") is not None]


class AgedsReadClient:
    def __init__(self, endpoint: str, token: str, *, transport: Optional[Transport] = None,
                 timeout: float = 30.0, client_id: str = "chatadhd"):
        if not endpoint.startswith(("http://", "https://")):
            raise AgedsDisabled("AGEDS endpoint must be an http(s) URL")
        self.endpoint = endpoint.rstrip("/")
        self._token = token
        self._transport = transport or _requests_transport
        self.timeout = timeout
        self.client_id = client_id

    # --- transport -------------------------------------------------------------
    def _request(self, method: str, path: str, *, params: Optional[dict] = None, body: Optional[dict] = None, raw: bool = False):
        url = self.endpoint + path
        if params:
            url += "?" + urlencode({k: v for k, v in params.items() if v is not None})
        headers = {"Authorization": f"Bearer {self._token}", "Accept": "application/json",
                   "User-Agent": f"chatadhd-ageds-connector/1 ({self.client_id})"}
        payload = None
        if body is not None:
            headers["Content-Type"] = "application/json"
            payload = json.dumps(body, ensure_ascii=False, allow_nan=False).encode("utf-8")
        try:
            status, _headers, content = self._transport(method, url, headers, payload, self.timeout)
        except AgedsError:
            raise
        except Exception as exc:  # transport failure; keep the token out of the message
            log.debug("AGEDS transport failure", exc_info=True)
            raise AgedsError("AGEDS request failed: " + redact(exc, self._token)) from exc
        if status >= 400:
            code, message = "http_error", redact(content[:500].decode("utf-8", "replace"), self._token)
            try:
                detail = json.loads(content.decode("utf-8")).get("detail")
                if isinstance(detail, dict):
                    code, message = str(detail.get("code", code)), redact(detail.get("message", ""), self._token)
                elif isinstance(detail, str):
                    message = redact(detail, self._token)
            except (ValueError, AttributeError):
                pass
            if status in (401, 403):
                raise AgedsAuthError(status, code, message)
            raise AgedsHttpError(status, code, message)
        if raw:
            return content
        try:
            return json.loads(content.decode("utf-8"))
        except ValueError as exc:
            raise AgedsContractError("AGEDS returned a non-JSON body") from exc

    # --- reads -----------------------------------------------------------------
    def contract(self) -> dict:
        result = self._request("GET", "/api/read/v1/contract")
        if not isinstance(result, dict) or result.get("schema") != CONTRACT_SCHEMA:
            raise AgedsContractError(f"unexpected contract schema: {result.get('schema') if isinstance(result, dict) else type(result).__name__}")
        return result

    def search(self, q: str, limit: Optional[int] = None) -> dict:
        return self._request("GET", "/api/read/v1/search", params={"q": q, "limit": limit})

    def timeline(self, **params) -> dict:
        if "from_" in params:
            params["from"] = params.pop("from_")
        return self._request("GET", "/api/read/v1/timeline", params=params)

    def artifact(self, artifact_id: int) -> dict:
        return self._request("GET", f"/api/read/v1/artifacts/{int(artifact_id)}")

    def citation_packet(self, artifact_id: int, anchor_id: int) -> dict:
        content = self._request("GET", f"/api/read/v1/artifacts/{int(artifact_id)}/citations/{int(anchor_id)}/packet", raw=True)
        try:
            packet = json.loads(content.decode("utf-8"))
        except ValueError as exc:
            raise AgedsContractError("citation packet is not JSON") from exc
        if packet.get("schema") != CITATION_SCHEMA:
            raise AgedsContractError(f"unexpected packet schema: {packet.get('schema')}")
        packet["_raw_sha256"] = hashlib.sha256(content).hexdigest()
        return packet

    def reports(self) -> dict:
        return self._request("GET", "/api/read/v1/reports")

    def intent_schema(self) -> dict:
        return self._request("GET", "/api/read/v1/intents/schema")

    # --- function 1.5: intent in, anchored quotes out ----------------------------
    def execute_intent(self, intent: dict) -> AnchoredResult:
        if not isinstance(intent, dict) or intent.get("kind") not in INTENT_KINDS:
            raise AgedsContractError("intent must be an object with kind in " + ", ".join(INTENT_KINDS))
        result = self._request("POST", "/api/read/v1/intents/execute", body=intent)
        if not isinstance(result, dict) or result.get("schema") != INTENT_RESULT_SCHEMA:
            raise AgedsContractError("unexpected intent result schema")
        if result.get("mode") != "anchored" or result.get("prose") is not None:
            # Never render anything but anchored evidence from this path.
            raise AgedsContractError("refusing a non-anchored intent result (mode=%r, prose present=%s)"
                                     % (result.get("mode"), result.get("prose") is not None))
        items = result.get("items")
        if not isinstance(items, list):
            raise AgedsContractError("intent result has no items array")
        return AnchoredResult(intent=dict(intent), items=items, has_more=result.get("has_more"),
                              complete=result.get("complete"), truncated=bool(result.get("truncated")), raw=result)

    def validate_interpretive(self, answer: dict) -> dict:
        """Ask AGEDS to label an interpretive answer; claims without known items come back ``unsupported``."""
        body = dict(answer)
        body["mode"] = "interpretive"
        result = self._request("POST", "/api/read/v1/intents/interpretive/validate", body=body)
        if not isinstance(result, dict) or result.get("schema") != INTERPRETIVE_SCHEMA or result.get("label", {}).get("mode") != "interpretive":
            raise AgedsContractError("interpretive answer came back without the mandatory label")
        return result

    def propose_annotation(self, proposal: dict) -> dict:
        """Submit a proposal (requires the propose scope); AGEDS stores it as 'proposed'."""
        result = self._request("POST", "/api/integrations/v1/proposals", body=proposal)
        if not isinstance(result, dict) or result.get("status") != "proposed":
            raise AgedsContractError("proposal was not stored as 'proposed'")
        return result

    # --- local verification (no network) ---------------------------------------
    @staticmethod
    def verify_quote(item: dict, packet: Optional[dict] = None) -> dict:
        """Recompute the quote hash locally and, with a packet, compare the pinned anchor."""
        text = item.get("quote_text")
        claimed = item.get("quote_sha256")
        computed = hashlib.sha256(text.encode("utf-8")).hexdigest() if isinstance(text, str) else None
        report = {"item_id": item.get("item_id"), "text_hash_recomputed": computed is not None,
                  "text_hash_matches": computed is not None and computed == claimed, "packet_checked": False,
                  "packet_quote_matches": None, "packet_selector_matches": None, "packet_version_matches": None,
                  "note": "hash agreement is byte identity of the stored transcript selection, not spoken truth or acoustic alignment"}
        if packet is not None:
            payload = packet.get("payload", {})
            anchor, projection, version = payload.get("anchor", {}), payload.get("projection", {}), payload.get("derived_text", {})
            report.update(packet_checked=True,
                          packet_quote_matches=anchor.get("quote_sha256") == claimed and anchor.get("quote_text") == text,
                          packet_selector_matches=projection.get("selector") == item.get("selector"),
                          packet_version_matches=version.get("id") == item.get("derived_text_id"),
                          packet_unknowns=list(payload.get("unknowns", [])))
        report["verified"] = bool(report["text_hash_matches"] and (not report["packet_checked"] or (
            report["packet_quote_matches"] and report["packet_selector_matches"] and report["packet_version_matches"])))
        return report


def render_quotes(result: AnchoredResult) -> str:
    """Plain-text rendering of anchored items for the chat view; no synthesis."""
    lines = []
    for item in result.items:
        kind = item.get("kind", "item")
        if item.get("quote_text") is not None:
            anchor = f"artifact {item.get('artifact_id')}"
            if item.get("derived_text_id") is not None:
                anchor += f", version {item['derived_text_id']}"
            selector = item.get("selector") or {}
            if selector.get("indices") is not None:
                anchor += f", segments {selector['indices']}"
            if item.get("event_id") is not None:
                anchor = f"event {item['event_id']}" + (f" at {item['ts_start']}" if item.get("ts_start") else " (untimed)")
            digest = (item.get("quote_sha256") or "")[:12]
            lines.append(f"[{item.get('item_id')}] «{item['quote_text'].strip()}» — {anchor}, sha256 {digest}…")
        else:
            reason = item.get("reason")
            lines.append(f"[{item.get('item_id')}] {kind}" + (f" — {reason}" if reason else ""))
    if result.truncated or result.has_more:
        lines.append("(more results exist; narrow the question)")
    return "\n".join(lines) if lines else "(no anchored evidence for this intent)"
