# Independent first-artifact audit

Reviewer: frontier_matrix, distinct from the retrieval implementation owner.
This audit reads only the authorized DEV source/gold and frozen first artifacts.
It changes no retrieval implementation, ranking recipe, annotation or first result.
The DEV results are already inspected; this is neither blind validation nor a new
population-quality experiment.

Before the audit recount, the checks are fixed as follows:

1. Verify ZIP identity, all three raw payload identities and the before-score and
   before-gold freezes. Reconstruct prefixes from raw source dates and preserve
   every eligible source turn, representation and original query.
2. Reconstruct gold evidence from matching source assertion polarity and explicit
   supersession events, validate UTF-8/character coordinates, then compare with
   the inherited targets. Do not import the original evaluation functions.
3. Independently bind all four primary query views plus the structured cap128
   control to the recorded pair cache; verify exact strings, source identity,
   timestamps, caps, raw finite logits and all 34 rankings with stable tie order.
4. Verify all 1062 unique inputs using the pinned tokenizer. Recompute token
   counts and little-endian int64 tensor hashes. Run the pinned CPU ONNX model
   once over these unique pairs, using two intra-op and one inter-op thread;
   predeclared absolute tolerance is 1e-6, with exact equality reported separately.
   This local audit costs no external API dollars. It must preserve the original
   logits and record any disagreement rather than replace them.
5. Recount hit@1/@2/@3, span recall, turn precision, reciprocal rank, AP, and
   within-query pair AUC across all methods and each language/family/label group.
   Independently recount paired gains/losses and all inherited byte allocations.
   Retain the 96-query, 60-evidence-query, 66-span and 252-candidate denominators.
6. Inspect the already reported correction and reversed-denial counterexamples
   with raw source strings and logits. Check whether the evidence supports the
   document's claim; do not promote a ranking score to a graph fact or select a
   new winner.

The audit script and its input hashes will be frozen before execution. The first
audit outcome is written exclusively; reruns need a distinct output. A source or
artifact mismatch is a failure to investigate, not permission to rewrite prior
evidence. Runtime measurements from this host are descriptive, not a production
throughput promise. No validation or old holdout file is opened.
