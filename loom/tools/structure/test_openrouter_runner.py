"""Mocked transport/state tests. No credentials or real model requests."""
from copy import deepcopy
from datetime import datetime, timedelta, timezone
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

try:
    from . import openrouter_runner as runner
except ImportError:
    import openrouter_runner as runner


def manifest(count=1):
    body = {"model": "test/tiny-v1", "stream": False, "temperature": 0, "max_tokens": 64,
            "messages": [{"role": "user", "content": "Jeżeli pada, ziemia jest mokra."}],
            "response_format": {"type": "json_object"},
            "provider": {"only": ["test/fp8"], "allow_fallbacks": False,
                         "require_parameters": True, "max_price": {"prompt": 1, "completion": 2}}}
    return {"schema": runner.MANIFEST_SCHEMA, "experiment_id": "unit-pilot", "budget_usd": "1",
            "max_requests": count, "requests": [{"id": "case-" + str(i), "body": deepcopy(body),
                                                 "reservation_usd": "0.01", "metadata": {"case": i}}
                                                for i in range(count)],
            "pricing_evidence": [{"model": "test/tiny-v1", "provider": "test/fp8",
                                  "pricing": {"prompt": "0.000001", "completion": "0.000002", "discount": 0},
                                  "source_url": runner.API_ROOT + "/models/test/tiny-v1/endpoints",
                                  "retrieved_at": datetime.now(timezone.utc).isoformat()}]}


def key_info(**changes):
    data = {"limit": 1, "limit_remaining": 1, "limit_reset": None,
            "is_management_key": False, "include_byok_in_limit": True}
    data.update(changes)
    return runner.canonical({"data": data})


def completion(**changes):
    data = {"id": "remote-test-id", "model": "test/tiny-v1", "provider": "test/fp8",
            "choices": [{"finish_reason": "stop", "message": {"role": "assistant", "content": '{"claims": []}'}}],
            "usage": {"prompt_tokens": 24, "completion_tokens": 6, "cost": 0.000036}}
    data.update(changes)
    return runner.canonical(data)


class MockHTTP:
    def __init__(self, replies=(), key=None, before_post=None):
        self.calls = []
        self.replies = list(replies)
        self.key = key if key is not None else key_info()
        self.before_post = before_post

    def __call__(self, method, path, body, key):
        self.calls.append((method, path, body))
        if method == "GET":
            return 200, self.key
        if self.before_post:
            self.before_post()
        reply = self.replies.pop(0) if self.replies else (200, completion())
        if isinstance(reply, BaseException):
            raise reply
        return reply


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.directory = Path(self.tmp.name) / "run"
        self.addCleanup(self.tmp.cleanup)

    def run_mock(self, m=None, http=None):
        return runner.run_manifest(m or manifest(), self.directory,
                                   transport_fn=http or MockHTTP(), key_loader=lambda: "fixture-token")

    def test_plan_has_utf8_allowance_and_exact_hashes(self):
        m = manifest(2)
        plan = runner.plan_manifest(m)
        self.assertEqual(plan["total_reservation_usd"], "0.02")
        self.assertEqual(plan["manifest_hash"], runner.digest(m))
        body = m["requests"][0]["body"]
        self.assertEqual(plan["requests"][0]["input_token_allowance"], len(runner.canonical(body)) + 1056)
        self.assertFalse(plan["billing_bound_guaranteed"])

    def test_rejects_under_reservation_and_aggregate_over_budget(self):
        for mutate in (lambda m: m["requests"][0].update(reservation_usd="0.0000001"),
                       lambda m: m.update(budget_usd="0.009")):
            m = manifest()
            mutate(m)
            with self.assertRaises(runner.RunnerError):
                runner.plan_manifest(m)

    def test_rejects_tools_plugins_models_streams_and_multimodal(self):
        for field, value in (("tools", []), ("plugins", []), ("models", ["test/tiny-v2"]),
                             ("stream", True), ("model", "test/tiny-v1:nitro"),
                             ("messages", [{"role": "user", "content": [{"type": "image_url"}]}])):
            with self.subTest(field=field):
                m = manifest()
                m["requests"][0]["body"][field] = value
                with self.assertRaises(runner.RunnerError):
                    runner.plan_manifest(m)

    def test_pinned_provider_and_supported_parameters_are_required(self):
        for field, value in (("allow_fallbacks", True), ("require_parameters", False),
                             ("only", ["test/fp8", "other"]), ("only", ["other"]),
                             ("max_price", {"prompt": 0.1, "completion": 2})):
            m = manifest()
            m["requests"][0]["body"]["provider"][field] = value
            with self.assertRaises(runner.RunnerError):
                runner.plan_manifest(m)

    def test_catalog_retained_inactive_search_cache_discount_and_unknown_charge(self):
        m = manifest()
        prices = m["pricing_evidence"][0]["pricing"]
        prices.update(web_search="0.01", input_cache_read="0.0000005", discount="0.2")
        runner.plan_manifest(m)
        for name in ("new_charge", "input_cache_write", "request", "internal_reasoning"):
            changed = deepcopy(m)
            changed["pricing_evidence"][0]["pricing"][name] = "0.1"
            with self.subTest(name=name), self.assertRaises(runner.RunnerError):
                runner.plan_manifest(changed)

    def test_no_body_or_secret_arguments_are_logged(self):
        http = MockHTTP([RuntimeError("secret fixture-token token in remote exception")])
        result = self.run_mock(http=http)
        self.assertEqual(result["attempts"][0]["state"], "uncertain")
        for path in self.directory.iterdir():
            self.assertNotIn(b"fixture-token", path.read_bytes())

    def test_writeahead_exists_before_post_and_response_is_exact(self):
        def before():
            ledger = runner.parse_json((self.directory / "ledger.json").read_bytes())
            self.assertEqual(ledger["attempts"][0]["state"], "started")
            self.assertEqual(ledger["attempts"][0]["reservation_usd"], "0.01")
        raw = completion()
        result = self.run_mock(http=MockHTTP([(200, raw)], before_post=before))
        self.assertEqual(result["attempts"][0]["state"], "completed")
        self.assertEqual((self.directory / "case-0.response.bin").read_bytes(), raw)
        self.assertEqual(result["attempts"][0]["reported_cost_usd"], "0.000036")

    def test_completed_resume_requires_no_key_and_never_retries(self):
        m = manifest()
        first = self.run_mock(m)
        def forbidden(*args):
            self.fail("Completed resume must not call network or load a credential")
        second = runner.run_manifest(m, self.directory, transport_fn=forbidden, key_loader=forbidden)
        self.assertEqual(first, second)

    def test_timeout_reservation_retained_and_resume_runs_only_untouched(self):
        m = manifest(2)
        first_http = MockHTTP([TimeoutError("simulated")])
        first = self.run_mock(m, first_http)
        self.assertEqual(len(first["attempts"]), 1)
        self.assertEqual(first["attempts"][0]["state"], "uncertain")
        self.assertEqual(first["attempts"][0]["reservation_usd"], "0.01")
        next_http = MockHTTP()
        second = self.run_mock(m, next_http)
        self.assertEqual([r["state"] for r in second["attempts"]], ["uncertain", "completed"])
        self.assertEqual(sum(c[0] == "POST" for c in next_http.calls), 1)

    def test_process_interrupt_is_uncertain_on_resume_not_retried(self):
        m = manifest(2)
        with self.assertRaises(KeyboardInterrupt):
            self.run_mock(m, MockHTTP([KeyboardInterrupt()]))
        ledger = runner.parse_json((self.directory / "ledger.json").read_bytes())
        self.assertEqual(ledger["attempts"][0]["state"], "started")
        http = MockHTTP()
        after = self.run_mock(m, http)
        self.assertEqual(after["attempts"][0]["state"], "uncertain")
        self.assertEqual(sum(c[0] == "POST" for c in http.calls), 1)

    def test_exact_resume_rejects_changed_manifest_before_network(self):
        m = manifest()
        self.run_mock(m)
        m["requests"][0]["body"]["messages"][0]["content"] += "!"
        http = MockHTTP()
        with self.assertRaisesRegex(runner.RunnerError, "ledger_manifest_mismatch"):
            self.run_mock(m, http)
        self.assertEqual(http.calls, [])

    def test_response_tampering_or_missing_file_rejected(self):
        m = manifest()
        self.run_mock(m)
        path = self.directory / "case-0.response.bin"
        path.write_bytes(b"tamper")
        with self.assertRaisesRegex(runner.RunnerError, "response_artifact_hash_mismatch"):
            self.run_mock(m)
        path.unlink()
        with self.assertRaisesRegex(runner.RunnerError, "response_artifact_missing"):
            self.run_mock(m)

    def test_failed_http_response_retained_without_retry(self):
        http = MockHTTP([(429, b'{"error":{"message":"rate limited"}}')])
        result = self.run_mock(http=http)
        self.assertEqual(result["attempts"][0]["state"], "http_error")
        self.assertEqual(len(http.calls), 2)
        self.assertTrue((self.directory / "case-0.response.bin").exists())

    def test_terminal_http_errors_stop_after_one_post_and_keep_partial_ledger_valid(self):
        for status in (401, 402, 403, 429):
            with self.subTest(status=status), tempfile.TemporaryDirectory() as folder:
                m = manifest(3)
                http = MockHTTP([(status, b'{"error":{"message":"terminal"}}')])
                result = runner.run_manifest(m, folder, transport_fn=http,
                                             key_loader=lambda: "fixture-token")
                self.assertEqual(sum(call[0] == "POST" for call in http.calls), 1)
                self.assertEqual(result["stopped_reason"], "terminal_http_" + str(status))
                self.assertEqual(len(result["attempts"]), 1)
                self.assertEqual(result["attempts"][0]["state"], "http_error")
                self.assertEqual(result["manifest_hash"], runner.digest(m))
                self.assertEqual(runner._validate_ledger(result, runner.plan_manifest(m), Path(folder)), result["attempts"])
                self.assertTrue((Path(folder) / result["attempts"][0]["response_file"]).exists())
                def forbidden(*args):
                    self.fail("Terminal HTTP stop must survive resume without network or key reads")
                self.assertEqual(runner.run_manifest(m, folder, transport_fn=forbidden,
                                                      key_loader=forbidden), result)

    def test_nonstop_refusal_invalidjson_and_providererrors_are_rejected(self):
        alternatives = [b"broken", runner.canonical({"error": {"code": 500}}),
                        completion(choices=[{"finish_reason": "length", "message": {"content": "{}"}}]),
                        completion(choices=[{"finish_reason": "stop", "message": {"content": "{}", "refusal": "no"}}])]
        for raw in alternatives:
            self.assertEqual(runner._response_result(raw)["state"], "rejected")

    def test_over_reservation_report_stops_and_cannot_resume_paid_requests(self):
        m = manifest(2)
        http = MockHTTP([(200, completion(usage={"cost": 0.02}))])
        first = self.run_mock(m, http)
        self.assertEqual(first["stopped_reason"], "reported_cost_exceeded_reservation")
        def forbidden(*args):
            self.fail("Stopped run must not resume automatically")
        second = runner.run_manifest(m, self.directory, transport_fn=forbidden, key_loader=forbidden)
        self.assertEqual(first, second)

    def test_strict_key_gate_rejects_unbounded_management_reset_byok_and_small_remaining(self):
        for change in ({"limit": None}, {"limit": 2}, {"limit_reset": "daily"},
                       {"is_management_key": True}, {"include_byok_in_limit": False},
                       {"limit_remaining": 0.001}, {"limit_remaining": True}):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as folder:
                http = MockHTTP(key=key_info(**change))
                with self.assertRaises(runner.RunnerError):
                    runner.run_manifest(manifest(), folder, transport_fn=http, key_loader=lambda: "fixture-token")
                self.assertEqual(len(http.calls), 1)

    def test_stale_evidence_fails_before_network(self):
        m = manifest()
        m["pricing_evidence"][0]["retrieved_at"] = (datetime.now(timezone.utc) - timedelta(days=2)).isoformat()
        http = MockHTTP()
        with self.assertRaisesRegex(runner.RunnerError, "pricing_evidence_stale"):
            self.run_mock(m, http)
        self.assertEqual(http.calls, [])

    def test_secret_in_manifest_or_response_never_written(self):
        m = manifest()
        m["metadata"] = {"accident": "fixture-token"}
        with self.assertRaisesRegex(runner.RunnerError, "credential_invalid_or_present_in_manifest"):
            self.run_mock(m)
        self.assertFalse((self.directory / "manifest.json").exists())
        result = self.run_mock(http=MockHTTP([(200, b'fixture-token')]))
        self.assertEqual(result["attempts"][0]["state"], "uncertain")
        self.assertFalse((self.directory / "case-0.response.bin").exists())

    def test_response_size_limit_and_lock(self):
        result = self.run_mock(http=MockHTTP([(200, b"a" * (runner.MAX_RESPONSE_BYTES + 1))]))
        self.assertEqual(result["attempts"][0]["state"], "uncertain")
        with runner._lock(self.directory), self.assertRaisesRegex(runner.RunnerError, "run_already_locked"):
            self.run_mock()

    def test_transport_rejects_arbitrary_host_route_without_request(self):
        with patch.object(runner.urllib.request, "build_opener") as opened:
            with self.assertRaisesRegex(runner.RunnerError, "invalid_transport_route"):
                runner.transport("POST", "https://other.invalid/", b"{}", "fixture-token")
            opened.assert_not_called()

    def test_redirect_is_denied(self):
        with self.assertRaisesRegex(runner.RunnerError, "redirect_refused"):
            runner._NoRedirect().redirect_request(None, None, 307, "moved", {}, "https://other.invalid")

    def test_whole_request_deadline_stops_trickling_transport(self):
        class SlowOpener:
            def open(self, request, timeout):
                time.sleep(0.2)
                raise AssertionError("deadline must fire first")
        with patch.object(runner, "TIMEOUT_SECONDS", 0.02), patch.object(runner.urllib.request, "build_opener", return_value=SlowOpener()):
            with self.assertRaisesRegex(runner.RunnerError, "request_deadline_exceeded"):
                runner.transport("GET", "/key", None, "fixture-token")

    def test_credential_file_private_external_and_exactly_one_source(self):
        secret = Path(self.tmp.name) / "api-key"
        secret.write_text("fixture-token")
        secret.chmod(0o600)
        with patch.dict(os.environ, {"OPENROUTER_API_KEY_FILE": str(secret)}, clear=True):
            self.assertEqual(runner.load_key(), "fixture-token")
            secret.chmod(0o644)
            with self.assertRaisesRegex(runner.RunnerError, "private_regular_file"):
                runner.load_key()
        with patch.dict(os.environ, {"OPENROUTER_API_KEY": "a", "OPENROUTER_API_KEY_FILE": str(secret)}, clear=True):
            with self.assertRaisesRegex(runner.RunnerError, "exactly_one_key_source"):
                runner.load_key()

    def test_nonfinite_duplicatejson_cycles_and_bool_money_rejected(self):
        for raw in (b'{"a":1,"a":2}', b'{"x":NaN}'):
            with self.assertRaises(runner.RunnerError):
                runner.parse_json(raw)
        for value in ("1e-100000000", "1" * 500):
            with self.assertRaises(runner.RunnerError):
                runner._money(value)
        m = manifest()
        m["budget_usd"] = True
        with self.assertRaises(runner.RunnerError):
            runner.plan_manifest(m)
        m = manifest()
        m["metadata"] = m
        with self.assertRaises(runner.RunnerError):
            runner.plan_manifest(m)


if __name__ == "__main__":
    unittest.main()
