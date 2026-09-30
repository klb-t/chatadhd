# STATE — ChatADHD / Loom

Updated 2026-10-01 (Europe/Amsterdam) by the continuation integrator. This replaces the internally
contradictory 2026-09-29 status summary. Historical measurements remain in their
original reports and Git revisions; they are not current verification claims.

## Current checkout and coordination

- **Current integration:** [W1–W6 status](coordination/PUBLICATION_STATUS.md).
  All six published lanes and the continuity audit are merged, with additional
  durable-acceptance, real-chat retrieval, importer and web corrections.
  Source freeze `8cc5a5648ada511053d57a0f3979d96c5e0f1d36` is published on
  `gpt/integration-w1-w6-2026-10-01`. Combined verification is in progress;
  historical green runs below do not establish this candidate's result.
- Repository: `klb-t/chatadhd`. GitHub metadata checked in this session reports
  **public** visibility, contrary to the older private-repository description.
  No visibility change was performed. Do not publish private source archives or credentials.
- Established integration line: `gpt/research-2026-09-30`; it will advance only
  after the current candidate passes its combined gates.
- Recovered source: `fafc77f8eeebdff4c32897b5e8ef94dd26d4ec38`.
- Previously verified baseline: `a79862837c6726f534471f8dbcdccfe749dfb942`.
  Its receipt is retained separately from current integration verification.
- Current Claude development history through `46066308ac6306a1b30f65b98e19a40da6df7e9c`
  was merged while preserving the newer research fixes and owner clarification.
- PR6 remains a draft from the older Codex branch into `main`. It has not been
  merged or silently repointed; `main` remains a separate historical line.
- The six conversation packages have delivered their initial increments:
  [coordination/README.md](coordination/README.md). Check this state before
  restarting them. ROOT is the sole integration/STATE writer.
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

## Current integration — implemented, combined verification pending

| Area | Integrated behavior and boundary |
|---|---|
| W1 — ActiveTaskSpec | Explicit supplied specs compile into real chat. Acceptance and its user row commit atomically in the existing EventLog; complete snapshots survive metadata removal, accepting-message movement and restart. Strict current source bindings and explicit successor/replay rules remain. This is not automatic inference from prose or owner confirmation. |
| W2 — Retrieval | Plans, per-thesis scope/detail, explicit claims/counters, graph/TF-IDF channels and lexical diagnostics now pass through the real chat parser and canonical validator. Plan provenance remains caller-declared, distinct from W1's checked message bindings. Recorded counter links are not proof of contradiction. |
| W3 — Recipes | `directed_refute_v3`, 48 prepared DEV requests, compatible profiles and historical-response replay are integrated. No new model-quality measurement or paid call was performed. |
| W4 — Graph runtime | `analysis-graph` connects AnalysisPlan, budgets, transforms and durable local coordination. Results remain GraphPacket projections; a native graph/store writer is not connected. |
| W5 — Workspace | Durable views/references/couplings and one browser-local perspective are integrated. The existing chat adds a default-off, explicit multi-thesis plan editor. Perspective restore does not enable or restore execution settings. |
| W6 — Import | Anthropic source-array order and forward parents are preserved, with separate traversal order, actual-root JSON pointers, member-local indices and unknown wrapper fields. Parser `export-2` creates a corrected representation on reimport while retaining old rows and source bytes. No real Anthropic or full-export quality claim follows. |

Implementation/review receipts: [W1 durability](coordination/receipts/W1-2026-10-01-durable-acceptance.md),
[W2 chat adapter](coordination/receipts/W2-2026-10-01-chat-adapter.md),
[import fidelity](coordination/receipts/IMPORT_FIDELITY_2026-10-01.md),
[web controls](research/integration_2026-10-01/WEB_PLAN_RECEIPT.md).
New native cases: 14 for W1, 8 for W2, and 4 importer cases; execution is pending.
These case counts are not added to suite-level CTest denominators.

## Baseline features retained in this integration

| Area | Change and boundary |
|---|---|
| Chat context | Explicit `knowledge_context` connects native ContextEngine to actual `loom_chat_ex` / HTTP chat. Memory tree, legacy graph and history are independently selectable. Existing composition remains default. |
| Scope and detail | `relation_hops` controls exploratory goal-edge reach; `detail_resolution` selects label/summary/full/raw independently of item budget. Required premises remain tracked. W2 now supplies per-thesis overrides as described above. |
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

## Historical baseline verification ledger

Baseline implementation `a798628`: full Debug build with warnings treated as
errors passes; the complete CTest run is **77/77 entries passed, 0 failed,
0 skipped**, in **94.60 seconds**. These entries include native suites and Python
regression suites; they are not 77 individual assertions. Detailed results,
commands, environment, source hashes and earlier environment failures are in
[the verification receipt](research/continuation_2026-09-30/verification/).

Captured in the earlier continuation run, before W1–W6 integration:

- Baseline `research.structure`: **821/821**, as part of that full CTest run.
  The separate projection and coordination suites below are not added to this
  historical denominator.
- Exact recovered baseline `fafc77f`: Python contracts **185/185**, eval
  **213/213**, seeding **36/36**. Separate suites; do not add overlapping counts.
- Active source projection: **56/56** boundary and inherited contract tests; combined selected
  retrieval/actor suites **145/145**. Historical object parity **84/84** native
  projections plus **12/12** controls. Synthetic/repository-derived evidence.
- Coordination with CLI: **34/34**, including actual subprocesses, competing
  processes, killed workers, unknown effects, backup and saved transcript replay.
  Independent review also passed. Earlier 19/21/22/31-test runs are historical
  stages, not additional tests; the final stage fixes scalar-type equality.
- Python GitHub defaults: **2/2**; legacy-preset negative control fails as expected.
- Web TypeScript/Vite build passes. Chromium → real C++ server → local fake
  provider passes: **5 local provider calls, 0 remote calls**, covering exact
  message/trace agreement, scope/detail, independent sources, trace opt-out,
  failure inspection, reload and mobile layout.
- Native targeted gate: **10/10 CTest entries**, including the **15** new chat
  cases and **9** scope/detail cases. This is a subset of the final 77-entry run,
  not an additional independent denominator.
- Fresh catalog synthetic DEV measurement in that full run: **31/45 recall**,
  **31/31 precision**, traps **0/5**, generic noise **0/15**, ranking AUC
  **0.965556**, hits@45 **42/45**. Existing acceptance thresholds pass unchanged.

Build environment uses bundled SQLite because system development headers were
unavailable. Credential-handoff tests use `TMPDIR=/var/tmp`, outside the enclosing
workspace Git tree; earlier environmental failures remain in the receipt.
No baseline native pass is claimed from its deliberately stopped build.

The older [continuation handoff](HANDOFF_CONTINUATION_2026-09-30.md) is historical;
this file and the publication map supersede its implementation queue.
PR6 and `main` remain unchanged.

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

## Next work and remaining product/research boundaries

1. Finish the single combined build, full CTest, frozen W6 synthetic import and
   transport instruments, and browser → native server → local fake-provider
   checks. Publish exact source/binary hashes and actual denominators before
   advancing the established integration branch. Individual lane results are
   not a substitute.
2. Automatic prose-to-ActiveTaskSpec inference is not implemented. Supplied-spec
   compilation, revisions, durable acceptance and per-thesis retrieval are now
   implemented; do not rebuild those mechanisms under a new name.
3. Real-export semantic quality and W3 recipe effectiveness need independent
   measurements. Existing GPT/Jev results are preserved. The original dedicated
   USD 2 budget and first-response accounting still apply to live experiments.
4. W4's native graph/store writer, cross-device workspace synchronization,
   full real-export/asset validation and Android device checks remain open.
5. Original `semantic_sketch` source/tests were not recovered. Two additional
   saved conversation captures were decoded and searched without locating them;
   see [bounded follow-up](coordination/receipts/CONTINUITY-2026-10-01-bounded-recovery.md).
   Its historical test counts are not reproducible current gates. Any future
   replacement must be labelled new work and compared with existing machinery.
6. Inherited limitations include per-query candidate caps, query-error visibility,
   model calibration and native precision follow-up. Default secret exclusions
   do not repair existing custom profiles. The importer retains its >64 KiB
   leading-whitespace fallback limitation and does not separately materialize
   every ZIP member as an individual source blob.
7. Resource reservations are checked, but historical overlapping memory peak is
   not retained as advertised: [reproduction](research/RESOURCE_PEAK_LIMITATION_2026-09-30.md).

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
