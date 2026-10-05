# Stage 3 manual source-relative neutral summary

This is a derivation of the frozen manual review and its six candidate aggregates
for the existing `loom.stage5.study_summary/1` contract. It does not rescore
responses, run selection, create a new corpus or establish production readiness.
The original review, independent peer receipt, normalized captures and mechanical
report remain unchanged and are bound by exact file hashes in `mapping.json`.

`study-summary.json` has exactly six neutral records. Completion and pattern
discovery have separate task IDs identifying **manual source-relative research on
known authored synthetic DEV**. Each candidate retains both planned cases. Their
comparison identities use the same canonical supplied-input inventory, and each
task uses ordered message/query and preregistered rubric inventories shared by all
three models. Model, provider and request-body hashes are not comparison-group
hashes. `comparison-inventories.json` binds each record to its original cases,
method/recipe/observed identity, physical operations and generation receipts.

The unchanged five-axis protocol reads correctness, semantic adequacy, grounding,
availability and actual cost. A quality count is present only when the frozen
`measured_quality_value` is non-null. Therefore GPT completion omits `correct` and
`semantically_valid`, and Sonnet completion omits all three quality count fields.
Qualified failures remain explicit zero pass counts. No unknown value is imputed.

`counts.available` is original manual evaluability: **8/12** first contents were
interpretable for manual review. It does not resolve uncertain qualifiers or mean
that a packet compiled. Mechanical validity remains **0/12**, with no native C++
execution; all candidates remain unready for automatic production graph adoption.
All **12** unique credit-billed first attempts are included, including four
truncated outputs and every mechanically invalid output: **$0.3170690**. The cost
evidence is provider-reported public capture, not an independently authenticated
invoice or an estimate.

The replay is a generic projection engine. All source paths/hashes, JSON pointers,
field mapping, omission tests, comparison groups and arithmetic assertions are
caller DATA in `mapping.json`; there is no stage, case, model or semantic scorer
logic in `replay.py`. It reads only the seven explicitly bound public files,
checks their bytes, verifies 255 DATA assertions, and writes deterministic outputs
exclusively. The derivation receipt binds its own source, mapping, input and output
hashes. Replay into a new directory from repository root:

```sh
python docs/research/model_research_2026-10-04/followup-results/stage3-neutral-summary-v1/replay.py \
  --mapping docs/research/model_research_2026-10-04/followup-results/stage3-neutral-summary-v1/mapping.json \
  --repository-root . \
  --output-dir /tmp/stage3-neutral-reproduction
```

An independent author replay reproduced the summary, inventories and receipt
byte-for-byte. The existing selector's record validator accepted **6/6** records;
its selection function was not called. `verification.json` preserves **15/15**
checks, including fabricated null/zero omission, duplicate-operation, changed
query, exact decimal sum, cyclic projection, duplicate JSON, nonfinite JSON,
source-byte drift and output-overwrite cases. These verify the derivation rather
than add model-quality judgments.

Selection and any subsequent protocol freeze belong to the root workflow. No
winner or repeat population is asserted here.
