"""Offline native-byte and billing evidence tests; no keys or paid calls."""
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch
import urllib.error
import http.client

try:
    from . import research_programme_transport as transport
    from . import new_budget5eur_gate as gate
except ImportError:
    import research_programme_transport as transport
    import new_budget5eur_gate as gate


def config():
    return {"base_url": "https://openrouter.ai", "timeout_seconds": 17,
            "routes": {"key": {"method": "GET", "path": "/api/v1/key"},
                       "chat": {"method": "POST", "path": "/api/v1/chat/completions"},
                       "jev": {"method": "POST", "path": "/api/alpha/decisions"},
                       "model_endpoints": {"method": "GET", "path": "/api/v1/models/{model_id}/endpoints"},
                       "generation": {"method": "GET", "path": "/api/v1/generation", "query": ["id"]}}}


def response(value, status=200):
    return {"http_status": status, "raw": json.dumps(value).encode(), "latency_seconds": 0.1}


def key_metadata(**changes):
    data = {"usage": "0.25", "byok_usage": "0.05", "limit": "1", "limit_remaining": "0.7",
            "limit_reset": None, "include_byok_in_limit": True,
            "is_management_key": False, "is_provisioning_key": False}
    data.update(changes)
    return response({"data": data})


def chat(**changes):
    value = {"id": "gen-offline", "model": "offline/tiny", "provider": "Offline",
             "usage": {"cost": "0.0000123", "prompt_tokens": 20, "completion_tokens": 4},
             "choices": [{"finish_reason": "length", "message": {"content": "broken"}}]}
    value.update(changes)
    return value


def generation(**changes):
    data = {"id": "gen-offline", "model": "offline/tiny", "provider_name": "Offline",
            "api_type": "completions", "total_cost": "0.0000123", "is_byok": False}
    data.update(changes)
    return response({"data": data})


class ByteTransportTests(unittest.TestCase):
    def setUp(self):
        self.transport = transport.OpenRouterTransport(config(), b"offline-fixture-token\n")

    def opened(self, raw, status=200):
        handle = io.BytesIO(raw)
        handle.getcode = lambda: status
        self.transport._opener = Mock()
        self.transport._opener.open.return_value = handle
        return self.transport._opener

    def test_native_bytes_body_and_all_routes(self):
        cases = [("GET", "key", None, None, "https://openrouter.ai/api/v1/key"),
                 ("POST", "chat", b'{ "keep" : "all bytes" }\n', None,
                  "https://openrouter.ai/api/v1/chat/completions"),
                 ("POST", "jev", b'{}\n', None, "https://openrouter.ai/api/alpha/decisions"),
                 ("GET", "model_endpoints", None, {"model_id": "offline/tiny?x=#"},
                  "https://openrouter.ai/api/v1/models/offline/tiny%3Fx%3D%23/endpoints"),
                 ("GET", "generation", None, {"id": "gen-a&key=b"},
                  "https://openrouter.ai/api/v1/generation?id=gen-a%26key%3Db")]
        for method, route, body, params, url in cases:
            with self.subTest(route=route):
                raw = b'\xff\x00first response\r\n' + bytes(range(256))
                opener = self.opened(raw, 418)
                result = self.transport.request(method, route, body, params)
                self.assertEqual(result["raw"], raw)
                self.assertEqual(result["http_status"], 418)
                self.assertGreaterEqual(result["latency_seconds"], 0)
                request = opener.open.call_args.args[0]
                self.assertEqual(request.full_url, url)
                self.assertEqual(request.data, body)
                self.assertEqual(request.get_header("Authorization"), "Bearer offline-fixture-token")
                self.assertEqual(opener.open.call_args.kwargs["timeout"], 17)
                self.assertEqual(opener.open.call_count, 1)

    def test_http_error_keeps_every_byte_beyond_old_response_ceiling(self):
        raw = b'\x00\xff' * (2 * 1024 * 1024) + b"terminal bytes"
        self.transport._opener = Mock()
        self.transport._opener.open.side_effect = urllib.error.HTTPError(
            "https://openrouter.ai/api/alpha/decisions", 429, "remote message", {}, io.BytesIO(raw))
        result = self.transport.request("POST", "jev", b"fixture")
        self.assertEqual(result["http_status"], 429)
        self.assertEqual(result["raw"], raw)
        self.assertEqual(self.transport._opener.open.call_count, 1)

    def test_redirect_is_returned_as_first_http_response(self):
        raw = b"first redirect response bytes"
        self.transport._opener = Mock()
        self.transport._opener.open.side_effect = urllib.error.HTTPError(
            "https://openrouter.ai/api/v1/key", 302, "redirect", {"Location": "https://elsewhere.invalid"},
            io.BytesIO(raw))
        result = self.transport.request("GET", "key")
        self.assertEqual(result["raw"], raw)
        self.assertEqual(result["http_status"], 302)
        self.assertIsNone(transport._NoRedirect().redirect_request(None, None, 302, "", {}, "https://elsewhere.invalid"))
        self.assertEqual(self.transport._opener.open.call_count, 1)

    def test_partial_read_survives_without_retry(self):
        handle = Mock()
        handle.getcode.return_value = 200
        handle.read.side_effect = [b"first chunk", http.client.IncompleteRead(b"last partial")]
        self.transport._opener = Mock()
        self.transport._opener.open.return_value = handle
        result = self.transport.request("POST", "chat", b"fixture")
        self.assertEqual(result["raw"], b"first chunklast partial")
        self.assertEqual(result["transport_error"], "response_read_failed")
        self.assertEqual(self.transport._opener.open.call_count, 1)

    def test_exception_and_origin_do_not_expose_credential_or_request(self):
        self.transport._opener = Mock()
        self.transport._opener.open.side_effect = RuntimeError("offline-fixture-token private request")
        with self.assertRaises(transport.TransportError) as failure:
            self.transport.request("POST", "chat", b"private request")
        self.assertEqual(str(failure.exception), "transport_failed")
        self.assertTrue(failure.exception.__suppress_context__)
        for origin in ("http://openrouter.ai", "https://openrouter.ai.evil.invalid", "https://openrouter.ai/api/v1"):
            edited = config()
            edited["base_url"] = origin
            with self.assertRaises(transport.TransportError):
                transport.OpenRouterTransport(edited, "offline-fixture-token")

    def test_editable_routes_cannot_change_credential_origin(self):
        for path in ("//evil.invalid/x", "https://evil.invalid/x", "/api/\nsecret", "/api/x#fragment", "/\\evil.invalid"):
            edited = config()
            edited["routes"]["key"]["path"] = path
            instance = transport.OpenRouterTransport(edited, "offline-fixture-token")
            instance._opener = Mock()
            with self.assertRaises(transport.TransportError):
                instance.request("GET", "key")
            self.assertEqual(instance._opener.open.call_count, 0)

    def test_proxy_environment_requires_explicit_data_opt_in(self):
        for mode, expected_args in ((None, ({},)), ("disabled", ({},)), ("environment", ())):
            with self.subTest(mode=mode):
                edited = config()
                if mode is not None:
                    edited["proxy_mode"] = mode
                with patch.object(transport.urllib.request, "ProxyHandler") as proxy, \
                        patch.object(transport.urllib.request, "build_opener") as build:
                    transport.OpenRouterTransport(edited, "offline-fixture-token")
                    proxy.assert_called_once_with(*expected_args)
                    self.assertIs(build.call_args.args[0], proxy.return_value)
                    self.assertIsInstance(build.call_args.args[1], transport._NoRedirect)
        for mode in ("auto", None, True, {}, []):
            edited = config()
            edited["proxy_mode"] = mode
            with self.subTest(invalid=mode), self.assertRaises(transport.TransportError):
                transport.OpenRouterTransport(edited, "offline-fixture-token")


class CredentialFileTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.repo = self.directory / "repo"
        self.repo.mkdir()
        (self.repo / ".git").mkdir()
        self.private = self.directory / "private"
        self.private.mkdir(mode=0o700)
        self.key = self.private / "credential"
        self.key.write_bytes(b"offline-fixture-token\n")
        self.key.chmod(0o600)

    def test_fingerprint_is_exact_bearer_bytes_after_one_terminator(self):
        key = transport.load_key_file(self.key, self.repo)
        self.assertEqual(key, "offline-fixture-token")
        expected = hashlib.sha256(b"offline-fixture-token").hexdigest()
        for value in (key, b"offline-fixture-token\n", b"offline-fixture-token\r\n"):
            self.assertEqual(transport.key_fingerprint(value), expected)
        for value in (b" offline-fixture-token", b"offline-fixture-token\n\n", b"offline fixture", b"x\r", b"\xff"):
            with self.assertRaises(transport.TransportError):
                transport.key_fingerprint(value)

    def test_symlink_hardlink_permissions_and_git_are_rejected(self):
        alias = self.private / "alias"
        alias.symlink_to(self.key)
        with self.assertRaises(transport.TransportError):
            transport.load_key_file(alias, self.repo)
        alias.unlink()
        os.link(self.key, alias)
        with self.assertRaises(transport.TransportError):
            transport.load_key_file(self.key, self.repo)
        alias.unlink()
        self.key.chmod(0o640)
        with self.assertRaises(transport.TransportError):
            transport.load_key_file(self.key, self.repo)
        self.key.chmod(0o600)
        self.private.chmod(0o755)
        with self.assertRaises(transport.TransportError):
            transport.load_key_file(self.key, self.repo)
        in_git = self.repo / "credential"
        in_git.write_bytes(b"offline-fixture-token")
        in_git.chmod(0o600)
        with self.assertRaises(transport.TransportError):
            transport.load_key_file(in_git, self.repo)


class MetadataReceiptTests(unittest.TestCase):
    def normalize(self, source):
        return transport.normalize_key_metadata(source, transport.key_fingerprint("offline-fixture-token"),
                                                "2026-10-05T00:00:00Z")

    def test_key_metadata_binds_fingerprint_and_typed_exact_money(self):
        result = self.normalize(key_metadata())
        self.assertEqual(result["key_fingerprint"], hashlib.sha256(b"offline-fixture-token").hexdigest())
        self.assertEqual(result["key_fingerprint_sha256"], result["key_fingerprint"])
        self.assertEqual(result["http_status"], 200)
        self.assertEqual(result["usage_usd"], "0.25")
        self.assertEqual(result["remaining_usd"], "0.7")
        self.assertEqual(result["byok_usage_usd"], "0.05")
        self.assertIs(result["is_management_key"], False)
        self.assertEqual(result["response_sha256"], hashlib.sha256(key_metadata()["raw"]).hexdigest())

    def test_normalized_metadata_satisfies_existing_budget_gate(self):
        policy = json.loads((Path(__file__).resolve().parents[3] /
            "docs/research/model_research_2026-10-04/billing/new-programme-5eur.json").read_bytes())
        policy["usd_cap"] = "5"
        normalized = self.normalize(key_metadata(usage="0", byok_usage="0", limit="5", limit_remaining="5"))
        fingerprint = normalized["key_fingerprint_sha256"]
        evidence = {"key_metadata": normalized,
            "key_binding": {"programme_id": policy["programme_id"], "key_fingerprint_sha256": fingerprint,
                            "bound_to_loaded_credential": True,
                            "separate_new_key_owner_confirmation_ref": "offline-fixture-owner-confirmation"},
            "fx": {"base_currency": "EUR", "quote_currency": "USD", "usd_per_eur": "1.1",
                   "source_ref": "offline-fixture-fx", "checked_at": normalized["checked_at"]},
            "pricing": [{"model_id": "offline/tiny", "provider_id": "Offline", "currency": "USD",
                         "source_ref": "offline-fixture-price", "checked_at": normalized["checked_at"],
                         "raw_sha256": "b" * 64, "all_charge_components_accounted": True,
                         "component_prices_usd": {"prompt": "0.000001", "completion": "0.000002"}}],
            "stage": {"stage_id": "offline-stage", "manifest_sha256": "c" * 64, "reservation_usd": "0.01",
                      "model_provider_pairs": [{"model_id": "offline/tiny", "provider_id": "Offline",
                                               "units_upper_bounds": {"prompt": 100, "completion": 10}}]}}
        report = gate.evaluate(policy, evidence, deepcopy(policy["initial_ledger"]),
                               datetime(2026, 10, 5, tzinfo=timezone.utc))
        self.assertEqual(report["blockers"], [])
        self.assertEqual(report["status"], "ready_for_bound_transport_preflight")

    def test_missing_unknown_and_inconsistent_key_evidence_fails(self):
        invalid = [{"is_management_key": "false"}, {"include_byok_in_limit": 0},
                   {"is_provisioning_key": None}, {"limit_reset": "yearly"},
                   {"limit_reset": False}, {"limit_remaining": "0.8"},
                   {"usage": True}, {"byok_usage": "NaN"}, {"limit": None},
                   {"limit_remaining": "1.1"}]
        for changes in invalid:
            with self.subTest(changes=changes), self.assertRaises(transport.TransportError):
                self.normalize(key_metadata(**changes))
        source = key_metadata()
        source["transport_error"] = "response_read_failed"
        with self.assertRaises(transport.TransportError):
            self.normalize(source)
        source = key_metadata()
        source["http_status"] = 401
        with self.assertRaises(transport.TransportError):
            self.normalize(source)

    def test_nullable_limits_and_known_resets_are_normalized_without_policy(self):
        result = self.normalize(key_metadata(limit=None, limit_remaining=None, limit_reset="monthly"))
        self.assertIsNone(result["limit_usd"])
        self.assertEqual(result["limit_reset"], "monthly")

    def test_cost_retained_even_for_http_error_and_semantic_failure(self):
        receipt = transport.extract_receipt(response(chat(error={"message": "fixture"}), status=500), "chat")
        self.assertEqual(receipt["reported_cost_usd"], "0.0000123")
        self.assertEqual(receipt["input_tokens"], 20)
        self.assertEqual(receipt["output_tokens"], 4)
        self.assertIsNone(receipt["is_byok"])
        self.assertFalse(receipt["billing_verified"])

    def test_jev_token_fields_and_snapshot_aliases(self):
        value = chat(model="offline/jev-20261005", provider="Offline Jev",
                     usage={"cost": "0.0000123", "input_tokens": 30, "output_tokens": 6})
        operation = {"protocol": "jev", "api_type": "decisions", "model_id": "offline/jev",
                     "model_aliases": ["offline/jev-20261005"], "provider_id": "offline-jev",
                     "provider_aliases": ["Offline Jev"]}
        receipt = transport.extract_receipt(response(value), operation)
        verified = transport.verify_generation_receipt(generation(model="offline/jev-20261005",
            provider_name="Offline Jev", api_type="decisions"), receipt, operation)
        self.assertEqual(receipt["input_tokens"], 30)
        self.assertEqual(receipt["output_tokens"], 6)
        self.assertTrue(verified["billing_verified"])
        self.assertIs(verified["is_byok"], False)
        self.assertEqual(verified["actual_cost_usd"], "0.0000123")

    def test_generation_credit_proof_rejects_mismatch_or_unknown(self):
        receipt = transport.extract_receipt(response(chat()), "chat")
        invalid = [{"id": "gen-other"}, {"model": "offline/other"}, {"provider_name": "Other"},
                   {"api_type": "decisions"}, {"total_cost": "0.00001231"},
                   {"is_byok": True}, {"is_byok": None}, {"is_byok": 0}]
        for changes in invalid:
            with self.subTest(changes=changes), self.assertRaises(transport.TransportError):
                transport.verify_generation_receipt(generation(**changes), receipt)
        self.assertTrue(transport.verify_generation_receipt(generation(), receipt)["billing_verified"])
        byok = transport.extract_receipt(response(chat(usage={"cost": "0.0000123", "is_byok": True})), "chat")
        with self.assertRaises(transport.TransportError):
            transport.verify_generation_receipt(generation(), byok)
        unavailable = generation()
        unavailable["http_status"] = 404
        with self.assertRaises(transport.TransportError):
            transport.verify_generation_receipt(unavailable, receipt)

    def test_duplicate_json_and_nonfinite_cost_do_not_become_receipts(self):
        for raw in (b'{"usage":{"cost":0,"cost":1}}', b'{"usage":{"cost":NaN}}',
                    b'{"usage":{"cost":true}}'):
            with self.subTest(raw=raw), self.assertRaises(transport.TransportError):
                transport.extract_receipt(raw, "chat")


if __name__ == "__main__":
    unittest.main()
