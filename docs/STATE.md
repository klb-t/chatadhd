# STATE — ChatADHD / Loom

Updated 2026-09-30 by the continuation integrator. This replaces the internally
contradictory 2026-09-29 status summary. Historical measurements remain in their
original reports and Git revisions; they are not current verification claims.

## Current checkout and coordination

- Repository: `klb-t/chatadhd`. GitHub metadata checked in this session reports
  **public** visibility, contrary to the older private-repository description.
  No visibility change was performed. Do not publish private source archives or credentials.
- Integration line: `gpt/research-2026-09-30`.
- Recovered source: `fafc77f8eeebdff4c32897b5e8ef94dd26d4ec38`.
- Current Claude development history through `46066308ac6306a1b30f65b98e19a40da6df7e9c`
  was merged while preserving the newer research fixes and owner clarification.
- PR6 remains a draft from the older Codex branch into `main`. It has not been
  merged or silently repointed; `main` remains a separate historical line.
- Six current agent lanes and six ready-to-start independent conversation
  packages: [coordination/README.md](coordination/README.md). ROOT is the sole
  integration/STATE writer. Other conversations use scoped branches and receipts.
- Builds/tests run locally. Commits retain `[skip ci]`; no paid inference was
  performed in this continuation session.

## What this system is

Loom is the C++20 kernel, C ABI, CLI, HTTP/SSE server, React workbench and Android
bridge in `loom/`. ChatADHD is the interaction/workspace layer. The older Python
app remains available. The knowledge pipeline is catalog → extract → resolve →
assess → generalize → materialize. Research prototypes and native runtime are
separate evidence levels; implementing a mechanism does not prove model quality.

Read `AGENTS.md`, `architecture/OWNER_REQUIREMENTS_2026-09-26.md`,
`architecture/LOOM_CONCEPTUAL_MODEL.md`, `CLAUDE.md`, `../loom/README.md`, and
`research/OWNER_CLARIFICATION_2026-09-30.md`. The ecosystem note in
`../ECOSYSTEM.md` is a conceptual direction, not a mandate to build every integration.

## Current continuation — implemented, final verification in progress

| Area | Change and boundary |
|---|---|
| Chat context | Explicit `knowledge_context` connects native ContextEngine to actual `loom_chat_ex` / HTTP chat. Memory tree, legacy graph and history are independently selectable. Existing composition remains default. |
| Scope and detail | `relation_hops` controls exploratory goal-edge reach; `detail_resolution` selects label/summary/full/raw independently of item budget. Required premises remain tracked. This is not yet per-thesis plan selection. |
| Inspectability | Optional auto/on/off trace records the exact compiled message array, selected run and native context. A compilation trace is not proof of provider delivery. |
| Persistence | Semantic reindex merges metadata instead of discarding source provenance/context traces; non-object metadata is preserved. |
| Configuration | Explicit stream option overrides config; unset respects config. Latest completed knowledge run is found even after many newer unfinished runs. |
| Confidence | Calibration preserves supplied User confidence instead of forcing 1.0; owner priority in conflict resolution remains independent. Explicit confirmation elsewhere retains its older semantics. |
| Source projection | Active strict adapter/CLI checks source hash, UTF-8 boundaries, JSON Pointer and ambiguous metadata, without modifying frozen research instruments. |
| Coordination | Local SQLite task leases/fencing, durable receipts, explicit unknown outcome handling, backup and exact saved graph-workflow replay CLI. No claim of distributed consensus or exactly-once external effects. |
| GitHub sync | New default profiles exclude root/nested `secrets.json`. Existing user profiles and explicit overrides are preserved. This is a default, not a complete data-loss-prevention system. |

Detailed contracts: [chat](research/CHAT_KNOWLEDGE_CONTEXT_2026-09-30.md),
[scope/detail](research/CONTEXT_SCOPE_DETAIL_2026-09-30.md),
[source projection](research/retrieval_exploration_v1/SOURCE_PROJECTION_CONTRACT_2026-09-30.md),
[coordination](research/coordination_runtime_v1/README.md),
[data flows](research/THREAT_MODEL_2026-09-30.md).

## Verification ledger

Final current-candidate results will be recorded here after the shared build and
real-server browser test complete. Do not infer native success from the table above.

Already captured in this session:

- Exact recovered baseline `fafc77f`: Python contracts **185/185**, eval
  **213/213**, seeding **36/36**. Separate suites; do not add overlapping counts.
- Active source projection: **56/56** new boundary tests; combined selected
  retrieval/actor suites **145/145**. Historical object parity **84/84** native
  projections plus **12/12** controls. Synthetic/repository-derived evidence.
- Coordination with CLI: **31/31**, including actual subprocesses, competing
  processes, killed workers, unknown effects, backup and saved transcript replay.
  Earlier 19/21/22-test runs are historical stages, not additional tests.
- Python GitHub defaults: **2/2**; legacy-preset negative control fails as expected.
- Web TypeScript/Vite build passes; complete current native/browser gate pending.

Build environment uses bundled SQLite because system development headers were
unavailable. This difference must remain visible in final native evidence.

## Historical research measurements and limits

- Catalog synthetic DEV: **31/45 recall**, **31/31 precision**, traps **0/5**,
  generic noise **0/15**, after round 3. Thus 14 misses, not 24. The earlier 13/45
  was a previous checkpoint. The thresholds were not weakened; DEV tuning does
  not establish blind/generalization performance.
- Export fidelity: six synthetic OpenAI/Anthropic fixtures, **1604/1604 JSON
  leaves** preserved; additional stress cases. This is not validation of the
  owner's full real exports. Provider wrappers, assets and semantic interpretation
  still require real-data checks. Import history is not a recorded model request.
- Recovery report retained native **73/74**, corrected structure **851/851**,
  contracts **185/185**, eval **213/213** from distinct runs. The 851-test result
  includes 30 tests whose unpublished source was lost. Do not claim those sources
  recovered or combine the runs into a fresh 74/74 measurement.
- Jev/model studies have task-dependent error rates; supplied graph accuracy,
  parser validity, agreement and actual extraction quality are different metrics.
  First responses, failed attempts and negative results remain in `research/`.
- Temporal prediction is not established: the older temporal package leaks
  later knowledge. Retrospective consistency is not predictive accuracy.

## Next work by priority

1. **ActiveTaskSpec in actual requests (W1):** retain source history while compiling
   refinements, exceptions, rejections and unresolved corrections into a current
   instruction. Current chat context integration does not implement this compiler.
2. **Retrieval quality (W2/W3):** real semantic candidate channels before selection,
   plan/thesis-level scope/detail, counter-evidence and omission diagnostics.
   Whole-request budgets remain separate from ContextEngine's item estimate.
3. **Independent source validation (W6):** verify actual authorized owner export
   slices locally; do not conflate availability of archives with permission for
   external model submission. No new request for files is justified until existing
   connected artifacts have been checked.
4. **Graph execution (W4):** connect existing graph operations to additional real
   callers; retain accounting and provenance. SQLite leases require one shared
   local database and are not a cross-conversation distributed lock.
5. **Workspace (W5):** integrate durable per-view couplings/profiles with the
   existing simultaneous views; no arbitrary five-view limit.
6. **Known inherited limits:** per-query candidate caps, query-error visibility,
   real export scale/Android device tests, model calibration, native precision
   follow-up. Default secret exclusions do not repair existing custom profiles.
7. **Resource accounting:** concurrent reservations are checked, but historical
   overlapping memory peak is not retained as advertised. See
   [reproduction](research/RESOURCE_PEAK_LIMITATION_2026-09-30.md).

## Non-resetting constraints

- Owner decisions and current instructions outrank older proposals. Defaults are
  configurable presets; acceptance policy never rewrites inference as observation.
- Python parity is no longer a product constraint; existing compatibility tests
  remain regression sentinels until an explicit migration. No schema migration or
  public ABI symbol addition is part of this continuation.
- Do not read/tune against `eval/real-holdout-key`, the sealed catalog corpus or
  sealed graph validation during development. Preserve first evaluation results.
- The existing dedicated-key USD 2 programme does not reset with a runtime or
  conversation. Historical key balance is not current balance; no new frontier
  spending is authorized by a larger ChatGPT token allocation.
- Small verified commits and evidence go to the integration branch. Only ROOT
  merges lanes and updates this document; no force pushes and no implicit PR6 merge.
