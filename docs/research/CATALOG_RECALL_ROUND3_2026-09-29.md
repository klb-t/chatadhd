# Catalog recall, round 3 (2026-09-29)

Corpus: `synthetic_dev` (45 relevant, 5 traps, 15 generic noise). Development set only;
the blind corpus was not read. Thresholds (tau 0.7 / 0.35, gates 0.55 / 0.75 / 0.05) unchanged.

## Before / after

| Metric | Round 2 | Round 3 |
|---|---|---|
| Recall (selection) | 21/45 = 0.467 | **31/45 = 0.689** |
| Precision | 21/21 | 31/31 = 1.0 |
| Traps selected | 0/5 | 0/5 |
| Generic noise selected | 0/15 | 0/15 |
| Ranking AUC, final / lexical / semantic | 0.944 / 0.777 / 0.963 | 0.966 / 0.777 / 0.981 |
| hits@45 final | 41/45 | see eval output (AUC up; ranking was never the bottleneck) |
| link mode | exhaustive | candidates |

Diagnosis by stage: evidence and ranking were already good (semantic AUC 0.96, 41 of the top 45 truly
relevant); the loss was in selection, because units backed by several moderate independent signals
stayed under tau 0.7 (candidate band 0.35 to 0.69, and the highest noise unit is 0.36).

## Changes (src/catalog/score.cpp, data/policy)

1. Semantic profile seeds now come from every non-irrelevant band, weighted by score (was: relevant band only).
2. Link propagation runs 2 rounds (`link_rounds`), then one feedback cycle (`feedback_cycles`):
   semantic re-seed from link-boosted scores, then propagate again.
3. New `consensus` feature (structure channel, weight 1.0 in relevance.json): 1 when lexical (bm25),
   semantic (sem_word/sem_ngram) and link evidence each pass a floor (0.15 / 0.3 / 0.15). No channel
   vetoes another; a unit missing a channel gets nothing. Steps: 21 -> 22 (soft seeds) -> 29 (consensus
   0.8) -> 31 (weight 1.0).
4. Linking uses candidate generation by default (`link_exact_max_units` 2000 -> 0). Proof: the per-unit
   score/feature table of the eval is byte-identical between exhaustive and candidate mode, 56 links in
   both; the eval now asserts mode == candidates and links == 56. `test_catalog_links` (exhaustive vs
   candidates equivalence, 50k-unit scale) already covered the algorithm.
5. The lexical shadow diagnostic (C_lexical - C_semantic, rescue/noise/undecided) already existed in the
   eval output; unchanged.

## Honest caveats

- The consensus weight and floors were set while looking at the dev set (weight 0.8 -> 1.0 bought 2
  units). This is fusion-weight tuning on the dev corpus, not validated on held-out data. The blind
  corpus is the check. No fixture names, aliases or substring matching were used; no threshold lowered.
- Round-2 propagation echoes (A boosts B boosts A) are damped (0.6) but not removed.
- 14 relevant units remain unselected (scores 0.25 to 0.68), mostly with weak links (link < 0.15) and
  weak semantic contrast; 0.9 recall is not reached. Next: better link strength for shared_rare,
  embeddings/LLM channel (`ScoreConfig.llm` still unused).
