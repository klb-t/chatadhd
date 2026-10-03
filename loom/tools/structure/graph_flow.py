#!/usr/bin/env python3
"""Compose source-linked core views, safe retrieval, exact witnesses and motifs.

Records are explicit sets of canonical Claim IDs, not new persisted knowledge.
The caller declares scope/grouping; source independence is never inferred from
the number of projected records. No result from this module promotes a Claim.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path

try:
    from .core_projection import project_core
    from .graph_search import rank_candidates
    from .graph_patterns import discover_patterns
except ImportError:
    from core_projection import project_core
    from graph_search import rank_candidates
    from graph_patterns import discover_patterns

VERSION = "core-graph-flow/1"


def _canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False)


def _names(graph):
    """Explicitly permit reference renaming; relation vocabulary stays fixed.

    Shared reference vertices retain equality and injectivity. Literal Claim
    values, scope membership, assessment and every other qualifier remain exact.
    The matcher's witness still reports each original name correspondence.
    """
    result = {}
    for node in graph["nodes"]:
        identity = node.get("lexical_identity")
        if identity and identity["namespace"] != "predicate":
            fields = {"lexical_identity": "<" + identity["namespace"] + ">"}
            if "symbol" in node.get("qualifiers", {}):
                fields["symbol"] = "<" + identity["namespace"] + ">"
            result[node["id"]] = fields
    return result


def run_flow(snapshot: dict, record_specs: list[dict], query_ids: list[str], *,
             mode: str = "structural", top_k: int = 5, state_budget: int = 10000,
             max_records: int = 32, max_graph_nodes: int = 2048,
             motif_parameters: dict | None = None) -> dict:
    if mode not in {"semantic", "structural"}:
        raise ValueError("flow requires a restriction-preserving core projection")
    for name, value, minimum in (("top_k", top_k, 0), ("state_budget", state_budget, 0),
                                 ("max_records", max_records, 1), ("max_graph_nodes", max_graph_nodes, 1)):
        if type(value) is not int or value < minimum:
            raise ValueError(name + " has an invalid bound")
    if not isinstance(record_specs, list) or not isinstance(query_ids, list):
        raise ValueError("record_specs and query_ids must be arrays")
    ids = [record.get("id") for record in record_specs]
    if any(not isinstance(ident, str) or not ident for ident in ids) or len(set(ids)) != len(ids):
        raise ValueError("record IDs must be unique nonempty strings")
    if any(not isinstance(ident, str) or ident not in ids for ident in query_ids) or len(set(query_ids)) != len(query_ids):
        raise ValueError("query IDs must be unique declared record IDs")
    selected_specs = sorted(record_specs, key=lambda record: record["id"])[:max_records]
    selected_ids = {record["id"] for record in selected_specs}
    omissions = [{"id": ident, "reason": "record_budget"} for ident in sorted(set(ids) - selected_ids)]
    records, reports, graphs, projections = [], {}, {}, {}
    for spec in selected_specs:
        claims = spec.get("claim_ids")
        if not isinstance(claims, list) or not claims:
            raise ValueError("each record requires a nonempty list of canonical Claim IDs")
        view = project_core(snapshot, mode=mode, claim_ids=claims)
        graph = view["structure"]
        # Old projection checkpoints used source_claim_ids; this is a metadata
        # spelling adapter only, with both spellings checked if supplied.
        for item in graph["nodes"] + graph["edges"]:
            if "source_claim_ids" in item:
                trace = item.pop("source_claim_ids")
                if "claim_ids" in item and item["claim_ids"] != trace:
                    raise ValueError("conflicting source Claim trace fields")
                item["claim_ids"] = trace
        ident = spec["id"]
        reports[ident] = {key: deepcopy(view[key]) for key in
                          ("mode", "source_claim_ids", "claim_node_ids", "reference_map", "dropped_attributes", "unknowns", "gates", "projection_scope")}
        reports[ident].update(node_count=len(graph["nodes"]), edge_count=len(graph["edges"]),
                              scope=deepcopy(spec.get("scope")), scope_verified=False)
        if len(graph["nodes"]) > max_graph_nodes:
            omissions.append({"id": ident, "reason": "graph_node_budget", "nodes": len(graph["nodes"])})
            continue
        graphs[ident] = graph
        projections[ident] = _names(graph) if mode == "structural" else None
        records.append({"id": ident, "structure": graph,
                        "source_group": spec.get("source_group"), "domain": spec.get("domain")})
    goal = "structural_analogy" if mode == "structural" else "literal_semantic"
    retrieval = []
    for ident in query_ids:
        if ident not in graphs:
            retrieval.append({"query_id": ident, "status": "not_run_budget", "result": None})
            continue
        candidates = []
        for other in sorted(graphs):
            if other == ident:
                continue
            candidate = {"id": other, "graph": graphs[other]}
            if mode == "structural":
                candidate["label_projection"] = {"pattern": projections[ident], "host": projections[other]}
            candidates.append(candidate)
        result = rank_candidates(graphs[ident], candidates, goal=goal, limit=top_k,
                                 verify_budget=state_budget)
        retrieval.append({"query_id": ident, "status": "searched", "result": result})
    # Motifs use the supplied exact-label view. Renaming for discovery is a
    # distinct experiment: silently erasing names here would lose bindings.
    motifs = discover_patterns(records, **motif_parameters) if motif_parameters is not None else None
    return {"version": VERSION, "run_id": snapshot["run_id"],
            "source_snapshot_sha256": hashlib.sha256(_canonical(snapshot).encode()).hexdigest(),
            "parameters": {"mode": mode, "goal": goal, "top_k": top_k, "state_budget_per_pair": state_budget,
                           "max_records": max_records, "max_graph_nodes": max_graph_nodes},
            "record_specs": deepcopy(record_specs), "projections": reports, "retrieval": retrieval,
            "motifs": motifs, "omissions": omissions,
            "coverage": {"declared_records": len(record_specs), "searched_records": len(records),
                         "declared_queries": len(query_ids),
                         "executed_queries": sum(row["status"] == "searched" for row in retrieval)},
            "semantics": {"authoritative_store": "source Claim + Assessment",
                          "projection_persistence": "recomputable_view_only",
                          "ranking_is_proof": False, "match_is_inference": False,
                          "source_scope_verified": False, "semantic_accuracy": None,
                          "graph_mutations_applied": 0, "claims_promoted": 0,
                          "motif_renaming": "none; exact labels of explicit projected view"}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("spec", type=Path, help="{records,query_ids,parameters?}")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = json.loads(args.snapshot.read_text(encoding="utf-8"))
    spec = json.loads(args.spec.read_text(encoding="utf-8"))
    result = run_flow(source, spec["records"], spec["query_ids"], **spec.get("parameters", {}))
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")


if __name__ == "__main__":
    main()
