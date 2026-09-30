# W1 — durable acceptance implementation

Date: 2026-10-01. Lane: `gpt/fix-w1-durability-2026-10-01`.
Base: `96ea1c727ee5bbba4ee056dfa15aa844d1102d22` (integrated W1–W6).
Production commit: `4c1d754`.

## Implemented boundary

The active-task chat path now uses the existing `loom_events` store via
`EventLog`, with no SQL schema migration or public ABI addition. Normal chat
without `active_task_spec` creates no acceptance authority. Supplied task specs
remain caller-authored derived products: acceptance is adapter acceptance, not
an owner Judgement, source truth certification, or provider delivery receipt.

`chat.active_task.accepted.v1` stores the complete existing accepted snapshot,
its canonical JSON SHA-256, and the originating user-message ID. The event
subject is the original conversation. Message IDs are projections, not the
location of authority. Full scope, exact source snapshots, product identities,
versions, goals and predecessor chains are validated while rebuilding history.
Queries paginate by event sequence without the default 500-record truncation.
Exact latest-product replay remains legal; stale products and competing roots
or successors reject before creating the current user row. Identical product
IDs cannot silently acquire changed source roles, attachments or other source
snapshot fields even when the text digest still matches.

Preparation calls custom context builders outside any SQL transaction. The
final `BEGIN IMMEDIATE` serializes independent SQLite connections before the
last authoritative read. Native conversation rows, source/ancestry snapshot,
system prompt and active memory are compared to the prepared inputs. The
current user row (including trace when enabled), legacy import if needed, and
acceptance event commit atomically. The DB lock and transaction are released
before message callbacks or provider transport. An enclosing caller-owned SQL
transaction is rejected before effects: releasing a savepoint cannot promise
an acceptance that its caller could still roll back after transport.

Graph/knowledge context and attachment-file bytes are frozen derived output
from preparation. This does not promise global currentness of independently
mutable stores or external files. The stored task source snapshot contains
attachment references, not an archive of external attachment bytes. Later
provider failure, auth failure, callback exception, or interrupted completion
does not undo the committed acceptance or claim an external outcome.

## Legacy compatibility and projections

Preview remains read-only; it never writes migration records. Before the first
durable acceptance in a conversation, visible top-level `metadata.active_task`
across all native statuses is validated as complete, consistent legacy chains.
The same transaction writes `chat.active_task.legacy_observed.v1` records with
`legacy_retention_observed` evidence, then `chat.active_task.baseline.v1` with
the count and boundary `visible_top_level_metadata_all_statuses_at_upgrade`.
Their EventLog times are observation times. No historical commit timestamps,
owner confirmation, or successful provider delivery are invented. Failure
rolls back imported records, marker, current user row, and new acceptance.
Already hidden or moved legacy metadata cannot be retroactively recovered by
this boundary; conflicting or foreign-scope evidence is reported explicitly.

After the marker, missing/moved metadata cannot erase authority, and injected
metadata on unknown rows cannot add authority. A present differing projection
on a known originating row rejects explicitly as observable divergence,
preserving the earlier fail-closed contract; it cannot choose another head.
Existing callback recovery preserves conflicting metadata while restoring its
inspectable accepted projection. EventLog is append-only through its API, not
SQL-tamper-proof; direct database deletion is outside these guarantees.

Accepting-message moves and source-message moves are different operations.
Moving the accepting message cannot reset its original conversation's head.
Moving an old source outside the current conversation does not make it a
current binding: those still require an active native row in the current
conversation. An explicit successor may bind a new source row/ref; the old
ancestor snapshot is preserved but its moved row is neither pulled into nor
suppressed from current history. In-scope source text changes still require
explicit re-binding before history replacement.

## Verification handoff

Production reviewed independently by `design_audit` and `retrieval_graph`.
Independent counterexamples authored by `design_audit` are in
`loom/tests/test_chat_active_task_durable_audit.cpp` (six cases): malformed
journal entry; more than 500 acceptance events; rollback of legacy migration
at both marker and acceptance insertion; post-baseline injected metadata;
source identity changes despite equal text digest; explicit fresh binding
after an inherited source moves, including negative cross-conversation binding.

`loom/tests/test_chat_active_task_durability.cpp` adds eight cases: metadata/row
relocation and reopen; auth failure with tracing off; caller-owned transaction;
acceptance-insert rollback; another engine's callback observing committed
history and accepting a successor; competing successors on two independently
opened SQLite connections synchronized before final validation; history changed
by another connection during preparation; abrupt child-process exit immediately
before acceptance insert and after commit before callbacks. The crash test is
POSIX-only and uses `_exit`, without destructors, then reopens from the parent.

`git diff --check` passed. No native build or test execution was performed in
this lane: the coordinator explicitly owns a single shared post-freeze build.
The new tests are unexecuted evidence specifications until that gate runs.
No existing tests were weakened. This receipt does not sum or replace historical
79/83/84-test reports and makes no paid-model or real-data quality claim.
