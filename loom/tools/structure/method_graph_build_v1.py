"""Declare this workstream's public offline receipts for the generic exporter."""
from __future__ import annotations
import argparse
from copy import deepcopy
import gzip
import json
from pathlib import Path
from . import method_graph_export_v1 as graph
from . import graph_free_extraction as free
from . import graph_free_schema_hint_v2 as grammar
from . import graph_free_semantic_axes_v3 as semantic
from . import free_evidence_format_projection_v1 as projection

BASE = "docs/research/model_research_2026-10-04/"


def source(path):
    return {"path": path, "sha256": graph.byte_hash((graph.ROOT / path).read_bytes())}


def population(planned, available, unit, note):
    return {"planned": planned, "available": available, "missing": planned - available,
            "unit": unit, "dependent_observations": note}


def scalar(ident, location, note, *, axis="planning", unit="requests", unavailable=False):
    return {"id": ident, "location": location, "value_pointer": location, "axis": axis,
            "method": "unavailable" if unavailable else "reported_scalar", "unit": unit, "note": note}


def ratio(ident, numerator, denominator, note, *, axis="mechanism"):
    common = []
    for left, right in zip(numerator.split("/")[1:], denominator.split("/")[1:]):
        if left != right:
            break
        common.append(left)
    return {"id": ident, "location": "/" + "/".join(common) if common else "",
            "numerator_pointer": numerator,
            "denominator_pointer": denominator, "axis": axis, "method": "counted_ratio",
            "unit": "fraction", "note": note}


def build(output, *, exported_on, followup_directory):
    output = Path(output).resolve()
    output.relative_to(graph.ROOT)
    output.mkdir(parents=True, exist_ok=False)
    methods, runs, event_mapping = [], [], []
    def method(ident, material, params, *, version="1", method_id=None, prompt_hashes=None, extra=None):
        recipe_hash = graph.byte_hash(material.encode()) if isinstance(material, str) else graph.codec.digest(material)
        row = {"id": ident, "method_id": method_id or ident, "version": version,
               "recipe_material": deepcopy(material), "recipe_sha256": recipe_hash,
               "prompt_sha256": deepcopy(prompt_hashes or {}), "parameters": deepcopy(params),
               "preset": {"id": ident + ".retained-default", "version": version, "values": deepcopy(params)},
               "components": [], **deepcopy(extra or {})}
        methods.append(row)
        return ident
    def run(ident, method_ref, path, kind, metrics, pop, when=None, **extra):
        runs.append({"id": ident, "method_ref": method_ref, "source": source(path),
                     "observed_on": when or exported_on, "measurement_kind": kind,
                     "metrics": metrics, "population": pop, **deepcopy(extra)})

    def scripted_group(ident, planned, requests, manifest_path, response_path, result_path,
                       compiler_material, instrument, expected_count):
        manifest = graph.read_json(graph.ROOT / manifest_path)
        responses = graph.read_json(graph.ROOT / response_path)
        results = graph.read_json(graph.ROOT / result_path)
        all_requests = manifest["requests"]
        ids = [r["id"] for r in requests]
        all_ids = [r["id"] for r in all_requests]
        all_row_ids = [r["request_id"] for r in results["rows"]]
        response_hashes = [r["request_sha256"] for r in responses]
        request_hashes = [r["request_sha256"] for r in all_requests]
        if (len(ids) != expected_count or len(ids) != len(set(ids)) or
                len(all_ids) != len(set(all_ids)) or len(all_row_ids) != len(set(all_row_ids)) or
                set(all_ids) != set(all_row_ids) or len(all_requests) != results["requests"] or
                len(response_hashes) != len(set(response_hashes)) or
                set(response_hashes) != set(request_hashes)):
            raise ValueError("scripted_manifest_result_response_population_mismatch")
        actual = method(ident + ".actual_scripted_producer", compiler_material,
                        {"instrument": instrument, "target_configuration": planned,
                         "planned_configuration_executed_by_real_model": False,
                         "selected_request_ids": ids}, version="saved-scripted-projection-v1",
                        method_id="research.scripted_capture_and_compiler",
                        extra={"execution_kind": "scripted_mechanism",
                               "model_quality_measured": False})
        events = []
        for request in requests:
            row_index = all_row_ids.index(request["id"])
            request_index = all_ids.index(request["id"])
            response_index = response_hashes.index(request["request_sha256"])
            events.append({"id": request["id"], "location": "/rows/" + str(row_index),
                           "identity_pointer": "/request_id", "binding": {
                               "prepared_request": {"source": source(manifest_path),
                                                    "location": "/requests/" + str(request_index),
                                                    "matches": [{"pointer": "/id", "equals": request["id"]},
                                                                {"pointer": "/request_sha256", "equals": request["request_sha256"]}]},
                               "scripted_response": {"source": source(response_path),
                                                     "location": "/" + str(response_index),
                                                     "matches": [{"pointer": "/request_sha256", "equals": request["request_sha256"]}]}}})
        metrics = [scalar("model_semantic_quality", "/model_semantic_quality",
                          "Scripted transport has no measured model semantics.", axis="unavailable",
                          unit="fraction", unavailable=True),
                   {"id": "mechanical_validity", "location": "/rows", "records_pointer": "/rows",
                    "complete_count_pointer": "/requests", "identity_pointer": "/request_id",
                    "match": {"pointer": "/request_id", "one_of": ids},
                    "success_pointer": "/mechanical_validity", "method": "counted_ratio",
                    "axis": "mechanism", "unit": "fraction",
                    "note": "Exact concrete planned configuration, actually produced by the saved scripted compiler. All declared request identities retained; no model quality or independent trials."}]
        run(ident + ".scripted", actual, result_path, "scripted_mechanism", metrics,
            population(expected_count, expected_count, "scripted configuration requests",
                       "Matched arms and models reuse authored cases; actual instrument is scripted."),
            intended_method_ref=planned, selected_request_ids=ids, events=events,
            evidence_sources=[source(manifest_path), source(response_path)])
        event_mapping.append({"group": ident, "intended_configuration": planned,
                              "actual_producer": actual, "request_ids": ids,
                              "manifest": source(manifest_path), "results": source(result_path),
                              "responses": source(response_path), "model_quality_measured": False})

    # Stage 1 saved arms, each with a separate exact recipe/configuration.
    for folder in sorted((graph.ROOT / (BASE + "study/prepared")).iterdir()):
        if not folder.is_dir() or not (folder / "manifest.json").exists():
            continue
        path = str((folder / "manifest.json").relative_to(graph.ROOT))
        manifest = graph.read_json(folder / "manifest.json")
        if not manifest.get("requests"):
            continue
        body = manifest["requests"][0].get("body", {})
        if body.get("messages"):
            material = body["messages"][0]["content"]
            params = {k: deepcopy(v) for k, v in body.items() if k != "messages"}
            prompts = {"system": graph.byte_hash(material.encode())}
        else:
            # Jev's recipe is its exact questions map. The state varies by case;
            # retain its full bytes in the source observation, not method identity.
            material = deepcopy(body["questions"])
            params = {k: deepcopy(v) for k, v in body.items() if k not in {"questions", "state"}}
            prompts = {"questions": graph.codec.digest(material)}
            if any(r["body"]["questions"] != material or
                   {k: v for k, v in r["body"].items() if k not in {"questions", "state"}} != params
                   for r in manifest["requests"]):
                raise ValueError("stage1_configuration_or_question_recipe_varies_within_arm")
        ident = method("stage1." + folder.name, material, params, prompt_hashes=prompts,
                       version="432-prepared-2026-10-04", extra={"source_manifest_sha256": source(path)["sha256"]})
        metrics = [scalar("planned_requests", "/max_requests", "Preparation count, not model quality.")] if "max_requests" in manifest else []
        run("stage1.prepared." + folder.name, ident, path, "planning", metrics,
            population(len(manifest["requests"]), 0, "planned requests", "Paired authored DEV; unexecuted."))

    # Stage 2 actual synthetic_dev source packets and native prompt variants.
    stage2_path = BASE + "stage2/prepared/prepared.json"
    stage2 = graph.read_json(graph.ROOT / stage2_path)
    recipes = graph.read_json(graph.ROOT / (BASE + "stage2/recipes.json"))["recipes"]
    recipe_by = {r["id"]: r for r in recipes}
    for row in stage2["methods"]:
        record = row["record"]
        material = recipe_by[record["recipe_id"]]["system_prompt"]
        if graph.byte_hash(material.encode()) != record["prompt_sha256"]:
            raise ValueError("stage2_prompt_bytes_changed")
        params = {k: deepcopy(v) for k, v in record.items() if k not in {"recipe_id", "recipe_version", "prompt_sha256"}}
        ident = method("stage2." + row["id"], material, params, version=row["version"],
                       method_id="research.native_semantic_extraction",
                       prompt_hashes={"system": record["prompt_sha256"]},
                       extra={"original_method_record_hash": row["hash"], "evidence_status": row["evidence_status"]})
        run(ident + ".prepared", ident, stage2_path, "planning", [],
            population(stage2["case_count"], 0, "source packets per method", stage2["population"]))

    # Historical graph extraction and separate syntax decoder preserve the negative.
    extraction_path = BASE + "extraction/replay/REPLAY.json"
    for name, module in (("source_only_v1", free), ("grammar_v2", grammar), ("semantic_v3", semantic)):
        body = module.requests(free.panel.load_dev_inputs())[0]["body"]
        params = {k: deepcopy(v) for k, v in body.items() if k != "messages"}
        ident = method("archive_extraction." + name, module.SYSTEM, params, version=name,
                       prompt_hashes={"system": graph.byte_hash(module.SYSTEM.encode())})
        if name == "source_only_v1":
            run("archive_extraction.first", ident, extraction_path, "historical_actual_model_response_replay",
                [ratio("strict_assertion_recall", "/primary/strict_edges/tp", "/primary/strict_edges/gold",
                       "Frozen strict alignment; not world truth.", axis="model_quality"),
                 ratio("strict_assertion_precision", "/primary/strict_edges/tp", "/primary/strict_edges/predicted",
                       "All alignment FPs retained; representation-dependent lower bound.", axis="model_quality")],
                population(24, 18, "conversations", "Inspected authored DEV, correlated families/translations."), "2026-09-30")
    decoder = method("archive_extraction.exact_evidence_decoder", projection.policy(),
                     {"primary_decoder": "unchanged", "duplicate_keys": "unavailable_no_salvage"})
    run("archive_extraction.decoder_replay", decoder, extraction_path, "historical_actual_model_response_replay",
        [ratio("strict_assertion_recall", "/historical_projection/strict_edges/tp", "/historical_projection/strict_edges/gold",
               "Existing decoder replay, not fresh inference.", axis="historical_decoder_result"),
         ratio("strict_assertion_precision", "/historical_projection/strict_edges/tp", "/historical_projection/strict_edges/predicted",
               "Source-invalid records remain; binding is not semantic support.", axis="historical_decoder_result")],
        population(24, 18, "conversations", "Same saved first responses, no new calls."), "2026-09-30")

    # Old 36-event DEV mechanism run is retained separately; its model labels
    # were unresolved planning placeholders and never become ModelProfiles.
    old_directory = graph.ROOT / (BASE + "frontier/final")
    old_manifest_path = str((old_directory / "manifest.json").relative_to(graph.ROOT))
    old_manifest = graph.read_json(old_directory / "manifest.json")
    for model in old_manifest["config"]["models"]:
        for track, material in old_manifest["config"]["recipes"].items():
            matching = [r for r in old_manifest["requests"] if r["model"]["key"] == model["key"] and r["track"] == track]
            ident = "old_frontier." + model["key"] + "." + track
            planned = method(ident + ".planned", material,
                             {"planned_model": model, "runtime": matching[0]["runtime"],
                              "repetitions": old_manifest["config"]["repetitions"]},
                             version="historical-preparation-v1",
                             prompt_hashes={"system": graph.byte_hash(material.encode())},
                             extra={"execution_kind": "planned_unresolved_model_identity", "model_quality_measured": False})
            scripted_group(ident, planned, matching, old_manifest_path,
                           str((old_directory / "responses.scripted.json").relative_to(graph.ROOT)),
                           str((old_directory / "results.scripted.json").relative_to(graph.ROOT)),
                           {"operation": "saved_scripted_frontier_capture_and_evaluate",
                            "saved_dependency_hashes": old_manifest["dependencies"]},
                           {"kind": "system", "actor": "scripted-comparison-dev", "model": None}, 6)

    # Stages 3/4 retain each concrete recipe/model/provider/parameter combination
    # while separately naming the actual scripted capture/compiler producer.
    followup = Path(followup_directory).resolve()
    descriptors = graph.read_json(followup / "method_graph_descriptors.json")
    for row in descriptors:
        if graph.byte_hash(row["recipe_material"].encode()) != row["recipe_sha256"]:
            raise ValueError("followup_recipe_bytes_changed")
        method_id = row["method_id"]
        ident = method("followup." + row["configured_method_id"], row["recipe_material"], row["parameters"], version=row["version"],
                       method_id=method_id, prompt_hashes=row["prompt_sha256"],
                       extra={"preset": row["preset"], "source_contracts": row["sources"],
                              "execution_kind": "planned_real_model_not_executed", "components": row["components"],
                              "historical_model_evidence": row["historical_model_evidence"]})
        stage = "stage4" if "reply" in method_id else "stage3"
        manifest = graph.read_json(followup / stage / "manifest.json")
        expected = {r["request_id"]: r["request_sha256"] for r in row["prepared_requests"]}
        matching = [r for r in manifest["requests"] if r["id"] in expected]
        if any(r["request_sha256"] != expected[r["id"]] or r["model"]["model"] != row["parameters"]["model"]
               or r["model"]["provider"] != row["parameters"]["provider"] for r in matching):
            raise ValueError("concrete_followup_configuration_binding_mismatch")
        scripted_group(ident, ident, matching,
                       str((followup / stage / "manifest.json").relative_to(graph.ROOT)),
                       str((followup / stage / "responses.json").relative_to(graph.ROOT)),
                       str((followup / stage / "results.json").relative_to(graph.ROOT)),
                       {"operation": stage + "_saved_scripted_capture_and_compiler",
                        "source_contracts": row["sources"], "components": row["components"],
                        "followup_source_sha256": source("loom/tools/structure/frontier_reply_followup_v1.py")["sha256"]},
                       {"kind": "system", "actor": "scripted-comparison-dev" if stage == "stage3" else "scripted-reply-compiler",
                        "model": None if stage == "stage3" else "scripted:authored-synthetic-reference"}, 2)

    all_event_ids = [ident for group in event_mapping for ident in group["request_ids"]]
    if len(all_event_ids) != 60 or len(all_event_ids) != len(set(all_event_ids)):
        raise ValueError("old_and_new_scripted_event_mapping_incomplete_or_duplicate")
    (output / "EVENT_MAPPING.json").write_bytes(graph.model_profiles.encoded(event_mapping))

    protocol_path = BASE + "stage5_protocol.json"
    protocol = graph.read_json(graph.ROOT / protocol_path)
    stage5 = method("stage5.repeat_and_unseen_protocol", protocol, {}, version=protocol["version"],
                    extra={"new_corpus_status": protocol["new_corpus"].get("status"), "evidence_status": "prepared_not_executed"})
    run("stage5.protocol_prepared", stage5, protocol_path, "planning", [],
        population(1, 0, "preregistered protocol", "No measured winner, repeat or unseen corpus exists."))
    projection_method = method("existing_profile_projection", {
        "source": "loom/tools/eval/model_profiles.py", "source_sha256": source("loom/tools/eval/model_profiles.py")["sha256"],
        "operation": "retain_existing_profile_and_rebind_metric_evidence"},
        {"format": "loom.model_profiles/1", "inherit_other_model_versions": False})
    methods_file = output / "METHODS.json"
    methods_file.write_bytes(graph.model_profiles.encoded(methods))
    declaration_source = source(str(methods_file.relative_to(graph.ROOT)))
    configured = [{**row, "declaration_source": declaration_source, "declaration_pointer": "/" + str(i)}
                  for i, row in enumerate(methods)]
    config = {"public_artifacts_only": True, "exported_on": exported_on, "methods": configured, "runs": runs,
              "model_profiles": [{"source": source(BASE + "study/historical_replay/profiles.json"),
                                  "observed_on": exported_on, "method_ref": projection_method}]}
    (output / "configuration.json").write_bytes(graph.model_profiles.encoded(config))
    result = graph.export(config)
    raw_packet = graph.codec.encode_packet(result)
    packet_path = output / "packet.json.gz"
    with packet_path.open("wb") as file:
        with gzip.GzipFile(filename="", mode="wb", fileobj=file, mtime=0) as compressed:
            compressed.write(raw_packet)
    stored_packet = packet_path.read_bytes()
    if gzip.decompress(stored_packet) != raw_packet:
        raise ValueError("method_graph_packet_storage_roundtrip_mismatch")
    receipt = {"schema_reused": graph.codec.PACKET_SCHEMA, "packet_id": result["packet_id"], "methods": len(methods),
               "runs": len(runs), "entities": len(result["entities"]), "claims": len(result["claims"]),
               "sources": len(result["sources"]), "new_provider_calls": 0, "canonical_store_written": False,
               "scripted_event_count": len(all_event_ids), "old_frontier_events": 36,
               "new_stage3_events": 12, "new_stage4_events": 12,
               "scripted_configuration_groups": len(event_mapping),
               "consumer_contract_status": "proposal_pending_threads_3_4",
               "packet_storage": {"path": packet_path.name, "format": "gzip", "mtime": 0, "filename": "",
                                  "compressed_bytes": len(stored_packet), "compressed_sha256": graph.byte_hash(stored_packet),
                                  "decompressed_bytes": len(raw_packet), "decompressed_sha256": graph.byte_hash(raw_packet),
                                  "semantic_change": False},
               "output_sha256": {p.name: graph.byte_hash(p.read_bytes()) for p in sorted(output.iterdir()) if p.is_file()}}
    (output / "VERIFICATION.json").write_bytes(graph.model_profiles.encoded(receipt))
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exported-on", required=True)
    parser.add_argument("--followup-directory", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.output, exported_on=args.exported_on, followup_directory=args.followup_directory)))


if __name__ == "__main__":
    main()
