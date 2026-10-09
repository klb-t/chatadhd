"""Offline research analysis: bound evidence, missingness and family-level pairs.

No inference, payer, source summarizer, profile engine or settings adoption.
Inputs may be private. Public output uses the separate allowlisted presentation
projection; never publish evaluation records, source locators or diagnostics raw.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from copy import deepcopy
from decimal import Decimal
import gzip
import hashlib
import json
import math
from pathlib import Path
import random
import re
import statistics
import tempfile

from loom.tools.seeding import method_graph as graph

ROOT = Path(__file__).resolve().parents[3]


def require(condition, code):
    if not condition:
        raise ValueError(code)


def digest(value):
    return hashlib.sha256(graph.canonical(value)).hexdigest()


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def metric(value=None, *, reason="not_measured", evaluator="mechanical", evidence=None,
           reference_status="not_applicable", numerator=None, denominator=None):
    require(value is None or finite(value), "invalid_metric_value")
    return {"value": value, "status": "missing" if value is None else "observed",
            "missing_reason": reason if value is None else None,
            "evaluator_class": evaluator, "reference_status": reference_status,
            "numerator": numerator, "denominator": denominator,
            "evidence": [] if evidence is None else deepcopy(evidence)}


def validate_protocol(protocol):
    require(protocol.get("schema") == "loom.research_evaluation_protocol/1", "protocol_schema")
    require(protocol.get("reference_status") in ("provisional", "independently_adjudicated"), "reference_status")
    require(protocol.get("owner_approved") is False, "approval_not_established_by_protocol")
    ids = [x["id"] for x in protocol["metrics"]]
    require(ids and len(ids) == len(set(ids)), "metric_ids")
    require(all(x.get("evaluator_classes") for x in protocol["metrics"]), "evaluator_classes")
    require(protocol["pairing"]["unit"] == "conversation_family", "family_unit_required")
    uncertainty = protocol["pairing"]["uncertainty"]
    require(type(uncertainty["minimum_families"]) is int and uncertainty["minimum_families"] >= 2, "minimum_families")
    require(type(uncertainty["bootstrap_samples"]) is int and uncertainty["bootstrap_samples"] > 0, "bootstrap_samples")
    require(0 < uncertainty["alpha"] < 1, "uncertainty_alpha")
    return protocol


def validate_binding(binding):
    for key in ("operation_id", "family", "question_id", "variant_id", "panel_id", "split"):
        require(isinstance(binding.get(key), str) and bool(binding[key]), "binding_identity")
    require(binding["split"] in ("development", "validation", "used_blind"), "binding_split")
    for key in ("source_sha256", "view_sha256", "request_sha256"):
        if key != "source_sha256" and binding.get(key) is None and binding.get("preparation_status") in ("pending", "blocked", "dependency_pending"):
            continue
        require(isinstance(binding.get(key), str) and re.fullmatch(r"[a-f0-9]{64}", binding[key]) is not None, "binding_hash")
    require(type(binding.get("repetition")) is int and binding["repetition"] >= 0, "binding_repetition")


def select_response_form(protocol, forms, name, *, stage=None):
    """Resolve caller form data to a new immutable protocol value/hash."""
    selected = deepcopy(forms["forms"][name])
    if "stage_one" in selected:
        require(stage in ("stage_one", "stage_two"), "response_stage_required")
        selected = selected[stage]
    else:
        require(stage is None, "unexpected_response_stage")
    result = deepcopy(protocol)
    result["response_form"] = {"name": name, "stage": stage, "forms_sha256": digest(forms)}
    if selected["format"] == "exact_text_bytes":
        result["mechanical"]["format"] = {"encoding": "utf8_text"}
        result["mechanical"]["graph"] = None
    else:
        result["mechanical"]["format"] = selected["format"]
        result["mechanical"]["graph"]["pointer"] = selected["graph_pointer"]
        if "graph_format" in selected:
            result["mechanical"]["graph"]["format"] = selected["graph_format"]
        if "reference_paths" in selected:
            result["mechanical"]["graph"]["reference_paths"] = selected["reference_paths"]
    return validate_protocol(result)


def _graph_metrics(document, rule, source_ids, evidence):
    """Syntax/integrity/reference checks only; an existing ID is not truth."""
    missing = (metric(reason="graph_unavailable", evidence=evidence),
               metric(reason="graph_unavailable", evidence=evidence))
    try:
        payload = graph.pointer(document, rule["pointer"])
    except (KeyError, TypeError, IndexError, ValueError):
        return missing
    if rule["format"] == "native_graph_packet":
        try:
            graph.codec.validate_packet(payload)
            integrity = metric(1, evidence=evidence)
        except (KeyError, TypeError, ValueError, AssertionError):
            integrity = metric(0, evidence=evidence)
        # Native integrity and source attribution are independent contracts.
        refs = []
        for path in rule.get("reference_paths", []):
            try:
                refs.extend(row for _, row in graph.walk(payload, path))
            except (KeyError, TypeError, ValueError, IndexError):
                pass  # Malformed reply shape is an observed failure, not a crash.
    else:
        require(rule["format"] == "nodes_edges", "unsupported_graph_format")
        try:
            nodes = graph.pointer(payload, rule["nodes_pointer"])
            edges = graph.pointer(payload, rule["edges_pointer"])
            require(isinstance(nodes, list) and isinstance(edges, list), "graph_arrays")
            ids = [graph.pointer(n, rule["node_id_pointer"]) for n in nodes]
            require(all(isinstance(n, str) and n for n in ids), "graph_node_id")
            require(len(set(ids)) == len(ids), "graph_duplicate_id")
            known = set(ids)
            require(all(graph.pointer(e, rule["edge_source_pointer"]) in known and
                        graph.pointer(e, rule["edge_target_pointer"]) in known for e in edges), "graph_dangling_edge")
            integrity = metric(1, evidence=evidence)
        except (KeyError, TypeError, ValueError, IndexError):
            integrity = metric(0, evidence=evidence)
        refs = []
        for path in rule.get("reference_paths", []):
            try:
                refs.extend(row for _, row in graph.walk(payload, path))
            except (KeyError, TypeError, ValueError, IndexError):
                pass
    if not refs:
        references = metric(reason="no_source_references", evidence=evidence, denominator=0)
    else:
        count = sum(isinstance(ref, str) and ref in source_ids for ref in refs)
        references = metric(count / len(refs), evidence=evidence, numerator=count, denominator=len(refs))
    return integrity, references


def evaluate_response(raw, binding, protocol, *, source_ids=(), assessments=(), instrumentation=None):
    """Measure available dimensions; semantic ratings are explicitly imported.

    Each imported judgement must bind this protocol, source, view, request and
    exact response bytes. These checks establish attribution, not truth or the
    identity/independence of the named evaluator. That distinction is retained.
    """
    validate_protocol(protocol)
    validate_binding(binding)
    require(raw is None or isinstance(raw, bytes), "response_requires_exact_bytes")
    require(raw is None or (binding.get("view_sha256") is not None and binding.get("request_sha256") is not None),
            "response_without_frozen_request")
    response_hash = None if raw is None else hashlib.sha256(raw).hexdigest()
    evidence = [{"source_sha256": binding["source_sha256"], "view_sha256": binding["view_sha256"],
                 "request_sha256": binding["request_sha256"], "response_sha256": response_hash}]
    result = {"schema": "loom.research_evaluation_record/1", **deepcopy(binding),
              "response_sha256": response_hash, "protocol_sha256": digest(protocol),
              "response_hash_scope": "evaluated_reply_bytes_not_transport_envelope",
              "measurement_kind": binding.get("measurement_kind", "unclassified"),
              "reference_status": protocol["reference_status"], "owner_approved": False,
              "metrics": {x["id"]: {} for x in protocol["metrics"]},
              "instrumentation": deepcopy(instrumentation or {})}
    for definition in protocol["metrics"]:
        for evaluator in definition["evaluator_classes"]:
            result["metrics"][definition["id"]][evaluator] = metric(
                reason="response_missing" if raw is None else "evaluation_not_performed",
                evaluator=evaluator, reference_status=protocol["reference_status"], evidence=evidence)
    document = None
    if raw is not None:
        try:
            rule = protocol["mechanical"]["format"]
            if rule.get("encoding") == "utf8_text":
                raw.decode("utf-8")
                valid = True
            else:
                document = graph.strict_json(raw)
                valid = isinstance(document, dict) and set(rule["required_keys"]) <= set(document)
                # Required shape is distinct from native execution.
                for path, kind in rule.get("types", {}).items():
                    value = graph.pointer(document, path)
                    valid = valid and {"string": isinstance(value, str), "object": isinstance(value, dict),
                                       "array": isinstance(value, list)}[kind]
            result["metrics"]["format"]["mechanical"] = metric(int(valid), evidence=evidence)
        except (json.JSONDecodeError, UnicodeDecodeError, ValueError, KeyError, TypeError, IndexError):
            result["metrics"]["format"]["mechanical"] = metric(0, evidence=evidence)
        if document is not None and protocol["mechanical"]["graph"] is not None:
            integrity, references = _graph_metrics(document, protocol["mechanical"]["graph"], set(source_ids), evidence)
            result["metrics"]["graph_integrity"]["mechanical"] = integrity
            result["metrics"]["source_references"]["mechanical"] = references
    required = {"protocol_sha256": digest(protocol), "source_sha256": binding["source_sha256"],
                "view_sha256": binding["view_sha256"], "request_sha256": binding["request_sha256"],
                "response_sha256": response_hash}
    seen = set()
    for assessment in assessments:
        require(raw is not None, "judgement_without_response")
        require(all(assessment.get(k) == v for k, v in required.items()), "assessment_binding_mismatch")
        name, evaluator = assessment["metric_id"], assessment["evaluator_class"]
        require(evaluator in ("assistant", "model", "independent_adjudicator"), "invalid_semantic_evaluator")
        require(name in result["metrics"] and evaluator in result["metrics"][name], "unplanned_assessment")
        require((name, evaluator) not in seen, "duplicate_assessment_requires_new_version")
        require(bool(assessment.get("evaluator_id")) and bool(assessment.get("source_locators")), "assessment_evidence_required")
        require(assessment.get("reference_status") == protocol["reference_status"], "assessment_reference_status")
        if evaluator == "independent_adjudicator":
            require(assessment.get("independence_evidence_sha256") is not None, "independence_evidence_required")
            require(re.fullmatch(r"[a-f0-9]{64}", assessment["independence_evidence_sha256"]) is not None, "independence_evidence_hash")
        value = assessment.get("value")
        require(value is None or (finite(value) and 0 <= value <= 1), "semantic_metric_range")
        measured = metric(value, reason=assessment.get("missing_reason", "unresolved"), evaluator=evaluator,
                          reference_status=protocol["reference_status"], evidence=[deepcopy(assessment)])
        measured["evaluator_identity_independently_verified"] = False
        result["metrics"][name][evaluator] = measured
        seen.add((name, evaluator))
    # Cost/latency are copied from verified observations only, never inferred
    # from status, timeout, an estimate, absent bill or current account balance.
    inst = result["instrumentation"]
    for name, key, proof in (("cost_usd", "cost_usd", "payer_cost_verified"),
                             ("latency_seconds", "latency_seconds", "elapsed_measured")):
        value = inst.get(key)
        if value is not None and inst.get(proof) is True:
            try:
                converted = Decimal(str(value))
                require(converted.is_finite() and converted >= 0, "invalid_instrumentation")
            except Exception as exc:
                raise ValueError("invalid_instrumentation") from None
            result["metrics"][name]["mechanical"] = metric(float(converted), evidence=evidence)
        else:
            result["metrics"][name]["mechanical"] = metric(reason="measurement_or_verification_missing", evidence=evidence)
    return result


def _bound_records(planned, records):
    plans = {}
    families = {}
    for plan in planned:
        validate_binding(plan)
        require(plan["operation_id"] not in plans, "duplicate_planned_operation")
        for key in (("family", plan["family"]), ("source", plan["source_sha256"])):
            require(key not in families or families[key] == plan["split"], "family_split_leakage")
            families[key] = plan["split"]
        plans[plan["operation_id"]] = plan
    observed = {}
    bind_keys = ("family", "question_id", "variant_id", "panel_id", "split", "source_sha256", "view_sha256", "request_sha256", "repetition")
    for record in records:
        key = record["operation_id"]
        require(key in plans, "unplanned_result")
        require(key not in observed, "duplicate_result_requires_evidence_revision")
        require(all(record.get(k) == plans[key][k] for k in bind_keys), "result_binding_mismatch")
        observed[key] = record
    return plans, observed


def _value(record, name, evaluator, protocol_sha=None, measurement_kinds=None):
    if record is None or (protocol_sha and record.get("protocol_sha256") != protocol_sha):
        return None
    if measurement_kinds is not None and record.get("measurement_kind") not in measurement_kinds:
        return None
    value = record.get("metrics", {}).get(name, {}).get(evaluator, {})
    if value.get("status") != "observed" or value.get("evaluator_class") != evaluator:
        return None
    return value["value"] if finite(value.get("value")) else None


def paired_comparison(planned, records, left, right, name, evaluator, protocol, *, measurement_kinds=None, arm_protocols=None):
    """Equal source/question/repetition pairing; equal weight per family.

    Missing slots stay in the denominator. Complete repetitions are first
    averaged per question, questions per family, then families. Family bootstrap
    describes observed paired families only; it is not a population guarantee.
    """
    validate_protocol(protocol)
    require(left != right, "identical_comparison_arms")
    arm_protocols = {left: protocol, right: protocol} if arm_protocols is None else arm_protocols
    require(set(arm_protocols) == {left, right}, "arm_protocol_mapping")
    metric_definitions = [x for x in protocol["metrics"] if x["id"] == name]
    require(len(metric_definitions) == 1 and evaluator in metric_definitions[0]["evaluator_classes"], "comparison_metric_definition")
    for arm_protocol in arm_protocols.values():
        validate_protocol(arm_protocol)
        require([x for x in arm_protocol["metrics"] if x["id"] == name] == metric_definitions, "incompatible_metric_definitions")
        for field in ("reference_status", "semantic_rubric", "pairing", "source_binding"):
            require(arm_protocol.get(field) == protocol.get(field), "incompatible_comparison_protocol")
    arm_hashes = {arm: digest(definition) for arm, definition in arm_protocols.items()}
    plans, observed = _bound_records(planned, records)
    slots = defaultdict(dict)
    for operation, p in plans.items():
        if p["variant_id"] not in (left, right):
            continue
        key = (p["panel_id"], p["split"], p["family"], p["source_sha256"], p["question_id"], p["repetition"])
        require(p["variant_id"] not in slots[key], "duplicate_pair_slot")
        slots[key][p["variant_id"]] = operation
    require(bool(slots), "empty_paired_scope")
    question_deltas = defaultdict(list)
    missing = {"unplanned_arm": 0, "missing_or_ineligible_measurement": 0}
    paired = []
    for key, arms in slots.items():
        if left not in arms or right not in arms:
            missing["unplanned_arm"] += 1
            continue
        a = _value(observed.get(arms[left]), name, evaluator, arm_hashes[left], measurement_kinds)
        b = _value(observed.get(arms[right]), name, evaluator, arm_hashes[right], measurement_kinds)
        if a is None or b is None:
            missing["missing_or_ineligible_measurement"] += 1
            continue
        delta = b - a
        question_deltas[key[:-1]].append(delta)
        paired.append({"panel_id": key[0], "split": key[1], "family": key[2], "source_sha256": key[3],
                       "question_id": key[4], "repetition": key[5], "left": a, "right": b, "delta": delta})
    family_questions = defaultdict(list)
    for (panel, split, family, source, question), deltas in question_deltas.items():
        family_questions[(panel, split, family)].append(statistics.mean(deltas))
    family_values = {key: statistics.mean(values) for key, values in family_questions.items()}
    # Do not combine panels or development/validation cohorts into one estimate.
    strata = defaultdict(list)
    planned_families = defaultdict(set)
    for panel, split, family, *_ in slots:
        planned_families[(panel, split)].add(family)
    for (panel, split, _family), delta in family_values.items():
        strata[(panel, split)].append(delta)
    cohorts = []
    for key, planned_family_set in sorted(planned_families.items()):
        values = strata[key]
        policy = protocol["pairing"]["uncertainty"]
        interval = None
        reason = "insufficient_families"
        if len(values) >= policy["minimum_families"]:
            rng = random.Random(policy["seed"])
            samples = sorted(statistics.mean(rng.choices(values, k=len(values))) for _ in range(policy["bootstrap_samples"]))
            interval = [samples[int((len(samples)-1)*policy["alpha"]/2)], samples[int((len(samples)-1)*(1-policy["alpha"]/2))]]
            reason = None
        cohorts.append({"panel_id": key[0], "split": key[1], "planned_families": len(planned_family_set),
                        "observed_paired_families": len(values), "missing_families": len(planned_family_set)-len(values),
                        "family_mean_delta": statistics.mean(values) if values else None,
                        "family_delta_range": [min(values), max(values)] if values else None,
                        "family_bootstrap_interval": interval, "uncertainty_missing_reason": reason,
                        "independent_validation": False if key[1] != "validation" else None})
    return {"schema": "loom.research_paired_comparison/1", "left": left, "right": right,
            "metric": name, "evaluator_class": evaluator, "delta_direction": "right_minus_left",
            "arm_protocol_sha256": arm_hashes,
            "comparison_definition_sha256": digest({"protocol": protocol, "arm_protocol_hashes": arm_hashes,
                                                        "metric": name, "evaluator": evaluator}),
            "planned_pair_slots": len(slots), "observed_pair_slots": len(paired), "missing": missing,
            "question_count": len({key[:-1] for key in slots}),
            "unit_of_independence": "conversation_family", "cohorts": cohorts,
            "paired_rows": paired, "global_winner": None, "policy_adopted": False,
            "uncertainty_scope": "conditional_on_observed_paired_families; missingness_may_be_informative"}


def stability(planned, records, *, measurement_kinds=("new_real_export_inference",)):
    plans, observed = _bound_records(planned, records)
    groups = defaultdict(list)
    for operation, p in plans.items():
        groups[(p["panel_id"], p["split"], p["family"], p["source_sha256"], p["question_id"], p["variant_id"],
                p["view_sha256"], p["request_sha256"], observed.get(operation, {}).get("protocol_sha256"))].append(operation)
    output = []
    for key, operations in sorted(groups.items(), key=lambda row: graph.canonical(row[0])):
        hashes, attempt_ids = [], set()
        for operation in operations:
            row = observed.get(operation, {})
            inst = row.get("instrumentation", {})
            attempt = inst.get("provider_attempt_id")
            if row.get("measurement_kind") not in measurement_kinds or inst.get("response_cache_hit") is not False or not attempt or attempt in attempt_ids:
                continue
            value = row.get("response_sha256")
            if isinstance(value, str) and re.fullmatch(r"[a-f0-9]{64}", value):
                hashes.append(value)
                attempt_ids.add(attempt)
        pairs = len(hashes)*(len(hashes)-1)//2
        agreements = sum(hashes[i] == hashes[j] for i in range(len(hashes)) for j in range(i+1, len(hashes)))
        output.append({"panel_id": key[0], "split": key[1], "family": key[2], "source_sha256": key[3],
                       "question_id": key[4], "variant_id": key[5], "planned_occurrences": len(operations),
                       "view_sha256": key[6], "request_sha256": key[7], "protocol_sha256": key[8],
                       "eligible_separate_attempts": len(hashes), "missing_or_ineligible": len(operations)-len(hashes),
                       "exact_response_byte_agreement": agreements/pairs if pairs else None,
                       "observed_response_pairs": pairs, "semantic_stability": None,
                       "independent_inference_guaranteed": False})
    return output


def compose_two_stage_outcome(first_plan, first_record, second_plan, second_record, protocol, *,
                              first_response_bytes=None, second_request_bytes=None, dependency=None):
    """One analytical slot for text then structure, preserving both phase proofs.

    The returned hashes identify a derived analysis record, not a transport
    request/response. When a second response exists, its exact request must bind
    the first response at a caller-declared JSON pointer, checked byte-for-byte.
    Pending extraction has no invented request/response. Semantic assessment of
    the combined deliverable stays missing; phase judgements remain inspectable.
    """
    validate_protocol(protocol)
    validate_binding(first_plan)
    plans = [first_plan] + ([] if second_plan is None else [second_plan])
    records = [row for row in (first_record, second_record) if row is not None]
    _bound_records(plans, records)
    require(second_record is None or second_plan is not None, "second_response_without_plan")
    require(second_record is None or first_record is not None, "extraction_without_first_response")
    identity_keys = ("panel_id", "split", "family", "source_sha256", "view_sha256", "question_id", "variant_id", "repetition")
    if second_plan is not None:
        require(all(first_plan[k] == second_plan[k] for k in identity_keys), "stage_identity_mismatch")
        require(first_plan["operation_id"] != second_plan["operation_id"], "stage_operations_not_distinct")
    if second_record is not None:
        require(isinstance(first_response_bytes, bytes) and isinstance(second_request_bytes, bytes), "stage_binding_bytes_required")
        require(hashlib.sha256(first_response_bytes).hexdigest() == first_record["response_sha256"], "first_response_bytes_mismatch")
        require(hashlib.sha256(second_request_bytes).hexdigest() == second_plan["request_sha256"], "second_request_bytes_mismatch")
        require(isinstance(dependency, dict), "stage_dependency_required")
        expected = {"first_operation_id": first_plan["operation_id"], "first_response_sha256": first_record["response_sha256"],
                    "second_operation_id": second_plan["operation_id"], "second_request_sha256": second_plan["request_sha256"]}
        require(all(dependency.get(k) == v for k, v in expected.items()), "stage_dependency_mismatch")
        try:
            consumed = graph.pointer(graph.strict_json(second_request_bytes), dependency["input_json_pointer"])
            for decoding in dependency.get("input_decoding", []):
                require(decoding.get("format") == "json" and isinstance(consumed, str), "unsupported_dependency_decoding")
                consumed = graph.pointer(graph.strict_json(consumed.encode("utf-8")), decoding["pointer"])
            require(isinstance(consumed, str) and consumed.encode("utf-8") == first_response_bytes, "stage_dependency_input_mismatch")
        except (KeyError, ValueError, TypeError, UnicodeError, IndexError):
            raise ValueError("stage_dependency_input_mismatch") from None
    request_binding = {"kind": "two_stage_request_binding", "version": 1,
                       "first": first_plan, "second": second_plan, "dependency": dependency}
    outcome_plan = {k: deepcopy(first_plan[k]) for k in identity_keys}
    outcome_plan["request_sha256"] = digest(request_binding)
    outcome_plan["request_hash_scope"] = "analysis_stage_binding_not_transport_request"
    outcome_plan["operation_id"] = "analysis-two-stage:" + digest(request_binding)
    kinds = {r.get("measurement_kind") for r in records}
    outcome_plan["measurement_kind"] = ("composed_real_export_outcome" if kinds == {"new_real_export_inference"}
                                         else "composed_unclassified_or_replay_outcome")
    record = evaluate_response(None, outcome_plan, protocol)
    record["request_hash_scope"] = outcome_plan["request_hash_scope"]
    record["response_hash_scope"] = "analysis_phase_capture_not_model_response"
    record["response_sha256"] = digest({"first_record": first_record, "second_record": second_record}) if records else None
    record["phase_records"] = {"generation": deepcopy(first_record), "extraction": deepcopy(second_record)}
    record["dependency"] = deepcopy(dependency)
    record["status"] = ("first_response_pending" if first_record is None or first_record.get("response_sha256") is None else
                         "extraction_pending" if second_record is None or second_record.get("response_sha256") is None else
                         "phase_responses_captured_not_semantically_validated")
    # Local formatting and graph integrity are observable on the emitted phases.
    format_values = [_value(r, "format", "mechanical") for r in (first_record, second_record)]
    if all(v is not None for v in format_values):
        record["metrics"]["format"]["mechanical"] = metric(min(format_values))
    for name in ("graph_integrity", "source_references"):
        value = _value(second_record, name, "mechanical")
        record["metrics"][name]["mechanical"] = metric(value, reason="extraction_measurement_missing")
    inst = {"cost_usd": None, "payer_cost_verified": False, "latency_seconds": None, "elapsed_measured": False,
            "observed_stage_cost_subtotal_usd": "0", "cost_unknown_stages": 2,
            "latency_scope": "sum_of_sequential_phase_request_durations_excludes_queue_wait",
            "exactly_once_inference_guaranteed": False}
    costs, times = [], []
    for phase in (first_record, second_record):
        p = (phase or {}).get("instrumentation", {})
        if p.get("cost_usd") is not None and p.get("payer_cost_verified") is True:
            v = Decimal(str(p["cost_usd"]))
            require(v.is_finite() and v >= 0, "invalid_instrumentation")
            costs.append(v)
        if p.get("latency_seconds") is not None and p.get("elapsed_measured") is True:
            require(finite(p["latency_seconds"]) and p["latency_seconds"] >= 0, "invalid_instrumentation")
            times.append(p["latency_seconds"])
    inst["observed_stage_cost_subtotal_usd"] = str(sum(costs, Decimal(0)))
    inst["cost_unknown_stages"] = 2-len(costs)
    if len(costs) == 2:
        inst["cost_usd"], inst["payer_cost_verified"] = str(sum(costs, Decimal(0))), True
        record["metrics"]["cost_usd"]["mechanical"] = metric(float(sum(costs, Decimal(0))))
    if len(times) == 2:
        inst["latency_seconds"], inst["elapsed_measured"] = sum(times), True
        record["metrics"]["latency_seconds"]["mechanical"] = metric(sum(times))
    record["instrumentation"] = inst
    return outcome_plan, record


def measurement_summary(planned, records):
    plans, observed = _bound_records(planned, records)
    costs, latencies = [], []
    for operation in plans:
        inst = observed.get(operation, {}).get("instrumentation", {})
        value = inst.get("cost_usd")
        if value is not None and inst.get("payer_cost_verified") is True:
            number = Decimal(str(value))
            require(number.is_finite() and number >= 0, "invalid_instrumentation")
            costs.append(number)
        value = inst.get("latency_seconds")
        if value is not None and inst.get("elapsed_measured") is True:
            require(finite(value) and value >= 0, "invalid_instrumentation")
            latencies.append(value)
    return {"planned_operations": len(plans), "captured_records": len(observed),
            "missing_records": len(plans)-len(observed), "cost_observed_count": len(costs),
            "cost_unknown_count": len(plans)-len(costs), "observed_cost_subtotal_usd": str(sum(costs, Decimal(0))),
            "total_cost_usd": str(sum(costs, Decimal(0))) if len(costs) == len(plans) else None,
            "latency_observed_count": len(latencies), "latency_missing_count": len(plans)-len(latencies),
            "observed_mean_latency_seconds": statistics.mean(latencies) if latencies else None}


def tradeoff(planned, records, left, right, quality, evaluator, protocol, *, measurement_kinds=("new_real_export_inference", "composed_real_export_outcome"), arm_protocols=None):
    """Matched deltas, never a composite or automatic winning preset.

    A tradeoff is complete only when all three dimensions cover every planned
    matched slot. This conservative rule avoids comparing quality on survivors
    against cost/time from a different hidden cohort.
    """
    dimensions = {"quality": paired_comparison(planned, records, left, right, quality, evaluator, protocol, measurement_kinds=measurement_kinds, arm_protocols=arm_protocols),
                  "cost": paired_comparison(planned, records, left, right, "cost_usd", "mechanical", protocol, measurement_kinds=measurement_kinds, arm_protocols=arm_protocols),
                  "latency": paired_comparison(planned, records, left, right, "latency_seconds", "mechanical", protocol, measurement_kinds=measurement_kinds, arm_protocols=arm_protocols)}
    complete = all(x["observed_pair_slots"] == x["planned_pair_slots"] for x in dimensions.values())
    return {"schema": "loom.research_tradeoff/1", "status": "complete_descriptive" if complete else "partial_missing_dimensions",
            "dimensions": dimensions, "composite_score": None, "winner": None, "automatic_adoption": False,
            "limitations": ["evaluator_classes_not_pooled", "family_dependence_retained", "no_universal_winner"]}


def build_handoff_artifact(presentation, protocol, projection, *, projected_at):
    """Existing canonical method exporter; no native persistence/execution claim.

    This captures exactly what is supplied. Callers must pass only the separately
    reviewed public projection; this routine is intentionally not a privacy scrubber.
    """
    validate_protocol(protocol)
    with tempfile.TemporaryDirectory(prefix="thread7-analysis-") as folder:
        base = Path(folder)
        sources = {"policy_frozen.json": graph.canonical(protocol), "prototype_frozen.py": Path(__file__).read_bytes(),
                   "protocol_frozen.md": b"Source-bound offline analysis. Captured record provenance is not content truth.\n"}
        for name, raw in sources.items():
            (base/name).write_bytes(raw)
        manifest = {"predicted_at": projected_at, "execution_status": "offline_public_projection_no_model_execution",
                    "hashes": {role: hashlib.sha256(sources[name]).hexdigest() for role, name in
                               (("policy", "policy_frozen.json"), ("code", "prototype_frozen.py"), ("protocol", "protocol_frozen.md"))}}
        (base/"predictions.json").write_bytes(graph.canonical({"manifest": manifest, "records": presentation["evidence"]}))
        (base/"results.json").write_bytes(graph.canonical({"manifest": manifest, "records": [presentation]}))
        artifact = graph.build_artifact(graph.capture_run(base, projection), projection, projected_at=projected_at)
    from jsonschema import Draft202012Validator
    validator = Draft202012Validator(graph.strict_json((ROOT/"loom/src/packet/method-graph.schema.json").read_bytes()))
    validator.validate(artifact["contract"])
    validator.validate(artifact["trace"])
    graph.codec.validate_packet(artifact["packet"])
    require(bool(graph.recover_files(artifact)) and bool(graph.recover_results(artifact)), "contract_recovery")
    return artifact


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--planned", type=Path, required=True)
    parser.add_argument("--records", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    # Private rows are never printed. Exclusive creation preserves old outputs.
    planned = graph.strict_json(args.planned.read_bytes())
    records = graph.strict_json(args.records.read_bytes())
    with args.output.open("xb") as handle:
        handle.write(graph.canonical(measurement_summary(planned, records))+b"\n")


if __name__ == "__main__":
    main()
