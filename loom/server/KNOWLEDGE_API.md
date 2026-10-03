# Knowledge HTTP API

These routes expose the existing `loom.h` knowledge APIs. They use the same
bearer-token middleware as every other `/api/` route and return the native JSON
models: evidence class, origin, confidence, provenance and Expected Properties
are not flattened into an HTTP-specific graph.

Build with `cmake --preset dev -DLOOM_BUILD_SERVER=ON`, then build and run the
`server.smoke` CTest entry. That test covers an offline catalog → knowledge →
context flow, dry-run behavior, owner judgements, invalid payloads and auth.

| Method | Route | Request | Result |
| --- | --- | --- | --- |
| GET | `/api/knowledge/pack` | — | Pack manifest |
| GET | `/api/knowledge/policy` | `?name=goal_types` (or a pack policy/path) | Pack document |
| GET | `/api/knowledge/runs` | `?limit=50` | Knowledge runs, newest first |
| POST | `/api/knowledge/query` | `{"what":"claims","run":"kr_…","subject":"e_…"}` | `{"run","items"}` |
| POST | `/api/knowledge/run` | `KnowledgeConfig` | `RunResult` with `status`, `error`, `stages`, `summary` |
| GET | `/api/knowledge/status` | Optional `?task_id=…` (latest by default) | Native task/run status |
| POST | `/api/knowledge/cancel` | `{}` | `{"ok":true}`; requests a pause at the next checkpoint |
| POST | `/api/knowledge/judge` | `{"target_kind","target","verdict","reason"?,"payload"?,"replay_run"?}` | Stored append-only judgement, plus replay result when requested |
| POST | `/api/knowledge/materialize` | `{"kind","run"?,"instance"?}` | `{"kind","title","markdown","data","product"}` |
| POST | `/api/knowledge/predict` | `{"run"?,"cut"}` | `{"predictions":[…]}` |
| POST | `/api/context/build` | `{"text","run"?,"project"?,"targets"?,"budget_tokens"?,"goal_type"?,"lang"?}` | `{"context_set","text"}` |
| POST | `/api/catalog/scan` | `ScanConfig`, including `sources` | Scan statistics; does not import messages |
| POST | `/api/catalog/score` | `ScoreConfig` (or `{}`) | Score statistics with `run_id` |
| POST | `/api/catalog/select` | `{"run_id"?}` (latest score run by default) | `{"decisions":[…]}`; applies policy and owner overrides without importing |
| POST | `/api/catalog/query` | `UnitQuery` (or `{}`) | `CatalogUnit[]` |
| GET | `/api/catalog/units` | UnitQuery fields as query parameters | `CatalogUnit[]` |
| GET | `/api/catalog/units/:id` | — | Unit preview, score/reasons, verified snippets, cost estimate |
| POST | `/api/catalog/override` | `{"unit_id","action":"include|exclude|pin","reason"?}` | `{"decisions":[…]}` |
| POST | `/api/catalog/import` | `ImportOptions` | Import statistics and conversation IDs |

Knowledge-query filters are **top-level fields**, as in the C ABI, not a
`filters` subobject. `what` supports `entities`, `claims`, `instances`, `slots`,
`principles`, `operators`, `morphisms`, `decisions`, `forks`, `areas`,
`predictions`, `models`, `products`, `status_history`, and `stats`. The last
returns an object in `items`; the others return arrays. See `loom.h` for the
filters supported by each collection.

Catalog GET filters are `label`, `project`, `text`, `run_id`, `sort`, `limit`,
`offset`, and `selected`. The latter accepts `true`/`false` or `1`/`0`.
`limit` must be positive and `offset` nonnegative. Catalog previews nest the
complete `CatalogUnit` under `unit`, so its ID is `preview.unit.unit.id`.

A full knowledge run can be requested with:

```json
{
  "sources": ["/path/on/server/conversations.json"],
  "llm": "off",
  "stage_params": {
    "catalog": {
      "import": {"mode": "full"}
    }
  }
}
```

For the separate import endpoint, the corresponding body is
`{"mode":"full","dry_run":true}` for an estimate, then
`{"mode":"full","store_mode":"copy"}` to import. Selective import uses
`"mode":"selective"` and the current selection/owner overrides: scan → score →
select → review/override → import. Selection is an explicit operation and does
not import anything. Import options
and their current capability limits are documented in `include/loom/catalog.h`.

Source paths refer to the server's filesystem. As with the archive HTTP API,
`out_dir` on a knowledge run is a **plain output name**; files are written under
`<data_dir>/exports/knowledge/<name>`. Omit it or use `""` for native
artifacts-only behavior. The CLI/C ABI path contract is unchanged.

Run, scan, score and import requests are synchronous JSON requests. While a
knowledge run is active, its status and cancel endpoints can be called from a
second request. A disconnected client does not automatically cancel work;
inspect the persisted run/task status before retrying. There is no knowledge
SSE protocol in this version.

Malformed JSON and non-object bodies return HTTP 400 with
`{"error":{"code","message"}}`. Native API errors use the same envelope and
HTTP mapping as existing routes (for example 404 for `not_found`, 409 for
`busy`). An orchestrated run may return HTTP 200 with `status:"failed"` or
`status:"paused"`: clients **must inspect `status` and `error`**. HTTP success
means the RunResult was delivered, not that every stage succeeded.

Context items are returned in `stable` → `project` → `goal` order, with the
selected resolution, token estimate, selection factors, `why`, dependencies,
and dropped candidates. The token budget is enforced by the native engine.
