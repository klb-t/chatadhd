#!/usr/bin/env python3
"""Replay the rejected, fixed-policy nearest-seed DEV prototype offline."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def percentile(values: list[float], percent: float) -> float:
    return sorted(values)[int(math.floor(percent / 100 * (len(values) - 1) + 0.5))] if values else 0.0


def run(baseline_path: Path) -> dict:
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    rows = baseline["conversations"] + baseline["auxiliary_documents"]
    count = len(rows)
    policy = baseline["inputs"]["native_thresholds"]["catalog"]
    if len(baseline["conversations"]) != 65 or count != 68:
        raise ValueError("This frozen replay requires 65 labeled DEV conversations and 3 auxiliary documents")
    if policy["sem_ref_percentile"] != 75:
        raise ValueError("The corrected experiment uses the recorded ref75 policy; ref50 was an invalid first attempt")
    words = [{term["term"]: term["tf"] for term in row["sketch"]["top_terms"]} for row in rows]

    def grams(term: str) -> list[str]:
        term = "_" + term + "_"
        return [term] if len(term) <= 4 else [term[i:i + 4] for i in range(len(term) - 3)]

    gram_counts = []
    for terms in words:
        counts = Counter()
        for term, frequency in terms.items():
            for gram in grams(term):
                counts[gram] += frequency
        gram_counts.append(counts)

    def vectors(documents: list[dict]) -> list[dict]:
        document_frequency = Counter(term for document in documents for term in document)
        result = []
        for document in documents:
            vector = {term: (1 + math.log(frequency)) *
                      (math.log((1 + count) / (1 + document_frequency[term])) + 1)
                      for term, frequency in document.items()}
            norm = math.sqrt(sum(value * value for value in vector.values()))
            result.append({term: value / norm for term, value in vector.items()} if norm else {})
        return result

    def dot(a: dict, b: dict) -> float:
        return sum(value * b.get(term, 0) for term, value in a.items())

    seeds = [i for i, row in enumerate(rows) if row["label"] == "relevant" and
             row["features"]["id_hits"] > 0 and not row["features"]["neg_context"]]
    spaces = [vectors(words), vectors(gram_counts)]
    similarities = [[max((dot(vector, space[seed]) for seed in seeds if seed != i), default=0)
                     for i, vector in enumerate(space)] for space in spaces]
    normalized, normalizers = [], []
    for similarities_in_space in similarities:
        floor = percentile([similarities_in_space[i] for i in range(count) if i not in seeds],
                           policy["sem_floor_percentile"])
        reference = percentile([similarities_in_space[i] for i in seeds], policy["sem_ref_percentile"])
        normalizers.append({"floor": floor, "ref": reference})
        normalized.append([max(0, min(policy["sem_cap"], (value - floor) / (reference - floor)))
                           if reference - floor > 1e-9 else 0 for value in similarities_in_space])
    measured = []
    for i, row in enumerate(rows):
        features = row["features"]
        # Retain the exact historical post-hoc recipe, including endpoint
        # clipping and fixed baseline-semantic weights. No native feedback,
        # linking, rule engine, threshold fitting or model inference is run.
        before = max(1e-15, min(1 - 1e-15, row["score"]))
        linear = (math.log(before / (1 - before)) - 1.5 * features["sem_word"] -
                  2 * features["sem_ngram"] + 1.5 * normalized[0][i] + 2 * normalized[1][i])
        score = 1 / (1 + math.exp(-linear))
        label = ("relevant" if score >= policy["tau_relevant"] else
                 "candidate" if score >= policy["tau_candidate"] else "irrelevant")
        has_owner = any(reason.get("project") == "owner" for reason in row["reasons"]["selection"])
        selected = label == "relevant" or (label == "candidate" and has_owner)
        measured.append({**row, "prototype_score": score, "prototype_label": label,
                         "prototype_selected": selected, "prototype_word": normalized[0][i],
                         "prototype_gram": normalized[1][i]})
    labeled = [row for row in measured if row["gold"] != "auxiliary"]
    tp = sum(row["gold"] == "relevant" and row["prototype_selected"] for row in labeled)
    fp = sum(row["gold"] != "relevant" and row["prototype_selected"] for row in labeled)
    summary = {"tp": tp, "fp": fp, "fn": 45 - tp, "tn": 20 - fp,
               "seeds": len(seeds), "normalization": normalizers,
               "rescued": [row["id"] for row in labeled if not row["selected"] and
                           row["prototype_selected"] and row["gold"] == "relevant"],
               "regressed": [row["id"] for row in labeled if row["selected"] and not row["prototype_selected"]],
               "noise": [row["id"] for row in labeled if row["prototype_selected"] and row["gold"] != "relevant"]}
    first = json.loads((ROOT / "evidence/corrected-first.json").read_text(encoding="utf-8"))
    matches_first = summary == first["summary"]
    if not matches_first:
        raise AssertionError("The portable replay did not reproduce the saved corrected summary")
    matches_rows = measured == first["rows"]
    if not matches_rows:
        raise AssertionError("The portable replay did not reproduce every saved corrected row")
    baseline_tp = baseline["summary"]["tp"]
    return {"schema": "loom.catalog_negative_nearest_seed_replay/1", "summary": summary, "rows": measured,
            "inputs": {"baseline_sha256": digest(baseline_path), "replay_script_sha256": digest(Path(__file__)),
                       "corrected_first_sha256": digest(ROOT / "evidence/corrected-first.json"),
                       "baseline_commit": baseline["inputs"]["native_commit"],
                       "baseline_library_sha256": baseline["inputs"]["library"]["sha256"],
                       "source_manifest": baseline["inputs"]["source_manifest"],
                       "source_manifest_sha256": baseline["inputs"]["source_manifest_sha256"],
                       "fixture_sha256": baseline["inputs"]["fixture_sha256"],
                       "catalog_policy": policy, "replacement_weights": {"sem_word": 1.5, "sem_ngram": 2},
                       "corpus": "public_synthetic_dev", "other_corpora_read": False,
                       "provider_calls": 0},
            "gates": {"recall_no_regression": {"passed": tp >= baseline_tp,
                                                  "baseline": [baseline_tp, 45], "prototype": [tp, 45]},
                      "noise_no_regression": {"passed": fp <= baseline["summary"]["fp"],
                                               "baseline": [baseline["summary"]["fp"], 20], "prototype": [fp, 20]},
                      "corrected_summary_reproduced": matches_first, "corrected_rows_reproduced": matches_rows,
                      "promotion": "rejected"},
            "limitations": ["Post-hoc DEV prototype; no independent, blind, holdout or real-archive evaluation.",
                             "Replaces two semantic terms in the rounded final score; does not rerun native feedback or linking.",
                             "Uses the historical prototype's simplified owner/candidate selection rather than a general rule interpreter.",
                             "Uses stored top-K word and character 4-gram lexical features, not model embeddings.",
                             "Source hashes identify the supplied frozen baseline; no native library is loaded by this replay."]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, default=ROOT / "inputs/baseline.json")
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    result = run(arguments.baseline)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"summary": result["summary"], "gates": result["gates"]}, sort_keys=True))


if __name__ == "__main__":
    main()
