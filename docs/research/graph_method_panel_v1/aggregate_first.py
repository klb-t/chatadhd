"""Recount preserved DEV first outcomes; no network, fitting, or validation access.

Run from repository root:
  python3 docs/research/graph_method_panel_v1/aggregate_first.py
Writes aggregate_first.json exclusively; reruns use --check without writes.
This post-outcome reporting script does not replace either frozen scorer.
"""
from __future__ import annotations

import argparse
from collections import Counter
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from loom.tools.structure import graph_panel_live as frozen  # noqa: E402

HERE = Path(__file__).resolve().parent
FIXTURE = ROOT / "loom/tests/fixtures/research/graph_methods_panel_v1"
CLASSES = ("supported", "refuted", "unknown")
PRED_CLASSES = CLASSES + ("unavailable",)
READS = {}


def read(path):
    path = Path(path)
    data = path.read_bytes()
    READS[str(path.relative_to(ROOT))] = hashlib.sha256(data).hexdigest()
    return json.loads(data)


def normalized(row):
    return row["label"] if row.get("label") in CLASSES else "unavailable"


def recount(gold, rows, ids=None):
    """Independent confusion algebra; unavailable stays in every denominator."""
    ids = sorted(gold if ids is None else ids)
    counts = Counter((gold[q], normalized(rows[q])) for q in ids)
    confusion = {g: {p: counts[g, p] for p in PRED_CLASSES} for g in CLASSES}
    per_class = {}
    for label in CLASSES:
        tp = counts[label, label]
        den_gold = sum(counts[label, p] for p in PRED_CLASSES)
        den_pred = sum(counts[g, label] for g in CLASSES)
        per_class[label] = {
            "tp": tp, "fp": den_pred - tp, "fn": den_gold - tp,
            "gold": den_gold, "predicted": den_pred,
            "precision": tp / den_pred if den_pred else None,
            "recall": tp / den_gold if den_gold else None,
        }
    correct = sum(counts[c, c] for c in CLASSES)
    unavailable = sum(counts[c, "unavailable"] for c in CLASSES)
    recalls = [v["recall"] for v in per_class.values() if v["recall"] is not None]
    precisions = [v["precision"] for v in per_class.values() if v["precision"] is not None]
    return {
        "query_count": len(ids), "correct": correct,
        "accuracy_all_queries": correct / len(ids) if ids else None,
        "available": len(ids) - unavailable, "unavailable": unavailable,
        "coverage": (len(ids) - unavailable) / len(ids) if ids else None,
        "confusion": confusion, "per_class": per_class,
        "macro_recall": sum(recalls) / len(recalls) if recalls else None,
        "macro_precision_defined_classes": sum(precisions) / len(precisions) if precisions else None,
        "macro_precision_defined_class_count": len(precisions),
        "failures": [
            {"query_id": q, "gold": gold[q], "predicted": normalized(rows[q]),
             "raw_label": rows[q].get("label"), "state": rows[q].get("state")}
            for q in ids if normalized(rows[q]) != gold[q]
        ],
    }


def equality_to_frozen(independent, primary):
    for key in ("query_count", "accuracy_all_queries", "available", "unavailable",
                "coverage", "confusion", "per_class", "macro_recall",
                "macro_precision_defined_classes", "macro_precision_defined_class_count"):
        assert independent[key] == primary[key], (key, independent[key], primary[key])


def diagnosis(case, query):
    family, qn = case["family"], query["id"].rsplit("_", 1)[1]
    if family == "attribution":
        return "nonendorsement_is_neither_endorsement_nor_edge_denial" if qn == "q2" else "reversed_edge_not_asserted_by_queried_speaker"
    if family == "negation_unknown":
        return "embedded_proposition_negation_not_relation_denial" if qn == "q1" else "omitted_relationship_unknown_not_false"
    if family == "correction_known_at":
        return "denial_scope_excludes_alternative_cause" if qn == "q1" else "preserved_old_assertion_not_current_support_after_correction"
    return "forward_paraphrase_does_not_assert_or_deny_reverse"


def build():
    freeze = read(HERE / "SCORER_DRIVER_FREEZE4.json")
    for filename, expected in freeze["files_sha256"].items():
        assert hashlib.sha256((ROOT / filename).read_bytes()).hexdigest() == expected, filename
    cases = read(FIXTURE / "inputs_dev.json")["cases"]
    golds = read(FIXTURE / "gold_dev.json")["cases"]
    case_input = {x["id"]: x for x in cases}
    case_gold = {x["id"]: x for x in golds}
    gold = {j["query_id"]: j["label"] for c in golds for j in c["judgments"]}
    queries = {q["id"]: q for c in cases for q in c["judgment_queries"]}
    assert len(cases) == 24 and len(gold) == 96 and set(gold) == set(queries)
    assert Counter(gold.values()) == {"supported": 36, "refuted": 24, "unknown": 36}
    methods, summaries, raw = {}, {}, {}
    for method, prefix in (("jev_dual_noul", "jev_judge"), ("gpt_ternary_v2", "gpt_judge_v2")):
        merged, executions = [], []
        for batch in (1, 2):
            score_dir = HERE / f"{prefix}_batch0{batch}/first_score"
            rows = read(score_dir / "compiled_first.json")
            original = read(score_dir / "score_first.json")
            executions.append(read(score_dir / "execution_summary.json"))
            assert len(rows) == 48 and len({x["query_id"] for x in rows}) == 48
            batch_map = {x["query_id"]: x for x in rows}
            equality_to_frozen(recount(gold, batch_map, list(batch_map)), original)
            merged.extend(rows)
        row_map = {x["query_id"]: x for x in merged}
        assert len(merged) == len(row_map) == 96 and set(row_map) == set(gold)
        raw[method] = row_map
        metric = recount(gold, row_map)
        equality_to_frozen(metric, frozen.score_judgments(golds, merged))
        metric["groups"] = {
            key: {
                name: recount(gold, row_map, [q for q in gold if case_gold[q.rsplit("_q", 1)[0]][key] == name])
                for name in sorted({c[key] for c in golds})
            } for key in ("family", "language")
        }
        metric["conflicting_query_ids"] = sorted(q for q, row in row_map.items() if row.get("label") == "conflicting")
        methods[method] = metric
        summaries[method] = {
            "planned_requests": sum(x["planned_requests"] for x in executions),
            "attempted_requests": sum(x["attempted_requests"] for x in executions),
            "compiled_complete": sum(x["compiled_complete"] for x in executions),
            "reported_cost_usd": str(sum((Decimal(x["reported_cost_usd"]) for x in executions), Decimal(0))),
            "missing_cost_attempts": sum(x["missing_cost_attempts"] for x in executions),
            "elapsed_seconds_recorded_sum": sum(x["elapsed_seconds_recorded_sum"] for x in executions),
            "elapsed_interpretation": "sum_of_recorded_per_request_durations_not_wall_time",
        }
    paired = Counter()
    disagreements, both_wrong = [], []
    for q in sorted(gold):
        jr, gr = raw["jev_dual_noul"][q], raw["gpt_ternary_v2"][q]
        j, g = normalized(jr), normalized(gr)
        category = "both_correct" if j == g == gold[q] else "jev_only_correct" if j == gold[q] else "gpt_only_correct" if g == gold[q] else "both_wrong"
        paired[category] += 1
        if category == "both_wrong":
            both_wrong.append(q)
        if j != g:
            cid = q.rsplit("_q", 1)[0]
            payload = frozen.query_payload(case_input[cid], queries[q])
            assert all(t["known_at"] <= queries[q]["as_of"] for t in payload["turns"])
            disagreements.append({
                "query_id": q, "gold": gold[q], "jev": jr, "gpt": gr,
                "family": case_gold[cid]["family"], "language": case_gold[cid]["language"],
                "diagnostic_source_category": diagnosis(case_gold[cid], queries[q]),
                "cause_status": "source_pattern_and_recipe_hypothesis_not_observed_model_reasoning",
                "matched_source_payload": payload,
            })
    assert dict(paired) == {"both_correct": 75, "jev_only_correct": 11, "gpt_only_correct": 6, "both_wrong": 4}
    assert len(disagreements) == 20
    local = read(ROOT / "loom/tools/structure/graph_local_baselines_v1/first_results.json")
    assert local["queries"] == 96 and local["label_counts"] == dict(Counter(gold.values()))
    baseline = {}
    for name, value in local["naive_judgments"].items():
        primary = value if name == "always_unknown" else value["0.5"]
        baseline[name] = {k: primary[k] for k in ("query_count", "accuracy_all_queries", "per_class", "confusion", "macro_recall", "coverage")}
    extraction = read(HERE / "extraction/first_score/score_first.json")
    extracted = read(HERE / "extraction/first_score/compiled_first.json")
    summaries["gpt_assisted_extraction"] = read(HERE / "extraction/first_score/execution_summary.json")
    v1 = read(HERE / "gpt_judge_batch01/first_score/score_first.json")
    summaries["gpt_judge_v1_transport_failure"] = read(HERE / "gpt_judge_batch01/first_score/execution_summary.json")
    extraction_groups = {}
    for group in ("family", "language"):
        extraction_groups[group] = {}
        for name in sorted({c[group] for c in golds}):
            rows = [c for c in extraction["cases"] if c[group] == name]
            e = {k: sum(c["edges"][k] for c in rows) for k in ("tp", "fp", "fn", "gold", "predicted")}
            e["precision"], e["recall"] = e["tp"] / e["predicted"], e["tp"] / e["gold"]
            extraction_groups[group][name] = e
    costs = sum((Decimal(s["reported_cost_usd"]) for s in summaries.values()), Decimal(0))
    report = {
        "schema": "loom.graph_method_panel_first_aggregate/1", "split": "dev",
        "post_outcome_report_only": True, "validation_accessed": False,
        "api_calls_by_author": 0, "content_truth_accuracy": None, "no_graph_promotion": True,
        "case_count": 24, "query_count": 96, "gold_class_counts": dict(Counter(gold.values())),
        "independent_recount_matches_frozen_batch_and_aggregate_scores": True,
        "supplied_edge_judgment": methods,
        "paired": dict(paired), "both_wrong_query_ids": both_wrong,
        "disagreements": disagreements,
        "local_naive_diagnostics_at_frozen_threshold_0_50": baseline,
        "local_retrieval_distinct_task": local["retrieval"],
        "assisted_extraction": {
            "strict_edges": extraction["strict_edges"], "strict_status_events": extraction["strict_status_events"],
            "event_convention_diagnostic": extraction["event_convention_diagnostic"],
            "groups": extraction_groups,
            "narrow_quote_matches_needing_clause_review": sum(c["narrow_quote_matches_needing_clause_review"] for c in extraction["cases"]),
            "compiled_source_assertions": sum(len(c["source_assertions"]) for c in extracted),
            "invalid_assertions": sum(c["invalid_assertions"] for c in extracted),
            "invalid_events": sum(c["invalid_events"] for c in extracted),
            "failure_cases": [c for c in extraction["cases"] if c["edges"]["fp"] or c["edges"]["fn"] or c["events"]["fp"] or c["events"]["fn"]],
            "semantic_quote_review": "separate_independent_audit_required_not_certified_by_turn_binding",
        },
        "execution": summaries,
        "v1_transport_failure_score_preserved": v1,
        "all_actual_attempts": sum(s["attempted_requests"] for s in summaries.values()),
        "all_compiled_complete": sum(s["compiled_complete"] for s in summaries.values()),
        "reported_known_cost_usd": str(costs),
        "unknown_cost_attempts": sum(s["missing_cost_attempts"] for s in summaries.values()),
        "retained_unknown_cost_reservation_usd": "0.001366",
        "files_sha256": dict(sorted(READS.items())),
    }
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--check", action="store_true")
    args = p.parse_args()
    result = build()
    path = HERE / "aggregate_first.json"
    if args.check:
        assert json.loads(path.read_text()) == result
    else:
        with path.open("x") as out:
            json.dump(result, out, ensure_ascii=False, indent=2, sort_keys=True)
            out.write("\n")
    print(json.dumps({"paired": result["paired"], "reported_known_cost_usd": result["reported_known_cost_usd"], "all_actual_attempts": result["all_actual_attempts"], "independent_recount_matches": True}, sort_keys=True))
