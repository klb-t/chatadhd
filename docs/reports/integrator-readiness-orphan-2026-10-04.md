# Integrator 9 — W7 orphan-response audit blocker, 2026-10-04

Negative case verified against W7
`f8bf51bc8a317d3e3c73494edcd3860e9427e01d`.
The active W7 branch is left unchanged. This archive preserves the tested source
and exact synthetic inputs/results; no paid runner or credential loader ran.

`model_study_readiness_v1.arm_audit` scans `*.response.bin` only when
`ledger.json` is absent. With a valid ledger for `request01`, an unreferenced
`request02.response.bin` is instead classified as an unattempted request with
no blocker. This contradicts the branch's documented whole-campaign resume rule
and leaves ambiguous whether request02 may safely be retried.

| Synthetic case | Expected | Actual |
|---|---|---|
| Valid ledger plus unreferenced response | Block | No blockers; request02 unattempted |
| No ledger plus the same response | Block | stranded_responses_without_ledger |
| Valid ledger without extra response | No block | No blockers |

All pricing evidence is explicitly fabricated fixture data and is fresh under
the pinned fixture clock. The valid ledger contains one hash-verified response.
Only `arm_audit` and offline `plan_manifest` were invoked; campaign execution and
current account balances were not tested.

Return to W7: compare response files against all ledger-bound files regardless
of whether a ledger exists, reject/report unreferenced evidence before classifying
requests as unattempted, and add the valid-ledger regression. Preserve the
unreferenced response; do not delete or silently attach it to another request.

Source SHA-256:
`8bcd043d50a2e36a024c1c48ac72573b06e2747e02932aca7da9437238cf1a47`.
The [record](integrator-readiness-orphan-2026-10-04/REPRODUCTION.json) includes
exact manifest, plan, ledger, response envelopes and dependency hashes. The
[recorded reproducer](integrator-readiness-orphan-2026-10-04/reproduce-recorded.py)
retains its original paths. Run the [portable reproducer](integrator-readiness-orphan-2026-10-04/reproduce.py)
from this archived checkout:

```sh
python3 docs/reports/integrator-readiness-orphan-2026-10-04/reproduce.py \
  --repository . --output /tmp/loom-readiness-orphan-evidence
```

It loads the pinned audited source through local Git and replaces transport and
credential loading with throwing stubs. It prints the negative result rather
than claiming a successful acceptance gate.
