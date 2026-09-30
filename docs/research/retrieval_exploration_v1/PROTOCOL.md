# Conditional source-evidence retrieval: second exploratory DEV panel

This protocol precedes this panel's new scores. The previous DEV cosine results
have already been seen; these are exploratory alternatives, not preregistered
independent validation. Only the existing 24 DEV conversations and 96 supplied
edge queries are authorized. No validation or old holdout, paid API, new model
download, canonical graph mutation, threshold fitting or end-to-end quality claim.

H1: prefix-local BM25 may improve evidence ranking over cosine because it weights
rare terms and controls document length. Freeze k1=1.2, b=.75, with b=0 and b=1
as declared length-normalization controls. IDF is fit separately to each query's
eligible full-turn candidate pool, never to future or other-case source turns.

H2: rank fusion avoids comparing differently calibrated cosine/BM25 magnitudes.
Compare RRF rank constants 10 and 60 for lexical/character/MiniLM, and 60 for
BM25+MiniLM. No zero-scoring candidate disappears: RRF fuses complete available
rankings. Separately measure membership union of each declared channel's top-1,
with each individual channel selected at the SAME query-specific cardinality.

H3: supplied role/direction and query wording may alter MiniLM and BM25 evidence
ranking. Eight deterministic query views: unchanged structured fields; endpoints;
forward natural-language conditional; explicit role labels; forward question;
explicit denial question; reversed direction question; supplied endpoint aliases.
Only English/Polish templates and supplied inventory text/aliases/actor are used.
The reverse view is an evidence-search control, not an assertion of reverse
implication. Preserve negation and direction in all source text; no stopwords,
synonyms, gold labels, inferred nodes or future turns enter features.

H4: explicit token endpoint coverage/order and a SOFT same-speaker bonus may
help role-sensitive retrieval, but cannot establish quotation attribution or
source judgment. Measure plain endpoint coverage, +.25 forward-order bonus,
BM25+.5 speaker equality, and BM25+1 speaker equality. Equality is literal
casefolded speaker ID versus query actor. Never veto other speakers: quotations
can ground an assertion attributed to a named person who is not the turn speaker.

Reuse the exact pinned offline multilingual MiniLM instrument already cached.
Encode forward/typed/question/denial/reverse variants against unchanged candidate
speaker/text representations. Preserve vectors, token counts/truncation, runtime,
weights/instrument hashes. Compare single variants, max of forward/denial/reverse,
and RRF60 of those three. Similarities are not probabilities or world-fact scores.

Primary endpoints: annotated turn/span precision and recall, hit@1/@2, first
evidence MRR; report 60 positive-evidence queries and 66 annotated spans separately
from 36 unknown queries. The full pool has 252 query-turn candidates, at most 3
per query, so top-3 saturation is a mechanical ceiling. No ternary classification
is claimed for any new method. Reuse the already-frozen gold evidence evaluation
function only AFTER first scores/vectors are frozen. Candidate prefixes/known_at,
source byte hashes and every planned query remain in the denominator.

Preserve family/language/label stratification, every per-query ranking and gain/
loss against token and MiniLM top-1. Diagnose channel-only hits and compare set
union at matched context budgets. Every series concludes keep/revert/investigate;
no best variant is promoted from this inspected, correlated synthetic DEV panel.
Code/config/protocol dependencies are hashed before execution; first results are
immutable. Synthetic tests verify BM25 math, IDF prefix isolation, ranking ties,
RRF rank—not score—behavior, soft actor rules, direction and negation preservation,
zero-score availability and empty-prefix abstention. Independent review is needed
before treating any method as a frozen finalist for a later release.

The first execution failed before any encoding or score output: the initial
renderer explicitly registered only `implies`, while 24 supplied DEV queries use
`causes`. `FIRST_FAILURE.json` preserves that failure and the original freeze.
Freeze2 registers both supplied relation types and renders causation as
cause/powoduje rather than implication. No outcome was observed or used to change
the method. This is a representation-contract correction, not quality tuning.
