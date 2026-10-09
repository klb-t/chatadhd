# Research scheduling and payer boundary — continuation 01

This increment contains offline mechanism code and controlled-transport tests.
It made **zero provider calls and spent USD 0**. Fabricated ledger costs in tests
are not part of the owner's campaign. Historical campaign accounting remains
720 attempts / USD 0.873216500 spent / USD 4.126783500 archival remainder under
the original nonrenewing USD 5 cap. Current provider usage remains unknown.

## Integration contract

`experiment_workflow_v1.Queue` stores scheduling/evidence references, not money.
`research_programme_runner.PrivateLedger` remains the only reservation and
settlement authority. Its implementation and original records are unchanged.

1. Render/freeze the spec and exact bodies, then add `ordered_jobs(spec)` to a
   private `Queue`. The payer manifest must use those same operation IDs and
   exact request hashes. `claim(worker, operation_id)` atomically assigns one
   owner. No expired lease silently releases a claim. Source-event deduplication
   is scoped to each spec; a shared queue may serve independent opt-ins. Older
   journals migrate transactionally, retaining their original events table as
   `events_legacy_v1` and copying every event into the composite-key journal.
2. Existing payer `run_stage` performs its usual key/campaign, manifest, pricing,
   cumulative-budget, pending-reservation and escalation gates. It reserves and
   fsyncs started/request witnesses before calling its transport.
3. `PayerBoundary.transport(...)`, used by the research integration harness
   inside that exclusive payer invocation, looks up the existing committed
   reservation. Programme, key fingerprint, operation, manifest, body, route,
   state, positive reservation and saved started/request witnesses must match.
   Only then can `Queue.mark_verified_dispatched` set pending and append the
   actual dispatch ordinal. Arbitrary strings and missing references do not
   authenticate dispatch. The historical `mark_dispatched` remains only an
   offline compatibility marker and is not used by this boundary.
4. Existing payer captures first raw bytes and verifies generation/billing.
   `sync_verified()` projects the fully validated receipt to the queue; it never
   dispatches or settles money. It does not promote pending or malformed captures.
5. `append_evidence` adds observations while preserving the first queue result.
   Existing payer `append_attempt_proof` retains the original response/result
   bytes and appends a separately verified settlement projection.

The boundary has no dispatch CLI. The existing runner's injected transport
argument is exercised only as a controlled offline integration hook here. Live
integration still requires a fresh frozen preflight and the authorized small
panel. A reference/queue entry is not proof of currently available budget.

## Crash and ambiguity procedure

The tests interrupt before reservation, after reservation, after POST reaches
the controlled server, before first-response write, after that write, and before
settlement. Each case reopens the durable queue and restarts the existing payer.

- Before reservation, an explicit `release_unreserved_claim` checks the existing
  payer under its exclusive lock; only absence of any attempt permits release.
- After reservation, restart refuses another POST and retains the full unknown
  reservation. Neither timeout, process death nor cancellation means zero cost.
- A captured response with known generation and eligible delayed billing may be
  resolved through the existing `reconcile_stop(..., captured_pending=True)` /
  `resolve_captured_attempt` GET-only procedure. It checks saved original proof,
  model/provider/API identity, credit billing, cost and current key accounting;
  it appends a resolution instead of rewriting originals. Then sync again.
- A crash leaving an uncaptured or partially journaled reserved attempt remains
  blocked. The present payer has **no automatic reconciliation** for that case.
  Preserve all bytes/reservation and obtain attributable provider evidence via
  an independently reviewed recovery. Do not delete rows, release the amount or
  retry merely because no response was found.

This is exclusive scheduling plus conservative at-most-one dispatch admission
per saved attempt, **not exactly-once model inference**. The transport cannot
prove what happened remotely after an ambiguous send. Both SQLite and the payer
use durable writes, but this test suite is process-exception/restart testing,
not a hardware power-loss or filesystem-corruption guarantee.

## Cache and ordering

Selectable modes are `blocked` (alias of historical `balanced_blocks`),
`controlled_random`, and `cache_aware` with explicit positive `window_size`.
`controlled_random` uses a seeded six-round Feistel permutation with cycle
walking over global job positions and constant scheduling memory. It is a
pseudorandom reproducible order, not a uniform draw from all permutations or a
promise that seeded inference repeats. `cache_aware` groups exact compatible
prefix/model/provider/route/endpoint hashes within bounded windows; it changes
order only. Historical `prefix_grouped` still explicitly sorts the full chosen
scope and is unsuitable for an unbounded matrix.

Variant, source, repetition and request hashes persist. `comparison_identity`
allows the same source/variant/repetition to be matched across order strategies;
`operation_id` retains the full frozen spec including the chosen order.
`queue_ordinal` is planned order; `actual_order()` journals dispatch admission
order separately. Different observed routes/providers are not assumed to share
cache. Statistical analysis should account for actual order and missing cache
observations, including cold/warm and endpoint changes.

`CacheObservedTransport` extends the existing transport without replacing its
origin, redirect, credential, timeout or retry rules. It sets
`X-OpenRouter-Cache: false` and records only the response
`X-OpenRouter-Cache-Status` (absent remains null). It never treats a request-header
echo as an observed hit. A HIT or malformed JSON retains first raw bytes in the
payer, stops admission to another POST, and is not accepted as independent
inference. Partial transport responses also remain uncertain.

Official response-cache documentation was freshly checked by the task owner
agent on 2026-10-09:
https://openrouter.ai/docs/guides/features/response-caching . Before any actual
provider dispatch, recheck relevant official provider/OpenRouter rules and save
a fresh receipt with the preflight. Existing prompts are never rewritten merely
to improve cache. Response cache, prompt cache and local replay remain distinct.

## Evidence and limitations

`queue-benchmark.json` records a finite 1000-job sample for each mode from
400,000,000,000,000,000,000 possible jobs, including repetitions. It measures
planning plus durable insertion wall time, Python traced allocation peak,
SQLite file size, exact restart content and idempotent repeated insertion.
Tracemalloc excludes SQLite/native allocations and total RSS. These measurements
say nothing about model quality, provider capacity, complete matrix disk needs
or independent source-family sample size.

Test failures and exception text contain only fixed diagnostics/fabricated
fixtures; source conversations and private requests are not used by these
public tests. The separate real-source preparator owns its own private manifests.
