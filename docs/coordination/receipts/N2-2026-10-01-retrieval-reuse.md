# N2 — Reuse deterministic TF-IDF corpus work

Base: `926072ce43b8de21a42a3abe439a7781598136f6`.
Preregistration: commit `3881c37ad8cbb6aaee35c7fc61010bb8bfd49d61`;
first instrument: `1762c523709eaa90b42d725c822ef173691df0f2`.
See the [protocol and pre-candidate addenda](N2-2026-10-01-retrieval-cost-protocol.md).

## Scope and mechanism

Only `loom/src/context/retrieval.cpp` and the ContextEngine constructor change.
An engine owns one lazily initialized built-in TF-IDF instrument. After a
successful fit/vectorization, it may retain one copy of the ordered document
IDs/text, normalized corpus vectors, representation states and collision-free
query ID. Matching every ID and text byte permits reuse for a later query;
scores, ranks, thresholds, result limits and query vectors are computed again.
A different document ID, text, ordering or count invalidates the corpus; an
empty corpus releases it. Initialization remains lazy when TF-IDF is unused.

There is **no database snapshot cache**. Each thesis still executes the existing
claim scan, evidence admission and entity-label reads. Fresh metadata, support,
run state, counter links, corpus caps and query errors flow through the existing
selector. The cache never stores failed/unavailable channel results. Explicitly
injected vector spaces keep every original fit/vector call, even if they name
their method `tfidf`; callers can still replace or disable the built-in by ID.

The instrument mutex spans exact corpus comparison, fit, query vectorization,
scoring and result construction. Concurrent retrievals on one instrument are
serialized; cached references never escape that lock. Default instrument
registration happens only in the constructor, with no new map writes in select.
The C ABI/chat builder creates an engine per request, so reuse serves theses
within that request. A longer-lived native engine retains at most its last
successful corpus per built-in instrument, subject to exact-content checks.

This is not free memory: one document copy and one normalized corpus-vector copy
remain alive for the instrument lifetime or until replacement/invalidation.
Their cost is O(document bytes + vector entries/keys); cold fitting also has
temporary vectors before the retained copy. Logical payload counters and
process-memory snapshots are recorded separately. No budget/radius/evidence
gate, ranking equation, output schema, C ABI, chat adapter or STATE changes.

## Evidence status at source checkpoint

The existing native baseline completed **18 cases / 54 calls**. All 18 full cold
outputs parse and match their recorded SHA-256; all warm outputs match cold
byte for byte. The original instrument and outputs are retained. TF-IDF cost
clearly grows with repeated corpus fitting, but the first run overlaps other
work and disk pressure and is **diagnostic**, not the final speed comparison.
Its uniform high-water RSS floor is explicitly unusable for cache-memory delta.

The shared integration verifier owns compilation/testing. Candidate native
execution, exact baseline parity and performance results are **pending** at
this checkpoint. Nine dedicated cases in `test_context_tfidf_reuse.cpp` cover
warm settings/results, changed corpus identity/content/order/size, injected
stateful instruments, fresh store/run metadata, external writes and query
errors, explicit instrument replacement, mutations between theses, shared
instrument concurrency, and logical retained-payload accounting. Existing
ranking gates and the full combined regression remain required.

The frozen V1 instrument is `loom/tools/benchmarks/context_candidate_cost.py`.
The pre-candidate V2 instrument adds streaming library hashing and Linux current
VmRSS snapshots at `loom/tools/benchmarks/context_candidate_cost_v2.py`, retaining
the exact workload and call timer. The final quiet sequence is full V2 baseline
repeat followed by full V2 candidate: 18 cases and 54 calls each, with all seeds,
requests and complete outputs equal. Keep first attempts and failures rather
than replacing evidence after a correction. Whole-process RSS snapshots include
the still-live returned output buffer and allocator effects; they are not an
isolated cache allocation or transient peak measurement.
