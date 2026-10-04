# Shared 3/4 gate — actual W3 export on W4/current main

Base main: `30ad7d37337d6641cb7714b03e9feff0e6e25d25` (accepted W2, R39–R41).
Canonical contract: `loom/src/packet/METHOD_GRAPH.md`.
Canonical artifact: **the original W3 `32e2381`**
`docs/reports/chat-selector-2026-10-04-evidence/golden-consumer/input.json`;
raw SHA-256 `8db8175c3b70c3947711ddf5daee5073b99bc5a5114ce0075e06e51932cec54e`.
The compressed ordinary-test mirror has exactly these bytes. Variant exports
are supplements, never another canonical golden.

`canonical-before` proves the original consumer accepts the actual W3 export
on the fresh library. `canonical-after` records the same native packet and
receipt after the first compatibility changes. Both receipt IDs equal W3's
`gpr_f4bb8b56d667eaa5defa539be4412a3759a521083bd4322aa7fb31b3fdadc49a`.
The **final** code is pinned by `ctest-command.json`: it checks unchanged source
hashes before the full gate and includes all four real-export fixtures plus all
semantic regressions. Earlier ad-hoc receipts describe their execution stages;
no final-source attribution is inferred from later filesystem hashes.

`variants-before-consumer.json`: actual old `1377e20` consumer on all three new
real registry exports, 0/3 accepted. `consumer-before.py.txt` preserves that
unchanged consumer (also available in archived git history).
`registry-variants-rerun` and `variants-consumer` preserve three real W3
load/resolve/prepare/bind runs and their independent native-store consumer
receipts. Both producer and consumer perform accept/restart/read/replay/retry.
Nested cases assert four repeated paths, signed weight −24, two effective
versions and unchanged unconsumed siblings.

Zero provider/transport calls are made by these supplemental variants. The
compiler projects a synthetic saved reply; it does not prove provider execution.
The original W3 golden's separately published producer proof supplies its actual
controlled fake HTTP execution. Consumer receipts always retain
`producer_execution_verified:false` and do not establish content truth.

## Reproduce

From `loom/`, with CMake/Ninja available (the recorded environment needs their
explicit absolute paths):

```sh
cmake --preset dev -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_BUILD_SERVER=ON \
  -DCMAKE_CXX_FLAGS_DEBUG=-g0 -DCMAKE_C_FLAGS_DEBUG=-g0
cmake --build --preset dev -j2
ctest --test-dir build/dev --output-on-failure -j2
```

`-g0` omits debug symbols to fit the execution workspace; WERROR, Debug behavior,
test cases, thresholds and timeouts are unchanged. Actual command files also
record the GNU linker memory option, not a product setting.

From the repository root:

```sh
python3 loom/src/packet/tests/reproduce_method_registry_variants.py \
  --evidence-dir /tmp/new-w4-registry-variant-proof

git show 32e2381:docs/reports/chat-selector-2026-10-04-evidence/golden-consumer/input.json \
  > /tmp/original-w3-golden.json
python3 loom/src/packet/tests/verify_method_graph_artifact.py \
  --library loom/build/dev/libloom.so.0.1.0 \
  --artifact /tmp/original-w3-golden.json --evidence-dir /tmp/new-w4-canonical-proof
```

The reproduction script reads the pinned W3 `03c670caa6ee3d8ac2c478186548114f8e83927f`
registry source through local `git show`, compiles it in a temporary overlay and
links the actual W4 static core. It never edits another lane's source. It uses
the recorded vendored-SQLite/OpenSSL build; missing build capabilities fail
explicitly. Every evidence directory must be new. Code, library, object and
artifact hashes are retained with exact argv and logs.

Large JSON receipts are stored without loss as `.json.gz`; `compression.json`
records original logical paths, raw hashes and gzip hashes. `gzip -dc FILE`
restores the exact JSON bytes; do this before passing a compressed artifact to
the consumer CLI. The original per-run manifests retain their original raw
file hashes. The final evidence manifest verifies compressed bytes separately.

## Preserved negative executions

- Initial configure: Ninja was absent from PATH, so no source build occurred.
  `configure-initial-*` retains command/error; final configure uses the actual
  installed Ninja path.
- Initial supplemental harness: W3 captured the manifest before it added the
  instantiated request hash. Searching for the complete returned manifest in
  that earlier source failed. `variants-initial.verify.cc.txt` is the exact
  initial source, SHA-256
  `8fa1d2bae5af6e3d0ff5132d93f51e1d84a5f0bf1b9f185d2a7bfdce864db5b5`.
  Use the reproduction script's `--harness` option with that snapshot and a new
  output directory to repeat the failure. The corrected harness adds a new
  immutable exact capture, matching W3's canonical producer; it preserves the
  earlier capture.
- Initial ordinary test run: 29 cases, four helper errors due to treating
  `canonical()` UTF-8 bytes as a string. `store-regressions.txt` and
  `store-helper-initial.py.txt` preserve the error and source. In a disposable
  checkout, restore that snapshot to `loom/tests/compat/test_graph_packet_store.py`
  and run it with `LOOM_LIBRARY` pointing to the real built shared library and
  repository-root `PYTHONPATH`. The corrected focused run checks 6/6 negative
  scenarios; its command explicitly records that it loaded the helper before
  subsequent batching. The final full gate exercises the batched helper, with
  identical assertions and no changed timeout.

Final gate: **110/110 PASS**, 437.69 s. GraphPacketStore: **29/29**, 201.564 s (CTest entry201.73 s). `resolve_lineage`: **2/2** after scoped historical-blob hydration; its 257.02 s includes partial-clone fetch latency. No test was skipped or changed for that hydration. The inherited `unit.test_catalog_scale` entry still selects zero cases and is reported to W8. Source and library hashes from `ctest-command.json` were verified unchanged after completion.
