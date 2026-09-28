# Jev direct structural pair judgments: measured exploratory follow-up

The frozen 48-pair experiment completed locally on 2026-09-28 with **47/48
correct first judgments**, no paid retries and **USD 0.001540350** reported
response cost. This supports Jev as a candidate structural-comparison judge at
an explicitly declared abstraction level. It does not establish free-form graph
discovery, broad conversation accuracy or superiority to another classifier.

**These are reused, previously inspected authored sources, not a fresh holdout.**
The pair question was frozen before these calls, but designed after inspecting
the earlier 64-state experiment. The inherited split named `validation` is not
independent validation of this new question. Eight families, their translations
and their three contrasts are correlated; 48 calls are not 48 independent tasks.

## Protocol and operational audit

The corpus and frozen question are in
`loom/tests/fixtures/eval/jev_structure_pairs_v1/`. Each request compares two
passages with one fixed Noul question about operation-and-role structure.
The model sees only the passages and question, without family, contrast or gold
labels. Synonyms and domain names should not determine structural equivalence.

All 48 responses passed the typed contract and returned model
`typesafe/jev-1.13-20260917`, provider TypeSafe. There were 36,675 input and 1,056
free output tokens. Recorded request latency: median **2.059389 s**, nearest-rank
p95 **4.356141 s**, maximum **8.413329 s**. The first-attempt-to-last-finish window
was **351.118356 s**, including auxiliary metadata lookups. This window excludes
the initial key preflight and the final key check.

Every response supplied a usable cost; their Decimal sum is USD 0.001540350,
matching the ledger, with zero unknown-cost attempts. All 48 optional generation
audits were unavailable. The final read-only key check failed, leaving the
original terminal reason **`jev_post_key_check_failed`**. Thus the model responses
and their reported costs are complete, while the run lacks that final account
snapshot and independent generation-cost verification. Any later account check
must remain a separate reconciliation, without rewriting this original ledger.

A separate read-only account recheck succeeded at **17:24:06 UTC**; see
`inputs/openrouter-local-recheck-2026-09-28.json`. The remaining credit declined
from USD 1.967436778 to USD 1.965896428, exactly USD 0.001540350, with zero BYOK
usage and the unchanged USD 2 cap. This reconciles the isolated run's response
cost against the account change; it does not make the unavailable per-generation
audit available or erase the original final-check failure.

Original first responses and run metadata are preserved in
`inputs/jev-pairs-first-responses-2026-09-28.zip` (42,511 bytes), SHA-256
`ca9d31fa03ddcce082840e0514c5ddda93278a891b50f5099626e6458de648f0`.

No GitHub Actions were used for this experiment. No response was promoted into
the canonical graph.

## Fixed threshold 0.5

| Group | Correct | TP | TN | FP | FN | Positive precision | Positive recall |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| All | 47/48 | 31 | 16 | 0 | 1 | 100% | 96.875% |
| Polish | 24/24 | 16 | 8 | 0 | 0 | 100% | 100% |
| English | 23/24 | 15 | 8 | 0 | 1 | 100% | 93.75% |

Overall accuracy is 97.9167%, positive F1 98.4127%, balanced accuracy 98.4375%,
and Brier score 0.02219375. An always-positive answer scores 32/48 (66.6667%).
No false positives were observed among these 16 foils; this small authored set
does not establish a zero false-positive rate in production.

| Contrast | Correct | Intended answer |
| --- | ---: | --- |
| Synonym/paraphrase | 16/16 | Same declared structure |
| Different domain | 15/16 | Same declared structure |
| Vocabulary-sharing structural foil | 16/16 | Different declared structure |

These are direct pair-classification results. Gold rows deliberately have
`variant=base` and self `base_case_id`; the generic scorer's legacy invariance
comparison is empty and must not be interpreted as a measured pair metric.

| Family | Correct across two languages and three contrasts |
| --- | ---: |
| Analogy versus identity | 5/6 |
| Causation versus correlation | 6/6 |
| Conditional direction | 6/6 |
| Conjunction versus disjunction | 6/6 |
| Inductive generalization | 6/6 |
| Negation scope | 6/6 |
| Support versus counterexample | 6/6 |
| Universal versus existential | 6/6 |

Seven of eight complete six-case families are correct. Corresponding Polish and
English decisions agree in 23/24 cases and are both correct in those same 23.
Inherited development scores 24/24 and inherited validation 23/24; neither
split supplies a new unseen-source test for this follow-up.

## The exact mismatch and the abstraction boundary

The only error is **`jp_426acda1a0c218da`**, English analogy domain transfer:
gold same-structure, returned probability **0.24**. Its left passage compares
controller–workers with conductor–musicians; its right passage compares
tributaries–river with side-roads–main-road. The Polish counterpart
`jp_d5187ba699226df6` returns **0.57**, technically correct but also uncertain
under the predeclared selective interval.

The frozen rubric explicitly compares the operation of mapping `R(a,b)` to
`R(c,d)` between distinct systems, discarding domain-specific relation meaning
and internal topology of role fillers. Under that coarse projection both texts
express analogy. A finer projection preserving direction of control, fan-out,
fan-in or merging can reasonably separate them. The response contains no
explanation, so we cannot claim the model actually used that finer projection.
This is a contract mismatch under the frozen label, not grounds to relabel the
case after seeing the answer.

The useful implementation lesson is to store which abstraction a comparison
uses. A pair can share an operation-level pattern while differing at a finer
graph level. One scalar should not silently flatten those distinct questions.

## Selective decisions

Keep scores outside the open interval `(low, high)`; boundary values are retained.

| Interval | Status | Retained | Errors retained | Positive decisions correct |
| --- | --- | ---: | ---: | ---: |
| 0.1–0.9 | Descriptive sensitivity only | 39/48 | 0 | 26/26 |
| **0.2–0.8** | **Predeclared** | **46/48** | **0** | **30/30** |
| 0.3–0.7 | Descriptive sensitivity only | 47/48 | 1 | 30/30 |

The predeclared interval sends both analogy-domain cases to review, retaining
95.8333% of the corpus. Its zero observed retained errors are encouraging, not
probability calibration or a production guarantee. The alternative intervals
are descriptive calculations, not newly selected operating thresholds. In the
0.3–0.7 arm the error is a retained false negative; positive precision alone
would conceal it.

## Reproduction and next evidence

`loom/tools/structure/jev_pair_analysis.py` calls the existing
`jev_live_pilot.score`, verifies frozen fixture bytes and response hashes, and
checks parsed model/provider/cost values against the ledger. It groups by
`contrast_kind`, family and language and treats missing answers as missing.
Machine-readable results, exact source text for the error, scores and audit
hashes are in `inputs/jev-pairs-analysis-2026-09-28.json`.

```bash
python -B -m unittest loom.tools.structure.test_jev_pair_analysis -v
python -B loom/tools/structure/jev_pair_analysis.py \
  --run-dir /path/to/saved/jev-structure-pairs-v1 \
  --fixture-dir loom/tests/fixtures/eval/jev_structure_pairs_v1 \
  --output /path/to/new-analysis.json
```

All six evaluator tests pass, using only fabricated responses. They exercise
input/response tampering, incomplete runs, missing-versus-negative scoring,
contrast grouping and threshold boundaries. The output path must be new.

Next evidence should compare classifiers on this same task, then use fresh
natural passages and conversations with frozen unseen families. Test swapped
pair order, ordinary-language role bindings, several abstraction levels,
negation/scope perturbations, ambiguous cases and retrieval coverage. The
current base-left order is fixed. Pair judgment still presupposes that the
candidate passages were found; it does not discover omitted graph candidates.

Retain the policy in `JEV_USAGE_RULES_2026-09-28.md`: judgments become
provenance-bearing assessments or reversible comparison proposals. Matching
patterns do not establish entity identity, authorize Claim merges or transfer
truth between domains. The earlier 768 individual-property decisions and the
native full-graph extraction pilot are different tasks; their percentages
cannot establish a head-to-head improvement from this result.
