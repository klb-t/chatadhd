# Direct typed-graph lookup: first DEV results

The frozen GPT-extraction → direct lookup pipeline answers **88/96** DEV
explicit-source queries correctly, with **96/96** available. This is a
retrospective source-cutoff projection: extraction saw each complete conversation,
including later turns, whereas the raw judges saw physically truncated prefixes.
The figures below are diagnostic comparisons, not a causal ranking of methods.
No validation or paid API was accessed for this increment; no graph was promoted.

## First outcomes and denominators

| Method | Correct / planned | Available / planned | Input presented to model |
|---|---:|---:|---|
| GPT extraction → frozen direct lookup | 88/96 | 96/96 | Full conversation, then source-time cutoff |
| Raw Jev dual-Noul judge | 86/96 | 90/96 | Query-specific prefix |
| Raw GPT ternary V2 judge | 81/96 | 96/96 | Query-specific prefix |
| Always supported | 36/96 | 96/96 | Constant |
| Always unknown | 36/96 | 96/96 | Constant |
| Always refuted | 24/96 | 96/96 | Constant |

Jev's six contradictory dual-Noul answers remain unavailable in the 96-query
denominator. The earlier GPT V1 transport failure is preserved separately and
is not silently replaced by V2. No gold-based ensemble was executed.

| Lookup class | TP | FP | FN | Precision | Recall |
|---|---:|---:|---:|---:|---:|
| supported | 32 | 3 | 4 | 32/35 | 32/36 |
| refuted | 21 | 1 | 3 | 21/22 | 21/24 |
| unknown | 35 | 4 | 1 | 35/39 | 35/36 |

Macro recall is 0.912037; macro precision over all three defined classes is
0.922089. Attribution and correction/known_at each score 20/24; negation/unknown
and paraphrase each score 24/24. EN scores 46/48 and PL 42/48; this small case mix
does not isolate a causal language effect.

Paired diagnostic recount against Jev: both correct 81, lookup-only correct 7,
Jev-only correct 5, both wrong 3. Against GPT: both correct 75, lookup-only correct
13, GPT-only correct 6, both wrong 2. These counts retain the information
asymmetry. All three fail `gpv1_dev_018_q2`. Full disagreements are preserved in
`direct_lookup_v1/first_comparison.json`.

## Error analysis without changing frozen v1

| Query | Gold → lookup | Preserved diagnostic cause |
|---|---|---|
| 001_q1, 003_q1, 004_q1 | supported → unknown | Model invented a supersession across attributed speakers; frozen policy applies it with a warning, so the older speaker assertion disappears from the active projection. |
| 006_q2 | unknown → refuted | Reporter's nonendorsement of another person's statement was compiled as a negative implication. Binding is valid; the quote does not semantically assert that negation. |
| 016_q1 | supported → unknown | Replacement implication is missing after the original quote binder rejected model evidence. |
| 016_q2, 017_q2, 018_q2 | refuted → supported | Later Polish negative assertion/status correction is missing, leaving the older positive assertion active. |

Query IDs use the common `gpv1_dev_` prefix. These are failures of the composed
pipeline, even where lookup correctly executes the supplied graph. Exact quote,
source, turn and timestamp binding proves neither semantic adequacy nor world
truth. The code intentionally does not repair model assertions from gold or
post-outcome interpretation. Cross-attribution authority and nonendorsement
semantics need independent policies/fixtures before a future model experiment.

## Mechanism, provenance and measurement limits

The same policy on the authored oracle graph scores **96/96**, reported separately
in `first_score.json` under `oracle_graph_mechanism_only`. That measures the lookup
mechanism on supplied records, not extraction or model quality. Direct matching
preserves predicate, direction, node-level operand meaning and attributed speaker;
there is no transitivity, contraposition or implicit reflexive inference.

Each prediction preserves history/active assertion IDs, applicable status events,
raw selected assertion records, evidence spans and source timestamps. Paths are
marked compiled candidate lookups with semantic verification false and world
content unverified. Full-conversation provenance and causal-prefix verification
false are explicit on every prediction. The original source `known_at` is not
model claim availability. `model_response_availability.json` separately preserves
all 24 actual response receipt timestamps and response hashes from the immutable
ledger; per-record compilation availability was not captured in the compiled ABI.

First independent mechanism run was 22/31 passing: one real ordering defect and
an unavailable null/absent schema ambiguity caused eight erroring methods. First
outcomes remain in `DIRECT_LOOKUP_FIRST_MECHANISM_RESULTS.json`; the ordering and
uniform-null API were fixed before freeze, then independent tests passed 31/31.
Own driver fixture failures were retained separately. Final relevant tests passed
67/67 before the 96 actual predictions; protocol and both test files are hashed
in `freeze_before_predictions.json`. Tests verify mechanisms, not model quality.

The existing extraction used 24 paid requests, reported USD 0.0188832 and summed
request duration 94.708727 s. Lookup made zero new requests; reusing those records
costs zero additional model calls. Raw Jev used USD 0.002989266 / 22.004745 summed
request seconds, and raw GPT V2 USD 0.0196232 / 99.851777 summed request seconds.
These duration sums are not wall-clock latency, and amortized extraction cost
must not be presented as zero initial model cost.

## Decision and reproduction

**Keep as an explicit candidate pipeline and investigate extraction semantics.**
The source-binding mechanism is useful, but it cannot correct unsupported
relation/event meaning. Preserve this v1 and all first predictions; do not tune
actor/event policy on these eight errors. Parent-approved next controlled
ablation substitutes the already frozen turn-reference binder output while
keeping the exact lookup policy and code. A causal pipeline comparison would
require fresh prefix-only extraction, not merely a source timestamp filter.

From repository root, using new output paths:

```sh
python loom/tools/structure/new_graph_direct_lookup.py freeze --output /tmp/direct-freeze.json
python loom/tools/structure/new_graph_direct_lookup.py predict --freeze /tmp/direct-freeze.json --output /tmp/direct-predictions.json
python loom/tools/structure/new_graph_direct_lookup.py score --freeze /tmp/direct-freeze.json --predictions /tmp/direct-predictions.json --output /tmp/direct-score.json
PYTHONPATH=loom/tools/structure python -m unittest test_new_graph_direct_lookup test_new_graph_direct_lookup_driver test_graph_panel_live test_graph_panel_score_run
```

The original immutable outputs are `direct_lookup_v1/freeze_before_predictions.json`,
`first_predictions.json`, `first_score.json`, `first_comparison.json`, and
`model_response_availability.json`. Reproduction has a new computation timestamp;
compare decisions and provenance, not raw timestamp-bearing JSON hashes. No sealed
validation, prefix extraction quality or real-world content truth was verified.
