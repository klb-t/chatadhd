# Unreferenced response regression — 2026-10-04

The integrator's negative case is preserved on
`origin/archive/2026-10-04/integrator-readiness-orphan-response`, tested against
W7 `f8bf51bc8a317d3e3c73494edcd3860e9427e01d`. Its exact synthetic record is retained
as `integrator-record.json`; the original archived source and reproducer were
not changed.

`before.json` reproduces the failure against the then-current W7 source before
the fix. `after.json` repeats the same three inputs after the fix. Each receipt
pins the readiness tool and original record by SHA-256 and records every input
file hash before and after inspection. All source files remain byte-identical.
These are offline mechanism results, with zero provider calls or credential
reads.

| Synthetic input | Before | After |
|---|---|---|
| Valid first-attempt ledger plus an unreferenced second response | No blocker | `unreferenced_responses_with_ledger` |
| No ledger plus the same response | `stranded_responses_without_ledger` | Same blocker |
| Valid ledger with only its bound response | No blocker | No blocker |

`arm_audit` now checks the response-file inventory in both ledger states. Only
an exact filename already bound by a validated ledger row counts as a bound
response. Unreferenced regular files are retained and reported by filename,
SHA-256 and byte count. Unreferenced symlinks are retained without reading their
targets. Any unreferenced artifact blocks the entire campaign.

No orphan becomes an attempt, a prediction or a cost receipt. Even identical
response bytes do not establish another request's execution. `unattempted_ids`
means that no ledger attempt is bound; it is explicitly not proof of no charge
or permission to launch. Resolving an orphan requires separate exact
generation/response/request receipt reconciliation, rather than a filename or
model-name guess. This audit does not perform that reconciliation or resume a
runner.

Regressions cover the valid-ledger negative, the clean-ledger control, a started
ledger with an unbound response, absent-ledger evidence and a symlink artifact.
All existing billing, denominator, evidence-class and no-retry checks remain.
