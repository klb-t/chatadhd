#!/usr/bin/env bash
# deploy/build-linux.sh — release build of the Loom C++ kernel + tests.
#
# What this builds: loom_core, libloom.so (LOOM_SHARED=ON, part of the
# `release` preset) and, once loom/server exists, loom-server — then runs
# the full unit + compat test suite (ctest). This is the same thing
# deploy/Dockerfile build stage does, minus the container;
# use this for a quick local/CI check without Docker.
#
# Usage: deploy/build-linux.sh [extra cmake -D args...]
#   deploy/build-linux.sh                        # library + tests only
#   deploy/build-linux.sh -DLOOM_BUILD_SERVER=ON  # + loom-server, if present
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
loom_dir="$(cd -- "${script_dir}/../loom" >/dev/null 2>&1 && pwd)"

echo "==> deploy/build-linux.sh: configuring (release preset)" >&2
cmake --preset release -S "${loom_dir}" "$@"

echo "==> building" >&2
cmake --build "${loom_dir}/build/release" -j"$(nproc)"

echo "==> testing (ctest)" >&2
ctest --test-dir "${loom_dir}/build/release" --output-on-failure -j"$(nproc)"

echo "==> done: ${loom_dir}/build/release/" >&2
