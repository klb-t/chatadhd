# Independent replay-integrity and DEV vector audit

2026-09-30. No validation access, credentials or API calls. Read-only review
of `graph_panel_score_run.py` and its eight passing mechanism tests; separate
independent recount of the already retained local retrieval scores/vectors.
No frozen implementation or first result was modified. Actual paid-run raw
responses were not read in this review.

## Replay driver mechanism risk

The driver checks manifest/ledger identity, prefix/request bytes, response
hashes, provider/model identity, billing fields and complete response shape.
Missing or invalid answers keep their planned denominators. However, a
synthetic test fixture demonstrated this accounting distinction:

- Preserved raw response cost: USD 0.002.
- Deliberately changed ledger cost: USD 0.000001.
- `load_run` returns unavailable output, rather than raising; its cost summary
  still reports USD 0.000001 and zero missing-cost attempts.

The broad response-compile exception catches the cost inconsistency together
with ordinary semantic/schema rejection. This is **not** an observed real bill
or paid-run failure. Before certifying raw/ledger-reconciled accounting, an
integrity mismatch must fail that certification or mark cost integrity invalid
with its own diagnostic. A rejected semantic answer may still have a valid
charge, so billing and semantic failure should remain separate.

The correction-event alternative score is explicitly diagnostic and keeps
primary gold unchanged. Its `gold_convention_ambiguous_cases` counter currently
counts cases where a prediction selects the alternative, not all cases whose
gold convention is ambiguous. Interpret that count as applied alternatives.

## Independent recount of retained local DEV results

Six first-score freeze hashes match. All 96 prepared query prefixes exactly
rebuild from the DEV input, including physical time filtering. All 252 candidate
turns bind to the original source, timestamp and text hash. The retained matrix
has 146 × 384 float32 vectors; maximum norm error is 5.8862095864142816e-8.
Every stored float64-dot cosine reproduces exactly from that matrix (maximum
absolute difference zero). No model re-encoding was performed.

Independently reconstructed relevance targets contain 66 eligible evidence
spans across 60 queries, including six applicable correction-event references.
No later source turn is a candidate for an earlier cutoff. Relevant denials
and withdrawal evidence are included; unknown is never a denial inferred from
low similarity.

| Frozen channel | hit@1 / 60 evidence-bearing queries | Turn TP / FP / FN at 0.5 | Naive ternary correct / 96 |
|---|---:|---:|---:|
| Token cosine | 45/60 | 53 / 76 / 13 | 41/96 |
| Character cosine | 44/60 | 29 / 22 / 37 | 50/96 |
| Cached learned MiniLM cosine | 46/60 | 52 / 110 / 14 | 36/96 |
| Nongating maximum union | 45/60 | 58 / 128 / 8 | 36/96 |

Always-unknown gives 36/96 accuracy. Every naive channel has refuted recall
0/24, because its declared output is supported or unknown, never refuted.
Thus learned similarity's one-query hit@1 gain over token cosine does not
establish better graph judgments. Union obtains five extra relevant turns
versus token cosine at this predeclared threshold while adding 52 false
relevance selections. Keep precision and recall separate; these are conditional
full-turn retrieval and explicitly naive diagnostic scores, not world truth,
clause adequacy, free extraction or independent validation results.

An unknown query's absence of annotated edge support is the evaluation's
relevance convention. Context explaining nonendorsement or missing information
can still be useful; `unknown_false_relevance` must not be read as universal
context uselessness. No threshold was selected or tuned in this audit.
