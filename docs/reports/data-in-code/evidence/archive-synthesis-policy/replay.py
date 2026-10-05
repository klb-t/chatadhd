#!/usr/bin/env python3
"""Offline isolated synthesis parity; requires an already built candidate library."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    evidence = Path(__file__).resolve().parent
    commands = json.loads((build / "compile_commands.json").read_text())
    production = next(x for x in commands if x["file"].endswith("/archive/synth.cpp"))
    test = next(x for x in commands if x["file"].endswith("/tests/test_archive.cpp"))
    source = Path(production["file"])
    repo = source.parents[3]
    recorded = []

    def compile_object(name, path, template=production, defines=()):
        words = shlex.split(template["command"])
        # Keep all project warning/standard/include/definition flags.
        index = words.index("-o")
        words[index + 1] = str(out / (name + ".o"))
        words[words.index("-c") + 1] = str(path)
        words[1:1] = list(defines)
        recorded.append(words)
        with (out / (name + ".compile.log")).open("wb") as log:
            subprocess.run(words, cwd=template["directory"], stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        return str(out / (name + ".o"))

    before = compile_object("synth-before", evidence / "synth-before.cpp.txt", defines=["-x", "c++"])
    after = compile_object("synth-after", source)
    profile = compile_object("profile-current", source.parent / "profile.cpp")
    framework = compile_object("framework-current", source.parents[1] / "model/runtime_profile.cpp")
    probe = compile_object("probe", repo / "loom/tests/test_archive_synthesis_policy.cpp", test,
                           ["-DARCHIVE_SYNTHESIS_POLICY_PROBE=1"])
    tests = compile_object("tests", repo / "loom/tests/test_archive_synthesis_policy.cpp", test)
    runner = compile_object("runner", evidence / "runner.cpp.txt", test, ["-x", "c++"])
    libs = [str(build / "libloom_core.a"), str(build / "libloom_sqlite3_amalgamation.a"),
            str(build / "libloom_miniz.a"), "-pthread", "-ldl", "-lm", "-lssl", "-lcrypto"]

    def link_run(name, objects, filename, arguments=()):
        link = [shlex.split(production["command"])[0], *objects, profile, framework,
                *libs, "-o", str(out / name)]
        recorded.append(link)
        with (out / (name + ".link.log")).open("wb") as log:
            subprocess.run(link, stdout=log, stderr=subprocess.STDOUT, check=True)
        with (out / filename).open("wb") as stdout, (out / (filename + ".stderr")).open("wb") as stderr:
            subprocess.run([str(out / name), *arguments], stdout=stdout, stderr=stderr, check=True)

    link_run("before-probe", [probe, before], "before.json")
    link_run("after-probe", [probe, after], "after.json")
    link_run("synthesis-tests", [runner, tests, after], "tests.stdout", ["--test-suite=archive.synthesis_policy"])
    old, new = (out / "before.json").read_bytes(), (out / "after.json").read_bytes()
    receipt = {"schema": "loom.archive_synthesis_policy.parity/1", "byte_equal": old == new,
               "before_bytes": len(old), "after_bytes": len(new),
               "before_sha256": hashlib.sha256(old).hexdigest(),
               "after_sha256": hashlib.sha256(new).hexdigest(), "commands": recorded}
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({key: value for key, value in receipt.items() if key != "commands"}))
    if old != new:
        raise SystemExit("Default synthesis differs; full outputs are preserved.")


if __name__ == "__main__":
    main()
