# First mechanical result, preserved

The first targeted run executed 16 synthetic checks: 15 passed, one failed.
`test_unmapped_premise_is_fp_and_does_not_hide_missing_gold` changed premise AB's
target from B to C. The strict correspondence correctly mapped that record to
the already authored AC premise. The test incorrectly expected it to be
unmapped: observed unique-premise TP2/FP0/FN2, expected TP1/FP1/FN3.

Decision: correct the invented counterexample, not the method or quality gate.
The repaired test changes attribution to an unannotated actor, which identifies
no gold assertion. It then measures the intended unmapped-premise behavior.
All 16 checks passed afterward. Four further integrity checks were added before
the final freeze; the final targeted suite is 20/20. No actual model output,
validation label, threshold or imported frozen method changed.
