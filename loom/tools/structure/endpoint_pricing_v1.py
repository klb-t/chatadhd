"""Pure, caller-policy endpoint selection and conservative price envelopes.

No network calls, credentials, model lists or domain price rules. The caller
supplies exact endpoint bytes, an already-bound operation and versioned policy.
Every published price component needs a unit bound; zero defaults need a body
schema witness. Envelope rates include every conditional schedule, even when
its conditions may not match this particular request. These are reservations,
not a prediction of which upstream endpoint or price schedule will be served.
"""
from __future__ import annotations

from decimal import Decimal, InvalidOperation
import hashlib

try:
    from . import openrouter_runner as wire
except ImportError:
    import openrouter_runner as wire


class PricingError(ValueError):
    pass


def _amount(value):
    if type(value) not in (str, int, float):
        raise PricingError("invalid_price_or_unit")
    try:
        result = Decimal(str(value))
    except InvalidOperation:
        raise PricingError("invalid_price_or_unit") from None
    if not result.is_finite() or result < 0:
        raise PricingError("invalid_price_or_unit")
    return result


def _snapshot(value):
    return wire.parse_json(wire.canonical(value))


def _body(operation):
    if "request_bytes" in operation:
        raw = operation["request_bytes"]
        if not isinstance(raw, bytes):
            raise PricingError("request_snapshot_required")
        body = wire.parse_json(raw)
        if "request_body" in operation and body != operation["request_body"]:
            raise PricingError("request_snapshot_mismatch")
        expected = operation.get("request_sha256")
        if expected is not None and hashlib.sha256(raw).hexdigest() != expected:
            raise PricingError("request_snapshot_mismatch")
        return body
    body = operation.get("request_body")
    if not isinstance(body, dict):
        raise PricingError("request_snapshot_required")
    return _snapshot(body)


def _schema(value, schema):
    """A deliberately small, checked declarative schema vocabulary."""
    if (not isinstance(schema, dict) or set(schema) - {
            "type", "required", "properties", "additionalProperties", "items", "const", "enum"}):
        raise PricingError("unsupported_body_schema")
    types = {"object": dict, "array": list, "string": str, "boolean": bool,
             "integer": int, "null": type(None)}
    kind = schema.get("type")
    if kind is not None:
        if kind not in types:
            raise PricingError("unsupported_body_schema")
        if type(value) is not types[kind]:
            raise PricingError("zero_unit_body_not_verified")
    if "const" in schema and (type(value) is not type(schema["const"]) or value != schema["const"]):
        raise PricingError("zero_unit_body_not_verified")
    if "enum" in schema and not any(type(value) is type(item) and value == item for item in schema["enum"]):
        raise PricingError("zero_unit_body_not_verified")
    if isinstance(value, dict):
        if not set(schema.get("required", [])).issubset(value):
            raise PricingError("zero_unit_body_not_verified")
        properties = schema.get("properties", {})
        if schema.get("additionalProperties", True) is False and set(value) - set(properties):
            raise PricingError("zero_unit_body_not_verified")
        for name, child in properties.items():
            if name in value:
                _schema(value[name], child)
    elif isinstance(value, list) and "items" in schema:
        for child in value:
            _schema(child, schema["items"])


def _path(value, path):
    if not isinstance(path, list) or not path:
        raise PricingError("invalid_bound_path")
    try:
        for name in path:
            value = value[name]
        return value
    except (KeyError, TypeError, IndexError):
        raise PricingError("required_request_bound_missing") from None


def resolve_units_upper_bounds(operation, prices, policy):
    """Resolve/verify every operation, including later rows sharing a quote.

    Explicit nonzero quantities are caller-owned. A zero quantity for a priced
    component named in zero_unit_profiles still needs its schema witness; an
    explicit zero cannot bypass the same check applied to a zero default.
    """
    body = _body(operation)
    profile = policy.get("operation_profile")
    if profile is not None:
        if profile not in policy.get("request_profiles", {}):
            raise PricingError("unknown_operation_profile")
        _schema(body, policy["request_profiles"][profile]["body_schema"])
    bounds = dict(operation.get("units_upper_bounds", {}))
    defaults = policy.get("unit_bounds_defaults", {})
    for name in prices:
        if name not in bounds:
            rule = defaults.get(name)
            if not isinstance(rule, dict) or len(set(rule) & {"value", "copy_bound"}) != 1:
                raise PricingError("unaccounted_price_component")
            if "copy_bound" in rule:
                if rule["copy_bound"] not in bounds:
                    raise PricingError("required_request_bound_missing")
                bounds[name] = bounds[rule["copy_bound"]]
            else:
                bounds[name] = rule["value"]
    if set(bounds) != set(prices):
        raise PricingError("unaccounted_price_component")
    for name, quantity in bounds.items():
        quantity = _amount(quantity)
        profile = policy.get("zero_unit_profiles", {}).get(name)
        if quantity == 0 and profile is not None:
            if profile not in policy.get("request_profiles", {}):
                raise PricingError("unknown_zero_unit_profile")
            _schema(body, policy["request_profiles"][profile]["body_schema"])
        floor = policy.get("units_bound_verifiers", {}).get(name, {})
        if "minimum_value" in floor and quantity < _amount(floor["minimum_value"]):
            raise PricingError("request_quantity_exceeds_bound")
        if "minimum_bound" in floor:
            reference = floor["minimum_bound"]
            if not isinstance(reference, str) or reference not in bounds:
                raise PricingError("required_request_bound_missing")
            if quantity < _amount(bounds[reference]):
                raise PricingError("request_quantity_exceeds_bound")
        if "minimum_request_path" in floor:
            if quantity < _amount(_path(body, floor["minimum_request_path"])):
                raise PricingError("request_quantity_exceeds_bound")
        for path in floor.get("optional_minimum_request_paths", []):
            try:
                minimum = _path(body, path)
            except PricingError as error:
                if str(error) == "required_request_bound_missing":
                    continue
                raise
            if quantity < _amount(minimum):
                raise PricingError("request_quantity_exceeds_bound")
        if floor.get("minimum_request_bytes") is True:
            if "request_bytes" not in operation:
                raise PricingError("request_snapshot_required")
            if quantity < len(operation["request_bytes"]):
                raise PricingError("request_quantity_exceeds_bound")
    return {name: str(_amount(value)) for name, value in bounds.items()}


def _routing(body, operation, policy):
    routing = policy["routing"]
    model = body.get("model")
    provider = body.get("provider")
    only = provider.get("only") if isinstance(provider, dict) else None
    if (model != operation.get("model_id") or only != [operation.get("provider_id")]
            or provider.get("allow_fallbacks") is not False):
        raise PricingError("request_route_binding_mismatch")
    requested = only[0]
    if not isinstance(requested, str) or not requested or not isinstance(model, str):
        raise PricingError("request_route_binding_mismatch")
    aliases = routing["request_tier_aliases"]
    explicit = body.get(routing["tier_request_field"])
    native = body.get(routing["native_tier_request_field"])
    native_tier = routing["native_tier_request_values"].get(native) if native is not None else None
    if native is not None and native_tier is None:
        raise PricingError("unknown_requested_service_tier")
    if explicit is not None and explicit not in aliases:
        raise PricingError("unknown_requested_service_tier")
    tier = aliases[explicit] if explicit is not None else native_tier
    if tier is not None and native_tier is not None and tier != native_tier:
        raise PricingError("conflicting_requested_service_tiers")
    variant_tiers = None
    base_model = model
    for suffix, admitted in routing["model_variant_tiers"].items():
        if model.endswith(suffix):
            base_model = model[:-len(suffix)]
            variant_tiers = admitted
            break
    suffix_tier = routing["endpoint_tier_suffixes"].get(requested.rsplit("/", 1)[-1]) if "/" in requested else None
    if suffix_tier is not None and tier is not None and suffix_tier != tier:
        raise PricingError("conflicting_requested_service_tiers")
    tiers = ([tier] if tier is not None else [suffix_tier] if suffix_tier is not None
             else variant_tiers if variant_tiers is not None else [routing["default_tier"]])
    return requested, base_model, tiers


def _endpoint_prices(pricing, policy):
    grammar = policy["pricing"]
    if not isinstance(pricing, dict) or not pricing:
        raise PricingError("endpoint_prices_missing")
    components = set(grammar["charge_components"])
    metadata = grammar["noncharge_fields"]
    conditions = grammar["condition_schemas"]
    schedules = pricing.get(grammar["overrides_field"], [])
    if not isinstance(schedules, list):
        raise PricingError("invalid_price_overrides")
    envelope = {}
    for index, row in enumerate([pricing, *schedules]):
        if not isinstance(row, dict):
            raise PricingError("invalid_price_overrides")
        for name, value in row.items():
            if index == 0 and name == grammar["overrides_field"]:
                continue
            if name in metadata:
                discount = _amount(value)
                if metadata[name] != "fractional_discount" or discount > 1:
                    raise PricingError("invalid_noncharge_price_field")
            elif index and name in conditions:
                _schema(value, conditions[name])
                if name in grammar.get("nonnegative_integer_conditions", []):
                    _amount(value)
            elif name in components:
                amount = _amount(value)
                envelope[name] = max(envelope.get(name, Decimal(0)), amount)
            else:
                raise PricingError("unknown_endpoint_price_field")
    for name, value in grammar.get("absent_component_prices_usd", {}).items():
        if name not in components:
            raise PricingError("unknown_endpoint_price_field")
        envelope.setdefault(name, _amount(value))
    if not set(grammar["required_components"]).issubset(envelope):
        raise PricingError("endpoint_prices_missing")
    return envelope


def resolve_operation_routing(operation, policy):
    """Bind each request's routing identity before reusing a cached quote.

    Identical model/provider names do not imply identical service-tier prices.
    Compare this detached audit to the quote's routing audit for every operation.
    """
    if not isinstance(policy, dict) or policy.get("schema") != "loom.endpoint_pricing_policy/1":
        raise PricingError("endpoint_snapshot_and_policy_required")
    try:
        requested, base_model, tiers = _routing(_body(operation), operation, policy)
    except PricingError:
        raise
    except (KeyError, TypeError, ValueError):
        raise PricingError("invalid_operation_routing_or_policy") from None
    return {"provider_only": requested, "base_model": base_model, "admitted_tiers": list(tiers)}


def resolve_endpoint_pricing(raw, operation, policy):
    """Return a source-bound pair envelope plus exact routing/schedule audit.

    Rates are the component-wise maxima of every admitted endpoint's base and
    conditional rates. No condition is discarded from the retained endpoint
    evidence or evaluated as though quote-fetch time were dispatch time.
    """
    if not isinstance(raw, bytes) or policy.get("schema") != "loom.endpoint_pricing_policy/1":
        raise PricingError("endpoint_snapshot_and_policy_required")
    try:
        data = wire.parse_json(raw)["data"]
        body = _body(operation)
        requested, base_model, tiers = _routing(body, operation, policy)
        if data["id"] != base_model or not isinstance(data["endpoints"], list):
            raise PricingError("endpoint_model_mismatch")
        routing = policy["routing"]
        admitted, excluded, prices = [], [], {}
        for index, endpoint in enumerate(data["endpoints"]):
            if not isinstance(endpoint, dict):
                raise PricingError("invalid_endpoint_identity")
            tag = endpoint.get(routing["endpoint_slug_field"])
            if not isinstance(tag, str) or not tag:
                raise PricingError("invalid_endpoint_identity")
            route_match = tag == requested or ("/" not in requested and tag.startswith(requested + "/"))
            if not route_match:
                continue
            if endpoint.get("model_id") != base_model:
                raise PricingError("endpoint_model_mismatch")
            tier = routing["endpoint_tier_suffixes"].get(tag.rsplit("/", 1)[-1], routing["default_tier"]) if "/" in tag else routing["default_tier"]
            if tier not in tiers:
                excluded.append({"endpoint_index": index, "tag": tag, "tier": tier,
                                 "reason": "service_tier_not_requested"})
                continue
            endpoint_prices = _endpoint_prices(endpoint["pricing"], policy)
            for name, value in endpoint_prices.items():
                prices[name] = max(prices.get(name, Decimal(0)), value)
            admitted.append({"endpoint_index": index, "tag": tag, "tier": tier,
                             "provider_name": endpoint.get("provider_name"),
                             "pricing": _snapshot(endpoint["pricing"]),
                             "component_prices_usd": {k: str(v) for k, v in endpoint_prices.items()}})
        if not admitted:
            raise PricingError("admitted_endpoint_missing")
        normalized = {k: str(v) for k, v in prices.items()}
        bounds = resolve_units_upper_bounds(operation, normalized, policy)
    except PricingError:
        raise
    except (KeyError, TypeError, ValueError):
        raise PricingError("invalid_endpoint_snapshot_or_policy") from None
    return {"model_id": operation["model_id"], "provider_id": operation["provider_id"],
            "raw_sha256": hashlib.sha256(raw).hexdigest(),
            "policy_sha256": hashlib.sha256(wire.canonical(policy)).hexdigest(),
            "all_charge_components_accounted": True,
            "component_prices_usd": normalized,
            "checked_operation_units_upper_bounds": bounds,
            "envelope_basis": "componentwise_max_all_admitted_endpoints_all_schedules",
            "routing": {"provider_only": requested, "base_model": base_model, "admitted_tiers": tiers},
            "admitted_endpoints": admitted, "excluded_tier_endpoints": excluded,
            "bound_rationale": _snapshot(policy.get("bound_rationale", {}))}
