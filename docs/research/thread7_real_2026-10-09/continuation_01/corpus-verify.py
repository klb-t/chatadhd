#!/usr/bin/env python3
"""Actual-source integrity checks, separate from mechanical unit tests."""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "loom/tools/structure"))
from experiment_corpus_v1 import canonical, digest, normalized

def verify(root, archive, audit):
    root = Path(root)
    freeze = json.loads((root / "FREEZE.json").read_bytes())
    panel = json.loads((root / "panel.json").read_bytes())
    source_index = json.loads((root / "source-index.json").read_bytes())
    checked = Counter()
    for name, row in freeze["files"].items():
        data = (root / name).read_bytes()
        assert digest(data) == row["sha256"] and len(data) == row["bytes"], "freeze_payload_mismatch"
        checked["frozen_payloads"] += 1
    z = zipfile.ZipFile(archive); az = zipfile.ZipFile(audit)
    selectors = json.loads(az.read("subset/selection/selection_manifest.json"))
    hashes = {r["native_id"]: r["raw_sha256"] for r in source_index}
    for row in selectors["conversations"]:
        assert hashes[row["id"]] == row["object_sha256_canonical_json"], "historical_selector_object_mismatch"
        checked["historical_selector_objects"] += 1
    families = set(); source_members = {}
    for row in panel["sources"]:
        record = json.loads((root / row["path"]).read_bytes())
        member, index = record["source_pointer"].split("#/")
        if member not in source_members:
            source_members[member] = json.loads(z.read(member))
        obj = source_members[member][int(index)]
        assert obj == record["native_conversation"] and digest(obj) == record["raw_sha256"], "native_source_mutated"
        rebuilt = normalized(obj, record["provider"], record["source_pointer"])
        assert record["messages"] == rebuilt["messages"], "normalized_message_loss"
        assert record["source_graph"] == rebuilt["source_graph"], "source_node_loss"
        checked["whole_native_objects"] += 1
        checked["raw_messages"] += len(record["messages"])
        checked["raw_nodes"] += len(record["source_graph"])
        assert record["family_id"] not in families, "family_leakage"
        families.add(record["family_id"])
        assert not record["historical_overlap"], "historical_panel_overlap"
        if record["split"] == "provisional_validation":
            assert not record["prior_exposure"], "known_prior_exposure_in_validation"
            checked["provisional_validation_unexposed_to_known_eval"] += 1
    attachments = json.loads((root / "attachment-audit.json").read_bytes())
    for row in attachments["files"]:
        if row["status"] == "downloaded":
            assert digest(z.read(row["output_relative_path"])) == row["sha256"], "binary_hash_mismatch"
            checked["source_binaries_verified"] += 1
    anthropic_missing = [r for r in attachments["unresolved_references"] if "native_export_contains_reference_only_no_binary_in_source_folder" in r["reasons"]]
    return {"schema": "loom.research_corpus_actual_qa/1", "status": "pass", "checks": dict(checked),
            "source_archive_sha256": digest(Path(archive).read_bytes()),
            "freeze_sha256": digest((root / "FREEZE.json").read_bytes()),
            "source_anthropic_missing_unique_binary_references": len({r["reference"] for r in anthropic_missing}),
            "source_anthropic_external_file_reference_occurrences": attachments["counts"]["anthropic_external_file_reference_occurrences"],
            "independent_validation": False, "new_inference_calls": 0, "new_cost_usd": "0"}

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    for key in ("root", "archive", "audit", "output"):
        p.add_argument("--" + key, required=True)
    a = p.parse_args()
    result = verify(a.root, a.archive, a.audit)
    Path(a.output).write_bytes(canonical(result) + b"\n")
    print(json.dumps(result["checks"], sort_keys=True))
