# Independent DEV measurement review

2026-09-30, read-only review by the existing repository/graph-driver auditor.
No files, vectors, scores, thresholds or existing adapter were changed.

Confirmed six first-score freeze hashes,96exact physically eligible prefixes,
252exact source candidate bindings and146×384learned vectors. All252stored learned
cosines reproduce by independent float64dot accumulation with maximum error0.0;
L2norm maximum error5.89e-8. There are66eligible evidence spans for60nonunknown
queries plus six applicable status-event references.

Independent top1query-hit counts match: character44/60,learned46/60,token45/60,
maxunion45/60. Naive0.50ternary accuracy matches50/96,36/96,41/96,36/96respectively,
versus unknown36/96; every refuted recall is0/24. Thresholded evidence turnTP/FP/FN:
character29/22/37,learned52/110/14,token53/76/13,union58/128/8. No discrepancy found.

Interpretation caution retained in the report: unknown false relevance means
no annotated positive/negative edge evidence, not that contextual evidence for
silence/nonendorsement/quotation is useless to a judge. Full-turn evidence ranking
is not clause adequacy or semantic graph judgment.
