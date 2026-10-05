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


def cache_receipt(path: Path, fields: list[str]) -> dict:
    """Record selected build inputs, without copying unrelated cache values."""
    requested = set(fields)
    if not requested or len(requested) != len(fields):
        raise ValueError("CMake fields must be nonempty and unique")
    raw = path.read_bytes()
    values = {}
    for line in raw.decode("utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        declaration, value = line.split("=", 1)
        key, separator, kind = declaration.partition(":")
        if key not in requested:
            continue
        if not separator or not kind or key in values:
            raise ValueError(f"invalid or duplicate CMake field: {key}")
        values[key] = {"type": kind, "value": value}
    missing = requested - values.keys()
    if missing:
        raise ValueError(f"missing CMake fields: {', '.join(sorted(missing))}")
    return {"path": str(path), "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest(), "fields": values}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--binary", type=Path, action="append", required=True)
    parser.add_argument("--optional-binary", type=Path, action="append", default=[])
    parser.add_argument("--cmake-cache", type=Path)
    parser.add_argument("--cmake-field", action="append", default=[])
    args = parser.parse_args()
    if bool(args.cmake_cache) != bool(args.cmake_field):
        parser.error("--cmake-cache and at least one --cmake-field must be supplied together")
    configuration = None
    if args.cmake_cache:
        try:
            configuration = cache_receipt(args.cmake_cache, args.cmake_field)
        except (OSError, UnicodeError, ValueError) as exc:
            parser.error(str(exc))
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
    if configuration is not None:
        receipt["cmake_configuration"] = configuration
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
