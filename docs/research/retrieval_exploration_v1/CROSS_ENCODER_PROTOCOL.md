# Local joint-attention reranker contrast

Frozen after both initial DEV retrieval series, before cross-encoder first scores.
The existing DEV labels have been inspected; no independent holdout claim. Public
download authorized by root, zero external API dollars. No validation/old holdout,
training, threshold fit or semantic graph promotion.

Hypothesis: a pretrained joint query/passage attention reranker may distinguish
evidence better than separate sentence-vector cosine, especially source role,
negation and direction. The competing explanation is a task/domain mismatch:
the official `cross-encoder/ms-marco-MiniLM-L6-v2` model is English passage-ranking
trained, while half this panel is Polish and all cases concern source-attributed
logical/causal relations, not standard MS-MARCO queries. Report EN and PL separately
as conditional instrument profiles; no universal reliability assumption.

Pin official model revision `233902d25c440f23af6f7d6e94d2946bac0bee0a`, the public
model metadata snapshot, ONNX weights, tokenizer/config and all artifact hashes
before inference. Weights stay outside Git. Only public JSON and ONNX data are
loaded; no model-supplied remote code. The public source is
https://huggingface.co/cross-encoder/ms-marco-MiniLM-L6-v2 .

Use the exact data-backed query views already proven byte-equivalent to v1:
structured, forward question, explicit denial question, reversed question. Pair
each with the unchanged full-turn speaker/text representation. Primary cap256
tokens, a structured-view-only cap128 control, pair special tokens included,
right longest-first truncation, no padding, raw single ONNX relevance logit,
identity activation. This logit is not a calibrated probability or assertion
confidence. CPU-only ONNX,2 intra-op threads,1 inter-op,sequential sessions.

All96 planned queries /252 eligible query-turn candidates remain. Prefix/source
identity, known_at filtering and full raw source bytes are unchanged. Cache only
identical (query text, candidate text, cap) triples; stateless joint encoding cannot
allow a future turn into another query's eligibility pool. Preserve every pair's
untruncated/encoded token counts, truncation indicator, input tensor hash, raw
logit, representation hashes, source bindings, latency and model/runtime identity.
Save first outputs/scores and their freeze BEFORE gold join.

Measure hit@1/@2, evidence recall/precision and MRR/AP/within-query AUC with the
same66 annotated spans/60 evidence queries. Compare all29 frozen earlier methods,
not only their winners. Compare each new method under all4 frozen byte-budget
families and both allocation policies; no budget or ranking fit follows results.
Report unknown-context selections separately; they are not evidence of false
world facts. Keep raw-direction sensitivity diagnostics for question vs reverse.
The128 control isolates truncation/configuration only; if no input truncates and
all outputs equal, report that redundancy rather than a new quality achievement.

Root requires saved first outcomes. Refuse existing output paths. Preserve any
first initialization/inference failure and do not replace it with a successful
claim. Synthetic tests validate shape/nonfinite rejection, raw logits, pair cache
keys, Unicode hashes, complete candidate inventory and prefix binding before
quality evaluation. Model initialization, unique pair inference and total elapsed
times are separate; CPU resource use/runtime versions are recorded.
