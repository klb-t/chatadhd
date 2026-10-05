# Source-bound endpoint reservations — 2026-10-05

The frontier bodies remain unchanged. This increment adds a pure estimator,
caller-owned [policy](endpoint-pricing-policy.json), three exact public endpoint
snapshots and an [offline 24-request projection](endpoint-quotes-2026-10-05/projection.json).
It performs no inference or account access. The runner must fetch fresh endpoint
bytes before admission; these saved snapshots are reproducible regression data.

## Observed public routing and prices

[OpenRouter provider selection](https://openrouter.ai/docs/guides/routing/provider-selection#base-slug-matching)
defines base-slug matching for regions and variants, with a service-tier exception.
Tier endpoints require opt-in. The [service-tier contract](https://openrouter.ai/docs/guides/features/service-tiers)
defines standard as the default and describes `service_tier`, `speed: fast`,
`:nitro` and `:floor`. The estimator uses the returned endpoint `tag`, rather
than a display name shared by standard/flex/priority rows. It keeps all admitted
regions and variants; it takes their component-wise maximum rather than choosing
the cheapest row or rejecting multiple matching endpoints.

| Requested provider | Admitted standard tag | Excluded tier tags | Endpoint snapshot SHA-256 |
|---|---|---|---|
| `openai` | `openai` | `openai/flex`, `openai/fast` | `7bebf2b554e51052caa6ea41f1cae172d802726faadf71c701dfe1a352b511b8` |
| `anthropic` | `anthropic` | None under this provider slug | `abed01919b595812d24c2d8221305da76270dfad5e10938e3851507308a3b110` |
| `google-ai-studio` | `google-ai-studio` | `google-ai-studio/flex`, `google-ai-studio/priority` | `0e5712baf4d4f51ccc649df16e151e374aacce4ad48edfd44eead40d9d340bbd` |

The table records the retained snapshots and the frozen bodies' absent tier
opt-ins. It does not establish which endpoint a future response will actually use.

The standard endpoint labels below are copied verbatim from the public snapshots.
Their `model_id` fields equal the requested undated IDs. The dated identifiers
appear in endpoint `name` labels; these snapshots do not contain an observed
response/generation model field. They provide source-backed alias candidates,
while actual receipt aliases remain unobserved until a response is retained.

| Requested model | Standard endpoint pointer | `provider_name` | Endpoint `name`, verbatim |
|---|---|---|---|
| `openai/gpt-6.1-sol` | [gpt61sol.json](endpoint-quotes-2026-10-05/gpt61sol.json), `/data/endpoints/2` | `OpenAI` | `OpenAI \| openai/gpt-6.1-sol-20260929` |
| `anthropic/claude-sonnet-5.5` | [sonnet55.json](endpoint-quotes-2026-10-05/sonnet55.json), `/data/endpoints/4` | `Anthropic` | `Anthropic \| anthropic/claude-sonnet-5.5-20260928` |
| `google/gemini-3.1-pro-preview` | [gemini31pro.json](endpoint-quotes-2026-10-05/gemini31pro.json), `/data/endpoints/3` | `Google AI Studio` | `Google AI Studio \| google/gemini-3.1-pro-preview-20260219` |

The same Gemini snapshot separately records Vertex endpoints with
`provider_name: Google` and `google-vertex/*` tags. Those labels do not identify
the requested AI Studio route. Its source-backed provider alias is exactly
`google-ai-studio: ["Google AI Studio"]`; a Vertex label must not be silently
normalized to AI Studio.

[Pricing overrides](https://openrouter.ai/docs/guides/overview/models#pricing-overrides)
are conditional schedules: all conditions must match, later matching entries win
per component, and omitted component prices inherit the base. `min_prompt_tokens`
is a strictly-greater threshold; time windows and weekdays also exist. The
estimator deliberately avoids dispatch-time predictions. It retains the original
conditions and rates, and reserves the maximum rate for each component across
every schedule, including schedules that may be unreachable. Unknown fields or
unsupported nested structures stop the estimate. Nothing named `overrides` is
silently discarded as a non-charge field.

## Complete quantities and uncertainty

All rates require a quantity bound. The preset reserves the full prompt quantity
at each published cache rate, including a separate 1-hour write rate, and the
full output quantity again at an internal-reasoning rate. This over-reserves
mutually exclusive cache possibilities and possible output overlap. A validated
fractional discount is ignored because it reduces the charge. Additional price
components, rules, schemas and tier classifications can be supplied as policy
data without changing estimator code.

Explicit quantity overrides receive the same declared minima as defaults.
The preset's `minimum_value: 1` records that each dispatched operation is one
API request, so an explicit request quantity of zero cannot evade a positive
flat request rate. `minimum_bound: prompt` preserves the full-prompt upper
reservation for cache read/write/1-hour write; `minimum_bound: completion`
preserves the full declared output allowance for internal reasoning. This also
covers an allowance larger than the wire output limit, such as stage3's retained
`4096` reservation with `max_tokens: 2048`. The universal operations compare
supplied values and references; component names, minima and assumptions remain
caller-owned data. They establish this conservative preset's consistency, not
actual cache activity or observed token consumption.

The [OpenRouter reasoning contract](https://openrouter.ai/docs/guides/best-practices/reasoning-tokens#reasoning-tokens-and-max_tokens)
describes a combined output budget on most providers. Google's
[generateContent thinking guide](https://ai.google.dev/gemini-api/docs/generate-content/thinking#token-limits-and-max_output_tokens)
explicitly describes a combined thought/output infrastructure cutoff. The
[wire API reference](https://ai.google.dev/api/generate-content#v1beta.GenerationConfig)
names `maxOutputTokens`. Applying that contract to OpenRouter's forwarded
`max_tokens` is a provider-contract inference, not a locally measured guarantee.
`reasoning.effort: low` does not establish a precise token budget; Gemini 3's
`reasoning.max_tokens` mapping is a different knob. Actual generation receipts
remain authoritative, including truncation and unexpected usage.

Image/audio/search zero quantities require an exact body-schema witness for
each operation: string-only messages, text output, one response, and no billable
tools/plugins. Explicit zero quantities need the same witness. The schema is a
configurable research preset. It neither removes engine capabilities nor limits
owner-selected future programmes. Validating only the first request sharing a
model/provider quote would let later media/plugin requests evade accounting;
the runner must check every operation in `price_plan`.

## API and checks

`loom/tools/structure/endpoint_pricing_v1.py` exposes:

- `resolve_endpoint_pricing(raw_bytes, operation, policy)`: exact raw and policy
  hashes, admitted/excluded endpoint tags, retained original pricing schedules,
  component maxima, first-operation quantity witness and reservation rationale.
- `resolve_units_upper_bounds(operation, component_prices_usd, policy)`: complete
  quantities for this individual request. Call it for every operation, including
  later requests sharing an already captured quote.
- `resolve_operation_routing(operation, policy)`: per-request routing audit.
  Compare it to the cached quote's audit; equal model/provider names alone do
  not establish equal service-tier prices.

The caller supplies detached operation snapshots with `request_bytes`, parsed
`request_body`, requested model/provider and manifest quantities. Hash or parsed
snapshot mismatches stop estimation. The existing runner keeps responsibility
for freshness, immutable reservations, API-origin authentication, account
reconciliation, response/generation identity, actual cost and the ×10 guard.

Package and flat CTest-style discovery both pass **27/27** focused tests. The
public-snapshot regression validates all **24/24** frozen stage3/stage4 requests.
Other cases cover region envelopes, opt-in variants/tiers, retained conditional
schedules, unknown components/conditions, malformed prices, explicit-zero media
bypasses, later-request plugin/tier changes and output under-reservation. These are
mechanical tests; they establish no model quality.

The unit-floor negatives are preserved on
[`archive/gpt/model-research-runner-review-2026-10-05`](https://github.com/klb-t/chatadhd/tree/archive/gpt/model-research-runner-review-2026-10-05),
in the supplementary ZIP described by its
[pinned evidence README](https://github.com/klb-t/chatadhd/blob/3a4d9ed6e76e44cf05a7d919a07d88815a84cfaf/docs/research/model_research_2026-10-05-runner-review/README.md).
The unchanged original baseline passes 22/22. Old helper code with the new
explicit floor data produces seven failed assertions and zero errors; corrected
code/data passes 27/27. The original old-data replay's five failed assertions
and two setup errors from missing rule maps are also retained and distinguished
from estimator defects. Full sources, public inputs and raw logs remain
reproducible; the first summary-parser mistake is preserved separately.

The first test run had **20/21** passing: its expected reasoning quantity assumed
the wire output preset `2048`. The prepared stage3 manifest actually preserves
its historical output reservation quantity `4096`. The corrected assertion
compares the copied reasoning quantity to that unchanged declared quantity. No
request, budget, threshold or production rule was reduced.

| Stage | Requests | Previous historical reservation, USD | Retained-snapshot component upper projection and reservation, USD |
|---|---:|---:|---:|
| Graph completion / pattern discovery | 12 | 1.227694 | 1.965986350 |
| Graph reply / text plus JSON | 12 | 1.003626 | 1.575160050 |

The larger projections include all conditional rates, all cache possibilities
and the extra reasoning quantity. Historical reservation floors are preserved.
They are neither expected bills nor an admission decision for the whole
programme. Each actual stage is admitted separately against current balance,
known actual spend and outstanding reservations; a later stage that does not fit
remains undispatched. The regenerated projection binds current canonical policy
SHA-256 `a76edd53f3c383712770cd51f7994c84b2330b2bdb9b9cbf8538180a8cb10f9c`.
All 24 request hashes, declared quantities and projected amounts remain
unchanged. Independent reproduction through the integrated runner's
`price_plan` confirms 24/24 operations across three model/provider pairs in each
stage (six stage/pair groups), with zero network or paid calls. Current
projection SHA-256:
`72a491dc7760a26f94e380076c3d50b42257e965d1b3fc6c253d5d6cb1bf13f5`.

## Do wątku 7

Enable this policy explicitly for later stages. Preserve the legacy 432-run
default. The runner integration is optional through its caller-owned
`endpoint_pricing` object: `endpoint_quote` captures the envelope, and
`price_plan` verifies every operation's units and routing against the quote.
Refresh public endpoint bytes immediately before paid admission.
Record served model/tier, first response, generation cost and estimator policy
hash in the actual-run evidence. Keep the uncertainty above visible in cost and
method-evaluation claims.
