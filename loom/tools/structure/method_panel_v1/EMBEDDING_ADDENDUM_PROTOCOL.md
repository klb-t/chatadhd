# Frozen learned-embedding addendum protocol

2026-09-30 UTC, before reading embedding scores against gold. This is a separate
addendum; the 13-method model-free first-run protocol, scores and results remain
unchanged. Parent requested evaluation of the independently frozen local learned
embedding arm using the already predeclared generic thresholds
**0.25 / 0.50 / 0.75 / 0.90** plus continuous AUROC, tie-block AP and coverage.
No encoder, vectors, weights, tokenization, pooling, scores or thresholds change
after this comparison. Raw artifact `prediction:null` remains untouched.

Input artifact: `loom/tools/structure/local_embedding_panel_v1/scores.json`,
SHA-256 `0b59dd984a254f6080edb74886d469436e3cb0bba98902b070d8e78f702bec3f`.
It declares the same 48-pair inputs SHA-256 as the main frozen protocol and a
pinned public multilingual MiniLM encoder using local CPU ONNXRuntime. The
independent arm read no labels before computing its vectors/scores. Its local
inference follows the separately saved model/tokenization/runtime protocol.
This is a **learned text embedding**, distinct from character TF-IDF and from
argument-graph embeddings. No graph-role representation is implied by its cosine.

Evaluate the exact frozen 48 scores, preserving all method/input/model/vector
provenance and pairing by opaque case ID. Missing scores abstain. Report fixed
threshold TP/FP/TN/FN, precision and planned/represented recall with denominators,
continuous ranking metrics and family/contrast/language/inherited-split groups.
All 48 remain exploratory reused authored material, never independent validation.
The generic thresholds are descriptive, not calibrated probabilities or a chosen
best operating point. Preserve every error; no post-result score adjustment.
