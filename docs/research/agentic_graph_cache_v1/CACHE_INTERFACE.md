# Optional validation resource method

`VerifiedPacketCache` consumes the existing `loom.graph_packet/1` interface.
It does not create another graph store, edit native Claim fields, execute model
calls, authorize spending, or apply diffs. The strict codec remains the baseline.

```python
from loom.tools.structure.agentic_graph_cache_v1.cache import VerifiedPacketCache

validator = VerifiedPacketCache({
    "schema": "loom.graph_packet_cache_policy/1",
    "mode": "verified_history",  # or the simpler "whole_packet" control
    "max_entries": 128,
    "max_retained_bytes": 16777216,
    "max_entry_bytes": 2097152,
    "on_capacity": "evict_lru",  # or "bypass"
})
validated_packet, diagnostic = validator.validate_with_receipt(packet)
```

These numbers are the measured experiment preset, not capability limits.
Each numeric field also permits `null`; zero disables storage where applicable.
Codec JSON resource limits remain separate optional call data:
`validator.validate_packet(packet, resource_limits=limits)`.

The result is a detached native packet with equal canonical bytes. The strict
codec returns its original object, so callers must not depend on object identity
when selecting this method. Caller mutation and returned-snapshot mutation do
not alter the privately retained canonical bytes. The cache applies resource
validation before lookup, and binds entries to exact bytes, codec/source version,
resource limits, cache policy and an instance-private admission token. The
public diagnostic is not an admission token and is not an application receipt.
Existing `codec.apply_diff` still produces the ordinary unchanged application
receipt and preserves all origin, availability time, source and undo behavior.

`whole_packet` stores only fully validated exact packets. `verified_history`
also validates the entire last journal event, reconstructs its exact parent,
checks forward replay and reuses that parent only if this instance has already
privately authorized the exact parent bytes. A missing parent uses full strict
validation. An old-prefix fork cannot borrow an unrelated parent's authority.

`clear()` invalidates all admission tokens held by the instance. `reconfigure`
accepts a new policy and clears existing entries. LRU/bypass and retained-byte
accounting are visible through `stats()`. Charged bytes estimate canonical
payload plus per-entry Python metadata; they do not limit process RSS or include
all allocator/container overhead. Actual traced memory is measured separately.

The bound strict source file is pinned to the tested version. Codec and serializer
runtime state must be trusted and unmodified when an instance is constructed;
disk hashes alone do not authenticate already-loaded Python functions. Subsequent
changes to codec or serialization source bytes, exposed callable bindings, or
native validation vocabulary/mapping fail closed with `CacheIntegrityError`.
This assumes a trusted Python runtime and libraries; it is not a sandbox against
hostile reflection, altered standard-library internals or arbitrary process
memory access. Concurrent validation uses private immutable entries and locked
cache metadata; this is not a claim of atomic caller mutation of arbitrary
Python objects. Input snapshot capture failures remain failures.

The benchmark tests a small fixed current graph with scripted empty-diff history.
Any resource improvement is conditional on that representation and cache state.
It says nothing about whether an edge, attribution or model proposal is true.
The first independent runtime-binding failure and its corrected retest are
retained in `philosophy_watch_2026-09-30/PACKET_CACHE_*_17/18.json`.
