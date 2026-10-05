# Native integration verification — 2026-10-03

**108/108 outer CTest entries passed, with no outer CTest skips.**
**Correction dated 2026-10-04:** the retained XML shows zero actual doctest cases
and assertions in `unit.test_context_engine` and `unit.test_knowledge`, in
addition to the documented opt-in `unit.test_catalog_scale`. The lineage entry
also returned early with zero assertions when Git history was unavailable.
These entries do not establish scenario coverage; the historical XML and binary
receipt remain unchanged. See the
[empty-suite audit and replacement verification](../repo-hygiene-2026-10-04/zero-discovery-audit.md).

The first full run passed
93 entries and failed 15 because the restored runtime lacked documented Python
dependencies and a generated compatibility executable was corrupted. Installing
those dependencies and relinking that executable changed no source. All 15
failed entries then passed. Both complete XML outputs are retained; this result
combines the full run with its failed-only rerun, rather than claiming a second
full run. The rerun text log was truncated; its complete XML is authoritative.

The new GraphPacket store also passed **13/13 actual FFI regressions** through
Python → C ABI → KnowledgeStore → SQLite. These are the same new suite included
in CTest, not 13 additional independent suites. Its documented minimal example
was separately executed against the real shared library.

## Source and build

[The receipt](receipt.json) pins **903 source/support files** in
[source-final.json](source-final.json), plus actual binary hashes. Of the previous
898 measured files, 894 are unchanged; four native files changed and five store
adapter/test files were added. Web source is unchanged. Previous browser/demo
checks and production web build remain dated evidence in
[the 2026-10-02 report](../current-2026-10-02/RESULTS.md); they were not rerun or
relabeled as current external-provider measurements.

Build: GNU C++ 13.3, CMake 3.31.10, Ninja 1.13.2, Debug `-O0 -g0`, warnings as
errors, bundled SQLite 3.47.2, shared library, CLI and server. Two zero-length
generated object files and the corrupted generated compatibility executable
were regenerated; source assertions and thresholds were unchanged. The earlier
build/failure logs remain in the recovery evidence package.

Run from the repository root with the documented Python dependencies installed:

```bash
python3 -m pip install requests cryptography -r loom/tools/contracts/requirements.txt
cmake --preset dev -S loom -DLOOM_USE_SYSTEM_SQLITE=OFF \
  -DLOOM_BUILD_SERVER=ON -DCMAKE_CXX_FLAGS_DEBUG='-O0 -g0' \
  -DCMAKE_C_FLAGS_DEBUG='-O0 -g0'
cmake --build loom/build/dev -j 4
env -u PYTHONPATH -u TMPDIR ctest --test-dir loom/build/dev \
  --output-on-failure -j 4 --output-junit ctest.xml
```

Only when the initial run has failures, inspect and resolve their actual cause
before `ctest --test-dir loom/build/dev --rerun-failed --output-on-failure -j 4`.
The exact installed Python versions are recorded in
[n3-python-environment.json](n3-python-environment.json).

## What the new store tests establish

- Explicit acceptance and selected-record closure; no implicit import outside
  the selection. Validated source bytes, quote/subspan, provenance and CAS.
- Full packet/history retention, assessment retention, immutable record identity
  and receipt protection against SQL update, delete and replace.
- Owner judgement reapplication before receipt creation, including later writes.
- Atomic rollback after both a late claim failure and a receipt insertion failure.
- Readback of actual SQL rows; changed/missing rows, indexes, aliases and support
  are detected. Replay verifies current state and never silently repairs drift.
- Reopening the database, idempotent receipt reuse, and KB schema 2→3 migration
  preserving earlier records; core schema remains v4.

See [the API and contract](../../NATIVE_GRAPH_PACKET_STORE.md). Python validates
the complete reversible history; native validation covers selected DTOs,
provenance, references and source integrity. Opaque principle/prediction/check
references remain explicitly unchecked. Acceptance does not establish content
truth or authenticate the external origin of source bytes.

## Evidence and boundaries

| Evidence | Role |
|---|---|
| [Initial full CTest XML](n3-ctest.xml) | All 108 entries, including the 15 first environment failures |
| [Failed-only rerun XML](n3-ctest-rerun.xml) | Complete 15/15 passing outputs after runtime repair |
| [Combined status](n3-ctest-combined-status.json) | Explicit merge of the two runs, with incomplete text-log limitation |
| [FFI log](n3-ffi.log) | 13/13 real native store regressions |
| [Receipt](receipt.json) | Commands/build boundary, source manifest and binary hashes |

No paid model calls, GitHub Actions, new real-provider quality evaluation,
Android device validation or independent holdout measurement were performed.
The prepared 432-request analysis programme remains blocked by an expired
credential envelope and unavailable private session/decryption material.
