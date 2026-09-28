# Graph-native and semantic-model round: measured checkpoints

The owner proposed one authoritative knowledge graph rather than an additional
store of thoughts/arguments, and recalled the separate inexpensive semantic
model from the Python app. This round implements source-linked graph experiments
and repairs the missing native model path. It does not establish a universal
representation of thought or a universally optimal pipeline.

## What is connected in the application

Both Python and C++ now preserve arbitrary user-selected semantic model names
on reload. Their legacy semantic analyzers reset a failure latch after relevant
configuration changes. Generation checks prevent an old HTTP completion from
changing failure state for corrected settings. Existing completed regex analyses
are not automatically reprocessed or charged when a model is enabled.

Native knowledge extraction now consumes `llm: "auto"`, uses `semantic_model`
rather than falling back to the chat model, and asks bounded source-local
questions. The web workbench previously hardcoded `llm: "off"`; it now offers
an opt-in, the configured model, all eight work limits and result diagnostics.
An optional candidate pane shows paginated drafts, exact quotes, observation
locators and model provenance through the existing knowledge query API. Existing
simultaneous views are retained. No public C ABI symbol or core schema changed.

The adapter accepts checked draft envelopes into the already-existing
`loom_kb_candidates` table. Quote bytes, UTF-8 boundaries and local references
are checked; proposal meaning is still unreviewed. It does not manufacture an
observed Claim from a model's confidence or silently merge entities. Canonical
claims and their source observations remain the authority.

Model/provider/prompt/budget identity participates in run caching. Only fully
validated responses enter the existing response cache. Request attempts are
checkpointed before sending; resume reuses a valid response or reports an
uncertain previous attempt instead of spending twice. A new explicit run can
retry failed or rejected responses once; a worker finishing before the first
poll cannot trigger an accidental second attempt. Candidate IDs are stable
across retries of the same draft, retaining the first response provenance.

These are verified transport/data-flow mechanisms, not evidence of live model
accuracy. See `SEMANTIC_MODEL_FLOW_2026-09-28.md` and
`SEMANTIC_UI_AUDIT_2026-09-28.md` for the contracts and actual limits.

## Independent supplied-graph measurements

The new fixture was frozen before testing the methods. It contains 12 pattern
families, six per development/validation split, with 76 candidate graphs and
228 task-specific judgments. The independent oracle enumerates small injective
node mappings and checks directed edge multiplicities; it does not import the
matcher. All 72 positive match witnesses were independently verified.

| Goal | Per-split positives found | Per-split negatives rejected | Ranking mAP, both splits |
|---|---:|---:|---:|
| Literal identity | 6/6 | 32/32 | 1.000 |
| Template containment | 12/12 | 26/26 | 1.000 |
| Structural analogy | 18/18 | 20/20 | 0.940 |

Exact verification decided all 228 cases correctly. Necessary-condition filters
retained every positive. Analogy ranking alone had MRR 0.917 and recall@3 0.944;
a hard negative could rank ahead of a valid match. Thus ranking remains a
candidate ordering step, followed by bounded exact verification. With a
one-state budget, 74 comparisons remained unknown and 154 were rejected;
there were no false definitive decisions. These are tiny graph measurements,
not extraction quality or deployment performance.

An independent native-schema fixture checked 16 Claim exports: both semantic
and structural projections preserved their selected restrictions and support
identity in 16/16 cases, with 36/36 contrast checks across the two modes.
Twenty observation-local byte spans and 14 raw-source hashes verified. An
unsafe topology control retained the copied source but reconstructed restrictions
and support identity in 0/16 cases. Keeping the source beside a lossy graph is
not equivalent to retaining its semantics in the comparison itself.

Full frozen inputs, method hashes, independent metrics and limits:
`loom/tests/fixtures/eval/independent_graph_native_v1/INITIAL_RESULTS.md`.
No hidden holdout key was accessed and no method was tuned against these labels.

## Actual native output: a useful negative result

A real native run over the three architecture documents, with LLM off and seed
priors disabled, produced 1002 observations, 60 entities and 252 extracted
claims. Generalization expanded this to 2182 claims, including 1861 absence
placeholders. Output volume is not a quality score.

The first source-backed diagnostic selected 24 of 238 active observed claims,
balanced by unit and subject, and compared all 552 directed pairs. Both strict
semantic and structural views rejected all 552 by necessary label conditions.
Both motif runs found 39 recurring neighborhoods, but **none contained a Claim
vertex**: they were provenance/context recurrence, not demonstrated argument
patterns. Shared entity provenance expanded observation context in 13 of 24
views, by up to 23 times direct support. The sample was mostly mention claims;
this bias is reported rather than interpreted as the entire corpus's structure.

This result motivates a separate assertion view and predicate-balanced sampling.
Evidence and provenance must remain recoverable, while the structural comparison
can expose which assertion/dependency relation it actually compares. Removing
those attributes must be an explicit projection with a reversible sidecar and
separate assessment checks. The original diagnostic stays frozen; follow-up
experiments use separate files and results.

Reproduction and data: `NATIVE_GRAPH_EXPERIMENT_2026-09-28.md` and
`results/native-graph-2026-09-28.json`. The read-only SQLite exporter uses one
transaction including committed WAL pages, requires an explicit run, and
fails on a row limit rather than returning a silently truncated graph.

## Validation of this increment

- New and existing research mechanism checks at the graph-flow checkpoint:
  **144/144**. Independent graph-oracle/metric guard checks: **12/12**.
- Targeted native configuration, knowledge, candidate query, model adapter,
  actual pipeline transport and C API checks: **50/50 cases, 570 assertions**.
  The model transport is local and synthetic; no provider quality score is claimed.
- Full native/compatibility/server/research CTest: **67/68**, 52.42 seconds.
  The one unchanged failure is catalog recall **13/45 = 0.289**, below the
  existing 0.55 gate. Precision remains 13/13, with zero selected traps or generic
  noise. The gate was not weakened. A final coverage-counter-only amendment is
  checked separately with the targeted model tests.
- ABI export compatibility, Python/C++ config compatibility and eight offline
  Python semantic-recovery tests pass after rebuilding the native binary.
- Web TypeScript/Vite build and isolated Chromium semantic-controls test pass;
  API responses were mocked, so this is a UI contract check rather than a real
  provider interaction. Lockfile unchanged.

The existing source-retention/branch/import work and its limitations remain in
`../CODEX_HANDOFF_2026-09-28.md`. This document adds the current round rather than
rewriting historical results.

## What remains before the intended thought workflow is complete

The new model adapter is a deliberately explicit first production connection.
Its current output is a flat, source-grounded relation or generalization among
already extracted entities. It cannot create expression entities or encode
arbitrary quantifier/operand/binder structure. A topic label and a chunk scope
are not conversation-level topic tracking or resolved cross-turn identity.
Candidate drafts are not yet an automatic canonical graph update.

Next experiments need typed occurrence graphs carried by existing
Entity/Claim/Assessment objects, independently scored extraction, explicit
scope/identity alternatives, and reversible graph updates. The live/background
legacy path still requires consolidation with the native knowledge adapter.
Research graph search and motif code are executable Python tools, not yet a
production web graph-search service. Lossy matching must never become proof,
source repetitions must not become independent evidence, and any performance
claim must keep extraction and supplied-graph results separate.
