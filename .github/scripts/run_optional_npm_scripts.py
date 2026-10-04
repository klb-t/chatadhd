#!/usr/bin/env python3
"""Run requested npm scripts when declared by the checked-out package.json.

An absent script is an unavailable capability, never an executed test. A
declared script is started through argv, and any process failure fails this
command. Script names and the npm executable are caller-configured.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-json", type=Path, required=True)
    parser.add_argument("--npm", default="npm", help="npm executable, without shell arguments")
    parser.add_argument("--script", action="append", required=True,
                        help="requested script name; repeat to configure the sequence")
    args = parser.parse_args(argv)
    package_path = args.package_json.resolve()
    try:
        package = json.loads(package_path.read_text(encoding="utf-8"))
        if not isinstance(package, dict):
            raise ValueError("package.json must be an object")
        scripts = package.get("scripts", {})
        if not isinstance(scripts, dict):
            raise ValueError("package.json scripts must be an object")
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"CONFIGURATION_ERROR: {package_path}: {exc}", file=sys.stderr)
        return 2

    for name in args.script:
        if name not in scripts:
            print(f"CAPABILITY_UNAVAILABLE: npm script {name!r} is not declared in "
                  f"{package_path}; no command executed for this capability.", flush=True)
            continue
        command = [args.npm, "run", "--", name]
        print(f"START: npm script {name!r}; argv={shlex.join(command)}; "
              f"cwd={package_path.parent}", flush=True)
        try:
            result = subprocess.run(command, cwd=package_path.parent, check=False)
        except OSError as exc:
            print(f"START_FAILED: npm script {name!r}: {exc}", file=sys.stderr)
            return 127 if isinstance(exc, FileNotFoundError) else 126
        status = result.returncode if result.returncode >= 0 else 128 - result.returncode
        print(f"PROCESS_RESULT: npm script {name!r}; exit={status}", flush=True)
        if status:
            return status
    return 0


if __name__ == "__main__":
    sys.exit(main())
