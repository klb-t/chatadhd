"""Offline extraction preparation and immutable first-response replay; no transport.

Contract, semantic reference alignment and source binding are separate axes.
Historical syntax projections never become responses to a different prompt.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from decimal import Decimal
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tempfile
import zipfile

try:
    from . import graph_free_extraction as free
    from . import graph_free_schema_hint_v2 as grammar
    from . import graph_free_semantic_axes_v3 as semantic
    from . import free_evidence_format_projection_v1 as projection
except ImportError:
    import graph_free_extraction as free
    import graph_free_schema_hint_v2 as grammar
    import graph_free_semantic_axes_v3 as semantic
    import free_evidence_format_projection_v1 as projection

panel, safe = free.panel, free.safe
SOURCE_COMMIT = "af3c81a5ddce70bdf4346a2c8c808ce6cc97b4d0"
ARCHIVE_PATH = "docs/research/graph_free_extraction_v1/first_evidence.zip"
ARCHIVE_SHA256 = "5571ab4ff59c85b23bd32f9733df68495155998666203394120a1b99632530ef"
PREFIX = "docs/research/graph_free_extraction_v1/"
PROJECTION_PATH = "docs/research/recipe_experiments_2026-09-30/evidence_format_projection_v1/"
METRICS = ("strict_edges", "strict_reference_atom_alignment", "strict_status_events")
ARMS = (("source_only_v1", free), ("grammar_v2", grammar), ("semantic_v3", semantic))


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def git_blob(path, commit=SOURCE_COMMIT):
    """Read only explicit public research paths from an immutable commit."""
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("immutable_commit_required")
    allowed = {ARCHIVE_PATH, PROJECTION_PATH + "score_first.json",
               PROJECTION_PATH + "SECONDARY_SOURCE_AUDIT.json"}
    if path not in allowed:
        raise ValueError("research_path_not_allowed")
    return subprocess.check_output(["git", "show", f"{commit}:{path}"], cwd=panel.ROOT)


def archive_payloads(raw, expected_sha256=ARCHIVE_SHA256):
    if sha(raw) != expected_sha256:
        raise ValueError("first_evidence_archive_hash_mismatch")
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError("duplicate_archive_member")
        closure = {"inventory.json", "docs/research/model_method_panel_v1/public_preflight/gpt41mini_endpoints.json"}
        closure.update("loom/tools/structure/" + name for name in (
            "graph_free_extraction.py", "graph_panel_live.py", "graph_panel_score_run.py",
            "jev_live_pilot.py", "openrouter_runner.py", "test_graph_free_extraction.py"))
        closure.update("loom/tests/fixtures/research/graph_methods_panel_v1/" + name
                       for name in ("inputs_dev.json", "gold_dev.json", "manifest.json"))
        for name in names:
            path = PurePosixPath(name)
            if ((not name.startswith(PREFIX) and name not in closure) or path.is_absolute() or ".." in path.parts
                    or "\\" in name or str(path) != name):
                raise ValueError("unsafe_archive_member")
        return {name: archive.read(name) for name in names}


def validate_fixture_snapshot(payloads, current_directory=None):
    """Compare exact fixture bytes before replay; aggregate ties cannot hide drift.

    Gold bytes are hashed without parsing labels. Labels are loaded for scoring
    only after first compiled outputs have been persisted.
    """
    directory = Path(current_directory) if current_directory is not None else panel.FIXTURE
    prefix = "loom/tests/fixtures/research/graph_methods_panel_v1/"
    hashes = {}
    for name in ("inputs_dev.json", "gold_dev.json", "manifest.json"):
        archived = payloads[prefix + name]
        current = (directory / name).read_bytes()
        if current != archived:
            raise ValueError("historical_fixture_bytes_changed:" + name)
        hashes[name] = sha(archived)
    return hashes


def prepare_rows(cases, request_options=None):
    """Source-only three-arm preview. Gold/reference inventory never enters bodies.

    Options are caller data; messages cannot be overridden because they define
    the source-only intervention. Historical defaults are preserved presets.
    This function has no execution path and current price authority is absent.
    """
    options = deepcopy({} if request_options is None else request_options)
    if not isinstance(options, dict) or "messages" in options:
        raise ValueError("source_only_messages_are_the_experiment_contract")
    if len({c["id"] for c in cases}) != len(cases):
        raise ValueError("duplicate_case_id")
    rows = []
    for arm_id, module in ARMS:
        for row in module.requests(cases):
            body = deepcopy(row["body"])
            body.update(options)
            source = safe.parse_json(body["messages"][1]["content"])
            case = next(c for c in cases if c["id"] == row["id"])
            if source != free.source_payload(case):
                raise ValueError("source_only_payload_drift")
            rows.append({"id": f"{arm_id}:{row['id']}", "arm": arm_id,
                         "case_id": row["id"], "body": body,
                         "body_sha256": safe.digest(body),
                         "source_payload_sha256": safe.digest(source),
                         "system_sha256": sha(body["messages"][0]["content"].encode()),
                         "historical_reservation_usd": (
                             safe.estimate_reservation(body)["minimum_reservation_usd"]
                             if not options else None)})
    return rows


def prepare(output, request_options=None):
    cases = panel.load_dev_inputs()  # Inputs only, no gold loader.
    rows = prepare_rows(cases, request_options)
    folder = Path(output)
    folder.mkdir(parents=True, exist_ok=False)
    panel.write_new(folder / "requests_preview.json", rows)
    by_arm = {}
    for arm_id, _ in ARMS:
        arm = [r for r in rows if r["arm"] == arm_id]
        estimates = [r["historical_reservation_usd"] for r in arm]
        by_arm[arm_id] = {
            "planned_cases": len(arm), "live_calls_in_this_preparation": 0,
            "historical_reservation_usd": (
                str(sum((Decimal(v) for v in estimates), Decimal(0)))
                if all(v is not None for v in estimates) else None)}
    summary = {
        "schema": "loom.extraction_readiness.preparation/1",
        "execution_kind": "offline_request_preview_not_execution_manifest",
        "prepared_requests": len(rows), "cases_per_arm": len(cases), "arms": by_arm,
        "request_options": deepcopy(request_options or {}),
        "request_provenance": "reconstructed_proposed_requests_not_transmission_records",
        "gold_read": False, "validation_read": False, "new_provider_calls": 0,
        "price_authority": "historical_byte_based_reservation_not_current_quote",
        "live_execution_requires_owner_approval": True,
        "account_identity_cost_and_reservations_must_be_reconciled": True,
        "shared_budget_is_not_reset": True,
        "not_included_in_the_432_request_study": True,
        "first_outputs_required_before_gold_loading": True,
        "request_preview_sha256": panel.digest_file(folder / "requests_preview.json"),
        "files_sha256": {str(Path(m.__file__).relative_to(panel.ROOT)):
                           panel.digest_file(m.__file__) for _, m in ARMS}}
    panel.write_new(folder / "PREPARATION.json", summary)
    return summary


def provenance_axis(cases, outputs):
    """Verify bytes, exact source-time binding and recomputability, not semantics."""
    by_case = {c["id"]: c for c in cases}
    seen = set()
    complete = assertions = events = checks = 0
    for output in outputs:
        ident = output["case_id"]
        if ident in seen or ident not in by_case:
            raise ValueError("provenance_case_inventory_drift")
        seen.add(ident)
        if output["state"] != "completed":
            continue
        case = free.source_payload(by_case[ident])
        if output != free.compile_free(output["raw_model_object"], case):
            raise ValueError("compiled_or_source_provenance_drift")
        complete += 1
        assertions += len(output["source_assertions"])
        events += len(output["status_events"])
        for record in output["source_assertions"] + output["status_events"]:
            panel._validated_compiled_evidence(record, case)
            checks += 1
    if seen != set(by_case):
        raise ValueError("missing_planned_provenance_case")
    return {"planned_cases": len(cases), "source_hash_and_recompile_checked_cases": complete,
            "accepted_assertions": assertions, "accepted_status_events": events,
            "exact_quote_span_and_source_time_checked_records": checks,
            "missing_or_unavailable_cases_retained": len(cases) - complete,
            "source_clause_semantics_proven": False,
            "actor_authority_or_world_truth_proven": False}


def replay(output):
    """Replay actual first responses, preserve primary and derived decoder separately."""
    raw = git_blob(ARCHIVE_PATH)
    files = archive_payloads(raw)
    fixture_hashes = validate_fixture_snapshot(files)
    cases = panel.load_dev_inputs()
    folder = Path(output)
    folder.mkdir(parents=True, exist_ok=False)
    receipt = {"schema": "loom.extraction_readiness.archive_receipt/1",
               "source_commit": SOURCE_COMMIT, "archive_path": ARCHIVE_PATH,
               "archive_sha256": sha(raw), "member_count": len(files),
               "exact_historical_fixture_sha256": fixture_hashes,
               "member_sha256": {name: sha(data) for name, data in files.items()},
               "new_provider_calls": 0, "validation_read": False}
    panel.write_new(folder / "ARCHIVE_RECEIPT.json", receipt)
    originals, summaries = [], []
    with tempfile.TemporaryDirectory(prefix="loom-extraction-replay-") as temporary:
        root = Path(temporary)
        # Archive code is never executed. Only immutable data is replayed.
        for name, data in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        for batch in ("batch01", "batch02"):
            source = root / PREFIX / "prepared" / batch
            outputs, summary = free.load_run(source / "prepared/manifest.json", source / "run", cases)
            if outputs != json.loads((source / "first_score/compiled_first.json").read_bytes()):
                raise ValueError("original_first_compilation_changed")
            originals.extend(outputs)
            summaries.append(summary)
    if len(originals) != len(cases):
        raise ValueError("planned_first_inventory_changed")
    by_case = {c["id"]: c for c in cases}
    projected, conversions = [], []
    for original in originals:
        derived, conversion = projection.project(original, free.source_payload(by_case[original["case_id"]]))
        projected.append(derived)
        conversions.append(conversion)
    # Persist all source-only outcomes before consulting the DEV reference labels.
    panel.write_new(folder / "compiled_original_replay.json", originals)
    panel.write_new(folder / "compiled_projection_replay.json", projected)
    panel.write_new(folder / "projection_receipts_replay.json", conversions)
    provenance = {"primary": provenance_axis(cases, originals),
                  "historical_projection": provenance_axis(cases, projected)}
    panel.write_new(folder / "PROVENANCE.json", provenance)
    gold = panel.load_dev_gold()
    primary_score = free.score_free(cases, gold, originals)
    projection_score = free.score_free(cases, gold, projected)
    historical_projection = json.loads(git_blob(PROJECTION_PATH + "score_first.json"))
    if projection_score != historical_projection:
        raise ValueError("historical_projection_score_changed")
    # Aggregate original archived first scores using every planned batch denominator.
    for metric in METRICS:
        prior = [json.loads(files[PREFIX + f"prepared/{batch}/first_score/score_first.json"])[metric]
                 for batch in ("batch01", "batch02")]
        combined = panel._metric(*(sum(p[k] for p in prior) for k in ("tp", "fp", "fn")))
        if primary_score[metric] != combined:
            raise ValueError("primary_first_score_changed")
    manual = json.loads(files[PREFIX + "manual_source_audit2.json"])
    secondary = json.loads(git_blob(PROJECTION_PATH + "SECONDARY_SOURCE_AUDIT.json"))
    result = {
        "schema": "loom.extraction_readiness.replay/1",
        "execution_kind": "offline_replay_existing_actual_first_responses_and_decoder",
        "planned_cases": len(cases), "original_first_compilations_equal": len(originals),
        "exact_historical_fixture_sha256": fixture_hashes,
        "primary": {k: primary_score[k] for k in METRICS},
        "historical_projection": {k: projection_score[k] for k in METRICS},
        "provenance": provenance,
        "evidence_items_converted": sum(r["evidence_items_converted"] for r in conversions),
        "original_model_objects_and_unavailability_preserved": True,
        "previously_accepted_records_preserved": True,
        "original_usage_reported_cost_usd": str(sum(
            (Decimal(s["reported_known_cost_usd"]) for s in summaries), Decimal(0))),
        "historical_manual_audit": {
            "reviewed_raw_assertion_records": manual["reviewed_assertion_records"],
            "source_classes": manual["assertion_source_classes"],
            "citation_time_bindings": manual["assertion_exact_citation_known_at_bindings"],
            "fixture_author_not_blind": True, "not_recall_against_60_reference_edges": True},
        "historical_projected_accepted_manual_classes": secondary["accepted_assertion_saved_manual_classes"],
        "syntax_projection_still_accepts_source_invalid_assertions": True,
        "new_provider_calls": 0, "new_cost_usd": "0", "new_model_quality_gain": False,
        "grammar_v2_or_semantic_v3_live_quality_measured": False,
        "primary_gates_unchanged": True, "no_graph_promotion": True, "validation_read": False}
    panel.write_new(folder / "primary_score_replay.json", primary_score)
    panel.write_new(folder / "projection_score_replay.json", projection_score)
    panel.write_new(folder / "REPLAY.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    preparing = commands.add_parser("prepare")
    preparing.add_argument("--output", type=Path, required=True)
    preparing.add_argument("--request-options", type=Path,
                           help="Optional JSON request options; source messages stay bound")
    replaying = commands.add_parser("replay")
    replaying.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "prepare":
        options = safe.parse_json(args.request_options.read_bytes()) if args.request_options else None
        result = prepare(args.output, options)
    else:
        result = replay(args.output)
    print(safe.canonical(result).decode())


if __name__ == "__main__":
    main()
