# Express: prompts, parameters, and user preferences

Status: **frozen offline preparation, 0 paid calls, no actual results read**. This is research data for workstream 7. It does not change application defaults or core code.

| Factor | Frozen variants |
|---|---|
| Prompt strategy | Literal evidence; chronological commitment ledger |
| Temperature | 0; 0.7 |
| Maximum output tokens | 256; 1024 |
| User preference | Concise; full trace and source-grounded counterarguments |
| Cases | First Polish family `new_label_001`; first English family `new_label_013`; all four queries each |

The full crossing is **16 configurations × 8 queries = 128 distinct operations**. Every configuration sees the identical eight source/query texts. Model `openai/gpt-4.1-mini`, provider `openai`, fallbacks disabled. The JSON response fields remain identical in every arm. The short output allocation is an experimental variant: truncation is measured separately and no response is silently repaired or replaced. These are author-visible synthetic DEV families, not a blind evaluation or a population sample.

`design.json` contains every study choice and prompt instruction. `prepare.py` performs generic materialization without API calls. `prepared/configurations.json` contains complete versioned prompts and prompt hashes. `prepared/crosswalk.json` records the full deterministic cell/query order. `prepared/manifest.json` binds exact request body files with SHA-256, source hashes, configuration identities, and conservative billing unit bounds. Gold labels and rationales are absent from the model prompts. Metadata contains the gold artifact hash for later verification.

The historical planning estimate is **0.3580992 USD**, using 567,568 upper-bound prompt units and 81,920 completion units at 0.4/1.6 USD per million. This is not a fresh quote or actual expenditure. The root payer must refresh endpoint metadata/pricing, evaluate cumulative available balance and the usage policy, and record the plan before dispatch. The programme remains `thread7-new-key-2026-10-04-eur5` with the existing cumulative ledger and actual 5 USD key cap.

## Handoff to conversation 2

1. Read this file, `design.json`, `SOURCE_FREEZE.json`, and the workstream 7 report. Obtain the existing immutable source/gold files from the workstream branch or `stage5/UNEXECUTED_PREPARATIONS_20261005.zip`, preserving the repository paths. Do not inspect holdout data or earlier actual model outputs while changing this design.
2. Verify the source, gold, prepared manifest, producer, and request hashes. Run `python test_prepare.py` and the existing `research_programme_manifest.py verify prepared/manifest.json`. Seven offline checks already pass, including corruption rejection, full crossing, source identity, absence of gold rationales in prompts, conservative bounds, and exact replay.
3. Keep **one payer: root, using the shared cumulative ledger**. This conversation prepares offline scoring and method claims; it must not independently send these operations, copy/reset the ledger, retry IDs, or use the same key concurrently. The owner requested separate conversations; no separate key or independent programme budget has been assigned here.
4. Prepare a strict first-response scorer before collection. Primary criterion is exact equality with the eight immutable gold labels. Record availability, valid JSON, exact schema, `finish_reason=length`, source quote grounding, explanation character count, evidence turn count, counterargument count, and cost independently. Duplicate keys, nonfinite numbers, extra JSON fields, fences, and malformed JSON are failures rather than repair opportunities. Denominator is all eight declared queries per cell; do not exclude invalid/truncated responses to improve the headline score.
5. Preference adherence has observable measures (length, coverage, counterargument presence) without invented universal limits. Report each preference separately and parameter interactions; a longer answer alone is not proof of better adherence. Do not select a single scalar winner from an arbitrary verbosity threshold.
6. After root dispatches and reconciles the first responses, store results as dated claims about exact configured method versions, with prompt hash, parameters, source/billing evidence, and `produced_by` edges in the shared method graph format. Retain all negative outputs in the research archive. No production preset is accepted from eight known DEV queries alone.

No paid calls were made by this preparation task. No secrets, private conversations, or actual provider response envelopes are contained here.
