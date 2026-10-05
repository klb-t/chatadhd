#!/usr/bin/env python3
"""Preparation-only admission checks for the owner's separate EUR5 programme.

Inputs are private, normalised evidence records. No secret loading, environment
inspection, network access, paid dispatch or mutation of the historical budget.
Passing this check is not proof a future transport actually uses the bound key.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import re


def amount(value):
    if isinstance(value, bool):
        raise ValueError("invalid_amount")
    try:
        result = Decimal(str(value))
    except InvalidOperation:
        raise ValueError("invalid_amount") from None
    if not result.is_finite() or result < 0:
        raise ValueError("invalid_amount")
    return result


def timepoint(value):
    result = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if result.tzinfo is None:
        raise ValueError("timezone_missing")
    return result.astimezone(timezone.utc)


def digest(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def evaluate(policy, evidence, ledger, now):
    blockers, checks = [], {}
    def check(name, valid):
        checks[name] = bool(valid)
        if not valid:
            blockers.append(name)
    def fresh(name, row):
        try:
            age = (now - timepoint(row["checked_at"])).total_seconds()
            options = policy["freshness_presets_seconds"]
            return -float(amount(options["future_clock_skew"])) <= age <= float(amount(options[name]))
        except (KeyError, TypeError, ValueError, AttributeError):
            return False

    authorization = policy.get("authorization", {})
    check("owner_authorization_record", authorization.get("status") == "owner_authorized_within_budget"
          and authorization.get("currency") == "EUR" and bool(authorization.get("source_ref")))
    check("provider_currency_usd", policy.get("provider_currency") == "USD")
    binding = evidence.get("key_binding", {})
    fingerprint = binding.get("key_fingerprint_sha256")
    check("new_private_key_binding", digest(fingerprint)
          and binding.get("programme_id") == policy.get("programme_id")
          and binding.get("bound_to_loaded_credential") is True
          and bool(binding.get("separate_new_key_owner_confirmation_ref")))

    key = evidence.get("key_metadata", {})
    fx = evidence.get("fx", {})
    check("fresh_bound_key_metadata", key.get("key_fingerprint_sha256") == fingerprint
          and digest(fingerprint) and key.get("http_status") == 200 and fresh("key_metadata", key))
    check("fresh_documented_fx", fx.get("base_currency") == "EUR" and fx.get("quote_currency") == "USD"
          and bool(fx.get("source_ref")) and fresh("fx", fx))
    budget_eur, cap_usd, converted_usd = None, None, None
    try:
        budget_eur = amount(authorization["amount"])
        cap_usd = amount(policy["usd_cap"])
        fx_rate = amount(fx["usd_per_eur"])
        converted_usd = budget_eur * fx_rate
        check("documented_usd_cap_within_eur_authority", budget_eur > 0 and fx_rate > 0
              and cap_usd > 0 and cap_usd <= converted_usd)
    except (KeyError, TypeError, ValueError):
        check("documented_usd_cap_within_eur_authority", False)

    provider_usage, provider_limit, provider_remaining = None, None, None
    try:
        provider_usage = amount(key["usage_usd"])
        provider_limit = amount(key["limit_usd"])
        provider_remaining = amount(key["remaining_usd"])
        check("separate_nonresetting_provider_cap", "limit_reset" in key and key["limit_reset"] is None
              and key.get("is_management_key") is False
              and cap_usd is not None and 0 < provider_limit <= cap_usd and provider_remaining <= provider_limit)
        check("byok_usage_accounted", amount(key["byok_usage_usd"]) == 0
              and type(key.get("include_byok_in_limit")) is bool)
    except (KeyError, TypeError, ValueError):
        check("separate_nonresetting_provider_cap", False)
        check("byok_usage_accounted", False)

    check("separate_programme_ledger", ledger.get("schema") == "loom.new_programme_billing_ledger/1"
          and ledger.get("programme_id") == policy.get("programme_id")
          and isinstance(ledger.get("attempts"), list))
    known, unresolved, unknown_count = Decimal(0), Decimal(0), 0
    operations = set()
    ledger_valid = True
    for row in ledger.get("attempts", []) if isinstance(ledger.get("attempts"), list) else []:
        try:
            operation = row["operation_id"]
            if not isinstance(operation, str) or not operation or operation in operations:
                raise ValueError("duplicate_or_missing_operation")
            operations.add(operation)
            if row.get("key_fingerprint_sha256") != fingerprint or row.get("programme_id") != policy.get("programme_id"):
                raise ValueError("unbound_attempt")
            cost = row.get("actual_cost_usd")
            if cost is None:
                unknown_count += 1
                unresolved += amount(row["reservation_usd"])
            else:
                if row.get("is_byok") is not False or not digest(row.get("response_sha256")):
                    raise ValueError("cost_without_credit_billing_receipt")
                known += amount(cost)
        except (KeyError, TypeError, ValueError):
            ledger_valid = False
    check("cumulative_attempt_receipts_valid", ledger_valid)
    check("unknown_cost_continuation_policy", unknown_count == 0 or policy.get("unknown_cost_policy") == "reserve")
    check("provider_usage_reconciles_with_cumulative_actuals", provider_usage is not None and provider_usage == known)

    stage = evidence.get("stage", {})
    price_rows = evidence.get("pricing", [])
    check("frozen_stage_manifest", bool(stage.get("stage_id")) and digest(stage.get("manifest_sha256")))
    requested = stage.get("model_provider_pairs", [])
    price_ok = isinstance(price_rows, list) and isinstance(requested, list) and bool(requested)
    price_projection = Decimal(0)
    for pair in requested if isinstance(requested, list) else []:
        rows = [row for row in price_rows if row.get("model_id") == pair.get("model_id")
                and row.get("provider_id") == pair.get("provider_id")] if isinstance(price_rows, list) else []
        price_ok = price_ok and len(rows) == 1 and fresh("pricing", rows[0]) if rows else False
        if rows:
            price_ok = price_ok and rows[0].get("currency") == "USD" and bool(rows[0].get("source_ref"))
            price_ok = price_ok and digest(rows[0].get("raw_sha256")) and rows[0].get("all_charge_components_accounted") is True
            try:
                prices, bounds = rows[0]["component_prices_usd"], pair["units_upper_bounds"]
                if not isinstance(prices, dict) or not prices or not isinstance(bounds, dict) or set(prices) != set(bounds):
                    raise ValueError("incomplete_charge_components")
                price_projection += sum((amount(prices[name]) * amount(bounds[name]) for name in prices), Decimal(0))
            except (KeyError, TypeError, ValueError):
                price_ok = False
    check("fresh_endpoint_price_evidence_for_every_stage_pair", price_ok)
    stage_reservation = None
    try:
        stage_reservation = amount(stage["reservation_usd"])
        check("stage_reservation_covers_price_projection", price_ok and stage_reservation >= price_projection)
        check("stage_fits_cumulative_budget", cap_usd is not None and provider_remaining is not None
              and known + unresolved + stage_reservation <= cap_usd
              and unresolved + stage_reservation <= provider_remaining)
    except (KeyError, TypeError, ValueError):
        check("stage_reservation_covers_price_projection", False)
        check("stage_fits_cumulative_budget", False)
    return {"schema": "loom.new_programme_readiness/1", "programme_id": policy.get("programme_id"),
            "status": "ready_for_bound_transport_preflight" if not blockers else "blocked",
            "preparation_only": True, "network_calls": 0, "paid_calls": 0,
            "blockers": blockers, "checks": checks,
            "accounting": {"authorized_eur": str(budget_eur) if budget_eur is not None else None,
                           "configured_usd_cap": str(cap_usd) if cap_usd is not None else None,
                           "cumulative_actual_usd": str(known), "unresolved_reservations_usd": str(unresolved),
                           "unknown_cost_attempts": unknown_count,
                           "declared_price_projection_usd": str(price_projection) if price_ok else None,
                           "next_stage_reservation_usd": str(stage_reservation) if stage_reservation is not None else None},
            "boundary": "Supplied private binding/pricing/FX records are checked, not remotely authenticated. A live transport must bind this exact key, durably reserve before dispatch, retain first responses, and rerun after every paid operation and before every stage. Reference FX does not certify a card settlement rate."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy", required=True)
    parser.add_argument("--evidence", help="Private normalized key/pricing/FX/stage records; never a secret")
    parser.add_argument("--ledger", help="Separate programme's cumulative receipts")
    parser.add_argument("--now", help="Explicit test clock; defaults to current UTC")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    inputs = {"policy": Path(args.policy).read_bytes()}
    policy = json.loads(inputs["policy"], parse_float=Decimal)
    evidence, ledger = {}, policy["initial_ledger"]
    if args.evidence:
        inputs["evidence"] = Path(args.evidence).read_bytes()
        evidence = json.loads(inputs["evidence"], parse_float=Decimal)
    if args.ledger:
        inputs["ledger"] = Path(args.ledger).read_bytes()
        ledger = json.loads(inputs["ledger"], parse_float=Decimal)
    now = timepoint(args.now) if args.now else datetime.now(timezone.utc)
    report = evaluate(policy, evidence, ledger, now)
    report["input_sha256"] = {name: hashlib.sha256(raw).hexdigest() for name, raw in inputs.items()}
    with open(args.output, "x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2); stream.write("\n")
    print(json.dumps({"status": report["status"], "blockers": report["blockers"], "accounting": report["accounting"]}, indent=2))


if __name__ == "__main__":
    main()
