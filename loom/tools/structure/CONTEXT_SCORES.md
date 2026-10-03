# Bounded offline context scoring

`context_scores.py` replays saved independent relevance probabilities over
existing Claims in a `context_delta.prepare_context(...)["packet"]` projection.
It makes **no provider calls**, does not select a model, and does not modify,
promote or delete graph data. Its author-written example is a mechanism demo,
not a Jev result or evidence of extraction quality.

## API

```python
request = make_score_request(packet, candidate_claim_ids, question=question)
report = replay_context_scores(
    packet, candidate_claim_ids, response,
    question=question, keep_threshold=0.8, drop_threshold=0.2,
)
comparison = compare_score_runs(left_report, right_report, mapping)
```

`question` contains exactly four nonblank strings:

```json
{
  "id": "context_relevance",
  "text": "Is this candidate useful for the current request?",
  "rubric_id": "context_relevance/1",
  "rubric": "Assess each candidate independently against the current request."
}
```

`make_score_request` returns `status: ready` with `packet_hash`, `question_hash`,
`request_hash`, `candidate_ids`, `question` and a copied `source_packet`.
The question hash covers all four fields; the request hash covers its schema,
packet hash, ordered candidate IDs and question hash. The packet uses the
existing context module's hash convention: SHA-256 of canonical JSON excluding
its own `packet_hash` field. A hash identifies supplied content, not source
truth or external authentication.

The scorer response envelope is:

```json
{
  "schema": "loom.context_scores_response/1",
  "packet_hash": "<request.packet_hash>",
  "request_hash": "<request.request_hash>",
  "question_hash": "<request.question_hash>",
  "rows": [
    {"candidate_id": "claim_a", "probability": 0.9},
    {"candidate_id": "claim_b", "probability": 0.85}
  ],
  "provider": {"kind": "saved_output", "model": "record-the-actual-version"}
}
```

`provider` is optional opaque JSON metadata. The rest of the envelope is closed.
Rows may be reordered; their identities must belong to the requested candidate
list. Candidates may belong to multiple topics, so their independent scores
are **never normalized across candidates**.

## Source and abstention boundaries

Only the packet itself is accepted. A complete preparation result, extra
`audit`/`retained_input` fields, or an unrelated `loom.source_packet/1` object
without the current context-packet shape is rejected. No audit sidecar is
searched or expanded. The caller must pass the bounded selected projection.

Validation checks the packet hash, supported context version, unique record
IDs, complete selected Claim/context correspondence, retained Assessment/body
consistency, exact UTF-8 current spans, text hashes, locators, direct support
dependency handles and explicit temporal metadata. The source selection is
supplied by the caller: replay cannot authenticate the unavailable original
snapshot, reconstruct excluded evidence, or prove the source entails a Claim.
Unknown times remain unknown. As with `prepare_context`, selected Observation
bodies remain complete; this is not causal text-prefix isolation.

At most 256 candidate Claims are accepted. Existing Claim membership in the
packet does not automatically make it a requested scoring candidate. Duplicate
or unknown candidate IDs in either the request or response reject the request
or whole response, respectively. A malformed row with no recoverable identity,
an envelope identity mismatch, an unsupported field, or invalid provider
metadata also rejects the response. When the request itself is valid, every
candidate stays present as `unknown/review` on whole-response rejection.

For an identifiable row, a missing score, extra row field, Boolean, string,
nonfinite number or value outside `[0,1]` produces **only that row's**
`score_status: unknown`, `probability: null`, `suggestion: review`. A missing
answer behaves the same way. Other valid scores are still usable. Invalid raw
scores are not copied into the result, so even NaN input yields valid JSON.

Thresholds are explicit; no default confidence threshold is implied:

| Condition | Suggestion |
| --- | --- |
| `p >= keep_threshold` | `keep` |
| `p <= drop_threshold` | `drop` |
| Between thresholds | `review` |
| Missing or invalid score | `review`, with `unknown` score status |

Require `0 <= drop_threshold < keep_threshold <= 1`, finite numbers excluding
Booleans. `drop` is a **context-priority suggestion**; the row and all source
data are retained. It never means delete, invalidate or demote the underlying
Claim. A low relevance score is not a low truth probability.

Every row copies its complete `context_claim` metadata and current span IDs;
the output retains the entire supplied source packet. `counts` separates
keep/review/drop from the number of unknown scores. `semantic_quality` remains
null, and `no_inference` / `no_persistence` remain true.

Preflight bounds object depth to 64, visited values to 100,000 and aggregate
string bytes to 4 MiB before recursive copying. Each collection is checked
before its members are pushed. CLI input is capped at 4 MiB and duplicate JSON
keys are rejected. These are prototype limits, not a new global Loom policy.

## Permutation comparison

`compare_score_runs` accepts two ready replay reports and an explicit complete
bijection `{left_candidate_id: right_candidate_id}`. Identity mappings handle
candidate-order permutations; a nonidentity mapping allows a caller-declared
renaming experiment. Both reports must use identical question/rubric hashes
and thresholds. Their packet hashes may differ when IDs or packet ordering
change; each report is independently checked against its own retained packet.

Comparison validates row coverage, source handles and score/threshold
consistency. It reports suggestion agreement, number of pairs with valid
scores on both sides, and mean/max absolute score drift over those pairs only.
Unknown pairs have null drift. Empty comparisons have null agreement, not a
fabricated perfect score. Two unknowns can agree on `review`; therefore always
read agreement beside `measured_pairs` and the original report counts.

The mapping is **caller-declared, not semantically verified**. A low drift
does not prove correct decisions, good calibration, true project affiliation,
or valid candidate correspondence. Score renaming/order experiments need
independent semantic labels before they can claim classification quality.

## CLI and reproducible example

From the repository root:

```sh
python loom/tools/structure/context_scores.py example > /tmp/context-scores-example.json
python loom/tools/structure/context_scores.py replay /tmp/context-scores-example.json
python -m unittest discover -s loom/tools/structure -p test_context_scores.py -v
```

The example emits a complete replay input with `packet`, `candidate_claim_ids`,
`response`, `question`, `keep_threshold` and `drop_threshold`. It contains two
authored probabilities, 0.9 and 0.85; both remain `keep`, demonstrating
overlapping relevance without forced competition. Provider metadata explicitly
identifies these as authored example scores.

For comparison, save a JSON object with `left`, `right` and `mapping` and run:

```sh
python loom/tools/structure/context_scores.py compare comparison.json
```

Use `-` instead of a path for stdin. JSON is written to stdout only. Exit 0
means a valid mechanical replay/comparison (including explicit abstentions),
while exit 2 means invalid input. No network, graph store or secret access is
part of this module.

The author suite currently has 21 tests. It checks source retention, request
binding, per-row and envelope failures, overlapping topics, threshold edges,
renaming/order comparison, unknown score handling and the real CLI paths.
These verify replay mechanics, not provider performance. See
`docs/research/GEMINI_JEV_AUDIT_2026-09-28.md` for the proposed independent
PL/EN live-evaluation protocol and its separate budget.
