#!/usr/bin/env python3
"""Experimental, read-only projections and comparisons of assessed Loom claims.

This module is not an extractor, truth oracle, or second knowledge store. Logical
annotations are supplied explicitly; all results are research reports. See README.
Only Python's standard library is required. No benchmark corpus is read here.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path
import re
from typing import Any

VERSION = "structure-experiment/1"
ROLES = {"intent", "constraint", "part", "actor", "resource", "interface", "flow",
         "event", "artifact", "transformation", "check", "output", "decision", "question"}
REGISTRY_PATH = Path(__file__).with_name("operations.json")


def canonical(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def digest(value: Any) -> str:
    return hashlib.sha256(canonical(value).encode("utf-8")).hexdigest()


def cosine(a: Counter, b: Counter) -> float:
    denominator = math.sqrt(sum(v * v for v in a.values()) * sum(v * v for v in b.values()))
    return sum(v * b.get(k, 0) for k, v in a.items()) / denominator if denominator else 0.0


def lexical_features(text: str) -> Counter:
    """Deliberately weak, transparent control: casefolded Unicode word counts."""
    return Counter(re.findall(r"\w+", text.casefold()))


def validate_graph(graph: dict) -> None:
    if not isinstance(graph, dict) or not isinstance(graph.get("nodes"), list) or not isinstance(graph.get("edges"), list):
        raise ValueError("structure requires nodes and edges arrays")
    ids = set()
    for node in graph["nodes"]:
        ident = node.get("id")
        if not isinstance(ident, str) or not ident or ident in ids:
            raise ValueError("node ids must be nonempty and unique")
        ids.add(ident)
        if node.get("role", "") not in ROLES | {""}:
            raise ValueError("role means a Loom universal role; put argument_role in qualifiers")
        if not isinstance(node.get("qualifiers", {}), dict):
            raise ValueError("node qualifiers must be an object")
    for edge in graph["edges"]:
        if edge.get("source") not in ids or edge.get("target") not in ids:
            raise ValueError("edge has an unknown endpoint")
        if not isinstance(edge.get("predicate"), str) or not edge["predicate"]:
            raise ValueError("edge predicate must be nonempty")
        if not isinstance(edge.get("qualifiers", {}), dict):
            raise ValueError("edge qualifiers must be an object")


def node_label(node: dict) -> str:
    return canonical([node.get("kind", ""), node.get("role", ""), node.get("qualifiers", {})])


def edge_label(edge: dict) -> str:
    return canonical([edge["predicate"], edge.get("qualifiers", {})])


def role_relation_features(graph: dict) -> Counter:
    """First-order role/predicate counts; ignores global identity consistency."""
    validate_graph(graph)
    labels = {n["id"]: node_label(n) for n in graph["nodes"]}
    result = Counter(("node", label) for label in labels.values())
    result.update(("edge", labels[e["source"]], edge_label(e), labels[e["target"]]) for e in graph["edges"])
    return result


def wl_features(graph: dict, rounds: int = 2) -> Counter:
    """Directed, edge-labeled 1-WL with round-tagged multiset counts.

    Node IDs/order never enter a feature. Incoming/outgoing, duplicate edges,
    edge roles and qualifiers do. Hash collisions remain theoretically possible.
    """
    validate_graph(graph)
    if not 0 <= rounds <= 8:
        raise ValueError("rounds must lie in [0,8]")
    labels = {n["id"]: digest(node_label(n)) for n in graph["nodes"]}
    incoming, outgoing = defaultdict(list), defaultdict(list)
    for edge in graph["edges"]:
        label = edge_label(edge)
        outgoing[edge["source"]].append((label, edge["target"]))
        incoming[edge["target"]].append((label, edge["source"]))
    features = Counter((0, label) for label in labels.values())
    for step in range(1, rounds + 1):
        labels = {ident: digest([label,
                               sorted(("out", e, labels[other]) for e, other in outgoing[ident]),
                               sorted(("in", e, labels[other]) for e, other in incoming[ident])])
                  for ident, label in labels.items()}
        features.update((step, label) for label in labels.values())
    return features


def align(left: dict, right: dict, budget: int = 10000) -> dict:
    """Bounded exact labeled-graph isomorphism, an experimental reranker only.

    No match at an exhausted search budget means unknown, never disproven.
    This is deliberately outside the production anchored-homomorphism matcher.
    """
    validate_graph(left)
    validate_graph(right)
    if budget < 1:
        raise ValueError("budget must be positive")
    a = {n["id"]: node_label(n) for n in left["nodes"]}
    b = {n["id"]: node_label(n) for n in right["nodes"]}
    result = {"score": 0.0, "status": "different", "mapping": {}, "explored": 0,
              "budget": budget, "interpretation": "structural_match_only"}
    if not a or not b:
        result.update(score=None, status="unrepresented")
        return result
    if Counter(a.values()) != Counter(b.values()) or len(left["edges"]) != len(right["edges"]):
        return result
    ea = Counter((e["source"], e["target"], edge_label(e)) for e in left["edges"])
    eb = Counter((e["source"], e["target"], edge_label(e)) for e in right["edges"])
    def signatures(nodes: dict, edges: Counter) -> dict:
        return {ident: (label,
                        sorted(("out", e, nodes[v], count) for (u, v, e), count in edges.items() if u == ident),
                        sorted(("in", e, nodes[u], count) for (u, v, e), count in edges.items() if v == ident))
                for ident, label in nodes.items()}
    sa, sb = signatures(a, ea), signatures(b, eb)
    choices = {u: sorted(v for v in b if sa[u] == sb[v]) for u in a}
    if any(not values for values in choices.values()):
        return result
    order = sorted(a, key=lambda u: (len(choices[u]), u))
    mapping, used = {}, set()
    exhausted = False
    def search(pos: int) -> bool:
        nonlocal exhausted
        if pos == len(order):
            return True
        u = order[pos]
        for v in choices[u]:
            if v in used:
                continue
            if result["explored"] >= budget:
                exhausted = True
                return False
            result["explored"] += 1
            pairs = [(u, u, v, v)]
            for x, y in mapping.items():
                pairs.extend([(u, x, v, y), (x, u, y, v)])
            if any(Counter({e: c for (p, q, e), c in ea.items() if p == x and q == z}) !=
                   Counter({e: c for (p, q, e), c in eb.items() if p == y and q == w})
                   for x, z, y, w in pairs):
                continue
            mapping[u] = v
            used.add(v)
            if search(pos + 1):
                return True
            used.remove(v)
            del mapping[u]
            if exhausted:
                return False
        return False
    if search(0):
        result.update(score=1.0, status="isomorphic", mapping=dict(sorted(mapping.items())))
    elif exhausted:
        result.update(score=None, status="budget_exhausted")
    return result


def validate_formula(formula: dict, depth: int = 0) -> None:
    if depth > 32 or not isinstance(formula, dict):
        raise ValueError("formula must be an object with depth <=32")
    op = formula.get("op")
    required = {"atom": {"op", "predicate", "args"}, "not": {"op", "arg"},
                "implies": {"op", "left", "right"}, "causes": {"op", "left", "right"},
                "and": {"op", "args"}, "or": {"op", "args"},
                "forall": {"op", "var", "body"}, "exists": {"op", "var", "body"},
                "modal": {"op", "mode", "arg"}}
    if op not in required or set(formula) != required[op]:
        raise ValueError(f"unsupported or malformed formula operation: {op!r}")
    if op == "atom":
        if not isinstance(formula["predicate"], str) or not formula["predicate"]:
            raise ValueError("atom predicate must be a nonempty symbol")
        if not isinstance(formula["args"], list) or not all(isinstance(x, str) and x for x in formula["args"]):
            raise ValueError("atom arguments must be nonempty term symbols")
    elif op in {"not", "modal"}:
        if op == "modal" and (not isinstance(formula["mode"], str) or not formula["mode"]):
            raise ValueError("modal mode must be nonempty")
        validate_formula(formula["arg"], depth + 1)
    elif op in {"implies", "causes"}:
        validate_formula(formula["left"], depth + 1)
        validate_formula(formula["right"], depth + 1)
    elif op in {"and", "or"}:
        if not isinstance(formula["args"], list) or len(formula["args"]) < 2:
            raise ValueError("and/or require at least two formulas")
        for arg in formula["args"]:
            validate_formula(arg, depth + 1)
    else:
        if not isinstance(formula["var"], str) or not formula["var"]:
            raise ValueError("quantifier requires a nonempty bound variable")
        validate_formula(formula["body"], depth + 1)


def formula_graph(logic: list[dict], abstraction: str = "structural") -> dict:
    """Project supplied formulas to an incidence graph; names are graph vertices.

    Structural projection allows consistent renaming of predicate/constant symbols,
    preserving their reuse and argument positions. Semantic projection retains
    names. Bound variables use explicit binder edges, not their spelling.
    """
    if abstraction not in {"structural", "semantic"}:
        raise ValueError("abstraction must be structural or semantic")
    graph = {"nodes": [], "edges": [], "projection": abstraction, "version": VERSION}
    symbols = {}
    def node(kind: str, claim: str, qualifiers: dict | None = None) -> str:
        ident = "n" + str(len(graph["nodes"]))
        graph["nodes"].append({"id": ident, "kind": kind, "role": "",
                               "qualifiers": qualifiers or {}, "claim_ids": [claim]})
        return ident
    def edge(src: str, dst: str, pred: str, claim: str) -> None:
        graph["edges"].append({"source": src, "target": dst, "predicate": pred,
                               "qualifiers": {}, "claim_ids": [claim]})
    def symbol(kind: str, name: str, claim: str) -> str:
        key = (kind, name)
        if key not in symbols:
            symbols[key] = node(kind, claim, {"symbol": name} if abstraction == "semantic" else {})
        else:
            rec = graph["nodes"][int(symbols[key][1:])]
            rec["claim_ids"] = sorted(set(rec["claim_ids"]) | {claim})
        return symbols[key]
    def visit(f: dict, claim: str, env: dict) -> str:
        op = f["op"]
        ident = node("formula", claim, {"operator": op, **({"mode": f["mode"]} if op == "modal" else {})})
        if op == "atom":
            edge(ident, symbol("predicate_symbol", f["predicate"], claim), "predicate", claim)
            for pos, term in enumerate(f["args"]):
                target = env[term] if term in env else symbol("constant", term, claim)
                edge(ident, target, f"argument:{pos}", claim)
        elif op in {"forall", "exists"}:
            variable = node("bound_variable", claim)
            edge(ident, variable, "binds", claim)
            edge(ident, visit(f["body"], claim, {**env, f["var"]: variable}), "body", claim)
        elif op in {"not", "modal"}:
            edge(ident, visit(f["arg"], claim, env), "operand", claim)
        elif op in {"and", "or"}:
            for child in f["args"]:
                edge(ident, visit(child, claim, env), "operand", claim)
        else:
            edge(ident, visit(f["left"], claim, env), "antecedent", claim)
            edge(ident, visit(f["right"], claim, env), "consequent", claim)
        return ident
    for entry in logic:
        validate_formula(entry["formula"])
        claim = entry.get("claim_id", "")
        if not claim:
            raise ValueError("logical annotations must reference a claim_id")
        wrapper = node("claim_projection", claim, deepcopy(entry.get("qualifiers", {})))
        edge(wrapper, visit(entry["formula"], claim, {}), "has_formula", claim)
    return graph


def _graph(record: dict) -> dict:
    if "structure" in record:
        return record["structure"]
    if "logic" in record:
        return formula_graph(record["logic"])
    return {"nodes": [], "edges": []}


def compare(left: dict, right: dict, rounds: int = 2, budget: int = 10000) -> dict:
    a, b = _graph(left), _graph(right)
    result = {"version": VERSION,
            "lexical_cosine": cosine(lexical_features(left.get("text", "")), lexical_features(right.get("text", ""))),
            "role_relation_cosine": cosine(role_relation_features(a), role_relation_features(b)),
            "wl_cosine": cosine(wl_features(a, rounds), wl_features(b, rounds)),
            "alignment": align(a, b, budget), "validity": "not_assessed",
            "coverage": {"left": bool(a["nodes"]), "right": bool(b["nodes"]),
                         "grounding": "supplied_annotation_not_verified_from_source"}}
    if "logic" in left and "logic" in right:
        result["semantic_alignment"] = align(formula_graph(left["logic"], "semantic"),
                                               formula_graph(right["logic"], "semantic"), budget)
    return result


def alpha_key(formula: dict, env: tuple = ()) -> str:
    """Exact formula equality modulo bound-variable spelling only."""
    def walk(f: dict, scope: tuple) -> Any:
        op = f["op"]
        if op == "atom":
            return [op, f["predicate"], [["bound", scope[::-1].index(t)] if t in scope else ["constant", t] for t in f["args"]]]
        if op in {"forall", "exists"}:
            return [op, walk(f["body"], scope + (f["var"],))]
        if op in {"not", "modal"}:
            return [op, f.get("mode"), walk(f["arg"], scope)]
        if op in {"and", "or"}:
            return [op, sorted((walk(x, scope) for x in f["args"]), key=canonical)]
        return [op, walk(f["left"], scope), walk(f["right"], scope)]
    return canonical(walk(formula, env))


def _terms(formula: dict, bound: frozenset = frozenset()) -> set[str]:
    op = formula["op"]
    if op == "atom":
        return set(formula["args"]) - bound
    if op in {"forall", "exists"}:
        return _terms(formula["body"], bound | {formula["var"]})
    if op in {"not", "modal"}:
        return _terms(formula["arg"], bound)
    if op in {"and", "or"}:
        return set().union(*(_terms(x, bound) for x in formula["args"]))
    return _terms(formula["left"], bound) | _terms(formula["right"], bound)


def _substitute(formula: dict, var: str, term: str) -> dict:
    """Capture-avoiding substitution of one quantified variable by a constant."""
    f = deepcopy(formula)
    op = f["op"]
    if op == "atom":
        f["args"] = [term if x == var else x for x in f["args"]]
    elif op in {"forall", "exists"}:
        if f["var"] == var:
            return f
        if f["var"] == term:
            # No accidental capture if a constant has a binder's spelling.
            occupied = _all_symbols(f) | {var, term}
            fresh = "_bound"
            while fresh in occupied:
                fresh += "_"
            f["body"] = _substitute(f["body"], f["var"], fresh)
            f["var"] = fresh
        f["body"] = _substitute(f["body"], var, term)
    elif op in {"not", "modal"}:
        f["arg"] = _substitute(f["arg"], var, term)
    elif op in {"and", "or"}:
        f["args"] = [_substitute(x, var, term) for x in f["args"]]
    else:
        f["left"] = _substitute(f["left"], var, term)
        f["right"] = _substitute(f["right"], var, term)
    return f


def _all_symbols(formula: dict) -> set[str]:
    if formula["op"] == "atom":
        return set(formula["args"])
    if formula["op"] in {"forall", "exists"}:
        return {formula["var"]} | _all_symbols(formula["body"])
    if formula["op"] in {"not", "modal"}:
        return _all_symbols(formula["arg"])
    if formula["op"] in {"and", "or"}:
        return set().union(*(_all_symbols(x) for x in formula["args"]))
    return _all_symbols(formula["left"]) | _all_symbols(formula["right"])


def _deductive(formula: dict) -> bool:
    op = formula["op"]
    if op in {"exists", "modal", "causes", "or"}:
        return False
    if op == "atom":
        return True
    if op == "forall":
        return _deductive(formula["body"])
    if op == "not":
        return _deductive(formula["arg"])
    if op == "and":
        return all(_deductive(x) for x in formula["args"])
    return _deductive(formula["left"]) and _deductive(formula["right"])


def infer(logic: list[dict], max_rounds: int = 3, max_candidates: int = 128) -> dict:
    """Bounded UI/MP/MT/conjunction-elimination proof search over supplied claims.

    Proof validity is conditional on annotations and premises. It is not calibrated
    confidence or source truth. Inference reports cannot be ingested as Claims:
    the production Expected-Property verifier for proof replay is still missing.
    """
    if not 1 <= max_rounds <= 8 or not 1 <= max_candidates <= 1024:
        raise ValueError("proof limits require rounds in [1,8], candidates in [1,1024]")
    known, blocked, candidates = {}, [], []
    ids = set()
    for entry in sorted(logic, key=lambda x: x.get("claim_id", "")):
        ident = entry.get("claim_id", "")
        if not ident or ident in ids:
            raise ValueError("logical claim_ids must be nonempty and unique")
        ids.add(ident)
        formula = entry["formula"]
        validate_formula(formula)
        assessment, qualifiers = entry.get("assessment", {}), entry.get("qualifiers", {})
        evidence = assessment.get("evidence_class")
        basis = assessment.get("basis", {})
        derivation = basis.get("derivation") or {}
        reason = None
        if evidence not in {"observed", "derived", "user"}:
            reason = "ineligible_evidence_class"
        elif assessment.get("status", "active") != "active":
            reason = "inactive_or_contested_premise"
        elif derivation.get("morphism"):
            reason = "transfer_cannot_chain"
        elif evidence == "observed" and not basis.get("support"):
            reason = "missing_observation_support"
        elif evidence == "derived" and not derivation.get("operator"):
            reason = "missing_derivation"
        elif qualifiers.get("defeasible") or qualifiers.get("exception_claim_ids"):
            reason = "defeasible_requires_operator_and_exception_check"
        elif set(qualifiers) - {"argument_role", "scope", "valid_from", "valid_to", "version", "branch", "lang", "defeasible", "exception_claim_ids"}:
            reason = "unhandled_semantic_qualifier"
        elif not _deductive(formula):
            reason = "operation_not_in_trusted_deductive_subset"
        if reason:
            blocked.append({"claim_id": ident, "reason": reason})
            continue
        scope = canonical({k: v for k, v in qualifiers.items() if k not in {"argument_role"}})
        key = (scope, alpha_key(formula))
        known.setdefault(key, {"formula": deepcopy(formula), "premise_claim_ids": [ident],
                               "proof": [], "scope": scope})
    original = set(known)
    initial = list(known.items())
    # Direct explicit contradictions do not license explosion or arbitrary choice.
    conflicting = set()
    for key, item in initial:
        f = item["formula"]
        opposite = f["arg"] if f["op"] == "not" else {"op": "not", "arg": f}
        other = (item["scope"], alpha_key(opposite))
        if other in known:
            conflicting.update((key, other))
    for key in conflicting:
        for ident in known[key]["premise_claim_ids"]:
            blocked.append({"claim_id": ident, "reason": "explicit_contradiction"})
        del known[key]
    constants_by_scope = defaultdict(set)
    for (scope, _), item in known.items():
        constants_by_scope[scope].update(_terms(item["formula"]))
    saturated, limited = False, False
    for step in range(1, max_rounds + 1):
        additions = {}
        def add(formula: dict, parents: list[dict], rule: str, substitution: dict | None = None) -> None:
            nonlocal limited
            scope = parents[0]["scope"]
            key = (scope, alpha_key(formula))
            if key in known or key in additions or key in conflicting:
                return
            if len(candidates) + len(additions) >= max_candidates:
                limited = True
                return
            premises = sorted(set().union(*(set(p["premise_claim_ids"]) for p in parents)))
            proof = {canonical(s): s for p in parents for s in p["proof"]}
            proof_step = {"rule_id": rule, "inputs": [deepcopy(p["formula"]) for p in parents],
                          "output": deepcopy(formula), "substitution": substitution or {}}
            proof[canonical(proof_step)] = proof_step
            additions[key] = {"formula": deepcopy(formula), "premise_claim_ids": premises,
                              "rule_id": rule, "proof": list(proof.values()), "scope": scope,
                              "proposed_evidence_class": "inferred", "origin": "system",
                              "confidence": None, "round": step,
                              "verification": "conditional_on_supplied_logical_annotations",
                              "expected_property_proposal": {"predicate": "proof_replay",
                                   "implemented_in_core": False,
                                   "confirm_if": "all source premises, scopes and listed rule steps verify",
                                   "refute_if": "a premise, scope, annotation or proof step is invalid"},
                              "persistable_claim": False}
        for key in sorted(known):
            item = known[key]
            f, scope = item["formula"], item["scope"]
            if f["op"] == "forall":
                for term in sorted(constants_by_scope[scope]):
                    add(_substitute(f["body"], f["var"], term), [item], "universal_instantiation", {f["var"]: term})
            elif f["op"] == "and":
                for child in f["args"]:
                    add(child, [item], "conjunction_elimination")
            elif f["op"] == "implies":
                antecedent = known.get((scope, alpha_key(f["left"])))
                if antecedent:
                    add(f["right"], [item, antecedent], "modus_ponens")
                negative = known.get((scope, alpha_key({"op": "not", "arg": f["right"]})))
                if negative:
                    add({"op": "not", "arg": f["left"]}, [item, negative], "modus_tollens")
        if not additions:
            saturated = not limited
            break
        for key in sorted(additions):
            known[key] = additions[key]
            if key not in original:
                candidates.append(additions[key])
        if limited:
            break
    # Mark any newly derived contradictions as contested research results.
    for item in candidates:
        opposite = item["formula"]["arg"] if item["formula"]["op"] == "not" else {"op": "not", "arg": item["formula"]}
        item["status"] = "contested" if (item["scope"], alpha_key(opposite)) in known else "candidate"
    return {"version": VERSION, "candidates": candidates,
            "blocked": sorted(blocked, key=lambda x: (x["claim_id"], x["reason"])),
            "coverage": {"input_claims": len(logic), "eligible_claims": len(original) - len(conflicting),
                         "trusted_rules": ["universal_instantiation", "modus_ponens", "modus_tollens", "conjunction_elimination"],
                         "saturated_within_limits": saturated, "candidate_limit_reached": limited,
                         "no_proof_means": "unknown_or_unsupported_by_this_bounded_subset",
                         "source_grounding_verified": False, "calibrated": False}}


def operation_registry() -> dict:
    """Data-defined family inventory; unknown operations remain unrepresented."""
    return json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))


def coverage_report(graph: dict) -> dict:
    """Describe modeled operation names without pretending arbitrary graphs execute."""
    validate_graph(graph)
    registry = {item["id"]: item["implementation"] for item in operation_registry()["operations"]}
    ast = {"atom", "not", "implies", "causes", "and", "or", "forall", "exists", "modal"}
    seen = Counter()
    unknown = Counter()
    for node in graph["nodes"]:
        qualifiers = node.get("qualifiers", {})
        op = qualifiers.get("operator", qualifiers.get("operation"))
        if op is not None:
            (seen if op in registry or op in ast else unknown)[str(op)] += 1
    return {"represented_operations": dict(sorted(seen.items())),
            "unrepresented_operations": dict(sorted(unknown.items())),
            "no_operation_annotation_nodes": sum(not any(k in n.get("qualifiers", {}) for k in ("operator", "operation")) for n in graph["nodes"]),
            "interpretation": "graph_representability_does_not_imply_executable_semantics"}


def pattern_profile(graph: dict, occurrences: list[dict]) -> dict:
    """Descriptive recurrence breadth and retained detail, deliberately independent.

    Occurrence IDs deduplicate repeats. Counts do not estimate universal validity.
    No scalar inverse-generalization score or learned threshold is fabricated.
    """
    validate_graph(graph)
    unique = {o["id"]: o for o in occurrences}
    return {"universality_observation": {"distinct_occurrences": len(unique),
            "distinct_domains": len({o["domain"] for o in unique.values() if o.get("domain")}),
            "distinct_contexts": len({o["context"] for o in unique.values() if o.get("context")}),
            "interpretation": "observed_coverage_only_not_universal_validity"},
            "specificity_description": {"nodes": len(graph["nodes"]), "relations": len(graph["edges"]),
            "qualified_fields": sum(len(x.get("qualifiers", {})) for x in graph["nodes"] + graph["edges"])}}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["compare", "infer", "graph", "registry"])
    parser.add_argument("input", nargs="?", help="JSON input file; compare uses {left,right}; infer/graph use {logic}")
    args = parser.parse_args()
    data = json.loads(Path(args.input).read_text(encoding="utf-8")) if args.input else {}
    if args.command == "compare":
        output = compare(data["left"], data["right"])
    elif args.command == "infer":
        output = infer(data["logic"])
    elif args.command == "graph":
        output = formula_graph(data["logic"])
    else:
        output = operation_registry()
    print(json.dumps(output, ensure_ascii=False, sort_keys=True, indent=2))


if __name__ == "__main__":
    main()
