# N2 retrieval cost protocol — before candidate implementation or outputs

Base source: `926072ce43b8de21a42a3abe439a7781598136f6`.
Existing native implementation baseline: `da77c769d3e020c396db5bef6a3a3755314d75c0`;
shared library SHA-256 `f1c7a45000bee6b6efc73a5ec92cb87d519872d4214dccc85df42dced479db40`.
Only the integration verifier builds C++. This lane first measures the existing
library through `loom_context_build`; no baseline source rebuild is needed.

## Hypotheses and selection rule

Source inspection finds that each thesis scans candidate claims, reads one entity
per distinct endpoint, creates a new built-in instrument, and fits/vectorizes the
same corpus again. For T theses, C claims and E endpoint IDs, this repeats T*C
claim projections, approximately T*E point reads and T corpus TF-IDF fits. The
existing instrument already caches labels within one thesis. These are operation
counts from code, not timing measurements.

Candidate directions are (a) batching endpoint reads, (b) sharing an immutable
corpus snapshot within a plan, or (c) reusing only the deterministic built-in
TF-IDF fitted corpus/vectors while still reading fresh source data every time.
Direction (c) has the narrowest invalidation surface: compare every ordered
document ID and text byte, retain at most one corpus per instrument, and never
reuse external/injected instrument results or failure states. Choose a change
only after observing the baseline workload. Do not implement all hypotheses.

## Frozen synthetic workload

Use fresh temporary data directories, disabled workers, no credentials or model
calls. The current native API creates the knowledge schema; the instrument then
inserts explicit synthetic typed entity/claim JSON through SQLite, exclusively
in its own fixture database. Seed bytes are deterministic and recorded by hash.
There is no user archive, DEV gold tuning or holdout input.

- Sizes: 64, 256 and 1024 claims, each with its own entity endpoint and a fixed
  support quote mixing a topic marker with common prose. Thirty-two topic groups.
- Plans: 1, 4 and 16 explicitly named theses; each query names one topic marker.
  Use `answer_question`, English, zero exploratory hops, raw detail and an ample
  fixed 100000 item-token budget. No filtering or rank threshold is relaxed.
- Instruments: `tfidf` versus an explicitly uninstalled channel (corpus assembly
  control); both use result limit 10, min_score 0, scan limit C+1. TF-IDF is still
  an offline glossary/lexical vector method, not a semantic truth oracle.
- For each case run one cold call and two warm calls in a fresh child process.
  Cold means first native context build in that process, not a flushed OS disk
  cache. Warm means the same runtime/database is already open; C ABI creates a
  new ContextEngine on every call. Each multi-thesis plan reuses one engine.
- Record native call wall time (serialization included, Python JSON decoding
  excluded), process peak RSS before/after, output bytes, exact raw output hash,
  seed/request/library/instrument hashes and each call's outcome. Keep the full
  cold output. Warm outputs must equal it byte for byte.
- The baseline/candidate comparison uses precisely the same instrument, seeds,
  request matrix and call counts. Do not silently replace a failed case or first
  result. A 120-second per-child timeout is an instrument resource boundary;
  retain timeout status instead of silently reducing size or thesis count.

## Acceptance and limitations

Every complete candidate output must equal baseline byte for byte, including
context IDs, ordering, floating-point scores, prompt, evidence/source factors,
counter diagnostics and unavailable/error/truncated statuses. Report cold and
warm timings separately, all sizes and control arms, not just favorable cases.
No timing pass threshold or generalization claim is preregistered: accept an
optimization only if its measured intended cost decreases with exact parity and
the safety controls pass; explain noise or regressions.

Tests must cover same-content reuse, changed query, changed document text/ID/
order/size, empty and unrepresentable inputs, unchanged errors/unavailability,
run switching, updated entity labels and support quotes, and store mutations
between theses from an injected channel. Repeated calls and fresh engines must
agree. The cache must not intercept injected instruments or suppress their
side effects/calls. The existing ranking and evidence gates remain unchanged.

RSS is process high-water memory including Python, SQLite and result JSON; it is
not an isolated allocator measurement. Report the additional cache ownership
and asymptotic payload explicitly. Per-thesis source scans, entity reads,
sorting, exact key comparison and full trace serialization may remain costs.
The study is synthetic mechanism/performance evidence, not answer quality or
proof of a tenfold overall application speedup.

## Timing-window addendum, before any candidate execution

The first baseline completed all 18 cases / 54 calls with exact warm parity,
while other lanes were active and disk availability reached zero. Its 18 saved
cold JSON outputs were read back, parsed and checked against their recorded
SHA-256 values. Preserve that first run as diagnostic evidence.

The primary timing comparator is now fixed **before candidate measurements**:
after the combined build/CTest and disk recovery, reserve a quiet window and
run the same complete baseline matrix once more, then the candidate matrix.
Compare all cells in that fixed order. The first baseline remains published;
do not choose the more favorable baseline post hoc. Acceptance also explicitly
checks all 18 seed/request/output comparisons, all 54 call records and matching
instrument SHA, even though the frozen runner's exit gate checks output parity
only. A failure/timeout leaves subsequent matrix cells unmeasured (fail-fast).

An additional native mechanism test will report logical retained corpus ID/text
bytes and normalized vector entry/key counts at the same three sizes. These are
owned payload counts, not exact allocator/RSS sizes; map/vector/string overhead
and temporary cold-fit copies are extra. Add one shared-instrument concurrency
regression, with varying corpora and queries, against the uncached equivalent.

## V2 memory-instrument correction, before any candidate execution

V1 reported the identical 177408 KiB peak RSS floor in all cases, masking the
retained cache cost. Keep that complete instrument and first run unchanged.
`context_candidate_cost_v2.py` retains the same seeds, requests, matrix, timer
scope and repetition counts, but hashes the library by streaming chunks and
adds Linux `/proc/self/status` current VmRSS snapshots before/after the native
call. The primary quiet baseline-repeat and candidate arms both use V2 and must
have identical V2 script hashes. V1 remains diagnostic, not a memory comparator.

VmRSS is a whole-process snapshot while the returned C output buffer is still
live, not the transient peak or isolated cache allocation. Preserve signed
deltas (including negative values); allocator behavior and page residency may
affect them. Keep the original high-water fields and their initial floor too.
The logical payload test is independent evidence of cache-owned data volume.
