# Integrator 9 — 2026-10-04

Integration checkpoint, not a claim that the cycle is complete.
Base: public main `161cc22dfb84fe863389d6b90323bd44516a68dc`, including
the merged INTERFEJS PR9. Its source, README, profile files and STATE entries
are retained. No implementation file outside an accepted lane is edited here.

## Queue at this checkpoint

| Lane | Reviewed source | Decision |
|---|---|---|
| 2 | 34cc920 | Held under new mandate: authoritative usage preset still in C++ |
| 3 | 36ec2a8 | In progress; embedding adapter only, no finished selector |
| 4 | 3caa6b4 | Held: confirmed replay accounting defect |
| 5 | f2af0a8 | Held: failed interpretation checkpoint can replay as complete |
| 1 | a042ab7 | Ready, queued after lanes 3/4/5 under the requested order |
| 6 | 1fb25ae | In progress; semantic helper not yet connected to selection |
| 7 | 3855178 | In progress; orphan-response audit defect remains |
| 8 | 51caa0c | In progress; CI Clang captures/final receipt still outstanding |
| 10 | ccc8bbf | In progress; source views only, final author verification pending |
| 11 | c21e664 | Inventory published before code edits; routed through INDEX |

No lane has been advanced to main by this checkpoint. W2 is linearly rebased
on the integrator review commits and published on the integration branch only
(hosted code receipt `85ac0a0`). Its native build and fresh web build passed;
the actual kernel passed 19/19 standalone policy groups, with both fake HTTP
sentinels at zero. The full W2 CTest gate is running separately.

Fresh baseline CTest executed 106/108 successfully; research.structure and
research.contracts exceeded their unchanged 60-second timeouts. A separate
tmpfs retry also timed out. No assertion failure is demonstrated by a timeout.
Observed shared-host CPU throttling and memory pressure are retained separately
from product correctness; neither timeout is waived. Original build logs retain
disk-full and linker-OOM failures. The successful local build uses low-memory
GNU linker flags, `-O0 -g0`, bundled SQLite and GNU thin static archives; all
129 actual core archive members are hashed. Assertions, tests and thresholds
are unchanged. No paid calls or Actions were started by this integrator.
Full original execution evidence is preserved on the
[baseline timeout archive](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-baseline-timeouts/docs/reports/integrator-baseline-timeouts-2026-10-04.md).

## Concrete returns to lane authors

**W4:** identical packet `capabilities` requests with the same operation ID and
`resources: {"calls": 1, "gpu_seconds": null}` execute twice while the durable
ledger retains one calls sample of 1. An unresolved reservation is not permission
to dispatch again. A freshly compiled W2-policy/W4-dispatcher static overlay
reproduced the defect. Preserve unresolved measurements; add an execution/replay
regression. Source, portable runner, first inputs/results, hashes and full log:
[negative archive](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-packet-unresolved-replay/docs/reports/integrator-packet-replay-2026-10-04.md).
The active W4 branch is unchanged.

**W5:** a valid Anthropic project/document with a transient link-write failure
rolls back its provider transaction. Source retention then records a successful
checkpoint under the same key. After removing the injected error, retry reports
complete without restoring either node or their link; the third attempt returns
the completed-source cache. Both this valid-input case and a malformed-known
member case were reproduced with actual c797225 importer bytes. The newer
f2af0a8 usage-receipt guard does not change the faulty member checkpoint.
Full sources, synthetic archives, results and hashes are preserved in the
[negative archive](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-import-checkpoint-replay/docs/reports/integrator-import-checkpoint-2026-10-04.md).
Return recognized-member failures independently from successful raw-byte
retention and add recovery regressions. The active W5 branch is unchanged.

**W7:** `arm_audit` checks stranded response files only when no ledger exists.
A valid ledger for request01 plus an unreferenced request02 response yields
no blockers and marks request02 unattempted. The no-ledger control blocks;
the valid-ledger/no-orphan control does not. Check unreferenced files in both
ledger states before offering resume, and retain the response. Only offline
instruments with fabricated pricing and throwing network/key stubs were used:
[negative archive](https://github.com/klb-t/chatadhd/blob/archive/2026-10-04/integrator-readiness-orphan-response/docs/reports/integrator-readiness-orphan-2026-10-04.md).
The active W7 branch is unchanged.

## Admission boundaries

The expanded owner rule changes W2's admission decision: its new
`usage_policy_defaults()` embeds growth10/window32/timeout30000/reservation
policy in C++. Those overrideable values must come from authoritative preset
data or a profile with an actual loader before admission. Existing tests still
prove the reviewed implementation's behavior; they do not prove compliance with
this new requirement. This is returned to W2, not repaired in another lane.

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

The expanded owner mandate is tracked in [INDEX.md](INDEX.md), including
report routing from lane11 and the method-graph contract gate for3/4.
Lane10's source-view increment is in progress and explicitly not ready for
integration; its new branch does not replace the already merged INTERFEJS work.

Current owner rules govern this batch: paid execution belongs only to lane7's
new separate EUR5 budget/key; this integrator makes no paid calls. No current
balance or newly spendable funds are inferred from the historical $1.10 gap.
The sealed answer key and the blind catalog corpus are not integration inputs.

## Do wątku N

- **3/4:** jointly publish the versioned method/recipe/prompt/run graph format,
  including result-to-method provenance edges and a cross-lane regression,
  before either completed combination is admitted. Keep model provenance explicit.
- **4:** repair the archived unresolved-replay accounting defect above.
- **5:** repair interpretation checkpoint replay and verify restored project,
  document and link after a transient error; publish final report/instructions.
- **7:** repair orphan-response discovery even when a verified ledger exists;
  publish the final research/billing report and recipes for1.
- **1/2:** renewed assignments are published in INDEX from lane11's pinned
  inventory: prompt/graph methods for1 and startup/config presets for2.
- **2:** move new usage-policy preset to authoritative data/profile; share one
  actual default loader across Config/get/open/settings/effective paths, preserve
  owner overlay and19 groups, demonstrate that edited data changes options.
- **6/8/10:** publish final readiness reports and full receipts; scope-specific
  pending work and dependencies are tracked in INDEX.
- **10:** repair unused captures for /api/logs and /api/version reported by8;
  leave /api/info's used capture intact. Do not suppress the Clang warning.
- **11:** per-lane reports and JSON have been routed; mark each hardcoded method's
  graph destination and coordinate Semantic LLM with1/graph selection with3.
  Ownership gaps in native/core/CAPI/crypto, Android and retained Python remain
  explicit; an inventory label does not authorize out-of-scope implementation.
