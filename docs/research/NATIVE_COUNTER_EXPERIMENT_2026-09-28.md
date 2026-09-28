# Native contested-Claim diagnostic, 2026-09-28

Including observed contested Claims reveals the native counter-reference
structure excluded by the earlier active-only sample. The resulting matches
still reuse overlapping source Claims; they do not establish a transferable
thought operation, independent recurrence or logical contradiction.

This is a deliberately relation-biased extension of the
[assertion diagnostic](NATIVE_ASSERTION_EXPERIMENT_2026-09-28.md). It uses the same
native run `kr_4b76e077273881e3`, source snapshot, frozen core projector, assertion
view and comparison functions. The new wrapper changes only selection:
`evidence_class == observed` and `status in {active, contested}` are eligible;
seeds must also have an explicit premise/counter/consequence Claim reference.
Assessments remain source data. No contested Claim becomes active or eligible
for inference. The original diagnostic script and result are unchanged.

The [result JSON](results/native-assertion-contested-2026-09-28.json) records the
full selection policy, source statuses, dependency versions, omission counts,
reference-port counts, bounded-search coverage and assessment dimensions. The
snapshot SHA256 remains
`e1d2915db13e02afb4aa4af8a87c91c3bf2e71625085c89b10b417db0db4dd01`.

## Source selection

The expanded class has 252 eligible Claims: 238 active and 14 contested. All 14
relation-bearing seeds are contested Claims with predicate `decides`; all their
92 direct references are `counter:claims` and target Claims among the same 14.
There are no direct premise or consequence Claim links in this seed set. The
other 238 eligible Claims have no such seed relation and are deliberately not
sampled. The absent/derived/extrapolated classes remain excluded from roots.

Each seed receives at most three explicitly referenced eligible Claim bodies,
ordered by Claim ID. This creates 14 views with four roots each: 56 root
occurrences, 14 unique Claims, and six repeated Claim sets. Fifty eligible
direct-target body occurrences are excluded by the context bound but retained
as identified reference ports. No same-subject co-occurrence contexts are added.

Counter-reference edges are source Assessment relationships. Their presence alone
does not establish a proposition-level logical contradiction or a thought
operation. No new relation was extracted or inferred from text in this run.

## Results

| Measurement | Semantic exact | Structural exact | Unsafe literal-type control |
|---|---:|---:|---:|
| Retrieval candidate pairs | 182 | 182 | 182 |
| Safe-filter survivors / verified matches | 24 / 24 | 24 / 24 | 24 / 24 |
| Matches with disjoint selected Claims | 0 | 0 | 0 |
| Matches across declared seed unit groups | 6 | 6 | 6 |
| Recurring motifs with multiple Claim roots and explicit counter links | 50 | 50 | 50 |
| Recurring motifs with one Claim root | 15 | 15 | 15 |
| Recurring motifs without Claim roots | 3 | 3 | 3 |

All 24 verified pairs share selected source Claims, including all six pairs that
cross declared seed-unit groups. A group is assigned from the seed's primary
support unit, so different seed groups do not make these overlapping contexts
independent. Domain identity remains unknown. Literal-type erasure produces no
additional verified matches and is not an admissible semantic conclusion.

Motifs use exact lexical names from the supplied views; structural retrieval
separately permits explicit consistent reference renaming. Recurrence counts
therefore include multiple views of the same counter-linked Claim cluster.
They are not counts of independent arguments or distinct atomic operations.

All 502 requested radius-one/two neighborhoods were enumerated within the bounds,
with 244 verified recurrence joins, no verification unknowns, no oversized
neighborhoods and no top-k omissions. Graphs have 13–21 nodes. Across the 14 views
there are 368 counter-edge occurrences, because each selected contextual root
also retains its source counter references. This is repeated source incidence,
not 368 newly discovered or unique relations.

Each mode retains 50 external Claim reference ports, 50 corresponding
outside-selection unknown occurrences, 66 missing export reference occurrences
and 20 opaque optional-record semantic unknown occurrences. The 338 projected
source observation references move to evidence sidecars; no counter-observation
ports remain in this sample. Every core graph reconstructs exactly from the
sidecars. Projection plus roundtrip takes 5.47–5.83 seconds per mode, retrieval
0.38–0.43 seconds and motifs 1.45–1.48 seconds in this single run.

This extension resolves a concrete selection limitation: the native graph does
contain explicit counter relations that active-only sampling concealed. It does
not resolve the separate limitation that observed matches reuse the same Claim
cluster. Broader semantic abstraction or argument interpretation requires its
own explicit data and validation contract.

## Reproduction

From repository root, write to a fresh output path:

```sh
python loom/tools/structure/native_counter_experiment.py \
  --database /tmp/loom-graph-native-yksxvm_2/runtime/chatadhd.db \
  --run kr_4b76e077273881e3 \
  --status-policy active_and_contested \
  --record-budget 24 --context-limit 3 --state-budget 1000 --top-k 3 \
  --output /tmp/native-counter-repeat.json
```

`--snapshot` can replace `--database`. `--status-policy active_only` explicitly
selects no relation-bearing seeds in this source; it does not silently expand
the policy. Version hashes are checked before and after execution. Existing
reports are not overwritten. The wrapper has two author tests for preserving
contested status, excluding ineligible/missing target bodies, and retaining
omitted reference metadata. No independent fixture content or new validation
outcomes were inspected. No model call, parser change, native build, source
mutation or confidence synthesis occurred.
