#!/usr/bin/env python3
"""Run W3's offline regressions against an existing CMake build.

--usage-policy-ref COMMIT compiles the actual W2 policy/configuration sources
in an isolated overlay and links them with W3's corresponding override objects.
It never changes the checkout, the CMake build, credentials, or provider state.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import uuid


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def run_logged(command: list[str], log: Path, cwd: Path | None = None) -> None:
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    log.write_text(result.stdout + result.stderr)
    if result.returncode:
        print(result.stdout + result.stderr, end="")
        raise subprocess.CalledProcessError(result.returncode, command)


def policy_objects(args: argparse.Namespace, root: Path, build: Path,
                   evidence: Path, manifest: dict) -> list[Path]:
    commit = subprocess.check_output(
        ["git", "rev-parse", "--verify", "--end-of-options", args.usage_policy_ref + "^{commit}"],
        cwd=root, text=True).strip()
    manifest["usage_policy_commit"] = commit
    overlay = evidence / "source"
    selected = [root / "loom/src/context/context_engine.cpp", root / "loom/src/context/provider_vector.cpp"]
    for relative in ["loom/include/loom/usage_policy.h", "loom/src/core/config.cpp",
                     "loom/src/core/config_usage_policy.cpp", "loom/src/policy/usage_policy.cpp"]:
        source = subprocess.check_output(["git", "show", f"{commit}:{relative}"], cwd=root)
        target = overlay / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source)
        manifest["sources"][relative + "@" + commit] = hashlib.sha256(source).hexdigest()
        if target.suffix == ".cpp":
            selected.append(target)

    rows = json.loads((build / "compile_commands.json").read_text())
    original = next(row for row in rows if row["file"].replace("\\", "/").endswith("/context/context_engine.cpp"))
    flags = list(original["arguments"]) if "arguments" in original else shlex.split(original["command"])
    flags[0] = args.compiler
    flags.remove("-c")
    flags.remove(original["file"])
    index = flags.index("-o")
    del flags[index:index + 2]
    old_root = str(Path(original["file"]).resolve().parents[3])
    flags = [argument.replace(old_root, str(root)) for argument in flags]
    flags[1:1] = ["-I", str(overlay / "loom/include")]
    flags += ["-O0", "-g" if args.debug_symbols else "-g0", "-Werror"]
    jobs = []
    for index, source in enumerate(selected):
        obj = evidence / f"component_{index}.o"
        command = flags + ["-c", str(source), "-o", str(obj)]
        manifest["commands"].append(command)
        manifest["sources"][str(source)] = digest(source)
        jobs.append((command, evidence / f"component_{index}.log", obj))

    def compile_one(job: tuple[list[str], Path, Path]) -> Path:
        command, log, obj = job
        run_logged(command, log, build)
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        return list(pool.map(compile_one, jobs))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--compiler", default=shutil.which("c++") or "c++")
    parser.add_argument("--usage-policy-ref", help="local W2 Git commit/ref for a real combined policy smoke")
    parser.add_argument("--jobs", type=int, default=2, help="parallel overlay compiles (preset: 2)")
    parser.add_argument("--debug-symbols", action="store_true", help="include debug symbols in verification artifacts")
    args = parser.parse_args()
    if args.jobs <= 0:
        parser.error("--jobs must be positive")
    root, build = args.repo.resolve(), args.build_dir.resolve()
    loom = root / "loom"
    owned = {"selector.verify.cc", "provider_vector.verify.cc", "unified_context.verify.cc", "context_goal_usage.verify.cc"}
    sources = sorted(path for path in (loom / "src").glob("**/*.verify.cc") if path.name in owned)
    if len(sources) != len(owned):
        raise SystemExit("missing W3 verification sources")

    # Retain each run, including first failures. The optional integration is
    # temporary evidence, not a checkout change or a selected branch merge.
    folder = "w3_usage_overlay" if args.usage_policy_ref else "w3_verification"
    evidence = build / folder / uuid.uuid4().hex
    evidence.mkdir(parents=True)
    print(f"W3 verification evidence: {evidence}", flush=True)
    manifest = {"mode": "combined" if args.usage_policy_ref else "offline_w3",
                "sources": {str(source): digest(source) for source in sources}, "commands": []}
    for relative in ("src/chat/chat_engine.cpp", "src/context/context_engine.cpp",
                     "src/context/unified_context.cpp", "src/context/provider_vector.cpp",
                     "src/providers/embedding.cpp"):
        source = loom / relative
        manifest["sources"][str(source)] = digest(source)
    for directory in (loom / "src/context", loom / "src/providers"):
        for header in directory.glob("*.h"):
            manifest["sources"][str(header)] = digest(header)
    objects = []
    try:
        if args.usage_policy_ref:
            objects = policy_objects(args, root, build, evidence, manifest)
        executable = evidence / "w3_selector_verification"
        command = [args.compiler, "-std=c++20", "-O0", "-g" if args.debug_symbols else "-g0",
                   "-Wall", "-Wextra", "-Werror", "-DJSON_USE_IMPLICIT_CONVERSIONS=1",
                   f'-DLOOM_TEST_FIXTURES="{loom / "tests/fixtures"}"']
        if args.usage_policy_ref:
            command += ["-I", str(evidence / "source/loom/include")]
        command += ["-I", str(loom / "include"), "-I", str(loom / "src"), "-I", str(loom / "tests"),
                    "-isystem", str(loom / "third_party/doctest"), "-isystem", str(loom / "third_party/nlohmann"),
                    *map(str, sources), *map(str, objects), "-o", str(executable), str(build / "libloom_core.a"),
                    str(build / "libloom_miniz.a")]
        vendor = build / "libloom_sqlite3_amalgamation.a"
        command += [str(vendor)] if vendor.exists() else ["-lsqlite3"]
        command += ["-lssl", "-lcrypto", "-pthread", "-ldl", "-lm"]
        manifest["commands"].append(command)
        manifest["core_library_sha256"] = digest(build / "libloom_core.a")
        run_logged(command, evidence / "link.log")
        test_command = [str(executable), "--no-intro=true"]
        manifest["commands"].append(test_command)
        run_logged(test_command, evidence / "tests.log")
        print((evidence / "tests.log").read_text(), end="")
    finally:
        (evidence / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
