#!/usr/bin/env python3
"""Exact request-file manifests and offline projections of prepared studies.

This module neither compiles prompts nor scores answers. Legacy embedded bodies
are materialized with the same canonical serializer their transports use; new
request-file manifests retain their original bytes, including whitespace.
"""
from __future__ import annotations

import argparse
from decimal import Decimal, InvalidOperation
import hashlib
from pathlib import Path
import re

try:
    from . import openrouter_runner as wire
except ImportError:
    import openrouter_runner as wire

SCHEMA = "loom.research_programme_manifest/1"
ManifestError = wire.RunnerError
UNITS_POLICY_PATH = (Path(__file__).resolve().parents[3] /
                     "docs/research/model_research_2026-10-04/billing/request-units.json")
_IDENTITY = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.:-]*\Z")
_SHA256 = re.compile(r"[0-9a-f]{64}\Z")
_OPERATION_FIELDS = {"operation_id", "route_id", "request_file", "request_sha256",
                     "model_id", "provider_id", "units_upper_bounds"}


def _identity(value):
    if not isinstance(value, str) or not _IDENTITY.fullmatch(value):
        raise ManifestError("invalid_programme_identity")
    return value


def _quantity(value):
    if type(value) not in (int, float, str):
        raise ManifestError("invalid_units_upper_bound")
    try:
        quantity = Decimal(str(value))
    except InvalidOperation:
        raise ManifestError("invalid_units_upper_bound") from None
    if not quantity.is_finite() or quantity < 0:
        raise ManifestError("invalid_units_upper_bound")
    return quantity


def _units(value):
    if not isinstance(value, dict) or not value:
        raise ManifestError("units_upper_bounds_required")
    for component, quantity in value.items():
        if not isinstance(component, str) or not component or component.strip() != component:
            raise ManifestError("invalid_units_component")
        _quantity(quantity)


def _units_policy(value):
    wire._keys(value, {"chat", "jev"})
    wire._keys(value["chat"], {"prompt_fixed_allowance", "prompt_per_message_allowance", "request"})
    wire._keys(value["jev"], {"prompt_fixed_allowance", "completion", "request"})
    for route in value.values():
        for quantity in route.values():
            _quantity(quantity)
    return value


def _policy_data(value):
    if value is None:
        try:
            value = wire.parse_json(UNITS_POLICY_PATH.read_bytes())
        except OSError:
            raise ManifestError("request_units_policy_missing") from None
    _units_policy(value)
    # Bind a detached snapshot, so later edits to a caller mapping cannot alter it.
    return wire.parse_json(wire.canonical(value))


def _body_pair(body):
    if not isinstance(body, dict) or not isinstance(body.get("model"), str) or not body["model"]:
        raise ManifestError("explicit_request_model_required")
    provider = body.get("provider")
    only = provider.get("only") if isinstance(provider, dict) else None
    if (not isinstance(only, list) or len(only) != 1 or
            not isinstance(only[0], str) or not only[0] or
            provider.get("allow_fallbacks") is not False):
        raise ManifestError("one_pinned_request_provider_required")
    return body["model"], only[0]


def _body_route(body):
    if "messages" in body and "questions" not in body:
        return "chat"
    if "questions" in body and "state" in body and "messages" not in body:
        return "jev"
    raise ManifestError("prepared_request_route_unavailable")


def _request_path(operation, base_dir):
    name = operation.get("request_file")
    if not isinstance(name, str) or not name or Path(name).is_absolute():
        raise ManifestError("invalid_request_file")
    base = Path(base_dir).resolve()
    path = (base / name).resolve()
    if not path.is_relative_to(base) or path == base:
        raise ManifestError("request_file_outside_manifest")
    return path


def read_request(operation, base_dir):
    """Return exact file bytes after checking hash and requested identity."""
    expected = operation.get("request_sha256")
    if not isinstance(expected, str) or not _SHA256.fullmatch(expected):
        raise ManifestError("invalid_request_sha256")
    try:
        raw = _request_path(operation, base_dir).read_bytes()
    except OSError:
        raise ManifestError("request_artifact_missing") from None
    if hashlib.sha256(raw).hexdigest() != expected:
        raise ManifestError("request_artifact_hash_mismatch")
    body = wire.parse_json(raw)
    model, provider = _body_pair(body)
    if model != operation.get("model_id") or provider != operation.get("provider_id"):
        raise ManifestError("request_model_provider_mismatch")
    if _body_route(body) != operation.get("route_id"):
        raise ManifestError("request_route_mismatch")
    return raw


def read_manifest_bytes(raw):
    """Parse one immutable manifest snapshot with duplicate/nonfinite rejection.

    Artifact validation is separate because it requires an explicit base_dir.
    Callers must hash these same bytes and pass this mapping to load operations.
    """
    if not isinstance(raw, bytes):
        raise ManifestError("manifest_snapshot_must_be_bytes")
    manifest = wire.parse_json(raw)
    if not isinstance(manifest, dict):
        raise ManifestError("programme_manifest_object_required")
    return manifest


def _source(source, base_dir=None):
    if isinstance(source, (str, Path)):
        path = Path(source)
        try:
            manifest = read_manifest_bytes(path.read_bytes())
        except OSError:
            raise ManifestError("programme_manifest_missing") from None
        return manifest, path.parent if base_dir is None else Path(base_dir)
    if base_dir is None:
        raise ManifestError("manifest_base_dir_required")
    return source, Path(base_dir)


def validate_manifest(manifest, *, base_dir):
    """Validate metadata and every exact request artifact without network I/O."""
    wire._keys(manifest, {"schema", "programme_id", "stage_id", "operations"}, {"metadata"})
    if manifest["schema"] != SCHEMA:
        raise ManifestError("invalid_programme_manifest_schema")
    _identity(manifest["programme_id"])
    _identity(manifest["stage_id"])
    metadata = manifest.get("metadata", {})
    if not isinstance(metadata, dict):
        raise ManifestError("invalid_programme_metadata")
    if "units_policy" in metadata or "units_policy_sha256" in metadata:
        policy = _units_policy(metadata.get("units_policy"))
        if metadata.get("units_policy_sha256") != wire.digest(policy):
            raise ManifestError("units_policy_hash_mismatch")
    operations = manifest["operations"]
    if not isinstance(operations, list) or not operations:
        raise ManifestError("programme_operations_required")
    identifiers = set()
    for operation in operations:
        wire._keys(operation, _OPERATION_FIELDS, {"minimum_reservation_usd", "metadata"})
        identifier = _identity(operation["operation_id"])
        if identifier in identifiers:
            raise ManifestError("duplicate_operation_id")
        identifiers.add(identifier)
        if operation["route_id"] not in ("chat", "jev"):
            raise ManifestError("unsupported_programme_route")
        _units(operation["units_upper_bounds"])
        if "minimum_reservation_usd" in operation:
            _quantity(operation["minimum_reservation_usd"])
        read_request(operation, base_dir)
    return manifest


def load_manifest(source, *, base_dir=None):
    """Load a file, or validate a mapping with an explicit artifact directory."""
    manifest, directory = _source(source, base_dir)
    return validate_manifest(manifest, base_dir=directory)


def load_operations(source, *, base_dir=None):
    """Load operations with exact request_bytes and parsed request_body added."""
    manifest, directory = _source(source, base_dir)
    validate_manifest(manifest, base_dir=directory)
    result = []
    for operation in manifest["operations"]:
        raw = read_request(operation, directory)
        result.append({**operation, "request_bytes": raw, "request_body": wire.parse_json(raw)})
    return result


def _prepared_rows(document, path):
    """Locate existing prepared bodies without invoking a compiler or scorer."""
    schema = document.get("schema") if isinstance(document, dict) else None
    rows = document if isinstance(document, list) else document.get("requests") if isinstance(document, dict) else None
    if not isinstance(rows, list) or not rows:
        raise ManifestError("prepared_requests_required")
    namespace = document.get("experiment_id", path.parent.name) if isinstance(document, dict) else path.parent.name
    for row in rows:
        if not isinstance(row, dict):
            raise ManifestError("invalid_prepared_request")
        exact_hashes, reservation, source_units = [], None, {}
        if schema == "loom.native_semantic_study.preparation/1":
            request = row.get("request", {})
            arm = row.get("method_id")
            exact_hashes.append(row.get("request_hash"))
        elif schema == "loom.frontier_comparison/1":
            historical = row.get("historical_openrouter_plan", {})
            request = {"id": row.get("id"), "body": historical.get("body")}
            arm = row.get("model", {}).get("key", namespace)
            reservation = historical.get("reservation", {})
        elif isinstance(schema, str) and schema.startswith("loom.frontier_reply_followup/1/"):
            request = row
            arm = row.get("arm", {}).get("key", namespace)
            reservation = row.get("historical_reservation", {})
            exact_hashes.append(reservation.get("exact_request_body_sha256"))
        else:
            request = row
            arm = row.get("arm", path.parent.name)
            exact_hashes.extend(row.get(key) for key in ("request_hash", "body_sha256") if key in row)
        if not isinstance(request, dict) or not isinstance(request.get("body"), dict):
            raise ManifestError("exact_prepared_body_required")
        if not isinstance(arm, str):
            raise ManifestError("invalid_prepared_arm")
        raw = wire.canonical(request["body"])
        digest = hashlib.sha256(raw).hexdigest()
        if any(value != digest for value in exact_hashes):
            raise ManifestError("prepared_request_hash_mismatch")
        if isinstance(reservation, dict):
            for name, component in (("prompt_token_allowance", "prompt"),
                                    ("completion_token_allowance", "completion")):
                if name in reservation:
                    source_units[component] = reservation[name]
            minimum = reservation.get("historical_usd")
        else:
            minimum = request.get("reservation_usd", row.get("historical_reservation_usd"))
        identifier = request.get("id")
        if not isinstance(identifier, str) or not identifier:
            raise ManifestError("invalid_prepared_request_id")
        # Preserve meaningful identifiers, including the existing colon separator.
        operation_id = _identity(str(namespace) + "." + identifier)
        yield operation_id, arm, identifier, request["body"], raw, minimum, source_units


def _default_units(body, raw, units_policy):
    route = _body_route(body)
    policy = units_policy[route]
    if route == "chat":
        messages = body["messages"]
        if not isinstance(messages, list) or type(body.get("max_tokens")) is not int or body["max_tokens"] < 0:
            raise ManifestError("prepared_chat_units_unavailable")
        prompt = (Decimal(len(raw)) + _quantity(policy["prompt_fixed_allowance"]) +
                  _quantity(policy["prompt_per_message_allowance"]) * len(messages))
        return "chat", {"prompt": int(prompt) if prompt == int(prompt) else format(prompt, "f"),
                        "completion": body["max_tokens"], "request": policy["request"]}
    if route == "jev":
        prompt = Decimal(len(raw)) + _quantity(policy["prompt_fixed_allowance"])
        return "jev", {"prompt": int(prompt) if prompt == int(prompt) else format(prompt, "f"),
                       "completion": policy["completion"], "request": policy["request"]}


def _write_exact(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("xb") as handle:
            handle.write(raw)
    except FileExistsError:
        if path.read_bytes() != raw:
            raise ManifestError("adapted_artifact_already_exists") from None


def adapt_prepared_manifests(sources, output_dir, *, programme_id, stage_id,
                             units_upper_bounds=None, units_by_arm=None,
                             units_by_operation=None, units_policy=None):
    """Project prepared files into one manifest; all overrides are explicit data.

    Overrides merge in this order: existing allowance, defaults supplied by the
    caller, arm override, operation override. Additional billable components are
    never assumed zero: a fresh price gate must require every priced component.
    """
    _identity(programme_id)
    _identity(stage_id)
    allowance_policy = _policy_data(units_policy)
    common = {} if units_upper_bounds is None else units_upper_bounds
    arms = {} if units_by_arm is None else units_by_arm
    per_operation = {} if units_by_operation is None else units_by_operation
    for value in (common, arms, per_operation):
        if not isinstance(value, dict):
            raise ManifestError("invalid_units_configuration")
    operations, artifacts, provenance, identifiers, arm_ids = [], {}, [], set(), set()
    for source in sources:
        path = Path(source)
        try:
            source_raw = path.read_bytes()
        except OSError:
            raise ManifestError("prepared_manifest_missing") from None
        document = wire.parse_json(source_raw)
        source_hash = hashlib.sha256(source_raw).hexdigest()
        provenance.append({"file": str(path), "sha256": source_hash})
        for identifier, arm, request_id, body, raw, minimum, source_units in _prepared_rows(document, path):
            if identifier in identifiers:
                raise ManifestError("duplicate_operation_id")
            identifiers.add(identifier)
            arm_ids.add(arm)
            model, provider = _body_pair(body)
            route, units = _default_units(body, raw, allowance_policy)
            for override in (source_units, common, arms.get(arm, {}), per_operation.get(identifier, {})):
                if not isinstance(override, dict):
                    raise ManifestError("invalid_units_configuration")
                units.update(override)
            _units(units)
            digest = hashlib.sha256(raw).hexdigest()
            filename = "requests/" + digest + ".json"
            artifacts[filename] = raw
            operation = {"operation_id": identifier, "route_id": route,
                         "request_file": filename, "request_sha256": digest,
                         "model_id": model, "provider_id": provider,
                         "units_upper_bounds": units,
                         "metadata": {"source_manifest_sha256": source_hash,
                                      "prepared_request_id": request_id, "arm_id": arm}}
            if minimum is not None:
                _quantity(minimum)
                operation["minimum_reservation_usd"] = minimum
            operations.append(operation)
    if not operations:
        raise ManifestError("programme_operations_required")
    if set(arms) - arm_ids:
        raise ManifestError("units_override_unknown_arm")
    if set(per_operation) - identifiers:
        raise ManifestError("units_override_unknown_operation")
    manifest = {"schema": SCHEMA, "programme_id": programme_id,
                "stage_id": stage_id, "operations": operations,
                "metadata": {"source_manifests": provenance,
                             "units_policy": allowance_policy,
                             "units_policy_sha256": wire.digest(allowance_policy),
                             "request_bytes": "existing_transport_canonical_utf8",
                             "billing_bound_guaranteed": False,
                             "paid_calls": 0}}
    directory = Path(output_dir)
    manifest_raw = wire.canonical(manifest) + b"\n"
    target = directory / "manifest.json"
    if target.exists() and target.read_bytes() != manifest_raw:
        raise ManifestError("adapted_artifact_already_exists")
    for filename, raw in artifacts.items():
        _write_exact(directory / filename, raw)
    validate_manifest(manifest, base_dir=directory)
    _write_exact(target, manifest_raw)
    return manifest


def adapt_prepared_manifest(source, output_dir, **kwargs):
    """Project one legacy prepared manifest or request preview."""
    return adapt_prepared_manifests([source], output_dir, **kwargs)


def adapt_analysis_optimization(prepared_dir, output_dir, **kwargs):
    """Project the arm order recorded in the existing 432-request plan."""
    directory = Path(prepared_dir)
    plan = wire.parse_json((directory / "plan.json").read_bytes())
    arms = plan.get("arms")
    if not isinstance(arms, list) or not arms:
        raise ManifestError("analysis_prepared_arms_required")
    sources = []
    expected = 0
    seen = set()
    for arm in arms:
        if not isinstance(arm, dict) or arm.get("arm") in seen:
            raise ManifestError("duplicate_prepared_arm")
        name = _identity(arm.get("arm"))
        seen.add(name)
        source = directory / name / "manifest.json"
        document = wire.parse_json(source.read_bytes())
        if type(arm.get("requests")) is not int or len(document.get("requests", [])) != arm["requests"]:
            raise ManifestError("analysis_prepared_count_mismatch")
        expected += arm["requests"]
        sources.append(source)
    if plan.get("total_requests") != expected:
        raise ManifestError("analysis_prepared_count_mismatch")
    return adapt_prepared_manifests(sources, output_dir, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("verify")
    check.add_argument("manifest", type=Path)
    for name in ("adapt", "adapt-analysis"):
        command = commands.add_parser(name)
        command.add_argument("sources", nargs="+", type=Path)
        command.add_argument("--output", required=True, type=Path)
        command.add_argument("--programme-id", required=True)
        command.add_argument("--stage-id", required=True)
        command.add_argument("--units-config", type=Path)
    args = parser.parse_args()
    if args.command == "verify":
        manifest = load_manifest(args.manifest)
    else:
        options = wire.parse_json(args.units_config.read_bytes()) if args.units_config else {}
        wire._keys(options, set(), {"units_upper_bounds", "units_by_arm", "units_by_operation", "units_policy"})
        kwargs = {"programme_id": args.programme_id, "stage_id": args.stage_id, **options}
        if args.command == "adapt-analysis":
            if len(args.sources) != 1:
                parser.error("adapt-analysis requires exactly one prepared directory")
            manifest = adapt_analysis_optimization(args.sources[0], args.output, **kwargs)
        else:
            manifest = adapt_prepared_manifests(args.sources, args.output, **kwargs)
    print(wire.canonical({"schema": SCHEMA, "programme_id": manifest["programme_id"],
                          "stage_id": manifest["stage_id"],
                          "operations": len(manifest["operations"]), "paid_calls": 0}).decode())


if __name__ == "__main__":
    main()
