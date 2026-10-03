# Local native verification — 2026-09-28

The recovered local environment built all **189/189 targets**, including the
shared library, native tools, CLI and HTTP server. Full CTest then returned
**70/72 passing entries** in **61.97 seconds** (exit 8). One failure was a missing
file in the partial local checkout. After restoring its exact remote contents,
the focused `eval.harness` rerun passed. The reconciled result is **71/72**, with
the existing catalog recall gate still failing. This is a full run plus one
targeted rerun, not a claim that a second complete suite ran.

## What ran

| Check | Actual result |
|---|---|
| Debug build, shared library, CLI, server and test tools | 189/189 build targets completed |
| Initial full CTest | 70 passed, 2 failed, 72 entries; 61.97 s |
| Python/C++ compatibility | All 13 entries passed, including ABI symbols, database, import, semantic recovery, native candidate validation and UTF-8/JSON utility parity |
| HTTP server smoke | Executed successfully, including knowledge/catalog/context and authentication checks |
| CLI smoke | Executed successfully |
| Research suites | 369 structure + 10 independent + 12 graph-native + 15 candidate-protocol tests passed: 406 total |
| Restored evaluation harness | Focused rerun passed; 19 Python tests; 0.88 s |

CTest ran with CMake 3.28.3, Ninja 1.11.1, GCC 13.3, Python 3.12.14, OpenSSL
3.0.13 and vendored SQLite 3.47.2. Tests used three parallel workers. No native
source, test assertion, test threshold or CMake configuration was changed to
produce these results. The structure count includes the 12 credential-handoff
checks and seven local anchor-assistance checks added since the earlier 350-test
structure result. These are offline mechanism tests, not new model evaluations.

## Remaining failure

`unit.test_catalog_eval` selected **13/45** relevant synthetic conversations:
recall **28.89%**, below the unchanged **55%** gate. Conversation precision was
**100%**; it selected **0/5** lexical noise traps and **0/15** generic-noise
conversations. It also selected three auxiliary provider documents, reported
separately from labeled conversation precision. This matches the previously
reported failure: the current selector misses 32 relevant conversations while
its noise rejection passes. It is a measured product limitation, not a toolchain
failure, and no threshold was weakened.

These measurements come from `synthetic_dev`, not the private holdout or a real
archive. They do not establish broad structural understanding or reliable topic
recovery in natural conversations.

## Checkout gap repaired

The initial `eval.harness` failure was Python's inability to open
`loom/tools/eval/test_eval.py`. The harness and its four imported modules were
absent locally. All five were restored from pinned remote commit
`30bea23e2aeff2ffa22fe7899b4ce220741dab0f`, with exact Git blob SHA and byte-length
verification before running the targeted test:

- `loom/tools/eval/test_eval.py`
- `loom/tools/eval/knowledge_eval.py`
- `loom/tools/eval/kbeval/common.py`
- `loom/tools/eval/kbeval/realrun.py`
- `loom/tools/eval/kbeval/synthetic.py`

The restored harness uses temporary repositories and fabricated data. Its
holdout-safety tests do not read the private holdout. There was no implementation
change in this repair and no reason to rerun the other 71 CTest entries.

## Coverage limits

CTest entry counts are not the same as fully exercised scenarios:

- `unit.test_catalog_scale` ran **zero cases**: the roughly 1 GB scale test is
  opt-in and was not enabled.
- `unit.test_resolve_lineage` reported two passing cases but both returned early
  because the original Git history is unavailable in the synthetic checkout.
  Real historical lineage was therefore **not verified** here.
- Release, sanitizer, JNI/Android and browser end-to-end configurations were not
  built or tested in this run.

No GitHub Actions, provider requests or paid model tests were started by this
verification. No credentials or private holdout data were read.

## Reproduction and evidence

The workspace helper ran the full suite as:

```sh
python /workspace/scratch/edcd10c4764c/run-native-local.py test
```

Its CTest invocation was `ctest --test-dir <native-build> --output-on-failure
--parallel 3 --timeout 180`, with the extracted toolchain's runtime library path.
After restoring the five files, only `-R '^eval.harness$'` was rerun with the
same CTest and environment.

The machine-readable summary is
`docs/research/inputs/local-native-test-summary.json`. It includes all 72 initial
entry results, restored-file hashes, observed Python test counts, log hashes,
and the targeted rerun as separate evidence. Scratch logs remain at
`/workspace/scratch/edcd10c4764c/native-local-test.log`,
`native-local-full-LastTest.log` and `native-local-eval-harness-retest.log`.
The full detailed log was preserved before CTest replaced its temporary log
during the focused rerun.

The original logs and exact session helper are also retained in
`inputs/local-native-verification-2026-09-28.zip` (SHA-256
`28dd7940382562816f2e0b763fc659a34363cec94a745de6cd8f6b46c189b5cf`).
Extracted dependency package names, versions and hashes are recorded in
`inputs/local-toolchain-packages.json`; package binaries are not vendored.
