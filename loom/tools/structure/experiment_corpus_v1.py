#!/usr/bin/env python3
"""Offline source-bound corpus preparation; native sources and private IDs stay private."""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import zipfile

SCHEMA = "loom.research_corpus/1"

def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()

def digest(value):
    return hashlib.sha256(value if isinstance(value, bytes) else canonical(value)).hexdigest()

def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical(value) + b"\n")
    path.chmod(0o600)

def private_directory(path):
    path = Path(path).resolve()
    if any((p / ".git").exists() for p in [path, *path.parents]):
        raise ValueError("private_output_inside_git")
    path.mkdir(parents=True, exist_ok=False)
    path.chmod(0o700)
    return path

def native_id(obj, provider):
    return str(obj.get("conversation_id") or obj.get("id") or obj.get("uuid") or "")

def extract_text(message, provider):
    """Only literal text: preserve raw beside this deliberate text projection."""
    if provider == "anthropic" and isinstance(message.get("text"), str):
        return message["text"], "native_text_field"
    content = message.get("content")
    if isinstance(content, dict):
        parts = content.get("parts", [])
    elif isinstance(content, list):
        parts = [p.get("text") for p in content if isinstance(p, dict) and p.get("type") == "text"]
    else:
        parts = []
    return "\n".join(p for p in parts if isinstance(p, str)), "newline_joined_literal_text_parts_nontext_omitted"

def normalized(obj, provider, pointer):
    raw_hash = digest(obj)
    records = []
    graph = []
    if provider == "openai":
        for key, node in obj.get("mapping", {}).items():
            parent = node.get("parent")
            children = node.get("children", [])
            graph.append({"node_id": key, "parent_ids": [parent] if parent else [], "child_ids": children,
                          "source_pointer": f"{pointer}/mapping/{key}", "native_node": node})
            message = node.get("message")
            if not message:
                continue
            text, projection = extract_text(message, provider)
            role = message.get("author", {}).get("role", "unknown")
            records.append({"message_id": message.get("id", key), "node_id": key,
                            "parent_ids": [parent] if parent else [], "child_ids": children,
                            "role": role, "text": text, "text_projection": projection,
                            "created_at": message.get("create_time"),
                            "source_pointer": f"{pointer}/mapping/{key}/message",
                            "native_message": message, "native_node": node})
    else:
        messages = obj.get("chat_messages", [])
        children = defaultdict(list)
        for m in messages:
            if m.get("parent_message_uuid"):
                children[m["parent_message_uuid"]].append(m.get("uuid"))
        for index, message in enumerate(messages):
            mid = str(message.get("uuid", index))
            parent = message.get("parent_message_uuid")
            text, projection = extract_text(message, provider)
            # A null/missing native parent is unknown; do not invent a linear edge.
            node = {"node_id": mid, "parent_ids": [parent] if parent else [],
                    "child_ids": children[mid], "source_pointer": f"{pointer}/chat_messages/{index}",
                    "native_node": message}
            graph.append(node)
            records.append({**node, "message_id": mid,
                            "role": {"human": "user"}.get(message.get("sender"), message.get("sender", "unknown")),
                            "text": text, "text_projection": projection,
                            "created_at": message.get("created_at"), "native_message": message})
    return {"schema": SCHEMA, "source_id": "source-" + raw_hash[:20], "family_id": None,
            "provider": provider, "native_id": native_id(obj, provider), "source_pointer": pointer,
            "raw_sha256": raw_hash, "native_conversation": obj, "messages": records,
            "source_graph": graph, "normalization_version": "experiment_corpus_v1",
            "normalization_loss": "None in native_conversation/native_message/native_node; text projection excludes nontext, metadata, alternate field representations and uses explicit newline joins.",
            "normalization_interpretation": "Anthropic human mapped to user. No inferred parent edges, semantic labels or summaries."}

def features(record, policy):
    user = "\n".join(m["text"] for m in record["messages"] if m["role"] == "user")
    out = {name: bool(re.search(pattern, user)) for name, pattern in policy["cue_rules"].items()}
    out["branching"] = any(len(node["child_ids"]) > 1 for node in record["source_graph"])
    tools = 0
    for msg in record["messages"]:
        native = msg["native_message"]
        content = native.get("content", [])
        if msg["role"] == "tool" or native.get("recipient") not in (None, "all") or native.get("metadata", {}).get("invoked_plugin"):
            tools += 1
        elif isinstance(content, list) and any(isinstance(p, dict) and p.get("type") in {"tool_use", "tool_result"} for p in content):
            tools += 1
    out["tool_calls"] = tools > 0
    out["tool_message_count"] = tools
    out["text_chars"] = sum(len(m["text"]) for m in record["messages"])
    out["message_count"] = len(record["messages"])
    out["node_count"] = len(record["source_graph"])
    return out

class UnionFind:
    def __init__(self, size):
        self.parent = list(range(size))
    def root(self, item):
        while self.parent[item] != item:
            self.parent[item] = self.parent[self.parent[item]]
            item = self.parent[item]
        return item
    def union(self, one, two):
        self.parent[self.root(two)] = self.root(one)

def group_families(records, minimum=200):
    uf = UnionFind(len(records)); seen = {}; reasons = Counter()
    for index, record in enumerate(records):
        keys = [("native_id", record["provider"], record["native_id"]), ("canonical_object", record["raw_sha256"])]
        for msg in record["messages"]:
            keys.append(("native_message_id", record["provider"], msg["message_id"]))
            keys.append(("native_node_id", record["provider"], msg["node_id"]))
            if len(msg["text"]) >= minimum:
                keys.append(("substantial_literal_text", msg["role"], digest(msg["text"].encode())))
        for key in set(keys):
            if key[-1] in (None, ""):
                continue
            if key in seen and uf.root(index) != uf.root(seen[key]):
                uf.union(index, seen[key]); reasons[key[0]] += 1
            seen[key] = index
    groups = defaultdict(list)
    for index in range(len(records)):
        groups[uf.root(index)].append(index)
    for indices in groups.values():
        family = "family-" + digest(sorted(records[i]["raw_sha256"] for i in indices))[:20]
        for index in indices:
            records[index]["family_id"] = family
    return list(groups.values()), dict(reasons)

def prior_exposure(audit):
    ids = set()
    def visit(value):
        if isinstance(value, dict):
            for key, val in value.items():
                if key in {"conversation_id", "family_id"} and isinstance(val, str):
                    ids.add(val.rsplit("/", 1)[-1])
                elif key == "expected_conversation_ids":
                    ids.update(val)
                else:
                    visit(val)
        elif isinstance(value, list):
            for item in value:
                visit(item)
    for name in audit.namelist():
        if name.startswith("evaluation/") and name.endswith(".json") and name not in {"evaluation/source_verification.json", "evaluation/subset_qa.json"}:
            visit(json.loads(audit.read(name)))
    return ids

def choose(records, policy):
    selected = []; used = set(); covered = set()
    names = list(policy["cue_rules"]) + policy["structural_features"]
    for provider, quota in policy["provider_quota"].items():
        candidates = sorted((r for r in records if r["provider"] == provider and not r["historical_overlap"]), key=lambda r: (r["features"]["text_chars"], r["raw_sha256"]))
        for i, record in enumerate(candidates):
            record["length_bin"] = policy["length_bins"][min(len(policy["length_bins"]) - 1, i * len(policy["length_bins"]) // max(len(candidates), 1))]
        picked = []
        # One round across bins then another preserves length diversity.
        for index in range(quota):
            bin_name = policy["length_bins"][index % len(policy["length_bins"])]
            eligible = [r for r in candidates if r["family_id"] not in used and r["length_bin"] == bin_name]
            if not eligible:
                eligible = [r for r in candidates if r["family_id"] not in used]
            need_unexposed = sum(not r["prior_exposure"] for r in picked) < policy["provisional_validation_per_provider"]
            if need_unexposed and any(not r["prior_exposure"] for r in eligible):
                eligible = [r for r in eligible if not r["prior_exposure"]]
            if not eligible:
                break
            def rank(r):
                values = [n for n in names if r["features"][n]]
                return (-sum(n not in covered for n in values), -len(values), r["raw_sha256"])
            winner = min(eligible, key=rank)
            picked.append(winner); used.add(winner["family_id"])
            covered.update(n for n in names if winner["features"][n])
        eligible_validation = sorted((r for r in picked if not r["prior_exposure"]), key=lambda r: r["raw_sha256"])
        validation_ids = {r["family_id"] for r in eligible_validation[:policy["provisional_validation_per_provider"]]}
        for record in picked:
            record["split"] = "provisional_validation" if record["family_id"] in validation_ids else "tuning"
        selected.extend(picked)
    return selected

def time_range(records):
    values = []
    for record in records:
        for m in record["messages"]:
            raw = m.get("created_at")
            try:
                if isinstance(raw, (int, float)):
                    dt = datetime.fromtimestamp(raw, timezone.utc)
                else:
                    dt = datetime.fromisoformat(raw.replace("Z", "+00:00"))
                    if dt.tzinfo is None:
                        dt = dt.replace(tzinfo=timezone.utc)
                values.append(dt.astimezone(timezone.utc).isoformat())
            except (ValueError, TypeError, AttributeError, OverflowError):
                pass
    return {"start": min(values) if values else None, "end": max(values) if values else None,
            "messages_with_valid_timestamp": len(values)}

def prepare(source, audit_path, historical, policy, output):
    source = Path(source); audit_path = Path(audit_path)
    if digest(source.read_bytes()) != policy["source_zip_sha256"]:
        raise ValueError("source_zip_hash_mismatch")
    z = zipfile.ZipFile(source); az = zipfile.ZipFile(audit_path)
    if z.testzip() or az.testzip():
        raise ValueError("source_crc_failure")
    inventory = json.loads(az.read("AUDIT_INVENTORY.json"))
    if inventory["source_zip_sha256"] != policy["source_zip_sha256"]:
        raise ValueError("audit_source_binding_mismatch")
    for name, row in inventory["files"].items():
        data = az.read(name)
        if len(data) != row["bytes"] or digest(data) != row["sha256"]:
            raise ValueError("audit_member_hash_mismatch")
    package = json.loads(az.read("subset/selection/package_report.json"))
    for row in package["files"]:
        data = z.read(row["path"])
        if len(data) != row["bytes"] or digest(data) != row["sha256"]:
            raise ValueError("source_member_hash_mismatch")
    selected_ids = {(r["provider"], r["id"]) for r in json.loads(az.read("subset/selection/selected_ids.json"))}
    prior = prior_exposure(az)
    records = []
    for member in z.namelist():
        if re.search(r"/conversations(?:-\d+)?\.json$", member):
            provider = "anthropic" if "/antro/" in member else "openai"
            data = z.read(member)
            for index, obj in enumerate(json.loads(data)):
                selector_provider = "antro" if provider == "anthropic" else "open"
                if (selector_provider, native_id(obj, provider)) not in selected_ids:
                    raise ValueError("selector_object_missing")
                record = normalized(obj, provider, member + "#/" + str(index))
                record["source_member_sha256"] = digest(data)
                record["features"] = features(record, policy)
                record["historical_panel"] = False
                records.append(record)
    if len(records) != len(selected_ids):
        raise ValueError("selector_count_mismatch")
    old = []
    for path in sorted(Path(historical).rglob("original-conversation.json")):
        record = normalized(json.loads(path.read_text()), "openai", str(path))
        record["historical_panel"] = True
        old.append(record)
    combined = records + old
    groups, union_reasons = group_families(combined, policy["substantial_exact_text_overlap_min_chars"])
    for group in groups:
        overlap = any(combined[i]["historical_panel"] for i in group)
        exposure = any(combined[i]["native_id"] in prior for i in group)
        for i in group:
            combined[i]["historical_overlap"] = overlap
            combined[i]["prior_exposure"] = exposure
    chosen = choose(records, policy)
    output = private_directory(output)
    attachments = json.loads(az.read("subset/selection/attachment_manifest.json"))
    index = []
    for record in chosen:
        cid = record["native_id"]
        linked = [r for r in attachments["files"] if cid in r.get("conversation_ids", [])]
        missing = [r for r in attachments["unresolved_references"] if cid in r.get("conversation_ids", [])]
        record["attachment_refs"] = {"manifest_records": linked, "unresolved": missing,
            "embedded_preserved": True, "complete": False if missing or any(r["status"] != "downloaded" for r in linked) else None}
        for attachment in linked:
            member = attachment.get("output_relative_path")
            if attachment["status"] == "downloaded":
                data = z.read(member)
                if digest(data) != attachment["sha256"]:
                    raise ValueError("selected_attachment_hash_mismatch")
                target = output / "attachments" / digest(data)
                if not target.exists():
                    target.parent.mkdir(exist_ok=True); target.write_bytes(data); target.chmod(0o600)
                attachment["private_payload_path"] = str(target.relative_to(output))
        relative = "conversations/" + record["source_id"] + ".json"
        write_json(output / relative, record)
        index.append({key: record[key] for key in ["source_id", "family_id", "provider", "split", "raw_sha256", "source_pointer", "features", "length_bin", "prior_exposure"]} | {"path": relative, "normalized_sha256": digest((output / relative).read_bytes())})
    panel = {"schema": SCHEMA, "version": policy["version"], "policy_sha256": digest(policy), "sources": index,
             "validation_status": "provisional_partition_only_not_independent_or_blind", "new_model_calls": 0,
             "source_zip_sha256": policy["source_zip_sha256"], "historical_panel_kept_separate": True}
    write_json(output / "panel.json", panel)
    write_json(output / "policy.json", policy)
    # Full source index is private; no UUIDs/titles/source paths in public receipt.
    write_json(output / "source-index.json", [{k: r[k] for k in ["source_id", "family_id", "provider", "native_id", "raw_sha256", "source_pointer", "historical_overlap", "prior_exposure", "features"]} for r in records])
    write_json(output / "attachment-audit.json", attachments)
    write_json(output / "prior-exposure.json", {"known_native_ids": sorted(prior), "unknown_other_exposure": True})
    payload = {str(p.relative_to(output)): {"sha256": digest(p.read_bytes()), "bytes": p.stat().st_size} for p in sorted(output.rglob("*")) if p.is_file()}
    freeze = {"schema": "loom.research_corpus_freeze/1", "policy_sha256": digest(policy), "files": payload,
              "source_archive_sha256": policy["source_zip_sha256"], "audit_archive_sha256": digest(audit_path.read_bytes()),
              "previous_checkpoint_sha256": policy["previous_private_checkpoint_sha256"], "code_sha256": digest(Path(__file__).read_bytes()),
              "calls": 0, "new_cost_usd": "0", "independent_validation": False}
    write_json(output / "FREEZE.json", freeze)
    feature_names = list(policy["cue_rules"]) + policy["structural_features"]
    receipt = {"schema": "loom.research_corpus_receipt/1", "version": policy["version"],
      "previous_checkpoint_sha256": policy["previous_private_checkpoint_sha256"], "previous_public_commit": policy["previous_public_commit"],
      "source_zip_sha256": policy["source_zip_sha256"], "audit_zip_sha256": digest(audit_path.read_bytes()),
      "freeze_sha256": digest((output / "FREEZE.json").read_bytes()), "policy_sha256": digest(policy),
      "source_objects": len(records), "source_by_provider": dict(Counter(r["provider"] for r in records)),
      "source_messages": sum(len(r["messages"]) for r in records), "source_period": time_range(records),
      "verified_archive_members": len(package["files"]), "verified_audit_members": len(inventory["files"]),
      "historical_panel_objects": len(old), "historical_overlap_source_objects": sum(r["historical_overlap"] for r in records),
      "family_components_with_historical": len(groups), "family_union_reasons": union_reasons,
      "known_prior_exposure_source_objects": sum(r["prior_exposure"] for r in records),
      "panel_families": len(chosen), "panel_messages": sum(len(r["messages"]) for r in chosen),
      "panel_nodes": sum(len(r["source_graph"]) for r in chosen), "panel_period": time_range(chosen),
      "panel_by_provider": dict(Counter(r["provider"] for r in chosen)), "panel_by_split": dict(Counter(r["split"] for r in chosen)),
      "panel_length_bins": dict(Counter(r["length_bin"] for r in chosen)),
      "candidate_feature_coverage": {name: sum(bool(r["features"][name]) for r in chosen) for name in feature_names},
      "candidate_feature_semantics": "lexical candidates are not verified corrections/preferences/topic transitions/checkpoints",
      "source_feature_coverage": {name: sum(bool(r["features"][name]) for r in records) for name in feature_names},
      "attachments": {"source_downloaded_binaries": attachments["binary_status_counts"]["downloaded"],
                      "selected_unique_copied_binaries": len(list((output / "attachments").glob("*"))),
                      "selected_unresolved_reference_records": sum(len(r["attachment_refs"]["unresolved"]) for r in chosen),
                      "source_binary_status_counts": attachments["binary_status_counts"],
                      "source_unresolved_records": len(attachments["unresolved_references"]),
                      "all_native_embedded_records_retained": True, "complete": False},
      "validation_status": "family-partitioned provisional validation, no known prior exposure within validation; unknown other exposure prevents independence/blind claim",
      "new_calls": 0, "new_cost_usd": "0", "model_quality": None}
    return receipt

def main():
    p = argparse.ArgumentParser()
    for key in ("source", "audit", "historical", "policy", "output", "receipt"):
        p.add_argument("--" + key, required=True)
    a = p.parse_args()
    receipt = prepare(a.source, a.audit, a.historical, json.loads(Path(a.policy).read_text()), a.output)
    Path(a.receipt).write_bytes(canonical(receipt) + b"\n")
    print(json.dumps({"families": receipt["panel_families"], "messages": receipt["panel_messages"], "new_calls": 0}))

if __name__ == "__main__":
    main()
