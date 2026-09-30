# T7 comparable ranking control: review-informed protocol before implementation

The independent reviewer re-ranked the already saved V2 candidate pools by donor
frequency. This reproduced every capability top-1/top-3 ordered prediction,
including top-1 precision 6/9 and recall 6/10, and improved all-task top-1 to
14/55 precision and 14/46 recall (weighted mapping: 13/55 and 13/46). The
expected outcome is already known. This run is a reproducibility/control
implementation check, not a fresh or blind evaluation.

## Fixed intervention

Add named method `premise_filtered_frequency`. Apply **exactly the same** donor
graph alignment requirement and capability-premise eligibility rules as
`partial_mapping`; retain the full same surviving candidate vocabulary. Rank
solely by count of supporting donor projects, then canonical label, without
weighted Jaccard scores. This isolates ranking from eligibility. Preserve the
unfiltered `most_frequent` and random baselines as complementary references.

New control policy enables four methods. Existing default v1/v2 policies and
their choice of weighted mapping remain unchanged. No weights, thresholds,
masks, controls, candidate budgets, random seeds, split or source data are tuned.
Run in a new immutable directory `synthetic_lopo_v3_ranking_control_first`, with
protocol/policy/code snapshots, gzip outputs and prediction-before-score order.
First v1/v2 outcomes remain untouched.

## Checks and decision

Before run: test that the new control pool matches weighted mapping eligibility,
including no-anchor and absent-premise abstention. Include a counterexample
where weighted rank and frequency rank differ. After run: compare every
unmodified-method score dictionary against corrected V2 and independently
recount the control; expected all-task top-1 14/55 and 14/46, top-3 16/149 and
16/46. Report any discrepancy and investigate rather than changing weights.

Do not claim weighted ranking caused the capability improvement. The result
supports a supplied-premise eligibility mechanism on this development corpus;
the simpler ranking ties capabilities and wins roles. Both policies remain
available as data, and all candidates remain hypotheses.

## Open mechanism limitation recorded before this control

`build_graph` currently unions the principle evidence of multiple independent
applications of one operator, and V2 eligibility requires the complete union.
This can falsely reject a target satisfying one sufficient donor application.
Independent counterexample: applications justified separately by pA and pB
become pA AND pB; target pA is rejected. Preserve alternative application chains
in a later increment. That correction is deliberately excluded from this
ranking control so the compared eligibility pools stay identical.
