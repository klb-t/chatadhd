# Offline conversation-context scorer

`loom/tools/structure/conversation_pilot_score.py` implements an evaluator for the
frozen conversation-context corpus. It makes no network/model calls and does not
modify the knowledge graph, causal exports, labels or fixtures. This is scorer
verification, **not measured extraction or model accuracy**.

The scorer consumes one exported causal prefix plus evaluator-only labels per
registered query. It accepts first-response envelopes with exactly these fields:

```json
{
  "case_id": "opaque case ID",
  "message_id": "opaque target message ID",
  "transport_status": "completed",
  "raw_response": "{...the original model JSON text...}"
}
```

Allowed transport statuses are `completed`, `missing`, `truncated`, `refused` and
`error`. Keep original transport artifacts and response bytes outside this report;
the scorer records SHA-256 of the supplied UTF-8 text without modifying it. A live
adapter must verify this text against its preserved provider artifact. The scorer
does not independently authenticate provider identity, finish reasons, prices or
billing. Duplicate response envelopes are refused rather than selecting a retry.
Missing envelopes remain missing registered queries.

Run on captured development responses:

```sh
python loom/tools/structure/conversation_pilot_score.py \
  --split development --responses /path/to/first-responses.jsonl \
  --output /path/to/new-score.json
```

An empty response file reports 48 missing development queries. The output path
must be new. `--split validation` is available only for the evaluator stage after
freezing model/prompt/settings and capturing the validation outputs; no validation
results were inspected or used to tune extraction in this implementation.

## Implemented checks and metrics

- Exact output fields and array/object shapes; only supplied topic IDs, visible
  memory Claim IDs and exact target-Observation UTF-8 spans are allowed. Citations
  to an earlier visible message are rejected as non-target, while absent citations
  are reported as foreign or out-of-prefix. Duplicate JSON keys, extra fields,
  duplicate Claim selections and conflicting repeated history decisions fail.
- Overlapping topics remain separate. Repeated/overlapping byte intervals for one
  topic are unioned before byte precision/recall. Topic-message metrics and exact
  topic-set accuracy are reported separately.
- Exact onset/return metrics, each type separately, first-onset ordinal MAE with
  matched/missing/extra topic counts. Missing and extra onset events also remain
  false negatives/positives in the event metrics.
- Exact typed relations target the old Claim from the current message, with
  correction/contradiction/analogy confusion counts. Reversed field names and
  identity-merge relation types are refused.
- Claim selection and exact-set accuracy; support Observation expansion deduplicates
  repeated Assessment support and shared sources across selected Claims. Source
  groups are counted separately. Missing source-group values are not invented.
- Ambiguous/unresolved reference status, exact span, alternative-topic set and null
  selection. Explicit resolution or a topic membership overlapping the ambiguous
  gold span is an unsupported resolution. Report opportunity count, resolution
  error rate and actual abstention frequency, without treating empty memberships
  as abstention.
- Required historical decisions include missing answers as incorrect. Known-prior
  requires a strictly earlier timestamp on the Claim, every referenced endpoint
  and every support Observation; unknown or equal timestamps cannot establish it.
  Selection of Claims whose availability remains unknown is scored separately.
- Invalid IDs, out-of-prefix citations, non-target citations, fabricated quotes,
  identity merges, asserted unknown chronology and unsupported resolution are
  separate bounded diagnostics. Counts per query and relevant span/ID/reference
  denominators accompany them. These checks are not a general hallucination test.
- Every registered query remains in all-query denominators. Conditional results
  on successful outputs are visibly separate. Precision/recall/F1 with an undefined
  denominator are null with TP/FP/FN retained. Empty/empty exact sets can be correct
  only for a valid completed output. Breakdowns and macro F1 are by family/language;
  macro reports how many groups have defined F1.

## Explicit evaluation choices and remaining limits

A structurally invalid response fails the entire query and earns no semantic
credit, including otherwise valid entries. This conservative policy is stricter
than an entry-by-entry salvage evaluator; the report states the policy. Structural
diagnostics are collected where parsing permits. Unsupported-resolution diagnostics
inspect individually validated raw spans and topic selections even when the whole
output is invalid, including an ambiguous/unresolved decision that contradictorily
selects a topic. The diagnostic probe does not repair the retained/scored output;
that query still earns no semantic credit. Unparseable or unlocatable content
cannot establish a resolution diagnostic and remains a failed query.

A causal packet alone cannot determine whether an absent ID names an actual future
record or is fabricated. The combined foreign/out-of-prefix classification makes
that limit explicit and never reads future text to make a model-facing decision.

These are short synthetic supplied-topic conversations. This scorer does not
measure unrestricted topic discovery, long-context behavior, or real archives.
There are no confidence intervals; bilingual pairs are correlated. `mae_first_onset`
compares the earliest prediction per topic; all repeated onset events remain in
exact event FP counts. Byte-union implementation is sized for this short pilot,
not an unbounded archive.

A separate live adapter still needs frozen prompt/response schema, per-query
provider artifact binding, budgeted requests, result capture and model/provider
comparisons. The extraction pilot's existing adapter is not reused implicitly.

## Verification

```sh
python -m unittest discover -s loom/tools/structure \
  -p 'test_conversation_pilot_score.py' -v
```

**15 tests pass.** Tests cover overlapping topics, repeated interval union,
multibyte UTF-8 boundaries, fabricated/foreign/future citations, non-target spans,
relation direction/type, identity merge rejection, unknown endpoint and equal
support timestamps, failure denominators, null empty metrics, ambiguous-reference
alternatives, contradictory resolutions in invalid outputs, duplicate JSON/envelope
rejection and deduplicated source evidence.
A mechanical replay constructs answers from the 48 development labels to check
the evaluator wiring; its perfect result is not a model-quality measurement.
