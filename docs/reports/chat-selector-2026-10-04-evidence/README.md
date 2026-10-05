# W3 selected verification — 2026-10-04

The source-private runner executes synthetic offline W3 regressions against the
production CMake core. The combined runner compiles exact pinned W2/W4 sources in
a temporary overlay; it does not merge them, call providers, or change CMake/CTest.

Reproduce from the final W3 implementation commit after fetching both dependencies:

```sh
cmake -S loom -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS_DEBUG=-g0 -DCMAKE_C_FLAGS_DEBUG=-g0 \
  '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=gold -Wl,--no-map-whole-files' \
  -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_WERROR=ON -DLOOM_BUILD_TESTS=ON \
  -DLOOM_BUILD_CLI=ON -DLOOM_BUILD_SERVER=ON -DLOOM_SHARED=ON
cmake --build build --parallel 1
env -u PYTHONPATH -u TMPDIR PYTHONUNBUFFERED=1 \
  ctest --test-dir build --output-on-failure --parallel 1 --no-tests=error
python3 loom/src/chat/verify_selector.py build --jobs 1
python3 loom/src/chat/verify_selector.py build --jobs 1 \
  --usage-policy-ref 910a1d6b3764f82c78e39c5b0adca796ba1168e5 \
  --packet-ref 14eccaf679c3897c82b252ca8504f35f653d93f0
```

Manifests retain commands, source/header hashes and production core SHA256. Paths
identify the original environment; the commands above use a new local build.
These new `.verify.cc` cases are not yet registered in CMake/CTest (outside W3's
file ownership); the existing full CTest remains unchanged. Counts of CTest
processes are distinct from executed native/Python cases and runner assertions.
The inherited `unit.test_catalog_scale` executes zero cases and is routed to W8.

Failed/intermediate experiments, full source snapshots and their restore script
are retained separately on `archive/gpt/chat-selector-methods-negative-2026-10-04`
(commit `785dd52c9949eb78ee021da35113af71f6378fe9`). The earlier stage is preserved
on `archive/gpt/chat-selector-pre-methods-2026-10-04`. Negative bundles are not in
this selected directory. No timing speedup or live model quality is inferred.
