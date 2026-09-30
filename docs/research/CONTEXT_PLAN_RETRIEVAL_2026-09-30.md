# W2 — Plan-directed context and explicit candidate instruments

This is a retrieval projection of a caller-supplied product plan (R12/R25/R27/R28).
It does not extract instructions, compile ActiveTaskSpec, establish that a plan
is current, or infer that candidate text supports a proposition. Source bytes
and the existing knowledge schema are unchanged.

## Native request

`ContextRequest::from_json`, `ContextEngine::select/build` and the existing
`loom_context_build` C ABI accept the following additive fields. The existing
chat adapter's strict whitelist needs a separate W1/ROOT integration change.
This branch does not edit W1-owned chat code or the web interface.

```json
{
  "text": "Assemble evidence for the agreed product plan",
  "run": "kr_existing_run",
  "goal_type": "answer_question",
  "budget_tokens": 4000,
  "candidate_channels": [{"id": "tfidf", "limit": 50, "min_score": 0}],
  "candidate_scan_limit": 10000,
  "lexical_shadow": true,
  "plan": {
    "id": "product-plan-v3",
    "source_ref": {"active_task_spec_id": "caller-owned-id", "version": 3},
    "theses": [
      {"id": "scope", "text": "Explain the dependency structure",
       "targets": ["e_component"], "relation_hops": 2,
       "detail_resolution": "label", "require_counter_evidence": true},
      {"id": "evidence", "text": "Examine the original evidence for preservation",
       "claims": ["cl_preservation"], "relation_hops": 0,
       "detail_resolution": "raw", "budget_weight": 2,
       "require_counter_evidence": true}
    ]
  }
}
```

Without a plan, `claim_targets` names explicit claim anchors and
`include_counter_evidence` enables recorded counter-link retrieval. Both default
to empty/off. Candidate channels and lexical shadow default off. Existing
request-level relation/detail controls remain independent of the item budget.

Plan thesis IDs and text are mandatory. Unknown plan/thesis keys, duplicate IDs,
invalid resolutions, non-positive/non-finite weights and malformed controls are
errors. Missing targets, claims, scope and detail inherit the enclosing request;
an empty ID list clears inherited anchors. Null detail inherits. `source_ref` is
opaque caller provenance, not a verified binding to a conversation or permission
to act. W1 can derive this projection from its already-bound ActiveTaskSpec.

## Selection and diagnostics

Each thesis calls the existing native selector with its own scope/detail.
Weights apportion one global item budget in plan order; unused capacity passes
forward. A zero allocation stays unexecuted with a gap instead of triggering the
legacy default budget. Identical safe representations share an item and retain
per-thesis factors; different detail or missing-premise projections remain
distinct. Earlier theses are not retried with later leftover capacity.

Explicit anchors and counter-links are independent of the exploratory graph
radius. Counter retrieval follows **one recorded counter-link layer** from
direct candidates, including counter-claims and counter-observations. It does
not discover previously unrecorded contradictions. Missing links, filtered
evidence, lookup failures and budget omissions remain separate states. Required
premises are included or marked incomplete; Decision views count as their
underlying claim. Mandatory premise inclusion may exceptionally include a
filtered claim, recorded separately from its eligibility.

Every included/dropped item keeps its route and scoring factors. Duplicate
graph/project/decision discovery is merged before budgeting with a union of
premises and retained view metadata. Band capacities sum exactly to the item
budget; unused capacity cascades forward. **The budget counts rendered item
estimates only** (Unicode codepoints/4), excluding headings, coverage markers,
trace metadata and the rest of an eventual provider request.

Trace fields under `goal.params`:

| Field | Meaning |
| --- | --- |
| `plan_trace` | Exact plan, resolved run, per-thesis controls, allocation, membership and gaps |
| `claim_selection` | Explicit anchor eligibility and actual inclusion |
| `counter_evidence` | Recorded links, per-reference outcomes and limits of coverage |
| `candidate_retrieval` | Corpus bounds, instrument availability/scores/hits and lexical omissions |
| `retrieval_diagnostics` | Store-query caps/errors, filtered candidates, duplicate routes, missing premises and item-budget ledger |

Missing support candidates, linked counter-evidence and retrieval failures also
appear in rendered coverage diagnostics. Structural coverage is not a semantic
support verdict. Explicit plan/channel context IDs bind the request, resolved
run and selected result, including different representations.

## Instruments and shadow

Native built-ins are `tfidf` (the existing glossary-normalized VectorSpace) and
`lexical` (exact normalized token overlap). Both search full claim projections,
including all support quotes, before selection; they are not restricted to
graph-reachable candidates or the requested display resolution. TF-IDF is a
local lexical/glossary vector method, not a neural embedding or evidence of
general semantic accuracy.

`ContextEngine::set_candidate_channel` permits explicit native injection of an
instrument, including the existing VectorSpace/embedding adapter. No request
JSON discovers credentials, installs a provider or authorizes network calls.
The native caller owns authorization and resource accounting for an external
implementation. Unknown capabilities report `unavailable`; failures report
`error`; unusable query vectors report `unrepresentable`. Actual measured zero
scores remain distinct, and document vectors lacking representations have no
fabricated score.

Instruments retain their raw score/method. Selection combines discovery routes
using the maximum relevance signal; channel ranks supply a reciprocal-rank
signal independently of raw score scales. This is a retrieval heuristic, not
claim-confidence calibration. Evidence authority/freshness/confidence and the
existing diversity pass still apply. Result threshold and count are adjustable
and enforced at the engine boundary for injected instruments too.

The lexical shadow compares its candidates with graph plus non-lexical
instrument hits **before item selection**. Gaps are `undecided`; they are not
automatically labelled rescues or noise. Shadow execution has no selection
effect, while explicitly selecting the lexical channel can contribute
candidates. Instrument method, rather than its registration alias, determines
whether it belongs to the lexical comparison side.

All store queries remain bounded. Reaching a limit means possibly truncated,
not proven exhaustive. The new run-wide scan limit is configurable; graph/store
queries retain their existing caps with explicit diagnostics. Paging remains a
separate shared-storage change. Missing entity labels and failed scans remain
visible. No sealed catalog/graph validation or private archive was used here.

## Verification boundary

The W2 receipt records exact verified source SHA, commands and outcomes.
The synthetic DEV protocol was committed before measurement. Report candidate
coverage, selected precision/coverage and ranking separately. These tests do
not measure answer correctness, real-export performance or neural retrieval
quality. No new provider request or paid experiment is part of W2.
