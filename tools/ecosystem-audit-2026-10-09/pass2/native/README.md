# Independent native conformance probes

This is audit code, not product code. `native_driver.cpp` is a JSON stdin/stdout transport into the checkout's real `Pack`, `Normalizer`, `OnboardingStore`, `DefaultLayers` and `KnowledgeStore`. It does not implement any of their algorithms. Each request runs in a separate process and reopens the same native SQLite file. Fixtures are public historical or synthetic data; no live service, key, provider or Runtime worker is used.

`run.py` checks the requested SHA against both the supplied checkout and the CMake build source. It compiles the transport, creates isolated temporary databases, executes acceptance and reproduction checks, then writes `receipt.json`. Do not reuse a stale build after editing product source. The report records the exact static-library hash. Git history must contain `b302df25e1a65f20c395eadc5ad5ef065d26e33d`, the authentic historical stemming fixture source.

Build the real engine (Linux host, C++20, OpenSSL development libraries). Set `AUDIT_REPO`, `AUDIT_SHA`, `AUDIT_BUILD`, and `AUDIT_OUT` to your checkout, pinned commit, separate build directory, and results directory:

```sh
cmake -S "$AUDIT_REPO/loom" -B "$AUDIT_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DLOOM_USE_SYSTEM_SQLITE=OFF \
  -DLOOM_SHARED=ON -DLOOM_BUILD_TESTS=ON -DLOOM_WERROR=ON \
  -DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-keep-memory
cmake --build "$AUDIT_BUILD" --target loom loom_tests -j 2
python3 tools/ecosystem-audit-2026-10-09/pass2/native/run.py \
  --repo "$AUDIT_REPO" --sha "$AUDIT_SHA" --build-dir "$AUDIT_BUILD" \
  --output "$AUDIT_OUT"
```

A missing CMake/Ninja is an installable dependency, not a permanent block (`python3 -m pip install --target SEPARATE_DEP_DIR cmake ninja`, then use those tools with their package directory on `PYTHONPATH`). The audited build used vendored SQLite 3.47.2 because the system header was unavailable. WERROR remained enabled. `--no-keep-memory` reduces GNU ld memory consumption without changing assertions or product logic.

The script returns **1 for a failed acceptance criterion or blocked case**. A PASS reproduction means the old bug was observed, not that the product passes. `NOT_REPRODUCED` identifies an old bug not observed in the tested variant. On baseline main, B-POLICY001 has four acceptance failures; on audited B it has four acceptance passes. Both revisions pass the declared-settings contract probe: an allowed open extension is retained; when the real pack declares a closed settings contract, unknown settings make the runtime unavailable and the actual model request rejects them. The earlier rejection-only hypothesis was invalid and is preserved in harness-review-history, not counted as a product defect. Read `docs/reports/ecosystem-audit-2026-10-09/pass2/native/test-index.json` for all test IDs and their classifications.

The first historical fixture is byte-identical to its manifest's public Git blob. Full historical pack loading is attempted through the actual current native loader. The test also constructs a **separate** explicit /2 candidate using pinned current `normalization` data and retaining historical tables. This follows the existing manual migration instruction in `loom/src/kb/NORMALIZER_RECIPE.md`; it is not an automatic product migration. Five samples compare candidate/current results; they do not certify every old algorithm input.

For an instrumented build, either build the entire `loom_core` or select the actual linked source closure with `build_sanitized_closure.py`. This audit used the second option: 37 real C++ translation units (including conservative duplicates of archive member basenames), vendored SQLite/miniz, and the audit transport. Unused engine modules and the full unit suite are outside the sanitizer claim. No unsanitized core object is reused.

To reproduce the closure, supply `--link-map "$AUDIT_MAP"` when running the ordinary probe above, then configure a separate build and compile selected real objects:

```sh
cmake -S "$AUDIT_REPO/loom" -B "$AUDIT_ASAN_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DLOOM_USE_SYSTEM_SQLITE=OFF \
  -DLOOM_SHARED=OFF -DLOOM_BUILD_TESTS=OFF -DLOOM_WERROR=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
python3 tools/ecosystem-audit-2026-10-09/pass2/native/build_sanitized_closure.py \
  --repo "$AUDIT_REPO" --sha "$AUDIT_SHA" \
  --sanitizer-build "$AUDIT_ASAN_BUILD" --regular-link-map "$AUDIT_MAP" \
  --output-build "$AUDIT_CLOSURE_BUILD" --jobs 2 --timeout 460
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:strict_string_checks=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
python3 tools/ecosystem-audit-2026-10-09/pass2/native/run.py \
  --repo "$AUDIT_REPO" --sha "$AUDIT_SHA" --build-dir "$AUDIT_CLOSURE_BUILD" \
  --output "$AUDIT_SANITIZER_OUT" --sanitizer
```

The instrumented build completed here, but LeakSanitizer cannot inspect `/proc/<pid>/task` in this environment and aborts. The original receipt remains BLOCKED. A separate, narrower ASan+UBSan run uses `detect_leaks=0` and has its own receipt; it does not pass or replace the leak-detection gate. Merely adding sanitizer flags to an otherwise uninstrumented archive does not qualify.

The native legacy-profile import probe uses `OnboardingStore.open(user, legacy_profile)` only within that API's real contract. It does not claim a complete export/import of outer layer state, exclusions, workflow definitions or browser tiers. C-result GraphPacket/native-store boundaries are independently covered by the sibling `c-review` tools.
