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
    from ._test_support import private_temporary_directory
except ImportError:
    import openrouter_runner as runner
    from _test_support import private_temporary_directory


def manifest(count=1):
    body = {"model": "test/tiny-v1", "stream": False, "temperature": 0, "max_tokens": 64,
            "messages": [{"role": "user", "content": "Jeżeli pada, ziemia jest mokra."}],
            "response_format": {"type": "json_object"}, "usage": {"include": True},
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
            "usage": {"prompt_tokens": 24, "completion_tokens": 6, "cost": 0.000036, "is_byok": False}}
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
        self.tmp = private_temporary_directory()
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

    def test_key_gate_diagnostics_distinguish_missing_invalid_and_privileged_flags(self):
        cases = [({"limit_reset": "daily"}, "key_reset_enabled"),
                 ({"limit_reset": "untrusted-remote-text"}, "key_reset_value_invalid"),
                 ({"is_management_key": None}, "key_management_flag_invalid"),
                 ({"is_management_key": "false"}, "key_management_flag_invalid"),
                 ({"is_management_key": True}, "management_key_forbidden"),
                 ({"is_provisioning_key": None}, "key_provisioning_flag_invalid"),
                 ({"is_provisioning_key": True}, "provisioning_key_forbidden")]
        for change, expected in cases:
            with self.subTest(change=change):
                result = runner.inspect_key(manifest(), transport_fn=MockHTTP(key=key_info(**change)),
                                            key_loader=lambda: "fixture-token")
                self.assertFalse(result["gate_valid"])
                self.assertEqual(result["reason"], expected)
        data = runner.parse_json(key_info())
        del data["data"]["is_management_key"]
        with self.assertRaisesRegex(runner.RunnerError, "key_management_flag_missing"):
            runner._key_gate(runner.canonical(data), runner.Decimal(1), runner.Decimal("0.01"))

    def test_failed_key_gate_persists_only_whitelisted_sanitized_metadata_no_post(self):
        raw = key_info(limit_reset="fixture-token", is_management_key="private-owner",
                       is_provisioning_key={"secret": "fixture-token"},
                       include_byok_in_limit=["private-label"], label="private-label",
                       hash="private-key-hash", creator="private-owner", headers="fixture-token")
        http = MockHTTP(key=raw)
        with self.assertRaisesRegex(runner.RunnerError, "key_reset_value_invalid"):
            self.run_mock(http=http)
        ledger = runner.parse_json((self.directory / "ledger.json").read_bytes())
        self.assertEqual(ledger["attempts"], [])
        self.assertNotIn("stopped_reason", ledger)
        self.assertEqual([call[:2] for call in http.calls], [("GET", "/key")])
        metadata = ledger["key_check_failure"]["metadata"]
        self.assertEqual(metadata, {"limit": "1", "limit_remaining": "1", "limit_reset": "invalid",
                         "is_management_key": "invalid", "is_provisioning_key": "invalid",
                         "include_byok_in_limit": "invalid", "byok_usage": "missing"})
        for path in self.directory.iterdir():
            for prohibited in (b"fixture-token", b"private-label", b"private-owner", b"private-key-hash"):
                self.assertNotIn(prohibited, path.read_bytes())

    def test_credits_only_key_with_zero_byok_history_runs_and_records_tripwire(self):
        http = MockHTTP(key=key_info(include_byok_in_limit=False, byok_usage=0))
        result = self.run_mock(manifest(2), http)
        self.assertEqual(len(result["attempts"]), 2)
        self.assertEqual(result["key_check"]["billing_mode"], "credits_with_byok_tripwire")
        self.assertFalse(result["key_check"]["includes_byok"])
        self.assertTrue(all(r["reported_is_byok"] is False for r in result["attempts"]))
        self.assertNotIn("stopped_reason", result)

    def test_credits_only_key_rejects_nonzero_invalid_or_missing_byok_history_before_post(self):
        for value in (1, "0.000001", None, False, "private-remote-label"):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as folder:
                http = MockHTTP(key=key_info(include_byok_in_limit=False, byok_usage=value))
                with self.assertRaises(runner.RunnerError):
                    runner.run_manifest(manifest(), folder, transport_fn=http, key_loader=lambda: "fixture-token")
                self.assertEqual([c[0] for c in http.calls], ["GET"])
                saved = runner.parse_json((Path(folder) / "ledger.json").read_bytes())
                self.assertEqual(saved["attempts"], [])
                self.assertNotIn(b"private-remote-label", runner.canonical(saved))

    def test_tripwire_requires_accounting_request_without_silently_mutating_prompt(self):
        m = manifest()
        del m["requests"][0]["body"]["usage"]
        http = MockHTTP(key=key_info(include_byok_in_limit=False, byok_usage=0))
        with self.assertRaisesRegex(runner.RunnerError, "byok_tripwire_requires_usage_accounting"):
            self.run_mock(m, http)
        self.assertEqual([c[0] for c in http.calls], ["GET"])
        for value in ({"include": False}, {"include": 1}, {"include": True, "extra": True}, True):
            with self.subTest(value=value):
                m["requests"][0]["body"]["usage"] = value
                with self.assertRaisesRegex(runner.RunnerError, "usage_accounting_must_be_enabled"):
                    runner.plan_manifest(m)

    def test_byok_and_unclear_billing_stop_after_one_and_remain_stopped_on_resume(self):
        cases = [(completion(usage={"cost": 0.001, "is_byok": True}), "uncapped_byok_detected_stop"),
                 (completion(usage={"cost": 0.001}), "byok_billing_unknown_stop"),
                 (completion(usage={"cost": 0.001, "is_byok": "false"}), "byok_billing_unknown_stop"),
                 (completion(usage={"is_byok": False}), "byok_billing_unknown_stop"),
                 (completion(usage={"cost": "invalid", "is_byok": False}), "byok_billing_unknown_stop"),
                 (b"invalid-json", "byok_billing_unknown_stop")]
        for raw, reason in cases:
            with self.subTest(reason=reason, raw=raw), tempfile.TemporaryDirectory() as folder:
                m = manifest(2)
                http = MockHTTP([(200, raw)], key=key_info(include_byok_in_limit=False, byok_usage=0))
                first = runner.run_manifest(m, folder, transport_fn=http, key_loader=lambda: "fixture-token")
                self.assertEqual(first["stopped_reason"], reason)
                self.assertEqual([c[0] for c in http.calls], ["GET", "POST"])
                self.assertEqual((Path(folder) / "case-0.response.bin").read_bytes(), raw)
                def forbidden(*args):
                    self.fail("Unknown or BYOK billing must stay stopped on resume")
                self.assertEqual(runner.run_manifest(m, folder, transport_fn=forbidden, key_loader=forbidden), first)

    def test_billing_preserved_for_truncated_error_and_http_error_responses(self):
        truncated = completion(choices=[{"finish_reason": "length", "message": {"content": "{"}}],
                               usage={"cost": 0.02, "is_byok": False})
        result = self.run_mock(manifest(2), MockHTTP([(200, truncated)]))
        self.assertEqual(result["attempts"][0]["state"], "rejected")
        self.assertEqual(result["attempts"][0]["reported_cost_usd"], "0.02")
        self.assertEqual(result["stopped_reason"], "reported_cost_exceeded_reservation")
        for status in (200, 502):
            with self.subTest(status=status), tempfile.TemporaryDirectory() as folder:
                raw = runner.canonical({"error": {"code": 502}, "usage": {"cost": 0.001, "is_byok": True}})
                http = MockHTTP([(status, raw)], key=key_info(include_byok_in_limit=False, byok_usage=0))
                result = runner.run_manifest(manifest(2), folder, transport_fn=http, key_loader=lambda: "fixture-token")
                self.assertEqual(result["attempts"][0]["reported_cost_usd"], "0.001")
                self.assertEqual(result["stopped_reason"], "uncapped_byok_detected_stop")
                self.assertEqual(len(result["attempts"]), 1)

    def test_ordinary_upstream_cost_is_not_misclassified_as_byok(self):
        raw = completion(usage={"cost": 0.001, "is_byok": False,
                                "cost_details": {"upstream_inference_cost": 0.0012}})
        result = self.run_mock(manifest(2), MockHTTP([(200, raw)],
                               key=key_info(include_byok_in_limit=False, byok_usage=0)))
        self.assertEqual(len(result["attempts"]), 2)
        self.assertNotIn("stopped_reason", result)

    def test_tripwire_uncertain_and_interrupted_attempts_never_advance_on_resume(self):
        for failure in (TimeoutError(), KeyboardInterrupt()):
            with self.subTest(failure=type(failure).__name__), tempfile.TemporaryDirectory() as folder:
                m = manifest(2)
                http = MockHTTP([failure], key=key_info(include_byok_in_limit=False, byok_usage=0))
                if isinstance(failure, KeyboardInterrupt):
                    with self.assertRaises(KeyboardInterrupt):
                        runner.run_manifest(m, folder, transport_fn=http, key_loader=lambda: "fixture-token")
                else:
                    runner.run_manifest(m, folder, transport_fn=http, key_loader=lambda: "fixture-token")
                def forbidden(*args):
                    self.fail("Unknown billing cannot advance after interruption")
                result = runner.run_manifest(m, folder, transport_fn=forbidden, key_loader=forbidden)
                self.assertEqual(result["stopped_reason"], "byok_billing_unknown_stop")
                self.assertEqual(len(result["attempts"]), 1)
                self.assertEqual(result["attempts"][0]["state"], "uncertain")
    def test_inspect_key_get_only_sanitizes_missing_and_null_without_artifacts(self):
        data = runner.parse_json(key_info())
        del data["data"]["is_management_key"]
        del data["data"]["limit_reset"]
        data["data"]["include_byok_in_limit"] = None
        http = MockHTTP(key=runner.canonical(data))
        result = runner.inspect_key(manifest(), transport_fn=http, key_loader=lambda: "fixture-token")
        self.assertEqual(result["reason"], "key_management_flag_missing")
        self.assertEqual(result["metadata"]["is_management_key"], "missing")
        self.assertEqual(result["metadata"]["is_provisioning_key"], "missing")
        self.assertEqual(result["metadata"]["limit_reset"], "missing")
        self.assertIsNone(result["metadata"]["include_byok_in_limit"])
        self.assertEqual(result["inference_requests"], 0)
        self.assertEqual([call[:2] for call in http.calls], [("GET", "/key")])
        self.assertFalse(self.directory.exists())
        valid = runner.inspect_key(manifest(), transport_fn=MockHTTP(), key_loader=lambda: "fixture-token")
        self.assertTrue(valid["gate_valid"])

    def test_key_diagnostics_do_not_copy_http_or_transport_error_text(self):
        for send, expected in ((lambda *args: (403, b"private-header fixture-token"), "key_metadata_http_error"),
                               (lambda *args: (_ for _ in ()).throw(RuntimeError("fixture-token")),
                                "key_metadata_unavailable")):
            with self.subTest(expected=expected):
                result = runner.inspect_key(manifest(), transport_fn=send, key_loader=lambda: "fixture-token")
                self.assertEqual(result["reason"], expected)
                self.assertNotIn("metadata", result)
                self.assertNotIn(b"fixture-token", runner.canonical(result))

    def test_inspect_key_cli_exit_code_and_json_for_refused_guard(self):
        from contextlib import redirect_stdout
        from io import StringIO
        source = Path(self.tmp.name) / "manifest.json"
        source.write_bytes(runner.canonical(manifest()))
        for valid, expected in ((True, 0), (False, 2)):
            output = StringIO()
            with patch.object(runner, "inspect_key", return_value={"gate_valid": valid}), redirect_stdout(output):
                code = runner.main(["inspect-key", str(source)])
            self.assertEqual(code, expected)
            self.assertEqual(json.loads(output.getvalue()), {"gate_valid": valid})

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
