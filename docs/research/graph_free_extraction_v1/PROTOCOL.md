# Source-only free graph extraction: preregistered DEV ablation

Protocol and code freeze precede all24 new model calls/outcomes. The source panel
is the same24 development conversations as graph_methods_panel_v1, with the same
raw turns/source identity. No validation input/gold or older blind holdout is read.
This adds a distinct task/information condition; it does not replace first assisted
or supplied-edge results, fit an alias threshold, or infer model-wide reliability.

| Track | Information supplied | Output | Meaning of primary measurement |
|---|---|---|---|
| Free source-only extraction |id, source_id, raw turns only|Own nodes + typed attributed/dated source assertions and events|Representation-dependent lower bound after fixed strict reference alignment|
| Assisted extraction (preserved DEV first run)|Raw turns + reference node inventory|Typed assertions/events using supplied node IDs|Typed edge/annotated-turn matching conditional on supplied nodes|
| Supplied-edge judgment (preserved Jev/GPT first runs)|Raw exact-cutoff prefix + inventory + supplied typed edge/actor/time|Source supported/refuted/unknown (Jev also conflicting)|Matched candidate-edge source judgment, not free graph extraction|

Free versus assisted is an inventory ablation/task contrast, not matched
classification. Node information differs; output allowance also differs2048 versus
1536 tokens. Any later comparison must state these conditions and cost. Supplied
edge judgment96queries and extraction24conversations have different denominators.
No graph-quality score should conflate them. Content truth remains unverified.

## Free input, output and provenance

`source_payload` selects exactly `id`, `source_id`, `turns`. Prompt receives no
reference inventory, aliases, judgments, gold/family labels or path annotations.
Preparation uses inputs only. Model pins stay GPT`openai/gpt-4.1-mini`, provider
`openai`, temperature0, no fallback, JSON-object mode, explicit usage and the same
prompt0.4/completion1.6 USD/million price caps/public snapshot as DEV. Output cap
is2048. Prompt/system/body hashes are saved before calls. Root's existing global
nonreset USD2 authorization/central ledger guard remains authoritative; this arm
does not receive a new USD2 budget. The author runs no API and handles no key.

Output is exactly `{nodes,source_assertions,status_events}` arrays. Nodes have
distinct local `id` and source-worded `text`. Source assertions/events have the
same frozen typed relation/direction/polarity/actor/known_at fields as assisted
extraction, with **own** node IDs and nonempty evidence `[{turn_id}]` only. Model
does not count spans or supply a possibly altered quote. The adapter copies the
complete identified raw turn and derives exact Unicode/UTF8spans by the unchanged
primary evidence/compiler path. Unknown turn, incompatible known_at, invalid
field/type, duplicate ID, truth flag and missing endpoint remain rejected records
with denominators, not silent omissions.

The output preserves accepted own nodes, raw model object/canonical hash, exact
source-payload hash and full-turn provenance. It never rewrites the raw graph to
reference IDs. Edges/content remainunverified. Node proposals are model-proposed
source expressions; existence of a raw quote does not prove its relation semantics.
No canonical graph fact, source confidence or world-truth promotion occurs.

## Frozen alignment and primary scoring

Reference inventory text+aliases enters **evaluation only after first outputs are
preserved**. Normalization is fixed: NFKC, casefold, whitespace collapse, outer
whitespace trim, terminal sentence marks `. ! ? ; : … 。 ！ ？` strip. Internal
negation, modality, quantifiers, relation operators, quotes and parentheses remain.
There are no embeddings, thresholds, learned mapping, synonym lookup, reference
gold-label-dependent choice or best-match search.

Normalized own node text matching exactly one distinct reference ID maps to that
ID. Zero matches is unmatched. Two or more distinct reference IDs is ambiguous
and remains unmapped even if one is the desired gold endpoint. Exact alternatives
declared in input aliases are allowed; novel valid paraphrases can still miss.
This makes the primary strict-edge score a **representation-dependent lower
bound**, not general end-to-end semantic graph accuracy. Independent semantic
alignment review of unmatched/ambiguous nodes is a separate diagnostic and cannot
rewrite first primary output/mappings or tune the frozen matcher.

Only an evaluation copy maps endpoints. An edge with either endpoint unmapped is
one strict-alignment FP and leaves its gold source edge FN. Typed edge P/R reuse
the unchanged primary one-to-one record scorer, including actor, polarity,
predicate, direction, known_at, exact evidence and status events. Invalid compiled
edges are already counted as FP and are not counted again as unmapped edges.
Every absent/unavailable conversation retains all applicable gold edges/events.
Primary supersession-target convention stays frozen; any legitimate alternative
target review remains explicitly secondary.

The node gold denominator is **reference atoms used as endpoints in explicit gold
source_assertions**, not every inventory/control node:60 atom records across24DEV
cases. Node TP is unique covered used-reference atoms; FN is missing used atoms.
Unmatched/ambiguous/invalid own node records are alignment FP, with precision
denominatorTP+those records. This is a strict-reference alignment count, **not a
hallucination/world-fact error count**. Valid source expressions outside the bounded
reference graph or novel paraphrases require independent review. Matched unused
reference control nodes are reported separately and excluded from primary node
P/R; discovering one is not a world-fact error. Multiple own IDs mapping to the
same used-reference atom are deduplicated for node coverage, separately diagnosed;
repeated source-edge records still face frozen one-to-one edge matching and FP.

Empty graph baseline: node recall0/60, assertion recall0/60, event recall0/6;
precisionundefined0/0, not a successful score. All denominators are retained.
Report case/family/language failure details; do not hide unmapped records inside
a high precision among only mapped predictions.

Full-turn evidence avoids source-span fabrication but does not certify clause
support, correction/attribution interpretation or valid discovered expression
type. Manual semantic quote/alignment review remains separate. A conditional
asserts a relationship between propositions, not the truth of either proposition.
Negation within a node is distinct from negative relation polarity. Nonendorsement,
silence and a different actor's denial do not deny/supersede a content edge.

## Execution and measurement preservation

`prepare` writes exact rows and deterministic greedy batches in fixture order,
at most12rows and0.10USD exact reservation each. A single row exceeding the cap
fails closed. This uses pre-outcome byte/allowance estimates; no adaptive outcome
choice or paid retry. Root uses existing bounded transport/shared accounting.
All inherited unknown charges/reservations remain; cost-unknown is never zero.

```sh
python3 -m loom.tools.structure.graph_free_extraction prepare \
  --output docs/research/graph_free_extraction_v1/prepared
python3 -m loom.tools.structure.graph_free_extraction score \
  EXACT_MANIFEST RUN_DIRECTORY --output NEW_FIRST_SCORE_DIRECTORY
```

Replay requires exact frozen complete batch rows (an arbitrary shortened inventory
cannot erase unattempted planned denominators), immutable ledger/response hashes,
frozen public model/provider identity, explicit GPT billing/nonBYOK and primary
integrity helper checks. Raw↔ledger cost mismatch fails hard, rather than producing
a falsely cheap unavailable result. Missing/declined/malformed outputs retain IDs.
First compiled own-node graphs and execution summaries are written exclusively
before gold loads. Scoring recompiles saved raw model output against raw source
before alias alignment to reject post-compilation edge/node/evidence mutation.

Record first class/edge/node/event denominators, cost-known and cost-unknown,
per-request timing sums, all unmatched own-node texts/ambiguous alias IDs, missed
gold assertion IDs, language/family groups and semantic audit needs. Root combines
disjoint batches only after exact planned inventory verification. Keep/revert/
investigate decision is about this proposed method/information condition; do not
modify these first outcomes or the primary rules once observed.
