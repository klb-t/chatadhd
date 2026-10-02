"""API payload and failure-boundary checks; no real provider requests."""
import base64
from copy import deepcopy
import json
import unittest
from unittest.mock import patch

try:
    from ..agentic_graph_v1 import packet
except ImportError:
    from agentic_graph_v1 import packet
from .prompt import build_request, reply_schema, request_receipt, context_payload
from .transport import consume_completion, CompletionError
from . import transport as api_transport


def fixture():
    return packet.make_packet(origin={"kind": "system", "actor": "api-fixture", "model": None,
                                     "recipe_sha256": None, "response_sha256": None})


def model_answer(base):
    return {"schema": "loom.graph_reply/1", "base_packet_sha256": base["packet_id"],
            "response_id": "r", "nodes": [
                {"id": "r", "text": "Żółć 🧠\nGotowe.", "role": "response", "children": ["p1", "p2"]},
                {"id": "p1", "text": "Żółć 🧠\n", "role": "answer", "children": []},
                {"id": "p2", "text": "Gotowe.", "role": "conclusion", "children": []}], "links": []}


def response(base):
    return {"model": "reported-model", "provider": "mock-provider", "choices": [
        {"finish_reason": "stop", "message": {"role": "assistant", "content": json.dumps(model_answer(base), ensure_ascii=False)}}],
        "usage": {"prompt_tokens": 100, "completion_tokens": 70}, "unknown_field": {"retained": "🧠"}}


class APITests(unittest.TestCase):
    def test_api_itself_requests_graph_schema_with_same_node_format(self):
        base = fixture()
        body = build_request(base, "Odpowiedz.", model="caller-model")
        fmt = body["response_format"]
        self.assertEqual(fmt["type"], "json_schema")
        self.assertTrue(fmt["json_schema"]["strict"])
        self.assertEqual(fmt["json_schema"]["schema"], reply_schema())
        self.assertEqual(body["provider"], {"require_parameters": True})
        self.assertNotIn("tools", body)
        schema = fmt["json_schema"]["schema"]
        node = schema["properties"]["nodes"]["items"]
        self.assertEqual(set(node["required"]), {"id", "text", "role", "children"})
        self.assertFalse(node["additionalProperties"])

    def test_schema_is_strict_portable_subset_and_validates_model_fixture(self):
        import jsonschema
        schema = reply_schema()
        jsonschema.Draft202012Validator.check_schema(schema)
        jsonschema.validate(model_answer(fixture()), schema)
        pending = [schema]
        while pending:
            value = pending.pop()
            if isinstance(value, dict):
                if value.get("type") == "object":
                    self.assertFalse(value["additionalProperties"])
                    self.assertEqual(set(value["properties"]), set(value["required"]))
                pending.extend(value.values())
            elif isinstance(value, list):
                pending.extend(value)

    def test_context_modes_preserve_same_records_and_prior_packet(self):
        base = fixture(); before = deepcopy(base)
        structured, flat = context_payload(base), context_payload(base, "flat")
        reconstructed = {row.split(": ", 1)[0]: json.loads(row.split(": ", 1)[1]) for row in flat.splitlines()}
        self.assertEqual(structured, reconstructed)
        self.assertNotIn("history", structured)
        self.assertEqual(base, before)

    def test_explicit_weaker_modes_do_not_masquerade_as_strict(self):
        base = fixture()
        for mode in ("json_object", "prompt"):
            body = build_request(base, "Odpowiedz.", model="selected", format_mode=mode)
            receipt = request_receipt(body, context_mode="graph", format_mode=mode)
            self.assertFalse(receipt["provider_schema_requested"])
            self.assertFalse(receipt["request_sent"])
            self.assertFalse(receipt["provider_schema_measured"])
        with self.assertRaises(ValueError):
            build_request(base, "Odpowiedz.", model="selected", provider={"require_parameters": False})

    def test_openai_payload_has_no_router_preferences(self):
        body = build_request(fixture(), "Odpowiedz.", model="selected", api="openai")
        self.assertNotIn("provider", body)
        self.assertIn("response_format", body)
        self.assertNotIn("max_tokens", body)

    def test_optional_composed_graph_schema_avoids_duplicate_parent_text(self):
        import jsonschema
        base = fixture()
        body = build_request(base, "Odpowiedz.", model="selected", wire_schema="loom.graph_reply/2")
        schema = body["response_format"]["json_schema"]["schema"]
        self.assertEqual(schema["properties"]["nodes"]["items"]["properties"]["text"]["type"], ["string", "null"])
        value = model_answer(base); value["schema"] = "loom.graph_reply/2"; value["nodes"][0]["text"] = None
        jsonschema.validate(value, schema)
        self.assertIn("Use text: null", body["messages"][0]["content"])
        self.assertIn("schema is loom.graph_reply/2.", body["messages"][0]["content"])

    def test_unknown_wire_schema_does_not_silently_downgrade(self):
        with self.assertRaises(ValueError):
            build_request(fixture(), "Odpowiedz.", model="selected", wire_schema="unknown")

    def test_valid_http_completion_compiles_and_keeps_exact_bytes(self):
        base = fixture(); before = deepcopy(base)
        raw = json.dumps(response(base), ensure_ascii=False).encode()
        result = consume_completion(base, raw, request_id="req_api", turn_id="turn_api", requested_model="caller-model")
        self.assertEqual(base64.b64decode(result["http_response_base64"]), raw)
        self.assertEqual(result["compilation"]["response_text"], "Żółć 🧠\nGotowe.")
        self.assertEqual(result["reported_model"], "reported-model")
        self.assertFalse(result["automatic_retry"])
        self.assertEqual(base, before)

    def test_refusal_truncation_tools_missing_content_and_http_error_keep_first_attempt(self):
        base = fixture(); before = deepcopy(base)
        variants = []
        for finish in ("length", "content_filter", "tool_calls", None):
            value = response(base); value["choices"][0]["finish_reason"] = finish; variants.append(value)
        value = response(base); value["choices"][0]["message"]["refusal"] = "refused"; variants.append(value)
        value = response(base); value["choices"][0]["message"]["content"] = None; variants.append(value)
        value = response(base); value["choices"][0]["message"]["tool_calls"] = [{}]; variants.append(value)
        variants.append({"error": {"message": "service unavailable"}})
        for value in variants:
            raw = json.dumps(value).encode()
            with self.assertRaises(CompletionError) as caught:
                consume_completion(base, raw, request_id="r", turn_id="t", requested_model="model")
            self.assertEqual(base64.b64decode(caught.exception.capture["http_response_base64"]), raw)
            self.assertEqual(base, before)
        raw = json.dumps(response(base)).encode()
        with self.assertRaises(CompletionError):
            consume_completion(base, raw, request_id="r", turn_id="t", requested_model="model", http_status=429)

    def test_invalid_or_duplicate_json_is_never_repaired(self):
        for raw in (b'{"choices":[],"choices":[]}', b'{"choices": NaN}', b'\xff', b'{'):
            with self.assertRaises(CompletionError) as caught:
                consume_completion(fixture(), raw, request_id="r", turn_id="t", requested_model="model")
            self.assertEqual(base64.b64decode(caught.exception.capture["http_response_base64"]), raw)

    def test_parser_capacity_failure_also_retains_first_http_bytes(self):
        raw = b'{"unknown_nested_field":[]}'
        with patch.object(api_transport.json, "loads", side_effect=RecursionError):
            with self.assertRaises(CompletionError) as caught:
                consume_completion(fixture(), raw, request_id="r", turn_id="t", requested_model="model")
        self.assertEqual(str(caught.exception), "graph_reply_completion_parser_capacity_exceeded")
        self.assertEqual(base64.b64decode(caught.exception.capture["http_response_base64"]), raw)

    def test_overflowing_numeric_http_metadata_is_rejected_with_capture(self):
        raw = json.dumps(response(fixture())).replace('"prompt_tokens": 100', '"prompt_tokens": 1e999').encode()
        with self.assertRaises(CompletionError) as caught:
            consume_completion(fixture(), raw, request_id="r", turn_id="t", requested_model="model")
        self.assertEqual(str(caught.exception), "graph_reply_completion_nonfinite")
        self.assertEqual(base64.b64decode(caught.exception.capture["http_response_base64"]), raw)


if __name__ == "__main__":
    unittest.main()
