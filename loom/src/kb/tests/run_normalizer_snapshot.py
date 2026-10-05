#!/usr/bin/env python3
"""Compile against an existing core build, capture behavior, optionally compare.

The launcher never builds or edits the core. Provenance is outside snapshot.json
so a before/after byte comparison checks behavior rather than changing hashes.
Use --corpus before/corpus.json to freeze the identical input set across builds.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shlex
import subprocess
import sys


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: pathlib.Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8")


def invocation(command: list[str], cwd: pathlib.Path, output: pathlib.Path, prefix: str) -> subprocess.CompletedProcess[bytes]:
    result = subprocess.run(command, cwd=cwd, capture_output=True)
    (output / f"{prefix}.stdout").write_bytes(result.stdout)
    (output / f"{prefix}.stderr").write_bytes(result.stderr)
    write_json(output / f"{prefix}.command.json", {"argv": command, "cwd": str(cwd), "exit_code": result.returncode})
    if result.returncode:
        raise RuntimeError(f"{prefix} failed ({result.returncode}); see {output / (prefix + '.stderr')}")
    return result


def compile_command(build: pathlib.Path, source: pathlib.Path, binary: pathlib.Path) -> tuple[list[str], pathlib.Path]:
    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    original = next(item for item in commands if pathlib.Path(item["file"]).as_posix().endswith("/kb/normalize.cpp"))
    arguments = original.get("arguments") or shlex.split(original["command"])
    command = [arguments[0]]
    index = 1
    while index < len(arguments):
        argument = arguments[index]
        if argument in {"-o", "-MT", "-MQ", "-MF"}:
            index += 2
            continue
        if argument in {"-c", "-MD", "-MMD", "-MP"} or argument == original["file"]:
            index += 1
            continue
        # Joined forms are legal too. Never reuse a core object's dependency
        # target/file when compiling this independent executable.
        if any(argument.startswith(flag) and argument != flag for flag in ("-MT", "-MQ", "-MF")):
            index += 1
            continue
        command.append(argument)
        index += 1
    core = build / "libloom_core.a"
    miniz = build / "libloom_miniz.a"
    if not core.is_file() or not miniz.is_file():
        raise RuntimeError("a completed core build with libloom_core.a and libloom_miniz.a is required")
    command += [str(source), "-o", str(binary), "-Wl,--no-keep-memory", str(core)]
    sqlite = build / "libloom_sqlite3_amalgamation.a"
    command += [str(sqlite)] if sqlite.is_file() else ["-lsqlite3"]
    command += [str(miniz), "-lssl", "-lcrypto", "-pthread", "-ldl", "-lm"]
    return command, pathlib.Path(original["directory"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True, help="new evidence directory; never overwritten")
    parser.add_argument("--corpus", type=pathlib.Path, help="reuse the baseline's exact public synthetic inputs")
    parser.add_argument("--compare", type=pathlib.Path, help="baseline snapshot.json for exact byte equality")
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    source = pathlib.Path(__file__).with_name("normalizer_snapshot.cc").resolve()
    repository = source.parents[4]
    output.mkdir(parents=True, exist_ok=False)
    status: dict[str, object] = {"schema": "loom.normalizer_snapshot_verification/1", "paid_calls": 0}
    try:
        binary = output / "normalizer_snapshot"
        command, cwd = compile_command(build, source, binary)
        sources = [source, pathlib.Path(__file__).resolve(), repository / "loom/src/kb/normalize.cpp",
                   repository / "loom/include/loom/kb.h", repository / "loom/src/kb/pack.cpp",
                   repository / "loom/src/kb/pack_embedded.inc", repository / "loom/data/lexicons/stemming.json"]
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repository, capture_output=True, check=True).stdout.decode().strip()
        archives = [build / name for name in ("libloom_core.a", "libloom_miniz.a", "libloom_sqlite3_amalgamation.a")]
        write_json(output / "provenance.json", {
            "git_commit": head, "build": str(build),
            "sources": {str(path.relative_to(repository)): digest(path) for path in sources},
            "archives": {path.name: digest(path) for path in archives if path.is_file()},
            "scope": "native normalizer API outputs; offline builtin lexicons and public synthetic inputs",
        })
        invocation(command, cwd, output, "compile")
        if args.corpus:
            (output / "corpus.json").write_bytes(args.corpus.resolve().read_bytes())
        else:
            corpus = invocation([str(binary), "--corpus"], cwd, output, "corpus")
            (output / "corpus.json").write_bytes(corpus.stdout)
        observed = invocation([str(binary), str(output / "corpus.json")], cwd, output, "observe")
        (output / "snapshot.json").write_bytes(observed.stdout)
        snapshot = json.loads(observed.stdout)
        status.update({"token_records": len(snapshot["tokens"]), "phrase_records": len(snapshot["phrases"]),
                       "corpus_sha256": digest(output / "corpus.json"), "snapshot_sha256": digest(output / "snapshot.json"),
                       "native_execution": "PASS"})
        if args.compare:
            baseline = args.compare.resolve()
            equal = baseline.read_bytes() == observed.stdout
            status.update({"baseline": str(baseline), "baseline_sha256": digest(baseline),
                           "byte_equal_to_baseline": equal})
            if not equal:
                raise RuntimeError("before/after native snapshots differ; both artifacts were preserved")
        write_json(output / "verification.json", status)
        print(json.dumps(status, ensure_ascii=False, sort_keys=True))
        return 0
    except (OSError, RuntimeError, ValueError, StopIteration, subprocess.CalledProcessError) as error:
        status.update({"status": "FAIL", "error": str(error)})
        write_json(output / "verification.json", status)
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
