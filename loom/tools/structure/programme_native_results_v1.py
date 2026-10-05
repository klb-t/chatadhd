"""Source-bound native validation of public, normalized first responses.

No account/private capture access, inference, graph compilation or semantic
judgement. Every alternative is sent unchanged to the configured native tool.
Injected callbacks are explicitly lower evidence and never prove native use.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import importlib
import json
import math
from pathlib import Path
import subprocess
import time

try:
    from . import openrouter_runner as wire
except ImportError:
    import openrouter_runner as wire


class NativeResultsError(ValueError):
    pass


def canonical(value):
    return wire.canonical(value)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def digest(value):
    return sha(canonical(value))


def parse(raw):
    return wire.parse_json(raw)


def read(path):
    return parse(Path(path).read_bytes())


def path(value, parts):
    for part in parts:
        value = value[part]
    return value


def same(left, right):
    return canonical(left) == canonical(right)


def aggregate(values):
    return False if any(value is False for value in values) else None if any(value is None for value in values) else True


def check_schema(value, schema):
    """Small universal schema vocabulary; all envelope constants are data."""
    if set(schema) - {"type", "const", "required", "properties", "additionalProperties", "items"}:
        raise NativeResultsError("unsupported_policy_schema")
    kinds = {"object": dict, "array": list, "string": str, "integer": int, "boolean": bool, "null": type(None)}
    if "type" in schema and (schema["type"] not in kinds or type(value) is not kinds[schema["type"]]):
        raise NativeResultsError("envelope_schema_mismatch")
    if "const" in schema and not same(value, schema["const"]):
        raise NativeResultsError("envelope_schema_mismatch")
    if isinstance(value, dict):
        properties = schema.get("properties", {})
        if not set(schema.get("required", [])) <= set(value) or (schema.get("additionalProperties") is False and set(value) - set(properties)):
            raise NativeResultsError("envelope_schema_mismatch")
        for name, child in properties.items():
            if name in value:
                check_schema(value[name], child)
    if isinstance(value, list) and "items" in schema:
        for child in value:
            check_schema(child, schema["items"])


class NativeValidator:
    """Exact existing packet/bundle/vocabulary stdin API, with no shell."""
    def __init__(self, options):
        if not isinstance(options["executable"], str) or not options["executable"]:
            raise NativeResultsError("native_binary_not_configured")
        executable = Path(options["executable"]).resolve()
        if not executable.is_file():
            raise NativeResultsError("native_binary_missing")
        timeout = options["timeout_seconds"]
        if type(timeout) not in (int, float) or not math.isfinite(timeout) or timeout <= 0:
            raise NativeResultsError("native_timeout_invalid")
        binary_hash = sha(executable.read_bytes())
        expected = options.get("expected_binary_sha256")
        if expected is not None and expected != binary_hash:
            raise NativeResultsError("native_binary_hash_mismatch")
        self.executable, self.timeout = executable, timeout
        self.bound_files = {str(executable): binary_hash}
        for filename in options["provenance_files"]:
            filename = str(Path(filename).resolve())
            self.bound_files[filename] = sha(Path(filename).read_bytes())
        self.provenance = {"backend": "native_binary", "native_execution": True,
                           "binary": str(executable), "binary_sha256": binary_hash,
                           "bound_file_sha256": deepcopy(self.bound_files), "timeout_seconds": timeout}

    def __call__(self, payload):
        if any(sha(Path(name).read_bytes()) != expected for name, expected in self.bound_files.items()):
            raise NativeResultsError("native_binary_or_provenance_changed")
        try:
            process = subprocess.run([str(self.executable)], input=payload, capture_output=True, timeout=self.timeout)
        except subprocess.TimeoutExpired as error:
            capture = {"returncode": None, "stdout": error.stdout or b"", "stderr": error.stderr or b"", "error": "native_timeout"}
        else:
            capture = {"returncode": process.returncode, "stdout": process.stdout, "stderr": process.stderr, "error": None}
        if any(sha(Path(name).read_bytes()) != expected for name, expected in self.bound_files.items()):
            capture["error"] = "native_binary_or_provenance_changed"
        capture["executed"] = True
        return capture


def audit_native_report(report, packet, bundle, vocabulary, policy):
    """Check transport/report identity; delegate candidate semantics to C++."""
    options = policy["native_report"]
    if not isinstance(report, dict) or type(report.get("valid")) is not bool:
        raise NativeResultsError("native_report_shape")
    accepted = report["valid"]
    common = set(options["common_fields"])
    if accepted:
        check_schema(report["coverage"], options["accepted_coverage_schema"])
        expected = common | set(options["accepted_fields"])
        if set(report) != expected:
            raise NativeResultsError("native_report_shape")
    elif not common <= set(report) or set(report) - common - set(options["rejected_optional_fields"]):
        raise NativeResultsError("native_report_shape")
    if (report["version"] != options["version"] or report["hash_algorithm"] != options["hash_algorithm"]
            or report["status"] != options["accepted_status" if accepted else "rejected_status"]
            or report["no_inference"] is not True or report["no_persistence"] is not True
            or not isinstance(report["coverage"], dict) or report["coverage"].get("semantic_accuracy", "missing") is not None):
        raise NativeResultsError("native_report_contract")
    errors = report["errors"]
    if (not isinstance(errors, list) or any(not isinstance(error, dict) or set(error) != {"code", "path", "message"}
                                          or any(not isinstance(value, str) for value in error.values()) for error in errors)
            or (accepted and errors) or (not accepted and not errors)):
        raise NativeResultsError("native_report_errors")
    retained = {"bundle": bundle, "source_packet": packet}
    if not same(report["retained_input"], retained):
        if accepted or report["retained_input"] is not None or not same(report.get("retention"), options["retention_exception"]):
            raise NativeResultsError("native_retained_input_drift")
    elif "retention" in report:
        raise NativeResultsError("native_retention_contract")
    if accepted:
        if (report["packet_hash"] != digest(packet) or report["vocabulary_hash"] != digest(vocabulary)
                or not same(report["drafts"], {"entities": bundle["entity_drafts"], "claims": bundle["claim_drafts"]})):
            raise NativeResultsError("native_hash_or_draft_drift")
        for binding in options["accepted_equal_bindings"]:
            if not same(path(report, binding["report_path"]), path({"packet": packet, "bundle": bundle, "vocabulary": vocabulary}[binding["input"]], binding["input_path"])):
                raise NativeResultsError("native_coverage_binding_drift")
        for binding in options["accepted_derived_bindings"]:
            value = path({"packet": packet, "bundle": bundle, "vocabulary": vocabulary}[binding["input"]], binding["input_path"])
            if binding["operation"] == "length":
                expected_value = len(value)
            elif binding["operation"] == "sum_utf8_bytes":
                expected_value = sum(len(path(item, binding["item_path"]).encode("utf-8")) for item in value)
            else:
                raise NativeResultsError("unsupported_policy_binding_operation")
            if not same(path(report, binding["report_path"]), expected_value):
                raise NativeResultsError("native_coverage_binding_drift")
    elif report["packet_hash"] is not None or not same(report["drafts"], {"entities": [], "claims": []}):
        raise NativeResultsError("native_rejection_contract")
    elif not same(report["coverage"], options["rejected_coverage"]):
        raise NativeResultsError("native_rejection_contract")
    return accepted


def native_alternative(packet, bundle, vocabulary, validator, policy, index, native_execution):
    stdin = canonical({"packet": packet, "bundle": bundle, "vocabulary": vocabulary})
    result = {"bundle_index": index, "bundle": deepcopy(bundle), "bundle_sha256": digest(bundle),
              "native_stdin_sha256": sha(stdin), "native_execution": False,
              "evidence_class": "native_binary" if native_execution else "callback_test_only",
              "native_verified_valid": False,
              "mechanically_valid": False, "native_report": None, "failure": None, "semantic_accuracy": None}
    started = time.perf_counter_ns()
    try:
        capture = validator(stdin)
        result["native_execution"] = native_execution and capture.get("executed") is True
        stdout, stderr = capture["stdout"], capture["stderr"]
        if not isinstance(stdout, bytes) or not isinstance(stderr, bytes):
            raise NativeResultsError("validator_capture_not_bytes")
        result.update(returncode=capture["returncode"], stdout_sha256=sha(stdout), stderr_sha256=sha(stderr),
                      stdout_utf8=stdout.decode("utf-8", errors="replace"), stderr_utf8=stderr.decode("utf-8", errors="replace"),
                      stdout_hex=stdout.hex(), stderr_hex=stderr.hex())
        if capture.get("error") or capture["returncode"] != 0:
            raise NativeResultsError("native_transport_failure")
        report = parse(stdout)
        result["native_report"] = deepcopy(report)
        result["mechanically_valid"] = audit_native_report(report, packet, bundle, vocabulary, policy)
        result["native_verified_valid"] = result["native_execution"] and result["mechanically_valid"]
        if not result["mechanically_valid"]:
            result["failure"] = "native_candidate_rejected"
    except Exception as error:
        result["failure"] = str(error) if isinstance(error, NativeResultsError) else type(error).__name__
    result["wrapper_latency_ms"] = (time.perf_counter_ns() - started) / 1e6
    return result


def _index(rows, key):
    result = {}
    if not isinstance(rows, list):
        raise NativeResultsError("input_inventory_not_array")
    for row in rows:
        value = key(row)
        if not isinstance(value, str) or not value or value in result:
            raise NativeResultsError("duplicate_or_missing_input_identity")
        result[value] = row
    return result


def evaluate(normalized, prepared, source_inputs, vocabulary, policy, *, validator=None, callback_id=None, review_reference=None):
    """All planned slots/first contents survive; no semantic selection occurs."""
    # Detach supplied objects before invoking an arbitrary injected test callback.
    normalized, prepared, source_inputs, vocabulary, policy = (parse(canonical(value)) for value in
                                                             (normalized, prepared, source_inputs, vocabulary, policy))
    if review_reference is not None:
        review_reference = parse(canonical(review_reference))
    if policy.get("schema") != "loom.programme_native_results_policy/1":
        raise NativeResultsError("policy_schema")
    for name, value in (("normalized", normalized), ("prepared", prepared), ("source_inputs", source_inputs)):
        if value.get("schema") != policy["input_schemas"][name]:
            raise NativeResultsError("input_schema")
    pp, sp = policy["prepared"], policy["source_inputs"]
    plans = _index(path(prepared, pp["requests_path"]), lambda row: path(row, pp["request_id_path"]))
    if type(path(prepared, pp["planned_count_path"])) is not int or path(prepared, pp["planned_count_path"]) != len(plans):
        raise NativeResultsError("prepared_planned_denominator_drift")
    sources = _index(path(source_inputs, sp["cases_path"]), lambda row: path(row, sp["id_path"]))
    requests = _index(normalized["requests"], lambda row: row["operation_id"])
    planned_ids = normalized["planned_operation_ids"]
    if (not isinstance(planned_ids, list) or len(planned_ids) != len(set(planned_ids)) or set(planned_ids) != set(requests)
            or type(normalized["planned_operations"]) is not int or normalized["planned_operations"] != len(plans) or len(requests) != len(plans)):
        raise NativeResultsError("normalized_planned_denominator_drift")
    rows = _index(normalized["rows"], lambda row: row["operation_id"])
    responses = _index(normalized["responses"], lambda row: row["operation_id"])
    if not set(rows) <= set(requests) or not set(responses) <= set(requests):
        raise NativeResultsError("unplanned_row_or_response")
    mapped = {}
    for operation_id, request in requests.items():
        prepared_id = request["metadata"]["prepared_request_id"]
        if prepared_id not in plans or prepared_id in mapped:
            raise NativeResultsError("prepared_request_mapping_drift")
        mapped[prepared_id] = operation_id
    if set(mapped) != set(plans):
        raise NativeResultsError("prepared_request_grid_drift")
    if validator is None:
        validator = NativeValidator(policy["validator"])
        provenance = deepcopy(validator.provenance)
    elif type(validator) is NativeValidator:
        configured = NativeValidator(policy["validator"])
        if not same(validator.provenance, configured.provenance):
            raise NativeResultsError("native_validator_policy_binding_drift")
        # Supplied instances are mutable; only a freshly policy-bound invoker
        # may dispatch. Its captured binary is the binary named in provenance.
        validator = configured
        provenance = deepcopy(validator.provenance)
    else:
        if not isinstance(callback_id, str) or not callback_id:
            raise NativeResultsError("callback_identity_required")
        provenance = {"backend": "injected_callback", "native_execution": False, "callback_id": callback_id}
    receipts, skeleton = [], []
    refs = _index((review_reference or {}).get("cases", []), lambda case: case["case_id"])
    reference_validation = None
    if review_reference is not None:
        reference_policy = policy["manual_review"]["reference"]
        if review_reference.get("schema") != reference_policy["schema"] or set(refs) != set(sources):
            raise NativeResultsError("review_reference_inventory_drift")
        criterion_ids = set()
        for case_id, reference_case in refs.items():
            if path(reference_case, reference_policy["packet_hash_path"]) != path(sources[case_id], sp["packet_hash_path"]):
                raise NativeResultsError("review_reference_source_drift")
            for criterion in reference_case["criteria"]:
                criterion_id = path(criterion, policy["manual_review"]["criterion_id_path"])
                if not isinstance(criterion_id, str) or not criterion_id or criterion_id in criterion_ids:
                    raise NativeResultsError("review_reference_criterion_identity")
                criterion_ids.add(criterion_id)
        configured = reference_policy["validator"]
        module = importlib.import_module((__package__ + "." if __package__ else "") + configured["module"])
        reference_source = Path(module.__file__)
        reference_source_hash = sha(reference_source.read_bytes())
        result = getattr(module, configured["function"])(deepcopy(review_reference), deepcopy(source_inputs))
        if sha(reference_source.read_bytes()) != reference_source_hash:
            raise NativeResultsError("reference_validator_source_changed")
        reference_validation = {"module": configured["module"], "function": configured["function"],
                                "source_sha256": reference_source_hash, "result": result,
                                "semantic_verdict": None}
    for prepared_id, plan in plans.items():
        operation_id = mapped[prepared_id]
        request, row, response = requests[operation_id], rows.get(operation_id), responses.get(operation_id)
        body = path(plan, pp["body_path"])
        case_id = path(plan, pp["case_id_path"])
        source = sources.get(case_id)
        if source is None:
            raise NativeResultsError("planned_source_missing")
        packet, packet_hash = path(source, sp["packet_path"]), path(source, sp["packet_hash_path"])
        request_hash = path(plan, pp["request_hash_path"])
        rp = policy["request_packet"]
        packet_messages = [item for item in path(body, rp["items_path"]) if all(item.get(key) == value for key, value in rp["item_filter"].items())]
        if len(packet_messages) != 1:
            raise NativeResultsError("request_packet_content_ambiguous")
        request_content = parse(path(packet_messages[0], rp["content_path"]).encode("utf-8"))
        if (set(request_content) != set(rp["exact_fields"]) or not same(path(request_content, rp["packet_path"]), packet)
                or path(request_content, rp["packet_hash_path"]) != packet_hash or digest(packet) != packet_hash
                or path(plan, pp["packet_hash_path"]) != packet_hash or digest(body) != request_hash
                or not same(request["body"], body) or request["request_sha256"] != request_hash
                or request["metadata"]["arm_id"] != path(plan, pp["method_id_path"])):
            raise NativeResultsError("request_source_binding_drift")
        if row is not None and row["request_sha256"] != request_hash:
            raise NativeResultsError("row_request_binding_drift")
        if response is not None and (row is None or row["response_sha256"] != response["response_sha256"]):
            raise NativeResultsError("first_response_binding_drift")
        if response is not None and (type(response["response_sha256"]) is not str or len(response["response_sha256"]) != 64
                                     or set(response["response_sha256"]) - set("0123456789abcdef")):
            raise NativeResultsError("first_response_hash_invalid")
        receipt = {"operation_id": operation_id, "request_id": prepared_id, "case_id": case_id,
                   "method_id": path(plan, pp["method_id_path"]), "request_hash": request_hash, "packet_hash": packet_hash,
                   "source_packet": deepcopy(packet), "transport_state": row.get("state") if row else "not_attempted",
                   "transport_completed": row.get("completed") if row else False,
                   "billing_replay_verified": row.get("billing_replay_verified") if row else False,
                   "first_response_sha256": response["response_sha256"] if response else None,
                   "input_response_ledger_bound": row.get("response_ledger_bound") if row else None,
                   "private_capture_independently_read": False, "status": "missing_response", "choices": [],
                   "native_contract_valid": None, "validator_contract_valid": None, "native_verified_valid": False,
                   "evidence_class": "native_binary" if provenance["native_execution"] else "callback_test_only",
                   "response_projection": deepcopy(response["projection"]) if response else None,
                   "semantic_accuracy": None, "all_alternatives_preserved": True}
        if response is not None:
            try:
                choices = path(response, policy["response"]["choices_path"])
                if not isinstance(choices, list) or not choices:
                    raise NativeResultsError("missing_response_choices")
                for choice_index, choice in enumerate(choices):
                    choice_receipt = {"choice_index": choice_index, "finish_reason": choice.get("finish_reason") if isinstance(choice, dict) else None,
                                      "content": None, "content_sha256": None, "envelope_valid": False,
                                      "failure": None, "alternatives": [], "native_contract_valid": None, "validator_contract_valid": None}
                    receipt["choices"].append(choice_receipt)
                    try:
                        content = path(choice, policy["response"]["content_path"])
                        choice_receipt["content"] = deepcopy(content)
                        if not isinstance(content, str):
                            raise NativeResultsError("response_content_not_string")
                        choice_receipt["content_sha256"] = sha(content.encode("utf-8"))
                        envelope = parse(content.encode("utf-8"))
                        check_schema(envelope, policy["envelope_schema"])
                        if path(envelope, policy["envelope_packet_hash_path"]) != packet_hash:
                            raise NativeResultsError("response_packet_hash_drift")
                        bundles = path(envelope, policy["envelope_bundles_path"])
                        maximum = policy["maximum_bundles"]
                        if maximum is not None and (type(maximum) is not int or maximum < 0 or len(bundles) > maximum):
                            raise NativeResultsError("configured_alternative_budget_exceeded")
                        choice_receipt["envelope_valid"] = True
                        for index, bundle in enumerate(bundles):
                            choice_receipt["alternatives"].append(native_alternative(packet, bundle, vocabulary, validator, policy, index, provenance["native_execution"]))
                        if bundles:
                            choice_receipt["validator_contract_valid"] = all(item["mechanically_valid"] for item in choice_receipt["alternatives"])
                            if provenance["native_execution"]:
                                choice_receipt["native_contract_valid"] = all(item["native_verified_valid"] for item in choice_receipt["alternatives"])
                    except Exception as error:
                        choice_receipt["failure"] = str(error) if isinstance(error, NativeResultsError) else type(error).__name__
                        choice_receipt["validator_contract_valid"] = False
                        if provenance["native_execution"]:
                            choice_receipt["native_contract_valid"] = False
                receipt["native_contract_valid"] = (aggregate([choice["native_contract_valid"] for choice in receipt["choices"]])
                                                     if provenance["native_execution"] and any(choice["alternatives"] for choice in receipt["choices"]) else None)
                receipt["validator_contract_valid"] = (aggregate([choice["validator_contract_valid"] for choice in receipt["choices"]])
                                                     if any(choice["alternatives"] for choice in receipt["choices"]) else None)
                receipt["native_verified_valid"] = (receipt["native_contract_valid"] is True and
                    all(alternative["native_verified_valid"] for choice in receipt["choices"] for alternative in choice["alternatives"]))
                receipt["status"] = ("mechanically_valid" if receipt["validator_contract_valid"] is True else
                                     "mechanically_rejected" if receipt["validator_contract_valid"] is False else
                                     "partial_abstention" if any(choice["alternatives"] for choice in receipt["choices"]) and all(choice["envelope_valid"] for choice in receipt["choices"]) else
                                     "envelope_abstention" if all(choice["envelope_valid"] for choice in receipt["choices"]) else "invalid_response")
            except Exception as error:
                receipt.update(status="invalid_response", failure=str(error) if isinstance(error, NativeResultsError) else type(error).__name__)
        receipts.append(receipt)
        alternatives = [{"bundle_index": global_index, "choice_index": choice["choice_index"], "choice_bundle_index": alternative["bundle_index"],
                         "content_sha256": choice["content_sha256"], "bundle_sha256": alternative["bundle_sha256"],
                         "receipt_pointer": "/choices/" + str(choice["choice_index"]) + "/alternatives/" + str(alternative["bundle_index"]) + "/bundle",
                         "content_envelope_pointer": "/bundles/" + str(alternative["bundle_index"])}
                        for global_index, (choice, alternative) in enumerate((choice, alternative) for choice in receipt["choices"] for alternative in choice["alternatives"])]
        criteria = [{"criterion_id": path(criterion, policy["manual_review"]["criterion_id_path"]),
                     "bundle_judgements": [{"bundle_index": alternative["bundle_index"], "judgement": None, "rationale": None,
                                            "candidate_pointers": [], "source_support": []} for alternative in alternatives]}
                    for criterion in refs.get(case_id, {}).get("criteria", [])]
        skeleton.append({"request_id": prepared_id, "request_hash": request_hash, "response_sha256": receipt["first_response_sha256"],
                         "native_contract_valid": receipt["native_contract_valid"] if len(receipt["choices"]) == 1 else None,
                         "mechanical_native_contract_valid": receipt["native_contract_valid"], "bundle_count": len(alternatives),
                         "reviewer": None, "criteria": criteria, "alternatives": alternatives,
                         "single_envelope_review_compatible": len(receipt["choices"]) == 1,
                         "semantic_review_status": "unreviewed"})
    return {"schema": "loom.programme_native_results/1", "stage_id": normalized["stage_id"],
            "planned_requests": len(plans), "receipts": receipts,
            "counts": {"planned": len(plans), "responses": sum(receipt["first_response_sha256"] is not None for receipt in receipts),
                       "mechanically_valid": sum(receipt["validator_contract_valid"] is True for receipt in receipts),
                       "mechanically_rejected": sum(receipt["validator_contract_valid"] is False for receipt in receipts),
                       "validator_alternative_attempts": sum(len(choice["alternatives"]) for receipt in receipts for choice in receipt["choices"]),
                       "native_bundle_invocations": sum(alternative["native_execution"] for receipt in receipts for choice in receipt["choices"] for alternative in choice["alternatives"]),
                       "native_execution_captures": sum(alternative["native_execution"] for receipt in receipts for choice in receipt["choices"] for alternative in choice["alternatives"]),
                       "native_verified_valid": sum(receipt["native_verified_valid"] for receipt in receipts),
                       "missing_responses": sum(receipt["status"] == "missing_response" for receipt in receipts)},
            "source_hashes": {"normalized_canonical": digest(normalized), "prepared_canonical": digest(prepared),
                              "source_inputs_canonical": digest(source_inputs), "vocabulary_canonical": digest(vocabulary),
                              "policy_canonical": digest(policy), "adapter": sha(Path(__file__).read_bytes())},
            "validator": provenance, "effective_policy": deepcopy(policy), "new_model_calls": 0, "semantic_accuracy": None,
            "boundary": "Native structural/source-span validation is distinct from Python parsing/hash binding, transport/billing and manual source interpretation. No semantic verdict, repairs, selection, persistence or private capture verification.",
            "manual_review_skeleton": {"schema": policy["manual_review"]["schema"], "draft": True,
                                       "reference_canonical_sha256": digest(review_reference) if review_reference is not None else None,
                                       "reference_validation": reference_validation,
                                       "records": skeleton}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("normalized", "prepared", "source-inputs", "vocabulary", "policy", "output"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--native-validator")
    parser.add_argument("--expected-binary-sha256")
    parser.add_argument("--review-reference")
    args = parser.parse_args(argv)
    source_names = ("normalized", "prepared", "source-inputs", "vocabulary", "policy") + (("review-reference",) if args.review_reference else ())
    raw_inputs = {name: Path(getattr(args, name.replace("-", "_"))).read_bytes() for name in source_names}
    inputs = {name: parse(raw) for name, raw in raw_inputs.items()}
    policy = inputs["policy"]
    if args.native_validator is not None:
        policy["validator"]["executable"] = args.native_validator
    if args.expected_binary_sha256 is not None:
        policy["validator"]["expected_binary_sha256"] = args.expected_binary_sha256
    result = evaluate(inputs["normalized"], inputs["prepared"], inputs["source-inputs"], inputs["vocabulary"], policy,
                      review_reference=inputs.get("review-reference"))
    result["source_file_sha256"] = {name: sha(raw) for name, raw in raw_inputs.items()}
    target = Path(args.output)
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("xb") as file:
        file.write(canonical(result) + b"\n")
    print(json.dumps({"status": "written", "planned_requests": result["planned_requests"], "counts": result["counts"],
                      "native_execution": result["validator"]["native_execution"], "semantic_accuracy": None}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
