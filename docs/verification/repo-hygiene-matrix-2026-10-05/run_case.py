#!/usr/bin/env python3
"""Record one already-built, complete CTest preset without changing its gates."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--preset", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ffi-companion", type=Path)
    parser.add_argument("--companion-cache", type=Path)
    args = parser.parse_args()
    root, out = args.source.resolve(), args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    build = root / "loom/build" / args.preset
    env = os.environ.copy()
    if args.ffi_companion:
        env["LOOM_LIBRARY"] = str(args.ffi_companion.resolve())
        if not args.companion_cache:
            parser.error("--ffi-companion requires --companion-cache")
    elif args.companion_cache:
        parser.error("--companion-cache requires --ffi-companion")
    commands = []

    def run(argv, filename, cwd=root, required=True):
        start = datetime.now(timezone.utc).isoformat()
        with (out / filename).open("wb") as stream:
            result = subprocess.run(argv, cwd=cwd, env=env, stdout=stream,
                                    stderr=subprocess.STDOUT)
        commands.append({"argv": [str(x) for x in argv], "cwd": str(cwd),
                         "started_at_utc": start, "exit_code": result.returncode})
        (out / "commands.json").write_text(json.dumps({
            "source": str(root), "preset": args.preset,
            "explicit_environment": {key: env[key] for key in
                ("LOOM_LIBRARY", "LD_LIBRARY_PATH") if key in env},
            "commands": commands}, indent=2) + "\n")
        if required and result.returncode:
            raise SystemExit(result.returncode)
        return result.returncode

    def receipt(preset, cache, binaries, filename):
        fields = ["CMAKE_GENERATOR", "CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER",
                  "CMAKE_CXX_COMPILER", "CMAKE_C_FLAGS", "CMAKE_C_FLAGS_DEBUG",
                  "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_DEBUG",
                  "LOOM_USE_SYSTEM_SQLITE", "LOOM_SHARED", "LOOM_WERROR",
                  "LOOM_BUILD_SERVER"]
        argv = ["python3", root / ".github/scripts/write_build_receipt.py",
                "--preset", preset, "--cmake-cache", cache,
                "--output", out / filename]
        for field in fields:
            argv += ["--cmake-field", field]
        for binary in binaries:
            argv += ["--binary", binary]
        run(argv, filename + ".log")

    binaries = [build / name for name in ("loom_tests", "loom_compat_tool",
                "loom_candidate_graph_native_tool", "cli/loom", "server/loom-server")]
    if (build / "libloom.so").is_file():
        binaries.append(build / "libloom.so")
    receipt(args.preset, build / "CMakeCache.txt", binaries, "build-receipt.json")
    if args.ffi_companion:
        receipt("ordinary-ffi-companion", args.companion_cache.resolve(),
                [args.ffi_companion.resolve()], "ffi-companion-receipt.json")
    tracked = subprocess.check_output(["git", "ls-files", "-z", "loom", "engine",
        "core", "docs/contracts", ".github"], cwd=root).decode().split("\0")
    inputs = []
    for path in tracked:
        file = root / path
        if not file.is_file() or path.startswith("loom/tools/seeding/results/"):
            continue
        raw = file.read_bytes()
        inputs.append({"path": path, "bytes": len(raw),
                       "sha256": hashlib.sha256(raw).hexdigest()})
    (out / "source-sha256.json").write_text(json.dumps(inputs, indent=2) + "\n")
    run(["ctest", "--preset", args.preset, "--show-only=json-v1"],
        "ctest-manifest.json", root / "loom")
    status = run(["ctest", "--preset", args.preset, "--no-tests=error",
        "--test-output-size-passed", "10485760", "--test-output-size-failed",
        "10485760", "--output-junit", out / "ctest.xml"],
        "ctest.log", root / "loom", required=False)
    temporary = build / "Testing/Temporary"
    for name in ("LastTest.log", "LastTestsFailed.log"):
        if (temporary / name).is_file():
            shutil.copyfile(temporary / name, out / name)
    gate = run(["python3", root / ".github/scripts/verify_ctest.py",
        "--policy", root / ".github/ctest-evidence-policy.json", "--preset",
        args.preset, "--manifest", out / "ctest-manifest.json", "--junit",
        out / "ctest.xml", "--output", out / "executed-cases.json"],
        "guard.log", required=False)
    for filename in ("build-receipt.json", "ffi-companion-receipt.json"):
        path = out / filename
        if not path.is_file():
            continue
        data = json.loads(path.read_text())
        for binary in data["binaries"]:
            current = Path(binary["path"]).read_bytes()
            if hashlib.sha256(current).hexdigest() != binary["sha256"]:
                raise RuntimeError("binary changed during tests: " + binary["path"])
    raise SystemExit(status or gate)


if __name__ == "__main__":
    main()
