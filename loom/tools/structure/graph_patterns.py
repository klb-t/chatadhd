#!/usr/bin/env python3
"""Bounded recurring neighborhoods in supplied typed directed Loom graph views.

WL fingerprints propose comparisons, never prove motif equivalence. A motif joins
a candidate only after exact injective graph matching at equal graph sizes. All
occurrences and source claims remain separate. No graph or validation is mutated.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from copy import deepcopy
import hashlib
import json
from pathlib import Path

VERSION = "graph-pattern-experiment/1"


def _canonical(value) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def _digest(value) -> str:
    return hashlib.sha256(_canonical(value).encode("utf-8")).hexdigest()


def _positive(name: str, value: int, minimum: int = 1, maximum: int = 100000) -> None:
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{name} must be an integer in [{minimum},{maximum}]")


def _project(graph: dict) -> dict:
    # Shared generic-graph validation and exact label interpretation. This is
    # an identity projection: no scope, symbol or lexical identity is erased.
    try:
        from .graph_search import comparison_graph
    except ImportError:
        from graph_search import comparison_graph
    return comparison_graph(graph, goal="literal_semantic")["graph"]


def _decorate(graph: dict, root: dict) -> dict:
    result = deepcopy(graph)
    for node in result["nodes"]:
        positions = []
        if root["kind"] == "node" and node["id"] == root["node_id"]:
            positions.append("root")
        if root["kind"] == "edge":
            if node["id"] == root["source"]:
                positions.append("edge_source")
            if node["id"] == root["target"]:
                positions.append("edge_target")
        # Wrapping avoids collision with ANY source qualifier name. These
        # transient projection annotations are never new domain/argument roles.
        node["qualifiers"] = {"source_qualifiers": node.get("qualifiers", {}),
                              "motif_root_positions": positions}
    for index, edge in enumerate(result["edges"]):
        edge["qualifiers"] = {"source_qualifiers": edge.get("qualifiers", {}),
                              "motif_root_edge": root["kind"] == "edge" and index == root["local_edge_index"]}
    return result


def fingerprint(graph: dict, root: dict | None = None, rounds: int = 2) -> str:
    """ID/order-invariant directed, typed 1-WL fingerprint, NOT a proof.

    Includes kind, universal role, label, lexical_identity and all qualifiers.
    IDs, Claim IDs and provenance do not enter structural comparison.
    """
    _positive("rounds", rounds, 0, 8)
    graph = _project(_decorate(graph, root) if root else graph)
    labels = {n["id"]: _digest({k: n.get(k, default) for k, default in
                                (("kind", ""), ("role", ""), ("qualifiers", {}),
                                 ("label", None), ("lexical_identity", None))}) for n in graph["nodes"]}
    incoming, outgoing = defaultdict(list), defaultdict(list)
    edge_labels = []
    for edge in graph["edges"]:
        label = _canonical([edge["predicate"], edge.get("qualifiers", {})])
        edge_labels.append(label)
        outgoing[edge["source"]].append((label, edge["target"]))
        incoming[edge["target"]].append((label, edge["source"]))
    layers = [sorted(Counter(labels.values()).items())]
    for _ in range(rounds):
        labels = {ident: _digest([label,
                                 sorted(("out", edge, labels[other]) for edge, other in outgoing[ident]),
                                 sorted(("in", edge, labels[other]) for edge, other in incoming[ident])])
                  for ident, label in labels.items()}
        layers.append(sorted(Counter(labels.values()).items()))
    return _digest({"version": VERSION, "rounds": rounds, "layers": layers,
                    "edge_labels": sorted(Counter(edge_labels).items())})


def _neighborhood(graph: dict, seeds: set[str], radius: int, max_nodes: int):
    adjacent = defaultdict(set)
    by_id = {n["id"]: n for n in graph["nodes"]}
    for edge in graph["edges"]:
        adjacent[edge["source"]].add(edge["target"])
        adjacent[edge["target"]].add(edge["source"])
    selected, frontier = set(seeds), set(seeds)
    if len(selected) > max_nodes:
        return None
    for _ in range(radius):
        frontier = {other for ident in frontier for other in adjacent[ident]} - selected
        selected.update(frontier)
        if len(selected) > max_nodes:
            return None
        if not frontier:
            break
    edge_indices = [i for i, edge in enumerate(graph["edges"])
                    if edge["source"] in selected and edge["target"] in selected]
    motif = {"nodes": [deepcopy(by_id[ident]) for ident in sorted(selected)],
             "edges": [deepcopy(graph["edges"][i]) for i in edge_indices]}
    return motif, edge_indices


def _leaf_count(value) -> int:
    if isinstance(value, dict):
        return sum(_leaf_count(v) for v in value.values())
    if isinstance(value, list):
        return sum(_leaf_count(v) for v in value)
    return 1


def specificity(graph: dict) -> dict:
    """Retained detail counts, independent of the number of occurrences."""
    return {"node_count": len(graph["nodes"]), "edge_count": len(graph["edges"]),
            "node_kind_count": len({n.get("kind", "") for n in graph["nodes"]}),
            "relation_type_count": len({e["predicate"] for e in graph["edges"]}),
            "qualifier_leaf_count": sum(_leaf_count(n.get("qualifiers", {})) for n in graph["nodes"]) +
                                    sum(_leaf_count(e.get("qualifiers", {})) for e in graph["edges"]),
            "labeled_node_count": sum("label" in n for n in graph["nodes"]),
            "lexical_identity_count": sum("lexical_identity" in n for n in graph["nodes"]),
            "interpretation": "retained_detail_counts_not_a_probability_or_universality_inverse"}


def _occurrence(record: dict, motif: dict, root: dict, edge_indices: list[int], radius: int) -> dict:
    node_ids = [node["id"] for node in motif["nodes"]]
    selected = set(node_ids)
    boundary_edges = [{"source_edge_index": i, "edge": deepcopy(edge)}
                      for i, edge in enumerate(record["structure"]["edges"])
                      if (edge["source"] in selected) != (edge["target"] in selected)]
    claim_ids = sorted({claim for item in motif["nodes"] + motif["edges"] for claim in item.get("claim_ids", [])})
    return {"id": "occ_" + _digest([record["id"], root, node_ids, edge_indices])[:24],
            "record_id": record["id"], "source_group": record.get("source_group"),
            "domain": record.get("domain"), "root": deepcopy(root), "radii": [radius],
            "source_node_ids": node_ids, "source_edge_indices": edge_indices,
            "source_claim_ids": claim_ids,
            "boundary_edges": boundary_edges,
            "scope_completeness": "not_established_by_neighborhood",
            "source_node_provenance": {n["id"]: deepcopy(n.get("provenance", [])) for n in motif["nodes"]},
            "source_edge_provenance": [{"edge_index": i, "provenance": deepcopy(e.get("provenance", []))}
                                       for i, e in zip(edge_indices, motif["edges"])]}


def _support(occurrences: list[dict]) -> dict:
    groups = sorted({o["source_group"] for o in occurrences if o["source_group"] is not None})
    domains = sorted({o["domain"] for o in occurrences if o["domain"] is not None})
    unknown_groups = sorted({o["record_id"] for o in occurrences if o["source_group"] is None})
    unknown_domains = sorted({o["record_id"] for o in occurrences if o["domain"] is None})
    by_domain = {domain: sorted({o["source_group"] for o in occurrences
                                if o["domain"] == domain and o["source_group"] is not None}) for domain in domains}
    return {"occurrence_count": len(occurrences), "record_count": len({o["record_id"] for o in occurrences}),
            "source_groups": groups, "independent_support": len(groups),
            "ungrouped_record_ids": unknown_groups, "domain_count": len(domains), "domains": domains,
            "unknown_domain_record_ids": unknown_domains, "domain_source_groups": by_domain,
            "universality_observation": "observed_domain_breadth_only_not_universal_validity",
            "independence_assumption": "caller_declared_source_groups_not_verified"}


def discover_patterns(records: list[dict], *, radii=(1, 2), roots=("node", "edge"),
                      max_enumerations: int = 256, max_nodes: int = 12, min_nodes: int = 2,
                      min_edges: int = 1, wl_rounds: int = 2, alignment_budget: int = 10000,
                      max_verifications: int = 10000, min_occurrences: int = 2) -> dict:
    """Generate recurring rooted neighborhood candidates without a query graph.

    Inputs are {id, structure:{nodes,edges}, source_group?, domain?}. Structural
    equality uses the literal semantic labels of the supplied graph view. Supply
    an explicitly justified abstract projection to discover more general motifs;
    this module never silently removes names, scope or polarity.
    """
    try:
        from .graph_search import match_subgraph
    except ImportError:
        from graph_search import match_subgraph
    for name, value, minimum in (("max_enumerations", max_enumerations, 1), ("max_nodes", max_nodes, 1),
                                 ("min_nodes", min_nodes, 1), ("min_edges", min_edges, 0),
                                 ("alignment_budget", alignment_budget, 1), ("max_verifications", max_verifications, 0),
                                 ("min_occurrences", min_occurrences, 2)):
        _positive(name, value, minimum)
    _positive("wl_rounds", wl_rounds, 0, 8)
    if min_nodes > max_nodes:
        raise ValueError("min_nodes exceeds max_nodes")
    if not radii or any(type(radius) is not int or not 0 <= radius <= 4 for radius in radii):
        raise ValueError("radii must be nonempty integers in [0,4]")
    if not roots or any(root not in {"node", "edge"} for root in roots):
        raise ValueError("roots must contain node and/or edge")
    if not isinstance(records, list):
        raise ValueError("records must be an array")
    inputs, seen = [], set()
    for record in records:
        ident = record.get("id")
        if not isinstance(ident, str) or not ident or ident in seen:
            raise ValueError("record ids must be nonempty and unique")
        seen.add(ident)
        for field in ("source_group", "domain"):
            if record.get(field) is not None and (not isinstance(record[field], str) or not record[field]):
                raise ValueError(f"{field} must be a nonempty string or null")
        _project(record.get("structure", {}))
        for item in record["structure"]["nodes"] + record["structure"]["edges"]:
            claim_ids = item.get("claim_ids", [])
            if not isinstance(claim_ids, list) or any(not isinstance(c, str) or not c for c in claim_ids):
                raise ValueError("claim_ids must be an array of nonempty source Claim IDs")
        inputs.append(deepcopy(record))
    inputs.sort(key=lambda record: record["id"])
    stats = {"planned_enumerations": sum((len(r["structure"]["nodes"]) * ("node" in roots) +
                                          len(r["structure"]["edges"]) * ("edge" in roots)) * len(set(radii)) for r in inputs),
             "enumerations": 0, "oversized_neighborhoods": 0, "below_minimum_neighborhoods": 0,
             "duplicate_radius_views": 0, "unique_motifs": 0, "verification_calls": 0,
             "verified_matches": 0, "verified_nonmatches": 0, "verification_unknown": 0,
             "verification_skipped_by_budget": 0}
    buckets, occurrence_seen, verification_log = defaultdict(list), {}, []
    cheap_occurrences = defaultdict(list)
    for record in inputs:
        graph = record["structure"]
        seeds = []
        if "node" in roots:
            seeds.extend({"kind": "node", "node_id": ident} for ident in sorted(n["id"] for n in graph["nodes"]))
        if "edge" in roots:
            seeds.extend({"kind": "edge", "source": edge["source"], "target": edge["target"], "source_edge_index": i}
                         for i, edge in sorted(enumerate(graph["edges"]), key=lambda item:
                                               (item[1]["source"], item[1]["target"], item[1]["predicate"],
                                                _canonical(item[1].get("qualifiers", {})), item[0])))
        for seed in seeds:
            for radius in sorted(set(radii)):
                if stats["enumerations"] >= max_enumerations:
                    break
                stats["enumerations"] += 1
                selected = {seed["node_id"]} if seed["kind"] == "node" else {seed["source"], seed["target"]}
                neighborhood = _neighborhood(graph, selected, radius, max_nodes)
                if neighborhood is None:
                    stats["oversized_neighborhoods"] += 1
                    continue
                motif, edge_indices = neighborhood
                if len(motif["nodes"]) < min_nodes or len(motif["edges"]) < min_edges:
                    stats["below_minimum_neighborhoods"] += 1
                    continue
                root = deepcopy(seed)
                if seed["kind"] == "edge":
                    root["local_edge_index"] = edge_indices.index(seed["source_edge_index"])
                occurrence = _occurrence(record, motif, root, edge_indices, radius)
                if occurrence["id"] in occurrence_seen:
                    occurrence_seen[occurrence["id"]]["radii"].append(radius)
                    stats["duplicate_radius_views"] += 1
                    continue
                occurrence_seen[occurrence["id"]] = occurrence
                stats["unique_motifs"] += 1
                key = fingerprint(motif, root, wl_rounds)
                cheap_occurrences[key].append(occurrence)
                decorated = _decorate(motif, root)
                group = None
                for candidate in buckets[key]:
                    if stats["verification_calls"] >= max_verifications:
                        stats["verification_skipped_by_budget"] += 1
                        verification_log.append({"candidate_id": candidate["id"], "occurrence_id": occurrence["id"],
                                                 "status": "not_attempted_budget"})
                        continue
                    representative = candidate["comparison_graph"]
                    # Equal cardinalities make an injective multigraph match an
                    # isomorphism; approximate fingerprints alone never merge.
                    if len(representative["nodes"]) != len(decorated["nodes"]) or len(representative["edges"]) != len(decorated["edges"]):
                        raise AssertionError("WL bucket lost graph cardinality")
                    matched = match_subgraph(representative, decorated, goal="literal_semantic", state_budget=alignment_budget)
                    stats["verification_calls"] += 1
                    status = matched["status"]
                    verification_log.append({"candidate_id": candidate["id"], "occurrence_id": occurrence["id"], "status": status})
                    if status == "matched":
                        stats["verified_matches"] += 1
                        group = candidate
                        occurrence["witness"] = {"status": "verified_isomorphism", "node_mapping": matched["node_mapping"],
                                                  "edge_mapping": matched["edge_mapping"],
                                                  "source_edge_witness": [{"representative_edge": m["pattern_edge"],
                                                                            "source_edge_index": edge_indices[m["host_edge"]]}
                                                                           for m in matched["edge_mapping"]],
                                                  "edge_mapping_domain": "representative_local_to_occurrence_local"}
                        break
                    if status == "different":
                        stats["verified_nonmatches"] += 1
                    else:
                        stats["verification_unknown"] += 1
                if group is None:
                    group = {"id": "pattern_" + key[:20] + "_" + str(len(buckets[key])),
                             "fingerprint": key, "fingerprint_semantics": "1wl_invariant_not_equivalence_proof",
                             "representative": motif, "representative_root": root, "comparison_graph": decorated,
                             "occurrences": [], "specificity": specificity(motif)}
                    occurrence["witness"] = {"status": "representative_identity", "node_mapping": {n["id"]: n["id"] for n in motif["nodes"]},
                                              "edge_mapping": [{"pattern_edge": i, "host_edge": i} for i in range(len(motif["edges"]))],
                                              "source_edge_witness": [{"representative_edge": i, "source_edge_index": original}
                                                                       for i, original in enumerate(edge_indices)],
                                              "edge_mapping_domain": "representative_local_to_occurrence_local"}
                    buckets[key].append(group)
                group["occurrences"].append(occurrence)
    candidates = []
    singleton_count = 0
    for key in sorted(buckets):
        for group in buckets[key]:
            if len(group["occurrences"]) < min_occurrences:
                singleton_count += 1
                continue
            group = deepcopy(group)
            del group["comparison_graph"]
            group["support"] = _support(group["occurrences"])
            group["source_claim_ids"] = sorted({c for o in group["occurrences"] for c in o["source_claim_ids"]})
            group.update(status="candidate", evidence_class="not_assigned", validation_status="not_promoted",
                         persistable_claim=False, mutations_applied=0, confidence=None)
            candidates.append(group)
    cheap_recurring = {key: value for key, value in cheap_occurrences.items() if len(value) >= min_occurrences}
    return {"version": VERSION, "parameters": {"radii": sorted(set(radii)), "roots": sorted(set(roots)),
              "max_enumerations": max_enumerations, "max_nodes": max_nodes, "min_nodes": min_nodes,
              "min_edges": min_edges, "wl_rounds": wl_rounds, "alignment_budget": alignment_budget,
              "max_verifications": max_verifications, "min_occurrences": min_occurrences},
            "input_hash": _digest(inputs), "candidates": candidates, "statistics": stats,
            "comparison": {"cheap_recurring_fingerprint_buckets": len(cheap_recurring),
                           "cheap_occurrences_in_recurring_buckets": sum(len(v) for v in cheap_recurring.values()),
                           "verified_recurring_motifs": len(candidates), "nonrecurring_candidate_groups": singleton_count,
                           "buckets_with_unmerged_groups": sum(len(groups) > 1 for groups in buckets.values()),
                           "interpretation": "fingerprint_retrieval_vs_verified_equality_not_semantic_accuracy"},
            "verification_log": verification_log,
            "coverage": {"enumeration_complete": stats["enumerations"] == stats["planned_enumerations"],
                         "omitted_enumerations": stats["planned_enumerations"] - stats["enumerations"],
                         "all_requested_neighborhoods_represented": stats["oversized_neighborhoods"] == 0 and
                             stats["enumerations"] == stats["planned_enumerations"],
                         "verification_complete": not(stats["verification_unknown"] or stats["verification_skipped_by_budget"]),
                         "search_scope": "induced_undirected_radius_neighborhoods_with_directed_typed_edges"},
            "limitations": ["only_supplied_graph_view", "wl_collisions_require_exact_verification",
                            "absence_under_bounds_is_unknown", "no_entity_merge_or_claim_promotion",
                            "local_isomorphism_does_not_establish_source_scope_completeness",
                            "source_group_independence_is_caller_supplied", "no_graph_persistence"]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="JSON array of records or {records, parameters}")
    args = parser.parse_args()
    value = json.loads(args.input.read_text(encoding="utf-8"))
    records = value if isinstance(value, list) else value["records"]
    parameters = {} if isinstance(value, list) else value.get("parameters", {})
    print(json.dumps(discover_patterns(records, **parameters), ensure_ascii=False, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
