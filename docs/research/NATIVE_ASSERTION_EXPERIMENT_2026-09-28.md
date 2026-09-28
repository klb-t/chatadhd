# Native assertion-view diagnostic, 2026-09-28

The assertion view exposes recurring multi-Claim shapes in this sample, but no
explicit argument dependency. Exact matches are overlapping source records, not
evidence of a transferable thought operation. This is a source/projection/search
diagnostic, with no independent semantic accuracy labels.

The input is the same native run `kr_4b76e077273881e3` as the initial
[graph diagnostic](NATIVE_GRAPH_EXPERIMENT_2026-09-28.md), built from the three
architecture sources with external model calls and priors disabled. The snapshot
SHA256 is `e1d2915db13e02afb4aa4af8a87c91c3bf2e71625085c89b10b417db0db4dd01`.
The new [result JSON](results/native-assertion-2026-09-28.json) records source and
dependency hashes, selection, exclusions, graph sizes, unknowns, losses, bounded
search coverage, source-assessment dimensions and timings. No baseline files or
native records were modified.

## Selection and scope

Of 2,182 Claims, 238 are source-assessed observed and active. Excluded from the
root sample are 1,861 absent, 68 extrapolated, one derived, and 14 observed but
contested Claims. These records remain in the source snapshot.

The 24 seed Claims are balanced by predicate, then by primary support unit, then
ascending Claim ID. All nine eligible predicates occur: `decides` once,
`states_principle` twice, and each of the other seven predicates three times.
This is a sampling choice, not an estimate of the native predicate distribution.
The primary support unit is the smallest resolved unit ID for multi-unit support;
unit independence and topic identity are unverified. Seeds span three declared
unit groups with counts 13, eight and three. No domain labels were inferred.

Each seed may include up to three eligible contexts, prioritizing explicit
premise/counter/consequence references, then same-unit/same-subject Claims. The
latter are only co-occurrence metadata; no graph relation is invented. The 24
views contain 32 unique Claims and 78 root occurrences, including one repeated
four-Claim set. There are 214 eligible omitted seeds, 206 eligible Claims outside
all views, and 912 omitted co-occurrence context occurrences under the bound.
Overlapping views are not independent observations.

The entire eligible observed-active set contains **zero** explicit premise,
counter or consequence Claim references. Across the full snapshot there are 92
counter-Claim references on the excluded contested Claims, and zero premise or
consequence Claim references. The result does not claim those unselected counter
relationships are absent, invalid, or exhaust all possible native semantics.

## Explicit comparison projections

[assertion_view.py](../../loom/tools/structure/assertion_view.py) consumes the
frozen native core projection. It retains selected Claim roots, subject,
predicate, object/value, scope and logical qualifiers, shared identity bindings,
entity kind, slot roles, and dependency/derivation ports. It moves evidence
neighborhoods and source-assessment labels to reversible sidecars. Unselected
Claim targets remain reference ports. Full source attribution and assessments
remain available and no source status changes.

The three runs share identical selected records and bounds. Semantic exact mode
requires literal reference identities. Structural exact mode explicitly allows
consistent reference renaming while keeping predicate vocabulary and all other
assertion labels exact. Literal-type control additionally erases literal content;
it is deliberately unsafe and cannot establish analogy or entailment. Motif
discovery retains the exact lexical identities in each supplied view in every
mode; the retrieval renaming experiment is separate.

| Measurement | Semantic exact | Structural exact | Literal-type control |
|---|---:|---:|---:|
| Retrieval candidate pairs | 552 | 552 | 552 |
| Safe-filter survivors / verified matches | 2 / 2 | 2 / 2 | 4 / 4 |
| Matches with disjoint selected Claims | 0 | 0 | 0 |
| Matches across declared unit groups | 0 | 0 | 0 |
| Recurring motifs with at least two Claim roots | 20 | 20 | 20 |
| Those motifs with explicit Claim dependencies | 0 | 0 | 0 |
| Recurring motifs with one Claim root | 74 | 74 | 74 |
| Recurring motifs without Claim roots | 9 | 9 | 9 |

The exact two matches are reciprocal matches of the repeated four-Claim set.
Even the unsafe control only adds overlapping-source matches. Motif counts are
rooted neighborhood candidates, not unique reasoning operations or general laws.
The multi-Claim motifs are explicitly classified as having no Claim-dependency
edges; shared subject/predicate/value neighborhoods can recur without expressing
an argument.

All 768 requested radius-one/two node neighborhoods were enumerated per mode
under the 1,536 enumeration limit; six radius views duplicated an earlier view.
No neighborhood exceeded the 24-node bound. There were 356 verified recurrence
joins, no verification unknowns, and no top-k omissions. Completeness applies to
these requested finite neighborhoods, not all subgraphs or all source Claims.

## Evidence, unknowns and overhead

Before the assertion projection, selected core views contain 553 observation
reference occurrences, including 473 outside their selected Claims' direct
support. Shared entity provenance explains this expansion; it does not supply
new support for each Claim. All 553 move out of comparison shape. No counter-
observation ports occur in this selected sample. Assertion graphs have four to
27 vertices. Every graph was exactly reconstructed from its sidecars, across
all 72 projection executions.

Each mode retains 94 missing-reference unknown occurrences and 55 opaque optional
record-semantics unknown occurrences from the source projection. Moving a
reference outside shape does not resolve it. Missing export references are not
assertions that the corresponding data are absent from the native system.

Projection plus roundtrip took roughly 10.1–10.5 seconds per mode; retrieval
0.84–0.90 seconds; motifs 1.03–1.09 seconds. These are single-run elapsed
diagnostic measurements. Full source copying and reversible sidecars account for
work outside graph matching; no throughput or scaling claim follows.

This experiment changes predicate sampling and context grouping as well as the
projection relative to the initial diagnostic. Consequently the before/after
retrieval or motif difference does not isolate the causal effect of evidence
removal. The directly measured projection effect is loss-audited shape reduction.
The source scan independently identifies the missing explicit argument links in
the selected evidence/status class. Neither observation proves source text lacks
arguments: native extraction may not yet encode them as Claim dependencies.

## Reproduction

From repository root, write to a new output path:

```sh
python loom/tools/structure/native_assertion_experiment.py \
  --database /tmp/loom-graph-native-yksxvm_2/runtime/chatadhd.db \
  --run kr_4b76e077273881e3 \
  --record-budget 24 --context-limit 3 --state-budget 1000 --top-k 3 \
  --output /tmp/native-assertion-repeat.json
```

Alternatively replace `--database` with `--snapshot` and the exported snapshot
path. Source run identity must match. Dependencies are hashed before and after
execution; changes abort the report. Existing output files are not overwritten.
No native build, graph mutation, promotion, model call or parser grammar change
is performed. Eight assertion-view tests and three source-selection tests use
author examples only; independent fixture contents and new evaluation outcomes
were not inspected.
