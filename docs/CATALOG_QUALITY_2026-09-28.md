# Catalog quality checkpoint — 2026-09-28

The catalog recall gate is **still failing**. The complete native test run
finished with **59/60 entries passing**; `unit.test_catalog_eval` is the sole
failure. Correctness regressions for alias boundaries, BM25 normalization,
link evidence, and attribution pass. The recall threshold remains unchanged
at 0.55. This checkpoint does not establish production retrieval quality.

## Final development-set measurement

`loom/tests/fixtures/eval/synthetic_dev` contains 65 labeled conversations
and three auxiliary documents. The final C++ selector measurement is:

| Measure | Result | Existing gate |
| --- | ---: | ---: |
| Relevant conversations selected | 13/45 = 0.288889 recall | ≥ 0.55 — **FAIL** |
| Labeled selected conversations relevant | 13/13 = 1.000000 precision | ≥ 0.75 — pass |
| Noise traps selected | 0/5 | ≤ 0.05 fraction — pass |
| Generic noise selected | 0/15 | ≤ one third — pass |
| Auxiliary documents selected | 3/3 | Reported separately; no relevance labels |

The final scorer reports 10 relevant, 6 candidate and 52 irrelevant units,
56 links and two vocabulary expansion terms. Those score labels are not
selection decisions: selection rules can also admit candidates.

Precision now uses the same labeled conversation population as recall.
Previously, the numerator counted relevant conversations while the
denominator could include project and memory documents without ground-truth
relevance labels. The three selected auxiliary documents remain visible in
the report; they are not silently discarded. Dividing 13 by all 16 selected
units gives 0.8125, but that is not labeled-conversation precision and does
not determine whether those auxiliary documents are relevant.

## Why the former recall was misleading

Before the alias boundary correction, the same development test selected
30/45 relevant conversations and three generic-noise conversations:
recall 0.666667 and labeled-conversation precision 30/33 = 0.909091.
Identity matching accepted arbitrary substrings. Short aliases such as
`EP` and `LEM` matched unrelated ordinary words, including `przepis`,
`lepiej` and `problem`. These false identities could trigger both scoring
and selection rules.

Of the 17 relevant conversations removed by strict boundaries, 12 had relied
on incidental `EP`/`LEM` matches. The other five exposed a real limitation:
inflected forms of legitimate aliases are not recognized by exact folded
identity matching. The former recall therefore mixed successful retrieval
with accidental matches. Reintroducing substrings would conceal the gap.

Identity aliases now require Unicode word boundaries. Explicit profile
prefix metadata remains available; existing principle stems retain their
prefix behavior. Tests cover short aliases, Unicode adjacency, underscores,
literal symbol aliases such as `C++`, and overlapping matches.

## Mechanism corrections included

- BM25 query terms now follow the same tokenization and `match_key`
  normalization as document sketches. Single-word inflections and
  punctuation-separated identifiers previously bypassed that normalization.
  Short content identifiers remain supported. Independent fixtures cover
  `orchids`/`orchid`, `copper_valve`, and `VR` without identity hits.
- Link propagation now accepts existing philosophy BM25 evidence as well as
  self-profile BM25 or identity evidence. A reordered principle phrase gains
  link support; a neighboring unit with neither evidence channel does not.

These corrections pass their regressions but do not change the final 13/45
selection result. Scorer weights, selection thresholds and quality gates
were not relaxed. The owner holdout answer-key branch was not read or used.

## Next work

1. Add whole-token morphological alias equivalence, with explicit language
   handling and independent multilingual collision tests. Preserve context
   gates and exact treatment of short identifiers; do not use arbitrary
   substring matching to recover inflections.
2. Diagnose each remaining miss by retrieval stage: profile/sketch evidence,
   expansion, structural or content links, then final selection. Extend
   corroborated continuity retrieval for unnamed conversations. A nearby
   timestamp alone must not make a conversation relevant.
3. Measure candidate ranking and selection separately before calibrating
   policy weights on development data. Validate proposed improvements on
   independent data and the prescribed temporal holdout without tuning to
   its answer key. The unchanged recall gate remains the minimum checkpoint.

Reproduce from `loom/` after building the dev preset:

```sh
ctest --preset dev --output-on-failure
LOOM_CATALOG_EVAL_VERBOSE=1 build/dev/loom_tests --test-suite=catalog_eval
```

The second command intentionally exits nonzero while recall remains below
the existing gate; its diagnostics identify missed conversations and expose
their score features and selection reasons.
