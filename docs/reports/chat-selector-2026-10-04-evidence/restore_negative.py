#!/usr/bin/env python3
"""Restore one retained W3 failure in a new detached worktree, never in-place."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", choices=("methods-negative-core", "methods-negative-overlay", "methods-negative-runtime"))
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    snapshot = json.loads((Path(__file__).parent / args.case / "source-snapshot.json").read_text())
    destination = args.destination.resolve()
    if destination.exists():
        parser.error("destination must not exist; use a new worktree")
    files = snapshot["files"]
    for relative, content in files.items():
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts or not relative.startswith(("loom/src/context/", "loom/src/chat/", "loom/src/providers/")):
            raise ValueError(f"outside the retained W3 scope: {relative}")
        if not isinstance(content, str):
            raise ValueError(f"not exact UTF-8 source bytes: {relative}")
    subprocess.run(["git", "worktree", "add", "--detach", str(destination), snapshot["base_commit"]], cwd=root, check=True)
    for relative, content in files.items():
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content.encode("utf-8"))
    print(f"Restored {len(files)} exact files for {args.case}: {destination}")
    print(snapshot["reason"])


if __name__ == "__main__":
    main()
