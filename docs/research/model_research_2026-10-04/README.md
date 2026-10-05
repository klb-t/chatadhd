# Workstream 7 — model research

Updated 2026-10-05 after a runtime restart. Branch:
`gpt/model-research-2026-10-04`, rebased onto `main` `30ad7d3`.
Published increments and archived first failures survived the restart.
The owner authorized a separate **5 EUR** programme and supplied an encrypted
credential on 2026-10-05. Two Jev responses have matching generation charges
totalling **USD 0.000070014**. Their first receipts survive stops caused by
delayed account/generation metadata; reconciliation uses only GET. The full
432-request stage and later stages are still in progress. See
[the current budget and actual-cost boundary](BUDGET.md).

| Work | Evidence and boundary |
|---|---|
| [Billing](BILLING.md) | 992 ledger rows reduce to 631 unique attempts; exact known-cost evidence reconciles. The old $1.098135722 gap remains unassigned. |
| [432 paired requests and Jev recipes](STUDY_AND_RECIPES.md) | Frozen bytes restored exactly; nine execution arms/eight scored combinations. Historical 42/48 and 45/48 are replayed with original profiles; v3 is unmeasured. |
| [Native semantic variants](STAGE2_NATIVE_SEMANTICS.md) | 60 requests on fifteen branches of the actual synthetic DEV archives. Native validation and semantic interpretation are distinct dimensions. |
| [Extraction](EXTRACTION.md) | Original 6/60 reproduced; separate historical decoder projection 38/60. No new model-quality improvement claimed. |
| [Original frontier mechanics](FRONTIER.md) | 36 scripted request/result events, not provider measurements; historical pricing duplicate is not another population. |
| [Smaller frontier and answer-format comparison](FOLLOWUP_FRONTIER_REPLY.md) | Stages 3 and 4 each prepare twelve calls, with declared transformation lineage and unchanged source bytes. |
| [Winner repeats and unseen protocol](STAGE5.md) | Cost-aware data-defined selection; incomplete billing or unmeasured quality cannot become a winner. |
| [Method-graph claims](METHOD_GRAPH.md) | Concrete version/parameter/prompt identities, actual versus planned producers and dated evidence; existing ModelProfile metric vocabulary. |
| [Ordered spending plan](BUDGET.md) | 516 prepared calls before winner repeats, historical $3.1804544; current quotes and FX required before execution. |

Full rejected source/input/output proofs stay on
[the independent-review archive](https://github.com/klb-t/chatadhd/tree/f14a4035eaad896a9905439fc3a60e46c6b0f1ba/docs/research/model_research_2026-10-04).
The V3 ZIP includes seven reproductions and nested unchanged predecessors;
SHA-256 `ec7020cbdd32a69a2ab7c41e1874375882837541f534add8c6c3f7c67764639d`.
Earlier frontier drafts remain on
`archive/gpt/model-research-prefreeze-2026-10-04`. Negative results are preserved
and are not promoted as successful model research.

Production method contracts and graph-store integration belong to workstreams
3/4. This research exports observations and claims, not an assertion that a
production selector accepted or executed those methods. Default recipes are
handed to workstream 1 as data; no production prompts or UI files are changed.
Final verification and remaining handoffs are recorded in
`docs/reports/model-research-2026-10-04.md`.
