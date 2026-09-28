#!/usr/bin/env python3
"""Explicit assertion-shape view with reversible provenance/assessment sidecars.

This consumes a frozen core_projection result. It changes only comparison shape;
source assessments remain source assessments and never become truth guarantees.
"""
from __future__ import annotations

from collections import defaultdict, deque
from copy import deepcopy
import hashlib
import json

try:
    from .structure_methods import validate_graph
except ImportError:
    from structure_methods import validate_graph

VERSION = "core-assertion-view/1"
SOURCE_VERSION = "core-incidence-projection/1"
CONTROLS = {"exact_literals", "literal_type_control"}
PROVENANCE_EDGES = {"supported_by", "uninterpreted_support", "observation", "unit", "source", "source_locator",
                    "native_provenance:units", "native_provenance:observations"}
EPISTEMIC_FIELDS = {"evidence_class", "origin", "confidence", "status", "check_state"}
DEPENDENCY_EDGES = {"premises:claims", "counter:claims", "consequences:claims"}


def _canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def _hash(value):
    return hashlib.sha256(_canonical(value).encode("utf-8")).hexdigest()


def _json_type(value):
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, (int, float)):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, list):
        return "array"
    if isinstance(value, dict):
        return "object"
    raise ValueError("literal must be a JSON value")


def project_assertions(core_view, *, literal_policy="exact_literals"):
    """Keep assertion relations; retain excluded evidence and assessments verbatim.

    Scope/validity/branch/extra qualifiers, entity kind, predicate vocabulary,
    argument direction, identity bindings and universal slot roles remain exact.
    Epistemic labels move to sidecars; their compatibility is a separate report,
    not part of shape equality and never a confidence score.
    """
    if literal_policy not in CONTROLS:
        raise ValueError("literal_policy must be exact_literals or literal_type_control")
    if not isinstance(core_view, dict) or core_view.get("version") != SOURCE_VERSION:
        raise ValueError("input must be the frozen core_projection result")
    if core_view.get("mode") not in {"semantic", "structural"}:
        raise ValueError("assertion view requires a restriction-preserving source projection")
    source = core_view.get("source")
    if not isinstance(source, dict) or _hash(source) != core_view.get("source_hash"):
        raise ValueError("source snapshot hash differs from core projection reference")
    original = core_view["structure"]
    validate_graph(original)
    by_id = {node["id"]: node for node in original["nodes"]}
    roots = core_view.get("claim_node_ids", {})
    if not isinstance(roots, dict) or set(roots) != set(core_view.get("source_claim_ids", [])):
        raise ValueError("selected core Claim root map is inconsistent")
    if any(ident not in by_id or by_id[ident]["kind"] != "core_claim" for ident in roots.values()):
        raise ValueError("selected root must reference an actual projected core Claim node")
    outgoing = defaultdict(list)
    for edge in original["edges"]:
        outgoing[edge["source"]].append(edge)
    retained, retained_edges = set(), set()
    queue = deque(sorted(roots.values()))
    root_set = set(roots.values())
    while queue:
        ident = queue.popleft()
        if ident in retained:
            continue
        retained.add(ident)
        node = by_id[ident]
        # Referenced unselected Claims stay ports. An exported body outside the
        # selection never silently turns into an additional asserted root.
        if node["kind"] == "core_claim" and ident not in root_set:
            continue
        # Counter-observation ports preserve ref identity but no evidence closure.
        if node["kind"] == "core_observation":
            continue
        for edge in outgoing[ident]:
            if edge["predicate"] not in PROVENANCE_EDGES:
                retained_edges.add(edge["id"])
                queue.append(edge["target"])
    losses, changed_nodes, nodes = [], [], []

    def remove(attributes, keys, node_id, prefix):
        moved = {}
        for key in sorted(keys):
            if key in attributes:
                moved[key] = attributes.pop(key)
                losses.append({"node_id": node_id, "path": prefix + [key],
                               "action": "moved_to_sidecar", "source_value": deepcopy(moved[key]),
                               "reason": "epistemic_or_source_context_not_assertion_shape"})
        return moved

    assessment_by_node, reference_ports = {}, []
    for ident in sorted(retained):
        original_node = by_id[ident]
        node = deepcopy(original_node)
        qualifiers = node["qualifiers"]
        moved = {}
        if node["kind"] == "core_claim" and ident not in root_set:
            attrs = qualifiers.pop("attributes", None)
            if attrs is not None:
                moved["opaque_unselected_claim"] = attrs
                losses.append({"node_id": ident, "path": ["qualifiers", "attributes"],
                               "action": "moved_to_sidecar", "source_value": deepcopy(attrs),
                               "reason": "unselected_claim_body_is_reference_context_only"})
            node["kind"] = "core_claim_reference"
            qualifiers["reference_only"] = True
            reference_ports.append({"node_id": ident, "source_reference": deepcopy(core_view["reference_map"].get(ident)),
                                    "resolution": qualifiers.get("resolution", "unknown"),
                                    "selected_as_root": False, "interpreted_as_fact": False})
            losses.append({"node_id": ident, "path": ["kind"], "action": "explicit_reference_port",
                           "source_value": original_node["kind"], "reason": "unselected_or_unresolved_claim_is_not_a_new_asserted_root"})
        elif node["kind"] == "core_claim":
            assessment = qualifiers.get("assessment_attributes", {})
            if isinstance(assessment, dict):
                moved["assessment_attributes"] = remove(assessment, EPISTEMIC_FIELDS, ident, ["qualifiers", "assessment_attributes"])
            moved["contract_report"] = remove(qualifiers, {"source_contract", "contract_errors", "contract_unknowns"}, ident, ["qualifiers"])
            moved["uninterpreted_evidence"] = remove(qualifiers, {"support_uninterpreted"}, ident, ["qualifiers"])
        elif node["kind"] == "core_entity":
            attrs = qualifiers.get("attributes", {})
            if isinstance(attrs, dict):
                moved["entity_assessment_and_context"] = remove(attrs, EPISTEMIC_FIELDS | {"first_seen", "last_seen", "canonical_key", "label", "labels", "aliases"}, ident, ["qualifiers", "attributes"])
                if isinstance(attrs.get("attrs"), dict):
                    moved["entity_extractor_metadata"] = remove(attrs["attrs"], {"method"}, ident, ["qualifiers", "attributes", "attrs"])
        elif node["kind"] == "core_observation":
            moved["counter_observation_context"] = remove(qualifiers, {"attributes", "locator"}, ident, ["qualifiers"])
            qualifiers["reference_only"] = True
        elif node["kind"] == "core_alternative":
            moved["alternative_assessment"] = remove(qualifiers, {"score"}, ident, ["qualifiers"])
        if node["kind"] == "core_literal" and literal_policy == "literal_type_control" and "json_value" in qualifiers:
            value = qualifiers.pop("json_value")
            qualifiers["json_type"] = _json_type(value)
            losses.append({"node_id": ident, "path": ["qualifiers", "json_value"], "action": "unsafe_type_only_ablation",
                           "source_value": deepcopy(value), "reason": "literal_content_may_encode_negation_quantification_or_other_essential_meaning"})
        if any(moved.values()):
            assessment_by_node[ident] = moved
        if node != original_node:
            changed_nodes.append(deepcopy(original_node))
        nodes.append(node)
    removed_nodes = [deepcopy(node) for node in original["nodes"] if node["id"] not in retained]
    removed_edges = [deepcopy(edge) for edge in original["edges"] if edge["id"] not in retained_edges]
    losses.extend({"node_id": node["id"], "action": "moved_to_sidecar",
                   "path": ["structure", "nodes"], "sidecar_collection": "removed_nodes",
                   "reason": "outside_assertion_shape_closure"} for node in removed_nodes)
    losses.extend({"edge_id": edge["id"], "action": "moved_to_sidecar",
                   "path": ["structure", "edges"], "sidecar_collection": "removed_edges",
                   "reason": "evidence_edge_or_outside_assertion_shape_closure"} for edge in removed_edges)
    graph = {"nodes": nodes, "edges": [deepcopy(edge) for edge in original["edges"] if edge["id"] in retained_edges]}
    validate_graph(graph)
    claims = {claim["id"]: claim for claim in source.get("claims", [])}
    source_assessments = {claim_id: deepcopy(claims[claim_id].get("assessment")) for claim_id in roots}
    result = {"version": VERSION, "run_id": core_view["run_id"], "literal_policy": literal_policy,
              "structure": graph, "source_claim_ids": deepcopy(core_view["source_claim_ids"]),
              "claim_node_ids": deepcopy(roots), "reference_map": deepcopy(core_view["reference_map"]),
              "source": deepcopy(source), "source_hash": core_view["source_hash"],
              "source_projection": {"version": core_view["version"], "mode": core_view["mode"],
                                    "graph_hash": _hash(original), "dropped_attributes": deepcopy(core_view["dropped_attributes"]),
                                    "projection_scope": deepcopy(core_view["projection_scope"])},
              "source_assessments": source_assessments, "assessment_by_node": assessment_by_node,
              "sidecar": {"removed_nodes": removed_nodes, "removed_edges": removed_edges, "changed_nodes_before_projection": changed_nodes},
              "loss_map": losses,
              "unknowns": deepcopy(core_view["unknowns"]), "source_gates": deepcopy(core_view["gates"]),
              "reference_ports": reference_ports,
              "counts": {"source_nodes": len(original["nodes"]), "assertion_nodes": len(nodes),
                         "source_edges": len(original["edges"]), "assertion_edges": len(graph["edges"]),
                         "selected_claim_roots": len(roots), "external_claim_reference_ports": len(reference_ports),
                         "explicit_claim_dependency_edges": sum(edge["predicate"] in DEPENDENCY_EDGES for edge in graph["edges"])},
              "interpretation": "assertion_shape_only; source_assessment_compatibility_is_separate",
              "comparison_admissible": literal_policy == "exact_literals",
              "analogy_or_entailment_established": False,
              "control_warning": "literal_type_control_cannot_establish_analogy_or_entailment" if literal_policy != "exact_literals" else None,
              "confidence": None, "inference_eligible": False, "persistable_claim": False,
              "automatic_mutation": False}
    result["id"] = "assertion_view_" + _hash([VERSION, result["source_projection"]["graph_hash"], literal_policy])[:24]
    return result


def restore_core_graph(assertion_view):
    """Reconstruct the input projection graph exactly from the retained sidecars."""
    nodes = {node["id"]: deepcopy(node) for node in assertion_view["structure"]["nodes"]}
    for node in assertion_view["sidecar"]["removed_nodes"] + assertion_view["sidecar"]["changed_nodes_before_projection"]:
        nodes[node["id"]] = deepcopy(node)
    edges = assertion_view["structure"]["edges"] + assertion_view["sidecar"]["removed_edges"]
    graph = {"nodes": sorted(nodes.values(), key=lambda item: item["id"]),
             "edges": sorted(deepcopy(edges), key=lambda item: item["id"])}
    if _hash(graph) != assertion_view["source_projection"]["graph_hash"]:
        raise ValueError("sidecar reconstruction differs from original core projection")
    return graph


def assessment_dimensions(left, right, node_mapping):
    """Report source assessment dimensions for a supplied shape witness, no score."""
    if not isinstance(node_mapping, dict):
        raise ValueError("node_mapping must be a supplied graph witness mapping")
    left_roots = {node: claim for claim, node in left["claim_node_ids"].items()}
    right_roots = {node: claim for claim, node in right["claim_node_ids"].items()}
    rows, unmatched = [], []
    for node, claim_id in sorted(left_roots.items()):
        target = node_mapping.get(node)
        if target not in right_roots:
            unmatched.append(claim_id)
            continue
        other_id = right_roots[target]
        a, b = left["source_assessments"][claim_id], right["source_assessments"][other_id]
        a = a if isinstance(a, dict) else {}
        b = b if isinstance(b, dict) else {}
        dimensions = {field: {"left": deepcopy(a.get(field)), "right": deepcopy(b.get(field)),
                              "left_present": field in a, "right_present": field in b,
                              "equal": a[field] == b[field] if field in a and field in b else None}
                      for field in sorted(EPISTEMIC_FIELDS)}
        rows.append({"left_claim_id": claim_id, "right_claim_id": other_id, "dimensions": dimensions,
                     "left_source_gate": deepcopy(left["source_gates"][claim_id]),
                     "right_source_gate": deepcopy(right["source_gates"][other_id])})
    return {"mapped_claim_assessments": rows, "unmapped_left_claim_ids": unmatched,
            "aggregate_score": None, "confidence": None, "identity_verified": False,
            "meaning": "source_dimension_comparison_only; no_assessment_changed_or_truth_established"}
