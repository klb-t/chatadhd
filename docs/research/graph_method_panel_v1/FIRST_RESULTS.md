# First graph-method panel DEV results, 2026-09-30

The frozen matched supplied-edge recipes produce **Jev 86/96 correct and GPT
81/96 correct**. Jev's 96 completed responses contain six dual-channel conflicts,
so semantic label coverage is 90/96; GPT has 96/96 ternary labels. Assisted GPT
extraction separately yields 56 TP / 6 FP / 4 FN source assertion records, with
precision 56/62 and recall 56/60. These are conditional results for this authored
development panel, not a model-wide reliability score or a content-truth test.

`aggregate_first.json` preserves independently recounted class/family/language
metrics, all failure IDs, all 20 direct disagreement source payloads, first
artifact hashes and execution summaries. `aggregate_first.py --check` reproduces
the report without overwriting it. Independent confusion algebra matches all four
frozen batch reports and the frozen aggregate scoring function. No API calls were
made by this report's author. Validation was not accessed.

## Scope, freeze and comparison

There are 24 new conversations: four development families with six controlled
variations each (three EN and three PL), four supplied queries per conversation.
Gold is supported 36, refuted 24, unknown 36. These are **source predicates** at an
explicit direction, attribution and `as_of`, not whether a claim is true in the
world. Missing assertion is unknown; reporting a quotation is not endorsing it;
negation inside a proposition is not negation of the relation. Node inventories
and queried endpoints are supplied. Extraction does not receive judgment queries
or gold. Models receive the same eligible sources and query scope for the matched
judge comparison; recipes/output formats remain instrument-specific.

Whole families were split before outcomes. The four sealed validation families
remain unavailable to developers. There is no threshold fitting, outcome-driven
prompt change, gold change, validation tuning, node-discovery measurement or
archive-wide graph recall. The 96 queries share family templates and conversations
and are not 96 independent natural-text trials. EN/PL counts describe this small
controlled panel; they do not estimate a general language effect.

Primary extraction/scoring/recipes were frozen in `FREEZE.json` before paid calls.
Integrity wrapper freeze4 was after transport started but before successful
response content was inspected by its author. Its history preserves that timing.
The original GPT JSON-mode judge failed on its first HTTP 400 because its prompt
lacked the provider-required literal word JSON. V1 remains separately scored as
48/48 unavailable, including 47 never attempted, and one unknown-cost attempt.
V2 changes only `Return exactly {` to `Return one JSON object exactly {` in a
separate declared recipe. The frozen primary compiler, labels and thresholds did
not change. V2's results do not replace or conceal V1 transport failure.

## Matched supplied-edge judgment

Precision and recall retain different denominators. Unavailable/conflicting
answers count as false negatives in their gold class, not as unknown predictions.

| Recipe | Supported precision / recall | Refuted precision / recall | Unknown precision / recall | Correct / planned | Label coverage |
|---|---|---|---|---:|---:|
| Jev dual Noul, independently >0.50 |34/34 /34/36|19/23 /19/24|33/33 /33/36|86/96 =89.58%|90/96|
| GPT ternary V2 |31/38 /31/36|22/30 /22/24|28/28 /28/36|81/96 =84.38%|96/96|
| Always unknown |undefined 0/0 /0/36|undefined 0/0 /0/24|36/96 /36/36|36/96|96/96|
| Token cosine naive ≥0.50⇒supported |33/84 /33/36|undefined 0/0 /0/24|8/12 /8/36|41/96|96/96|
| Character cosine naive ≥0.50⇒supported |22/41 /22/36|undefined 0/0 /0/24|28/55 /28/36|50/96|96/96|
| Learned MiniLM cosine naive ≥0.50⇒supported |29/78 /29/36|undefined 0/0 /0/24|7/18 /7/36|36/96|96/96|
| Max-score union naive ≥0.50⇒supported |33/89 /33/36|undefined 0/0 /0/24|3/7 /3/36|36/96|96/96|

Jev macro recall is 0.884259, GPT 0.851852; defined-class macro precision is
0.942029 versus 0.849708 (three defined classes for both). Accuracy among Jev's
available labels would be 86/90, but **86/96 is the primary planned-denominator
comparison**. Six conflicts are observed semantic conflicts, not transport loss.
Both Noul channels remain visible; neither channel vetoes the other, and conflicts
are not forced to whichever score is higher. Noul values were not assessed for
probability calibration here.

Confusions are gold rows and predicted columns:

| Jev gold | supported | refuted | unknown | unavailable |
|---|---:|---:|---:|---:|
| supported (36) |34|1|0|1|
| refuted (24) |0|19|0|5|
| unknown (36) |0|3|33|0|

| GPT V2 gold | supported | refuted | unknown | unavailable |
|---|---:|---:|---:|---:|
| supported (36) |31|5|0|0|
| refuted (24) |2|22|0|0|
| unknown (36) |5|3|28|0|

The local cosine rows are intentionally naive diagnostics, separately frozen.
Numeric output coverage is not graph-judgment capability. All have 0/24 refuted
recall. Local **retrieval**, a different task, ranks gold-bound evidence first on
token 45/60, character 44/60, MiniLM 46/60 and max-score union 45/60 queries. It
cannot be numerically equated to judgment accuracy. Token beats MiniLM and
max-score fusion in the correction family (12/18 versus 11/18 and 10/18 evidence
hits). K≥3 exhausts the at-most-three-turn pool; the resulting 60/60 retrieval
ceiling does not prove a strong reranker. See the independently frozen local
baseline report for evidence precision, span recall and matched context budgets.

| Family | Jev correct /24 | GPT correct /24 | Main diagnostic |
|---|---:|---:|---|
| attribution |21|18|quotation, nonendorsement and actor identity|
| negation / unknown |24|20|negative proposition versus negative relation|
| correction / known_at |17|20|active assertion versus preserved history|
| paraphrase |24|23|forward relation does not assert or deny its reverse|

Language results: Jev EN43/48 and PL43/48; GPT EN41/48 and PL40/48. Complete class
P/R with denominators for both family and language remain in the JSON artifact.
In particular Jev correction refutation recall is **1/6**, despite aggregate
19/24 refutation recall; GPT gets4/6. High aggregate scores conceal this lifecycle
failure. All 12 historical cutoff queries in this family are correct for both
instruments: physically excluding future turns worked, while interpreting all
eligible historical statements at the latest cutoff remained difficult.

## Failure and direct-disagreement audit

Diagnoses below describe observed source patterns and compatible recipe/error
hypotheses. Responses do not expose model reasoning, so these are not proven
internal causal explanations.

All Jev failures:

| Query IDs | Gold → primary prediction | Source pattern |
|---|---|---|
|gpv1_dev_004_q2, gpv1_dev_005_q2, gpv1_dev_006_q2|unknown→refuted|PL reporter explicitly does not endorse quoted conditional; this is not their denial of the edge. Another actor's denial also cannot be reassigned to reporter.|
|gpv1_dev_013_q1|supported→unavailable (conflicting)|Correction denies A→B and asserts A→C; Noul support0.57 and refute0.56 both cross the fixed threshold.|
|gpv1_dev_013_q2, gpv1_dev_014_q2, gpv1_dev_015_q2, gpv1_dev_017_q2, gpv1_dev_018_q2|refuted→unavailable (conflicting)|Old positive A→B is preserved in history, latest correction denies A→B. Both historical support and denial remain detectable; the recipe does not reliably resolve active status.|
|gpv1_dev_015_q1|supported→refuted|Correction's affirmative alternative A→C is outside the denied A→B scope (Noul support0.43/refute0.51).|

All GPT V2 failures:

| Query IDs | Gold → prediction | Source pattern |
|---|---|---|
|gpv1_dev_001_q2, gpv1_dev_002_q2, gpv1_dev_003_q2, gpv1_dev_005_q2|unknown→supported|Quoted person's conditional becomes reporter's endorsement despite explicit nonendorsement.|
|gpv1_dev_005_q4|unknown→refuted|Forward A→B quoted by Olek neither asserts nor denies Olek's B→A.|
|gpv1_dev_006_q4|unknown→supported|Iga's forward A→B becomes reverse B→A.|
|gpv1_dev_008_q1, gpv1_dev_010_q1, gpv1_dev_012_q1|supported→refuted|Affirmed implication has negative propositions at both endpoints; source explicitly affirms that signed-node implication. Reverse implication denial is a different edge.|
|gpv1_dev_012_q4|unknown→refuted|No explicit C→B is supplied; silence and a different reverse denial do not refute it.|
|gpv1_dev_013_q1, gpv1_dev_015_q1|supported→refuted|Alternative cause in correction falls outside preceding A→B denial scope.|
|gpv1_dev_016_q2, gpv1_dev_018_q2|refuted→supported|Latest explicit denial loses to preserved earlier causal assertion.|
|gpv1_dev_020_q3|unknown→refuted|Two paraphrased forward statements and a separate A→C denial do not deny reverse B→A.|

Paired outcomes: **75 both correct, 11 Jev-only correct, 6 GPT-only correct, 4 both
wrong**. The methods give different primary labels on20/96 query IDs. Both are
wrong on gpv1_dev_005_q2, gpv1_dev_013_q1, gpv1_dev_015_q1 and gpv1_dev_018_q2; only
015_q1 is a shared identical wrong label. Agreement is therefore not correctness.
An oracle selecting the correct method would cover92/96, but this uses gold and
is **not an executable ensemble policy or reported model score**. Disagreement
should trigger source diagnosis, not one channel's veto. The complete matched
prefix/query/node inventory and both raw outputs for all20 disagreements are
saved, including Jev's independently retained support/refute scores.

## Assisted source-graph extraction and status events

| Group | TP | FP | FN | Precision | Recall |
|---|---:|---:|---:|---|---|
| all24 conversations |56|6|4|56/62|56/60|
| attribution |12|1|0|12/13|12/12|
| negation / unknown |12|1|0|12/13|12/12|
| correction / known_at |14|4|4|14/18|14/18|
| paraphrase |18|0|0|18/18|18/18|
| EN |30|1|0|30/31|30/30|
| PL |26|5|4|26/31|26/30|

62 records were proposed:58 compiled valid records and four compiler-rejected
assertions. Two remaining FPs are semantic: gpv1_dev_006 adds a negative reporter
A→B from nonendorsement; gpv1_dev_007 adds a negative B→C from “says nothing about
whether C.” Four rejected records and four missed gold records occur at
gpv1_dev_016 (e2/e3),017(e2),018(e2); exact raw-output quote/type causes require
the separate audit. Invalid outputs retain the proposed-record denominator.
The empty extraction baseline is0/60 recall with precision undefined0/0.

Primary status events fail: **0 TP /9 FP /6 FN**. Three FPs in cases001,003,004
incorrectly treat a different actor's denial as superseding the quoted person's
assertion. Three EN correction events in013–015 select the same-turn negative
A→B as the supersession target, while gold selects positive replacement A→C.
Both targets can be a reasonable convention; the frozen prompt did not specify
that distinction adequately. The separately declared convention diagnostic
accepts those three legitimate alternatives and gets3 TP /6 FP /3 FN. It does
not replace primary0/6 or excuse the actor errors; PL correction events fail
because their targeted assertions did not compile. This is a protocol ambiguity,
not evidence that all six corrections were semantically missed by the model.

Exact source quote/UTF8/turn binding is verified mechanically, but does not by
itself prove a quote supports the claimed directed relation.21 narrow quote
matches are flagged for clause review. An independent pre-outcome mechanism
counterexample using unique one-character quotes can satisfy all60 edge bindings.
Consequently56TP means typed edge plus annotated turn matching, not certified
semantic quote adequacy. The separate independent quote audit must accompany any
stronger grounding claim. Expression type also matters: an observed statement
about nonendorsement/silence must not be silently encoded as a denial of the
content relation. Node-internal negation and relation polarity remain separate.

## Execution accounting and decisions

| Track | Actual attempts | Compiled transport outputs | Reported known USD | Sum recorded request seconds |
|---|---:|---:|---:|---:|
| GPT assisted extraction |24|24|0.0188832|94.708727|
| GPT supplied-edge V2 |96|96|0.0196232|99.851777|
| Jev supplied-edge |96|96|0.002989266|22.004745|
| GPT supplied-edge V1 failed first request |1|0|unknown, not certified zero|0.254900|
| Total actual |217|216|0.041495666 known +1 unknown attempt|216.820149|

Durations are sums of recorded request timings, not end-to-end wall-clock or a
hardware-controlled latency benchmark. Raw/ledger billing agreement is required
by freeze4; mismatches fail hard instead of being swallowed as unavailable model
outputs. The unknown V1 HTTP400 retains0.001366 USD reservation in the existing
shared nonreset budget. This report adds no budget or paid authorization.

- **Keep** both independently measured judgment instruments and the local
  retrieval channels. Their failure profiles differ; no global winner/reliability
  claim follows from this development panel.
- **Reject** similarity-to-fact/edge promotion, nonendorsement-to-denial,
  silence-to-refutation and different-actor supersession.
- **Investigate** explicit active-status denial/support recipes on a separately
  frozen lifecycle fixture (reassertion after denial matters), signed proposition
  versus edge polarity, and typed expression events. Preserve these first results
  and freeze any new method before new outcomes.
- **Resolve in a later protocol** the legitimate supersession-target convention
  and clause-support verification, without rewriting V1 primary labels/results.
- **Next release** whole-family validation only after every intended method and
  execution/scoring wrapper is frozen. Older GPT48/48 matched-pair results do not
  transfer to these fresh conversations; the new GPT81/96 is direct evidence of
  that task/representation/recipe dependence.
