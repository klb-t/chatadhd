#!/usr/bin/env python3
"""Run an offline archive fixture and compare exact artifacts/stage hashes.

Use the same fixture root for both libraries. Pass --compare with the baseline
output directory on the second run; the receipt records both library digests.
"""

import argparse
import ctypes
import hashlib
import json
import pathlib
import shutil
import sqlite3
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def materialize_request(root, result):
    stage = next(stage for stage in result["stages"] if stage["stage"] == "materialize")
    with sqlite3.connect(root / "data/chatadhd.db") as database:
        row = database.execute("SELECT params FROM loom_tasks WHERE id = ?", (stage["task_id"],)).fetchone()
    raw = json.loads(row[0])
    blob = raw["bindings"]
    bindings = json.loads((root / "data/blobs" / blob[:2] / blob[2:4] / blob).read_text())
    conversations = {}
    normalized_bindings = {}
    for key, binding in sorted(bindings.items()):
        conversation = binding["conv"]
        if conversation not in conversations:
            conversations[conversation] = "conversation:" + str(len(conversations))
        normalized_bindings[key] = {"msg": "message:" + key, "conv": conversations[conversation]}
    normalized = dict(raw)
    normalized["bindings"] = normalized_bindings
    normalized["run_id"] = "generated run identity"
    return raw, normalized


def run(library, output, fixtures, materialized):
    lib = ctypes.CDLL(str(library))
    lib.loom_init_ex.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p)]
    lib.loom_init_ex.restype = ctypes.c_void_p
    lib.loom_shutdown.argtypes = [ctypes.c_void_p]
    lib.loom_archive_run.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p
    ]
    lib.loom_archive_run.restype = ctypes.c_char_p
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="archive-parity-") as temporary:
        root = pathlib.Path(temporary)
        error = ctypes.c_char_p()
        options = {"data_dir": str(root / "data"), "start_workers": False}
        context = lib.loom_init_ex(json.dumps(options).encode(), ctypes.byref(error))
        if not context:
            raise RuntimeError(error.value)
        config = {
            "sources": [str(fixtures / "exports"), str(fixtures / "docs")],
            "repo": str(fixtures / "repo"),
            "git": False,
            "seed_terms": ["ChatADHD", "graph", "kivy", "importer"],
            "project": "fixture",
            "out_dir": str(materialized),
        }
        try:
            result = json.loads(
                lib.loom_archive_run(context, json.dumps(config).encode(), None, None)
            )
            if result.get("status") != "done":
                raise RuntimeError(result)
            rows = []
            for artifact in sorted(materialized.iterdir()):
                if artifact.name.startswith("."):
                    continue
                rows.append({
                    "name": artifact.name,
                    "bytes": artifact.stat().st_size,
                    "sha256": digest(artifact),
                })
                shutil.copyfile(artifact, output / artifact.name)
            receipt = {
                "library_sha256": digest(library),
                "config": {key: value for key, value in config.items() if key != "out_dir"},
                "materialized_dir": str(materialized),
                "status": result["status"],
                "summary": result["summary"],
                "artifacts": rows,
                "stage_input_hashes": [stage["input_hash"] for stage in result["stages"]],
                "stage_output_hashes": [stage["output_hash"] for stage in result["stages"]],
                "stage_names": [stage["stage"] for stage in result["stages"]],
                "stable_stage_input_hashes": {stage["stage"]: stage["input_hash"] for stage in result["stages"]
                                              if stage["stage"] != "materialize"},
            }
            raw, normalized = materialize_request(root, result)
            receipt["materialize_input_raw"] = raw
            receipt["materialize_input_normalized"] = normalized
            receipt["materialize_normalization"] = "Generated run/message/conversation IDs become stable symbols; conversation equivalence and every doc key are preserved."
            (output / "parity-receipt.json").write_text(
                json.dumps(receipt, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
            )
            print(json.dumps({
                "status": result["status"], "artifacts": len(rows),
                "bytes": sum(row["bytes"] for row in rows), "rows": rows,
            }))
            return receipt
        finally:
            lib.loom_shutdown(context)


def compare(baseline, output, receipt):
    before = json.loads((baseline / "parity-receipt.json").read_text(encoding="utf-8"))
    differences = []
    for key in ("artifacts", "stage_names", "stable_stage_input_hashes", "stage_output_hashes", "materialize_input_normalized"):
        if before.get(key) != receipt.get(key):
            differences.append(key)
    for row in receipt["artifacts"]:
        previous = baseline / row["name"]
        if not previous.exists() or previous.read_bytes() != (output / row["name"]).read_bytes():
            differences.append(row["name"])
    if differences:
        raise SystemExit("Exact parity failed: " + ", ".join(differences))
    print("Exact parity passed: artifact bytes, stable stage input hashes, all output hashes, and materialize request after generated-ID normalization")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--compare", type=pathlib.Path, help="Baseline output directory")
    parser.add_argument("--materialized-dir", type=pathlib.Path,
                        help="Use the same path in both runs; it participates in stage input hashing")
    parser.add_argument(
        "--fixture-root", type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parent / "fixtures/archive",
    )
    args = parser.parse_args()
    materialized = args.materialized_dir or args.output.parent / "archive-parity-materialized"
    receipt = run(args.library.resolve(), args.output, args.fixture_root.resolve(), materialized.resolve())
    if args.compare:
        compare(args.compare, args.output, receipt)
