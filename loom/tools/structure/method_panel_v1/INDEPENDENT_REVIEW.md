# Independent scorer review

Reviewed 2026-09-30 UTC by the existing sibling research agent after first
model-free results and the separate embedding addendum were frozen. Read-only
review; no encoder, vector, score, threshold, fixture or production edit occurred.

Using separate recalculation code, the reviewer confirmed:

- All 13 model-free methods × four predeclared thresholds: TP/FP/TN/FN,
  planned/represented denominators and positive/negative abstentions match.
- AUROC half-ties and average precision on complete score-tie blocks match.
- All 48 token/character/word-path/kernel scores independently regenerate
  within 1e-14; exact-source deduplication gives 64 texts, 9,241 character
  features and 833 word-bigram features; document-frequency hashes match.
- 10/10 synthetic mechanism tests pass independently.
- Gold/family/split labels enter `summarize` only, not feature/extraction
  computation. The nongating union uses the maximum available score; unknown
  or negative channels cannot veto a positive channel.
- At 0.50: union TP17/FP16/TN0/FN15; explicit alignment TP7/FP2/TN2/FN1,
  with 24 positive and 12 negative abstentions.
- Frozen learned embedding: TP22/FP16/TN0/FN10; AUROC exactly 5/512 =
  0.009765625; tie-block AP 0.4617357683081786. Raw score artifact remains
  `prediction:null`; no fitting/threshold polarity reversal followed gold read.

No scoped scoring blocker was found. These checks verify the measurement
mechanism, not independent semantic generalization. The low text-embedding
AUROC concerns this authored operation-and-role target; it is not a global
model-quality estimate.
