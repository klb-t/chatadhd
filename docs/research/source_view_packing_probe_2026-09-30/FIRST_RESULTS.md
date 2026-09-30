# First results: selected DEV question packing and replication

All **24/24 declared physical measurements** completed; every one of the
12 cells has both planned observations. Provider-reported raw cost is **USD
0.000751884**. Independent offline replay reproduced both first compiled
responses and first score exactly. All 24 request-body bindings, raw hashes,
probabilities and raw/ledger costs match. The pre-outcome freeze precedes the
first actual request. No new API, validation access or frozen-result edit was
used in the audit.

A is original historical q01+q02; B changes only q02 to active refutation;
C is the identical q01 alone. Both measurements preserve each original inner
query ID and source state. The four queries were selected **after original DEV
outcomes**: three largest q01 deltas and one exact stable control. They are not
a representative or blind sample.

| Selected query | A: two q01 values | B: two q01 values | C: two q01 values | B−A mean | Within A / B / C ranges |
|---|---|---|---|---:|---|
| `svlv1_dev_001_q4` | 0.21, 0.15 | 0.18, 0.11 | 0.19, 0.16 | −0.035 | 0.06 / 0.07 / 0.03 |
| `svlv1_dev_012_q2` | 0.08, 0.07 | 0.08, 0.08 | 0.07, 0.07 | +0.005 | 0.01 / 0 / 0 |
| `svlv1_dev_005_q3` | 0.07, 0.07 | 0.07, 0.06 | 0.07, 0.07 | −0.005 | 0 / 0.01 / 0 |
| `svlv1_dev_001_q1` | 0.97, 0.97 | 0.97, 0.97 | 0.97, 0.97 | 0 | 0 / 0 / 0 |

**Decision: investigate; no identified causal packing effect.** The largest
selected mean contrast, absolute 0.035, is smaller than the observed ranges of
0.06 and 0.07 within repeated equal-body A/B requests. The other 0.005 contrasts
are comparable with a 0.01 within-condition range. q01 alone also varies on the
largest-delta query, with range 0.03. The exact stable control stays 0.97 in all
six observations.

This demonstrates observed response variation under identical bodies in these
declared physical measurements. With **two observations per cell**, selected
queries and fixed condition order, it cannot quantify general stochasticity,
exclude provider/time/order effects, prove packing independence, or separate
variation from a packing contribution. Every first probability is retained;
no additional measurement is silently substituted for an inconvenient outcome.
Identical hard decisions are not a proof of correctness.

All q02 values are retained for A/B. One additional observation cautions against
calling the original recipe improvement stable: active q02 for
`svlv1_dev_012_q2` was **0.44** in original SOURCE_VIEW v2 and is **0.52, 0.51**
in these two B measurements, crossing the same 0.5 boundary. This is a preserved
probability observation, not a new gold-quality score. Original SOURCE_VIEW
**42/48 and 45/48 remain unchanged**, and no calibration or world-truth claim is
made.

`AUDIT.json` records all 24 raw receipt/binding reviews, all 12 means/ranges,
four complete A/B/C contrasts, current artifact hashes and exact replay checks.
`first_score/score_first.json` remains the frozen primary probe output. Further
work, if valuable, needs a separately frozen design with more observations,
broader unselected inputs and an order control; these 24 results alone do not
choose a production format.
