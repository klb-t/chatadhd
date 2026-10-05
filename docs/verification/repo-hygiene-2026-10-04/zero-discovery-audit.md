# Audit of empty native verification entries — 2026-10-04

The 2026-10-03 CTest receipt counted two unintended empty native suites as
passing: `unit.test_context_engine` and `unit.test_knowledge`. The retained
JUnit output is authoritative: each contains **0 test cases and 0 assertions**
while doctest prints `Status: SUCCESS!` and CTest records `status="run"`.
The source currently contains 18 `TEST_CASE` declarations in each file.
Those declarations are a static inventory, not evidence of execution.

| Entry | Historical executed cases | Historical assertions | Historical time |
|---|---:|---:|---:|
| `unit.test_context_engine` | 0 | 0 | 0.00601098 s |
| `unit.test_knowledge` | 0 | 0 | 0.00659632 s |
| `unit.test_catalog_scale` | 0 | 0 | 0.00787436 s |

The third empty entry is a separately documented opt-in scale test, skipped
unless `LOOM_RUN_SLOW_TESTS` is present. It remains an unexecuted scale scenario;
it must not become successful memory/throughput evidence. The two unintended
empty entries have no source-level skip annotation. The cause of their missing
historical discovery is not established by the saved receipt. Historical notes
mention corrupted generated objects; that is a candidate explanation, not a
demonstrated diagnosis for these two entries.

## Sources and reproducibility

Historical evidence:

- [`n3-ctest.xml`](../current-2026-10-03/n3-ctest.xml): SHA-256
  `8402370c3ac814022fdf51b65449c90eb80e81dc2a20544f0d8c717f93298892`.
- [`receipt.json`](../current-2026-10-03/receipt.json) pins the historical
  `loom_tests` binary to SHA-256
  `3883abbd24433792bd76bb356c58abe570bb83b7b134ca11e826be248bd89c12`.
- [`RESULTS.md`](../current-2026-10-03/RESULTS.md) accurately reports the
  combined CTest entry status, but its “no skips” wording does not describe
  doctest scenario coverage. A green entry is not proof that its cases ran.

Current audited source at base `161cc22`:

| Source | Static case declarations | SHA-256 |
|---|---:|---|
| `loom/tests/test_context_engine.cpp` | 18 | `6f3cc9055f4cfb609c3fd094d91d56d7d696347f54c2a597d16cf162bdc8be32` |
| `loom/tests/test_knowledge.cpp` | 18 | `ee845583b9d109600baa3b2d21a1481d4cdbf7a526c079dd9eb5ef1108af5cfd` |

Rebuild from current source and run the actual suites without changing their
assertions or thresholds. From the repository root:

```bash
cmake --build loom/build/dev --target loom_tests --parallel 4
loom/build/dev/loom_tests --source-file='*/test_context_engine.cpp' --count --no-intro=true
loom/build/dev/loom_tests --source-file='*/test_knowledge.cpp' --count --no-intro=true
env -u PYTHONPATH -u TMPDIR ctest --test-dir loom/build/dev \
  -R '^unit\.(test_context_engine|test_knowledge)$' \
  --verbose --no-tests=error --output-junit focused-native.xml
```

Retain complete console output, XML, binary/source hashes, command and exit code.
Require positive executed-case and assertion counts in both suite outputs;
discovery alone does not replace execution. A failed-only rerun will not rerun
these historical empty suites because CTest had marked them successful.

## CI protection within the workflow scope

`loom/CMakeLists.txt` creates one entry per native test file and passes
`--source-file=*/<name>.cpp`. The current doctest runner returns success when
that filter selects nothing. `ctest --no-tests=error` detects an empty CTest
list, but not a nonempty CTest entry whose internal runner executes zero cases.

The `.github/` lane can protect CI without touching another lane's CMake or
native sources:

1. Produce a complete JUnit report from CTest. Parse its `unit.*` `system-out`
   fields and require a recognized doctest execution summary with a positive
   case count. Missing or truncated summaries must remain an explicit error or
   be replaced by complete per-entry capture; they cannot be silently accepted.
2. Keep explicit opt-in scenarios distinct. Report `unit.test_catalog_scale`
   as unexecuted when not enabled. If enabled, require a positive case count
   and the unchanged original assertions. Do not claim 1 GB scale coverage
   from its default empty run.
3. Record binary case discovery before execution and compare it to executed
   counts. The two named suites currently have 18 cases each. Any future
   declared change should update the inventory deliberately; a vanished suite
   must fail verification rather than silently lowering the denominator.
4. For Python discovery, additionally parse `Ran N tests` and reject `N=0`.
   CTest's outer entry count does not establish inner unittest discovery either.

Doctest's count/list query uses the same filters and avoids running product
operations. An unfiltered inventory plus per-file filtered counts can also
expose wrong source-file filters or missing linked test registrations. Such
checks protect test execution, not model or extraction quality.

## Implemented evidence gate

[`verify_ctest.py`](../../../.github/scripts/verify_ctest.py) checks manifest
identity, duplicate/missing/unexpected entries, outer failures/skips, complete
inner native/unittest summaries and actual positive execution counts. JSON
evidence records observed counts and validation errors; rejection exits 1.
Script smoke checks reject explicit `SKIP:` output. It requires assertions as
well as cases, so the historically empty-assertion `unit.test_resolve_lineage`
also fails verification until its underlying scenarios actually execute.

The existing non-shared `asan` preset has explicit availability exceptions for
two skipped ABI cases and the fully skipped packet-store/HTTP FFI suites. The
gate requires the documented `LOOM_LIBRARY not set` reason. Skipped cases are
subtracted, and fully skipped entries are reported as unexecuted. The `dev`
and `vendored` shared builds have no such exception. These are representations
of existing build capabilities, not new product-quality thresholds.

```bash
ctest --test-dir loom/build/dev --show-only=json-v1 > ctest-manifest.json
python .github/scripts/verify_ctest.py \
  --manifest ctest-manifest.json --junit complete-ctest.xml \
  --preset dev --output ctest-evidence.json
python -m unittest discover -s .github/scripts -p 'test_verify_ctest.py' -v
```

The evidence-gate regressions passed **16/16 tests** on synthetic XML fixtures:
[complete log](zero-discovery-gate-tests.log). They exercise historical 0/0
success, zero assertions, omitted/truncated summaries, manifest identity,
outer failures/skips, expected opt-in classification, Python zero discovery,
unexpected skips, preset-specific unavailability and CLI failure evidence.
These checks validate the reporting gate; they are not new native product
coverage.

## Boundary

This audit used public checked-in evidence and source only. No sealed holdout,
blind corpus, private export, credential, paid model call or GitHub Actions
execution was used. The historical receipt is preserved unchanged. Fresh
execution results, when available, are a separately dated measurement and do
not retroactively convert the old empty entries into completed scenarios.
