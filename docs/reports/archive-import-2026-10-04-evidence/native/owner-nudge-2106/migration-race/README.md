# Forward-only message extension migration: deterministic race proof

The old implementation failed **6 of 24 assertions in 1 case** (18 passed,
10 cases skipped). For each of the annotation and import-checkpoint version
keys, a second connection committed version 2 after preflight but before
`BEGIN IMMEDIATE`: the old opener succeeded, downgraded 2 to 1, and ran extension
DDL. The fixed `db.annotations` suite passed **11/11 cases and 189/189
assertions**. All four compiles and both links exited 0 with C++20, `-O0`,
`-DNDEBUG`, and `-Werror`; the red test's exit 1 was expected.

This was a focused derivative: newly compiled test/main and before/after
annotation objects were linked ahead of a **historical `libloom_core.a`**. The
archive's annotation object was not pulled, and its recorded SHA-256 was
unchanged. This is **not a fresh build or full CTest result for the rebased
main + W5 tree**. Fresh full verification is outside this measurement.

`run.py`, `receipt.json`, the before source, and every compile/link/test log
preserve the executed bytes. The before source is exactly
`loom/src/db/annotations.cpp` at public commit
`a343a8c0694a70317a1fe54713b24e96cccf4a2c`; the fix and regression were later
committed as `f18af911`. The receipt hashes identify the patched working-tree
inputs used in this measurement. Fixtures contain synthetic data only; there
are no compiled binaries, object files, databases, or real archive exports.

The executed runner intentionally remains unchanged. It has absolute workspace
paths and extracts its before source from `HEAD`; running it under a later HEAD
would choose a different before source and would not reproduce this red
measurement. The pinned before source is included so that historical input
remains available. To verify the committed regression with a fresh native build
from a checkout containing `f18af911`:

```sh
cd loom
cmake --preset vendored
cmake --build --preset vendored --parallel 2
ctest --preset vendored -R '^unit\.test_db_annotations$'
```

`manifest.json` records each original and stored SHA-256 and byte length.
`test-before.log.gz` and `test-after.log.gz` are lossless gzip copies (fixed
mtime, no filename header); decompress before comparing their original hashes.
The green log contains a deliberately invalid UTF-8 fixture byte, preserved
without decoding or rewriting. Empty compile logs are intentional: successful
compilers emitted no diagnostics, while their exit codes are in the receipt.
