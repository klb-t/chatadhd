#!/usr/bin/env python3
"""One frozen, offline native evaluation of the public blind_catalog_v2.

This runner never opens the real-holdout-key branch or generator. ZIP blobs are
loaded once from the pinned Git commit, hashed from those bytes, then supplied
to native scanning. The public gold blob is opened only after predictions have
been durably written. It never determines profile inputs or semantic vectors.
An existing attempt marker blocks repeated invocation, including failed runs.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sqlite3
import subprocess
import traceback

BRANCH_COMMIT = "5d85034e2353a3a6e2b2beacd1549b8cd0566584"
EXPECTED_LIBRARY = "5ded94c8771d79105c32d8b7de38c80a7500ec7cb475198e49148a9bb49a1fe5"
FIXTURE = "loom/tests/fixtures/eval/blind_catalog_v2"


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def digest(value):
    return hashlib.sha256(value).hexdigest()


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2,
                                    sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def git(repo, *args):
    return subprocess.run(["git", *args], cwd=repo, check=True,
                          capture_output=True).stdout


def summarize(rows, auxiliary):
    counts = {"tp": 0, "fp": 0, "fn": 0, "tn": 0}
    for row in rows:
        positive = row["gold"] == "relevant"
        key = ("t" if row["selected"] == positive else "f") + ("p" if row["selected"] else "n")
        counts[key] += 1
    tp, fp, fn, tn = (counts[key] for key in ("tp", "fp", "fn", "tn"))
    totals = {category: sum(row["gold"] == category for row in rows)
              for category in ("relevant", "noise_traps", "noise_generic")}
    selected = {category: sum(row["selected"] for row in rows if row["gold"] == category)
                for category in totals}
    positive = [row for row in rows if row["gold"] == "relevant"]
    negative = [row for row in rows if row["gold"] != "relevant"]
    rankings = {}
    k = len(positive)
    for field in ("score", "lexical_score", "semantic_score"):
        ordered = sorted(rows, key=lambda row: (-row[field], row["id"]))
        hits = sum(row["gold"] == "relevant" for row in ordered[:k])
        wins = sum(1.0 if p[field] > n[field] else .5 if p[field] == n[field] else 0.0
                   for p in positive for n in negative)
        rankings[field] = {"auc": wins / (len(positive) * len(negative)) if positive and negative else None,
                           "k": k, "hits_at_k": hits, "recall_at_k": hits / k if k else None}
    return {**counts, "labeled_conversations": len(rows), "category_totals": totals,
            "category_selected": selected, "precision": tp / (tp + fp) if tp + fp else None,
            "recall": tp / (tp + fn) if tp + fn else None,
            "auxiliary_total": len(auxiliary),
            "auxiliary_selected": sum(row["selected"] for row in auxiliary),
            "auxiliary_selected_ids": [row["id"] for row in auxiliary if row["selected"]],
            "ranking": rankings}


def run(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with (output / "ATTEMPT_ONCE").open("x", encoding="utf-8") as marker:
        marker.write(datetime.now(timezone.utc).isoformat() + "\n")
    receipt = {"schema": "loom.catalog_blind_first_look_inputs/1",
               "annotation": "pierwsze spojrzenie", "invocation_count": 1,
               "recorded_at": datetime.now(timezone.utc).isoformat(),
               "corpus_commit": BRANCH_COMMIT, "corpus_directory": FIXTURE,
               "protocol": {"blind_specific_protocol": "absent",
                            "source": "frozen synthetic_dev native catalog evaluation protocol",
                            "metadata_references": ["docs/CATALOG_QUALITY_2026-09-28.md",
                                                    "loom/tools/eval/README.md"],
                            "scope": "first transfer to a public fictional blind corpus; not a real archive or temporal holdout"},
               "boundaries": {"holdout_read": False, "generator_read": False,
                              "provider_calls": 0, "core_import": False,
                              "semantic_candidates_enabled": False,
                              "profile_derived_from_blind_labels": False,
                              "evaluation_labels_opened_after_predictions": True}}
    try:
        repo = args.repository.resolve()
        library = args.library.resolve()
        library_hash = digest(library.read_bytes())
        if library_hash != EXPECTED_LIBRARY:
            raise ValueError("frozen library hash mismatch; no corpus has been opened")
        baseline_bytes = args.baseline_inputs.read_bytes()
        baseline = json.loads(baseline_bytes)
        aliases = baseline["profile_input"]["aliases"]
        receipt["native_commit"] = git(repo, "rev-parse", "HEAD").decode().strip()
        receipt["library"] = {"path": str(library), "sha256": library_hash}
        receipt["profile_input"] = {"source": "frozen DEV owner declarations, unchanged",
                                    "aliases": aliases,
                                    "receipt_sha256": digest(baseline_bytes)}
        # No labels are opened or parsed in this input stage.
        sources = []
        receipt["fixture_sha256"] = {}
        input_dir = output / "inputs"
        input_dir.mkdir()
        for name in ("chatgpt_export.zip", "claude_export.zip"):
            blob = git(repo, "show", BRANCH_COMMIT + ":" + FIXTURE + "/" + name)
            receipt["fixture_sha256"][name] = digest(blob)
            destination = input_dir / name
            destination.write_bytes(blob)
            sources.append(str(destination))
            del blob
        scan = {"sources": sources, "threads": 1}
        score = {"llm": "off"}
        config = {"sources": sources, "stages": ["catalog"], "llm": "off",
                  "stage_params": {"catalog": {"scan": scan,
                                                 "profile": {"extra_terms": aliases},
                                                 "score": score,
                                                 "import": {"dry_run": True, "import_messages": False}}}}
        receipt["knowledge_config"] = config
        receipt["score_config"] = score
        receipt["runtime_config_patch"] = {}
        spec = importlib.util.spec_from_file_location(
            "catalog_dev_runner", repo / "loom/src/catalog/tests/evaluate_dev.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        receipt["native_source_manifest"] = module.source_manifest()
        receipt["native_source_manifest_sha256"] = digest(canonical(receipt["native_source_manifest"]))
        native = module.Native(library)
        runtime_path = output / "runtime"
        options = {"data_dir": str(runtime_path), "start_workers": False}
        receipt["runtime_options"] = options
        context = native.open(options)
        try:
            receipt["runtime_config"] = native.call("loom_get_config", context)
            receipt["native_pack"] = native.call("loom_kb_pack", context)
            receipt["native_thresholds"] = native.call("loom_kb_policy", context, "thresholds")
            write_json(output / "inputs_receipt.json", receipt)
            preparation = native.call("loom_knowledge_run", context, config, progress=True)
            if preparation.get("status") != "done":
                raise RuntimeError("native preparation failed: " + json.dumps(preparation))
            scored = native.call("loom_catalog_score", context, score, progress=True)
            run_id = scored["run_id"]
            decisions = native.call("loom_catalog_select", context, run_id)["decisions"]
            by_unit = {row["unit_id"]: row for row in decisions}
            units = native.call("loom_catalog_query", context,
                                {"run_id": run_id, "sort": "id", "limit": max(1, len(decisions))})
            if len(units) != len(decisions):
                raise RuntimeError("native query did not cover every native decision")
            rows = []
            for unit in units:
                uid = unit["unit"]["id"]
                identifier = unit["ext_id"] or uid
                decision = by_unit[uid]
                preview = native.call("loom_catalog_preview", context, uid)
                features = preview["score"]["features"]
                rows.append({"id": identifier, "unit_id": uid,
                             "content_hash": unit["content_hash"], "title": unit["unit"]["title"],
                             "n_chars": unit["n_chars"], "selected": decision["selected"],
                             "score": decision["score"], "label": decision["label"],
                             "decided_by": decision["decided_by"], "features": features,
                             "lexical_score": features.get("lexical_score", 0.0),
                             "semantic_score": features.get("semantic_score", 0.0),
                             "reasons": {"selection": decision["reasons"],
                                         "score": preview["score"]["reasons"]}})
            with sqlite3.connect((runtime_path / "chatadhd.db").as_uri() + "?mode=ro", uri=True) as connection:
                native_profiles = [json.loads(body) for (body,) in connection.execute(
                    "SELECT body FROM loom_cat_profiles ORDER BY id")]
            predictions = {"annotation": "pierwsze spojrzenie", "native_preparation": preparation,
                           "native_score_summary": scored, "native_profiles": native_profiles,
                           "units": sorted(rows, key=lambda row: row["id"])}
            write_json(output / "predictions_before_gold.json", predictions)
            # This public synthetic answer blob is opened ONCE, only after scores
            # and selection decisions are fixed on disk. No real key is queried.
            gold_bytes = git(repo, "show", BRANCH_COMMIT + ":" + FIXTURE + "/ground_truth.json")
            gold_hash = digest(gold_bytes)
            (input_dir / "ground_truth.json").write_bytes(gold_bytes)
            truth = json.loads(gold_bytes)
            del gold_bytes
            receipt["fixture_sha256"]["ground_truth.json"] = gold_hash
            receipt["predictions_before_gold_sha256"] = digest(canonical(predictions))
            write_json(output / "inputs_receipt.json", receipt)
            categories = truth["units"]
            if set(categories) != {"relevant", "noise_traps", "noise_generic"}:
                raise ValueError("public gold category schema differs; retain fixed predictions without guessing labels")
            gold = {row["conv_id"]: category for category, items in categories.items() for row in items}
            if len(gold) != sum(len(items) for items in categories.values()):
                raise ValueError("duplicate gold IDs")
            labeled, auxiliary = [], []
            for row in rows:
                row["gold"] = gold.get(row["id"], "auxiliary")
                (labeled if row["id"] in gold else auxiliary).append(row)
            if {row["id"] for row in labeled} != set(gold):
                raise ValueError("some gold IDs have no native prediction")
            declared_aliases = [alias for project in truth.get("projects", []) for alias in project.get("aliases", [])]
            profile_transfer = {"blind_declared_aliases_opened_after_scoring": True,
                                "declared_alias_set_matches_frozen_dev": set(declared_aliases) == set(aliases),
                                "profile_changed": False,
                                "note": "No blind declarations were applied; comparison is a frozen-profile transfer."}
            report = {"schema": "loom.catalog_blind_first_look_report/1",
                      "annotation": "pierwsze spojrzenie", "status": "measured",
                      "inputs": receipt, "profile_transfer": profile_transfer,
                      "summary": summarize(labeled, auxiliary),
                      "native_score_summary": scored,
                      "conversations": sorted(labeled, key=lambda row: row["id"]),
                      "auxiliary_documents": sorted(auxiliary, key=lambda row: row["id"]),
                      "limitations": ["Public fictional blind_catalog_v2; no private archive or real holdout was opened.",
                                      "No dedicated blind protocol or predeclared blind owner profile accompanies these artifacts.",
                                      "The exact DEV owner profile was frozen before viewing this corpus; labels entered metrics only.",
                                      "Externally supplied semantic evidence was disabled; no provider or paid model was called.",
                                      "Only one invocation is permitted; no code, parameters, aliases or vectors were tuned to this result.",
                                      "A future measurement after any such change is no longer a first look at this corpus."]}
            write_json(output / "report.json", report)
        finally:
            native.library.loom_shutdown(context)
        if digest(library.read_bytes()) != EXPECTED_LIBRARY:
            raise ValueError("library changed during measurement")
        print(json.dumps(report["summary"], ensure_ascii=False, sort_keys=True))
        print(json.dumps(profile_transfer, ensure_ascii=False, sort_keys=True))
    except Exception as error:
        write_json(output / "unavailable.json", {"annotation": "pierwsze spojrzenie",
                   "status": "unavailable", "inputs": receipt,
                   "reason": str(error), "traceback": traceback.format_exc(),
                   "repeated": False})
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--baseline-inputs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
