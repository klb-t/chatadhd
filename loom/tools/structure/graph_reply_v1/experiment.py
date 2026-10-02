"""Offline DEV scorer for first graph-reply samples; no calls or store writes.

Mechanical validation is not semantic adjudication. Gold is loaded only here,
never into prompt preparation. Raw first responses remain in scoring evidence.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys

if not __package__:
    sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
    from loom.tools.structure.graph_reply_v1 import reply
    from loom.tools.structure.agentic_graph_v1 import packet as graph
else:
    from . import reply
    try:
        from ..agentic_graph_v1 import packet as graph
    except ImportError:
        from agentic_graph_v1 import packet as graph

SCORE_SCHEMA = "loom.graph_reply_dev_score/1"
SAMPLE_SCHEMA = "loom.graph_reply_samples/1"
ADJUDICATION_SCHEMA = "loom.graph_reply_adjudications/1"
MODES = ("graph", "flat")
AUTO = {"schema": "loom.graph_packet_apply_policy/1", "acceptance": "auto", "allow_source_tombstones": False}


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def byte_size(value):
    return len(canonical(value).encode("utf-8"))


def sha256(value):
    return hashlib.sha256(canonical(value).encode("utf-8")).hexdigest()


def _unique(entries, key, label):
    result = {}
    for entry in entries:
        ident = key(entry)
        if ident in result:
            raise ValueError("duplicate_" + label + ": " + str(ident))
        result[ident] = entry
    return result


def normalize_samples(value, *, origin=None, mode=None, model_identity=None):
    """Accept a documented envelope or an explicitly labelled case→raw map.

    A dict candidate in the convenience map is rejected: reconstruction is not
    the raw first response. Its sender must preserve the original string.
    """
    if isinstance(value, dict) and value.get("schema") == SAMPLE_SCHEMA:
        if not isinstance(value.get("samples"), list):
            raise ValueError("samples_array_required")
        entries = deepcopy(value["samples"])
        for entry in entries:
            entry.setdefault("sample_origin", value.get("sample_origin"))
            entry.setdefault("model_identity", value.get("model_identity"))
    elif isinstance(value, dict) and origin and mode in MODES:
        entries = [{"case_id": ident, "context_mode": mode, "attempt": 1,
                    "raw_response": raw, "sample_origin": origin,
                    "model_identity": model_identity, "request_sha256": None}
                   for ident, raw in value.items()]
    else:
        raise ValueError("sample_envelope_or_explicitly_labelled_raw_mapping_required")
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("sample_object_required")
        if (not isinstance(entry.get("case_id"), str) or entry.get("context_mode") not in MODES
                or type(entry.get("attempt")) is not int or entry["attempt"] < 1
                or not isinstance(entry.get("raw_response"), str)
                or not isinstance(entry.get("sample_origin"), str) or not entry["sample_origin"]):
            raise ValueError("invalid_sample_metadata_or_missing_exact_raw_text")
        if entry.get("model_identity") is not None and not isinstance(entry["model_identity"], str):
            raise ValueError("invalid_declared_model_identity")
        request_hash = entry.get("request_sha256")
        if request_hash is not None and (not isinstance(request_hash, str) or len(request_hash) != 64
                                         or any(c not in "0123456789abcdef" for c in request_hash)):
            raise ValueError("invalid_request_sha256")
    _unique(entries, lambda x: (x["case_id"], x["context_mode"], x["attempt"]), "sample_attempt")
    return entries


def preservation_checks(before, after):
    """Exact old-record preservation, without interpreting any content."""
    records, order, provenance = True, True, True
    for name in graph.COLLECTIONS:
        old_ids = [graph.record_id(name, item) for item in before[name]]
        new_ids = [graph.record_id(name, item) for item in after[name]]
        index = {graph.record_id(name, item): item for item in after[name]}
        records &= all(index.get(ident) == item for ident, item in zip(old_ids, before[name]))
        order &= new_ids[:len(old_ids)] == old_ids
        provenance &= all(after["provenance"][name].get(ident) == metadata
                          for ident, metadata in before["provenance"][name].items())
    return {"old_records_unchanged": bool(records), "old_record_order_retained": bool(order),
            "old_provenance_unchanged": bool(provenance), "task_unchanged": before["task"] == after["task"],
            "history_extended_once": len(after["history"]) == len(before["history"]) + 1
            and after["history"][:-1] == before["history"]}


def _score_mechanics(case, sample):
    result = {"sample_status": "present", "sample_origin": sample["sample_origin"],
              "declared_model_identity": sample.get("model_identity"), "attempt": 1,
              "request_sha256": sample.get("request_sha256"), "raw_response": sample["raw_response"],
              "parse_valid": False, "contract_valid": False, "compilation_valid": False, "projection_valid": False,
              "append_only_preservation_passed": False}
    base = case["base_packet"]
    stage = "raw_capture"
    try:
        capture = reply.capture_response(sample["raw_response"])
        result["raw_capture"] = capture
        stage = "json_parse"
        parsed = graph.safe.parse_json(sample["raw_response"])
        result["parse_valid"] = True
        stage = "reply_contract"
        reply.validate_reply(parsed, base)
        result["contract_valid"] = True
        stage = "compile"
        compilation = reply.compile_reply(base, sample["raw_response"],
            request_id="dev:" + case["case_id"] + ":" + sample["context_mode"],
            turn_id="dev:" + case["case_id"] + ":" + sample["context_mode"] + ":1",
            model=sample.get("model_identity") or "declared-unknown-model",
            actor="offline-dev-instrument", known_at="2026-10-02T16:00:00Z")
        result["compilation_valid"] = True
        stage = "projection_apply"
        after, receipt = reply.apply_compiled_reply(base, compilation, AUTO)
        result["projection_valid"] = True
        stage = "preservation_checks"
        preservation = preservation_checks(base, after)
        result["preservation"] = preservation
        result["append_only_preservation_passed"] = all(preservation.values())
        nodes = {item["id"]: item for item in parsed["nodes"]}
        pending, max_depth = [(parsed["response_id"], 0)], 0
        while pending:
            ident, depth = pending.pop()
            max_depth = max(max_depth, depth)
            pending.extend((child, depth + 1) for child in nodes[ident]["children"])
        result.update({"response_text": compilation["response_text"], "node_count": len(nodes),
            "leaf_count": sum(not item["children"] for item in nodes.values()), "max_depth": max_depth,
            "link_count": len(parsed["links"]), "links": parsed["links"],
            "link_targets": [link["to"] for link in parsed["links"]],
            "compilation": compilation, "application_receipt": receipt,
            "wire_sizes_utf8_bytes": {"base_packet": byte_size(base), "raw_reply": capture["byte_len"],
                "projected_diff": byte_size(compilation["diff"]), "full_updated_packet": byte_size(after),
                "current_native_records_with_provenance": byte_size({key: after[key]
                    for key in (*graph.COLLECTIONS, "task", "provenance")}),
                "response_text": len(compilation["response_text"].encode("utf-8"))}})
        # Deliberately corrupted regenerated snapshots are mechanism controls,
        # not outputs from a model and not independent generation attempts.
        mutation, omission = deepcopy(after), deepcopy(after)
        if base["entities"]:
            old_id = base["entities"][0]["id"]
            mutation["entities"][0]["label"] += " [CONTROL MUTATION]"
            omission["entities"] = [item for item in omission["entities"] if item["id"] != old_id]
            result["snapshot_preservation_controls"] = {
                "origin": "deterministic-negative-control",
                "old_record_edit_detected": not preservation_checks(base, mutation)["old_records_unchanged"],
                "old_record_omission_detected": not preservation_checks(base, omission)["old_records_unchanged"]}
    except (ValueError, TypeError, UnicodeError) as failure:
        result["failure"] = str(failure)
        result["failure_stage"] = stage
        if getattr(failure, "capture", None) is not None:
            result["raw_capture"] = failure.capture
    return result


def _adjudication_index(document, criteria_by_case):
    if document is None:
        return {}
    if not isinstance(document, dict) or document.get("schema") != ADJUDICATION_SCHEMA:
        raise ValueError("adjudication_envelope_required")
    entries = document.get("adjudications")
    if not isinstance(entries, list):
        raise ValueError("adjudications_array_required")
    result = _unique(entries, lambda x: (x["case_id"], x["context_mode"]), "adjudication")
    for (case_id, mode), entry in result.items():
        if (case_id not in criteria_by_case or mode not in MODES
                or not isinstance(entry.get("adjudicator"), str) or not entry["adjudicator"].strip()):
            raise ValueError("invalid_adjudication_identity")
        criteria = entry.get("criteria")
        if not isinstance(criteria, list):
            raise ValueError("adjudication_criteria_array_required")
        indexed = _unique(criteria, lambda x: x["criterion_id"], "adjudication_criterion")
        if set(indexed) - set(criteria_by_case[case_id]):
            raise ValueError("unknown_adjudication_criterion")
        for criterion in criteria:
            if criterion.get("passed") is not None and type(criterion["passed"]) is not bool:
                raise ValueError("criterion_passed_requires_boolean_or_null")
            if not isinstance(criterion.get("rationale"), str) or not criterion["rationale"].strip():
                raise ValueError("criterion_rationale_required")
        entry["criteria"] = indexed
    return result


def score_samples(cases_document, expectations_document, samples_document, *, adjudications=None,
                  prepared_requests=None, origin=None, mode=None, model_identity=None):
    if cases_document.get("schema") != "loom.graph_reply_dev_cases/1" or cases_document.get("split") != "dev":
        raise ValueError("synthetic_dev_cases_required")
    if expectations_document.get("schema") != "loom.graph_reply_dev_expectations/1":
        raise ValueError("dev_expectations_required")
    cases = _unique(cases_document["cases"], lambda x: x["case_id"], "case")
    expectations = _unique(expectations_document["expectations"], lambda x: x["case_id"], "expectation")
    if set(cases) != set(expectations):
        raise ValueError("case_expectation_identity_mismatch")
    criteria = {ident: _unique(item["criteria"], lambda x: x["criterion_id"], "criterion")
                for ident, item in expectations.items()}
    samples = normalize_samples(samples_document, origin=origin, mode=mode, model_identity=model_identity)
    if any(sample["case_id"] not in cases for sample in samples):
        raise ValueError("unknown_sample_case")
    first = {(entry["case_id"], entry["context_mode"]): entry for entry in samples if entry["attempt"] == 1}
    request_index = {}
    if prepared_requests is not None:
        if not isinstance(prepared_requests, dict) or not isinstance(prepared_requests.get("requests"), list):
            raise ValueError("prepared_requests_envelope_required")
        request_index = _unique(prepared_requests["requests"],
            lambda x: (x["case_id"], x["context_mode"]), "prepared_request")
        for key, request in request_index.items():
            if key[0] not in cases or key[1] not in MODES or sha256(request["body"]) != request["receipt"]["body_sha256"]:
                raise ValueError("prepared_request_identity_or_hash_mismatch")
    judges = _adjudication_index(deepcopy(adjudications), criteria)
    if set(judges) - set(first):
        raise ValueError("adjudication_without_preserved_first_sample")
    scored, summaries = [], {}
    for context_mode in MODES:
        summary = {"planned_cases": len(cases), "present_first_samples": 0, "missing_first_samples": 0,
            "contract_valid": 0, "projection_valid": 0, "append_only_preservation_passed": 0,
            "request_hash_matches": 0, "request_hash_mismatches": 0, "request_hash_unassessed": 0,
            "semantic_criteria_planned": sum(len(x) for x in criteria.values()),
            "semantic_criteria_passed": 0, "semantic_criteria_failed": 0, "semantic_criteria_unassessed": 0,
            "fully_assessed_semantic_cases": 0, "all_semantic_criteria_passed_cases": 0}
        for ident, case in cases.items():
            sample = first.get((ident, context_mode))
            mechanical = _score_mechanics(case, sample) if sample else {"sample_status": "missing",
                "contract_valid": False, "projection_valid": False, "append_only_preservation_passed": False}
            expected_request = request_index.get((ident, context_mode))
            request_matches = (sample.get("request_sha256") == expected_request["receipt"]["body_sha256"]
                               if sample and expected_request else None)
            mechanical["request_hash_matches"] = request_matches
            mechanical["request_binding_meaning"] = "reported request hash only; no proof of API delivery"
            summary["request_hash_matches"] += int(request_matches is True)
            summary["request_hash_mismatches"] += int(request_matches is False)
            summary["request_hash_unassessed"] += int(request_matches is None)
            summary["present_first_samples" if sample else "missing_first_samples"] += 1
            for metric in ("contract_valid", "projection_valid", "append_only_preservation_passed"):
                summary[metric] += int(mechanical[metric])
            judge = judges.get((ident, context_mode), {})
            judgments = []
            for criterion_id, criterion in criteria[ident].items():
                assessed = judge.get("criteria", {}).get(criterion_id, {})
                passed = assessed.get("passed")
                judgments.append({"criterion_id": criterion_id, "description": criterion["description"],
                    "passed": passed, "rationale": assessed.get("rationale"), "adjudicator": judge.get("adjudicator")})
                summary["semantic_criteria_unassessed" if passed is None else
                        "semantic_criteria_passed" if passed else "semantic_criteria_failed"] += 1
            assessed_all = all(item["passed"] is not None for item in judgments)
            summary["fully_assessed_semantic_cases"] += int(assessed_all)
            summary["all_semantic_criteria_passed_cases"] += int(assessed_all and all(item["passed"] for item in judgments))
            scored.append({"case_id": ident, "context_mode": context_mode, "mechanics": mechanical,
                           "semantic_adjudication": judgments})
        summaries[context_mode] = summary
    return {"schema": SCORE_SCHEMA, "split": "dev", "cases_sha256": sha256(cases_document),
        "expectations_sha256": sha256(expectations_document), "samples_sha256": sha256(samples_document),
        "adjudications_sha256": sha256(adjudications) if adjudications else None,
        "prepared_requests_sha256": sha256(prepared_requests) if prepared_requests else None,
        "summary_by_context_mode": summaries, "case_scores": scored,
        "later_attempts_excluded_from_first_sample_success": [entry for entry in samples if entry["attempt"] != 1],
        "limits": ["synthetic annotated DEV conversations", "input format ablation; identical graph output schema",
            "no API calls, token measurement or canonical store writes", "unassessed semantics remains null",
            "full snapshot size comparison is deterministic, not model regeneration quality"]}


def prepare_requests(cases_document, *, model="caller-selected-model"):
    """Prepare both arms from prompt-facing cases only; never loads gold."""
    if not __package__:
        from loom.tools.structure.graph_reply_v1 import prompt
    else:
        from . import prompt
    requests = []
    for case in cases_document["cases"]:
        for mode in MODES:
            body = prompt.build_request(case["base_packet"], case["user_query"], model=model, context_mode=mode)
            requests.append({"case_id": case["case_id"], "context_mode": mode, "body": body,
                "receipt": prompt.request_receipt(body, context_mode=mode, format_mode="json_schema")})
    return {"schema": "loom.graph_reply_prepared_dev_requests/1", "cases_sha256": sha256(cases_document),
            "requests": requests, "network_calls": 0, "gold_loaded": False}


def compare_payloads(score):
    """Deterministic representation sizes; no model snapshot generation arm.

    Compiled diffs include rich host-produced provenance and source capture;
    complete packets additionally include reversible history. These are
    different transport artifacts, not interchangeable prompt/token estimates.
    """
    rows, totals = [], {mode: {} for mode in MODES}
    for item in score["case_scores"]:
        mechanics = item["mechanics"]
        sizes = mechanics.get("wire_sizes_utf8_bytes")
        row = {"case_id": item["case_id"], "context_mode": item["context_mode"],
               "available": sizes is not None, "sizes_utf8_bytes": deepcopy(sizes)}
        rows.append(row)
        if sizes is not None:
            arm = totals[item["context_mode"]]
            for key, value in sizes.items():
                arm[key] = arm.get(key, 0) + value
            arm["measured_samples"] = arm.get("measured_samples", 0) + 1
    return {"schema": "loom.graph_reply_payload_comparison/1", "origin": "deterministic-saved-sample-transport-measurement",
            "rows": rows, "totals_by_context_mode": totals, "model_snapshot_regeneration_samples": 0,
            "not_measured": ["tokens", "latency", "cost", "model snapshot corruption rate"],
            "interpretation": "raw compact model wire versus host compiled rich native diff and full packet; full packet includes reversible history"}


def compare_composed_payloads(cases_document, samples_document):
    """Compare V1 samples with a deterministic V2 compositional encoding.

    These V2 candidates are host transformations of saved V1 responses, not
    fresh model outputs. Normalized V1/V2 byte counts isolate parent text
    duplication from whitespace/key-order choices in the original raw sample.
    """
    cases = _unique(cases_document["cases"], lambda x: x["case_id"], "case")
    samples = normalize_samples(samples_document)
    rows = []
    for sample in samples:
        if sample["attempt"] != 1:
            continue
        case_id, mode = sample["case_id"], sample["context_mode"]
        if case_id not in cases:
            raise ValueError("unknown_sample_case")
        packet = cases[case_id]["base_packet"]
        row = {"case_id": case_id, "context_mode": mode,
               "candidate_origin": "deterministic-v1-to-v2-representation-transform",
               "fresh_v2_model_sample": False, "source_raw_capture": reply.capture_response(sample["raw_response"])}
        stage = "v1_validation"
        try:
            candidate = graph.safe.parse_json(sample["raw_response"])
            reply.validate_reply(candidate, packet)
            if candidate["schema"] != "loom.graph_reply/1":
                raise ValueError("v1_sample_required_for_composed_comparison")
            composed = deepcopy(candidate)
            composed["schema"] = "loom.graph_reply/2"
            composite_bytes, composite_count = 0, 0
            for node in composed["nodes"]:
                if node["children"]:
                    composite_bytes += len(node["text"].encode("utf-8"))
                    composite_count += 1
                    node["text"] = None
            raw_v2 = canonical(composed)
            row.update({"converted_raw_response": raw_v2, "composite_nodes": composite_count,
                "repeated_composite_text_utf8_bytes": composite_bytes,
                "sizes_utf8_bytes": {"v1_original_raw": row["source_raw_capture"]["byte_len"],
                    "v1_normalized": byte_size(candidate), "v2_normalized": byte_size(composed)},
                "normalized_bytes_saved": byte_size(candidate) - byte_size(composed)})
            stage = "v2_validation"
            reply.validate_reply(composed, packet)
            stage = "compilation"
            host = {"request_id": "dev:composed:" + case_id + ":" + mode,
                "turn_id": "dev:composed:" + case_id + ":" + mode,
                "model": sample.get("model_identity") or "declared-unknown-model",
                "actor": "deterministic-representation-instrument", "known_at": "2026-10-02T16:00:00Z"}
            v1 = reply.compile_reply(packet, sample["raw_response"], **host)
            v2 = reply.compile_reply(packet, raw_v2, **host)
            after, _receipt = reply.apply_compiled_reply(packet, v2, AUTO)
            row.update({"v2_contract_valid": True, "v2_projection_valid": True,
                "rendered_text_identical": v1["response_text"] == v2["response_text"],
                "spans_identical": v1["spans"] == v2["spans"],
                "links_identical": candidate["links"] == composed["links"],
                "original_packet_preserved": all(preservation_checks(packet, after).values()),
                "v2_compilation_sha256": v2["compilation_sha256"], "v2_raw_capture": v2["raw_capture"]})
        except (ValueError, TypeError, UnicodeError) as failure:
            row.update({"failure": str(failure), "failure_stage": stage, "v2_contract_valid": False})
        rows.append(row)
    totals = {mode: {"attempted_candidates": 0, "valid_candidates": 0, "normalized_v1_bytes": 0,
                    "normalized_v2_bytes": 0, "normalized_bytes_saved": 0} for mode in MODES}
    for row in rows:
        total = totals[row["context_mode"]]
        total["attempted_candidates"] += 1
        if row.get("v2_contract_valid"):
            total["valid_candidates"] += 1
            total["normalized_v1_bytes"] += row["sizes_utf8_bytes"]["v1_normalized"]
            total["normalized_v2_bytes"] += row["sizes_utf8_bytes"]["v2_normalized"]
            total["normalized_bytes_saved"] += row["normalized_bytes_saved"]
    return {"schema": "loom.graph_reply_composed_encoding_comparison/1", "rows": rows,
        "totals_by_context_mode": totals, "fresh_v2_model_samples": 0,
        "interpretation": "representation-only transformation of saved first V1 samples; no V2 prompt compliance or token saving measurement"}


def _load(path):
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def main():
    root = Path(__file__).resolve().parents[4]
    evidence = root / "docs/research/graph_reply_2026-10-02"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=Path, default=evidence / "cases.json")
    parser.add_argument("--expectations", type=Path, default=evidence / "expectations.json")
    parser.add_argument("--responses", type=Path)
    parser.add_argument("--adjudications", type=Path)
    parser.add_argument("--requests", type=Path, help="prepared request envelope for exact reported-hash binding")
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--model", default="caller-selected-model")
    parser.add_argument("--origin", help="required explicit provenance for convenience raw-response mappings")
    parser.add_argument("--mode", choices=MODES, help="context mode of a convenience raw-response mapping")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    cases = _load(args.cases)
    if args.prepare:
        if args.responses or args.adjudications:
            parser.error("--prepare does not load responses or semantic gold")
        result = prepare_requests(cases, model=args.model)
    else:
        if not args.responses:
            parser.error("scoring requires --responses")
        result = score_samples(cases, _load(args.expectations), _load(args.responses),
            adjudications=_load(args.adjudications) if args.adjudications else None,
            prepared_requests=_load(args.requests) if args.requests else None,
            origin=args.origin, mode=args.mode, model_identity=args.model if args.origin else None)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")
    summary = result["summary_by_context_mode"] if "summary_by_context_mode" in result else {"prepared_requests": len(result["requests"])}
    print(json.dumps(summary, ensure_ascii=False))


if __name__ == "__main__":
    main()
