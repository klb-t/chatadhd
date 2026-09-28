#!/usr/bin/env python3
"""Prepare and score a bounded source-to-occurrence-graph experiment.

Preparation reads public inputs, never gold. Inference is exclusively delegated
to openrouter_runner; this module's only HTTP call reads public endpoint prices.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import json
import math
from pathlib import Path
import re
import urllib.request

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
FIXTURES = REPO / "loom/tests/fixtures/eval/live_structure_pilot_v1"
METHODS = {"native_v1", "native_anchors_v1"}
MAX_FILE = 8 * 1024 * 1024
CODE_FILES = ("live_pilot.py", "live_pilot_score.py", "candidate_graph.py",
              "candidate_graph_vocabulary.json", "openrouter_runner.py")


def code_hashes():
    return {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest() for name in CODE_FILES}


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def _pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def parse(raw):
    if len(raw) > MAX_FILE:
        raise ValueError("JSON exceeds input limit")
    return json.loads(raw, object_pairs_hook=_pairs,
                      parse_constant=lambda x: (_ for _ in ()).throw(ValueError("nonfinite JSON")))


def read_json(path):
    with Path(path).open("rb") as stream:
        return parse(stream.read(MAX_FILE + 1))


def write_new(path, value):
    with Path(path).open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write("\n")


def native_prompt():
    source = (REPO / "loom/src/extract/semantic.cpp").read_text(encoding="utf-8")
    matches = re.findall(r'kGraphPrompt = R"PROMPT\((.*?)\)PROMPT";', source, re.S)
    if len(matches) != 1:
        raise ValueError("native prompt could not be identified uniquely")
    return matches[0]


def token_spans(packet):
    """Mechanical word/punctuation boundaries; no semantic/gold annotation."""
    result = []
    for observation in packet["observations"]:
        text = observation["text"]
        for match in re.finditer(r"\w+|[^\w\s]", text, re.UNICODE):
            result.append({"observation": observation["id"],
                           "byte_start": len(text[:match.start()].encode("utf-8")),
                           "byte_len": len(match[0].encode("utf-8")), "quote": match[0]})
    return result


def messages_for(case, method):
    if method not in METHODS:
        raise ValueError("unknown extraction method")
    packet = case["source_packet"]
    prompt = native_prompt()
    user = {"packet_hash": digest(packet), "source_packet": packet}
    if method == "native_anchors_v1":
        prompt += ("\nSupport predicate and constant terms with the smallest exact source span "
                   "that identifies them, rather than the whole sentence. Preserve argument "
                   "roles: agent/subject first and patient/object second. Represent syntactic "
                   "negation explicitly. A quotation is not an assertion by the speaker. "
                   "If a central referent or scope cannot be resolved, retain located "
                   "ambiguity instead of guessing. The supplied token_spans table is only "
                   "a mechanical UTF-8 coordinate aid; it supplies no interpretation. "
                   "Return JSON only, without a reasoning transcript.\n")
        user["token_spans"] = token_spans(packet)
    return [{"role": "system", "content": prompt},
            {"role": "user", "content": canonical(user).decode("utf-8")}]


def validate_request(request):
    fields = {"schema", "enabled", "experiment_id", "split", "methods", "models",
              "budget_usd", "max_tokens", "max_requests"}
    if not isinstance(request, dict) or set(request) != fields:
        raise ValueError("unexpected pilot request fields")
    if request["schema"] != "loom.live_pilot_request/1" or type(request["enabled"]) is not bool:
        raise ValueError("invalid pilot request schema/enabled flag")
    if not isinstance(request["experiment_id"], str) or not re.fullmatch(r"[a-zA-Z0-9_-]{1,96}", request["experiment_id"]):
        raise ValueError("invalid experiment_id")
    if request["split"] not in {"dev", "validation"}:
        raise ValueError("invalid split")
    methods = request["methods"]
    if not isinstance(methods, list) or not methods or any(x not in METHODS for x in methods) or len(set(methods)) != len(methods):
        raise ValueError("invalid methods")
    models = request["models"]
    if not isinstance(models, list) or not 1 <= len(models) <= 4:
        raise ValueError("one to four explicit model/provider pairs required")
    pairs = []
    for model in models:
        if not isinstance(model, dict) or set(model) != {"id", "provider"}:
            raise ValueError("model requires exact id/provider")
        for value in model.values():
            if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_.:/-]{1,180}", value) or ".." in value:
                raise ValueError("invalid model/provider identifier")
        pairs.append((model["id"], model["provider"]))
    if len(set(pairs)) != len(pairs):
        raise ValueError("duplicate model/provider pair")
    for field, low, high in (("max_tokens", 256, 16384), ("max_requests", 1, 128)):
        if type(request[field]) is not int or not low <= request[field] <= high:
            raise ValueError("invalid " + field)
    if type(request["budget_usd"]) not in (int, float) or not math.isfinite(request["budget_usd"]) or not Decimal("0") < Decimal(str(request["budget_usd"])) <= Decimal("2"):
        raise ValueError("pilot budget must be positive and at most USD 2")
    return request


def load_inputs(split):
    manifest = read_json(FIXTURES / "manifest.json")
    name = "inputs." + split + ".json"
    raw = (FIXTURES / name).read_bytes()
    if hashlib.sha256(raw).hexdigest() != manifest["files_sha256"][name]:
        raise ValueError("frozen inputs changed")
    inputs = parse(raw)
    expected_ids = {x["case_id"] for x in manifest["cases"] if x["split"] == split}
    if len(inputs) != len(expected_ids) or {x["case_id"] for x in inputs} != expected_ids:
        raise ValueError("input case inventory mismatch")
    return inputs, manifest


def fetch_endpoints(model):
    url = "https://openrouter.ai/api/v1/models/" + model + "/endpoints"
    with urllib.request.urlopen(url, timeout=20) as response:
        return parse(response.read(MAX_FILE + 1))


def prepare(request, output_dir, *, endpoint_loader=fetch_endpoints):
    """Read-only API metadata + immutable local plan; no credential or inference."""
    validate_request(request)
    cases, fixture_manifest = load_inputs(request["split"])
    total = len(cases) * len(request["methods"]) * len(request["models"])
    if total > request["max_requests"]:
        raise ValueError("case/method/model product exceeds max_requests")
    try:
        from .openrouter_runner import plan_manifest, estimate_reservation
    except ImportError:
        from openrouter_runner import plan_manifest, estimate_reservation
    records, evidence, snapshots = [], [], []
    retrieved_at = datetime.now(timezone.utc).isoformat()
    for choice in request["models"]:
        data = endpoint_loader(choice["id"])
        if data["data"]["id"] != choice["id"]:
            raise ValueError("endpoint model identity mismatch")
        found = [e for e in data["data"]["endpoints"] if e["tag"] == choice["provider"]]
        if len(found) != 1:
            raise ValueError("explicit provider endpoint unavailable or ambiguous")
        endpoint = found[0]
        if endpoint.get("status") != 0:
            raise ValueError("explicit provider endpoint currently unavailable")
        if not {"response_format", "temperature", "max_tokens"} <= set(endpoint["supported_parameters"]):
            raise ValueError("endpoint lacks required request parameters")
        if request["max_tokens"] > endpoint["max_completion_tokens"]:
            raise ValueError("output budget exceeds endpoint limit")
        rates = {key: Decimal(str(endpoint["pricing"][key])) * 1000000 for key in ("prompt", "completion")}
        reported_model_ids = {choice["id"], endpoint.get("model_id", choice["id"])}
        endpoint_name = endpoint.get("name", "")
        if " | " in endpoint_name:
            reported_model_ids.add(endpoint_name.split(" | ", 1)[1])
        response_identity = {"model_ids": sorted(reported_model_ids),
                             "providers": sorted({choice["provider"], endpoint.get("provider_name", choice["provider"])})}
        evidence.append({"model": choice["id"], "provider": choice["provider"],
                         "pricing": endpoint["pricing"], "retrieved_at": retrieved_at,
                         "source_url": "https://openrouter.ai/api/v1/models/" + choice["id"] + "/endpoints"})
        snapshots.append(data)
        for case in cases:
            for method in request["methods"]:
                body = {"model": choice["id"], "messages": messages_for(case, method),
                        "max_tokens": request["max_tokens"], "temperature": 0, "stream": False,
                        "usage": {"include": True},
                        "response_format": {"type": "json_object"},
                        "provider": {"only": [choice["provider"]], "allow_fallbacks": False,
                                     "require_parameters": True,
                                     "max_price": {k: float(v) for k, v in rates.items()}}}
                reserved = estimate_reservation(body)["minimum_reservation_usd"]
                # Request IDs carry no semantic labels and are stable within the frozen plan.
                identifier = "r" + digest({"case": case["case_id"], "method": method, "choice": choice})[:24]
                records.append({"id": identifier, "body": body, "reservation_usd": str(reserved),
                                "metadata": {"case_id": case["case_id"], "method": method,
                                             "language": case["language"], "packet_hash": digest(case["source_packet"]),
                                             "response_identity": response_identity}})
    # Interleave models: an interrupted run should not contain just one model's cases.
    records.sort(key=lambda x: (x["metadata"]["case_id"], x["metadata"]["method"], x["body"]["model"]))
    manifest = {"schema": "loom.openrouter_manifest/1", "experiment_id": request["experiment_id"],
                "budget_usd": request["budget_usd"], "max_requests": request["max_requests"],
                "requests": records, "pricing_evidence": evidence,
                "metadata": {"pilot_request": request, "fixture_manifest_sha256": digest(fixture_manifest),
                             "code_sha256": code_hashes(),
                             "native_prompt_sha256": hashlib.sha256(native_prompt().encode()).hexdigest(),
                             "live_calls_made_by_prepare": 0, "representation": "occurrence_graph_v1",
                             "transport_difference": "research runner, output cap may exceed native cap; JSON object mode"}}
    plan = plan_manifest(manifest)
    output = Path(output_dir)
    output.mkdir(parents=True, exist_ok=False)
    write_new(output / "manifest.json", manifest)
    write_new(output / "plan.json", plan)
    write_new(output / "inputs.json", cases)
    write_new(output / "endpoint_snapshots.json", snapshots)
    return plan


def score(manifest_path, run_dir, output):
    try:
        from .live_pilot_score import score_case
    except ImportError:
        from live_pilot_score import score_case
    manifest = read_json(manifest_path)
    if manifest["metadata"].get("code_sha256") != code_hashes():
        raise ValueError("experiment code changed; score from the frozen preparation commit")
    split = manifest["metadata"]["pilot_request"]["split"]
    cases, fixture_manifest = load_inputs(split)
    if digest(fixture_manifest) != manifest["metadata"]["fixture_manifest_sha256"]:
        raise ValueError("scorer fixture manifest differs from request plan")
    gold_name = "gold." + split + ".json"
    gold_bytes = (FIXTURES / gold_name).read_bytes()
    if hashlib.sha256(gold_bytes).hexdigest() != fixture_manifest["files_sha256"][gold_name]:
        raise ValueError("frozen gold changed")
    by_case = {x["case_id"]: x for x in cases}
    gold = {x["case_id"]: x for x in parse(gold_bytes)}
    ledger = read_json(Path(run_dir) / "ledger.json")
    if ledger["manifest_hash"] != digest(manifest):
        raise ValueError("ledger does not belong to the frozen manifest")
    try:
        from .openrouter_runner import plan_manifest, _validate_ledger
    except ImportError:
        from openrouter_runner import plan_manifest, _validate_ledger
    checked_attempts = _validate_ledger(ledger, plan_manifest(manifest), Path(run_dir))
    attempts = {x["id"]: x for x in checked_attempts}
    rows = []
    for request in manifest["requests"]:
        meta = request["metadata"]
        row = {"request_id": request["id"], **meta, "model": request["body"]["model"],
               "provider": request["body"]["provider"]["only"][0], "status": "not_attempted",
               "score": None, "semantic_exact": False}
        attempt = attempts.get(request["id"])
        if attempt:
            row["status"] = attempt["state"]
            if attempt["state"] == "completed":
                filename = attempt["response_file"]
                if Path(filename).name != filename:
                    raise ValueError("unsafe ledger response path")
                raw = (Path(run_dir) / filename).read_bytes()
                if hashlib.sha256(raw).hexdigest() != attempt["response_sha256"]:
                    raise ValueError("response hash mismatch")
                try:
                    response = parse(raw)
                    row["observed_model"] = response.get("model")
                    row["observed_provider"] = response.get("provider")
                    identity = meta["response_identity"]
                    if (response.get("model") not in identity["model_ids"] or
                            response.get("provider") not in identity["providers"]):
                        row["status"] = "identity_mismatch"
                        rows.append(row)
                        continue
                    choices = response["choices"]
                    if len(choices) != 1 or choices[0]["finish_reason"] != "stop":
                        raise ValueError("non-final response")
                    envelope = parse(choices[0]["message"]["content"].encode("utf-8"))
                    if not isinstance(envelope, dict) or set(envelope) != {"schema_version", "packet_hash", "bundles"}:
                        raise ValueError("invalid candidate response envelope")
                    if type(envelope["schema_version"]) is not int or envelope["schema_version"] != 2 or envelope["packet_hash"] != meta["packet_hash"]:
                        raise ValueError("candidate envelope identity mismatch")
                    bundles = envelope["bundles"]
                    if not isinstance(bundles, list) or len(bundles) > 16:
                        raise ValueError("invalid alternative count")
                    row["alternative_count"] = len(bundles)
                    if not bundles:
                        row["status"] = "unlocated_abstention"
                    else:
                        # Predeclared first reading; never choose the closest alternative using gold.
                        row["score"] = score_case(by_case[meta["case_id"]], gold[meta["case_id"]], bundles[0])
                        row["status"] = "scored"
                        row["semantic_exact"] = bool(row["score"].get("semantic_exact", False))
                except (ValueError, TypeError, KeyError, IndexError, RecursionError):
                    row["status"] = "invalid_response"
        rows.append(row)
    groups = defaultdict(list)
    for row in rows:
        groups[(row["model"], row["provider"], row["method"], row["language"])].append(row)
    summary = []
    for (model, provider, method, language), items in sorted(groups.items()):
        summary.append({"model": model, "provider": provider, "method": method, "language": language,
                        "planned": len(items), "scored": sum(x["status"] == "scored" for x in items),
                        "semantic_exact": sum(x["semantic_exact"] for x in items),
                        "semantic_exact_over_planned": sum(x["semantic_exact"] for x in items) / len(items)})
    result = {"schema": "loom.live_pilot_report/1", "manifest_hash": digest(manifest),
              "split": split, "rows": rows, "summary": summary,
              "limitations": ["small authored diagnostic sample; not natural conversation accuracy",
                              "first alternative only; no gold-based selection or response healing",
                              "transport failures and missing responses remain in planned denominator",
                              "native prompt reused; research transport/output limits differ from native execution"],
              "no_persistence": True}
    write_new(output, result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("prepare")
    p.add_argument("--request", required=True)
    p.add_argument("--output-dir", required=True)
    p = sub.add_parser("score")
    p.add_argument("--manifest", required=True)
    p.add_argument("--run-dir", required=True)
    p.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        result = (prepare(read_json(args.request), args.output_dir) if args.command == "prepare"
                  else score(args.manifest, args.run_dir, args.output))
        print(json.dumps(result.get("summary", result), ensure_ascii=False, indent=2))
    except (ValueError, OSError, KeyError, TypeError) as exc:
        # No credentials or provider bodies are included in diagnostics.
        parser.exit(2, "pilot " + args.command + " failed (" + type(exc).__name__ + ")\n")


if __name__ == "__main__":
    main()
