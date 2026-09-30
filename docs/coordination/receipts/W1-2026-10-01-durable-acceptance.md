# W1 — durable ActiveTask acceptance implementation and verification

Date: 2026-10-01 Europe/Amsterdam. Scope: W1 only.

## Result

Implemented an additive durable acceptance authority over the existing
append-only `EventLog`, without a schema migration or public ABI change.
`ChatEngine::send` now persists the current user row and the complete accepted
task snapshot in one outer `BEGIN IMMEDIATE` transaction. The event, rather
than mutable message metadata, governs later ancestry and replay.

The implementation adds:

- `chat.active_task.accepted.v1` self-contained acceptance events and a single
  `chat.active_task.acceptance_baseline.v1` compatibility marker per original
  conversation;
- atomic, fail-closed import of still-visible legacy top-level task metadata;
- one ordered, fully paginated read of the `chat.active_task.*` namespace;
- strict event, marker, snapshot hash, full-scope chain, origin-row and
  inherited-evidence validation;
- final task/source and ordinary native-history revalidation under the SQLite
  writer reservation;
- serialization across separate `Database` connections and rejection of an
  already externally owned transaction before callbacks or transport;
- durable replay semantics keyed by supplied specification plus bindings,
  including replay with a different history-composition mode;
- preservation of immutable inherited source evidence when its original row is
  later moved, while an active edited sibling in the original conversation
  still requires an explicit binding.

Metadata remains an inspectable projection. Later relocation, replacement,
nulling, or a fabricated maximal version in that projection does not erase or
replace the append-only authority.

## Changed files

- `loom/src/chat/active_task_acceptance.{h,cpp}` (new internal adapter)
- `loom/src/chat/chat_engine.cpp`
- `loom/tests/test_chat_active_task_durable.cpp` (new native suite)
- `loom/tests/compat/test_chat_active_task_durable.py` (new public C ABI and
  local loopback-provider proof)
- `loom/tests/test_chat_active_task.cpp` and
  `loom/tests/test_chat_active_task_retention.cpp` (existing projection tests
  updated to the durable-authority contract)
- `docs/coordination/receipts/W1-2026-10-01-durable-review.md` (independent
  source review)

No file, row, branch, worktree, history, log, or prior checkpoint was deleted.
No shared `docs/STATE.md`, coordination index, integration branch, main branch,
PR6, sealed data, holdout data, or frozen instrument was changed.

## Frozen source hashes tested

| File | SHA-256 |
|---|---|
| `loom/src/chat/active_task_acceptance.h` | `04c4f5dbdff26931e2d27996d37e39322c74e03d2b1e8553032d961f2890bfc9` |
| `loom/src/chat/active_task_acceptance.cpp` | `4e13cf3142200068038854208ed5fd3ab7b4e9a360ce9d0195f9e17ea6f6241a` |
| `loom/src/chat/chat_engine.cpp` | `a85540b324e3216117c6ef9929eebd89e9ab68f3f25b92788cfc273de46257b3` |
| `loom/tests/test_chat_active_task_durable.cpp` | `1a00e285c8a497a7c5af9727422f2a8126c47b4b13599a6efe8088e6ef9b2a00` |
| `loom/tests/compat/test_chat_active_task_durable.py` | `0d734e24bffd1d54b5ff42185c3dacd6458c3b14b289a0bd42cf5871747ada6e` |
| `loom/tests/test_chat_active_task.cpp` | `a9134b43c0b6ce239aeae947deb983464ecc35bb2a58095003e6df69535b88ba` |
| `loom/tests/test_chat_active_task_retention.cpp` | `c065a8847f1ed6d9423792be3817d63c95deef403078c2cf6d6901f567929b74` |

The independent review records the same five production/new-test hashes in
`W1-2026-10-01-durable-review.md` and found no remaining authority-layer
correctness blocker after its requested corrections.

## Executed verification

Build:

```text
cmake --build /workspace/scratch/23947acf329f/verification/build -j4
exit 0
```

Focused durable gate after corrections:

```text
unit.test_chat_active_task_durable      Passed
compat.test_chat_active_task_durable    Passed
2/2 CTest entries passed in 5.35 s
```

All ActiveTask boundaries:

```text
unit.test_chat_active_task
unit.test_chat_active_task_concurrency
unit.test_chat_active_task_durable
unit.test_chat_active_task_retention
unit.test_chat_active_task_revisions
compat.test_chat_active_task_durable
compat.test_chat_active_task_http
server.chat_active_task
8/8 CTest entries passed in 20.99 s
```

Full configured regression:

```text
85/85 CTest entries passed
0 failed, 0 skipped, 0 disabled
279.81 s
```

JUnit evidence was written outside the repository at
`/workspace/scratch/23947acf329f/verification/w1-durable-full-ctest.xml`; its
root reports `tests=85`, `failures=0`, `skipped=0`, `disabled=0`.

The new C ABI proof executes two restart scenarios. Each makes exactly three
loopback provider calls (seed, v1, v2); two competing-root attempts per
scenario add zero provider calls, rows, or acceptance events. Total for that
test: six local loopback calls, zero remote/model calls. Native tests use the
scripted in-process transport. No paid inference or external provider was used.

Two early verification runs were informative failures and are not counted as
passes: the first new native run exposed an invalid synthetic `known_at` and an
incorrect expected SQLite error category; the first whole ActiveTask run found
two older tests still asserting mutable metadata as authority. The fixtures and
expectations were corrected, independently reviewed, rebuilt, and the focused,
ActiveTask, and full gates above were then rerun successfully.

## Covered counterexamples

The executable gates cover metadata relocation, accepting-row movement,
runtime reopen, moved inherited source rows, exact and mixed-history replay,
proper successors, stale and competing roots, unrelated scopes, malformed
authority beyond page 500, legacy bootstrap, non-user legacy contamination,
null attachments, event-write rollback, post-capture native-history mutation,
an externally owned transaction, two engines sharing one database, and two
independent connections to one SQLite file.

## Remaining boundary

- A legacy acceptance already moved, hidden inside another metadata object, or
  otherwise absent from its original conversation before the first baseline
  cannot be recovered retrospectively. The marker states this boundary.
- The event preserves accepted task/source JSON and attachment references, not
  the external bytes at attachment paths.
- Preview is read-only and can observe a transient append-only head between
  pages; a real accepting send repeats the authority read under the writer
  transaction before any effect.
- Configuration, memory, graph context, knowledge-context output, and file
  contents are composed before the SQLite transaction. Native conversation
  history is revalidated exactly, but this patch does not claim a cross-store
  freshness transaction.
- An acceptance event proves adapter acceptance, not provider delivery,
  exactly-once external effects, or owner judgement.

## Publication of the tested tree

The approved public W1 branch still pointed to remote commit
`ba792b920fba36fb738ff39421d27b6d965aa11a`, tree
`8dbd887aa8a3e3025df1eb02d679bb1db6ec9f48`, immediately before publication.
The frozen local implementation commit was
`9eff1e8c48d46d5084220f6649b4351cc74b515a`, tree
`8c818cc3deb1068eb34dc8dd7ab6f5d4a70bf956`.

GitHub created remote commit `200fae8ccab3ad50f954d35ea60b9a9148ddc514`
with parent `ba792b920fba36fb738ff39421d27b6d965aa11a` and the exact same tree
`8c818cc3deb1068eb34dc8dd7ab6f5d4a70bf956`. The branch ref was advanced with
`force=false`, then read back at `200fae8ccab3ad50f954d35ea60b9a9148ddc514`.
The local and remote commit SHAs differ because the connector authored a new
commit on the remote history; equality of the complete tree is the content
identity proof.
