"""Actual first-attempt study replay with existing pure judgment scorers.

No frozen manifest, legacy ledger, historical key limit or model-alias inventory
is rewritten. Programme dispatch/billing admission remains the runner's task.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path

from . import analysis_optimization_v1 as study
from . import programme_results_v1 as results


def score_predictions(raw_by_arm, gold=None):
    """Aggregate normalized judgments without execution admission or ledger reads.

    The nine execution arms retain their planned query slots; the two split
    question arms produce one scored decision arm. Inputs are copied so joining
    those arms cannot change a caller's retained normalization records.
    """
    raw = deepcopy(raw_by_arm)
    merged = study.combine_split(raw.pop('j_split_q01'), raw.pop('j_split_q02'))
    raw['j_split'] = merged
    gold = study.w3.load_controls('gold_dev.json') if gold is None else deepcopy(gold)
    reports = {name: study.panel.score_judgments(gold, rows) for name, rows in raw.items()}
    targets = {q['query_id']: q['label'] for g in gold for q in g['judgments']}
    slices = {}
    for name, rows in raw.items():
        slices[name] = {}
        for field in ('language','family'):
            for value in sorted({g[field] for g in gold}):
                selected = [g for g in gold if g[field] == value]
                ids = {q['query_id'] for g in selected for q in g['judgments']}
                slices[name][field+':'+value] = study.panel.score_judgments(selected,[r for r in rows if r['query_id'] in ids])
    pairs = []
    for baseline, candidate in [('j_active','j_directed'),('j_directed','j_roles'),('j_directed','j_split'),
                                ('g_brief_t0','g_rules_t0'),('g_brief_t03','g_rules_t03'),
                                ('g_brief_t0','g_brief_t03'),('g_rules_t0','g_rules_t03')]:
        a = {r['query_id']: r for r in raw[baseline]}
        b = {r['query_id']: r for r in raw[candidate]}
        correct = lambda rows, q: rows.get(q,{}).get('state') == 'completed' and rows[q].get('label') == targets[q]
        valid = lambda rows, q: rows.get(q,{}).get('state') == 'completed' and rows[q].get('label') in study.LABELS
        pairs.append({'baseline':baseline,'candidate':candidate,
            'corrections':[q for q in targets if valid(a,q) and valid(b,q) and not correct(a,q) and correct(b,q)],
            'regressions':[q for q in targets if valid(a,q) and valid(b,q) and correct(a,q) and not correct(b,q)],
            'availability_gained':[q for q in targets if not valid(a,q) and valid(b,q)],
            'availability_lost':[q for q in targets if valid(a,q) and not valid(b,q)]})
    result = {'schema':'loom.analysis_optimization.score/1','reports':reports,'paired_changes':pairs,
        'by_language_and_family':slices,
        'all_planned_queries_per_arm':48,'model_quality_measured':any(r['available'] for r in reports.values()),
        'missing_is_not_correct':True,'heldout_evaluation':False}
    return result



def score_bundle(bundle, prepared):
    prepared = Path(prepared)
    plan = study.read(prepared / "plan.json")
    freeze = study.read(prepared / "freeze.json")
    # Payload identity is still mandatory. Source drift is reported separately,
    # never concealed by changing the original source/freeze receipt.
    for relative, expected in freeze["files_sha256"].items():
        path = prepared / relative
        if results.sha(path.read_bytes()) != expected:
            raise ValueError("study_frozen_payload_drift")
    source_drift = [{"path": relative, "frozen_sha256": expected,
                     "current_sha256": results.sha((study.ROOT / relative).read_bytes())}
                    for relative, expected in freeze["source_sha256"].items()
                    if results.sha((study.ROOT / relative).read_bytes()) != expected]
    manifests = {arm["arm"]: study.read(prepared / arm["arm"] / "manifest.json") for arm in plan["arms"]}
    by_request = {arm: {row["id"]: row for row in manifest["requests"]} for arm, manifest in manifests.items()}
    expected_grid = {(arm, query) for arm, rows in by_request.items() for query in rows}
    actual_grid = []
    for request in bundle["requests"]:
        arm, query = request["metadata"]["arm_id"], request["metadata"]["prepared_request_id"]
        if arm not in by_request or query not in by_request[arm]:
            raise ValueError("study_programme_request_identity_drift")
        if (request["metadata"]["source_manifest_sha256"] != results.sha((prepared / arm / "manifest.json").read_bytes())
                or request["body"] != by_request[arm][query]["body"]):
            raise ValueError("study_full_prepared_request_binding_drift")
        actual_grid.append((arm, query))
    if (len(actual_grid) != len(expected_grid) or set(actual_grid) != expected_grid
            or bundle["planned_operations"] != plan["total_requests"]):
        raise ValueError("study_full_planned_request_grid_drift")
    raw_by_arm = {arm: [] for arm in manifests}
    captures = {row["operation_id"]: row for row in bundle["responses"]}
    requests = {row["operation_id"]: row for row in bundle["requests"]}
    predictions = {}
    gold = study.w3.load_controls("gold_dev.json")
    for row in bundle["rows"]:
        request = requests[row["operation_id"]]
        arm, query = request["metadata"]["arm_id"], request["metadata"]["prepared_request_id"]
        if arm not in manifests or query not in by_request[arm]:
            raise ValueError("study_programme_request_identity_drift")
        prepared_path = prepared / arm / "manifest.json"
        if request["metadata"]["source_manifest_sha256"] != results.sha(prepared_path.read_bytes()):
            raise ValueError("study_source_manifest_binding_drift")
        if request["body"] != by_request[arm][query]["body"]:
            raise ValueError("study_exact_prepared_body_drift")
        prediction = {"query_id": query, "state": "unavailable"}
        capture = captures.get(row["operation_id"], {}).get("projection")
        if (row["response_ledger_bound"] and row["http_status"] == 200 and isinstance(capture, dict)):
            try:
                if "questions" in request["body"]:
                    answers = capture["answers"]
                    if set(answers) != set(request["body"]["questions"]) or any(answer.get("type") != "noul" for answer in answers.values()):
                        raise ValueError("study_answer_inventory_or_type_drift")
                    probabilities = {key: answer["noul"] for key, answer in answers.items()}
                    # Schema ingestion uses the existing pure threshold compiler,
                    # with no substitution of historical admission aliases.
                    for value in probabilities.values():
                        study.panel.jev_label(value, 0)
                    prediction.update(state="completed", probabilities=probabilities)
                    if set(probabilities) == {"q01", "q02"}:
                        prediction.update(study.panel.compile_jev_judgment({"probabilities": probabilities}, query))
                else:
                    prediction = study.panel.compile_gpt_judgment(capture["choices"][0]["message"]["content"], query)
            except (ValueError, KeyError, TypeError, IndexError) as error:
                prediction = {"query_id": query, "state": "unavailable", "reason": type(error).__name__ + ":" + str(error)}
        predictions[row["operation_id"]] = prediction
        raw_by_arm[arm].append(prediction)
        row["prediction"] = deepcopy(prediction)
    scored = score_predictions(raw_by_arm, gold=gold)
    scored.update(programme_id=bundle["programme_id"], stage_id=bundle["stage_id"],
                  planned_operations=bundle["planned_operations"], attempted_operations=len(bundle["rows"]),
                  planned_scored_decisions=sum(report["query_count"] for report in scored["reports"].values()),
                  model_quality_scope="Explicit source commitment on authored independent DEV; not content/world truth or held-out quality.",
                  source_drift_from_frozen_preparation=source_drift,
                  scorer_sources={"programme_study_aggregation": results.sha(Path(__file__).read_bytes()),
                                  "original_study_pure_helpers": results.sha(Path(study.__file__).read_bytes()),
                                  "pure_judgment_compiler_and_scorer": results.sha(Path(study.panel.__file__).read_bytes()),
                                  "gold_dev": results.sha((study.w3.CONTROLS / "gold_dev.json").read_bytes()),
                                  "prepared_freeze": results.sha((prepared / "freeze.json").read_bytes())},
                  no_legacy_ledger_or_budget_relabeling=True, no_historical_alias_admission=True,
                  new_model_calls=0, first_attempts_only=True)
    bundle["scoring"] = scored

    def evaluator(planned, rows):
        arm = planned[0]["metadata"]["arm_id"]
        group_predictions = [predictions[row["operation_id"]] for row in rows]
        if any("label" not in prediction and prediction["state"] == "completed" for prediction in group_predictions):
            # A split component is not a complete decision; the existing shared
            # aggregator composes its paired output in SCORE.json.
            report = study.panel.score_judgments(gold, [])
            note = "Single-question split component has no standalone semantic decision. Combined split report remains in exact SCORE.json."
        else:
            report = study.panel.score_judgments(gold, group_predictions)
            note = "Existing pure DEV scorer; all planned query slots retained, including failed/unattempted. Explicit source commitment, not world truth. Exact scorer/gold hashes retained in SCORE.json."
        denominator = report["query_count"]
        correct = sum(report["confusion"][label][label] for label in study.LABELS)
        return {
            "accuracy_all_queries": results.shared_metric(correct / denominator if report["available"] else None,
                numerator=correct, denominator=denominator, unit="fraction", note=note),
            "semantic_coverage": results.shared_metric(report["available"] / denominator,
                numerator=report["available"], denominator=denominator, unit="fraction", note=note)}
    return bundle, evaluator


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--ledger-directory", type=Path, required=True)
    parser.add_argument("--prepared", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--observed-on", required=True)
    args = parser.parse_args()
    bundle, evaluator = score_bundle(results.normalize(args.manifest, args.ledger_directory), args.prepared)
    receipt = results.export_results(bundle, args.output, observed_on=args.observed_on, evaluator=evaluator)
    print(json.dumps({key: receipt[key] for key in ("planned_operations", "attempted_operations", "packet_id", "entities", "claims", "sources")}))


if __name__ == "__main__":
    main()
