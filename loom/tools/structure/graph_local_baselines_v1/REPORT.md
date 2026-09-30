# Local graph-panel DEV baselines: evidence retrieval versus source judgment

**Cosine helps retrieve source evidence; it does not establish a directed,
attributed supported/refuted/unknown relation.** On this new development panel,
frozen MiniLM ranks an annotated evidence turn first for 46/60 queries with
evidence, versus token 45/60 and character 44/60. Naively turning cosine into
`supported` misses every refutation: 0/24. The independently checked measurements
keep these capabilities and denominators separate.

Only 24 new DEV conversations / 96 supplied-edge queries were used. Validation inputs
and gold remain sealed and unread. Query endpoints, relation, attribution and
node inventory are supplied, so this measures **conditional evidence retrieval**,
not archive-wide recall, node discovery or source-only graph extraction. The
four development families are controlled variations; 96 queries are not 96 independent
natural-text trials. No paid API, new download, training or threshold fit occurred.

## Frozen instruments and matched source views

`PROTOCOL.md`, `policy.json` and `freeze_before_scores.json` precede all new scores.
The unchanged `graph_panel_live.query_payload` physically supplies only turns with
`known_at<=as_of`. Exact prefix/source hashes and full-turn Unicode/UTF8spans are
preserved in `prepared_inputs.json`. All 96 prefixes and 252 candidate bindings were
independently checked; zero future candidates leak. 18 queries have a truncated
past prefix; 36 query/turn future candidates are excluded. Candidate pools contain
one turn for 18 queries and three turns for 78 queries.

Features receive only the frozen directed query surface and candidate turn/speaker
surface. Gold enters only evaluation after `freeze_before_gold.json`. Token
counts and character 3–5 counts have no global IDF, avoiding future corpus-frequency
leakage. MiniLM reuses the exact cached public model/tokenizer/pooling/normalization
instrument offline: 146 unique strings, 384 dimensions, 0 truncations, 1.632 seconds
inference excluding model initialization. Vectors/model/code/runtime/tokenization
hashes remain saved. It is a learned text encoder, not a graph-role encoder.

Every raw retrieval row has `prediction:null` and
`source_judgment_available:false`. Numeric score availability is 96/96, but that
is not semantic source-judgment capability. No canonical edge, content truth,
world fact or source confidence is promoted from a similarity score.

## Gold-bound evidence ranking

Gold labels: supported 36, refuted 24, unknown 36. 60 queries have explicit assertion,
denial or withdrawal evidence; there are 66 distinct query-evidence spans, including
repeated supporting sources. Six applicable supersession/status-event references
are incorporated into refutation evidence at their cutoffs. Duplicate spans are
deduplicated; prior support is not treated as current withdrawal evidence. No
annotation is outside its eligible prefix. All spans are full-turn targets;
retrieval does not measure clause adequacy or true graph-edge extraction.

| Method | Evidence hit@1 /60 queries | Span recall@1 /66 spans | First-evidence MRR | Span recall at score≥0.50 | Selected-turn precision at0.50 |
|---|---:|---:|---:|---:|---:|
| Token cosine | 45/60=75.0% | 45/66=68.2% |0.8750|53/66=80.3%|53/129=41.1%|
| Character cosine |44/60=73.3%|44/66=66.7%|0.8667|29/66=43.9%|29/51=56.9%|
| Learned MiniLM |46/60=76.7%|46/66=69.7%|0.8806|52/66=78.8%|52/162=32.1%|
| Primary max-score union |45/60=75.0%|45/66=68.2%|0.8750|58/66=87.9%|58/186=31.2%|

All methods hit 60/60 queries and cover 66/66 spans at K≥3 because the eligible pool
has at most 3 turns. This is a **pool-exhaustion ceiling**, not strong reranker or
model-quality evidence. The primary informative comparisons are top 1, selected
candidate precision and costs/coverage at the fixed thresholds.

At 0.50 the union adds five evidence spans beyond token53/66, but selects 57 more
turns (186 versus 129). No channel vetoes a candidate at this cutoff. The larger
coverage is not a free quality improvement. False candidate relevance among
unknown queries is 8/36 (character), 28/36 (token), 29/36 (MiniLM), 33/36 (union), meaning
selected candidates have **no annotated positive/negative edge evidence for this
query**. It does not mean that quotation, nonendorsement or silence context is
useless to a real ternary judge. In particular, an unknown query's contextual
turn can correctly help a judge return unknown while remaining outside this
fixture's explicit-edge evidence annotations.

Top 1 hit by family (denominators retain only queries with annotated evidence):

| Family | Token | Character | MiniLM | Max-score union |
|---|---:|---:|---:|---:|
| Attribution |9/12|10/12|10/12|11/12|
| Correction / known_at |12/18|10/18|11/18|10/18|
| Negation / unknown |6/12|6/12|7/12|6/12|
| Paraphrase |18/18|18/18|18/18|18/18|

The simpler token baseline wins the correction family against both MiniLM and
max-score fusion. Family/label/language tables with every denominator remain in
`first_results.json`; no aggregate hides this regression.

## Deliberately naive ternary diagnostics

Separate frozen recipe: maximum candidate cosine≥t⇒supported; otherwise unknown.
It never invents refutation from a low/missing score. The unchanged live adapter
scores this diagnostic on the matched96queries. It is **not** a semantic classifier
claim, and these labels never modify the raw retrieval artifacts or graph.

| Recipe at 0.50 | Supported precision / recall | Refuted precision / recall | Unknown precision / recall | Accuracy /96 |
|---|---|---|---|---:|
| Always unknown |— (0 predictions) /0/36|— (0 predictions) /0/24|36/96 /36/36|36/96|
| Token diagnostic |33/84 /33/36|— (0 predictions) /0/24|8/12 /8/36|41/96|
| Character diagnostic |22/41 /22/36|— (0 predictions) /0/24|28/55 /28/36|50/96|
| MiniLM diagnostic |29/78 /29/36|— (0 predictions) /0/24|7/18 /7/36|36/96|
| Max-score union diagnostic |33/89 /33/36|— (0 predictions) /0/24|3/7 /3/36|36/96|

All have96/96numeric output coverage; all have **0/24refuted recall**. Character's
50/96 accuracy cannot conceal that failure. Macro recall at 0.50 is 0.380 (token),
0.463 (character), 0.333 (MiniLM/union/unknown). Precision for never-predicted refuted
is undefined 0/0, not 100% or an omitted successful class. The machine adapter also
reports its defined-class macro-precision denominator explicitly.

Four generic thresholds were fixed before first scores; none is selected as best:

| Recipe accuracy /96 |0.25|0.50|0.75|0.90|
|---|---:|---:|---:|---:|
| Token |36|41|44|36|
| Character |39|50|36|36|
| MiniLM |36|36|37|36|
| Max-score union |36|36|43|36|

## Secondary top-K set union and matched context budgets

`SECONDARY_SET_UNION_PROTOCOL.md` was frozen **after** primary development results,
before this second measurement. It tests a distinct membership policy, not a
post-result change to encoder, scores, weights or thresholds. Take union of each
channel's top K without reranking/trimming it by another cosine. This preserves all
three channels' proposed candidates at a cost of at most 3K.

At K=1 per channel: 55/60 hit queries and 55/66 evidence spans, 55/138 selected-turn precision;
138 unique candidate turns, mean 1.4375/query (range 1–3), versus 96 turns for a single
top 1 channel. However, query-specific matched-cardinality controls also cover
55/66 spans for token, character, max-score fusion and 54/66 for MiniLM. The apparent
recall gain is largely **additional context budget/allocation**, not evidence of
superior set-union ranking. At K≥3 both union and 3K controls exhaust all 252 candidates,
cover 66/66 spans with 66/252 precision; that ceiling is mechanically expected.

Ten individual-channel evidence hits are lost by shared max-score top 1. Example:
`gpv1_dev_001_q3` requests the analyst's refutation. MiniLM selects analyst denial
turn t3 (score 0.660262), above reporter's quoted conditional t1 (0.470479). Max-score
fusion instead selects t1 because token 0.666667 beats t3 maximum 0.660262. Scores on
these different channels are not calibrated interchangeably. Set union preserves
t3; shared max-score top 2 at its matched two-turn budget also preserves it. This
is a retrieval disagreement/cost diagnostic, not a claim that the quoted source
is false or that an edge has been grounded automatically.

## Decisions and next experiment

- **Keep** lexical,character and learned channels as complementary evidence
  proposals, with attribution/time identity and all per-query scores preserved.
- **Reject** unqualified similarity⇒directed-source-judgment promotion. Every
  naive variant fails refutation; missing relation remains unknown.
- **Investigate** max-score versus membership union using matched context cost;
  retain alternatives. Development shows no intrinsic matched-budget advantage
  for set union, while exposing concrete candidate veto/demotion risks.
- **Next:** compare these exact 96 source-prefix proposals against the separately
  frozen GPT/Jev source judgments, then evaluate frozen finalists once on the
  still-sealed independent-family validation after root release. Measure evidence
  retrieval, source attribution/polarity/direction and semantic judgment separately.
  No additional threshold search or encoder change is justified by this run.

## Reproducibility and review

New synthetic mechanisms 13/13 pass; unchanged graph-adapter regression 20/20 passes.
An independent reviewer verified 6 first-score freeze hashes, all 96 physical
prefixes,252 exact source candidate bindings and 146×384 vectors. Recomputed 252 learned
cosines exactly (max error 0.0); vector norm maximum error 5.89e-8. Independently
recounted 66 evidence spans / 60 queries, six status-event references, primary top1
and ternary tables and thresholded turnTP/FP/FN; no discrepancy found.

All first vectors/scores/results and secondary first results remain preserved.
Gold influences relevance evaluation only. Root's paid GPT/Jev runs are separate;
this arm made zero API calls. Limitations: very small pools, supplied endpoints,
English field labels also on PL texts, correlated synthetic conversations, unmeasured
free extraction, no population generalization and no current validation exposure.
