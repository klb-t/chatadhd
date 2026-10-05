#!/usr/bin/env python3
"""Preserve a completed or failed DIC-0301 replay as a reproducible tar.gz.

Includes full own sources, pack/DEV inputs, before/current module snapshots,
reports and command logs. Native binaries/objects/archives remain represented
by SHA256 receipts only. Output must be new; earlier evidence is never replaced.
"""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[4]
    run = args.run.resolve()
    output = args.output.resolve()
    if not run.is_dir(): raise RuntimeError("replay directory does not exist")
    if output.exists(): raise FileExistsError(output)
    # A failed replay may not have produced summary.json. Its source and first
    # command/error output still belong in the archive, with that boundary clear.
    summary_path = run / "summary.json"
    summary = json.loads(summary_path.read_text()) if summary_path.is_file() else None
    inputs_path = run / "inputs.json"
    inputs = json.loads(inputs_path.read_text()) if inputs_path.is_file() else None
    entries = {}
    excluded = {}

    def put(path, name):
        if not path.is_file(): return
        data = path.read_bytes()
        entries[name] = data

    if summary:
        for label, expected in summary["sha256"].items():
            path = Path(label)
            if not path.is_absolute():
                path = repo / path if (repo / path).is_file() else run / path
            if not path.is_file():
                raise FileNotFoundError(f"receipted input absent: {label}")
            binary = path.suffix in {".a", ".o", ".so"} or path.name in {"dic0301_before", "dic0301_after"}
            if binary:
                excluded[label] = expected
                continue
            data = path.read_bytes()
            if sha(data) != expected:
                raise RuntimeError(f"receipted source changed since replay: {label}")
            name = "repo/" + str(path.relative_to(repo)) if path.is_relative_to(repo) else "run/" + str(path.relative_to(run))
            entries[name] = data
    elif inputs:
        for label, expected in inputs["sha256"].items():
            snapshot = inputs["snapshots"].get(label)
            if not snapshot:
                excluded[label] = expected
                continue
            data = (run / snapshot).read_bytes()
            if sha(data) != expected:
                raise RuntimeError(f"frozen negative source changed: {label}")
            entries["repo/" + label] = data
    else:
        # Snapshot full text inputs for failed compilation/execution, without
        # pretending they were already frozen by a successful summary receipt.
        manifest = json.loads((repo / "loom/data/pack.json").read_text())
        paths = [repo / "loom/data/pack.json", *(repo / "loom/data" / item["path"] for item in manifest["files"]),
            repo / "loom/src/kb/pack_embedded.inc", repo / "loom/tests/test_generalize_fixture.h",
            repo / "loom/include/loom/generalize.h",
            *sorted((repo / "loom/src/generalize").glob("*.cpp")),
            *sorted((repo / "loom/src/generalize").glob("*.h")),
            *sorted((repo / "loom/tests/fixtures/eval/synthetic_dev").rglob("*"))]
        for path in paths:
            put(path, "repo/" + str(path.relative_to(repo)))
    for name in ("dic0301.cpp.fixture", "dic0301_legacy_classes.json.fixture", "dic0301_replay.py",
                 "dic0301_archive_replay.py", "README.md"):
        path = Path(__file__).resolve().parent / name
        name = "repo/" + str(path.relative_to(repo))
        if name not in entries:
            put(path, name)
    # Do not include on-disk overlays: they intentionally contain malformed
    # JSON. Every control's complete input is in the fixture and native output.
    for name in ("commands.log", "before.json", "after.json", "summary.json", "inputs.json"):
        put(run / name, "run/" + name)
    for directory in (run / "legacy", run / "current"):
        if directory.is_dir():
            for path in sorted(directory.iterdir()):
                put(path, "run/" + str(path.relative_to(run)))
    receipt = {
        "schema": "loom.test.dic0301_archive/1",
        "replay_summary_present": summary is not None,
        "frozen_inputs_receipt_present": inputs is not None,
        "replay_passed": summary["passed"] if summary else None,
        "base_pin": summary["base_pin"] if summary else (inputs["base_pin"] if inputs else "e4109df7e4af22b461def5f7d62e268d9b9a8825"),
        "sha256": {name: sha(data) for name, data in sorted(entries.items())},
        "excluded_native_sha256_receipts": excluded,
        "limitations": ["Native ELF/objects/archives are not included; build from the pinned Git source plus archived source overlay.",
            "Missing replay summary means interrupted/failed evidence; do not certify success from this archive."]}
    entries["ARCHIVE_MANIFEST.json"] = (json.dumps(receipt, ensure_ascii=False, indent=1) + "\n").encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation prevents changing an earlier positive or negative run.
    with output.open("xb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as zipped:
            with tarfile.open(fileobj=zipped, mode="w", format=tarfile.PAX_FORMAT) as archive:
                for name, data in sorted(entries.items()):
                    item = tarfile.TarInfo(name)
                    item.size = len(data)
                    item.mode = 0o644
                    item.mtime = item.uid = item.gid = 0
                    item.uname = item.gname = ""
                    archive.addfile(item, io.BytesIO(data))
    print(json.dumps({"path": str(output), "bytes": output.stat().st_size, "files": len(entries),
        "sha256": sha(output.read_bytes()), "replay_passed": receipt["replay_passed"]}))


if __name__ == "__main__":
    main()
