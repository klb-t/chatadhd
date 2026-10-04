# Native verification — measured gates and reproduction

Final source: **1474b260582a80f006f99953baaad1b3253ca6e1**, rebased onto
**7282437b1c88933977f64b3468b9f42f7b400494** (main's new documentation only).
The isolated checkout has that exact base and **27 changed Loom paths**, byte
identical to the published implementation. It is an implementation build,
not an unmodified baseline. All inputs are synthetic/public; zero paid calls.

**Full CTest: 112/112 passed in 316.20 s; 696 native cases / 25,341 assertions,
1,276 Python unittest cases, 0 skips**. [receipt-final.json](receipt-final.json) binds actual binaries,
CMake flags/cache, registration, logs, source bytes before/after and commands.
The test groups/thresholds/timeouts are unchanged. The final full run is serial
and verbose, with inherited PYTHONPATH/TMPDIR removed, and logs actual case
counts. The pre-existing opt-in `unit.test_catalog_scale` reports **0/0** unless
explicitly enabled; that optional scale corpus was not exercised. It is disclosed
separately from CTest's registered-entry result and the four nonempty new groups.

| Final focused source | Cases | Assertions |
|---|---:|---:|
| test_db_annotations.cpp | 10/10 | 147/147 |
| test_import_resume.cpp | 23/23 | 696/696 |
| test_import_audit.cpp | 3/3 | 28/28 |
| test_import_usage.cpp | 1/1 | 5/5 |
| Total | **37/37** | **876/876** |

These are actual executed doctest cases, not CTest registration counts.
The absent-W2 capability case does not demonstrate a working growth guard.
Additional usage cases become available after integrating real W2; the runner
records its observed denominator rather than imposing a fixed maximum.

## Preserved failures and checkpoint replay

The first full run executed **112 entries: 110 passed, 2 failed**, 359.99 s.
[ctest-first-failed.txt](ctest-first-failed.txt) retains failures in exports/source
materialization, corrected in implementation without altering existing tests.
The targeted three-group retry passed. A subsequent full attempt was stopped
after research-contract timeout under shared-host load; its unchanged log and
[interruption receipt](ctest-overloaded-stopped-receipt.json) remain. It is not a
completed full gate or an aggregate of retries. Intermediate focused proofs
(16,20,23 cases) retain their own measured source/version boundaries.

[Exact integrator checkpoint probes](probes/README.md) preserve original and
adapted sources, synthetic ZIPs, full outputs and hashes. Final **2/2 scenarios,
31/31 checks** use this implementation's actual stable core archive. Valid
transient failure recovers one project, one document and their link while keeping
source/conversation IDs; the third attempt caches. Malformed member stays partial
through all three attempts. The only adaptation is explicit `resume=true`, because
the library retains its historical opt-in default. Phase1 proof and original
integrator negative archive remain separate. Four CLI completion cases also pass:
complete0/partial4, each with and without audit.

## Reproduce on a fresh clone

Linux/macOS, C++20 GCC/Clang, CMake >=3.24, Python 3.10+ and normal build dependencies.
Choose fresh empty external build/evidence directories from the repository root:

```bash
python3 docs/reports/archive-import-2026-10-04-evidence/native/verify_native.py \
  --build-dir /tmp/loom-thread5-build --out-dir /tmp/loom-thread5-evidence --jobs 1
```

The public runner enables tests/CLI/server/shared, vendored SQLite and WERROR,
`Release` with **-O0 -DNDEBUG** C/C++ flags; these are preparation/verification
binaries, not optimized production throughput. Jobs is a positive caller preset.
CMake/CTest are discovered on PATH, with explicit path overrides available; Ninja
is chosen if present. The script was syntax/control-checked; this retained full
measurement used the already prepared build and recorded commands, rather than
claiming a fresh execution of the entire build script.

The runner records full registration/execution, logs, actual binaries, source
manifests and four focused filters; zero focused cases fail verification.
Fresh directories preserve earlier failures. Run `probes/verify_checkpoint_replay.py`
against the resulting core using its README recipe. Source manifests distinguish
HEAD from actual bytes; the expanded snapshot taken during this measured gate
and after-gate snapshot bind all 1,186 implementation/contract files, while the
27-path before-gate snapshot pins the changes against main. Source must remain
unchanged for a passing stable-source receipt.

## Baseline, W2 and performance boundaries

An independent W2 public proof reports **108/108** full CTest on exact base
`161cc22`, published at
[34cc920](https://github.com/klb-t/chatadhd/blob/34cc920dd3cdb0c0fca0a514569b19111429583f/loom/src/policy/tests/evidence/2026-10-04/manifest.json).
W5 does not claim its own full baseline rerun; it built the baseline CLI for
comparison. Main's later integrator commits changed documentation only.

[Historical actual W2×W5 proof](../w2-integration/README.md):3/3 cases, 47/47
assertions, zero provider calls, pinned real W2 `34cc920` and W5 source bytes.
It is not a merged full gate, latest W2 preset certification, or server/UI adapter.

[Matched 64 MiB import measurements](../import/README.md) preserve both variants:
wrapper RSS 155,844→25,556 KiB (−83.6%); array 24,328→25,920 KiB (**+6.5%**, negative
retained). All64 conversations/128messages/rawpayloads/sourcehash/integrity pass.
Single shared-host timings and completed-source cache hits are distinct from
crash-recovery tests, and these older binaries do not cover later corrections.

[Executed 2.15 GB recovery](../import/large-resume/) completes 2,047 conversations /
4,094 messages after preserved ENOSPC, keeping earlier IDs and verifying source,
text/rawrecords/links/checkpoints. CLI RSS 19,272 KiB, 168.599 s, binary `9a8b88b`.
Whitespace padding models scanner traversal; a 2.15 GB temporary tmpfs copy lies
outside CLI RSS. No private large export or realistic 2 GB stored-message corpus
was evaluated. Full negative/positive stdout and the exact executed fidelity
verifier are retained with compressed/original hashes.


## Owner-nudge acceptance repeat

After the owner requested a fresh replay/rebase/gates, main remained 7282437
and rebase confirmed up-to-date. [New full receipt](owner-nudge-1851/receipt.json):
**112/112 CTest in 307.19 s**, 696 native cases/25,341 assertions, 1,276 Python / 0 skips;
the four new groups execute 37/37 cases and 876/876 assertions. The opt-in
catalog_scale remains explicitly 0, as in the earlier run. Full build reports
no work; actual sources and binaries remain byte-identical to the compiled
1474b260. No test/threshold/timeout edits. [Web build](owner-nudge-1851/web-receipt.json)
passes TypeScript+Vite, 85 modules, using an offline exact-lockfile install.
[Independent replay repeat](probes/owner-nudge-1851/receipt.json) passes 2/2
scenarios and 31/31 controls against the stable actual core; no importer overlay.
Earlier positive/negative receipts remain unchanged.
