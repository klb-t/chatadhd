# First verified-packet cache results

Decision: keep both methods as opt-in resource choices. Whole-packet memoization
is the simpler control for repeated identical heads. Verified immediate-parent
reuse is useful for a continuing journal, while retaining more bytes. Neither
becomes the universal default. Cold input, an invalidated old prefix and the
empty-history graph usually favor the unchanged strict validator. These are
mechanism/resource measurements, not model or content quality.

The strict packet remains SHA256
`1b949dad8319f21713b984367cba162d1330f188d26557b21b264ef10910bbc4`.
Cache implementation SHA256 is
`bb6ac98489a85a807eb2879d877943c2f38843d852c8ffe1aaa4d0a67e225425`.
First-series freeze SHA256 is
`982f85d568142f1c54dc0ac2c92f464f8279f5cddba6c76f557adadacb9ab415`.

## Timing and denominators

The frozen matrix planned 225 timed cells: 198 applicable measurements and 27
explicitly not applicable because extension/fork/old-prefix histories do not
exist at zero events. There are three timed repetitions per applicable cell.
Separate memory probes planned 75 cells: 66 applicable and nine not applicable.
All 66 applicable condition/method combinations passed exact validated packet,
applied packet and application receipt byte comparison, path expectations and
exact inverse checks outside the timing probes. All 17 measured/priming fixture
heads passed the unchanged strict baseline before measurements. Raw first
measurements and all denominators remain in `first_series/`.

Wall medians, in milliseconds, for the fixed two-Entity/one-Claim/one-Source graph:

| Events | Condition | Strict | Whole memo | Verified parent |
|---:|---|---:|---:|---:|
| 0 | Same head, warm | 0.382 | 0.399 | 0.399 |
| 5 | Same head, warm | 5.006 | 0.749 | 0.764 |
| 20 | Same head, warm | 32.529 | 2.040 | 1.975 |
| 50 | Same head, warm | 203.291 | 4.880 | 4.988 |
| 100 | Same head, warm | 602.591 | 7.859 | 8.074 |
| 5 | Next event, warm parent | 4.913 | 5.655 | 2.100 |
| 20 | Next event, warm parent | 32.859 | 34.125 | 5.411 |
| 50 | Next event, warm parent | 191.555 | 209.680 | 11.153 |
| 100 | Next event, warm parent | 573.402 | 622.761 | 21.332 |
| 100 | Cold | 641.695 | 697.235 | 653.169 |
| 100 | Valid old-prefix fork | 569.345 | 599.111 | 635.269 |

At 100 events the CPU medians for a same-head call were 602.525/7.861/8.077 ms;
for extension 573.335/622.696/21.335 ms. Wall and process CPU are recorded
separately. Shared-host variability and only three repetitions mean small
differences are not evidence of an algorithmic advantage. In particular, one
50-event old-prefix cold fallback happened to favor whole memo in wall median;
it still performs the full strict proof and is not an inferred cache benefit.

Preparation is not included in these call medians. At 100 events, same-head
whole/parent preparation took 634.472/607.014 ms. Parent extension preparation
took 581.407 ms, making one isolated prepared extension about 602.739 ms versus
573.402 ms for direct strict validation. The prepared cache has value when
already available or amortized over more calls. Zero-event warm reuse remains
slightly slower even before its preparation cost. Strict preparation records
only the tiny dispatch wrapper (roughly 0.001–0.004 ms), with no cache creation
or prefill; this refines the freeze's shorthand description “zero setup”.

## Memory costs

At 100 events, payload is 130,255 canonical bytes. Tracemalloc starts before
cache constructor/prefill; original fixture allocation is excluded and returned
detached snapshots are included. It measures Python traced allocations, not
full process RSS, and is separate from the timed run.

| Condition/method | Traced current bytes | Traced peak bytes | Retained cache payload | Charged cache bytes |
|---|---:|---:|---:|---:|
| Same head / strict | 22,336 | 2,133,330 | 0 | 0 |
| Same head / whole memo | 694,058 | 1,087,706 | 130,255 | 130,844 |
| Same head / parent | 823,590 | 1,217,238 | 259,241 | 260,419 |
| Extension / strict | 22,336 | 2,133,330 | 0 | 0 |
| Extension / parent | 970,351 | 2,345,449 | 386,958 | 388,725 |
| Old-prefix fork / parent | 1,089,835 | 3,448,364 | 520,472 | 522,828 |

Warm whole memo lowers the temporary peak here but retains substantially more
memory. Parent extension's traced peak is slightly higher than strict despite
its call-time gain. Retained-byte budgets are estimates of payload and per-entry
metadata, not guarantees on allocator/OrderedDict/process memory. Eviction and
bypass remain caller policy data.

## Corrections and independent verification

The first independent cache audit passed 12/13 cases and found that changing
the transitive `_validate_history` binding could leave an old whole-hit token
usable. First failure/source were preserved by `philosophy_watch` as revision17.
Only the new cache guard changed: it now snapshots every exposed codec/serializer
callable identity/code binding plus native vocabulary and validator-map contents.
The identical independent retest passed 13/13 (revision18). This requires trusted,
unmodified codec/serializer runtime state at instance construction; the guard
detects later drift. Disk hashes do not authenticate already-loaded functions.
It is not a hostile Python sandbox. The strict codec was unchanged.

Own cache tests passed 17/17 before the benchmark. An additional preregistered
nonempty native journal audit exercises twelve meaningful events, raw source
tombstones/restoration, native updates, open types, task replacement and order.
Its first fixture setup failed before any cache case: the authored fixture used
a string instead of a native Alias record. Exact first source/log/zero-test
result are preserved. The repaired typed fixture passed four tests and 67
structured cases: 26 native heads (including 24 exact application receipt and
inverse checks), 38 rehashed journal corruption comparisons, two stale-edit/inverse
cases and source/provenance/time retention. Overlapping assertions are not
independent model samples. Original support text and native origin remain exact;
automatic projection acceptance never certifies content truth.

A separate independent native-fixture audit passed 6/6 (watch revision24): coupled
Claim/source tombstone, restoration, exact application/inverse, retained UTF8
source/provenance/availability time/native origin, stale inverse, rehashed
retained-source corruption and an independently competing last event. Its own
source, first evidence and archive remain under `philosophy_watch_2026-09-30/`.

No model call, payment, key read, validation/old holdout access, native store write,
quality threshold change or frozen baseline modification occurred in this lane.
