"""AGEDS connector stub: off by default, bearer header, anchored items only, local verification.

Runs without a live AGEDS: a fake transport records requests and returns canned
responses shaped like AGEDS ``ageds.read/v1`` / ``ageds.intent-result/v1``.
    python3 -m unittest engine.tests.test_ageds_adapter -v
"""
from __future__ import annotations

import hashlib
import json
import unittest

from engine import ageds_adapter as ageds

TOKEN = "ageds_0123456789ab.sEcReT-value_123"
QUOTE = " spotkanie w biurze o dziewiątej"
QUOTE_SHA = hashlib.sha256(QUOTE.encode("utf-8")).hexdigest()
SELECTOR = {"kind": "segments", "indices": [1], "text_join": "concatenate_exact", "time_unit": "seconds",
            "stored_time_unit": "milliseconds", "rounding": "nearest_ms", "precision": "segment"}
SEGMENT_ITEM = {"item_id": "segment:7:1", "kind": "transcript_segment", "artifact_id": 3, "derived_text_id": 7, "selector": SELECTOR,
                "quote_text": QUOTE, "quote_sha256": QUOTE_SHA, "start_ms": 1250, "end_ms": 2500, "verification": "projection_from_stored_segments",
                "audio_verification": "not_performed"}
EVENT_ITEM = {"item_id": "event:12", "kind": "event", "event_id": 12, "artifact_id": 5, "ts_start": "2026-03-01T08:00:00+00:00",
              "quote_text": "spotkanie potwierdzone", "quote_sha256": hashlib.sha256(b"spotkanie potwierdzone").hexdigest(), "verification": "exact_stored_event_text"}
ANCHORED = {"schema": "ageds.intent-result/v1", "mode": "anchored", "intent": {"kind": "search", "q": "biurze"}, "items": [SEGMENT_ITEM, EVENT_ITEM],
            "count": 2, "has_more": False, "complete": True, "truncated": False, "prose": None}
PACKET = {"schema": "ageds.citation-evidence/v1",
          "payload": {"anchor": {"id": 9, "derived_text_id": 7, "quote_text": QUOTE, "quote_sha256": QUOTE_SHA, "start_ms": 1250, "end_ms": 2500},
                      "projection": {"selector": SELECTOR}, "derived_text": {"id": 7}, "unknowns": ["processing_run_unknown"]},
          "integrity": {"algorithm": "sha256", "payload_sha256": "0" * 64}}


class Store(dict):
    """Minimal stand-in for engine.config.Config / Secrets (``get(key, fallback)``)."""

    def get(self, key, fallback=None):
        return super().get(key, fallback)


class FakeTransport:
    def __init__(self, responses):
        self.responses = list(responses)
        self.calls = []

    def __call__(self, method, url, headers, body, timeout):
        self.calls.append({"method": method, "url": url, "headers": headers, "body": json.loads(body) if body else None, "timeout": timeout})
        status, payload = self.responses.pop(0)
        content = payload if isinstance(payload, bytes) else json.dumps(payload, ensure_ascii=False).encode("utf-8")
        return status, {"content-type": "application/json"}, content


def client(*responses):
    transport = FakeTransport(responses)
    return ageds.AgedsReadClient("http://127.0.0.1:8000/", TOKEN, transport=transport, timeout=5), transport


class SettingsTests(unittest.TestCase):
    def test_off_by_default_and_no_request_without_endpoint_or_token(self):
        transport = FakeTransport([])
        with self.assertRaises(ageds.AgedsDisabled):
            ageds.from_settings(Store(), Store(), transport=transport)
        with self.assertRaises(ageds.AgedsDisabled):
            ageds.from_settings(Store(ageds_enabled=True), Store(), transport=transport)
        with self.assertRaises(ageds.AgedsDisabled) as ctx:
            ageds.from_settings(Store(ageds_enabled=True, ageds_endpoint="http://127.0.0.1:8000"), Store(), transport=transport)
        self.assertIn("ageds_read_token", str(ctx.exception))
        self.assertEqual(transport.calls, [])
        built = ageds.from_settings(Store(ageds_enabled=True, ageds_endpoint="http://127.0.0.1:8000/", ageds_timeout_s=7),
                                    Store(ageds_read_token=TOKEN), transport=transport)
        self.assertEqual((built.endpoint, built.timeout, built.client_id), ("http://127.0.0.1:8000", 7.0, "chatadhd"))
        self.assertEqual(transport.calls, [], "construction contacts nothing")
        with self.assertRaises(ageds.AgedsDisabled):
            ageds.AgedsReadClient("ftp://x", TOKEN, transport=transport)

    def test_presets_are_not_written_into_config_defaults(self):
        from engine.config import DEFAULTS
        self.assertFalse(set(ageds.PRESETS) & set(DEFAULTS), "compat invariant: config.json layout unchanged")

    def test_connector_descriptor_is_tool_source_shaped_and_never_holds_the_secret(self):
        descriptor = ageds.connector_descriptor(Store(ageds_endpoint="http://host:8000"), Store(ageds_read_token=TOKEN))
        self.assertEqual((descriptor["id"], descriptor["kind"], descriptor["transport"]), ("ageds", "ageds_read", "http"))
        self.assertEqual(descriptor["access"], {"secret": "ageds_read_token", "configured": True})
        self.assertFalse(descriptor["enabled"])
        self.assertTrue(descriptor["effects"]["reads_only"])
        self.assertTrue(descriptor["effects"]["touches_private_data"])
        self.assertFalse(descriptor["effects"]["effects_unknown"])
        self.assertNotIn(TOKEN, json.dumps(descriptor))
        self.assertEqual(descriptor["graph"]["node"], "connector:ageds")
        self.assertIn("ageds.intent", descriptor["tools"])


class RequestTests(unittest.TestCase):
    def test_bearer_header_and_contract_check(self):
        api, transport = client((200, {"schema": "ageds.read/v1", "endpoints": {}}))
        contract = api.contract()
        self.assertEqual(contract["schema"], "ageds.read/v1")
        call = transport.calls[0]
        self.assertEqual((call["method"], call["url"]), ("GET", "http://127.0.0.1:8000/api/read/v1/contract"))
        self.assertEqual(call["headers"]["Authorization"], f"Bearer {TOKEN}")
        self.assertEqual(call["timeout"], 5)
        api, _ = client((200, {"schema": "something-else/v9"}))
        with self.assertRaises(ageds.AgedsContractError):
            api.contract()

    def test_search_and_timeline_parameters(self):
        api, transport = client((200, {"schema": "ageds.read/v1", "kind": "search", "items": [], "has_more": False, "complete": True}),
                                (200, {"schema": "ageds.read/v1", "kind": "timeline_window", "items": [], "next_cursor": None}))
        api.search("umowa AND biuro", limit=20)
        self.assertEqual(transport.calls[0]["url"], "http://127.0.0.1:8000/api/read/v1/search?q=umowa+AND+biuro&limit=20")
        api.timeline(from_="2026-03-01", to=None, phone="+48 000 000 001", limit=10)
        from urllib.parse import parse_qs, urlsplit
        parts = urlsplit(transport.calls[1]["url"])
        self.assertEqual(parts.path, "/api/read/v1/timeline")
        self.assertEqual(parse_qs(parts.query), {"from": ["2026-03-01"], "phone": ["+48 000 000 001"], "limit": ["10"]})

    def test_auth_errors_carry_server_code_and_never_the_token(self):
        api, _ = client((401, {"detail": {"code": "revoked_token", "message": "Token was revoked."}}))
        with self.assertRaises(ageds.AgedsAuthError) as ctx:
            api.contract()
        self.assertEqual((ctx.exception.status, ctx.exception.code), (401, "revoked_token"))
        self.assertNotIn(TOKEN, str(ctx.exception))
        api, _ = client((403, {"detail": {"code": "insufficient_scope", "message": "Token lacks the required scope."}}))
        with self.assertRaises(ageds.AgedsAuthError) as ctx:
            api.propose_annotation({"artifact_id": 1, "body": "x"})
        self.assertEqual(ctx.exception.code, "insufficient_scope")
        api, _ = client((503, {"detail": {"code": "work_limit", "message": "narrow the request"}}))
        with self.assertRaises(ageds.AgedsHttpError) as ctx:
            api.search("x")
        self.assertEqual((ctx.exception.status, ctx.exception.code), (503, "work_limit"))
        api, _ = client((500, b"<html>" + TOKEN.encode() + b" leaked?</html>"))
        with self.assertRaises(ageds.AgedsHttpError) as ctx:
            api.search("x")
        self.assertNotIn(TOKEN, str(ctx.exception))
        self.assertIn("***", str(ctx.exception))
        self.assertEqual(ageds.redact("see ageds_0123456789ab.other-secret_9 here"), "see ageds_*** here")

        def broken(*args):
            raise ConnectionError(f"refused while sending {TOKEN}")
        api = ageds.AgedsReadClient("http://127.0.0.1:1", TOKEN, transport=broken)
        with self.assertRaises(ageds.AgedsError) as ctx:
            api.contract()
        self.assertNotIn(TOKEN, str(ctx.exception))


class AnchoredTests(unittest.TestCase):
    def test_intent_returns_items_only_and_refuses_prose(self):
        api, transport = client((200, ANCHORED))
        result = api.execute_intent({"kind": "search", "q": "biurze"})
        self.assertEqual(transport.calls[0]["body"], {"kind": "search", "q": "biurze"})
        self.assertEqual(transport.calls[0]["headers"]["Content-Type"], "application/json")
        self.assertEqual(result.item_ids(), ["segment:7:1", "event:12"])
        self.assertEqual([q["quote_text"] for q in result.quotes()], [QUOTE, "spotkanie potwierdzone"])
        self.assertFalse(result.truncated)
        self.assertNotIn("prose", result.__dataclass_fields__)
        for bad in ({**ANCHORED, "prose": "Umowa została podpisana."}, {**ANCHORED, "mode": "interpretive"},
                    {**ANCHORED, "schema": "other"}, {**ANCHORED, "items": "x"}):
            with self.subTest(bad={k: bad[k] for k in ("mode", "prose", "schema")}):
                api, _ = client((200, bad))
                with self.assertRaises(ageds.AgedsContractError):
                    api.execute_intent({"kind": "search", "q": "biurze"})
        with self.assertRaises(ageds.AgedsContractError):
            api.execute_intent({"kind": "summarize"})

    def test_verify_quote_locally_and_against_packet(self):
        api, _ = client((200, PACKET))
        packet = api.citation_packet(3, 9)
        self.assertEqual(len(packet["_raw_sha256"]), 64)
        report = ageds.AgedsReadClient.verify_quote(SEGMENT_ITEM, packet)
        self.assertTrue(report["text_hash_matches"])
        self.assertTrue(report["packet_quote_matches"])
        self.assertTrue(report["packet_selector_matches"])
        self.assertTrue(report["packet_version_matches"])
        self.assertTrue(report["verified"])
        self.assertEqual(report["packet_unknowns"], ["processing_run_unknown"])
        tampered = {**SEGMENT_ITEM, "quote_text": QUOTE + "!"}
        self.assertFalse(ageds.AgedsReadClient.verify_quote(tampered)["verified"])
        other_version = {**SEGMENT_ITEM, "derived_text_id": 8}
        self.assertFalse(ageds.AgedsReadClient.verify_quote(other_version, packet)["verified"])
        self.assertIn("not spoken truth", report["note"])
        api, _ = client((200, {"schema": "ageds.graph-export/v1"}))
        with self.assertRaises(ageds.AgedsContractError):
            api.citation_packet(3, 9)

    def test_render_quotes_is_verbatim(self):
        api, _ = client((200, {**ANCHORED, "has_more": True}))
        text = ageds.render_quotes(api.execute_intent({"kind": "search", "q": "biurze"}))
        self.assertIn("«spotkanie w biurze o dziewiątej» — artifact 3, version 7, segments [1], sha256 " + QUOTE_SHA[:12], text)
        self.assertIn("[event:12] «spotkanie potwierdzone» — event 12 at 2026-03-01T08:00:00+00:00", text)
        self.assertIn("more results exist", text)
        reference = {**ANCHORED, "items": [{"item_id": "derived_text:7", "kind": "derived_text_reference", "anchor": None, "reason": "no_segment_contains_query_terms"}]}
        api, _ = client((200, reference))
        self.assertEqual(ageds.render_quotes(api.execute_intent({"kind": "search", "q": "x"})), "[derived_text:7] derived_text_reference — no_segment_contains_query_terms")

    def test_interpretive_validation_requires_label(self):
        labelled = {"schema": "ageds.interpretive-answer/v1", "mode": "interpretive", "label": {"mode": "interpretive", "provider": "openrouter", "model": "m", "run": "unknown"},
                    "claims": [{"index": 0, "text": "x", "item_ids": ["segment:7:1"], "unknown_item_ids": [], "support": "referenced"},
                               {"index": 1, "text": "y", "item_ids": [], "unknown_item_ids": [], "support": "unsupported"}], "unsupported_count": 1, "supported_count": 1}
        api, transport = client((200, labelled))
        result = api.validate_interpretive({"provider": "openrouter", "model": "m", "prose": "x y", "claims": [{"text": "x", "item_ids": ["segment:7:1"]}, {"text": "y"}],
                                            "item_ids_available": ["segment:7:1"]})
        self.assertEqual(transport.calls[0]["body"]["mode"], "interpretive")
        self.assertEqual(result["unsupported_count"], 1)
        api, _ = client((200, {**labelled, "label": {}}))
        with self.assertRaises(ageds.AgedsContractError):
            api.validate_interpretive({"prose": "x"})

    def test_propose_annotation_is_only_a_proposal(self):
        api, transport = client((201, {"id": 4, "status": "proposed", "client_id": "chatadhd"}))
        stored = api.propose_annotation({"artifact_id": 3, "derived_text_id": 7, "body": "ważne", "basis": ["segment:7:1"], "mode": "anchored"})
        self.assertEqual(stored["status"], "proposed")
        self.assertEqual(transport.calls[0]["url"], "http://127.0.0.1:8000/api/integrations/v1/proposals")
        api, _ = client((201, {"id": 4, "status": "accepted"}))
        with self.assertRaises(ageds.AgedsContractError):
            api.propose_annotation({"artifact_id": 3, "body": "x"})


if __name__ == "__main__":
    unittest.main()
