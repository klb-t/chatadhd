# Existing campaign: credential handoff and live audit blockers

This increment follows the completed offline package at
`cb16b336ee0c59a1ff80b4899dadebce33aa7610`. It does not repeat the retrieval
benchmark, annotations, representations, rankings or model preparations.
The owner reprioritized credential access and a small real model experiment.

Dependencies fetched and pinned on 2026-10-09:

- C/base: `cb16b336ee0c59a1ff80b4899dadebce33aa7610`.
- A/audit: `9f931da3bb1d1001f9b7d865914ae195d9255b7e`.
- B/runtime: `949a86efefe899c961daad835f5fed86abebd114`.

The existing `credential_handoff_v2.py` generated a working downloadable HTML
form before audit work resumed. The owner was given the file and the steps:
download/open locally, encrypt the existing campaign key, attach the encrypted
JSON. The receiver's private key is retained outside Git, with mode 0600 in a
0700 directory. Its public/private match was checked. The actual receiver was
not consumed by tests. The unchanged crypto script passed the existing ten
handoff tests. Only the generated form's visible legacy "new programme / EUR5"
labels changed to describe continuation of the nonrenewing USD5 campaign.
Neither encryption nor receiving a credential authorizes a new campaign.
No plaintext key or receiver private key belongs in this directory or archive.

`handoff-receipt.json` is a public status receipt, not a credential reference.
The actual receive action must use the retained private receiver and the
owner's actual encrypted envelope through the existing decrypt command. It
must not consume a test envelope against that receiver. No envelope had arrived
when this increment was prepared.

Audit work is restricted to live blockers: A4-C-001/002 source/version and
request identity, A2-C-001/004 public auxiliary inputs and diagnostics, and the
adjacent reproduced A2-C-003 payer/request association. Individual reports give
before/after results, code hashes and complete test evidence. Synthetic fixtures
exercise mechanisms only; they do not measure model quality.

The read-only historical ledger inspection includes all 714 append-only
attempt resolutions: **720 completed attempts, USD 0.873216500**. Counting only
the immutable initial rows would incorrectly report 714 unresolved attempts.
This archive has zero unresolved costs in its effective projection. It proves
neither today's provider usage nor the absence of later reservations elsewhere.
USD 4.126783500 remains an archival remainder, not a current spendable balance.

An unauthenticated GET of the OpenRouter model endpoints succeeded using the
existing environment proxy policy. Direct DNS failed; no workaround or proxy
change is needed because the campaign's saved policy already uses `environment`.
The public quote is saved separately. It is not an authenticated preflight.
No owner-key API operation, paid POST, reservation or new response occurred.

Resume after receipt: decrypt with the existing receiver; verify fingerprint
against the existing campaign, fresh `/api/v1/key` usage/nonrenewing USD5 limit,
effective reservations and current endpoint prices. Use the single existing
payer ledger. Do not bootstrap an empty campaign from the historical template.
Existing ready preparation bytes can be wired through connector v2 under a new
small scoped plan identity, with `source_content_verified=true`; historical
queues and paid operation IDs remain untouched. Preserve the phase estimate,
remaining authorization and reservations before any POST. Do not dispatch the
full matrix or blindly retry an ambiguous request.

Current quote inventory: the existing six-request scope compares three context
representations on two source families using one model. It is not a cross-model
study. The byte-based conservative prompt bounds and 768 completion-token caps
yield USD 0.9615540 at the retrieved price, without any cache discount. This is
an inventory quote, not a booked reservation or approved fresh phase. Fresh
preflight can reduce the scope; it cannot silently truncate a source or renew
the USD5 authorization.

Official provider documentation was checked on 2026-10-09:

- https://openrouter.ai/docs/api/api-reference/api-keys/get-current-api-key
- https://openrouter.ai/docs/api_reference/limits
- https://openrouter.ai/docs/guides/best-practices/prompt-caching
- https://openrouter.ai/docs/guides/features/response-caching

Key limits/usage and account credit are distinct. Prompt cache is model/endpoint
dependent; no cross-model sharing is assumed. The existing transport sends
`X-OpenRouter-Cache: false` for independent trials, overriding response-cache
presets. A response-cache hit would not be independent inference. Cache status,
requested/observed routing, actual usage and first response remain evidence.
Saved seed values do not establish deterministic inference.

New paid calls: **0**. New API spend: **USD 0**. Current provider usage,
reservations, authorized available balance and new model-quality metrics remain
**unknown** until the corresponding evidence exists.
