#!/usr/bin/env python3
"""Verify six saved DEV receipts; never load a corpus, model, or native binary.

python loom/src/catalog/tests/verify_dev_followup.py \
  loom/src/catalog/tests/results/2026-10-04/dev-followup --output verification.json
"""
import argparse
import hashlib
import json
import math
from pathlib import Path


def verify(directory):
    result = {"schema": "loom.catalog_dev_followup_verification/1", "ok": False,
              "scope": "saved DEV receipts only", "files_sha256": {}, "checks": []}

    def check(condition, name):
        if not condition:
            raise ValueError(name)
        result["checks"].append(name)

    def canonical(value):
        return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()

    try:
        reports, inputs, rows = {}, {}, {}
        for name in ("before", "after", "calibrated"):
            for suffix, destination in ((".json", reports), (".inputs.json", inputs)):
                filename = name + suffix
                raw = (directory / filename).read_bytes()
                result["files_sha256"][filename] = hashlib.sha256(raw).hexdigest()
                destination[name] = json.loads(raw)
            report, receipt = reports[name], inputs[name]
            check(report["inputs"] == receipt, name + ": embedded inputs match separate receipt")
            check(receipt["boundaries"] == {"blind_read": False, "holdout_read": False,
                  "provider_calls": 0, "core_import": False, "corpus": "synthetic_dev_only"},
                  name + ": declared offline DEV boundary")
            combined = report["conversations"] + report["auxiliary_documents"]
            rows[name] = {row["id"]: row for row in combined}
            check(len(report["conversations"]) == 65 and len(report["auxiliary_documents"]) == 3
                  and len(combined) == len(rows[name]) == 68, name + ": 68 unique rows")
            tp = sum(row["gold"] == "relevant" and row["selected"] for row in combined)
            fp = sum(row["gold"].startswith("noise_") and row["selected"] for row in combined)
            check(tp == report["summary"]["tp"] and fp == report["summary"]["fp"] == 0
                  and report["summary"]["false_positive_ids"] == [], name + ": recorded TP and zero FP match rows")

        check(rows["before"].keys() == rows["after"].keys() == rows["calibrated"].keys(), "same row identities")
        identical = [key for key in rows["before"] if rows["before"][key] == rows["after"][key]]
        check(len(identical) == 68, "all 68 complete default rows identical including all channel scores")
        check(all(len({json.dumps(rows[name][key][field], sort_keys=True) for name in rows}) == 1
                  for key in rows["before"] for field in ("content_hash", "gold", "sketch")),
                  "same recorded source hashes labels and sketches")
        for key in ("fixture_sha256", "profile_input", "native_thresholds", "score_config", "knowledge_config",
                    "runtime_config_before", "runtime_config_effective", "runtime_config_patch", "semantic_input"):
            check(inputs["before"][key] == inputs["after"][key] == inputs["calibrated"][key], "unchanged " + key)
        check([{k: v for k, v in inputs[name]["runtime_options"].items() if k != "data_dir"} for name in inputs]
              == [{k: v for k, v in inputs["before"]["runtime_options"].items() if k != "data_dir"}] * 3,
              "runtime options unchanged except isolated data directories")
        profiles = [[{k: v for k, v in profile.items() if k not in ("id", "input_hash")}
                     for profile in reports[name]["native_profiles"]] for name in reports]
        check(profiles[0] == profiles[1] == profiles[2], "all profile terms projects and aliases unchanged")
        check(inputs["after"]["library"]["sha256"] == inputs["calibrated"]["library"]["sha256"],
              "final default and calibrated recorded binary SHA256 equal")
        check(inputs["after"]["source_manifest"] == inputs["calibrated"]["source_manifest"],
              "final default and calibrated recorded source manifests equal")
        check(all(inputs["before"]["source_manifest"][key] == inputs["after"]["source_manifest"][key]
                  for key in inputs["before"]["source_manifest"] if key.startswith("loom/data/")),
                  "all tracked data defaults unchanged")
        default, calibrated = inputs["after"]["native_relevance"], inputs["calibrated"]["native_relevance"]
        check({k: v for k, v in default.items() if k != "bias"}
              == {k: v for k, v in calibrated.items() if k != "bias"} and default["bias"] != calibrated["bias"],
              "relevance document differs only in bias")
        check(inputs["calibrated"]["relevance_overlay"]["document"] == calibrated,
              "supplied overlay equals recorded native relevance")
        for name in ("after", "calibrated"):
            recipe = reports[name]["native_score_summary"]["relevance_recipe"]
            doc = inputs[name]["native_relevance"]
            expected = {key: doc[key] for key in ("bias", "weights", "term_class_weights", "bm25")}
            expected["channels"] = {feature: channel for channel, features in doc["channels"].items() for feature in features}
            check(recipe["parameters"] == expected, name + ": receipt contains actual relevance parameters")
            check(hashlib.sha256(canonical(recipe["parameters"])).hexdigest() == recipe["sha256"],
                  name + ": effective recipe SHA256 matches parameters")

        lost = sorted(key for key in rows["after"] if rows["after"][key]["selected"] and not rows["calibrated"][key]["selected"])
        recovered = sorted(key for key in rows["after"] if not rows["after"][key]["selected"] and rows["calibrated"][key]["selected"])
        check(not lost, "no previously selected row lost")
        check(recovered == ["nf-02-storage", "nf-12-encryption-and-lost-again"]
              and all(rows["calibrated"][key]["gold"] == "relevant" for key in recovered), "exactly two declared relevant rows recovered")
        baseline = reports["before"]["summary"]["ranking"]
        for name in ("after", "calibrated"):
            ranking = reports[name]["summary"]["ranking"]
            check(ranking.keys() == baseline.keys(), name + ": all recorded ranking channels preserved")
            for channel, metrics in baseline.items():
                check(ranking[channel].keys() == metrics.keys(), name + ": all ranking metrics preserved for " + channel)
                for metric, value in metrics.items():
                    if metric == "auc" or metric.startswith("hits_at_"):
                        actual = ranking[channel][metric]
                        check(math.isfinite(actual) and actual >= value, name + ": no regression in " + channel + "/" + metric)
        result.update(ok=True, default_rows_identical=len(identical), recovered=recovered, lost=lost,
                      tp={name: reports[name]["summary"]["tp"] for name in reports}, fp=0,
                      final_binary_sha256=inputs["after"]["library"]["sha256"])
    except (OSError, ValueError, KeyError, TypeError) as error:
        result["error"] = str(error)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    outcome = verify(args.directory)
    rendered = json.dumps(outcome, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end="")
    raise SystemExit(0 if outcome["ok"] else 1)
