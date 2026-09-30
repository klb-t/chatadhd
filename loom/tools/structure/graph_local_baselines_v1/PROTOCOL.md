# Graph-panel local retrieval baselines v1 — frozen DEV protocol

Frozen 2026-09-30 before local scores/vectors/gold evaluation. Only the 24
`graph_methods_panel_v1/inputs_dev.json` conversations and their 96 supplied-edge
queries are used. Validation inputs and gold remain sealed. No paid/provider API,
model download, training, threshold search or fixture change is authorized here.
The existing cached pinned multilingual MiniLM instrument may be reused offline.
Root accepted this design; root disclosed the always-unknown aggregate reference
36/96 before scoring. No per-query gold labels are supplied to feature generation.

## Scope, hypotheses and eligibility

This is **retrieval conditioned on a supplied candidate edge** (inventory endpoints,
relation, attribution, scope, cutoff), not archive-wide recall or free graph
discovery. Full source turns are the candidate evidence units. This preserves
quotation/correction context and matches the fixture's full-turn evidence targets.
Every query uses the unchanged `graph_panel_live.query_payload`: physically
eligible turns satisfy `known_at <= as_of`. Empty prefixes abstain. No candidate
from a future turn may appear in a row. Node inventory is the same supplied input
as in the live judge track; it is not an oracle assertion or discovered entity set.

H1: lexical/character/learned cosine can rank relevant evidence while conflating
assertion, denial, quotation, direction, attribution and temporal status. The
primary criterion is evidence rank/recall, separately from ternary judgments.
H2: maximum-score union can recover a candidate missed by one channel without
another channel vetoing it. Its evidence recall gain and false relevance are
measured; no structural or truth claim follows from the union.
H3 (negative control): a naive high-cosine=>supported recipe will miss explicit
refutation and may beat class-imbalanced accuracy accidentally. Do not describe
it as a successful source-graph classifier or treat silence as denial.

## Exact frozen representations and methods

Query string, unchanged field order, no aliases or other nodes:

```
attribution: {query.attributed_to}
relation: {query.relation}
source: {inventory[query.source].text}
target: {inventory[query.target].text}
```

Candidate string:

```
speaker: {turn.speaker}
text: {turn.text}
```

Raw texts, source/turn IDs, timestamps and original eligible payloads remain
preserved independently; these strings are rebuildable projections. The English
field labels also apply to Polish cases, a fixed representation limitation.
Neither aliases, case family, gold, expected edges nor future source turns enter
a query's string or candidate pool.

- Token-count cosine: casefolded Unicode `\w+` frequencies; no stemming or
  stopword deletion.
- Character cosine: whitespace-normalized casefolded 3–5-gram frequency counts.
  No global IDF fitting or corpus statistics: future turns cannot affect earlier
  feature weights. This is not a learned semantic embedding.
- Learned cosine: the exact cached MiniLM model revision/tokenizer/pooling/L2/CPU
  instrument frozen in `local_embedding_panel_v1`; reuse public local artifacts,
  max128 tokens,384dimensions. Report every truncation and model/vector/code hash.
  No query-specific fine-tuning or representation adjustment follows gold read.
- Nongating union: maximum available token/character/learned cosine for each
  eligible candidate. Any channel can add a candidate; no exclusion/veto by a
  different channel. No extra weights, min score or hidden attribution filter.

Sort by score descending; exact score ties use original eligible turn order,
then turn ID. Preserve ties and candidate identity. Embeddings are stateless:
shared vectors may cache identical strings across eligible query payloads, but
candidate selection always uses that query's physically supplied prefix. No
cross-query TF/IDF or statistical fitting is performed.

## Availability and diagnostic labels

Every raw row has `prediction:null`, `source_judgment_available:false` and its
retrieval availability/prefix/source hashes. Retrieval capability is not semantic
judgment capability. Always-unknown is a separate frozen ternary reference.

Only for a separately labelled **naive diagnostic**: if the maximum eligible
candidate similarity is >=t, emit supported; otherwise unknown. Never emit
refuted from a missing/low score. Empty/unavailable candidate scores abstain.
Predeclared generic t: **0.25,0.50 primary descriptive,0.75,0.90**. These are not
calibrated probabilities and no best threshold will be selected. The unmodified
`graph_panel_live.score_judgments` reports3×3confusion, per-class P/R, coverage,
macro averages and accuracy on the matched96queries. Content truth remains
unverified and no canonical graph is modified.

## Gold-bound evaluation after scores freeze

Only then read `gold_dev.json`. Join labels/IDs in evaluation only. Ground relevant
evidence for supported queries in their gold assertion references. For refuted
queries include explicit denials/withdrawal evidence by cutoff, including the
applicable status event for a superseded assertion. Preserve relation/direction/
attribution and temporal eligibility when deriving these evaluation references.
Unknown queries have no positive relevance annotation; absence of gold evidence
is not a factual refutation.

Report eligible annotated evidence counts, query hit@1/3/5, evidence recall@1/3/5,
reciprocal first evidence rank, thresholded relevance precision/recall and unknown
false-relevance counts, with all denominators. Report evidence/score availability
separately, including out-of-prefix annotations and mechanically excluded future
turn counts. Full-turn relevance is not clause-adequacy or free edge-extraction
quality. Queries within conversations and six cases within each dev family are
correlated; report family, language and supported/refuted/unknown separately.

Preserve raw vectors, scores, prefix payloads, first results, failures and hashes.
Run synthetic mechanism tests for time cutoff, ties, source identity, missing
scores, nongating union and naive/refutation semantics. Then run affected existing
adapter regression. Freeze code/config/scorer before any validation release. No
validation file is read by this arm unless root explicitly unseals it later.
