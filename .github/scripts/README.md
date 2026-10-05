# CTest execution evidence

`verify_ctest.py` compares CTest's discovered manifest with its complete JUnit
receipt. A successful outer entry is insufficient: native entries must report
positive executed cases and assertions, and unittest entries must report actual
execution. Missing entries, duplicates, failures, unexpected skips, missing
summaries and truncated outputs fail the check. No product assertion or quality
threshold is changed.

The existing opt-in `unit.test_catalog_scale` is reported as **unexecuted** when
its output contains zero cases/assertions; it does not count as 1 GB coverage.
Historical non-shared `asan` runs have explicitly named unavailable ctypes
cases; those are reported separately and subtracted from executed coverage.
Current ASan CI retains the instrumented native build and additionally builds an
ordinary GCC shared-library companion from the same commit/tree. Its absolute
`LOOM_LIBRARY` reaches ctypes tests, including the mandatory Packet suite.
The separate companion receipt records its binary hash and ordinary flags:
this is FFI execution without sanitizer coverage, alongside native ASan/UBSan.
The CI matrix uses GCC's minimal `-g1` debug information for ASan: stack source
locations remain, while local-variable debug data is omitted to reduce compiler
memory. Assertions, optimization, warnings and sanitizer flags are unchanged.
The sanitizer options and execution guard remain unchanged. A local full GCC
build of the same source can supply this ordinary companion too, with its own
receipt. `dev` and `vendored` builds require all shared-library cases to execute.
Lineage tests require checkout of the complete preserved Git history.

From the repository root, after building the selected preset:

```bash
ctest --test-dir loom/build/dev --show-only=json-v1 > ctest-manifest.json
ctest --test-dir loom/build/dev --output-on-failure --no-tests=error \
  --test-output-size-passed 10485760 --test-output-size-failed 10485760 \
  --output-junit "$PWD/ctest.xml"
python3 .github/scripts/verify_ctest.py --preset dev \
  --policy .github/ctest-evidence-policy.json \
  --manifest ctest-manifest.json --junit ctest.xml --output executed-cases.json
python3 -m unittest discover -s .github/scripts -p 'test_*.py' -v
```

Retain the JSON evidence even after a rejected run: the command writes its
observations and errors before returning nonzero. The output capture size in
the workflow is an instrumentation setting; raise it if a complete summary
would otherwise be truncated. This check does not establish model quality or
execution of script-internal scenarios that do not expose a case count.

Before CTest, `write_build_receipt.py` records the exact Git commit/tree and
SHA-256 of the native runner, CLI, server and available shared library. Required
executables must exist. Receipts are retained with the manifest, JUnit and
observed execution counts; the initial unsuccessful CI run predates this
binary-pinning step and cannot supply a retrospective executable hash.

## Versioned evidence policy

`.github/ctest-evidence-policy.json` is the default descriptor. The local
`ctest-evidence-policy.schema.json` validates its format using `jsonschema`,
already included in the documented contract requirements. No external schema
is fetched. `--policy PATH` explicitly selects another local descriptor;
omitting it preserves the default behavior of existing invocations.

Preset names, ordered runner bindings and exact named availability exceptions
come from that data. Supported parsers and integrity/accounting operations
remain code. A declared new preset needs no Python preset-name branch. Policy
files cannot declare an ignore list or wildcard availability exception. Missing,
malformed, duplicate or contradictory policy entries fail without fallback.

The initial descriptor preserves all existing conditions: scale 0/0 remains
unexecuted, ASan shared-library exceptions require the recorded reason, exactly
two ABI skips still require a case to run, and all other skips remain errors.
Unavailable cases never become coverage, and policy cannot waive outer
failures, missing manifest entries or incomplete summaries.

Each receipt records `policy_provenance`: schema ID, revision, exact-byte
SHA-256, schema SHA-256 and resolved path. The loaded descriptor recursively
copies JSON objects into read-only mappings and arrays into tuples, including
the provenance metadata, so it cannot drift after hashing.
A valid policy's provenance is retained even when the manifest/JUnit input
fails. Invalid policies produce failure evidence without claiming a validated
policy version.

`--cmake-cache PATH` plus repeated `--cmake-field NAME` records the selected
build settings and an exact-byte cache hash. Field selection belongs to the
caller/workflow; unrelated cache values are omitted. Missing, duplicate or
malformed selected fields fail. CI pins both native compatibility tools as well
as the test runner, CLI, server and shared library.

`run_optional_npm_scripts.py` runs caller-selected names from the actual package
after existing web checks. An absent name produces `CAPABILITY_UNAVAILABLE`;
it establishes no test coverage. Every declared name starts through argv in its
package directory, and a failing child fails CI. The current main does not yet
declare W10's `test:interface-2` or `test:interface-2-native`.
