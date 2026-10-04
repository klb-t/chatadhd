#!/usr/bin/env python3
"""Native catalog mechanism regressions on known public DEV cases.

The matching vectors for 14 known omissions are deliberately label-derived mock
fixtures. They test receipt binding, pre-selection routing and restoration,
never model efficacy, specificity or improved real-archive retrieval quality.
No model or provider is called. Auxiliary project/memory records are excluded
from the 19 named regressions and retained when checking complete restoration.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sqlite3
import tempfile
from typing import Any
import zipfile

from evaluate_dev import FIXTURE, Native, canonical, commit_id, sha256, source_manifest, write_json


INVENTORY = Path(__file__).with_name("dev_regressions.json")
DISCLOSURE = (
    "The 14 matching vectors are deliberately assigned from known DEV labels. "
    "These are synthetic mechanism fixtures, not saved model responses. "
    "No improved model recall or model specificity is measured; the observed "
    "retrieval baseline remains 31/45 with 0/20 selected noise conversations."
)


def require(condition: Any, description: str) -> None:
    if not condition:
        raise AssertionError(description)


def set_envelope(native: Native, context: int, envelope: dict) -> None:
    status = native.library.loom_set_config_json(context, canonical({"catalog_semantic_candidates": envelope}))
    require(status == 0, "native configuration update failed")


def score_and_inspect(native: Native, context: int) -> tuple[dict, dict[str, dict]]:
    summary = native.call("loom_catalog_score", context, {"llm": "off"}, progress=True)
    decisions = native.call("loom_catalog_select", context, summary["run_id"])["decisions"]
    by_unit = {row["unit_id"]: row for row in decisions}
    units = native.call("loom_catalog_query", context,
                        {"run_id": summary["run_id"], "sort": "id", "limit": len(decisions)})
    require(len(units) == len(decisions), "query must cover every native catalog decision")
    result = {}
    for unit in units:
        uid = unit["unit"]["id"]
        identifier = unit["ext_id"] or uid
        decision = by_unit[uid]
        preview = native.call("loom_catalog_preview", context, uid)
        require(identifier not in result, "duplicate fixture conversation ID")
        result[identifier] = {"unit": unit, "decision": decision,
                              "features": preview["score"]["features"],
                              "score_reasons": preview["score"]["reasons"]}
    return summary, result


def decision_state(row: dict) -> dict:
    decision = row["decision"]
    return {name: decision[name] for name in ("selected", "score", "label", "decided_by", "reasons")}


def frozen_state(row: dict) -> dict:
    return {"selected": row["selected"], "score": row["score"], "label": row["label"],
            "decided_by": row["decided_by"], "reasons": row["reasons"]["selection"]}


def run(arguments: argparse.Namespace) -> dict:
    baseline = json.loads(arguments.baseline.read_text(encoding="utf-8"))
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    require(sha256(arguments.baseline) == inventory["baseline_report_sha256"],
            "baseline receipt differs from the frozen regression inventory")
    require(baseline["summary"]["tp"] == 31 and baseline["summary"]["fn"] == 14 and
            baseline["summary"]["fp"] == 0 and baseline["summary"]["tn"] == 20,
            "regression baseline changed")
    for filename, expected in inventory["fixture_sha256"].items():
        require(sha256(FIXTURE / filename) == expected, "public DEV fixture bytes changed: " + filename)
    omissions = [row for row in inventory["cases"] if row["regression_group"] == "known_omission"]
    rescues = [row for row in inventory["cases"] if row["regression_group"] == "lexical_rescue"]
    require(len(omissions) == 14 and len(rescues) == 5, "named regression denominators changed")
    require(all(row["gold_group"] == "relevant" for row in inventory["cases"]),
            "auxiliary or negative records entered positive mechanism regressions")
    for filename in ("chatgpt_export.zip", "claude_export.zip"):
        with zipfile.ZipFile(FIXTURE / filename) as archive:
            conversations = json.loads(archive.read("conversations.json"))
            for case in inventory["cases"]:
                locator = case["fixture_locator"]
                if locator["source_file"] != filename:
                    continue
                record = conversations[int(locator["json_pointer"].removeprefix("/"))]
                require(record.get("id", record.get("uuid")) == case["id"],
                        "regression locator no longer resolves its exact conversation")
    truth = json.loads((FIXTURE / "ground_truth.json").read_text(encoding="utf-8"))
    aliases = [alias for project in truth["projects"] for alias in project["aliases"]]
    noise_ids = {row["conv_id"] for category in ("noise_traps", "noise_generic")
                 for row in truth["units"][category]}
    require(len(noise_ids) == 20, "noise denominator changed")
    library = arguments.library.resolve()
    source_hashes = source_manifest()
    inputs = {"recorded_at": datetime.now(timezone.utc).isoformat(), "native_commit": commit_id(),
              "library": {"path": str(library), "sha256": sha256(library)},
              "source_manifest": source_hashes, "inventory_sha256": sha256(INVENTORY),
              "baseline_report_sha256": sha256(arguments.baseline),
              "fixture_sha256": inventory["fixture_sha256"], "disclosure": DISCLOSURE}
    inputs_path = arguments.output.with_name(arguments.output.stem + ".inputs.json")
    write_json(inputs_path, inputs)
    native = Native(library)
    with tempfile.TemporaryDirectory(prefix="loom-catalog-replay-") as temporary:
        context = native.open({"data_dir": str(Path(temporary) / "runtime"), "start_workers": False})
        try:
            config = {"sources": [str(FIXTURE / name) for name in ("chatgpt_export.zip", "claude_export.zip")],
                      "stages": ["catalog"], "llm": "off", "stage_params": {"catalog": {
                          "scan": {"threads": 1}, "profile": {"extra_terms": aliases},
                          "score": {"llm": "off"}, "import": {"dry_run": True, "import_messages": False}}}}
            inputs["knowledge_config"] = config
            inputs["runtime_config_before"] = native.call("loom_get_config", context)
            inputs["native_pack"] = native.call("loom_kb_pack", context)
            write_json(inputs_path, inputs)
            prepared = native.call("loom_knowledge_run", context, config, progress=True)
            require(prepared["status"] == "done", "native catalog preparation failed")
            baseline_summary, before = score_and_inspect(native, context)
            expected = {row["id"]: row for row in baseline["conversations"] + baseline["auxiliary_documents"]}
            require(set(before) == set(expected), "native fixture coverage differs from the frozen baseline")
            for identifier, row in before.items():
                require(decision_state(row) == frozen_state(expected[identifier]),
                        "disabled native decisions differ from frozen baseline: " + identifier)
            with sqlite3.connect((Path(temporary) / "runtime/chatadhd.db").as_uri() + "?mode=ro", uri=True) as database:
                profiles = [json.loads(body) for (body,) in database.execute(
                    "SELECT body FROM loom_cat_profiles ORDER BY created DESC")]
            require(len(profiles) == 1, "fresh fixture runtime must have one native profile")
            records = []
            for case in omissions:
                unit = before[case["id"]]["unit"]
                require(unit["unit"]["id"] == case["native_unit_id"], "native unit locator hash changed")
                require(unit["content_hash"] == case["content_sha256"], "native exact record hash changed")
                records.append({"unit_id": unit["unit"]["id"], "content_hash": unit["content_hash"], "vector": [1, 0]})
            envelope = {"schema": "loom.catalog_semantic_candidates/1", "enabled": True,
                        "profile_input_hash": profiles[0]["input_hash"], "channel": "synthetic_mechanism_fixture",
                        "model": "test-mock-not-model-quality", "method": "synthetic_mechanism_fixture",
                        "query": [1, 0], "records": records,
                        "policy": {"bias": -3, "weight": 8, "tau_relevant": 0.7, "fusion": "union"}}
            inputs["mock_envelope"] = envelope
            inputs["mock_vector_origin"] = "deliberately_label_derived_mechanism_fixture"
            write_json(inputs_path, inputs)
            set_envelope(native, context, envelope)
            active_summary, active = score_and_inspect(native, context)
            require("external_semantic" in active_summary, "native scorer does not expose the replay extension")
            require(active_summary["external_semantic"]["available"] is True,
                    "native library did not consume the replay extension")
            require(active_summary["external_semantic"]["supplied_units"] == 14,
                    "native replay supplied-unit count changed")
            omission_checks = []
            for case in omissions:
                row = active[case["id"]]
                evidence = row["features"].get("external_semantic_evidence", {})
                require(row["decision"]["selected"] is True, "known omission was not selected: " + case["id"])
                require(row["decision"]["label"] == "relevant", "semantic hit did not precede label selection")
                require(evidence.get("content_hash") == case["content_sha256"], "exact content hash receipt lost")
                require(evidence.get("profile_input_hash") == profiles[0]["input_hash"], "profile binding lost")
                require(evidence.get("model") == "test-mock-not-model-quality", "mock model identity lost")
                require(evidence.get("origin") == "supplied_vector", "vector origin was fabricated")
                require(evidence.get("score_kind") == "retrieval_rank", "retrieval rank provenance was lost")
                expected_vector_hash = hashlib.sha256(canonical([1, 0])).hexdigest()
                require(evidence.get("query_hash") == expected_vector_hash and
                        evidence.get("vector_hash") == expected_vector_hash,
                        "query/vector byte hashes were lost")
                require(row["features"].get("identity_alias_hits") == 0 and row["features"].get("principle_hits") == 0,
                        "semantic vector manufactured lexical identity")
                require(any("semantic_candidate" in reason for reason in row["decision"]["reasons"]),
                        "selection omitted semantic receipt")
                omission_checks.append({"id": case["id"], "passed": True,
                    "was_tiny_exclusion": case["baseline"]["decided_by"] == "rule:sel.tiny",
                    "content_hash": evidence["content_hash"], "origin": evidence["origin"]})
            rescue_checks = []
            for case in rescues:
                row = active[case["id"]]
                require(row["decision"]["selected"] is True, "lexical rescue was lost")
                require("external_semantic_evidence" not in row["features"], "unprovided lexical rescue gained a vector hit")
                require(decision_state(row) == decision_state(before[case["id"]]), "lexical rescue decision changed")
                rescue_checks.append({"id": case["id"], "passed": True, "external_hit": False})
            for identifier in noise_ids:
                require(active[identifier]["decision"]["selected"] is False, "unprovided noise was selected")
                require("external_semantic_evidence" not in active[identifier]["features"],
                        "unprovided noise gained an external vector hit")
            supplied = {case["id"] for case in omissions}
            for identifier in set(active) - supplied:
                require(decision_state(active[identifier]) == decision_state(before[identifier]),
                        "unprovided decision changed: " + identifier)
            set_envelope(native, context, {"enabled": False})
            disabled_summary, restored = score_and_inspect(native, context)
            require(disabled_summary["external_semantic"]["available"] is False, "disabled channel remained active")
            for identifier in before:
                require(decision_state(restored[identifier]) == decision_state(before[identifier]),
                        "disabling channel did not restore exact baseline decision: " + identifier)
                require(restored[identifier]["features"] == before[identifier]["features"],
                        "disabling channel left stale semantic features: " + identifier)
            result = {"schema": "loom.catalog_mechanism_regressions/1", "inputs": inputs,
                      "summary": {"mechanism_cases_passed": 14, "mechanism_cases_total": 14,
                                  "lexical_rescues_passed": 5, "lexical_rescues_total": 5,
                                  "unprovided_noise_rejected": 20, "unprovided_noise_total": 20,
                                  "tiny_case_promotions_verified": sum(row["was_tiny_exclusion"] for row in omission_checks),
                                  "restored_decisions_exact": len(before), "provider_calls": 0,
                                  "model_quality_evaluated": False, "observed_baseline_recall": 31 / 45},
                      "omission_checks": omission_checks, "lexical_rescue_checks": rescue_checks,
                      "noise_checks": [{"id": identifier, "excluded": True, "external_hit": False}
                                       for identifier in sorted(noise_ids)],
                      "native_channel": active_summary["external_semantic"],
                      "run_ids": {"baseline": baseline_summary["run_id"], "synthetic": active_summary["run_id"],
                                  "restored": disabled_summary["run_id"]},
                      "disclosure": DISCLOSURE,
                      "limitations": ["Only known public DEV cases are exercised; no blind or holdout corpus is read.",
                                      "The positive-only supplied mock records do not measure model false positives.",
                                      "Auxiliary project/memory records are not among the 19 labeled mechanism regressions."]}
        finally:
            native.library.loom_shutdown(context)
    require(sha256(library) == inputs["library"]["sha256"], "native library changed during execution")
    write_json(arguments.output, result)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    result = run(arguments)
    print(json.dumps(result["summary"], sort_keys=True))


if __name__ == "__main__":
    main()
