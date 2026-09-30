# Active source metadata projection — 2026-09-30

`loom/tools/structure/retrieval_exploration_v1/source_projection.py` makes the
existing native actor projection usable on a raw source and observation array
or native snapshot. It is an offline research adapter; it does not alter the
native graph, infer relations or identify real people.

## Why this increment

Review at repository base `fafc77f` reproduced two false positive bindings in the
historical v2 mechanism on new handcrafted fixtures:

1. `/mapping/turn~2a/message/content/parts/0` resolved a literal `turn~2a`
   object key even though `~2` is not a JSON Pointer escape.
2. Raw `"name": "Bela", "name": "Aria"` was decoded with last-key-wins
   semantics and reported actor `Aria` as unambiguously source-recorded.

These findings concern source reference integrity, not model or retrieval
quality. The previous v1/v2 runners, frozen hashes, first failures and receipts
are untouched. They remain historical instruments for reproducing published
results. New callers use the active adapter via its Python API or module CLI.

## Input and output contract

The API exposes `project(observation, raw_bytes, policy)` and
`project_many(observations, raw_bytes, policy)`. The latter hashes and decodes the
raw source once for all observations. It retains input ordering and does not
merge colliding observation or actor identifiers across sources.

The supported source format is the explicitly named
`frozen_synthetic_openai_wrapper`: object `mapping`, object messages, list
`content.parts`, and recipe-selected metadata paths. The default policy comes
from the existing `actor_projection_v1/actor_projection_policy_v2.json`.
Other allowed part indices and metadata paths remain policy data. A different
declared source format fails explicitly; arbitrary OpenAI and Anthropic exports
are not implicitly covered by this wrapper contract.

Every bound projection requires:

- the observation's source locator equals the exact raw bytes' SHA-256;
- a valid JSON Pointer with a unique path through the source;
- the expected source container types and a recipe-supported content part;
- nonnegative integer UTF-8 byte offsets, within the source text;
- both span endpoints at UTF-8 character boundaries, including empty spans;
- an exact match between the decoded source span and observation text.

Actor label, source ID, source turn ID, source-known-at statement and transport
role stay separate. The actor remains `source_recorded`; a `user` transport role
does not establish an actor identity. A recipe cannot relabel this mechanism as
external identity verification; unsupported evidence semantics fail explicitly.
Source-known-at remains a literal statement
checked for agreement with the observation date and source epoch, never an
externally verified timestamp. Missing/null values do not gain fabricated dates.

Duplicate object members are retained as ambiguity during decoding. A duplicate
on the quote/message path returns `unavailable/ambiguous_source_binding`. A
duplicate on an actor or other metadata path returns that field as
`ambiguous_source_fields`, preserving independently bound fields and the quote.
An unrelated duplicate does not veto valid evidence. Invalid metadata container
types are `invalid_source_structure`; invalid Unicode metadata is
`invalid_unicode`. Original raw bytes remain authoritative and unmodified.

The CLI accepts a JSON observation array or a native snapshot's
`bodies.loom_kb_observations`. Its output includes the raw-source and exact policy
file SHA-256, every projection and the observation denominator. `--output`
creates a new file and refuses to overwrite an existing result.

```sh
python3 -m loom.tools.structure.retrieval_exploration_v1.source_projection \
  --raw-source source.json \
  --observations native_snapshot.json \
  --policy recipe.json \
  --output projected.json
```

The policy flag is optional. This command needs no experiment directory,
restored receipts, compiled native binary, model weights, credentials or network.

## Verification

```sh
python3 -m unittest loom.tools.structure.retrieval_exploration_v1.test_source_projection -v
```

Final result: **56/56 tests passed**, comprising the existing 27 v2 contract tests run
against the active adapter, 26 additional boundary/batch tests, two read-only
receipt parity tests, and one subprocess CLI test.
The combined active, historical actor v1/v2, retrieval, byte-budget and template
renderer suites pass **145/145 tests**. Read-only archive verification also passes
for all **18 actor payloads** and **342 native payloads**; no restore is required.

The initial 52-test pass was independently repeated before adversarial review
found three uncovered cases: an empty span inside a multibyte character, a
5000-digit configured array index, and a recipe claiming externally verified
identity. The final four regressions cover those corrections and valid empty
spans. This preserves the initial test result without implying it covered those
later discoveries.

- All **84/84** historical primary native projection objects equal the saved
  v2 objects; source bytes are read directly from the canonical ZIP and checked
  against the saved run ledger hashes.
- All **12/12** saved v2 control projection objects remain equal.
- New checks exercise invalid/valid escaped pointers, duplicate ancestor/text/
  actor/epoch fields, escaped duplicate names, unrelated duplicates, malformed
  containers, non-JSON constants, Unicode surrogates, booleans as offsets or
  timestamps, huge timestamps, custom policy paths, and per-observation hashes.
- The subprocess test invokes the actual module CLI on a native snapshot,
  verifies actor output, preserves both input files, and confirms that a second
  invocation cannot overwrite the first result.

These are mechanism and backward-compatibility checks on already exposed DEV
material plus newly authored toy cases. They do not estimate generalization,
real-world actor identification or semantic edge accuracy. No sealed validation,
holdout, live model call or new paid experiment was used.
