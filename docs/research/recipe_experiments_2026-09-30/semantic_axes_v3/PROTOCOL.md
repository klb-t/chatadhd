# Source-only semantic contrast v3, registered before inference

The baseline source-only v1 diagnostic revealed two kinds of semantic errors
after isolating evidence-format loss: negated operand propositions mistaken
for denial of the relation, and report/nonendorsement or missing scope mistaken
for an explicit negative source commitment. These observations motivate a new
prompt recipe, not silent correction of its historical outputs.

## Single intervention and comparator

Append one generic semantic-contrast paragraph to the **unchanged v2 literal
JSON grammar system prompt**. Symbolic P/Q examples distinguish affirming
`if NOT(P) then NOT(Q)` from denying `P implies Q`; polarity belongs to the
whole relation, while operand negation remains in exact source wording. A
reporter's nonendorsement is contrasted with explicit denial by the attributed
speaker; missing scope and silence remain neither positive nor negative.

No fixture/source terms, gold labels, selected query IDs, corrected outputs,
semantic aliases or outcome-specific record list enters this paragraph. It
explains the inherited rules; it does not change their policy. Preserve the
entire v2 grammar prefix byte-for-byte. Source-only user payload remains exactly
`id/source_id/turns`: no supplied inventory, aliases, judgment queries or gold.

Same24 DEV conversations, order, full raw sources, GPT4.1-mini/OpenAI pin,
temperature0, JSON-object response mode, max2048 output tokens, usage accounting,
caps and original strict compiler/alignment/scorer stay unchanged. The additional
prompt bytes necessarily change reservation planning. Two12-request batches use
new experiment IDs; each exact batch is at mostUSD0.10 within the existing
shared nonresettingUSD2 budget. No frontier USD20 pilot belongs to this method.

V2→v3 is the semantic prompt contrast; v1→v2 is the grammar intervention. The
evidence-format decoder of the historical v1 responses is a third, separate
post-DEV compiler experiment. Do not collapse these hypotheses, compare v3 to
v1 as if only semantics changed, or use projected v1 outputs as v2/v3 responses.

## First measurements, errors and policy decision

Keep all planned24 case denominators including duplicate JSON keys, malformed
evidence, length truncation, identity failures and unavailable first responses.
First compiled outputs and execution summaries persist before reference labels
load. Primary metrics: availability /24, node/assertion/event precision and
recall with the existing60/60/6 reference denominators. Report actual cost,
unknown reservations, tokens and request timers separately.

Manually inspect semantic false positives, especially negated-operand/polarity
and reporting/silence confusions, and every new false negative relative to v2.
Source citation validity does not prove the clause or relation. Source-history
retention, quoted attribution and correction events remain separate error axes.
The model is a conditional task/representation/recipe instrument; this authored
correlated DEV panel gives no global reliability or independent validation.

Keep as an experimental recipe only if actual first responses improve the
identified semantic failures without hiding regressions. Investigate a gain
accompanied by losses or reduced candidate coverage; preserve both variants
when data do not decide. No threshold tuning, gold changes, duplicate-key
salvage, timestamp/actor/predicate repair, paid retry or production promotion.

## Execution status

The protocol and exact prepared bodies are frozen before any v3 inference.
At freeze time v2 outcomes are also still unavailable. Root alone may execute
after recovering the existing key and checking the shared remaining budget.
This workstream reads no credential and performs no paid call. Offline mock
transport tests measure parser/request integrity, not semantic model quality.
Sealed validation and temporal holdout are not read or used for development.

```sh
python3 -B -m loom.tools.structure.graph_free_semantic_axes_v3 prepare \
  --output docs/research/recipe_experiments_2026-09-30/semantic_axes_v3/prepared
python3 -B -m loom.tools.structure.graph_free_semantic_axes_v3 freeze \
  --prepared docs/research/recipe_experiments_2026-09-30/semantic_axes_v3/prepared
python3 -B -m loom.tools.structure.graph_free_semantic_axes_v3 score MANIFEST RUN \
  --output NEW_FIRST_SCORE_DIRECTORY
```
