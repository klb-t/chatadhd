#!/usr/bin/env python3
"""Ephemeral incidence views of canonical Loom Claim/Assessment export objects.

The exported core bodies remain authoritative. This module never parses text,
creates Claims, guesses missing semantics, proves conclusions, or writes a store.
"""
from __future__ import annotations

from collections import defaultdict, deque
from copy import deepcopy
import hashlib
import json
import math

try:
    from .structure_methods import ROLES, validate_graph
except ImportError:
    from structure_methods import ROLES, validate_graph

VERSION = "core-incidence-projection/1"
MODES = {"semantic", "structural", "topology_control"}
COLLECTIONS = {"claim": "claims", "entity": "entities", "observation": "observations",
               "principle": "principles", "operator": "operators", "morphism": "morphisms",
               "instance": "instances", "area": "areas", "model": "models",
               "prediction": "predictions", "check": "checks", "source": "sources", "unit": "units"}
EVIDENCE = {"observed", "derived", "inferred", "extrapolated", "absent", "user"}
ORIGINS = {"archive", "repo", "user", "external_authority", "model_knowledge", "system"}
STATUSES = {"active", "contested", "superseded", "rejected"}
CHECK_STATES = {"pending", "holds", "violated", "n/a"}


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def _id(kind, value):
    return "view_" + kind + "_" + hashlib.sha256(canonical(value).encode("utf-8")).hexdigest()[:24]


def _list(value, path):
    if not isinstance(value, list):
        raise ValueError(path + " must be an array")
    return value


def _text(value):
    return isinstance(value, str) and bool(value)


def _contract(claim):
    """Conservative report of visible source-contract violations, not C++ approval."""
    errors, unknown, restrictions = [], [], []
    for key in ("id", "subject", "predicate"):
        if not _text(claim.get(key)):
            errors.append(key + "_missing_or_invalid")
    if not isinstance(claim.get("object"), str):
        errors.append("object_must_be_string")
    if "value" not in claim:
        unknown.append("value_field_missing")
    qualifiers, assessment = claim.get("qualifiers"), claim.get("assessment")
    if not isinstance(qualifiers, dict):
        errors.append("qualifiers_missing_or_invalid")
        qualifiers = {}
    for key in ("valid_from", "valid_to", "version", "branch", "scope", "lang"):
        if key not in qualifiers:
            unknown.append("qualifiers." + key + "_missing")
        elif not isinstance(qualifiers[key], str):
            errors.append("qualifiers." + key + "_must_be_string")
    if not isinstance(qualifiers.get("extra", {}), dict):
        errors.append("qualifiers.extra_must_be_object")
    if not isinstance(assessment, dict):
        errors.append("assessment_missing_or_invalid")
        assessment = {}
    for key, permitted in (("evidence_class", EVIDENCE), ("origin", ORIGINS), ("status", STATUSES), ("check_state", CHECK_STATES)):
        if key not in assessment:
            unknown.append("assessment." + key + "_missing")
        elif not isinstance(assessment[key], str) or assessment[key] not in permitted:
            errors.append("assessment." + key + "_invalid")
    confidence = assessment.get("confidence")
    if not isinstance(confidence, (int, float)) or isinstance(confidence, bool) or not math.isfinite(confidence) or not 0 <= confidence <= 1:
        errors.append("assessment.confidence_must_be_finite_unit_interval")
    basis = assessment.get("basis", {})
    if not isinstance(basis, dict):
        errors.append("assessment.basis_must_be_object")
        basis = {}
    support, derivation = basis.get("support", []), basis.get("derivation")
    if not isinstance(support, list):
        errors.append("assessment.basis.support_must_be_array")
        support = []
    evidence = assessment.get("evidence_class")
    if evidence == "observed" and not support:
        errors.append("observed_requires_observation_support")
    if evidence == "absent" and support:
        errors.append("absent_must_not_have_support")
    has_object, has_value = bool(claim.get("object")), claim.get("value") is not None
    if evidence == "absent":
        if has_object or has_value:
            errors.append("absent_must_not_have_object_or_value")
    elif has_object == has_value:
        errors.append("nonabsent_requires_exactly_one_object_or_value")
    if evidence in {"derived", "inferred", "extrapolated"} and (not isinstance(derivation, dict) or not _text(derivation.get("operator"))):
        errors.append("derived_inferred_extrapolated_requires_operator")
    expected = assessment.get("expected_property")
    if evidence == "inferred" and (expected is None or assessment.get("check_state") == "n/a"):
        errors.append("inferred_requires_expected_property_and_check_state")
    if assessment.get("check_state", "n/a") != "n/a" and expected is None:
        errors.append("check_state_requires_expected_property")
    if evidence in {"absent", "extrapolated"}:
        restrictions.append("not_a_core_inference_premise")
    if assessment.get("status") != "active":
        restrictions.append("source_status_not_active")
    if isinstance(derivation, dict) and derivation.get("morphism"):
        restrictions.append("transfer_depth_restriction")
    if qualifiers.get("extra"):
        restrictions.append("extra_qualifiers_retained_without_interpreting_them")
    return {"source_contract": "invalid" if errors else "unknown" if unknown else "locally_consistent",
            "errors": errors, "unknowns": unknown, "restrictions": restrictions,
            "core_validation_executed": False, "inference_eligible": False}


def project_core(export, mode="semantic", claim_ids=None):
    """Project actual core JSON bodies from one explicit knowledge run.

    Required wrapper: ``{run_id, claims:[Claim::to_json()]}``. Optional record
    arrays use COLLECTIONS; slot_values uses actual SlotRow JSON. Product slots
    containing only Claim IDs cannot substitute for Claim bodies.
    """
    if mode not in MODES:
        raise ValueError("mode must be semantic, structural, or topology_control")
    if not isinstance(export, dict) or not _text(export.get("run_id")):
        raise ValueError("export requires an explicit nonempty run_id")
    canonical(export)  # Reject NaN/non-JSON data before assigning deterministic IDs.
    run = export["run_id"]
    indexes = {}
    for namespace, collection in COLLECTIONS.items():
        records = _list(export.get(collection, []), collection)
        index = {}
        for record in records:
            if not isinstance(record, dict) or not _text(record.get("id")) or record["id"] in index:
                raise ValueError(collection + " requires unique nonempty record IDs and JSON bodies")
            index[record["id"]] = record
        indexes[namespace] = index
    if "claims" not in export:
        raise ValueError("export requires claims containing core Claim bodies")
    requested = list(indexes["claim"]) if claim_ids is None else _list(claim_ids, "claim_ids")
    if any(not _text(ident) for ident in requested):
        raise ValueError("claim_ids must contain nonempty strings")
    selected = sorted(set(requested))
    if any(not _text(ident) or ident not in indexes["claim"] for ident in selected):
        raise ValueError("selected claim IDs must exist in this exported run")
    selected_set = set(selected)
    nodes, edges, refs, unknowns, losses = {}, {}, {}, [], []
    gates = {ident: _contract(indexes["claim"][ident]) for ident in selected}
    built, building = set(), set()

    def omit(path, attribute, value, reason):
        losses.append({"path": path, "attribute": attribute, "value": deepcopy(value),
                       "scope": "comparison_labels_only", "retained_in_source": True, "reason": reason})

    def node(key, kind, qualifiers=None, role="", lexical=None, source_ref=None):
        ident = _id("node", [run, key])
        if ident not in nodes:
            nodes[ident] = {"id": ident, "kind": kind, "role": role, "qualifiers": deepcopy(qualifiers or {}),
                            "claim_ids": []}
            if lexical is not None:
                nodes[ident]["lexical_identity"] = deepcopy(lexical)
            refs[ident] = {"run_id": run, "reference": deepcopy(source_ref), "source_claim_ids": []}
        return ident

    def edge(source, target, predicate, key=None, qualifiers=None):
        ident = _id("edge", [run, source, target, predicate, key])
        edges[ident] = {"id": ident, "source": source, "target": target, "predicate": predicate,
                        "qualifiers": deepcopy(qualifiers or {}), "claim_ids": []}
        return ident

    def unknown(reason, namespace, ident, path=None):
        item = {"reason": reason, "namespace": namespace, "id": ident, "path": path}
        if item not in unknowns:
            unknowns.append(item)

    def literal(value, owner, path):
        # JSON scalars/objects/arrays retain type, content and array order.
        return node(["literal", owner, path], "core_literal", {"json_value": value},
                    source_ref={"collection": "claims", "id": owner, "path": path})

    def reference(namespace, ident):
        key = (namespace, ident)
        record = indexes.get(namespace, {}).get(ident)
        known = record is not None
        qualifiers = {"resolution": "resolved" if known else "unresolved"}
        # Predicate names are relation semantics, not disposable entity names.
        if mode == "semantic" or namespace == "predicate":
            qualifiers["symbol"] = ident
        elif namespace not in {"claim"}:
            omit([namespace, ident], "reference_name", ident, "named_reference_identity_may_be_consistently_renamed")
        if namespace == "predicate":
            qualifiers["resolution"] = "data_predicate"
        if namespace == "scope":
            qualifiers["resolution"] = "declared_scope_reference"
        if namespace == "claim" and ident in selected_set:
            qualifiers = {}
            if mode == "semantic":
                qualifiers["symbol"] = ident
        result = node(["reference", namespace, ident], "core_" + namespace, qualifiers,
                      lexical={"namespace": namespace, "value": ident},
                      source_ref={"collection": COLLECTIONS.get(namespace), "id": ident})
        if key in built or key in building:
            return result
        building.add(key)
        if namespace == "scope":
            matches = [ns for ns in ("entity", "area", "instance") if ident in indexes[ns]]
            if len(matches) == 1:
                edge(result, reference(matches[0], ident), "scope_refers_to")
            elif len(matches) > 1:
                unknown("ambiguous_scope_reference", namespace, ident)
            else:
                unknown("scope_record_not_exported_or_context_kind_unsupported", namespace, ident)
        elif namespace == "predicate":
            pass
        elif not known:
            unknown("referenced_record_missing", namespace, ident)
        elif namespace == "entity":
            attrs = deepcopy(record)
            attrs.pop("id", None)
            parent = attrs.pop("parent", "")
            # Documented native producer convention, not arbitrary ID guessing:
            # extractors.cpp::Run::entity/run and extract/stage.cpp::merge_entity.
            extra = attrs.get("attrs")
            if isinstance(extra, dict):
                for field, target_namespace in (("units", "unit"), ("observations", "observation")):
                    if field not in extra:
                        continue
                    values = extra[field]
                    if isinstance(values, list) and all(_text(value) for value in values):
                        del extra[field]
                        for pos, value in enumerate(values):
                            edge(result, reference(target_namespace, value), "native_provenance:" + field,
                                 key=pos, qualifiers={"ordinal": pos, "convention": "native_extract_entity_attrs/1"})
                        if field == "observations" and len(values) >= 256:
                            unknown("native_entity_observation_list_may_be_capped_at_256", "entity", ident, "attrs.observations")
                    else:
                        unknown("native_entity_reference_list_invalid_retained_opaque", "entity", ident, "attrs." + field)
            if mode != "semantic":
                for field in ("canonical_key", "label", "labels", "aliases"):
                    if field in attrs:
                        omit(["entities", ident], field, attrs.pop(field), "lexical_entity_description_not_used_as_identity")
            nodes[result]["qualifiers"]["attributes"] = attrs
            if parent:
                edge(result, reference("entity", parent), "parent")
        elif namespace == "observation":
            attrs = deepcopy(record)
            attrs.pop("id", None)
            unit = attrs.pop("unit", "")
            locator = attrs.pop("locator", {})
            if mode != "semantic" and "text" in attrs:
                omit(["observations", ident], "text", attrs.pop("text"), "source_quote_retained_as_provenance_not_structure_label")
            nodes[result]["qualifiers"]["attributes"] = attrs
            if unit:
                edge(result, reference("unit", unit), "unit")
            add_locator(result, locator, ["observations", ident, "locator"])
        elif namespace == "unit":
            attrs = deepcopy(record)
            attrs.pop("id", None)
            source = attrs.pop("source", "")
            locator = attrs.pop("locator", {})
            if mode != "semantic" and "title" in attrs:
                omit(["units", ident], "title", attrs.pop("title"), "source_title_not_structure_label")
            nodes[result]["qualifiers"]["attributes"] = attrs
            if source:
                edge(result, reference("source", source), "source")
            add_locator(result, locator, ["units", ident, "locator"])
        elif namespace == "claim" and ident in selected_set:
            # Selected Claim bodies are filled below after every root exists.
            pass
        elif namespace == "claim":
            nodes[result]["qualifiers"]["outside_selected_claim_set"] = True
            nodes[result]["qualifiers"]["attributes"] = {k: deepcopy(v) for k, v in record.items() if k != "id"}
            unknown("referenced_claim_retained_opaque_outside_selection", namespace, ident)
        else:
            # No fabricated interpretation of optional principle/operator/etc
            # bodies. They remain exact opaque attributes plus source records.
            nodes[result]["qualifiers"]["attributes"] = {k: deepcopy(v) for k, v in record.items() if k != "id"}
            if namespace != "source":
                unknown("optional_record_semantics_retained_opaque", namespace, ident)
        building.remove(key)
        built.add(key)
        return result

    def add_locator(owner, locator, path):
        if not isinstance(locator, dict):
            unknown("locator_not_object", "locator", str(owner), path)
            nodes[owner]["qualifiers"]["locator_uninterpreted"] = deepcopy(locator)
            return
        attrs = deepcopy(locator)
        source = attrs.pop("source", "")
        if source:
            edge(owner, reference("source", source), "source_locator")
        if mode == "semantic":
            nodes[owner]["qualifiers"]["locator"] = attrs
        elif attrs:
            omit(path, "locator_coordinates", attrs, "raw_location_not_structure_label; source_reference_edge_preserved")

    # Construct roots before dependency edges so references reuse authoritative IDs.
    roots = {ident: reference("claim", ident) for ident in selected}
    for ident in selected:
        claim = indexes["claim"][ident]
        root = roots[ident]
        q = deepcopy(claim.get("qualifiers")) if isinstance(claim.get("qualifiers"), dict) else {"uninterpreted": claim.get("qualifiers")}
        scope = q.pop("scope", None)
        q["scope_presence"] = "declared" if _text(scope) else "explicit_unscoped" if scope == "" else "missing_or_invalid"
        a = deepcopy(claim.get("assessment")) if isinstance(claim.get("assessment"), dict) else {"uninterpreted": claim.get("assessment")}
        basis = a.pop("basis", {})
        alternatives = a.pop("alternatives", [])
        premises, counter, consequences = (a.pop(field, {}) for field in ("premises", "counter", "consequences"))
        nodes[root]["qualifiers"].update({"claim_qualifiers": q, "assessment_attributes": a,
                                          "source_contract": gates[ident]["source_contract"],
                                          "contract_errors": gates[ident]["errors"], "contract_unknowns": gates[ident]["unknowns"]})
        extra_claim = {k: deepcopy(v) for k, v in claim.items() if k not in {"id", "subject", "predicate", "object", "value", "qualifiers", "assessment"}}
        if extra_claim:
            nodes[root]["qualifiers"]["additional_claim_attributes"] = extra_claim
        for field, namespace in (("subject", "entity"), ("predicate", "predicate"), ("object", "entity")):
            if _text(claim.get(field)):
                edge(root, reference(namespace, claim[field]), field)
            elif field != "object":
                unknown("claim_endpoint_missing_or_invalid", "claim", ident, field)
        if claim.get("value") is not None:
            edge(root, literal(claim["value"], ident, "value"), "value")
        if _text(scope):
            edge(root, reference("scope", scope), "qualified_by_scope")
        elif scope not in (None, ""):
            nodes[root]["qualifiers"]["invalid_scope_value"] = scope
        if isinstance(alternatives, list):
            for pos, alternative in enumerate(alternatives):
                if not isinstance(alternative, dict):
                    edge(root, literal(alternative, ident, ["assessment", "alternatives", pos]), "uninterpreted_alternative", pos)
                    unknown("alternative_not_object", "claim", ident, pos)
                    continue
                attrs = {k: deepcopy(v) for k, v in alternative.items() if k not in {"object", "value"}}
                attrs["ordinal"] = pos
                alt_node = node(["alternative", ident, pos], "core_alternative", attrs,
                                source_ref={"collection": "claims", "id": ident, "path": ["assessment", "alternatives", pos]})
                edge(root, alt_node, "alternative", pos)
                if _text(alternative.get("object")):
                    edge(alt_node, reference("entity", alternative["object"]), "object")
                if alternative.get("value") is not None:
                    edge(alt_node, literal(alternative["value"], ident, ["assessment", "alternatives", pos, "value"]), "value")
        else:
            nodes[root]["qualifiers"]["alternatives_uninterpreted"] = alternatives
            unknown("alternatives_not_array", "claim", ident)
        for container, data, fields in (("premises", premises, {"claims": "claim", "principles": "principle"}),
                                        ("counter", counter, {"claims": "claim", "observations": "observation"}),
                                        ("consequences", consequences, {"claims": "claim", "predictions": "prediction", "checks": "check"})):
            if not isinstance(data, dict):
                nodes[root]["qualifiers"][container + "_uninterpreted"] = data
                unknown("assessment_reference_container_invalid", "claim", ident, container)
                continue
            remaining = deepcopy(data)
            for field, namespace in fields.items():
                values = remaining.pop(field, [])
                if not isinstance(values, list) or any(not _text(value) for value in values):
                    nodes[root]["qualifiers"][container + "." + field + "_uninterpreted"] = values
                    unknown("assessment_reference_list_invalid", "claim", ident, container + "." + field)
                    continue
                for pos, value in enumerate(values):
                    edge(root, reference(namespace, value), container + ":" + field, key=pos, qualifiers={"ordinal": pos})
            if remaining:
                nodes[root]["qualifiers"][container + "_attributes"] = remaining
        if not isinstance(basis, dict):
            nodes[root]["qualifiers"]["basis_uninterpreted"] = basis
            unknown("basis_invalid", "claim", ident)
            continue
        if not isinstance(basis.get("support", []), list):
            nodes[root]["qualifiers"]["support_uninterpreted"] = deepcopy(basis.get("support"))
            unknown("support_array_invalid_retained_opaque", "claim", ident)
        for pos, support in enumerate(basis.get("support", []) if isinstance(basis.get("support", []), list) else []):
            path = ["claims", ident, "assessment", "basis", "support", pos]
            if not isinstance(support, dict):
                unknown("support_not_object", "claim", ident, path)
                edge(root, literal(support, ident, path), "uninterpreted_support", pos)
                continue
            attrs = deepcopy(support)
            observation = attrs.pop("observation", "")
            locator = attrs.pop("locator", {})
            if mode != "semantic" and "quote" in attrs:
                omit(path, "quote", attrs.pop("quote"), "source_quote_not_structure_label")
            support_node = node(["support", ident, pos], "core_support", {"ordinal": pos, "attributes": attrs},
                                source_ref={"collection": "claims", "id": ident, "path": path})
            edge(root, support_node, "supported_by", pos)
            if _text(observation):
                edge(support_node, reference("observation", observation), "observation")
                observed = indexes["observation"].get(observation)
                if observed and isinstance(support.get("quote"), str) and support["quote"] not in observed.get("text", ""):
                    unknown("support_quote_not_in_exported_observation", "claim", ident, path)
            else:
                unknown("support_observation_reference_missing", "claim", ident, path)
            add_locator(support_node, locator, path + ["locator"])
        derivation = basis.get("derivation")
        if isinstance(derivation, dict):
            attrs = deepcopy(derivation)
            operator, morphism = attrs.pop("operator", ""), attrs.pop("morphism", "")
            derived_node = node(["derivation", ident], "core_derivation", attrs,
                                source_ref={"collection": "claims", "id": ident, "path": "assessment.basis.derivation"})
            edge(root, derived_node, "derivation")
            for namespace, value in (("operator", operator), ("morphism", morphism)):
                if _text(value):
                    edge(derived_node, reference(namespace, value), namespace)
        elif derivation is not None:
            nodes[root]["qualifiers"]["derivation_uninterpreted"] = derivation
        extra_basis = {k: deepcopy(v) for k, v in basis.items() if k not in {"support", "derivation"}}
        if extra_basis:
            nodes[root]["qualifiers"]["additional_basis_attributes"] = extra_basis
    for row in sorted(_list(export.get("slot_values", []), "slot_values"), key=canonical):
        if not isinstance(row, dict) or row.get("claim") not in selected_set:
            continue
        ident = row["claim"]
        role = row.get("role")
        if role not in ROLES | {None, ""}:
            unknown("unrecognized_universal_slot_role", "claim", ident, "slot_values")
        attrs = {k: deepcopy(v) for k, v in row.items() if k not in {"claim", "instance", "role"}}
        if role not in ROLES | {None, ""}:
            attrs["uninterpreted_role"] = role
        assignment = node(["slot_assignment", row], "core_slot_assignment", attrs,
                          role=role if role in ROLES else "", source_ref={"collection": "slot_values", "row": deepcopy(row)})
        edge(roots[ident], assignment, "slot_assignment")
        if _text(row.get("instance")):
            edge(assignment, reference("instance", row["instance"]), "instance")
        else:
            unknown("slot_instance_reference_missing", "claim", ident)
    # Propagate source ownership over directed projection references, never by
    # equal labels. This is a trace map, not an inference or identity operation.
    outgoing = defaultdict(list)
    for item in edges.values():
        outgoing[item["source"]].append(item)
    for claim_id, root in roots.items():
        queue, visited = deque([root]), set()
        while queue:
            current = queue.popleft()
            if current in visited:
                continue
            visited.add(current)
            nodes[current]["claim_ids"].append(claim_id)
            refs[current]["source_claim_ids"].append(claim_id)
            for item in outgoing[current]:
                item["claim_ids"].append(claim_id)
                queue.append(item["target"])
    if mode == "topology_control":
        for ident, item in nodes.items():
            omit(["projection", "nodes", ident], "kind_role_qualifiers_lexical_identity",
                 {k: item.get(k) for k in ("kind", "role", "qualifiers", "lexical_identity")}, "deliberately_lossy_topology_control")
            item.update(kind="topology_vertex", role="", qualifiers={})
            item.pop("lexical_identity", None)
        for ident, item in edges.items():
            omit(["projection", "edges", ident], "predicate_qualifiers",
                 {k: item[k] for k in ("predicate", "qualifiers")}, "deliberately_lossy_topology_control")
            item.update(predicate="incidence", qualifiers={})
    graph = {"nodes": sorted(nodes.values(), key=lambda item: item["id"]),
             "edges": sorted(edges.values(), key=lambda item: item["id"])}
    validate_graph(graph)
    unique_losses = {canonical(item): item for item in losses}
    unprojected = {collection: sorted(ident for ident in indexes[namespace] if (namespace, ident) not in built)
                   for namespace, collection in COLLECTIONS.items()}
    return {"version": VERSION, "mode": mode, "run_id": run, "structure": graph,
            "source_claim_ids": selected, "claim_node_ids": roots, "reference_map": refs,
            "source": deepcopy(export), "source_hash": hashlib.sha256(canonical(export).encode()).hexdigest(),
            "dropped_attributes": [unique_losses[key] for key in sorted(unique_losses)], "unknowns": unknowns, "gates": gates,
            "projection_scope": {"selected_claim_ids": selected,
                                 "excluded_claim_ids": sorted(set(indexes["claim"]) - selected_set),
                                 "records_retained_only_in_source": unprojected,
                                 "nonprojected_export_fields": sorted(set(export) - set(COLLECTIONS.values()) - {"run_id", "slot_values"})},
            "interpretation": "lossy_topology_control" if mode == "topology_control" else "core_incidence_view_not_semantic_validation",
            "restriction_preserving_mode": mode != "topology_control", "semantic_soundness": "not_assessed", "core_authoritative": True,
            "raw_source_verified": False, "inference_eligible": False, "persistable_claim": False,
            "automatic_mutation": False, "confidence": None,
            "loss_policy": {"source_attributes_deleted": False, "scope_membership_preserved": mode != "topology_control",
                            "status_evidence_preserved": mode != "topology_control",
                            "predicate_and_entity_kind_preserved": mode != "topology_control",
                            "all_unknown_attributes_preserved_as_opaque_data": True}}
