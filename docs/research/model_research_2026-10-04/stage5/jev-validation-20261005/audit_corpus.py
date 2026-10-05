"""Descriptive audit of frozen authored DATA; no model/result/holdout input."""
from collections import Counter
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
GOLD_SHA = "2f6e041024658543ffacc20f123ee9ef047ac32858638b8eecb49ff3c9d4c6e2"


def main():
    gold_raw = (ROOT / "new-label-corpus-v1/gold.json").read_bytes()
    inputs_raw = (ROOT / "new-label-corpus-v1/inputs.json").read_bytes()
    if hashlib.sha256(gold_raw).hexdigest() != GOLD_SHA:
        raise ValueError("frozen_gold_changed")
    gold = json.loads(gold_raw)["cases"]
    cases = json.loads(inputs_raw)["cases"]
    languages = {c["id"]: c["language"] for c in cases}
    labels = Counter(q["label"] for c in gold for q in c["judgments"])
    positions = {str(position + 1): Counter(c["judgments"][position]["label"] for c in gold)
                 for position in range(4)}
    modes = {key: sorted(counts, key=lambda label: (-counts[label], label))[0]
             for key, counts in positions.items()}
    output = {
        "schema": "loom.jev_authored_corpus_audit/1", "observed_on": "2026-10-05",
        "produced_by": {"method": "local.frozen_corpus_counts", "version": "1",
                        "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                        "parameters": {"position": "one_based_judgment_order",
                                       "mode_tie_break": "lexicographic"}},
        "input_sha256": {"gold": GOLD_SHA, "inputs": hashlib.sha256(inputs_raw).hexdigest()},
        "families": len(cases), "queries": sum(labels.values()), "labels": dict(labels),
        "labels_by_language": {language: dict(Counter(q["label"] for c in gold
                                 if languages[c["id"]] == language for q in c["judgments"]))
                               for language in sorted(set(languages.values()))},
        "labels_by_question_position": {key: dict(counts) for key, counts in positions.items()},
        "relations": dict(Counter(q["relation"] for c in cases for q in c["judgment_queries"])),
        "source_turn_count_distribution": dict(Counter(len(c["turns"]) for c in cases)),
        "majority_label_descriptive_fit": {"label": labels.most_common(1)[0][0],
                                          "correct": max(labels.values()), "denominator": sum(labels.values())},
        "position_only_descriptive_fit": {"labels_by_position": modes,
                                          "correct": sum(positions[key][mode] for key, mode in modes.items()),
                                          "denominator": sum(labels.values()),
                                          "fitted_to_this_gold": True,
                                          "held_out_prediction": False,
                                          "not_a_measured_competing_method": True},
        "corpus_or_scoring_changed": False, "actual_model_results_read": False, "model_calls": 0,
        "interpretation": "Position-label association is a construction artifact. It does not show that either recipe uses position, and does not invalidate matched recipe comparison. It limits claims of broad source understanding."
    }
    target = Path(__file__).with_name("CORPUS_AUDIT.json")
    raw = json.dumps(output, ensure_ascii=False, indent=2).encode() + b"\n"
    if target.exists() and target.read_bytes() != raw:
        raise ValueError("existing_audit_changed")
    target.write_bytes(raw)
    print(json.dumps({"families": len(cases), "queries": sum(labels.values()),
                      "position_only_descriptive_fit": output["position_only_descriptive_fit"]}))


if __name__ == "__main__":
    main()
