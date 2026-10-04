# W3 — chat context selector, 2026-10-04

Status: implementation and verification in progress; no measured improvement claimed yet.
Base: `161cc22dfb84fe863389d6b90323bd44516a68dc` (fresh `main`).
Branch: `gpt/chat-selector-2026-10-04`. Only the integrator promotes this lane.

## Implementation plan and scope

1. Provide a real capability-selected embedding adapter in `loom/src/providers`
   and a vector candidate channel in `loom/src/context`. Cache exact content
   hashes with model/provider identity; use fake transport in verification.
2. Bind explicit configured goal-typing execution to chat `send()`; previews
   stay offline. Use W2's durable `UsagePolicy` before provider operations.
   Missing W2 is an unavailable capability, never permission to send.
3. Adapt legacy memory nodes and graph-linked messages as source units into
   one selector result with the knowledge context. Keep source identity,
   authority distinctions, stable/project/goal order and one prompt budget.
   The historical recipe remains a configurable compatibility preset.
4. Report installed/unavailable channels in existing JSON context traces,
   including provider errors and selection limits separately from zero scores.
5. Rebase on current `main`; retain every existing quality gate.

No UI, public headers, server routes, C ABI exports, runtime composition,
profile files, other lanes or private exports are edited. `ProviderRegistry`
currently has no `embed` member. This lane supplies a source-private adapter
over that registry; a public method declaration belongs to the header owner.
The W2 header is available on `gpt/usage-policy-2026-10-04`, not yet on `main`.
Its actual integrated configuration is used when present; no policy fork is
introduced here. Remote execution is disabled when that dependency is absent.

## Verification

Baseline full build/CTest and new synthetic selector regressions pending.
Paid calls: **0**. No sealed holdout, blind catalogue or real archive read.
