# Candidate response replay and graph comparison

`candidate_flow.py` connects the three research mechanisms without treating them
as separate authorities. A direct Entity/Claim draft bundle and B grounded frames
both pass through `candidate_graph.compile_bundle`. C can prepare their source
packet from an explicit snapshot and keep proposed thread/context changes in a
reversible overlay. The original graph and its Assessments remain unchanged.

This is an **offline response replay**, not a natural-language parser or a live
model benchmark. It does not call the configured semantic model. The native
application's existing bounded, source-grounded adapter still accepts its v1 flat
relation envelope; this research carrier has not replaced that production
contract. A provider adapter will need its own response schema, budget accounting,
native validation, cache/resume identity and quality evaluation before adoption.

## Input and execution

The JSON input has `records`, `query_ids` and optional `parameters`.
Every record has a unique `id`, `strategy` (`direct` or `frames`) and exactly one
of the following source forms:

- `source_packet`: the explicit `loom.source_packet/1` input.
- `context`: `base_snapshot`, `current_spans`, `claim_refs`, `time_cut` and optional
  `delta`, following `CONTEXT_DELTA.md`. Only the prepared model-facing packet
  feeds compilation. The retained full snapshot and undo sidecar remain local
  report data. Failed preparation or delta application stops that record;
  it never falls back to unfiltered source data.

For C-prepared input, every compiled Entity and Claim draft support span must
also fall within the union of selected `current_spans`. An exact quote elsewhere
in the same complete Observation is insufficient. Old Claim dependencies remain
separate context and cannot supply undeclared current evidence. This checks
explicit citations; it does not prevent implicit model use of other visible text.

A direct record adds `bundle`, following `CANDIDATE_GRAPH.md`. A frames record
adds `anchoring` and `composition`, following `GROUNDED_FRAMES.md`. Optional
`source_group` and `domain` are caller declarations, not verified independence.

```sh
python loom/tools/structure/candidate_flow.py /tmp/replay.json \
  --output /tmp/replay-result.json
```

The output path must be new. Python callers can use
`run_candidate_flow(records, query_ids, **parameters)`.
Default bounds are 32 records, 2048 graph vertices per alternative, five ranked
candidates per query and 10000 exact-search states per pair. Records are selected
by ID for deterministic bounded replay, not by a relevance heuristic. All record,
node-budget, abstention and invalid-candidate omissions are explicit. Query IDs
address input records; every accepted interpretation is queried separately.
Top-k omissions and exhausted exact-search budgets retain the matcher's reports.

## Comparison policy

`mode: exact` compares supplied graph labels. `mode: occurrence_shape` explicitly
removes local occurrence display labels and printed symbols, recording each
original field in a loss map. Those strings are source notation, not assertions
that two occurrences have the same referent. Binding and identity represented by
shared vertices, `bound_to` or `denotes` remain in the graph. Existing canonical
Entity and Claim identities remain exact; they are not silently renamed.

Operation and quantifier literals, port names and ordinals, polarity, assertion
context, scope types and all directed edges remain exact in both modes. This is
not a logical-equivalence procedure: for example conjunction source order is
preserved even when reordering would preserve classical truth conditions.
Source lexical content remains available in the compiled report. A shape match
does not establish semantic equivalence, entailment or correct extraction.

Necessary filters and feature ranking order candidates. Bounded exact matching
then returns a graph witness or an explicit unresolved result. Alternative
readings are never merged or automatically selected. Alternatives from the same
input record do not become each other's recurrence candidates. Other records may
still contain the same source; overlap diagnostics make that visible.

For every ranked candidate, the report compares used Observation IDs, exact
located text hashes, prior Claim dependencies and repeated quote hashes. Conflicting
bodies under a reused Observation ID are exposed. Different declared groups do
not override shared source evidence. No detected overlap is **not** proof of
independence, and repeated short quotes are only a diagnostic. Consequently
`independent_recurrence` stays unknown and this runner does not compute a
universality score from overlapping views.

Eight author integration checks cover A/B convergence, explicit lexical loss,
separate alternatives, source overlap despite different group labels, invalid
quotes, abstention and budgets, malformed/future context, and a reversible C
overlay whose model-facing packet actually feeds the compiler, including current
span containment for draft support. They are separate
from independently authored supplied-graph measurements and live extraction
quality. No Claims are promoted, source bytes changed, or database writes made.
