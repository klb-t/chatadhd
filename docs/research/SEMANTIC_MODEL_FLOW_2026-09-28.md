# Native semantic-model proposal flow

This increment connects the independently configured semantic model to the
native knowledge extraction pipeline, using the existing candidate queue.
It preserves `Claim + Assessment` as the knowledge authority. Model output is
an unreviewed source interpretation until a separate, explicit promotion path
validates and accepts it. No new canonical thought store, truth schema or
logical proof checker is introduced.

The production-path audit and the baseline defects are in
`GRAPH_NATIVE_AUDIT_2026-09-28.md`. The earlier `llm:off` results remain useful
deterministic extraction baselines; they are not a measurement of configured
model quality.

## Implemented helper contract

Files:

- `loom/include/loom/knowledge_semantic.h`
- `loom/src/extract/semantic.cpp`
- `loom/tests/test_knowledge_semantic.cpp`

The public C++ helper API, without a public ABI addition, is:

```cpp
Json semantic_fingerprint(Runtime& rt, const Json& extract_params,
                          std::string_view llm_mode);
Result<Json> propose_semantics(knowledge::StageContext& ctx,
    const std::vector<model::Observation>& observations,
    const std::vector<model::Entity>& entities,
    const std::vector<model::Claim>& claims);
```

The parent integration owns the call after normal native extraction has
written observations/entities/claims, incorporates the returned `output`
hash into the extract output, and stamps a runtime semantic identity into
`stage_params.extract._semantic_identity` before run/task fingerprinting.
The helper rejects a mismatch before any model call. This prevents a resumed
run from silently using a different model or prompt policy. The current
runtime key is captured for the request series; it is never persisted or
hashed into the knowledge fingerprint.

The optional path requires all of:

1. Knowledge-run `llm` is explicitly `auto`.
2. Runtime `semantic_analysis` is enabled.
3. Runtime `semantic_model`, `base_url` and `api_key` are available.

There is no fallback to `default_model`. Disabled or missing configuration
returns `off` or `unavailable` with a reason and sends no request. The helper
uses the runtime's injected HTTP transport; all tests use ScriptedTransport.
No DB lock is held across an HTTP request.

## Input scope and coverage

Observations are grouped by unit, source, source member and branch attrs,
ordered by source `seq`, ordinal and ID. If a conversation node has no known
branch, it remains node-local rather than being combined with other unknown
branches. A group is then split by observation and byte budgets. Large
observations are reported as omitted rather than silently taking a prefix.
Source order remains intact inside each chunk.

The bounded chunk schedule visits the first and last chunks, then bisects
remaining ranges breadth-first. Selection chunks are sorted by source/member,
unit and source sequence before sampling; random conversation-node IDs do not
determine which chunk counts as late. This gives late source material an opportunity
under the default budget. It is a deterministic sampling baseline, not proof
that every late project or topic was detected. All omitted chunk observations
retain IDs and locators in `skipped`; `omitted_observations` and
`selected_chunks` make partial coverage explicit. Future relevance-based
selection must be compared against this baseline independently.

Only grounded existing entity IDs are supplied: entities whose native
`attrs.observations` intersects the chunk, plus endpoints of existing claims
whose entire support lies in the chunk. Only those local claims may be named
as proposal premises. Rejected/superseded entities and claims, absent claims,
unlocated observations and ambiguous duplicate observation IDs cannot supply
invented grounding. Empty entity context is an explicit skipped chunk.

The prompt asks for local topic labels and explicit unknown scope. It does
not assume all statements in a conversation concern one project. A proposed
topic is still a candidate interpretation; no topic label merges source
groups or changes canonical graph focus automatically.

## Draft shape and validation

The model receives exact observation text and IDs, located source metadata,
local entities and local claims. The prompt identifies source text as data,
asks for strict JSON, and forbids adding external world knowledge. The response
envelope is:

```json
{
  "schema_version": 1,
  "proposals": [{
    "kind": "structure",
    "claim": {
      "subject": "<existing local entity ID>",
      "predicate": "<relation name>",
      "object": "",
      "value": "<literal source interpretation>",
      "qualifiers": {"extra": {
        "polarity": "positive",
        "assertion_context": "asserted",
        "topic": "<local topic or unknown>"
      }},
      "assessment": {
        "basis": {"support": [{
          "observation": "<local observation ID>",
          "quote": "<exact nonempty source substring>",
          "byte_start": 0,
          "byte_len": 1
        }]},
        "premises": {"claims": []}
      }
    },
    "unknowns": []
  }]
}
```

This is deliberately a **partial draft**, not valid canonical Claim JSON.
There is no generated claim ID, evidence class, calibrated confidence or
Expected Property. The illustrative quote and offsets above are placeholders,
not a valid support span. `kind` is `structure` or `generalization`; calling a
proposal a generalization does not establish an induction or universal rule.

Validation checks:

- Subject/object IDs must exist in the supplied local entity set; object XOR
  a non-null literal is required. Literal arrays/objects are rejected because
  nested expression semantics are not implemented here.
- A bounded nonempty predicate is required. Its semantic correctness remains
  unreviewed; being a well-formed string is not a truth check.
- Polarity must be `positive`, `negative` or `unknown`; assertion context must
  be `asserted`, `hypothetical`, `quoted` or `unknown`. Topic is explicit.
  Missing polarity/context is rejected, not filled by guessing.
- Every proposal needs one or more exact, nonempty source spans. Offsets are
  UTF-8 bytes within the Observation text, with bounds and UTF-8 boundaries
  checked. Quotes must equal those bytes. The helper copies the authoritative
  Observation locator and a text hash into persisted support.
- Premise IDs must be supplied local claims. References to another chunk,
  branch, missing entity or invented observation are rejected.
- Unknown fields and malformed envelopes are rejected. Proposal count and
  response byte limits apply; nested binders, quantified inference, proof
  replay, new entity creation and claim promotion are unsupported.

Exact quotation verifies that a span exists in the supplied observation. It
does **not** prove that the proposed relation follows from the quote, that its
polarity was interpreted correctly, or that the source assertion is true.
Those remain explicit evaluation/review questions. Original-source integrity
continues to depend on native source/observation provenance; observation-local
offsets are never misrepresented as offsets in an escaped JSON source file.

## Existing-table persistence and cache

Accepted drafts go to `loom_kb_candidates`, whose schema already existed.
Each row has:

- `kind: semantic_structure`, `status: candidate`.
- `payload`: run ID, unit, group, chunk ID, normalized proposal and provenance.
- Provenance: selected model/provider, exact request identity hash, response
  hash and helper method version.
- `support`: normalized exact spans with copied locators.
- `eval`: exact-observation grounding, review pending, logical semantics
  unvalidated, promoted false.

The helper adds `qualifiers.scope` equal to the source chunk ID and
`qualifiers.extra.semantic_status: unreviewed_source_interpretation`. It does
not label all constituent statements true. Candidate IDs include run and unit
identity; identical requests reused across runs still create separately
traceable run candidates. Candidate identity hashes the normalized draft and
request provenance, excluding the raw response hash. Retrying the same accepted
draft inside a changed partial response does not duplicate it. Inserts preserve
the first response provenance, existing candidate row and status. A later
different normalized interpretation remains a separate candidate. The returned
`candidate_ids` and `accepted` describe unique candidates selected by this
invocation/resume, while an all-run candidate query may also show earlier
attempts and interpretations. Those historical rows are not deleted merely
because a later response omits them.
Canonical claims, observations and owner judgements are never deleted or
updated by this helper. Promotion is intentionally absent.

`loom_kb_llm_cache` stores the raw model-content response only after the whole
response passes the bounded schema and grounding checks. Request identity
includes exact input, provider, model, prompt and relevant decoding/validation
policy, including the response-byte cap. Cached content is also byte-checked
before parsing. The cache is independent of run ID so an identical source request can
be reused without a second charge. Responses with transport/HTTP/schema errors
or any rejected proposal are not cached; accepted members of a partially
rejected response may still remain reviewable candidates.

Selected-chunk budgets count cache hits as well as new calls. This makes
replay coverage stable: caching the first chunk cannot silently permit another
chunk beyond the original selected budget on the next run. Operational request
and cache-hit counters are excluded from the semantic output hash.

Before each HTTP request the helper persists a semantic checkpoint containing
the non-secret identity, attempted prompt hashes, requests/input bytes spent
and accepted candidate IDs. Pause/crash resume validates that checkpoint and
reuses valid cached responses. A previously attempted prompt with no validated
cache is reported as `previous_attempt_uncached`, with `retry_required:true`;
it is not automatically sent again. A fresh explicit task attempt may retry
it within a new task budget. This deliberately favors a visible incomplete
analysis over spending twice after an uncertain request outcome.

The response sink checks cancellation, and the helper checks again after HTTP
and before persistence. Cancelling during the last request therefore cannot
silently persist its candidates and return completed. A request already sent
may have incurred provider cost, which is why its attempt remains checkpointed
even when no response is accepted. Earlier validated proposals/cache entries
remain intact.

The stage-level task cache also needs to allow a fresh *explicit* retry after
failed model responses. A completed deterministic extraction must not turn a
temporary provider failure into a permanent cached semantic success. The
helper reports `failed` separately so orchestration can make that decision
without automatic repeated provider calls. The integration owner maintains
this task-cache policy separately from response caching.

## Limits and result shape

Overrides live at `stage_params.extract.semantic`; unknown keys, non-integers
and out-of-range values are errors. The hard bounds limit accidental work even
when a UI supplies a larger number.

| Key | Default | Allowed range |
|---|---:|---:|
| `max_requests` | 4 | 0–8 selected chunks |
| `max_observations` | 16 | 1–64 per chunk |
| `max_chunk_bytes` | 16000 | 1–64000 prompt plus input bytes |
| `max_input_bytes` | 64000 | 0–256000 selected prompt/input bytes |
| `max_output_tokens` | 1600 | 1–4096 per response request |
| `max_proposals` | 16 | 1–64 per response |
| `max_response_bytes` | 128000 | 1–256000 transport response bytes |
| `timeout_ms` | 30000 | 1–60000 |

Returned stats include `status`, `reason` when unavailable, `candidate_ids`,
`requests`, `cache_hits`, `accepted`, `rejected`, `failed`, `input_bytes`,
`chunks`, `omitted_observations`, `selected_chunks`, `skipped`, `rejections`,
`failed_chunk_observations`, `requests_spent`, `input_bytes_spent`,
`retry_required`, non-secret `identity`
and deterministic `output`. Status values are `off`,
`unavailable`, `completed`, `partial`, `failed`, `budget_exhausted`.
`completed` means all selected bounded calls passed the adapter's checks; it
does not mean all natural-language semantics were extracted or proved.
Stage stats contain candidate IDs, not canonicalized draft claims. The
candidate-query/UI integration should show the pending status and exact
evidence alongside the draft.
`omitted_observations` counts input skipped before response evaluation;
`failed_chunk_observations` separately counts observations in selected chunks
with transport/schema/cache failures or a blocked uncached previous attempt.
A selected chunk is not automatically a successfully analyzed chunk.

The knowledge fingerprint includes an API-key-availability boolean so adding
a missing key permits a different run identity; it never contains the key or
a digest of the key. Model/provider/toggle and prompt/chunker/schema/limit
changes also affect the fingerprint. A separately configured default chat
model cannot change the selected semantic model.

## Validation boundaries

Thirteen dedicated tests use a ScriptedTransport supplied at Runtime construction,
whose unmatched requests fail locally. They cover selected model and actual
request shape, no HTTP while off/unavailable, exact quote and UTF-8 validation,
missing polarity and dangling references, branch/chunk separation, candidates
without claim mutation, cross-run response reuse, stable cached replay,
failure without cache or claim deletion, budget/response/proposal limits,
identity mismatch, and late-source chunk selection with located omissions.
The review regressions additionally cover misordered node IDs, stable IDs and
first provenance after a partial-response retry, stricter response caps after
a prior cache, cancellation before/after response delivery, and budget-safe
pause/resume.

At the time of authoring this note the tests were saved and `git diff --check`
was clean; native build/test execution is owned by the coordinating agent and
recorded in the round's results/handoff. Scripted integration is not a model
quality measurement. No live provider was called to produce a quality score,
and no independent validation labels or holdout key were used to tune the
adapter.

The first increment is intentionally bounded: it can propose relations and
generalizations among already grounded entities, retain ambiguous readings and
show where analysis stopped. Full compositional thought graphs, validated
quantifier binding and automatic promotion require additional shared data
contracts and separate evaluation. Graph-native projection/search can already
operate on existing assessed knowledge; these candidate drafts must not be
silently mixed into its asserted input.
