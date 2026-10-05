#!/usr/bin/env python3
"""Compile a public pure archive probe against two matching static builds.

Requires completed native builds; runs offline. The before/after probe outputs
must match byte for byte, with nonempty expansion and relation observations.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess


def probe(repository, build, output, source, current, compiler):
    binary = output.with_suffix("")
    command = [compiler, "-std=c++20", "-g0", "-DJSON_USE_IMPLICIT_CONVERSIONS=1",
               "-I" + str(repository / "loom/include"),
               "-I" + str(repository / "loom/src"),
               "-isystem", str(repository / "loom/third_party/nlohmann")]
    if current:
        command.append("-DARCHIVE_NEW")
    command += [str(source), "-o", str(binary), str(build / "libloom_core.a"),
                str(build / "libloom_sqlite3_amalgamation.a"), "-ldl", "-lm",
                str(build / "libloom_miniz.a"), "-lssl", "-lcrypto", "-lpthread"]
    subprocess.run(command, check=True)
    with output.open("wb") as stream:
        subprocess.run([str(binary)], stdout=stream, check=True)
    return command


if __name__ == "__main__":
    repository = pathlib.Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-repo", type=pathlib.Path, required=True)
    parser.add_argument("--baseline-build", type=pathlib.Path, required=True)
    parser.add_argument("--candidate-repo", type=pathlib.Path, default=repository)
    parser.add_argument("--candidate-build", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--cxx", default="c++")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    source = repository / "loom/tests/fixtures/archive_profile_parity/probe.cpp"
    before = args.output / "before.json"
    after = args.output / "after.json"
    commands = [
        probe(args.baseline_repo.resolve(), args.baseline_build.resolve(), before,
              source, False, args.cxx),
        probe(args.candidate_repo.resolve(), args.candidate_build.resolve(), after,
              source, True, args.cxx),
    ]
    previous, current = before.read_bytes(), after.read_bytes()
    observations = json.loads(current)
    receipt = {"exact_equal": previous == current, "before_bytes": len(previous),
               "after_bytes": len(current), "sha256": hashlib.sha256(current).hexdigest(),
               "coverage": {key: len(value) for key, value in observations.items()},
               "probe_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
               "commands": commands}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if previous != current:
        raise SystemExit("Archive pure defaults changed")
    if not observations["added"] or not observations["edges"]:
        raise SystemExit("Archive probe needs nonempty expansion and relation observations")
