# Native GraphPacket and graph reply API

## Delivered interface

`loom_packet(ctx, request_json)` returns caller-freed JSON. It performs local
projection operations. `POST /api/packet` exposes the same command without
reserializing the HTTP request. Existing bearer-token middleware applies.
No provider calls, new database schema or second graph store are introduced.
PR9's `/api/graph/packets/store` and profile receipts remain unchanged.

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

`host` requires `request_id`, `turn_id`, `model`. Defaults: `actor =
graph_reply_model`, null `recipe_sha256`, `parent_turn_id`, `known_at`.
A parent must identify an existing `conversation_turn` entity. Retry attempts
use distinct host turn IDs. Local model IDs are namespaced deterministically;
unknown context links, duplicate/disconnected/cyclic nodes and mismatched
partitions fail. Reply v2 permits null composite text, never null leaf text.
Ordered leaves render exact text; UTF-8 byte and Unicode code-point ranges are
computed by the compiler, not trusted from model output.

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
The native JSON representation supports signed/unsigned 64-bit integers and
finite binary64 numbers. Overflowing integer literals are rejected with a
capability error rather than rounded. Invalid UTF-8, duplicate JSON keys,
BOM-prefixed JSON and nonfinite numbers fail. `raw_base64` allows actual invalid
response bytes to be captured before their UTF-8/JSON validation fails.
Error replies retain the first response capture; they do not repair/retry it.
Parser/allocator exhaustion remains a runtime capability boundary, not a new
fixed product limit. Standard timezone-aware ISO date/time spellings are
supported; Python's broader `fromisoformat` lexical acceptance is not claimed.

## Shared usage policy dependency

The packet C ABI accepts an optional `usage_estimate` matching thread 2's
`UsagePolicy` API. When its header/implementation are integrated, admission is
recorded before execution; an unauthorized decision returns
`{usage_decision, executed:false}`. The estimate binds the exact packet command
hash, so a reused operation ID cannot authorize different packet content.
Confirmation happens through thread 2's API; retry the same packet request with
the same operation ID after confirmation. Successful execution returns
`{result, usage_decision, usage_settlement, executed:true}`.

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
```

The policy check requires the pinned thread-2 commit in local Git objects;
fetch `gpt/usage-policy-2026-10-04` first if it is unavailable. It compiles an
isolated executable without modifying other lanes. All fixture responses are
synthetic pinned outputs of the Python compiler in
`gpt/graph-replies-2026-10-02`, source SHA-256
`50fffaffbf1abcbb2de1b626ae51f270a5842cd3e023c1dc3cee8011a401a903`.
It does not load the historical reply branch wholesale.
