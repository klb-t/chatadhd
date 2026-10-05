"""Export declared offline research receipts into the existing GraphPacket/1.

Native records and exact ModelProfile metric fields are reused. Method/run
identity is data in native entity attrs; no new production/profile schema,
transport, paid call, automatic routing or canonical write is introduced.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from datetime import date
import hashlib
import json
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker

try:
    from .agentic_graph_v1 import packet as codec
    from ..eval import model_profiles
except ImportError:
    from agentic_graph_v1 import packet as codec
    from loom.tools.eval import model_profiles

ROOT = Path(__file__).resolve().parents[3]
PROFILE_SCHEMA = ROOT / "docs/contracts/model_profile.schema.json"
DEFAULT_VOCABULARY = Path(__file__).with_name("method_graph_vocabulary_v1.json")


def read_json(path):
    return model_profiles.load(path)


def byte_hash(raw):
    return hashlib.sha256(raw).hexdigest()


def identifier(kind, value):
    return kind + ":" + codec.digest(value)


def pointer(value, location):
    if location == "":
        return value
    if not isinstance(location, str) or not location.startswith("/"):
        raise ValueError("json_pointer_required")
    current = value
    for segment in location[1:].split("/"):
        # Validate RFC 6901 escapes before unescaping.
        remaining = segment.replace("~0", "").replace("~1", "")
        if "~" in remaining:
            raise ValueError("invalid_json_pointer_escape")
        key = segment.replace("~1", "/").replace("~0", "~")
        if isinstance(current, list):
            if not key.isdigit() or (len(key) > 1 and key[0] == "0"):
                raise ValueError("json_pointer_array_index_invalid")
            current = current[int(key)]
        else:
            current = current[key]
    return current


def validate_shared(value, field):
    """Use the existing ModelProfile field definition, without a parallel schema."""
    schema = read_json(PROFILE_SCHEMA)
    fragment = schema["$defs"]["profile"]["properties"][field]
    errors = sorted(Draft202012Validator(fragment, format_checker=FormatChecker()).iter_errors(value),
                    key=lambda e: str(list(e.path)))
    if errors:
        raise ValueError("shared_model_profile_" + field + ":" + errors[0].message)
    return value


def date_text(value):
    if not isinstance(value, str) or date.fromisoformat(value).isoformat() != value:
        raise ValueError("exact_observation_date_required")
    return value


def source_record(path, raw, observed_on):
    text = raw.decode("utf-8")
    ident = identifier("observation", {"path": path, "sha256": byte_hash(raw)})
    locator = {"source": "sha256:" + byte_hash(raw), "member": path,
               "json_pointer": "", "byte_start": 0, "byte_len": len(raw),
               "time_start": None, "time_end": None, "line": None}
    return {"observation": {"id": ident, "unit": "receipt:" + byte_hash(raw),
                            "kind": "field", "text": text, "locator": locator,
                            "lang": "", "date": observed_on, "ordinal": 0,
                            "artifact_type": "research_receipt", "speaker": "recorded-research-artifact",
                            "attrs": {"path": path, "raw_sha256": byte_hash(raw),
                                      "source_authenticity": "public_saved_artifact_not_provider_invoice"}},
            "known_at": None, "text_sha256": byte_hash(raw)}


def entity(kind, identity, label, observed_on, attrs, parent=""):
    ident = identifier(kind, identity)
    return {"id": ident, "kind": kind, "canonical_key": ident, "label": label,
            "labels": {}, "aliases": [], "parent": parent, "first_seen": observed_on,
            "last_seen": observed_on, "evidence_class": "derived", "origin": "repo",
            "confidence": 1, "status": "active", "attrs": deepcopy(attrs)}


def json_span(text, location):
    """Locate exact original JSON value bytes through an RFC 6901 pointer."""
    keys = [] if location == "" else location[1:].split("/")
    keys = [k.replace("~1", "/").replace("~0", "~") for k in keys]
    decoder = json.JSONDecoder()
    def whitespace(index):
        while index < len(text) and text[index] in " \t\r\n":
            index += 1
        return index
    def locate(index, remaining):
        index = whitespace(index)
        if not remaining:
            return index, decoder.raw_decode(text, index)[1]
        opening = text[index]
        if opening not in "{[":
            raise ValueError("json_span_noncontainer")
        index = whitespace(index + 1)
        ordinal = 0
        while text[index] not in "}]":
            if opening == "{":
                key, index = decoder.raw_decode(text, index)
                index = whitespace(index)
                if text[index] != ":":
                    raise ValueError("json_span_colon_required")
                index = whitespace(index + 1)
            else:
                key = str(ordinal)
            if key == remaining[0]:
                return locate(index, remaining[1:])
            index = whitespace(decoder.raw_decode(text, index)[1])
            ordinal += 1
            if text[index] == ",":
                index = whitespace(index + 1)
            else:
                break
        raise ValueError("json_span_pointer_not_found")
    start, end = locate(0, keys)
    return start, end


def claim(subject, predicate, *, value=None, object_id="", source, location,
          observed_on, method_version, run_id, measurement_kind, extra=None):
    # Every claim concerns the saved report/declared method relationship only.
    locator = deepcopy(source["observation"]["locator"])
    text = source["observation"]["text"]
    anchor = location
    # A structural run link cites the report's schema, while its evidence
    # pointer still names the full report. No assertion of provider causality.
    if not anchor and isinstance(json.loads(text), dict) and "schema" in json.loads(text):
        anchor = "/schema"
    start, end = json_span(text, anchor)
    quote = text[start:end]
    locator["byte_start"] = len(text[:start].encode("utf-8"))
    locator["byte_len"] = len(quote.encode("utf-8"))
    assessment = {"basis": {"support": [{"observation": source["observation"]["id"],
                                           "locator": locator,
                                           "quote": quote,
                                           "extractor": "method_graph_export_v1", "quality": 1}],
                            "derivation": {"operator": "research_receipt_projection",
                                           "operator_version": 1, "morphism": "", "depth": 0}},
                  "evidence_class": "derived", "origin": "repo", "confidence": 1,
                  "premises": {"claims": [], "principles": [], "assumptions": []},
                  "counter": {"observations": [], "claims": []}, "status": "active",
                  "consequences": {"claims": [], "predictions": [], "checks": []},
                  "open": {"slots": [], "questions": [], "fill_query": None},
                  "expected_property": None, "check_state": "n/a", "alternatives": []}
    result = {"subject": subject, "predicate": predicate, "object": object_id,
              "value": deepcopy(value),
              "qualifiers": {"valid_from": observed_on, "valid_to": "", "version": method_version,
                             "branch": "", "scope": "declared_saved_research_receipt", "lang": "",
                             "extra": {"run_id": run_id, "measurement_kind": measurement_kind,
                                       "evidence": {"source_id": source["observation"]["id"], "location": location},
                                       "observed_on": observed_on,
                                       "confidence_scope": "exact_export_structure_and_saved_report_binding_only",
                                       "automatic_promotion": False, **deepcopy(extra or {})}},
              "assessment": assessment}
    result["id"] = identifier("claim", result)
    return result


def make_metric(spec, receipt, source):
    evidence = {"source_id": source["observation"]["id"], "location": spec["location"]}
    if "metric_pointer" in spec:
        metric = deepcopy(pointer(receipt, spec["metric_pointer"]))
        metric["evidence"] = evidence
    elif spec["method"] == "counted_ratio":
        if "records_pointer" in spec:
            records = pointer(receipt, spec["records_pointer"])
            if not isinstance(records, list) or len(records) != pointer(receipt, spec["complete_count_pointer"]):
                raise ValueError("incomplete_saved_record_denominator")
            ids = [pointer(r, spec["identity_pointer"]) for r in records]
            if len(ids) != len(set(ids)):
                raise ValueError("duplicate_saved_record_identity")
            if "one_of" in spec["match"]:
                if spec["match"]["pointer"] != spec["identity_pointer"]:
                    raise ValueError("planned_identity_selection_must_use_identity_pointer")
                expected_ids = spec["match"]["one_of"]
                if (not isinstance(expected_ids, list) or not expected_ids or
                        any(not isinstance(v, str) for v in expected_ids) or
                        len(expected_ids) != len(set(expected_ids))):
                    raise ValueError("declared_selection_ids_invalid")
                selected = [r for r in records if pointer(r, spec["match"]["pointer"]) in expected_ids]
                # Planned identities remain in the denominator when a receipt is missing.
                d = len(expected_ids)
            else:
                selected = [r for r in records if pointer(r, spec["match"]["pointer"]) == spec["match"]["equals"]]
                d = len(selected)
            results = [pointer(r, spec["success_pointer"]) for r in selected]
            if any(type(v) is not bool for v in results):
                raise ValueError("boolean_record_success_required")
            n = sum(results)
        else:
            n = pointer(receipt, spec["numerator_pointer"])
            d = pointer(receipt, spec["denominator_pointer"])
        if type(n) is not int or type(d) is not int or n < 0 or d < 0 or n > d:
            raise ValueError("invalid_receipt_ratio_counts")
        metric = model_profiles.metric(n / d if d else None, n, d, "counted_ratio",
                                       evidence["source_id"], evidence["location"], spec["note"], spec["unit"])
    else:
        value = pointer(receipt, spec["value_pointer"])
        if spec["method"] == "unavailable" and value is not None:
            raise ValueError("unavailable_metric_cannot_have_a_measured_value")
        metric = model_profiles.metric(value, None, None, spec["method"], evidence["source_id"],
                                       evidence["location"], spec["note"], spec["unit"])
    validate_shared({spec["id"]: metric}, "metrics")
    return metric


def export(config, *, root=ROOT, vocabulary=None):
    """Only explicitly declared files are read; no discovery or external calls."""
    if config.get("public_artifacts_only") is not True:
        raise ValueError("explicit_public_artifact_declaration_required")
    vocabulary = read_json(DEFAULT_VOCABULARY) if vocabulary is None else deepcopy(vocabulary)
    kinds, predicates = vocabulary["entity_kinds"], vocabulary["predicates"]
    observed_on = date_text(config["exported_on"])
    entities, claims, sources, source_cache = {}, [], {}, {}
    methods = {}
    def add_entity(item):
        if item["id"] in entities and entities[item["id"]] != item:
            raise ValueError("method_record_identity_collision")
        entities[item["id"]] = item
        return item["id"]

    def get_source(spec, source_date):
        path = spec["path"]
        recorded_on = date_text(spec.get("recorded_on", observed_on))
        cache_key = (path, spec["sha256"], recorded_on)
        if cache_key in source_cache:
            return source_cache[cache_key]
        candidate = Path(root) / path
        if Path(path).is_absolute() or ".." in Path(path).parts or not candidate.resolve().is_relative_to(Path(root).resolve()):
            raise ValueError("source_path_outside_declared_root")
        raw = candidate.read_bytes()
        if byte_hash(raw) != spec["sha256"]:
            raise ValueError("declared_receipt_sha256_drift")
        # Receipt capture date is distinct from the historical measurement date.
        result = source_record(path, raw, recorded_on)
        ident = result["observation"]["id"]
        if ident in sources and sources[ident] != result:
            raise ValueError("same_receipt_declared_with_conflicting_observation_dates")
        sources[ident] = result
        source_cache[cache_key] = (read_json(candidate), result)
        return source_cache[cache_key]

    for descriptor in config["methods"]:
        method_id = descriptor["method_id"]
        declaration, source = get_source(descriptor["declaration_source"], observed_on)
        saved_declaration = pointer(declaration, descriptor.get("declaration_pointer", ""))
        for field in ("method_id", "version", "recipe_sha256", "prompt_sha256", "parameters", "preset", "components"):
            if field in descriptor and saved_declaration.get(field) != descriptor[field]:
                raise ValueError("method_declaration_source_mismatch:" + field)
        recipe = descriptor["recipe_material"]
        expected_hash = byte_hash(recipe.encode("utf-8")) if isinstance(recipe, str) else codec.digest(recipe)
        if descriptor["recipe_sha256"] != expected_hash:
            raise ValueError("recipe_material_sha256_drift")
        base = entity(kinds["method"], {"method_id": method_id}, method_id, "",
                      {"method_id": method_id, "confidence_scope": "declared_method_identity"})
        base_id = add_entity(base)
        version_attrs = {k: deepcopy(v) for k, v in descriptor.items()
                         if k not in {"id", "declaration_source", "declaration_pointer", "parameters", "preset"}}
        version_attrs["confidence_scope"] = "declared_method_version_and_content_hashes"
        # All declared version content participates in identity; same human
        # version labels cannot collapse differing metadata/recipe snapshots.
        version_identity = deepcopy(version_attrs)
        version = entity(kinds["method_version"], version_identity,
                         method_id + "@" + descriptor["version"], observed_on, version_attrs, base_id)
        version_id = add_entity(version)
        preset = deepcopy(descriptor.get("preset", {"id": "unspecified", "version": None, "values": {}}))
        parameters = deepcopy(descriptor.get("parameters", {}))
        preset_id = add_entity(entity(kinds["parameter_preset"], preset, preset["id"], observed_on, preset))
        combination_identity = {"method_version": version_id, "parameters": parameters,
                                "preset": preset, "components": descriptor.get("components", [])}
        configured_id = add_entity(entity(kinds["configured_method"], combination_identity,
                                          method_id + " / " + preset["id"], observed_on,
                                          {**combination_identity, "parameters_sha256": codec.digest(parameters)}, version_id))
        for subject, predicate, object_id in ((version_id, predicates["version_of"], base_id),
                                               (configured_id, predicates["uses_version"], version_id),
                                               (configured_id, predicates["uses_preset"], preset_id)):
            claims.append(claim(subject, predicate, object_id=object_id, source=source,
                                location=descriptor.get("declaration_pointer", ""), observed_on=observed_on,
                                method_version=descriptor["version"], run_id="", measurement_kind="declared_method_configuration"))
        if descriptor["id"] in methods:
            raise ValueError("duplicate_method_descriptor_id")
        methods[descriptor["id"]] = (configured_id, descriptor)

    for run in config["runs"]:
        configured_id, method = methods[run["method_ref"]]
        run_date = date_text(run["observed_on"])
        receipt, source = get_source(run["source"], run_date)
        population = validate_shared(deepcopy(run["population"]), "population")
        validity = validate_shared({"observed_on": run_date, "expires_on": None,
                                    "applicability": "historical_recipe_and_population_only",
                                    "inherit_to_other_version": False}, "validity")
        run_identity = {"run_id": run["id"], "configured_method": configured_id,
                        "source_sha256": run["source"]["sha256"],
                        "measurement_kind": run["measurement_kind"],
                        "intended_method_ref": run.get("intended_method_ref")}
        supporting_sources = []
        for declaration in run.get("evidence_sources", []):
            _, supporting = get_source(declaration, run_date)
            supporting_sources.append({"observation": supporting["observation"]["id"],
                                       "sha256": declaration["sha256"], "path": declaration["path"]})
        run_id = add_entity(entity(kinds["run"], run_identity, run["id"], run_date,
                                   {**run_identity, "population": population, "validity": validity,
                                    "receipt_source": {"observation": source["observation"]["id"],
                                                       "sha256": run["source"]["sha256"]}, "new_model_calls": 0,
                                    "supporting_sources": supporting_sources,
                                    "selected_request_ids": deepcopy(run.get("selected_request_ids", [])),
                                    "automatic_promotion": False}))
        claims.append(claim(run_id, predicates["produced_by"], object_id=configured_id,
                            source=source, location="", observed_on=run_date, method_version=method["version"],
                            run_id=run_id, measurement_kind=run["measurement_kind"],
                            extra={"consumer_contract_status": "proposal_pending_threads_3_4"}))
        if run.get("intended_method_ref"):
            intended_id, intended_method = methods[run["intended_method_ref"]]
            claims.append(claim(run_id, predicates["intended_configuration"], object_id=intended_id,
                                source=source, location="", observed_on=run_date,
                                method_version=intended_method["version"], run_id=run_id,
                                measurement_kind=run["measurement_kind"],
                                extra={"configuration_executed_by_real_model": False}))
        seen_event_ids = set()
        for event in run.get("events", []):
            if event["id"] in seen_event_ids:
                raise ValueError("duplicate_run_event_identity")
            seen_event_ids.add(event["id"])
            row = pointer(receipt, event["location"])
            if pointer(row, event["identity_pointer"]) != event["id"]:
                raise ValueError("run_event_source_identity_mismatch")
            bindings = {}
            for role, declaration in event.get("binding", {}).items():
                bound_receipt, bound_source = get_source(declaration["source"], run_date)
                bound_record = pointer(bound_receipt, declaration["location"])
                for expected in declaration.get("matches", []):
                    if pointer(bound_record, expected["pointer"]) != expected["equals"]:
                        raise ValueError("event_request_response_binding_mismatch")
                bindings[role] = {**deepcopy(declaration),
                                  "observation": bound_source["observation"]["id"]}
            event_identity = {"run_id": run_id, "event_id": event["id"],
                              "receipt_source_sha256": run["source"]["sha256"]}
            event_id = add_entity(entity(kinds["event"], event_identity, event["id"], run_date,
                                         {**event_identity, "recorded_result": deepcopy(row),
                                          "request_and_response_binding": bindings,
                                          "new_model_calls": 0, "automatic_promotion": False}))
            for predicate, object_id in ((predicates["produced_by"], configured_id),
                                          (predicates["event_of"], run_id)):
                claims.append(claim(event_id, predicate, object_id=object_id,
                                    source=source, location=event["location"], observed_on=run_date,
                                    method_version=method["version"], run_id=run_id,
                                    measurement_kind=run["measurement_kind"]))
        if run.get("selected_request_ids") is not None and seen_event_ids != set(run["selected_request_ids"]):
            raise ValueError("run_events_do_not_cover_selected_request_ids")
        for spec in run["metrics"]:
            if spec["axis"] == "model_quality" and run["measurement_kind"] != "historical_actual_model_response_replay":
                raise ValueError("mechanism_or_planning_cannot_be_model_quality")
            # Explicit contrary receipt evidence wins over the declaration.
            if (spec["axis"] == "model_quality" and
                    ("scripted" in str(receipt.get("execution_kind", "")) or
                     receipt.get("model_quality_measured") is False)):
                raise ValueError("scripted_receipt_cannot_be_model_quality")
            pointer(receipt, spec["location"])
            metric = make_metric(spec, receipt, source)
            evaluation_identity = {"run_id": run_id, "metric_id": spec["id"],
                                   "metric": metric, "metric_specification": spec}
            evaluation_id = add_entity(entity(kinds["evaluation"], evaluation_identity,
                                              run["id"] + "/" + spec["id"], run_date,
                                              {**evaluation_identity, "configured_method": configured_id,
                                               "measurement_axis": spec["axis"], "population": population,
                                               "validity": validity, "automatic_promotion": False}))
            for predicate, object_id in ((predicates["produced_by"], configured_id),
                                          (predicates["evaluates_run"], run_id)):
                claims.append(claim(evaluation_id, predicate, object_id=object_id,
                                    source=source, location=spec["location"], observed_on=run_date,
                                    method_version=method["version"], run_id=run_id,
                                    measurement_kind=run["measurement_kind"]))
            claims.append(claim(evaluation_id, predicates["evaluation"], value=metric,
                                source=source, location=spec["location"], observed_on=run_date,
                                method_version=method["version"], run_id=run_id,
                                measurement_kind=run["measurement_kind"],
                                extra={"metric_id": spec["id"], "measurement_axis": spec["axis"],
                                       "population": population, "validity": validity,
                                       "metric_specification": deepcopy(spec),
                                       "shared_format": "docs/contracts/model_profile.schema.json#/$defs/profile/properties/metrics"}))

    for item in config.get("model_profiles", []):
        configured_id, method = methods[item["method_ref"]]
        document, source = get_source(item["source"], date_text(item["observed_on"]))
        errors = model_profiles.validate(document)
        if errors:
            raise ValueError("existing_model_profile_invalid:" + str(errors[0]))
        for index, profile in enumerate(document["profiles"]):
            ident = add_entity(entity(kinds["model_profile"], {"source": item["source"]["sha256"], "id": profile["id"]},
                                      profile["id"], profile["validity"]["observed_on"],
                                      {"profile": deepcopy(profile), "shared_format": "loom.model_profiles/1",
                                       "source_document_sha256": item["source"]["sha256"], "automatic_promotion": False}))
            run_identity = {"profile_id": profile["id"], "source_sha256": item["source"]["sha256"],
                            "configured_method": configured_id}
            run_id = add_entity(entity(kinds["run"], run_identity, "saved-profile:" + profile["id"],
                                      item["observed_on"], {**run_identity,
                                      "measurement_kind": "historical_existing_model_profile_projection",
                                      "original_model_request_parameters": "not_reconstructed_from_legacy_profile",
                                      "automatic_promotion": False}))
            claims.append(claim(run_id, predicates["produced_by"], object_id=configured_id,
                                source=source, location="/profiles/" + str(index),
                                observed_on=item["observed_on"], method_version=method["version"], run_id=run_id,
                                measurement_kind="historical_existing_model_profile_projection",
                                extra={"producer_scope": "exact_offline_profile_projection_not_original_model_inference",
                                       "consumer_contract_status": "proposal_pending_threads_3_4"}))
            for metric_id, original in profile["metrics"].items():
                location = "/profiles/" + str(index) + "/metrics/" + metric_id.replace("~", "~0").replace("/", "~1")
                metric = deepcopy(original)
                metric["evidence"] = {"source_id": source["observation"]["id"], "location": location}
                validate_shared({metric_id: metric}, "metrics")
                claims.append(claim(ident, predicates["evaluation"], value=metric, source=source,
                                    location=location, observed_on=profile["validity"]["observed_on"],
                                    method_version=method["version"], run_id=run_id,
                                    measurement_kind="historical_existing_model_profile_projection",
                                    extra={"metric_id": metric_id, "original_metric": deepcopy(original),
                                           "population": deepcopy(profile["population"]),
                                           "validity": deepcopy(profile["validity"])}))
    origin = {"kind": "recorded", "actor": "method_graph_export_v1.saved_public_receipts",
              "model": None, "recipe_sha256": codec.digest(config), "response_sha256": None}
    task = {"operation": "inspect_research_methods_and_evidence", "exported_on": observed_on,
            "configuration_sha256": codec.digest(config), "new_model_calls": 0,
            "canonical_store_written": False, "consumer_contract_status": "proposal_pending_threads_3_4",
            "native_routes": {"projection": "/api/packet", "explicit_acceptance": "/api/graph/packets/store"}}
    return codec.make_packet(entities=entities.values(), claims=claims, sources=sources.values(),
                             task=task, origin=origin, known_at=None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("configuration", type=Path)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = export(read_json(args.configuration), root=args.root)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("xb") as stream:
        stream.write(codec.encode_packet(result))
    print(json.dumps({"packet_id": result["packet_id"], "entities": len(result["entities"]),
                      "claims": len(result["claims"]), "sources": len(result["sources"]),
                      "new_model_calls": 0, "canonical_store_written": False}))


if __name__ == "__main__":
    main()
