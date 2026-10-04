# Integrator 9 — 2026-10-04

Integration checkpoint, not a claim that the cycle is complete.
Base: public main `161cc22dfb84fe863389d6b90323bd44516a68dc`, including
the merged INTERFEJS PR9. Its source, README, profile files and STATE entries
are retained. No implementation file outside an accepted lane is edited here.

## Queue at this checkpoint

| Lane | Reviewed source | Decision |
|---|---|---|
| 2 | 34cc920 | Ready infrastructure; awaiting integrator full gates |
| 3 | 36ec2a8 | In progress; embedding adapter only, no finished selector |
| 4 | 3caa6b4 | Held: confirmed replay accounting defect |
| 5 | 9295a88 | In progress; streaming/resume and final report outstanding |
| 1 | a042ab7 | Ready, queued after lanes 3/4/5 under the requested order |
| 6 | 1fb25ae | In progress; semantic helper not yet connected to selection |
| 7 | 68f7531 | In progress; confirmed orphan-response audit defect in f8bf51b |
| 8 | ee97311 | In progress; evidence corrections published, final receipt/report pending |

No lane has been advanced to main by this checkpoint. Web build at the base
passed. Fresh full native build/CTest is still running: an initial Ninja
configuration could not find its installed executable on PATH, then compilation
hit a full shared disk. Both original failures are retained. The executable
path was set and only reproducible local intermediates were removed; assertions,
thresholds and tests were not changed. No paid calls or Actions were started.

## Concrete returns to lane authors

**W4:** identical packet `capabilities` requests with the same operation ID and
`resources: {"calls": 1, "gpu_seconds": null}` execute twice while the durable
ledger retains one calls sample of 1. An unresolved reservation is not permission
to dispatch again. A freshly compiled W2-policy/W4-dispatcher static overlay
reproduced the defect. Preserve unresolved measurements; add an execution/replay
regression. Source, portable runner, first inputs/results, hashes and full log:
[negative archive](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-packet-unresolved-replay/docs/reports/integrator-packet-replay-2026-10-04.md).
The active W4 branch is unchanged.

**W7:** `arm_audit` checks stranded response files only when no ledger exists.
A valid ledger for request01 plus an unreferenced request02 response yields
no blockers and marks request02 unattempted. The no-ledger control blocks;
the valid-ledger/no-orphan control does not. Check unreferenced files in both
ledger states before offering resume, and retain the response. Only offline
instruments with fabricated pricing and throwing network/key stubs were used:
[negative archive](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-readiness-orphan-response/docs/reports/integrator-readiness-orphan-2026-10-04.md).
The active W7 branch is unchanged.

## Admission boundaries

W2 supplies rolling baselines, durable decisions/reservations and configurable
default ×10 admission. Its 19 new contract groups are standalone, outside CTest;
they must be run separately. Its JSON command is static-kernel only, with no new
shared export, and existing production callers are not automatically guarded.

W1's saved matched-input metrics and first negative variant were independently
checked: standard synthetic metric sections are unchanged, and all 37,249
observations survive the stored-claim reduction 18,005→12,863. Targeted version
cases improve 3/15→15/15. These are branch measurements on its frozen 9d15d2d
corpus, not new integrator measurements or general semantic-accuracy evidence.
Its rejected local-only variant remains on its original archive branch.

Current owner rules govern this batch: paid execution needs consent; no current
balance or newly spendable funds are inferred from the historical $1.10 gap.
The sealed answer key and the blind catalog corpus are not integration inputs.
