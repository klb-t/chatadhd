"""Optional endpoint-policy integration: fabricated fixtures, no network."""
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import unittest

import research_programme_manifest as manifest
import research_programme_runner as runner
import test_research_programme_runner as fixtures

wire = fixtures.wire


class ProgrammeEndpointPricingTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.ProgrammeRunnerTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        policy_file = Path(__file__).resolve().parents[3] / "docs/research/model_research_2026-10-04/billing/endpoint-pricing-policy.json"
        self.fixture.policy["endpoint_pricing"] = json.loads(policy_file.read_bytes())
        self.endpoints = [
            {"model_id": "fake/model", "provider_name": "Fake", "tag": "fake",
             "pricing": {"prompt": "0.0001", "completion": "0.0002",
                         "overrides": [{"min_prompt_tokens": 100000, "prompt": "0.0002", "input_cache_write_1h": "0.0003"}],
                         "internal_reasoning": "0.0004", "image": "0.01"}},
            {"model_id": "fake/model", "provider_name": "Fake", "tag": "fake/priority",
             "pricing": {"prompt": "1", "completion": "1"}},
        ]
        self.base_send = self.fixture.send

        def send(method, route, body=None, params=None):
            response = self.base_send(method, route, body, params)
            if route == "model_endpoints":
                response["raw"] = wire({"data": {"id": "fake/model", "endpoints": self.endpoints}})
            return response

        self.fixture.send = send

    def operations(self):
        return manifest.load_operations(json.loads(self.fixture.manifest.read_bytes()), base_dir=self.fixture.repo)

    def quote(self, operation=None):
        return runner.endpoint_quote({"http_status": 200, "raw": wire({"data": {"id": "fake/model", "endpoints": self.endpoints}})},
                                     operation or self.operations()[0], runner.utc().isoformat(), self.fixture.policy)

    def add_changed_second(self, update):
        data = json.loads(self.fixture.manifest.read_bytes())
        body = {**json.loads(self.fixture.body), **update}
        raw = wire(body)
        (self.fixture.repo / "second.json").write_bytes(raw)
        second = {**data["operations"][0], "operation_id": "two", "request_file": "second.json",
                  "request_sha256": hashlib.sha256(raw).hexdigest()}
        data["operations"].append(second)
        self.fixture.manifest.write_bytes(wire(data))
        self.base_send.distinct_generations = True

    def test_source_envelope_and_all_component_bounds_reach_real_plan(self):
        quote = self.quote()
        reservations, pairs, expected = runner.price_plan(self.operations(), [quote], self.fixture.policy)
        self.assertEqual(quote["component_prices_usd"]["prompt"], "0.0002")
        self.assertEqual(quote["routing"]["admitted_tiers"], ["default"])
        self.assertEqual(quote["excluded_tier_endpoints"][0]["tag"], "fake/priority")
        self.assertEqual(pairs[0]["units_upper_bounds"]["input_cache_write_1h"], "200")
        self.assertEqual(pairs[0]["units_upper_bounds"]["internal_reasoning"], "10")
        self.assertEqual(pairs[0]["units_upper_bounds"]["image"], "0")
        self.assertEqual(reservations["one"], Decimal("0.1060"))
        self.assertEqual(expected["one"], reservations["one"])
        self.assertIn("overrides", quote["admitted_endpoints"][0]["pricing"])

    def test_final_accounting_validates_original_request_then_spends_zero(self):
        result = self.fixture.run_stage()
        self.assertEqual(result["status"], "completed")
        self.assertEqual(result["cumulative_actual_usd"], "0.01")
        self.assertEqual(len(self.base_send.posts()), 1)
        self.assertEqual(self.base_send.posts()[0][2], self.fixture.body)
        self.assertEqual(self.base_send.calls[-1][1], "key")

    def test_later_same_pair_tier_change_blocks_all_posts(self):
        for update in ({"service_tier": "priority"}, {"speed": "fast"}):
            with self.subTest(update=update):
                self.add_changed_second(update)
                with self.assertRaisesRegex(runner.ProgrammeError, "endpoint_policy_operation_or_quote_invalid"):
                    self.fixture.run_stage()
                self.assertEqual(self.base_send.posts(), [])
                # Restore the same bound stage for the next independent variant.
                data = json.loads(self.fixture.manifest.read_bytes())
                data["operations"] = data["operations"][:1]
                self.fixture.manifest.write_bytes(wire(data))
                # Failure admitted no attempt; use a fresh private directory.
                self.fixture.private = self.fixture.root / ("private-" + next(iter(update)))

    def test_later_plugin_body_cannot_reuse_zero_search_quote(self):
        self.add_changed_second({"plugins": [{"id": "web"}]})
        with self.assertRaisesRegex(runner.ProgrammeError, "endpoint_policy_operation_or_quote_invalid"):
            self.fixture.run_stage()
        self.assertEqual(self.base_send.posts(), [])

    def test_policy_and_routing_audit_are_bound_for_offline_plan(self):
        for field, replacement in (("policy_sha256", "0" * 64), ("routing", {"admitted_tiers": ["priority"]})):
            with self.subTest(field=field):
                quote = {**self.quote(), field: replacement}
                with self.assertRaisesRegex(runner.ProgrammeError, "endpoint_policy_operation_or_quote_invalid"):
                    runner.price_plan(self.operations(), [quote], self.fixture.policy)

    def test_multiple_responses_streaming_and_second_output_maximum_block_preflight(self):
        quote = self.quote()
        for update in ({"n": 2}, {"stream": True}, {"max_completion_tokens": 1000}):
            with self.subTest(update=update):
                operation = self.operations()[0]
                raw = wire({**operation["request_body"], **update})
                operation.update(request_bytes=raw, request_body=json.loads(raw), request_sha256=hashlib.sha256(raw).hexdigest())
                operation["units_upper_bounds"]["prompt"] = str(len(raw) + 100)
                with self.assertRaisesRegex(runner.ProgrammeError, "endpoint_policy_operation_or_quote_invalid"):
                    runner.price_plan([operation], [quote], self.fixture.policy)

    def test_completed_only_stop_resolution_keeps_cap_checks_without_new_reservation(self):
        self.fixture.policy["usd_cap"] = "5"
        self.base_send.post_usage_mismatch = True
        result = self.fixture.run_stage()
        self.assertEqual(result["status"], "stopped")
        self.base_send.post_usage_mismatch = False
        self.base_send.calls.clear()
        result = self.fixture.reconcile()
        self.assertEqual(result["status"], "stop_resolved_read_only")
        self.assertEqual(self.base_send.posts(), [])
        proof = json.loads(next(self.fixture.private.glob("*.resolution.json")).read_bytes())
        self.assertEqual(proof["accounting"]["next_stage_reservation_usd"], "0")
        self.assertEqual(proof["accounting"]["declared_price_projection_usd"], "0.0000")

    def test_no_paid_dispatch_does_not_skip_real_request_validation(self):
        operations = self.operations()
        quote = self.quote()
        operations[0]["units_upper_bounds"]["completion"] = "0"
        with self.assertRaisesRegex(runner.ProgrammeError, "endpoint_policy_operation_or_quote_invalid"):
            runner.stage_evidence(self.fixture.policy, {"stage_id": "stage-one"}, "a" * 64, operations,
                                  {**self.fixture.evidence, "pricing": [quote]}, {}, "b" * 64, [], no_paid_dispatch=True)


if __name__ == "__main__":
    unittest.main()
