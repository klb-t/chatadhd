#!/usr/bin/env python3
"""Rebuild the scoped offline test/parity evidence with an explicit baseline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[5])
parser.add_argument("--baseline-source", type=Path, required=True)
parser.add_argument("--baseline-build", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
repo, old, build, out = (p.resolve() for p in (args.repo, args.baseline_source, args.baseline_build, args.output))
out.mkdir(parents=True, exist_ok=True)
cxx = shlex.split(os.environ.get("CXX", "c++"))
flags = ["-std=c++20", "-g0", "-Wall", "-Wextra", "-Wpedantic", "-Wconversion", "-Wsign-conversion", "-Werror"]
libs = [str(build / p) for p in ("libloom_core.a", "libloom_sqlite3_amalgamation.a", "libloom_miniz.a")]
libs += ["-pthread", "-ldl", "-lm", "-lssl", "-lcrypto"]
probe = repo / "loom/tests/compat/profile_archive_text_parity.cc"


def run(command, stdout=None):
    subprocess.run(command, cwd=repo, check=True, stdout=stdout)


new_inc = [f"-I{repo / p}" for p in ("loom/include", "loom/src", "loom/third_party/nlohmann", "loom/third_party/doctest", "loom/tests")]
old_inc = [f"-I{old / p}" for p in ("loom/include", "loom/src")]
old_inc += [f"-I{repo / 'loom/third_party/nlohmann'}"]
run(cxx + flags + old_inc + [str(probe)] + libs + ["-o", str(out / "before")])
objects = []
sources = ["archive/text.cpp", "archive/items.cpp", "archive/vocab.cpp", "archive/profile.cpp", "model/runtime_profile.cpp"]
for source in sources:
    obj = out / (source.replace("/", "-") + ".o")
    run(cxx + flags + new_inc + ["-c", str(repo / "loom/src" / source), "-o", str(obj)])
    objects.append(str(obj))
run(cxx + flags + new_inc + [str(probe)] + objects + libs + ["-o", str(out / "after")])
run(cxx + flags + new_inc + [str(repo / "loom/tests/main.cpp"), str(repo / "loom/tests/test_archive_text_policy.cpp")]
    + objects + libs + ["-o", str(out / "focused")])
for side in ("before", "after"):
    with (out / (side + ".json")).open("wb") as stream:
        run([str(out / side)], stdout=stream)
with (out / "focused-tests.log").open("wb") as stream:
    run([str(out / "focused"), "--no-colors"], stdout=stream)
before, after = ((out / (side + ".json")).read_bytes() for side in ("before", "after"))
receipt = {"schema": "loom.archive_text_policy.parity/1", "byte_identical": before == after,
           "before_bytes": len(before), "after_bytes": len(after),
           "before_sha256": hashlib.sha256(before).hexdigest(), "after_sha256": hashlib.sha256(after).hexdigest(),
           "baseline_source": str(old), "baseline_build": str(build),
           "baseline_core_sha256": hashlib.sha256((build / "libloom_core.a").read_bytes()).hexdigest(),
           "output_counts": {key: len(value) for key, value in json.loads(before).items()},
           "source_sha256": {source: hashlib.sha256((repo / "loom/src" / source).read_bytes()).hexdigest() for source in sources}}
(out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt, indent=2))
if before != after:
    raise SystemExit("Default output differs; complete before/after files retained.")
