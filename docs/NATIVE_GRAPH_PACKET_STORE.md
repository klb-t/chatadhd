# Native GraphPacket acceptance and receipts

The N3 adapter persists an explicitly selected part of a `loom.graph_packet/1`
packet into the existing native KnowledgeStore. It retains the complete packet,
including definitions, instrument provenance and reversible history, in an
immutable receipt. Selection and persistence do not establish that the packet's
statements are true: every receipt has
`acceptance_establishes_content_truth: false`.

This is an exchange-packet storage boundary. It does not implement the complete
GraphPacket transformation algebra in C++, make native CandidateGraph equivalent
to GraphPacket, or automatically connect accepted tasks to retrieval plans.

## API and explicit selection

The C ABI adds:

```c
const char* loom_graph_packet_store(LoomContext* ctx,
                                   const char* request_json);
```

The result is JSON; errors use the existing
`{"error":{"code":"...","message":"..."}}`
convention. Release every returned string with `loom_free_string`. The C++
entry point is `loom::kb::GraphPacketStore::execute`. Python's
[`NativeGraphStore`](../loom/tools/coordination/graph_store.py) manages the
context and result strings, raises `GraphStoreError` for native errors, and
validates the full packet/history through the
[`GraphPacket codec`](../loom/tools/structure/agentic_graph_v1/packet.py).

| Operation | Exact request fields | Behavior |
|---|---|---|
| `accept` | `operation`, `target`, `packet`, `selection`, `expected_rows`, `explicitly_accepted` | Atomically writes selected native rows, replays owner judgements and appends a receipt |
| `read` | `operation`, `receipt_id` | Returns the original receipt and a coherent current-row drift report |
| `replay` | `operation`, `receipt_id` | Returns the original receipt only if all recorded row snapshots still match; writes nothing |

`target` is a nonempty caller-chosen string. It identifies the deterministic
KnowledgeStore run for this adapter; it is not a filesystem path or an existing
run ID. Both `selection` and `expected_rows` must have exactly the keys
`entities`, `claims`, and `sources`. Each selection value is an array of unique
packet record IDs; each expected value is an object with exactly those IDs.
At least one record must be selected. `explicitly_accepted` must be JSON `true`.
Definitions are retained in the complete receipt rather than imported as native
rows by this operation.

Selections must include their native dependencies: entity parents, claim
subjects/objects, referenced claims, support observations and counter
observations. The adapter rejects a partial selection that omits these records;
it does not silently add them or resolve them from another run.

## Minimal Python example

Run from the repository root after building the shared library. This example
uses one explicitly entered fictional entity and a fresh temporary data store.
The exact example has been exercised against the native library.

```python
from pathlib import Path
from tempfile import TemporaryDirectory

from loom.tools.structure.agentic_graph_v1 import packet as codec
from loom.tools.coordination.graph_store import NativeGraphStore

origin = {
    "kind": "user", "actor": "example.review", "model": None,
    "recipe_sha256": None, "response_sha256": None,
}
entity = {
    "id": "e_demo", "kind": "concept", "canonical_key": "demo-concept",
    "label": "Demo concept", "labels": {}, "aliases": [], "parent": "",
    "first_seen": "", "last_seen": "", "evidence_class": "user",
    "origin": "user", "confidence": 1.0, "status": "active", "attrs": {},
}
packet = codec.make_packet(
    entities=[entity], origin=origin, task={"operation": "explicit-review"},
)
selection = {"entities": ["e_demo"], "claims": [], "sources": []}
expected = {"entities": {"e_demo": None}, "claims": {}, "sources": {}}

with TemporaryDirectory() as directory:
    with NativeGraphStore(Path("loom/build/dev/libloom.so"), directory) as store:
        result = store.accept(
            packet, target="example/entity-review", selection=selection,
            expected_rows=expected, explicitly_accepted=True,
        )
        receipt = result["receipt"]
        assert receipt["packet"] == packet
        assert receipt["acceptance_establishes_content_truth"] is False
        assert store.read(receipt["id"])["row_drift"]["matches"]
        assert store.replay(receipt["id"])["receipt"] == receipt
```

Use the platform's actual shared-library path outside this Linux build. The
wrapper starts the native context with workers disabled and performs no model
request. Use a persistent caller-owned data directory when receipts must survive
after the process exits.

## Compare-and-swap, identity and owner authority

For each selected row, `expected_rows` contains either JSON `null` for an absent
native row, or the SHA-256 of its current native canonical JSON **body**. Packet
provenance hashes and receipt hashes are different values. After acceptance,
`receipt.stored_row_sha256` supplies the actual native body hashes for a later
compare-and-swap request; `receipt.row_snapshots` records materialized rows and
related alias/support rows. A changed packet requires a fresh acceptance request
with the expected hashes for that same target. A mismatch rejects the entire
transaction.

An exact repeated acceptance request has the same `gpr_...` receipt ID. If its
recorded rows still match, it returns that immutable receipt with `replayed:
true`; if they have drifted, it fails. A first successful acceptance returns
`replayed: false`.

Compare-and-swap does not authorize changing a record's semantic identity.
Existing observations cannot change any native observation field under the
same ID. Entity `kind` and `canonical_key`, and claim `subject`, `predicate`,
`object`, `value`, and `qualifiers`, are immutable under the same ID. Such changes
require a new record identity. Other permitted row changes still require CAS and
all normal validation.

After writing selected observations, entities and claims, acceptance resets the
run's judgement replay cursor and replays the existing global append-only owner
judgement log. A later packet cannot overwrite an earlier owner rejection or
confirmation merely by passing CAS. The receipt preserves the submitted packet;
its native row snapshots and stored hashes describe the state **after** owner
judgements, which can differ from that packet. Schema changes, run creation,
selected writes, judgement replay and receipt insertion are within the same
transaction; a late failure rolls them back together.

## Read, drift and validation boundaries

`read` leaves the original receipt unchanged. `row_drift.matches` compares its
recorded materialized row snapshots with a single current database snapshot;
`row_drift.rows` identifies missing or changed selected rows, and
`row_drift.current_row_snapshots` exposes their current state. Drift includes
indexed native columns and related alias/support rows, beyond the body hashes
used for CAS. `replay` verifies these snapshots and rejects drift. Neither replay
nor an acceptance retry repairs missing or changed rows.

The native adapter verifies packet head identity, record/provenance hash
coverage and selected native DTO projections. It rejects DTO fields that native
serialization would discard or change, checks reference closure, source text
hashes, support quotes and exact UTF-8 subspans. Full packet JSON is retained;
unselected records are not materialized as knowledge rows.

Python `accept`, `read` and `replay` additionally validate the complete codec
contract, including backward/forward consistency of retained reversible history.
Direct C ABI callers do not receive that full history validation: receipts
explicitly report `reversible_history_validation` as not performed by the native
adapter. Principle, prediction and pack-check references remain opaque IDs;
the receipt lists them in `opaque_native_references` and explicitly records that
this adapter has not resolved them. Internal hashes establish consistency, not
source authenticity or content truth.

## Schema and verification

The knowledge schema advances from **KB 2 to KB 3**, independently of the
unchanged **core schema 4**. The additive table is
`loom_kb_graph_receipts(id, run_id, body)`, with a run index. Existing observation,
entity and claim tables remain the materialization destination. Migration updates
`loom_kb_meta.schema_version` and `_meta.loom_kb_schema_version`; it does not bump
the core `_meta.schema_version`. Receipt triggers reject updates, deletes and
replacement inserts. The receipt also carries a verified `receipt_sha256`.

The dedicated real-FFI boundary suite passed **13/13 tests**, covering full
packet/history retention and restart, UTF-8 support spans, drift and retry,
immutable receipts, explicit acceptance and selection closure, CAS and late
transaction rollback, direct ABI validation, forged Python history, KB migration,
stable record identity, owner judgement authority and opaque references.
Its source is
[`test_graph_packet_store.py`](../loom/tests/compat/test_graph_packet_store.py).
The complete updated CTest run is a separate gate; earlier whole-project
verification does not by itself validate this added implementation.

```bash
LOOM_LIBRARY="$PWD/loom/build/dev/libloom.so" \
  python3 -m unittest discover -s loom/tests/compat \
  -p test_graph_packet_store.py -v
```
