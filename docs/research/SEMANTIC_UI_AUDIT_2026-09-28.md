# Semantic model controls and candidate visibility

This audit follows the native `KnowledgeSemantic` candidate helper being added
on 2026-09-28. It inspects the existing React workbench, generic HTTP/JNI adapters,
C API query dispatch and knowledge store. It does not run a paid model or promote
model output into the canonical graph.

## Existing controls and gaps

| Need | Existing path | Gap before this change |
|---|---|---|
| Configure the separate semantic model | Settings → Semantic model; `getConfig` / `setConfigKey("semantic_model", value)` | Setting exists, but its label describes background analysis only. |
| Opt into model proposals for a knowledge run | `POST /api/knowledge/run`, generic `KnowledgeConfig.llm` accepts `off` / `auto` | `KnowledgeWorkbench.RunControls` hardcodes `llm: "off"`. |
| Bound model work | Generic `stage_params.extract` reaches the native extract stage | Workbench has no semantic request or input/output controls. Exact helper contract is being coordinated with its author. |
| Inspect run outcome | Native `RunResult.stages[].stats`; existing Analysis result JSON detail | New helper stats are carried intact, but users would need to inspect nested JSON. |
| See in-progress run status | HTTP `/api/knowledge/status`; JNI `knowledge_status` | Workbench adapter does not expose this method; Analyze only shows a pending indicator and cancel. |
| Inspect stored semantic candidates | Existing `loom_kb_candidates` table | No `what: candidates` branch in `loom_kb_query`, no candidate list in `KnowledgeStore`, no candidate view. |
| Preserve workbench choice | Add/duplicate/close coordinated views, each with independent filters | Existing design can accommodate an optional candidate view without replacing claims, graph or context panes. |

The native helper's proposals belong in existing `loom_kb_candidates` with
`kind: semantic_structure`, `payload.run_id`, source/model/prompt identity and
draft Claims. They are candidate interpretations, not observed Claims or validated
truth. A UI must preserve that distinction and must not render proposal-only
entities/relations as canonical stored knowledge.

## Implemented controls

1. Model use remains off by default. An explicit opt-in beside source analysis
   reads the configured model using the existing config API and explains where to
   change it. It does not change the default chat model or select a provider.
   Missing model and disabled global `semantic_analysis` are visible; the native
   engine remains responsible for reporting availability if the user runs anyway.
2. Eight request/input/output controls use the helper's parameter names and
   native ranges. They are sent under `stage_params.extract.semantic` only when
   enabled. Source selection, priors and full/selective options remain available.
3. The extract-stage summary shows semantic status, accepted proposal count,
   stored candidate-ID count, rejected drafts, omitted observations, skipped
   entries and actual request/cache/input counts after the run. Complete
   result JSON remains inspectable. A completed analysis does
   not imply model success or candidate promotion.
4. Root added candidate reads to the existing store/query path. The UI reuses
   `/api/knowledge/query` / `kb_query` with `what: candidates` and explicit
   `run`, `kind`, `limit`, `offset` filters and the returned pagination fields.
   An optional Candidate proposals pane defaults to `semantic_structure`, offers
   25/50/100-row pages and a freely editable kind filter. The shared inspector
   preserves the partial draft, exact source quotes, observation IDs, UTF-8 byte
   spans, source locators, unknowns, premises, model/prompt provenance and review
   state. It provides no promotion action. Claims and graph panes remain open.

Root approved `KnowledgeWorkbench.tsx`, the narrow query-result/type extension in
`api/knowledge.ts`, and the isolated web test. Native pipeline/helper edits and
candidate-query ownership remain separate. No additional authoritative data model,
HTTP route or exported C ABI symbol was introduced by the UI lane.

| `stage_params.extract.semantic` key | Default | Accepted UI range | Meaning |
|---|---:|---:|---|
| `max_requests` | 4 | 0–8 | Selected request chunks, including cache hits |
| `max_input_bytes` | 64000 | 0–256000 | Total prompt/context bytes |
| `max_output_tokens` | 1600 | 1–4096 | Output tokens per request |
| `max_observations` | 16 | 1–64 | Observations per chunk |
| `max_chunk_bytes` | 16000 | 1–64000 | Prompt/context bytes per chunk |
| `max_proposals` | 16 | 1–64 | Proposals per response |
| `max_response_bytes` | 128000 | 1–256000 | Response-size cap |
| `timeout_ms` | 30000 | 1–60000 | Timeout per request |

The browser requires integer values in these ranges; native validation remains
authoritative. Input counts include prompt/context overhead, not just source text.
Zero request or total-input budget permits no model work. These are work bounds,
not price estimates. Current settings are displayed as configuration, while the
result JSON records the helper's actual identity and limits.

## Verification and limits

Passed on 2026-09-28:

```bash
cd loom/web
npm run build
node e2e/semantic-controls.mjs
```

The production TypeScript/Vite build passed. The targeted Chromium test intercepts
every API request and rejects unexpected network requests. It checks default
`llm: off`, explicit `llm: auto`, all eight exact parameter values, rejection of an
out-of-range request count, missing-model messaging, partial-result/cache/omission
diagnostics, candidate pagination/run scoping, literal source quote rendering
(HTML remains text), provenance and retained graph/claim views. All requests are
mocked: no native run, source import, paid provider call or promotion occurred.

Full native build and native API tests remain the root agent's responsibility.
This establishes the UI request/response contract under mocks, not extraction
quality or correctness of draft Claims. There is no live knowledge status poll in
this change; pending analysis keeps its existing running/cancel controls, and
semantic counts are displayed when the synchronous run returns. That result lives
in component state; persisted candidates can be queried again after a reload,
while historical semantic-stage diagnostics still require the native task result.

The initially missing web dependencies were restored with the unchanged lockfile
using `npm ci --ignore-scripts --no-audit --no-fund`; Chromium was installed through
Playwright. No dependency manifest/lockfile change or native build was needed.
