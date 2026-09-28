# Same-task structure-pair comparator, frozen 2026-09-28

This is a prepared experiment, not a measured model-quality result. Compare the
48-question Jev pair pilot with OpenAI GPT-4.1-mini answering the same binary
classification task. Graph serialization is not part of either condition. This
comparison can distinguish task difficulty from the native source-to-graph
pilot's output-contract failures; it cannot establish production performance.
The pair corpus reuses development texts and is not an unseen holdout.

## Freeze boundary

The implementing agent read only the frozen `inputs.json`, runner code and
protocol documentation. It did not read the pair gold, observed Jev answers or
pair evaluation output before freezing this manifest and prompt. The only live
network operation during preparation was an unauthenticated public endpoint
catalog GET. No credential was accessed and no inference request was made.

Every user message contains the original `state.text` and complete `questions`
object encoded in a JSON wrapper. Decoding that wrapper recovers the exact source
strings, q01 instructions, criteria and noul type. Case IDs, language, family,
variant, split and labels do not enter the model message. The only additional
instruction is this fixed system message:

> Answer the supplied q01 using its instructions and true/false criteria applied to state.text. Return only one JSON object with exactly one key, q01, whose value is a JSON boolean: {"q01":true} or {"q01":false}. Do not include explanation, confidence, or Markdown.

The request order is the frozen input order. There is no selection based on Jev
confidence, correctness, or output. One first response per case, no automatic
retry, repair, majority vote or few-shot examples. Model settings are
`openai/gpt-4.1-mini`, pinned provider `openai`, no fallback, temperature 0,
max_tokens 128, JSON-object response format and usage accounting enabled.

## Cost and provenance

Public endpoint evidence retrieved at 2026-09-28T17:18:25.917612+00:00 from
<https://openrouter.ai/api/v1/models/openai/gpt-4.1-mini/endpoints>: provider OpenAI
status 0; prompt USD 0.40 and completion USD 1.60 per million tokens; required
parameters supported. The unchanged bounded runner plans USD **0.0912696** total
reservation for 48 requests, below the USD 0.10 batch ceiling. The dedicated key
cap remains USD 2 overall. This reservation is conservative accounting, not a
formal billing guarantee. The existing nonresetting-credit/BYOK-tripwire policy,
write-ahead ledger, uncertainty stop and first-response preservation apply.

Frozen at 2026-09-28T17:19:50.141332Z:

| Item | SHA-256 |
| --- | --- |
| Manifest, canonical JSON | `b1d3a15565cd0326b7b19579a2363cb5601746122db79c72cb18588403054948` |
| Original inputs file bytes | `3474eef3ee9060f790aae21ee30fd962569ae902f2352dda46deea576e44c545` |
| Inputs, canonical JSON | `f6892450ac5500258874c31584ca46c1ca71427118b9c3991f8302ff1131e42a` |
| System prompt UTF-8 | `d74ce46fb5a9499d6e7358d767924ee777b782ce8283ba05efc79cdb398b1baf` |
| Endpoint snapshot, canonical JSON | `7c72fe6da0d7badcebf94bff91d88db1fa9b2be05d60c542c6d437033f4e132d` |
| Comparator code | `5bece63d4d1b3572a2379da07c9c528fefca40f78585dfe2a8c0065ce6c15ae8` |
| Unchanged runner code | `2cd14424ca190f3d5fed47faf1e328993668a620b8202cfb7b196b363f971635` |

The exact prepared bundle is at
`/workspace/scratch/edcd10c4764c/structure-pair-chat-v1/`: manifest, plan, input
copy, endpoint snapshot and system prompt. The parent must persist the bundle
and later first responses in a `[skip ci]` checkpoint. No repository request
switch or GitHub Actions workflow is activated by this preparation.

## Scoring and interpretation

Only after freezing/collecting first responses, load the existing frozen pair
gold. The scorer verifies the manifest/code hashes and runner ledger/artifact
hashes before parsing model output. It accepts exactly `{"q01":true}` or
`{"q01":false}` as JSON objects; whitespace is allowed. Duplicate JSON keys,
extra fields, string/numeric truth values, Markdown wrappers, refusal, tool calls,
non-stop finish reason, unexpected model/provider and non-200 responses are
unavailable predictions, never repaired answers.

Report planned/attempted/available counts, coverage, all errors and attempt
states, TP/TN/FP/FN, precision/recall/F1, accuracy among available outputs and
accuracy over all 48 planned cases (missing outputs cannot count as correct).
Group by language, variant and family from gold only at scoring time. Retain
per-case decisions to compare both models on exactly the common case IDs.
Report spend for every attempt with known usage, count attempts missing billing,
and retain response elapsed times and the stop reason. A provider failure is not
an incorrect semantic decision, but does lower end-to-end coverage/accuracy.

Jev's probability threshold remains its frozen 0.5 rule; this chat condition
emits only a boolean and has no calibrated confidence estimate. Model/API/output
formats, cost rates and service latency differ. Summed response elapsed time is
not wall-clock batch time and excludes preflight overhead. Do not compare this
classification accuracy directly with the earlier graph-admission rate.

## Local commands and verification

Run only after parent audit and approved credential setup outside the repository:

```sh
python -B loom/tools/structure/structure_pair_chat.py run \
  --manifest /workspace/scratch/edcd10c4764c/structure-pair-chat-v1/manifest.json \
  --run-dir /workspace/scratch/edcd10c4764c/structure-pair-chat-v1/run
python -B loom/tools/structure/structure_pair_chat.py score \
  --manifest /workspace/scratch/edcd10c4764c/structure-pair-chat-v1/manifest.json \
  --run-dir /workspace/scratch/edcd10c4764c/structure-pair-chat-v1/run \
  --gold loom/tests/fixtures/eval/jev_structure_pairs_v1/gold.json \
  --output /workspace/scratch/edcd10c4764c/structure-pair-chat-v1/score.json
```

Offline verification: **6/6 comparator tests passed**. They cover exact input
preservation, strict booleans and duplicate-key rejection, refusal/truncation and
identity checks, coverage/error denominators, a 48-case fabricated manifest,
artifact-ledger integrity, missing cases and protocol-mutation rejection. No real
pair labels or model responses were used by those tests.
