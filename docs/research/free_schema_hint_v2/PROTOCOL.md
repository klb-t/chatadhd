# Free source-only schema hint v2 — preregistered DEV syntax experiment

Registered 2026-09-30 after observing first free-source v1 failures and before any
v2 call/response. V1 first responses, costs, missing/invalid outputs and planned
denominators remain immutable. This is a diagnostic development intervention,
not independent validation, a retry or a silent repair of v1 outputs.

## Hypothesis and one controlled variable

The first source-only recipe sometimes emitted duplicate `source` JSON keys or
string evidence entries where the frozen compiler requires `{turn_id}` objects.
The hypothesis is that a literal generic output grammar example reduces these
contract failures. The sole model-input change is appending `HINT_SUFFIX` to the
unchanged `graph_free_extraction.SYSTEM`. No source text, queried node, gold
label, fixture term or observed response content enters that suffix.

The example uses symbolic `node1/node2`, `proposition1/proposition2`,
`assertion1/assertion2`, `speaker`, `turn1/turn2` and `time1/time2` placeholders.
It demonstrates one distinct `source` and `target` key per assertion and object
evidence arrays for assertions/events. Two assertion IDs make the illustrative
status-event references structurally complete. Values must be replaced from raw
source; the example is not source content, an instruction to create two records,
a correction policy, a proposed edge or an answer for any case. Inclusion,
attribution, polarity, chronology and event rules remain exactly the original
system's rules. A possible format-example bias is an experimental limitation;
record counts/false positives remain primary rather than assuming pure syntax
improvement also implies semantic improvement.

Every v2 user body is the identical original `source_payload(case)` with exactly
`id`, `source_id`, `turns`. There is no inventory, aliases, judgment query,
family label, gold, retrieval/index state or model-selected input. The same24
development conversations run once in the same order, two fixed12-request
batches. GPT`openai/gpt-4.1-mini`, provider`openai`, temperature0, JSON-object mode,
2048 output-token cap, no provider fallback, usage reporting and prompt0.4 /
completion1.6 USD/million price caps are unchanged. Only the system suffix and its
necessary byte-based reservation change. Outer experiment IDs and directories
are new and distinct from v1.

## Unchanged mechanism and integrity boundary

Original `graph_free_extraction.py` SHA-256 is
`5ccb81d038d80343a0407630fc4ce806b549e9f3c1a2c5798971092f5a70d91d`.
V2 aliases the unchanged `source_payload`, `compile_free`, `score_free`,
`align_nodes` and `batches` operations. It never mutates module globals to trick
the v1 loader. Own-node outputs remain model-proposed, source expressions
unverified, with all raw sources and exact full-turn provenance retained.

The new wrapper reconstructs the exact full expected v2 batch from frozen source
inputs and requires exact manifest metadata, body/row inventory, ordering,
reservation and corresponding outer experiment ID before touching the ledger.
Original/v1, shortened, reordered, foreign, altered or stale-recipe manifests
fail closed; unattempted planned outcomes cannot disappear by shortening a
manifest. It then uses the existing unchanged ledger validation, first-response
hash/size checks, hard raw↔ledger billing audit, public model/provider identity
check, non-BYOK requirement and free compiler. Billing discrepancies remain hard
errors, never swallowed into semantic unavailability. Duplicate JSON keys,
malformed evidence, invalid records, truncation/refusal and unavailable outputs
are not salvaged, dropped or retried.

First compiled own-node output and execution summary are written exclusively
**before** development gold loads for scoring. Existing strict alias alignment
(NFKC/casefold/whitespace/terminal sentence punctuation) and one-to-one source
assertion/status scoring stay unchanged. No threshold, normalization, semantic
matching, candidate coverage or gold change. Reference inventory enters evaluation
only after source-only output persistence; production graphs receive no writes.

## Measured criteria and preserved denominators

Primary: compiled availability **/24 planned conversations**, strict reference
node alignment precision/recall, strict source assertion precision/recall and
status-event precision/recall, compared with unchanged v1 first results on the
same24 planned IDs. Retain all absent/unavailable gold nodes/edges/events. The
existing DEV reference denominators are60 used atoms,60 source assertions and6
status events; unused control nodes stay separate. Do not equate these strict
representation-dependent counts to world truth or general semantic graph recall.

Report raw contract failure IDs and categories, all invalid/unmatched/ambiguous
own-node and edge records, family/language groups, known vs unknown usage cost,
unknown reservations, recorded request-timer sum/distribution and the explicit
limits of clause/evidence semantic adequacy. Never silently make a failed JSON
response into a success by parsing a later duplicate field or converting string
evidence to objects. Full-turn evidence proves bytes, not the relation semantics.

Keep if contract availability improves without obscuring semantic regressions;
investigate if scaffolding raises availability while strict precision/recall
worsens or record patterns copy the example; retain/reject the intervention if
it gives no useful improvement. V1 remains the first comparator regardless.
No outcome-driven tuning or another paid rerun is part of this preregistration.

## Authorization, freeze and execution handoff

The existing shared **nonreset USD2** session authorization and central budget
guard apply. Each exact batch reservation is≤USD0.10, at most12 requests.
`budget_usd:2` in the legacy manifest does not create another USD2 allowance.
Root runs each newly frozen manifest once with the existing guarded runner. This
adapter/protocol author makes zero API calls, reads no credential, and does not
inspect any sealed validation. A freeze binds the literal suffix/full prompt,
adapter/tests/protocol, immutable original helpers/public identity snapshot,
source-input manifest/hash and all prepared request/batch bytes before calls.

```sh
python -B -m loom.tools.structure.graph_free_schema_hint_v2 prepare \
  --output docs/research/free_schema_hint_v2/prepared
python -B -m loom.tools.structure.graph_free_schema_hint_v2 freeze \
  --prepared docs/research/free_schema_hint_v2/prepared
python -B -m loom.tools.structure.graph_free_schema_hint_v2 score \
  EXACT_NEW_V2_MANIFEST RUN_DIRECTORY --output NEW_FIRST_SCORE_DIRECTORY
```

Mechanism tests are synthetic/mock transport artifacts; they verify request
isolation, grammar shape, batch integrity, billing/identity, rejection semantics
and pre-gold persistence. They are not model-quality measurements. Root compares
the two disjoint12-case outputs only after independent exact-inventory checks.
