# W1 — independent durable-acceptance source review

Date: 2026-10-01 Europe/Amsterdam. Reviewer: separate `durable_review`
agent. Review target: the uncommitted durable-acceptance increment based on
local W1 HEAD `13f7e0be9233d2a178ffe22bf117e4694e0504e2`.

This is a source/design review only. I did not build the code, execute tests,
invoke a model, read sealed data, commit, or publish. Execution evidence and
the final committed SHA belong to the coordinator.

## Design challenges sent before implementation

The review required the implementation and counterexample agents to address:

- complete `EventLog` pagination rather than the default 500-row page;
- selection and ancestry by exact full scope, with unrelated tasks isolated;
- an atomic upgrade-time baseline whose marker cannot survive a partial import;
- strict rejection of malformed, conflicting, out-of-order, or unknown future
  authority events;
- one SQLite writer reservation across final reads, user-row creation, and the
  acceptance event, including separate `Database` connections;
- rejection before effects when an external transaction already owns the
  connection, because releasing a nested savepoint is not a durable commit;
- callbacks and provider transport only after the durable commit;
- replay without duplicate-product ambiguity, and current-source checks that
  do not make immutable accepted evidence depend on a moved original row;
- exact revalidation of ordinary native history after tentative request
  composition. Rechecking only the task snapshot would permit a stale outbound
  array after a write through another connection.

The first drafts exposed concrete problems: two exact event queries could
silently ignore a future authority type; the baseline marker was not bound to
event chronology or all recovered scopes; inherited evidence was only partly
validated; a moved inherited source was rejected despite its durable snapshot;
JSON `null` attachments from the supported import path were rejected; and a
valid task snapshot copied onto an assistant row could be imported as legacy
caller acceptance. Each issue was reported before the final source freeze.

## Final inspected source

| File | SHA-256 |
|---|---|
| `loom/src/chat/active_task_acceptance.cpp` | `4e13cf3142200068038854208ed5fd3ab7b4e9a360ce9d0195f9e17ea6f6241a` |
| `loom/src/chat/active_task_acceptance.h` | `04c4f5dbdff26931e2d27996d37e39322c74e03d2b1e8553032d961f2890bfc9` |
| `loom/src/chat/chat_engine.cpp` | `a85540b324e3216117c6ef9929eebd89e9ab68f3f25b92788cfc273de46257b3` |
| `loom/tests/test_chat_active_task_durable.cpp` | `1a00e285c8a497a7c5af9727422f2a8126c47b4b13599a6efe8088e6ef9b2a00` |
| `loom/tests/compat/test_chat_active_task_durable.py` | `0d734e24bffd1d54b5ff42185c3dacd6458c3b14b289a0bd42cf5871747ada6e` |

No blocking source defect remained in this frozen set.

The final implementation reads the entire ordered
`chat.active_task.*` namespace by `after_seq`, rejects unknown types and exact
schema drift, validates canonical snapshot hashes, full per-scope chains and
reconstructed inherited evidence, and treats the single baseline marker as an
upgrade-time observation boundary. Legacy observations must precede the marker;
new caller acceptances must follow it. Marker counts, unique originating row
IDs and the last legacy sequence are checked. Baseline import scans every
message status and fails atomically for malformed chains, conflicting products,
or a non-user origin row.

`ChatEngine::send` rejects active-task acceptance in an already open external
transaction. It tentatively composes the request, captures the ordered native
history fields that affect that array, then takes `BEGIN IMMEDIATE`, imports or
loads durable authority, recomputes the task snapshot, and compares the current
history projection. It creates the user row and appends the self-contained
acceptance event within that outer transaction. `create_msg` therefore uses a
nested savepoint, while the outer commit remains the durability boundary.
Callbacks and HTTP occur only afterward. A moved inherited source no longer
invalidates its immutable accepted bytes; an active edited sibling still needs
an explicit current binding.

The inspected tests contain source-level gates for restart and metadata/row
relocation, exact replay and stale roots, separate engines and connections,
more than one event page, baseline import, null attachments, moved inherited
sources, malformed/non-user legacy state, native-history staleness, event-write
rollback, and externally owned transactions. This inventory is not a claim
that those tests executed or passed.

## Remaining evidence boundary

The durable event establishes that the adapter accepted the exact task snapshot
and source evidence. It is not owner judgement, provider delivery, or exactly-once
external execution. Native conversation history is revalidated under the SQLite
writer reservation. Configuration, memory, graph context and attachment-file
bytes are composed and frozen before that transaction, not atomically refreshed
as one cross-store snapshot. Attachment references remain references; this patch
does not preserve the external file bytes. The legacy baseline observes only
top-level metadata still discoverable at upgrade time and cannot recover a row
that had already been hidden, moved, or structurally relocated.
