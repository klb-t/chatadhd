# W1 — durable acceptance boundary: reproduced failures and additive design

Date: 2026-10-01. Independent acceptance-audit agent. This is an executed
boundary audit and a proposed implementation, **not a durable-store patch**.
Production and test sources were read only; no build, commit or publication was
performed by this agent. The coordinator owns integration and execution gates.

Inspected local HEAD: `57007d9a69d0cef658f6737aebbb78184c830cd6`.
Inspected `loom/src/chat/chat_engine.cpp` SHA-256:
`1c20e43f5b98dd2a233f60705b9344cdf5a30d414ce6751bcc348a822ced8c81`.
Inspected `loom/include/loom/chat_engine.h` SHA-256:
`b03ba4dcf19a43aa0f62f1ba00bc745f6597aae0ac9bf2482118a6e5264e4883`.

## Observed implementation

`prepare_active_task` enumerates `Database::get_msgs(conv_id, true)` and reads
top-level `metadata.active_task`. It overlays `ChatEngine::active_task_in_flight_`
while that engine is sending. Thus excluded/version/deleted **statuses** are
already included; changing status alone is not the missing-authority failure.
The map ends at send return or exception and is not shared by another engine.

`Database::update_msg` permits replacing metadata and changing `conv_id`.
Both operations are public through `loom_update_message`; the HTTP server's
`PATCH /api/messages/:id` uses the same update path. Existing callback recovery
does not protect arbitrary later edits. A retained malformed top-level task
fails closed, but an absent key is indistinguishable from no historical task.
Moving the accepting row removes it from the original conversation's scan.

One `Database` recursive mutex coordinates its users, including multiple engines
which share that exact Database object. Separate Database objects/connections
have separate mutexes. `send` currently has no SQLite transaction spanning the
read/validate/create sequence: `create_msg` begins its own transaction only
after preparation. Source inspection therefore predicts a two-connection race
in which both clients validate the same head before either creates its row.
That interleaving was **not executed** in this audit.

## Executed C ABI counterexamples

The existing shared library was loaded using Python `ctypes`; no recompilation.
Library: `/workspace/scratch/23947acf329f/verification/build/libloom.so.0.1.0`.
SHA-256: `0916b6bb82a497bd1038501f85c5f1d73fce94857ff07a6727a814211d7c7496`.
No API key or transport was installed; `start_workers=false`, `auto_title=false`,
`semantic_analysis=false`, memory/graph off, stream off, trace off. Authentication
fails after acceptance, allowing the persistence boundary to be tested with
**zero provider calls, zero remote calls, zero paid inference**. The log's
`GraphEngine started (LLM=yes)` is configuration availability, not an executed call.

Each scenario used a fresh synthetic directory and these public calls:

1. `loom_init_ex`, `loom_set_config_json`, `loom_create_conversation`.
2. `loom_chat_ex` with ordinary synthetic source text. Assert `auth` and one
   retained user source row. Record its complete JSON and text SHA-256.
3. Send valid version-one product `p-a`, scope `(conversation, native:active,
   report)`, goal `report`, binding its one history event to that source.
   Statement: `Use report format.` The supplied compiled placeholder
   `UNTRUSTED` has a valid nine-byte source span and is rebuilt by the compiler.
   Assert `auth` and a second user row with `metadata.active_task` for `p-a`.
4. Change only `product_ref.id` to `p-competing-root`; retain version 1/null
   predecessor. Assert `invalid_argument` and no new message.
5. Apply the scenario mutation with `loom_update_message`. Preserve the entire
   original acceptance value, as described below.
6. `loom_shutdown`, then `loom_init_ex` on the same directory and repeat step 4.
   Assert `auth`, one new row, and top-level retained product
   `p-competing-root`. This is acceptance followed by authentication failure,
   **not** successful provider delivery. Assert the original source row is
   byte-equivalent as JSON to its pre-mutation value.

| Scenario | Non-destructive mutation | Before mutation | After runtime reopen |
|---|---|---|---|
| Metadata relocation | Replace metadata with `{preserved_original_metadata: old_metadata, client_annotation: "synthetic relayout"}`; exact old metadata remains nested | Competing root rejected; zero rows added | Competing root accepted; one row added; then auth error |
| Accepting-row move | Create a second synthetic conversation and patch only the accepting row's `conv_id`; all metadata remains unchanged | Competing root rejected; zero rows added | Competing root accepted in original conversation; one row added; then auth error |

**2/2 specified failure scenarios reproduced.** This is a negative boundary
measurement, not two passing correctness cases. Runtime destruction/recreation
was exercised; an operating-system process crash was not.

Synthetic databases remain at
`/workspace/scratch/23947acf329f/w1-acceptance-audit-jm6w6gld/` in
`metadata_relayout/` and `move_accepting_row/`. No files, rows, branches or
histories were deleted. Original acceptance metadata and source rows remain.
An earlier setup attempt at
`/workspace/scratch/23947acf329f/w1-acceptance-audit-zhup7tv3/` failed before
acceptance because an empty compiled placeholder violated the contract; it is
also preserved. It is not counted as a reproduced scenario.

### Preserved standalone replay

The exact standalone [reproduction script](W1-2026-10-01-acceptance-audit/reproduce.py)
was subsequently executed once, successfully repeating these **same two**
scenarios. This is not four distinct examples. Its exit status was 0 because
the expected **known failures reproduced**, not because durability is correct.
It remains outside CTest and normal correctness gates.

[Machine-readable results](W1-2026-10-01-acceptance-audit/results.json),
[raw stdout](W1-2026-10-01-acceptance-audit/stdout.txt), and
[raw stderr](W1-2026-10-01-acceptance-audit/stderr.txt) retain actual error codes,
message IDs, row counts, preserved-source assertions and binary/script hashes.
The repeated audit used the same library SHA-256 recorded above. Its synthetic
data remains at `/workspace/scratch/23947acf329f/w1-acceptance-audit-9njhp9_n/`.
Provider/remote calls remain zero across both audit executions.

From the repository root, use a fresh output filename on each execution:

```sh
LOOM_LIBRARY=/absolute/path/to/libloom.so python3 \
  docs/coordination/receipts/W1-2026-10-01-acceptance-audit/reproduce.py \
  --results /absolute/path/to/new-results.json \
  --data-parent /absolute/path/to/retained-synthetic-data
```

The script creates and retains unique `mkdtemp` data directories, closes every
runtime, refuses to overwrite an existing results file, and performs no cleanup
or deletion. It installs no key/provider and does not call external services.

## Existing universal persistence primitives

| Primitive | Actual contract | Role in a durable design |
|---|---|---|
| `BlobStore` / `SourceRecord` | Content-addressed immutable bytes; original source representation | Optional larger snapshot backing; not a head selector |
| `ArtifactRecord` / `model::Product` | Materialized output with dependencies/checks, separate from source | Rebuildable representation of compiled instruction and provenance |
| `KnowledgeStore::put_products` | Upserts by `(run_id,id)`; `clear_run` clears derived product rows | Useful projection; cannot alone be acceptance authority |
| `model::Judgement` | Owner judgement, append-only and replayed last | Use only for actual owner judgements; `explicit_caller_supplied` must not become owner confirmation |
| `EventLog` / `loom_events` | Existing append-only event API, monotonically increasing sequence, arbitrary type and JSON payload | Durable record that an adapter accepted a particular product; source for a derived acceptance index |
| `sql::Txn` | `BEGIN IMMEDIATE` by default; nested calls become savepoints | Serialize writers and atomically persist acceptance plus user row |

References: `loom/include/loom/{provenance,knowledge_store,model,sqlite}.h`,
`loom/src/core/provenance.cpp`, `loom/src/kb/store.cpp`,
`loom/src/db/{db,sqlite}.cpp`. `loom_events` already exists in the core Loom
schema, with indices on type and subject. `EventLog::query` defaults to 500 rows;
an authority reader must page through **all** relevant rows by `after_seq`.

## Recommended first implementation — existing EventLog, no new schema

Proposal: a chat-owned adapter over the existing universal event store, with an
open event type such as `chat.active_task.accepted.v1`. Its subject is the
original conversation ID, not the current location of the accepting message.
This is an event about accepting a derived product, not a new knowledge model,
not a second source of truth for the original conversation, and not a new
closed-set enum. The event payload is self-contained:

```json
{
  "schema": "loom.chat_active_task_acceptance/1",
  "acceptance": "explicit_caller_supplied",
  "originating_message_id": "m_...",
  "scope": {"conversation_id": "c_...", "branch_id": "native:active", "task_id": "report"},
  "goal_id": "report",
  "product_ref": {"kind": "product", "id": "p-..."},
  "version": 1,
  "previous_product_ref": null,
  "accepted_snapshot": {"schema": "loom.chat_active_task/1"},
  "snapshot_sha256": "sha256 of canonical accepted_snapshot JSON"
}
```

The abbreviated `accepted_snapshot` above means the **complete existing
snapshot**, including supplied/compiled specs, renderer version, current and
inherited source snapshots, bindings and history mode. Do not store only a
message ID or a hash of content that can later disappear. Attachment references
still do not preserve external attachment bytes; say so explicitly.

Distinguish three things:

- **Durable snapshot:** exact accepted content and native source evidence.
- **Authoritative acceptance history:** successful adapter acceptance events,
  independent of mutable message metadata and provider outcomes.
- **Acceptance index:** a deterministic projection of those events by full
  scope, product identity and version. Rebuild it from events. A cached latest
  product is an optimization, never the only authority.

The `model::Product` representation may subsequently point to an Artifact
derived from the accepted snapshot. Its run can be cleared/rebuilt without
erasing the acceptance history. Preserve caller product identity alongside any
content-derived native Product ID; do not pretend the caller's arbitrary ID is
already a validated knowledge-layer Product identity.

### Transaction and callback boundary

1. Build a tentative request, allowing the custom context builder before the
   final write transaction. Keep its exact task snapshot for comparison.
2. Acquire the Database lock and begin `sql::Txn(db.conn(), true)` **before**
   the final authoritative read. This obtains the SQLite writer reservation
   across distinct connections, not just a C++ object mutex.
3. Read all acceptance events for the scope, validate/fold the exact chain,
   and recheck current native sources and request inputs. Reject a changed
   snapshot; do not silently substitute new history after compiling the array.
   History/memory composition must also be stable or explicitly revalidated:
   protecting only the task digest does not prove the whole array is fresh.
4. Create the current user row, append its complete acceptance event, and commit
   the outer transaction. `create_msg` uses a nested savepoint here. Any storage
   error rolls back both operations; no callback or provider call occurs.
5. Release locks, then run message callbacks and transport. The event is already
   committed, so another engine can reject a competing root immediately even
   if a callback relocates metadata or moves the originating row.
6. Keep metadata as an inspectable projection and preserve callback differences.
   A current-turn mutation still stops transport. Provider failure or callback
   exception never rewrites the committed acceptance into non-acceptance.

Do not hold a write transaction across arbitrary callbacks or HTTP. Also do
not promise durability when `send` is invoked inside an externally owned
transaction: a nested savepoint release is not a database commit. The first
patch must explicitly handle that case (for example, reject active-task
acceptance before effects with a documented transaction-boundary error), or
define a separate deferred-acceptance capability. It must not fire transport
while an enclosing caller can still roll back the acceptance.

Potential orphan BlobStore files are a separate concern if blobs are introduced:
write immutable bytes before committing references, preserve an unreferenced
blob on failure, and do not call a file rename plus SQLite commit one atomic
filesystem/database transaction. Embedding the initial snapshot in event JSON
avoids needing BlobStore constructor/runtime wiring for the bounded first patch.

### Revisions, replay and forks

Keep the present contract: full scope identifies a task; a successor names the
latest product, increments version exactly, and preserves goal. An identical
latest product may be accepted for a new current follow-up and gets a distinct
acceptance event; its specification/bindings identity must agree. Replaying an
older product after a successor remains rejected. Two concurrent successors
cannot both extend the same head; the second transaction reads the first's
committed successor and rejects before creating its current row.

Do not silently interpret a move, metadata edit or missing source row as a
fork or task reset. A future explicit fork uses the existing model's Fork and
scope/version semantics, preserving both branches; it is a separate interface
extension. An arbitrary caller-authored spec is not a verified owner decision.

### Upgrade and ownership

This first option needs no database DDL or ABI addition: W1 can own an internal
`src/chat/active_task_acceptance.*`, call `EventLog(db)` through its existing
constructor, and change `send` plus new `test_chat_*` suites. Keep shared STATE
and foundation files unchanged in this lane; integrator receives this receipt.

Existing metadata-only acceptances require an explicit compatibility import.
Before the first durable acceptance for a legacy conversation, scan all statuses,
validate complete chains and snapshot consistency, and atomically append
`legacy_retention_observed` records plus a completed-baseline marker. Their
observation time is upgrade time: do not fabricate original commit timestamps,
provider delivery, or a user-confirmation event. On conflict/incomplete ancestry,
report the actual condition without deleting, rewriting, or choosing a winner.

After that baseline, durable records govern ancestry; subsequent arbitrary
message metadata cannot add/delete authoritative products. Preserve mismatching
metadata as observable projection divergence. Unchanged legacy copies deduplicate
against durable identity. A failed baseline transaction must leave no marker.
The importer cannot recover an already hidden or moved legacy acceptance that
is absent from its scan. Mark the recovered evidence boundary explicitly; do
not promise retroactive completeness. A global search of arbitrary nested JSON
would not establish acceptance and is not a safe repair.

If full event scans prove too costly, a later foundation-reviewed forward-only
migration can add a generic product-acceptance index with unique full-scope
version and immutable payload digest, rebuilt from event sequence. That requires
schema version/migration tests and an integrator STATE note. Do not introduce
that index, a new Judgement verdict, or a replacement Product store silently in
W1 to hide the current limitation.

## Counterexample gates for the implementation

1. Re-run both executed scenarios: new root must reject after relocation/move
   and reopen, while originals remain inspectable and current-turn editing
   still works. Trace-off and auth/provider failure remain accepted history.
2. Two engines over one Database; then two Database objects over one file:
   force both tentative preparations to the same head and release together.
   Exactly one distinct successor persists. Check losing-row absence and no
   provider call for the loser, without timing-only synchronization.
3. Across separate processes, kill after user-row creation but before event
   insertion/commit; reopen and require either both committed or neither.
   Kill after commit but before callbacks; require durable accepted history
   with provider outcome unknown. Do not infer exactly-once external effects.
4. Callback uses a second engine to relocate metadata and submit a competing
   root; accepted event must block it before provider transport. A valid nested
   successor remains allowed, and completion order does not change ancestry.
5. Exercise more than one EventQuery page, all message statuses, unrelated
   scopes, exact latest replay, conflicting product identity, malformed event
   payload, legacy imports, duplicate legacy rows, missing ancestor and failed
   baseline marker commit. Fail closed on corrupt authoritative records.
6. Preview remains read-only: no acceptance event or compatibility migration.
   Ordinary chat creates no active-task authority. Enclosing transactions get
   the declared behavior; no partial event/row or early network side effect.
7. Rebuild knowledge products and confirm acceptance authority survives.
   An immutable-source snapshot check must remain independent of whether the
   original native row still exists or has been moved by its owner.

These are recommended future gates, **not executed test claims**. Immediate
next action: implement the EventLog-backed acceptance path and its scoped
compatibility import, then independently review the transaction boundary before
advertising restart/separate-engine durability. Queue item 3 has an executed
audit and concrete design; its implementation remains open.
