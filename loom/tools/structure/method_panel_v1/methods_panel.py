#!/usr/bin/env python3
"""Frozen model-free exploratory pair panel; local files only, no model calls."""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
STRUCTURE_ROOT = HERE.parent
sys.path.insert(0, str(STRUCTURE_ROOT))
import extract as source_extract
import structure_methods as graph_methods


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def sha_bytes(value):
    return hashlib.sha256(value).hexdigest()


def digest(value):
    return sha_bytes(canonical(value).encode())


def write_new(path, value):
    with Path(path).open("x", encoding="utf-8") as out:
        out.write(json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n")


def lexical_tokens(text):
    return re.findall(r"\w+", text.casefold())


def lexical_features(text):
    return Counter(lexical_tokens(text))


def char_features(text, ngram_range=(3, 5)):
    value = " ".join(text.casefold().split())
    return Counter((n, value[i:i+n]) for n in range(ngram_range[0], ngram_range[1]+1)
                   for i in range(len(value)-n+1))


def word_path_features(text, n=2):
    tokens = lexical_tokens(text)
    return Counter(tuple(tokens[i:i+n]) for i in range(len(tokens)-n+1))


def tfidf_fit(texts, featurizer):
    """Fit unlabelled document frequencies on unique exact passage texts."""
    unique = sorted(set(texts))
    vectors = {text: featurizer(text) for text in unique}
    df = Counter(feature for vector in vectors.values() for feature in vector)
    n = len(unique)
    weighted = {text: Counter({feature: (1 + math.log(count)) *
                  (1 + math.log((1 + n)/(1 + df[feature])))
                  for feature, count in vector.items()}) for text, vector in vectors.items()}
    serial_df = sorted(((canonical(feature), count) for feature, count in df.items()))
    return weighted, {"unique_source_count": n, "feature_count": len(df),
                      "document_frequency_sha256": digest(serial_df),
                      "deduplicated_by": "exact_text_not_normalized_features",
                      "fit_scope": "unlabelled_all_pair_sources_transductive_not_semantic_training"}


def cosine(a, b):
    result = graph_methods.cosine(a, b)
    # Floating point roundoff does not create values outside the declared range.
    return max(0.0, min(1.0, result))


def graph_scores(left, right, *, rounds=2, budget=10000):
    """Absent raw representations remain unknown, never empty-graph matches."""
    if not left["nodes"] or not right["nodes"]:
        return {"role": None, "wl": None, "alignment": None}, "unrepresented"
    alignment = graph_methods.align(left, right, budget=budget)
    return {"role": cosine(graph_methods.role_relation_features(left),
                            graph_methods.role_relation_features(right)),
            "wl": cosine(graph_methods.wl_features(left, rounds),
                          graph_methods.wl_features(right, rounds)),
            "alignment": alignment["score"]}, alignment["status"]


def nongating_union(scores):
    available = [x for x in scores if x is not None]
    return max(available) if available else None


def metrics(rows, threshold):
    counts = Counter()
    for row in rows:
        score, label = row["score"], bool(row["label"])
        if score is None:
            counts["abstained_positive" if label else "abstained_negative"] += 1
        else:
            if not math.isfinite(score):
                raise ValueError("nonfinite_score")
            pred = score >= threshold
            counts["tp" if pred and label else "fp" if pred else "fn" if label else "tn"] += 1
    positive = sum(bool(row["label"]) for row in rows)
    negative = len(rows) - positive
    tp, fp, fn, tn = (counts[k] for k in ("tp", "fp", "fn", "tn"))
    return {"planned": len(rows), "positive_planned": positive, "negative_planned": negative,
            "represented": tp+fp+fn+tn,
            **{k: counts[k] for k in ("tp", "fp", "fn", "tn", "abstained_positive", "abstained_negative")},
            "precision": tp/(tp+fp) if tp+fp else None,
            "precision_denominator": tp+fp,
            "recall_planned": tp/positive if positive else None,
            "recall_planned_denominator": positive,
            "recall_represented": tp/(tp+fn) if tp+fn else None,
            "recall_represented_denominator": tp+fn,
            "specificity_represented": tn/(tn+fp) if tn+fp else None,
            "accuracy_planned": (tp+tn)/len(rows) if rows else None,
            "missed_positive_opportunities": fn+counts["abstained_positive"]}


def ranking_metrics(rows):
    positive = [r["score"] for r in rows if r["score"] is not None and r["label"]]
    negative = [r["score"] for r in rows if r["score"] is not None and not r["label"]]
    # Ties are half a win for AUROC; average precision evaluates complete tie blocks.
    auc = sum(1 if p > n else .5 if p == n else 0 for p in positive for n in negative)
    available = [r for r in rows if r["score"] is not None]
    blocks = {}
    for row in available:
        blocks.setdefault(row["score"], []).append(row)
    tp = seen = 0
    ap = 0.0
    for score in sorted(blocks, reverse=True):
        block = blocks[score]
        added = sum(bool(r["label"]) for r in block)
        tp += added
        seen += len(block)
        if positive:
            ap += added/len(positive) * tp/seen
    return {"represented": len(available), "positive_represented": len(positive),
            "negative_represented": len(negative),
            "auroc": auc/(len(positive)*len(negative)) if positive and negative else None,
            "auroc_positive_negative_comparisons": len(positive)*len(negative),
            "average_precision_tie_blocks": ap if positive else None,
            "interpretation": "available_scores_only_abstentions_not_ranked"}


def load_fixture(fixture, policy):
    fixture = Path(fixture)
    if fixture.name != "jev_structure_pairs_v1":
        raise ValueError("this_frozen_arm_only_accepts_declared_pair_fixture")
    manifest = json.loads((fixture/"manifest.json").read_text())
    for name, expected in manifest["files_sha256"].items():
        if name not in {"README.md", "inputs.json", "gold.json"}:
            raise ValueError("unexpected_fixture_name")
        if sha_bytes((fixture/name).read_bytes()) != expected:
            raise ValueError("fixture_hash_mismatch")
    for name, expected in (("inputs.json", policy["input_sha256"]),
                           ("gold.json", policy["gold_sha256"])):
        if sha_bytes((fixture/name).read_bytes()) != expected:
            raise ValueError("frozen_policy_hash_mismatch")
    inputs = json.loads((fixture/"inputs.json").read_text())
    gold = json.loads((fixture/"gold.json").read_text())
    gold_by_id = {row["case_id"]: row for row in gold}
    if len(inputs) != 48 or len(gold_by_id) != 48 or len(gold) != 48:
        raise ValueError("pair_inventory_mismatch")
    pairs = []
    for row in inputs:
        state = json.loads(row["state"]["text"])
        if set(state) != {"left", "right"} or not all(isinstance(v, str) for v in state.values()):
            raise ValueError("invalid_pair_text")
        if row["case_id"] not in gold_by_id or set(gold_by_id[row["case_id"]]["labels"]) != {"q01"}:
            raise ValueError("invalid_pair_gold")
        pairs.append({"case_id": row["case_id"], "language": row["language"], **state})
    if len({r["case_id"] for r in pairs}) != 48 or sum(g["labels"]["q01"] for g in gold) != 32:
        raise ValueError("fixed_label_inventory_mismatch")
    return pairs, gold_by_id, manifest


def score_pairs(pairs, policy):
    """Only raw text and opaque IDs enter scoring; no gold/family/variant labels."""
    texts = [pair[side] for pair in pairs for side in ("left", "right")]
    char_vectors, char_info = tfidf_fit(texts, lambda t: char_features(t, policy["character_ngram_range"]))
    word_vectors, word_info = tfidf_fit(texts, lambda t: word_path_features(t, policy["word_ngram_length"]))
    extractions = {}
    for text in sorted(set(texts)):
        sha = sha_bytes(text.encode())
        record = {"id": "jev_raw_"+sha, "source_id": "authored_fixture_"+sha,
                  "text": text, "known_at": None}
        extractions[text] = {p: source_extract.extract_record(record, policy=p)
                             for p in policy["extractor_policies"]}
    rows = []
    for pair in pairs:
        left, right = pair["left"], pair["right"]
        scores = {"always_yes": 1.0, "always_no": 0.0,
                  "lexical_token_cosine": cosine(lexical_features(left), lexical_features(right)),
                  "char_tfidf_cosine": cosine(char_vectors[left], char_vectors[right]),
                  "directed_word_path_cosine": cosine(word_vectors[left], word_vectors[right])}
        weights = policy["word_char_kernel_weights"]
        scores["word_char_kernel"] = (weights[0]*scores["directed_word_path_cosine"] +
                                      weights[1]*scores["char_tfidf_cosine"])
        structural = {}
        for extraction_policy in policy["extractor_policies"]:
            a, b = extractions[left][extraction_policy], extractions[right][extraction_policy]
            comparisons, status = graph_scores(a["structure"], b["structure"],
                rounds=policy["wl_rounds"], budget=policy["alignment_budget"])
            for method, value in comparisons.items():
                scores["raw_"+extraction_policy+"_"+method] = value
            structural[extraction_policy] = {
                "left_status": a["status"], "right_status": b["status"],
                "left_candidates": len(a["candidates"]), "right_candidates": len(b["candidates"]),
                "left_coverage": a["coverage"], "right_coverage": b["coverage"],
                "alignment_status": status,
                "representation": "unassessed_opaque_operation_slot_envelopes_not_complete_gold_skeleton"}
        scores["nongating_union"] = nongating_union([score for name, score in scores.items()
                                                     if name not in {"always_yes", "always_no", "word_char_kernel"}])
        rows.append({"case_id": pair["case_id"], "language": pair["language"],
                     "left_sha256": sha_bytes(left.encode()), "right_sha256": sha_bytes(right.encode()),
                     "scores": scores, "structural": structural})
    return rows, extractions, {"character": char_info, "word_path": word_info}


def summarize(rows, gold, thresholds, primary):
    methods = sorted(rows[0]["scores"])
    summary = {}
    errors = []
    for method in methods:
        method_rows = [{"case_id": row["case_id"], "score": row["scores"][method],
                        "label": gold[row["case_id"]]["labels"]["q01"]} for row in rows]
        groups = {}
        for field in ("family_id", "contrast_kind", "language", "split"):
            partitions = {}
            for row in method_rows:
                partitions.setdefault(gold[row["case_id"]][field], []).append(row)
            groups[field] = {name: {"primary": metrics(items, primary), "ranking": ranking_metrics(items)}
                             for name, items in sorted(partitions.items())}
        summary[method] = {"thresholds": {str(t): metrics(method_rows, t) for t in thresholds},
                           "ranking": ranking_metrics(method_rows), "groups": groups}
        for row in method_rows:
            score, label = row["score"], bool(row["label"])
            prediction = None if score is None else score >= primary
            if prediction is None or prediction != label:
                errors.append({**row, "method": method, "prediction": prediction,
                               "kind": "abstained_positive" if score is None and label else
                                       "abstained_negative" if score is None else "false_positive" if prediction else "false_negative",
                               **{k: gold[row["case_id"]][k] for k in ("family_id", "contrast_kind", "language", "split")}})
    disagreement_rows = []
    for row in rows:
        positive = [m for m, s in row["scores"].items()
                    if s is not None and s >= primary and m not in {"always_yes", "always_no", "nongating_union"}]
        negative = [m for m, s in row["scores"].items()
                    if s is not None and s < primary and m not in {"always_yes", "always_no", "nongating_union"}]
        if positive and negative:
            disagreement_rows.append({"case_id": row["case_id"], "positive_channels": positive,
                                      "negative_channels": negative, "label": gold[row["case_id"]]["labels"]["q01"],
                                      "union_prediction": row["scores"]["nongating_union"] >= primary})
    return {"methods": summary, "primary_errors_and_abstentions": errors,
            "channel_disagreements": disagreement_rows}


def run(fixture, output):
    policy = json.loads((HERE/"policy.json").read_text())
    pairs, gold, fixture_manifest = load_fixture(fixture, policy)
    directory = Path(output)
    directory.mkdir(parents=True, exist_ok=False)
    code_paths = [Path(__file__), STRUCTURE_ROOT/"extract.py", STRUCTURE_ROOT/"structure_methods.py",
                  STRUCTURE_ROOT/"relation_envelopes.py", STRUCTURE_ROOT/"relation_envelopes_policy.json",
                  STRUCTURE_ROOT/"operations.json"]
    source_hashes = {str(p.relative_to(STRUCTURE_ROOT)): sha_bytes(p.read_bytes()) for p in code_paths}
    manifest = {"schema": "loom.research.method_panel_run/1", "started_at": datetime.now(timezone.utc).isoformat(),
        "protocol_sha256": sha_bytes((HERE/"PROTOCOL.md").read_bytes()),
        "policy_sha256": sha_bytes((HERE/"policy.json").read_bytes()), "source_hashes": source_hashes,
        "input_sha256": policy["input_sha256"], "gold_sha256": policy["gold_sha256"],
        "fixture_manifest_sha256": sha_bytes((Path(fixture)/"manifest.json").read_bytes()),
        "evaluation_status": policy["evaluation_status"], "no_model_calls": True,
        "independent_structural_annotations": {"available_pairs": 0, "planned_pairs": 48,
            "status": "not_present_in_pair_or_source_fixture", "quality_metrics": None},
        "source_extraction_is_separate_unassessed_channel": True, "no_graph_promotion": True}
    write_new(directory/"run_manifest.json", manifest)
    rows, extractions, frequencies = score_pairs(pairs, policy)
    with (directory/"first_scores.jsonl").open("x", encoding="utf-8") as out:
        for row in rows:
            out.write(canonical(row)+"\n")
    with (directory/"source_extractions.jsonl").open("x", encoding="utf-8") as out:
        for text in sorted(extractions):
            out.write(canonical({"source_sha256": sha_bytes(text.encode()), "extractions": extractions[text]})+"\n")
    results = {"schema": "loom.research.method_panel_results/1", "evaluation_status": policy["evaluation_status"],
               "thresholds": policy["thresholds"], "primary_descriptive_threshold": policy["primary_descriptive_threshold"],
               "tfidf_frequency_fit": frequencies, "independent_structural_annotations": manifest["independent_structural_annotations"],
               **summarize(rows, gold, policy["thresholds"], policy["primary_descriptive_threshold"])}
    write_new(directory/"first_results.json", results)
    evidence = {p.name: {"sha256": sha_bytes(p.read_bytes()), "bytes": p.stat().st_size}
                for p in sorted(directory.iterdir()) if p.is_file()}
    write_new(directory/"evidence_manifest.json", {"schema": "loom.research.method_panel_evidence/1", "files": evidence})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    value = run(args.fixture, args.output)
    print("First model-free scores preserved: " + str(len(value["methods"])) + " methods; 48 exploratory pairs.")


if __name__ == "__main__":
    main()
