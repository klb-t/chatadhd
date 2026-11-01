"""Small serial standalone native harness; no core library, DB, ffmpeg or helper tests.

Compiles the new plan and only actual pure SHA/JSON/ID dependency sources. No
foreign archive, model stubs or mocked validator. Objects/binary are ephemeral.
"""
import argparse
import json
import os
from pathlib import Path
import resource
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
BUDGET = 8 * 1024 * 1024


def guard(directory=None):
    for target in (Path("/"), ROOT, Path(directory) if directory else Path("/tmp")):
        v = os.statvfs(target)
        if v.f_bavail * v.f_frsize < 1073741824:
            raise RuntimeError("BLOCKED_CAPACITY: global/target free < 1 GiB")
    if directory and sum(p.stat().st_size for p in Path(directory).rglob("*") if p.is_file()) > BUDGET:
        raise RuntimeError("output budget exceeded")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    guard()
    # No shell, credential environment, inherited archives or compiler wrappers.
    env = {"PATH": os.defpath, "LC_ALL": "C"}
    start = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="video-source-plan-") as temp:
        directory = Path(temp)
        include = ["-I" + str(ROOT / p) for p in
                   ("loom/include", "loom/src", "loom/third_party/nlohmann", "loom/third_party/doctest")]
        flags = ["-std=c++20", "-Os", "-g0", "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror"]
        flags += ['-DVIDEO_PLAN_POLICY="' + str(ROOT / "loom/src/import/video_source_plan/policy.json") + '"']
        sources = ["loom/src/import/video_source_plan/source_plan.cpp", "loom/tests/video_source_plan/test_source_plan.cpp",
                   "loom/src/util/sha256.cpp", "loom/src/util/json.cpp", "loom/src/util/utf8.cpp",
                   "loom/src/model/model_core.cpp", "loom/src/kb/evidence.cpp"]
        # Bound this serial light harness, including compiler subprocesses.
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        resource.setrlimit(resource.RLIMIT_FSIZE, (BUDGET, BUDGET))
        resource.setrlimit(resource.RLIMIT_AS, (1073741824, 1073741824))
        resource.setrlimit(resource.RLIMIT_CPU, (120, 120))
        objects = []
        for index, source in enumerate(sources):
            guard(directory)
            target = directory / f"{index}.o"
            print("compile", source, flush=True)
            subprocess.run([args.cxx, *flags, *include, "-c", str(ROOT / source), "-o", str(target)],
                           check=True, timeout=45, env=env)
            objects.append(str(target))
        guard(directory)
        binary = directory / "focused-tests"
        subprocess.run([args.cxx, *objects, "-Wl,--gc-sections", "-o", str(binary)], check=True, timeout=15, env=env)
        guard(directory)
        output_bytes = sum(p.stat().st_size for p in directory.iterdir() if p.is_file())
        print("RUN new native plan tests (once)", flush=True)
        result = subprocess.run([str(binary), "--no-colors"], timeout=15, env=env)
        print(json.dumps({"native_test_exit": result.returncode, "elapsed_seconds": round(time.monotonic() - start, 3),
                          "temporary_output_bytes": output_bytes,
                          "helper_tests_rerun": False, "core_library_or_DB_linked": False}), flush=True)
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
