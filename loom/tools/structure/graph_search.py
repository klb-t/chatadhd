#!/usr/bin/env python3
"""Bounded matching of existing directed, typed graph patterns.

No extraction, ontology, persistence or truth assertion. Matching is non-induced:
extra host nodes/edges are permitted, while pattern nodes map injectively and
every pattern edge requires a distinct, identically labeled host edge.
"""
from __future__ import annotations

from collections import Counter, defaultdict
from copy import deepcopy
import json
from typing import Any

try:
    from .structure_methods import cosine, role_relation_features, validate_graph, wl_features
except ImportError:
    from structure_methods import cosine, role_relation_features, validate_graph, wl_features

VERSION = "graph-native-search/1"
GOALS = {"literal_semantic", "structural_analogy"}
NODE_FIELDS = {"id", "kind", "role", "qualifiers", "label", "lexical_identity", "claim_ids", "provenance"}
EDGE_FIELDS = {"id", "source", "target", "predicate", "qualifiers", "claim_ids", "provenance"}
PROJECTION_FIELDS = {"label", "symbol", "lexical_identity"}


def _key(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)


def _validate(graph: dict) -> None:
    validate_graph(graph)
    for node in graph["nodes"]:
        extra = set(node) - NODE_FIELDS
        if extra:
            raise ValueError(f"unrepresented node fields {sorted(extra)}; put semantics in qualifiers, evidence in provenance")
        if not isinstance(node.get("kind"), str) or not node["kind"]:
            raise ValueError("node kind must be a nonempty type string")
        if "label" in node and not isinstance(node["label"], str):
            raise ValueError("label must be a string when present")
        if "lexical_identity" in node:
            identity = node["lexical_identity"]
            if not isinstance(identity, dict) or set(identity) != {"namespace", "value"} or not all(
                    isinstance(identity[k], str) and identity[k] for k in identity):
                raise ValueError("lexical_identity requires nonempty namespace and value strings")
        if "symbol" in node.get("qualifiers", {}) and not isinstance(node["qualifiers"]["symbol"], str):
            raise ValueError("qualifiers.symbol must be a string")
    for edge in graph["edges"]:
        extra = set(edge) - EDGE_FIELDS
        if extra:
            raise ValueError(f"unrepresented edge fields {sorted(extra)}; put semantics in qualifiers, evidence in provenance")
    # JSON canonicalization makes nested qualifiers exact and rejects NaN/Inf.
    _key(graph)


def _projection(label_projection: dict | None, goal: str, side: str) -> dict:
    if goal not in GOALS or side not in {"pattern", "host"}:
        raise ValueError("goal must be literal_semantic or structural_analogy; side must be pattern or host")
    if goal == "literal_semantic":
        if label_projection is not None:
            raise ValueError("literal_semantic forbids name projection")
        return {}
    if not isinstance(label_projection, dict) or set(label_projection) != {"pattern", "host"}:
        raise ValueError("structural_analogy requires explicit {pattern:{node_id:...},host:{node_id:...}} projection")
    if not all(isinstance(label_projection[k], dict) for k in ("pattern", "host")):
        raise ValueError("projection sides must be objects")
    return label_projection[side]


def comparison_graph(graph: dict, *, side: str = "pattern", goal: str = "literal_semantic",
                     label_projection: dict | None = None) -> dict:
    """Prepare exact comparison labels without silently removing constraints.

    An explicit analogy projection can replace only node label, qualifiers.symbol,
    and lexical_identity.value. Kind, role, identity namespace, edge predicates,
    direction and every other qualifier stay exact. Repeated lexical identities
    are also checked globally by match_subgraph, beyond this label projection.
    """
    _validate(graph)
    projection = _projection(label_projection, goal, side)
    ids = {node["id"] for node in graph["nodes"]}
    if set(projection) - ids:
        raise ValueError("projection references unknown " + side + " nodes")
    result = {"nodes": [], "edges": []}
    changes = []
    for node in graph["nodes"]:
        names = projection.get(node["id"], {})
        if not isinstance(names, dict) or set(names) - PROJECTION_FIELDS or not all(isinstance(v, str) for v in names.values()):
            raise ValueError("projection values support string label, symbol and lexical_identity only")
        qualifiers = deepcopy(node.get("qualifiers", {}))
        lexical = {}
        if "label" in node:
            lexical["label"] = node["label"]
        if "lexical_identity" in node:
            lexical["lexical_identity"] = deepcopy(node["lexical_identity"])
        for field, replacement in names.items():
            if field == "symbol":
                if "symbol" not in qualifiers:
                    raise ValueError("cannot project a missing qualifiers.symbol")
                original = qualifiers["symbol"]
                qualifiers["symbol"] = replacement
            elif field == "lexical_identity":
                if field not in lexical:
                    raise ValueError("cannot project a missing lexical_identity")
                original = lexical[field]["value"]
                lexical[field]["value"] = replacement
            else:
                if field not in lexical:
                    raise ValueError("cannot project a missing label")
                original = lexical[field]
                lexical[field] = replacement
            changes.append({"side": side, "node_id": node["id"], "field": field,
                            "original": original, "projected": replacement,
                            "information_loss": "literal_name_replaced; consistent_injective_renaming_still_required"})
        result["nodes"].append({"id": node["id"], "kind": node["kind"], "role": node.get("role", ""),
                                "qualifiers": {"semantic": qualifiers, "lexical": lexical},
                                "claim_ids": deepcopy(node.get("claim_ids", []))})
    for edge in graph["edges"]:
        result["edges"].append({"source": edge["source"], "target": edge["target"],
                                "predicate": edge["predicate"], "qualifiers": deepcopy(edge.get("qualifiers", {})),
                                "claim_ids": deepcopy(edge.get("claim_ids", []))})
    return {"graph": result,
            "projection_report": {"goal": goal, "changes": changes,
                                  "preserved": ["kind", "role", "nonlexical_qualifiers", "identity_namespace",
                                                "edge_predicate", "direction", "multiplicity", "scope_bindings"],
                                  "ignored_metadata": ["ids_as_names", "claim_ids", "provenance"],
                                  "scope_policy": "scope IDs in qualifiers are exact; cross-source scope renaming requires explicit shared scope vertices and binding edges"}}


def _label(node: dict) -> str:
    return _key([node["kind"], node.get("role", ""), node.get("qualifiers", {})])


def _edge_label(edge: dict) -> str:
    return _key([edge["predicate"], edge.get("qualifiers", {})])


def _identities(node: dict) -> dict:
    result = {}
    # Multiple lexical channels are explicit. Repeated spellings are preserved
    # within their namespace; labels are not silently treated as independent.
    if "label" in node:
        result["label:" + node["kind"]] = node["label"]
    if "symbol" in node.get("qualifiers", {}):
        result["symbol:" + node["kind"]] = node["qualifiers"]["symbol"]
    if "lexical_identity" in node:
        result["identity:" + node["lexical_identity"]["namespace"]] = node["lexical_identity"]["value"]
    return result


class _Index:
    def __init__(self, graph: dict):
        self.graph = graph
        self.labels = {node["id"]: _label(node) for node in graph["nodes"]}
        self.by_label = defaultdict(list)
        for ident, label in self.labels.items():
            self.by_label[label].append(ident)
        for values in self.by_label.values():
            values.sort()
        self.label_counts = Counter(self.labels.values())
        self.edge_counts = Counter()
        self.neighborhood = defaultdict(Counter)
        self.pairs = defaultdict(Counter)
        self.edge_indices = defaultdict(list)
        for index, edge in enumerate(graph["edges"]):
            src, dst, label = edge["source"], edge["target"], _edge_label(edge)
            self.edge_counts[(self.labels[src], label, self.labels[dst])] += 1
            self.neighborhood[src][("out", label, self.labels[dst])] += 1
            self.neighborhood[dst][("in", label, self.labels[src])] += 1
            self.pairs[(src, dst)][label] += 1
            self.edge_indices[(src, dst, label)].append(index)


def _contained(required: Counter, available: Counter) -> bool:
    return all(available.get(key, 0) >= count for key, count in required.items())


def _domains(pattern: _Index, host: _Index) -> tuple[dict, dict]:
    report = {"possible": True, "reason": "necessary_conditions_passed", "candidate_pairs": 0,
              "checks": ["node_label_multiplicity", "typed_directed_edge_multiplicity", "labeled_degree_lower_bounds", "self_loop_multiplicity"],
              "wl_used_as_hard_filter": False}
    if not _contained(pattern.label_counts, host.label_counts):
        report.update(possible=False, reason="insufficient_node_labels")
        return {}, report
    if not _contained(pattern.edge_counts, host.edge_counts):
        report.update(possible=False, reason="insufficient_typed_edges")
        return {}, report
    domains = {}
    for p in pattern.labels:
        domains[p] = [h for h in host.by_label[pattern.labels[p]]
                      if _contained(pattern.neighborhood[p], host.neighborhood[h])
                      and _contained(pattern.pairs.get((p, p), Counter()), host.pairs.get((h, h), Counter()))]
        report["candidate_pairs"] += len(domains[p])
        if not domains[p]:
            report.update(possible=False, reason="empty_degree_compatible_domain")
            return domains, report
    return domains, report


def _prepare(pattern, host, goal, projection):
    p = comparison_graph(pattern, goal=goal, side="pattern", label_projection=projection)
    h = comparison_graph(host, goal=goal, side="host", label_projection=projection)
    pi, hi = _Index(p["graph"]), _Index(h["graph"])
    domains, filters = _domains(pi, hi)
    return p, h, pi, hi, domains, filters


def match_subgraph(pattern: dict, host: dict, *, goal: str = "literal_semantic",
                   label_projection: dict | None = None, state_budget: int = 10000) -> dict:
    """Find one directed non-induced injective witness within a state budget.

    Safe count/degree filters can disprove a match without search. A truncated
    search returns budget_exhausted with matched=None, never a false nonmatch.
    It returns one witness, not all occurrences, and makes no logical-validity or
    real-world-identity assertion. No source graph is altered.
    """
    if type(state_budget) is not int or state_budget < 0:
        raise ValueError("state_budget must be a nonnegative integer")
    p, h, pi, hi, domains, filters = _prepare(pattern, host, goal, label_projection)
    result = {"version": VERSION, "goal": goal, "status": "different", "matched": False,
              "node_mapping": {}, "edge_mapping": [], "lexical_renaming": [],
              "states_explored": 0, "state_budget": state_budget, "filters": filters,
              "projection_report": {"pattern": p["projection_report"], "host": h["projection_report"]},
              "interpretation": "typed_graph_pattern_witness_only; semantic_identity_and_inference_validity_not_assessed",
              "confidence": None, "persistable_claim": False, "automatic_mutation": False}
    if not pi.labels:
        result.update(status="unrepresented", matched=None)
        return result
    if not filters["possible"]:
        return result
    originals_p = {n["id"]: _identities(n) for n in pattern["nodes"]}
    originals_h = {n["id"]: _identities(n) for n in host["nodes"]}
    order = sorted(pi.labels, key=lambda node: (len(domains[node]), -sum(pi.neighborhood[node].values()), node))
    mapping, used = {}, set()
    forward, reverse, binding_changes = {}, {}, {}
    stack = [iter(domains[order[0]])]
    while stack:
        depth = len(stack) - 1
        node = order[depth]
        try:
            candidate = next(stack[-1])
        except StopIteration:
            stack.pop()
            if stack:
                prior = order[len(stack) - 1]
                used.remove(mapping.pop(prior))
                for namespace, original, renamed in binding_changes.pop(prior):
                    del forward[(namespace, original)]
                    del reverse[(namespace, renamed)]
            continue
        if candidate in used:
            continue
        if result["states_explored"] >= state_budget:
            result.update(status="budget_exhausted", matched=None)
            return result
        result["states_explored"] += 1
        compatible = True
        for prior, assigned in mapping.items():
            if not _contained(pi.pairs.get((node, prior), Counter()), hi.pairs.get((candidate, assigned), Counter())) or not _contained(
                    pi.pairs.get((prior, node), Counter()), hi.pairs.get((assigned, candidate), Counter())):
                compatible = False
                break
        if not compatible:
            continue
        pending = []
        left_names, right_names = originals_p[node], originals_h[candidate]
        if set(left_names) != set(right_names):
            continue
        for namespace, original in left_names.items():
            renamed = right_names[namespace]
            if (namespace, original) in forward and forward[(namespace, original)] != renamed:
                compatible = False
                break
            if (namespace, renamed) in reverse and reverse[(namespace, renamed)] != original:
                compatible = False
                break
            if (namespace, original) not in forward:
                pending.append((namespace, original, renamed))
        if not compatible:
            continue
        for namespace, original, renamed in pending:
            forward[(namespace, original)] = renamed
            reverse[(namespace, renamed)] = original
        binding_changes[node] = pending
        mapping[node] = candidate
        used.add(candidate)
        if len(mapping) == len(order):
            result.update(status="matched", matched=True, node_mapping=dict(sorted(mapping.items())))
            offsets = Counter()
            for index, edge in enumerate(pattern["edges"]):
                key = (mapping[edge["source"]], mapping[edge["target"]], _edge_label(edge))
                host_index = hi.edge_indices[key][offsets[key]]
                offsets[key] += 1
                result["edge_mapping"].append({"pattern_edge": index, "host_edge": host_index})
            result["lexical_renaming"] = [{"namespace": ns, "pattern": original, "host": renamed}
                                          for (ns, original), renamed in sorted(forward.items())]
            return result
        stack.append(iter(domains[order[len(mapping)]]))
    return result


def _containment(required: Counter, available: Counter) -> float:
    total = sum(required.values())
    return sum(min(count, available.get(key, 0)) for key, count in required.items()) / total if total else 0.0


def rank_candidates(pattern: dict, candidates: list[dict], *, goal: str = "literal_semantic",
                    label_projection: dict | None = None, limit: int = 10, rounds: int = 2,
                    verify_budget: int = 0) -> dict:
    """Safe filters, then feature ranking; optional bounded verification of top-k.

    Each candidate is {id,graph,label_projection?}. A candidate-specific explicit
    projection overrides the common one. WL is a soft ranking signal: extra host
    neighbors can change every WL color, so it is never a subgraph hard filter.
    """
    if type(limit) is not int or limit < 0 or type(verify_budget) is not int or verify_budget < 0:
        raise ValueError("limit and verify_budget must be nonnegative integers")
    ids, rows, rejected = set(), [], []
    for candidate in candidates:
        ident = candidate.get("id")
        if not isinstance(ident, str) or not ident or ident in ids:
            raise ValueError("candidate ids must be nonempty and unique")
        ids.add(ident)
        projection = candidate.get("label_projection", label_projection)
        p, h, pi, hi, domains, filters = _prepare(pattern, candidate["graph"], goal, projection)
        if not filters["possible"]:
            rejected.append({"id": ident, "reason": filters["reason"]})
            continue
        p_features, h_features = role_relation_features(p["graph"]), role_relation_features(h["graph"])
        row = {"id": ident, "filters": filters,
               "scores": {"role_relation_containment": _containment(p_features, h_features),
                          "role_relation_cosine": cosine(p_features, h_features),
                          "wl_cosine": cosine(wl_features(p["graph"], rounds), wl_features(h["graph"], rounds))},
               "verification": {"status": "not_run"}, "_candidate": candidate, "_projection": projection}
        rows.append(row)
    rows.sort(key=lambda row: (-row["scores"]["role_relation_containment"], -row["scores"]["wl_cosine"],
                              -row["scores"]["role_relation_cosine"], row["id"]))
    results = []
    for row in rows[:limit]:
        candidate, projection = row.pop("_candidate"), row.pop("_projection")
        if verify_budget:
            row["verification"] = match_subgraph(pattern, candidate["graph"], goal=goal,
                                                  label_projection=projection, state_budget=verify_budget)
        results.append(row)
    return {"version": VERSION, "goal": goal, "results": results, "filtered_out": rejected,
            "input_candidates": len(candidates), "filter_survivors": len(rows),
            "omitted_by_top_k": max(0, len(rows) - limit), "limit": limit, "verify_budget_per_result": verify_budget,
            "ranking_is_proof": False, "scores_are_probabilities": False, "confidence": None}


def topology_only_control(pattern: dict, host: dict, *, state_budget: int = 10000) -> dict:
    """Explicitly unsafe ablation: discard semantic labels, retain graph wiring."""
    def erase(graph):
        _validate(graph)
        return {"nodes": [{"id": n["id"], "kind": "untyped", "role": "", "qualifiers": {}} for n in graph["nodes"]],
                "edges": [{"source": e["source"], "target": e["target"], "predicate": "untyped", "qualifiers": {}}
                          for e in graph["edges"]]}
    result = match_subgraph(erase(pattern), erase(host), state_budget=state_budget)
    result.update(goal="topology_only_unsafe_control", semantic_use_permitted=False,
                  discarded=["kind", "role", "all_qualifiers_including_scope_polarity_quantifier_modality",
                             "lexical_identity", "edge_predicate"],
                  interpretation="unsafe_ablation_only; topology_is_not_semantic_identity_analogy_or_validity")
    return result
