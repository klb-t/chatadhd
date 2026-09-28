#!/usr/bin/env python3
"""Independent graph-native oracle and task-conditioned evaluation.

The oracle exhaustively enumerates small node injections and edge-multiset
capacity. It imports no matching implementation. Source/fixture hashes freeze
before method outcomes; identity, containment and analogy are separate tasks.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from copy import deepcopy
import hashlib
import importlib.util
import itertools
import json
from pathlib import Path
import statistics
import sys
import time


DEFAULT_FIXTURE = Path(__file__).resolve().parents[2] / "tests/fixtures/eval/independent_graph_native_v1/cases.json"


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def digest(value):
    return hashlib.sha256(canonical(value).encode()).hexdigest()


def read_fixture(path=DEFAULT_FIXTURE):
    fixture = json.loads(Path(path).read_text())
    if fixture["schema"] != "loom.independent_graph_native/1":
        raise ValueError("wrong fixture schema")
    if fixture["frozen_sha256"] != digest({k: v for k, v in fixture.items() if k != "frozen_sha256"}):
        raise ValueError("frozen fixture changed")
    ids = set()
    for case in fixture["cases"]:
        if case["id"] in ids or case["split"] not in {"development", "validation"}:
            raise ValueError("duplicate case or invalid split")
        ids.add(case["id"])
        check_graph(case["pattern"])
        for candidate in case["candidates"]:
            check_graph(candidate["graph"])
    return fixture


def check_graph(graph):
    if not isinstance(graph, dict) or not isinstance(graph.get("nodes"), list) or not isinstance(graph.get("edges"), list):
        raise ValueError("nodes and edges required")
    ids = [n["id"] for n in graph["nodes"]]
    if len(ids) != len(set(ids)) or any(not isinstance(i, str) or not i for i in ids):
        raise ValueError("node IDs must be unique nonempty strings")
    allowed_nodes = {"id", "kind", "role", "label", "qualifiers", "lexical_identity", "claim_ids", "provenance"}
    allowed_edges = {"id", "source", "target", "predicate", "qualifiers", "claim_ids", "provenance"}
    for node in graph["nodes"]:
        if set(node) - allowed_nodes:
            raise ValueError("unknown semantic node field")
        if not isinstance(node.get("kind"), str) or not node["kind"]:
            raise ValueError("typed node kind required")
        if not isinstance(node.get("qualifiers", {}), dict):
            raise ValueError("qualifiers must be an object")
    for edge in graph["edges"]:
        if set(edge) - allowed_edges:
            raise ValueError("unknown semantic edge field")
        if edge["source"] not in ids or edge["target"] not in ids:
            raise ValueError("edge endpoint missing")
        if not isinstance(edge.get("predicate"), str) or not edge["predicate"] or not isinstance(edge.get("qualifiers", {}), dict):
            raise ValueError("typed edge predicate and qualifier object required")
    canonical(graph)  # Reject NaN and non-JSON semantic attributes.


def projected_node(node, replacements=None):
    result = deepcopy(node)
    if replacements is not None:
        if set(replacements) - {"label", "symbol", "lexical_identity"}:
            raise ValueError("unknown lexical projection channel")
        if "label" in replacements:
            if "label" not in result:
                raise ValueError("cannot project an absent lexical channel")
            result["label"] = replacements["label"]
        if "symbol" in replacements:
            if "symbol" not in result.get("qualifiers", {}):
                raise ValueError("cannot project an absent symbol")
            result["qualifiers"]["symbol"] = replacements["symbol"]
        if "lexical_identity" in replacements:
            if "lexical_identity" not in result:
                raise ValueError("cannot project an absent identity")
            result["lexical_identity"]["value"] = replacements["lexical_identity"]
    return result


def node_signature(node):
    # Presence matters for literal lexical fields. Only a wholly absent
    # qualifiers object and an empty object are normalized as equivalent.
    semantic = {key: node[key] for key in ("kind", "role", "label", "lexical_identity") if key in node}
    semantic["qualifiers"] = node.get("qualifiers", {})
    return canonical(semantic)


def edge_signature(edge, mapping=None):
    a, b = edge["source"], edge["target"]
    if mapping is not None:
        a, b = mapping[a], mapping[b]
    return a, b, edge["predicate"], canonical(edge.get("qualifiers", {}))


def lexical_channels(node):
    values = {}
    kind = node.get("kind", "")
    if "label" in node:
        values["label:" + kind] = canonical(node["label"])
    if "symbol" in node.get("qualifiers", {}):
        values["symbol:" + kind] = canonical(node["qualifiers"]["symbol"])
    if "lexical_identity" in node:
        identity = node["lexical_identity"]
        values["identity:" + identity["namespace"]] = canonical(identity["value"])
    return values


def binding_preserved(pattern, host, mapping):
    """Consistent injective lexical renaming; neither collapse nor invention."""
    pn, hn = {n["id"]: n for n in pattern["nodes"]}, {n["id"]: n for n in host["nodes"]}
    forward, reverse = {}, {}
    for p, h in mapping.items():
        a, b = lexical_channels(pn[p]), lexical_channels(hn[h])
        if set(a) != set(b):
            return False
        for channel in a:
            fkey, rkey = (channel, a[channel]), (channel, b[channel])
            if fkey in forward and forward[fkey] != b[channel]:
                return False
            if rkey in reverse and reverse[rkey] != a[channel]:
                return False
            forward[fkey], reverse[rkey] = b[channel], a[channel]
    return True


def witness_valid(pattern, host, mapping, *, goal, projection=None, edge_mapping=None):
    if goal not in {"semantic_identity", "template_containment", "structural_analogy"}:
        raise ValueError("unknown evaluation goal")
    check_graph(pattern)
    check_graph(host)
    pn, hn = {n["id"]: n for n in pattern["nodes"]}, {n["id"]: n for n in host["nodes"]}
    if set(mapping) != set(pn) or len(set(mapping.values())) != len(mapping) or not set(mapping.values()) <= set(hn):
        return False
    if goal == "semantic_identity" and (len(pn) != len(hn) or len(pattern["edges"]) != len(host["edges"])):
        return False
    analogy = goal == "structural_analogy"
    projection = projection or {"pattern": {}, "host": {}}
    for p, h in mapping.items():
        a = projected_node(pn[p], projection["pattern"].get(p)) if analogy else pn[p]
        b = projected_node(hn[h], projection["host"].get(h)) if analogy else hn[h]
        if node_signature(a) != node_signature(b):
            return False
    if analogy and not binding_preserved(pattern, host, mapping):
        return False
    required = Counter(edge_signature(e, mapping) for e in pattern["edges"])
    available = Counter(edge_signature(e) for e in host["edges"])
    if any(available[key] < count for key, count in required.items()):
        return False
    if edge_mapping is not None:
        try:
            pindices = [entry["pattern_edge"] for entry in edge_mapping]
            hindices = [entry["host_edge"] for entry in edge_mapping]
            if any(type(i) is not int for i in pindices + hindices):
                return False
            if sorted(pindices) != list(range(len(pattern["edges"]))) or len(hindices) != len(set(hindices)):
                return False
            for entry in edge_mapping:
                if entry["host_edge"] < 0 or entry["host_edge"] >= len(host["edges"]):
                    return False
                if edge_signature(pattern["edges"][entry["pattern_edge"]], mapping) != edge_signature(host["edges"][entry["host_edge"]]):
                    return False
        except (KeyError, IndexError, TypeError):
            return False
    return True


def exhaustive_oracle(pattern, host, *, goal, projection=None):
    """Finite exhaustive truth on small fixtures, independent of method scores."""
    check_graph(pattern)
    check_graph(host)
    if len(pattern["nodes"]) > 6 or len(host["nodes"]) > 9:
        raise ValueError("oracle fixture bounds exceeded; do not approximate truth")
    pids = sorted(n["id"] for n in pattern["nodes"])
    hids = sorted(n["id"] for n in host["nodes"])
    examined = 0
    for permutation in itertools.permutations(hids, len(pids)):
        examined += 1
        mapping = dict(zip(pids, permutation))
        if witness_valid(pattern, host, mapping, goal=goal, projection=projection):
            return {"present": True, "witness": mapping, "mappings_examined": examined}
    return {"present": False, "witness": None, "mappings_examined": examined}


def oracle_labels(fixture):
    cases = {c["id"]: c for c in fixture["cases"]}
    rows = []
    for query in fixture["queries"]:
        case = cases[query["case_id"]]
        for candidate in case["candidates"]:
            projection = {"pattern": case["label_projection"], "host": candidate["label_projection"]}
            outcome = exhaustive_oracle(case["pattern"], candidate["graph"], goal=query["goal"], projection=projection)
            # Authored construction checks keep an accidental positive/negative
            # from silently changing the intended challenge category.
            expected = candidate["kind"] == "exact_clean" or (
                query["goal"] != "semantic_identity" and candidate["kind"] == "literal_embedded") or (
                query["goal"] == "structural_analogy" and candidate["kind"] == "cross_domain_embedded")
            if outcome["present"] != expected:
                raise ValueError("authored label and exhaustive oracle disagree: " + query["id"] + "/" + candidate["id"])
            rows.append({"query_id": query["id"], "candidate_id": candidate["id"], "split": query["split"],
                "goal": query["goal"], "dimension": case["dimension"], "candidate_kind": candidate["kind"], **outcome})
    return {"schema": "loom.independent_graph_oracle/1", "fixture_sha256": fixture["frozen_sha256"],
            "oracle_source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "rows": rows}


def ranking_metrics(relevant, ranked, survivors, ks=(1, 3)):
    relevant, survivors = set(relevant), set(survivors)
    if len(ranked) != len(set(ranked)):
        raise ValueError("duplicate ranked candidate")
    hits = [int(item in relevant) for item in ranked]
    precision_sum = sum(sum(hits[:i + 1]) / (i + 1) for i, hit in enumerate(hits) if hit)
    first = next((i + 1 for i, hit in enumerate(hits) if hit), None)
    return {"relevant_total": len(relevant), "filter_survivors": len(survivors),
        "filter_recall": len(relevant & survivors) / len(relevant) if relevant else None,
        "average_precision": precision_sum / len(relevant) if relevant else None,
        "reciprocal_rank": 1 / first if first else 0.0,
        "top_k": {str(k): {"recall": sum(hits[:k]) / len(relevant) if relevant else None,
            "precision": sum(hits[:k]) / min(k, len(ranked)) if ranked else None,
            "returned": min(k, len(ranked))} for k in ks}}


def load_method(path):
    path = Path(path).resolve()
    sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location("independent_graph_method", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_method(fixture, implementation):
    method = load_method(implementation)
    oracle = oracle_labels(fixture)
    truth = {(r["query_id"], r["candidate_id"]): r for r in oracle["rows"]}
    cases = {c["id"]: c for c in fixture["cases"]}
    direct, rankings, probes = [], [], []
    for query in fixture["queries"]:
        case = cases[query["case_id"]]
        pattern, goal = case["pattern"], query["goal"]
        method_goal = "structural_analogy" if goal == "structural_analogy" else "literal_semantic"
        supplied, cardinality_rejected = [], []
        for candidate in case["candidates"]:
            host = candidate["graph"]
            projection = {"pattern": case["label_projection"], "host": candidate["label_projection"]}
            # Full graph identity is intentionally a different query task from
            # the method's non-induced containment contract.
            cardinality_ok = goal != "semantic_identity" or (
                len(pattern["nodes"]) == len(host["nodes"]) and len(pattern["edges"]) == len(host["edges"]))
            if cardinality_ok:
                supplied.append({"id": candidate["id"], "graph": host,
                    "label_projection": projection if method_goal == "structural_analogy" else None})
            else:
                cardinality_rejected.append(candidate["id"])
            for budget in fixture["protocol"]["budgets"]:
                started = time.perf_counter_ns()
                if not cardinality_ok:
                    outcome = {"status": "different", "matched": False, "states_explored": 0,
                               "adapter": "full_graph_cardinality_guard"}
                else:
                    outcome = method.match_subgraph(pattern, host, goal=method_goal,
                        label_projection=projection if method_goal == "structural_analogy" else None,
                        state_budget=budget)
                elapsed = (time.perf_counter_ns() - started) / 1_000_000
                expected = truth[(query["id"], candidate["id"])]["present"]
                status = outcome["status"]
                witness_ok = (witness_valid(pattern, host, outcome.get("node_mapping", {}), goal=goal,
                    projection=projection, edge_mapping=outcome.get("edge_mapping")) if status == "matched" else None)
                direct.append({"query_id": query["id"], "candidate_id": candidate["id"], "split": query["split"],
                    "dimension": case["dimension"], "goal": goal, "budget": budget, "oracle_present": expected,
                    "status": status, "witness_valid": witness_ok, "false_match": status == "matched" and not expected,
                    "false_different": status == "different" and expected, "latency_ms": elapsed,
                    "states_explored": outcome.get("states_explored"), "outcome": outcome})
        latencies, calls = [], []
        for _ in range(3):
            start = time.perf_counter_ns()
            ranked = method.rank_candidates(pattern, supplied, goal=method_goal,
                limit=len(supplied), verify_budget=0)
            latencies.append((time.perf_counter_ns() - start) / 1_000_000)
            calls.append(ranked)
        ranked_ids = [r["id"] for r in ranked["results"]]
        rejected = {r["id"] for r in ranked["filtered_out"]}
        survivors = {c["id"] for c in supplied} - rejected
        relevant = {c["id"] for c in case["candidates"] if truth[(query["id"], c["id"])]["present"]}
        rankings.append({"query_id": query["id"], "split": query["split"], "dimension": case["dimension"], "goal": goal,
            "metrics": ranking_metrics(relevant, ranked_ids, survivors, query["top_k"]),
            "ranked_ids": ranked_ids, "cardinality_rejected": cardinality_rejected, "filtered_out": ranked["filtered_out"],
            "filter_false_negatives": sorted(relevant - survivors), "latency_ms": latencies,
            "median_latency_ms": statistics.median(latencies), "deterministic": all(canonical(c) == canonical(calls[0]) for c in calls),
            "result": ranked})
    pattern = fixture["cases"][0]["pattern"]
    for probe in fixture["abstention_probes"]:
        try:
            outcome = method.match_subgraph(pattern, probe["input"])
            acceptable = outcome["status"] == "unrepresented"
            probes.append({"id": probe["id"], "outcome": outcome, "acceptable": acceptable})
        except (TypeError, ValueError) as error:
            probes.append({"id": probe["id"], "outcome": "rejected", "reason": str(error), "acceptable": True})
    summaries = {}
    for split in ("development", "validation"):
        summaries[split] = {}
        for goal in ("semantic_identity", "template_containment", "structural_analogy"):
            d = [r for r in direct if r["split"] == split and r["goal"] == goal and r["budget"] == max(fixture["protocol"]["budgets"])]
            r = [r for r in rankings if r["split"] == split and r["goal"] == goal]
            summaries[split][goal] = {"comparisons": len(d), "oracle_positives": sum(x["oracle_present"] for x in d),
                "statuses": dict(Counter(x["status"] for x in d)), "false_matches": sum(x["false_match"] for x in d),
                "false_different": sum(x["false_different"] for x in d),
                "invalid_witnesses": sum(x["witness_valid"] is False for x in d),
                "macro_filter_recall": statistics.mean(x["metrics"]["filter_recall"] for x in r),
                "mean_average_precision": statistics.mean(x["metrics"]["average_precision"] for x in r),
                "mean_reciprocal_rank": statistics.mean(x["metrics"]["reciprocal_rank"] for x in r),
                "mean_recall_at_1": statistics.mean(x["metrics"]["top_k"]["1"]["recall"] for x in r),
                "mean_recall_at_3": statistics.mean(x["metrics"]["top_k"]["3"]["recall"] for x in r),
                "median_ranking_latency_ms": statistics.median(x["median_latency_ms"] for x in r)}
    low = [r for r in direct if r["budget"] == min(fixture["protocol"]["budgets"])]
    return {"schema": "loom.independent_graph_report/1", "fixture_sha256": fixture["frozen_sha256"],
        "implementation_sha256": hashlib.sha256(Path(implementation).read_bytes()).hexdigest(),
        "oracle_source_sha256": oracle["oracle_source_sha256"], "summaries": summaries, "direct_results": direct,
        "ranking_results": rankings, "abstention_probes": probes,
        "low_budget": {"budget": min(fixture["protocol"]["budgets"]), "statuses": dict(Counter(r["status"] for r in low)),
            "false_matches": sum(r["false_match"] for r in low), "false_different": sum(r["false_different"] for r in low),
            "budget_violations": sum(r["states_explored"] > r["budget"] for r in low if isinstance(r["states_explored"], int))},
        "limitations": ["Curated small graphs; the exhaustive oracle is bounded to six pattern and nine host nodes.",
            "Three repeated local timing calls per query are descriptive microbenchmarks, not deployment latency guarantees.",
            "Analogy requires explicitly supplied lexical abstraction; semantic interpretation and abstraction discovery are not measured.",
            "Semantic identity uses a declared full-graph cardinality adapter; the matcher itself promises non-induced containment.",
            "Graph witnesses verify represented relations, not external truth, source authority or valid deductions."]}


def projected_claim_audit(export, projection, claim):
    """Reconstruct selected native semantics from actual graph incidence.

    The copy under projection.source is deliberately not consulted here.
    This checks graph fields/edges against the independent source record.
    """
    graph = projection["structure"]
    nodes = {n["id"]: n for n in graph["nodes"]}
    roots = [n for n in nodes.values() if n.get("lexical_identity") == {"namespace": "claim", "value": claim["id"]}]
    if len(roots) != 1:
        return {"claim_id": claim["id"], "restriction_graph_retained": False, "reason": "no_unique_claim_identity"}
    root = roots[0]
    outgoing = defaultdict(list)
    for edge in graph["edges"]:
        outgoing[edge["source"]].append(edge)

    def targets(node_id, predicate):
        return [nodes[e["target"]] for e in outgoing[node_id] if e["predicate"] == predicate]

    def identities(node_id, predicate):
        return [n.get("lexical_identity") for n in targets(node_id, predicate)]

    endpoint_checks = {}
    for field, namespace in (("subject", "entity"), ("predicate", "predicate"), ("object", "entity")):
        if claim.get(field):
            endpoint_checks[field] = identities(root["id"], field) == [{"namespace": namespace, "value": claim[field]}]
    scope = claim["qualifiers"].get("scope")
    scope_ok = identities(root["id"], "qualified_by_scope") == ([{"namespace": "scope", "value": scope}] if scope else [])
    scope_targets = targets(root["id"], "qualified_by_scope")
    if scope:
        scope_ok = scope_ok and len(scope_targets) == 1 and identities(scope_targets[0]["id"], "scope_refers_to") == [{"namespace": "entity", "value": scope}]
    expected_qualifiers = deepcopy(claim["qualifiers"])
    expected_qualifiers.pop("scope", None)
    expected_qualifiers["scope_presence"] = "declared" if scope else "explicit_unscoped"
    expected_assessment = {k: v for k, v in claim["assessment"].items()
                           if k not in {"basis", "alternatives", "premises", "counter", "consequences"}}
    qualifiers_ok = root.get("qualifiers", {}).get("claim_qualifiers") == expected_qualifiers
    assessment_ok = root.get("qualifiers", {}).get("assessment_attributes") == expected_assessment
    support_nodes = targets(root["id"], "supported_by")
    observation_ids, source_ids = set(), set()
    for support in support_nodes:
        observation_ids.update(x["value"] for x in identities(support["id"], "observation") if x and x["namespace"] == "observation")
        source_ids.update(x["value"] for x in identities(support["id"], "source_locator") if x and x["namespace"] == "source")
    supports = claim["assessment"]["basis"]["support"]
    expected_counts = {"entries": len(supports), "unique_observations": len({s["observation"] for s in supports}),
                       "unique_sources": len({s["locator"]["source"] for s in supports})}
    counts = {"entries": len(support_nodes), "unique_observations": len(observation_ids), "unique_sources": len(source_ids)}
    # Equal cardinalities alone do not establish source identity or correctly
    # paired evidence. Check every support incidence against its source ordinal.
    ordinals = [n.get("qualifiers", {}).get("ordinal") for n in support_nodes]
    ordinals_ok = all(type(i) is int for i in ordinals) and sorted(ordinals) == list(range(len(supports)))
    support_links_ok = ordinals_ok
    semantic_quotes = ordinals_ok if projection["mode"] == "semantic" else None
    if ordinals_ok:
        for node in support_nodes:
            support = supports[node["qualifiers"]["ordinal"]]
            support_links_ok = support_links_ok and identities(node["id"], "observation") == [{"namespace": "observation", "value": support["observation"]}]
            support_links_ok = support_links_ok and identities(node["id"], "source_locator") == [{"namespace": "source", "value": support["locator"]["source"]}]
            if semantic_quotes is not None:
                semantic_quotes = semantic_quotes and node.get("qualifiers", {}).get("attributes", {}).get("quote") == support["quote"]
    signature = {"endpoints": {field: identities(root["id"], field) for field in endpoint_checks},
        "scope": identities(root["id"], "qualified_by_scope"), "qualifiers": root.get("qualifiers", {}).get("claim_qualifiers"),
        "assessment": root.get("qualifiers", {}).get("assessment_attributes")}
    return {"claim_id": claim["id"], "restriction_graph_retained": all(endpoint_checks.values()) and scope_ok and qualifiers_ok and assessment_ok,
        "endpoint_checks": endpoint_checks, "scope_membership_retained": scope_ok, "qualifiers_retained": qualifiers_ok,
        "assessment_retained": assessment_ok, "support_counts": counts, "expected_support_counts": expected_counts,
        "support_identity_retained": counts == expected_counts and support_links_ok,
        "support_links_retained": support_links_ok, "semantic_quotes_retained": semantic_quotes,
        "restriction_signature": signature}


def run_core(fixture_path, implementation):
    fixture = json.loads(Path(fixture_path).read_text())
    if fixture["frozen_sha256"] != digest({k: v for k, v in fixture.items() if k != "frozen_sha256"}):
        raise ValueError("frozen native Claim fixture changed")
    sources = {s["id"]: s for s in fixture["sources"]}
    source_checks = []
    for source in sources.values():
        if hashlib.sha256(source["raw_utf8"].encode()).hexdigest() != source["sha256"]:
            raise ValueError("source bytes changed")
    method = load_method(implementation)
    results = []
    for case in fixture["cases"]:
        gold_export = deepcopy(case["export"])
        for span in case["expected"]["quote_spans"]:
            data = sources[span["source"]]["raw_utf8"].encode()
            actual = data[span["byte_start"]:span["byte_start"] + span["byte_len"]].decode()
            source_checks.append({"case_id": case["id"], "observation": span["observation"], "verified": actual == span["quote"]})
        for mode in ("semantic", "structural", "topology_control"):
            export = deepcopy(gold_export)
            before = canonical(gold_export)
            started = time.perf_counter_ns()
            result = method.project_core(export, mode=mode)
            elapsed = (time.perf_counter_ns() - started) / 1_000_000
            audits = [projected_claim_audit(gold_export, result, c) for c in gold_export["claims"]]
            results.append({"id": case["id"], "split": case["split"], "dimension": case["dimension"], "mode": mode,
                "source_export_preserved": canonical(result["source"]) == before and canonical(export) == before,
                "claim_graph_audits": audits, "nodes": len(result["structure"]["nodes"]), "edges": len(result["structure"]["edges"]),
                "latency_ms": elapsed, "unknowns": result["unknowns"], "dropped_attributes": result["dropped_attributes"],
                "inference_eligible": result["inference_eligible"], "persistable_claim": result["persistable_claim"],
                "automatic_mutation": result["automatic_mutation"], "graph": result["structure"]})
    by_key = {(r["id"], r["mode"]): r for r in results}
    relationships = []
    for relation in fixture["relationships"]:
        for mode in ("semantic", "structural"):
            left = by_key[(relation["left"], mode)]["claim_graph_audits"][0]
            right = by_key[(relation["right"], mode)]["claim_graph_audits"][0]
            if relation["relation"] == "must_distinguish":
                passed = left.get("restriction_signature") != right.get("restriction_signature")
            elif relation["relation"] == "same_claim_content_different_support_provenance":
                passed = left.get("restriction_signature") == right.get("restriction_signature") and left.get("support_counts") != right.get("support_counts")
            else:
                passed = (left.get("support_counts", {}).get("unique_sources") == relation["expected_left"] and
                          right.get("support_counts", {}).get("unique_sources") == relation["expected_right"])
            passed = passed and left["restriction_graph_retained"] and right["restriction_graph_retained"]
            if relation["relation"] != "must_distinguish":
                passed = passed and left.get("support_identity_retained", False) and right.get("support_identity_retained", False)
            relationships.append({**relation, "mode": mode, "passed": passed})
    summaries = {}
    for mode in ("semantic", "structural", "topology_control"):
        subset = [r for r in results if r["mode"] == mode]
        audits = [a for r in subset for a in r["claim_graph_audits"]]
        summaries[mode] = {"cases": len(subset), "source_copies_preserved": sum(r["source_export_preserved"] for r in subset),
            "restriction_graphs_retained": sum(a["restriction_graph_retained"] for a in audits),
            "support_identity_retained": sum(a.get("support_identity_retained", False) for a in audits),
            "claims_inference_eligible": sum(r["inference_eligible"] for r in subset),
            "median_latency_ms": statistics.median(r["latency_ms"] for r in subset)}
    return {"schema": "loom.independent_core_projection_report/1", "fixture_sha256": fixture["frozen_sha256"],
        "implementation_sha256": hashlib.sha256(Path(implementation).read_bytes()).hexdigest(),
        "evaluator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "summaries": summaries, "source_checks": source_checks, "case_results": results, "relationships": relationships,
        "limitations": ["Curated native Claim exports, not quality labels for the owner's actual history.",
            "Export-copy preservation and actual graph-incidence retention are measured separately.",
            "Relation checks reconstruct represented restrictions and source identity; they do not validate the asserted world claim.",
            "Topology control deliberately drops semantic labels, so its failed retention is expected diagnostic behavior."]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, default=DEFAULT_FIXTURE)
    parser.add_argument("command", choices=("validate", "oracle", "run", "run-core"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--implementation", type=Path)
    args = parser.parse_args()
    if args.command == "run-core":
        if args.implementation is None or args.output is None:
            parser.error("run-core requires --implementation and --output")
        result = run_core(args.fixture, args.implementation)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
        print(json.dumps({"path": str(args.output), "fixture_sha256": result["fixture_sha256"]}))
        return
    fixture = read_fixture(args.fixture)
    if args.command == "validate":
        result = {"case_families": len(fixture["cases"]), "queries": len(fixture["queries"]),
                  "fixture_sha256": fixture["frozen_sha256"]}
    elif args.command == "oracle":
        result = oracle_labels(fixture)
    else:
        if args.implementation is None:
            parser.error("run requires --implementation")
        result = run_method(fixture, args.implementation)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
        print(json.dumps({"path": str(args.output), "fixture_sha256": fixture["frozen_sha256"]}))
    else:
        print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
