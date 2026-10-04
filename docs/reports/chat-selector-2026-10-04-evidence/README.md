# W3 retained negative experiments

This directory is published only on `archive/gpt/chat-selector-methods-negative-2026-10-04`.
It must not be included in a fast-forward of the selected W3 implementation.
Inputs and provider responses are synthetic; no paid calls or credentials were used.

The three source snapshots contain complete UTF-8 files and their exact base
commit (`8293fc7f0821d1913a8fcd866082020415b66b7e`). Some snapshots were reconstructed
from the first retained source bytes and a single explicitly recorded correction;
their `reason` and failed-fixture SHA distinguish reconstruction from capture.
The manifests and original logs retain the failing commands and dependency pins.
Absolute build paths describe the original machine, not a portable requirement.

To restore each failure, check out this archive branch in a separate checkout,
then run the following with a new destination:

```sh
python3 docs/reports/chat-selector-2026-10-04-evidence/restore_negative.py \
  methods-negative-runtime /tmp/chatadhd-w3-negative-replay
cmake -S /tmp/chatadhd-w3-negative-replay/loom -B /tmp/chatadhd-w3-negative-build \
  -G Ninja -DLOOM_USE_SYSTEM_SQLITE=OFF -DLOOM_WERROR=ON \
  -DLOOM_BUILD_TESTS=ON -DLOOM_BUILD_SERVER=ON -DLOOM_BUILD_CLI=ON -DLOOM_SHARED=ON \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_DEBUG=-g0 -DCMAKE_C_FLAGS_DEBUG=-g0 \
  '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=gold -Wl,--no-map-whole-files'
cmake --build /tmp/chatadhd-w3-negative-build --parallel 1
python3 /tmp/chatadhd-w3-negative-replay/loom/src/chat/verify_selector.py \
  /tmp/chatadhd-w3-negative-build --jobs 1 \
  --usage-policy-ref 910a1d6b3764f82c78e39c5b0adca796ba1168e5 \
  --packet-ref 8e0e86bbfb2d58a15cccf9a985b8167fc7dcbb1e
```

- `methods-negative-core`: build the restored native sources; `-Werror=shadow`
  fails on `key` in `provider_vector.cpp`. Its snapshot covers the production
  build, not the subsequently added verification sources.
- `methods-negative-overlay`: native build succeeds; the verification compile
  fails on the fixture's `unwrap(optional<Conversation>)` expression.
- `methods-negative-runtime`: verification compiles, then the original chat fake
  mistakes a capture sink for SSE and aborts when decoded content is absent.
  Real `request.stream` decides the wire format; the corrected test also asserts
  that ordinary JSON still reaches the capture sink. Existing assertions remain.

The diagnostics contain a tiny optional abort backtrace shim and its output;
it leaves assertion failure fatal. Clean helper/policy and non-chat results are
retained as diagnosis, not the final all-tests gate. Rebuild the shim locally
if needed; no binary/object files are included. Selected final green proofs live
on the W3 implementation branch, separately from these failed snapshots.
