# 432-request study and Jev recipes: offline readiness, 2026-10-04

No provider calls, account lookups, credential reads or holdout reads were made.
This work improves the reproducibility and execution accounting of the study;
it does not demonstrate improved model analysis quality.

## First failure and preserved successor

The historical preparations did **not** verify against current main. The exact
first failures are saved in [`study/first_failures.json`](study/first_failures.json):
`frozen_analysis_drift` for the 432 preparation and `frozen_plan_drift` for W3.
Both refer to the same two changed source files:

- `loom/tools/structure/w3_directed_commitment_v1/experiment.py`;
- `loom/tools/structure/w3_directed_commitment_v1/test_experiment.py`.

The recovered historical-source binding supports the original commit identity
and a separately verified metadata-corrected tree. That recovery changed source
hashes after the old preparations froze them. Neither old freeze was amended.

[`study/successor_receipt.json`](study/successor_receipt.json) records each old
and current hash. A new preparation uses the current frozen dependencies and
saved endpoint snapshots only. It checks exact equality of all request records,
including IDs, body bytes after canonicalization and reservations:

| Check | Before | After |
|---|---:|---:|
| Original preparations passing current source verification | 0/2 | Still 0/2, preserved first failure |
| New separately frozen preparations passing verification | 0 | 2/2 |
| 432 study requests preserved exactly | 432 | 432/432 |
| Study arms with unchanged price evidence and retrieval dates | 9 | 9/9 |
| W3 prepared query/input records preserved exactly | 192 | 192/192 |
| New model calls | 0 | 0 |
| Preparation reading reference labels into model inputs | 0 | 0 |

Both DEV gold files retain equal byte hashes. Preparation hashes their bytes;
scoring loads their labels only after predictions are collected. These are
inspected synthetic DEV data, not a blind evaluation. The model-method samples
remain 12 authored bilingual conversation families with 48 correlated queries.

The ready files are [`study/prepared/`](study/prepared/) and
[`study/recipes/`](study/recipes/). Endpoint prices retain their actual October 2
retrieval dates. All nine are stale for new live execution on October 4;
[`study/readiness.json`](study/readiness.json) reports that fact. Generating a new
freeze is not a price refresh, owner authorization or restored credential.

## What the successor adds

`loom.tools.structure.model_study_readiness_v1` has three offline commands:

- `successor-prepare`: separately freeze current tools using saved snapshots,
  record source drift, prove request and reservation equality, retain old dates;
- `audit`: validate ledgers and response hashes, inventory attempted and untouched
  requests, preserve unknown-cost reservations and make one campaign-level
  decision about resume readiness;
- `score`: reuse the frozen ternary scorer and its per-language/family and paired
  contrasts, then attach first-attempt cost, token, latency and response hashes.

A started or uncertain attempt is never scheduled for retry. A stopped ledger,
unknown billing or stranded response file blocks continuation of the **whole
campaign**, including another arm. Untouched request IDs are still listed for
reconciliation. That list alone is not permission to send them. The tool never
modifies a run ledger or response, and output files are created exclusively.

Price age and future tolerance are explicit audit parameters, with 86,400 and
300 second historical presets. Existing frozen execution clients retain their
own supported policies. NaN, infinity, boolean clocks and invalid policy values
cannot produce a positive readiness result. Missing/invalid request latency is
reported as missing, never as a measured zero.

The score explicitly requires `saved_provider_replay` or `scripted_mechanism`.
Scripted envelopes always report `model_quality_measured: false`; saved real
responses are historical replay, with `new_model_quality_measured: false`.
Missing, invalid or conflicting outcomes retain all 48 planned query slots.
`j_split` joins both paid question calls into one decision and adds **both**
charges. Invalid semantic answers keep their billed cost. Unknown billing
retains the attempt's reservation and makes cost per correct usable query null.
Summed request latency is labelled separately from campaign wall time.

[`study/empty_run_score.json`](study/empty_run_score.json) is a negative control:
all eight scored arms have 0/48 available, zero attempts, no measured quality,
and no invented token or cost observations. The cost fields are observations
only when attempts exist; a zero total for an empty ledger is not a completed
study or an account-spend result.

## Jev historical replay and overlap

The existing verified W3 replay was executed offline again, preserving exact
first outputs from the immutable source archive. It verifies 23 historical
Git-bound sources and produces two `loom.model_profiles/1` records in
[`study/historical_replay/`](study/historical_replay/).

| Historical recipe | Correct / all planned | Reported response cost | New calls |
|---|---:|---:|---:|
| `historical_refute_v1` | 42/48 | $0.001482852 | 0 |
| `active_refute_v2` | 45/48 | $0.001664292 | 0 |
| `directed_refute_v3` | Unmeasured | Unexecuted | 0 |

Those first two measurements belong to September 30 and its original DEV
population. The replay does not transfer them to the independently authored
October study or to the new directed recipe. All 96 generation billing audits
were unavailable; response-reported amounts are not invoice-certified totals.
The profiles retain that limitation and do not enable automatic routing.

The 48 W3 `independent_dev/active_refute_v2` requests are identical to study arm
`j_active`; the 48 `independent_dev/directed_refute_v3` requests are identical to
`j_directed`. The audit detects all **96 duplicates by exact request-body hash**.
Execute either preparation, not both. The historical comparator is replayed;
the separate historical directed arm is a distinct optional future comparison,
not extra work silently appended to the 432 plan.

## Historical spending plan for owner approval

These are frozen October 2 allowances, **not current prices or predicted bills**.
No execution approval is granted by this document. Paid calls under this new
task require the owner's explicit consent and current account reconciliation.

| Group | Calls | Frozen allowance | Purpose |
|---|---:|---:|---|
| `j_active` | 48 | $0.048 | Unchanged Jev comparator on independent DEV |
| `j_directed` | 48 | $0.048 | Isolate direction/speaker/withdrawal criterion |
| `j_roles` | 48 | $0.048 | Source-preserving role representation |
| `j_split_q01` + `j_split_q02` | 96 | $0.096 | Isolate split-call composition; score 48 decisions |
| `g_brief_t0` | 48 | $0.0753676 | Brief GPT semantic contract at temperature 0 |
| `g_brief_t03` | 48 | $0.0754060 | Temperature-only comparison |
| `g_rules_t0` | 48 | $0.0865228 | Explicit structural/commitment rules at temperature 0 |
| `g_rules_t03` | 48 | $0.0865612 | Same rule contrast at temperature 0.3 |
| Complete study | 432 | **$0.5638576** | Seven paired contrasts, eight scored arms |
| Historical unknown-charge reserve | — | **$0.00738793** | Preserve unresolved historical liability |
| Minimum current allowance before a fresh complete plan | — | **$0.57124553** | Full plan plus historical uncertainty |

The historical dedicated programme cap remains $2, non-resetting. Its old key
metadata is not a current balance. After any attempts, the audit replaces the
full-plan amount with untouched-request reservations plus current uncertain
attempt reservations plus the historical allowance. Fresh account reconciliation
must check overlaps with that historical allowance rather than treat a local
sum as provider accounting. Do not resume paid work if the $1.098135722
unassigned account-spend difference is unresolved.

Adjacent arm execution should begin with `j_active` then `j_directed`; use
one frozen campaign directory and one shared account ledger. Keep unchanged
counterparts adjacent where possible and retain actual request timing/order.
The four GPT arms provide both prompt and temperature contrasts; a partial run
cannot be labelled the complete factorial study. Additional stability repeats,
models, recipes and output allowances are configurable future preparations with
their own freeze and budget, never replacements for failed first responses.

## Owner run instructions

From the repository root, offline verification and a read-only resume audit:

```bash
python3 -m loom.tools.structure.analysis_optimization_v1 verify \
  --prepared docs/research/model_research_2026-10-04/study/prepared

python3 -m loom.tools.structure.w3_directed_commitment_v1.experiment verify \
  --plan docs/research/model_research_2026-10-04/study/recipes

python3 -m loom.tools.structure.model_study_readiness_v1 audit \
  --prepared docs/research/model_research_2026-10-04/study/prepared \
  --recipes docs/research/model_research_2026-10-04/study/recipes \
  --runs /private/analysis-runs --output /private/study-readiness.json
```

For paid execution, first obtain current permission, a valid dedicated credential,
fresh account identity/usage and endpoint evidence, and resolve any campaign
stop. Do not run a stale successor manifest. Save fresh endpoint GET responses
and retrieval metadata into a **new private preparation**; preserve these
published historical files and first failures. Compare new request bodies with
the frozen counterparts and record any intentional change. Only then use the
existing immutable-attempt clients: `jev_live_pilot run --manifest ... --run-dir
...` for Jev, or `openrouter_runner run <manifest> --run-dir ...` for GPT. There
is deliberately no live-call command in the readiness tool.

After actual first responses are retained:

```bash
python3 -m loom.tools.structure.model_study_readiness_v1 score \
  --prepared /private/fresh-study/prepared --runs /private/analysis-runs \
  --evidence-class saved_provider_replay --output /private/study-score.json
```

For mechanism fixtures select `--evidence-class scripted_mechanism`. Keep every
raw probability/envelope, including failures. Never publish private archive
content or credential/account identifiers with this public synthetic study.

## Verification

The 14 new offline regressions pass. They cover preserved source drift and
request equality, refused payload alteration, 96-request duplicate detection,
configured freshness, nonfinite inputs, write-ahead uncertainty, campaign-wide
stops, stranded responses, provider and billing mismatch, invalid-answer billing,
split-call accounting, unknown charges and exclusive first-score persistence.
The existing paired-study and W3 suites also pass: 36/36 regressions, including
historical archive/member binding, full denominators and profile consumers.
The root lane records the full CTest gate separately. No thresholds, existing
tests or frozen historical tools were changed.
