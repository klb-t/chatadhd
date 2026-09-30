# Knowledge context on the native chat path — 2026-09-30

[P] Implementation of the production gap in the 2026-09-29 options audit
O02/O03/O15, grounded in owner requirements R26/R28/R31. This is an opt-in
integration of the existing native ContextEngine, not an implementation of
ActiveTaskSpec refinement consolidation or a new context selector.

## Public request

The existing `loom_chat_ex` C ABI and `POST /api/chat` accept these additional
request fields. They are independent of interface/provider profiles.

```json
{
  "message": "Explain the current implementation and its unresolved questions.",
  "knowledge_context": {
    "run": "the-existing-knowledge-run-id",
    "project": "the-existing-project-entity-id",
    "targets": ["the-existing-project-entity-id"],
    "budget_tokens": 4000,
    "relation_hops": 2,
    "detail_resolution": "summary",
    "goal_type": "answer_question",
    "lang": "en"
  },
  "include_memory": true,
  "include_graph_memory": false,
  "include_history": true,
  "trace_context": true,
  "stream": false
}
```

- `knowledge_context` is absent/null by default: the existing chat composition
  remains in place. Its fields are those of `ContextRequest`; an omitted/empty
  `text` uses the current message as the selection query. A different query
  affects selection and does not rewrite the current user message.
- `run` may be omitted/empty, selecting the latest finished run. The concrete
  selected run is pinned before compilation and recorded. A nonexistent
  explicit run fails. Status filtering happens before the database limit, so
  more than fifty newer unfinished runs cannot hide an older finished one.
- `budget_tokens` defaults to 4000. The chat boundary accepts positive integers
  representable by the native request type. Zero/negative values fail explicitly;
  they do not silently activate ContextEngine's legacy 4000-token fallback.
  Omit/null `knowledge_context` to disable it.
- `relation_hops` is a nonnegative integer, default 1, controlling goal-band
  graph traversal distance. `detail_resolution` is absent/null by default
  (the goal type's representation policy), or `label`, `summary`, `full`,
  `raw`. These controls are independent of each other and the item budget;
  they do not remove mandatory premise tracking.
- `include_memory`, `include_graph_memory`, and `include_history` default to
  true. They independently control the existing memory tree, legacy graph
  selector, and active conversation history. `context_depth:0` also disables
  the legacy graph selector, independently of the knowledge selector.
- Knowledge context is appended as a system message after the existing
  system/memory/graph sections and before history/current input. Its internal
  stable/project/goal ordering and evidence markers come from ContextEngine.
- `stream` now observes request override → `config.stream` → true, and still
  requires a chunk callback. This repairs the pre-existing ignored Settings
  default without altering explicit overrides.

Nested knowledge options reject unknown keys and malformed types. Unknown
goal types, missing runs, a missing native adapter or compilation failure
produce an explicit error instead of sending a silently different legacy
request. As in the existing send lifecycle, the user's message may already
have been preserved before such an error.

## Trace and retention

One compilation produces both the outgoing message array and `context_trace`.
The trace is returned by native `ChatResult`, its JSON projection and the C ABI
SSE `done` event. It is also stored under the user message's
`metadata.context_trace` before the chat provider call, retaining it on provider
failure. It contains:

- `kind:"compiled_messages"`, `version:1`;
- the exact assembled `messages` array and SHA-256 of Loom's serialization of
  that array (not a hash of the entire HTTP body or a cross-language canonical
  JSON hash);
- selected source history message IDs and effective section controls;
- the resolved knowledge request, native ContextSet and exact rendered prompt.

When `trace_context` is omitted, tracing activates for knowledge opt-in or an
explicitly false `include_*` control. Explicit `trace_context:true` enables it for
any recipe; explicit `false` suppresses the trace without changing selection.
Legacy/default calls do not gain stored traces
or response fields. A stored trace is local user content, including any inline
attachments; no Authorization headers or transport credentials are included.
This trace is **not** the `RequestSnapshot` contract or evidence of provider
receipt. It intentionally describes compilation even if delivery fails.

The existing preview endpoint remains an independent invocation. Its content
may differ if the knowledge run or policy changes; only the trace produced by
the sending invocation is guaranteed to match that invocation's messages.

Reindexing previously replaced all message metadata. The accompanying narrow
GraphEngine repair merges only its semantic metadata under the database lock,
preserving stored context traces, import provenance and other metadata. Legacy
non-object JSON metadata is retained under `loom_preserved_metadata` before
enrichment; it is not discarded or allowed to prevent semantic indexing.

## Boundaries

ContextEngine's token budget applies to its selected items, using its existing
code-point estimate. It is not a whole-request budget and does not include all
rendering markers, system prompt, memory tree, legacy graph context, history,
attachments or provider tokenization. Per-plan-point scope/detail policies,
history budgeting, ActiveTaskSpec compilation, provider request/response
snapshots, and semantic quality evaluation remain separate work.

The adapter invokes `ContextEngine::build`, whose goal typing is offline.
Installing a semantic model/key does not authorize an additional goal-typing
model call. Existing separately enabled graph/semantic background analysis
retains its own behavior; this integration does not modify that subsystem's
permissions or spending policy.

## Verification

`loom/tests/test_chat_knowledge_context.cpp` adds deterministic native cases
covering default compatibility, offline compilation, exact trace/request
equality, independent section controls and immutable history, item budgeting,
strict option errors, unavailable/failed selection, failed delivery retention,
metadata survival through reindexing, and stream precedence. All provider calls
in these cases use ScriptedTransport; none use a live service. Final execution
results are recorded in the session's central validation artifacts and STATE.
