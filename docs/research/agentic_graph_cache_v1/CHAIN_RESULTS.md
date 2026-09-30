# Cold-to-continuing journal results

Decision: keep the one-entry verified-parent policy as an optional choice for
one advancing head. Keep the larger policy for branching/reuse of older heads,
and whole memo for repeated identical heads. The first matrix and this follow-up
remain separate; no universal default changed.

All 1,212 timed head observations (four arms × three fresh trajectories × 101
heads) matched exact strict fixture bytes and the frozen proof-path gate. All
202 separately traced finalist head observations matched. Both parent arms had
one strict cold fallback and 100 authorized-parent hits per run. Whole memo had
101 strict fallbacks because no head repeated. Constructor cost is included in
the totals. Final-head strict preflight reconstructed every prefix before timing.
Chain freeze SHA256:
`166b6bd0a027cca66f475b583edaf1898616b6a01a836a3b1087bd6bc34a7e03`.

Median cumulative wall time, milliseconds, including the one cold start:

| Final events | Strict | Whole128 | Parent128 | Parent1 |
|---:|---:|---:|---:|---:|
| 0 | 0.631 | 1.320 | 1.289 | 1.152 |
| 5 | 17.414 | 20.351 | 10.422 | 10.972 |
| 20 | 322.765 | 333.026 | 68.687 | 74.963 |
| 50 | 3,487.715 | 3,562.922 | 363.993 | 381.509 |
| 100 | 25,302.466 | 26,029.092 | 1,342.726 | 1,365.953 |

At 100, median cumulative process CPU was 25,297.872 / 26,026.774 / 1,342.739 /
1,366.077 ms respectively. These are three-run shared-host medians. Parent1's
small timing difference from Parent128 is not evidence of a general speed
difference; its intended measured benefit is retention. The represented workload
is a fixed small current graph with growing scripted empty-diff history.
Nonempty native histories passed correctness audits but were not timed here.

One separate traced trajectory per finalist, excluding original fixture allocation
and including cache construction, returned native snapshots and replay/check
overhead, gave these final values:

| Policy | Entries | Cache payload bytes | Charged bytes | Traced current bytes | Traced peak bytes |
|---|---:|---:|---:|---:|---:|
| Parent128 | 101 | 6,747,361 | 6,806,850 | 7,397,325 | 9,289,391 |
| Parent1 | 1 | 130,255 | 130,844 | 723,729 | 2,745,152 |

Parent1 evicted 100 redundant packet snapshots while retaining the complete current
journal, raw source records and provenance. Charged bytes fell about 52×, traced
current about 10.2× and traced peak about 3.4× in this workload. These are Python
traced allocations, not RSS. Strict/whole cumulative traced memory was explicitly
not measured; the first matrix's full 66 memory cells cover a different usage
trajectory and must not be substituted as an equal cumulative baseline.

The two-case preregistered sibling-fork probe confirmed the retention tradeoff.
Priming head100 allows Parent128 to reuse privately authorized head99 for an
alternative last event. Parent1 has evicted head99 and correctly uses full strict
fallback. Both preserve exact native packet/application-receipt/inverse bytes,
retain the competing proposal identity, and select no semantic consensus. This
probe measures proof paths, not latency or content quality. A small corresponding
regression test also passed in the full structure discovery.

Raw trajectories, checkpoints, first proof paths, source/fixture freezes and
all denominators remain in `chain_first/`. No implementation or strict baseline
changed between the first matrix and chain. There were no model calls, key reads,
payments, holdout access or canonical graph writes.
