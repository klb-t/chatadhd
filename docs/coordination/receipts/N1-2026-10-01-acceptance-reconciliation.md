# N1 — reconcile the two durable W1 implementations without replacing their journals

Date: 2026-10-01. Lane: `gpt/night2-acceptance-2026-10-01`.
Integrated base: `926072c` (verified implementation `da77c769d3e020c396db5bef6a3a3755314d75c0`).
External comparison: `ba792b920fba36fb738ff39421d27b6d965aa11a` →
`b28e7bedb0094f8c0e98e653d156104f2709f15c`; external production commit
`200fae8ccab3ad50f954d35ea60b9a9148ddc514`, tree `8c818cc3deb1068eb34dc8dd7ab6f5d4a70bf956`.

## Reconciliation contract

This is a selective compatible increment, **not a merge of the external W1
branch and not a journal migration**. Both original histories and their receipts
remain in Git. The external branch has a receipt with the same pathname as our
existing W1 receipt; neither was overwritten. The comparison read its
`W1-2026-10-01-durable-acceptance.md` and `W1-2026-10-01-durable-review.md`
using `git show b28e7be:<path>`.

| Contract | Integrated W1 before N1 | External W1 `b28e7be` | N1 decision |
|---|---|---|---|
| Accepted envelope | `accepted.v1`, five fields, SHA over `json::dump(snapshot)` | Same event name and schema label, ten fields, SHA over `json::canonical(snapshot)` | Retain integrated bytes/format; require exactly its existing five fields. Detect alternate envelope and fail explicitly, without rewriting events. |
| Compatibility baseline | `legacy_observed.v1` records, then three-field `baseline.v1` | Legacy provenance inside `accepted.v1`, then seven-field `acceptance_baseline.v1` | Retain integrated baseline and all observed rows. No implicit conversion or second marker. |
| Later known-row metadata divergence | Explicit rejection; missing/moved projection cannot reset authority | Ignores present malformed/null/fabricated projections and changes three old tests | Retain our reviewed fail-closed diagnostic contract and all original tests. |
| Product identity | Supplied/compiled spec, bindings, full current-source snapshot | Supplied spec and bindings; recipe-mode replays supported | Preserve stronger source identity; also compare immutable inherited evidence/coverage. History mode remains an independent recipe choice. |
| Duplicate observations | Identical event copies permitted; duplicate legacy rows retained | Unique acceptance origins, deduplicated legacy products with observed-row list | Preserve integrated compatibility; no origin deduplication migration. |
| Snapshot validation | Primarily compiled spec, bindings and current source text hashes | Full emitted representation, coverage and inherited ancestry checked | Add complete validation to our format, preserving supported null/opaque attachment JSON and native signed version values. |
| Legacy origin role | Did not distinguish user from assistant/system | Rejects non-user legacy origin | Add rejection before migration or current-row/provider effects. This describes caller acceptance, not owner judgement. |
| Explicit event chronology | Validates final complete scope graph | Also validates final graph, despite reading events in order | Add per-scope head folding at each explicit acceptance event. Unordered legacy recovery is validated and seeds heads at its completed marker. |
| Source/accepting-row move, restart, transaction, crash | Existing 14 cases, including process exits and separate connections | Separate native/C ABI cases, different journal expectations | Keep existing tests; port compatible missing counterexamples, not a replacement suite or count. |
| Native history freshness | Before/after native-row comparison | Captures actual native history used during composition | Investigate deterministic ABA separately; see below. |

External receipts report an 85/85 regression on their own tree. Our integration
ledger reports 94/94 on `da77c76`. Those are separate historical runs and are
neither summed nor claimed as execution of this increment.

## Implemented authority checks

`active_task_acceptance.cpp` validates all twelve fields emitted by the current
snapshot schema and all ten fields of each native source snapshot. Coverage is
reconstructed from current and inherited message IDs. Inherited source bytes,
roles, attachment references and product references must then match the actual
validated ancestors; a fresh self-consistent SHA cannot certify invented
ancestry. Source bytes remain preserved; no mutable native row is required to
revalidate historical evidence.

The reader seeds each full-scope head only after validating the complete
unordered legacy recovery set. Later explicit events must be a compatible
latest replay or the exact next revision at that point in the sequence. Thus
`v2 → v1` and `v1 → v2 → stale v1` fail even if the final set of products could
form a complete graph. Independent tasks may interleave their own revisions.
There is no new arbitrary event cap; all pages still participate.

The alternate external baseline/envelope now gets an explicit incompatible
journal error requiring deliberate migration. Unknown same-schema fields also
fail closed. No event is rewritten, erased, reclassified as an owner decision,
or silently imported from the alternate format. SQL schema and public ABI are
unchanged.

## Independent tests and checkpoints

- `4b5c2f6`: five independently authored Runtime/EventLog cases by
  `context_runtime/acceptance_audit`: stale and reversed event order,
  interleaved full scopes, assistant/system legacy origin, and rehashed
  inherited-evidence forgery. Final source review found no blocker in the
  approved journal-validation scope.
- `bc166ef`: production validation plus six complementary public native cases:
  sixteen full-representation mutations; a first successor with invented
  inherited evidence; reversed legacy observation order and duplicates;
  null attachments and mixed recipe replay; both external formats preserved
  on rejection; same-schema envelope/marker drift.
- `d9f4b11`: separate test-only native-history ABA reproducer, before its fix.
  A custom builder changes unrelated native history from A to B; public SQLite
  PROFILE tracing restores A through an independent connection after the actual
  history read. The test demands rejection with no row, journal, callback or
  provider effect. If a vulnerable baseline transmits, it also checks that B
  actually reached the synthetic provider payload while the native row is A.

The production validator at `bc166ef` and the first three new test files passed
C++20 syntax-only compilation with `-Wall -Wextra -Werror`; `git diff --check` passed. No full native build was
run in this lane. The coordinator's single verification lane owns runtime
red/green and the combined regression. The source-only ABA checkpoint was
subsequently executed against the preserved baseline, as recorded below.


## Scope member-order follow-up

The approved narrow follow-up is documented separately in
`N1-2026-10-01-scope-identity.md`: test-first `5152f20`, implementation `bd1d9f1`.
The same named scope now has one lookup identity even when an ordered JSON
object arrives with its members rearranged. Raw supplied specifications and
all existing journal encodings remain preserved. This source-level finding
also affects external W1's `prepare_active_task` comparison; canonical keys
in its later graph fold alone do not prevent a first bad acceptance.


## Executed ABA baseline failure and minimal correction

The separate verification lane compiled the unchanged test `d9f4b11` against
the preserved `da77c76` static archives, with no core rebuild. Compilation
passed in 19.76 seconds; the baseline probe exited 1 in 0.065 seconds: one
failed case, nine assertions, four passed and five failed. Successful setup
assertions establish that the second connection restored A and the entire
native row equals its original value. The payload assertion independently
establishes that B reached the actual scripted-provider request. The baseline
incorrectly accepted the send, persisted four rows rather than two, ran one
callback, and produced provider and acceptance-event effects.

Evidence is retained at
`/workspace/scratch/a371a1ca13b1/verification/night2-aba-baseline-first/`:
`receipt.json`, `compile-first.log`, and `probe-first.log`. The receipt records
all compiler/linker arguments, source/archive hashes, and confirms unchanged
inputs. Test SHA-256:
`555fc9ce95fae6a60fd722dd2175cb6a552e41813e027e61612d29d7b2bed1c6`.
Baseline core archive SHA-256:
`42f0f8a1b0d1fc274878db40983b750687654c0b2c52ca2ad6fc3fbd9b0a9a15`.
This is a reproduced latent baseline failure, not a passing new-feature test.

Fix `72f1b6f` captures the native history rows actually consumed inside
`build_messages`, alongside its existing exact-message trace. Under the final
writer transaction, the same row projection (ID, conversation, status, role,
text and weight) must match. It supplements the existing before/after and task
source checks, preserving earlier conservative rejection tests. The temporary
capture control and result are removed before any stored or returned context
trace; public trace and journal schemas remain unchanged. No arbitrary builder,
callback or provider is invoked inside that transaction.

All N1 production changes are now frozen for the single combined native build
and green regression. The green result is pending central verification, not
claimed by this implementation receipt. No paid calls, private archives or
holdout data were used.
