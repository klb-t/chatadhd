# Conversation context pilot v1

Sixteen newly authored, short conversations: eight structural families with Polish
and English versions, six messages each. This directory contains evaluation inputs
and labels, not a new knowledge representation or a store. No model was called.

- `inputs.jsonl`: immutable source text, ordered message handles, supplied target
  topic descriptions and native `loom.source_packet/1` memory candidates.
- `gold.jsonl`: evaluator-only message/span labels, relation targets, source
  selections and explicit abstentions. Never include it in a model prompt.
- `split.json`: evaluator-only family partition. Eight development conversations
  and eight validation conversations; translations remain in the same split.
- `protocol.json`: exact scoring rules and operational limitations.
- `materialize.py`: executable input validation and causal request export using
  the existing `context_delta.prepare_context` contract.
- `validation.json`: input-validation result and frozen hashes, not model scores.

From the repository root:

```sh
python loom/tests/fixtures/eval/conversation_context_pilot_v1/materialize.py validate
python loom/tests/fixtures/eval/conversation_context_pilot_v1/materialize.py export CASE_ID 3
```

`CASE_ID` is an opaque `case_id` from the inputs, and `3` means the fourth target
message. The export includes only messages through that target, all eligible
memory candidates and fixed instructions. It never opens the labels or split.
It does not transmit anything or spend money. Unknown-time old records remain
explicitly unknown; any future-dated Claim, supporting Observation or endpoint
prevents that Claim from appearing until the dependency becomes available.

Do not send complete `inputs.jsonl`: it deliberately contains future messages and
future/unknown memory observations needed for prefix tests. Send only one `export`
result. The full native snapshot's hash remains opaque in the prepared packet for
existing delta compatibility, but its audit/undo sidecar and future bodies do not
enter the request.

The pilot tracks **supplied target topics**, allowing overlapping memberships. It
does not evaluate unrestricted topic discovery. Gold topic spans cover complete
single-discourse-unit messages; this is not a clause-boundary corpus. Labels are
synthetic author judgments, not independently adjudicated natural conversations.
There are only two ambiguous-reference opportunities and two correction edges;
per-phenomenon percentages therefore have very low precision. Report counts and
family/language breakdowns, and treat bilingual pairs as correlated.

Before inference, implement and independently verify a scorer against the frozen
protocol and response-shape checks. The existing live structure pilot's runner is
not automatically compatible with this request/answer shape. This contribution
prepares the next corpus and causal input path; it does not claim that the new live
experiment or its scorer has already run. Freeze model, prompt, scorer and decoding
settings before reading validation results. Preserve first responses and failures.
