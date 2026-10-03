# Secondary DEV profile: union of channel top-K candidate sets

2026-09-30 UTC, after the primary first scores/results were inspected and before
this secondary profile was measured. This is an explicitly development-inspired
retrieval diagnostic. The primary max-score union, raw vectors and first results
remain frozen. No feature, encoder, weight, threshold or gold label is modified.
Validation remains sealed.

Observed diagnostic: primary max-score fusion can demote a candidate retained by
another channel at the same shared rank budget K. Hypothesis: preserve each
channel's top-K membership before fusion, with no veto/truncation by another
channel, and measure its recall/cost/precision tradeoff. This is retrieval
conditioned on supplied edges, not graph discovery or ternary source judgment.

Fixed channels: token cosine, character3–5cosine, frozen MiniLM cosine. Fixed
K:1/3/5, same values as the primary preregistration. Take set union of each
channel's top-K eligible turns; retain provenance for channel membership. Do not
rerank or trim this union by a shared cosine. Exact score ties keep the original
frozen ranking order. The union has at most3K distinct candidates, often fewer.
Gold is used only after selection to evaluate exact annotated evidence.

Report unique candidate counts (total/min/max/mean/perquery), precision of selected
turns, evidence recall and query hit rates with original denominators. Compare
against each primary method at K and3K, and a **query-specific matched cardinality**
control: each primary method may select the same number N of turns as the set
union selected for that query. N is derived from predictions/channel disagreement,
never from gold. This controls actual context cost; neither a larger candidate
budget nor a tiny candidate pool can be mistaken for improved ranking quality.
At K>=3 the pool may be exhausted, producing a ceiling rather than discrimination.
Preserve at least one case where an individual channel's evidence hit is lost
by shared max-score top1 ranking; record the retrieval/scoring cause.

No new ternary judgments or truth flags are generated. All proposed candidates
retain source identity, prefix/as_of and unverified content semantics. A later
validation run requires root release and this complete profile/scorer freeze.
