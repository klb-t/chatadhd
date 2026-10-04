# Thread 2 — usage policy and settings, 2026-10-04

Branch: `gpt/usage-policy-2026-10-04`. Base: current public main
`161cc22dfb84fe863389d6b90323bd44516a68dc`, including the INTERFACE increment.
First interface published as `c794c3c`; implementation published as `682af21`.
Final fetch/rebase found main unchanged. Main/STATE/README/web/profile files
were not edited. No provider calls or GitHub Actions were executed.

## Delivered

- Native `UsagePolicy`: preflight estimate, atomic admission/reservation,
  receipt-bound owner approval/rejection, actual usage, cancellation and inspection.
  The dedicated SQLite ledger preserves decisions and unresolved reservations
  across restart and serializes independent connections.
- Per-resource rolling measured baseline, caller-named comparison cohorts,
  configurable window (`null` = all history), growth factor (preset 10),
  initial declared baselines and concurrent-reservation comparison.
  Unknown amounts remain unknown; their known lower bound still detects growth.
- Exactly ×10, including `0.1 -> 1.0`, requests confirmation. No baseline
  reports unavailable rather than fabricating zero. Measured zero followed by
  positive projected usage requests confirmation. Actual overruns remain recordable.
- A Loom-only `loom_usage_policy` fallback and validation through the existing
  configuration C ABI. Invalid mixed patches change no settings. Historical
  12-key defaults, existing file bytes, model choices and open config keys remain.
  The ledger lock timeout is also a configurable preset.
- [API/settings contract](../../loom/src/policy/README.md) for lanes 3–5 and a
  future settings screen. New C++ and JSON C commands work against the static
  kernel; existing shared config get/set APIs expose the setting.

## Before / after and evidence

| Check | Before | After |
|---|---:|---:|
| Full CTest entries, server/CLI/shared build enabled | 108/108 | 108/108 |
| Focused policy contract groups | Absent | 19/19 |
| Common native ×10 admission ledger | Absent | Implemented |
| Hard ceilings found in owned config implementation | 0 | 0 |
| New paid provider calls | 0 | 0 |

The new focused groups cover boundary/decimal/overflow cases, rolling windows,
provenance, null amounts, partial completion without duplicate training,
idempotency/conflicts, restart, two concurrent connections, stale confirmations,
corrupt records and the static C dispatcher/config setters. A fake HTTP callback
fails any network request in the C API fixtures; its observed calls were zero.

The final focused binary links the actual freshly built `libloom_core.a`, not a
replacement policy implementation. Evidence and source/binary hashes are in
[the retained receipt](../../loom/src/policy/tests/evidence/2026-10-04/manifest.json),
with both full CTest logs and the focused-test log. No external `PYTHONPATH` or
`TMPDIR` was supplied. Baseline and after timings are not a performance benchmark.
The first baseline build encountered an empty compiler object; rebuilding that
object passed. Its original build failure is retained, without source/test changes.

## Integration work outside this lane

1. Lanes **3/4/5** must explicitly adopt `UsagePolicy` before their operations;
   adding this module does not automatically guard existing production callers.
   A ledger receipt is not exactly-once tool dispatch or a paid-call permission.
2. The exact shared-symbol gate scans central `loom.h`. That header and its test
   are outside this lane. `loom_usage_policy_json` is therefore deliberately
   **static-kernel only**, with no new shared export. The integrator can register
   its declaration/export in `loom.h` and verify the shared ABI in a separate
   scoped change. Focused tests already exercise the real static C API.
3. The new standalone `.cc` groups have a documented compiler/run command.
   Registering them as a CTest target requires the CMake/test registration owner.
   They supplement, and do not replace, the unchanged 108-entry CTest gate.
4. Quantities use finite binary64 JSON numbers, not the research ledger's exact
   decimal billing representation. Retain exact provider receipts separately.
   Existing generic config save failures can leave changes in RAM; atomic
   storage rollback requires a `JsonStore` API change outside the allowed header scope.

## Remaining audited ceilings — do not describe these as fixed

The owned config store already accepted arbitrary settings. Limits occur in
consumers and were left to their owners, per the file boundary:

| Owner | Remaining source areas |
|---|---|
| 3 | Goal typing request/bytes/tokens/timeout ceilings and invocation policy; context scan windows; chat attachment/reasoning/web-result/timeout presets; batch worker ceiling. |
| 1 / extraction owner | `extract/semantic.cpp` lower-only budgets and checkpoint windows; `candidate_graph.cpp` packet/arity/text/nesting maxima. |
| 5 / KB owner | Import depth 512 and inline 1,000,000 bytes; KB candidate pages and query windows belong to `kb/`/knowledge, outside the literal `import/db` assignment. |
| 6 | Catalog eligible stems 4,000, sketch sample 4 MiB, Bloom/hash clamps and expansion-pass policy. |
| Unassigned in this round | Task scheduler's first 64 rows; archive clamps/refinement windows; legacy graph/search/semantic windows; provider-list/GitHub/media/worker transport timeouts; CLI scan windows. |

The complete starting inventory remains
[LIMITS_AND_WIRING](../LIMITS_AND_WIRING_2026-10-01.md). Representation ranges,
schema/version checks and audit/transaction invariants were not removed.
