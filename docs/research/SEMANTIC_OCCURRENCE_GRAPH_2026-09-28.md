# Optional native occurrence-graph proposals

The separately configured semantic model can now propose compositional graph
drafts in the native knowledge pipeline. `relation_v1` remains the default;
`occurrence_graph_v1` is an explicit experimental option. The workbench exposes
the choice beside model opt-in and the existing request limits. It keeps the
current panes open and presents graph candidates with source quotes, local
entities, Claim drafts, roots, coverage, unknowns and validation details.

This connects the earlier research carrier to the application. It does not show
that a particular cheap model extracts correct structures, promote its drafts to
truth, or replace the existing knowledge graph with another authoritative store.
The frame adapter and temporal context-delta experiments remain separate offline
tools; this production increment accepts the direct occurrence-graph contract.

## Selecting the mode

Use the existing semantic-model setting and provider configuration, then enable
model proposals in the knowledge workbench and choose the experimental occurrence
graph representation. Native/API callers can provide:

```json
{
  "llm": "auto",
  "stage_params": {
    "extract": {
      "semantic": {
        "representation": "occurrence_graph_v1",
        "max_requests": 4,
        "max_output_tokens": 1600
      }
    }
  }
}
```

The adapter uses `semantic_model`, not `default_model`. Missing configuration
produces an explicit unavailable result. Existing completed regex analyses are
not automatically charged or reprocessed. Representation is validated separately
from the eight integer limits. No default or hard budget was raised:

| Limit | Default | Maximum |
| --- | ---: | ---: |
| Selected requests/chunks | 4 | 8 |
| Observations per chunk | 16 | 64 |
| Prompt plus input bytes per chunk | 16,000 | 64,000 |
| Total prompt plus input bytes | 64,000 | 256,000 |
| Output tokens per request | 1,600 | 4,096 |
| Proposals/bundles per response | 16 | 64 |
| HTTP/cached response bytes | 128,000 | 256,000 |
| Request timeout, ms | 30,000 | 60,000 |

Full source records and graph bundles can exceed small budgets. Such work is
reported as omitted, truncated, rejected or unresolved; the adapter does not
silently enlarge the budget or strip Claim Assessments. Mechanical JSON sizes in
the independent pilot do not establish tokenizer counts or provider costs.

## Source packet and request identity

Observations remain grouped by unit, source, archive member and branch. An
unknown conversation branch is isolated by node. Bounded selection visits the
beginning and end, then bisects remaining intervals. It gives late material an
opportunity but is not a learned topic-boundary detector.

Graph mode sends a `loom.source_packet/1` containing full selected native
Observations, Entities and Claims. An existing Claim is selected only when all
its direct support Observations belong to the chunk. An Entity is selected as
an endpoint or by an overlapping source reference. Original Claim Assessments
and Entity evidence/status fields remain intact. Known external metadata and
dependency references are inventoried separately; opaque metadata is not claimed
to be chronologically verified or to supply new source evidence. Graph proposals
can introduce local occurrences even when extraction found no existing Entities.

The user message contains `{packet_hash, source_packet}`. `snapshot_id` is derived
before insertion into the packet; `packet_hash` then hashes the complete packet
using native `loom::json::canonical`. The model must return:

```json
{
  "schema_version": 2,
  "packet_hash": "exact request packet hash",
  "bundles": []
}
```

Each nonempty entry follows `loom.candidate_graph/1`, documented in
`../../loom/tools/structure/CANDIDATE_GRAPH.md`. Different readings remain
separate bundles. The response packet hash is checked before accepting any
bundle; a matching snapshot label alone is insufficient. Source-packet validation
against the runtime policy occurs before spending a request.

## Shared runtime contract

`loom/data/policy/candidate_graph.json` supplies the five-operation experimental
policy. The pack loader and adapter use the same pure vocabulary validator.
The policy participates in pack manifests and hashes and is embedded for native
distribution. Overlays can lower supported limits; unsupported operation
semantics are rejected rather than silently treated as equivalent.

The native bundle validator checks exact UTF-8 support, typed and unique handles,
local subjects, allowed existing references, operand ports/count/order, scope
ownership and cycles, nearest visible same-symbol binders, shared expression DAG
depth, roots and located coverage. Predicate application, conditional, negation,
conjunction and quantification are supported. Missing or ambiguous structure
remains explicit; free variables and other unsupported semantics do not become
wildcard graph nodes.

Reports retain local-handle Entity/Claim drafts with partial Assessments. They do
not call canonical model parsers to invent confidence, evidence, origin or status
defaults, and they do not generate canonical IDs or a native comparison graph.
Ordinary bounded rejections retain their submitted inputs. Inputs rejected by
hard UTF-8/nesting/resource preflight are explicitly not copied into the report;
the caller retains ownership. Both HTTP wrapper and graph response JSON receive
a quote/escape-aware depth check before recursive parsing/copy helpers.

`complete_declared` describes declared source coverage, not measured extraction
recall. Exact quotes and well-formed scope do not establish correct meaning or
valid inference. Located unknown-only bundles and empty envelopes are abstentions.

## Candidate persistence, cache and resume

Accepted bundles enter the existing `semantic_structure` candidate queue with
`payload.representation: occurrence_graph_v1`. The payload retains the source
packet, response-bound packet hash, original bundle, validation report, graph
counts and model/prompt/response/policy provenance. Source support carries copied
locators and Observation text hashes. Review remains pending, logical semantics
unvalidated and `promoted: false`. Canonical Entity/Claim tables are unchanged.

The run/task fingerprint and response cache include representation, prompt/schema,
vocabulary, validator version, exact input, model/provider and relevant limits.
The semantic method version is 2 and the knowledge pipeline version is 7. Identity
comparison uses canonical bytes, so persistence-induced JSON key reordering does
not create a false configuration conflict.

Attempted request hashes and spent input bytes are checkpointed before HTTP.
Resume reuses a valid cache or reports an uncertain uncached attempt; it does not
spend again automatically. Only fully validated responses are cached. Provider
`length`/`max_tokens` finish reasons reject even syntactically complete JSON.
Missing finish reasons, including on cached content, remain unknown. Partial
accepted responses keep their candidates without caching the rejected response.

Graph counts are checkpointed per unique candidate: `accepted_bundles`,
`entity_drafts`, `claim_drafts`, and `abstentions` are separate from legacy proposal
counts. Duplicate responses and cache replay do not multiply these totals. The
existing cancellation and explicit-new-run retry rules remain in force.

## Verification

- Native validator and pack checks: 24 cases, 298 assertions passed.
- Native configuration, knowledge pipeline, candidate query, both semantic
  representations, legacy semantic recovery and C API checks: 62 cases,
  701 assertions passed. These include catalog → actual extraction → local
  scripted model transport → candidate readback, unchanged canonical Claims,
  repeat-run caching and isolation between representation modes.
- Independent native parity uses the frozen bilingual 32-case pilot and manual
  labels: 26 represented draft sets, four empty abstentions and two rejected
  dangling references. Source, partial draft, coverage and eligibility checks
  remain separate from agreement with Python. Four additional mechanical negative
  probes and wrapper bounds are reported separately; no native graph-projection
  or live model accuracy is inferred. Results are under
  `../../loom/tests/fixtures/eval/independent_candidate_graph_native_v1/`.
- Web TypeScript/Vite build and extended mocked Chromium controls/inspector test
  passed. This UI contract check does not contact a provider.
- Full application CTest status is recorded in `GRAPH_NATIVE_RESULTS_2026-09-28.md`.
  The preexisting catalog recall limitation remains separately diagnosed in
  `CATALOG_RECALL_DIAGNOSIS_2026-09-28.md`.

Peer findings and reviewed code hashes are in
`NATIVE_CANDIDATE_BOUNDARY_REVIEW_2026-09-28.md`. No external model requests were
made during these checks. Live source-to-structure quality, cross-turn context
selection, operation coverage beyond this contract, confidence calibration and
canonical promotion still require separate evaluation.
