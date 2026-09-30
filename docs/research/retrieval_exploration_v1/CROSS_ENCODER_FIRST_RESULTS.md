# Local cross-encoder first DEV results

**Joint attention adds useful evidence proposals, but the simpler BM25 control still leads this inspected DEV panel.** No source judgment or graph fact is inferred from relevance logits. The same96 supplied-edge queries /252 eligible source-turn candidates were tested, with60 annotated-evidence queries and66 spans. No validation, old holdout, paid API or training was used.

## Frozen results and competing recipes

| Recipe | Hit@1 /60 | Span recall@1 /66 | Turn precision@1 | Hit@2 /60 | EN hit@1 /30 | PL hit@1 /30 | Mean AP |
|---|---:|---:|---:|---:|---:|---:|---:|
| `lexical_token_cosine` |45/60|45/66|45/96|60/60|21/30|24/30|0.8583|
| `learned_minilm_cosine` |46/60|46/66|46/96|59/60|27/30|19/30|0.8694|
| `bm25_structured_b0.0` |52/60|52/66|52/96|60/60|27/30|25/30|0.9167|
| `minilm_denial` |51/60|51/66|51/96|60/60|26/30|25/30|0.9167|
| `crossencoder_denial_256` |49/60|49/66|49/96|60/60|26/30|23/30|0.9083|
| `crossencoder_question_256` |48/60|48/66|48/96|60/60|26/30|22/30|0.8958|
| `crossencoder_reverse_256` |49/60|49/66|49/96|60/60|26/30|23/30|0.9000|
| `crossencoder_structured_128` |51/60|51/66|51/96|60/60|24/30|27/30|0.9083|
| `crossencoder_structured_256` |51/60|51/66|51/96|60/60|24/30|27/30|0.9083|

All34 competing method tables remain in the archive, including family/label stratification, within-query AUC, exact top-1 gains/losses and all4 inherited per-query byte-budget families ×2 allocation policies. Unknown-context selections are included in candidate precision; their lack of explicit-edge annotation is not a claim that their context is useless to a semantic judge.

Structured cross-encoder51/60 gains9/losses4 against original MiniLM46/60, yet gains2/losses3 against BM25 b0=52/60. The three latter losses are English correction queries013/014/015_q2: it chooses the older positive statement instead of the current explicit correction. Denial wording fixes that family to18/18, but paraphrase falls14/18 compared with structured18/18. No aggregate recipe is an unconditional improvement.

English-trained does not imply uniformly better EN performance. Structured hasEN24/30 andPL27/30; denial hasEN26/30 andPL23/30. This is a task/recipe/language conditional profile on correlated synthetic cases, not a multilingual capability estimate or universal model reliability.

## Direction, context cap and failure diagnosis

Question/reverse variants share top-1 rankings94/96 and complete rankings85/96. Separate MiniLM had complete equality93/96 and top-1 equality94/96. Joint attention changes more lower ranks but leaves the same number of first-rank changes. Same source text can be relevant to either question; ranking similarity alone does not prove direction failure or source truth.

In the negation/direction family every new view stays6/12 evidence hit@1. Example007_q1 requests sensor-no-error→record-not-discarded; a high relevance turn instead explicitly denies the reverse record-not-discarded→sensor-no-error. It mentions both endpoints and negation while failing to ground the requested directed edge. For007_q2 that denial is the correct explicit evidence. This diagnoses a direction-specific evidence-grounding tradeoff; neither high logit nor stable ranking establishes an assertion.

Structured cap128 and256 outputs are exactly equal252/252 candidate-logit pairs, with96/96 equal complete rankings and maximum delta0.0. No pair truncated at either cap. The control is therefore redundant on these short inputs, not another model-quality achievement. Future longer source views need a separate measured truncation profile.

## Instrument, runtime and first outcome preservation

Pinned official `cross-encoder/ms-marco-MiniLM-L6-v2` revision `233902d25c440f23af6f7d6e94d2946bac0bee0a`; official model card labels English and MS-MARCO passage ranking. Source: https://huggingface.co/cross-encoder/ms-marco-MiniLM-L6-v2 . Six public JSON/README/ONNX artifacts total91728555bytes, with Git blob/LFS hashes independently matched during download. ONNX SHA256 `5d3e70fd0c9ff14b9b5169a51e957b7a9c74897afd0a35ce4bd318150c1d4d4a`. Weights are cached outside Git; no model remote code was executed.

1260 query/candidate/variant bindings required1062 unique joint pair inferences;0 truncated. CPUExecutionProvider,2 intra-op threads,1 inter-op,sequential ONNX. Initialization0.1648s; session inference12.6817s; pair encode/cache/inference13.1898s; process CPU during pair phase26.3698s; total integrity/import/init/runtime13.5266s. Max RSS199608KiB. These observed timings come from this concurrently used host and are not production throughput promises.

Raw single relevance logits use the model’s identity activation, without sigmoid, clipping, probability calibration or source-confidence promotion. Every unique pair preserves exact input strings/hashes, tensor hashes, token counts, raw logit and inference timing; each planned source binding retains source/turn/prefix/known_at identity. Runtime versions are observed in first outputs; model/recipe/caps/code are frozen before inference.

First archive contains3 payloads,6115987 raw bytes; archive559535bytes SHA256 `685784b964606a672e988d31b2a3e7614ab7ec7d2032c4ffc9b7384472d12c5a`. All3 hashes verified. It excludes credentials, weights, validation and old holdouts. Restore with `python3 -m loom.tools.structure.retrieval_exploration_v1.cross_encoder_v1.first_archive restore`. Eleven new mechanism tests passed before freeze; tests concern the execution/identity contract, not model quality. Independent review and native regression are not claimed.

## Decisions

- **Keep** cross-encoder structured and denial recipes as competing DEV evidence instruments with all per-family losses retained.
- **Do not promote** cross-encoder over BM25:52/60 versus51/60 and correction losses disprove a simple hierarchy on this panel.
- **Investigate** retrieving both forward-support and reverse-denial evidence without a shared top-1 veto; source/actor/direction semantics must be assessed by a separate typed mechanism or properly evaluated semantic instrument.
- **Keep cap256** as the declared primary, with128 control redundancy recorded; do not tune a cap based on an unseen holdout.
- **Next:** independently review these artifacts and move frozen finalists onto authorized real source windows with episode/source-cluster denominators. More tuning on these small inspected pools has limited information value.
