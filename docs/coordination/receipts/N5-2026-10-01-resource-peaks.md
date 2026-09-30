# N5 — historical reservation peaks without invented measurements

- Owner: `design_audit`; date: 2026-10-01.
- Base: `926072ce43b8de21a42a3abe439a7781598136f6`.
- Protocol and first BEFORE commit: `e8e706c18727097c6c48fe4fdd76fbc9bdfedd8c`.
- Reviewed/tested source commit: `7ecbe6ed34dfd024b571d5c4581794c12fe3a99d`.
- Final test-only strengthening: `bcc930ac0e8fa3ff28da06f5227604c8e6398e2c`.
- Worktree: `night2-resources`; integration/publication/STATE belong to ROOT.
- Production file: `loom/tools/contracts/analysis_plan_ref.py`; dedicated tests:
  `loom/tools/contracts/test_resource_peak_history.py`; contract:
  `docs/contracts/ANALYSIS_PLAN.md`.

## Result and evidence boundary

The counterexample from `docs/research/RESOURCE_PEAK_LIMITATION_2026-09-30.md`
is fixed for new recorded reservation boundaries. Existing admission and capacity
reuse are unchanged. New reservation records embed their before/after original
held load in the same fsynced file, before packet loading or callback dispatch.
There is no independently committed peak receipt that can be lost on settlement.

| Original two-callback fixture | Before | After |
| --- | --- | --- |
| Current admission while both reservations are held | 20 | 20 |
| Current admission after both return scripted quantity 7 | 7 | 7 |
| Durable historical reserved peak | Not represented | 20 |
| Current original reserved load after completion | Not separately represented | 0 |
| Actual simultaneous instrumented peak | Not measured | `null`, unavailable |

These are authored scripted mechanism cases, not a measurement of actual memory,
real model quality, billing, or a global peak of 14. Callback maxima retain their
caller-supplied provenance status separately and are not added into a global peak.
The independent reviewers inspected source; they did not independently run these
tests. No paid calls, private archives, holdouts, remote transport, or C++ build.

The additive `accounting()` / `resource_accounting` API distinguishes current
admission, original held reservations, historical reserved peak, unavailable
global instrumented peak and maximum callback quantity by provenance status.
Limits, attempt identities, callback first returns and uncertainty/no-retry rules
remain unchanged. N3 confirmed its separate native-store consumer does not settle
or reinterpret these prior attempts.

## Compatibility and durability

New attempts use reservation schema version 2 with content/header-bound
observations. The ledger header remains version 1. Old version 1 reservations,
completion records, headers and execution receipts are read without rewriting.
Old attempts lacking observations contribute to admission as before and count
as missing historical coverage; no earlier overlap is reconstructed. A new
boundary can observe older reservations still held, with coverage still marked
incomplete. Opening balances preserve their provenance; external preceding
history is explicitly not reconstructed.

Tests use actual spawned processes and SIGKILL at three boundaries. A killed
process before reservation write leaves no callback and an incomplete attempt
that fails closed. After durable reservation, its historical observation and held
load survive together without retry. After completion, replay uses the first
result without dispatch and keeps earlier overlapping reservation history.
These establish process-crash behavior on this filesystem, not power-loss or
distributed durability. Local hashes are integrity checks, not protection against
a party rewriting the whole filesystem. Coverage describes retained records;
it cannot prove deleted history never existed. Callback external effects remain
outside a global transaction, with no exactly-once external-effect claim.

## Verification

Frozen protocol, exact reproduction script, original output, corrected output,
full logs and file SHA-256 manifest are in
[`N5-2026-10-01-resource-peaks/`](N5-2026-10-01-resource-peaks/).

All commands ran from the worktree root with:

```sh
export PYTHONUSERBASE=/workspace/scratch/a371a1ca13b1/verification/python-userbase
python3 -B docs/coordination/receipts/N5-2026-10-01-resource-peaks/reproduce.py
python3 -B -m unittest loom.tools.contracts.test_resource_peak_history -v
python3 -B -m unittest \
  loom.tools.contracts.test_analysis_plan_ref \
  loom.tools.contracts.test_resource_peak_history \
  loom.tools.coordination.test_leases \
  loom.tools.coordination.test_graph_workflow \
  loom.tools.coordination.test_cli \
  loom.tools.coordination.test_analysis_graph \
  loom.tools.coordination.test_analysis_graph_adversarial -v
git diff --check
```

- Original reproduction preserved before implementation; corrected reproduction
  ran on the committed source SHA above. Both use the identical frozen script.
- Inherited AnalysisPlan first run: **32/32 PASS**, 13.936 s.
- Dedicated first run: **12/12 PASS**, 5.082 s (`first-new-suite.txt`). After that
  pass, replay immutability comparison was strengthened to key by relative path
  so both reservation files are checked instead of a shared basename.
- First combined run on the committed source bytes: **94/94 PASS**, 40.964 s
  (`regression-suite.txt`): 32 inherited AnalysisPlan + 12 dedicated history +
  50 inherited coordination. Assertions in inherited suites were not changed.
- Final strengthened run: **94/94 PASS**, 39.727 s
  (`final-regression-suite.txt`). The test-only change above moves the
  after-reservation kill point to after the entire `reserve()` call, including
  fsync of the ledger directory. Production bytes are unchanged. Both combined
  logs are retained; `final-after.json` repeats the frozen reproduction at the
  final test commit. `after.json` retains the first committed-source reproduction.
- The new cases cover separate ledger objects in threads, two spawned processes,
  reopen/replay, three SIGKILL boundaries, old completed and old still-held
  attempts, opening balances, estimate 30 vs reservation 10, subsequent reservation
  5, limit rejection/capacity reuse, corrupted observations and exact Decimal sums.
- `git diff --check`: PASS before source commit. Disk free space was reported as
  zero during the combined run; the run nevertheless completed without I/O errors.
  The first evidence staging attempt failed with ENOSPC; source was already
  committed. ROOT coordinated cleanup before completing the remaining commits.

N1 (`context_runtime`), N3 (`coordination_runtime`) and central verification each
performed a read-only review of the production diff and evidence contract and
reported no blocker. They specifically checked same-record crash boundaries,
unchanged admission equations, original reservation vs policy-inflated estimates,
legacy coverage and unavailable actual peak. ROOT still owns final integrated
verification and publication.
