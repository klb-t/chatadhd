# Jev: first measured structural judgments

Run https://github.com/klb-t/chatadhd/actions/runs/36449503844 completed all
**64 first requests / 768 Noul decisions** with no paid retry. Activation:
`8dc01c717ef20b1c166a30ef1560b4e06b26ce56`. Returned model:
`typesafe/jev-1.13-20260917`, provider TypeSafe.

Original archive: `inputs/jev-structure-results-2026-09-28.zip`, artifact
10981838362; verified SHA-256:
`0de198b136ecf32789be4da5a55c2c5730b1b1e5953fa500fbd13eaf4c9de63e`.
Additional aggregate calculations: `inputs/jev-structure-analysis-2026-09-28.json`.
Frozen protocol and corpus: `JEV_LIVE_PROTOCOL_2026-09-28.md` and
`loom/tests/fixtures/eval/jev_structure_pilot_v1/`.

## What actually worked

All responses satisfied the typed output contract. Reported credit cost was
**USD 0.005341182**, with 127,171 input tokens and 14,080 free output tokens.
Per-request latency: median 0.183 s, nearest-rank p95 7.680 s, maximum 10.273 s.
Sequential execution took 150.731 s, including auxiliary metadata requests.
The optional generation audit was unavailable for all 64 replies; the complete
cost total above comes from documented response usage, not a claimed second
independent billing verification. The key's before/after data can include the
concurrent native experiment and is not attributed solely to Jev.

| Metric, fixed threshold 0.5 | All | Polish | English |
| --- | ---: | ---: | ---: |
| Correct individual decisions | 742/768 | 369/384 | 373/384 |
| Accuracy | 96.61% | 96.09% | 97.14% |
| Positive precision | 71.74% | 68.75% | 75.00% |
| Positive recall | 100% | 100% | 100% |
| Positive F1 | 83.54% | 81.48% | 85.71% |
| Completely correct 12-answer vectors | 39/64 | 18/32 | 21/32 |

The labels contain only 66 positive judgments. **Always answering no already
scores 91.41% accuracy**. Actual confusion counts are TP 66, TN 676, FP 26, FN 0;
balanced accuracy is 98.15%, Brier score 0.02987. Do not present the headline
accuracy as a near-perfect graph extractor: complete vectors are correct in only
60.94% of these cases. The 12-bit vector is not a unique representation of meaning.

Development positive precision/recall: 75%/100%; validation: 66.67%/100%.
Both used frozen questions and thresholds, with no intermediate tuning. These
are four authored families per split, not an independent natural-archive sample.

## Invariance and sensitivity

| Pair transformation | Same individual decisions | Same full vectors | Both full vectors correct |
| --- | ---: | ---: | ---: |
| Synonym/paraphrase | 188/192 | 12/16 | 8/16 |
| Different domain, intended same abstract pattern | 190/192 | 14/16 | 7/16 |

All 26 deliberately changed foil bits changed their predicted decision. However,
the exact intended change mask was preserved in only 10/16 foil pairs: six pairs
also changed an unintended bit. Consistent errors account for four paraphrase
pairs and seven domain-transfer pairs. Therefore invariance alone is not accuracy.
Whole-vector agreement between paired Polish and English texts is 28/32.

## Errors and a limitation of the questions themselves

All 26 mismatches were extra positive labels: 14 on implication q01, six on
support q07, five on causation q11 and one on universality q03.

- Five high-confidence mismatches, p=0.81–0.86, classified causal sentences as
  P→Q despite q01 explicitly requiring locally defined P/Q roles. Seeing a
  conditional pattern in a causal statement is not absurd. It violates this
  narrow question contract, but may be useful as a separately marked inferred
  abstraction. The question's literal-role prerequisite does not fully match the
  owner's goal of discovering latent structure in ordinary language.
- Analogies/identity descriptions received an inferred evidential target T.
  A structural hypothesis must identify its roles; the classifier must not
  silently choose a different target than the one the caller is evaluating.
- Some conditional sentences received causal interpretation, such as the
  sensor–alarm example (PL 0.72, EN 0.62). A plausible mechanism is stronger
  than the relation explicitly expressed by an implication.
- The universal-rule mismatch at 0.53 is interpretation-sensitive: a sufficient
  condition can pragmatically read as general, while this rubric distinguishes
  category quantification from a proposition-level conditional.

At the predeclared selective thresholds <=0.2 or >=0.8, 673 decisions remain,
with five errors. Positive decisions alone are **63 correct out of 68**, or
92.65% precision. The much smaller overall error rate, 5/673, is dominated by
605 easy negative decisions. This is not a calibrated production safety promise.

The source texts and labels remain unchanged. A direct text-pair comparison is
frozen but unrun as a separate exploratory follow-up: ask whether two passages
instantiate the same declared operation/role pattern, without requiring literal
P/Q labels. Reuse of these already inspected texts is not a fresh holdout.

## Consequence for the system

Jev is promising for fast, inexpensive judgments about candidate structures.
These measurements do not establish that it is the best classifier, that it can
freely discover every structure, or that it outperforms the native graph extractor:
the two experiments ask different tasks. The native prompt also carries graph
serialization and source-coordinate obligations absent from this Jev test.

Use the rules in `JEV_USAGE_RULES_2026-09-28.md`: keep represented and inferred
structure distinguishable, preserve role bindings and provenance, and use lexical
checks only as secondary omission diagnostics. Nothing here promotes model output
directly into the authoritative graph.

## Actions quota update

The owner reported exhausted GitHub Actions minutes on 2026-09-28. The completed
64-request experiment is unaffected. The frozen 48-pair direct-comparison corpus
at `loom/tests/fixtures/eval/jev_structure_pairs_v1/` has **not been run**.
Its disabled request is `docs/research/openrouter-jev-pairs-request.json`.
No result for that follow-up may be inferred from the first experiment.
