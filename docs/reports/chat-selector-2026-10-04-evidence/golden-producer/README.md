# Actual W3 producer — shared W3/W4 golden

`summary.json`, `manifest.json`, `link.log`, `tests.log` and `golden-first.log`
preserve the final sources, commands and results. Full private W3 suite:
**121/121 cases, 1604/1604 assertions, zero skipped, 397.54 seconds**.
The isolated golden invocation passed **1/1 case, 97 assertions**.
Both execute production W3 through a transport which checks exact request bytes,
model, prompt and every effective parameter before returning saved response data.
Each golden makes one fake HTTP call and zero paid calls.

The common contract is W4's sole `loom/src/packet/METHOD_GRAPH.md` at
`1377e20c71f200b01416c7c87f06c3ffdafb02b6`. The shared original fixture remains
byte-identical. Caller recipe data fill its absent HTTP request; their native
version/hash and the explicitly described reply-stamp projection are captured.
The full prepared manifest and final trace are native Observations; results have
real produced-in-run, produced-by-version and compiler edges.

The exact isolated export and independent native receipt are
`../golden-consumer/input.json` and `../golden-consumer/receipt.json`.
Only the raw-response source ID in the final trace differs between the two
invocations; dependent trace/record/history hashes change, while the full contract,
exact invocation data and model results remain identical. Such distinct captures
can change packet identities;
the full suite's second export has its own hash in the manifest and compressed
input/receipt in `../golden-consumer`. This is not a byte-equality claim for the
two output packets. Native acceptance does not establish model content truth.

## Reproduce

Fetch the W2/W4 branches so these exact pins exist locally. Build W3 with
vendored SQLite and compile command export, preserving normal test thresholds:

```sh
cmake -S loom -B /tmp/w3-build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS_DEBUG=-g0 -DCMAKE_C_FLAGS_DEBUG=-g0 \
  '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=gold -Wl,--no-map-whole-files' \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DLOOM_USE_SYSTEM_SQLITE=OFF \
  -DLOOM_WERROR=ON -DLOOM_BUILD_TESTS=ON -DLOOM_BUILD_CLI=ON \
  -DLOOM_BUILD_SERVER=ON -DLOOM_SHARED=ON
cmake --build /tmp/w3-build --parallel 1
ctest --test-dir /tmp/w3-build --output-on-failure --parallel 1
python3 loom/src/chat/verify_selector.py /tmp/w3-build \
  --usage-policy-ref 5a73a360f44626333ce6510f26261c32239de73c \
  --packet-ref 1377e20c71f200b01416c7c87f06c3ffdafb02b6 \
  --method-graph-artifact /tmp/new-w3-method-graph-artifact.json --jobs 2
```

Choose a previously nonexistent artifact path. The runner refuses an existing
export, requires both real dependencies and fails if the actual test produces
no shared-schema artifact. Each overlay preserves fresh sources, commands,
hashes and logs. It does not edit or merge another lane's checkout files.
The final recorded run compiled all nine actual overlay components afresh and
linked all nine current W3 test sources against the recorded production core;
`manifest.json` retains the exact staged commands. The portable runner performs
that same complete sequence without requiring the earlier staging directory.

Then run the native consumer reproduction described in
`../golden-consumer/README.md`, using this newly exported artifact.
The CMake core was built/tested on `7282437`; the final rebase to `ba6eaf6` added
only owner requirements documentation. Production/test hashes match the frozen
verification manifest after rebase; this records identity, not a new CTest run.
