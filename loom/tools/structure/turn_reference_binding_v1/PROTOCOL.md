# Turn-reference binding v1 — frozen DEV replay protocol

This is a post-result, deterministic mechanism experiment on the same 24
preserved GPT assisted-extraction responses. It is not a new model evaluation
and is not confirmatory validation. The parent supplied the observation that
four Polish quote-copy failures caused four strict edge false positives and
four false negatives. That observation motivated this arm. No validation
input/gold is available to this experiment.

## Hypothesis and single intervention

Hypothesis: a compiler input derived from an explicit, valid source turn ID
can avoid quote-copy failures without changing the model's typed assertions.
For each evidence entry with exactly the existing fields `turn_id` and
`quote`, and a valid string turn ID in that case, replace only `quote` with
the full, unchanged source `turn.text`. Preserve the original model quote
as a model hint in a separate provenance artifact. Do not change relation,
source/target, polarity, attribution, assertion IDs, supersession IDs, status,
known_at, evidence count/order, or any other field. Invalid IDs and malformed
evidence shapes remain untouched and are subject to the existing compiler.
The policy does not silently invent a missing quote field.

The source store is the authority for bytes, not the model's copied quote.
Full-turn binding establishes a source locator, not semantic support for the
claimed relation. A wrong model quote remains visible as a wrong hint. The
existing compiler continues to validate known_at, endpoints, ID consistency,
and exact spans; it does not prove that the cited turn entails the assertion.
Attribution and source truth remain unverified. No actor filter is applied.

## Frozen comparator, preservation, and criteria

Replay the original run through unchanged `graph_panel_score_run.load_run`
and require equality with its preserved first compiled artifact. Validate
original response hashes, billing ledger integrity, frozen request identity,
and recorded provider/model identity using the existing replay machinery.
Freeze protocol, policy, implementation, dependencies and all response/input
hashes before running this transform. Save the original extracted model
content, transformed inputs, complete provenance sidecar, original compiled
outputs and first transformed compiled outputs before loading DEV gold.
Then score both arms with unchanged `graph_panel_live.score_extraction`.
Require original primary metric equality with the preserved first score.

Report strict edge and status-event TP/FP/FN, precision/recall with predicted
and gold denominators; invalid assertion/event counts; per-case deltas;
binding coverage/refusals; unchanged typed fields; unchanged raw byte hashes.
Any new event acceptance is a diagnostic downstream effect of repaired edge
binding, not a separate intervention. Preserve ambiguous event-gold outcomes
under the frozen convention. No gold-derived event/actor repair is allowed.

Decision rule: keep as an opt-in candidate binding mechanism if it recovers
mechanical failures with no typed-field drift or lost previously accepted
record, while preserving all original hints. Investigate any precision/event
regression and semantic-support limits; do not promote it to a production
semantic extractor or claim generalization from this post-hoc DEV replay.

## Limits

This uses whole-case, retrospective model extraction. It does not establish
causally independent past-prefix judgments or graph completion quality.
Whole-turn source binding can retain an unsupported edge and cannot solve
negation, direction, cross-speaker supersession, or correction conventions.
No API, download, encoder/model change, label fit, or threshold tuning occurs.
Validation remains sealed. Raw responses and existing frozen files are read
only. A future actor-consistency arm, if any, must be separately named and
preserve invalid-event denominators; it is outside this primary arm.
