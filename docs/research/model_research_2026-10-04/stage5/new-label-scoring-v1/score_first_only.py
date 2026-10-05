"""Offline, DATA-bound replay of public normalized first source-label responses.

No fixture/prepare module is imported and no private captures or ledger are read.
The original pure compiler/scorer functions are loaded from hash-pinned source AST.
"""
from __future__ import annotations

import argparse
import ast
from collections import Counter
from copy import deepcopy
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import re
from types import SimpleNamespace


class ReplayError(ValueError):
    """Fixed error codes; never include request, credential or remote text."""


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def _pairs(values):
    out = {}
    for key, value in values:
        if key in out:
            raise ReplayError("duplicate_json_key")
        out[key] = value
    return out


def parse(raw):
    try:
        return json.loads(raw, object_pairs_hook=_pairs,
                          parse_constant=lambda _: (_ for _ in ()).throw(ReplayError("nonfinite_json")))
    except (UnicodeError, json.JSONDecodeError, TypeError):
        raise ReplayError("invalid_json") from None


def pointer(value, path):
    if path == "":
        return value
    if not isinstance(path, str) or not path.startswith("/"):
        raise ReplayError("invalid_json_pointer")
    try:
        for part in path[1:].split("/"):
            key = part.replace("~1", "/").replace("~0", "~")
            value = value[int(key)] if isinstance(value, list) else value[key]
        return value
    except (KeyError, IndexError, TypeError, ValueError):
        raise ReplayError("missing_json_pointer") from None


def bound(raw, expected, reason):
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ReplayError("unbound_hash")
    if sha(raw) != expected:
        raise ReplayError(reason)
    return parse(raw)


def artifact(spec, base):
    path = (Path(base) / spec["path"]).resolve()
    raw = path.read_bytes()
    return bound(raw, spec["raw_sha256"], "artifact_hash_changed"), raw, path


def inventory(rows, allowed=None, field="operation_id", reason="invalid_inventory"):
    if not isinstance(rows, list):
        raise ReplayError(reason)
    out = {}
    for row in rows:
        key = row.get(field) if isinstance(row, dict) else None
        if not isinstance(key, str) or not key or key in out or (allowed is not None and key not in allowed):
            raise ReplayError(reason)
        out[key] = row
    return out


def money(value):
    if type(value) not in (str, int, float):
        raise ReplayError("invalid_cost")
    try:
        number = Decimal(str(value))
    except InvalidOperation:
        raise ReplayError("invalid_cost") from None
    if not number.is_finite() or number < 0:
        raise ReplayError("invalid_cost")
    return number


def _ast_namespace(spec, base, initial):
    path = (Path(base) / spec["path"]).resolve()
    raw = path.read_bytes()
    if sha(raw) != spec["raw_sha256"]:
        raise ReplayError("instrument_source_changed")
    tree = ast.parse(raw)
    namespace = dict(initial)
    constants = set(spec.get("constants", []))
    symbols = set(spec.get("symbols", []))
    selected = []
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id in constants:
                    namespace[target.id] = ast.literal_eval(node.value)
                    constants.remove(target.id)
        elif isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in symbols:
            selected.append(node)
            symbols.remove(node.name)
    if constants or symbols:
        raise ReplayError("instrument_symbol_missing")
    exec(compile(ast.Module(body=selected, type_ignores=[]), str(path), "exec"), namespace)
    return namespace


def pure_panel(config, base):
    support = _ast_namespace(config["instrument"]["key_validator"], base, {})
    panel = _ast_namespace(config["instrument"]["panel"], base,
                           {"Counter": Counter, "safe": SimpleNamespace(_keys=support["_keys"])})
    return panel["compile_jev_judgment"], panel["score_judgments"]


def _time(value):
    from datetime import datetime
    try:
        result = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if result.tzinfo is None:
            raise ValueError
        return result
    except (AttributeError, TypeError, ValueError):
        raise ReplayError("invalid_source_time") from None


def validate(config_raw, manifest_raw, gold_raw, base, expected_config_sha256):
    config = bound(config_raw, expected_config_sha256, "config_hash_changed")
    if sha(Path(__file__).read_bytes()) != config["replay_adapter_raw_sha256"]:
        raise ReplayError("replay_adapter_changed")
    if config.get("binding_status") != "bound":
        raise ReplayError("paid_manifest_binding_pending")
    manifest = bound(manifest_raw, config["paid_manifest"]["raw_sha256"], "paid_manifest_hash_changed")
    gold_container = bound(gold_raw, config["gold"]["raw_sha256"], "gold_hash_changed")
    gold = pointer(gold_container, config["gold"]["cases_pointer"])
    if manifest.get("schema") != config["paid_manifest"]["schema"]:
        raise ReplayError("manifest_schema_changed")
    for field in config["paid_manifest"]["identity_fields"]:
        if manifest.get(field) != config["paid_manifest"][field]:
            raise ReplayError("manifest_identity_changed")
    operations = inventory(manifest.get("operations"), reason="duplicate_or_invalid_operation")
    mappings = inventory(config["operation_mapping"], reason="duplicate_or_invalid_mapping")
    if set(operations) != set(mappings):
        raise ReplayError("planned_operation_mapping_mismatch")
    inputs, _, _ = artifact(config["source_inputs"], base)
    cases = pointer(inputs, config["source_inputs"]["cases_pointer"])
    family_ids = inventory(cases, field="id", reason="source_family_inventory_invalid")
    sources = inventory(cases, field="source_id", reason="source_id_inventory_invalid")
    source_queries = {}
    for case in cases:
        nodes = inventory(case["node_inventory"], field="id", reason="source_node_inventory_invalid")
        inventory(case["turns"], field="id", reason="source_turn_inventory_invalid")
        for query in case["judgment_queries"]:
            if query["id"] in source_queries or query["source"] not in nodes or query["target"] not in nodes:
                raise ReplayError("source_query_inventory_invalid")
            if query["scope"] != config["source_policy"]["scope"]:
                raise ReplayError("source_scope_changed")
            source_queries[query["id"]] = (case, query)
    gold_groups = inventory(gold, field="id", reason="gold_family_inventory_invalid")
    if set(gold_groups) != set(family_ids):
        raise ReplayError("gold_source_family_inventory_mismatch")
    for ident, group in gold_groups.items():
        if set(q["query_id"] for q in group["judgments"]) != set(q["id"] for q in family_ids[ident]["judgment_queries"]):
            raise ReplayError("gold_source_family_query_binding_changed")
    gold_queries = inventory([q for group in gold for q in group["judgments"]],
                             field="query_id", reason="gold_query_inventory_invalid")
    if set(gold_queries) != set(source_queries) or any(q["label"] not in config["source_policy"]["gold_labels"] for q in gold_queries.values()):
        raise ReplayError("gold_source_query_inventory_mismatch")
    spec_rows = {}
    if len({arm["arm_id"] for arm in config["arms"]}) != len(config["arms"]):
        raise ReplayError("duplicate_arm_inventory")
    for arm in config["arms"]:
        document, _, _ = artifact(arm["prepared_specs"], base)
        spec_rows[arm["arm_id"]] = inventory(document, field=arm["prepared_specs"]["query_id_field"],
                                            reason="prepared_query_inventory_invalid")
        chosen = [v for v in mappings.values() if v["arm_id"] == arm["arm_id"]]
        identifiers = [v["query_id"] for v in chosen]
        if len(identifiers) != len(set(identifiers)) or set(identifiers) != set(gold_queries) or set(spec_rows[arm["arm_id"]]) != set(gold_queries):
            raise ReplayError("arm_planned_query_inventory_mismatch")
        if len(chosen) != arm["authored_planned_queries"]:
            raise ReplayError("authored_inventory_count_changed")
    if set(v["arm_id"] for v in mappings.values()) != set(spec_rows):
        raise ReplayError("arm_inventory_mismatch")
    return config, manifest, operations, mappings, gold, source_queries, spec_rows


def score(config_raw, manifest_raw, bundle_raw, gold_raw, *, base, manifest_base,
          expected_config_sha256, expected_bundle_sha256):
    config, manifest, operations, mappings, gold, source_queries, specs = validate(
        config_raw, manifest_raw, gold_raw, base, expected_config_sha256)
    bundle = bound(bundle_raw, expected_bundle_sha256, "normalized_bundle_hash_changed")
    if bundle.get("schema") != config["normalized_bundle"]["schema"]:
        raise ReplayError("normalized_bundle_schema_changed")
    for field in config["paid_manifest"]["identity_fields"]:
        if bundle.get(field) != manifest[field]:
            raise ReplayError("bundle_identity_changed")
    if (bundle.get("manifest_sha256") != sha(manifest_raw)
            or bundle.get("planned_operation_ids") != list(operations)
            or bundle.get("planned_operations") != len(operations)):
        raise ReplayError("bundle_planned_binding_changed")
    rows = inventory(bundle.get("rows"), operations, reason="duplicate_or_unknown_first_row")
    responses = inventory(bundle.get("responses"), operations, reason="duplicate_or_unknown_first_capture")
    requests = inventory(bundle.get("requests"), operations, reason="request_inventory_invalid")
    if set(requests) != set(operations) or set(responses) - set(rows) or bundle.get("saved_row_count") != len(rows):
        raise ReplayError("first_or_request_inventory_mismatch")
    row_generations = [row.get("generation_id") for row in rows.values() if row.get("generation_id") is not None]
    if any(not isinstance(generation, str) or not generation for generation in row_generations) or len(set(row_generations)) != len(row_generations):
        raise ReplayError("duplicate_or_invalid_first_generation_identity")
    compile_judgment, score_judgments = pure_panel(config, base)
    policy = config["response_policy"]
    generations = set(); successful_hashes = set(); billing_hashes = set(); records = []
    for ident, operation in operations.items():
        mapping = mappings[ident]; request = requests[ident]; case, query = source_queries[mapping["query_id"]]
        for field in config["mapping_source_fields"]:
            source = {"family_id": case["id"], "source_id": case["source_id"], "language": case["language"]}
            if mapping[field] != source[field]:
                raise ReplayError("mapping_source_identity_changed")
        for field, path in config["manifest_metadata_bindings"].items():
            if pointer(operation, path) != mapping[field]:
                raise ReplayError("operation_metadata_mapping_changed")
        meta = {field: operation.get("metadata", {}).get(field) for field in config["normalized_metadata_fields"]}
        if request.get("metadata") != meta:
            raise ReplayError("public_request_metadata_changed")
        path = (Path(manifest_base) / operation["request_file"]).resolve()
        if not path.is_relative_to(Path(manifest_base).resolve()) or path == Path(manifest_base).resolve():
            raise ReplayError("request_outside_manifest")
        original = bound(path.read_bytes(), operation["request_sha256"], "request_raw_hash_changed")
        if request.get("request_sha256") != operation["request_sha256"] or canonical(request.get("body")) != canonical(original):
            raise ReplayError("public_request_body_changed")
        for field in config["normalized_request_identity_fields"]:
            if request.get(field) != operation.get(field):
                raise ReplayError("public_request_identity_changed")
        rp = config["request_policy"]
        if (operation["route_id"] != rp["route_id"] or pointer(original, rp["model_pointer"]) != operation["model_id"]
                or pointer(original, rp["provider_pointer"]) != operation["provider_id"]):
            raise ReplayError("request_model_provider_route_changed")
        prepared = specs[mapping["arm_id"]][mapping["query_id"]]
        if original.get("questions") != prepared["questions"] or original.get("state") != prepared["state"]:
            raise ReplayError("frozen_prepared_spec_changed")
        payload = parse(pointer(original, rp["source_payload_pointer"]))
        prefix = [t for t in case["turns"] if _time(t["known_at"]) <= _time(query["as_of"])]
        if not prefix or payload != {"case_id": case["id"], "source_id": case["source_id"], "turns": prefix,
                                     "node_inventory": case["node_inventory"], "query": query}:
            raise ReplayError("request_source_query_time_binding_changed")
        row = rows.get(ident); response = responses.get(ident)
        prediction = {"query_id": mapping["query_id"], "state": "unavailable"}
        error = "missing_first_row"; cost = None; probabilities = None; projected = None
        if row is not None:
            if row.get("request_sha256") != operation["request_sha256"] or row.get("manifest_sha256") != sha(manifest_raw) or row.get("metadata") != meta:
                raise ReplayError("first_row_binding_changed")
            if row.get("requested_model") != operation["model_id"] or row.get("requested_provider") != operation["provider_id"]:
                raise ReplayError("first_requested_identity_changed")
            if response is not None:
                rh, gh = row.get("response_sha256"), row.get("generation_sha256")
                if not isinstance(rh, str) or not re.fullmatch(r"[0-9a-f]{64}", rh):
                    raise ReplayError("first_capture_hash_invalid")
                if gh is not None and (not isinstance(gh, str) or not re.fullmatch(r"[0-9a-f]{64}", gh)):
                    raise ReplayError("generation_capture_hash_invalid")
                if response.get("response_sha256") != rh or response.get("generation_sha256") != gh:
                    raise ReplayError("first_capture_binding_changed")
                projected = response.get("projection")
            error = "missing_first_capture" if response is None else "first_attempt_unavailable"
            if isinstance(projected, dict):
                generation = projected.get("id")
                if generation is not None:
                    if not isinstance(generation, str) or not generation or generation != row.get("generation_id") or generation in generations:
                        raise ReplayError("duplicate_or_invalid_generation_identity")
                    generations.add(generation)
                elif row.get("state") in policy["identity_required_states"]:
                    raise ReplayError("missing_generation_identity")
                if projected.get("model") != row.get("response_model"):
                    raise ReplayError("response_model_projection_changed")
                eligible = (row.get("state") in policy["label_response_states"] and row.get("http_status") == policy["successful_http_status"]
                            and all(row.get(k) is v for k, v in policy["label_required_flags"].items()))
                if eligible:
                    if row["response_sha256"] in successful_hashes:
                        raise ReplayError("duplicate_successful_first_capture")
                    successful_hashes.add(row["response_sha256"])
                    try:
                        if money(projected.get("usage", {}).get("cost")) != money(row.get("reported_cost_usd")):
                            raise ReplayError("reported_cost_projection_changed")
                        answers = projected.get("answers")
                        if not isinstance(answers, dict) or set(answers) != set(original["questions"]):
                            raise ReplayError("answer_inventory_changed")
                        probabilities = {key: answer.get("noul") if isinstance(answer, dict) else None for key, answer in answers.items()}
                        if any(not isinstance(answer, dict) or answer.get("type") != policy["answer_type"]
                               or type(answer.get("noul")) not in (int, float) for answer in answers.values()):
                            raise ReplayError("invalid_probability_answer")
                        prediction = compile_judgment({"probabilities": probabilities}, mapping["query_id"])
                        error = "conflicting_first_source_label_answer" if prediction["label"] == "conflicting" else None
                    except (ValueError, KeyError, TypeError):
                        error = "invalid_first_source_label_answer"
                billed = (row.get("state") in policy["billing_response_states"]
                          and all(row.get(k) is v for k, v in policy["billing_required_flags"].items()))
                if billed:
                    if row.get("billing_mode") not in policy["billing_modes"] or projected.get("usage", {}).get("is_byok") is True:
                        raise ReplayError("credit_attestation_contradiction")
                    receipt = response.get("generation_projection", {})
                    if (receipt.get("id") != generation or receipt.get("model") != row.get("observed_model")
                            or receipt.get("provider_name") != row.get("observed_provider") or receipt.get("is_byok") is not False
                            or not isinstance(row.get("generation_sha256"), str)):
                        raise ReplayError("credit_projection_mismatch")
                    if row["generation_sha256"] in billing_hashes:
                        raise ReplayError("duplicate_verified_generation_capture")
                    billing_hashes.add(row["generation_sha256"])
                    cost = money(row.get("actual_cost_usd"))
                    if cost != money(receipt.get("total_cost")) or cost != money(row.get("reported_cost_usd")):
                        raise ReplayError("verified_credit_cost_mismatch")
        records.append({**deepcopy(mapping), "prediction": prediction, "label_error": error,
                        "source_first_row_state": None if row is None else row.get("state"),
                        "source_first_row_present": row is not None, "source_first_capture_present": response is not None,
                        "response_probabilities": probabilities,
                        "first_row_projection": None if row is None else {k: deepcopy(row.get(k)) for k in config["retained_first_row_fields"]},
                        "actual_cost_usd": None if cost is None else format(cost, "f"), "billing_complete": cost is not None,
                        "native_grounding": None, "semantic_adequacy": None, "world_truth_accuracy": None})
    groups = []
    for dimensions in config["group_dimensions"]:
        cohorts = {}
        for record in records:
            key = tuple(record[field] for field in dimensions)
            cohorts.setdefault(key, []).append(record)
        for key, selected in cohorts.items():
            query_ids = {v["query_id"] for v in selected}
            if len(query_ids) != len(selected):
                raise ReplayError("group_reuses_query_without_arm_dimension")
            selected_gold = [{**g, "judgments": [q for q in g["judgments"] if q["query_id"] in query_ids]} for g in gold]
            selected_gold = [g for g in selected_gold if g["judgments"]]
            raw_score = score_judgments(selected_gold, [v["prediction"] for v in selected])
            known = sum((Decimal(v["actual_cost_usd"]) for v in selected if v["actual_cost_usd"] is not None), Decimal(0))
            unknown = sum(v["actual_cost_usd"] is None for v in selected)
            groups.append({"dimensions": dict(zip(dimensions, key)), "planned_family_count": len(selected_gold),
                           "quality": {"source_commitment_accuracy": raw_score["accuracy_all_queries"],
                                       "availability": {k: raw_score[k] for k in ("query_count", "available", "unavailable", "coverage")}},
                           "confusion": raw_score["confusion"], "failures": raw_score["failures"],
                           "cost": {"known_verified_usd": format(known, "f"), "unknown_planned_attempts": unknown,
                                    "complete": unknown == 0, "total_usd": format(known, "f") if unknown == 0 else None},
                           "native_grounding": None, "semantic_adequacy": None, "world_truth_accuracy": None})
    return {"schema": config["output_schema"], "programme_id": manifest["programme_id"], "stage_id": manifest["stage_id"],
            "input_sha256": {"config": sha(config_raw), "paid_manifest": sha(manifest_raw), "normalized_bundle": sha(bundle_raw),
                             "gold_raw_container": sha(gold_raw), "source_inputs": config["source_inputs"]["raw_sha256"]},
            "gold_selection": config["gold"], "instrument": config["instrument"], "population": config["population"],
            "planned_operations": len(operations), "received_first_rows": len(rows), "records": records, "groups": groups,
            "first_response_only": True, "replacement_responses_used": False, "new_model_calls": 0,
            "native_grounding": None, "semantic_adequacy": None, "world_truth_accuracy": None,
            "holdout_evaluation": False, "automatic_promotion": False,
            "billing_evidence_boundary": config["billing_evidence_boundary"], "evidence_boundary": config["evidence_boundary"]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("config", "manifest", "bundle", "gold", "config-sha256", "bundle-sha256", "output"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args(argv)
    result = score(Path(args.config).read_bytes(), Path(args.manifest).read_bytes(), Path(args.bundle).read_bytes(),
                   Path(args.gold).read_bytes(), base=Path(args.config).parent, manifest_base=Path(args.manifest).parent,
                   expected_config_sha256=args.config_sha256, expected_bundle_sha256=args.bundle_sha256)
    output = Path(args.output); raw = json.dumps(result, ensure_ascii=False, indent=2).encode() + b"\n"
    if output.exists() and output.read_bytes() != raw:
        raise ReplayError("existing_score_output_changed")
    output.parent.mkdir(parents=True, exist_ok=True); output.write_bytes(raw)
    print(json.dumps({"output": str(output), "sha256": sha(raw), "planned_operations": result["planned_operations"]}))


if __name__ == "__main__":
    main()
