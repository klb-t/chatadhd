#!/usr/bin/env bash
# loom/deploy/build-linux.sh — release build of the Loom C++ kernel + tests.
#
# What this builds: loom_core, libloom.so (LOOM_SHARED=ON, part of the
# `release` preset) and, once loom/server exists, loom-server — then runs
# the full unit + compat test suite (ctest). This is the same thing
# loom/deploy/Dockerfile's server-builder stage does, minus the container;
# use this for a quick local/CI check without Docker.
#
# Usage: loom/deploy/build-linux.sh [extra cmake -D args...]
#   loom/deploy/build-linux.sh                        # library + tests only
#   loom/deploy/build-linux.sh -DLOOM_BUILD_SERVER=ON  # + loom-server, if present
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
loom_dir="$(cd -- "${script_dir}/.." >/dev/null 2>&1 && pwd)"

echo "==> loom/deploy/build-linux.sh: configuring (release preset)" >&2
cmake --preset release -S "${loom_dir}" "$@"

echo "==> building" >&2
cmake --build "${loom_dir}/build/release" -j"$(nproc)"

echo "==> testing (ctest)" >&2
ctest --test-dir "${loom_dir}/build/release" --output-on-failure -j"$(nproc)"

echo "==> done: ${loom_dir}/build/release/" >&2
