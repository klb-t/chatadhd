#!/usr/bin/env python3
"""Compare two existing-public-API utility probes; retain both full outputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    combined = {"before": {}, "after": {}}
    mismatches = []
    for name, level in (("default", None), ("debug", "DEBUG"), ("warning", "warn"),
                        ("error", "error"), ("unknown", "unrecognized"), ("fallback", None)):
        env = os.environ.copy()
        env["LOOM_LOG_STDERR"] = "0"
        if level is None:
            env.pop("LOOM_LOG_LEVEL", None)
        else:
            env["LOOM_LOG_LEVEL"] = level
        if name == "fallback":
            env["TMPDIR"] = str(args.out / "absent-system-temp-fixture")
        pair = []
        for variant, executable in (("before", args.before), ("after", args.after)):
            process = subprocess.run([str(executable.resolve())], env=env,
                                     check=False, capture_output=True)
            (args.out / f"{name}-{variant}.json").write_bytes(process.stdout)
            (args.out / f"{name}-{variant}.stderr").write_bytes(process.stderr)
            if process.returncode:
                raise RuntimeError(f"{variant} failed for {name}: exit {process.returncode}")
            pair.append(process.stdout)
            combined[variant][name] = json.loads(process.stdout)
        if pair[0] != pair[1]:
            mismatches.append(name)
    for variant in ("before", "after"):
        data = (json.dumps(combined[variant], ensure_ascii=False, separators=(",", ":")) + "\n").encode()
        (args.out / f"{variant}.json").write_bytes(data)
        print(f"{variant}: {len(data)} bytes; sha256={hashlib.sha256(data).hexdigest()}")
    if mismatches:
        raise RuntimeError("different public outputs: " + ", ".join(mismatches))
    print("6/6 environment cases: byte-identical raw outputs")


if __name__ == "__main__":
    main()
