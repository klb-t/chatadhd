# Frozen native-prompt versus coordinate-aided comparison

Registered while the first corrected live baseline is running, before inspecting
its model responses. This is a small diagnostic comparison, not a model ranking.

## Arms and data

- `native_v1`: the exact current native occurrence-graph extraction prompt.
- `native_anchors_v1`: the already implemented alternative adds mechanically
  computed UTF-8 token spans plus generic instructions about argument roles,
  negation, quotation and unresolved scope. It does not supply interpretation,
  family names, gold structures or gold anchors.
- Models: GPT-4.1-mini on `openai` and Qwen3-30B-A3B-Instruct-2507 on
  `siliconflow/fp8`, temperature zero, JSON-object responses, 8192 output tokens.
- Frozen corpus: `live_structure_pilot_v1`, 16 development and 16 validation
  cases. Whole bilingual construction families stay together across splits.
  Validation labels never enter request preparation.

First run the native development arm (`live-structure-dev-v2`, 32 planned
requests). If its billing/transport path is usable, run the coordinate-aided
development arm (`live-structure-anchors-dev-v1`, 32 planned requests). Freeze
both methods unchanged, then compare both on validation together
(`live-structure-validation-v1`, 64 planned requests). This document does not
activate another run by itself. Each activation records its own immutable
manifest and fresh prices; no failed/uncertain request is automatically retried.

Current public-price preparation reserves USD 0.29711246 for the first arm and
USD 0.30666109 for the second. Validation must pass a fresh aggregate reservation
check against the dedicated key's remaining balance. The authorization is USD 2
for the dedicated key overall, not USD 2 per arm. Preserve all earlier ledgers.

## Interpretation

Report planned/attempted/transport-complete/valid-envelope/source-valid counts
separately. Use the existing frozen scorer's structural and anchored-semantic
exactness with every planned case in the denominator. Report abstentions and
unknown results explicitly. An invalid graph, incorrect UTF-8 anchor or missing
response is not silently repaired into a successful answer.

Compare models and methods separately in PL and EN, then inspect matched case
differences. Record actual reported credit cost, token counts and latency from
retained responses. A cheap invalid response is not a useful extraction. If BYOK
is observed, credit cost is not total provider spend and the run must stop.

Use validation once, with no post-result prompt changes. Sixteen cases and
correlated translations do not support a universal winner or statistical
precision claims. Exact-tree/source-anchor scoring tests this particular
representation contract; it is not a complete logical-equivalence judge.
The native and coordinate-aided methods are still single-call extraction, not
the future staged low-cost semantic-analysis pipeline.

No result is automatically promoted to the user's authoritative graph.
