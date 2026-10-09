#!/usr/bin/env python3
"""Offline Jev admission; exact bytes go to the existing programme payer.

This is a versioned research adapter, not a transport, price oracle, or ledger.
The legacy validator is retained unchanged. Provider eligibility and campaign
admission require the existing payer's fresh preflight, even after local PASS.
"""
from __future__ import annotations

import argparse
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import re

try:
    from . import jev_live_pilot as legacy
    from . import research_programme_manifest as manifests
except ImportError:
    import jev_live_pilot as legacy
    import research_programme_manifest as manifests

VERSION = "loom.jev_admission/2"
Error = legacy.safe.RunnerError


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def require(condition, code):
    if not condition:
        raise Error(code)


def money(value):
    require(type(value) in (str, int), "policy_amount_invalid")
    try:
        result = Decimal(value)
    except InvalidOperation:
        raise Error("policy_amount_invalid") from None
    require(result.is_finite() and result >= 0, "policy_amount_invalid")
    return result


def validate_policy(policy):
    legacy.safe._keys(policy, {"schema", "id", "model", "provider", "max_body_bytes",
        "max_questions", "prompt_fixed_allowance", "historical_prompt_unit_usd",
        "per_request_planning_allowance_usd", "price_status"})
    require(policy["schema"] == "loom.jev_admission_policy/2", "policy_schema_invalid")
    require(isinstance(policy["id"], str) and re.fullmatch(r"[a-z0-9_-]+", policy["id"]), "policy_id_invalid")
    require(policy["model"] == legacy.MODEL and policy["provider"] == {
        "only": ["typesafe"], "allow_fallbacks": False,
        "max_price": {"prompt": "0.042", "completion": "0"}}, "unsupported_model_provider_policy")
    for key in ("max_body_bytes", "max_questions", "prompt_fixed_allowance"):
        require(type(policy[key]) is int and policy[key] > 0, "policy_integer_invalid")
    require(policy["max_questions"] <= 32, "unsupported_question_count")
    require(money(policy["historical_prompt_unit_usd"]) == legacy.INPUT_CAP,
            "historical_price_pin_mismatch")
    require(money(policy["per_request_planning_allowance_usd"]) > 0,
            "planning_allowance_required")
    require(policy["price_status"] == "historical_unverified_for_live_dispatch", "price_status_invalid")
    return policy


def validate_shape(body, policy):
    """Versioned Decisions shape; unknown parameters are rejected, never dropped."""
    legacy.safe._keys(body, {"model", "state", "questions", "provider"})
    require(body["model"] == policy["model"] and body["provider"] == policy["provider"],
            "jev_model_provider_or_price_pin_invalid")
    legacy.safe._keys(body["state"], {"text"})
    require(isinstance(body["state"]["text"], str) and bool(body["state"]["text"]), "jev_text_required")
    questions = body["questions"]
    require(isinstance(questions, dict) and 1 <= len(questions) <= policy["max_questions"], "jev_questions_invalid")
    for identifier, question in questions.items():
        require(isinstance(identifier, str) and re.fullmatch(r"q[0-9]{2}", identifier), "jev_question_id_invalid")
        legacy.safe._keys(question, {"type", "instructions", "criteria"})
        legacy.safe._keys(question["criteria"], {"true", "false"})
        require(question["type"] == "noul" and all(isinstance(value, str) and bool(value)
            for value in (question["instructions"], *question["criteria"].values())), "jev_noul_question_invalid")


def admit(raw, policy):
    """Return three distinct gates. Never infer live eligibility from local bounds."""
    validate_policy(policy)
    require(isinstance(raw, bytes), "exact_request_bytes_required")
    row = {"schema": VERSION, "policy_id": policy["id"], "policy_sha256": sha(legacy.safe.canonical(policy)),
           "request_sha256": sha(raw), "wire_body_bytes": len(raw), "canonical_body_bytes": None,
           "local_admission": "rejected", "reasons": [], "historical_profile": None,
           "historical_planning_upper_usd": None, "provider_eligibility": None,
           "provider_context_limit": None, "current_price_upper_usd": None,
           "campaign_admission": None, "reservation_reference": None,
           "dispatch_ready": False, "execution_status": "not_executed",
           "pending_requirements": ["fresh_provider_capabilities_and_limits", "fresh_component_prices",
               "bound_campaign_key_identity", "current_usage_and_unresolved_reservations", "existing_payer_reservation"]}
    try:
        body = legacy.safe.parse_json(raw)
        validate_shape(body, policy)
    except Error as error:
        row["reasons"] = [str(error)]
        return row
    canonical_bytes = len(legacy.safe.canonical(body))
    row["canonical_body_bytes"] = canonical_bytes
    row["parameter_inventory"] = sorted(body)
    row["parameter_tree_sha256"] = sha(legacy.safe.canonical(body))
    legacy_reason = None
    try:
        legacy.validate_body(body)
    except Error as error:
        legacy_reason = str(error)
    row["historical_profile"] = {
        "status": "admitted" if legacy_reason is None else "rejected", "reason": legacy_reason,
        "canonical_bytes_limit": legacy.BODY_LIMIT, "per_request_allowance_usd": str(legacy.RESERVE),
        "byte_limit_exceeded": canonical_bytes > legacy.BODY_LIMIT,
        "allowance_exceeded": (canonical_bytes + 1024) * legacy.INPUT_CAP > legacy.RESERVE}
    upper = (Decimal(len(raw)) + policy["prompt_fixed_allowance"]) * money(policy["historical_prompt_unit_usd"])
    row["historical_planning_upper_usd"] = format(upper, "f")
    if len(raw) > policy["max_body_bytes"]:
        row["reasons"].append("study_wire_body_byte_policy_exceeded")
    if upper > money(policy["per_request_planning_allowance_usd"]):
        row["reasons"].append("study_planning_allowance_exceeded")
    if not row["reasons"]:
        row.update(local_admission="admitted", reasons=["shape_and_explicit_study_bounds_satisfied"])
    return row


def write_new(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    with path.open("xb") as handle:
        handle.write(raw)
    path.chmod(0o600)


def write_json(path, value):
    write_new(path, legacy.safe.canonical(value) + b"\n")


def verify_prior(directory, expected_freeze_sha256):
    directory = Path(directory).resolve()
    raw = (directory / "FREEZE.json").read_bytes()
    require(sha(raw) == expected_freeze_sha256, "prior_freeze_sha256_mismatch")
    freeze = legacy.safe.parse_json(raw)
    for name, digest in freeze["files"].items():
        target = (directory / name).resolve()
        require(target.is_relative_to(directory), "prior_freeze_path_escape")
        require(sha(target.read_bytes()) == digest, "prior_freeze_payload_mismatch")
    return freeze


def prepare(prior_dir, output_dir, plan):
    """Re-admit each saved full intent; preserve input bytes and emit payer v1 data."""
    prior_dir, output = Path(prior_dir).resolve(), Path(output_dir).resolve()
    require(not output.exists(), "new_private_output_required")
    require(not any((parent / ".git").exists() for parent in (output, *output.parents)), "private_output_inside_git")
    freeze = verify_prior(prior_dir, plan["prior_freeze_sha256"])
    require(sha(Path(legacy.__file__).read_bytes()) == plan["legacy_validator_sha256"],
            "legacy_validator_version_mismatch")
    prior_payer = manifests.load_manifest(prior_dir / "payer/manifest.json")
    require(prior_payer["programme_id"] == plan["programme_id"], "campaign_programme_identity_mismatch")
    policy = validate_policy(plan["policy"])
    output.mkdir(parents=True, mode=0o700)
    admissions, operations = [], []
    for arm in plan["arms"]:
        paths = sorted((prior_dir / "desired-requests" / arm).glob("*.json"))
        require(len(paths) == plan["intentions_per_arm"], "prior_intention_count_mismatch")
        for path in paths:
            relative = str(path.relative_to(prior_dir))
            require(relative in freeze["files"], "unfrozen_prior_intention")
            raw = path.read_bytes()
            admission = admit(raw, policy)
            operation_id = plan["stage_id"] + "." + arm + "." + path.stem
            admission.update(operation_id=operation_id, arm=arm, input_file=relative)
            admissions.append(admission)
            # Preserve rejected intents too. A rejected intent is not a payer operation.
            write_new(output / "intentions" / arm / path.name, raw)
            if admission["local_admission"] != "admitted":
                continue
            body = legacy.safe.parse_json(raw)
            filename = "requests/" + sha(raw) + ".json"
            write_new(output / "payer" / filename, raw)
            operations.append({"operation_id": operation_id, "route_id": "jev", "request_file": filename,
                "request_sha256": sha(raw), "model_id": body["model"], "provider_id": body["provider"]["only"][0],
                "units_upper_bounds": {"prompt": len(raw) + policy["prompt_fixed_allowance"], "completion": 0, "request": 1},
                "minimum_reservation_usd": policy["per_request_planning_allowance_usd"],
                "metadata": {"admission_policy_sha256": admission["policy_sha256"], "arm_id": arm,
                    "source_request_sha256": sha(raw), "provider_eligibility": None, "execution_status": "not_executed"}})
    payer = {"schema": manifests.SCHEMA, "programme_id": plan["programme_id"], "stage_id": plan["stage_id"],
        "operations": operations, "metadata": {"admission_schema": VERSION, "prior_freeze_sha256": plan["prior_freeze_sha256"],
            "admission_policy_sha256": sha(legacy.safe.canonical(policy)), "dispatch_ready": False,
            "requires_existing_payer_fresh_preflight": True, "minimum_reservation_is_plan_not_booked_money": True}}
    if operations:
        manifests.validate_manifest(payer, base_dir=output / "payer")
        write_json(output / "payer" / "manifest.json", payer)
    write_json(output / "ADMISSIONS.json", admissions)
    write_json(output / "PLAN.json", plan)
    receipt = {"schema": "loom.thread7_jev_admission_receipt/2", "prior_freeze_sha256": plan["prior_freeze_sha256"],
        "policy_sha256": sha(legacy.safe.canonical(policy)), "intended": len(admissions),
        "historically_admitted": sum(bool(row["historical_profile"] and row["historical_profile"]["status"] == "admitted") for row in admissions),
        "locally_admitted": len(operations), "locally_rejected": len(admissions) - len(operations),
        "all_intentions_preserved": True, "request_bytes_unchanged": True, "parameters_dropped": [],
        "wire_bytes_min": min(row["wire_body_bytes"] for row in admissions),
        "wire_bytes_max": max(row["wire_body_bytes"] for row in admissions),
        "provider_eligibility": None, "campaign_admission": None, "current_price_upper_usd": None,
        "billing_bound_guaranteed": False,
        "planned_minimum_reservations_usd": format(len(operations) * money(policy["per_request_planning_allowance_usd"]), "f"),
        "historical_tariff_planning_upper_usd": format(sum((money(row["historical_planning_upper_usd"])
            for row in admissions if row["historical_planning_upper_usd"] is not None), Decimal(0)), "f"),
        "reservation_actually_booked": False, "new_paid_calls": 0, "new_paid_cost_usd": "0",
        "dispatch_ready": False, "quality": None,
        "payer_manifest_sha256": sha(legacy.safe.canonical(payer) + b"\n") if operations else None,
        "admissions_sha256": sha(legacy.safe.canonical(admissions) + b"\n"),
        "adapter_sha256": sha(Path(__file__).read_bytes()),
        "legacy_validator_sha256": sha(Path(legacy.__file__).read_bytes())}
    write_json(output / "RECEIPT.json", receipt)
    write_json(output / "FREEZE.json", {"schema": "loom.thread7_jev_admission_freeze/2",
        "prior_freeze_sha256": plan["prior_freeze_sha256"], "before_evaluation": True,
        "files": {str(path.relative_to(output)): sha(path.read_bytes()) for path in sorted(output.rglob("*")) if path.is_file()}})
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prior", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--plan", type=Path, required=True)
    args = parser.parse_args()
    try:
        receipt = prepare(args.prior, args.output, legacy.safe.parse_json(args.plan.read_bytes()))
        print(json.dumps(receipt, sort_keys=True))
    except (Error, OSError, KeyError, TypeError, ValueError):
        # No private paths, source text, exception payloads or request bodies in diagnostics.
        print('{"error":"jev_admission_preparation_failed"}')
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
