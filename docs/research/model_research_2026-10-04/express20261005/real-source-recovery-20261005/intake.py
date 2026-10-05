#!/usr/bin/env python3
"""Verify a frozen private source capsule and add a loss-labelled graph view.

Offline only. Never edits the capsule, sends provider requests, or writes source
text to the public receipt. Archive array order is not conversation chronology.
"""
from __future__ import annotations

import argparse
import hashlib
import heapq
import io
import math
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import zipfile
from typing import Any


class IntakeError(ValueError):
    """A stable diagnostic code, without private source values."""


def require(condition: bool, code: str) -> None:
    if not condition:
        raise IntakeError(code)


def canonical(value: Any) -> bytes:
    # Original-conversation canonical hashes exclude the terminating newline.
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        require(key not in result, "duplicate_json_key")
        result[key] = value
    return result


def _nonfinite(_: str) -> None:
    raise IntakeError("nonfinite_json_number")


def _float(text: str) -> float:
    value = float(text)
    require(math.isfinite(value), "nonfinite_json_number")
    return value


def decode(data: bytes) -> Any:
    return json.loads(data, object_pairs_hook=_pairs, parse_constant=_nonfinite,
                      parse_float=_float)


def safe_member(name: str) -> str:
    require(isinstance(name, str) and bool(name), "invalid_member_path")
    p = PurePosixPath(name)
    require(not p.is_absolute() and "\\" not in name and "\x00" not in name
            and ":" not in name and all(x not in ("", ".", "..")
                                       for x in name.split("/")),
            "unsafe_member_path")
    return name


def load_capsule(path: Path, expected_sha256: str) -> tuple[dict[str, bytes], str]:
    require(re.fullmatch(r"[0-9a-f]{64}", expected_sha256) is not None,
            "invalid_expected_capsule_hash")
    raw = path.read_bytes()
    require(sha256(raw) == expected_sha256, "capsule_hash_mismatch")
    result: dict[str, bytes] = {}
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        for info in archive.infolist():
            if info.is_dir():
                safe_member(info.filename.rstrip("/"))
                continue
            name = safe_member(info.filename)
            require(name not in result, "duplicate_zip_member")
            require(not stat.S_ISLNK(info.external_attr >> 16), "zip_symlink")
            require(not info.flag_bits & 1, "encrypted_zip_member")
            result[name] = archive.read(info)  # zipfile verifies member CRC.
    return result, expected_sha256


def _read(files: dict[str, bytes], name: str) -> bytes:
    name = safe_member(name)
    require(name in files, "missing_capsule_member")
    return files[name]


def project_case(original: dict[str, Any], wrapped: dict[str, Any]) -> tuple[dict, dict]:
    """Keep all saved branches and existing turn IDs; derive a partial-order view."""
    require(isinstance(original, dict) and isinstance(wrapped, dict), "invalid_case_container")
    mapping = original.get("mapping")
    require(isinstance(mapping, dict) and bool(mapping), "missing_native_mapping")
    require(wrapped.get("native_mapping_including_saved_branches") == mapping,
            "native_mapping_mismatch")
    rows = wrapped.get("messages")
    require(isinstance(rows, list), "invalid_message_array")
    message_nodes: dict[str, str] = {}
    for node_id, node in mapping.items():
        require(isinstance(node_id, str) and isinstance(node, dict), "invalid_node")
        msg = node.get("message")
        if msg is not None:
            require(isinstance(msg, dict) and isinstance(msg.get("id"), str),
                    "invalid_native_message")
            require(isinstance(msg.get("author"), dict), "invalid_native_author")
            require(msg["id"] not in message_nodes, "ambiguous_native_message_id")
            message_nodes[msg["id"]] = node_id
    by_node: dict[str, dict] = {}
    turn_ids: set[str] = set()
    for index, row in enumerate(rows):
        require(isinstance(row, dict), "invalid_projected_message")
        native = row.get("native_message")
        require(isinstance(native, dict), "missing_native_message")
        node_id = message_nodes.get(native.get("id"))
        require(node_id is not None, "unbound_projected_message")
        require(node_id not in by_node, "duplicate_projected_message")
        require(native == mapping[node_id]["message"], "native_message_mismatch")
        require(row.get("role") == native.get("author", {}).get("role"),
                "speaker_role_mismatch")
        turn = row.get("turn_id")
        require(isinstance(turn, str) and bool(turn) and turn not in turn_ids,
                "invalid_or_duplicate_turn_id")
        turn_ids.add(turn)
        by_node[node_id] = {"row": row, "array_index": index}
    require(set(by_node) == set(message_nodes.values()), "native_message_coverage_gap")

    # Parent links define ancestry. Array position and clocks cannot override it.
    ordinal = {node_id: i for i, node_id in enumerate(mapping)}
    children: dict[str, list[str]] = {node_id: [] for node_id in mapping}
    pending: dict[str, int] = {}
    for node_id, node in mapping.items():
        parent = node.get("parent")
        require(parent is None or (isinstance(parent, str) and parent in mapping),
                "missing_native_parent")
        pending[node_id] = int(parent is not None)
        if parent is not None:
            children[parent].append(node_id)
    for node_id, node in mapping.items():
        if "children" in node:
            declared = node["children"]
            require(isinstance(declared, list) and all(isinstance(x, str) for x in declared),
                    "invalid_declared_children")
            require(len(set(declared)) == len(declared) and set(declared) == set(children[node_id]),
                    "native_parent_children_mismatch")
    ready = [(ordinal[node_id], node_id) for node_id, degree in pending.items()
             if degree == 0]
    heapq.heapify(ready)
    ordered: list[str] = []
    while ready:
        _, node_id = heapq.heappop(ready)
        ordered.append(node_id)
        for child in children[node_id]:
            pending[child] -= 1
            if not pending[child]:
                heapq.heappush(ready, (ordinal[child], child))
    require(len(ordered) == len(mapping), "native_parent_cycle")
    current = original.get("current_node")
    require(current is None or (isinstance(current, str) and current in mapping), "missing_current_node")
    active: list[str] = []
    cursor = current
    while cursor is not None:
        active.append(cursor)
        cursor = mapping[cursor].get("parent")
    active.reverse()
    alias = {node_id: "node_" + sha256(node_id.encode()) for node_id in mapping}
    nodes: list[dict] = []
    inversions = 0
    clock_conflicts = 0
    missing_times = 0
    for node_id in ordered:
        native = mapping[node_id].get("message")
        parent = mapping[node_id].get("parent")
        bound = by_node.get(node_id)
        parent_bound = by_node.get(parent)
        if bound and parent_bound:
            inversions += int(parent_bound["array_index"] > bound["array_index"])
        stamp = native.get("create_time") if native is not None else None
        if native is not None:
            missing_times += int(stamp is None)
        parent_native = mapping[parent].get("message") if parent is not None else None
        parent_stamp = parent_native.get("create_time") if parent_native is not None else None
        if type(stamp) in (int, float) and type(parent_stamp) in (int, float):
            clock_conflicts += int(stamp < parent_stamp)
        nodes.append({"node_id": alias[node_id],
                      "parent_node_id": alias[parent] if parent is not None else None,
                      "turn_id": bound["row"]["turn_id"] if bound else None,
                      "source_array_index": bound["array_index"] if bound else None,
                      "role": bound["row"]["role"] if bound else None,
                      "source_create_time": stamp,
                      "source_message_sha256": sha256(canonical(native)) if native is not None else None,
                      "native_message": native,
                      "source_pointer": "/mapping/" + node_id.replace("~", "~0").replace("/", "~1")})
    view = {"schema": "loom.real_source_graph_view/1", "nodes": nodes,
            "source_array_turn_ids": [row["turn_id"] for row in rows],
            "topological_turn_ids": [by_node[n]["row"]["turn_id"] for n in ordered if n in by_node],
            "current_path_node_ids": [alias[n] for n in active],
            "leaf_node_ids": [alias[n] for n in ordered if not children[n]],
            "order_semantics": "parent_partial_order; sibling order is not asserted chronology",
            "timestamp_semantics": "unaltered source values; null stays unknown",
            "projection_losses": ["top-level conversation and additional node metadata remain in the immutable capsule",
                                  "binary attachments are not materialized by this view; consult the capsule inventory",
                                  "this derived graph view is not a provider request or semantic gold"]}
    counts = {"native_nodes": len(mapping), "messages": len(rows),
              "parent_edges": sum(node.get("parent") is not None for node in mapping.values()),
              "array_parent_inversions": inversions,
              "parent_clock_conflicts": clock_conflicts,
              "messages_without_create_time": missing_times,
              "saved_leaves": len(view["leaf_node_ids"]),
              "current_path_nodes": len(active)}
    return view, counts


def verify_and_project(files: dict[str, bytes], capsule_hash: str, layout: dict) -> tuple[dict, dict]:
    require(layout.get("schema") == "loom.real_source_capsule_layout/1",
            "unsupported_layout_schema")
    source = safe_member(layout["source_directory"])
    previous = safe_member(layout["previous_freeze_directory"])
    selection_raw = _read(files, source + "/selection-manifest.json")
    selection = decode(selection_raw)
    require(sha256(_read(files, source + "/configuration.json")) == selection["configuration_sha256"],
            "source_configuration_hash_mismatch")
    freeze_raw = _read(files, previous + "/FREEZE.json")
    freeze = decode(freeze_raw)
    require(freeze["source_selection_sha256"] == sha256(selection_raw),
            "source_selection_hash_mismatch")
    for name, digest in freeze["files"].items():
        require(sha256(_read(files, previous + "/" + safe_member(name))) == digest,
                "previous_freeze_member_hash_mismatch")
    manifest = decode(_read(files, previous + "/prepared/manifest.json"))
    require(len(manifest["operations"]) == freeze["operations"], "operation_count_mismatch")
    redactions = freeze.get("redactions")
    require(isinstance(redactions, list), "missing_previous_source_bindings")
    previous_sources = {item["case_id"]: item for item in redactions}
    require(len(previous_sources) == len(redactions), "duplicate_previous_source_binding")
    private_cases: list[dict] = []
    public_cases: list[dict] = []
    seen: set[str] = set()
    for ordinal, selected in enumerate(selection["selected"], 1):
        case_id = safe_member(selected["case_id"])
        require("/" not in case_id and case_id not in seen, "invalid_or_duplicate_case_id")
        seen.add(case_id)
        original_raw = _read(files, source + "/" + case_id + "/original-conversation.json")
        messages_raw = _read(files, source + "/" + case_id + "/messages-full.json")
        require(case_id in previous_sources, "missing_previous_source_binding")
        require(sha256(messages_raw) == previous_sources[case_id]["source_messages_sha256"],
                "previous_source_messages_hash_mismatch")
        original = decode(original_raw)
        wrapped = decode(messages_raw)
        require(sha256(canonical(original)) == selected["full_original_conversation_canonical_sha256"],
                "original_canonical_hash_mismatch")
        require(len(canonical(original)) == selected["full_original_conversation_canonical_utf8_bytes"],
                "original_canonical_size_mismatch")
        require(wrapped.get("case_id") == case_id, "case_identity_mismatch")
        view, counts = project_case(original, wrapped)
        require(counts["messages"] == selected["messages"], "selection_message_count_mismatch")
        hashes = {"original_bytes_sha256": sha256(original_raw),
                  "original_canonical_sha256": sha256(canonical(original)),
                  "messages_bytes_sha256": sha256(messages_raw),
                  "graph_view_sha256": sha256(canonical(view))}
        private_cases.append({"case_id": case_id, "view": view, "source_hashes": hashes})
        # Allowlist construction: no copied titles, text, IDs, paths, or clocks.
        public_cases.append({"case_ordinal": ordinal, "counts": counts, "hashes": hashes})
    require(bool(private_cases), "empty_selection")
    require(seen == set(previous_sources), "previous_source_selection_coverage_gap")
    producer_hash = sha256(Path(__file__).read_bytes())
    private = {"schema": "loom.real_source_intake_private/1", "cases": private_cases,
               "capsule_sha256": capsule_hash, "previous_freeze_sha256": sha256(freeze_raw),
               "produced_by": {"method": "real_source_intake_v1", "source_sha256": producer_hash},
               "layout_sha256": sha256(canonical(layout))}
    public = {"schema": "loom.real_source_intake_receipt/1",
              "capsule_sha256": capsule_hash, "previous_freeze_sha256": sha256(freeze_raw),
              "source_selection_sha256": sha256(selection_raw),
              "private_projection_sha256": sha256(canonical(private)),
              "produced_by": private["produced_by"], "layout_sha256": private["layout_sha256"],
              "previous_freeze_files_verified": len(freeze["files"]),
              "previous_prepared_operations_preserved": freeze["operations"],
              "cases": public_cases, "source_intake_ready": True,
              "dispatch_ready": False, "new_paid_calls": 0,
              "missing_gates": ["reviewed_gold_freeze", "new_request_freeze", "central_7A_cost_preflight"],
              "privacy": {"raw_source_published": False, "public_full_replay_possible": False},
              "quality": None}
    return private, public


def outside_git(path: Path) -> None:
    for parent in (path.resolve(), *path.resolve().parents):
        require(not (parent / ".git").exists(), "private_output_inside_git")


def write_new(path: Path, value: Any, private: bool) -> None:
    if private:
        outside_git(path.parent)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    if private:
        os.chmod(path.parent, 0o700)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600 if private else 0o644)
    with os.fdopen(fd, "wb") as handle:
        handle.write(canonical(value) + b"\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capsule", type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--private-output", type=Path, required=True)
    parser.add_argument("--public-receipt", type=Path, required=True)
    args = parser.parse_args()
    files, digest = load_capsule(args.capsule, args.expected_sha256)
    private, public = verify_and_project(files, digest, decode(args.layout.read_bytes()))
    write_new(args.private_output, private, private=True)
    write_new(args.public_receipt, public, private=False)
    print(json.dumps({"cases": len(public["cases"]), "source_intake_ready": True,
                      "dispatch_ready": False, "paid_calls": 0}, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (IntakeError, OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        # Never print source values, source paths, or malformed JSON payloads.
        print(json.dumps({"status": "blocked", "code": str(error) if isinstance(error, IntakeError)
                          else type(error).__name__}, sort_keys=True))
        raise SystemExit(2)
