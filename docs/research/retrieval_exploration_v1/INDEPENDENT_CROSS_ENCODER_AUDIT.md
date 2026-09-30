# Independent cross-encoder audit

**The first cross-encoder artifacts and reported comparisons pass an independent
recount. BM25 b0 remains ahead on this inspected DEV panel: 52/60 evidence hits at
rank one, versus 51/60 for the structured cross-encoder.** This review does not
select a general model winner, independently adjudicate the annotations, or
validate the methods on unseen conversations.

Reviewer `frontier_matrix` did not implement the retrieval methods. The executable
audit imports none of the project preparation or evaluation functions. It reads
the three exact first payloads directly from their verified ZIP, reconstructs the
source prefixes and annotation targets from authorized DEV files, and retains its
own outcomes separately. No original implementation or first result was edited.

## What was checked

| Check | Independently verified result |
|---|---:|
| Planned queries / source cases | 96 / 24 |
| EN / PL planned queries | 48 / 48 |
| EN / PL queries with explicit evidence | 30 / 30 |
| Evidence queries / annotated spans | 60 / 66 |
| Supported / refuted / unknown targets | 36 / 24 / 36 |
| Eligible query-turn candidates | 252 |
| Future query-turn exclusions | 36 |
| Out-of-prefix annotated spans | 0 |
| Explicit supersession event references | 6 |
| Query-turn-view-cap bindings | 1260 |
| Unique joint input pairs | 1062 |
| Cache reuses beyond the first pair use | 198 |
| Complete rankings across all 34 methods | 3264 / 3264 |
| Language/family/label metric summaries | 306 / 306 |
| Frozen byte-family/policy/method allocations | 272 / 272 |

Every candidate matches its full raw turn, speaker, source ID, timestamp,
character/UTF-8 span and stable source order. The query prefix is reconstructed
from `known_at <= as_of`. The four primary views and structured cap128 control
match the exact recorded query and candidate strings. None contains gold labels,
assertion IDs, evidence coordinates or status-event annotations as a feature.
Supplied proposition nodes and actor/relation/query choices remain assisted task
inputs; this is not free graph extraction from a conversation.

The original inference implementation hashes prior DEV outcome files before
inference to check its dependency freeze. Thus it is incorrect to claim that the
process never opened a file containing prior outcomes. The narrower claim is
verified: the actual joint input strings are exactly source/query-derived, with
no gold features. Earlier DEV results were already inspected, as the original
protocol states. No validation or old holdout was opened by this audit.

The cache contains 876 pairs used once, 174 used twice and 12 used three times:
876 + 348 + 36 = 1260 bindings. Identical strings/caps can be reused across
different source prefixes because encoding is stateless. Their separate source
bindings still satisfy each query's cutoff. Cache reuse introduces no future turn
into an earlier candidate pool.

## Raw instrument replay

All six cached public artifacts match their saved SHA256 identities. Five small
artifacts independently match the official snapshot's Git blob SHA1; the ONNX
weights match its LFS content SHA256. Total verified bytes are 91,728,555. The
exact public metadata snapshot is copied into this audit's files; weights remain
outside Git. The revision is `233902d25c440f23af6f7d6e94d2946bac0bee0a`.

An independent CPU ONNX run recomputed all 1062 unique logits from the pinned
tokenizer and model. All 3186 little-endian int64 input tensor hashes match.
Every logit is **exactly equal**, with maximum absolute difference 0.0, against a
predeclared tolerance of 1e-6. Re-encoding/inference took 13.0111 seconds on this
shared host. This is a reproducibility check, not a second independent quality
sample or a production throughput estimate.

No unique pair truncates. The longest untruncated pair has 106 tokens, including
special tokens; both 128 and 256 caps exceed every input. Their 252 candidate
logits and 96 complete rankings are identical. The cap control is redundant on
this panel. Raw logits span -11.4431 to 9.6672; identity activation is retained.
They are relevance scores, not probabilities or source assertion confidence.

## Recounted quality and counterexamples

| Instrument / recipe | Hit@1 /60 | Span recall@1 /66 | Turn precision@1 | EN hit@1 /30 | PL hit@1 /30 |
|---|---:|---:|---:|---:|---:|
| Lexical token cosine | 45/60 | 45/66 | 45/96 | 21/30 | 24/30 |
| Original MiniLM cosine | 46/60 | 46/66 | 46/96 | 27/30 | 19/30 |
| BM25 structured b0 | 52/60 | 52/66 | 52/96 | 27/30 | 25/30 |
| MiniLM denial | 51/60 | 51/66 | 51/96 | 26/30 | 25/30 |
| Cross-encoder structured | 51/60 | 51/66 | 51/96 | 24/30 | 27/30 |
| Cross-encoder denial | 49/60 | 49/66 | 49/96 | 26/30 | 23/30 |
| Cross-encoder question | 48/60 | 48/66 | 48/96 | 26/30 | 22/30 |
| Cross-encoder reverse | 49/60 | 49/66 | 49/96 | 26/30 | 23/30 |

All 34 overall summaries and all stratified summaries match, including hit@1/2/3,
span recall, turn precision, reciprocal rank, AP and within-query pair AUC.
Inherited method rankings are independently reconstructed from their saved
scores; the earlier cosine/BM25/RRF score algorithms themselves were not
reimplemented by this review. All four inherited exact source-byte budget
families and both allocation policies match without selecting new budgets.

The structured cross-encoder gains nine and loses four against original MiniLM.
Against BM25 b0 it gains two Polish correction queries (`016_q2`, `017_q2`) and
loses three English correction queries (`013_q2`, `014_q2`, `015_q2`). In all three
English losses, the older positive turn outranks its later explicit correction.
For `013_q2`, the structured logits are 4.59894 for the old statement and 3.84626
for the correction; BM25 b0 gives 3.77606 and 3.96070 respectively. These are
actual ranking disagreements, with both eligible source turns preserved.

Denial wording reverses those three mistakes and achieves 18/18 correction
evidence hits, while paraphrase falls to 14/18 from structured 18/18 and attribution
falls to 11/12 from 12/12. Its overall 49/60 is lower. A recipe that fixes one
family introduces losses in others; no unconditional recipe improvement follows.
The EN/PL interaction also changes with recipe. On these correlated templates,
English training does not establish a uniform advantage on English cases.

For `007_q1`, the requested edge is sensor-no-error -> record-not-discarded.
Both BM25 and cross-encoder instead rank the explicit denial of the reverse edge
first. Structured logits are 4.40904 for forward support and 5.75150 for reverse
denial. For `007_q2`, that reverse-denial turn is precisely the required evidence.
The reversal changes the epistemic role of the same passage. It diagnoses a
direction/grounding limitation; it does not show that the selected passage is
globally irrelevant. Question/reverse top-one equality 94/96 and full ranking
equality 85/96 are reproduced, without treating stability as correctness.

Precision uses all 96 selected turns, including 36 unknown queries that still
receive context. Missing explicit-edge annotation does not establish that the
context is useless, nor that a world claim is false. The tiny candidate pools and
repeated family templates prevent population-level accuracy or language claims.

## Preserved outcomes and decision

The first audit harness failed because it expected only three ZIP members and
omitted the legitimate `INVENTORY.json` metadata member. This was a reviewer
harness error, not a retrieval result failure. The original failure outcome,
freeze and exact script copy remain as `INDEPENDENT_CROSS_ENCODER_AUDIT.json`,
`INDEPENDENT_CROSS_ENCODER_AUDIT_FREEZE.json`, and
`independent_cross_encoder_audit_v1.py`. The corrected second audit is
`INDEPENDENT_CROSS_ENCODER_AUDIT2.json`; it does not replace the first record.

Keep structured and denial instruments as competing DEV proposals. The evidence
supports keeping BM25 as a live comparator and preserving cross-channel gains
without a shared top-one veto. Further recipe tuning on these inspected pools
has limited information value. The next useful measurement is a frozen comparison
on authorized real source episodes with source/episode denominators and an
independently adjudicated evidence contract. Source judgment remains a separate
typed or measured semantic mechanism. No graph fact or model trust score follows
from this audit. Actual external API requests and cost: **0 / USD 0**.
