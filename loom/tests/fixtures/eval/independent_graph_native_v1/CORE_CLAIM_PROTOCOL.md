# Independent native core Claim fixture protocol

The fixture `core_claim_cases.json` was independently authored from the native
model header and serializers, after API-only coordination with the projector
author. No projector implementation, method outcome, author test example,
existing independent case, `synthetic_dev` example, or real holdout key was read.
Repository instructions were read; the instruction to read research outcomes
was not followed because this task explicitly required a blind benchmark.

The fixture contains fictional source records authored for this benchmark.
The basin group is development (8 cases), and the gallery group is validation
(8 cases). Entire groups were assigned before method execution. Each group has
observed, quoted, possible, relation-negated, epistemically-negated, narrow-scope,
repeated-source, and independent-acquisition cases. There are 16 native Claims,
20 exported Observation records, 22 Support entries, 14 raw source records, and
18 predeclared relationships across the two groups.

## Frozen artifact

Full-file SHA256 of `core_claim_cases.json`:

`c81eb758cd52f9ee3cc98d2519df8c2c17f4c39029234b8aff614a35eb24d8c0`

The JSON `frozen_sha256` is a separate self-excluding canonical payload hash:

`719b19fb140ab83ce58bbe2230f69bc257a68a5eb9c3ca974710f11e9b11e2a6`

Its algorithm is SHA256 over compact sorted-key UTF-8 JSON, with the root
`frozen_sha256` field omitted and no terminal newline. The full-file hash includes
the formatted JSON, hash field, and final newline. Both were recorded before
any projection or scoring outcome. Preserve content, splits, native annotations,
and relationships after outcomes are exposed; report flaws instead of tuning.

## Native grounding and exact bytes

Each case exposes the actual API input at `case.export`. Claims use native
`subject`, `predicate`, `object`, `value`, `qualifiers`, and `assessment` fields.
All assessments are observed source statements and carry nonempty native
`basis.support`. Support observations, copied locators, quotes, and qualities
are explicit. Entities, Units, Observations, and Claims use native content-derived
ID recipes. Qualifier scopes refer to context Entities; a narrow context has an
explicit native `Entity.parent` pointing to its wider context.

Raw source strings are at the fixture root, outside the projector export. Source
IDs are `sha256:` plus the SHA256 of the exact UTF-8 raw string. Byte positions
and lengths measure encoded bytes, not Unicode code points. Deliberate CRLF,
leading spaces/tabs, Greek header characters, an em dash, and typographic quote
marks make character-offset or quote-normalization errors observable.

A source-native integrity check, without importing any projector or scoring
method, verified all 14 source hashes, 22/22 exact Support spans, Observation and
Support equality, required observed-assessment invariants, native ID recipes,
reference resolution, expected preservation values, support counts, disjoint
split groups, relationship references, and both fixture hashes. This was a
Python integrity check against the native definitions; no C++ build or native
serializer execution was performed.

## Oracle interpretation

`expected.preservation` maps JSON pointers into the source export to exact native
values. `expected.unique_source_counts` maps native Claim IDs to distinct source
counts. `expected.support_counts` separately names entries, unique observations,
and unique sources. `quote_spans` gives exact quotation bytes and their hashes.
`source_native_gold` states the authored semantic annotations independently of
any projected graph labels. The assertions and relationships are predeclared
oracle intent, not a method's self-reported score.

The quoted variant is an observed third-party utterance whose embedded proposition
is explicitly attributed in `qualifiers.extra`; it is not independent world
observation. The possible variant preserves an uncertain report and lower native
confidence. The relation-negated variant negates the relation. The epistemically
negated variant denies establishment of the proposition without thereby negating
the world relation. The scope variant narrows the Claim's explicit context.
The core has no dedicated quotation/negation/modality fields: these are exact,
explicit open-map annotations in `qualifiers.extra`, not a claim that the
projector can infer those meanings from natural language.

Repeated-source cases have three Support entries: two distinct located
Observations in one source, plus one repeated Support entry. Independent-source
cases have two Support entries from two independently authored acquisitions.
The baseline and both support-count variants share the exact native Claim ID and
content and retain the same confidence. Source cardinality is a provenance count;
two different hashes alone do not establish epistemic independence in general.

Retaining an untouched source export demonstrates source preservation only. It
does not establish that the projected graph retains the relevant distinction.
Projection evaluation must inspect the actual structure or an independently
specified projection contract, and keep that result separate from source-copy
integrity. Scores, round trips, source cardinality, and observed evidence must
not be promoted into proof of the embedded proposition. The task's parent owns
all projector execution, scoring, and oracle runner integration.
