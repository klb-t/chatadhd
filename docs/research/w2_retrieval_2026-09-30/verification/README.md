# W2 native verification — 2026-09-30

Implementation: `9a6f54c254b3996946bc87067b467d6c927b8361`, tree
`cd89eaebd4bb71c4b6a08a8a5d43af7e7c7ad685`. The initially tested local commit
`3bd4b9e` has the exact same tree; publication changed commit metadata only.
`source-sha256.json` binds the changed context sources/tests and the unchanged
native C ABI/chat consumers. This is an integration verification, **not** a
pristine pre-change baseline measurement: implementation landed during the first
build.

## Results

- Full Debug build, warnings treated as errors, shared C ABI, CLI and server:
  **passed** with vendored SQLite.
- Corrected targeted gate: **20/20 CTest entries passed**, zero skipped,
  **25.20 seconds**. These entries are suites, not individual assertions.
- New W2 suites contain **53 doctest cases**: candidate integration 5,
  counters 4, diagnostics 9, native C ABI consumer 6, plan 15, retrieval
  instruments 13, fixed synthetic DEV measurement 1. They are subsets of the
  targeted/full runs, not additional independent denominators.
- Existing controls 9 and native chat context 15 cases passed. Exported-symbol,
  exact-signature and ctypes smoke ABI checks: **3/3**.
- Full corrected gate: **84/84 CTest entries passed**, zero failures,
  disabled entries or skipped entries; **133 seconds** as recorded in JUnit.
  The process returned exit 0. `research.structure` ran **821/821** Python
  tests as part of that gate; this is not added to overlapping earlier runs.

The actual `loom_context_build` JSON consumer is exercised by six synthetic
C ABI tests, including per-thesis scope/detail, explicit claims, recorded
counter-evidence, missing support, TF-IDF candidates, unavailable capabilities
and malformed input. No new C API symbol or schema migration was introduced.
The existing chat `knowledge_context` parser has a field whitelist and still
rejects the new W2 fields; existing scope/detail chat integration passes, but
new plan/channel chat integration is **not** claimed here.

## First results retained, corrections explained

1. `targeted-first.log/xml`: **18/19 entries passed**. The inherited
   missing-premise test did not produce an incomplete case after duplicate
   material stopped consuming budget. Its local premise fixture was made
   sufficiently large and excluded from direct selection; all existing
   complete/incomplete assertions were retained. The rerun passes.
2. `dev-first.log`: the first DEV run had **239/241 assertions passing**.
   Graph and graph-with-shadow selected identical material and rankings, but
   two exact-JSON assertions exposed an extra `direct_goal_candidate` metadata
   field only when shadow was enabled. The implementation now records that
   field consistently. No DEV fixture, gold, threshold or assertion was changed.
   The corrected run passes. All **24 case/mode** candidate metrics, selection
   metrics and ranked IDs/scores are exactly equal between first and corrected
   output. Raw first output is retained, including its failures.
3. `full.log/xml`: **79/84 entries passed**. Four compatibility entries failed
   because their CTest environment replaces `PYTHONPATH`, hiding cached
   `requests`; the research suite could not import the repository `loom`
   namespace. The corrected environment adds a temporary Python user site
   pointing to cached dependencies and includes the repository on `PYTHONPATH`.
   Source and binaries were not changed for this correction.

DEV rows/summaries are extracted without modification into
`dev-first-data.json` and `targeted-corrected-data.json`; raw logs remain the
primary evidence. There are six fixed synthetic cases, with eight gold
case–claim opportunities across the five nonempty-gold cases. Candidate
recovery is **2/8 graph**, **5/8 TF-IDF**, **7/8 union**; post-budget recovery
is **1/8**, **4/8**, **5/8** respectively. This is a small DEV mechanism
diagnostic, not real-export quality, calibrated semantic understanding or
answer correctness. Answer correctness was not measured. The plan/channel
mechanism does not establish full R28 realization.

## Build environment and observed artifact failures

- GCC/G++ 13.3.0; CMake 4.4.3; Ninja 1.13.2; Python 3.12.14.
- `Debug`, `LOOM_WERROR=ON`, `LOOM_SHARED=ON`,
  `LOOM_USE_SYSTEM_SQLITE=OFF`, `LOOM_BUILD_CLI=ON`,
  `LOOM_BUILD_SERVER=ON`. Vendored SQLite 3.47.2; OpenSSL enabled.
- Tool/package cache: `/workspace/scratch/a371a1ca13b1/verification/deps`.
  The first pip launcher invocation lacked this cache on `PYTHONPATH` and
  raised `ModuleNotFoundError: cmake`; the corrected configure passed.
- Logical build path: `/workspace/scratch/6a857f5e4336/build-w2`.
  Physical build path after the workaround:
  `/var/tmp/chatadhd-w2-build-6a857f5e4336`; the logical path is a symlink.
- The first short build ended with exit 1 without a compiler diagnostic.
  Subsequent links found zero-byte objects after Ninja had reported successful
  compilation: first `batch_api.cpp.o` and `infer.cpp.o`, then
  `graph_memory.cpp.o` and `export_openai.cpp.o`. No OOM kill or disk exhaustion
  was observed. The immediate cause was invalid derived objects; synchronized
  workspace interference was suspected, not proven. Only those empty objects
  were deleted. Moving physical build output outside the synchronized
  workspace, retaining its logical path, reconfiguring and rebuilding resolved
  the issue. No further empty objects were observed; shared linking and all
  native targets then passed.
- `build.log`, `build-retry.log`, `build-final.log`, `build-incremental.log`
  and `build-corrections.log` retain available build-stage output. The earlier
  direct-output linker failure on `batch_api.cpp.o` was observed in the tool
  transcript; its entire raw build stream was not separately saved.
- `TMPDIR=/var/tmp` keeps credential-handoff test temporary directories outside
  the enclosing workspace Git tree. The corrected full run also uses
  `PYTHONUSERBASE=/var/tmp/chatadhd-w2-python-6a857f5e4336`; its
  `lib/python3.12/site-packages` is a symlink to the existing dependency cache.

Commands are recorded in `commands.txt`. No paid provider call, private source
archive, sealed catalog/graph validation or real holdout key was used. All new
cases use explicit synthetic fixtures and local/scripted capabilities.

## Receipt completeness

`full-corrected.xml` is the complete raw 84-entry JUnit result and is the
authoritative final gate receipt. The final redirected progress log
`full-corrected.log` ends at 65/84 despite process success and a complete XML;
workspace synchronization interference is suspected. It is retained as-is,
without manufacturing its missing tail. JUnit limits individual captured
outputs to its configured threshold; this does not truncate its test inventory
or failure/skip counts. The temporary verbose CTest log was inspected before
a later `ctest -N` inventory check replaced it. No passed gate was rerun merely
to regenerate prettier logs.

The exact recorded targeted regex was checked with `ctest -N`: **20 entries**.
After the full gate, all **24** recorded source hashes still matched. All 24
DEV row-level metrics and rankings were compared across first/corrected runs
and are identical. `checks.json` records these receipt checks.
