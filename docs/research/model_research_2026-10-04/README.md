# Workstream 7 — model research

Updated 2026-10-05 after completed stages 1 and 2. Branch:
`gpt/model-research-2026-10-04`, rebased onto `main` `30ad7d3`.
Published increments and archived first failures survived the restart.
The owner authorized a separate **5 EUR** programme and supplied an encrypted
credential on 2026-10-05. Stage 1 completed **432 first attempts** for
**USD 0.052296100**; stage 2 completed **60** for **USD 0.1639080**. Completed
stages total **USD 0.216204100**; stage 3 is in progress. Original first responses,
stops and delayed-generation captures remain immutable, with GET-only billing
resolution and no repeated POST. The provider cap remains **USD 5**, within
the owner's 5 EUR authorization at the captured ECB rate. See
[the current budget and actual-cost boundary](BUDGET.md).

| Work | Evidence and boundary |
|---|---|
| [Billing](BILLING.md) | 992 historical ledger rows reduce to 631 unique attempts. The old $1.098135722 gap requires old-key proof; fresh-key reconciliation cannot assign it. |
| [432 paired requests and Jev recipes](STUDY_AND_RECIPES.md) | Complete first-response study on 12 authored DEV cases, four queries each: eight scored arms of 48 judgments, with 96 physical calls in split. Historical 42/48 and 45/48 remain separate; current Jev arms score 40/48 or 43/48. |
| [W1 measured recipe data](stage1-measured-recipes-for-w1-v1.json) | Eight task-scoped recipes with exact consumed bodies, parameters and observed/requested identities; no production or native-extraction adoption. |
| [Native semantic variants](STAGE2_NATIVE_SEMANTICS.md) | 60 completed first responses on fifteen synthetic DEV branches: 41 invalid responses and 19 native rejections, 0 mechanically accepted. Semantic accuracy remains unmeasured; no winner. |
| [Extraction](EXTRACTION.md) | Original 6/60 reproduced; separate historical decoder projection 38/60. No new model-quality improvement claimed. |
| [Original frontier mechanics](FRONTIER.md) | 36 scripted request/result events, not provider measurements; historical pricing duplicate is not another population. |
| [Smaller frontier and answer-format comparison](FOLLOWUP_FRONTIER_REPLY.md) | Stages 3 and 4 each prepare twelve calls, with declared transformation lineage and unchanged source bytes. |
| [Stage5 default and exploratory selection](stage5/stage1-exploratory-v1/README.md) | Default five-criterion protocol unchanged and stage1 ineligible. Separate post-stage1 three-criterion Pareto preset retains j_active/j_directed without backfilled dimensions, quotas or thresholds. No new corpus before all selection groups freeze. |
| [Method-graph claims](METHOD_GRAPH.md) | Concrete version/parameter/prompt identities, actual versus planned producers and dated evidence; existing ModelProfile metric vocabulary. |
| [Ordered spending plan](BUDGET.md) | 516 prepared calls before repeats; historical $3.1804544 is not a bill. Completed stages1–2 cost $0.216204100; stage3 upper projection $1.965986350 is not actual spend. |

Full rejected source/input/output proofs stay on
[the independent-review archive](https://github.com/klb-t/chatadhd/tree/f14a4035eaad896a9905439fc3a60e46c6b0f1ba/docs/research/model_research_2026-10-04).
The V3 ZIP includes seven reproductions and nested unchanged predecessors;
SHA-256 `ec7020cbdd32a69a2ab7c41e1874375882837541f534add8c6c3f7c67764639d`.
Earlier frontier drafts remain on
`archive/gpt/model-research-prefreeze-2026-10-04`. Negative results are preserved
and are not promoted as successful model research.

The complete compact stage-1 result snapshot was published at `43ed079` and
the additional stage-1 exploratory stage5 freeze at `cb647034`. The reduced
selection measures explicit source-commitment label agreement on inspected,
dependent DEV controls; it adds no semantic adequacy, rationale, reasoning
mechanism, source-evidence or world-truth score. Stage 2's failed native outputs
do not become semantic accuracy zero.

The runner's later portable before/fix proofs remain separate on
`archive/gpt/model-research-runner-review-2026-10-05`; original first attempts,
negative inputs and stop witnesses are retained. The unrecovered old V1 generic
source SHA still limits reproduction of that historical full orchestration;
the new frozen runner and current public projections do not retroactively
recover the old producer. Public functional code `1259d475` (adapter SHA-256 `f92da5c4…`) passed the latest
108/108 native gate; its immutable [verification package](verification/native-adapter-full-20261005/receipt.json) was published at `82e1d448`.

Production method contracts and graph-store integration belong to workstreams
3/4. This research exports observations and claims, not an assertion that a
production selector accepted or executed those methods. Default recipes are
handed to workstream 1 as data; no production prompts or UI files are changed.
Final verification and remaining handoffs are recorded in
`docs/reports/model-research-2026-10-04.md`.
