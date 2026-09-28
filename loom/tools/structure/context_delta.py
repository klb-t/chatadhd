#!/usr/bin/env python3
"""Reversible, source-linked candidate overlays over one explicit JSON snapshot.

No extraction, identity merge, Claim promotion, source edit or database write.
Temporal checks use explicit observation/availability times, never created dates.
"""
from __future__ import annotations

import base64
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json

VERSION = "context-delta-experiment/1"
PACKET_SCHEMA = "loom.source_packet/1"
LINK_TYPES = {"thread_membership", "continuation", "return", "correction", "contradiction", "analogy"}


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def _hash(value):
    return hashlib.sha256(canonical(value).encode("utf-8")).hexdigest()


def _text(value):
    return isinstance(value, str) and bool(value)


def _error(errors, path, reason):
    errors.append({"path": path, "reason": reason})


def _snapshot(value):
    raw = value if isinstance(value, bytes) else value.encode("utf-8") if isinstance(value, str) else canonical(value).encode("utf-8")
    parsed = json.loads(raw)
    if not isinstance(parsed, dict):
        raise ValueError("snapshot must be a JSON object")
    encoded = canonical(parsed)
    return parsed, {"snapshot_bytes_b64": base64.b64encode(raw).decode("ascii"),
                    "canonical_json": encoded, "base_hash": _hash(parsed),
                    "raw_hash": hashlib.sha256(raw).hexdigest()}


def _instant(value):
    if not _text(value):
        return None
    try:
        result = datetime.fromisoformat(value.replace("Z", "+00:00"))
        return result.astimezone(timezone.utc) if result.tzinfo else None
    except ValueError:
        return None


def _time(value, cut):
    stamp = _instant(value)
    return {"observed_at": value, "status": "unknown" if stamp is None else "future" if stamp > cut else "at_or_before_cut"}


def _records(snapshot, collection, errors):
    result = {}
    rows = snapshot.get(collection, [])
    if not isinstance(rows, list):
        _error(errors, collection, "requires_array")
        return result
    for i, row in enumerate(rows):
        if not isinstance(row, dict) or not _text(row.get("id")) or row["id"] in result:
            _error(errors, f"{collection}[{i}]", "requires_unique_nonempty_id_and_object")
        else:
            result[row["id"]] = row
    return result


def _timestamp(snapshot, collection, record):
    # Explicit metadata only. Native created/valid_from are not availability.
    times = snapshot.get("context_metadata", {}).get(collection + "_times", {})
    return times.get(record["id"], record.get("observed_at"))


def _source_group(snapshot, observation):
    return snapshot.get("context_metadata", {}).get("source_groups", {}).get(observation["id"], observation.get("source_group"))


def _span(raw, observations, snapshot, cut, errors, path):
    if not isinstance(raw, dict):
        _error(errors, path, "requires_span_object")
        return None
    allowed = {"id", "observation", "byte_start", "byte_len", "quote"}
    if set(raw) - allowed:
        _error(errors, path, "unrepresented_span_fields")
    if not _text(raw.get("id")) or not _text(raw.get("observation")) or raw["observation"] not in observations:
        _error(errors, path, "unknown_observation_or_invalid_span_id")
        return None
    observation = observations[raw["observation"]]
    start, length, quote = raw.get("byte_start"), raw.get("byte_len"), raw.get("quote")
    text = observation.get("text")
    if not isinstance(text, str) or type(start) is not int or start < 0 or type(length) is not int or length < 1 or not _text(quote):
        _error(errors, path, "invalid_utf8_byte_span")
        return None
    encoded = text.encode("utf-8")
    if start + length > len(encoded) or encoded[start:start + length] != quote.encode("utf-8"):
        _error(errors, path, "quote_does_not_match_exact_observation_bytes")
        return None
    temporal = _time(_timestamp(snapshot, "observation", observation), cut)
    if temporal["status"] == "future":
        _error(errors, path, "future_observation_refused")
    return {**deepcopy(raw), "source_group": deepcopy(_source_group(snapshot, observation)),
            "locator": deepcopy(observation.get("locator", {})), "temporal": temporal,
            "observation_text_hash": hashlib.sha256(encoded).hexdigest()}


def prepare_context(base_snapshot, current_spans, claim_refs, *, time_cut):
    """Build a common source packet plus explicit source/context separation.

    ``claim_refs`` are {claim_id, selection_reason}; complete old Claim bodies,
    Assessment, status and source groups are retained. ``current_spans`` use the
    common UTF-8 span tuple plus a local id. Unknown time is not treated as past.
    Failed preparation retains the submitted snapshot and references with errors.
    """
    errors, warnings = [], []
    retained = {"current_spans": deepcopy(current_spans), "claim_refs": deepcopy(claim_refs), "time_cut": time_cut}
    try:
        snapshot, origin = _snapshot(base_snapshot)
        retained.update(origin)
    except (ValueError, TypeError, UnicodeError) as failure:
        raw = base_snapshot if isinstance(base_snapshot, bytes) else str(base_snapshot).encode("utf-8")
        retained["snapshot_bytes_b64"] = base64.b64encode(raw).decode("ascii")
        return {"status": "invalid", "packet": None, "retained_input": retained,
                "errors": [{"path": "snapshot", "reason": str(failure)}], "warnings": []}
    cut = _instant(time_cut)
    if cut is None:
        _error(errors, "time_cut", "requires_explicit_timezone_timestamp")
    if not isinstance(snapshot.get("context_metadata", {}), dict):
        _error(errors, "context_metadata", "requires_object")
    else:
        for field in ("observation_times", "claim_times", "thread_times", "entity_times", "source_groups"):
            if not isinstance(snapshot.get("context_metadata", {}).get(field, {}), dict):
                _error(errors, "context_metadata." + field, "requires_object")
    observations = _records(snapshot, "observations", errors)
    claims = _records(snapshot, "claims", errors)
    entities = _records(snapshot, "entities", errors)
    threads = _records(snapshot, "threads", errors)
    if not isinstance(snapshot.get("assignments", []), list):
        _error(errors, "assignments", "requires_array")
    if not isinstance(current_spans, list) or not isinstance(claim_refs, list):
        _error(errors, "references", "current_spans_and_claim_refs_require_arrays")
    if errors:
        return {"status": "invalid", "packet": None, "retained_input": retained, "errors": errors, "warnings": warnings}
    spans, contexts, seen = [], [], set()
    selected_observations, selected_entities = set(), set()
    for i, raw in enumerate(current_spans):
        span = _span(raw, observations, snapshot, cut, errors, f"current_spans[{i}]")
        if span:
            if span["id"] in seen:
                _error(errors, f"current_spans[{i}]", "duplicate_span_id")
            seen.add(span["id"])
            spans.append(span)
            selected_observations.add(span["observation"])
    seen = set()
    for i, ref in enumerate(claim_refs):
        path = f"claim_refs[{i}]"
        if not isinstance(ref, dict) or set(ref) != {"claim_id", "selection_reason"} or not _text(ref.get("claim_id")) or ref["claim_id"] not in claims or not _text(ref.get("selection_reason")):
            _error(errors, path, "requires_existing_claim_id_and_selection_reason")
            continue
        ident = ref["claim_id"]
        if ident in seen:
            _error(errors, path, "duplicate_claim_ref")
            continue
        seen.add(ident)
        claim = claims[ident]
        assessment = claim.get("assessment")
        if not isinstance(assessment, dict) or not _text(assessment.get("status")):
            _error(errors, path, "claim_assessment_and_status_required")
            continue
        temporal = _time(_timestamp(snapshot, "claim", claim), cut)
        dependencies, groups = [], []
        basis = assessment.get("basis", {})
        support = basis.get("support", []) if isinstance(basis, dict) else None
        if not isinstance(support, list):
            _error(errors, path, "claim_support_requires_array")
            continue
        for support_index, item in enumerate(support):
            obs = observations.get(item["observation"]) if isinstance(item, dict) and _text(item.get("observation")) else None
            if obs is None:
                _error(errors, path + f".support[{support_index}]", "missing_support_observation")
                continue
            selected_observations.add(obs["id"])
            stamp = _time(_timestamp(snapshot, "observation", obs), cut)
            group = _source_group(snapshot, obs)
            dependencies.append({"observation": obs["id"], "temporal": stamp, "source_group": deepcopy(group), "support": deepcopy(item)})
            if group is not None and group not in groups:
                groups.append(deepcopy(group))
            if stamp["status"] == "future":
                _error(errors, path, "future_claim_support_refused")
        if temporal["status"] == "future":
            _error(errors, path, "future_claim_availability_refused")
        contexts.append({"claim_id": ident, "claim": deepcopy(claim), "assessment": deepcopy(assessment),
                         "status": assessment["status"], "source_groups": groups,
                         "selection_reason": ref["selection_reason"], "temporal": temporal,
                         "observation_dependencies": dependencies})
        for endpoint in (claim.get("subject"), claim.get("object")):
            if isinstance(endpoint, str) and endpoint in entities:
                selected_entities.add(endpoint)
    thread_context = []
    excluded_threads = []
    for ident, record in threads.items():
        temporal = _time(_timestamp(snapshot, "thread", record), cut)
        if temporal["status"] == "future":
            excluded_threads.append({"id": ident, "reason": "future_thread_refused"})
        else:
            thread_context.append({"id": ident, "record": deepcopy(record), "temporal": temporal})
    for item in spans + contexts + thread_context:
        if item["temporal"]["status"] == "unknown":
            warnings.append({"id": item.get("id", item.get("claim_id")), "reason": "timestamp_unknown_not_established_as_past"})
    entity_context = []
    for ident in sorted(selected_entities):
        temporal = _time(_timestamp(snapshot, "entity", entities[ident]), cut)
        entity_context.append({"id": ident, "temporal": temporal})
        if temporal["status"] == "future":
            _error(errors, "entities." + ident, "future_entity_body_refused")
        elif temporal["status"] == "unknown":
            warnings.append({"id": ident, "reason": "entity_body_timestamp_unknown"})
    packet = {"schema": PACKET_SCHEMA, "snapshot_id": snapshot.get("snapshot_id", snapshot.get("run_id", origin["base_hash"])),
              "observations": [deepcopy(observations[i]) for i in sorted(selected_observations)],
              "entities": [deepcopy(entities[i]) for i in sorted(selected_entities)],
              "claims": [deepcopy(c["claim"]) for c in contexts],
              "context_delta_version": VERSION, "base_hash": origin["base_hash"], "time_cut": time_cut,
              "current_spans": spans, "context_claims": contexts, "context_threads": thread_context,
              "excluded_context_counts": {"future_threads": len(excluded_threads)},
              "entity_times": entity_context,
              "immutable_span_ids": sorted({item["span_id"] for item in snapshot.get("assignments", [])
                                            if isinstance(item, dict) and isinstance(item.get("span_id"), str)
                                            and item["span_id"] in {span["id"] for span in spans}}),
              "temporal_policy": "future_refused_unknown_retained_without_past_assertion"}
    packet["packet_hash"] = _hash(packet)
    return {"status": "invalid" if errors else "ready", "packet": None if errors else packet,
            "audit": origin, "retained_input": retained, "errors": errors, "warnings": warnings}


def _packet_errors(packet):
    if not isinstance(packet, dict) or packet.get("schema") != PACKET_SCHEMA or packet.get("context_delta_version") != VERSION:
        return [{"path": "packet", "reason": "unsupported_packet"}]
    value = {k: v for k, v in packet.items() if k != "packet_hash"}
    try:
        digest = _hash(value)
    except (TypeError, ValueError):
        return [{"path": "packet", "reason": "non_json_packet"}]
    if packet.get("packet_hash") != digest:
        return [{"path": "packet", "reason": "packet_hash_mismatch"}]
    arrays = ("observations", "entities", "claims", "current_spans", "context_claims", "context_threads", "entity_times", "immutable_span_ids")
    if any(not isinstance(packet.get(key), list) for key in arrays) or any(not isinstance(value, str) for value in packet["immutable_span_ids"]):
        return [{"path": "packet", "reason": "invalid_packet_collections"}]
    # Packet-relative consistency only. The full source snapshot intentionally
    # lives outside this model-facing packet. apply_view verifies it separately.
    try:
        observations = {o["id"]: o for o in packet["observations"]}
        claims = {c["id"]: c for c in packet["claims"]}
        for span in packet["current_spans"]:
            raw = observations[span["observation"]]["text"].encode("utf-8")
            start, length = span["byte_start"], span["byte_len"]
            if type(start) is not int or type(length) is not int or start < 0 or length < 1 or raw[start:start + length] != span["quote"].encode("utf-8"):
                return [{"path": "packet", "reason": "selected_span_bytes_mismatch"}]
        for item in packet["context_claims"]:
            if claims[item["claim_id"]] != item["claim"] or item["assessment"] != item["claim"]["assessment"] or item["status"] != item["assessment"]["status"]:
                return [{"path": "packet", "reason": "context_claim_body_mismatch"}]
    except (KeyError, TypeError, ValueError, UnicodeError, AttributeError):
        return [{"path": "packet", "reason": "invalid_packet_references"}]
    return []


def validate_delta(packet, delta):
    """Validate a proposed overlay; unknown references need no selected target.

    This checks representation, source bytes, referential integrity and chronology,
    not semantic entailment or whether the caller chose the right thread.
    """
    errors = _packet_errors(packet)
    retained = deepcopy(delta)
    if errors:
        return {"status": "invalid", "errors": errors, "retained_input": retained}
    try:
        canonical(delta)
    except (TypeError, ValueError):
        return {"status": "invalid", "errors": [{"path": "delta", "reason": "requires_finite_json_data"}], "retained_input": retained}
    allowed = {"id", "base_hash", "packet_hash", "additions", "threads", "links", "references"}
    if not isinstance(delta, dict) or set(delta) - allowed or not _text(delta.get("id")):
        return {"status": "invalid", "errors": [{"path": "delta", "reason": "requires_known_fields_and_nonempty_id"}], "retained_input": retained}
    if delta.get("base_hash") != packet["base_hash"]:
        _error(errors, "base_hash", "base_hash_mismatch")
    if delta.get("packet_hash") != packet["packet_hash"]:
        _error(errors, "packet_hash", "packet_hash_mismatch")
    for field in ("additions", "threads", "links", "references"):
        if not isinstance(delta.get(field, []), list):
            _error(errors, field, "requires_array")
    if errors:
        return {"status": "invalid", "errors": errors, "retained_input": retained}
    spans = {item["id"]: item for item in packet["current_spans"]}
    claims = {item["claim_id"]: item for item in packet["context_claims"]}
    threads = {item["id"]: item for item in packet["context_threads"]}
    additions, new_threads, all_ids = {}, {}, set()
    reserved = set(spans) | set(claims) | set(threads)
    for field in ("additions", "threads", "links", "references"):
        for i, item in enumerate(delta.get(field, [])):
            if not isinstance(item, dict) or not _text(item.get("id")) or item["id"] in all_ids | reserved:
                _error(errors, f"{field}[{i}]", "duplicate_reserved_or_invalid_id")
            else:
                all_ids.add(item["id"])
                if field == "additions": additions[item["id"]] = item
                elif field == "threads": new_threads[item["id"]] = item
    if errors:
        return {"status": "invalid", "errors": errors, "retained_input": retained}
    immutable_span_ids = set(packet["immutable_span_ids"])

    def span_ids(values, path, required=False):
        if not isinstance(values, list) or any(not isinstance(v, str) or v not in spans for v in values) or len(set(values)) != len(values) or (required and not values):
            _error(errors, path, "requires_unique_current_span_ids" + ("_nonempty" if required else ""))
            return []
        return values

    def old_ids(values, path):
        if not isinstance(values, list) or any(not isinstance(v, str) or v not in claims for v in values) or len(set(values)) != len(values):
            _error(errors, path, "requires_allowlisted_existing_claim_ids")
            return []
        return values

    def reference(ref, path):
        if not isinstance(ref, dict) or set(ref) != {"kind", "id"} or ref.get("kind") not in {"span", "claim", "thread", "addition"} or not _text(ref.get("id")):
            _error(errors, path, "invalid_typed_reference")
            return False
        index = {"span": spans, "claim": claims, "thread": {**threads, **new_threads}, "addition": additions}[ref["kind"]]
        if ref["id"] not in index:
            _error(errors, path, "reference_not_in_context_or_delta")
            return False
        if ref["kind"] == "thread" and ref["id"] in threads and threads[ref["id"]]["temporal"]["status"] == "future":
            _error(errors, path, "future_thread_refused")
            return False
        return True

    def not_later(anchor_id, span_id, path):
        anchor, current = spans[anchor_id], spans[span_id]
        if anchor["observation"] == current["observation"]:
            okay = anchor["byte_start"] + anchor["byte_len"] <= current["byte_start"] + current["byte_len"]
        else:
            left, right = _instant(anchor["temporal"]["observed_at"]), _instant(current["temporal"]["observed_at"])
            okay = left is not None and right is not None and left < right
        if not okay:
            _error(errors, path, "later_or_temporally_unknown_anchor_cannot_rewrite_earlier_span")

    def target_chronology(target, current_id, path):
        if target["kind"] == "span":
            not_later(target["id"], current_id, path)
        elif target["kind"] in {"claim", "thread"}:
            index = claims if target["kind"] == "claim" else threads
            if target["id"] in index:
                record = index[target["id"]]
                times = [record["temporal"]]
                if target["kind"] == "claim":
                    times += [item["temporal"] for item in record["observation_dependencies"]]
                right = _instant(spans[current_id]["temporal"]["observed_at"])
                if right is None or any(_instant(item["observed_at"]) is None or _instant(item["observed_at"]) > right for item in times):
                    _error(errors, path, "context_availability_not_established_before_reference")
            else:
                for ident in new_threads[target["id"]].get("current_span_ids", []):
                    if ident in spans: not_later(ident, current_id, path)
        elif target["kind"] == "addition":
            for ident in additions[target["id"]].get("current_span_ids", []):
                if ident in spans: not_later(ident, current_id, path)

    for ident, item in additions.items():
        if set(item) - {"id", "draft", "current_span_ids", "existing_claim_ids"} or not isinstance(item.get("draft"), dict):
            _error(errors, "additions." + ident, "requires_unpromoted_draft_and_known_fields")
        span_ids(item.get("current_span_ids"), "additions." + ident, True)
        old_ids(item.get("existing_claim_ids", []), "additions." + ident)
    for ident, item in new_threads.items():
        if set(item) - {"id", "label", "current_span_ids"} or not _text(item.get("label")):
            _error(errors, "threads." + ident, "requires_label_and_known_fields")
        span_ids(item.get("current_span_ids"), "threads." + ident, True)
    for i, item in enumerate(delta.get("links", [])):
        path = f"links[{i}]"
        if set(item) - {"id", "type", "source", "target", "current_span_ids", "existing_claim_ids", "reason"} or item.get("type") not in LINK_TYPES or not _text(item.get("reason")):
            _error(errors, path, "requires_distinct_supported_link_type_reason_and_known_fields")
        evidence = span_ids(item.get("current_span_ids"), path, True)
        old_ids(item.get("existing_claim_ids", []), path)
        source_ok, target_ok = reference(item.get("source"), path + ".source"), reference(item.get("target"), path + ".target")
        if source_ok and target_ok:
            source, target = item["source"], item["target"]
            relation = item.get("type")
            if relation in {"thread_membership", "continuation", "return"}:
                if source["kind"] != "span" or target["kind"] != "thread":
                    _error(errors, path, "thread_links_require_span_to_thread")
                else:
                    if source["id"] not in evidence: _error(errors, path, "source_span_must_be_current_evidence")
                    for anchor in evidence: not_later(anchor, source["id"], path)
                    target_chronology(target, source["id"], path)
            elif relation in {"correction", "contradiction", "analogy"}:
                if source["kind"] not in {"span", "addition"} or target["kind"] not in {"claim", "addition", "thread"}:
                    _error(errors, path, "comparison_links_require_current_source_and_context_target")
                if target["kind"] == "claim" and target["id"] not in item.get("existing_claim_ids", []):
                    _error(errors, path, "old_claim_target_must_be_explicit_dependency")
                if source["kind"] == "span":
                    if source["id"] not in evidence:
                        _error(errors, path, "source_span_must_be_current_evidence")
                    for anchor in evidence:
                        not_later(anchor, source["id"], path)
                elif source["kind"] == "addition" and not set(additions[source["id"]].get("current_span_ids", [])) <= set(evidence):
                    _error(errors, path, "addition_source_spans_must_be_current_evidence")
                current_ids = [source["id"]] if source["kind"] == "span" else additions.get(source["id"], {}).get("current_span_ids", [])
                for current_id in current_ids:
                    if current_id in spans:
                        target_chronology(target, current_id, path)
    for i, item in enumerate(delta.get("references", [])):
        path = f"references[{i}]"
        if set(item) - {"id", "span_id", "status", "alternatives", "selected", "evidence_span_ids"} or not _text(item.get("span_id")) or item["span_id"] not in spans:
            _error(errors, path, "invalid_reference_fields_or_source_span")
            continue
        if item["span_id"] in immutable_span_ids:
            _error(errors, path, "base_reference_assignment_is_immutable")
        alternatives = item.get("alternatives")
        if not isinstance(alternatives, list):
            _error(errors, path, "alternatives_require_array")
            continue
        valid = [reference(ref, path + ".alternatives") for ref in alternatives]
        if len({_hash(ref) for ref in alternatives}) != len(alternatives):
            _error(errors, path, "duplicate_alternatives")
        evidence = span_ids(item.get("evidence_span_ids", []), path)
        status, selected = item.get("status"), item.get("selected")
        if status == "anchored":
            if not evidence or selected not in alternatives or len(alternatives) != 1 or not all(valid):
                _error(errors, path, "anchored_reference_requires_one_selected_supported_alternative")
            else:
                for anchor in evidence: not_later(anchor, item["span_id"], path)
                target_chronology(selected, item["span_id"], path)
        elif status in {"ambiguous", "unresolved"}:
            if selected is not None or (status == "ambiguous" and len(alternatives) < 2):
                _error(errors, path, "uncertain_reference_cannot_select_identity")
        else:
            _error(errors, path, "unknown_reference_status")
    return {"status": "invalid" if errors else "valid", "errors": errors, "retained_input": retained,
            "delta_hash": _hash(delta), "semantic_validity": "not_assessed", "persistable_claim": False}


def apply_view(base_snapshot, packet, delta, *, previous_view=None):
    """Apply a validated delta only to an ephemeral, immutable-base overlay.

    Same id+content is idempotent. Conflicting IDs reject the whole application;
    no last-writer-wins overwrite. Every applied delta retains its full packet.
    """
    checked = validate_delta(packet, delta)
    failure = {"status": "invalid", "view": deepcopy(previous_view), "retained_input": deepcopy(delta), "errors": list(checked["errors"])}
    try:
        snapshot, origin = _snapshot(base_snapshot)
    except (TypeError, ValueError, UnicodeError) as error:
        _error(failure["errors"], "snapshot", str(error))
        return failure
    failure["retained_base"] = origin
    failure["retained_packet"] = deepcopy(packet)
    if failure["errors"]:
        return failure
    if origin["base_hash"] != packet.get("base_hash"):
        _error(failure["errors"], "snapshot", "base_snapshot_hash_mismatch")
    base_ids = {item["id"] for rows in snapshot.values() if isinstance(rows, list)
                for item in rows if isinstance(item, dict) and _text(item.get("id"))}
    requested_ids = {item["id"] for field in ("additions", "threads", "links", "references") for item in delta.get(field, [])}
    if base_ids & requested_ids:
        _error(failure["errors"], "delta", "base_record_ids_cannot_be_reused_for_additions")
    if not failure["errors"]:
        selected = [{key: item[key] for key in ("id", "observation", "byte_start", "byte_len", "quote")} for item in packet["current_spans"]]
        refs = [{"claim_id": item["claim_id"], "selection_reason": item["selection_reason"]} for item in packet["context_claims"]]
        rebuilt = prepare_context(base_snapshot, selected, refs, time_cut=packet["time_cut"])
        if rebuilt["status"] != "ready" or rebuilt["packet"]["packet_hash"] != packet["packet_hash"]:
            _error(failure["errors"], "packet", "packet_does_not_replay_against_original_snapshot")
    if previous_view is not None:
        if not isinstance(previous_view, dict) or previous_view.get("base_hash") != origin["base_hash"] or previous_view.get("view_hash") != _hash({k: v for k, v in previous_view.items() if k != "view_hash"}):
            _error(failure["errors"], "previous_view", "invalid_or_changed_base_view")
        elif previous_view.get("origin") != origin or previous_view.get("snapshot") != snapshot:
            _error(failure["errors"], "previous_view", "previous_view_base_was_rewritten")
    if failure["errors"]:
        return failure
    view = deepcopy(previous_view) if previous_view is not None else {
        "version": VERSION, "base_hash": origin["base_hash"], "origin": origin,
        "snapshot": snapshot, "deltas": [], "overlay": {"additions": [], "threads": [], "links": [], "references": []},
        "persistable_claim": False, "automatic_mutation": False}
    for prior in view["deltas"]:
        if prior["delta"]["id"] == delta["id"]:
            if prior["delta_hash"] == checked["delta_hash"] and prior["packet"]["packet_hash"] == packet["packet_hash"]:
                return {"status": "unchanged", "view": view, "errors": []}
            _error(failure["errors"], "delta.id", "conflicting_delta_id_no_overwrite")
    prior_ids = {item["id"] for values in view["overlay"].values() for item in values}
    new_ids = {item["id"] for field in view["overlay"] for item in delta.get(field, [])}
    if prior_ids & new_ids:
        _error(failure["errors"], "delta", "conflicting_overlay_ids_no_overwrite")
    # A later packet cannot append a replacement resolution to an already
    # recorded span. Multiple simultaneous alternatives stay in its first delta.
    def span_key(value):
        return canonical({k: value[k] for k in ("observation", "byte_start", "byte_len", "quote")})
    prior_subjects = set()
    for prior in view["deltas"]:
        prior_spans = {s["id"]: s for s in prior["packet"]["current_spans"]}
        for item in prior["delta"].get("references", []):
            prior_subjects.add(span_key(prior_spans[item["span_id"]]))
    current_spans = {s["id"]: s for s in packet["current_spans"]}
    if any(span_key(current_spans[item["span_id"]]) in prior_subjects for item in delta.get("references", [])):
        _error(failure["errors"], "references", "existing_reference_assignment_is_immutable")
    if failure["errors"]:
        return failure
    for field in view["overlay"]:
        view["overlay"][field].extend({**deepcopy(item), "context_packet_hash": packet["packet_hash"], "delta_id": delta["id"]}
                                     for item in delta.get(field, []))
    view["deltas"].append({"delta": deepcopy(delta), "delta_hash": checked["delta_hash"], "packet": deepcopy(packet)})
    view.pop("view_hash", None)
    view["view_hash"] = _hash(view)
    return {"status": "applied", "view": view, "errors": []}


def undo_view(view):
    """Remove the complete overlay and return exact original bytes as base64.

    ``base64.b64decode(result['snapshot_bytes_b64'])`` yields the input bytes,
    including formatting. ``canonical_json`` serves object-input callers.
    """
    if not isinstance(view, dict) or view.get("view_hash") != _hash({k: v for k, v in view.items() if k != "view_hash"}):
        return {"status": "invalid", "retained_input": deepcopy(view), "errors": [{"path": "view", "reason": "view_hash_mismatch"}]}
    return {"status": "restored", **deepcopy(view["origin"]), "snapshot": deepcopy(view["snapshot"]),
            "source_refs": [{"packet_hash": item["packet"]["packet_hash"], "current_spans": deepcopy(item["packet"]["current_spans"]),
                             "context_claims": deepcopy(item["packet"]["context_claims"])} for item in view["deltas"]],
            "removed_delta_ids": [item["delta"]["id"] for item in view["deltas"]], "errors": []}
