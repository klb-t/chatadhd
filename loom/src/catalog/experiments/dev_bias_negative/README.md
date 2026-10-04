# Rejected DEV intercept calibration, 2026-10-04

This experiment is preserved only on
`archive/2026-10-04/catalog-dev-bias-negative`. It changes the existing relevance
intercept from -3.0 to -2.5 through a complete KB file overlay, with all feature
weights, fictional owner aliases, selection rules and quality thresholds
unchanged. No blind/holdout corpus or provider was used.

The actual native DEV result improves selected relevant conversations from
31/45 to 38/45 with FP 0/20, retaining all originally selected units. It is
rejected under the tracked-metric ratchet: lexical AUC decreases
0.777222 -> 0.775556 and local TF-IDF semantic AUC decreases
0.981111 -> 0.980000. Final-ranking AUC improves 0.965556 -> 0.970000;
hits@45 stay unchanged. Improvements do not excuse those regressions.

`baseline.json`, `negative.json` and both input receipts retain every row,
source/pack/fixture/binary hash and exact configuration. `relevance.json` is
the complete candidate document; `measurement.log` is the original summary.
Recorded local rebased commit 87d8892 has the same tree as the published base
a8b70e2; the uncommitted evaluator enhancement is preserved at its canonical
path on this archive branch. Inputs also list the then-unlinked decoder source;
the native library used the pre-decoder scorer. No label-derived vectors exist.

Reproduction from a completed build of this archive branch:

```sh
python3 loom/src/catalog/tests/evaluate_dev.py \
  --library loom/build/dev/libloom.so.0.1.0 \
  --relevance-overlay loom/src/catalog/experiments/dev_bias_negative/relevance.json \
  --output /tmp/catalog-dev-bias-negative.json
```

Only the public synthetic DEV corpus is opened by that runner. The built-in
default remains -3.0; no rejected policy is installed in production. This is
calibration on training data, not independent model-quality evidence.
