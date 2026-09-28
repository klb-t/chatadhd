# Initial independent graph-native results

The frozen matcher found every oracle-positive represented graph relation and
rejected every oracle-negative relation at a state budget of 10,000. All 72
positive node/edge witnesses were independently valid. This is a small controlled
graph result, not evidence of broad natural-language understanding.

## Retrieval and exact verification

The following values hold separately for development and fresh validation;
each row in each split has 38 candidate judgments and six ranked queries.

| Goal | Positive / negative correct | Filter recall | mAP | MRR | Recall@1 | Recall@3 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Exact represented semantic identity | 6 / 32 | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 |
| Literal template containment | 12 / 26 | 1.000 | 1.000 | 1.000 | 0.500 | 1.000 |
| Structural analogy | 18 / 20 | 1.000 | 0.940 | 0.917 | 0.278 | 0.944 |

Recall@1 is the fraction of all relevant candidates returned in one slot. Each
containment query has two relevant candidates and each analogy query has three,
so perfect rank-one choices would produce 0.500 and 0.333 respectively.

One analogy query per split ranked a lexical hard negative before a relevant
motif. The candidate filter retained those hard negatives; exact verification
rejected them. The practical result favors a retrieval-then-witness flow when a
query requires a verified relation. It does not show that ranking alone proves
analogy or that an unexamined top-K cutoff retains all matches.

At budget 1, 74/228 comparisons correctly returned `budget_exhausted` and 154
returned `different`; there were no false positive or false negative decisions,
and no state-budget overruns. No positive claim is licensed by exhaustion. Both
missing/invalid graph probes were rejected. All three ranking repeats produced
the same results.

Median local ranking latency in milliseconds, over the six query medians per
split:

| Goal | Development | Validation |
| --- | ---: | ---: |
| Exact represented identity | 0.396 | 0.524 |
| Template containment | 0.875 | 1.147 |
| Structural analogy | 1.169 | 1.471 |

These are tiny-graph descriptive measurements, three calls per query. They do
not establish large-corpus throughput, index efficiency, memory use, or worst
case matching complexity. `initial_search_report.json` contains the raw results.

## Native Claim projection

| Measured property | Semantic | Structural | Topology control |
| --- | ---: | ---: | ---: |
| Untouched copied exports | 16/16 | 16/16 | 16/16 |
| Selected Claim restrictions in actual graph | 16/16 | 16/16 | 0/16 |
| Exact per-Support Observation/source identity | 16/16 | 16/16 | 0/16 |
| Claims made inference-eligible | 0 | 0 | 0 |

All 36 relationship-mode checks passed across semantic and structural modes.
This includes attributed quotation versus observed report, possible versus
asserted, relation negation versus epistemic negation, narrower scope, and
repeated versus independently acquired support. Three Support entries referring
to one physical source remain one unique source; two separately acquired
sources remain two. Both preserve the underlying Claim content.

All 14 raw-source hashes and 20 Observation quotation spans verify. The separate
native fixture integrity check also verified all 22 Support byte spans. Semantic
projection preserved quotations for all 22 Support entries. Structural mode
explicitly omits lexical descriptions, raw location coordinates and quotation
text from comparison attributes while preserving source identities and the full
export. Its losses are listed in the report. Source records themselves are
outside the projector input, so unresolved source-record references are reported
explicitly; their IDs remain represented.

Topology control deliberately strips semantic distinctions. Its untouched
source copy therefore cannot serve as evidence that the comparison graph keeps
them. No projection enabled inference, persistence, or automatic mutation.
`initial_core_projection_report.json` retains the actual graphs and per-case
independent reconstruction checks.

## Limits and unmeasured work

This benchmark measures supplied structured graphs and supplied abstraction
maps. It does not measure extraction, learned abstraction, owner-history
relevance, open-domain truth, calibration, motif discovery, integrated production
flow, or LLM interpretation. The native extension checks selected explicitly
annotated semantics; it does not prove every native field or entity hierarchy
has a lossless graph representation. Development and validation are different
motifs/domains but small and procedurally controlled, so matching scores should
not be extrapolated to unrestricted conversation graphs.
