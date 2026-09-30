# First exploratory DEV retrieval results

**Representation and context allocation matter; neither cosine nor BM25 establishes a source assertion.**
29 frozen ranking methods were measured on the same 24 previously inspected DEV conversations, 96 supplied-edge queries and 252 physically eligible full-turn candidates. The four DEV families are correlated synthetic variations. Validation and old holdouts were not accessed. Zero paid/API calls; no training or threshold fitting. All source judgment fields remain unavailable/null.

Gold-bound evidence targets remain 60 queries /66 spans (36 supported,24 refuted,36 unknown queries overall). P/R concern explicit annotated edge evidence, not world truth, clause adequacy, free graph discovery or whether unknown-context turns would help a real judge. Unknown queries remain in selected-context precision denominators.

## First frozen comparison

| Method | Hit@1 /60 | Span recall@1 /66 | Selected-turn precision@1 | Hit@2 /60 | Mean AP | Within-query AUC |
|---|---:|---:|---:|---:|---:|---:|
| `bm25_aliases` | 47/60 | 47/66 | 47/96 | 60/60 | 0.8917 | 0.8646 |
| `bm25_denial` | 45/60 | 45/66 | 45/96 | 60/60 | 0.8583 | 0.7812 |
| `bm25_endpoints` | 42/60 | 42/66 | 42/96 | 60/60 | 0.8333 | 0.7500 |
| `bm25_forward` | 45/60 | 45/66 | 45/96 | 60/60 | 0.8639 | 0.8021 |
| `bm25_question` | 45/60 | 45/66 | 45/96 | 60/60 | 0.8583 | 0.7812 |
| `bm25_reverse` | 45/60 | 45/66 | 45/96 | 60/60 | 0.8583 | 0.7812 |
| `bm25_speaker_soft_0.5` | 48/60 | 48/66 | 48/96 | 60/60 | 0.8833 | 0.8125 |
| `bm25_speaker_soft_1.0` | 48/60 | 48/66 | 48/96 | 60/60 | 0.8833 | 0.8125 |
| `bm25_structured` | 48/60 | 48/66 | 48/96 | 60/60 | 0.8833 | 0.8125 |
| `bm25_structured_b0.0` | 52/60 | 52/66 | 52/96 | 60/60 | 0.9167 | 0.8646 |
| `bm25_structured_b1.0` | 48/60 | 48/66 | 48/96 | 60/60 | 0.8833 | 0.8125 |
| `bm25_typed` | 48/60 | 48/66 | 48/96 | 60/60 | 0.8833 | 0.8125 |
| `character_3_5_cosine` | 44/60 | 44/66 | 44/96 | 60/60 | 0.8500 | 0.7708 |
| `endpoint_coverage` | 42/60 | 42/66 | 42/96 | 60/60 | 0.8444 | 0.8073 |
| `endpoint_order` | 48/60 | 48/66 | 48/96 | 60/60 | 0.8944 | 0.8698 |
| `learned_minilm_cosine` | 46/60 | 46/66 | 46/96 | 59/60 | 0.8694 | 0.8021 |
| `lexical_token_cosine` | 45/60 | 45/66 | 45/96 | 60/60 | 0.8583 | 0.7812 |
| `minilm_denial` | 51/60 | 51/66 | 51/96 | 60/60 | 0.9167 | 0.8438 |
| `minilm_forward` | 47/60 | 47/66 | 47/96 | 59/60 | 0.8806 | 0.8229 |
| `minilm_multi_max` | 51/60 | 51/66 | 51/96 | 60/60 | 0.9083 | 0.8438 |
| `minilm_question` | 50/60 | 50/66 | 50/96 | 60/60 | 0.9111 | 0.8438 |
| `minilm_reverse` | 50/60 | 50/66 | 50/96 | 60/60 | 0.9111 | 0.8438 |
| `minilm_typed` | 49/60 | 49/66 | 49/96 | 60/60 | 0.8972 | 0.8438 |
| `nongating_union` | 45/60 | 45/66 | 45/96 | 60/60 | 0.8639 | 0.8021 |
| `rrf_bm25_minilm_60` | 46/60 | 46/66 | 46/96 | 60/60 | 0.8722 | 0.8177 |
| `rrf_bm25_multiquery_60` | 45/60 | 45/66 | 45/96 | 60/60 | 0.8583 | 0.7812 |
| `rrf_multiquery_60` | 50/60 | 50/66 | 50/96 | 60/60 | 0.9111 | 0.8438 |
| `rrf_original_10` | 46/60 | 46/66 | 46/96 | 60/60 | 0.8667 | 0.7917 |
| `rrf_original_60` | 46/60 | 46/66 | 46/96 | 60/60 | 0.8667 | 0.7917 |

Within-query AUC retains 96 positive-negative candidate pairs over 48 eligible queries. Scores from different query-specific BM25 fits are not claimed comparable or calibrated. All methods exhaust evidence at top-3 because the pool contains at most three turns; that ceiling is mechanical.

BM25 without length normalization reaches52/60 hit@1 versus token45/60 and original MiniLM46/60. It gains13/losses6 against token, gains8/losses2 against MiniLM. The loss family matters: negation_unknown stays6/12, while attribution rises12/12 and correction16/18. This does not establish an independent method winner.

Denial-worded MiniLM reaches51/60 hit@1, but loses two paraphrase cases (16/18 versus original18/18). Its net gain5 against the original encoder is9 new hits/4 lost. Multiquery max also51/60 yet correction drops15/18 versus denial17/18. Keep these competing recipes; selecting only the aggregate winner hides regressions.

## Membership union and matched context

| Union channels | Hit queries /60 | Covered spans /66 | Selected turns | Matched-cardinality finding |
|---|---:|---:|---:|---|
| Token/char/original MiniLM |55/60|55/66|138|Token and BM25 also55/66 at the identical per-query cardinality|
| BM25/original MiniLM |55/60|55/66|129|BM25 also55/66 at the identical per-query cardinality|
| Token/BM25/original MiniLM |55/60|55/66|139|Token and BM25 also55/66|
| Forward/denial/reverse MiniLM |59/60|59/66|133|Denial,question,reverse and multiquery-max also59/66|

Each union preserves its channels’ top-1 memberships without another score veto. The apparent gain over a single top-1 channel is not free: more context and different per-query budget allocation explain much of it. Matched-cardinality controls do not show an intrinsic fusion advantage here. All exact query selections, gains and losses are retained.

## Secondary byte-budget intervention

Only allocation changed; frozen scores/rankings were reused. Every method receives the exact source-UTF8-byte budget used by each union for that query. Rank-prefix stops at the first oversized turn; rank-skip explicitly skips it and continues. Neither truncates a turn. Bytes are not LLM tokens or dollars.

| Budget derived from | Sum source bytes | Union spans /66 | Original MiniLM prefix /skip spans | Denial MiniLM prefix /skip spans | Gold-conditioned attainable ceiling |
|---|---:|---:|---:|---:|---:|
| bm25_minilm | 8663 | 55/66 | 54/55 | 51/55 | 55/66 |
| lexical_bm25_minilm | 9272 | 55/66 | 54/55 | 51/55 | 55/66 |
| multiquery_minilm | 9233 | 59/66 | 51/53 | 58/59 | 59/66 |
| original3 | 9198 | 55/66 | 54/55 | 51/55 | 55/66 |

Rank-prefix can produce empty selections (up to11/96 for displayed denial control); those cases remain in the denominator. Rank-skip has a different cost/relevance tradeoff and is a separate explicit heuristic. The gold-conditioned subset oracle is an attainable recall ceiling only, never a model, deployable retrieval method or precision claim. Comparing different union budget families also changes query-wise allocation, so similar global byte totals alone do not isolate ranking quality.

## Direction and conditional instrument profile

Forward-question and reverse-question MiniLM have the same complete ranking93/96 times, despite maximum candidate-score delta0.04399 (mean0.008987 over252 pairs). Two top-1 changes occur among unknown negation-family queries; the third changes only lower ranks. A reverse question can retrieve the same text as a forward question. These embeddings are weak evidence-search instruments for typed orientation, not conditional truth classifiers.

The exact pinned cached multilingual MiniLM encoded422 distinct texts with0 truncations,384dimensions,max128tokens,CPU2 threads in4.1373s excluding initialization. Model revision `e8f8c211226b894fcb81acc59f3b34ba3efd5f42` and model/vector/tokenization/runtime/code hashes are retained. No new download or paid call occurred in these two series.

V1’s local language templates were fixed in code. A separate data-backed renderer v2 now preserves all768/768 query-variant UTF8 representations exactly, without changing the executed first instrument or remeasuring quality. Future variants can be template data. The original first freeze/failure are retained: the initial renderer rejected the supplied causes relation before encoding; freeze2 explicitly registered both implies and causes before any successful outcome.

## Decisions

- **Keep** prefix-local BM25 and learned wording variants as complementary retrieval proposals with separate task/recipe profiles and preserved source identities.
- **Investigate** the b=0 length-control and denial wording on independent/source-archive data; current DEV gains include explicit family regressions and do not justify promotion.
- **Keep alternatives** for rank fusion, membership union and prefix/skip allocation. No matched-budget fusion winner is established.
- **Reject** similarity/relevance promotion to directed source truth. No new semantic judgment classifier exists in this arm.
- **Next** is an independently frozen local cross-encoder task contrast, English-trained with PL transfer disclosed; paid calls remain forbidden on these synthetic fixtures.

## Reproducibility

40 synthetic mechanism/renderer tests pass (23 main,12 byte-budget,5 data renderer). The first source-prefix candidate checks report0 future leaks and0 out-of-prefix annotations; all6 status-event evidence references remain. These are tests of mechanisms, not model quality. Independent review is pending; no fresh native regression claim.

`first_evidence.zip` retains6 exact first measurement payloads,5832023 uncompressed bytes,archive SHA256 `03686a8a6eb2fa79e0f6791ff6f06655e83cf9f65274c6fcd7d9836a747a4f82` (808022 bytes). All6 payload hashes verified. Large measurement JSON and vectors are ignored as duplicated working files, with original bytes in the archive. No weights, dependencies, credentials or validation data are archived.

Restore exact first data with `python3 -m loom.tools.structure.retrieval_exploration_v1.first_archive restore`; verify with the same module’s `verify` command. Existing differing files are refused. Source/config/protocol/freezes and test logs accompany the archive. Independent replay can recompute rankings from the frozen model outputs without downloading weights or contacting a provider.
