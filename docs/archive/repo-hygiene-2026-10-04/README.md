# Repository hygiene: retained unsuccessful verification attempts

These are execution evidence, not accepted product experiments. No paid model
call was made. Complete first failures are preserved rather than reported as
successful coverage. [The current report](../../reports/repo-hygiene-2026-10-04.md)
and [fresh verification](../../verification/repo-hygiene-2026-10-04/RESULTS.md)
describe the separately measured result after correction.

## First GitHub Actions run

Run [37210513736](https://github.com/klb-t/chatadhd/actions/runs/37210513736)
tested branch head `ee97311d6545ca19e19266d2f37514f2663f0aa9`, merge commit
`295de864dc42507ce4f54a0bbd7adf42fd1bc9fa`, based on `161cc22`.
`ci-first/*-job.log` retains each complete job log. Matching JSON records the
hash, source, public URL, commands and any redactions (none were needed).
Those job JSON files are first-capture metadata and can still mark artifact
inspection as pending; later downloaded files and the case inventory retain
the subsequent inspection without rewriting that first capture.
`ci-first/run-terminal.json` records all job/artifact identities and ZIP hashes.
Artifact directories retain every downloaded file, including complete
`LastTest.log` and discovery manifest. Original capture paths in diagnostics
refer to the runner or the pre-archive location; no private source was used.

| Preset | Native execution | Primary failure |
|---|---|---|
| dev | 108/108 outer entries; 106.91 s | Relative preset JUnit path; guard correctly rejects missing input |
| asan | 108/108 outer entries; 296.19 s | Same JUnit path problem |
| vendored / Clang | Not executed | Unused lambda captures at `loom/server/src/app.cpp:768,772`, with `-Werror` |

The dev case inventory is derived from complete `LastTest.log`, not from an
original JUnit receipt. It does not claim the original guard passed. Binary
hashes were not recorded by that workflow; this cannot be repaired retrospectively.
The successor workflow pins them before CTest and uses an absolute JUnit path.

To reproduce the first failure on a suitable Linux toolchain, check out the
recorded head in an isolated clone, follow its `.github/workflows/loom.yml`,
and retain `Testing/Temporary/LastTest.log` even if the guard fails. For the
Clang error, the original commands are:

```bash
CC=clang CXX=clang++ cmake --preset vendored -S loom \
  -DLOOM_BUILD_SERVER=ON -DLOOM_SHARED=ON
cmake --build loom/build/vendored --parallel 2
```

No warning suppression, test deletion or relaxed assertion is required to
reproduce the failure. The source fix belongs to the server/UI owner.

## Incomplete local build

`local-build/` preserves the initial configure failure, two disk-full build
failures and subsequent invalid-object linker failures. The environment and
source hashes are retained with the current verification. No local native
CTest run occurred. Configure used bundled SQLite after system headers were
found unavailable:

```bash
cmake --preset dev -S loom -DLOOM_USE_SYSTEM_SQLITE=OFF \
  -DLOOM_BUILD_SERVER=ON -DCMAKE_MAKE_PROGRAM=/root/.local/bin/ninja \
  -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g0' -DCMAKE_C_FLAGS_DEBUG='-O0 -g0'
cmake --build loom/build/dev --parallel 2
```

The absolute Ninja path describes the original environment; substitute the
installed Ninja path when reproducing. A resumed attempt used a task-owned
`TMPDIR`, then parallelism 1. Those attempts still produced an invalid empty
`fts.cpp.o`. The incomplete build and inactive compiler temporaries were
removed after preserving logs; no other thread's files were removed. Disk
exhaustion is an environment failure, not a test result or demonstrated source bug.

The earlier derived `ci-first/dev/last-test-counts-before-archive-path-update.json`
retains its original verification-directory path strings. Its observations are
identical to `last-test-counts.json`; only the input paths were updated when
archiving. It is not an additional run or an original JUnit receipt.
