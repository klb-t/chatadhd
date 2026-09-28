# Correcting the BYOK preflight assumption

The owner disabled the key's periodic reset and explicitly asked us to resolve
the disabled BYOK toggle in code rather than require more account setup.
The earlier unconditional `include_byok_in_limit: true` requirement was our
experimental policy, not an OpenRouter authentication requirement. An ordinary
OpenRouter key is not itself an upstream provider BYOK credential.

## Replacement control

Keep the dedicated nonresetting credit limit at USD 2, explicit model/provider
selection, current price ceilings, aggregate reservation, finite requests,
write-ahead ledger and no automatic retries. Accept a boolean false inclusion
flag when the key reports zero historical BYOK spend. Request usage accounting
and inspect actual `usage.is_byok` and cost after each response. Unexpected BYOK
or missing/invalid accounting stops the batch before the next request; retain
the first response and the reservation. An observed stop persists on resume.

Zero historical BYOK spend does **not** prove no provider keys are configured.
This is a bounded first-request check, not a request-level BYOK disable switch
or a guarantee about external provider billing. The public routing docs do not
offer such a switch; `provider.only` and `allow_fallbacks: false` do not disable
BYOK. The local reservation is deliberately conservative, but is not a formal
billing guarantee. No administrative credential or account mutation is needed.

The response's `upstream_inference_cost` alone is not used as a BYOK detector:
OpenRouter's generation metadata example includes a positive value alongside
`is_byok: false`. Use the explicit flag. Preserve cost even for malformed or
truncated model content, because semantic failure does not imply zero spend.

## Reconciled execution

`live-structure-dev-v1` sent zero model POSTs; its original artifact and ID stay
unchanged. The new `live-structure-dev-v2` uses the same 16 development cases,
native prompt and two explicit models (32 requests). The public preflight found
the original Nebius endpoint unavailable (`status: -5`); before any inference,
Qwen's endpoint was explicitly changed to `siliconflow/fp8` (`status: 0`, required
parameters supported, prompt/completion USD 0.09/0.30 per million). GPT-4.1-mini
remains pinned to `openai` (USD 0.40/1.60 per million). There is no automatic
provider fallback. These are current catalog observations, not performance
claims. Its manifest
is freshly prepared and records the corrected runner's code hashes. The prior
USD 0.2978870 reservation is historical; the new plan determines current cost
reservations. The USD 2 authorization applies to the dedicated key overall.

## Primary sources checked

- https://openrouter.ai/docs/guides/overview/auth/byok
- https://openrouter.ai/docs/guides/routing/provider-selection
- https://openrouter.ai/docs/api/api-reference/api-keys/get-current-api-key
- https://openrouter.ai/blog/announcements/gif-prompts-omni-search-tool-caching-and-byok-flags/
- https://openrouter.ai/docs/api/api-reference/generations/get-generation

The next checkpoint will record the observed execution outcome, not infer
success from a green workflow or preparation-only tests.
