# Independent review of native anchor assistance

Review date: 2026-09-28. Scope: `anchor_assist.py`, its tests, the accompanying
research note, and `inputs/native-anchor-assist-report.json`. This review used
only the two saved native development runs. It made no model/API calls, read no
private holdout or credentials, started no Actions, and changed none of the
implementation, inputs or original responses.

**Verdict: ready for checkpoint as an offline post-hoc diagnostic. No blocking
correctness or evidence-reporting issue was found.** This is not approval to
admit the resulting graphs: full graph-contract admission remains zero.

## Independently verified

- The existing seven boundary tests pass (`python -m unittest
  loom.tools.structure.test_anchor_assist -v`). They cover UTF-8, immutability,
  idempotence, repeated and overlapping quotes, absence of normalization,
  malformed/foreign spans, declared support locations, identity checks and
  duplicate-JSON refusal.
- A second replay against `live-structure-dev-v2` and `native-remainder-v1`
  produced a byte-for-byte identical report. Both reports are 193,231 bytes with
  SHA-256 `89e24453d6f17a773a3ab3aba8e11f3bb59be0054966c255b3e89f02c4bdb961`.
- All 20 original response files still match their saved ledger SHA-256 values.
  The replay validates ledger/request bindings and response hashes before
  reading the model's candidate. Output uses exclusive creation at a new path.
- Independent recursive comparison of every original parsed candidate with its
  derived candidate found changes only to `byte_start` and `byte_len` at audited
  support paths. Array lengths, object keys, graph identifiers, labels, quotes,
  semantic fields and all other values are preserved.
- Each of the 92 corrected spans was independently checked by searching the
  source **bytes**, including overlapping positions. Each quote has exactly one
  match, at the resulting byte start, with the resulting byte length. The other
  31 spans remain unchanged: 26 already exact and five malformed.
- Strict parsing is shared with the existing pilot parser, which rejects
  duplicate keys at every object depth. All six duplicate-key responses remain
  `invalid_json_unchanged`; assistance does not select a repaired interpretation.
- The assistance function receives only the source packet and the candidate,
  not development gold. Gold is used downstream for the unchanged scorer.
  Invalid local identifiers, predicates, scope encodings and literal forms are
  not repaired. No graph promotion or graph database write occurs.
- Current rule and implementation hashes match both the note and machine report.

## Interpretation of the result

The note keeps the relevant denominators visible: 32 planned requests, 22
attempts, 20 returned responses, and 14 strictly parsable candidates. Source
support passes for 12/14 parsable candidates after assistance (12/20 returned;
12/32 planned). Full graph-contract admission remains 0/32. Two uncertain and
ten untouched requests are retained without retry or invented outcomes.

The structural and semantic score columns remain zero because no candidate
passes the prerequisite graph validator. They do not establish that every
intended structure is semantically wrong; conditional accuracy among admitted
graphs is unmeasured. The note states this distinction, distinguishes exact
quote location from semantic support, and describes reported validation errors
as first failures rather than a complete defect inventory.

The method was designed after seeing development failure categories. The note
labels it post-hoc and does not claim a clean holdout, synonym invariance, a new
model experiment or superiority over Jev. Literal matching is used to locate
an unchanged quote supplied by the model; it is not used to choose a semantic
operation or classify a thought structure.

## Limits of this review

The replay and hashes establish reproducibility of the present files; this
review cannot independently prove the chronology of the author's pre-replay
rule freeze. That does not change the explicitly post-hoc interpretation.

The saved responses exercise exact and malformed spans, but do not exercise
absent or ambiguous quotes. Those branches were checked through the boundary
tests, including overlapping occurrences. The review does not turn those tests
into a measured claim about real-world coverage.

No implementation edit or further test expansion is required for this scoped
checkpoint. A future production integration or a simpler structural-output
compiler needs its own prospective protocol and validation; this review covers
only the present deterministic coordinate-assistance experiment.
