# Native GraphPacket and graph reply API

## Delivered interface

`loom_packet(ctx, request_json)` returns caller-freed JSON. It performs local
projection operations. `POST /api/packet` exposes the same command without
reserializing the HTTP request. Literal NUL bytes are rejected before the
C-string ABI boundary; escaped JSON NULs are decoded and captured exactly. Existing bearer-token middleware applies.
No provider calls, core/KB schema migration or second graph store are introduced.
PR9's `/api/graph/packets/store` now checks the full packet and backwards/forwards
history before accepting selected rows. Its CAS, owner judgements, source bytes
and immutable historical profile receipts are preserved.

Commands use `operation` plus the fields below. Unknown graph definition kinds,
entity kinds, predicates, tasks/attrs and descriptive operation intents remain
data. Execution envelopes and versioned native DTOs are checked; preserved
unknown native fields are not silently discarded by a lossy DTO conversion.

| Operation | Input | Result |
|---|---|---|
| `capabilities` | No packet | Operations, schemas, resource/number capabilities |
| `make` | `origin`; optional collections, `task`, `known_at` | Hash-bound new packet with per-record provenance |
| `validate` | `packet` | Validated packet, including backwards/forwards history replay |
| `encode` | `packet` | Canonical JSON string, Python-compatible hashing |
| `decode` | `raw` or `raw_base64` | Strictly parsed and validated packet |
| `empty_diff` | `packet`, `proposal_id`, `origin`; optional `known_at` | Versioned diff proposal |
| `preview` | `packet`, `diff` | Candidate, changes and annotations; no acceptance/write |
| `apply` | `packet`, `diff`, `policy`; optional `explicitly_accepted` | `{packet, receipt}`; preview/auto are caller policy |
| `invert` | Current `packet`, `receipt` | Exact earlier projection, only at the receipt head |
| `capture` | `raw` or `raw_base64` | Exact-byte hash/length/base64 capture |
| `compile_reply` | `packet`, `raw` or `raw_base64`, `host` | Deterministic `loom.graph_reply_compilation/1` |
| `validate_compilation` | `packet`, `compilation` | Full recompilation from captured bytes/host inputs |
| `apply_compiled_reply` | `packet`, `compilation`, `policy`; optional acceptance | Validated append-only reply applied through the same algebra |
| `reply_fragment` | Base `packet`, `compilation`, `address` | Verified fragment text, local/native IDs, exact span, source locator and model-content origin |

`host` requires `request_id`, `turn_id`, `model`. Defaults: `actor =
graph_reply_model`, null `recipe_sha256`, `parent_turn_id`, `known_at`.
A parent must identify an existing `conversation_turn` entity. Retry attempts
use distinct host turn IDs. Local model IDs are namespaced deterministically;
unknown context links, duplicate/disconnected/cyclic nodes and mismatched
partitions fail. Reply v2 permits null composite text, never null leaf text.
Ordered leaves render exact text; UTF-8 byte and Unicode code-point ranges are
computed by the compiler, not trusted from model output.

`address` has exactly one member: `{local_id: "model-local-id"}` or
`{node_id: "native-entity-id"}`. Fragment lookup first recompiles the captured
reply against its original base packet; an edited compilation, stale base or
unknown address returns an explicit error. `span` contains `byte_start`,
`byte_len`, `char_start`, `char_len`, local `parent_id`, and `ordinal`.
Character offsets count Unicode code points, not UTF-16 units or graphemes.
`source_locator` addresses the same UTF-8 subspan in the retained display source.
This operation provides addressing for expand/correct commands; executing those
model requests belongs to chat. It performs no provider call or canonical write.

Every emitted reply node/source records `model_origin.kind = model`, including
the host-declared model, optional recipe hash and captured-response hash.
The compiler preserves supplied bytes; it cannot establish that the caller
supplied the provider's first response. The diff's system origin
records the deterministic compiler transformation; structural claims describe
containment, without establishing model content as true. Chat must retain both
origins and use the model-content origin for response annotations.

`policy` is the existing data envelope:
`{schema: loom.graph_packet_apply_policy/1, acceptance: preview|auto,
allow_source_tombstones: boolean}`. Removing a source projection keeps its
previous bytes/provenance in history. It does not erase an immutable source.
Source mutation and entity/claim identity changes require new record identities.
No operation equates acceptance with content truth. Structural claims carry
`confidence_scope: structure_only`; model semantic links remain draft attrs.
An internally replayable history establishes consistency, not source authenticity.

`resource_limits` optionally supplies `max_nodes`, `max_depth`,
`max_string_bytes`, `max_integer_bits`; null/unset means no codec policy ceiling.
Explicit limits are nonnegative integers, including zero: an exhausted budget
returns a configured-limit verdict rather than rejecting the setting itself.
Limits apply separately to decoded inputs, generated candidates/previews and
successful `packet::execute` result JSON, including retained history and codec
output envelopes. They do not bound C ABI usage/accounting wrapper metadata.
`max_string_bytes` counts UTF-8 object keys plus string values; `max_nodes`
counts values plus object keys; root depth is zero. Failure still retains the
first-byte capture, even when that capture exceeds a requested output limit.
The native JSON representation supports signed/unsigned 64-bit integers and
finite binary64 numbers. Overflowing integer literals are rejected with a
capability error rather than rounded. Some native DTO fields use narrower
integer/double representations; a lossless roundtrip rejects field overflow
or rounding. These representation boundaries are distinct from configurable
resource policy. Invalid UTF-8, duplicate JSON keys,
BOM-prefixed JSON and nonfinite numbers fail. `raw_base64` allows actual invalid
response bytes to be captured before their UTF-8/JSON validation fails.
Error replies retain the first response capture; they do not repair/retry it.
Parser/allocator exhaustion remains a runtime capability boundary, not a new
fixed product limit. Standard timezone-aware ISO date/time spellings are
supported; Python's broader `fromisoformat` lexical acceptance is not claimed.

New store receipts mark `reversible_history_validation` as
`native_codec_backwards_and_forwards_history_replay`. Read/replay return old
immutable receipts with their original validation markers; acceptance retries
do so only after the request passes the current full codec validation. They do
not claim that an earlier version performed this newly added check. No database
schema or receipt schema version changes are needed for this marker update.

## Shared usage policy dependency

The packet C ABI accepts an optional `usage_estimate` matching thread 2's
`UsagePolicy` API. When its header/implementation are integrated, admission is
recorded before execution; an unauthorized decision returns
`{usage_decision, executed:false}`. The estimate binds the exact packet command
hash, so a reused operation ID cannot authorize different packet content.
Confirmation happens through thread 2's API; retry the same packet request with
the same operation ID after confirmation. Successful execution returns
`{result, usage_decision, usage_settlement, executed:true}`.

Admission and accounting remain thread 2's responsibility. A packet-owned
`loom_packet_dispatch_v1` table in the same `usage-policy.sqlite` records only
the execution claim, hash-bound output and original measurements. It uses the
shared configurable ledger timeout and leaves W2 table definitions and its
ledger schema version unchanged. Usage rows still change through W2's APIs.
An atomic claim precedes execution; held requests have no claim. States are
`claimed -> result_ready -> finished`. Exact duplicates return a cached result
with `executed:false, replayed:true`; `usage_current_decision` is separate from
the historical decision accompanying that result. A failed settlement leaves
`result_ready`: retry reconciles the saved measurements, preserving unknowns,
without running the packet again. A claimed receipt with no persisted outcome
returns `dispatch_status: pending_or_interrupted`; it is never silently retried.

Pre-upgrade unresolved accounting with no execution receipt returns
`legacy_unresolved_indeterminate`, preserving its reservation. An old `allowed`
record with empty actuals cannot distinguish a confirmed first execution from
a historical execution whose result was lost before accounting. The new durable
claim protects new operations; it does not reconstruct that missing evidence.
Use a fresh operation ID for a deliberately new execution rather than treating
an indeterminate old operation as proven unexecuted. Shared admission is a
committed snapshot: external cancellation after admission does not abort
already admitted packet work. An atomic cancellation/dispatch contract requires
a future thread-2 API; no private usage-table coupling is introduced here.

Only requested dimensions that this operation can measure are settled:
`input_bytes`, `output_bytes`, `wall_seconds`, `calls`, `money_usd` (zero).
Other resource quantities stay unknown/reserved. Failure cancels its reservation;
a settlement failure is exposed, never disguised as successful accounting.
Estimates/cohorts are caller data; no invented universal packet spending ceiling
or estimated CPU/memory accuracy is claimed. Calls without `usage_estimate`
retain the plain projection interface. This is an explicit caller guard, not
proof that every application operation passes through a universal guard.
On current main, an explicit usage estimate fails `unavailable` until thread 2
is integrated, rather than bypassing the requested policy.


## Method/version/run graph data

[METHOD_GRAPH.md](METHOD_GRAPH.md) defines `loom.method_graph/1` and
`loom.method_run_trace/1` as versioned caller data patterns. Existing packet
operations preserve method/version/parameters/prompt/recipe/preset/combination/run records
and real result-to-version/run/compiler Claims. Parameter-set versions are
addressable Entities, with version/run-to-parameters and run-to-combination
Claims. Exact captured definitions accompany the hashes. No algorithm-name dispatch or
closed domain taxonomy is added. The synthetic native C ABI/store regression
covers exact captured bytes, hashes, effective user settings, full history and
separate dated, unmeasured model assessments. Thread 3 must adopt this pattern
in its actual method registry; that cross-lane execution adapter is still open.

## Offline checks

The standard build discovers `tests/test_packet.cpp` and
`tests/compat/test_packet.py` through the existing CMake globs.

```sh
cd loom
cmake --preset dev -DLOOM_BUILD_SERVER=ON -DLOOM_USE_SYSTEM_SQLITE=OFF
cmake --build --preset dev
ctest --preset dev
python src/packet/tests/test_http.py build/dev/server/loom-server
python src/packet/tests/reproduce_policy.py
python src/packet/tests/audit_regressions.py build/dev/libloom.so --expect rejected
```

When the environment cannot retain all debug link data in memory, configure
`-DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-keep-memory` and
`-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--no-keep-memory`, then build with `-j 1`.
The follow-up receipt records this local GNU ld setting; no repository build
preset or quality gate was changed. For pre-fix reproduction, build commit
`3caa6b4` in a separate worktree and run the current `audit_regressions.py`
against that library with `--expect accepted`. That commit remains in this
branch's history; the archived before observations are transcript evidence,
not a claimed rerun of the final regression suite on that older commit.

The policy check requires the pinned thread-2 commit in local Git objects;
fetch `gpt/usage-policy-2026-10-04` first if it is unavailable. It compiles an
isolated executable without modifying other lanes. All fixture responses are
synthetic pinned outputs of the Python compiler in
`gpt/graph-replies-2026-10-02`, source SHA-256
`50fffaffbf1abcbb2de1b626ae51f270a5842cd3e023c1dc3cee8011a401a903`.
It does not load the historical reply branch wholesale.
