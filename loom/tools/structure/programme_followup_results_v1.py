"""Offline, source-bound evaluation of every public normalized first choice.

Evaluator selection, paths, source hashes and representation transforms belong
in caller data. No transport, private capture, model call, repair, ranking or
canonical graph write is performed. Existing pinned instruments decide validity.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path

try:
    from . import openrouter_runner as wire
    from . import frontier_comparison_v1 as frontier
    from . import frontier_reply_followup_v1 as followup
except ImportError:
    import openrouter_runner as wire
    import frontier_comparison_v1 as frontier
    import frontier_reply_followup_v1 as followup


class FollowupResultsError(ValueError):
    pass


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def digest(value):
    return sha(wire.canonical(value))


def serialize_report(value):
    """Reversible JSON escapes retain even invalid decoded Unicode choices.

    A lone surrogate has no valid first-content UTF-8 and receives no such
    hash. It still survives as its original JSON string beside its failure.
    """
    return json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":"),
                      allow_nan=False).encode("ascii") + b"\n"


def _sha256(value):
    return isinstance(value, str) and len(value) == 64 and all(char in "0123456789abcdef" for char in value)


def at(value, path):
    for part in path:
        value = value[part]
    return value


def _failure(error):
    return {"type": type(error).__name__, "code": str(error)}


def _bound_source(raw, binding):
    if not isinstance(raw, bytes) or sha(raw) != binding["sha256"]:
        raise FollowupResultsError("source_bytes_hash_mismatch")
    value = wire.parse_json(raw)
    if "schema" in binding and value.get("schema") != binding["schema"]:
        raise FollowupResultsError("source_schema_mismatch")
    return value


def _instrument_files(policy):
    result = {}
    for name, expected in policy["instrument_files_sha256"].items():
        path = (frontier.ROOT / name).resolve()
        path.relative_to(frontier.ROOT.resolve())
        if sha(path.read_bytes()) != expected:
            raise FollowupResultsError("instrument_source_hash_mismatch")
        result[name] = expected
    if not result:
        raise FollowupResultsError("instrument_source_bindings_required")
    dependencies = {**frontier.dependencies(), **followup.dependencies()}
    if any(result.get(name) != value for name, value in dependencies.items()):
        raise FollowupResultsError("instrument_dependency_binding_missing")
    return result


def _frontier(row, raw, model, references, limits, options):
    content = wire.parse_json(raw)
    frontier.codec.validate_json_resources(content, limits)
    packet = at(row, options["base_packet_path"])
    before = digest(packet)
    reference = at(references, [at(row, options["reference_id_path"])])
    response = {"schema": options["recorded_response_schema"],
                "request_sha256": row["request_sha256"], "input_sha256": row["input_sha256"],
                "response_origin": options["recorded_origin"], "captured_model": model,
                "content": content, "content_sha256": frontier.digest(content)}
    scored = frontier.score_response(row, response, reference)
    if digest(packet) != before:
        raise FollowupResultsError("instrument_mutated_input")
    result = {"mechanical_validity": scored["mechanical_validity"], "instrument_report": scored,
              "reference_agreement": scored.get("reference_agreement"),
              "reference_agreement_is_semantic_judgement": False,
              "base_packet_sha256": before, "model_semantic_quality": None}
    if "instrument_origin" in scored and "diff" in content:
        diff = deepcopy(content["diff"])
        diff["origin"] = deepcopy(scored["instrument_origin"])
        preview = frontier.codec.preview_diff(packet, diff)
        result.update(raw_proposed_diff=deepcopy(content["diff"]), bound_diff=diff,
                      candidate_packet=preview["candidate_packet"], changes=preview["changes"])
    return result


def _graph(row, raw, model, limits, options, compiler, operation_id, response_index, choice_index):
    packet = at(row, options["base_packet_path"])
    before = digest(packet)
    arm = at(row, options["arm_path"])
    parsed = wire.parse_json(raw)
    if not isinstance(parsed, dict) or parsed.get(options["schema_field"]) != arm[options["wire_schema_field"]]:
        raise FollowupResultsError("wire_schema_mismatch")
    transform = options["representations"].get(arm[options["representation_field"]])
    if transform is None:
        raise FollowupResultsError("representation_not_declared")
    compiler_raw, declared_text = raw, None
    loss, augmentation = [], []
    if transform["operation"] == "omit_field_and_rename_schema":
        components = arm.get(options["components_field"], [])
        declared = [part for part in components if part.get("method_id") == transform["component_id"]]
        if len(declared) != 1 or declared[0].get("parameters") != transform["required_parameters"]:
            raise FollowupResultsError("text_projection_not_declared_in_arm")
        text_field = transform["omitted_field"]
        declared_text = parsed[text_field]
        if not isinstance(declared_text, str):
            raise FollowupResultsError("public_text_not_string")
        projected = deepcopy(parsed)
        del projected[text_field]
        projected[options["schema_field"]] = arm[options["compiler_schema_field"]]
        compiler_raw = wire.canonical(projected)
        loss = [{"kind": "field_omitted_from_compiler_projection", "field": text_field,
                 "original_retained_in": "first_content_utf8"}]
        augmentation = [{"kind": "declared_schema_projection", "original": parsed[options["schema_field"]],
                         "projected": projected[options["schema_field"]]}]
    elif transform["operation"] != "identity":
        raise FollowupResultsError("representation_operation_unavailable")
    if parsed[options["base_hash_field"]] != at(packet, options["packet_identity_path"]):
        raise FollowupResultsError("reply_base_packet_binding_mismatch")
    host = {"request_id": operation_id, "turn_id": operation_id + ":response:" + str(response_index) + ":choice:" + str(choice_index),
            "model": model, "actor": options["actor"],
            "recipe_sha256": sha(at(row, options["recipe_path"]).encode("utf-8"))}
    compiled = compiler.compile_reply(packet, compiler_raw, **host, resource_limits=limits)
    if declared_text is not None and declared_text != compiled["response_text"]:
        raise FollowupResultsError("text_annotation_mismatch")
    preview = frontier.codec.preview_diff(packet, compiled["diff"])
    if digest(packet) != before:
        raise FollowupResultsError("instrument_mutated_input")
    return {"mechanical_validity": True, "base_packet_sha256": before,
            "base_packet_identity": at(packet, options["packet_identity_path"]),
            "compilation": compiled, "candidate_packet": preview["candidate_packet"],
            "changes": preview["changes"], "spans": compiled["spans"],
            "response_text": compiled["response_text"], "response_text_sha256": sha(compiled["response_text"].encode("utf-8")),
            "compiler_input_sha256": sha(compiler_raw), "compiler_input_is_original_content": compiler_raw == raw,
            "transformation_report": {"loss": loss, "augmentation": augmentation,
                                      "exact_original_content_retained": True},
            "model_semantic_quality": None}


def evaluate(normalized_raw, source_manifest_raw, config_raw, policy, *, references_raw=None):
    """Evaluate all planned slots and alternatives; malformed choices stay local.

    The policy pins raw source bytes and local evaluator files before evaluation.
    A body/response binding failure invalidates its slot while retaining every
    original request, response, choice and billing/resource field for inspection.
    """
    policy = wire.parse_json(wire.canonical(policy))
    adapter_sha256 = sha(Path(__file__).read_bytes())
    if policy.get("schema") != "loom.programme_followup_results_policy/1":
        raise FollowupResultsError("policy_schema_mismatch")
    if not isinstance(normalized_raw, bytes):
        raise FollowupResultsError("normalized_bytes_required")
    normalized = wire.parse_json(normalized_raw)
    if normalized.get("schema") != policy["normalized_schema"]:
        raise FollowupResultsError("normalized_schema_mismatch")
    source = _bound_source(source_manifest_raw, policy["sources"]["manifest"])
    config = _bound_source(config_raw, policy["sources"]["config"])
    references = (_bound_source(references_raw, policy["sources"]["references"])
                  if "references" in policy["sources"] else None)
    pins = _instrument_files(policy)
    options = policy["evaluator"]
    kind = options["kind"]
    compiler = None
    if kind == "graph_reply":
        _, compiler, _, _ = followup._sources(config)
    elif kind != "frontier_comparison":
        raise FollowupResultsError("evaluator_kind_unavailable")
    limits = at(config, options["resource_limits_path"])
    planned = at(source, policy["prepared"]["requests_path"])
    if not isinstance(planned, list):
        raise FollowupResultsError("planned_inventory_not_array")
    planned_ids = [at(row, policy["prepared"]["id_path"]) for row in planned]
    if any(not isinstance(i, str) or not i for i in planned_ids) or len(set(planned_ids)) != len(planned_ids):
        raise FollowupResultsError("duplicate_or_missing_planned_identity")
    requests, responses, billing = (normalized[name] for name in ("requests", "responses", "rows"))
    if any(not isinstance(value, list) for value in (requests, responses, billing)):
        raise FollowupResultsError("normalized_inventory_not_array")
    integrity_errors = []
    normalized_ids = normalized.get("planned_operation_ids", [])
    request_ids = [row.get("operation_id") for row in requests if isinstance(row, dict)]
    duplicate_operation_ids = {value for value in request_ids if isinstance(value, str) and request_ids.count(value) > 1}
    if (normalized.get("planned_operations") != len(planned) or len(requests) != len(planned)
            or not isinstance(normalized_ids, list) or len(normalized_ids) != len(planned)
            or len(normalized_ids) != len(set(normalized_ids))
            or set(normalized_ids) != set(request_ids)):
        integrity_errors.append("normalized_planned_denominator_drift")
    if any(not isinstance(value, str) or not value for value in request_ids):
        integrity_errors.append("invalid_operation_identity")
    if duplicate_operation_ids:
        integrity_errors.append("duplicate_physical_operation_identity")
    mapped, unused_requests = {}, []
    for request in requests:
        prepared_id = request.get("metadata", {}).get("prepared_request_id") if isinstance(request, dict) else None
        if prepared_id not in planned_ids:
            unused_requests.append(deepcopy(request))
        else:
            mapped.setdefault(prepared_id, []).append(request)
    if unused_requests:
        integrity_errors.append("unmapped_requests")
    result_rows, used_response_indices = [], set()
    for prepared in planned:
        prepared_id = at(prepared, policy["prepared"]["id_path"])
        supplied = mapped.get(prepared_id, [])
        operation_ids = [row.get("operation_id") for row in supplied]
        errors, alternatives, retained_responses = [], [], []
        body = at(prepared, policy["prepared"]["body_path"])
        expected_body_hash = digest(body)
        expected_row_hash = digest({k: v for k, v in prepared.items() if k != policy["prepared"]["row_digest_field"]})
        if prepared.get(policy["prepared"]["row_digest_field"]) != expected_row_hash:
            errors.append("prepared_row_hash_mismatch")
        if len(supplied) != 1:
            errors.append("missing_planned_request" if not supplied else "duplicate_prepared_request_mapping")
        if any(operation_id in duplicate_operation_ids for operation_id in operation_ids):
            errors.append("duplicate_physical_operation_identity")
        if any(not isinstance(operation_id, str) or not operation_id for operation_id in operation_ids):
            errors.append("invalid_operation_identity")
        for request in supplied:
            if (request.get("request_sha256") != expected_body_hash or request.get("body") != body
                    or digest(request.get("body")) != expected_body_hash
                    or request.get("metadata", {}).get("source_manifest_sha256") != sha(source_manifest_raw)):
                errors.append("request_body_or_source_binding_mismatch")
        matched = [(i, response) for i, response in enumerate(responses)
                   if isinstance(response, dict) and response.get("operation_id") in operation_ids]
        if not matched:
            errors.append("missing_planned_response")
        if len(matched) > 1:
            errors.append("duplicate_operation_response")
        billing_rows = [row for row in billing if isinstance(row, dict) and row.get("operation_id") in operation_ids]
        if len(billing_rows) > 1:
            errors.append("duplicate_billing_row")
        for response_index, response in matched:
            used_response_indices.add(response_index)
            retained_responses.append(deepcopy(response))
            projection = response.get("projection")
            response_errors = []
            if not isinstance(projection, dict):
                errors.append("response_projection_unavailable")
                continue
            if any(field in projection for field in policy["content"]["disallowed_projection_fields"]):
                response_errors.append("ambiguous_content_envelope")
            bound_rows = [row for row in billing_rows if row.get("response_sha256") == response.get("response_sha256")]
            if len(bound_rows) != 1 or not _sha256(response.get("response_sha256")):
                response_errors.append("response_row_hash_binding_mismatch")
            elif (bound_rows[0].get("request_sha256") != expected_body_hash
                  or not _sha256(normalized.get("manifest_sha256"))
                  or bound_rows[0].get("manifest_sha256") != normalized.get("manifest_sha256")):
                response_errors.append("response_request_or_manifest_binding_mismatch")
            model = projection.get(policy["content"]["model_field"])
            if not isinstance(model, str) or not model:
                model = bound_rows[0].get("response_model") if len(bound_rows) == 1 else None
            if not isinstance(model, str) or not model:
                response_errors.append("observed_model_identity_missing")
            choices = projection.get(policy["content"]["choices_field"])
            if not isinstance(choices, list) or not choices:
                errors.append("missing_or_invalid_choices")
                continue
            for choice_index, choice in enumerate(choices):
                result = {"response_index": response_index, "choice_index": choice_index,
                          "choice": deepcopy(choice), "mechanical_validity": False,
                          "failure": None, "binding_errors": list(response_errors),
                          "first_content_utf8": None, "first_content_sha256": None,
                          "semantic_accuracy": None}
                try:
                    content = at(choice, policy["content"]["text_path"])
                    if not isinstance(content, str):
                        raise FollowupResultsError("ambiguous_or_nonstring_content")
                    raw = content.encode("utf-8", errors="strict")
                    result.update(first_content_utf8=content, first_content_sha256=sha(raw),
                                  first_content_byte_len=len(raw), first_content_hex=raw.hex())
                    if errors or response_errors:
                        raise FollowupResultsError("input_or_response_binding_invalid")
                    if kind == "frontier_comparison":
                        evaluated = _frontier(deepcopy(prepared), raw, model, references, limits, options)
                    else:
                        evaluated = _graph(deepcopy(prepared), raw, model, limits, options, compiler,
                                           response["operation_id"], response_index, choice_index)
                    result.update(evaluated)
                except Exception as error:
                    result["failure"] = _failure(error)
                alternatives.append(result)
        hashes = [choice["first_content_sha256"] for choice in alternatives if choice["first_content_sha256"] is not None]
        counts = {key: hashes.count(key) for key in set(hashes)}
        valid = bool(alternatives) and not errors and all(choice["mechanical_validity"] for choice in alternatives)
        result_rows.append({"prepared_request_id": prepared_id, "operation_ids": operation_ids,
                            "expected_request_body_sha256": expected_body_hash, "planned_row_sha256": expected_row_hash,
                            "input_requests": deepcopy(supplied), "responses": retained_responses,
                            "billing_and_resource_rows": deepcopy(billing_rows), "errors": errors,
                            "billing_or_identity_verification_performed_here": False,
                            "choices": alternatives, "choice_count": len(alternatives),
                            "repeated_content_sha256_counts": {key: count for key, count in counts.items() if count > 1},
                            "all_choices_mechanically_valid": valid, "semantic_accuracy": None})
    unused_responses = [deepcopy(response) for i, response in enumerate(responses) if i not in used_response_indices]
    if unused_responses:
        integrity_errors.append("unmapped_responses")
    if _instrument_files(policy) != pins:
        raise FollowupResultsError("instrument_source_changed_during_evaluation")
    if sha(Path(__file__).read_bytes()) != adapter_sha256:
        raise FollowupResultsError("adapter_source_changed_during_evaluation")
    return {"schema": "loom.programme_followup_results/1", "normalized_sha256": sha(normalized_raw),
            "source_manifest_sha256": sha(source_manifest_raw), "config_sha256": sha(config_raw),
            "references_sha256": sha(references_raw) if references_raw is not None else None,
            "policy_sha256": digest(policy), "instrument_files_sha256": pins,
            "effective_policy": deepcopy(policy), "adapter_sha256": adapter_sha256,
            "evaluator_kind": kind, "planned_requests": len(planned), "rows": result_rows,
            "locally_all_choices_valid_requests": sum(row["all_choices_mechanically_valid"] for row in result_rows),
            "all_choices_valid_requests": 0 if integrity_errors else sum(row["all_choices_mechanically_valid"] for row in result_rows),
            "input_integrity_valid": not integrity_errors,
            "evaluator_backend": options["backend"], "native_cpp_execution": False,
            "compiler_source_bindings": deepcopy(config.get("sources")) if compiler is not None else None,
            "input_integrity_errors": integrity_errors, "unmapped_requests": unused_requests,
            "unmapped_responses": unused_responses, "input_planned_operation_ids": deepcopy(normalized_ids),
            "network_calls": 0, "paid_calls": 0, "canonical_store_written": False,
            "semantic_accuracy": None, "winner": None,
            "billing_or_identity_verification_performed_here": False,
            "projection_boundary": "Exact decoded public first-content UTF-8 is retained; the original HTTP envelope is hash-bound but is not reconstructed or claimed here."}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("normalized", "source-manifest", "config", "policy", "output"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--references")
    args = parser.parse_args(argv)
    result = evaluate(Path(args.normalized).read_bytes(), Path(args.source_manifest).read_bytes(),
                      Path(args.config).read_bytes(), wire.parse_json(Path(args.policy).read_bytes()),
                      references_raw=Path(args.references).read_bytes() if args.references else None)
    encoded = serialize_report(result)
    with Path(args.output).open("xb") as output:
        output.write(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
