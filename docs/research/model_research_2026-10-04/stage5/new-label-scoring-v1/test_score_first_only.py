"""Small fake-only protocol controls; preserve the before and each negative input."""
from copy import deepcopy
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("first_only_replay", HERE / "score_first_only.py")
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


def write(path, value):
    raw = replay.canonical(value)
    if path.exists() and path.read_bytes() != raw:
        raise ValueError("fake_control_existing_bytes_changed")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw)
    return replay.sha(raw)


def fake(directory):
    """Two query views, two arms, one fictional source, and four first captures."""
    directory.mkdir(parents=True)
    cfg = deepcopy(replay.parse((HERE / "configuration.json").read_bytes()))
    for item in (cfg["instrument"]["panel"], cfg["instrument"]["key_validator"]):
        item["path"] = str((HERE / item["path"]).resolve())
    case = {"id": "fake_family", "source_id": "fake_source", "language": "en",
            "node_inventory": [{"id": "p", "text": "P."}, {"id": "q", "text": "Q."}],
            "turns": [{"id": "t1", "speaker": "S", "known_at": "2026-01-01T00:00:00Z", "text": "P implies Q."},
                      {"id": "t2", "speaker": "S", "known_at": "2026-01-01T00:01:00Z", "text": "Q does not imply P."}],
            "judgment_queries": [{"id": "positive", "source": "p", "target": "q", "relation": "implies", "attributed_to": "S", "scope": "explicit_source", "as_of": "2026-01-01T00:01:00Z"},
                                 {"id": "negative", "source": "q", "target": "p", "relation": "implies", "attributed_to": "S", "scope": "explicit_source", "as_of": "2026-01-01T00:01:00Z"}]}
    inputs = {"cases": [case]}
    gold = {"cases": [{"id": case["id"], "judgments": [{"query_id": "positive", "label": "supported"}, {"query_id": "negative", "label": "refuted"}]}]}
    cfg["source_inputs"] = {"path": "inputs.json", "raw_sha256": write(directory / "inputs.json", inputs), "cases_pointer": "/cases"}
    cfg["gold"] = {"path": "gold.json", "raw_sha256": write(directory / "gold.json", gold), "cases_pointer": "/cases"}
    cfg["population"] = {"id": "fake_protocol_control", "author_visible_source_and_gold": True, "blind_sealed_holdout": False}
    cfg["authored_inventory"] = {"families": 1, "queries_per_arm": 2, "operations": 4, "all_counts_are_DATA_inventory_not_runtime_caps": True}
    cfg["arms"] = []; cfg["operation_mapping"] = []; cfg["manifest_metadata_bindings"] = {
        "arm_id": "/metadata/arm_id", "query_id": "/metadata/context_preparation/case_id"}
    operations = []; requests = []; rows = []; responses = []
    for arm in ("arm_a", "arm_b"):
        prepared = []
        for query in case["judgment_queries"]:
            payload = {"case_id": case["id"], "source_id": case["source_id"], "turns": case["turns"], "node_inventory": case["node_inventory"], "query": query}
            questions = {"q01": {"type": "noul"}, "q02": {"type": "noul"}}
            state = {"text": replay.canonical(payload).decode()}
            prepared.append({"case_id": query["id"], "language": "en", "state": state, "questions": questions})
            ident = arm + "." + query["id"]
            body = {"model": "fake-model", "provider": {"only": ["fake-provider"]}, "state": state, "questions": questions}
            request_hash = write(directory / "requests" / (ident + ".json"), body)
            metadata = {"arm_id": arm, "prepared_request_id": ident, "source_manifest_sha256": replay.sha(b"fake-prepared-source"), "context_preparation": {"case_id": query["id"]}}
            op = {"operation_id": ident, "route_id": "jev", "model_id": "fake-model", "provider_id": "fake-provider", "request_sha256": request_hash, "request_file": "requests/" + ident + ".json", "metadata": metadata, "units_upper_bounds": {"prompt": 1, "completion": 0, "request": 1}}
            operations.append(op)
            norm_meta = {key: metadata[key] for key in cfg["normalized_metadata_fields"]}
            requests.append({"operation_id": ident, "route_id": "jev", "model_id": "fake-model", "provider_id": "fake-provider", "request_sha256": request_hash, "metadata": norm_meta, "body": body})
            projection = {"id": "generation." + ident, "model": "fake-observed-model", "usage": {"cost": "0.01", "is_byok": False}, "answers": {"q01": {"type": "noul", "noul": 0.9 if query["id"] == "positive" else 0.1}, "q02": {"type": "noul", "noul": 0.1 if query["id"] == "positive" else 0.9}}}
            generation_projection = {"id": projection["id"], "model": "fake-observed-model", "provider_name": "fake-observed-provider", "is_byok": False, "total_cost": "0.01"}
            rh = replay.sha(replay.canonical(projection)); gh = replay.sha(replay.canonical(generation_projection))
            rows.append({"operation_id": ident, "state": "completed", "http_status": 200, "request_sha256": request_hash, "metadata": norm_meta, "generation_id": projection["id"], "response_sha256": rh, "generation_sha256": gh, "requested_model": "fake-model", "requested_provider": "fake-provider", "response_model": "fake-observed-model", "response_provider": "fake-observed-provider", "observed_model": "fake-observed-model", "observed_provider": "fake-observed-provider", "response_available": True, "response_ledger_bound": True, "exact_sent_request_capture_verified": True, "sent_request_capture_sha256": request_hash, "reported_cost_usd": "0.01", "actual_cost_usd": "0.01", "billing_mode": "credits", "billing_verified": True, "billing_replay_verified": True, "is_byok": False})
            responses.append({"operation_id": ident, "response_sha256": rh, "generation_sha256": gh, "projection": projection, "generation_projection": generation_projection})
            cfg["operation_mapping"].append({"operation_id": ident, "arm_id": arm, "query_id": query["id"], "family_id": case["id"], "source_id": case["source_id"], "language": "en", "relation": "implies"})
        cfg["arms"].append({"arm_id": arm, "authored_planned_queries": 2, "prepared_specs": {"path": arm + ".json", "raw_sha256": write(directory / (arm + ".json"), prepared), "query_id_field": "case_id"}})
    manifest = {"schema": "loom.research_programme_manifest/1", "programme_id": "fake_programme", "stage_id": "fake_stage", "operations": operations}
    mh = write(directory / "manifest.json", manifest)
    cfg["paid_manifest"].update(path="manifest.json", raw_sha256=mh, programme_id="fake_programme", stage_id="fake_stage")
    for row in rows:
        row["manifest_sha256"] = mh
    bundle = {"schema": "loom.programme_results/1", "programme_id": "fake_programme", "stage_id": "fake_stage", "manifest_sha256": mh, "planned_operation_ids": [op["operation_id"] for op in operations], "planned_operations": len(operations), "saved_row_count": len(rows), "rows": rows, "responses": responses, "requests": requests}
    return {"directory": directory, "config": cfg, "manifest": manifest, "bundle": bundle, "gold": gold, "inputs": inputs}


def run_case(case):
    directory = case["directory"]
    # Preserve the inputs exactly as attempted, including the mutation before a rejection.
    raw_config = replay.canonical(case["config"])
    raw_manifest = (directory / "manifest.json").read_bytes()
    raw_gold = (directory / "gold.json").read_bytes()
    raw_bundle = replay.canonical(case["bundle"])
    write(directory / "attempted-config.json", case["config"])
    write(directory / "attempted-bundle.json", case["bundle"])
    return replay.score(raw_config, raw_manifest, raw_bundle, raw_gold, base=directory,
                        manifest_base=directory, expected_config_sha256=replay.sha(raw_config),
                        expected_bundle_sha256=replay.sha(raw_bundle))


def arm_report(result, arm="arm_a"):
    return next(g for g in result["groups"] if g["dimensions"] == {"arm_id": arm})


def controls():
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    destination = HERE / "fake-controls" / stamp
    receipts = []
    before = fake(destination / "before")
    baseline = run_case(before)
    assert baseline["planned_operations"] == 4
    assert arm_report(baseline)["quality"]["source_commitment_accuracy"] == 1.0
    assert arm_report(baseline)["cost"] == {"known_verified_usd": "0.02", "unknown_planned_attempts": 0, "complete": True, "total_usd": "0.02"}
    write(destination / "before-score.json", baseline)

    def fatal(name, mutate, expected):
        case = fake(destination / name); mutate(case)
        try:
            run_case(case)
        except replay.ReplayError as error:
            assert str(error) == expected, (name, str(error))
            receipts.append({"control": name, "expected_error": expected, "observed_error": str(error), "metrics_emitted": False, "attempt_inputs_preserved": True})
        else:
            raise AssertionError(name + ": expected rejection")

    fatal("wrong_gold_hash", lambda c: (c["directory"] / "gold.json").write_bytes(b'{"cases":[]}'), "gold_hash_changed")
    fatal("duplicate_first_row", lambda c: c["bundle"]["rows"].append(deepcopy(c["bundle"]["rows"][0])), "duplicate_or_unknown_first_row")

    def duplicate_generation(case):
        case["bundle"]["rows"][1]["generation_id"] = case["bundle"]["rows"][0]["generation_id"]
        case["bundle"]["responses"][1]["projection"]["id"] = case["bundle"]["rows"][0]["generation_id"]
    fatal("duplicate_generation", duplicate_generation, "duplicate_or_invalid_first_generation_identity")

    def duplicate_capture(case):
        value = case["bundle"]["rows"][0]["response_sha256"]
        case["bundle"]["rows"][1]["response_sha256"] = value
        case["bundle"]["responses"][1]["response_sha256"] = value
    fatal("duplicate_first_capture", duplicate_capture, "duplicate_successful_first_capture")

    def wrong_cost(case):
        case["bundle"]["rows"][0]["actual_cost_usd"] = "0.02"
    fatal("verified_cost_mismatch", wrong_cost, "verified_credit_cost_mismatch")

    def source_inventory(case):
        case["inputs"]["cases"][0]["node_inventory"].pop()
        raw = replay.canonical(case["inputs"])
        (case["directory"] / "inputs.json").write_bytes(raw)
        case["config"]["source_inputs"]["raw_sha256"] = replay.sha(raw)
    fatal("missing_source_endpoint", source_inventory, "source_query_inventory_invalid")

    missing = fake(destination / "missing_first")
    missing["bundle"]["rows"].pop(0); missing["bundle"]["responses"].pop(0); missing["bundle"]["saved_row_count"] -= 1
    result = run_case(missing); group = arm_report(result)
    assert group["quality"]["availability"] == {"query_count": 2, "available": 1, "unavailable": 1, "coverage": 0.5}
    assert group["quality"]["source_commitment_accuracy"] == 0.5
    assert group["cost"]["known_verified_usd"] == "0.01" and group["cost"]["unknown_planned_attempts"] == 1 and group["cost"]["total_usd"] is None
    receipts.append({"control": "missing_first", "planned_denominator": 2, "available": 1, "unavailable": 1, "unknown_cost_attempts": 1, "replacement_used": False})
    write(destination / "missing-first-score.json", result)

    invalid = fake(destination / "invalid_q02")
    invalid["bundle"]["responses"][0]["projection"]["answers"]["q02"]["noul"] = "invalid"
    invalid_hash = replay.sha(replay.canonical(invalid["bundle"]["responses"][0]["projection"]))
    invalid["bundle"]["responses"][0]["response_sha256"] = invalid_hash
    invalid["bundle"]["rows"][0]["response_sha256"] = invalid_hash
    result = run_case(invalid); group = arm_report(result)
    assert group["quality"]["availability"]["query_count"] == 2 and group["quality"]["availability"]["unavailable"] == 1
    assert group["quality"]["source_commitment_accuracy"] == 0.5
    assert result["records"][0]["label_error"] == "invalid_first_source_label_answer"
    assert group["cost"]["known_verified_usd"] == "0.02" and group["cost"]["complete"] is True
    receipts.append({"control": "invalid_q02", "planned_denominator": 2, "available": 1, "unavailable": 1, "first_probability_preserved": "invalid", "known_verified_cost_independent_of_label": "0.02", "replacement_used": False})
    write(destination / "invalid-q02-score.json", result)
    receipt = {"schema": "loom.first_source_label_score.fake_controls/1", "before_directory": str(destination / "before"), "control_directory": str(destination), "baseline_known_cost_usd_each_arm": "0.02", "controls": receipts, "control_count": len(receipts), "result": "pass", "fake_only": True, "real_response_or_ledger_or_key_data_read": False, "model_calls": 0, "network_calls": 0}
    write(destination / "receipt.json", receipt)
    print(json.dumps({"result": "pass", "fake_controls": len(receipts), "receipt": str(destination / "receipt.json"), "receipt_sha256": replay.sha(replay.canonical(receipt))}))
    return receipt


if __name__ == "__main__":
    controls()
