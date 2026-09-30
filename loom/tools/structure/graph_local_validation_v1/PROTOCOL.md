# Generic released graph-local replay

This wrapper is frozen before root releases graph-panel validation. It never
opens a fixture or gold file itself. The caller supplies cases only after
release, and supplies gold only after immutable first scores and vectors exist.
Its dependency release binds the unchanged graph-local DEV methods, policy,
scorer, cached learned instrument and model/environment manifests. No threshold,
weight, text representation, encoder, candidate eligibility or score rule changes.

Only `explicit_source` queries are prepared. Other query scopes are recorded as
excluded and belong to a separate formal-reasoning recipe. Each prepared query
physically includes only turns with known_at <= as_of using the unchanged
adapter. Lexical frequency cosine has no globally fit IDF or vocabulary;
character frequency cosine uses unchanged 3–5 grams; learned MiniLM uses the
same cached pinned ONNX model, pooling, normalization and token limit. A
stateless exact-string vector cache shares computation only. Fusion is the
unchanged maximum available candidate score, without gating other channels.

Primary output is conditional retrieval for a supplied edge, with null semantic
prediction. A separate naive diagnostic at unchanged thresholds .25/.5/.75/.9
maps maximum similarity to supported/unknown and never infers refutation.
The main descriptive threshold is .5; K is unchanged at 1/3/5. Evidence targets
are derived only during evaluation with the unchanged `relevant_evidence`
helper, including explicit negative assertions and applicable supersession
events. Cases and query inventories must match; first-score hashes, exact
physical prefixes and candidate bindings are rechecked before evaluation.

The wrapper may be checked against already observed DEV outputs to establish
transport equivalence only. No validation data is used before release. No API,
model download, tuning, graph promotion or secret access occurs. Models and
runtime packages are loaded only from the existing verified local cache.
