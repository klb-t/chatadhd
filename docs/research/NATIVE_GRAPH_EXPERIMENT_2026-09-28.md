# Native core graph diagnostic, 2026-09-28

This experiment runs `graph_flow.py` on canonical persisted Claim/Assessment
objects. It tests source export, projection, bounded retrieval and motif discovery
together. It uses no parser annotations, external model calls, independent
evaluation fixtures or semantic correctness labels.

Executable: `loom/tools/structure/native_graph_experiment.py`.
Compact result: `results/native-graph-2026-09-28.json`.

## Source and declared selection

The source is native run `kr_4b76e077273881e3`, produced from the three repository
architecture documents with external models and priors disabled. The read-only
snapshot contains 2,182 Claims, 1,002 observations, 60 entities and 2,018 slot rows.
Its canonical body SHA-256 is
`8fc0e86429169f9ae7f77d80939590746882006ec9feb9fb043bcd4d7ba07069`.
The report records the complete snapshot hash, source metadata and hashes of all
seven Python dependencies. A dependency change during execution rejects the run.

Only the 238 stored observed/active Claims are eligible for this particular
diagnostic. The excluded source classes remain in the snapshot:

| Exclusion | Claims |
|---|---:|
| Absent | 1,861 |
| Extrapolated | 68 |
| Derived, outside this diagnostic's observed-only selection | 1 |
| Observed but contested | 14 |

Twenty-four one-Claim records are selected deterministically: round-robin by
supporting unit, then by subject, with ascending Claim IDs within each subject.
For a Claim supported by multiple units, the smallest resolved unit ID is the
declared primary grouping key; all support units and source IDs remain recorded.
Eight Claims are sampled from each of three units. The other 214 eligible Claims
are explicitly omitted by the record budget.

Unit grouping is **not verified source independence or topic scope**. All domain
labels remain unknown. Each record preserves its source qualifiers, subject,
support observation IDs, unit IDs and source IDs. The sample contains 18
`mentioned_in`, five `has_status` and one `states_principle` Claims. All 24 use
literal values, with six distinct values. This is a visible consequence of the
selection policy, not a representative distribution of every kind of Claim.
The full eligible predicate distribution is recorded in the JSON result.

## Fixed experiment bounds

Both modes use the same 24 records and all 24 as queries:

- Semantic and structural core projections, with all status/scope restrictions
  retained by their declared policies.
- Top three filter-surviving candidates per query; 1,000 verification states per
  pair; maximum 2,048 vertices per view.
- Exact-label motif discovery over radius-one node neighborhoods, at most 512
  enumerations, ten vertices per neighborhood, 500 states per alignment and
  1,000 verification calls. Motif names are not silently abstracted.

## Measured results

| Measurement | Semantic | Structural |
|---|---:|---:|
| Query–candidate pairs | 552 | 552 |
| Safe-filter survivors | 0 | 0 |
| Rejected for insufficient node labels | 552 | 552 |
| Exact retrieval verification calls | 0 | 0 |
| Motif root enumerations / planned | 399 / 399 | 399 / 399 |
| Neighborhoods over the size bound | 18 | 18 |
| Exact motif match verifications | 44 | 44 |
| Verified recurring motif groups | 39 | 39 |
| Recurring motifs containing a core Claim vertex | 0 | 0 |
| Elapsed flow time | 6.80 s | 6.77 s |

The snapshot read took 0.14 seconds. Timings are environment-specific diagnostic
measurements, not performance guarantees. No retrieval candidate reached exact
verification: these results do not establish the absence of related knowledge.
They show that none of these complete projected views survived the declared
strict-label containment filters.

The 39 recurring motifs contain **no core Claim vertices**. In this run, the
recurrence is in provenance/context neighborhoods; it does not demonstrate
repeated thought-operation relations. Their source groups are caller-declared
unit groups, not independent confirmations. Motif enumeration visited every
planned root, but the 18 oversized neighborhoods mean representation coverage was
still incomplete. There were no verification-budget unknowns in this run.

## Source closure and retained unknowns

Views range from eight to 49 vertices. Selecting one Claim also brings in
provenance attached to its referenced entity. Thirteen of 24 views therefore
contain observation references beyond the Claim's direct support: 97 extra
references in total, counting repeated references across views. The largest view
contains 23 times as many observation references as direct support observations.
The sample has 51 distinct direct support observations.

These extra observations are source-context closure, **not additional evidence
for the selected Claim**. Per-record direct support, expanded reference IDs and
ratios remain in the report. This separates source-preserving projection overhead
from inference support.

Each mode reports 96 unresolved reference occurrences across views. These refer
to records missing from the exported wrapper, including unit/source bodies; they
do not establish that those records or original source bytes are absent from the
system. Unknowns are retained, raw source verification is not claimed, and
neither motifs nor retrieval results become Claims. Graph mutations and Claim
promotions are both zero.

## Reproduce

From a native database containing the same run:

```sh
python loom/tools/structure/native_graph_experiment.py \
  --database /path/to/chatadhd.db \
  --run kr_4b76e077273881e3 \
  --record-budget 24 --state-budget 1000 --top-k 3 \
  --output /path/to/new-native-graph-report.json
```

Or use the canonical JSON emitted by `core_snapshot.py`:

```sh
python loom/tools/structure/native_graph_experiment.py \
  --snapshot /path/to/core_snapshot.json \
  --run kr_4b76e077273881e3 \
  --record-budget 24 --state-budget 1000 --top-k 3 \
  --output /path/to/new-native-graph-report.json
```

The output path must be new. The explicit run ID is checked against the snapshot.
The database path is opened through the existing read-only snapshot helper; this
script performs no native build or database writes. Reproduction should compare
record selection, source/dependency hashes and structural counts; elapsed times
will vary.
