# Stage 5: repeat measured candidates, then evaluate new data

This is a preregistered preparation, not an executed evaluation. No corpus was
authored for stage 5; no unseen cases, unseen labels, `validation_cases` or
`eval/real-holdout-key` were read. There are no stage-5 winners or quality numbers.
The configuration is [`stage5_protocol.json`](stage5_protocol.json), version
`stage5-preregistration-2026-10-04-v2`.

The owner's sequence is retained: existing 432 requests → synthetic semantic
prompt/parameter study → small frontier study → graph reply versus text JSON →
repeat measured candidates and test independently authored new data. Each
executed stage needs a frozen plan and an allocation within the owner's
authorized budget. Preparation alone does not authorize paid execution or
require the owner to reconfirm a budget already authorized for this sequence.
The owner has authorized this sequence in the separate **5 EUR programme**.
There is no additional per-stage approval; the shared expected ×10 usage-growth
guard still applies when triggered. A valid new dedicated key, live prices and
exchange-rate evidence, cumulative programme accounting and frozen request
inputs remain execution prerequisites.

## Selection before new data

`loom.tools.structure.stage5_selection_v1` reads an explicitly supplied neutral
aggregate report, without following any source reference or loading cases,
labels, credentials or providers. It compares only configurations with identical
task, dataset hash, planned-query-set hash and rubric hash. It cannot compare
different DEV populations as though they were paired measurements.

The default policy retains all Pareto-nondominated measured configurations; ties
remain. There is no selection quota or hidden scalar quality/cost score. Its
five separate criteria are:

| Criterion | Numerator / denominator | Direction |
|---|---|---|
| Correctness | Correct usable first responses / all planned query slots | Higher |
| Semantic validity | Semantically adequate usable first responses / all planned query slots | Higher |
| Source grounding | Correctly grounded usable first responses / all planned query slots | Higher |
| Availability | Usable first responses / all planned query slots | Higher |
| Observed cost | All first-attempt charges / all planned query slots | Lower |

Semantic adequacy and source grounding require separately frozen rubrics. A
parseable graph or JSON object establishes neither. Grounding checks attribution,
support in the supplied source and the treatment of unsupported claims. Report
schema validity and transport diagnostics separately from semantic quality.
Failures and missing outputs remain in each planned denominator; billed failures
remain in cost. Unknown cost stays unknown. Provider-reported cost and an
invoice-reconciled amount retain distinct provenance.

Version 2 makes billing admission independent of the mathematical criterion
configuration. The explicit preset
`selection.billing_admission.require_complete_actual_billing: true` excludes
incomplete or unsupported cost evidence even for quality-only criteria or a
criterion with cost in its denominator. Any metric referring to `cost.*` in
either numerator or denominator remains unavailable until billing is complete.
An owner-selected false setting can admit quality-only research comparisons
while retaining unknown costs explicitly; it never manufactures a cost metric
or grants budget/execution authority. Missing settings preserve the true preset.
Changing a ratio's denominator does not change this admission policy.

Selection directions, criteria, source stages and optional eligibility thresholds
are data. The preset contains no numerical eligibility threshold. Changing a
policy, prompt, scorer or corpus creates a new version and freeze. Exact rational
comparison avoids silently collapsing close scores through decimal rounding.

Unmeasured configurations, scripted mechanism evidence, best-of/retry evidence,
missing required dimensions and unknown billing are reported as ineligible rather
than assigned invented zero measurements. `directed_refute_v3` currently has no
measured provider quality and therefore cannot be a winner. Historical Jev
42/48 and 45/48 establish the old supplied-candidate DEV task; they do not
establish measured grounding or semantic quality for a new task. The empty
432-study score likewise cannot supply candidates. No normalized result is
fabricated merely to make this preparation produce a winner.

## Neutral summary and offline command

The input schema is `loom.stage5.study_summary/1`, with a `records` array. Each
record supplies `configuration_id`, `source_stage`, `task_id`,
`dataset_sha256`, `planned_query_set_sha256`, `rubric_sha256`, `counts`, `cost`
and `evidence`. The protocol contains the precise field semantics.

`counts` must include nonnegative integer `planned`, `attempted_requests` and
`available`. Zero attempts exclude an arm even when a campaign-level quality
flag was accidentally copied to it. Split-call tasks may have more attempted
requests than planned query slots. Default
selection also needs `correct`, `semantically_valid` and `source_grounded`.
Each quality count must be at most `available`, which is at most both `planned`
and `attempted_requests`.
`cost` retains `total_usd`, `complete`, `unknown_attempts` and `evidence_kind`
(`provider_reported` or `invoice_reconciled`). `evidence` carries
`model_quality_measured`, `first_response_only`, `kind`, `source_report_sha256`
and explicit `dataset_inspection_status`. Permitted measured kinds are
`saved_provider_first_response` and `historical_provider_first_response_replay`.
No case text, reference answers or label-file pointers belong in this summary.
The top-level, record, evidence and cost objects reject undeclared fields instead
of copying arbitrary payloads into the selection output. Decimal numeric JSON
costs preserve their exact lexical values before rational comparison. A shared
planned-query-set group must retain the same planned count in every arm.

After independently auditing a completed source study and producing this summary:

```bash
python3 -m loom.tools.structure.stage5_selection_v1 \
  --protocol docs/research/model_research_2026-10-04/stage5_protocol.json \
  --summary /private/studies/neutral-summary.json \
  --output /private/studies/stage5-selection.json
```

Output is created exclusively, preserving a previous selection. It contains
every input configuration, metrics as exact rational strings, exclusion reasons,
dominating alternatives and selected IDs within each comparison group. It binds
the exact input/protocol file hashes, protocol version, selector source hash and
selection payload hash. The payload hash covers the canonical JSON object
without its own `selection_payload_sha256` field; it is not the output file hash.
The selector checks aggregate arithmetic, hash syntax and evidence declarations;
it does **not** independently certify raw scoring or billing. Retain manifests,
raw responses, scoring rubric/code and source reports for that audit.

## Repetitions and unseen status

Freeze selected IDs and the selection output before authors create the new corpus.
Repetitions, corpus size, languages, task families, model identities, parameters,
ordering, seed, output allowance and budget are configurable quantities. They
remain unset in this preparation because no measured stage-5 selection is
available. Allocate them within the already authorized programme and bind them
and their hashes before first collection.

Each planned repetition has a distinct preregistered attempt ID. A repeated
configuration is a planned replicate, not a replacement retry. Preserve the first
response, error or uncertain attempt under each ID. Never substitute a later,
better response; preserve all-planned denominators and every first-attempt
charge. Report both per-repeat results and pooled counts within each pinned
corpus. Keep old-population repeats and the newly authored corpus in separate
comparison groups. Paired before/after changes require the same query population.
Compute uncertainty at conversation-family level because related query slots
are correlated.

The new corpus's current status is **not created, not inspected**. Its future
author or custodian records lineage, overlap checks, manifest hashes and an
inspection log. New independently authored synthetic data can be unseen to the
selection process while visible to its author; that does not make it a blind
sealed holdout. A genuine blind external evaluation requires an independent
custodian and its own protocol. This stage does not open or reuse the existing
sealed holdout. Freeze labels outside model inputs, collect first responses,
then score. Once cases or labels are inspected or used to tune a method, change
their inspection status and cease describing them as unseen.

## Graph entities and verification

Protocol, configurations, comparison groups, selection result and corpus manifest
are versioned graph entities. Selection claims are `derived`/`system`, with the
source report, dataset, query-set and rubric hashes retained. The protocol's
`graph_export` block names the binding hashes. No automatic adoption is implied.

The 22 summary-only regressions pass: Pareto tradeoffs/ties, all-planned
denominators, separate semantics/grounding/availability, unknown billing, missing
dimensions, nonfinite values, exact arithmetic, comparison-group isolation,
unmeasured and scripted exclusions, retry exclusion, configurable thresholds,
exclusive output and reproducible content hashes. Their artificial aggregate
fixtures test the selector; they are not model measurements or an unseen corpus.
The independent review's custom quality/cost-denominator counterexample,
predecessor source bytes and before/after results are preserved separately in
the negative research review archive. This admission correction does not
replace a failed first response or alter any of the frozen 432 requests.

```bash
python3 -m unittest loom.tools.structure.test_stage5_selection_v1 -v
```
