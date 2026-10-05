#!/usr/bin/env python3
"""Offline identity binding of the frozen 7C study to an operator policy.

Only the programme identity changes. This does not read a key or private ledger,
verify their current state, or dispatch anything. The resulting config must be
placed beside the original scoring directory so its frozen relative references
still resolve to the same dependency bytes.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

try:
    from . import research_programme_manifest as manifests
    from . import openrouter_runner as wire
except ImportError:
    import research_programme_manifest as manifests
    import openrouter_runner as wire


ORIGINAL_MANIFEST_SHA256 = "14cbb6bb88f0397554815c06ac63d0e19121bd662ceb985f301a2a4770d092b2"
ORIGINAL_GOLD_SHA256 = "2f6e041024658543ffacc20f123ee9ef047ac32858638b8eecb49ff3c9d4c6e2"
ORIGINAL_CONFIG_SHA256 = "ebc02a4cef3a5c02189815d69ab09217a73d2d240a638f3dd127ca920004bd41"


class BindingError(ValueError):
    """Fixed error codes; never include private policy values or content."""


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _read(path):
    try:
        return Path(path).read_bytes()
    except OSError:
        raise BindingError("binding_input_missing") from None


def _pinned(path, digest, reason):
    raw = _read(path)
    if sha(raw) != digest:
        raise BindingError(reason)
    return raw


def _dependencies(config):
    yield config["gold"]
    yield config["source_inputs"]
    for arm in config["arms"]:
        yield arm["prepared_specs"]
    yield config["instrument"]["panel"]
    yield config["instrument"]["key_validator"]


def _validate_delta(original_manifest, bound_manifest, original_config, bound_config):
    recovered_manifest = deepcopy(bound_manifest)
    recovered_manifest["programme_id"] = original_manifest["programme_id"]
    if recovered_manifest != original_manifest:
        raise BindingError("forbidden_manifest_drift")
    recovered_config = deepcopy(bound_config)
    for field in ("path", "raw_sha256", "programme_id"):
        recovered_config["paid_manifest"][field] = original_config["paid_manifest"][field]
    if recovered_config != original_config:
        raise BindingError("forbidden_config_drift")


def bind(manifest_path, config_path, operator_policy_path, output_dir):
    """Return an offline receipt; preserve originals and every request byte.

    All input validation runs before the output directory is created. A fresh,
    empty output directory is required, and sibling placement is intentional:
    changing the remaining frozen paths to accommodate relocation is forbidden.
    """
    manifest_path = Path(manifest_path).resolve()
    config_path = Path(config_path).resolve()
    operator_policy_path = Path(operator_policy_path).resolve()
    output = Path(output_dir).resolve()
    if output == config_path.parent or output.parent != config_path.parent.parent:
        raise BindingError("output_must_be_sibling_of_frozen_scoring_directory")
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise BindingError("output_directory_not_empty")
    manifest_raw = _pinned(manifest_path, ORIGINAL_MANIFEST_SHA256, "original_manifest_hash_changed")
    config_raw = _pinned(config_path, ORIGINAL_CONFIG_SHA256, "original_config_hash_changed")
    policy_raw = _read(operator_policy_path)
    manifest = manifests.read_manifest_bytes(manifest_raw)
    config = wire.parse_json(config_raw)
    policy = wire.parse_json(policy_raw)
    if not isinstance(policy, dict):
        raise BindingError("operator_policy_object_required")
    programme_id = manifests._identity(policy.get("programme_id"))
    if config.get("binding_status") != "bound":
        raise BindingError("original_scoring_binding_not_bound")
    paid = config["paid_manifest"]
    if ((config_path.parent / paid["path"]).resolve() != manifest_path
            or paid["raw_sha256"] != ORIGINAL_MANIFEST_SHA256
            or paid["programme_id"] != manifest["programme_id"]
            or paid["stage_id"] != manifest["stage_id"]):
        raise BindingError("original_config_manifest_binding_changed")
    if config["gold"]["raw_sha256"] != ORIGINAL_GOLD_SHA256:
        raise BindingError("original_gold_binding_changed")
    dependencies = []
    for spec in _dependencies(config):
        original = (config_path.parent / spec["path"]).resolve()
        target = (output / spec["path"]).resolve()
        if original != target:
            raise BindingError("frozen_dependency_path_resolution_changed")
        raw = _pinned(original, spec["raw_sha256"], "frozen_dependency_hash_changed")
        dependencies.append({"path": spec["path"], "sha256": sha(raw)})
    helper_raw = _pinned(config_path.parent / "score_first_only.py",
                         config["replay_adapter_raw_sha256"], "frozen_replay_helper_hash_changed")
    operations = manifests.load_operations(manifest, base_dir=manifest_path.parent)
    if len(operations) != config["authored_inventory"]["operations"]:
        raise BindingError("original_operation_count_changed")
    if [op["operation_id"] for op in operations] != [row["operation_id"] for row in config["operation_mapping"]]:
        raise BindingError("original_operation_mapping_changed")
    bound_manifest = deepcopy(manifest)
    bound_manifest["programme_id"] = programme_id
    bound_manifest_raw = wire.canonical(bound_manifest) + b"\n"
    bound_config = deepcopy(config)
    bound_config["paid_manifest"].update({"path": "manifest.json",
                                          "raw_sha256": sha(bound_manifest_raw),
                                          "programme_id": programme_id})
    _validate_delta(manifest, bound_manifest, config, bound_config)
    bound_config_raw = json.dumps(bound_config, ensure_ascii=False, indent=2, allow_nan=False).encode() + b"\n"
    output.mkdir(parents=False, exist_ok=True)
    for operation in operations:
        target = output / operation["request_file"]
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists() and target.read_bytes() != operation["request_bytes"]:
            raise BindingError("duplicate_request_path_bytes_changed")
        target.write_bytes(operation["request_bytes"])
    (output / "manifest.json").write_bytes(bound_manifest_raw)
    (output / "configuration.json").write_bytes(bound_config_raw)
    (output / "score_first_only.py").write_bytes(helper_raw)
    copies = manifests.load_operations(output / "manifest.json")
    all_equal = len(copies) == len(operations) and all(
        copied == original for copied, original in zip(copies, operations))
    if not all_equal:
        raise BindingError("operation_or_request_copy_changed")
    if manifest_path.read_bytes() != manifest_raw or config_path.read_bytes() != config_raw:
        raise BindingError("original_artifacts_changed_during_binding")
    receipt = {"schema": "loom.jev_dispatch_binding/1",
               "captured_at": datetime.now(timezone.utc).isoformat(),
               "original": {"manifest_raw_sha256": sha(manifest_raw), "configuration_raw_sha256": sha(config_raw),
                            "programme_id": manifest["programme_id"], "gold_raw_sha256": ORIGINAL_GOLD_SHA256},
               "bound": {"manifest_raw_sha256": sha(bound_manifest_raw), "configuration_raw_sha256": sha(bound_config_raw),
                         "programme_id": programme_id, "stage_id": manifest["stage_id"]},
               "operator_policy_raw_sha256": sha(policy_raw),
               "allowed_delta": {"manifest": ["/programme_id"],
                                 "configuration": ["/paid_manifest/path", "/paid_manifest/raw_sha256", "/paid_manifest/programme_id"]},
               "planned_operations": len(operations), "all_operations_request_bytes_and_ids_equal": all_equal,
               "original_artifacts_preserved": True, "frozen_dependencies": dependencies,
               "frozen_helper_raw_sha256": sha(helper_raw), "no_threshold_gold_mapping_or_parameter_change": True,
               "network_calls": 0, "paid_calls": 0, "private_key_or_ledger_read": False,
               "dispatch": False, "registry_state_verified": False,
               "registry_boundary": "Identity bound to supplied existing operator policy only; 7A must verify the current shared registry and budget before dispatch."}
    (output / "BINDING_RECEIPT.json").write_bytes(json.dumps(receipt, ensure_ascii=False, indent=2).encode() + b"\n")
    return receipt


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("manifest", "config", "operator-policy", "output"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args(argv)
    receipt = bind(args.manifest, args.config, args.operator_policy, args.output)
    print(json.dumps({"output": str(Path(args.output).resolve()),
                      "manifest_sha256": receipt["bound"]["manifest_raw_sha256"],
                      "config_sha256": receipt["bound"]["configuration_raw_sha256"],
                      "planned_operations": receipt["planned_operations"], "dispatch": False}))


if __name__ == "__main__":
    main()
