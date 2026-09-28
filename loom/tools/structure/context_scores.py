#!/usr/bin/env python3
"""Offline, provider-neutral context score replay. No inference or graph writes."""
from __future__ import annotations

import argparse
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import sys

try:
    from .context_delta import VERSION as CONTEXT_VERSION, canonical, prepare_context, validate_delta
except ImportError:
    from context_delta import VERSION as CONTEXT_VERSION, canonical, prepare_context, validate_delta

VERSION = "context-scores/1"
PACKET_KEYS = {
    "schema", "snapshot_id", "observations", "entities", "claims",
    "context_delta_version", "base_hash", "time_cut", "current_spans",
    "context_claims", "context_threads", "excluded_context_counts",
    "entity_times", "immutable_span_ids", "temporal_policy", "packet_hash",
}


def _hash(value):
    return hashlib.sha256(canonical(value).encode("utf-8")).hexdigest()


def _text(value):
    return isinstance(value, str) and bool(value.strip())


def _number(value):
    return type(value) in (int, float) and 0 <= value <= 1 and math.isfinite(value)


def _bounded(value):
    """Avoid recursive copying/serialization of malformed unbounded inputs."""
    stack, count, chars = [(value, 0)], 0, 0
    while stack:
        item, depth = stack.pop()
        count += 1
        if count > 100000 or depth > 64:
            raise ValueError("input_size_or_depth_limit")
        if isinstance(item, (dict, list)):
            # Cycles terminate at the depth/node budget before recursive copies.
            if len(item) > 100000:
                raise ValueError("input_collection_size_limit")
            if isinstance(item, dict):
                if any(not isinstance(k, str) for k in item):
                    raise ValueError("non_string_json_key")
                stack.extend((k, depth + 1) for k in item)
                stack.extend((v, depth + 1) for v in item.values())
            else:
                stack.extend((v, depth + 1) for v in item)
        elif isinstance(item, str):
            chars += len(item.encode("utf-8"))
            if chars > 4 * 1024 * 1024:
                raise ValueError("input_text_size_limit")
        elif item is not None and type(item) not in (int, float, bool):
            raise ValueError("non_json_value")


def _index(rows, field="id"):
    if not isinstance(rows, list):
        raise ValueError("packet_collection_requires_array")
    result = {}
    for row in rows:
        if not isinstance(row, dict) or not _text(row.get(field)) or row[field] in result:
            raise ValueError("packet_record_ids_must_be_unique")
        result[row[field]] = row
    return result


def _check_packet(packet):
    # Refuse the preparation wrapper before even traversing local-only sidecars.
    if not isinstance(packet, dict) or len(packet) != len(PACKET_KEYS) or set(packet) != PACKET_KEYS:
        raise ValueError("requires_only_prepare_context_packet")
    _bounded(packet)
    if packet["schema"] != "loom.source_packet/1" or packet["context_delta_version"] != CONTEXT_VERSION:
        raise ValueError("unsupported_context_packet")
    if not _text(packet["snapshot_id"]) or not re.fullmatch(r"[0-9a-f]{64}", str(packet["base_hash"])):
        raise ValueError("invalid_packet_identity")
    cut = _instant(packet["time_cut"])
    if cut is None or packet["temporal_policy"] != "future_refused_unknown_retained_without_past_assertion":
        raise ValueError("invalid_packet_temporal_policy")
    # Reuse the existing packet-relative source/Assessment consistency checks.
    checked = validate_delta(packet, {"id": "score_packet_check", "base_hash": packet["base_hash"],
                                     "packet_hash": packet["packet_hash"]})
    if checked["status"] != "valid":
        raise ValueError("invalid_context_packet:" + checked["errors"][0]["reason"])
    observations = _index(packet["observations"])
    _index(packet["entities"])
    claims = _index(packet["claims"])
    contexts = _index(packet["context_claims"], "claim_id")
    spans = _index(packet["current_spans"])
    threads = _index(packet["context_threads"])
    entity_times = _index(packet["entity_times"])
    if set(entity_times) != {e["id"] for e in packet["entities"]}:
        raise ValueError("entity_time_allowlist_mismatch")
    if set(claims) != set(contexts):
        raise ValueError("context_claim_allowlist_mismatch")
    if any(not isinstance(o.get("text"), str) for o in observations.values()):
        raise ValueError("observation_text_required")
    if not isinstance(packet["excluded_context_counts"], dict):
        raise ValueError("excluded_context_counts_requires_object")
    if any(type(v) is not int or v < 0 for v in packet["excluded_context_counts"].values()):
        raise ValueError("invalid_excluded_context_count")
    immutable = packet["immutable_span_ids"]
    if len(set(immutable)) != len(immutable) or not set(immutable) <= set(spans):
        raise ValueError("invalid_immutable_span_ids")
    for span in spans.values():
        _check_temporal(span.get("temporal"), cut)
        observation = observations[span["observation"]]
        if span.get("observation_text_hash") != hashlib.sha256(observation["text"].encode("utf-8")).hexdigest():
            raise ValueError("span_text_hash_mismatch")
        if span.get("locator") != observation.get("locator", {}):
            raise ValueError("span_locator_mismatch")
    for ident, context in contexts.items():
        _check_temporal(context.get("temporal"), cut)
        if not _text(context.get("selection_reason")) or not isinstance(context.get("source_groups"), list):
            raise ValueError("context_metadata_required")
        basis = claims[ident]["assessment"].get("basis", {})
        support = basis.get("support", []) if isinstance(basis, dict) else None
        dependencies = context.get("observation_dependencies")
        if not isinstance(support, list) or not isinstance(dependencies, list) or len(support) != len(dependencies):
            raise ValueError("context_support_dependencies_mismatch")
        for item, dependency in zip(support, dependencies):
            if not isinstance(item, dict) or not isinstance(dependency, dict) or item.get("observation") not in observations:
                raise ValueError("unknown_context_support_observation")
            if dependency.get("observation") != item["observation"] or dependency.get("support") != item:
                raise ValueError("context_support_dependencies_mismatch")
            _check_temporal(dependency.get("temporal"), cut)
    for thread in threads.values():
        if not isinstance(thread.get("record"), dict) or thread["record"].get("id") != thread["id"]:
            raise ValueError("thread_record_identity_mismatch")
        _check_temporal(thread.get("temporal"), cut)
    for entity in entity_times.values():
        _check_temporal(entity.get("temporal"), cut)
    # This cannot authenticate an unavailable base snapshot, nor establish that
    # the selected evidence semantically supports its claims.
    return contexts


def _instant(value):
    if not isinstance(value, str):
        return None
    try:
        stamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
        return stamp.astimezone(timezone.utc) if stamp.tzinfo else None
    except ValueError:
        return None


def _check_temporal(value, cut):
    if not isinstance(value, dict) or set(value) != {"observed_at", "status"}:
        raise ValueError("temporal_metadata_required")
    stamp = _instant(value["observed_at"])
    expected = "unknown" if stamp is None else "future" if stamp > cut else "at_or_before_cut"
    if value["status"] != expected or expected == "future":
        raise ValueError("temporal_status_mismatch_or_future_context")


def make_score_request(packet, candidate_claim_ids, *, question):
    """Bind selected existing Claims and exact question/rubric content."""
    try:
        contexts = _check_packet(packet)
        _bounded(question)
        if not isinstance(question, dict) or set(question) != {"id", "text", "rubric_id", "rubric"} or not all(_text(v) for v in question.values()):
            raise ValueError("question_requires_id_text_rubric_id_rubric")
        if not isinstance(candidate_claim_ids, list) or len(candidate_claim_ids) > 256 or any(not _text(i) for i in candidate_claim_ids):
            raise ValueError("candidate_ids_require_bounded_string_array")
        if len(set(candidate_claim_ids)) != len(candidate_claim_ids):
            raise ValueError("duplicate_candidate_id")
        if not set(candidate_claim_ids) <= set(contexts):
            raise ValueError("unknown_candidate_id")
        identity = {"schema": "loom.context_scores_request/1", "packet_hash": packet["packet_hash"],
                    "candidate_ids": list(candidate_claim_ids), "question_hash": _hash(question)}
        return {"status": "ready", **identity, "request_hash": _hash(identity),
                "question": deepcopy(question), "source_packet": deepcopy(packet), "errors": []}
    except (ValueError, TypeError, KeyError, UnicodeError, OverflowError, RecursionError, AttributeError) as error:
        return {"status": "invalid", "schema": "loom.context_scores_request/1", "errors": [str(error)]}


def _suggestion(probability, keep, drop):
    return "keep" if probability >= keep else "drop" if probability <= drop else "review"


def replay_context_scores(packet, candidate_claim_ids, response, *, question, keep_threshold, drop_threshold):
    """Suggest per-candidate context priority; never remove or promote a Claim."""
    request = make_score_request(packet, candidate_claim_ids, question=question)
    report = {"schema": "loom.context_scores_replay/1", "version": VERSION, "status": "invalid", "errors": [],
              "rows": [], "no_inference": True, "no_persistence": True, "semantic_quality": None}
    if request["status"] != "ready":
        report["errors"] = request["errors"]
        return report
    report.update({key: deepcopy(request[key]) for key in ("packet_hash", "request_hash", "question_hash", "question", "candidate_ids", "source_packet")})
    report["thresholds"] = {"keep": keep_threshold if _number(keep_threshold) else None,
                            "drop": drop_threshold if _number(drop_threshold) else None}
    contexts = {c["claim_id"]: c for c in packet["context_claims"]}
    report["rows"] = [{"candidate_id": ident, "probability": None, "suggestion": "review", "score_status": "unknown",
                       "reason": "response_rejected", "context_claim": deepcopy(contexts[ident]),
                       "current_span_ids": [s["id"] for s in packet["current_spans"]]} for ident in candidate_claim_ids]
    try:
        if not _number(keep_threshold) or not _number(drop_threshold) or drop_threshold >= keep_threshold:
            raise ValueError("thresholds_require_0_le_drop_lt_keep_le_1")
        _bounded(response)
        if not isinstance(response, dict) or set(response) - {"schema", "packet_hash", "request_hash", "question_hash", "rows", "provider"}:
            raise ValueError("invalid_response_envelope")
        if response.get("schema") != "loom.context_scores_response/1":
            raise ValueError("unsupported_response_schema")
        for key in ("packet_hash", "request_hash", "question_hash"):
            if response.get(key) != request[key]:
                raise ValueError(key + "_mismatch")
        rows = response.get("rows")
        if not isinstance(rows, list) or len(rows) > 256:
            raise ValueError("response_rows_require_bounded_array")
        answers = {}
        for row in rows:
            if not isinstance(row, dict) or not _text(row.get("candidate_id")):
                raise ValueError("response_row_identity_missing")
            ident = row["candidate_id"]
            if ident not in contexts or ident not in candidate_claim_ids:
                raise ValueError("unknown_response_candidate_id")
            if ident in answers:
                raise ValueError("duplicate_response_candidate_id")
            answers[ident] = row
        provider = response.get("provider", {})
        if not isinstance(provider, dict):
            raise ValueError("provider_metadata_requires_object")
        canonical(provider)  # Nonfinite metadata cannot be faithfully saved as JSON.
        report["provider"] = deepcopy(provider)
        for row in report["rows"]:
            answer = answers.get(row["candidate_id"])
            if answer is None:
                row["reason"] = "missing_answer"
            elif set(answer) != {"candidate_id", "probability"} or not _number(answer.get("probability")):
                row["reason"] = "invalid_answer"
            else:
                probability = answer["probability"]
                row.update(probability=probability, suggestion=_suggestion(probability, keep_threshold, drop_threshold),
                           score_status="measured", reason="saved_independent_score")
        report["status"] = "ready"
    except (ValueError, TypeError, KeyError, UnicodeError, OverflowError, RecursionError, AttributeError) as error:
        report["errors"].append(str(error))
    report["counts"] = {name: sum(row["suggestion"] == name for row in report["rows"]) for name in ("keep", "review", "drop")}
    report["counts"]["unknown"] = sum(row["score_status"] == "unknown" for row in report["rows"])
    return report


def _check_replay(report):
    """Rebuild supplied outputs so forged/incomplete rows cannot skew comparison."""
    if not isinstance(report, dict) or report.get("schema") != "loom.context_scores_replay/1" or report.get("status") != "ready":
        raise ValueError("comparison_requires_ready_replays")
    _bounded(report)
    request = make_score_request(report.get("source_packet"), report.get("candidate_ids"), question=report.get("question"))
    if request["status"] != "ready":
        raise ValueError("invalid_comparison_request")
    for key in ("packet_hash", "question_hash", "request_hash"):
        if report.get(key) != request[key]:
            raise ValueError("comparison_" + key + "_mismatch")
    thresholds = report.get("thresholds", {})
    keep, drop = thresholds.get("keep"), thresholds.get("drop")
    if not _number(keep) or not _number(drop) or drop >= keep:
        raise ValueError("invalid_comparison_thresholds")
    rows = _index(report.get("rows"), "candidate_id")
    if set(rows) != set(request["candidate_ids"]):
        raise ValueError("comparison_candidate_coverage_mismatch")
    contexts = {c["claim_id"]: c for c in request["source_packet"]["context_claims"]}
    for ident, row in rows.items():
        if row.get("context_claim") != contexts[ident] or row.get("current_span_ids") != [s["id"] for s in request["source_packet"]["current_spans"]]:
            raise ValueError("comparison_source_handle_mismatch")
        if row.get("score_status") == "unknown":
            if row.get("probability") is not None or row.get("suggestion") != "review" or row.get("reason") not in ("missing_answer", "invalid_answer"):
                raise ValueError("invalid_unknown_comparison_row")
        elif row.get("score_status") == "measured":
            if not _number(row.get("probability")) or row.get("suggestion") != _suggestion(row["probability"], keep, drop):
                raise ValueError("invalid_measured_comparison_row")
        else:
            raise ValueError("invalid_comparison_score_status")
    return rows


def compare_score_runs(left, right, mapping):
    """Compare declared corresponding candidates; mapping is no semantic proof."""
    report = {"schema": "loom.context_scores_comparison/1", "status": "invalid", "errors": [],
              "semantic_quality": None, "correspondence": "caller_declared_not_verified"}
    try:
        a, b = _check_replay(left), _check_replay(right)
        if left["question_hash"] != right["question_hash"] or left["thresholds"] != right["thresholds"]:
            raise ValueError("comparison_question_or_threshold_mismatch")
        if not isinstance(mapping, dict) or len(mapping) > 256 or any(not _text(k) or not _text(v) for k, v in mapping.items()):
            raise ValueError("mapping_requires_string_pairs")
        if set(mapping) != set(a) or set(mapping.values()) != set(b) or len(set(mapping.values())) != len(mapping):
            raise ValueError("mapping_requires_complete_bijection")
        pairs = []
        for ident in left["candidate_ids"]:
            other = mapping[ident]
            measured = a[ident]["score_status"] == b[other]["score_status"] == "measured"
            pairs.append({"left_id": ident, "right_id": other, "both_measured": measured,
                          "suggestion_equal": a[ident]["suggestion"] == b[other]["suggestion"],
                          "absolute_score_drift": abs(a[ident]["probability"] - b[other]["probability"]) if measured else None})
        drifts = [p["absolute_score_drift"] for p in pairs if p["both_measured"]]
        report.update(status="ready", pairs=pairs, candidate_count=len(pairs), measured_pairs=len(drifts),
                      suggestion_agreement=sum(p["suggestion_equal"] for p in pairs) / len(pairs) if pairs else None,
                      mean_absolute_score_drift=sum(drifts) / len(drifts) if drifts else None,
                      max_absolute_score_drift=max(drifts) if drifts else None,
                      left_request_hash=left["request_hash"], right_request_hash=right["request_hash"])
    except (ValueError, TypeError, KeyError, UnicodeError, OverflowError, RecursionError, AttributeError) as error:
        report["errors"].append(str(error))
    return report


def example():
    """A mechanical illustration with authored scores, not model output."""
    old, current = "Aster and Beacon share storage.", "Compare both storage plans."
    snapshot = {"snapshot_id": "example", "observations": [{"id": "old", "text": old}, {"id": "now", "text": current}],
                "claims": [{"id": ident, "subject": ident + "_entity", "predicate": "uses", "value": "storage",
                            "assessment": {"status": "contested", "basis": {"support": [{"observation": "old", "quote": old}]}}}
                           for ident in ("aster", "beacon")], "entities": []}
    packet = prepare_context(snapshot, [{"id": "current", "observation": "now", "byte_start": 0,
                                        "byte_len": len(current.encode()), "quote": current}],
                             [{"claim_id": i, "selection_reason": "example candidate"} for i in ("aster", "beacon")],
                             time_cut="2026-09-28T00:00:00Z")["packet"]
    question = {"id": "context_relevance", "text": "Is this candidate useful for the current request?",
                "rubric_id": "example/1", "rubric": "Score each candidate independently; both may be useful."}
    candidates = ["aster", "beacon"]
    request = make_score_request(packet, candidates, question=question)
    response = {"schema": "loom.context_scores_response/1", **{k: request[k] for k in ("packet_hash", "request_hash", "question_hash")},
                "rows": [{"candidate_id": "aster", "probability": 0.9}, {"candidate_id": "beacon", "probability": 0.85}],
                "provider": {"kind": "authored_example_not_inference"}}
    return {"packet": packet, "candidate_claim_ids": candidates, "response": response, "question": question,
            "keep_threshold": 0.8, "drop_threshold": 0.2}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("replay", "compare", "example"))
    parser.add_argument("input", nargs="?", help="JSON file, or - for stdin")
    args = parser.parse_args(argv)
    try:
        if args.operation == "example":
            result = example()
        else:
            if not args.input:
                raise ValueError("input_required")
            with (sys.stdin.buffer if args.input == "-" else Path(args.input).open("rb")) as stream:
                raw = stream.read(4 * 1024 * 1024 + 1)
            if len(raw) > 4 * 1024 * 1024:
                raise ValueError("input_byte_limit")
            def object_pairs(pairs):
                obj = {}
                for key, value in pairs:
                    if key in obj:
                        raise ValueError("duplicate_json_key")
                    obj[key] = value
                return obj
            payload = json.loads(raw, object_pairs_hook=object_pairs)
            if not isinstance(payload, dict):
                raise ValueError("input_requires_object")
            result = replay_context_scores(**payload) if args.operation == "replay" else compare_score_runs(**payload)
        print(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False))
        return 0 if result.get("status", "ready") == "ready" else 2
    except (ValueError, TypeError, OSError, RecursionError, UnicodeError) as error:
        print(json.dumps({"status": "invalid", "errors": [str(error)]}, ensure_ascii=True))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
