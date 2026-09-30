# First results: eight selected DEV key-control calls

Executed **8/8 calls, 16/16 available binary decisions**, at provider-reported
cost **USD 0.000249312**. There are 4 TP, 12 TN, 0 FP and 0 FN among the new
decisions. The evidence archive's **43 payload hashes** pass independent audit;
relocated offline replay reproduces the saved first score byte for byte.

The comparison uses four cases selected after first32 DEV outcomes and the
immutable earlier meaningful-object/string comparators. **All four arms score
4/4 on each task.** Expressed has **0 positive / 4 negative gold labels per
arm**: positive precision and recall are undefined, and all-negative already
scores 4/4 with Brier 0. Inferred has **2 positive / 2 negative** per arm, with
2 TP and 2 TN; its constant-label baseline scores 2/4.

Neutral/nonsense object arms change only instruction key names after verifying
source, criteria and instruction value order. Their probability differences and
small selected-subset Brier differences do not identify a general format winner.
There is one physical response per case/arm, and the older string arm is not a
key-only control. Inferred yes remains a scoped interpretation, not a fact.

**Decision: preserve and investigate; no format promotion.** Full probabilities,
denominators, Brier values, receipts and replay checks are in `AUDIT.md/json`.
Original plans, first responses, code, gold and comparators remain unchanged.
