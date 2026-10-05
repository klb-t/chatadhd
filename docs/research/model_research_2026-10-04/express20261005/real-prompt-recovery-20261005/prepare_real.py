#!/usr/bin/env python3
"""Offline real-export adapter for the unchanged 7B materializer and scorer.

No provider transport, pricing lookup, payer ledger, native write or synthetic
model experiment. The input annotation is a private, provisional reference;
source binding proves provenance, not the correctness of its semantic labels.
"""
from __future__ import annotations

import argparse
import copy
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import tempfile
from typing import Any


class PreparationError(ValueError):
    """Stable diagnostic codes, without private input values."""


def require(condition: bool, code: str) -> None:
    if not condition:
        raise PreparationError(code)


def canonical(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def digest(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def decode(raw: bytes) -> Any:
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "duplicate_json_key")
            result[key] = value
        return result

    def number(text):
        value = float(text)
        require(math.isfinite(value), "nonfinite_json_number")
        return value

    def constant(_):
        raise PreparationError("nonfinite_json_number")

    return json.loads(raw, object_pairs_hook=pairs, parse_float=number,
                      parse_constant=constant)


def bound(path: Path, expected: str) -> bytes:
    require(isinstance(expected, str) and re.fullmatch(r"[0-9a-f]{64}", expected) is not None,
            "invalid_expected_hash")
    raw = path.read_bytes()
    require(digest(raw) == expected, "input_hash_changed")
    return raw


def module(path: Path, expected: str, name: str):
    # Execute exactly the verified bytes, not a potentially stale pyc cache.
    raw = bound(path, expected)
    spec = importlib.util.spec_from_loader(name, loader=None, origin=str(path))
    result = importlib.util.module_from_spec(spec)
    result.__file__ = str(path)
    exec(compile(raw, str(path), "exec"), result.__dict__)
    return result


def iso_time(value: Any) -> str:
    require(type(value) in (int, float) and math.isfinite(value), "unknown_source_time")
    try:
        return datetime.fromtimestamp(value, timezone.utc).isoformat(timespec="microseconds")
    except (ValueError, OverflowError, OSError):
        raise PreparationError("unsupported_source_time") from None


def source_text(native: dict) -> tuple[str, dict]:
    content = native.get("content")
    require(isinstance(content, dict) and isinstance(content.get("parts"), list),
            "unsupported_native_content")
    parts = content["parts"]
    strings = [p for p in parts if isinstance(p, str)]
    return "\n".join(strings), {
        "method": "native_string_parts_joined_by_newline",
        "content_type": content.get("content_type"),
        "text_parts": len(strings), "nontext_parts_not_interpreted": len(parts) - len(strings),
        "native_message_preserved_in_capsule": True,
        "attachments_decoded": False,
    }


def compile_sources(graph: dict, annotation: dict, protocol: dict) -> tuple[dict, dict, list[dict]]:
    require(graph.get("schema") == "loom.real_source_intake_private/1", "graph_schema_changed")
    require(annotation.get("schema") == "loom.real_source_reference_annotations/1",
            "annotation_schema_changed")
    require(annotation.get("review_status") == protocol["reference_status"], "reference_status_changed")
    require(annotation.get("independent_review") is False, "independent_review_not_established")
    rows = annotation.get("questions")
    require(isinstance(rows, list) and bool(rows), "empty_question_inventory")
    case_ids = [c.get("case_id") for c in graph["cases"]]
    require(len(case_ids) == len(set(case_ids)), "duplicate_source_case")
    questions = {}; seen = set()
    for row in rows:
        require(isinstance(row, dict), "invalid_question")
        qid = row.get("id")
        require(isinstance(qid, str) and re.fullmatch(r"[A-Za-z0-9_.-]+", qid) is not None
                and qid not in seen, "duplicate_or_invalid_query_id")
        require(row.get("case_id") in case_ids, "unknown_question_case")
        seen.add(qid)
        questions.setdefault(row["case_id"], []).append(row)
    require(set(questions) == set(case_ids), "source_case_coverage_gap")
    cases = []; gold_cases = []; payloads = []
    for source in graph["cases"]:
        view = source["view"]; nodes = view["nodes"]
        node_ids = [n["node_id"] for n in nodes]
        # This compatibility adapter is exact only for saved linear histories.
        # It refuses other shapes instead of flattening branches into chronology.
        require(len(node_ids) == len(set(node_ids)) and node_ids == view["current_path_node_ids"],
                "branched_source_requires_graph_aware_adapter")
        turns = []; times = []; previous_node = None
        for node in nodes:
            require(node["parent_node_id"] == previous_node, "source_parent_order_changed")
            previous_node = node["node_id"]
            native = node["native_message"]
            if native is None:
                require(node["turn_id"] is None, "missing_native_message")
                continue
            require(isinstance(node["turn_id"], str) and bool(node["turn_id"]), "missing_turn_id")
            require(digest(canonical(native)) == node["source_message_sha256"], "native_hash_changed")
            require(node["role"] == native["author"]["role"], "source_speaker_changed")
            stamp = iso_time(node["source_create_time"])
            times.append(stamp)
            # Equal rounded clocks are not sufficient for exact before/after views.
            require(len(times) < 2 or times[-1] > times[-2], "non_strict_source_chronology")
            text, projection = source_text(native)
            for pattern in protocol["credential_patterns"]:
                require(re.search(pattern, text) is None, "credential_like_source_requires_review")
            turns.append({"id": node["turn_id"], "speaker": node["role"], "text": text,
                          "known_at": stamp, "source_create_time": node["source_create_time"],
                          "source_message_sha256": node["source_message_sha256"],
                          "parent_node_id": node["parent_node_id"], "node_id": node["node_id"],
                          "projection": projection})
        require([t["id"] for t in turns] == view["topological_turn_ids"], "turn_order_coverage_changed")
        require(len({t["id"] for t in turns}) == len(turns), "duplicate_turn_id")
        positions = {t["id"]: i for i, t in enumerate(turns)}
        source_id = "source_" + source["source_hashes"]["original_canonical_sha256"]
        case = {"id": source["case_id"], "source_id": source_id,
                "language": protocol["source_language_description"], "turns": turns,
                # Query-time schema, not a future-derived entity catalogue.
                "node_inventory": {"type": "query_terms_not_extracted_entities",
                                   "relations": "ordered_subject_predicate_object"},
                "judgment_queries": []}
        judgments = []
        for row in questions[source["case_id"]]:
            require(row.get("as_of_turn_id") in positions, "unknown_as_of_turn")
            idx = positions[row["as_of_turn_id"]]
            require(row.get("speaker") in {t["speaker"] for t in turns[:idx + 1]}, "unknown_attributed_speaker")
            proposition = row.get("proposition")
            require(isinstance(proposition, dict) and set(proposition) == {"subject", "predicate", "object"}
                    and all(isinstance(v, str) and bool(v) for v in proposition.values()), "invalid_directed_proposition")
            gold = row.get("gold", {})
            require(gold.get("label") in protocol["labels"], "invalid_reference_label")
            evidence = gold.get("evidence")
            require(isinstance(evidence, list), "invalid_reference_evidence")
            require(gold["label"] == "unknown" or bool(evidence), "decisive_evidence_required")
            require(isinstance(gold.get("rationale"), str) and bool(gold["rationale"]), "reference_rationale_required")
            for citation in evidence:
                require(citation.get("turn_id") in positions, "unknown_evidence_turn")
                pos = positions[citation["turn_id"]]
                require(pos <= idx, "future_evidence_not_visible")
                turn = turns[pos]
                require(turn["speaker"] == row["speaker"], "reference_speaker_mismatch")
                require(citation.get("source_message_sha256") == turn["source_message_sha256"], "reference_message_hash_changed")
                quote = citation.get("quote"); start = citation.get("start_character"); end = citation.get("end_character")
                require(isinstance(quote, str) and bool(quote) and type(start) is int and type(end) is int
                        and 0 <= start < end <= len(turn["text"]) and turn["text"][start:end] == quote,
                        "reference_quote_span_changed")
                require(citation.get("start_utf8") == len(turn["text"][:start].encode())
                        and citation.get("end_utf8") == len(turn["text"][:end].encode()), "reference_utf8_span_changed")
            query = {"id": row["id"], "speaker": row["speaker"], "as_of": turns[idx]["known_at"],
                     "as_of_turn_id": row["as_of_turn_id"], "proposition": proposition,
                     "scope": protocol["query_scope"]}
            visible = turns[:idx + 1]
            require(visible == [t for t in turns if t["known_at"] <= query["as_of"]], "temporal_projection_not_exact")
            case["judgment_queries"].append(query)
            judgments.append({"query_id": row["id"], **copy.deepcopy(gold)})
            payloads.append({"case_id": case["id"], "source_id": source_id,
                             "turns": visible, "node_inventory": case["node_inventory"], "query": query})
        cases.append(case); gold_cases.append({"id": case["id"], "judgments": judgments})
    require([p["query"]["id"] for p in payloads] == [r["id"] for r in rows], "query_case_order_changed")
    return ({"schema": "loom.real_source_label_inputs/1", "cases": cases},
            {"schema": "loom.real_source_reference_gold/1", "review_status": annotation["review_status"],
             "independent_review": False, "world_truth_validation": False, "cases": gold_cases}, payloads)


def write(path: Path, value: Any) -> bytes:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    raw = canonical(value) + b"\n"
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as handle:
        handle.write(raw)
    return raw


def outside_git(path: Path) -> None:
    for parent in (path.resolve(), *path.resolve().parents):
        require(not (parent / ".git").exists(), "private_output_inside_git")


def prepare_bundle(*, graph: dict, annotation: dict, protocol: dict, original_design: dict,
                   upstream, out: Path, provenance: dict) -> dict:
    """Create a new private bundle, publishing its directory only on success."""
    require(not out.exists(), "output_already_exists")
    outside_git(out.parent)
    require(protocol.get("schema") == "loom.real_prompt_recovery_protocol/1", "protocol_schema_changed")
    require(set(provenance) == {"capsule", "graph_projection", "annotations", "protocol", "producer",
                                "intake", "upstream", "upstream_design"}
            and all(isinstance(v, str) and re.fullmatch(r"[0-9a-f]{64}", v) is not None
                    for v in provenance.values()), "invalid_public_hash_provenance")
    inputs, gold, payloads = compile_sources(graph, annotation, protocol)
    out.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    os.chmod(out.parent, 0o700)
    with tempfile.TemporaryDirectory(prefix=".real-prompt-preparing-", dir=out.parent) as temporary:
        root = Path(temporary)
        inputs_raw = write(root / "inputs.json", inputs)
        gold_raw = write(root / "gold.json", gold)
        write(root / "reference-annotations.json", annotation)
        write(root / "protocol.json", protocol)
        source_ops = []
        for i, payload in enumerate(payloads):
            name = f"queries/{i:04d}.json"
            raw = write(root / "source-views" / name, {"state": {"text": canonical(payload).decode()}})
            source_ops.append({"request_file": name, "request_sha256": digest(raw),
                               "metadata": {"arm_id": "real_source_view",
                                            "context_preparation": {"case_id": payload["query"]["id"]}}})
        source_raw = write(root / "source-views/manifest.json", {
            "schema": "loom.real_query_source_views/1", "not_executable": True,
            "operations": source_ops})
        design = copy.deepcopy(original_design)
        require(design.get("schema") == "loom.express_prompt_parameters.design/1", "upstream_design_schema_changed")
        require(protocol["stage_id"] != design["stage_id"], "old_stage_identity_reused")
        design.update(stage_id=protocol["stage_id"], study_id=protocol["stage_id"],
                      collection_status="prepared_unexecuted_provisional_reference")
        design["selection"] = {"family_ids": [c["id"] for c in inputs["cases"]],
                               "query_ids": [p["query"]["id"] for p in payloads],
                               "rule": protocol["question_selection_description"],
                               "split": protocol["population_description"]}
        design["source"] = {"inputs_file": "inputs.json", "inputs_sha256": digest(inputs_raw),
                            "gold_file": "gold.json", "gold_sha256": digest(gold_raw),
                            "prepared_manifest_file": "source-views/manifest.json",
                            "prepared_manifest_sha256": digest(source_raw), "prepared_arm": "real_source_view"}
        design_raw = write(root / "design.json", design)
        old_mask = os.umask(0o077)
        try:
            manifest = upstream.prepare(root / "design.json", root / "prepared")
        finally:
            os.umask(old_mask)
        # Reuse the original scorer's exact source/time/gold/request binder.
        _, _, selected = upstream._scoring_sources(root / "design.json", digest(design_raw))
        require(len(selected) == len(payloads), "scorer_source_binding_incomplete")
        write(root / "PROVENANCE.json", provenance)
        inventory = {str(p.relative_to(root)): digest(p.read_bytes())
                     for p in sorted(root.rglob("*")) if p.is_file()}
        freeze = {"schema": "loom.real_prompt_recovery_freeze/1", "files": inventory,
                  "source_provenance": provenance, "operation_count": len(manifest["operations"]),
                  "reference_status": protocol["reference_status"], "independent_review": False,
                  "new_paid_calls": 0, "dispatch_ready": False, "missing_gates": protocol["missing_gates"]}
        freeze_raw = write(root / "FREEZE.json", freeze)
        for p in root.rglob("*"):
            os.chmod(p, 0o700 if p.is_dir() else 0o600)
        counts = {"conversations": len(inputs["cases"]), "source_messages": sum(len(c["turns"]) for c in inputs["cases"]),
                  "questions": len(payloads), "configurations": manifest["metadata"]["configuration_count"],
                  "prepared_operations": len(manifest["operations"]),
                  "nontext_parts_not_interpreted": sum(t["projection"]["nontext_parts_not_interpreted"]
                                                       for c in inputs["cases"] for t in c["turns"])}
        receipt = {"schema": "loom.real_prompt_recovery_receipt/1", "counts": counts,
                   "hashes": {"private_freeze": digest(freeze_raw), "design": digest(design_raw),
                              "inputs": digest(inputs_raw), "reference_gold": digest(gold_raw),
                              "manifest": inventory["prepared/manifest.json"], **provenance},
                   "offline_preparation_complete": True, "dispatch_ready": False,
                   "reference_status": protocol["reference_status"], "independent_review": False,
                   "model_quality": None, "new_paid_calls": 0, "new_spend_usd": "0",
                   "public_full_replay": False, "raw_source_published": False,
                   "missing_gates": protocol["missing_gates"]}
        # Parent directory is private. A racing nonempty target causes rename
        # to fail; fail closed if any target appeared after initial validation.
        require(not out.exists(), "output_already_exists")
        os.rename(root, out)
    return receipt


def score_real(upstream, design_path: Path, manifest_path: Path, bundle_raw: bytes, *,
               protocol: dict, **bindings) -> dict:
    score = upstream.score_first_responses(design_path, manifest_path, bundle_raw, **bindings)
    score["evidence_boundary"] = protocol["scoring_evidence_boundary"]
    score["reference_review"] = {"status": protocol["reference_status"], "independent_review": False}
    score["upstream_algorithm_unchanged"] = True
    return score


def shared_metrics_real(upstream, score: dict, *, source_id: str, protocol: dict) -> dict:
    metrics = upstream.shared_metrics_by_configuration(score, source_id=source_id)
    for values in metrics.values():
        values["source_gold_label_match_all_planned"]["note"] = protocol["reference_metric_note"]
    return metrics


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capsule", type=Path, required=True)
    parser.add_argument("--annotations", type=Path, required=True)
    parser.add_argument("--annotations-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dependencies", type=Path, default=Path(__file__).with_name("dependencies.json"))
    parser.add_argument("--protocol", type=Path, default=Path(__file__).with_name("protocol.json"))
    args = parser.parse_args()
    deps = decode(args.dependencies.read_bytes()); root = args.dependencies.parent
    intake = module(root / deps["intake"]["path"], deps["intake"]["sha256"], "pinned_real_intake")
    upstream = module(root / deps["upstream"]["path"], deps["upstream"]["sha256"], "pinned_prompt_preparation")
    layout_raw = bound(root / deps["layout"]["path"], deps["layout"]["sha256"])
    design_raw = bound(root / deps["design"]["path"], deps["design"]["sha256"])
    annotation_raw = bound(args.annotations, args.annotations_sha256)
    protocol_raw = args.protocol.read_bytes()
    files, capsule_hash = intake.load_capsule(args.capsule, deps["capsule_sha256"])
    graph, _ = intake.verify_and_project(files, capsule_hash, decode(layout_raw))
    provenance = {"capsule": capsule_hash, "graph_projection": digest(canonical(graph)),
                  "annotations": digest(annotation_raw), "protocol": digest(protocol_raw),
                  "producer": digest(Path(__file__).read_bytes()), "intake": deps["intake"]["sha256"],
                  "upstream": deps["upstream"]["sha256"], "upstream_design": deps["design"]["sha256"]}
    receipt = prepare_bundle(graph=graph, annotation=decode(annotation_raw), protocol=decode(protocol_raw),
                             original_design=decode(design_raw), upstream=upstream, out=args.output,
                             provenance=provenance)
    print(json.dumps(receipt, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (PreparationError, ValueError, OSError, KeyError, TypeError) as error:
        print(json.dumps({"status": "blocked", "code": str(error) if isinstance(error, PreparationError)
                          else type(error).__name__}, sort_keys=True))
        raise SystemExit(2)
