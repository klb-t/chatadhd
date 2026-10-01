# Research and cost audit for the Claude handoff — 2026-10-01

Audited source: `gpt/night-development-2026-10-01` at
`33fb30a5e8c226b64e67ee6531304046392b6ad2` (use the exact branch snapshot in the
machine-readable inventory if a ref subsequently moves). Cost discovery also
covered **36 actual non-sealed remote branch tips**. No key, private conversation,
sealed validation or `eval/real-holdout-key` was opened. This audit made **zero
provider calls**.

**The saved campaign arithmetic supports $0.133931438, rounded to $0.134.
It does not support claiming that this was the complete key/account spend.**
The useful research result is a separation of source preservation, candidate
retrieval, typed source assertions and explicit acceptance. Free graph extraction
from conversations is not yet demonstrated as reliable. BM25's 52/60 measures
evidence retrieval; Jev's promising context result measures selection among
supplied candidates. Neither is a general graph-quality score.

## Costs: reconstructed, deduplicated and bounded

The [offline auditor](research/handoff_2026-10-01/audit_saved_costs.py) scanned
saved `ledger.json` files and their ZIP copies, inspected referenced response
envelopes and compared SHA-256 and reported costs. It counts an attempt once by
experiment ID + request hash + actual start timestamp. Identical requests sent
at different times remain different attempts. Scripted test ledgers, including
the artificial $2,000,000 configurability test, are excluded.

Scope correction: the first audit counted 37 local `origin` tracking refs.
`origin/codex/recovery-audit-20261001` was a clone-local alias, not an actual
remote branch. Recalculation against the saved `git ls-remote --heads`
inventory covers 36 non-sealed remote branches and produces exactly the
same attempts, costs and ledger receipts. The corrected JSON retains the
first audit digest, initial counts and correction history. Historical
research links below pin the original source commit so archival cleanup
on the active branch does not break the evidence trail.

The [complete inventory](research/handoff_2026-10-01/saved_cost_audit.json)
records every audited tip, container/ledger hash, unique attempt, source copy,
response model and cost. **992 saved ledger records reduce to 631 attempts**.
All **629** available response hashes verify; all **628** available response
usage costs match their ledger. The three other attempts have unknown cost,
not certified zero cost. No `ledger.json` path outside `docs/research/` was
found in these non-sealed tips. This is a saved-artifact audit, not a provider
invoice export or exhaustive proof about calls whose receipts were lost.

| Accounting scope | Attempts | Known usage-reported USD | Unknown-cost attempts / reservation USD |
|---|---:|---:|---:|
| 2026-09-30 campaign | 497 | 0.089848266 | 1 / 0.001366 |
| Older 2026-09-28 saved trials | 134 | 0.033053722 | 2 / 0.00602193 |
| All unique saved attempts | 631 | 0.122901988 | 3 / 0.00738793 |

The campaign total cited in the handoff is **opening key usage 0.044083172 +
campaign 0.089848266 = 0.133931438**. The opening amount is a saved key-balance
observation, not the sum of the recovered older ledgers. Those older ledgers
explain 0.033053722 of it, leaving **0.011029450 not individually reconciled**.
Do not add the older 0.033053722 to 0.133931438: it belongs inside the opening
balance. Likewise, old uncertain reservations may already be reflected in that
balance and are not new incremental charges.

More significantly,
[PAID_RESUME_ALLOCATION_2026-09-30.json](https://github.com/klb-t/chatadhd/blob/33fb30a5e8c226b64e67ee6531304046392b6ad2/docs/research/PAID_RESUME_ALLOCATION_2026-09-30.json)
records a later successful provider metadata check at **2026-09-30 14:24:07 UTC**:
limit **$2**, non-resetting; usage **$1.23206716**; BYOK usage **$0**. The document
explicitly leaves the **$1.098135722** difference from 0.133931438 unassigned and
says the restored key's identity against the lost credential was not independently
reverified. This is historical metadata, not the present balance. It prevents
certifying either "$0.134 total key usage" or "no other paid usage".

| Campaign batch | Attempts | Known reported USD |
|---|---:|---:|
| GPT-4.1-mini structure pairs | 48 | 0.011127600 |
| Jev context | 48 | 0.005774496 |
| Jev T3 recipe comparison | 32 | 0.000950964 |
| Jev recipe key control | 8 | 0.000249312 |
| Assisted graph extraction | 24 | 0.018883200 |
| GPT supplied-edge v2, both batches | 96 | 0.019623200 |
| Jev supplied-edge, both batches | 96 | 0.002989266 |
| GPT supplied-edge v1 HTTP 400 | 1 | Unknown; retains 0.001366 reservation |
| Historical/active source commitment recipes | 96 | 0.003147144 |
| Free source graph extraction, both batches | 24 | 0.026351200 |
| Question packing probe | 24 | 0.000751884 |
| **Campaign total** | **497** | **0.089848266 + one unknown** |

Returned models in the campaign are **192 GPT-4.1-mini**, **304
typesafe/jev-1.13-20260917**, plus the one HTTP 400 with no returned model
(request: GPT-4.1-mini). The older trials contain **11 GPT-4.1-mini**, **112 Jev**,
**9 Qwen3-30B-A3B-Instruct-2507** responses and **2 uncertain Qwen requests**.
Consequently other paid model trials did exist, including Qwen; they precede
the campaign and are not additional spend to add on top of its opening balance.
**No frontier-model invocation was found in the documented trials.** This
finding cannot classify the unassigned balance difference or absent receipts.
Names such as `frontier_panel` in offline test code are not paid frontier calls.

The allocation file also reserves **$0.4023704 for 107 proposed requests**.
Its creation record says zero resumed attempts. No additional live ledger for
those allocations exists in the audited tips. A reservation is not spending,
and missing launch receipts are not evidence either of execution or of zero
execution. Reconcile current usage and outstanding reservations before another
live batch; the budget does not restart with a conversation.

## What the graph experiments actually establish

| Stage or intervention | Result | Meaning and remaining boundary |
|---|---|---|
| Supplied-node source extraction, first compiler | 56 TP / 6 FP / 4 FN | Precision 56/62 and recall 56/60 on 24 authored DEV conversations; supplied node inventory assists the task. |
| Same responses, corrected turn-reference binding | 60 TP / 2 FP / 0 FN | Recovers four copied-quote binding failures without new inference. A correct locator does not certify semantic support. |
| Graph → direct lookup | 88/96 → 92/96 after binding correction → 95/96 with individual-source commitment policy | Useful, separately preserved DEV interventions. Extraction saw full conversations; later time filtering is retrospective, not a causal prefix experiment. |
| Status-event extraction, original strict convention | 0 TP / 9 FP / 6 FN | Some supersession-target convention ambiguity exists, but cross-speaker authority errors are real. A downstream 95/96 does not replace this score. |
| Free node + edge extraction | 6 TP / 43 FP / 54 FN under frozen strict edge alignment | 6/49 precision, 6/60 recall; only 18/24 compiler-available graphs, although 22/24 raw graphs are complete. This is partly contract/alignment failure, not a clean estimate of semantic extraction skill. |

Sources: [first graph panel](https://github.com/klb-t/chatadhd/blob/33fb30a5e8c226b64e67ee6531304046392b6ad2/docs/research/graph_method_panel_v1/FIRST_RESULTS.md),
[binding ablation](https://github.com/klb-t/chatadhd/blob/33fb30a5e8c226b64e67ee6531304046392b6ad2/docs/research/graph_method_panel_v1/DIRECT_BINDING_ABLATION_RESULTS.md),
[source commitment policy](https://github.com/klb-t/chatadhd/blob/33fb30a5e8c226b64e67ee6531304046392b6ad2/docs/research/graph_method_panel_v1/SOURCE_COMMITMENT_PROJECTION_RESULTS.md),
free extraction [batch 1 score](https://github.com/klb-t/chatadhd/blob/33fb30a5e8c226b64e67ee6531304046392b6ad2/docs/research/graph_free_extraction_v1/prepared/batch01/first_score/score_first.json)
and [batch 2 score](https://github.com/klb-t/chatadhd/blob/33fb30a5e8c226b64e67ee6531304046392b6ad2/docs/research/graph_free_extraction_v1/prepared/batch02/first_score/score_first.json).

The free-extraction [secondary manual audit](https://github.com/klb-t/chatadhd/blob/33fb30a5e8c226b64e67ee6531304046392b6ad2/docs/research/graph_free_extraction_v1/manual_source_audit2.json)
reviews 62 raw assertions: 38 source-valid, 10 source-invalid and 14 needing
review. All 62 cited source/time bindings validate. These are post-outcome
judgments by a fixture author, not a blind replacement for the first score.
Nineteen node expressions were source-supported but outside the fixed reference
inventory; unmatched nodes cannot automatically be called hallucinations.

The practical graph should retain speaker/attribution, signed proposition
versus relation polarity, source spans, source time, model observation time,
revision history and alternative status policies. Nonendorsement is not denial;
silence is not refutation; a different person's statement does not automatically
retract an earlier speaker's commitment. Candidate retrieval must not silently
promote an edge or confer factual truth.

## Selector: BM25 and Jev answer different questions

**BM25 structured with b=0 finds explicit evidence at rank one in 52/60 eligible
queries**, on 24 inspected authored DEV conversations / 96 queries / 252 eligible
query-turn candidates. Its corresponding span recall is **52/66**, and selected
turn precision is **52/96**. The 36 unknown queries remain in the precision
denominator. Pools have at most three turns, so top-three completion is a
mechanical ceiling. Original MiniLM gets 46/60; the structured cross-encoder
gets 51/60. Their disagreements are useful, but 52 versus 51 is not evidence
of a universal BM25 advantage. See the independently recounted
[cross-encoder audit](https://github.com/klb-t/chatadhd/blob/33fb30a5e8c226b64e67ee6531304046392b6ad2/docs/research/retrieval_exploration_v1/INDEPENDENT_CROSS_ENCODER_AUDIT.md).

**Jev is promising as a conditional selector of supplied topics and old Claims**:
topic TP36/FP1/FN0/TN47; old-Claim TP16/FP2/FN0/TN26. Joint exact selection is
45/48. Half the queries contain no old-Claim candidate; the candidate-bearing
exact-set result is 22/24. Its two Claim false positives retain obsolete SQLite
context after a PostgreSQL correction. At the predeclared .2/.8 selective band,
zero retained Claim errors comes with retaining only **3/16 positive Claims**.
Those numbers do not establish calibration or useful high-recall abstention.
See [Jev context results](https://github.com/klb-t/chatadhd/blob/33fb30a5e8c226b64e67ee6531304046392b6ad2/docs/research/model_method_panel_v1/jev_context/FIRST_RESULTS.md).

BM25 evidence ranking and Jev candidate usefulness were measured on different
tasks; their fractions cannot be used as a head-to-head league table. Keep BM25
as a cheap retrieval comparator and Jev as an optional, separately evaluated
selector. A fair comparison needs the same candidate pool, temporal prefix,
selection budget and independently judged target.

Jev's separate graph judge scored 86/96, including six unavailable contradictory
answers; GPT's ternary judge scored 81/96. Jev's correction-refutation recall was
only 1/6. The historical → active-refutation recipe improved 42/48 → 45/48 on
another DEV panel, but a repeated packing probe crossed the decision threshold
on one case. Saved first results remain valid observations, not stable production
accuracy estimates.

## Completed analysis, unfinished experiments and archival disposition

This handoff completes a fresh cost/response reconstruction and a consolidated
analysis of the saved graph and selector outcomes. Five independent fixture
tests check copied-branch/ZIP deduplication, repeated real attempts, unknown
reservations, scripted-cost exclusion, conflicting copies sealed-ref denial and actual-remote versus local-alias scope.
They pass. No model experiment was replayed against a live provider.

[W3 directed_refute_v3](https://github.com/klb-t/chatadhd/blob/33fb30a5e8c226b64e67ee6531304046392b6ad2/docs/research/w3_directed_commitment_v1/README.md) remains
**prepared, not measured**. Historical W3 replay is already complete; it must not
be described as new model calls or a new quality gain. Schema-hint, semantic-axis,
explicit-role and agent-review preparations likewise do not establish model
quality from scripted responses. Re-running already completed offline quality
replays would not repair the missing accounting for later proposed live work.

Preserve all first failures and negatives, with their exact source/dependency
revision and raw bytes, in archival branches/archives. They need not remain
prominent on the active branch. Package decisions:

| Package | Active value to retain | Archive disposition |
|---|---|---|
| `graph_method_panel_v1` | Citation binding, optional source-commitment policy, typed lookup, useful bounded results | Original failed transport, failed first harnesses and superseded raw projections remain recoverable with their freezes. |
| `graph_free_extraction_v1` | Input/compiler contracts and fixtures needed by active tests; this concise diagnosis | Archive the failed free-extraction run and manual diagnostic artifacts together, with first raw responses, manifests, code closure and history. Do not present it as a successful extraction pipeline. |
| `retrieval_exploration_v1` | Reproducible method definitions, evidence selectors and bounded positive comparison | Preserve whole first measurement archives, including method losses and first audit failure; do not retain only winning rows as evidence. |
| `source_view_packing_probe_2026-09-30` | The warning that packing effects are unresolved and repeated bodies varied | Archive full 24-observation probe; no identified causal packing gain. |
| `w3_directed_commitment_v1` | Current `prepared_final/`, data recipe, historical comparator and replay fixtures | Archive superseded `prepared/`; never count either preparation as executed. |
| `philosophy_watch_2026-09-30`, `analysis_plan_independent_audit_v1` | Accepted corrections and necessary counterexample fixtures | Archive superseded investigations, raw failure logs and duplicated evidence bundles with exact source versions. |
| `recipe_experiments_2026-09-30`, `agentic_graph_cache_v1` | Accepted mechanisms, active scripts and fixture closure | Archive superseded logs/results individually; directory deletion breaks active imports. |

Known dependencies include
`test_recipe_scripted_transport.py` →
`docs/research/recipe_experiments_2026-09-30/scripted_transport_v1.py`, and
`agentic_graph_cache_v1/test_native_history.py` →
`docs/research/agentic_graph_cache_v1/native_history_repaired_fixture_results.json`.
Other replay/scoring code binds exact docs manifests, inputs, ZIPs and hashes.
Archive placement must follow the full runtime-read fixture closure, not only
text references in test files. A mixed positive/negative study cannot be reduced
to selected successful observations and still substantiate its measurements.

## Recommendation to the incoming lead

Claude owns the next integration decisions. I would first finish one inspectable
conversation → candidate graph → correction → accepted task → selected context
flow, with the original source and withheld counterarguments visible. This
tests whether the components form a usable product before producing more panels.
Then freeze a small same-input comparison of BM25 retrieval, Jev selection and
typed graph lookup on independently adjudicated episodes. Keep causal prefix
extraction separate from retrospective full-conversation projection.

The next graph experiment should isolate contract compliance from semantic
correctness, attribution and revision handling. It should compare repaired
recipes against preserved first responses and use new cases for confirmation;
further tuning on the same 24 DEV conversations has little independent evidence
value. Use authorized source episodes privately when they become available;
no private conversation bytes belong in this public repository. Reconcile
account usage before any new paid attempt. No auxiliary GPT branch is claimed
by this audit after its commit is integrated.

## Reproduce the accounting without credentials

From a clone containing the recorded refs, use a **new output path**:

```sh
python3 docs/research/handoff_2026-10-01/audit_saved_costs.py --remote-heads docs/research/handoff_2026-10-01/remote_heads_2026-10-01.tsv --output /tmp/saved-cost-audit-new.json
python3 -B -m unittest discover -s docs/research/handoff_2026-10-01 -p 'test_*.py' -v
```

The command uses the [saved actual remote heads](research/handoff_2026-10-01/remote_heads_2026-10-01.tsv), excluding the sealed branch. Without `--remote-heads`, the default scans local non-sealed `origin` tracking refs; local aliases are not proof of actual remote branches. For exact historical replay,
pass each `snapshots[].commit` from the saved JSON as a repeated `--ref` argument.
The report retains branch identities separately; data can be reproduced from
commit IDs even after refs move. Source/response bytes are never overwritten.
