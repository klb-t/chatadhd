"""Selective, offline programme receipt normalization and native graph projection.

Only explicitly named attempt fields and operation capture files are read. No
key, account binding, metadata lookup, inference, retry or admission is done.
Scoring is injected by callers; this module contains no study/stage dispatch.
"""
from __future__ import annotations

from copy import deepcopy
from decimal import Decimal
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import sqlite3
from urllib.parse import quote

from . import method_graph_export_v1 as graph
from . import research_programme_manifest as manifests
from . import research_programme_transport as transport

FIELDS = ("operation_id", "programme_id", "stage_id", "manifest_sha256", "request_sha256",
          "reservation_usd", "actual_cost_usd", "reported_cost_usd", "state", "started_at",
          "finished_at", "response_sha256", "generation_sha256", "generation_id", "http_status",
          "latency_seconds", "input_tokens", "output_tokens", "billing_verified", "is_byok",
          "model", "provider", "billing_mode")
OPERATION_FIELDS = ("model_id", "provider_id", "model_aliases", "provider_aliases", "route_id", "api_type")
RESOLUTION_FIELDS = ("state", "reported_cost_usd", "actual_cost_usd", "generation_id", "model", "provider",
                     "is_byok", "billing_mode", "billing_verified", "input_tokens", "output_tokens",
                     "response_sha256", "generation_sha256")


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as file:
        file.write(graph.model_profiles.encoded(value))


def _number(value):
    if type(value) not in (int, float, str, Decimal):
        return None
    try:
        number = Decimal(str(value))
    except Exception:
        return None
    result = float(number) if number.is_finite() and number >= 0 else None
    return result if result is not None and math.isfinite(result) else None


def _plain(value):
    if isinstance(value, Decimal):
        return float(value)
    if isinstance(value, list):
        return [_plain(v) for v in value]
    if isinstance(value, dict):
        return {k: _plain(v) for k, v in value.items()}
    return value


def read_attempts(database, *, programme_id, stage_id):
    """SQLite JSON selection never loads fingerprint/binding payload fields."""
    columns = ["json_extract(payload, '$." + name + "')" for name in FIELDS]
    columns += ["json_extract(payload, '$.receipt_operation." + name + "')" for name in OPERATION_FIELDS]
    uri = "file:" + quote(str(Path(database).resolve()), safe="/") + "?mode=ro"
    with sqlite3.connect(uri, uri=True) as db:
        db.execute("BEGIN")
        selected = db.execute("SELECT " + ",".join(columns) + " FROM attempts WHERE "
                              "json_extract(payload, '$.programme_id')=? AND "
                              "json_extract(payload, '$.stage_id')=? ORDER BY rowid",
                              (programme_id, stage_id)).fetchall()
        resolutions = {}
        if db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='attempt_resolutions'").fetchone():
            resolution_columns = ["operation_id"]
            for field in RESOLUTION_FIELDS:
                resolution_columns += ["json_type(payload, '$.projection." + field + "')",
                                       "json_extract(payload, '$.projection." + field + "')"]
            resolution_columns += ["json_extract(payload, '$.late_reads[#-1].file')",
                                   "json_extract(payload, '$.late_reads[#-1].raw_sha256')",
                                   "json_extract(payload, '$.late_reads[#-1].http_status')",
                                   "json_extract(payload, '$.late_reads[#-1].transport_error') IS NOT NULL"]
            for item in db.execute("SELECT " + ",".join(resolution_columns) + " FROM attempt_resolutions WHERE operation_id IN "
                                   "(SELECT operation_id FROM attempts WHERE json_extract(payload, '$.programme_id')=? "
                                   "AND json_extract(payload, '$.stage_id')=?)", (programme_id, stage_id)):
                resolutions[item[0]] = item
    rows = []
    for values in selected:
        row = dict(zip(FIELDS, values[:len(FIELDS)]))
        for name in ("billing_verified", "is_byok"):
            row[name] = bool(row[name]) if row[name] in (0, 1) else None
        operation = dict(zip(OPERATION_FIELDS, values[len(FIELDS):]))
        for name in ("model_aliases", "provider_aliases"):
            if isinstance(operation[name], str):
                operation[name] = json.loads(operation[name])
        row["receipt_operation"] = operation
        row.update(original_state=row["state"], original_generation_sha256=row["generation_sha256"], billing_late_resolved=False)
        if row["operation_id"] in resolutions:
            resolved = resolutions[row["operation_id"]]
            original_response_hash = row["response_sha256"]
            for index, field in enumerate(RESOLUTION_FIELDS):
                if resolved[1 + 2 * index] is not None:
                    row[field] = resolved[2 + 2 * index]
            for name in ("billing_verified", "is_byok"):
                row[name] = bool(row[name]) if row[name] in (0, 1) else None
            late_file, late_hash, late_status, late_error = resolved[-4:]
            if (not isinstance(late_file, str) or not re.fullmatch(r"[0-9a-f]{32}\.captured-generation-read-(0|[1-9][0-9]*)\.bin", late_file)
                    or late_hash != row["generation_sha256"] or late_status != 200 or late_error
                    or row["response_sha256"] != original_response_hash):
                raise ValueError("programme_late_generation_binding_drift")
            row.update(billing_late_resolved=True, _late_generation_file=late_file)
        rows.append(row)
    return rows


def _capture(directory, operation_id, suffix, expected):
    path = Path(directory) / "records" / (sha(operation_id.encode()) + suffix)
    if path.is_symlink():
        raise ValueError("programme_capture_symlink")
    if not path.exists():
        if expected is not None:
            raise ValueError("programme_bound_capture_missing")
        return None
    raw = path.read_bytes()
    if expected is not None and sha(raw) != expected:
        raise ValueError("programme_bound_capture_hash_drift")
    return raw


def _late_capture(directory, filename, expected):
    path = Path(directory) / filename
    if path.is_symlink() or not path.is_file():
        raise ValueError("programme_late_capture_missing_or_symlink")
    raw = path.read_bytes()
    if sha(raw) != expected:
        raise ValueError("programme_late_capture_hash_drift")
    return raw


def _response_projection(value):
    """Keep synthetic model output and resource fields, drop other metadata."""
    result = {name: _plain(value[name]) for name in ("id", "model", "provider") if name in value}
    if isinstance(value.get("usage"), dict):
        allowed = ("cost", "is_byok", "input_tokens", "output_tokens", "prompt_tokens", "completion_tokens")
        result["usage"] = {name: _plain(value["usage"][name]) for name in allowed if name in value["usage"]}
    if isinstance(value.get("answers"), dict):
        result["answers"] = {name: {key: _plain(answer[key]) for key in ("type", "noul") if key in answer}
                             for name, answer in value["answers"].items() if isinstance(answer, dict)}
    if isinstance(value.get("choices"), list):
        result["choices"] = [{"message": {key: choice.get("message", {})[key] for key in ("role", "content")
                                           if key in choice.get("message", {})},
                              "finish_reason": choice.get("finish_reason")}
                             for choice in value["choices"] if isinstance(choice, dict)
                             and isinstance(choice.get("message", {}), dict)]
    return result


def normalize(manifest_path, ledger_directory):
    """Snapshot one unchanged manifest and selected attempt rows, including failures."""
    manifest_path, directory = Path(manifest_path), Path(ledger_directory)
    manifest_raw = manifest_path.read_bytes()
    parsed_manifest = manifests.read_manifest_bytes(manifest_raw)
    manifest = manifests.load_manifest(parsed_manifest, base_dir=manifest_path.parent)
    operations = manifests.load_operations(parsed_manifest, base_dir=manifest_path.parent)
    by_id = {op["operation_id"]: op for op in operations}
    rows = read_attempts(directory / "ledger.sqlite3", programme_id=manifest["programme_id"], stage_id=manifest["stage_id"])
    normalized, responses, requests = [], [], []
    for operation in operations:
        requests.append({key: deepcopy(operation[key]) for key in ("operation_id", "route_id", "request_sha256", "model_id", "provider_id")})
        requests[-1].update(metadata={key: operation.get("metadata", {}).get(key) for key in
                                    ("arm_id", "prepared_request_id", "source_manifest_sha256")},
                            body=deepcopy(operation["request_body"]))
    seen = set()
    for source in rows:
        ident = source["operation_id"]
        if ident not in by_id or ident in seen:
            raise ValueError("programme_attempt_inventory_drift")
        seen.add(ident)
        operation = by_id[ident]
        if source["manifest_sha256"] != sha(manifest_raw) or source["request_sha256"] != operation["request_sha256"]:
            raise ValueError("programme_attempt_request_binding_drift")
        row = {key: deepcopy(value) for key, value in source.items() if key != "receipt_operation" and not key.startswith("_")}
        row.update(metadata=deepcopy(requests[list(by_id).index(ident)]["metadata"]),
                   requested_model=operation["model_id"], requested_provider=operation["provider_id"],
                   response_model=None, response_provider=None, observed_model=None, observed_provider=None,
                   response_available=False, response_ledger_bound=False, billing_replay_verified=False,
                   completed=source["state"] == "completed", normalized_errors=[])
        sent_request = _capture(directory, ident, ".request.bin", None)
        if sent_request is not None and sent_request != operation["request_bytes"]:
            raise ValueError("programme_exact_sent_request_capture_drift")
        row.update(exact_sent_request_capture_verified=sent_request is not None,
                   sent_request_capture_sha256=sha(sent_request) if sent_request is not None else None)
        raw = _capture(directory, ident, ".response.bin", source["response_sha256"])
        original_generation = _capture(directory, ident, ".generation.bin", source["original_generation_sha256"])
        generation = (_late_capture(directory, source["_late_generation_file"], source["generation_sha256"])
                      if source["billing_late_resolved"] else original_generation)
        if raw is not None:
            row.update(response_available=True, response_sha256=sha(raw), response_ledger_bound=source["response_sha256"] == sha(raw))
            try:
                value, _ = transport._json(raw)
                projected = _response_projection(value)
                row.update(response_model=value.get("model"), response_provider=value.get("provider"),
                           observed_model=value.get("model"), observed_provider=value.get("provider"))
                try:
                    parsed = transport.extract_receipt(raw, operation)
                    row.update(input_tokens=parsed["input_tokens"], output_tokens=parsed["output_tokens"],
                               reported_cost_usd=parsed["reported_cost_usd"])
                    receipt_operation = source["receipt_operation"]
                    if generation is not None and all(receipt_operation.get(k) is not None for k in OPERATION_FIELDS):
                        rebuilt = transport.verify_generation_receipt({"http_status": 200, "raw": generation}, parsed, receipt_operation)
                        if (source["billing_verified"] is not True or source["actual_cost_usd"] != rebuilt["actual_cost_usd"]
                                or source["generation_sha256"] != sha(generation)):
                            raise ValueError("programme_billing_ledger_replay_drift")
                        row.update(billing_replay_verified=True, observed_model=rebuilt["model"], observed_provider=rebuilt["provider"])
                except Exception as error:
                    row["normalized_errors"].append(type(error).__name__ + ":" + str(error))
                generation_projection = {}
                if generation is not None:
                    try:
                        generation_value = transport._json(generation)[0].get("data", {})
                        if isinstance(generation_value, dict):
                            generation_projection = {name: _plain(generation_value[name]) for name in
                                ("id", "model", "provider_name", "api_type", "total_cost", "is_byok") if name in generation_value}
                    except Exception as error:
                        row["normalized_errors"].append(type(error).__name__ + ":" + str(error))
                responses.append({"operation_id": ident, "response_sha256": sha(raw), "raw_byte_count": len(raw),
                                  "projection": projected, "projection_loss": "Only model output, model/provider identity and selected resource fields retained; original immutable bytes remain in the private capture.",
                                  "generation_sha256": sha(generation) if generation is not None else None,
                                  "generation_projection": generation_projection})
            except Exception as error:
                row["normalized_errors"].append(type(error).__name__ + ":" + str(error))
                responses.append({"operation_id": ident, "response_sha256": sha(raw), "raw_byte_count": len(raw),
                                  "projection": None, "parse_or_billing_error": row["normalized_errors"][-1],
                                  "projection_loss": "Invalid first capture is not repaired; private immutable byte hash retained.",
                                  "generation_sha256": sha(generation) if generation is not None else None})
        row["ledger_actual_cost_usd"] = source["actual_cost_usd"]
        row["actual_cost_usd"] = source["actual_cost_usd"] if row["billing_replay_verified"] else None
        normalized.append(row)
    generation_ids = [r["projection"].get("id") for r in responses if isinstance(r.get("projection"), dict)
                      and isinstance(r["projection"].get("id"), str) and r["projection"]["id"]]
    if len(generation_ids) != len(set(generation_ids)):
        raise ValueError("programme_duplicate_generation_identity")
    return {"schema": "loom.programme_results/1", "programme_id": manifest["programme_id"],
            "stage_id": manifest["stage_id"], "manifest_sha256": sha(manifest_raw),
            "planned_operation_ids": list(by_id), "planned_operations": len(operations),
            "saved_row_count": len(normalized), "rows": normalized, "requests": requests,
            "responses": responses, "new_model_calls": 0, "private_binding_fields_read": False,
            "source_hashes": {"adapter": sha(Path(__file__).read_bytes()),
                              "manifest_adapter": sha(Path(manifests.__file__).read_bytes()),
                              "transport_receipt_parser": sha(Path(transport.__file__).read_bytes())}}


def shared_metric(value, *, unit, note, numerator=None, denominator=None):
    metric = {"value": value, "numerator": numerator, "denominator": denominator,
              "method": "counted_ratio" if denominator is not None else "unavailable" if value is None else "reported_scalar",
              "unit": unit, "note": note, "evidence": {"source_id": "rebound_by_exporter", "location": "/metrics"}}
    graph.validate_shared({"metric": metric}, "metrics")
    return metric


def export_results(bundle, output, *, observed_on, evaluator=None):
    """Caller evaluator receives (request group, saved rows) and returns dated metrics."""
    output = Path(output).resolve()
    output.relative_to(graph.ROOT)
    output.mkdir(parents=True, exist_ok=False)
    requests = deepcopy(bundle["requests"])
    request_by = {r["operation_id"]: r for r in requests}
    write(output / "REQUESTS.json", {"schema": "loom.programme_public_requests/1", "rows": requests})
    write(output / "RESPONSES.json", {"schema": "loom.programme_public_responses/1", "rows": bundle["responses"]})
    write(output / "NORMALIZED.json", bundle)
    if "scoring" in bundle:
        write(output / "SCORE.json", bundle["scoring"])
    methods, runs, reports = [], [], []
    def source(path):
        return {"path": str(path.relative_to(graph.ROOT)), "sha256": sha(path.read_bytes())}
    def descriptor(request, *, observed_model, observed_provider, actual, identity_verified=None):
        body = request["body"]
        recipe = deepcopy(body["questions"]) if "questions" in body else body["messages"][0]["content"]
        parameters = {k: deepcopy(v) for k, v in body.items() if k not in ("messages", "state", "questions", "model", "provider")}
        parameters.update(model=observed_model, provider=observed_provider, requested_model=body["model"],
                          requested_provider=request["provider_id"], request_provider_settings=deepcopy(body["provider"]),
                          observed_model_identity_verified=identity_verified)
        identity = graph.codec.digest([request["metadata"]["arm_id"], recipe, parameters, actual])
        recipe_hash = sha(recipe.encode()) if isinstance(recipe, str) else graph.codec.digest(recipe)
        return {"id": identity, "method_id": "research.programme." + str(request["metadata"]["arm_id"]),
                "version": recipe_hash, "recipe_material": recipe, "recipe_sha256": recipe_hash,
                "prompt_sha256": {"system" if isinstance(recipe, str) else "questions": recipe_hash},
                "parameters": parameters, "preset": {"id": "exact_consumed_parameters", "version": graph.codec.digest(parameters), "values": parameters},
                "components": [], "execution_kind": "saved_actual_provider_capture" if actual else "planned_requested_configuration",
                "model_identity_scope": "observed_model_separate_from_requested_alias"}
    groups, planned_groups = {}, {}
    for request in requests:
        intended = descriptor(request, observed_model=None, observed_provider=None, actual=False)
        planned_groups.setdefault(intended["id"], []).append(request)
    for row in bundle["rows"]:
        request = request_by[row["operation_id"]]
        d = descriptor(request, observed_model=row["observed_model"], observed_provider=row["observed_provider"], actual=True,
                       identity_verified=row["billing_replay_verified"])
        group = groups.setdefault(d["id"], {"descriptor": d, "rows": [], "request": request})
        group["rows"].append(row)
    # Preserve unattempted configuration populations without inventing an observed model.
    attempted_configurations = {descriptor(g["request"], observed_model=None, observed_provider=None, actual=False)["id"] for g in groups.values()}
    for request in requests:
        d = descriptor(request, observed_model=None, observed_provider=None, actual=False)
        if d["id"] not in attempted_configurations:
            groups.setdefault(d["id"], {"descriptor": d, "rows": [], "request": request})
    for group in groups.values():
        request, rows, d = group["request"], group["rows"], group["descriptor"]
        arm = request["metadata"]["arm_id"]
        intended = descriptor(request, observed_model=None, observed_provider=None, actual=False)
        planned = planned_groups[intended["id"]]
        for method in (d, intended):
            if not any(m["id"] == method["id"] for m in methods):
                methods.append(method)
        metrics = evaluator(planned, rows) if evaluator is not None else {}
        metrics = deepcopy(metrics)
        complete = sum(r["completed"] for r in rows)
        metrics["completion_coverage"] = shared_metric(complete / len(planned), numerator=complete, denominator=len(planned), unit="fraction",
            note="All prepared operation slots remain denominator, including failed and unattempted requests; this is transport completion, not semantic correctness.")
        known_costs = [Decimal(r["actual_cost_usd"]) for r in rows if r["actual_cost_usd"] is not None]
        known_cost = sum(known_costs, Decimal(0))
        resource_cohort = {"attempts": len(rows), "billing_verified_attempts": len(known_costs),
                           "unknown_cost_attempts": len(rows) - len(known_costs),
                           "known_cost_usd_decimal": format(known_cost, "f"),
                           "transport_states": {state: sum(r["state"] == state for r in rows) for state in sorted({r["state"] for r in rows})}}
        metrics["known_billed_cost_usd"] = shared_metric(float(known_cost) if rows else None, unit="USD",
            note="Sum of independently replayed billing for recorded attempts only. Unknown or unattempted costs are excluded explicitly, not treated as zero; exact decimal/cohort counts retained in resource_cohort.")
        metrics["total_attempt_cost_usd"] = shared_metric(float(known_cost) if rows and len(known_costs) == len(rows) else None, unit="USD",
            note="Total for this recorded-attempt cohort is unavailable when any attempt lacks verified billing. Does not claim the full planned population was executed.")
        for field in ("input_tokens", "output_tokens"):
            known = [r[field] for r in rows if type(r[field]) is int and r[field] >= 0]
            resource_cohort[field + "_available_attempts"] = len(known)
            metrics["attempt_" + field + "_total"] = shared_metric(sum(known) if rows and len(known) == len(rows) else None, unit="tokens",
                note="Reported token total for the recorded-attempt cohort; null if any recorded attempt has unavailable usage. Unattempted operations remain in the separate planned population.")
        latencies = [_number(r["latency_seconds"]) for r in rows]
        measured_latencies = [v for v in latencies if v is not None]
        resource_cohort["latency_available_attempts"] = len(measured_latencies)
        metrics["measured_latency_mean_seconds"] = shared_metric(sum(measured_latencies) / len(measured_latencies) if measured_latencies else None,
            unit="seconds", note="Host-measured first-response latency mean over the explicitly counted available attempt cohort; missing times remain unavailable.")
        for index, row in enumerate(rows):
            for metric_id, field, unit in (("cost_usd", "actual_cost_usd", "USD"), ("input_tokens", "input_tokens", "tokens"),
                                          ("output_tokens", "output_tokens", "tokens"), ("latency_seconds", "latency_seconds", "seconds")):
                metrics[row["operation_id"] + "." + metric_id] = shared_metric(_number(row[field]), unit=unit,
                    note="Saved first-attempt resource field; unavailable values stay null. Exact monetary decimal and request/response hashes remain in rows; cost requires recorded billing verification.")
        report = {"schema": "loom.programme_result_group/1", "arm_id": arm, "planned_operation_ids": [r["operation_id"] for r in planned],
                  "saved_row_count": len(rows), "rows": deepcopy(rows), "metrics": metrics, "resource_cohort": resource_cohort,
                  "model_quality_measured": any(m.get("value") is not None and key.startswith("accuracy") for key, m in metrics.items())}
        report_path = output / ("group-" + d["id"] + ".json")
        write(report_path, report)
        reports.append({"path": str(report_path.relative_to(graph.ROOT)), "arm_id": arm, "configuration": d["id"]})
        events = []
        for index, row in enumerate(rows):
            req_index = next(i for i, r in enumerate(requests) if r["operation_id"] == row["operation_id"])
            binding = {"prepared_request": {"source": source(output / "REQUESTS.json"), "location": "/rows/" + str(req_index),
                "matches": [{"pointer": "/operation_id", "equals": row["operation_id"]}, {"pointer": "/request_sha256", "equals": row["request_sha256"]}]}}
            if row["response_available"] and any(r["operation_id"] == row["operation_id"] for r in bundle["responses"]):
                resp_index = next(i for i, r in enumerate(bundle["responses"]) if r["operation_id"] == row["operation_id"])
                binding["captured_response"] = {"source": source(output / "RESPONSES.json"), "location": "/rows/" + str(resp_index),
                    "matches": [{"pointer": "/operation_id", "equals": row["operation_id"]}, {"pointer": "/response_sha256", "equals": row["response_sha256"]}]}
            events.append({"id": row["operation_id"], "location": "/rows/" + str(index), "identity_pointer": "/operation_id", "binding": binding})
        specs = [{"id": key, "location": "/metrics/" + key.replace("~", "~0").replace("/", "~1"),
                  "metric_pointer": "/metrics/" + key.replace("~", "~0").replace("/", "~1"),
                  "axis": ("model_quality" if report["model_quality_measured"] else "unavailable") if key.startswith("accuracy")
                          else "mechanism" if key in ("completion_coverage", "semantic_coverage") else "resource"}
                 for key in metrics]
        runs.append({"id": bundle["programme_id"] + ":" + bundle["stage_id"] + ":" + d["id"], "method_ref": d["id"],
                     "intended_method_ref": intended["id"], "source": source(report_path), "observed_on": observed_on,
                     "measurement_kind": "historical_actual_model_response_replay", "metrics": specs,
                     "evidence_sources": [source(output / "NORMALIZED.json")] + ([source(output / "SCORE.json")] if "scoring" in bundle else []),
                     "population": {"planned": len(planned), "available": len(rows), "missing": len(planned) - len(rows), "unit": "prepared operations",
                                    "dependent_observations": "Authored DEV reused across matched configurations; observation is first attempt, not independent model trials."},
                     "selected_request_ids": [r["operation_id"] for r in rows], "events": events})
    write(output / "METHODS.json", methods)
    declarations = source(output / "METHODS.json")
    config = {"public_artifacts_only": True, "exported_on": observed_on,
              "methods": [{**d, "declaration_source": declarations, "declaration_pointer": "/" + str(i)} for i, d in enumerate(methods)], "runs": runs}
    write(output / "configuration.json", config)
    packet = graph.export(config, root=graph.ROOT)
    raw = graph.codec.encode_packet(packet)
    with (output / "packet.json.gz").open("xb") as file:
        with gzip.GzipFile(filename="", fileobj=file, mode="wb", mtime=0) as zipped:
            zipped.write(raw)
    receipt = {"schema": "loom.programme_result_graph/1", "programme_id": bundle["programme_id"], "stage_id": bundle["stage_id"],
               "planned_operations": bundle["planned_operations"], "attempted_operations": len(bundle["rows"]),
               "packet_id": packet["packet_id"], "packet_sha256": sha(raw), "entities": len(packet["entities"]),
               "claims": len(packet["claims"]), "sources": len(packet["sources"]), "reports": reports,
               "new_model_calls": 0, "canonical_store_written": False, "no_fake_model_profiles": True,
               "native_validation": "existing Python native DTO codec; no CABI or persistence execution",
               "output_sha256": {p.name: sha(p.read_bytes()) for p in sorted(output.iterdir()) if p.is_file()}}
    write(output / "VERIFICATION.json", receipt)
    return receipt
