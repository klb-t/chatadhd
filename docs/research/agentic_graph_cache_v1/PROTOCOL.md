# Verified packet cache experiment — 2026-09-30

Frozen strict baseline remains `agentic_graph_v1/packet.py` SHA256
`1b949dad8319f21713b984367cba162d1330f188d26557b21b264ef10910bbc4`.
Earlier 0/5/20/50 timing observations remain untouched. This is a separate
mechanism/resource experiment, with no model quality, paid calls, validation
text, native store writes or changed baseline gates.

Hypothesis H1: exact whole-packet memoization reduces repeated same-head
validation cost while keeping cold/setup overhead. H2: separately verified
immediate-parent journal reuse reduces warm-extension cost compared with the
simpler whole-packet control. H3: changed old-prefix history must miss the
cache and fall back to the complete strict validator; stale hashes/corrupt
journals/cycles must still reject. Caching may lose on cold/small inputs.

Methods: (1) unchanged strict validation; (2) whole-packet memoization only;
(3) exact whole-packet memoization plus an authorized verified-parent fast
path. Parent reuse checks complete current native records, exact last-event
before/after/provenance/order/base identity and forward replay against an exact
immutable parent previously validated by the bound strict validator. If no
such parent is present, use full strict validation. No caller-provided hash or
serialized authorization receipt authorizes a hit. All stored entries are
immutable canonical bytes, with private instance tokens and exact byte equality
as well as SHA256, strict code identity and resource-policy identity. Return a
detached snapshot, so caller mutation cannot change a cache entry.

Resource controls are data: mode, entry count, per-entry bytes, retained-byte
budget, LRU eviction or bypass. Null means this cache imposes no numeric bound;
zero disables storage. Charged bytes include canonical payload and conservative
per-entry Python metadata accounting, not a claim to bound the process allocator.
Measure actual traced current/peak memory separately. Resource limits selected
for a codec call still apply before a hit and form part of cache identity.

Required inputs: unchanged semantic graph with 0/5/20/50/100 history events.
Three timed repetitions per cell, deterministic alternating method order.
Conditions: cold; warm repeated same head; warm extension from an exactly primed
immediate parent; valid last-event fork; valid invalidated old-prefix history.
Store wall time, process CPU time, payload bytes, cache charged bytes, entries,
setup/prefill time and traced memory separately. Memory probes run separately
from timed probes. Preserve every first measurement. Full input preparation
may construct scripted empty-diff fixture histories without claiming model
quality; all measured heads must pass the unchanged strict baseline.

All acceptance/rejection counterexamples must agree with strict validation,
including source quote/UTF8/hash/locator mismatch, native Assessment failures,
dangling dependencies, malformed and rehashed history, stale per-record hashes,
caller mutation, shared acyclic references and cycles. Applied graph and exact
application receipt bytes must match the strict baseline after validating a
cached packet. Concurrent caller/cache use must not insert caller-owned mutable
objects. An external code-version change fails closed; no changes to the frozen
baseline, source semantics or explicit validation release are permitted here.

Decision: keep a method only as an opt-in resource optimization if correctness
matches on all exercised cases and an informative timing condition improves;
report cold/memory losses explicitly. Simpler whole memoization is the control,
not assumed inferior. No universal default or model trust claim follows from a
small scripted fixture. If a mismatch occurs, preserve it and fix or revert the
new cache, never weaken the strict baseline.
