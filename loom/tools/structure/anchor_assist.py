#!/usr/bin/env python3
"""Offline, audited coordinate assistance; never infer or repair graph meaning."""
from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import hashlib
from pathlib import Path

try:
    from . import live_pilot as pilot
    from .live_pilot_score import _preflight, score_case
    from .openrouter_runner import plan_manifest, _validate_ledger
except ImportError:
    import live_pilot as pilot
    from live_pilot_score import _preflight, score_case
    from openrouter_runner import plan_manifest, _validate_ledger

METHOD = "unique_exact_quote_utf8_v1"
RULES = {
    "method": METHOD,
    "post_hoc_dev_only": True,
    "quote_matching": "literal_case_sensitive_no_normalization_unique_including_overlaps",
    "modifiable_fields": ["byte_start", "byte_len"],
    "required_span_fields": ["observation", "quote", "byte_start", "byte_len"],
    "coordinate_types": "existing_integers_not_bool",
    "support_locations": ["entity_drafts[].support", "claim_drafts[].assessment.basis.support",
                          "coverage[].support", "unknowns[].support"],
    "multiple_matches": "unchanged_even_if_old_offset_was_valid",
    "missing_or_malformed": "unchanged",
    "duplicate_json_keys": "reject",
    "alternative_selection": "first_only",
    "semantic_or_identifier_repair": False,
    "graph_promotion": False,
}


def _supports(bundle):
    for collection in ("entity_drafts", "claim_drafts", "coverage", "unknowns"):
        rows = bundle.get(collection)
        if not isinstance(rows, list):
            continue
        for index, row in enumerate(rows):
            if not isinstance(row, dict):
                continue
            path = f"/{collection}/{index}"
            if collection == "claim_drafts":
                assessment = row.get("assessment")
                if not isinstance(assessment, dict):
                    continue
                row = assessment.get("basis")
                if not isinstance(row, dict):
                    continue
                path += "/assessment/basis"
            support = row.get("support")
            if isinstance(support, list):
                for number, span in enumerate(support):
                    yield f"{path}/support/{number}", span


def assist_bundle(packet, bundle):
    """Return a new bundle plus audit, modifying only unambiguous byte offsets."""
    if _preflight(packet) or _preflight(bundle):
        raise ValueError("bounded JSON inputs required")
    if (not isinstance(packet, dict) or packet.get("schema") != "loom.source_packet/1" or
            not isinstance(packet.get("snapshot_id"), str) or not packet["snapshot_id"] or
            not isinstance(packet.get("observations"), list) or
            not isinstance(bundle, dict) or bundle.get("schema") != "loom.candidate_graph/1" or
            bundle.get("packet_id") != packet["snapshot_id"]):
        raise ValueError("source/bundle identity mismatch")
    observations = {}
    for observation in packet["observations"]:
        if (not isinstance(observation, dict) or not isinstance(observation.get("id"), str) or
                not observation["id"] or observation["id"] in observations or
                not isinstance(observation.get("text"), str)):
            raise ValueError("source observation identity/text invalid")
        observations[observation["id"]] = observation["text"]
    result = deepcopy(bundle)
    audits = []
    for path, span in _supports(result):
        old = deepcopy(span)
        decision = "malformed_span_unchanged"
        if (isinstance(span, dict) and set(span) == set(RULES["required_span_fields"]) and
                type(span["byte_start"]) is int and type(span["byte_len"]) is int and
                isinstance(span["observation"], str) and isinstance(span["quote"], str) and span["quote"]):
            text = observations.get(span["observation"])
            if text is None:
                decision = "foreign_observation_unchanged"
            else:
                first = text.find(span["quote"])
                if first < 0:
                    decision = "quote_absent_unchanged"
                elif text.find(span["quote"], first + 1) >= 0:
                    decision = "quote_ambiguous_unchanged"
                else:
                    start = len(text[:first].encode("utf-8"))
                    length = len(span["quote"].encode("utf-8"))
                    decision = "already_exact" if (span["byte_start"], span["byte_len"]) == (start, length) else "coordinates_updated"
                    span["byte_start"], span["byte_len"] = start, length
        audits.append({"path": path, "decision": decision, "old": old, "new": deepcopy(span)})
    return result, {"method": METHOD, "rules_sha256": pilot.digest(RULES),
                    "source_packet_sha256": pilot.digest(packet), "original_bundle_sha256": pilot.digest(bundle),
                    "assisted_bundle_sha256": pilot.digest(result), "spans": audits,
                    "decision_counts": dict(Counter(a["decision"] for a in audits))}


def replay(run_dirs, output):
    """Replay disjoint saved dev runs, never call a model or overwrite a report."""
    cases, fixture = pilot.load_inputs("dev")
    by_case = {case["case_id"]: case for case in cases}
    raw_gold = (pilot.FIXTURES / "gold.dev.json").read_bytes()
    if hashlib.sha256(raw_gold).hexdigest() != fixture["files_sha256"]["gold.dev.json"]:
        raise ValueError("frozen development gold changed")
    gold = {row["case_id"]: row for row in pilot.parse(raw_gold)}
    frozen = {name: hashlib.sha256((pilot.HERE/name).read_bytes()).hexdigest()
              for name in ("candidate_graph.py", "candidate_graph_vocabulary.json", "live_pilot_score.py")}
    rows, sources, attempted, inventory = {}, [], set(), None
    for run_dir in map(Path, run_dirs):
        manifest = pilot.read_json(run_dir/"manifest.json")
        if (manifest["metadata"]["pilot_request"]["split"] != "dev" or
                manifest["metadata"]["fixture_manifest_sha256"] != pilot.digest(fixture) or
                any(manifest["metadata"]["code_sha256"].get(k) != v for k, v in frozen.items())):
            raise ValueError("source experiment/scorer contract changed")
        plan = plan_manifest(manifest)
        ledger = pilot.read_json(run_dir/"run/ledger.json")
        attempts = _validate_ledger(ledger, plan, run_dir/"run")
        requests = {r["id"]: r for r in manifest["requests"]}
        if inventory is None:
            inventory = requests
        elif any(identifier not in inventory or pilot.digest(request["body"]) != pilot.digest(inventory[identifier]["body"])
                 for identifier, request in requests.items()):
            raise ValueError("continuation request inventory/body mismatch")
        sources.append({"experiment_id": manifest["experiment_id"], "manifest_sha256": pilot.digest(manifest),
                        "ledger_file_sha256": hashlib.sha256((run_dir/"run/ledger.json").read_bytes()).hexdigest()})
        for attempt in attempts:
            identifier = attempt["id"]
            if identifier in attempted:
                raise ValueError("overlapping attempted requests")
            attempted.add(identifier)
            request = requests[identifier]; meta = request["metadata"]
            if meta["method"] != "native_v1":
                raise ValueError("this diagnostic arm requires native_v1")
            case = by_case[meta["case_id"]]; packet = case["source_packet"]
            user = pilot.parse(request["body"]["messages"][1]["content"].encode())
            if (user != {"packet_hash": pilot.digest(packet), "source_packet": packet} or
                    meta["packet_hash"] != pilot.digest(packet)):
                raise ValueError("request/case/source packet mismatch")
            row = {"request_id": identifier, "case_id": case["case_id"], "model": request["body"]["model"],
                   "provider": request["body"]["provider"]["only"][0], "language": case["language"],
                   "status": attempt["state"], "response_sha256": attempt.get("response_sha256"),
                   "before": None, "after": None, "audit": None}
            rows[identifier] = row
            if attempt["state"] != "completed":
                continue
            raw = (run_dir/"run"/attempt["response_file"]).read_bytes()
            response = pilot.parse(raw)
            if (response.get("model") not in meta["response_identity"]["model_ids"] or
                    response.get("provider") not in meta["response_identity"]["providers"]):
                raise ValueError("saved response model/provider mismatch")
            choices = response["choices"]
            if len(choices) != 1 or choices[0]["finish_reason"] != "stop":
                raise ValueError("saved completed response is not final")
            try:
                envelope = pilot.parse(choices[0]["message"]["content"].encode())
            except ValueError:
                row["status"] = "invalid_json_unchanged"
                continue
            if (not isinstance(envelope, dict) or set(envelope) != {"schema_version", "packet_hash", "bundles"} or
                    type(envelope["schema_version"]) is not int or envelope["schema_version"] != 2 or
                    envelope["packet_hash"] != pilot.digest(packet) or not isinstance(envelope["bundles"], list) or
                    not 1 <= len(envelope["bundles"]) <= 16):
                row["status"] = "invalid_envelope_unchanged"
                continue
            bundle = envelope["bundles"][0]
            row["before"] = score_case(case, gold[case["case_id"]], bundle)
            try:
                assisted, audit = assist_bundle(packet, bundle)
            except ValueError:
                row["status"] = "ineligible_bundle_unchanged"
                row["after"] = deepcopy(row["before"])
                continue
            row.update(status="assisted", audit=audit, assisted_bundle=assisted,
                       after=score_case(case, gold[case["case_id"]], assisted))
    if inventory is None:
        raise ValueError("at least one saved experiment required")
    for identifier, request in inventory.items():
        if identifier not in rows:
            meta = request["metadata"]
            rows[identifier] = {"request_id": identifier, "case_id": meta["case_id"],
                               "model": request["body"]["model"], "provider": request["body"]["provider"]["only"][0],
                               "language": meta["language"], "status": "not_attempted",
                               "response_sha256": None, "before": None, "after": None, "audit": None}
    ordered = [rows[identifier] for identifier in inventory]
    metrics = ("contract_valid", "source_support_valid", "structural_exact", "anchored_semantic_exact", "semantic_exact")
    summary = {"planned": len(inventory), "attempted": len(attempted),
               "status_counts": dict(Counter(row["status"] for row in ordered)),
               "before": {key: sum(bool((row["before"] or {}).get(key)) for row in ordered) for key in metrics},
               "after": {key: sum(bool((row["after"] or {}).get(key)) for row in ordered) for key in metrics},
               "span_decisions": dict(Counter(span["decision"] for row in ordered
                                               for span in (row["audit"] or {}).get("spans", []))),
               "after_first_errors": dict(Counter(error["code"] for row in ordered
                                                   for error in (row["after"] or {}).get("errors", [])))}
    report = {"schema": "loom.anchor_assist_report/1", "method": METHOD, "rules": RULES,
              "rules_sha256": pilot.digest(RULES), "implementation_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "scoring_code_sha256": frozen, "sources": sources, "summary": summary, "rows": ordered,
              "post_hoc_dev_only": True, "model_calls": 0, "original_responses_modified": False,
              "no_graph_promotion": True}
    pilot.write_new(output, report)
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", action="append", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    report = replay(args.run_dir, args.output)
    print(pilot.canonical(report["summary"]).decode())
