# Repository hygiene verification — 2026-10-04

This receipt separates actual inner case execution from CTest's outer entry
status. It does not measure extraction quality or paid-model behavior.
Source base is `161cc22dfb84fe863389d6b90323bd44516a68dc`. Native and web
sources remain unchanged; the hygiene branch changes CI, documentation and
seeding fixture documentation only.

## Corrected CI

Run [37211656572](https://github.com/klb-t/chatadhd/actions/runs/37211656572)
tests branch code head `6523c1b1a3870f2a6c7bba66554ad082cd9ea832`.
The dev build receipt pins actual merge commit `93420bbb82124ba5493e5fc9c3dea793703bfb74`
and tree `237d9618ca61780a4011896a03719e8b4184daf9`, equal to the tested branch
tree. Later branch commits only add evidence and reports.

| Check | Result |
|---|---|
| dev CTest | 108/108 outer entries passed in 100.83 s; 107 executed entries, opt-in scale unexecuted |
| dev native cases / assertions | 659 / 24,465 |
| dev Python cases / skips | 1,276 / 0 |
| dev evidence gate | Passed on complete original JUnit; no validation errors |
| dev JNI against actual libloom | 1/1 passed |
| dev web build and full browser suites | Passed; production build 85 modules |
| ASan CTest and evidence gate | 108/108 passed in 219.25 s; 105 executed entries, three explicitly unexecuted |
| ASan native cases / assertions | 659 / 24,464 |
| ASan Python cases / skips | 1,252 / 24 known unavailable FFI cases |
| vendored Clang | Build failed; CTest not executed |

The remaining Clang errors are the unused `this` captures in
`loom/server/src/app.cpp:768,772`. They belong to thread 10's server scope.
No warning suppression, assertion change or test removal was made. Overall
CI therefore remains blocked; green native dev coverage does not make the
whole matrix green. Thread 9 owns integration to main after that correction.

The one-assertion difference between dev and ASan is the existing sanitizer
branch of `unit.test_import_stream`: 13 versus 12 assertions. The source and
its preset-specific RSS checks were not changed. ASan's three unexecuted
entries are the scale opt-in and two entirely unavailable FFI suites; its two
unavailable ABI cases are also subtracted from Python totals.

Complete dev web checks include e2e **16/16**, workspace **10/10**, profile
runtime **19/19**, adapters **9/9**, profile browser **15/15**, imported
content state **8/8**, browser **1/1**, native **4/4**, and profile graph
**11/11**. Transport, context-plan and chat-context checks also passed. These
are the script's reported test/group units, not an additional CTest total.
Local fake providers exercised chat; no paid model was called.

`ci-corrected/{dev,asan}/verification/` preserves original JUnit, discovery
manifest, valid execution JSON and native binary hashes. Complete successful
job logs and terminal metadata are alongside those directories. The repeated
Clang failure and its artifact are archived under
`docs/archive/repo-hygiene-2026-10-04/ci-corrected-vendored/`.

### Previously empty native entries

| Entry | 2026-10-03 cases / assertions | Corrected dev cases / assertions |
|---|---:|---:|
| `unit.test_context_engine` | 0 / 0 | 18 / 1,364 |
| `unit.test_knowledge` | 0 / 0 | 18 / 150 |
| `unit.test_resolve_lineage` | 2 / 0 | 2 / 15 |

The original source files and assertions are unchanged. The original historical
XML/binary receipt is also unchanged; its coverage interpretation was corrected
in its RESULTS document. The cause of the two historical missing registrations
is not proven. Complete-history checkout removes the known lineage early-return
condition. `unit.test_catalog_scale` remains a separately identified unexecuted
opt-in, not a completed 1 GB scenario.

### Reproduction

Run from the repository root on a toolchain with the documented dependencies:

```bash
python3 -m pip install requests cryptography -r loom/tools/contracts/requirements.txt
cmake --preset dev -S loom -DLOOM_BUILD_SERVER=ON
cmake --build loom/build/dev --parallel 2
mkdir -p loom/build/dev/verification
ctest --test-dir loom/build/dev --show-only=json-v1 > loom/build/dev/verification/ctest-manifest.json
env -u PYTHONPATH -u TMPDIR ctest --test-dir loom/build/dev \
  --output-on-failure --no-tests=error \
  --test-output-size-passed 10485760 --test-output-size-failed 10485760 \
  --output-junit "$PWD/loom/build/dev/verification/ctest.xml"
python3 .github/scripts/verify_ctest.py --preset dev \
  --manifest loom/build/dev/verification/ctest-manifest.json \
  --junit loom/build/dev/verification/ctest.xml \
  --output loom/build/dev/verification/executed-cases.json
```

Full CI also runs the current package.json web commands, browser suites and
the separate native JNI CTest. Its exact commands are in the retained job logs
and `.github/workflows/loom.yml`. The existing non-shared ASan preset reports
known unavailable FFI cases separately; dev and vendored shared builds require
those cases to execute. Discovery-only counts do not establish execution.

## Local checks and retained first failures

- Evidence-gate regressions: **16/16 passed**, including false zero-case success,
  zero assertions, truncation, missing/duplicate entries and unexpected skips.
- Real preset-path probe: **1/1 passed**, JUnit present at the absolute guard
  path and guard accepted it. This checks instrumentation, not native coverage.
- Seeding: **36/36 passed**; 26 result files / **13,800,361 bytes** retained.
  `research.seeding` consumes them, so archiving them would break real tests.
- Local web: strict TypeScript / Vite build passed (**85 modules**), as did
  transport, workspace-state, context-plan, application-profiles and imported
  message-content state tests. [Local checks](local-checks.json) pin their logs.
- Local native configure/build did not complete: system SQLite headers were
  unavailable initially; bundled-SQLite retries encountered disk exhaustion
  and an empty object. No local native CTest run is claimed.

[The first complete CI/local failures](../../archive/repo-hygiene-2026-10-04/README.md)
are archived with commands, source/job identities and hashes. First CI dev and
ASan each passed 108 outer entries, but their guards rejected a misplaced
relative-path JUnit file. The first dev inner counts are labelled as derived
from complete LastTest.log, not as an original JUnit receipt. The corrected
run supplies the separately measured original JUnit and executable hashes.

No sealed holdout key, blind corpus, private export or credential was read or
published. No paid model call was made. [Source hashes](source-sha256.json),
[local environment](local-environment.json), [seeding audit](seeding-audit.json)
and [historical audit](zero-discovery-audit.md) retain the supporting boundaries.
