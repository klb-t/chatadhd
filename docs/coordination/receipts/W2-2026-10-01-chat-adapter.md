# W2 — Explicit retrieval plans in the native chat request

- Base: `96ea1c727ee5bbba4ee056dfa15aa844d1102d22`, containing the published W1/W2 lanes.
- Branch: `gpt/adapter-w2-chat-2026-10-01`.
- Owned implementation: only `chat_context_request` in `loom/src/chat/chat_engine.cpp`.
- Dedicated tests: `loom/tests/test_chat_retrieval_plan.cpp`, suite `chat_retrieval_plan`.
- No `send`, selector, ActiveTaskSpec compiler, web, C ABI or global STATE edits.

## Contract

`ChatOptions::from_json` now admits the six existing ContextRequest extensions:
`plan`, `claim_targets`, `include_counter_evidence`, `candidate_channels`,
`candidate_scan_limit`, and `lexical_shadow`. It delegates their nested validation
to the canonical ContextRequest parser. Unknown request keys remain errors;
malformed extensions fail before a chat turn can be sent. This completes the
chat integration left open in the historical
[W2 native contract](../../research/CONTEXT_PLAN_RETRIEVAL_2026-09-30.md).

The caller supplies the retrieval plan explicitly. This adapter does not extract
theses from user text or from W1's ActiveTaskSpec, and does not compile another
instruction representation. W1 and W2 may be supplied separately or together.
`plan.source_ref` remains opaque, caller-declared provenance. Matching a bound
ActiveTaskSpec's product ID does not verify the plan's version, derivation,
currentness or acceptance. The two traces remain separate:

- `context_trace.active_task`: W1's independently checked native message bindings.
- `context_trace.knowledge_context_request`: resolved ContextRequest.
- `context_trace.knowledge_context.context_set.goal.params.plan_trace`: exact
  supplied plan and per-thesis selection/coverage. Each thesis's
  `selection_params.candidate_retrieval` records requested instrument results.

Missing or default extensions retain the existing recipe. Request-level explicit
claims and counter policy also work without a plan. Per-thesis counter policy
continues to default to `true` in the canonical selector; it does not inherit the
request-level default `false`. Scope, detail and item budget remain separate.
Built-in TF-IDF and lexical retrieval are offline; a named but uninstalled
instrument produces an `unavailable` result, not a provider call or a fabricated
score. Counter retrieval still follows recorded links only. Neither candidate
similarity nor structural coverage establishes support or truth.

## Verification handoff

Eight hand-authored synthetic test cases cover parsing/defaults, malformed and
ambiguous nested inputs, actual plan/counter/channel dispatch, independent
scope/detail, counters without a plan, unavailable instruments and W1/W2
coexistence. Runtime cases use the native builder and ScriptedTransport, check
the exact transmitted messages/hash against the returned and persisted trace,
and assert the number of provider requests. They do not measure answer quality.

The integration verifier owns the shared build and test run after all lanes
freeze. No separate full compilation or paid call is requested by this lane.
At the initial implementation checkpoint, execution results are pending; this
receipt must not be cited as a passed gate until the verifier supplies evidence.
The new test source passed a local C++20 `-fsyntax-only` check with `-Wall
-Wextra -Wpedantic -Wshadow -Werror`; `git diff --check` also passed. Neither check
executes a scenario or establishes a native regression result.

Set `LOOM_CHAT_RETRIEVAL_EVIDENCE_DIR` to a fresh directory for the native run.
The suite creates it, refuses to overwrite existing evidence files, and writes:

| File | Captured scenario |
| --- | --- |
| `plan_counter_channels.json` | Explicit plan, claim/premise/counter, offline TF-IDF, lexical shadow |
| `plan_scope_detail.json` | Different per-thesis scope and detail in one request |
| `counter_without_plan.json` | Counter policy disabled/enabled, two real bodies and traces |
| `unavailable_channel.json` | Uninstalled instrument, visible gap and one answer request |
| `independent_w1_w2.json` | Native W1 binding alongside an unverified plan version declaration |

These records contain the actual serialized request body, parsed body and both
returned/persisted traces, with synthetic provenance. They never include HTTP
authentication headers. Preserve native XML/stdout, the source hashes and the
binary hash alongside them; do not substitute later reruns for the first result.
