#!/usr/bin/env python3
"""Record source, completed-build objects and executables without modifying them."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--loom", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    loom, build = args.loom.resolve(), args.build.resolve()
    root = loom.parent
    sources = []
    for folder in ("src", "include", "data", "tests", "cli", "server"):
        for directory, children, files in os.walk(loom / folder):
            # Prune before entering fixture/result directories. This manifest
            # is code provenance; the DEV runner records its chosen inputs.
            children[:] = sorted(d for d in children if d not in {
                "fixtures", "results", "__pycache__", "build"})
            for filename in sorted(files):
                p = Path(directory) / filename
                if p.suffix in {".cpp", ".cc", ".c", ".h", ".json", ".py", ".pack"}:
                    sources.append(p)
    sources += [loom / "CMakeLists.txt", loom / "CMakePresets.json"]
    binaries = [p for p in build.rglob("*") if p.is_file() and not p.is_symlink()
                and (p.suffix in {".o", ".a"} or p.name in {
                    "loom_tests", "loom_compat_tool", "loom_candidate_graph_native_tool",
                    "loom", "loom-server"} or p.name.startswith("libloom.so."))]
    artifacts = {}
    for path in sorted(binaries):
        with path.open("rb") as stream:
            signature = stream.read(8)
        if signature[:4] != b"\x7fELF" and signature != b"!<arch>\n":
            raise SystemExit("Not a native object, executable or archive: " + str(path))
        artifacts[str(path.relative_to(build))] = {"sha256": sha256(path), "bytes": path.stat().st_size}
    source_hashes = {str(p.relative_to(root)): sha256(p) for p in sorted(set(sources))}
    receipt = {"head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
               "status": subprocess.check_output(["git", "status", "--short"], cwd=root, text=True).splitlines(),
               "sources": source_hashes, "native_artifacts": artifacts,
               "cache_sha256": sha256(build / "CMakeCache.txt"),
               "compile_commands_sha256": sha256(build / "compile_commands.json")}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    print(f"Recorded {len(source_hashes)} sources and {len(artifacts)} native artifacts.")


if __name__ == "__main__":
    main()
