#!/usr/bin/env bash
# Offline standalone fixture against the matching, freshly built dev libraries.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../../../.."
KLB_SEMANTIC_FIXTURE_BIN="${1:-/tmp/loom-semantic-prompt-integration}"
"${CXX:-c++}" -std=c++20 -O0 -g0 -Wl,--no-keep-memory \
  -Iloom/include -Iloom/src -Iloom/tests \
  -Iloom/third_party/nlohmann -Iloom/third_party/doctest \
  -Iloom/third_party/miniz \
  -x c++ loom/src/extract/tests/semantic_prompt_integration.cpp.fixture -x none \
  loom/build/dev/libloom_core.a \
  loom/build/dev/libloom_sqlite3_amalgamation.a \
  loom/build/dev/libloom_miniz.a -pthread -ldl -lm -lssl -lcrypto \
  -o "${KLB_SEMANTIC_FIXTURE_BIN}"
"${KLB_SEMANTIC_FIXTURE_BIN}" --no-colors=1
