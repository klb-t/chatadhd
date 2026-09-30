# Cold-to-continuing journal follow-up — before outcomes

Observation from the preserved first matrix: prepared same-head and parent
reuse improve long-journal call time, but one-off strict priming can erase the
gain and storing old packets increases retained memory. The first matrix is
unchanged. This follow-up varies the usage trajectory and cache retention
policy, using the same frozen cache implementation and strict codec. It does
not tune semantic/model quality or modify a baseline.

Hypotheses: a sequential workload can amortize one cold start without separately
paying full strict proof for each long parent; one retained packet suffices for
immediate-parent reuse on a single advancing head and can lower retained bytes.
It may lose fork reuse or repeated old-head hits, so a one-entry policy must not
become a universal default. Prior fixtures already tested LRU correctness.

Fixed workload: 101 exact scripted native packets, histories zero through 100,
with the unchanged small two-Entity/one-Claim/one-Source graph and one empty-diff
event per step. Record every first head result. Checkpoints are 0/5/20/50/100.
Full strict validation of the final head recursively checks every prefix before
measurement. Original native/source bytes and model-origin annotations remain
unchanged; empty-diff history is not an extraction quality test.

Four arms: unchanged strict baseline; whole memo with the first preset's 128
entries; verified parent with that same 128-entry preset; verified parent with
only max_entries changed to one. Three timed trajectory repetitions per arm,
rotating declared arm order. Each run creates a fresh instance, reports its
constructor separately and includes it in total cost. Store wall and process CPU
for each validation, exact bytes, proof path, payload/charged bytes, entry count,
evictions/fallbacks and cumulative cost. Canonical result checks are outside the
validation timer. There are 1,212 timed head observations (4×3×101).

Measure traced memory in two separate finalist trajectories: verified parent128
and parent1, one run each (202 head observations). Tracemalloc begins before
constructor and excludes already-created fixture allocation; both current and
peak are recorded at fixed checkpoints. Do not claim traced cumulative memory
for strict/whole arms, which would repeat expensive quadratic tracing and is not
needed to compare the retention variable. The first matrix's full 66 memory
probes remain available but are a different workload. No timing claims derive
from traced runs. Preserve first observations incrementally.

Mandatory equality/path gates: every result equals the strict fixture's canonical
bytes; parent arms have exactly one strict cold fallback and 100 verified-parent
hits; whole memo has 101 strict fallbacks on this never-repeated sequence;
one-entry parent keeps at most one entry and may evict old heads. Any mismatch
preserves first output and fails the series. Keep one-entry only as an opt-in
trajectory-specific policy if correctness agrees and actual memory improves,
while reporting cache/fork tradeoffs. No universal policy, default store, truth
certification or provider authorization follows.
