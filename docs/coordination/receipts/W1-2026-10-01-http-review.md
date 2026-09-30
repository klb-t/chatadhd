# W1 — independent ABI and HTTP validation review

This receipt records source review by a separate agent from the agents writing
and running the boundary tests. It does not itself report execution results.
The review scope is the deterministic native acceptance mechanism with synthetic
sources and a loopback provider, not extraction fidelity or model quality.

## Design review before implementation

Read the W1 request, native request contract and overnight addendum, existing
`loom/tests/compat/test_chat_active_task_http.py`, C ABI chat/message wrappers,
native message edit/version semantics, and native acceptance/history code.
The existing integration test covers the first specification, explicit append,
trace opt-out and invalid-hash rejection; it does not establish successive
revision or runtime-restart behavior through this boundary.

The following checks were sent to the implementing agents before reviewing their
new code:

- Restart must close the native context and initialize another context over the
  same data directory; a public HTTP facade restart must terminate the old server
  process and start a new process. Reusing an existing runtime is insufficient.
- HTTP 503 must be an actual captured loopback response. Acceptance retained
  after a provider failure must be identified from persisted row IDs, since an
  error result need not include the new user-message ID.
- Validation rejection must leave both the provider-call count and all-status
  message rows unchanged. Active-only queries can hide mutations to history.
- Native editing creates a new active message ID in the same version group,
  retaining the old row. A positive rebind therefore needs the new ID, new byte
  hash and matching quote; otherwise a hash/quote precheck can mask the intended
  version-group behavior.
- Stale-version and competing-root checks must use otherwise valid bindings,
  so they do not accidentally test an earlier source-identity error.
- A rejected-source marker must be specific to the covered source. Returning
  the same marker in every later assistant response makes the absence test
  misleading when unrelated responses correctly remain in history.
- Compare actual outbound message arrays with persisted context traces when
  tracing is enabled. Scope contamination claims to the history channel being
  tested; memory and knowledge channels are independent controls.
- In the HTTP/SSE facade, prevalidation failure must have no start event, while
  provider failure follows a start event and leaves an accepted user row.

## Expanded-source review

The expanded ABI test adds three scenarios while retaining the existing test:
three selective successive versions and latest replay; native edit/rebind with
an unbound active sibling and stale-hash rejection; trace-off HTTP 503 acceptance
followed by native runtime restart, exact replay and a successor. Its independently
constructed expected message array includes unbound accepting turns and excludes
the exact ancestor coverage. Distinct provider replies avoid the marker collision
described above. Rejections compare complete all-status rows and provider counts.
Source review found no blocking coverage defect in the frozen implementation.

The new `loom/server/tests/test_chat_active_task_server.py` exercises the public
`/api/chat` facade and actual server subprocess, with additive conditional CTest
registration. It distinguishes SSE `error` without `start` on validation failure
from `start` followed by `error` after a real provider HTTP 503. The old process
is terminated and waited for; a new process opens the same directory and must
observe exactly equal saved rows before further requests. Source review found
no blocking defect. The reviewer requested a stronger exact assertion on the
failed v3 outbound array, matching start/error request IDs and accepted user-row
role/text/bindings; final disposition is recorded below.

Both requested changes were applied and inspected: the server test now checks
the exact two-message failed v3 array and acceptance fields/request ID; the new
ABI fixture retains its synthetic database directory and reports its location.
The historical original ABI test is unchanged.

Final source review passes for these SHA-256 hashes:

| File | SHA-256 |
|---|---|
| `loom/tests/compat/test_chat_active_task_http.py` | `4fd63aa16d0fc06aaabd260ecf7d1c996d10c4b688cc1495401ff0f5ab446a5c` |
| `loom/server/tests/test_chat_active_task_server.py` | `4b140e97dbb57f6f06ad7efd9d8aa4a0f9e89d949414d21fb054768454157802` |
| `loom/CMakeLists.txt` | `a5dbf2e207bf9aa06cb24c1396f82c95c9a3c6d38720b7bc0e3f51a1f2d53fe8` |

Execution evidence belongs to the coordinating agent's separate verification
receipt; this reviewer did not run or build these tests. This increment closes
boundary-test coverage gaps. It does not fix a newly demonstrated production
bug, establish model quality, or implement a separate durable acceptance store.
Arbitrary metadata changes after acceptance and coordination across separate
engines/database instances remain the previously documented storage boundary.
