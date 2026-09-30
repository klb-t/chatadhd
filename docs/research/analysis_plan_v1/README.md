# AnalysisPlan reference mechanism, 2026-09-30

This increment adds a domain-independent, data-configured method DAG, symbolic
Cartesian variants and a durable callback-only local executor. It does not call
any provider, load credentials, allocate agent workers or apply graph candidates.
Method/runtime/model identifiers, roles, scopes, reasoning, acceptance/evaluation
policies and limits are caller data. Schema dispatch `SCHEMAS` is unchanged; the
existing local registry loads the new schema. Native wiring is outside scope.

## Preserved development chronology

1. `scripted_first_evidence/` is the original mechanism run with four local
   callbacks. Its first bytes remain unchanged. Independent audits subsequently
   exposed empty/null provenance releasing a reservation, ambiguous unbound
   source replay, a completion/result accounting mismatch and loss of a returned
   candidate when metadata was invalid. Original code, repros and evidence are
   retained in `../analysis_plan_independent_audit_v1/` and the philosophy watch
   archive. This first demo is not rewritten to look correct.
2. `scripted_repaired_evidence/` preserves the first corrected accounting demo.
   `scripted_final_evidence/` adds an explicit admission-accounting label. These
   names reflect their creation sequence; neither supersedes earlier first bytes.
3. `scripted_snapshot_evidence/` is the current example: four local callbacks over
   two selected coordinates and two dependent methods. Loaded aggregate packets
   and finite first returns are preserved before callback/result validation.
   Completion binds exact measurement/provenance and both retained hashes.
4. Final direct-execution selection validation closes an additional independent
   counterexample: an unselected coordinate raises `variant_not_selected` before
   creating a reservation or invoking any loader/callback. Coordinate inspection
   remains possible without execution.

All runs are **scripted mechanisms**, not model inference, quality measurements,
validation or holdout observations. External API calls and incremental provider
spend are zero. The current demo allocates zero workers and performs zero
canonical graph writes. Its admission usage is money $0, calls 4, CPU held 0.04,
memory held 16384 bytes and storage held 4096 bytes. These held quantities are
reservations, not measured CPU/memory/storage consumption; those instruments are
not supplied. Planned role cardinality 1,000,000,000 creates no workers.

## Verification and independent counterexamples

- Original contract regression: 173/173 tests, preserved `contracts_regression_first.log`.
- Provenance/accounting repair: 182/182 tests, preserved repaired log.
- Exact arithmetic repair: 183/183 tests, preserved final log.
- Snapshot receipt addition: 184/184 tests, preserved frozen-candidate log.
- Current targeted suite: 32/32, `targeted_selection_receipt.log`.
- Current full contracts suite: 185/185, `contracts_regression_final_selection.log`.

The independent validation reviewer preserved original failures, reproduced
repairs with multiple ledger instances/processes, verified hard process exit
retains every reservation and causes no retry, exercised all ten independent
resource dimensions, drifted receipt layers and compared exact decimal sums to
Fraction over 512 cases under four ambient precisions. Philosophy independently
retested null/empty provenance, unknown/estimated/reported accounting, source
bindings and explicitly labeled snapshot replay. Each reviewer owns its artifacts;
this directory does not rewrite them.

Per-dimension measurement statuses and admission treatment are explicit data.
Estimated/unknown/declaration quantities remain epistemically distinct even if a
caller explicitly permits them for planning. Conservative example policies hold
the larger of reservation and uncertain amount. Source descriptors are
`caller_declared_not_verified`; domain adapters own underlying raw-byte binding
validation. Loaded aggregate packet hashes are separate reproducibility evidence,
not proof of source identity or content truth.

The shared runtime budget seam audit at `../shared_runtime_budget_audit_v1/`
clarifies that arbitrary callbacks cannot claim enforced per-child paid admission,
fee aggregation or revocation propagation. A later lease-aware adapter needs one
accounting owner, lineage and pre-dispatch admission for each external step.
This reference supports supplied callbacks and records their results; it does not
silently wire a paid runtime or claim a sandbox over callback side effects.

`FREEZE.json` pins the reviewed implementation and preserved evidence after
mechanism verification, before any external execution. It is a code/evidence
handoff, not a preregistered model-quality experiment. Ledger lock files are
excluded because they are synchronization surfaces, not observations. Opening
an old demo with the changed schema is not a migration/replay promise; its
original audit snapshot remains the route for reproducing the original behavior.
