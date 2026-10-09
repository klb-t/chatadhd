"""Offline measurements of already frozen context views, without model inference.

The caller supplies dimensions, repetitions and the pinned preparation producer.
Coverage refers to exact JSON fields and native export links, not semantics.
Only aggregate rows are exported publicly; source identifiers stay private.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import statistics
import time
import tracemalloc

from loom.tools.structure.experiment_workflow_v1 import canonical, digest, require

VERSION = "loom.research_representation_measurement/1"


def leaves(value, pointer=""):
    """JSON atomic leaves; empty containers count as one leaf, too."""
    if isinstance(value, dict) and value:
        for key, item in value.items():
            escaped = key.replace("~", "~0").replace("/", "~1")
            yield from leaves(item, pointer + "/" + escaped)
    elif isinstance(value, list) and value:
        for index, item in enumerate(value):
            yield from leaves(item, pointer + "/" + str(index))
    else:
        yield pointer, value


def resolve(value, pointer):
    for part in pointer.split("/")[1:]:
        part = part.replace("~1", "/").replace("~0", "~")
        value = value[int(part)] if isinstance(value, list) else value[part]
    return value


def fraction(numerator, denominator):
    return numerator / denominator if denominator else None


def output_records(view):
    payload = view["payload"]
    if "nodes" in payload:
        return payload["nodes"]
    if "records" in payload:
        return payload["records"]
    return payload["segments"]


def represented_native_leaves(record):
    if "native_message" in record:
        return dict(leaves(record["native_message"]))
    fields = dict(zip(record["native_text_field_pointers"], record["exact_text_parts"]))
    for alias in record.get("equivalent_native_text_aliases", []):
        if "identical_to_field" in alias:
            fields[alias["field"]] = fields[alias["identical_to_field"]]
        else:
            fields[alias["field"]] = alias["separator"].join(
                fields[p] for p in alias["identical_to_concatenation_of_fields"])
    return fields


def inspect_view(source, view, producer):
    """Compare every retained claim with original source values and denominators."""
    by_id = {node["node_id"]: node for node in source["nodes"]}
    records = output_records(view)
    selected_ids = [record["node_id"] for record in records]
    require(len(selected_ids) == len(set(selected_ids)), "duplicate_view_record")
    require(set(selected_ids) <= set(by_id), "view_contains_unknown_source_record")
    selected = [by_id[node_id] for node_id in selected_ids]
    native_total = sum(len(dict(leaves(n["native_message"]))) for n in source["nodes"])
    native_selected = sum(len(dict(leaves(n["native_message"]))) for n in selected)
    node_total = sum(len(dict(leaves(n))) for n in source["nodes"])
    node_selected = sum(len(dict(leaves(n))) for n in selected)
    retained_native = retained_nodes = mismatched = message_reconstruction = node_reconstruction = 0
    expected_text_fields = retained_text_fields = exact_text_bytes = 0
    for record in records:
        original = by_id[record["node_id"]]
        expected = dict(leaves(original["native_message"]))
        present = represented_native_leaves(record)
        retained_native += sum(pointer in expected and canonical(value) == canonical(expected[pointer])
                               for pointer, value in present.items())
        mismatched += sum(pointer not in expected or canonical(value) != canonical(expected[pointer])
                          for pointer, value in present.items())
        original_leaves = dict(leaves(original))
        # Text envelopes rename native fields; map those back to their exact pointers.
        if "exact_text_parts" in record:
            mapped = {"/" + key: record[key] for key in ("node_id", "role", "source_pointer")}
            mapped.update({"/native_message" + pointer: value for pointer, value in present.items()})
        else:
            mapped = dict(leaves(record))
        retained_nodes += sum(pointer in original_leaves and canonical(value) == canonical(original_leaves[pointer])
                              for pointer, value in mapped.items())
        if "native_message" in record:
            message_reconstruction += canonical(record["native_message"]) == canonical(original["native_message"])
        node_reconstruction += canonical(record) == canonical(original)
        projection = producer.text_projection(original["native_message"])
        text_pointers = projection["fields"] + [a["field"] for a in projection["equivalent_aliases"]]
        expected_text_fields += len(text_pointers)
        retained_text_fields += sum(pointer in present and canonical(present[pointer]) == canonical(resolve(original["native_message"], pointer))
                                    for pointer in text_pointers)
        exact_text_bytes += sum(len(text.encode("utf-8")) for text in projection["parts"])
    all_parent_links = {(n["parent_node_id"], n["node_id"]) for n in source["nodes"] if n.get("parent_node_id") is not None}
    expected_links = {link for link in all_parent_links if link[1] in selected_ids}
    observed_links = {(r["parent_node_id"], r["node_id"]) for r in records if r.get("parent_node_id") is not None}
    if "edges" in view["payload"]:
        observed_links.update((e["source"], e["target"]) for e in view["payload"]["edges"] if e["relation"] == "native_parent")
        observed_links.update((e["parent"], e["child"]) for e in view["payload"].get("boundary_edges", []))
    encoded = canonical(view)
    scalar_bytes = sum(len(canonical(value)) for _, value in leaves(view))
    return {
        "source_nodes": len(source["nodes"]), "selected_nodes": len(records),
        "source_node_coverage": fraction(len(records), len(source["nodes"])),
        "source_canonical_bytes": len(canonical(source)), "selected_node_canonical_bytes": len(canonical(selected)),
        "view_canonical_bytes": len(encoded), "payload_canonical_bytes": len(canonical(view["payload"])),
        "view_json_scalar_bytes": scalar_bytes, "view_json_keys_and_syntax_bytes": len(encoded) - scalar_bytes,
        "selected_literal_text_bytes_without_identical_aliases": exact_text_bytes,
        "view_bytes_above_literal_text": len(encoded) - exact_text_bytes,
        "source_native_message_leaves": native_total, "selected_native_message_leaves": native_selected,
        "retained_native_message_leaves": retained_native,
        "native_message_leaf_coverage_selected": fraction(retained_native, native_selected),
        "native_message_leaf_coverage_source": fraction(retained_native, native_total),
        "source_normalized_node_leaves": node_total, "selected_normalized_node_leaves": node_selected,
        "retained_normalized_node_leaves": retained_nodes,
        "normalized_node_leaf_coverage_selected": fraction(retained_nodes, node_selected),
        "normalized_node_leaf_coverage_source": fraction(retained_nodes, node_total),
        "selected_literal_native_text_fields": expected_text_fields, "retained_literal_native_text_fields": retained_text_fields,
        "literal_text_field_coverage_selected": fraction(retained_text_fields, expected_text_fields),
        "mismatched_or_unknown_native_fields": mismatched,
        "native_messages_reconstructed": message_reconstruction, "normalized_nodes_reconstructed": node_reconstruction,
        "source_parent_links": len(all_parent_links), "selected_child_parent_links": len(expected_links),
        "retained_parent_links": len(expected_links & observed_links), "invented_parent_links": len(observed_links - all_parent_links),
        "parent_link_coverage_selected_children": fraction(len(expected_links & observed_links), len(expected_links)),
        "parent_link_coverage_source": fraction(len(expected_links & observed_links), len(all_parent_links)),
        "unresolved_external_parent_links": len([x for x in all_parent_links if x[0] not in by_id]),
        "conversation_wrapper_reconstructible": False, "attachment_binaries_in_view": False,
        "original_file_serialization_reconstructible": False,
        "token_count": None, "tokenizer": None,
    }


def load_pinned_producer(path, expected_sha256):
    require(hashlib.sha256(path.read_bytes()).hexdigest() == expected_sha256, "producer_hash_mismatch")
    spec = importlib.util.spec_from_file_location("_pinned_representation_preparator", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify_manifest(root, manifest):
    for relative, expected in manifest["files"].items():
        require(hashlib.sha256((root / relative).read_bytes()).hexdigest() == expected, "preparation_payload_hash_mismatch")


def aggregate(rows):
    groups = collections.defaultdict(list)
    for row in rows:
        groups[(row["representation"], row["resolution"])].append(row)
    output = []
    count_keys = ("source_nodes", "selected_nodes", "view_canonical_bytes", "view_json_keys_and_syntax_bytes",
                  "selected_literal_text_bytes_without_identical_aliases", "source_native_message_leaves",
                  "selected_native_message_leaves", "retained_native_message_leaves", "selected_normalized_node_leaves",
                  "retained_normalized_node_leaves", "selected_literal_native_text_fields", "retained_literal_native_text_fields",
                  "source_parent_links", "selected_child_parent_links", "retained_parent_links",
                  "native_messages_reconstructed", "normalized_nodes_reconstructed", "mismatched_or_unknown_native_fields", "invented_parent_links")
    for (representation, resolution), group in sorted(groups.items()):
        summary = {"representation": representation, "resolution": resolution, "families": len(group),
                   "family_split_counts": dict(collections.Counter(r["split"] for r in group))}
        summary.update({key: sum(r[key] for r in group) for key in count_keys})
        for numerator, denominator, name in (
            ("retained_native_message_leaves", "selected_native_message_leaves", "native_message_leaf_coverage_selected_micro"),
            ("retained_native_message_leaves", "source_native_message_leaves", "native_message_leaf_coverage_source_micro"),
            ("retained_normalized_node_leaves", "selected_normalized_node_leaves", "normalized_node_leaf_coverage_selected_micro"),
            ("retained_literal_native_text_fields", "selected_literal_native_text_fields", "literal_text_field_coverage_selected_micro"),
            ("retained_parent_links", "selected_child_parent_links", "parent_link_coverage_selected_children_micro"),
            ("retained_parent_links", "source_parent_links", "parent_link_coverage_source_micro")):
            summary[name] = fraction(summary[numerator], summary[denominator])
        for name in ("native_message_leaf_coverage_source", "normalized_node_leaf_coverage_source", "source_node_coverage", "parent_link_coverage_source"):
            values = [r[name] for r in group if r[name] is not None]
            summary[name + "_family_macro"] = statistics.mean(values) if values else None
        for key in ("preparation_seconds_median", "serialization_seconds_median", "peak_python_allocation_bytes"):
            summary[key + "_family_median"] = statistics.median(r[key] for r in group)
            summary[key + "_family_max"] = max(r[key] for r in group)
        summary["all_rebuilt_views_match_frozen"] = all(r["rebuild_matches_frozen"] for r in group)
        summary["all_canonical_json_roundtrips_match"] = all(r["canonical_json_roundtrip_matches"] for r in group)
        summary["token_count"] = None
        output.append(summary)
    return output


def run(root, output, policy):
    require(not output.exists(), "measurement_output_already_exists")
    output.mkdir(parents=True)
    manifest = json.loads((root / "MANIFEST.json").read_bytes())
    verify_manifest(root, manifest)
    producer = load_pinned_producer(root / "producer.py", manifest["files"]["producer.py"])
    sources = json.loads((root / "sources.json").read_bytes())
    tasks = {task["family_id"]: task for task in json.loads((root / "tasks.json").read_bytes())}
    plan = json.loads((root / "plan.json").read_bytes())
    require(policy["repeats"] > 0, "measurement_repeats_must_be_positive")
    start = {"schema": VERSION, "policy": policy, "policy_sha256": digest(policy),
             "preparation_manifest_sha256": hashlib.sha256((root / "MANIFEST.json").read_bytes()).hexdigest(),
             "producer_sha256": manifest["files"]["producer.py"], "sources_sha256": manifest["files"]["sources.json"],
             "tasks_sha256": manifest["files"]["tasks.json"], "python": platform.python_version(),
             "producer": producer.VERSION, "status": "started_before_measurement", "new_paid_calls": 0, "new_cost_usd": "0"}
    (output / "START.json").write_bytes(canonical(start))
    rows = []
    raw = output / "measurements.jsonl"
    with raw.open("xb") as handle:
        for source in sources:
            task = tasks[source["family_id"]]
            for representation in policy["representations"]:
                for resolution in policy["resolutions"]:
                    durations, serializations = [], []
                    for _ in range(policy["repeats"]):
                        begin = time.perf_counter()
                        rebuilt = producer.prepare_view(source, task, representation, resolution, plan["context_policy"])
                        durations.append(time.perf_counter() - begin)
                        begin = time.perf_counter()
                        encoded = canonical(rebuilt["exact_output"])
                        serializations.append(time.perf_counter() - begin)
                    expected_hash = rebuilt["output_sha256"]
                    frozen_path = root / "views" / (expected_hash + ".json")
                    require(frozen_path.exists(), "rebuilt_view_not_in_original_freeze")
                    frozen_bytes = frozen_path.read_bytes()
                    require(hashlib.sha256(frozen_bytes).hexdigest() == expected_hash, "frozen_view_hash_mismatch")
                    require(encoded == frozen_bytes, "rebuild_not_identical_to_frozen_view")
                    tracemalloc.start()
                    producer.prepare_view(source, task, representation, resolution, plan["context_policy"])
                    _, peak = tracemalloc.get_traced_memory()
                    tracemalloc.stop()
                    row = {"family_id": source["family_id"], "source_sha256": source["source_sha256"],
                           "context_source_sha256": digest(source), "task_sha256": digest(task), "split": source["split"],
                           "representation": representation, "resolution": resolution, "output_sha256": expected_hash,
                           "preparation_seconds": durations, "preparation_seconds_median": statistics.median(durations),
                           "serialization_seconds": serializations, "serialization_seconds_median": statistics.median(serializations),
                           "peak_python_allocation_bytes": peak, "rebuild_matches_frozen": True,
                           "canonical_json_roundtrip_matches": canonical(json.loads(encoded)) == encoded,
                           "loss": rebuilt["loss"], **inspect_view(source, rebuilt["exact_output"], producer)}
                    rows.append(row)
                    handle.write(canonical(row) + b"\n"); handle.flush()
    summary = {"schema": VERSION, "status": "measured_offline", "families": len(sources), "comparisons": len(rows),
               "policy_sha256": digest(policy), "preparation_manifest_sha256": start["preparation_manifest_sha256"],
               "method": "exact_source_field_and_export_relation_accounting", "results": aggregate(rows),
               "new_paid_calls": 0, "new_cost_usd": "0", "model_answer_quality": None,
               "limits": policy["interpretation_limits"], "measurements_sha256": hashlib.sha256(raw.read_bytes()).hexdigest()}
    (output / "SUMMARY.json").write_bytes(canonical(summary))
    (output / "MANIFEST.json").write_bytes(canonical({"schema": "loom.private_measurement_freeze/1", "files": {
        p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(output.iterdir()) if p.is_file()}}))
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preparation", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--public-summary", type=Path, required=True)
    args = parser.parse_args()
    summary = run(args.preparation, args.output, json.loads(args.policy.read_bytes()))
    require(not args.public_summary.exists(), "public_summary_already_exists")
    args.public_summary.write_bytes(canonical(summary))
    print(json.dumps({"comparisons": summary["comparisons"], "families": summary["families"], "new_paid_calls": 0}))


if __name__ == "__main__":
    main()
