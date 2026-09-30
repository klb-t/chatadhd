# Independent graph-panel scorer audit

2026-09-30. Read-only review of the frozen DEV adapter, protocol, prepared
hashes and DEV inputs/gold. No validation inputs/gold, credentials, API calls
or paid responses were read. The existing freeze and scoring code were not
changed. Machine-readable counts: `INDEPENDENT_SCORER_AUDIT.json`.

## Verified boundaries

- Five core freeze hashes and nine additional frozen file hashes match.
- All 20 existing adapter mechanism tests pass independently.
- DEV contains 24 conversations, 60 source assertions, six correction events
  and 96 explicit-source queries: 36 supported, 24 refuted, 36 unknown.
- Requests physically omit future turns and other queries; formal scope is
  rejected by the explicit-source recipe. Extraction omits judgment queries.
- Compiler derives exact Unicode/UTF-8 coordinates, rejects ambiguous quotes
  and timestamp mismatches, and retains historical assertions.
- Missing judgments retain all 96 denominators: coverage/accuracy zero,
  class FN 36/24/36, precision null. Missing extraction retains all gold edges
  and events. Jev conflicts remain unavailable, rather than unknown/refuted.

These checks validate mechanisms and the accounting boundary. They do not
measure Jev/LLM quality, content truth, natural-text recall or production use.

## Counterexample 1: exact citation is not clause support

In-memory oracle-shaped assertions retained all gold typed fields but changed
each evidence quote to a unique **single character** in its correct source
turn. All 60 were accepted and scored TP: 60 TP / 0 FP / 0 FN, with 60 narrow
quote review flags. No model generated these predictions.

This demonstrates the already declared limitation: the current primary edge
score measures typed relation/direction/polarity/attribution/time plus exact
turn binding. It cannot establish that the selected substring supports the
relation. Report the narrow-quote review count and separately adjudicate clause
adequacy; do not relabel this score strict semantic grounding accuracy.

## Counterexample 2: correction replacement convention is underspecified

DEV013 retains these observed-source assertion labels:

- `e1`: positive causes A→B at t1;
- `e2`: explicit negative causes A→B at t2;
- `e3`: positive causes A→C at the same correction turn t2.

Gold selects `e1 superseded_by e3`. The prompt asks for a grounded local
replacement ID but does not specify why the new explicit denial `e2` is an
invalid replacement for the earlier positive A→B assertion. Changing only
the oracle event target to `e2` is accepted by the compiler (zero invalid
events), preserves 3 TP / 0 FP / 0 FN edges, yet gives events
0 TP / 1 FP / 1 FN. The complete correction quote and date are unchanged.

Keep the frozen first metric. Its event score measures agreement with this
gold convention. Record semantically defensible replacement alternatives as
a separate diagnostic; any broader adjudicated score is explicitly post hoc.
Do not count this mechanism counterexample as a model failure or success.

## Unmeasured recipe risk: reaffirmation after denial

Jev support asks for an *active* positive assertion. Its refutation criterion
asks whether the speaker denied/withdrew the relation *by as_of*, without
explicitly limiting that negative to the latest applicable view. An authored
positive→negative→reaffirmation sequence can therefore satisfy both literal
criteria although the latest view supports the relation. This is a recipe
hypothesis, not an observed Jev error. DEV has no reinstatement sequence.
Retain v1; a later recipe needs an explicit active-negative policy and a new
diagnostic sequence. GPT's latest-view instruction is clearer on this point.

## Interpretation limits

The panel is a supplied-inventory diagnostic. Inventory may name propositions
not yet asserted in an earlier prefix, so this is not a historical node-
discovery or future-prediction benchmark. Compiled `observed_source_assertion`
is the class being predicted; the score does not certify it as an observation
outside this research output. No result may mutate the authoritative graph.
Formal paths, family-wise results and per-class errors must remain separate
from direct source-edge judgments. Validation remains sealed.
