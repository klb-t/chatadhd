#!/usr/bin/env python3
"""Pin the source tree and actual executables used by a CTest run."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path, action="append", required=True)
    parser.add_argument("--optional-binary", type=Path, action="append", default=[])
    args = parser.parse_args()
    binaries = []
    for required, paths in ((True, args.binary), (False, args.optional_binary)):
        for path in paths:
            if not path.is_file():
                if required:
                    parser.error(f"required executable missing: {path}")
                binaries.append({"path": str(path), "present": False})
                continue
            binaries.append({"path": str(path), "present": True,
                             "bytes": path.stat().st_size, "sha256": digest(path)})
    git = lambda revision: subprocess.check_output(
        ["git", "rev-parse", revision], text=True).strip()
    receipt = {
        "schema": "loom.build_evidence/1",
        "preset": args.preset,
        "source_commit": git("HEAD"),
        "source_tree": git("HEAD^{tree}"),
        "python": platform.python_version(),
        "platform": platform.system(),
        "architecture": platform.machine(),
        "binaries": binaries,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
