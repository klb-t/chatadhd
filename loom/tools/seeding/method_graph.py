#!/usr/bin/env python3
"""Opt-in projection of captured offline runs into the shared native contract.

No prediction, score, algorithm dispatch, provider call or graph-store write.
Policy/vocabulary/traversal paths are caller data. Source bytes are retained once;
record claims identify a decoded JSON Pointer and its native canonical hash.
"""
from __future__ import annotations

import argparse
import base64
from copy import deepcopy
from datetime import datetime, timezone
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import time

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from loom.tools.structure.agentic_graph_v1 import packet as codec

DEFAULT_PROJECTION = Path(__file__).with_name("profiles") / "method_graph.json"


class ProjectionProfile(dict):
    """Resolved data plus optional exact input-file bytes, outside JSON fields."""
    source_bytes = None


def canonical(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def strict_json(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate_json_key:" + key)
            result[key] = value
        return result
    def constant(value):
        raise ValueError("nonfinite_json:" + value)
    return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)


def pointer(value, path):
    if not isinstance(path, str) or path and not path.startswith("/"):
        raise ValueError("invalid_json_pointer")
    for token in path.split("/")[1:] if path else ():
        if re.search(r"~(?![01])", token):
            raise ValueError("invalid_json_pointer_escape")
        token = token.replace("~1", "/").replace("~0", "~")
        if isinstance(value, list):
            if not re.fullmatch(r"0|[1-9][0-9]*", token):
                raise ValueError("invalid_json_pointer_array_index")
            value = value[int(token)]
        else:
            value = value[token]
    return value


def exists(value, path):
    try:
        pointer(value, path)
        return True
    except (KeyError, IndexError, TypeError, ValueError):
        return False


def walk(value, path, prefix=""):
    """Data-selected object/list traversal; no method or dimension vocabulary."""
    if not path:
        yield prefix, value
        return
    head, *rest = path
    if head == "*":
        children = enumerate(value) if isinstance(value, list) else value.items() if isinstance(value, dict) else ()
    elif isinstance(value, list):
        if not isinstance(head, str) or not re.fullmatch(r"0|[1-9][0-9]*", head):
            raise ValueError("invalid_json_pointer_array_index")
        children = [(head, value[int(head)])] if int(head) < len(value) else ()
    elif isinstance(value, dict):
        children = [(head, value[head])] if head in value else ()
    else:
        children = ()
    for key, child in children:
        escaped = str(key).replace("~", "~0").replace("/", "~1")
        yield from walk(child, rest, prefix + "/" + escaped)


def load_projection(path=DEFAULT_PROJECTION):
    raw = Path(path).read_bytes()
    data = ProjectionProfile(strict_json(raw))
    data.source_bytes = raw
    return validate_projection(data)


def validate_projection(data):
    if not isinstance(data, dict):
        raise ValueError("invalid_projection_object")
    if data.get("schema") != "loom.seeding.method_graph_projection/1":
        raise ValueError("unsupported_projection_schema")
    for field in ("namespace",):
        if not isinstance(data.get(field), str) or not data[field]:
            raise ValueError("invalid_projection:" + field)
    required_kinds = {"method", "method_version", "run", "parameter_set_version", "recipe_version", "profile_version", "source_version", "result", "projection_version"}
    required_predicates = {"version_of", "requests_method_version", "produced_in_run", "produced_by_method_version", "uses_parameter_set", "uses_recipe", "uses_profile", "uses_source_version", "captured_record", "uses_projection", "evaluation_record"}
    for group, required in (("kinds", required_kinds), ("predicates", required_predicates)):
        values = data.get("vocabulary", {}).get(group)
        if not isinstance(values, dict) or not required <= set(values) or any(not isinstance(value, str) or not value for value in values.values()):
            raise ValueError("invalid_projection_vocabulary:" + group)
    export = data.get("export", {})
    if type(export.get("confirmation_ratio")) not in (int, float) or not math.isfinite(export["confirmation_ratio"]) or export["confirmation_ratio"] <= 0:
        raise ValueError("invalid_confirmation_ratio")
    quote_chars = data.get("support", {}).get("quote_chars")
    if quote_chars is not None and (type(quote_chars) is not int or quote_chars < 1):
        raise ValueError("invalid_support_quote_chars")
    if data["export"]["source_encoding"] != "base64":
        raise ValueError("unavailable_source_encoding")
    if data["export"]["format"] not in ("json", "json.gz"):
        raise ValueError("unavailable_export_format")
    if not isinstance(data.get("projections"), dict) or export.get("mode") not in data["projections"]:
        raise ValueError("invalid_projection_modes")
    for mode, rules in data["projections"].items():
        if not isinstance(mode, str) or not isinstance(rules, list):
            raise ValueError("invalid_projection_mode")
        for rule in rules:
            if not isinstance(rule, dict) or not {"id", "input", "path"} <= set(rule):
                raise ValueError("invalid_projection_rule")
            if set(rule) - {"id", "input", "path", "when_pointer", "unless_pointer", "exclude_final_keys", "evaluation"}:
                raise ValueError("unavailable_projection_operation")
            if not isinstance(rule["path"], list) or any(not isinstance(x, str) for x in rule["path"]):
                raise ValueError("invalid_projection_path")
            if not isinstance(rule["id"], str) or not rule["id"] or rule["input"] not in data["inputs"]:
                raise ValueError("invalid_projection_role")
            if "evaluation" in rule and type(rule["evaluation"]) is not bool:
                raise ValueError("invalid_evaluation_flag")
            if "exclude_final_keys" in rule and (not isinstance(rule["exclude_final_keys"], list) or any(not isinstance(x, str) for x in rule["exclude_final_keys"])):
                raise ValueError("invalid_projection_exclusions")
            for key in ("when_pointer", "unless_pointer"):
                if key in rule and (not isinstance(rule[key], str) or rule[key] and not rule[key].startswith("/")):
                    raise ValueError("invalid_projection_condition")
    return data


def relative_file(directory, relative):
    path = directory / relative
    if not path.resolve().is_relative_to(directory.resolve()):
        raise ValueError("source_path_outside_run")
    return path


def capture_run(directory, projection, *, input_paths=None):
    directory = Path(directory)
    files, documents = {}, {}
    def capture(role, path, expected=None):
        raw = Path(path).read_bytes()
        if expected is not None and sha256(raw) != expected:
            raise ValueError("frozen_source_hash_mismatch:" + role)
        decoded = gzip.decompress(raw) if raw[:2] == b"\x1f\x8b" else raw
        files[role] = {"raw": raw, "decoded": decoded, "name": Path(path).name,
                       "compression": "gzip" if raw[:2] == b"\x1f\x8b" else "none",
                       "sha256": sha256(raw), "decoded_sha256": sha256(decoded)}
        try:
            documents[role] = strict_json(decoded)
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass  # Non-JSON program/protocol sources are exact bytes, not executions.
    input_paths = {} if input_paths is None else input_paths
    if set(input_paths) - set(projection["inputs"]):
        raise ValueError("unavailable_input_role; declare it in the projection profile")
    for role, spec in projection["inputs"].items():
        path = Path(input_paths[role]) if role in input_paths else next(
            (relative_file(directory, name) for name in spec["paths"] if relative_file(directory, name).is_file()), None)
        if path is not None:
            capture(role, path)
        elif spec["required"]:
            raise ValueError("frozen_source_unavailable:" + role)
    manifest = pointer(documents[projection["manifest"]["input"]], projection["manifest"]["pointer"])
    for selector in projection["output_manifests"]:
        if pointer(documents[selector["input"]], selector["pointer"]) != manifest:
            raise ValueError("output_manifests_differ")
    for key, role in projection["hash_bindings"].items():
        expected = manifest.get("hashes", {}).get(key)
        if role in files and expected is not None and files[role]["sha256"] != expected:
            raise ValueError("manifest_source_hash_mismatch:" + role)
    module_hashes = (pointer(manifest, projection["profiles"]["modules_pointer"])
                     if exists(manifest, projection["profiles"]["modules_pointer"]) else {})
    for name, expected in module_hashes.items():
        capture("module:" + name, relative_file(directory, name), expected)
    descriptor = pointer(manifest, projection["profiles"]["descriptor_pointer"]) if exists(manifest, projection["profiles"]["descriptor_pointer"]) else None
    if descriptor is not None:
        role = projection["profiles"]["effective_role"]
        if role not in files or files[role]["sha256"] != descriptor["effective_sha256"]:
            raise ValueError("effective_profile_hash_mismatch")
        for index, item in enumerate(descriptor["source_files"]):
            capture("profile-source:" + str(index), relative_file(directory, item["frozen_path"]), item["sha256"])
    return {"files": files, "documents": documents, "manifest": manifest, "profile_descriptor": descriptor}


def selected_records(captured, projection, mode=None):
    mode = projection["export"]["mode"] if mode is None else mode
    if mode not in projection["projections"]:
        raise ValueError("unavailable_projection_mode:" + mode)
    seen = set()
    for rule in projection["projections"][mode]:
        document = captured["documents"][rule["input"]]
        if "when_pointer" in rule and not exists(document, rule["when_pointer"]):
            continue
        if "unless_pointer" in rule and exists(document, rule["unless_pointer"]):
            continue
        for path, value in walk(document, rule["path"]):
            last = path.rsplit("/", 1)[-1].replace("~1", "/").replace("~0", "~")
            if last in rule.get("exclude_final_keys", []):
                continue
            key = (rule["input"], path)
            if key in seen:
                raise ValueError("duplicate_projected_record:" + str(key))
            seen.add(key)
            yield {"role": rule["id"], "input": rule["input"], "json_pointer": path, "value": value,
                   "evaluation": rule.get("evaluation", False)}


def locator(member=""):
    return {"source": "captured-run", "member": member, "json_pointer": "", "byte_start": None,
            "byte_len": None, "time_start": None, "time_end": None, "line": None}


def source(id_, text, timestamp, *, attrs=None, member=""):
    return {"observation": {"id": id_, "unit": "captured-run", "kind": "field", "text": text,
            "locator": locator(member), "lang": "", "date": timestamp or "", "ordinal": 0,
            "artifact_type": "captured-bytes", "speaker": "", "attrs": attrs or {}},
            "known_at": timestamp, "text_sha256": sha256(text.encode())}


def entity(id_, kind, label, attrs, timestamp):
    return {"id": id_, "kind": kind, "canonical_key": id_, "label": label, "labels": {},
            "aliases": [], "parent": "", "first_seen": timestamp or "", "last_seen": timestamp or "",
            "evidence_class": "derived", "origin": "system", "confidence": 1, "status": "active", "attrs": attrs}


def claim(id_, subject, predicate, *, object_="", value=None, support, quote_chars):
    obs = support["observation"]
    return {"id": id_, "subject": subject, "predicate": predicate, "object": object_, "value": value,
            "qualifiers": {"valid_from": "", "valid_to": "", "version": "", "branch": "", "scope": "captured-output-structure", "lang": "",
                           "extra": {"confidence_scope": "structure_only"}},
            "assessment": {"basis": {"support": [{"observation": obs["id"], "locator": obs["locator"],
                "quote": obs["text"][:quote_chars], "extractor": "seeding-method-graph/1", "quality": 1}],
                "derivation": {"operator": "captured-record-projection", "operator_version": 1, "morphism": "", "depth": 0}},
                "evidence_class": "derived", "origin": "system", "confidence": 1, "status": "active",
                "premises": {"claims": [], "principles": [], "assumptions": []}, "counter": {"observations": [], "claims": []},
                "consequences": {"claims": [], "predictions": [], "checks": []}, "open": {"slots": [], "questions": [], "fill_query": None},
                "expected_property": None, "check_state": "n/a", "alternatives": []}}


def build_artifact(captured, projection, *, mode=None, projected_at=None):
    """Capture provenance of the complete experiment, never world-truth promotion."""
    validate_projection(projection)
    mode = projection["export"]["mode"] if mode is None else mode
    ns = projection["namespace"]
    identity = ns + ":method:" + codec.digest(projection["method"]["identity"])
    prepared_at = (pointer(captured["manifest"], projection["manifest"]["timestamp_pointer"])
                   if exists(captured["manifest"], projection["manifest"]["timestamp_pointer"]) else None)
    projected_at = projected_at or datetime.now(timezone.utc).isoformat()
    # This projection first observes the archived bytes/nodes now. Original
    # producer times stay in raw capture and trace; a score has no earlier
    # recorded publication time merely because it shares a prediction manifest.
    timestamp = projected_at
    kinds, predicates = projection["vocabulary"]["kinds"], projection["vocabulary"]["predicates"]
    sources, source_map, entities, claims = [], {}, [], []
    quote_chars = projection["support"]["quote_chars"]
    def id_(role, value):
        return ns + ":" + role + ":" + codec.digest(value)
    for role, row in captured["files"].items():
        sid = id_("source", [role, row["sha256"]])
        source_map[role] = source(sid, base64.b64encode(row["raw"]).decode("ascii"), timestamp,
            member=row["name"], attrs={"encoding": "base64", "compression": row["compression"], "role": role,
                "raw_sha256": row["sha256"], "decoded_sha256": row["decoded_sha256"], "raw_bytes": len(row["raw"]), "decoded_bytes": len(row["decoded"]),
                "original_prepared_at": prepared_at, "known_at_semantics": "bytes observed at projection"})
        sources.append(source_map[role])
    projection_source = source(id_("source", ["projection", projection]), canonical(projection).decode(), projected_at, member="method_graph.json")
    sources.append(projection_source)
    projection_runtime = {}
    # Instrument modules are captured separately from the historical producer.
    # These are actual loaded capabilities, not configurable execution dispatch.
    for name, path in (("adapter", Path(__file__)), ("packet_codec", Path(codec.__file__)),
                       ("canonical_codec", Path(codec.safe.__file__))):
        raw = path.read_bytes()
        sid = id_("source", ["projection-module", name, sha256(raw)])
        sources.append(source(sid, base64.b64encode(raw).decode("ascii"), projected_at, member=path.name,
            attrs={"role": "projection-module:" + name, "encoding": "base64", "compression": "none",
                   "raw_sha256": sha256(raw), "decoded_sha256": sha256(raw), "raw_bytes": len(raw), "decoded_bytes": len(raw)}))
        projection_runtime[name] = {"source_ref": sid, "raw_sha256": sha256(raw)}
    original_projection = getattr(projection, "source_bytes", None)
    if original_projection is not None:
        sid = id_("source", ["projection-profile-bytes", sha256(original_projection)])
        sources.append(source(sid, base64.b64encode(original_projection).decode("ascii"), projected_at, member="method_graph.json",
            attrs={"role": "projection-profile-bytes", "encoding": "base64", "compression": "none",
                   "raw_sha256": sha256(original_projection), "decoded_sha256": sha256(original_projection),
                   "raw_bytes": len(original_projection), "decoded_bytes": len(original_projection)}))
        projection_runtime["profile_source"] = {"source_ref": sid, "raw_sha256": sha256(original_projection)}
    projection_definition = {"profile": projection, "profile_sha256": codec.digest(projection), "runtime": projection_runtime}
    projection_id = id_("projection-version", projection_definition)
    entities.append(entity(projection_id, kinds["projection_version"], "captured projection", {"definition": projection_definition, "definition_sha256": codec.digest(projection_definition)}, projected_at))
    params = pointer(captured["documents"][projection["parameters"]["input"]], projection["parameters"]["pointer"])
    overrides = ({"status": "recorded", "profile_sources": captured["profile_descriptor"]["source_files"]}
                 if captured["profile_descriptor"] else {"status": "unrecorded", "profile_sources": None})
    parameter_definition = {"effective_parameters": params, "user_overrides": overrides}
    parameter_hash = codec.digest(parameter_definition)
    parameter_id = id_("parameters", parameter_definition)
    recipe_files = {role: {"raw_sha256": row["sha256"], "decoded_sha256": row["decoded_sha256"],
                          "source_ref": source_map[role]["observation"]["id"]}
                    for role, row in captured["files"].items() if role not in projection["source_roles"]["recipe_exclude"]}
    recipe_definition = {"parameters": params, "captured_runtime": recipe_files,
                         "profile": captured["documents"].get(projection["profiles"]["effective_role"]),
                         "profile_status": "recorded" if captured["profile_descriptor"] else "unrecorded"}
    recipe_hash, recipe_id = codec.digest(recipe_definition), id_("recipe", recipe_definition)
    method_definition = {"scope": "complete-captured-experiment", "parameter_set_sha256": parameter_hash,
                         "recipe_sha256": recipe_hash}
    method_hash, method_id = codec.digest(method_definition), id_("version", method_definition)
    input_sha = (pointer(captured["manifest"], projection["manifest"]["input_hash_pointer"])
                 if exists(captured["manifest"], projection["manifest"]["input_hash_pointer"]) else None)
    run_id = id_("run", {"method": method_id, "input": input_sha, "manifest": captured["manifest"],
                        "outputs": {role: captured["files"][role]["sha256"] for role in projection["source_roles"]["outputs"]},
                        "projection": codec.digest(projection_definition), "mode": mode})
    entities.append(entity(identity, kinds["method"], projection["method"]["label"], {"identity": projection["method"]["identity"]}, timestamp))
    for role, eid, definition in (("method_version", method_id, method_definition), ("parameter_set_version", parameter_id, parameter_definition), ("recipe_version", recipe_id, recipe_definition)):
        entities.append(entity(eid, kinds[role], role, {"definition": definition, "definition_sha256": codec.digest(definition)}, timestamp))
    def edge(subject, predicate, target, support=projection_source):
        if not support["observation"]["text"]:
            support = projection_source
        claims.append(claim(id_("claim", [subject, predicate, target]), subject, predicates[predicate], object_=target, support=support, quote_chars=quote_chars))
    edge(method_id, "version_of", identity)
    edge(run_id, "requests_method_version", method_id)
    edge(method_id, "uses_parameter_set", parameter_id)
    edge(run_id, "uses_parameter_set", parameter_id)
    edge(method_id, "uses_recipe", recipe_id)
    edge(run_id, "uses_projection", projection_id)
    # Each consumed runtime/profile file is a real immutable graph entity.
    for role, definition in recipe_files.items():
        eid = id_("source-version", [role, definition])
        entities.append(entity(eid, kinds["source_version"], role, {"definition": definition, "definition_sha256": codec.digest(definition)}, timestamp))
        edge(recipe_id, "uses_source_version", eid, source_map[role])
    profile_role = projection["profiles"]["effective_role"]
    if profile_role in captured["documents"]:
        definition = captured["documents"][profile_role]
        eid = id_("profile", definition)
        entities.append(entity(eid, kinds["profile_version"], "effective profile", {"definition": definition, "definition_sha256": codec.digest(definition), "source_ref": source_map[profile_role]["observation"]["id"]}, timestamp))
        edge(recipe_id, "uses_profile", eid, source_map[profile_role])
    result_ids, result_bindings, counts = [], [], {}
    for row in selected_records(captured, projection, mode):
        source_id = source_map[row["input"]]["observation"]["id"]
        reference = {"source_ref": source_id, "json_pointer": row["json_pointer"], "value_sha256": codec.digest(row["value"]),
                     "decode": ["base64", captured["files"][row["input"]]["compression"], "json"], "projection_role": row["role"]}
        rid = id_("result", [run_id, row["input"], row["json_pointer"]])
        entities.append(entity(rid, kinds["result"], row["role"], reference, timestamp))
        claims.append(claim(id_("claim", [rid, "record"]), rid, predicates["captured_record"], value=reference, support=source_map[row["input"]], quote_chars=quote_chars))
        edge(rid, "produced_in_run", run_id, source_map[row["input"]])
        edge(rid, "produced_by_method_version", method_id, source_map[row["input"]])
        if row["evaluation"]:
            edge(method_id, "evaluation_record", rid, source_map[row["input"]])
        result_ids.append(rid)
        result_bindings.append({"result_entity_id": rid, "run_id": run_id, "method_version_id": method_id})
        counts[row["role"]] = counts.get(row["role"], 0) + 1
    trace = {"schema": "loom.method_run_trace/1", "run_id": run_id, "method_identity_id": identity, "method_version_id": method_id,
             "parameter_set_version_id": parameter_id, "parameter_set_sha256": parameter_hash, "recipe_sha256": recipe_hash,
             "projection_version_id": projection_id,
             "effective_parameters": params, "user_overrides": overrides, "input_sha256": input_sha, "prepared_at": prepared_at,
             "projected_at": projected_at, "execution_kind": "offline-captured-output-projection", "projection_status": "complete",
             "measurements": {"result_records": len(result_ids), "adapter_provider_calls": 0, "producer_cpu_seconds": None,
                              "producer_cost_usd": None, **{"records/" + key: value for key, value in counts.items()}},
             "measurement_scope": "captured output counts; original producer CPU/cost unrecorded", "result_bindings": result_bindings}
    entities.append(entity(run_id, kinds["run"], "captured run", deepcopy(trace), timestamp))
    contract = {"schema": "loom.method_graph/1", "vocabulary": projection["vocabulary"],
                "bindings": {"method_identity_id": identity, "method_version_id": method_id, "run_id": run_id,
                             "parameter_set_version_id": parameter_id, "recipe_version_id": recipe_id, "projection_version_id": projection_id},
                "definition_hashes": {"method_version": method_hash, "parameter_set": parameter_hash, "recipe": recipe_hash, "projection": codec.digest(projection_definition)},
                "trace": {key: value for key, value in trace.items() if key not in ("result_bindings", "measurements", "projection_status", "projected_at")}}
    definition_source = source(id_("source", ["contract", contract]), canonical(contract).decode(), projected_at)
    trace_source = source(id_("source", ["trace", trace]), canonical(trace).decode(), projected_at)
    sources.extend((definition_source, trace_source))
    packet = codec.make_packet(entities=entities, claims=claims, sources=sources,
        task={"operation": "captured-run-projection", "mode": mode, "projection_sha256": codec.digest(projection)},
        origin={"kind": "recorded", "actor": "seeding-method-graph", "model": None, "recipe_sha256": recipe_hash, "response_sha256": None}, known_at=projected_at)
    return {"schema": "loom.method_graph_fixture/1", "contract": contract, "trace": trace, "packet": packet,
            "result_entity_ids": result_ids, "definition_capture_source_id": definition_source["observation"]["id"],
            "trace_capture_source_id": trace_source["observation"]["id"],
            "producer_evidence": {"scope": "captured whole experiment; result references are exact records, not content truth",
                                  "producer_execution_verified": False, "projection_profile_sha256": codec.digest(projection)}}


def recover_files(artifact):
    """Recover exact source bytes, verifying both byte and decoded hashes."""
    files = {}
    for item in artifact["packet"]["sources"]:
        obs, attrs = item["observation"], item["observation"]["attrs"]
        if attrs.get("encoding") != "base64":
            continue
        raw = base64.b64decode(obs["text"], validate=True)
        decoded = gzip.decompress(raw) if attrs["compression"] == "gzip" else raw
        if sha256(raw) != attrs["raw_sha256"] or sha256(decoded) != attrs["decoded_sha256"]:
            raise ValueError("captured_source_hash_mismatch")
        if attrs["role"] in files:
            raise ValueError("duplicate_captured_source_role")
        files[attrs["role"]] = raw
    return files


def recover_results(artifact):
    """Resolve every result reference and verify each original record hash."""
    sources = {row["observation"]["id"]: row["observation"] for row in artifact["packet"]["sources"]}
    documents, results = {}, {}
    result_ids = set(artifact["result_entity_ids"])
    if len(result_ids) != len(artifact["result_entity_ids"]):
        raise ValueError("duplicate_result_ids")
    predicate = artifact["contract"]["vocabulary"]["predicates"]["captured_record"]
    captured_claims = {}
    for row in artifact["packet"]["claims"]:
        if row["subject"] in result_ids and row["predicate"] == predicate:
            if row["subject"] in captured_claims:
                raise ValueError("duplicate_captured_record_claim")
            captured_claims[row["subject"]] = row["value"]
    for entity_ in artifact["packet"]["entities"]:
        if entity_["id"] not in result_ids:
            continue
        attrs = entity_["attrs"]
        if captured_claims.get(entity_["id"]) != attrs:
            raise ValueError("captured_record_claim_mismatch")
        sid = attrs["source_ref"]
        if sid not in documents:
            obs = sources[sid]
            raw = base64.b64decode(obs["text"], validate=True)
            if sha256(raw) != obs["attrs"]["raw_sha256"]:
                raise ValueError("captured_source_hash_mismatch")
            decoded = gzip.decompress(raw) if obs["attrs"]["compression"] == "gzip" else raw
            if sha256(decoded) != obs["attrs"]["decoded_sha256"]:
                raise ValueError("captured_decoded_hash_mismatch")
            documents[sid] = strict_json(decoded)
        value = pointer(documents[sid], attrs["json_pointer"])
        if codec.digest(value) != attrs["value_sha256"]:
            raise ValueError("captured_record_hash_mismatch")
        results[entity_["id"]] = value
    if set(results) != result_ids:
        raise ValueError("missing_result_entity")
    return results


def estimate(captured, projection, *, mode=None, format_=None, projected_at="2000-01-01T00:00:00+00:00"):
    """Preflight from real bytes; exact serialization for ordinary-size presets.

    Large repeated support quotes can dominate CPU/memory even in compressed
    output. A conservative raw-work lower bound catches that before constructing
    those strings/packet hashes. Explicit confirmation permits that operation.
    """
    started_cpu, started = time.process_time(), time.perf_counter()
    format_ = format_ or projection["export"]["format"]
    if format_ not in ("json", "json.gz"):
        raise ValueError("unavailable_export_format")
    validate_projection(projection)
    baseline = sum(len(captured["files"][role]["raw"]) for role in projection["source_roles"]["outputs"])
    decoded_baseline = sum(len(captured["files"][role]["decoded"]) for role in projection["source_roles"]["outputs"])
    quote_chars, work, count = projection["support"]["quote_chars"], 0, 0
    for row in selected_records(captured, projection, mode):
        encoded_length = 4 * ((len(captured["files"][row["input"]]["raw"]) + 2) // 3)
        work += (3 + int(row["evaluation"])) * (encoded_length if quote_chars is None else min(quote_chars, encoded_length))
        count += 1
    work += sum(4 * ((len(row["raw"]) + 2) // 3) for row in captured["files"].values())
    work_ratio = work / decoded_baseline if decoded_baseline else None
    if work_ratio is not None and work_ratio >= projection["export"]["confirmation_ratio"]:
        return {"baseline_output_bytes": baseline, "baseline_decoded_output_bytes": decoded_baseline,
                "export_bytes": None, "decoded_export_bytes": None, "additional_output_ratio": None,
                "total_output_ratio": None, "requires_confirmation": True,
                "estimate_kind": "serialization-work-lower-bound-before-build",
                "expected_serialization_work_bytes_lower_bound": work,
                "expected_work_ratio_lower_bound": work_ratio, "producer_cpu_seconds": None,
                "estimated_cpu_seconds": None, "estimate_wall_seconds": time.perf_counter() - started,
                "result_count": count, "format": format_}
    artifact = build_artifact(captured, projection, mode=mode, projected_at=projected_at)
    raw = canonical(artifact)
    encoded = gzip.compress(raw, mtime=0) if format_ == "json.gz" else raw
    ratio = len(encoded) / baseline if baseline else None
    return {"baseline_output_bytes": baseline, "export_bytes": len(encoded), "decoded_export_bytes": len(raw),
            "additional_output_ratio": ratio, "total_output_ratio": 1 + ratio if ratio is not None else None,
            "requires_confirmation": ratio is not None and 1 + ratio >= projection["export"]["confirmation_ratio"],
            "comparison_status": "measured" if ratio is not None else "baseline_unavailable",
            "estimate_kind": "exact-serialized-export", "expected_work_ratio_lower_bound": work_ratio,
            "estimated_cpu_seconds": time.process_time() - started_cpu, "estimate_wall_seconds": time.perf_counter() - started,
            "producer_cpu_seconds": None, "result_count": len(artifact["result_entity_ids"]), "format": format_}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--projection", type=Path, default=DEFAULT_PROJECTION)
    parser.add_argument("--mode", default=None)
    parser.add_argument("--format", choices=("json", "json.gz"), default=None)
    parser.add_argument("--input", action="append", default=[], metavar="ROLE=PATH")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--confirm-large-export", action="store_true", help="explicit caller confirmation after reviewing dry-run estimate")
    args = parser.parse_args()
    projection = load_projection(args.projection)
    inputs = dict(item.split("=", 1) for item in args.input)
    captured = capture_run(args.run, projection, input_paths=inputs)
    projected_at = datetime.now(timezone.utc).isoformat()
    report = estimate(captured, projection, mode=args.mode, format_=args.format, projected_at=projected_at)
    print(json.dumps(report))
    if args.output is None:
        return
    if report["requires_confirmation"] and not args.confirm_large_export:
        parser.error("expected usage increase requires confirmation; review estimate and choose export settings")
    artifact = build_artifact(captured, projection, mode=args.mode, projected_at=projected_at)
    raw = canonical(artifact)
    encoded = gzip.compress(raw, mtime=0) if report["format"] == "json.gz" else raw
    with args.output.open("xb") as stream:
        stream.write(encoded)


if __name__ == "__main__":
    main()
