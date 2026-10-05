from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

try:
    from . import endpoint_pricing_v1 as pricing
    from . import research_programme_manifest as manifest
    from . import openrouter_runner as wire
except ImportError:
    import endpoint_pricing_v1 as pricing
    import research_programme_manifest as manifest
    import openrouter_runner as wire


ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / "docs/research/model_research_2026-10-04/billing"


class EndpointPricingTests(unittest.TestCase):
    def setUp(self):
        self.policy = json.loads((DATA / "endpoint-pricing-policy.json").read_bytes())
        self.body = {"model": "test/model", "provider": {"only": ["test"], "allow_fallbacks": False},
                     "messages": [{"role": "user", "content": "Text"}], "max_tokens": 200}
        self.operation = self.operation_for(self.body)
        self.endpoint = {"model_id": "test/model", "provider_name": "Display label",
                         "tag": "test", "pricing": {"prompt": "0.01", "completion": "0.02"}}

    def operation_for(self, body):
        raw = wire.canonical(body)
        return {"model_id": body["model"], "provider_id": body["provider"]["only"][0],
                "request_bytes": raw, "request_body": deepcopy(body),
                "request_sha256": hashlib.sha256(raw).hexdigest(),
                "units_upper_bounds": {"prompt": len(raw) + 100, "completion": body["max_tokens"], "request": 1}}

    def raw(self, endpoints=None, model="test/model"):
        return wire.canonical({"data": {"id": model, "endpoints": endpoints or [self.endpoint]}})

    def resolve(self, endpoints=None, operation=None):
        return pricing.resolve_endpoint_pricing(self.raw(endpoints), operation or self.operation, self.policy)

    def test_actual_public_quotes_and_all_24_frozen_bodies(self):
        aliases = {"openai/gpt-6.1-sol": ("gpt61sol", "openai", 2),
                   "anthropic/claude-sonnet-5.5": ("sonnet55", "anthropic", 0),
                   "google/gemini-3.1-pro-preview": ("gemini31pro", "google-ai-studio", 2)}
        base = ROOT / "docs/research/model_research_2026-10-04/followup-frontier-reply/final-verified-lineage"
        counted = 0
        with tempfile.TemporaryDirectory() as temporary:
            for stage in ("stage3", "stage4"):
                prepared = manifest.adapt_prepared_manifest(base / stage / "manifest.json", Path(temporary) / stage,
                    programme_id="offline-quote-regression", stage_id=stage)
                for operation in manifest.load_operations(prepared, base_dir=Path(temporary) / stage):
                    slug, standard_tag, excluded_count = aliases[operation["model_id"]]
                    raw = (DATA / "endpoint-quotes-2026-10-05" / (slug + ".json")).read_bytes()
                    quote = pricing.resolve_endpoint_pricing(raw, operation, self.policy)
                    self.assertEqual([standard_tag], [row["tag"] for row in quote["admitted_endpoints"]])
                    self.assertEqual(excluded_count, len(quote["excluded_tier_endpoints"]))
                    self.assertTrue(quote["all_charge_components_accounted"])
                    if standard_tag == "openai":
                        self.assertEqual("0.000004", quote["component_prices_usd"]["prompt"])
                        self.assertEqual("0.000015", quote["component_prices_usd"]["completion"])
                    if standard_tag == "google-ai-studio":
                        self.assertEqual("0.000004", quote["component_prices_usd"]["prompt"])
                        self.assertEqual("0.000018", quote["component_prices_usd"]["completion"])
                        self.assertEqual(str(operation["units_upper_bounds"]["completion"]),
                                         quote["checked_operation_units_upper_bounds"]["internal_reasoning"])
                    counted += 1
        self.assertEqual(24, counted)

    def test_variants_regions_multiple_endpoints_take_component_envelope(self):
        region = deepcopy(self.endpoint)
        region.update(tag="test/us", provider_name="Other display name")
        region["pricing"] = {"prompt": "0.005", "completion": "0.04"}
        quote = self.resolve([self.endpoint, region])
        self.assertEqual({"prompt": "0.01", "completion": "0.04", "request": "0"}, quote["component_prices_usd"])
        self.assertEqual(2, len(quote["admitted_endpoints"]))

    def test_specific_region_slug_does_not_include_other_regions(self):
        self.body["provider"]["only"] = ["test/us"]
        self.endpoint["tag"] = "test/us"
        other = deepcopy(self.endpoint)
        other["tag"] = "test/eu"
        other["pricing"]["prompt"] = "1000"
        quote = self.resolve([self.endpoint, other], self.operation_for(self.body))
        self.assertEqual(["test/us"], [row["tag"] for row in quote["admitted_endpoints"]])

    def test_standard_excludes_tiers_even_if_same_display_label(self):
        tier = deepcopy(self.endpoint)
        tier["tag"] = "test/fast"
        tier["pricing"]["prompt"] = "10"
        quote = self.resolve([self.endpoint, tier])
        self.assertEqual("0.01", quote["component_prices_usd"]["prompt"])
        self.assertEqual("service_tier_not_requested", quote["excluded_tier_endpoints"][0]["reason"])

    def test_tier_optin_spelling_and_exact_slug(self):
        for field, value, slug in (("service_tier", "fast", "test"), ("speed", "fast", "test"),
                                  ("service_tier", "flex", "test"), (None, None, "test/priority")):
            with self.subTest(field=field, value=value, slug=slug):
                body = deepcopy(self.body)
                body["provider"]["only"] = [slug]
                if field:
                    body[field] = value
                tier = deepcopy(self.endpoint)
                tier["tag"] = "test/flex" if value == "flex" else "test/priority"
                quote = self.resolve([self.endpoint, tier], self.operation_for(body))
                self.assertEqual([tier["tag"]], [row["tag"] for row in quote["admitted_endpoints"]])

    def test_variant_nitro_admits_priority_plus_standard_not_flex(self):
        body = deepcopy(self.body)
        body["model"] += ":nitro"
        fast, flex = deepcopy(self.endpoint), deepcopy(self.endpoint)
        fast["tag"], flex["tag"] = "test/fast", "test/flex"
        quote = self.resolve([self.endpoint, fast, flex], self.operation_for(body))
        self.assertEqual(["test", "test/fast"], [row["tag"] for row in quote["admitted_endpoints"]])

    def test_conflicting_or_unknown_tier_never_silently_routes_standard(self):
        for update in ({"service_tier": "unknown"}, {"service_tier": "flex", "speed": "fast"}):
            body = {**self.body, **update}
            with self.assertRaises(pricing.PricingError):
                self.resolve(operation=self.operation_for(body))

    def test_all_overrides_retained_including_unreachable_threshold_time(self):
        overrides = [{"min_prompt_tokens": 200000, "prompt": "0.04", "completion": "0.01"},
                     {"utc_start": 1630, "utc_end": 30, "utc_days": ["friday"], "completion": "0.08"}]
        self.endpoint["pricing"]["overrides"] = deepcopy(overrides)
        quote = self.resolve()
        self.assertEqual("0.04", quote["component_prices_usd"]["prompt"])
        self.assertEqual("0.08", quote["component_prices_usd"]["completion"])
        self.assertEqual(overrides, quote["admitted_endpoints"][0]["pricing"]["overrides"])

    def test_override_component_absent_from_base_is_bounded(self):
        self.endpoint["pricing"]["overrides"] = [{"min_prompt_tokens": 200000, "input_cache_write_1h": "0.07"}]
        quote = self.resolve()
        self.assertEqual(str(self.operation["units_upper_bounds"]["prompt"]), quote["checked_operation_units_upper_bounds"]["input_cache_write_1h"])

    def test_unknown_charge_condition_or_nested_field_is_not_ignored(self):
        for extra in ({"new_charge": "0.1"}, {"overrides": [{"new_condition": 100, "prompt": "0.1"}]},
                      {"overrides": [{"nested": {"prompt": "10"}}]}):
            with self.subTest(extra=extra):
                endpoint = deepcopy(self.endpoint)
                endpoint["pricing"].update(extra)
                with self.assertRaises(pricing.PricingError):
                    self.resolve([endpoint])

    def test_invalid_price_and_boolean_condition_fail(self):
        for value in (True, "NaN", "Infinity", "-1", []):
            endpoint = deepcopy(self.endpoint)
            endpoint["pricing"]["prompt"] = value
            with self.assertRaises(pricing.PricingError):
                self.resolve([endpoint])
        self.endpoint["pricing"]["overrides"] = [{"min_prompt_tokens": True, "prompt": "0.1"}]
        with self.assertRaises(pricing.PricingError):
            self.resolve()

    def test_fractional_discount_ignored_but_invalid_discount_rejected(self):
        self.endpoint["pricing"]["discount"] = "0.9"
        self.assertEqual("0.01", self.resolve()["component_prices_usd"]["prompt"])
        self.endpoint["pricing"]["discount"] = "1.1"
        with self.assertRaises(pricing.PricingError):
            self.resolve()

    def test_body_and_model_provider_binding_and_exact_bytes(self):
        for name, value in (("model_id", "other/model"), ("provider_id", "other"), ("request_sha256", "0" * 64)):
            operation = {**self.operation, name: value}
            with self.assertRaises(pricing.PricingError):
                self.resolve(operation=operation)
        operation = deepcopy(self.operation)
        operation["request_body"]["messages"][0]["content"] = "Later edited body"
        with self.assertRaises(pricing.PricingError):
            self.resolve(operation=operation)

    def test_provider_prefix_boundary_and_missing_endpoint_identity(self):
        endpoint = deepcopy(self.endpoint)
        endpoint["tag"] = "test-other"
        with self.assertRaises(pricing.PricingError):
            self.resolve([endpoint])
        endpoint.pop("tag")
        with self.assertRaises(pricing.PricingError):
            self.resolve([endpoint])

    def test_zero_defaults_cannot_hide_media_tools_plugins_or_unknown_knobs(self):
        for update in ({"plugins": [{"id": "web"}]}, {"tools": [{"type": "web_search"}]},
                       {"modalities": ["audio"]}, {"n": 2}, {"future_paid_knob": True},
                       {"messages": [{"role": "user", "content": [{"type": "image_url", "image_url": {"url": "https://example.test/i.png"}}]}]}):
            with self.subTest(update=update):
                body = {**self.body, **update}
                operation = self.operation_for(body)
                self.endpoint["pricing"].update(image="0.5", web_search="0.01")
                with self.assertRaises(pricing.PricingError):
                    self.resolve(operation=operation)

    def test_explicit_zero_also_needs_profile_even_when_general_profile_disabled(self):
        policy = deepcopy(self.policy)
        policy["operation_profile"] = None
        body = {**self.body, "plugins": [{"id": "web"}]}
        operation = self.operation_for(body)
        operation["units_upper_bounds"]["web_search"] = 0
        self.endpoint["pricing"]["web_search"] = "0.01"
        with self.assertRaises(pricing.PricingError):
            pricing.resolve_endpoint_pricing(self.raw(), operation, policy)

    def test_later_operation_sharing_quote_is_validated_separately(self):
        self.endpoint["pricing"]["web_search"] = "0.01"
        quote = self.resolve()
        later = self.operation_for({**self.body, "plugins": [{"id": "web"}]})
        with self.assertRaises(pricing.PricingError):
            pricing.resolve_units_upper_bounds(later, quote["component_prices_usd"], self.policy)

    def test_later_same_pair_service_tier_cannot_reuse_standard_quote(self):
        quote = self.resolve()
        self.assertEqual(quote["routing"], pricing.resolve_operation_routing(self.operation, self.policy))
        for update in ({"service_tier": "priority"}, {"service_tier": "fast"}, {"speed": "fast"}):
            with self.subTest(update=update):
                later = self.operation_for({**self.body, **update})
                self.assertEqual(self.operation["model_id"], later["model_id"])
                self.assertEqual(self.operation["provider_id"], later["provider_id"])
                # Quantity/body checks alone succeed: routing must also be bound.
                pricing.resolve_units_upper_bounds(later, quote["component_prices_usd"], self.policy)
                audit = pricing.resolve_operation_routing(later, self.policy)
                self.assertNotEqual(quote["routing"], audit)
                self.assertEqual(["priority"], audit["admitted_tiers"])

    def test_reasoning_cap_cannot_be_inferred_from_low_effort(self):
        self.endpoint["pricing"]["internal_reasoning"] = "0.02"
        operation = deepcopy(self.operation)
        operation["units_upper_bounds"]["internal_reasoning"] = 20
        with self.assertRaises(pricing.PricingError):
            self.resolve(operation=operation)
        quote = self.resolve()
        self.assertEqual("200", quote["checked_operation_units_upper_bounds"]["internal_reasoning"])

    def test_second_output_limit_and_missing_component_cannot_underreserve(self):
        operation = self.operation_for({**self.body, "max_completion_tokens": 500})
        with self.assertRaises(pricing.PricingError):
            self.resolve(operation=operation)
        policy = deepcopy(self.policy)
        policy["unit_bounds_defaults"].pop("input_cache_write_1h")
        self.endpoint["pricing"]["input_cache_write_1h"] = "0.03"
        with self.assertRaises(pricing.PricingError):
            pricing.resolve_endpoint_pricing(self.raw(), self.operation, policy)

    def test_explicit_request_zero_cannot_evade_positive_flat_price(self):
        self.endpoint["pricing"]["request"] = "0.01"
        operation = deepcopy(self.operation)
        operation["units_upper_bounds"]["request"] = 0
        with self.assertRaisesRegex(pricing.PricingError, "request_quantity_exceeds_bound"):
            self.resolve(operation=operation)
        self.assertEqual("1", self.resolve()["checked_operation_units_upper_bounds"]["request"])

    def test_explicit_cache_bounds_cover_full_prompt_quantity(self):
        for name in ("input_cache_read", "input_cache_write", "input_cache_write_1h"):
            with self.subTest(component=name):
                endpoint = deepcopy(self.endpoint)
                endpoint["pricing"][name] = "0.02"
                operation = deepcopy(self.operation)
                operation["units_upper_bounds"][name] = operation["units_upper_bounds"]["prompt"] - 1
                with self.assertRaisesRegex(pricing.PricingError, "request_quantity_exceeds_bound"):
                    self.resolve([endpoint], operation)
                self.assertEqual(str(self.operation["units_upper_bounds"]["prompt"]),
                                 self.resolve([endpoint])["checked_operation_units_upper_bounds"][name])

    def test_explicit_reasoning_covers_declared_output_allowance(self):
        self.endpoint["pricing"]["internal_reasoning"] = "0.02"
        operation = deepcopy(self.operation)
        operation["units_upper_bounds"]["completion"] = 400
        # Meets wire max_tokens=200 but understates the declared output bound.
        operation["units_upper_bounds"]["internal_reasoning"] = 200
        with self.assertRaisesRegex(pricing.PricingError, "request_quantity_exceeds_bound"):
            self.resolve(operation=operation)
        operation["units_upper_bounds"].pop("internal_reasoning")
        self.assertEqual("400", self.resolve(operation=operation)["checked_operation_units_upper_bounds"]["internal_reasoning"])

    def test_minimum_value_is_caller_owned_data(self):
        policy = deepcopy(self.policy)
        policy["units_bound_verifiers"]["request"]["minimum_value"] = "2"
        with self.assertRaisesRegex(pricing.PricingError, "request_quantity_exceeds_bound"):
            pricing.resolve_endpoint_pricing(self.raw(), self.operation, policy)
        operation = deepcopy(self.operation)
        operation["units_upper_bounds"]["request"] = 2
        self.assertEqual("2", pricing.resolve_endpoint_pricing(self.raw(), operation, policy)["checked_operation_units_upper_bounds"]["request"])

    def test_unknown_minimum_bound_reference_is_not_ignored(self):
        policy = deepcopy(self.policy)
        policy["units_bound_verifiers"]["input_cache_read"]["minimum_bound"] = "missing_quantity"
        self.endpoint["pricing"]["input_cache_read"] = "0.01"
        with self.assertRaisesRegex(pricing.PricingError, "required_request_bound_missing"):
            pricing.resolve_endpoint_pricing(self.raw(), self.operation, policy)

    def test_caller_owned_data_can_add_component_without_code_change(self):
        policy = deepcopy(self.policy)
        policy["pricing"]["charge_components"].append("new_charge")
        policy["unit_bounds_defaults"]["new_charge"] = {"value": "2"}
        self.endpoint["pricing"]["new_charge"] = "0.03"
        quote = pricing.resolve_endpoint_pricing(self.raw(), self.operation, policy)
        self.assertEqual("0.03", quote["component_prices_usd"]["new_charge"])
        self.assertEqual("2", quote["checked_operation_units_upper_bounds"]["new_charge"])

    def test_immutable_result_and_raw_policy_hash_bindings(self):
        raw, before = self.raw(), deepcopy(self.policy)
        quote = pricing.resolve_endpoint_pricing(raw, self.operation, self.policy)
        self.assertEqual(hashlib.sha256(raw).hexdigest(), quote["raw_sha256"])
        self.assertEqual(hashlib.sha256(wire.canonical(self.policy)).hexdigest(), quote["policy_sha256"])
        quote["bound_rationale"]["rates"] = "changed"
        self.assertEqual(before, self.policy)
        quote["admitted_endpoints"][0]["pricing"]["prompt"] = "changed"
        self.assertEqual("0.01", self.endpoint["pricing"]["prompt"])


if __name__ == "__main__":
    unittest.main()
