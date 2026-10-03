# Configurability and integration audit — 2026-10-01

Audited source: `origin/gpt/night-development-2026-10-01`, commit
`33fb30a`, without modifying executable code. Line numbers below refer to this
commit. This is a source audit, not a new build, provider experiment or security
certification. New integration changes after this commit must be assessed as a
separate delta.

## Conclusions for Claude

The owner's configurability objection is substantiated. Goal typing, semantic
extraction, candidate-graph validation, archive processing and the historical
research runner contain implementation ceilings that a caller cannot raise.
The analysis-plan resource ledger is a **Python reference
implementation**, and its explicit resource quantities can be arbitrarily large
or `null` (no configured limit); its closed resource vocabulary is a different
limitation. No general execution path implementing the owner's **expected ×10
increase → confirmation** rule was found in the audited native, web or contract
runtime paths. Merely removing ceilings would not implement that rule.

Two reported wiring gaps are confirmed: production code does not call
`ContextEngine::type_goal_with_model`, and no production code installs
`make_vector_candidate_channel`. The graph claim needs a precise distinction:
`loom.graph_packet/1` and its diff/workflow implementation are Python; native
`loom.candidate_graph/1` validation and persisted semantic candidates already
exist, but are a different representation. Counter-evidence **is present** in
the current chat plan editor. Candidate-channel controls are absent there.

The inventory separates (A) owner-adjustable defaults, (B) hard resource or
product-policy restrictions, (C) representation/capability boundaries, and
(D) evidence/transaction invariants. Deleting all validation would conflate
these categories and destroy the stated meaning of accepted data. Preserve
unknown data, negotiate executable capabilities, and version semantics when
extending a contract.

## 1. Explicit ceilings and fixed execution choices

All paths below are repository-relative. “Hard” means this audited entry point
cannot raise the value through its current public configuration.

| Location / symbol | Current behavior | Classification and next change |
|---|---|---|
| `loom/src/context/context_engine.cpp:368` `type_goal_with_model`; `loom/include/loom/context_engine.h:96` `GoalTypingBudget` | Rejects requests > 1; input or response > **256,000 bytes**; output > **4,096 tokens**; timeout > **60,000 ms**. These are decimal bytes, not 256 KiB. | B: convert maxima to caller policy/preset. Keep finite/range validation and caller-owned accounting. A larger number alone must not imply a retry loop. |
| `loom/src/context/context_engine.cpp:412,443,448` | Calls only below heuristic confidence **0.55**, temperature **0**, one attempt with `retry_authorized=false`. | B/C: separate invocation criterion, model parameters, retry policy and total budget. Retain first response and uncertain-charge evidence. |
| `loom/src/extract/semantic.cpp:96` `read_limits` | Caller controls values only below: requests **8**, observations/chunk **64**, chunk **64,000 B**, total input **256,000 B**, output **4,096 tokens**, proposals **64**, response **256,000 B**, timeout **60,000 ms**. Defaults at lines 68–84 are lower. | B: move upper bounds to explicit resource policy; defaults are presets. |
| `loom/web/src/components/KnowledgeWorkbench.tsx:210` | Mirrors the semantic ceilings in form `budgetFields`; values above them fail before submission. | B: derive UI constraints from negotiated runtime capability/policy, not duplicated constants. |
| `loom/src/extract/semantic.cpp:39,142,312,323,548` | Response JSON nesting **128**; generic lists default **64** and strings **2,000 B**; predicate **128 B**, topic **300 B**; resume lists hard-code **8** attempted/empty replies and **512** candidate IDs; per-candidate draft counters **256 entities / 1,024 claims**. | B/C: raising request/proposal limits also needs checkpoint migration. Keep bounded parsing but make operational limits explicit and trace truncation/refusal. |
| `loom/src/extract/candidate_graph.cpp:153` | Vocabulary limits can only decrease native ceilings: packet **1,048,576 B**, bundle **262,144 B**, observations **128**, source **262,144 B**, entities **256**, claims **1,024**, support entries **16**, depth **32**. | B: caller policy currently masquerades as an open data limit. Replace fixed ceilings with supplied validated policy. |
| `loom/src/extract/candidate_graph.cpp:142,147`; `loom/tools/structure/candidate_graph.py:45,59` | Native predicate arity and conjunction membership capped at **64**; native/Python text capped at **4,096 characters**, native handles **128 B**, native JSON preflight depth **128** (`candidate_graph.cpp:51,79,85`); Python limit overrides can only lower `DEFAULT_LIMITS`. | B/C: retain cardinality semantics such as one negation body; make unbounded-operation arity/resource ceilings configurable. Frozen research measurements keep their original policies. |
| `loom/src/chat/chat_engine.cpp:60,451` | Text attachment contributes at most **15,000 characters**. Extension allowlists at lines 49–57 determine image/text treatment. | B/C: expose attachment interpretation and per-source context resolution; full raw attachment retention is separate from prompt inclusion. |
| `loom/src/chat/chat_engine.cpp:574` `configure_reasoning` | Model-name heuristics choose reasoning behavior; newer Claude effort presets force **5,000 / 15,000 / 30,000** reasoning tokens; unknown effort maps to 15,000. | B/C: use provider capability profiles plus explicit request overrides; this is not a universal reasoning contract. |
| `loom/src/chat/chat_engine.cpp:828,850` | Deep research sends web `max_results=10` and high context; all chat requests use **180 s** timeout. | B: make request controls configurable; actual provider support remains reported separately. |
| `loom/src/chat/chat_engine.cpp:823`; `loom/include/loom/context_engine.h:57,65,81`; `loom/include/loom/context_retrieval.h:21` | Chat output **4,096**, context **4,000**, hops **1**, candidate scan **10,000**, channel result count **50** are defaults; request/config overrides exist. | A: do not describe these as the same hard ceiling as goal typing. Native `int` range still applies. |
| `loom/src/context/context_engine.cpp:678,719,725,777,826,912`; `loom/include/loom/knowledge_store.h:117,125,133` | Context graph/slot queries inherit **10,000** per-query defaults; non-paginated principle/instance/decision/history queries use **1,000,000** fallback. `candidate_scan_limit` does not control these other scans. | B at the context entry point: add separate limits/pagination and retain existing cap-hit diagnostics. |
| `loom/src/kb/store.cpp:69,615` | Nonpositive query limits become **1,000,000**, not unlimited; some query methods do not expose a limit. | B/C: streaming/pagination plus explicit completeness status. |
| `loom/src/capi/capi_knowledge.cpp:54`; `loom/src/kb/candidates.cpp:10` | Candidate-list page must contain **1–1,000** rows. | B: page-size ceiling, not maximum corpus size; configurable transport policy or pagination contract. |
| `loom/src/graph/graph_memory.cpp:224`; `loom/src/search/selector.cpp:141` | Legacy context FTS seed query limit **10**, graph-context snippet **500 chars** (`graph_memory.cpp:109`); legacy TF-IDF vocabulary at most **5,000 features**; native legacy reindex scans **9,999 conversations** (`graph_engine.cpp:191`). | B: separate legacy channel preset and retrieval/scoring policy. |
| `loom/src/semantic/semantic_llm.cpp:82,117,134`; `loom/include/loom/semantic_llm.h:52` | Legacy semantic model stops after **5 consecutive failures**, analyzes **3,000 chars**, requests timeout after **30 s**. | B: configurable circuit breaker, text-window policy and transport budget. |
| `loom/src/chat/batch_api.cpp:78` | Concurrent model batch worker pool uses at most **3** workers. | B: concurrency preset. Fixed request timeouts also occur at lines 46/98/170/241: **30/30/60/120 s**. |
| `loom/src/archive/pipeline.cpp:122` `ArchiveConfig::from_json` | Silently clamps passes to **1–20**, new terms to **0–100**, hits/term to **0–5,000**, synthesis rounds to **0–5**. | B: strongest configuration mismatch; supplied values are changed without explicit policy negotiation. |
| `loom/src/archive/pipeline.cpp:224,235,236,359,390,641,1023` | Item refinement processes at most **100** low-confidence items in batches of **25** snippets, **300** chars each, **1,500 tokens / 60 s**; FTS scans **5,000**; co-occurrence document groups > **300** skipped; at most **12** named themes plus `other`; automatic hit preset capped **25–200**. | B: represent each choice in archive policy and output provenance. Preserve original corpus. |
| `loom/src/archive/items.cpp:518,576`; `loom/src/archive/ingest_parse.cpp:299,549` | Skips relation candidates with sentences > **1,200 chars** or groups > **200**; code uses max **500** references/document and **200** symbols in its code digest and **12,000 B** comment budget (`ingest_parse.cpp:554`). | B: explicit extraction/digest policy and omitted-count diagnostics. |
| `loom/src/catalog/profile.cpp:58`; `loom/src/catalog/catalog_internal.h:94`; `loom/src/catalog/sketch.cpp:188,204` | Profile derives at most **4,000** eligible file stems; sketch/prose/code sampling cap **4 MiB**. | B: configurable indexing policy. Sketch records truncation; this is not a raw-file retention cap. |
| `loom/src/catalog/sketch.cpp:49,53`; `loom/src/catalog/score.cpp:280` | Bloom false-positive target clamped **1e-6–0.5**, hash count **1–16**; scoring takes min(requested passes, pack `max_expansion_passes`). | B/A: distinguish hard algorithmic parameters from already data-controlled pack limits. |
| `loom/src/import/export_common.cpp:21`; `loom/src/import/export_archive.cpp:23` | Export nesting > **512** refused; JSON member inline threshold **1,000,000 B**. | B/C: parser guard is hard; inline threshold changes storage projection, not necessarily source retention. Do not simply remove parser protection—use an iterative parser or a declared capability. |
| `loom/src/core/tasks.cpp:223` | Scheduler looks at only first **64** pending rows, then skips task kinds without a handler. | B/C: pagination or handler-filtered query; 64 unhandled rows can hide a runnable later task. This audit records the source risk, not a new reproduction. |
| `loom/cli/main.cpp:699,925`; `loom/src/extract/stage.cpp:168`; `loom/src/knowledge/engine.cpp:624`; `loom/src/resolve/stage.cpp:194` | CLI task query **1,000**, CLI catalog and extraction query **1,000,000**, engine task lookup **100**, retained judgment votes **5**. | B/C: distinguish page/lookup windows from evidence summaries; avoid claiming exhaustive results from truncated scans. |
| `loom/src/github/github_sync.cpp:169,180,302,314,375`; `loom/src/providers/providers.cpp:202`; `loom/src/media/media_providers.cpp:100,159,225`; `loom/src/worker/semantic_worker.cpp:49,128,187` | Operation-specific fixed network timeouts: GitHub **10/30 s**, provider listing **15 s**, media **60 s**, semantic worker **300/30/120 s**. | B: shared transport policy with per-operation presets. `HttpRequest.timeout_ms=30,000` in `net/http.h:48` itself is only a default. |

Not every numeric constant is an owner restriction. Scores/cosines confined to
probability or cosine domains, byte-span bounds, valid calendar fields, the
TCP port range, integer overflow checks, buffer copies, preview labels and
viewport size are representation or presentation rules. The native integer
ceiling **2,147,483,647** in request parsing and web controls is a current ABI
representation limit, not a cost policy; changing it needs end-to-end type and
serialization migration.

### Historical research runner and contract tools

These instruments are not the general application provider interface. Preserve
frozen versions for reproducibility; design a configurable successor instead
of retroactively changing what an old experiment accepted.

| Location | Restriction and interpretation |
|---|---|
| `loom/tools/structure/openrouter_runner.py:26,91` | Manifest input **16 MiB**, response **2 MiB**, request body **256 KiB**; JSON traversal **200,000** nodes/items, depth **64**, integers **128 bits**. Hard tool resource policy. |
| Same file, `validate_manifest:147–259` | At most **256** requests and pricing rows, **16,384** output tokens, **64** messages; model ID ≤ **200** chars, explicit model/provider, no routing aliases, streaming, enabled reasoning, nontext messages, unaccounted non-token charges or provider fallbacks. Closed body/message/provider field sets. These are historical instrument capability restrictions, not owner-wide bans. |
| Same file, `run:604–608` | Pricing timestamp must be at most **86,400 s** old and at most **300 s** in future before a new execution. This is a live-spending policy check, not a reason for offline fixture tests to depend on today's date. Pin/mock test clock; make future live freshness policy explicit. |
| Same file, `get_key:328,333` | Credential file/string max **4,096 B**; owner-only file permissions and key-account checks. These protect credential handling and the explicitly authorized dedicated-key programme, not a product spending ceiling. |
| `loom/tools/contracts/validate.py:30,61` | Contract CLI input **10 MiB**. Hard local parser guard; not a model context length. |
| `loom/tools/contracts/analysis_plan_ref.py:26,84,107,187` | Exactly ten named resource dimensions; exact accounting/measurement fields. Quantity has no arbitrary finite upper cap; resource `limit: null` is supported. New dimensions currently need a schema/runtime migration. |
| `loom/tools/contracts/workspace_ref.py:318` | `max_proposals` is supplied in workspace data; no universal upper ceiling found. Reaching it aborts atomically. This is a user-configured transaction budget, not a hardcoded product cap. |
| `loom/tools/structure/agentic_graph_v1/packet.py:40` | GraphPacket JSON resource policy has caller-configurable node/depth/string/integer budgets; do not conflate it with the lower-only CandidateGraph validator above. |

The dedicated **USD 2** programme is owner-authorized execution scope for these
experiments. It is not a required universal maximum for ChatADHD. The spending
reconciliation belongs to the integration handoff; this source audit made zero
provider requests and does not assert completeness of historical billing.

### Retained Python client paths

The older Python client has separate ceilings; native fixes do not change it.
Relevant source-level matches include `core/selector.py:86` (TF-IDF 5,000
features), `engine/chat_engine.py:39,113,226,289` (15,000 attachment characters,
2,000 retained reasoning characters, 180 s timeout),
`engine/semantic_llm.py:153,171` and `engine/batch_api.py:99` (3,000 analyzed
characters; 30 s single-call timeout), `engine/semantic_worker.py:139,232,251`
(batch threshold 500, 10,000 fetched messages, 3,000-char prompts),
`engine/graph_engine.py:202` (9,999 conversations in reindex),
`engine/graph_memory.py:108` (500-char snippets), `engine/importer.py:668`
(50,000-character fallback HTML message), `gui/memory_panel.py:27,28,222,230`
(200 files / 10,000 characters per imported memory file), and
`gui/graph_viz.py:72,126` (120 retained physics nodes). Network callers also
use per-function 10/15/30/60/120/180/300-second constants. These are inherited
paths; the audit did not execute the Kivy/Android client.

## 2. Unknown fields, closed vocabularies and prohibitions

### Native chat and retrieval

- `chat_engine.cpp:72–98` validates a whitelist of `knowledge_context` keys;
  unknown keys fail. `ChatOptions::from_json` at lines 297–357 rejects unknown
  top-level options. There is no generic provider-options extension object.
- `context_plan.cpp:23,120–164` rejects extra plan keys beyond
  `id/source_ref/theses`; each thesis accepts only
  `id/text/targets/claims/relation_hops/detail_resolution/require_counter_evidence/budget_weight`.
  `source_ref` is caller-declared provenance, not verified ActiveTaskSpec authority.
- `retrieval.cpp:233–260` accepts channel keys `id/limit/min_score` only.
  Channel IDs are **open strings**; a missing instrument reports unavailable.
  `context_request_ext.cpp:36` rejects duplicate channel IDs. This is identity
  ambiguity, not a reason to prevent multiple configured instances with unique IDs.
- Native `ContextRequest::from_json` and chat's wrapper have different strictness:
  the wrapper rejects unknown top-level context options. A migration must test
  both C ABI preview and chat submission, not assume one parser covers both.

Recommended extension: preserve an opaque namespaced `extensions` object at
all transport boundaries; explicit capability IDs and versioned typed options
control execution. Do not silently interpret an unknown execution option or
silently drop it. A rejected execution request can still retain the exact
submitted source for inspection.

### ActiveTaskSpec and acceptance

`loom/src/chat/active_task_spec.cpp:24` `object_shape` closes the root envelope,
product refs, scope, materializer, locators, source refs, statements, compiled
instruction, source maps and byte spans. The root and each statement have
opaque `extensions` objects. Root representation is fixed to `derived_product`.
Statement kinds are fixed to `goal, required_information, format, style,
prohibition, exception, alternative, open_issue, executor_context`; statuses
are `active, contested, superseded, rejected` (lines 170–214).

Further restrictions are explicit: at least one active/contested goal;
references must exist, supersession is acyclic, compiled byte spans must match
UTF-8 boundaries and may reference only active/contested statements
(lines 219–262). The compiler skips inactive statements and inserts the text
`contested — do not resolve without clarification` and
`executor only — not product content` (lines 274–278).

Classification: the **mandatory clarification wording is a fixed acceptance
policy** and should become configurable. A typed `prohibition` statement is
user content, not automatically a system-created ban. Referential integrity,
source-map validity and honest evidence status are invariants. New statement
kinds can be preserved opaquely before a compiler explicitly supports them.

`chat_engine.cpp:125–291` permits only the current existing conversation and
`branch_id == native:active`, exactly bound current native messages. Locator shape/UTF-8 spans are checked,
but this source audit found no native source-locator resolution to the bound
message; quote checking is substring-based. Do not claim locator-level evidence
verification from the hash binding alone. History mode is only `replace_refinement` or `append`.
Successors/replays must match the durable acceptance ancestry. This is a real
capability gap for imported/alternative branches and history strategies; it
is not evidence that those branches should be universally forbidden. Extend
source-binding adapters and versioned history policies while preserving the
acceptance transaction's identity, chronology and concurrent-input checks.

### AnalysisPlan reference contract

`analysis_plan_ref.py:172` loads `docs/contracts/analysis_plan.schema.json`.
The schema uses **28 closed-object locations**, listed below. Method, runtime,
domain identifiers and configuration payloads are open data; the executor
still requires a registered callback for a method/runtime pair. It cannot
execute an unknown capability merely because its ID is representable.

Resource dimensions, measurement statuses and accounting treatment names are
closed (`analysis_plan_ref.py:26–30,84–95`). Resource quantities and variation
cardinality are not hard-capped; variants are lazily generated (lines 129–154).
Dependencies must exist and be acyclic, selection indexes must be valid, shared
ledger limits must agree with the plan, and replaying a ledger under changed
identity/limits fails. These preserve determinism/accounting. Supporting loops
requires loop semantics; silently permitting dependency cycles does not do it.

### Workspace: two different implementations

`docs/contracts/workspace.schema.json` / `workspace_ref.py` is the general
Python reference UI-IR. It closes structural envelopes but keeps named
parameters, container kinds, renderers, queries, profile config and several
`extensions` objects open. Transform callbacks can be added, but replacing a
built-in under the same name is rejected (`workspace_ref.py:151–155`). `*` may
be a binding wildcard but cannot be an event scope (182–193). Event/write
objects are closed (296–303,330–335). Frozen nodes cannot be written directly;
conflicting proposals abort (315–336). These are explicit transaction/freeze
semantics; add policies to choose alternative behavior without obscuring the
current meaning. Caller-provided parameter min/max/enum constraints and
`max_proposals` are configurable.

The browser uses **`loom.workbench/2`**, not that general interpreter:
`loom/web/src/workspace/state.ts:3–27,91–116`. It has a fixed set of view kinds,
parameters and `adaptive/stacked/compact` profiles. Unsupported known values
fail; extra unknown properties are **projected away** by the return whitelist,
not generally rejected. That distinction matters for fidelity. Confidence/fade
are percentages (0–100), and positive page/node/budget limits have no small hard
ceiling in this parser. Preserving unknown fields across save/restore requires
a migration; an arbitrary unknown renderer also needs actual implementation.

## 3. Wiring map and concrete next steps

| Element | Actual callers / availability at `33fb30a` | Missing work |
|---|---|---|
| Model-assisted goal typing | Declaration and implementation plus `loom/tests/test_context_engine.cpp:229–389`; no production caller found. `type_goal()` calls offline `type_goal_impl(req,nullptr)` at `context_engine.cpp:364`; select/build use that path. | Add explicit goal-typing policy to live request execution, provider/cost ledger linkage, first-response references, and separate preview/execution behavior. Expose it through C ABI/server/client after removing arbitrary ceilings. Installed credentials alone should not silently opt a user into a new charge. |
| Vector candidate channel | Factory `retrieval.cpp:278`; calls only in `test_context_retrieval.cpp` and `test_context_tfidf_reuse.cpp`. Explicit injection API is `ContextEngine::set_candidate_channel` (`context_engine.h:110`). | Runtime registry/provider constructs the requested `resolve::VectorSpace` and installs named channels on every relevant engine instance. Wire configured embedding capability, reuse/cache lifecycle, resource accounting and capability-unavailable diagnostics. |
| Built-in local channels | Constructor installs `tfidf` (`context_engine.cpp:272`). `context_candidates.cpp:58` resolves injected channels and built-in lexical/TF-IDF. Native request can already carry candidate channels, scan limit and lexical shadow through chat parser. | Add typed fields to `web/src/api/types.ts` and controls to ChatView/plan/context workbench. Per-thesis channel policy is currently inherited from the parent context request, not independently editable in each thesis. |
| GraphPacket | Codec/diff/apply/history: `loom/tools/structure/agentic_graph_v1/packet.py`. Workflow CLI: `loom/tools/coordination/__main__.py:48` → `analysis_graph.py`. The latter explicitly creates artifacts without native graph writes. N3 in `docs/coordination/NIGHT_DEVELOPMENT_2026-10-01.md:16` is a plan, not proof of merged code. | Validate and map accepted selected records to native `KnowledgeStore` with an atomic write/receipt, source hashes, drift/replay rules and C ABI export test. Do not map arbitrary opaque GraphPacket records to stronger native evidence claims. Check integration's later delta before rebuilding a bridge. |
| Native candidate graph | `loom/include/loom/knowledge_candidate_graph.h:33`; `loom/src/extract/candidate_graph.cpp:502`; `extract/semantic.cpp:754,783,807`. Native validation and persisted proposals exist. | This is `loom.candidate_graph/1` + `loom.source_packet/1`, not GraphPacket diff persistence. State representation conversions and information loss explicitly. |
| Counter-evidence UI | `ContextPlanEditor.tsx:38–48` has claim IDs and “Follow recorded counter-evidence links”; `context/retrieval-plan.ts:48` emits `require_counter_evidence`; ChatView builds and sends plan at lines 139–159. Native plan sets `sub.include_counter_evidence` at `context_plan.cpp:200`. | Top-level standalone counter controls and channel controls are still absent. Existing control follows recorded links; it does not generate counterarguments or establish logical contradiction. Improve explanations/inspection without claiming argument discovery. |
| Inspection | Native traces expose candidates/counters/gaps via `context_diagnostics.cpp` and plan traces. The web context inspector can show generic JSON; there is no dedicated candidate-channel comparison view. | Add per-channel ranked hits, inclusion/exclusion reasons and counter-link coverage UI backed by the existing trace, with separate labels for missing evidence and negative evidence. |

Search boundary: production C++ (`loom/src`, `include`, `cli`, `server`),
React (`loom/web/src`), Python contract/coordination/structure runtime and selected
retained Python client paths. Symbol references were checked repository-wide.
This is an explicit source inventory, not a claim that static pattern searching
proves there are no other constants, branch-specific restrictions or dependency
limits anywhere in the repository. No sealed holdout or private export was read.

## 4. Suggested change order under Claude's control

1. Establish one versioned execution-policy structure: defaults, overrides,
   capabilities, projected consumption, user-confirmed growth and recorded
   realized consumption. The ×10 comparison needs a named baseline and resource
   dimensions; do not invent a money-only interpretation.
2. Migrate goal typing and semantic extraction ceilings together with their
   UI/checkpoint mirrors. Keep existing defaults, expose larger policy values,
   and exercise a >old-limit case plus a caller-chosen lower-limit case.
3. Wire native vector providers and UI candidate selection using existing
   channel interfaces. Do not substitute provider existence for authorization.
4. Complete/verify the explicit GraphPacket-to-native store adapter; then
   measure retrieval benefit using the same context budget and provenance.
5. Replace silent archive clamps and hidden query windows with configurable
   limits, pagination and completeness diagnostics. Preserve frozen instruments
   and results on their historical/archival lineage.
6. Version open extension preservation independently from execution semantics;
   preserve source bytes even when a capability cannot execute them.

No helper task or continuing edit claim is retained by this audit lane. Claude
owns follow-up choices after integration; this lane's only owned file is this
report and its work is complete when the integration picks its commit.

## Appendix — closed object locations in the three requested schemas

Paths are JSON Pointers to objects with `additionalProperties: false` or
`unevaluatedProperties: false` in the audited schema. Referenced shared schemas
may impose additional constraints; the native ActiveTaskSpec validator has the
explicit nested checks documented above.

**`docs/contracts/analysis_plan.schema.json` (28)**

```text
/
/properties/semantic_contract
/properties/sources/items
/properties/resource_limits
/properties/resource_limits/properties/money_usd
/properties/resource_limits/properties/cpu_seconds
/properties/resource_limits/properties/gpu_seconds
/properties/resource_limits/properties/input_tokens
/properties/resource_limits/properties/output_tokens
/properties/resource_limits/properties/calls
/properties/resource_limits/properties/agents
/properties/resource_limits/properties/wall_seconds
/properties/resource_limits/properties/memory_bytes
/properties/resource_limits/properties/storage_bytes
/$defs/axis/oneOf/0
/$defs/axis/oneOf/1
/$defs/axis/oneOf/1/properties/range
/$defs/selection/oneOf/0
/$defs/selection/oneOf/1
/$defs/method
/$defs/method/properties/runtime
/$defs/method/properties/roles/items
/$defs/method/properties/reservation
/$defs/measurement_policy
/$defs/source_binding/oneOf/0
/$defs/source_binding/oneOf/1
/$defs/source_binding/oneOf/2
/$defs/source_binding/oneOf/3
```

**`docs/contracts/active_task_spec.schema.json` (6)**

```text
/
/properties/product_ref
/properties/previous_product_ref/anyOf/0
/properties/statements/items
/properties/compiled_instruction
/properties/compiled_instruction/properties/source_map/items
```

**`docs/contracts/workspace.schema.json` (10)**

```text
/
/properties/limits
/$defs/parameter
/$defs/parameter/properties/constraints
/$defs/endpoint
/$defs/profile
/$defs/binding
/$defs/binding/properties/transform
/$defs/container
/$defs/view
```
