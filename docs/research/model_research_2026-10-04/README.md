# Workstream 7 — model research, closing handoff

Closed 2026-10-05 on `gpt/model-research-2026-10-04`. The owner's latest
instruction stops new work and paid calls: finish stage 3, publish the existing
data and provide a handoff. There have been **no new paid calls after that
stop**. The ready commit, final gate evidence and exact continuation order are
in [the closing report](../../reports/model-research-2026-10-04.md).

The separate programme retains **720 unique physical first attempts** with
verified actual charges of **USD 0.873216500**, no pending charges and no
unresolved reservations. The provider limit remains **USD 5**, leaving
**USD 4.126783500**. The owner's authorization was EUR 5; the two currencies
are not interchangeable. See [BUDGET.md](BUDGET.md) and the machine-readable
[closing accounting data](stage5/CLOSE_ACCOUNTING_20261005.json).

Stage 3's original 12 calls cost **USD 0.317069000**. Eight scientific
repetitions on the same known cases cost **USD 0.148910200**: **USD 0.465979200**
combined. The source-relative manual review is separate from mechanical
acceptance, which remains **0/12** and **0/8**, respectively. The selected
GPT pattern and Gemini completion recipes are qualified observations about
this small inspected DEV population, not production presets or a new
independent population. The closing report records the review's conclusions
and unfinished peer checks.

Stage 4's original 12 calls and four old-case scientific repetitions had
already completed **before** the latest stop, for **USD 0.183834400** combined.
Those existing data are retained. No additional stage-4 experiment is started.

| Existing work | Evidence and boundary |
|---|---|
| [432-query study and Jev recipes](STUDY_AND_RECIPES.md) | Stage 1: 432 first attempts, USD 0.052296100. Twelve authored DEV families, four queries each; eight scored arms. Current Jev scores are 40/48 or 43/48; older 42/48 and 45/48 remain separate historical measurements. |
| [Measured recipes for workstream 1](stage1-measured-recipes-for-w1-v1.json) | Exact request, prompt, parameter and observed/requested identities; recipe data carry no native-extraction or production adoption. |
| [Native semantic variants](STAGE2_NATIVE_SEMANTICS.md) | Stage 2: 60 first attempts, USD 0.163908000. Forty-one invalid responses and 19 native rejections; 0/60 native acceptance. Semantic accuracy remains unmeasured, with no winner. |
| [Original extraction](EXTRACTION.md) | Original 6/60 reproduced. The separate historical decoder projection of 38/60 uses the same old responses and is not a measured model-quality gain. |
| [Frontier and answer formats](FOLLOWUP_FRONTIER_REPLY.md) | Original stage-3 and stage-4 protocols and transformation lineage; actual cost and closing status are in this report and accounting table. |
| [Measured method graph](followup-results/source-measured-graph-v1/README.md) | Existing dated ModelProfile metric vocabulary and concrete method/prompt/parameter versions, with `produced_by` provenance. Source-relative evidence remains qualified; no CABI or canonical-store write. |
| [Old-label repetitions](stage5/label-repetition-measured-v1/README.md) | 192 scientific first attempts, USD 0.007198800. `j_active`: 80/96; `j_directed`: 89/96. Same old DEV population; no holdout or semantic-adequacy score. |
| [Historical billing](BILLING.md) | Old-key USD 1.098135722 remains unassigned and requires old-key proof; fresh-key reconciliation cannot assign it. |

The old-label capsule's existing clean-extraction receipt records a byte-exact
packet rebuild, 105 preserved payloads and all 192 request hashes. Closing
read-only checks confirmed ZIP CRC, all payload hashes and sizes, the original
SCORING_INPUT and separate SCORE hashes, and gzip raw-packet hash and size.
The graph contains 384 overlapping event views of **192** physical attempts;
cost is counted once per attempt. Full private HTTP/GEN envelope bytes cannot
be reconstructed from this public projection; their hashes and declared loss
remain explicit. No exporter, model or semantic scorer was rerun for this
closing integrity check.

The **210 new-case calls remain unexecuted**: 192 label calls, 12 frontier
calls and six reply calls. Frozen synthetic inputs, reviewed source data,
preparations and recorded quotes are retained only as handoff data. The new
label scorer is unfinished. The frontier master reservation of
USD 4.161656950 exceeds the remaining provider capacity by USD 0.034873450;
its paired child partition and aggregate recovery are unfinished. Prices,
currency evidence and budget admission must be refreshed before any later
execution, and a new owner instruction must first supersede the stop.

Full rejected inputs, first responses and portable before/fix proofs remain
on `archive/gpt/model-research-runner-review-2026-10-05`; earlier frontier
drafts remain on `archive/gpt/model-research-prefreeze-2026-10-04`. See the
closing report for exact archive commits, capsule hashes and restoration
steps. Historical verification packages keep their original tested source
and base; the closing report identifies the fresh rebase gate separately.

Production method definitions and graph-store integration belong to
workstreams 1/3/4. This branch supplies research data and qualified claims;
it does not change production prompts, UI files, `STATE.md` or the root README.
