"""Verify the optional CTest hook without editing the checked-out root build.

Checks exact main and B pins, applies the patch only in temporary directories,
then executes its actual CTest registration against the supplied native build.
No native build or schema result is inferred from patch applicability.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


MAIN_BASE = "9e20f99ab27e7cd45e1892f83bf60fbe60db3de9"
B_BASE = "b20c0d8ac37934e1600c3b9216492fd524220941"


def verify(library, cmake="cmake", ctest="ctest"):
    here = Path(__file__).resolve().parent
    repo = here.parents[2]
    patch = here / "integration.patch"
    if not Path(library).is_file():
        raise FileNotFoundError("an actual libloom shared build is required")
    with tempfile.TemporaryDirectory(prefix="resource-graph-hook-") as directory:
        root = Path(directory)
        added = None
        for base in (MAIN_BASE, B_BASE):
            target = root / base
            (target / "loom").mkdir(parents=True)
            original = subprocess.check_output(
                ["git", "show", base + ":loom/CMakeLists.txt"], cwd=repo, text=True)
            cmake_file = target / "loom/CMakeLists.txt"
            cmake_file.write_text(original)
            subprocess.run(["git", "apply", "--check", str(patch)], cwd=target, check=True)
            subprocess.run(["git", "apply", str(patch)], cwd=target, check=True)
            updated = cmake_file.read_text()
            marker = "    # Offline research mechanism/protocol checks, separate from quality gates.\n"
            beginning = original.split(marker)[0]
            added = updated[len(beginning):].split(marker)[0]
            if "add_test(NAME resource_graph.contract" not in added:
                raise AssertionError("the expected registration block was not applied")
            print("patch applies:", base, flush=True)
        project = root / "registration"
        project.mkdir()
        # Run precisely the new registration block. An imported native target
        # supplies $<TARGET_FILE:loom> without rebuilding/modifying the repo.
        source = f'''cmake_minimum_required(VERSION 3.24)
project(ResourceGraphHook NONE)
enable_testing()
set(LOOM_SHARED ON)
set(LOOM_ROOT "{repo / 'loom'}")
set(Python3_EXECUTABLE "{sys.executable}")
set(_loom_python_env "PYTHONPATH={repo};PYTHONDONTWRITEBYTECODE=1")
add_library(loom SHARED IMPORTED)
set_target_properties(loom PROPERTIES IMPORTED_LOCATION "{Path(library).resolve()}")
{added}
'''
        (project / "CMakeLists.txt").write_text(source)
        build = project / "build"
        subprocess.run([cmake, "-S", str(project), "-B", str(build)], check=True)
        subprocess.run([ctest, "--test-dir", str(build), "--output-on-failure",
                        "--no-tests=error", "-R", "^resource_graph.contract$"], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ctest", default="ctest")
    args = parser.parse_args()
    verify(args.library, args.cmake, args.ctest)
