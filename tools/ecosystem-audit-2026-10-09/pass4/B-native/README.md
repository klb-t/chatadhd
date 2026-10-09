# Independent B4 native acceptance

Audit-only code. No product reimplementation; all parsing/resolution/persistence is native Loom. `run.py` accepts exact checkout/SHA and a proven native object manifest. It retains the native driver in output for the seven-byte relocation probe.

```sh
python tools/ecosystem-audit-2026-10-09/pass4/B-native/build.py --repo "$CHECKOUT" --sha "$SHA" --base-build "$BASE_BUILD" --prior-manifest "$B2_OBJECT_MANIFEST" --out "$BUILD_OUTPUT"
python tools/ecosystem-audit-2026-10-09/pass4/B-native/run.py --repo "$CHECKOUT" --sha "$SHA" --manifest "$BUILD_OUTPUT/object-manifest.json" --out "$OUTPUT"
python tools/ecosystem-audit-2026-10-09/pass4/B-native/relocation.py --repo "$CHECKOUT" --sha "$SHA" --driver "$OUTPUT/driver" --out "$OUTPUT/relocation.json"
```

`run.py` exits nonzero for any failed criterion, including known outstanding product acceptance. It does not xfail or weaken that acceptance. Read `kind` separately: reproduction PASS proves a bug; repair/contract/integration PASS prove only their named scope. Two unimplemented public contracts are BLOCKED.

The selective build is a documented space-saving composition, not a portable binary distribution or full build. It needs a verified prior B base build and B2 manifest from pass2. It refuses uncovered source/header/embedding changes rather than testing stale code. A wider future B fix requires a current full build or separately reviewed new manifest; the real-consumer driver and acceptance oracle are reusable unchanged. Compilation uses C++20 and local vendored dependencies, no paid transport. ScriptedTransport rejects every HTTP request; workers are disabled.

Fixture conversations are synthetic mechanical evidence. Message/children arrays retain order. Object key/whitespace equivalence concerns domain tuples, not byte hashes or every JSON numeric representation. Cold/warm idempotence does not prove configurable cache policy; pause/resume before running is not an in-flight crash test. No browser assertion is made.

A fresh full build can replace the historical selective composition. Configure it with CMake against the requested checkout (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`), then execute and attest its real build:

```sh
python tools/ecosystem-audit-2026-10-09/pass4/B-native/record_full_build.py --repo "$CHECKOUT" --sha "$SHA" --build-dir "$FRESH_BUILD" --out "$BUILD_RECEIPT"
python tools/ecosystem-audit-2026-10-09/pass4/B-native/run.py --repo "$CHECKOUT" --sha "$SHA" --build-dir "$FRESH_BUILD" --build-receipt "$BUILD_RECEIPT" --out "$OUTPUT"
```

The runner verifies current Git HEAD/clean product, CMake source path, compile-command hash, every recorded source hash and core archive hash. A bare prebuilt library path is explicitly BLOCKED. `record_full_build.py` records build intent first, runs CMake `loom_core`, checks source stability afterwards, and records exit status. Supports vendored SQLite archive or configured system `-lsqlite3`; miniz is the actual build archive. This alternate full-build route was syntax/CLI checked in PASS4 but **not** executed as another expensive native build; the published product receipts use the documented selective build.

`relocation.py --phase both` (default) exits 1 when acceptance fails even if reproduction succeeds. `--phase reproduction` and `--phase acceptance` deliberately gate those separate meanings. PASS of reproduction is never a product acceptance receipt. The CLI exit-code hardening was re-executed on B4: reproduction PASS, acceptance FAIL, exit 1.
