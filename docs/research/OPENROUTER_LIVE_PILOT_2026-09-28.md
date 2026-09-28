# Real-model extraction pilot: ready for a capped credential

The owner asked to continue autonomous work and use an OpenRouter key for actual
model experiments. Public unauthenticated models/endpoint API access was verified
(HTTP 200). No OpenRouter credential is configured here. **No paid inference has
run.** This checkpoint prepares source-text experiments; it does not substitute
another supplied-graph score for text understanding.

## Frozen cases and methods

`loom/tests/fixtures/eval/live_structure_pilot_v1/` contains 32 fresh short texts,
16 English and 16 Polish, split into 16 development and 16 validation cases.
Both languages of each construction family stay in one split. Cases test roles,
negation, conditional direction, conjunction, quantifier binding/order, quotation
and located abstention. This is not a representative conversation benchmark.
The independent author removed operation-revealing IDs before any inference.
Final fixture manifest file SHA-256:
`8ad989b50a164ad830dc8df62481cb2ac71949001dde10cdf4cff48bcc39fa07`.
Preparation reads inputs and the split manifest, never gold. Gold is read only
for offline scoring; family labels and gold never enter the model prompt.

- `native_v1` reuses the exact native `kGraphPrompt` and source-packet/hash
  envelope, producing the existing occurrence-graph candidates.
- `native_anchors_v1` adds universal grounding/role instructions and a mechanical
  UTF-8 coordinate table. This is a prompt variant, not the previously proposed
  two-call anchors/composition experiment.

The first disabled request selects `native_v1`: 16 development cases × two
explicit model/provider pairs = **32 calls**. Pilot models are
`qwen/qwen3-30b-a3b-instruct-2507` on `nebius/fp8` and `openai/gpt-4.1-mini` on
`openai`. This is an inexpensive comparison, not a measured model ranking.
Research transport uses JSON object mode and an 8192-token output allowance.
Native prompt reuse is not an end-to-end native runtime test; its transport and
output limit differ. Existing user-configured semantic model settings are intact.

## Measurement and cost

`live_pilot.py` freezes code/input/prompt hashes, full endpoint price evidence and
every exact request. `live_pilot_score.py` independently measures graph-contract
validity, source spans, structure and source-anchored semantics. Opaque handles
and display symbols do not affect comparison. Logical equivalence outside the
declared grammar is not scored. Only the first alternative is scored; gold never
selects a better response. No response repair. Empty alternatives are unlocated
abstentions, not successful located ambiguity handling. Missing/failed requests
stay in the planned denominator. Response model/provider must match identities
frozen from the selected endpoint or receive no semantic credit.

`openrouter_runner.py` pins one model/provider, disallows fallbacks, bounds input,
output, request count and price, and durably records each attempt before POST.
Completed/rejected/uncertain attempts are not retried. HTTP 401/402/403/429 stop
the run. Reported cost exceeding a reservation stops further calls. Raw first
responses and hashes remain inspectable; changed manifests cannot silently resume.

Saved public-price preflight: `inputs/openrouter-preflight-v1/`.

- 32 calls, **USD 0.2978870** conservative reservation.
- Proposed dedicated key ceiling **USD 2**, no periodic reset, BYOK usage included.
- Canonical manifest hash:
  `93f97d72ea470e57327f0b4aa111e265f69be486eb9ff8cf34115084fc0eed5b`.

Request bytes plus overhead are an input allowance, not an exact tokenizer or
guaranteed invoice ceiling. The server-side dedicated key cap is required by
this experimental runner. Price evidence expires after 24 hours; activation
prepares fresh evidence and a new immutable execution manifest.

## Activation and recovery

Use GitHub repository secret `LOOM_OPENROUTER_PILOT_KEY`; never put the key in
chat, tracked files, command arguments or experiment manifests. See
`OPENROUTER_ACTIONS_SETUP_2026-09-28.md`.
`openrouter-pilot-request.json` is **disabled**. Ordinary code pushes do not start
paid inference. The owner must configure the secret and accept the proposed
capped pilot before its activation commit.

The workflow runs only on this research branch. It reserves the experiment
identity before inference and rejects reruns/previous activations. It uploads
retained artifacts after partial failures. Abrupt runner loss can prevent the
final upload; the reservation and activation history then block automatic repeat
spending. Artifacts last 90 days: after a real run, inspect and commit nonsecret
first results to the research branch. A running-agent indicator or completed
workflow alone is not evidence of successful semantic evaluation.

## Verified here

- Full restored research suite: **309/309 tests**, 1.459 seconds.
- New: 25 mocked runner tests, 10 scorer mechanics tests, six orchestration tests,
  eight independent integration tests.
- All 32 authored reference graphs mechanically validate/score; this verifies
  the scorer, not model quality.
- Workflow YAML/shell/Python checks, 12 activation scenarios and three reservation
  scenarios passed locally. Official action tags/contracts verified; full-SHA pins.
- Public-price preflight succeeded. No paid calls, real-model accuracy result,
  native CTest rerun, ABI/schema change or canonical graph mutation.

## Continue

Retain and score first responses before modifying prompts. Separate format,
grounding, structure, semantic, abstention and transport failures. Compare the
coordinate-aided variant on identical development inputs after inspecting costs.
Freeze the chosen method before validation; never tune to validation gold or
discard failed cases. Report language/model/method denominators and actual usage
before deciding whether two-stage composition is worth testing.

Other open work: natural multithread conversations, late topic starts/returns,
graph-context selection, and the Gemini/Jev-inspired relevance-score experiment.
None is claimed complete here.

Official sources checked 2026-09-28:

- https://openrouter.ai/docs/guides/features/structured-outputs
- https://openrouter.ai/docs/guides/routing/provider-selection
- https://openrouter.ai/docs/api/api-reference/api-keys/get-current-api-key
- https://openrouter.ai/api/v1/models
